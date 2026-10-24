# 下屏中文译文字库检查报告

- 翻译文件：`port/3ds/localization/ui_translations_zh_CN.csv`
- 翻译条目：230
- CHARSET_IQUE 中可直接映射的汉字/符号：2362 个不同字符
- CHARSET_IQUE 映射覆盖的字形编号：2362 个（范围 0xA6–0x9DF；内嵌字库共 2528 个 16×16 字形）
- 译文中的非 ASCII 字符出现次数：978
- 字库中找不到映射的字符：0 个

## 字库映射缺失

所有译文字符均可通过 `CHARSET_IQUE` 找到映射。

## 同一英文原文存在不同译文

全局按原文查找会产生歧义；这些条目需要显式上下文键或在调用点指定分类：

- `Start`：New game/randomizer → `开始` (bottom_screen.c)；Controls names → `Start` (pm3ds_settings.c)

## 转码规则

1. 保留 CSV 作为可读、可校对的 UTF-8 翻译源。
2. 构建时从 `tools/splat_ext/pm_msg.py` 解析 `CHARSET_IQUE`，并使用 `include/enums.h` 的原生编码常量计算字形编号。
3. 所有目标字符通过严格字库检查后，才把中文转换为当前 3DS 字体渲染器识别的私用区字形标记（U+E000 + 字形编号）；ASCII 与 `%d`、`%s`、`%08X` 等占位符保持原样。
4. 不把转换后的不可读字节硬编码到手写 C 源码；由 Python 生成头文件，使修改翻译后能在 CI 自动检查和重建。
5. 主字号和 8×8 小字号绘制入口都识别私用区字形标记；小字号以 0.5 倍比例绘制游戏字库中的 16×16 汉字，并按字符单元换行。
6. 含运行时参数的格式串由调用点先按翻译键查表，再传给 `snprintf`；不在最终绘制阶段猜测动态字符串的含义。

## 重要提醒

本工具只生成字库审计报告；实际 C 翻译表由 `tools/generate_bottom_screen_translations.py` 生成。正式构建必须运行该生成器的 `--strict` 检查。游戏字体尚未加载时（例如找不到 ROM 的早期错误画面）会回退英文，因为此时运行时没有可读取的 iQue 字库。
