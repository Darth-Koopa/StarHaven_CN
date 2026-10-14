/*
 * m9_data_syms.c -- real storage for decomp DATA symbols whose defining
 * translation unit is excluded from the port build.
 *
 * THE DEFECT CLASS THIS CLOSES
 * ----------------------------
 * tools/gen_m6_link_stubs.py emits
 *
 *     __attribute__((weak)) void SYM(void) {}
 *
 * for every unresolved symbol, with NO data/function distinction (the
 * generator's own docstring admits this). For a *function* that is a harmless
 * no-op. For a *data* symbol it is a trap: the name ends up as 4 bytes of ARM
 * code in read-only .text, so
 *
 *   - reads return the instruction encoding (`bx lr` = 0xE12FFF1E), and if the
 *     declared type is bigger than 4 bytes you read your *neighbours'* code as
 *     your struct fields;
 *   - writes target read-only .text -> data abort on real hardware.
 *
 * This is not hypothetical: it is exactly what caused the M9 title-screen hang.
 * `gBackgroundImage` (0x10-byte BackgroundHeader) resolved to a 4-byte stub, so
 * its width/height were read out of the three following stubs, came back as
 * s16 -226 and -7889, and drove a display-list loop 876 iterations long into a
 * 512-entry buffer. See m9_background_shim.c for the full writeup.
 *
 * HOW THE LIST BELOW WAS FOUND (re-run this after regenerating the stubs):
 *
 *   grep -oP '(?<=^__attribute__\(\(weak\)\) void )\w+' \
 *       port/3ds/source/n64_shim/m6_link_stubs.c | sort -u > /tmp/stubs.txt
 *   find include -name '*.h' -print0 | xargs -0 cat \
 *     | grep -E '^\s*extern\s' | grep -v '(' \
 *     | grep -oE '\b\w+\s*(\[[^]]*\])?\s*;' | grep -oE '^\w+' | sort -u > /tmp/data.txt
 *   comm -12 /tmp/stubs.txt /tmp/data.txt
 *
 * That intersection is "decomp DATA symbols currently living in .text". As of
 * 2026-08-03 it returned exactly four: gBackgroundImage (fixed in
 * m9_background_shim.c) plus the three defined here. The grep is heuristic --
 * it only sees `extern` declarations in the include/ headers, so re-check it whenever
 * the excluded-TU list in the Makefile changes.
 *
 * This TU deliberately does NOT include the decomp headers, so the sizes here
 * can never be checked against the real typedefs by the compiler. Each one
 * therefore cites its type and size explicitly -- keep them in sync by hand.
 */

/*
 * u32 gRandSeed -- include/variables.h.
 * Strong definition is src/43F0.c:28 (`u32 gRandSeed = 1;`), and 43F0.c is in
 * the Makefile's M4_EXCLUDE list, so the weak stub was winning.
 * WRITTEN at src/main.c:97 (`gRandSeed += osGetCount();`) during boot_main --
 * i.e. this port has been doing a read-modify-write into .text on every boot.
 * Azahar tolerates it; real hardware maps .text R-X and would fault.
 * Initialised to 1 to match the decomp's definition exactly.
 *
 * NOTE: rand_int()/rand_float() also live in 43F0.c and are still weak no-op
 * stubs, so the RNG does not actually advance yet. Fixing that is a separate
 * job -- this only makes the storage real and writable.
 */
/* gRandSeed now comes from src/43F0.c (M11 compiles it, real RNG). */

/*
 * HeapNode heap_battleHead -- include/common_structs.h:170-176, size = 0x10
 * (next ptr, u32 length, u16 allocated, u16 entryID, u32 capacity).
 * Defined in src/heap.c, which is in M4_EXCLUDE.
 *
 * PRE-EMPTIVE, NOT a live bug today -- I checked rather than assumed. The only
 * two referrers are src/heap.c (excluded) and src/battle_heap_create_obfuscated.c
 * :51, and that reference sits in the `#else` of an `#ifdef SHIFT` while the port
 * builds -DSHIFT=1 (port/3ds/Makefile:184), so it is preprocessed out. Nothing in
 * the current link touches the symbol, and --gc-sections duly strips this
 * definition from the ELF.
 *
 * It is defined here anyway so that when the battle heap is eventually wired up
 * it binds to real writable .bss instead of silently reappearing as a .text stub.
 * Zero is the correct pre-init state; _heap_create() fills it in.
 */
