/*
 * m6_load_engine_data_wrap.c -- diagnostic wrapper around load_engine_data
 *
 * Linked via -Wl,--wrap=load_engine_data so calls go through
 * __wrap_load_engine_data instead of the real one.
 *
 * M6.1h: replicate the original body call-by-call with a log line
 * between each step. The last log before the crash identifies the
 * specific init function that breaks the emulator.
 *
 * PM3DS_SKIP_LOAD_ENGINE_DATA=1 short-circuits the whole thing for
 * the boot_main-shell-only sanity check.
 */

#include "pm3ds_log.h"
#include "pm3ds_rom_layout.h"

extern void __real_load_engine_data(void);

#ifndef PM3DS_SKIP_LOAD_ENGINE_DATA

/* Externs for everything load_engine_data calls. Signatures match
 * the decomp's declarations in src header files. We only declare what we use. */
extern int  gOverrideFlags;
extern int  gTimeFreezeMode;
extern signed char gGameStepDelayCount;   /* s8 in decomp: an int store clobbered gGameStatusPtr */
extern void *gGameStatusPtr;  /* opaque; load_engine_data writes fields
                                via the real layout; we don't poke them
                                from here, so a void* is enough for the
                                fio_init_flash/clear_* call chain. */

extern void fio_init_flash(void);
extern void clear_input(void);
extern void *general_heap_create(void);
extern void clear_render_tasks(void);
extern void clear_worker_list(void);
extern void clear_script_list(void);
extern void create_cameras(void);
extern void clear_player_status(void);
extern void spr_init_sprites(int playerSpritesID);
extern void clear_entity_models(void);
extern void clear_animator_list(void);
extern void clear_model_data(void);
extern void clear_sprite_shading_data(void);
extern void reset_background_settings(void);
extern void clear_character_set(void);
extern void clear_printers(void);
extern void clear_game_mode(void);
extern void clear_npcs(void);
extern void hud_element_clear_cache(void);
extern void clear_trigger_data(void);
extern void clear_entity_data(int);
extern void clear_player_data(void);
extern void init_encounter_status(void);
extern void clear_screen_overlays(void);
extern void clear_effect_data(void);
extern void clear_saved_variables(void);
extern void clear_item_entity_data(void);
extern void bgm_reset_sequence_players(void);
extern void reset_ambient_sounds(void);
extern void sfx_clear_sounds(void);
extern void clear_windows(void);
extern void initialize_curtains(void);
extern void poll_rumble(void);
extern void set_game_mode(short mode);   /* s16 in decomp */
extern void pm3ds_load_engine_status_fields(void);

/* From include/enums.h / src/game_modes.h — literals to keep this shim
 * header-free, same approach as PM3DS_PLAYER_SPRITES_MARIO_WORLD below. */
#define PM3DS_GLOBAL_OVERRIDES_DISABLE_DRAW_FRAME 0x8
#define PM3DS_GAME_MODE_STARTUP 0

/* PLAYER_SPRITES_MARIO_WORLD value from include/enums.h — using the
 * literal so we don't have to drag the whole header in. The real
 * value is documented as 0 in the decomp; if it changes upstream
 * we'll get a behaviour difference, not a build break. */
#define PM3DS_PLAYER_SPRITES_MARIO_WORLD 0

#define STEP(tag, expr) do {                                    \
    pm3ds_log(PM3DS_LOG_INFO, "led-step: " tag);                \
    expr;                                                       \
} while (0)

#endif

