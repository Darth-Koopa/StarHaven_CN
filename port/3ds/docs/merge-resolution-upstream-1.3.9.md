# Upstream 1.3.9 merge resolution (iQue Chinese)

This source tree reconciles the `cn(1.2.15)` work with upstream `1.3.9` UI and build changes.

## Translation architecture

- Keep upstream `pm3ds_lang.c` and `pm3ds_lang_table.inc` as the source of truth for the six upstream languages: English, Deutsch, Français, Español, Italiano, and 日本語.
- Do not migrate or reformat the upstream language table into the iQue CSV. This is intentional: keeping the upstream files structurally unchanged makes later upstream merges smaller and easier to review.
- `port/3ds/localization/ui_translations_zh_CN.csv` is the iQue-only Simplified Chinese overlay. `menu_text()` in `source/platform/bottom_screen.c` routes iQue text through the Chinese translation/private-glyph path and routes non-iQue builds through upstream `T()`.
- iQue builds keep the menu fixed to Simplified Chinese and omit the Language selector. Non-iQue builds keep the upstream six-language selector.
- When upstream adds or edits a bottom-screen menu string, keep the upstream `TR()` entry as upstream provides it, and add/update the matching English key and Chinese translation in `ui_translations_zh_CN.csv` for iQue. Do not copy the six translations into the Chinese CSV.
- Run `python3 port/3ds/tools/generate_bottom_screen_translations.py --strict` and regenerate `gen/ui_small_font.h` after CSV changes.

## Conflict resolutions

- `port/3ds/Makefile`: retained the PAL/US/iQue/JP build selection and host-test goal gating without duplicate iQue condition branches.
- `port/3ds/include/pm3ds_rom_layout.h`: retained the distinct region layouts and the original iQue ROM layout constants.
- `port/3ds/source/n64_shim/touch_menu.c`: retained both the ROM-layout and game-mode declarations required by the merged code.
- `port/3ds/source/platform/bottom_screen.c`: retained upstream's scrollable settings UI, six-language support for non-iQue builds, and quit-game flow; made iQue use the Chinese overlay and fixed-Chinese settings. Preserved the iQue glyph rendering path and the existing custom small-font behavior. The partner-name renderer uses tighter inter-glyph spacing without reducing its nominal scale.
- `port/3ds/source/platform/pm3ds_rom.c`: retained upstream ROM identification while accepting the official iQue blank game ID under the iQue build, with CRC/size checks still applied.
- `port/3ds/tools/gen_version_headers.sh`: retained the per-version header generation flow for US/iQue/JP and strict shell error handling.
- `port/3ds/tools/package.sh`: retained packaging branches for PAL, universal, US, iQue, and JP targets.
- `port/3ds/tools/romdata.py`: retained the iQue copy-prefix and official ROM metadata alongside US handling.

## Translation coverage

The CSV is checked against every English `TR()` key in `pm3ds_lang_table.inc`, as well as the iQue-specific bottom-screen strings. Format placeholders must match between source and translation. The current CSV has 343 rows (341 unique English keys); all 233 upstream `TR()` keys are represented.

## Build/test notes

The source archive is a resolved working tree, not a Git merge commit. It does not contain a ROM or compiled 3DS artifacts. Run the project's GitHub Actions/devkitARM build on the target branch after copying these files into the repository. The ROMFS manifest test is expected to skip until the ROMFS packaging step has generated `port/3ds/romfs/manifest.txt`.
