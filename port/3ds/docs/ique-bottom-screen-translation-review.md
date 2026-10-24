# iQue 3DS 下屏内嵌英文文案校对表

本表只列源码中写死、由 3DS 下屏 UI 自己绘制的英文文案，以及下屏显示的区域/伙伴名、ROM 错误提示和触控菜单固定标题。**不包含**之前已经修复为读取 iQue 游戏消息并保留中文字形编号的敌人名、道具名、战斗指令等动态游戏文本。

完整逐条清单在同目录的 `ique-bottom-screen-translation-review.csv`，Windows Excel 可直接打开（UTF-8 BOM）。本版列出 230 条源码内嵌文案/名称。当前中文是用户提交的 v3 校对结果。构建时由 `tools/generate_bottom_screen_translations.py` 检查源码键、格式占位符和 `CHARSET_IQUE` 字形覆盖，并生成 `gen/ui_translations_zh_CN.h`；无需把转码后的字节手工写进 C 源码。

## 首轮核心菜单用语（供快速校对）

| 英文 UI | 建议译文 | 校对重点 |
|---|---|---|
| Stats | 状态 | 也可按页面内容译为“统计” |
| Map | 地图 |  |
| Party | 伙伴 | 是否符合神游版既有叫法 |
| Tattle | 图鉴 | 可译作“调查”；这里既是菜单标签，也指 Goombario 调查敌人的功能 |
| Found | 收集 | “已发现/收集记录”也可能更准确 |
| Settings | 设置 |  |
| Controls | 按键 | 也可用“操作/按键设置” |
| Debug | 调试 | 开发者页 |
| Tattle Log | 敌人图鉴 | 与 “Tattle” 术语保持一致 |
| Action command | 动作指令 | 战斗机制术语，请优先核对神游版译名 |
| Randomizer | 随机模式 | 如项目内已有固定译名，请以项目术语为准 |
| Star Pieces | 星星碎片 | 物品专有名词 |
| Badges | 徽章 |  |
| Key items | 关键道具 | 也可按神游版术语调整 |
| FP / SP / HP | 暂保留缩写 | 避免首轮擅自创造专名；可由你决定是否中文化 |

区域名与伙伴名在 CSV 中单独列出，多数先标成“暂译/名称待定”，避免把未校对的美版音译误当作神游版官方名。

## 已接入方案：UTF-8 CSV → 构建时校验/转码 → iQue 字形 token

CSV `localization/ui_translations_zh_CN.csv` 是可读的翻译源。`tools/generate_bottom_screen_translations.py` 从 `tools/splat_ext/pm_msg.py` 解析 `CHARSET_IQUE`，并结合 `include/enums.h` 的多字节编码常量计算字形编号。每个非 ASCII 字符会转换为 U+E000 + 原生字形编号的私用区 token，生成 `gen/ui_translations_zh_CN.h`，供 3DS 下屏渲染器识别。字形映射缺失、源文案键找不到、或目标译文引入未知 printf 占位符都会使构建失败。

`text()` / `text_width()` / `text_center()` 在字体可用时按完整文案键查表；带运行时数字的格式串已在调用处先查译文再 `snprintf`。`small_text()` / `small_text_center()` 与新游戏帮助文本也能识别 token，并把 iQue 16×16 字形缩小为原 8×8 字体单元；换行按字形单元处理，不会把三字节 token 拆开。Tattle 正文中的 `Max HP:`、`Attack Power:`、`Defense Power:` 标签单独翻译，原始缓冲区仍用于提取数值。

同一英文源键重复时生成器保留 CSV 中第一条映射。本表目前 `Start` 同时对应“开始”和 `Start`，这次按照用户要求不阻塞构建；如果测试发现控制按键名称也被译为“开始”，后续再改成上下文键。

### 字体可用时机的限制

普通菜单、战斗触控菜单、伙伴/区域名、随机模式帮助、更新提示和存档导入提示会在内嵌 iQue 字体载入后走中文字形渲染。**没有 ROM、ROM 错误、或游戏字体尚未加载时的早期启动/错误画面仍回退显示英文**，因为这时运行时没有可用的 iQue 字库。若后续需要这些错误画面也中文化，需另外嵌入一份独立的少量中文字形资源，不能只依赖玩家 ROM。

敌人名、道具名、战斗指令等已经修复为从游戏消息系统读取并保留原生字形编号的动态文本，仍保持原路径，不做 CSV 覆盖。新出现的未翻译内嵌文案可以在实机测试反馈后增补。
## 交付/打包注意

`.3dsx` 与 `.cia` 的 RomFS 中仍需要内嵌 `romdata_ique.bin` 这张运行时映射表，因为 `pm3ds_rom.c` 会通过 `romfs:/romdata_ique.bin` 读取它。发布 ZIP 可以不把它作为独立文件交付；但不能从两个可执行包的内部彻底移除，除非另行修改运行时读取机制。
