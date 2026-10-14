# port/3ds Scaffolding Audit — 2026-05-13

Baseline snapshot before the Boot + HAL skeleton refactor described in
[`docs/superpowers/specs/2026-05-13-papermario-3ds-boot-hal-design.md`](../../../docs/superpowers/specs/2026-05-13-papermario-3ds-boot-hal-design.md)
and executed via the plan in `docs/superpowers/plans/2026-05-13-papermario-3ds-boot-hal.md`.

The goal of this audit is to record the *current* state of `port/3ds/` — what
files exist, what they define, where they map to in the target layout, and
what the build does right now. No code is changed by this task.

## Docker build result

Image build succeeded.

```
docker build -t papermario-3ds-builder -f port/3ds/Dockerfile .
```

Tail of `/tmp/pm3ds-docker-build.log`:

```
#5 [2/4] RUN dkp-pacman -Syyu --noconfirm 3ds-dev      CACHED
#6 [3/4] RUN arm-none-eabi-gcc --version && which 3dsxtool && echo "OK"  CACHED
#7 [4/4] WORKDIR /project                              CACHED
#8 exporting to image
#8 writing image sha256:8ab6a2e6ed3362312e52ea55cd89b91606627a9aecde3ed6a48bc688265ea3f5 done
#8 naming to docker.io/library/papermario-3ds-builder done
#8 DONE 0.0s
```

The devkitARM + libctru + citro3d toolchain is reachable inside the container.

## Make result

```
docker run --rm -v "$(pwd)":/project \
  -e DEVKITPRO=/opt/devkitpro \
  -e DEVKITARM=/opt/devkitpro/devkitARM \
  -e PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:/usr/bin:/bin \
  papermario-3ds-builder \
  make -C /project/port/3ds
```

Exit code: `2`. First error and tail of `/tmp/pm3ds-make.log`:

```
make: Entering directory '/project/port/3ds'
5B320.c
/project/port/3ds/../../src/hud_element.h:245:63: error:
    'ICON_anim_hand_0_raster' undeclared here (not in a function)
  245 | #define hs_SetIcon(time, icon)
        HUD_ELEMENT_OP_SetImage, time, ICON_##icon##_raster, ICON_##icon##_palette, ...
...
/project/port/3ds/../../src/5B320.c:295:10: fatal error:
    item_data.inc.c: No such file or directory
  295 | #include "item_data.inc.c"
compilation terminated.
make[1]: *** [/opt/devkitpro/devkitARM//base_rules:40: 5B320.o] Error 1
make:    *** [Makefile:148: all] Error 2
make: Leaving directory '/project/port/3ds'
```

The shim layer's own `.c` files compiled cleanly in an earlier run (objects
exist in `port/3ds/build/` from a prior attempt: `audio_impl.o`,
`cont_impl.o`, `gfx_impl.o`, `heap_impl.o`, `math_impl.o`, `os_impl.o`,
`romfs_impl.o`, `game_loop_3ds.o`). The failure is downstream of the shims:
the Makefile's `SOURCES` line includes the entire decomp `../../src` directory,
which depends on build-time-generated headers and data inc files that the
N64 decomp build produces but the 3DS port has no equivalent for yet:

  * `ICON_*_raster` / `ICON_*_palette` enums — generated from `assets/.../icon_offsets`.
  * `item_data.inc.c` — generated table from `assets/.../item_data`.

No `multiple definition` link errors were reached because compilation failed
before linking. No `undefined reference` errors either, for the same reason.

The build is therefore **front-loaded with decomp asset/generation
dependencies** that need to be either stubbed, regenerated, or excluded
before the shim layer's behavior can be exercised.

## File inventory

`port/3ds/source/` — game-side entry and loop:

