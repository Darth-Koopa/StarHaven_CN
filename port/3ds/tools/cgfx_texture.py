#!/usr/bin/env python3
"""List, dump and replace the textures of a CGFX model (the 3D HOME-menu banner,
cia/banner.cgfx), and place its root bone. Replacing keeps the texture's size and
format and leaves every other byte alone, so the model and its animations are untouched.

usage: cgfx_texture.py list <model.cgfx>
       cgfx_texture.py dump <model.cgfx> <outdir>
       cgfx_texture.py replace <model.cgfx> <texture name> <image> <out.cgfx>
       cgfx_texture.py root <model.cgfx> <scale> <x> <y> <z> <depth scale> <out.cgfx>

Layout (image TXOB, offsets from the object): +0x1C width, +0x34 PICA format,
+0x44 data size, +0x48 self-relative pointer to the pixels. Pixels are 8x8
Morton-ordered tiles, rows stored bottom-up. Names are not changed in place:
CGFX dictionaries are Patricia trees keyed on the names' bits.

Bone (offsets from the bone): +0 self-relative name pointer, +4 flags, +8 index,
+0xC parent index (-1 for the root), +0x20 scale, +0x2C rotation, +0x38 translation
(vec3 each), then the local, world and inverse-world 3x4 matrices (row-major) at
+0x44, +0x74 and +0xA4. bannertool's flat-banner template quad spans x -13..13 and
y -7.5..5.5, but on a console the bottom of that is cut off: keep a model above y = -3.
"""
import struct
import sys

from PIL import Image

FORMATS = {0: "RGBA8", 1: "RGB8", 2: "RGBA5551", 3: "RGB565", 4: "RGBA4", 5: "LA8",
           7: "L8", 8: "A8", 9: "LA4", 12: "ETC1", 13: "ETC1A4"}
BPP = {0: 32, 1: 24, 2: 16, 3: 16, 4: 16, 5: 16, 7: 8, 8: 8, 9: 8}


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def rel(d, o):
    v = u32(d, o)
    return o + v if v else 0


def textures(d):
    """the image textures: name, width, height (stored rows), PICA format, pixel offset"""
    assert d[:4] == b"CGFX", "not a CGFX file"
    data = struct.unpack_from("<H", d, 6)[0]
    assert d[data:data + 4] == b"DATA"
    dic = rel(d, data + 0x14)   # dictionary 1: textures
    out = []
    if not dic:
        return out
    for i in range(u32(d, dic + 8)):
        e = dic + 0x1C + i * 0x10
        name_o, obj = rel(d, e + 8), rel(d, e + 12)
        assert d[obj + 4:obj + 8] == b"TXOB"
        w, fmt, size, px = u32(d, obj + 0x1C), u32(d, obj + 0x34), u32(d, obj + 0x44), rel(d, obj + 0x48)
        h = size * 8 // BPP[fmt] // w if fmt in BPP else u32(d, obj + 0x18)
        out.append(dict(name=d[name_o:d.index(b"\0", name_o)].decode(), w=w, h=h, fmt=fmt, px=px))
    return out


def morton(x, y):
    m = 0
    for b in range(3):
        m |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1)
    return m


def texel_offsets(t):
    """(byte offset, x, y) of every texel, y counted from the top of the image"""
    bpp = BPP[t["fmt"]]
    for ty in range(0, t["h"], 8):
        for tx in range(0, t["w"], 8):
            tile = t["px"] + ((ty // 8) * (t["w"] // 8) + tx // 8) * 64 * bpp // 8
            for y in range(8):
                for x in range(8):
                    yield tile + morton(x, y) * bpp // 8, tx + x, t["h"] - 1 - (ty + y)


def decode(d, t):
    """RGBA image of an RGBA8 / RGBA4 / RGBA5551 / RGB565 texture"""
    im = Image.new("RGBA", (t["w"], t["h"]))
    px = im.load()
    for o, x, y in texel_offsets(t):
        if t["fmt"] == 0:
            a, b, g, r = d[o:o + 4]
        elif t["fmt"] == 4:
            v = struct.unpack_from("<H", d, o)[0]
            r, g, b, a = (v >> 12) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17
        elif t["fmt"] == 2:
            v = struct.unpack_from("<H", d, o)[0]
            r, g, b, a = (v >> 11) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31, 255 * (v & 1)
        elif t["fmt"] == 3:
            v = struct.unpack_from("<H", d, o)[0]
            r, g, b, a = (v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255
        else:
            raise ValueError(f"{t['name']}: format {FORMATS.get(t['fmt'], t['fmt'])} not supported")
        px[x, y] = (r, g, b, a)
    return im


def replace(d, name, image):
    """d (bytearray) with texture `name` (an RGBA4 one) redrawn from `image`, scaled to its size"""
    found = [t for t in textures(bytes(d)) if t["name"] in (name, name + ".png")]
    assert found, f"no texture {name!r}"
    t = found[0]
    assert t["fmt"] == 4, f"{name}: only RGBA4 textures can be replaced"
    px = image.convert("RGBA").resize((t["w"], t["h"]), Image.LANCZOS).load()
    for o, x, y in texel_offsets(t):
        r, g, b, a = px[x, y]
        struct.pack_into("<H", d, o, (r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | a >> 4)
    return d


def root_bone(d):
    """offset of the skeleton's root bone (parent index -1)"""
    for o in range(0, len(d) - 0x10, 4):
        name = rel(d, o)
        if (0 < name < len(d) and u32(d, o + 8) == 0 and struct.unpack_from("<i", d, o + 12)[0] == -1
                and d[name:name + 11] == b"Scene root\0"):
            return o
    raise ValueError("no root bone named 'Scene root'")


def place_root(d, scale, x, y, z, depth_scale=None):
    """d (bytearray) with the root bone scaled and moved, so every bone under it follows;
    depth_scale (default: scale) is the scale along z, the way the banner faces"""
    sz = scale if depth_scale is None else depth_scale
    o = root_bone(bytes(d))
    # local + world matrix calculated, rendered (+ uniform scale when it is)
    struct.pack_into("<I", d, o + 4, 0x1D0 if sz == scale else 0x1C0)
    struct.pack_into("<9f", d, o + 0x20, scale, scale, sz, 0, 0, 0, x, y, z)
    m = (scale, 0, 0, x, 0, scale, 0, y, 0, 0, sz, z)
    inv = (1 / scale, 0, 0, -x / scale, 0, 1 / scale, 0, -y / scale, 0, 0, 1 / sz, -z / sz)
    struct.pack_into("<12f", d, o + 0x44, *m)
    struct.pack_into("<12f", d, o + 0x74, *m)
    struct.pack_into("<12f", d, o + 0xA4, *inv)
    return d


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        for t in textures(open(argv[1], "rb").read()):
            print(t["name"], f"{t['w']}x{t['h']}", FORMATS.get(t["fmt"], t["fmt"]))
    elif len(argv) == 3 and argv[0] == "dump":
        d = open(argv[1], "rb").read()
        for t in textures(d):
            if not t["name"].endswith(".png"):
                decode(d, t).save(f"{argv[2]}/{t['name']}.png", format="PNG")
    elif len(argv) == 5 and argv[0] == "replace":
        d = replace(bytearray(open(argv[1], "rb").read()), argv[2], Image.open(argv[3]))
        open(argv[4], "wb").write(d)
    elif len(argv) == 8 and argv[0] == "root":
        d = place_root(bytearray(open(argv[1], "rb").read()), *map(float, argv[2:7]))
        open(argv[7], "wb").write(d)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
