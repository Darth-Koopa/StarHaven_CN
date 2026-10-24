# Star Haven — native Nintendo 3DS port of Paper Mario

A native port of the Paper Mario decompilation to Nintendo 3DS. PAL and US are the established release targets; an iQue Chinese target is being brought up separately.
The game's C code is compiled for the ARM11 and linked with a 3DS platform layer;
nothing is emulated except the N64 RSP/RDP work, which is translated on the fly.

Status: the whole game plays (v1.0.12), with 60 fps in-between frames, fast-forward, a touch
bottom screen and a randomizer. Runs on **New 3DS / New 2DS XL** (124 MB New-3DS mode); the
original 3DS and 2DS start the CIA in their 80 MB mode (96 MB crashes HOME Menu) at about
30 fps, with the audio mixer on the second core, frame skip when behind, and no reverb, 3D or
NPC/enemy shadows.
The existing release package supports PAL and US. The experimental iQue build is produced separately by the GitHub Actions workflow until it has been validated on hardware.

Players supply the ROM: it goes on the SD card as `/3ds/StarHaven/baserom.z64`, and release
packages contain no game data (see *Releases*). `RELEASE_README.md` is the player-facing readme.
The name, icon and banner are the port's own (`include/pm3ds_brand.h`, `tools/make_branding.py`).

## What is where