| Existing file                              | LOC | Top-level symbols (approx)                              | Maps to (spec)                | Action          |
|--------------------------------------------|-----|---------------------------------------------------------|-------------------------------|-----------------|
| `source/main_3ds.c`                        | 114 | `main`, `gfxRetrace_Callback`, `gfxPreNMI_Callback`     | `source/main.c`               | rename+refactor |
| `source/game_loop_3ds.c`                   | 387 | `step_game_loop`, `gfx_draw_frame`, `gfx_init_state`, `gfx_task_background`, ~30 game-side stubs | merge into `source/main.c` (or keep as `source/game_loop.c`) | merge / rename  |

`port/3ds/source/n64_shim/` — HAL surface:

| Existing file                              | LOC | Defines                                                                                                                                                                                                                                                                | Maps to (spec)                                                                                                       | Action  |
|--------------------------------------------|-----|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------|---------|
| `source/n64_shim/os_impl.c`                | 420 | `osCreateThread`, `osStartThread`, `osStopThread`, `osYieldThread`, `osGetThreadId`, `osSetThreadPri`, `osGetThreadPri`, `osCreateMesgQueue`, `osSendMesg`, `osRecvMesg`, `osGetCount`, `osSetTime`, `osGetTime_custom`, `osSetTimer`, `osStopTimer`, `osSetIntMask`, `osGetIntMask`, `osInvalDCache`, `osInvalICache`, `osWritebackDCache`, `osWritebackDCacheAll`, `osVirtualToPhysical`, `osPhysicalToVirtual`, `osSyncPrintf`, `osPanic`, `osEepromLongRead`, `osEepromLongWrite`, `osEepromRead`, `osEepromWrite`, `osEepromProbe`, `osPfsInitPak`, `osPfsInit`, `osPfsFileStat`, `osPfsFileDelete`, `osPfsFileDataRead`, `osPfsFileDataWrite`, `osPfsFreeBlocks`, `osPfsNumFiles`, `osPfsAllocateFile`, `osPfsReadWriteFile`, `pm3ds_dma_copy_segment`, `pm3ds_load_file`, `guNormalize`, `guMtxIdentF` | split: `os_thread.c`, `os_mesgqueue.c`, `os_time.c`, `os_timer.c`, `os_event.c`, `os_cache.c`, `os_pi.c`, `os_eeprom.c`, `os_si_sram.c`; logging/panic into `platform/pm3ds_log.c` + `platform/pm3ds_panic.c`; `gu*` minimal helpers into a separate `gu_minimal.c` | split   |
| `source/n64_shim/cont_impl.c`              | 189 | `pm3ds_update_controller_state` (static), `nuContInit`, `nuContDataGet`, `nuContDataGetAll`, `nuContDataRead`, `nuContDataReadWait`, `nuContDataReadStart`, `nuContDataLock`, `nuContDataUnLock`, `nuContRmbForceStop`, `nuContRmbStart`, `nuContRmbStop`, `nuContRmbCheck`, `nuContRmbModeSet`, `nuContRmbForceStopEnd`, `nuContRmbSearchTimeSet` | `source/n64_shim/os_cont.c`                                                                                          | rename  |
| `source/n64_shim/gfx_impl.c`               | 418 | `pm3ds_parse_*` (static DL walkers), `pm3ds_translate_display_list` (static), `nuGfxInit`, `nuGfxInitEX2`, `nuGfxThreadStart`, `nuGfxTaskMgrInit`, `nuGfxTaskStart`, `nuGfxFuncSet`, `nuGfxPreNMIFuncSet`, `nuGfxSwapCfbFuncSet`, `nuGfxSetCfb`, `nuGfxSwapCfb`, `nuGfxSetUcodeFifo`, `nuGfxTaskEndFuncSet`, `nuGfxTaskAllEndWait`, `nuGfxRetraceWait`, `nuGfxDisplayOff`, `nuGfxDisplayOn`, `osViSetMode`, `osViSetYScale`, `osViSetSpecialFeatures`, `osViSwapBuffer`, `osViGetCurrentFramebuffer` | split: DL-walking + `gSP*` / `gDP*` translation → `source/n64_shim/gfx_dl.c`; CFB/`nuGfx*`/`osVi*` swap → `source/n64_shim/gfx_present.c` (+ `os_vi.c` for `osVi*` specifically per spec) | split   |
| `source/n64_shim/audio_impl.c`             | 108 | `nuAuInit`, `nuAuPreNMIFuncSet`, `nuAuMgr`, `n_alSndpNew`, `n_alSndpSetSound`, `n_alSndpPlay`, `n_alSndpStop`, `n_alSndpSetVol`, `n_alSndpSetPitch`, `n_alSndpSetPan`, `n_alSndpSetPriority`, `n_alSndpGetState`, `n_alSndpAllocate`, `n_alSndpDeallocate` | `source/n64_shim/audio_stub.c`                                                                                       | rename  |
| `source/n64_shim/heap_impl.c`              |  74 | `_heap_create`, `_heap_malloc`, `_heap_malloc_tail`, `_heap_free`, `_heap_realloc`, `heap_malloc`, `heap_free`, `heap_realloc`, `pm3ds_heap_malloc_linear`, `pm3ds_heap_free_linear` | `source/platform/pm3ds_heap.c` (and platform exports `heap_*` consumed by decomp) | move    |
| `source/n64_shim/romfs_impl.c`             |  75 | `pm3ds_asset_exists`, `pm3ds_load_asset`, `pm3ds_load_segment`, `pm3ds_load_sprite`, `pm3ds_load_background` | (keep, helper only) | keep    |
| `source/n64_shim/math_impl.c`              |  64 | `n64_mtxf_lookat`, `n64_mtxf_perspective`, `n64_rand_int`, `n64_rand_seed`, `pm_atan2` | (keep, helper only) | keep    |