void __wrap_load_engine_data(void) {
    pm3ds_log(PM3DS_LOG_INFO, "wrap: load_engine_data:enter");

#ifdef PM3DS_SKIP_LOAD_ENGINE_DATA
    pm3ds_log(PM3DS_LOG_INFO, "wrap: load_engine_data SKIPPED (flag set)");
#else
    /* We skip the DMA_COPY_SEGMENT macro calls and the
     * gGameStatusPtr-> field writes because they're inline
     * compile-time-known operations and (a) DMA_COPY_SEGMENT is a
     * no-op stub on this port, (b) the gGameStatusPtr writes are
     * very unlikely to be the crash given the heap was up and
     * malloc/free worked. Focus instrumentation on the function
     * calls. */

    STEP("fio_init_flash",          fio_init_flash());
    STEP("clear_input",             clear_input());
    STEP("general_heap_create",     general_heap_create());
    STEP("clear_render_tasks",      clear_render_tasks());
    STEP("clear_worker_list",       clear_worker_list());
    STEP("clear_script_list",       clear_script_list());
    STEP("create_cameras",          create_cameras());
    STEP("clear_player_status",     clear_player_status());
    STEP("spr_init_sprites",        spr_init_sprites(PM3DS_PLAYER_SPRITES_MARIO_WORLD));
    STEP("clear_entity_models",     clear_entity_models());
    STEP("clear_animator_list",     clear_animator_list());
    STEP("clear_model_data",        clear_model_data());
    STEP("clear_sprite_shading_data", clear_sprite_shading_data());
    STEP("reset_background_settings", reset_background_settings());
    STEP("clear_character_set",     clear_character_set());
    STEP("clear_printers",          clear_printers());
    STEP("clear_game_mode",         clear_game_mode());
    STEP("clear_npcs",              clear_npcs());
    STEP("hud_element_clear_cache", hud_element_clear_cache());
    STEP("clear_trigger_data",      clear_trigger_data());
    STEP("clear_entity_data",       clear_entity_data(0));
    STEP("clear_player_data",       clear_player_data());
    STEP("init_encounter_status",   init_encounter_status());
    STEP("clear_screen_overlays",   clear_screen_overlays());
    STEP("clear_effect_data",       clear_effect_data());
    STEP("clear_saved_variables",   clear_saved_variables());
    STEP("clear_item_entity_data",  clear_item_entity_data());
    STEP("bgm_reset_sequence_players", bgm_reset_sequence_players());
    STEP("reset_ambient_sounds",    reset_ambient_sounds());
    STEP("sfx_clear_sounds",        sfx_clear_sounds());
    STEP("clear_windows",           clear_windows());
    STEP("initialize_curtains",     initialize_curtains());
    STEP("poll_rumble",             poll_rumble());

    /* M8.7: the tail of the real load_engine_data that this wrap had been
     * dropping. Without set_game_mode(GAME_MODE_STARTUP) the mode slots
     * stay empty after clear_game_mode(), step_game_mode() never runs
     * state_step_startup, and the game idles forever in pre-startup —
     * which is exactly what the M8.6e trace run showed (frame 1411,
     * zero state_step_startup calls).
     *
     * The gGameStatusPtr field writes (musicEnabled etc.) are still
     * skipped: state_step_startup rewrites all the ones needed to reach
     * the logos screen, and pulling common_structs.h into this shim
     * isn't worth it yet. Revisit if title-screen behavior looks off. */
    gOverrideFlags = 0;
    pm3ds_load_engine_status_fields();
    gTimeFreezeMode = 0;                 /* TIME_FREEZE_NONE */
    gGameStepDelayCount = 5;
    gOverrideFlags |= PM3DS_GLOBAL_OVERRIDES_DISABLE_DRAW_FRAME;
    STEP("set_game_mode(STARTUP)",  set_game_mode(PM3DS_GAME_MODE_STARTUP));
#endif

    pm3ds_log(PM3DS_LOG_INFO, "wrap: load_engine_data:done");
}

/* M6.1i: spr_init_sprites identified as a crash site by step-by-step
 * tracing. The real implementation tries to DMA-load sprite asset
 * data from ROM, which on 3DS returns zero bytes, then iterates over
 * what it thinks is a sprite header table — wild pointers /
 * infinite loop result. Replace with a logging no-op so the rest
 * of load_engine_data can continue.
 *
 * M8.3 (2026-05-17): Skipping the ENTIRE function also skipped the
 * `_heap_create(&heap_spriteHead, SPRITE_HEAP_SIZE)` + `imgfx_init()`
 * that runs at the very top of spr_init_sprites. That left
 * ImgFXInstances NULL, and func_8013A4D0 (called from step_game_loop's
 * frame tick via src/101b90_len_8f0.c) crashed Azahar with a flood of
 * unmapped Write32 @ NULL+offset (offsets 0x3C, 0x40, ..., 0x58
 * matched imgfx_init_instance's field writes through a NULL state*).
 *
 * Fix: run the two safe init lines from spr_init_sprites — the heap
 * and imgfx_init — and bail out before the sprite-table DMA loop
 * that does the actual infinite-loop. */
/* M8.6e: instrument game-mode transitions so we can see whether the
 * startup → logos → title progression is actually firing. */
