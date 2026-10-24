#!/usr/bin/env python3
"""Validate the Chinese UI CSV and emit a C lookup table of iQue glyph tokens.

The CSV remains readable UTF-8. Every non-ASCII code point is converted through
CHARSET_IQUE from tools/splat_ext/pm_msg.py to U+E000 + native glyph index,
which is the token consumed by the 3DS UI font renderer. No ROM data is read.
"""
from __future__ import annotations

import argparse
import ast
import collections
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CSV = ROOT / "port/3ds/localization/ui_translations_zh_CN.csv"
DEFAULT_OUT = ROOT / "port/3ds/gen/ui_translations_zh_CN.h"
CHARSET_FILE = ROOT / "tools/splat_ext/pm_msg.py"
ENUMS_FILE = ROOT / "include/enums.h"
SOURCE_ROOT = ROOT / "port/3ds/source"
GLYPH_DATA_BYTES = 0x4F000
GLYPH_BYTES = 0x80
PRIVATE_BASE = 0xE000
PLACEHOLDER_RE = re.compile(r"%(?:[-+#0 ]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|t|L)?[diouxXfFeEgGaAcspn%])")


def enum_value(name: str) -> int:
    source = ENUMS_FILE.read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*=\s*(0x[0-9A-Fa-f]+|\d+)", source)
    if not match:
        raise ValueError(f"cannot find {name} in {ENUMS_FILE}")
    return int(match.group(1), 0)


def load_ique_character_map() -> dict[str, int]:
    tree = ast.parse(CHARSET_FILE.read_text(encoding="utf-8"), filename=str(CHARSET_FILE))
    node = next((n.value for n in tree.body if isinstance(n, ast.Assign) and
                 any(isinstance(t, ast.Name) and t.id == "CHARSET_IQUE" for t in n.targets)), None)
    if not isinstance(node, ast.Dict):
        raise ValueError(f"CHARSET_IQUE literal not found in {CHARSET_FILE}")
    lead_first = enum_value("MSG_CHAR_MULTIBYTE_FIRST")
    lead_last = enum_value("MSG_CHAR_MULTIBYTE_LAST")
    glyph_start = enum_value("MSG_CHAR_ZH_START")
    span = lead_last - lead_first + 1
    limit = GLYPH_DATA_BYTES // GLYPH_BYTES
    result: dict[str, int] = {}
    collisions: dict[str, set[int]] = collections.defaultdict(set)
    for lead_node, trails_node in zip(node.keys, node.values):
        if not (isinstance(lead_node, ast.Constant) and isinstance(lead_node.value, int)
                and lead_first <= lead_node.value <= lead_last and isinstance(trails_node, ast.Dict)):
            continue
        lead = lead_node.value
        for trail_node, char_node in zip(trails_node.keys, trails_node.values):
            if not (isinstance(trail_node, ast.Constant) and isinstance(trail_node.value, int)
                    and isinstance(char_node, ast.Constant) and isinstance(char_node.value, str)
                    and len(char_node.value) == 1 and trail_node.value >= 1):
                continue
            glyph = (glyph_start - lead_first) + lead + (trail_node.value - 1) * span
            if not 0 <= glyph < limit:
                raise ValueError(f"CHARSET_IQUE maps {char_node.value!r} outside the font ({glyph})")
            collisions[char_node.value].add(glyph)
            result.setdefault(char_node.value, glyph)
    bad = {c: values for c, values in collisions.items() if len(values) > 1}
    if bad:
        detail = ", ".join(f"{c!r}: {sorted(v)}" for c, v in sorted(bad.items()))
        raise ValueError(f"ambiguous CHARSET_IQUE character mappings: {detail}")
    return result


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        required = {"category", "source_file", "english_source", "proposed_simplified_chinese", "review_notes"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError(f"CSV missing columns: {', '.join(sorted(required - set(reader.fieldnames or [])))}")
        rows = list(reader)
    for n, row in enumerate(rows, 2):
        if not row["english_source"]:
            raise ValueError(f"CSV line {n}: empty english_source")
        if row["proposed_simplified_chinese"] is None or row["proposed_simplified_chinese"] == "":
            raise ValueError(f"CSV line {n}: empty Chinese translation")
        src_specs = collections.Counter(PLACEHOLDER_RE.findall(row["english_source"]))
        dst_specs = collections.Counter(PLACEHOLDER_RE.findall(row["proposed_simplified_chinese"]))
        if any(count > src_specs[spec] for spec, count in dst_specs.items()):
            raise ValueError(f"CSV line {n}: translation introduces an unknown format placeholder: {row['english_source']!r} -> {row['proposed_simplified_chinese']!r}")
    return rows


def validate_source_rows(rows: list[dict[str, str]]) -> list[str]:
    cache: dict[str, str] = {}
    problems = []
    for line_no, row in enumerate(rows, 2):
        name = row["source_file"]
        if name not in cache:
            files = list(SOURCE_ROOT.rglob(name))
            if not files:
                problems.append(f"CSV line {line_no}: source file {name!r} not found")
                cache[name] = ""
                continue
            # Source files are expected to be unique by basename in this project.
            cache[name] = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in files)
        if row["english_source"] not in cache[name]:
            problems.append(f"CSV line {line_no}: source text not found in {name}: {row['english_source']!r}")
    return problems