**Total shim LOC:** 1849.

## Missing target files (to create)

Per the spec's layout (referenced from the plan task list):

* `port/3ds/source/platform/pm3ds_panic.c`
* `port/3ds/source/platform/pm3ds_log.c`
* `port/3ds/source/platform/pm3ds_clock.c`
* `port/3ds/source/platform/pm3ds_heap.c` *(moved from `source/n64_shim/heap_impl.c`)*
* `port/3ds/source/n64_shim/os_thread.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/os_mesgqueue.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/os_time.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/os_timer.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/os_event.c`
* `port/3ds/source/n64_shim/os_cache.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/os_pi.c`
* `port/3ds/source/n64_shim/os_vi.c` *(extracted from `gfx_impl.c`)*
* `port/3ds/source/n64_shim/os_cont.c` *(renamed from `cont_impl.c`)*
* `port/3ds/source/n64_shim/os_eeprom.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/os_si_sram.c` *(extracted from `os_impl.c`)*
* `port/3ds/source/n64_shim/gfx_dl.c` *(split from `gfx_impl.c`)*
* `port/3ds/source/n64_shim/gfx_present.c` *(split from `gfx_impl.c`)*
* `port/3ds/source/n64_shim/audio_stub.c` *(renamed from `audio_impl.c`)*
* `port/3ds/source/n64_shim/libc_shims.c`
* `port/3ds/source/main.c` *(renamed from `main_3ds.c`, with `game_loop_3ds.c` merged)*
* `port/3ds/include/pm_endian.h`
* `port/3ds/tools/check_undef_syms.py`
* `port/3ds/tools/check_endian_access.py`
* `port/3ds/tools/byteswap_blob.py` *(stub)*
* `port/3ds/tools/extract_baserom.py`
* `port/3ds/tests/` *(host-side test scaffolding — runner.h, Makefile, per-shim tests)*

## Build attempt 2 — 2026-05-13 (Boot + HAL milestone close)

After Tasks 1–22 of the plan landed, a clean Docker build was attempted
with the new SOURCES layout and the goal-pinning Makefile changes from
Task 19.

Iteration log:

1. **First run** — `host-tests` was the first-listed target in the file,
   so `make -C port/3ds` (no goal) picked it up as the default. Fix:
   `.DEFAULT_GOAL := all` pinned inside the configuration branch.