extern void __real_state_step_logos(void);
void __wrap_state_step_logos(void) {
    /* M8.11: natural logo playback (skipLogos hack removed — the title
     * pivot hit nondeterministic hangs from the half-init sprite system).
     * The logos use the pre-loaded logos segment (no asset load, no Yay0,
     * no sprites) drawn as RGBA16 texrects — the simplest path to a real
     * on-screen image. Trace startupState (logo state machine) on change
     * so we can confirm FADE_IN(0)->HOLD(1)->FADE_OUT(2)->... advances. */
    static int last_ss = -999;
    __real_state_step_logos();
    /* M9: advance to the TITLE SCREEN once the N64 logo's HOLD finishes.
     * The fade-in (state 0) completes to HOLD (1); when HOLD's logoTime
     * hits 0 the real step bumps startupState to >=2 (FADE_OUT). At that
     * point — instead of the old M8.11 freeze-clamp — we set the game's own
     * GameStatus.skipLogos flag (@ 0xAE). On the next call the real
     * state_step_logos takes its skipLogos branch:
     *   startup_fade_screen_out(10) -> set_game_mode(GAME_MODE_TITLE_SCREEN)
     * which routes to the title (2D texrects — now working) and BYPASSES
     * GAME_MODE_INTRO (the absolute-pointer hos_05 map that needs the
     * deferred 3D matrix/vtx chain + relocation). One-shot; skipLogos is
     * sticky so the fade + mode transition proceed naturally, and the N64
     * logo still gets its full HOLD first.
     * Every pass through the logos, not just the first boot: Quit on the
     * end-of-chapter save prompt (kmr_24 ExitGame) and the soft reset go
     * back through GAME_MODE_STARTUP, and a one-shot here played the whole
     * story intro there (GitHub #34). state_init_logos clears skipLogos. */
    if (gGameStatusPtr) {
        unsigned char *gs = (unsigned char *)gGameStatusPtr;
        signed char   *ss = (signed char *)&gs[0xAC];   /* GameStatus.startupState */
        if (*ss >= 2 && !gs[0xAE]) {
            gs[0xAE] = 1;                                /* GameStatus.skipLogos = true */
            pm3ds_log(PM3DS_LOG_INFO,
                      "logos: N64 hold done -> skipLogos=1 (-> TITLE_SCREEN)");
        }
        if ((int)*ss != last_ss) {
            last_ss = (int)*ss;
            pm3ds_log(PM3DS_LOG_INFO, "logos: startupState=%d", last_ss);
        }
    }
}

/* M8.10: byte-swap the title screen's TitleDataStruct header after it is
 * Yay0-decompressed. state_init_title_screen does
 *   decode_yay0(titleData, TitleScreen_ImgList);
 *   TitleScreen_ImgList_Logo = TitleScreen_ImgList->logo + (s32)TitleScreen_ImgList;
 * so logo/copyright/pressStart/copyrightPalette are file-RELATIVE s32
 * offsets stored big-endian. We detect this specific decode by dst ==
 * TitleScreen_ImgList and swap the 4 header words; the map-shape decode
 * (absolute pointers, different handling) is left untouched. */
/* M8.10 breadcrumbs: localize the post-init coproc crash (wild PC).
 * Logs how far the title screen progresses: end of init (load_map_bg),
 * first per-frame step, first per-frame draw. */
/* M8.10: SKIP load_map_bg. It does
 *   decode_yay0(load_asset_by_name("title_bg"), &gBackgroundImage)
 * but (a) "title_bg" is not in this ROM's mapfs (the title bg lives
 * elsewhere / under another name) so load_asset_by_name returns junk,
 * and (b) gBackgroundImage is only a 0x10 BackgroundHeader, so decoding
 * a full raster into it overflows — together producing a wild PC
 * (coproc-exception JIT assert in Azahar) right after the logo data is
 * already set up. Skipping the background lets the title LOGO render;
 * the background is deferred until its real asset name + buffer are
 * sorted.
 *
 * M9 CORRECTION: the original note here claimed "gBackgroundImage stays
 * zero, so set_background() leaves backgroundRaster = 0 and the
 * bg-texture pass hits the no-texture path harmlessly". Both halves were
 * wrong, and together they were the title-screen per-frame hang:
 * set_background() sets BACKGROUND_FLAG_TEXTURE unconditionally
 * (src/background.c:51) so a null raster does NOT bypass the textured
 * path, and gBackgroundImage did not stay zero -- it was a 4-byte weak
 * stub read as a 0x10-byte header, so its width/height came from the
 * neighbouring symbols. See m9_background_shim.c for the full writeup and
 * the fix (real backing store + a __wrap_set_background sanity guard). */
extern void __real_load_map_bg(char *name);
/* M9: HOLD the title screen instead of letting it time out.
 *
 * state_step_title_screen counts TitleScreen_TimeLeft (s16, TITLE_START_TIME =
 * 480 on PAL) down each frame; at 0 it goes to TITLE_STATE_BEGIN_DISMISS and
 * sets TitleScreenNextState = NEXT_STATE_DEMO, i.e. the attract-mode demo.
 * That means loading a real map, which walks straight into the unported
 * absolute-pointer map-shape path: the 2026-08-03 run showed the timeout at
 * ~frame 1064 kicking off a decode_yay0 into a garbage destination and 857k
 * unmapped byte writes (an effective hang).
 *
 * Keep the counter off 0 so the title stays up indefinitely. This is the same
 * "hold the screen we just got working" move as the M8.11 logo clamp, and it
 * gives a stable title to develop the renderer against. REMOVE THIS once the
 * demo/intro map path is real -- it suppresses a legitimate transition. */
