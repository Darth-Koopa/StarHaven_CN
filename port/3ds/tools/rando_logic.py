#!/usr/bin/env python3
"""Where the randomizer may put key items, from the PMR seed generator's world graph.

Star Haven's randomizer keeps the normal story, so a key item is placed only
where a normal-story player can pick it up before the chapter that needs it.
For each location this works out `kav`: the first chapter N whose key items
may go there. That needs
  * the location's area to belong to a chapter before N (its story is done),
  * the location reachable in the PMR world graph with what a vanilla player
    owns at the start of chapter N: boots, hammer, partners, star spirits and
    cleared chapters -- and no key items, since those are shuffled.
Events the graph models as pseudo-items (bridges built, switches pulled) come
from walking it. Locations that never qualify get kav 99.

The graph is the Paper Mario 64 Randomizer's (PMR-SeedGenerator, MIT licence,
maps/graph_edges/base_graph); it is read at build time from PMR_DIR and is not
shipped. A location is matched to PMR's item nodes by map, kind and vanilla
item; with several candidates all of them must be reachable.
"""
import glob
import importlib.util
import os
import re
import sqlite3

# the chapter whose story opens each area; None: never holds key items
# (Peach's castle and the whale are only visited during their chapter's story,
# the playroom and test maps are not part of it)
AREA_CHAPTER = {
    "kmr": 0, "mac": 0, "tik": 0, "osr": 0, "hos": 0,
    "nok": 1, "trd": 1,
    "iwa": 2, "dro": 2, "sbk": 2, "isk": 2,
    "mim": 3, "obk": 3, "arn": 3, "dgb": 3,
    "omo": 4,
    "jan": 5, "kzn": 5,
    "flo": 6,
    "sam": 7, "pra": 7,
    "kpa": 8,
}

# what a vanilla player has at the start of chapter N (gained during chapter N-1)
GAINED_IN = [
    ["Boots", "Hammer", "Goombario"],                      # prologue
    ["Kooper", "Bombette", "Eldstar"],                     # 1
    ["Parakarry", "SuperHammer", "Mamar"],                 # 2
    ["Bow", "SuperBoots", "Skolar", "RF_OpenedGustyGulch"],  # 3
    ["Watt", "Muskular", "RF_ToyboxOpen"],                 # 4
    ["Sushie", "UltraHammer", "Misstar", "RF_CanRideWhale"],  # 5
    ["Lakilester", "Klevar"],                              # 6
    ["Kalmar"],                                            # 7
]
PARTNERS = ["Goombario", "Kooper", "Bombette", "Parakarry", "Bow", "Watt", "Sushie", "Lakilester"]
SPIRITS = ["Eldstar", "Mamar", "Skolar", "Muskular", "Misstar", "Klevar", "Kalmar"]
BOOTS = ["Boots", "SuperBoots", "UltraBoots"]
HAMMERS = ["Hammer", "SuperHammer", "UltraHammer"]

# my source kinds -> PMR node name prefixes
KINDS = {
    "SRC_ITEM": ("Item",),
    "SRC_BLOCK": ("YBlock", "HiddenYBlock", "RBlock", "HiddenRBlock", "RandomBlockItem"),
    "SRC_HIDDEN_BLOCK": ("HiddenYBlock", "HiddenRBlock"),
    "SRC_TREE": ("Tree",),
    "SRC_BUSH": ("Bush",),
    "SRC_PANEL": ("HiddenPanel",),
    "SRC_CHEST": ("Chest",),
    "SRC_GIFT": ("Gift", "KootGift"),
}


def norm(name):
    """ITEM_FIRE_FLOWER / FireFlower -> fireflower; star pieces lose their PMR number"""
    n = re.sub(r"[^a-z0-9]", "", name.lower().replace("item_", "", 1))
    if n.startswith("starpiece"):
        return "starpiece"
    return n