2. **Second run** — the recursive `make -C build` re-read the Makefile;
   in that scope `host-tests` was still the first phony target and won
   the implicit default. Fix: gate the `host-tests` rule on
   `$(notdir $(CURDIR)) != build` so it doesn't exist in the recursive
   child make.

3. **Third run** — first real compile failure: `os_mesgqueue.c`
   conflict between our LightLock-backed struct (host-test branch) and
   `include/n64/os_shim.h`'s legacy `OSMesgQueue` layout (`msg`,
   `msgCount`, `validCount`, `first` fields). Per audit open question
   #1, dropped `../../src` from SOURCES (decomp tree isn't compilable
   at this milestone; asset codegen is owned by milestone 3, decomp
   compile fidelity by milestone 4). Also made `os_mesgqueue.c`'s
   struct definition unconditional and removed its
   `#include "n64/os_shim.h"` — the two layouts now coexist (private
   to os_mesgqueue.c vs public in os_shim.h) since no in-tree caller
   observes the public layout yet. Reconciliation is owned by
   milestone 4.

4. **Fourth run** — `multiple definition of osGetTime` against
   `libctru.a(os.o)`. libctru exports a Unix-epoch-millis `osGetTime`,
   the N64 contract is "cycles since reset". Suppressed our
   `osGetTime` for Milestone 1; the N64-semantics version (named
   `osGetTime_custom` in the meantime) is still exported alongside
   libctru's. `osGetCount` did not clash. Milestone 4 will own the
   real reconciliation, likely via `-Wl,--wrap=osGetTime`.

5. **Fifth run** — **CLEAN.** Artifacts produced:

   ```
   port/3ds/PaperMario3DS.3dsx    161 320 bytes
   port/3ds/PaperMario3DS.elf   1 143 860 bytes
   port/3ds/PaperMario3DS.smdh    14 016 bytes
   ```

   `tools/check_undef_syms.py port/3ds/PaperMario3DS.elf` reports
   `OK: no unresolved libultra symbols`.

The Milestone 1 build product is intentionally a *shim-only* smoke
binary: `main_func` is unresolved (weak symbol), so `main.c` falls
through to its smoke-loop branch and runs the frame loop with no
decomp game logic in the link. Real-hardware / Citra / Azahar boot
verification is owned by Task 25.

## Open questions discovered during audit

1. **The Makefile pulls in the entire decomp `../../src` tree at the top level.**
   The plan (Task 2) calls for excluding `../../src/os/`, `../../src/os/nusys/`,
   and `../../src/buffers.c`. Those happen to be *sub-directories* of `../../src/`,
   so `wildcard ../../src/*.c` does not currently pick them up (the build only
   compiles top-level `../../src/*.c`). However, `../../src/` itself contains
   ~200 top-level `.c` files (`5B320.c`, `25AF0.c`, `38F00.c`, `43F0.c`,
   `7B440.c`, …) that depend on generated headers (`icon_offsets.h` enums,
   `item_data.inc.c`, etc.) that the 3DS port does not yet produce.
   **Question for the spec:** Does Milestone 1 actually need to compile any
   of `../../src/*.c` to "reach the existing main game loop", or should the
   Boot+HAL milestone *exclude the entire decomp `src/` tree* and only
   compile the shim layer + a stub main loop? The current Task 2 in the plan
   only documents libultra-dir exclusion; it does not address top-level
   decomp `.c` files. **Recommendation:** trim `SOURCES` in a later task to
   `source source/n64_shim` only (plus, eventually, the platform/ dir) until
   the asset/codegen pipeline is wired up in a future milestone.

2. **`include_asm.h` is force-included via `-include include_asm.h`**
   (Makefile line 65). The 3DS port has its own
   `port/3ds/include/include_asm.h` shim, but the decomp's own `.c` files use
   `INCLUDE_ASM(...)` macros that reference `.s` files under
   `ver/us/asm/nonmatchings/...` which the 3DS port has no path to. This is
   a second reason any `../../src/*.c` file that hits an `INCLUDE_ASM` will
   fail (separate from the icon/item-data generation issue).

