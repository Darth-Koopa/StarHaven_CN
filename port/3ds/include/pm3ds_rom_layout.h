/*
 * pm3ds_rom_layout.h — where the port finds things in the ROM it is built for.
 *
 * Cart offsets from ver/<version>/splat.yaml, for the data the port reads
 * straight from the ROM instead of through the decomp's link stubs. Only
 * #defines: assembly and the decomp's C include it too. Build with exactly
 * one of VERSION_PAL, VERSION_US, VERSION_JP or VERSION_IQUE enabled.
 */
#ifndef PM3DS_ROM_LAYOUT_H
#define PM3DS_ROM_LAYOUT_H

#if VERSION_PAL
#define PM3DS_ROM_ICON          0x1DA1E0    /* HUD icons */
#define PM3DS_ROM_LOGOS         0x20C080    /* 0x1B000 bytes */
#define PM3DS_ROM_TITLE_BG_1    0x227080    /* story pages; tape at +0x2A440 */
#define PM3DS_ROM_IMGFX_DATA    0x2705A0
#define PM3DS_ROM_SHADING       0x33A930    /* sprite_shading_profiles */
#define PM3DS_ROM_AUDIO         0x13A0000   /* SBN */
#define PM3DS_ROM_AUDIO_END     0x1DE2C40   /* kept in RAM (n64_shim/rom_stream.c) */
#define PM3DS_ROM_SPRITES       0x1DF0000
#define PM3DS_ROM_MAPFS         0x2600000
#define PM3DS_ROM_MAPFS_END     0x2FD7362
/* the game's data: 64 MB dumps repeat 0x2000000-0x3000000 after it */
#define PM3DS_ROM_DATA_SIZE     0x3000000
#define PM3DS_ROM_CRC32         0x9FC00CE3  /* of PM3DS_ROM_DATA_SIZE bytes in .z64 order */
#define PM3DS_ROM_NAME          "Paper Mario (Europe)"
#define PM3DS_ROM_SIZES         "48 or 64 MB"   /* the dump sizes players may have */
#define PM3DS_ROM_GAME_ID       "NMQP"          /* N64 header 0x3B: game code + region */
#define PM3DS_ROM_GAME_ID_LABEL "NMQP (Europe)"
#define PM3DS_SAVE_NAME         "save.fla"
#define PM3DS_ROMDATA_PATH      "romfs:/romdata_eu.bin"   /* tools/romdata.py's table */
#elif VERSION_US
#define PM3DS_ROM_ICON          0x1CC310
#define PM3DS_ROM_LOGOS         0x1FE1B0
#define PM3DS_ROM_TITLE_BG_1    0x2191B0
#define PM3DS_ROM_IMGFX_DATA    0x24B7F0
#define PM3DS_ROM_SHADING       0x315B80
#define PM3DS_ROM_AUDIO         0xF00000
#define PM3DS_ROM_AUDIO_END     0x1942C40
#define PM3DS_ROM_SPRITES       0x1943000
#define PM3DS_ROM_MAPFS         0x1E40000
#define PM3DS_ROM_MAPFS_END     0x27FEE22
#define PM3DS_ROM_MSG           0x1B83000   /* PAL has one block per language */
#define PM3DS_ROM_DATA_SIZE     0x2800000
#define PM3DS_ROM_CRC32         0xA7F5CD7E  /* the whole 40 MB, .z64 order */
#define PM3DS_ROM_NAME          "Paper Mario (USA)"
#define PM3DS_ROM_SIZES         "40 MB"
#define PM3DS_ROM_GAME_ID       "NMQE"
#define PM3DS_ROM_GAME_ID_LABEL "NMQE (USA)"
#define PM3DS_SAVE_NAME         "save_us.fla"   /* PAL saves have another layout */
#define PM3DS_ROMDATA_PATH      "romfs:/romdata_us.bin"
#elif VERSION_IQUE
#define PM3DS_ROM_ICON          0x215390    /* HUD icons */
#define PM3DS_ROM_LOGOS         0x247230    /* 0x1B000 bytes */
#define PM3DS_ROM_TITLE_BG_1    0x262230    /* story pages; tape at +0x2A440 */
#define PM3DS_ROM_IMGFX_DATA    0x294870
#define PM3DS_ROM_SHADING       0x35EC00    /* sprite_shading_profiles */
#define PM3DS_ROM_AUDIO         0xF00000    /* SBN */
#define PM3DS_ROM_AUDIO_END     0x1942C40   /* kept in RAM (n64_shim/rom_stream.c) */
#define PM3DS_ROM_SPRITES       0x1943000
#define PM3DS_ROM_MAPFS         0x1E40000
#define PM3DS_ROM_MAPFS_END     0x27FEE1E
#define PM3DS_ROM_MSG           0x1B83000   /* native iQue Chinese message table */
#define PM3DS_ROM_DATA_SIZE     0x2800000   /* 40 MiB */
#define PM3DS_ROM_CRC32         0x18C02AD5  /* canonical .z64 order */
#define PM3DS_ROM_NAME          "Paper Mario (iQue China)"
#define PM3DS_ROM_SIZES         "40 MB"
/* The official iQue dump has a blank 4-byte game-code/region field. The
 * CRC32 below covers its canonical first 40 MiB; do not stamp a fake ID. */
#define PM3DS_ROM_GAME_ID       "\0\0\0\0"
#define PM3DS_ROM_GAME_ID_LABEL "the blank iQue game-ID field"
#define PM3DS_SAVE_NAME         "save_ique.fla"
#define PM3DS_ROMDATA_PATH      "romfs:/romdata_ique.bin"
#elif VERSION_JP
/* from ver/jp/splat.yaml; the CRC is filled in once a JP ROM has been checked */
#define PM3DS_ROM_ICON          0x1D4720
#define PM3DS_ROM_LOGOS         0x2065C0
#define PM3DS_ROM_TITLE_BG_1    0x2215C0
#define PM3DS_ROM_IMGFX_DATA    0x253C00
#define PM3DS_ROM_SHADING       0x31DF90
#define PM3DS_ROM_AUDIO         0xFC0000
#define PM3DS_ROM_AUDIO_END     0x1A01AF0
#define PM3DS_ROM_SPRITES       0x1A40000
#define PM3DS_ROM_MAPFS         0x1E00000
#define PM3DS_ROM_MAPFS_END     0x27BCB14
#define PM3DS_ROM_MSG           0x1D40000
#define PM3DS_ROM_DATA_SIZE     0x2800000
#define PM3DS_ROM_CRC32         0           /* unknown yet: 0 = unchecked */
#define PM3DS_ROM_NAME          "Mario Story (Japan)"
#define PM3DS_ROM_SIZES         "40 MB"
#define PM3DS_ROM_GAME_ID       "NMQJ"
#define PM3DS_ROM_GAME_ID_LABEL "NMQJ (Japan)"
#define PM3DS_SAVE_NAME         "save_jp.fla"   /* JP saves have another layout */
#define PM3DS_ROMDATA_PATH      "romfs:/romdata_jp.bin"
#else
#error "the 3DS port is built for VERSION_PAL, VERSION_US, VERSION_IQUE or VERSION_JP"
#endif

/* cart-tagged offset, as the decomp's DMA code takes it */
#define PM3DS_CART(off)         (0x10000000 + (off))
#define PM3DS_ICON_ROM_ADDR(offset) (PM3DS_CART(PM3DS_ROM_ICON) + (offset))

#endif
