#!/usr/bin/env python3
"""Audit bottom-screen Chinese strings against the iQue glyph map.

The Unicode -> native glyph mapping is sourced from tools/splat_ext/pm_msg.py's
CHARSET_IQUE table, and the formula is checked against include/enums.h. This is
an audit tool, not a runtime translator: do not emit C strings until all glyphs
and context-dependent translation keys have been reviewed.
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
CSV_DEFAULT = ROOT / "port/3ds/localization/ui_translations_zh_CN.csv"
CHARSET_FILE = ROOT / "tools/splat_ext/pm_msg.py"
ENUMS_FILE = ROOT / "include/enums.h"
GLYPH_DATA_BYTES = 0x4F000
GLYPH_BYTES = 0x80


def enum_value(name: str) -> int:
    source = ENUMS_FILE.read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*=\s*(0x[0-9A-Fa-f]+|\d+)", source)
    if not match:
        raise ValueError(f"cannot find {name} in {ENUMS_FILE}")
    return int(match.group(1), 0)


def load_ique_character_map() -> dict[str, list[tuple[int, int, int]]]:
    tree = ast.parse(CHARSET_FILE.read_text(encoding="utf-8"), filename=str(CHARSET_FILE))
    charset_node = None
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "CHARSET_IQUE"
            for target in node.targets
        ):
            charset_node = node.value
            break
    if not isinstance(charset_node, ast.Dict):
        raise ValueError(f"CHARSET_IQUE literal not found in {CHARSET_FILE}")

    lead_first = enum_value("MSG_CHAR_MULTIBYTE_FIRST")
    lead_last = enum_value("MSG_CHAR_MULTIBYTE_LAST")
    glyph_start = enum_value("MSG_CHAR_ZH_START")
    span = lead_last - lead_first + 1
    glyph_limit = GLYPH_DATA_BYTES // GLYPH_BYTES
    out: dict[str, list[tuple[int, int, int]]] = collections.defaultdict(list)
    for lead_node, trails_node in zip(charset_node.keys, charset_node.values):
        # CHARSET_IQUE starts with **CHARSET_COMMON. Chinese mappings are literal
        # {lead_byte: {trail_byte: one_character}} entries following that merge.
        if not (isinstance(lead_node, ast.Constant) and isinstance(lead_node.value, int)):
            continue
        lead = lead_node.value
        if not isinstance(trails_node, ast.Dict):
            continue
        if not lead_first <= lead <= lead_last:
            continue
        for trail_node, char_node in zip(trails_node.keys, trails_node.values):
            if not (
                isinstance(trail_node, ast.Constant)
                and isinstance(trail_node.value, int)
                and isinstance(char_node, ast.Constant)
                and isinstance(char_node.value, str)
                and len(char_node.value) == 1
            ):
                continue
            trail = trail_node.value
            if trail < 1:
                continue
            glyph = (glyph_start - lead_first) + lead + (trail - 1) * span
            if glyph < 0 or glyph >= glyph_limit:
                raise ValueError(
                    f"CHARSET_IQUE maps {char_node.value!r} to out-of-range glyph {glyph}"
                )
            out[char_node.value].append((lead, trail, glyph))
    return dict(out)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        needed = {"category", "source_file", "english_source", "proposed_simplified_chinese", "review_notes"}
        if not needed.issubset(reader.fieldnames or []):
            raise ValueError(f"CSV needs columns: {', '.join(sorted(needed))}")
        return list(reader)


def markdown_report(rows, charmap, csv_path) -> tuple[str, bool]:
    missing_occurrences: collections.Counter[str] = collections.Counter()
    missing_rows: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    source_map: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    total_non_ascii = 0
    for row in rows:
        source_map[row["english_source"]].append(row)
        target = row["proposed_simplified_chinese"] or ""
        for ch in target:
            if ord(ch) > 127:
                total_non_ascii += 1
                if ch not in charmap:
                    missing_occurrences[ch] += 1
                    missing_rows[ch].append(row)

    conflicts = {
        source: entries for source, entries in source_map.items()
        if len({entry["proposed_simplified_chinese"] for entry in entries}) > 1
    }
    duplicate_glyphs = {ch: entries for ch, entries in charmap.items() if len(entries) > 1}
    glyph_indices = {entry[2] for values in charmap.values() for entry in values}
    glyph_limit = GLYPH_DATA_BYTES // GLYPH_BYTES

    lines = [
        "# 下屏中文译文字库检查报告",
        "",
        f"- 翻译文件：`{csv_path.relative_to(ROOT) if csv_path.is_relative_to(ROOT) else csv_path}`",
        f"- 翻译条目：{len(rows)}",
        f"- CHARSET_IQUE 中可直接映射的汉字/符号：{len(charmap)} 个不同字符",
        f"- CHARSET_IQUE 映射覆盖的字形编号：{len(glyph_indices)} 个（范围 0x{min(glyph_indices):X}–0x{max(glyph_indices):X}；内嵌字库共 {glyph_limit} 个 16×16 字形）",
        f"- 译文中的非 ASCII 字符出现次数：{total_non_ascii}",
        f"- 字库中找不到映射的字符：{len(missing_occurrences)} 个",
        "",
    ]
    if missing_occurrences:
        lines += ["## 字库映射缺失（需要校对/改写）", "", "以下字符不能按现有 `CHARSET_IQUE` 转码。严格模式下构建必须失败，不能猜字形编号：", ""]
        for ch, count in sorted(missing_occurrences.items(), key=lambda item: (-item[1], ord(item[0]))):
            rows_for_char = missing_rows[ch]
            refs = "; ".join(
                f"{r['category']} / `{r['english_source']}` → `{r['proposed_simplified_chinese']}`"
                for r in rows_for_char[:5]
            )
            if len(rows_for_char) > 5:
                refs += f"；另有 {len(rows_for_char) - 5} 条"
            label = f"U+{ord(ch):04X}"
            lines.append(f"- `{ch}` ({label})：出现 {count} 次。{refs}")
        lines.append("")
    else:
        lines += ["## 字库映射缺失", "", "所有译文字符均可通过 `CHARSET_IQUE` 找到映射。", ""]

    if conflicts:
        lines += ["## 同一英文原文存在不同译文", "", "全局按原文查找会产生歧义；这些条目需要显式上下文键或在调用点指定分类：", ""]
        for source, entries in sorted(conflicts.items()):
            lines.append(f"- `{source}`：" + "；".join(
                f"{r['category']} → `{r['proposed_simplified_chinese']}` ({r['source_file']})"
                for r in entries
            ))
        lines.append("")
    if duplicate_glyphs:
        lines += ["## 字符映射冲突", ""]
        for ch, entries in duplicate_glyphs.items():
            lines.append(f"- `{ch}`：" + ", ".join(f"0x{lead:02X} 0x{trail:02X} → {glyph}" for lead, trail, glyph in entries))
        lines.append("")

    lines += [
        "## 转码规则",
        "",
        "1. 保留 CSV 作为可读、可校对的 UTF-8 翻译源。",
        "2. 构建时从 `tools/splat_ext/pm_msg.py` 解析 `CHARSET_IQUE`，并使用 `include/enums.h` 的原生编码常量计算字形编号。",
        "3. 所有目标字符通过严格字库检查后，才把中文转换为当前 3DS 字体渲染器识别的私用区字形标记（U+E000 + 字形编号）；ASCII 与 `%d`、`%s`、`%08X` 等占位符保持原样。",
        "4. 不把转换后的不可读字节硬编码到手写 C 源码；由 Python 生成头文件，使修改翻译后能在 CI 自动检查和重建。",
        "5. 主字号和 8×8 小字号绘制入口都识别私用区字形标记；小字号以 0.5 倍比例绘制游戏字库中的 16×16 汉字，并按字符单元换行。",
        "6. 含运行时参数的格式串由调用点先按翻译键查表，再传给 `snprintf`；不在最终绘制阶段猜测动态字符串的含义。",
        "",
        "## 重要提醒",
        "",
        "本工具只生成字库审计报告；实际 C 翻译表由 `tools/generate_bottom_screen_translations.py` 生成。正式构建必须运行该生成器的 `--strict` 检查。游戏字体尚未加载时（例如找不到 ROM 的早期错误画面）会回退英文，因为此时运行时没有可读取的 iQue 字库。",
    ]
    return "\n".join(lines) + "\n", bool(missing_occurrences or conflicts or duplicate_glyphs)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=CSV_DEFAULT)
    parser.add_argument("--report", type=Path, help="write a Markdown report")
    parser.add_argument("--strict", action="store_true", help="return non-zero if glyphs/keys need review")
    args = parser.parse_args()

    charmap = load_ique_character_map()
    rows = read_rows(args.csv)
    report, has_issues = markdown_report(rows, charmap, args.csv.resolve())
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    print(report)
    if args.strict and has_issues:
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, csv.Error) as exc:
        print(f"translation glyph audit failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