3. **Build cache from a prior attempt is present** (`port/3ds/build/*.o` files
   dated 2026-05-13 02:38 with root ownership from the container). These
   include working `audio_impl.o`, `cont_impl.o`, `game_loop_3ds.o`,
   `gfx_impl.o`, `heap_impl.o`, etc., confirming the shim layer compiles
   independently when it isn't blocked by decomp asset dependencies. A
   `make clean` step is recommended early in the refactor to avoid stale
   .o files masking real breakage.

4. **`source/main_3ds.c` already declares many decomp-side symbols as
   `extern`** (e.g., `step_game_loop`, `gfx_draw_frame`, `load_engine_data`,
   `create_audio_system`). Most of these are *also* re-defined as empty stubs
   in `source/game_loop_3ds.c`. When Task 18 (new `main.c`) is executed,
   the duplicate extern declarations and stub defs need to be reconciled
   — currently the same symbol is both declared `extern` in one file and
   defined in another, which works but is hostile to future split.

5. **`heap_impl.c` defines both decomp-named (`_heap_create`,
   `heap_malloc`, …) and 3DS-named (`pm3ds_heap_malloc_linear`,
   `pm3ds_heap_free_linear`) helpers.** The spec places this file under
   `source/platform/pm3ds_heap.c` and intends it as a *platform primitive*
   that the n64-shim layer + game code consume. The split between "decomp
   heap-management API" and "3DS linear-heap helper" should be made
   explicit when this file moves: the decomp-shaped `heap_*` API likely
   wraps the platform `pm3ds_heap_*` primitives, not the other way around.

6. **`gfx_impl.c` already contains the `osVi*` group** (`osViSetMode`,
   `osViSetYScale`, `osViSetSpecialFeatures`, `osViSwapBuffer`,
   `osViGetCurrentFramebuffer`). The plan's Task 12 (`os_vi.c`) extracts
   these; Task 15 (`gfx_dl.c` / `gfx_present.c`) extracts the `nuGfx*` + DL
   walker. The split between Task 12 and Task 15 in this file is clean — DL
   parsing and `nuGfx*` go to gfx_dl/gfx_present, `osVi*` go to `os_vi.c`.

7. **No `port/3ds/source/platform/` directory exists yet.** Tasks 5–7
   (`pm3ds_log.c`, `pm3ds_panic.c`, `pm3ds_clock.c`) implicitly require it;
   it can be created when the first platform file lands.

8. **No `port/3ds/tests/` directory exists yet.** Task 3 creates it.

9. **No `port/3ds/Makefile` change has yet been made for the HAL split.**
   The current `SOURCES := source source/n64_shim ../../src ...` will need
   to be updated by Task 19 to add `source/platform` and (per question 1)
   probably to drop the bare `../../src` until later milestones.

## Milestone 2 starting state — 2026-05-13

Heading into the F3DEX2 → PICA200 translator with the milestone-1
deliverable as the baseline. Current shim-layer LoC: 1812 across the
n64_shim + platform + main.c trees. Current `.3dsx` size: 161 KB.

Files this milestone will grow:
- `port/3ds/source/n64_shim/gfx_dl.c` (currently a STUB recording
  osSpTaskStart count only)
- `port/3ds/source/n64_shim/gfx_present.c` (currently clears top
  screen to debug-blue and prints bottom-screen overlay)

Files this milestone will create:
- `port/3ds/source/n64_shim/gfx_translate.c`
- `port/3ds/source/n64_shim/gfx_combiner.c`
- `port/3ds/source/n64_shim/gfx_texture.c`
- `port/3ds/source/n64_shim/gfx_geom.c`
- `port/3ds/include/gfx_state.h`, `gfx_f3dex2.h`
- `port/3ds/tests/mock_c3d.h` + six test_gfx_*.c files
- `port/3ds/source/debug_dl.c` (gated by PM3DS_DEBUG_DL)
- `port/3ds/tools/dump_frame.py`

No files are deleted at this milestone.

## Milestone 2 close — 2026-05-13

