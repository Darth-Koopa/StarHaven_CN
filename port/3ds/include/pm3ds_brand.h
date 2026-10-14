/*
 * pm3ds_brand.h — the port's name and its folder on the SD card.
 *
 * Kept free of Nintendo's names and trademarks; the game itself is only named
 * where the player is told which ROM to supply. Change the name here, in
 * cia/app.rsf, in the Makefile's APP_* lines and in tools/make_branding.py.
 */
#ifndef PM3DS_BRAND_H
#define PM3DS_BRAND_H

#define PM3DS_APP_NAME       "Star Haven"
#define PM3DS_APP_NAME_CAPS  "STAR HAVEN"

/* release version: shown on the bottom screen and in the log; the CIA's
 * title version comes from these numbers too (tools/package.sh) */
#define PM3DS_VERSION_MAJOR  1
#define PM3DS_VERSION_MINOR  0
#define PM3DS_VERSION_MICRO  2
#define PM3DS_VERSION_STR    "1.0.1"

/* save, settings, log and the player's ROM */
#define PM3DS_SD_DIR         "sdmc:/3ds/StarHaven"
#define PM3DS_SD_DIR_SHOWN   "/3ds/StarHaven"
/* builds before the rename used this folder; it is moved on first boot */
#define PM3DS_SD_DIR_OLD     "sdmc:/3ds/PaperMario3DS"

#endif
