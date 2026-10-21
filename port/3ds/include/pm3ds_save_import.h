#ifndef PM3DS_SAVE_IMPORT_H
#define PM3DS_SAVE_IMPORT_H
/* N64 save import (n64_shim/os_flash.c): an import.fla / import.srm in the SD
 * folder is converted into the port's save at boot. */
#include <stddef.h>
#include <stdint.h>

#define PM3DS_FLASH_SIZE 0x20000

/* Fills out (PM3DS_FLASH_SIZE bytes) with the port's save image made from an
 * N64 FlashRAM image (.fla in either word order, or a RetroArch/mupen64plus .srm)
 * or a save already in the port's order. Returns the number of file slots found,
 * 0 if there is none. */
int pm3ds_save_import_convert(const uint8_t *in, size_t len, uint8_t *out);

/* for the bottom screen: >0 save files imported this boot, -1 the file held none
 * (it was renamed .bad), 0 nothing to show */
int pm3ds_save_import_result(void);
const char *pm3ds_save_import_backup(void);   /* where the old save went, or "" */
void pm3ds_save_import_dismiss(void);

#endif