All mechanised acceptance gates pass:

- `docker run … papermario-3ds-builder make -C port/3ds` produces a
  clean `.3dsx` (172 136 bytes, up from 161 320 in milestone 1 —
  the new shim sources add ~11 KB to the binary).
- `port/3ds/tools/check_undef_syms.py port/3ds/PaperMario3DS.elf`
  reports `OK`.
- `port/3ds/tools/check_endian_access.py` reports `OK`.
- `make -C port/3ds host-tests` runs 9 test binaries with **41 host
  test cases total**, 0 failures:
  - test_pm_endian (3), test_pm3ds_heap (3), test_os_mesgqueue (4),
    test_os_timer (1) — the milestone-1 inheritance.
  - test_gfx_state (12), test_gfx_dl (2), test_gfx_geom (5),
    test_gfx_texture (9), test_gfx_combiner (2) — milestone-2 new.

CI workflow runs four jobs in parallel: host-tests, lint,
docker-build, debug-dl-build. All four are wired through
`.github/workflows/port-3ds-ci.yml` and exercise the same paths the
mechanised gates above test locally.

The remaining acceptance criterion — boot the debug .3dsx in Azahar
and verify the magenta rectangle renders on the top screen with
<10 unimpl/frame and ≥30 FPS — is owned by the project owner since
emulator-on-PATH visual confirmation is the one step automation
cannot cover.

Open questions from the spec, resolved:
- Texture cache backing: host malloc + LRU eviction at 16 MB
  (Task 17). Devkit-ARM side gets the same malloc behavior since
  the cache lives in `linearAlloc`-free CPU memory until milestone
  3 wires the GPU upload path. Revisited if texture working-set
  exceeds 16 MB during real gameplay.
- Title-screen debug DL location: `port/3ds/source/debug_dl.c`
  gated by `PM3DS_DEBUG_DL` (Task 23).
- Viewport handling: `handle_movemem` under opcode 0xDC
  (Task 7B) — viewport decoded into the state mirror; lighting/
  lookat-X/Y are mirror-only at this milestone.

Open questions still deferred to milestone 4 (decomp compile-
fidelity sweep):
- `G_SETBLENDCOLOR` participation in alpha tests — the debug DL
  doesn't exercise it.
- Two-cycle CC with `COMBINED` feedback — same reason; the three
  seeded entries are single-cycle.

## Milestone 2 visual verification — 2026-05-13

After the planned 28 tasks closed, the produced .3dsx booted with
the debug-blue clear color but no rendered geometry (Azahar test).
A short follow-on session added the vertex-shader hookup the plan
had glossed over (Tasks 28–30 in the plan), and then chased three
secondary issues:

1. **Debug DL submit was one-shot** — moved into the aptMainLoop
   body so each frame's drain has fresh work.
2. **Default citro3d state** — added explicit C3D_CullFace(NONE),
   C3D_DepthTest(false, ALWAYS, ...) so the OrthoTilt-flipped
   winding and z=0 don't get rejected.
3. **DebugOSTask layout mismatch** — the local stub in main.c had
   data_ptr at offset 48; the real OSTask in gfx_dl.c puts it at
   offset 88 under ARM 8-byte u64 alignment. Drain read garbage,
   walk_dl saw NULL, terminated immediately; bottom-screen
   counters incremented (submit succeeded) but no pixels rendered.
   Fixed by copying the OSTask field list verbatim into main.c.

