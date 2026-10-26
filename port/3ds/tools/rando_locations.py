#!/usr/bin/env python3
"""List the randomizer's item locations from the decomp's map scripts.

A location is an item the game spawns with a save flag that marks it taken,
so the flag identifies it for good (no spawn order involved):
  item   Call(MakeItemEntity, ITEM_X, x, y, z, mode, GF_Y)
  block  Call(MakeEntity, Ref(Entity_...Block), ..., ITEM_X, MAKE_ENTITY_END)
         followed by Call(AssignBlockFlag, GF_Y); an invisible one (Entity_Hidden...)
         is a hidden block
  tree / bush   FoliageDropList entries: .itemID = ITEM_X ... .pickupFlag = GF_Y
  panel  Call(AssignPanelFlag, GF_Y): a hidden panel's star piece
  chest  EVT_OPEN_CHEST_CONSUMABLE/KEY/BADGE(ITEM_X, GF_Y)
  gift   EVT_GIVE_KEY/BADGE/CONSUMABLE_REWARD(ITEM_X): an NPC's gift, which
         has no flag of its own (flag 0); the map and item name it

Writes port/3ds/source/n64_shim/rando_locations.inc (_us.inc / _jp.inc for us / jp), one
  LOC(GF_Y, ITEM_X, SRC_..., "map", kav, chapter)
per location; the C side resolves the names (n64_shim/rando.c). kav is the
first chapter whose key items may be placed there (rando_logic.py; 99 never),
chapter the one whose story opens the location's area (99: none, see
rando_logic.AREA_CHAPTER) -- a key item found there is needed no earlier;
computed when PMR_DIR points at a PMR-SeedGenerator checkout, else 99. Only
the map directories the port compiles for the version are scanned
(world_files.mk / world_files_us.mk), so both builds list what they have.

Shop slots go to rando_shops*.inc, one
  SHOP(ITEM_X, price, MSG_desc, "map", rowf)
per ShopItemData entry of a coin shop (rowf: Rowf's one-time badge stock);
Merlow's star-piece shop (hos_06) stays out.

usage: [PMR_DIR=...] rando_locations.py [pal|us]
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PORT = os.path.join(ROOT, "port", "3ds")

MAKE_ITEM = re.compile(r"Call\(MakeItemEntity,\s*(ITEM_\w+),[^)]*?,\s*(GF_\w+)\s*\)")
MAKE_BLOCK = re.compile(r"Call\(MakeEntity,\s*Ref\(Entity_(\w*Block\w*)\)[^\n]*?,\s*(ITEM_\w+),\s*MAKE_ENTITY_END\)\s*\n"
                        r"\s*Call\(AssignBlockFlag,\s*(GF_\w+)\)")
PANEL = re.compile(r"Call\(AssignPanelFlag,\s*(GF_\w+)\)")
CHEST = re.compile(r"EVT_OPEN_CHEST_(?:CONSUMABLE|KEY|BADGE)\((ITEM_\w+),\s*(GF_\w+)\)")
GIFT = re.compile(r"EVT_GIVE_(?:KEY|BADGE|CONSUMABLE)_REWARD(?:_ALT)?\((ITEM_\w+)")
DROP = re.compile(r"\.itemID\s*=\s*(ITEM_\w+),.*?\.pickupFlag\s*=\s*(GF_\w+)", re.S)
SHOP_ARRAY = re.compile(r"ShopItemData\s+N\((\w+)\)\[\]\s*=\s*\{(.*?)\n\};", re.S)
SHOP_ENTRY = re.compile(r"\.itemID\s*=\s*(ITEM_\w+),\s*\.price\s*=\s*(\d+),\s*(?:\.descMsg\s*=\s*)?(MSG_\w+)")
DROP_LIST = re.compile(r"FoliageDropList\s+N\((\w+)\)\s*=\s*\{(.*?)\n\};", re.S)


def map_dirs(version):
    mk = os.path.join(PORT, "world_files.mk" if version == "pal" else "world_files_%s.mk" % version)
    dirs = set()
    for m in re.finditer(r"(?:^|\s)world/(area_\w+/\w+)/", open(mk).read()):
        dirs.add(m.group(1))
    return sorted(dirs)


def scan(version):
    found = {}   # flag -> (item, src, map); first one wins (a flag shared by maps is one location)
    for d in map_dirs(version):
        mapname = d.split("/")[1]
        for dp, _, files in os.walk(os.path.join(ROOT, "src", "world", d)):
            for f in sorted(files):
                if not f.endswith(".c"):
                    continue
                text = open(os.path.join(dp, f), errors="replace").read()
                hits = []
                for m in MAKE_ITEM.finditer(text):
                    hits.append((m.group(2), m.group(1), "SRC_ITEM"))
                for m in MAKE_BLOCK.finditer(text):
                    hits.append((m.group(3), m.group(2), "SRC_HIDDEN_BLOCK" if m.group(1).startswith("Hidden")
                                 else "SRC_BLOCK"))
                for m in PANEL.finditer(text):
                    hits.append((m.group(1), "ITEM_STAR_PIECE", "SRC_PANEL"))
                for m in CHEST.finditer(text):
                    hits.append((m.group(2), m.group(1), "SRC_CHEST"))
                for m in GIFT.finditer(text):
                    hits.append(("gift:%s:%s" % (mapname, m.group(1)), m.group(1), "SRC_GIFT"))
                for lm in DROP_LIST.finditer(text):
                    src = "SRC_TREE" if "Tree" in lm.group(1) else "SRC_BUSH"
                    for m in DROP.finditer(lm.group(2)):
                        hits.append((m.group(2), m.group(1), src))
                for flag, item, src in hits:
                    found.setdefault(flag, (item, src, mapname))
    return found


def scan_shops(version):
    slots = []
    for d in map_dirs(version):
        mapname = d.split("/")[1]
        if mapname == "hos_06":
            continue
        for dp, _, files in os.walk(os.path.join(ROOT, "src", "world", d)):
            for f in sorted(files):
                if not f.endswith(".c"):
                    continue
                text = open(os.path.join(dp, f), errors="replace").read()
                for am in SHOP_ARRAY.finditer(text):
                    for m in SHOP_ENTRY.finditer(am.group(2)):
                        slots.append((m.group(1), m.group(2), m.group(3), mapname, int("Rowf" in am.group(1))))
    return slots


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "pal"
    found = scan(version)
    kav, unmatched = {}, 0
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rando_logic
    if os.environ.get("PMR_DIR"):
        kav, unmatched = rando_logic.key_availability(
            os.environ["PMR_DIR"], [(f, i, s, m) for f, (i, s, m) in found.items()])
    out = os.path.join(PORT, "source", "n64_shim", "rando_locations%s.inc" % ("" if version == "pal" else "_" + version))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as fh:
        fh.write("/* GENERATED by tools/rando_locations.py %s -- do not edit */\n" % version)
        for flag in sorted(found, key=lambda k: (found[k][2], k)):
            item, src, mapname = found[flag]
            chapter = rando_logic.AREA_CHAPTER.get(mapname.split("_")[0])
            fh.write('LOC(%s, %s, %s, "%s", %d, %d)\n' % ("0" if flag.startswith("gift:") else flag, item, src, mapname,
                                                        kav.get(flag, 99), 99 if chapter is None else chapter))
    shops = scan_shops(version)
    shops_out = os.path.join(PORT, "source", "n64_shim", "rando_shops%s.inc" % ("" if version == "pal" else "_" + version))
    with open(shops_out, "w") as fh:
        fh.write("/* GENERATED by tools/rando_locations.py %s -- do not edit */\n" % version)
        for item, price, desc, mapname, rowf in shops:
            fh.write('SHOP(%s, %s, %s, "%s", %d)\n' % (item, price, desc, mapname, rowf))
    print("%d shop slots (%d Rowf's)" % (len(shops), sum(r for *_, r in shops)))
    by_src = {}
    for item, src, _ in found.values():
        by_src[src] = by_src.get(src, 0) + 1
    print("%d locations in %s: %s" % (len(found), os.path.relpath(out, ROOT),
                                       ", ".join("%s %d" % kv for kv in sorted(by_src.items()))))
    if kav:
        per = {}
        for k in kav.values():
            per[k] = per.get(k, 0) + 1
        print("key items allowed from chapter:", ", ".join("%s: %d" % kv for kv in sorted(per.items())),
              "| not matched to PMR:", unmatched)


if __name__ == "__main__":
    main()