extern short TitleScreen_TimeLeft;
extern void __real_state_step_title_screen(void);
void __wrap_state_step_title_screen(void) {
    static int first = 0;
    if (!first) { first = 1; pm3ds_log(PM3DS_LOG_INFO, "title: state_step FIRST CALL"); }

#ifdef PM3DS_AUTO_START
    /* Headless testing only: synthesise a START press a few seconds in, so an
     * automated run can exercise the real title -> FILE_SELECT transition
     * (state_title_screen.c:307-312) without a human at the controls.
     *
     * Deliberately injects the BUTTON into GameStatus.pressedButtons[0] rather
     * than poking startupState/TitleScreenNextState directly, so the genuine
     * branch runs — including sfx_play_sound() and bgm_set_song(). Faking the
     * end state would test nothing but my own assignment.
     * Never enabled in the default build: PM3DS_AUTO_START lives only in
     * EXTRA_CFLAGS, like PM3DS_TITLE_DBG. */
    {
        static int steps = 0;
        if (gGameStatusPtr && ++steps == 150) {
            unsigned int *pressed = (unsigned int *)((unsigned char *)gGameStatusPtr + 0x010);
            pressed[0] |= 0x1000u;   /* BUTTON_START (include/enums.h:2776) */
            pm3ds_log(PM3DS_LOG_INFO, "title: AUTO-START injected -> FILE_SELECT");
        }
    }
#endif

    __real_state_step_title_screen();
    /* M14: the title used to be held here (timeout -> intro/DEMO suppressed)
     * while the map path was unported. Maps work now, so the real attract
     * sequence runs: title timeout -> story intro -> DEMO scenes. */
}
extern void __real_appendGfx_title_screen(void);
void __wrap_appendGfx_title_screen(void) {
    static int first = 0;
    if (!first) { first = 1; pm3ds_log(PM3DS_LOG_INFO, "title: appendGfx FIRST CALL"); }
    __real_appendGfx_title_screen();
}

extern void *TitleScreen_ImgList;   /* TitleDataStruct* in the decomp */
extern void __real_decode_yay0(void *src, void *dst);
static unsigned int pm3ds_bswap32(unsigned int v) {
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}
#ifdef PM3DS_MAPSHAPE_DBG
/* M10 forensics: recover the N64 load address the map shape was linked for.
 *
 * A decoded ShapeFile's header holds ABSOLUTE N64 RDRAM pointers (0x80xxxxxx)
 * baked for wherever the N64 put gMapShapeData. To use the data on 3DS every
 * pointer needs p_port = p_n64 - B_n64 + &gMapShapeData, applied recursively —
 * and the one number that unblocks all of it is B_n64, which is auto-laid BSS
 * and therefore NOT in ver/pal/symbol_addrs.txt.
 *
 * It does not have to be guessed. The header is at offset 0 of the blob, so on
 * N64 B_n64 == &gMapShapeData, and every header pointer must satisfy
 *     B <= p < B + dsize.
 * That alone pins B to a window; we then SELF-VALIDATE each candidate by
 * rebasing modelNames and checking its entries land on printable ASCII inside
 * the blob. Only the true base survives. Read-only: logs, changes nothing. */
extern unsigned char gMapShapeData[];

static unsigned int pm3ds_be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8)  | (unsigned)p[3];
}

#ifdef PM3DS_TREEWALK
/* DEPTH-1 walk only, deliberately tiny.
 *
 * The full recursive traversal (explicit stack, nested loops) reproducibly
 * produced a 3DSX that Azahar refuses to load -- "Failed to load ROM (Error 1)"
 * -- while an otherwise identical build with the loop removed loads fine. An
 * A/B/A/B test alternated 0/1/0/1, so it is the file, not the environment; but
 * the 3DSX header, segment sizes and relocation counts all look normal, so the
 * cause is some opaque toolchain/loader interaction rather than anything wrong
 * with the walk. Not worth chasing for a diagnostic: this smaller version
 * validates the same critical pointer chain
 *     root -> ModelGroupData -> childList[] -> child ModelNode
 * which is exactly what the relocator must traverse. Node types are
 * MODEL=2 GROUP=5 ROOT=7 SPECIAL_GROUP=10 (include/model.h:152-155). */