class Inventory:
    def __init__(self, chapter):
        self.items = set()
        for gained in GAINED_IN[:chapter]:
            self.items.update(gained)
        # normal-story facts the open-world graph leaves to its settings:
        # Kent C. Koopa only blocks Pleasant Path from chapter 6 on (and then
        # for a fee), the Flower Fields door opens with chapter 6's seeds
        if chapter <= 5:
            self.items.add("RF_CanPassKentC")
        if chapter >= 7:
            self.items.update(["RF_MagicalSeed1", "RF_MagicalSeed2", "RF_MagicalSeed3", "RF_MagicalSeed4"])
        self.clears = max(chapter - 1, 0)
        self.chapter = chapter

    def boots(self):
        return max([i for i, b in enumerate(BOOTS) if b in self.items], default=-1)

    def hammer(self):
        return max([i for i, h in enumerate(HAMMERS) if h in self.items], default=-1)

    def has(self, req):
        if isinstance(req, dict):
            if "chapterclears" in req:
                return self.clears >= req["chapterclears"]
            if "starspirits" in req:
                return sum(s in self.items for s in SPIRITS) >= req["starspirits"]
            return False   # star pieces, power stars, key counts: not assumed
        p = self.items
        if req == "can_climb_steps":
            return self.boots() >= 0 or "Parakarry" in p
        if req == "can_flip_panels":
            return self.hammer() >= 2 or self.boots() >= 1
        if req == "can_shake_trees":
            return self.hammer() >= 0 or "Bombette" in p
        if req == "can_hit_grounded_blocks":
            return self.hammer() >= 0 or self.boots() >= 1 or "Kooper" in p or "Bombette" in p
        if req == "can_hit_floating_blocks":
            return self.boots() >= 0 or "Kooper" in p
        if req == "can_see_hidden_blocks":
            return "Watt" in p
        if req in BOOTS:
            return self.boots() >= BOOTS.index(req)
        if req in HAMMERS:
            return self.hammer() >= HAMMERS.index(req)
        return req in p

    def fulfils(self, reqs):
        return all(any(self.has(r) for r in group) for group in reqs)

    def add(self, pseudo):
        m = re.match(r"CHAPTERCLEAR_(\d)", pseudo)
        if m:   # a later chapter's boss: not before its story
            return False
        if pseudo in SPIRITS:
            return False
        if pseudo in self.items:
            return False
        self.items.add(pseudo)
        return True


def load_edges(pmr):
    edges = []
    for f in sorted(glob.glob(os.path.join(pmr, "maps", "graph_edges", "base_graph", "*.py"))):
        spec = importlib.util.spec_from_file_location(os.path.basename(f)[:-3], f)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for k, v in vars(mod).items():
            if k.startswith("edges_"):
                edges += v
    return edges


def node(n):
    return (n["map"], str(n["id"]))


def reachable(edges, chapter):
    inv = Inventory(chapter)
    seen = {("KMR_00", "0"), ("KMR_02", "1")}
    if chapter >= 1:
        seen.add(("MAC_00", "0"))
    changed = True
    while changed:
        changed = False
        for e in edges:
            a, b = node(e["from"]), node(e["to"])
            if a not in seen or (b in seen and not e.get("pseudoitems")):
                continue
            if not inv.fulfils(e["reqs"]):
                continue
            if b not in seen:
                seen.add(b)
                changed = True
            for p in e.get("pseudoitems", []):
                if inv.add(p):
                    changed = True
    return seen


def pmr_nodes(pmr):
    """(map, kind prefix, normalised vanilla item) -> [PMR node key names]"""
    db = sqlite3.connect(os.path.join(pmr, "default_db.sqlite"))
    q = """select m.name, n.key_name_item, i.item_name from node n join maparea m on m.id = n.map_area_id
           left join item i on i.id = n.vanilla_item_id where n.key_name_item is not null"""
    out = {}
    for mapname, key, item in db.execute(q):
        if item is None:
            continue
        prefix = re.sub(r"[0-9]*[A-Z]?(_.*)?$", "", key)
        out.setdefault((mapname.lower(), prefix, norm(item)), []).append(key)
    return out


def key_availability(pmr, locations):
    """locations: [(flag, item, src, map)] -> {flag: kav}"""
    edges = load_edges(pmr)
    reach = [reachable(edges, n) for n in range(9)]
    nodes = pmr_nodes(pmr)
    kav = {}
    unmatched = 0
    for flag, item, src, mapname in locations:
        area = AREA_CHAPTER.get(mapname.split("_")[0])
        cands = []
        for prefix in KINDS.get(src, ()):
            cands += nodes.get((mapname, prefix, norm(item)), [])
        if area is None or not cands:
            unmatched += cands == []
            kav[flag] = 99
            continue
        kav[flag] = 99
        for n in range(area + 1, 9):
            if all((mapname.upper(), c) in reach[n] for c in cands):
                kav[flag] = n
                break
    return kav, unmatched