After (3), the top screen renders the magenta FILLRECT cleanly.
Cosmetic notes: 2-pixel bottom rim from `(239-1)` packing in
debug_dl.c (libultra's lrx/lry are inclusive — no -1 needed); the
right edge of the magenta rect ends at NDC=1 of a 320-wide ortho,
which on a 400-wide top screen leaves ~80px of unfilled visible
area on the right (the "centered with side bars" arrangement the
spec called for). Both are polish items for milestone 3+.

## Milestone 3 starting state — 2026-05-13

Heading into the asset / romfs pipeline with the M2 deliverable as
baseline. Current shim LoC: 2917 across n64_shim + platform + main.c.
Current .3dsx size: 177 KB (M2 close, no romfs embedded). Host test
binaries: 9 (41 host test cases).

Files this milestone will grow:
- `port/3ds/Makefile` (add `assets` and `romfs` targets + 3dsxtool
  --romfs flag gated by PM3DS_ENABLE_ROMFS=1)
- `port/3ds/source/n64_shim/gfx_texture.c` (real GPU upload via
  linearAlloc + C3D_TexInit + C3D_TexUpload)
- `port/3ds/source/n64_shim/gfx_translate.c` (bind texture in
  flush_draw, fetch-and-decode on cache miss)
- `port/3ds/source/n64_shim/os_pi.c` (expose pm3ds_pi_read for the
  texture pipeline)
- `port/3ds/source/debug_dl.c` (PM3DS_DEBUG_DL=2 variant)

Files this milestone will create:
- `port/3ds/tools/extract_assets.py` (configure + splat wrapper)
- `port/3ds/tools/pack_romfs.py` (romfs packer + manifest writer)
- `port/3ds/tools/manifest_signature.txt` (committed expected manifest)
- `port/3ds/source/debug_dl_assets.h` (gitignored; offset table)
- `port/3ds/tests/test_romfs_manifest.py` (host test)
- `port/3ds/romfs/` (gitignored build artefact)

No files are deleted at this milestone.

## Milestone 3 close — 2026-05-13

All mechanised acceptance gates pass without a baserom (gates that
need the user's baserom are deferred to the developer):

- Gate 1 (extract_assets.py): reachable; no-arg diagnostic is the
  expected `usage:` + exit 2. Full pipeline run requires the user's
  baserom.
- Gate 2 (pack_romfs.py): reachable; pre-extract diagnostic is the
  expected `FAIL: <repo>/assets/.extract.stamp missing` + exit 1.
- Gate 3 (Docker cross-build): clean 181 KB .3dsx with
  PM3DS_ENABLE_ROMFS=0 (no romfs embedded). check_undef_syms.py
  reports OK. .3dsx is ~4 KB larger than M2 close (177 KB) — the
  added runtime is the gfx_tex_fetch_and_upload helper plus the
  flush_draw bind dance.
- Gate 4 (dump_frame.py): SKIP exit 77 (no Citra/Azahar on PATH).
- Gate 5 (test_romfs_manifest.py): SKIP exit 0 (manifest absent).
- Bonus: 42/42 host tests green.

CI workflow runs five jobs in parallel: host-tests, lint,
docker-build, debug-dl-build, romfs-manifest-test (the last SKIPs
cleanly in CI).

Gates owned by the project owner with a US baserom:
- Gate 1 in full: `make assets BASEROM=<path>` produces assets/.
- Gate 2 in full: `make romfs` produces port/3ds/romfs/manifest.txt
  + source/debug_dl_assets.h.
- Gate 3 with embedded romfs: `make PM3DS_ENABLE_ROMFS=1
  EXTRA_CFLAGS=-DPM3DS_DEBUG_DL=2` produces a ~35 MB .3dsx.
- Gate 6 (Azahar visual): top screen should show the actual Paper
  Mario title-screen logo as a textured rectangle, bottom-screen
  unimpl/warn counters at 0.

Open questions from the spec, resolved:
- Splat dependency footprint: extract_assets.py assumes ./configure
  has been run; missing splat.yaml fires a clear stderr diagnostic.
- Title-screen logo offset: captured by pack_romfs.py via regex
  over ver/us/splat.yaml and emitted into
  port/3ds/source/debug_dl_assets.h. Sentinel zeros if the regex
  doesn't match, in which case debug_dl variant 2 falls back to
  variant 1's magenta at compile time (__has_include guard).
- Asset path mapping: preserved exactly from splat output.
- Content-SHA1 cache key: deferred — the M2 address-based key
  remains in place; M4 can revisit if a real workload exposes
  hot-multimapped textures.
