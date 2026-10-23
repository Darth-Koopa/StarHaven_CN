#!/usr/bin/env python3
"""Check that version-generated decomp headers are available to the 3DS build.

This runs after configure + Ninja code generation and before cross-compilation.
It catches absent generated headers at the asset/codegen stage rather than
waiting for a long C compilation to fail on its first translation unit.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
VERSION = sys.argv[1] if len(sys.argv) > 1 else "ique"
if VERSION not in {"us", "ique"}:
    raise SystemExit(f"unsupported version: {VERSION!r}")

SOURCE = ROOT / "ver" / VERSION / "build" / "include"
DEST = ROOT / "port" / "3ds" / "gen" / VERSION

# These are referenced by the compiled decomp/port C sources or are required
# generated enum/data pairs from configure's rules. Keep this list explicit so
# a missing Ninja output fails with a useful message in the generation step.
REQUIRED = (
    "message_ids.h",
    "ld_addrs.h",
    "item_data.inc.c",
    "item_enum.h",
    "move_data.inc.c",
    "move_enum.h",
    "recipes.inc.c",
    "world_map.inc.c",
    "icon_offsets.h",
    "sprite/sprite_shading_profiles.h",
    "sprite/player.h",
    "sprite/npc/Toad.h",
    "battle/actor_types.h",
    "battle/actor_types.inc.c",
    "pause/world_map.png.h",
)

missing = []
for rel in REQUIRED:
    src = SOURCE / rel
    dst = DEST / rel
    if not src.is_file() or src.stat().st_size == 0:
        missing.append(f"Ninja did not generate {src.relative_to(ROOT)}")
    if not dst.is_file() or dst.stat().st_size == 0:
        missing.append(f"3DS generated include missing/empty: {dst.relative_to(ROOT)}")

# Check every generated header or C include output is mirrored. This protects
# against the same class of error for a future segment-specific header that is
# not yet listed in REQUIRED.
if SOURCE.is_dir():
    for src in SOURCE.rglob("*"):
        if not src.is_file():
            continue
        if src.suffix not in {".h", ".c"}:
            continue
        rel = src.relative_to(SOURCE)
        dst = DEST / rel
        if not dst.is_file():
            missing.append(f"generated include output was not copied: {rel.as_posix()}")
        elif rel.as_posix() == "ld_addrs.h":
            # The build intentionally replaces this one generated header with
            # a forwarding adapter so the port-specific ROM address overrides
            # cannot be shadowed by the version include directory.
            text = dst.read_text(errors="replace")
            if '#include "../../include/ld_addrs.h"' not in text:
                missing.append("generated ld_addrs.h does not forward through the 3DS address override wrapper")

if missing:
    print("Generated-header preflight failed:", file=sys.stderr)
    for item in sorted(set(missing)):
        print(f"  - {item}", file=sys.stderr)
    raise SystemExit(1)

print(
    f"generated-header preflight passed for {VERSION}: "
    f"required includes present; generated include outputs mirrored (ld_addrs.h uses the intentional 3DS adapter)"
)