| Part | Files | Notes |
|---|---|---|
| Boot, threads, frame loop | `source/port_main.c` | game thread + 60 Hz retrace callback |
| The player's ROM | `source/platform/pm3ds_rom.c` | finds, converts (.n64/.v64) and CRC-checks the ROM on the SD card (romfs works for dev builds), shows what's wrong on both screens, puts back the data release packages leave out |
| Renderer (F3DEX2 → PICA200) | `source/n64_shim/gfx_translate.c`, `gfx_present.c`, `gfx_texture.c`, `gfx_combiner.c`, `vshader.v.pica` | CPU does the RSP work (MVP, lighting, clipping); the GPU rasterizes. Texture cache keyed by content (TLUT hash + sampled texels). Stereoscopic 3D with the 3D slider. |
| Audio | `source/n64_shim/audio_system.c`, `audio_alist.c`, `audio_dsp.c`, `audio_endian.c` | the game's own synth driver (`src/audio/core`) runs; its N64 command lists are executed in software and streamed to NDSP (CSND fallback) |
| Input | `source/n64_shim/os_cont.c` | see *Controls* |
| Saves | `source/n64_shim/os_flash.c` | `sdmc:/3ds/StarHaven/save.fla` (128 KB, write-through; the old `/3ds/PaperMario3DS` folder is moved on first boot) |
| Big-endian data | `audio_endian.c`, `src/collision.c` (`hit_fix_endian`), `m10_shape_reloc.c`, `m10_texheader_endian.c`, … | ROM data is byte-swapped where it is loaded |
| Bottom screen | `source/platform/bottom_screen.c`, `source/n64_shim/hud_info.c`, `touch_menu.c`, `journal.c` | tabs Stats (with a quick-save button), Map, Party, Tattle (every enemy tattled, with its tattle) and Found (collected item spots per area, from the randomizer's location table); battle menus as buttons. Quick save goes through `src/world/menus.c`: allowed whenever the pause menu could open, saved like a save block |
| Update notice | `source/platform/pm3ds_update.c` | at launch, one request to `github.com/<repo>/releases/latest` (its redirect names the newest tag); a newer version shows a notice on the bottom screen. Options → Update check turns it off |
| Debug probes | `source/n64_shim/m13_debug.c` | GAMESTAT / HPSTAT / MEMSTAT / PERF log lines, map sweep, teleport, collision checks |

Decomp sources are edited in place under `#ifdef _3DS` / `PM3DS_*` guards only.

## Building

Everything builds in the `papermario-3ds-builder` Docker image (devkitARM + libctru + citro3d).
Each target uses its own original baserom and generated assets. Do not copy a translated or
patched ROM into a different region's `ver/<version>/` directory.

1. Put the target ROM at `ver/<version>/baserom.z64` and run the decomp asset extraction/configure
   steps. For iQue, the required original ROM SHA-1 is
   `5c724685085eba796537573dd6f84aaddedc8582`.
2. Generate ROM-derived images and fonts into the gitignored `port/3ds/build_assets*/` directory:
   ```sh
   PM3DS_VERSION=ique python3 port/3ds/tools/gen_include_assets.py
   ```
   Use `PM3DS_VERSION=pal` or `us` for those targets. `.incbin` assets are not Make dependencies,
   so clean the matching build directory after regenerating them.
3. For a local test build, place the same original ROM on the 3DS SD card as
   `/3ds/StarHaven/baserom.z64`.
4. Build the iQue target:
   ```sh
   docker run --rm -v "$PWD:/project" -w /project papermario-3ds-builder \
     bash -lc 'make -j2 -C port/3ds PM3DS_VERSION=ique PM3DS_GAME=1'
   ```
   Output: `port/3ds/PaperMario3DS_ique.3dsx` and `.elf`. For PAL omit `PM3DS_VERSION`;
   for US use `PM3DS_VERSION=us`.

`PM3DS_GAME=1` enables the native world/game build. The Makefile does **not** notice changes to
`EXTRA_CFLAGS` alone; touch affected files or clean the matching build directory when changing them.

### Build iQue with GitHub Actions

`.github/workflows/ique-3ds.yml` runs source/config consistency checks on pushes and pull requests.
A manual **Actions → StarHaven iQue 3DS build → Run workflow** dispatch builds both `.3dsx`
and `.cia`, then packages the two files and an installation README into
`StarHaven_iQue_3DS.zip`. Because ROMs must not be committed or uploaded with source, configure
repository secret `IQUE_ROM_URL` with a private direct-download URL that returns the original
uncompressed iQue `.z64` ROM. The workflow verifies its SHA-1 before extracting assets, building,
and packaging; no ROM image or loose `romdata_ique.bin` is published as a standalone file.
The small ROM-data mapping table is still embedded in the executable RomFS because the program
needs it at startup; it contains metadata, not the game ROM. The build requires a GitHub runner
that can fetch the private URL without an interactive login.

### Universal launcher

The universal-launcher sources have an experimental iQue path, but the Actions workflow does not
ship a combined PAL/US/iQue build. First validate the standalone iQue `.3dsx` on an emulator and
hardware; a combined launcher requires all three region builds and matching baseroms.

## Releases (no game data)

`tools/package.sh <game.elf> <outdir> [3dsx] [cia]` makes the packages players get:
`tools/romdata.py` finds every object the build compiled from the ROM (the `INCLUDE_IMG/PAL/RAW`
blobs and the `*.vtx.inc.c` / `*.gfx.inc.c` arrays, about 1.5 MB), proves each one can be rebuilt
bit for bit from the ROM, zeroes it and writes `romdata.bin` (ROM offsets and sizes only) into the
package's romfs. At boot `pm3ds_rom.c` puts the data back from the player's ROM and checks a CRC32
of the result. The universal CIA is about 9 MB.

```
python3 port/3ds/tools/gen_include_assets.py      # also writes build_assets/manifest.tsv
# build as above, with EXTRA_CFLAGS='' (no Wi-Fi log) for a public release
bash port/3ds/tools/package.sh port/3ds/PaperMario3DS.elf out/        # out/StarHaven.cia, .3dsx
# experimental universal package (needs the matching PAL, US and iQue baseroms)
PM3DS_VERSION=universal bash port/3ds/tools/package.sh port/3ds/StarHaven_universal.elf out/
```
It needs `makerom` (3DSGuy/Project_CTR) at `port/3ds/local/bin/makerom` or `$MAKEROM`.
The icon and banner (`cia/icon.icn`, `cia/banner.bnr`) come from `tools/make_branding.py`
(needs `bannertool`).

`cia/app.rsf` sets the 124 MB New-3DS memory mode and maps the DSP shared memory
(`IORegisterMapping 1ff00000-1ff7ffff`); without that mapping `ndspInit` faults on hardware.
Install with FBI (e.g. *Remote Install → Scan QR Code* against a LAN HTTP server).

Sound on hardware needs the DSP firmware dump (`sdmc:/3ds/dspfirm.cdc`, from DSP1).

## Controls

Defaults (`pm3ds_settings_default_keymap`; remappable on the bottom screen, saved in `port.cfg`):

| 3DS | N64 |
|---|---|
| Circle Pad | Stick |
| A / B | A / B |
| L | Z |
| R | R |
| Select | L |
| Start | Start |
| X | C-Down |
| Y, ZL | C-Left |
| D-Pad, C-Stick | C buttons |
| ZR | fast-forward (held) |

The randomizer (`source/n64_shim/rando.c`) places items from tables that
`tools/rando_locations.py` generates from the map scripts; key-item logic comes from the
PMR-SeedGenerator world graph at generation time (`PMR_DIR=...`, see `tools/rando_logic.py`).

## Debug builds

Add flags with `EXTRA_CFLAGS='…'` (and touch the files that use them):

- `-DPM3DS_START_AREA=AREA_KMR -DPM3DS_START_MAP=5 -DPM3DS_START_ENTRY=0 -DPM3DS_START_STORY=STORY_…`
  — *New Game* starts in that map (with the hammer and Goombario). The map number is the
  position in the area's list in `src/world/world.c`.
- `-DPM3DS_MAP_SWEEP=360` — visit every map (entry 0, battles off), logging `SWEEP area/map name`;
  `-DPM3DS_SWEEP_FIRST_AREA=n` starts at area *n*.
- `-DPM3DS_TELEPORT_X/Y/Z=…` — put the player somewhere ~3 s after reaching the field.
- `-DPM3DS_COLLIDER_CHECK` — validate collision data and report the first corruption.
- `-DPM3DS_AUDIO_WAVDUMP=secs` — write the first *secs* seconds of audio to `sdmc:/pm3ds_audio.wav`.

The bottom screen shows `FPS / game ms / render ms` once a second; the same goes to the log
as `PERF` lines. In Azahar the log (`~/.local/share/azahar-emu/log/azahar_log.txt`) carries
every `pm3ds_log` line via `svcOutputDebugString`.

## Porting notes (things that bit)

- devkitARM enums are *small* (1 byte if the values fit); `AuResult` carries 32-bit values.
- N64 code tells pointers from IDs by sign (`x < EVT_LIMIT`, `msgID >= 0`); 3DS pointers are
  positive → `MSG_IS_ID` / `EVT_IS_PTR` in `include/macros.h`.
- GCC reorders globals; the port builds with `-fno-toplevel-reorder` because N64 code relies
  on adjacent declarations (palette+image blobs, borrowed buffers).
- Display-list words read as `(*(u64*)gfx) >> 32` give `w1` on little-endian.
- Natively linked entity graphics must not be rebased (`ENTITY_ADDR`, `mdl_project_tex_coords`).
- Read big files in chunks: a single 64 MB `fread` panics the `fs` process on hardware.

## License

The port (everything under `port/3ds/` and the port's changes to the decompilation) is
licensed under the [Apache License 2.0](LICENSE), copyright 2026 SecondaryLifer. Forks are
welcome; keep the [NOTICE](NOTICE) file and credit Star Haven by SecondaryLifer. The
decompilation is [pmret/papermario](https://github.com/pmret/papermario); Paper Mario and its
assets belong to Nintendo and are not covered.