__attribute__((noinline))
static void pm3ds_walk_tree(const unsigned char *blob, unsigned int dsize,
                            unsigned int base, unsigned int root_ptr) {
    unsigned int root_off = root_ptr - base;
    if (root_off + 0x14u > dsize) return;

    unsigned int gdp = pm3ds_be32(blob + root_off + 0x10);
    if ((gdp >> 24) != 0x80) return;
    unsigned int gd_off = gdp - base;
    if (gd_off + 0x14u > dsize) return;

    unsigned int nchild = pm3ds_be32(blob + gd_off + 0x0C);
    unsigned int clp    = pm3ds_be32(blob + gd_off + 0x10);
    if ((clp >> 24) != 0x80) return;
    unsigned int cl_off = clp - base;
    if (cl_off + 4u > dsize) return;

    unsigned int ok = 0, t0 = 0xFFFFu;
    unsigned int lim = nchild > 8u ? 8u : nchild;
    for (unsigned int c = 0; c < lim; c++) {
        unsigned int cp = pm3ds_be32(blob + cl_off + c * 4);
        if ((cp >> 24) != 0x80) continue;
        unsigned int c_off = cp - base;
        if (c_off + 0x14u > dsize) continue;
        unsigned int ty = pm3ds_be32(blob + c_off + 0x00);
        if (c == 0) t0 = ty;
        if (ty == 2u || ty == 5u || ty == 7u || ty == 10u) ok++;
    }
    pm3ds_log(PM3DS_LOG_INFO,
              "MAPSHAPE: DEPTH1 numChildren=%u checked=%u validTypes=%u firstChildType=%u",
              nchild, lim, ok, t0);
}
#endif

static void pm3ds_mapshape_forensics(const void *src, const unsigned char *blob) {
    /* Yay0 header: "Yay0", then decompressed size big-endian at +4. */
    unsigned int dsize = pm3ds_be32((const unsigned char *)src + 4);
    unsigned int hdr[5];
    for (int i = 0; i < 5; i++) hdr[i] = pm3ds_be32(blob + i * 4);

    pm3ds_log(PM3DS_LOG_INFO,
              "MAPSHAPE: dsize=0x%X root=0x%08X vtxTable=0x%08X modelNames=0x%08X "
              "colliderNames=0x%08X zoneNames=0x%08X",
              dsize, hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]);

    unsigned int lo = 0xFFFFFFFFu, hi = 0;
    int plausible = 0;
    for (int i = 0; i < 5; i++) {
        if ((hdr[i] >> 24) != 0x80) continue;      /* not an RDRAM pointer */
        plausible++;
        if (hdr[i] < lo) lo = hdr[i];
        if (hdr[i] > hi) hi = hdr[i];
    }
    /* Sanity-bound against the PORT's backing store, not the decomp's
     * SHAPE_SIZE_LIMIT (0x8000). Measured PAL shapes are 0x13AE0, i.e. the
     * decomp constant is far too small and the port enlarges gMapShapeData in
     * m9_data_syms.c; bounding by 0x8000 here just rejected every real shape. */
    if (plausible < 2 || dsize == 0 || dsize > 0x40000u) {
        pm3ds_log(PM3DS_LOG_WARN,
                  "MAPSHAPE: not a shape file (plausible=%d dsize=0x%X)",
                  plausible, dsize);
        return;
    }

    /* B must satisfy B <= lo and hi < B + dsize. */
    unsigned int b_lo = (hi >= dsize) ? (hi - dsize + 1u) : 0u;
    unsigned int b_hi = lo;
    pm3ds_log(PM3DS_LOG_INFO, "MAPSHAPE: base window [0x%08X .. 0x%08X] (%u candidates)",
              b_lo, b_hi, (b_hi >= b_lo) ? ((b_hi - b_lo) / 4u + 1u) : 0u);

    /* Self-validate: rebase modelNames and require its first entries to point at
     * printable ASCII inside the blob. */
    unsigned int found = 0, hits = 0;
    for (unsigned int b = b_lo & ~3u; b <= b_hi; b += 4) {
        unsigned int names_off = hdr[2] - b;
        if (names_off + 8u > dsize) continue;
        int ok = 1;
        for (int n = 0; n < 2 && ok; n++) {
            unsigned int sp = pm3ds_be32(blob + names_off + n * 4);
            unsigned int soff = sp - b;
            if ((sp >> 24) != 0x80 || soff >= dsize) { ok = 0; break; }
            /* first char must be printable */
            unsigned char c = blob[soff];
            if (c < 0x20 || c > 0x7E) ok = 0;
        }
        if (ok) { hits++; if (!found) found = b; }
    }
    if (hits == 1) {
        pm3ds_log(PM3DS_LOG_INFO,
                  "MAPSHAPE: *** B_n64 = 0x%08X (unique) -> reloc delta = %p - B ***",
                  found, (void *)gMapShapeData);
    } else {
        pm3ds_log(PM3DS_LOG_INFO,   /* expected for many maps; the relocation proof below decides */
                  "MAPSHAPE: base not unique (hits=%u, first=0x%08X) — widen validation",
                  hits, found);
    }

    /* PROOF-OF-RELOCATION. Everything above is arithmetic on a header; this
     * actually applies `p_port = p_n64 - B + blob` to the modelNames array and
     * reads the strings back. If they come out as legible model names, then the
     * base AND the big-endian handling are both correct on real data — which is
     * the thing worth knowing before writing the full recursive walk.
     * Read-only: nothing is written back, so a wrong base just prints garbage
     * instead of corrupting the blob. */
    {
        unsigned int base = (hits == 1) ? found : 0x80210000u;  /* heaps.c layout */
        unsigned int names_off = hdr[2] - base;
        if (names_off + 4u > dsize) {
            pm3ds_log(PM3DS_LOG_WARN, "MAPSHAPE: modelNames offset 0x%X out of range",
                      names_off);
            return;
        }
        pm3ds_log(PM3DS_LOG_INFO, "MAPSHAPE: relocating modelNames with B=0x%08X ...", base);
        for (int n = 0; n < 6; n++) {
            unsigned int sp = pm3ds_be32(blob + names_off + n * 4);
            unsigned int soff = sp - base;
            if ((sp >> 24) != 0x80 || soff >= dsize) {
                pm3ds_log(PM3DS_LOG_INFO, "MAPSHAPE:   name[%d] end/invalid (0x%08X)", n, sp);
                break;
            }
            char tmp[32];
            unsigned int k = 0;
            while (k < sizeof(tmp) - 1 && soff + k < dsize && blob[soff + k]) {
                char c = (char)blob[soff + k];
                tmp[k++] = (c >= 0x20 && c <= 0x7E) ? c : '?';
            }
            tmp[k] = '\0';
            pm3ds_log(PM3DS_LOG_INFO, "MAPSHAPE:   name[%d] @0x%05X = \"%s\"", n, soff, tmp);
        }
    }