def c_string(s: str, glyph_map: dict[str, int]) -> str:
    out = []
    for ch in s:
        if ord(ch) <= 0x7F:
            if ch == '"': out.append('\\"')
            elif ch == "\\": out.append('\\\\')
            elif ch == "\n": out.append('\\n')
            elif ch == "\r": out.append('\\r')
            elif ch == "\t": out.append('\\t')
            elif ord(ch) < 0x20 or ord(ch) == 0x7F: out.append(f"\\{ord(ch):03o}")
            else: out.append(ch)
        else:
            glyph = glyph_map.get(ch)
            if glyph is None:
                raise ValueError(f"missing iQue glyph mapping for {ch!r} (U+{ord(ch):04X})")
            cp = PRIVATE_BASE + glyph
            encoded = chr(cp).encode("utf-8")
            out.extend(f"\\{b:03o}" for b in encoded)
    return '"' + ''.join(out) + '"'


def generate(rows: list[dict[str, str]], glyph_map: dict[str, int], csv_path: Path) -> tuple[str, list[str]]:
    # Keep first row for duplicate source keys: this is intentionally contextual
    # in the future, but preserves the user's current Start entry for this pass.
    mapped: dict[str, str] = {}
    conflicts: dict[str, set[str]] = collections.defaultdict(set)
    for row in rows:
        src = row["english_source"]
        dst = row["proposed_simplified_chinese"]
        conflicts[src].add(dst)
        mapped.setdefault(src, dst)
    warnings = [f"duplicate key {src!r} has translations {sorted(targets)!r}; using first CSV row"
                for src, targets in sorted(conflicts.items()) if len(targets) > 1]
    lines = [
        "/* Generated from the UTF-8 UI localization CSV by generate_bottom_screen_translations.py. */",
        "#ifndef PM3DS_UI_TRANSLATIONS_ZH_CN_H",
        "#define PM3DS_UI_TRANSLATIONS_ZH_CN_H",
        "#include <stddef.h>",
        "#include <string.h>",
        "typedef struct { const char *source; const char *target; } pm3ds_ui_translation_entry_t;",
        "static const pm3ds_ui_translation_entry_t s_pm3ds_ui_translations_zh_cn[] = {",
    ]
    for src in sorted(mapped):
        lines.append(f"    {{{c_string(src, glyph_map)}, {c_string(mapped[src], glyph_map)}}},")
    lines += [
        "};",
        "static const char *pm3ds_ui_translate(const char *source) {",
        "    if (source == NULL) return NULL;",
        "    size_t lo = 0, hi = sizeof(s_pm3ds_ui_translations_zh_cn) / sizeof(s_pm3ds_ui_translations_zh_cn[0]);",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        int cmp = strcmp(source, s_pm3ds_ui_translations_zh_cn[mid].source);",
        "        if (cmp == 0) return s_pm3ds_ui_translations_zh_cn[mid].target;",
        "        if (cmp < 0) hi = mid; else lo = mid + 1;",
        "    }",
        "    return source;",
        "}",
        "#endif /* PM3DS_UI_TRANSLATIONS_ZH_CN_H */",
        "",
    ]
    return "\n".join(lines), warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--strict", action="store_true", help="fail on missing glyphs or invalid source references")
    args = parser.parse_args()
    rows = read_rows(args.csv)
    glyph_map = load_ique_character_map()
    issues = validate_source_rows(rows)
    missing = sorted({ch for row in rows for ch in row["proposed_simplified_chinese"] if ord(ch) > 127 and ch not in glyph_map})
    if missing:
        issues.extend(f"missing CHARSET_IQUE mapping: {ch!r} U+{ord(ch):04X}" for ch in missing)
    if issues:
        print("Translation validation failed:", file=sys.stderr)
        for issue in issues: print(f"- {issue}", file=sys.stderr)
        return 1
    header, warnings = generate(rows, glyph_map, args.csv.resolve())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(header, encoding="utf-8")
    print(f"Validated {len(rows)} translations / {len(set(r['english_source'] for r in rows))} source keys; "
          f"{sum(1 for row in rows for ch in row['proposed_simplified_chinese'] if ord(ch) > 127)} non-ASCII character occurrences; "
          f"generated {args.out}.")
    for warning in warnings: print(f"WARNING: {warning}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
