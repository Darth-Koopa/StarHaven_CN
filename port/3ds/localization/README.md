# 下屏中文化与小字字体

## 翻译数据与生成步骤

- `ui_translations_zh_CN.csv` 是 **iQue 简体中文的独立翻译源**，使用 UTF-8，便于校对和维护。它只维护中文，不承载德语、法语、西班牙语、意大利语或日语。
- 上游六语言仍以 `source/platform/pm3ds_lang_table.inc` 为权威来源，继续使用上游 `T()` 和 `pm3ds_lang.c` 的选择逻辑。不要把六种语言迁移进中文 CSV；这样后续同步上游时，语言系统的改动面更小。
- `bottom_screen.c` 的 `menu_text()` 是两套路径的薄适配层：iQue 使用中文 CSV / 神游私用区字形，其他 ROM 版本调用上游 `T()`。iQue 设置页不显示 Language 项，始终固定简体中文；非 iQue 仍保留上游六语言切换。
- 上游新增或修改下屏英文键时：其他语言照常更新 `pm3ds_lang_table.inc`；同时在本 CSV 增加对应的中文行，并将 `source_file` 指向真实字符串所在文件。无需复制上游六语言译文到 CSV。
- `tools/splat_ext/pm_msg.py` 中的 `CHARSET_IQUE` 与 `include/enums.h` 是 iQue 字形映射的权威来源。严格生成器会校验译文字符映射、源码键和格式占位符。
- `tools/generate_bottom_screen_translations.py` 将中文转为 U+E000 私用区字形标记，生成 `port/3ds/gen/ui_translations_zh_CN.h`。
- 小字号字体选择 **Fusion Pixel 10px Prop zh-Hans（比例字体）**（10×10）；TTF 与 SIL Open Font License 1.1 文本放在 `port/3ds/fonts/`。
- `tools/generate_small_text_font.py` 从译文实际使用的汉字/标点及可打印 ASCII 构建紧凑比例位图贴图，并为每个字形保存墨迹边界和独立字宽，生成 `port/3ds/gen/ui_small_font.h`。运行时不读取 TTF、不需要 FreeType；字形位图随 C 程序编译。
- `small_text()` 与 `small_text_center()` 在 iQue 版使用该比例位图字体渲染英文和中文，因此主游戏字库尚不可用的启动、加载等阶段也能显示译文。需要保持原始 8×8 控制台风格的少数标签显式调用 `small_text_s_small()` / `small_text_center_s_small()`，绕过 TTF 贴图。`s_ui_small` 字库纹理使用线性过滤；游戏主字号 `text()` 仍然使用原生字库和 iQue 字形缓存。


## 与上游同步时的翻译维护

当前 CSV 包含上游语言表中的全部英文源键，并额外保留 iQue 专属的旧版菜单、作弊、奖励和设置标签。它是中文覆盖层，不是上游语言表的替代品。增加上游文本时，先检查本 CSV 是否已有该键；没有就新增中文译文，并运行严格生成器。若键仅存在于上游 `TR()` 表而当前没有直接 C 字符串，也可将 `source_file` 指向 `pm3ds_lang_table.inc`，避免漏掉未来重新启用的键。

## 本地检查命令

首次生成字体头文件前安装 host 依赖：`python3 -m pip install Pillow`。然后运行：

```sh
python3 port/3ds/tools/generate_bottom_screen_translations.py --strict
python3 port/3ds/tools/generate_small_text_font.py \
  --font port/3ds/fonts/FusionPixel-10px-Prop-zh-Hans.ttf \
  --csv port/3ds/localization/ui_translations_zh_CN.csv \
  --out port/3ds/gen/ui_small_font.h --size 10
python3 port/3ds/tools/check_bottom_screen_translation_glyphs.py \
  --report port/3ds/localization/ui_translation_glyph_audit.md
```

要尝试 12×12 字体，生成器支持 `--font <SimSun.ttf> --size 12`；还需在对应构建配置中把该字体文件作为输入，并按相同命令重新生成头文件。当前工作流默认使用仓库内的 Fusion Pixel 10×10 字体。

## 当前审查注意事项

全局按英文原文查表时，`Start` 同时用作“开始”动作和手柄上的 Start 键名。当前 Controls 绘制路径对按钮名使用原文绘制，其他对应动作继续显示“开始”；翻译生成器仍会对 CSV 中这个重复键给出警告。

所有中文译文都必须能通过 `CHARSET_IQUE` 映射。修改中文后，应重跑严格检查和小字字体生成器，避免生成头文件与 CSV 不同步。
