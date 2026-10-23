#!/usr/bin/env python3
"""List, dump and replace the textures of a CGFX model (the 3D HOME-menu banner,
cia/banner.cgfx). Replacing keeps the texture's size and format and leaves every
other byte alone, so the model and its animations are untouched.

usage: cgfx_texture.py list <model.cgfx>
       cgfx_texture.py dump <model.cgfx> <outdir>
       cgfx_texture.py replace <model.cgfx> <texture name> <image> <out.cgfx>

Layout (image TXOB, offsets from the object): +0x1C width, +0x34 PICA format,
+0x44 data size, +0x48 self-relative pointer to the pixels. Pixels are 8x8
Morton-ordered tiles, rows stored bottom-up. Names are not changed in place:
CGFX dictionaries are Patricia trees keyed on the names' bits.
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
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