/* M11.1: the real arena (BATTLE_HEAP_SIZE, 16-aligned) now lives in heap_impl.c. */

/*
 * OSViMode osViModeFpalLpn1 -- include/PR/os_vi.h:81-85.
 * size = 0x4C: u8 type (padded to 4) + OSViCommonRegs (8 x u32 = 0x20)
 *              + OSViFieldRegs[2] (2 x 5 x u32 = 0x28).
 * Defined in src/os/vimodefpallpn1.c, which is not in the port's SOURCES.
 * Read (not written) via src/main.c. The port emulates VI in the shim layer, so
 * the register values are never programmed into real hardware and zeros are
 * harmless -- but zeros are still strictly better than reading instruction
 * bytes, and it takes the symbol out of .text.
 */
unsigned char osViModeFpalLpn1[0x4C] __attribute__((aligned(4))) = { 0 };
/* the US boot_main picks one of these by osTvType (NTSC or MPAL) */
unsigned char osViModeNtscLan1[0x4C] __attribute__((aligned(4))) = { 0 };
unsigned char osViModeMpalLan1[0x4C] __attribute__((aligned(4))) = { 0 };

/*
 * NOTE on gMapShapeData: it is NOT defined here. It has a real decomp
 * definition (`BSS ShapeFile gMapShapeData;`, src/heaps.c:18) so a second one
 * collides at link time. Its SIZE problem is fixed instead by port-gating
 * SHAPE_SIZE_LIMIT in include/model.h -- see the comment there. Measured PAL
 * shapes are 0x13AE0 bytes against a nominal limit of 0x8000.
 */

/*
 * Audio engine globals -- src/audio.h:1413-1416. These are POINTERS:
 *     extern SoundManager*   gSoundManager;      // SoundManager  size = 0x6CC
 *     extern BGMPlayer*      gBGMPlayerA, *gBGMPlayerB;   // BGMPlayer size = 0xA9C
 *     extern AmbienceManager* gAuAmbienceManager;
 *
 * FOUND THE HARD WAY (2026-08-03 title run): my first audit only scanned
 * include/, but these live in src/audio.h, so they stayed weak FUNC stubs.
 * Reading the stub yields the instruction word 0xE12FFF1E *as a pointer*, and
 * au_sfx_set_state() (src/audio/sfx_player.c:743) then does `manager->state =
 * state` -> a write to 0xE12FFFDE. The log shows exactly that as the first
 * unmapped access of the run. Reads of a stubbed data symbol are bad; reads of
 * a stubbed *pointer* are worse, because the garbage propagates into a store.
 *
 * The decomp's au_* engine is never initialised on this port (M5 audio is a
 * separate NDSP path, and bgm_init_music_players is --wrap'd to a no-op), so
 * nothing legitimately fills these in. Pointing them at NULL would just convert
 * the stray write into a null-deref crash, so instead give each a real zeroed
 * backing object: the writes then land in valid, harmless .bss and the audio
 * code no-ops its way through instead of corrupting memory.
 *
 * Sizes are the decomp's own `// size = ...` comments. AmbienceManager carries
 * no size comment, so it gets a deliberately generous block -- over-allocating
 * is safe here (it only costs .bss), under-allocating would recreate the exact
 * overrun-into-the-next-symbol bug this file exists to prevent.
 */
static unsigned char s_sound_manager[0x6CC] __attribute__((aligned(8))) = { 0 };
static unsigned char s_bgm_player_a[0xA9C] __attribute__((aligned(8))) = { 0 };
static unsigned char s_bgm_player_b[0xA9C] __attribute__((aligned(8))) = { 0 };
static unsigned char s_ambience_mgr[0x1000] __attribute__((aligned(8))) = { 0 };

void *gSoundManager      = s_sound_manager;
void *gBGMPlayerA        = s_bgm_player_a;
void *gBGMPlayerB        = s_bgm_player_b;
void *gAuAmbienceManager = s_ambience_mgr;