#ifdef PM3DS_TREEWALK
    /* Structural walk of the ModelNode tree — READ-ONLY. Proving modelNames
     * rebases correctly only exercises one pointer hop; the relocator must
     * traverse root -> ModelGroupData -> childList[] -> ... , so validate the
     * whole tree is reachable under the same rule BEFORE writing anything back.
     * bad=0 would confirm the format model and the relocation rule together. */
    pm3ds_walk_tree(blob, dsize, 0x80210000u, hdr[0]);
#endif /* PM3DS_TREEWALK */
}
#endif /* PM3DS_MAPSHAPE_DBG */

void __wrap_decode_yay0(void *src, void *dst) {
#ifdef PM3DS_YAY0_DBG
    /* Log every decode BEFORE it runs. 855,943 unmapped writes were traced to
     * yay0.c:53 (`dst[outPos++] = ...`), i.e. some call passes a destination
     * that is not real memory — the same undersized/garbage-dst class as the
     * gMapShapeData 0x8000-vs-0x13AE0 bug. Logging after the fact is useless
     * because the bad decode is what floods; this names the culprit up front. */
    { static int n = 0;
      if (n < 24) { n++;
          unsigned int ds = pm3ds_be32((const unsigned char *)src + 4);
          pm3ds_log(PM3DS_LOG_INFO, "yay0 #%d: src=%p dst=%p dsize=0x%X", n, src, dst, ds);
      } }
#endif
    /*
     * SANITY GATE — always on, not debug-gated, because this one prevents
     * memory corruption rather than merely reporting it.
     *
     * MEASURED: the 4th decode of a map load is
     *     src=0xc12e130 dst=0x0 dsize=0x88E1120C
     * i.e. a NULL destination and a 2.3 GB "decompressed size" read out of what
     * is plainly not a Yay0 header. The decoder then walks
     * `dst[outPos++] = ...` from address 0, which is precisely the 855,943
     * unmapped writes traced to yay0.c:53. Whatever asset lookup produced that
     * src is broken (unported path returning junk), but the decompressor should
     * never be the thing that scribbles over the address space because of it.
     *
     * Yay0 header: 'Y''a''y''0', then decompressed size big-endian at +4.
     */
    {
        const unsigned char *h = (const unsigned char *)src;
        /* Read the size inline rather than via pm3ds_be32(), which only exists
         * under PM3DS_MAPSHAPE_DBG — this gate must be in every build. */
        unsigned int ds = src ? (((unsigned)h[4] << 24) | ((unsigned)h[5] << 16) |
                                 ((unsigned)h[6] << 8)  |  (unsigned)h[7]) : 0u;
        int bad_magic = !src || h[0] != 'Y' || h[1] != 'a' || h[2] != 'y' || h[3] != '0';
        /* 4 MB ceiling: the largest real asset seen is the 0x13AE0 map shape. */
        int bad_size  = (ds == 0u) || (ds > 0x400000u);
        if (!dst || bad_magic || bad_size) {
            pm3ds_warn("decode_yay0 REFUSED: src=%p dst=%p dsize=0x%X%s%s%s",
                       src, dst, ds,
                       !dst ? " [null-dst]" : "",
                       bad_magic ? " [bad-magic]" : "",
                       bad_size ? " [bad-size]" : "");
            return;
        }
    }

    __real_decode_yay0(src, dst);
    { extern void gfx_tex_invalidate_range(const void *addr, unsigned int len);
      const unsigned char *h = (const unsigned char *)src;
      gfx_tex_invalidate_range(dst, ((unsigned)h[4] << 24) | ((unsigned)h[5] << 16) | ((unsigned)h[6] << 8) | h[7]); }
#ifdef PM3DS_MAPSHAPE_DBG
    if (dst && dst == (void *)gMapShapeData) {
        pm3ds_mapshape_forensics(src, (const unsigned char *)dst);
#ifdef PM3DS_SHAPE_RELOC
        /* Must run AFTER the forensics, which read the raw big-endian form.
         * Exactly once per decode — relocating twice would rebase already-host
         * pointers into nonsense. */
        { extern void pm3ds_shape_relocate(void *blob, unsigned int dsize);
          unsigned int dsize = pm3ds_be32((const unsigned char *)src + 4);
          pm3ds_shape_relocate(dst, dsize); }
#endif
    }
#endif
    if (dst && dst == TitleScreen_ImgList) {
        unsigned int *h = (unsigned int *)dst;
        h[0] = pm3ds_bswap32(h[0]);  /* logo */
        h[1] = pm3ds_bswap32(h[1]);  /* copyright */
        h[2] = pm3ds_bswap32(h[2]);  /* pressStart */
        h[3] = pm3ds_bswap32(h[3]);  /* copyrightPalette */
        pm3ds_log(PM3DS_LOG_INFO,
                  "title: swapped ImgList header logo=0x%x copy=0x%x press=0x%x",
                  h[0], h[1], h[2]);
#ifdef PM3DS_TITLE_DBG
        { extern int pm3ds_title_dbg; pm3ds_title_dbg = 1; }  /* arm per-frame breadcrumbs */
#endif
    }
}
extern void __real_state_drawUI_logos(void);
void __wrap_state_drawUI_logos(void) {
    static int first = 0;
    if (!first) { first = 1;
        pm3ds_log(PM3DS_LOG_INFO, "wrap: state_drawUI_logos FIRST CALL");
    }
    __real_state_drawUI_logos();
}
extern void __real_state_step_startup(void);
void __wrap_state_step_startup(void) {
    static int call_count = 0;
    if (call_count < 4) {
        pm3ds_log(PM3DS_LOG_INFO, "wrap: state_step_startup call=%d (enter) gGameStatusPtr=%p",
                  call_count, gGameStatusPtr);
    }
    __real_state_step_startup();
    if (call_count < 4) {
        pm3ds_log(PM3DS_LOG_INFO, "wrap: state_step_startup call=%d (returned) gGameStatusPtr=%p",
                  call_count, gGameStatusPtr);
    }
    call_count++;
}

