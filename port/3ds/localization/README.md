# 下屏中文化与小字字体

## 翻译数据与生成步骤

- `ui_translations_zh_CN.csv` 是翻译源，使用 UTF-8，便于校对和维护。
- `tools/splat_ext/pm_msg.py` 中的 `CHARSET_IQUE` 与 `include/enums.h` 是 iQue 字形映射的权威来源。严格生成器会校验译文字符映射、源码键和格式占位符。
- `tools/generate_bottom_screen_translations.py` 将中文转为 U+E000 私用区字形标记，生成 `port/3ds/gen/ui_translations_zh_CN.h`。
- 小字号字体选择 **Fusion Pixel 10px Mono zh-Hans**（10×10）；TTF 与 SIL Open Font License 1.1 文本放在 `port/3ds/fonts/`。
- `tools/generate_small_text_font.py` 从译文实际使用的汉字/标点及可打印 ASCII 构建紧凑位图贴图，生成 `port/3ds/gen/ui_small_font.h`。运行时不读取 TTF、不需要 FreeType；字形位图随 C 程序编译。
- `small_text()` 与 `small_text_center()` 在 iQue 版统一使用该位图字体渲染英文和中文，因此启动页、ROM 加载进度页等主游戏字库尚不可用的阶段也能显示译文。主字号 `text()` 仍然使用游戏的原生字体资源。

## 本地检查命令

首次生成字体头文件前安装 host 依赖：`python3 -m pip install Pillow`。然后运行：

```sh
python3 port/3ds/tools/generate_bottom_screen_translations.py --strict
python3 port/3ds/tools/generate_small_text_font.py \
  --font port/3ds/fonts/FusionPixel-10px-Mono-zh-Hans.ttf \
  --csv port/3ds/localization/ui_translations_zh_CN.csv \
  --out port/3ds/gen/ui_small_font.h --size 10
python3 port/3ds/tools/check_bottom_screen_translation_glyphs.py \
  --report port/3ds/localization/ui_translation_glyph_audit.md
```

要尝试 12×12 字体，生成器支持 `--font <SimSun.ttf> --size 12`；还需在对应构建配置中把该字体文件作为输入，并按相同命令重新生成头文件。当前工作流默认使用仓库内的 Fusion Pixel 10×10 字体。

## 当前审查注意事项

全局按英文原文查表时，`Start` 同时用作“开始”动作和手柄上的 Start 键名。当前 Controls 绘制路径对按钮名使用原文绘制，其他对应动作继续显示“开始”；翻译生成器仍会对 CSV 中这个重复键给出警告。

所有中文译文都必须能通过 `CHARSET_IQUE` 映射。修改中文后，应重跑严格检查和小字字体生成器，避免生成头文件与 CSV 不同步。