/* M8.7: the decomp audio engine (au_* / gSoundGlobals / gBGMPlayerA) is
 * never initialized on this port — M5 replaced it with our own NDSP/CSND
 * .bgm player. state_step_startup's bgm_init_music_players ->
 * bgm_set_song chain dereferences the NULL gBGMPlayerA (unmapped reads
 * at 0x5C/0x8/0x60) and its downstream writes through garbage pointers
 * corrupted .data — observed as gGameStatusPtr reading 0 by the time
 * state_step_startup hit snd_set_stereo. Cut the chain at the three
 * entry points the boot state machine calls; the port-side song player
 * gets wired to these in the audio-integration milestone. */
/* bgm_set_song gets INLINED into bgm_init_music_players (same TU, -Os),
 * so --wrap=bgm_set_song alone can't intercept that call site — the
 * crash flood persisted. Wrap the outer function too; that call comes
 * from state_startup.c, cross-TU, where inlining can't bypass the
 * linker. */
/* M8.7 forensics: gGameStatusPtr reads as NULL by state_startup.c:77,
 * and the only call between the last known-good write (line 72) and
 * the fault is fio_load_globals. Bracket it to catch the corruption. */
extern int __real_fio_load_globals(void);
int __wrap_fio_load_globals(void) {
    int ret;
    pm3ds_log(PM3DS_LOG_INFO, "fio_load_globals: gGameStatusPtr pre=%p",
              gGameStatusPtr);
    ret = __real_fio_load_globals();
    pm3ds_log(PM3DS_LOG_INFO, "fio_load_globals: gGameStatusPtr post=%p ret=%d",
              gGameStatusPtr, ret);
    return ret;
}

int __wrap_bgm_init_music_players(void) {
    pm3ds_log(PM3DS_LOG_INFO, "wrap: bgm_init_music_players stubbed (au engine not init)");
    return 1;
}

int __wrap_bgm_set_song(int playerIndex, int songID, int variation,
                        int fadeOutTime, short volume) {
    static int first = 0;
    (void)playerIndex; (void)songID; (void)variation;
    (void)fadeOutTime; (void)volume;
    if (!first) { first = 1;
        pm3ds_log(PM3DS_LOG_INFO, "wrap: bgm_set_song stubbed (au engine not init)");
    }
    return 1;
}
void __wrap_snd_set_stereo(void) {
    pm3ds_log(PM3DS_LOG_INFO, "wrap: snd_set_stereo stubbed");
}
void __wrap_snd_set_mono(void) {
    pm3ds_log(PM3DS_LOG_INFO, "wrap: snd_set_mono stubbed");
}

extern void __real_spr_init_sprites(int);

/* From src/sprite.c:777-778 — first two lines of the real impl.
 * Use opaque types so we don't have to drag include/common_structs.h
 * into the port-side shim. SPRITE_HEAP_SIZE comes from include/macros.h. */
struct HeapNode;
extern struct HeapNode heap_spriteHead;
extern void _heap_create(struct HeapNode *heap, int size);
extern void imgfx_init(void);
extern unsigned char SpriteUseGeneralHeap; /* `bool` in decomp */
#define PM3DS_SPRITE_HEAP_SIZE 0x40000     /* SPRITE_HEAP_SIZE = 256 KiB */

void __wrap_spr_init_sprites(int playerSpriteSet) {
    pm3ds_log(PM3DS_LOG_INFO, "wrap: spr_init_sprites partial");
    SpriteUseGeneralHeap = 0;
    _heap_create(&heap_spriteHead, PM3DS_SPRITE_HEAP_SIZE);
    imgfx_init();

#ifdef PM3DS_SPRITE_TABLE
    /* M10: populate the sprite ROM table.
     *
     * spr_init_player_raster_cache() is what runs
     *     nuPiReadRom(SPRITE_ROM_START, &SpriteDataHeader, sizeof(...))
     * and then rebases SpriteDataHeader[0..2] by SPRITE_ROM_START. Skipping it
     * left SpriteDataHeader = {0,0,0}, so spr_load_sprite computed
     * `base = SpriteDataHeader[1]` = 0, read the "asset table" from ROM offset
     * 0, got garbage sizes, malloc'd NULL and handed decode_yay0 a null
     * destination — the measured 859,731 unmapped accesses at
     * 101b90_len_8f0.c:114.
     *
     * This only became worth calling once SPRITE_ROM_START was fixed to a real
     * cart offset (PM3DS_PORT_ROM_ADDRS); under -DSHIFT it was the address of a
     * weak linker stub and reading from it would have been just as wrong.
     *
     * The cacheSize/rasterSize normally come from PlayerSpriteSets[set]; that
     * table is ROM data we may not have yet, so pass the modest fixed values
     * below and let it fail loudly rather than silently mis-size the cache. */
    { extern void spr_init_player_raster_cache(int cacheSize, int maxRasterSize);
      extern int SpriteDataHeader[3];
      extern int pm3ds_pi_read(unsigned int addr, void *dst, unsigned int size);
      static int done = 0;

      spr_init_player_raster_cache(8, 0x4000);

      /* The table is BIG-ENDIAN cart data, and spr_init_player_raster_cache
       * stores it raw then adds SPRITE_ROM_START. Measured: the ROM bytes are
       * `00 00 00 10 | 00 09 D9 60 | ...` (i.e. 0x10, 0x9D960) but the header
       * came out {0x11DF0010, 0x62B80910, 0xAA160A10} — the little-endian
       * misreads plus the base. Same endian defect as the mapfs asset table
       * (fixed in M8.9 by swapping in-buffer) and the Yay0 headers.
       *
       * Re-read the 3 words ourselves, byte-swap, then apply the base, so
       * SpriteDataHeader ends up with genuine cart offsets. */
      {
          unsigned char raw[12];
          const unsigned int rom = PM3DS_ROM_SPRITES + 0x10u;   /* SPRITE_ROM_START */
          if (pm3ds_pi_read(rom, raw, sizeof(raw)) == 0) {
              for (int i = 0; i < 3; i++) {
                  unsigned int be = ((unsigned)raw[i * 4 + 0] << 24) |
                                    ((unsigned)raw[i * 4 + 1] << 16) |
                                    ((unsigned)raw[i * 4 + 2] << 8)  |
                                     (unsigned)raw[i * 4 + 3];
                  SpriteDataHeader[i] = (int)(be + rom);
              }
          }
      }

      /* spr_init_sprites runs more than once; without this the second call
       * re-adds the base and doubles every entry (observed 0x01DF0010 ->
       * 0x03BE0020). Log once so the values are checkable. */
      if (!done) {
          done = 1;
          pm3ds_log(PM3DS_LOG_INFO,
                    "wrap: SpriteDataHeader = {0x%08X, 0x%08X, 0x%08X} (byte-swapped)",
                    (unsigned)SpriteDataHeader[0], (unsigned)SpriteDataHeader[1],
                    (unsigned)SpriteDataHeader[2]);
      } }
#else
    (void)playerSpriteSet;
    pm3ds_log(PM3DS_LOG_INFO, "wrap: heap_spriteHead + imgfx_init done; sprite-table DMA loop SKIPPED");
#endif
}
