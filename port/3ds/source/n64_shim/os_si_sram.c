/*
 * os_si_sram.c — Controller Pak (Pfs) stubs.
 * Fidelity tier: STUB — no-op success. The decomp uses these for the
 *                save-file menu's controller-pak path; we report success
 *                and the game falls through to its EEPROM path.
 */

#include <stdint.h>
#include "pm3ds_panic.h"

int osPfsAllocateFile(void *pfs, int company, int game, uint8_t *gname,
                      uint8_t *ename, int length, int *file_no) {
    (void)pfs; (void)company; (void)game; (void)gname; (void)ename; (void)length;
    if (file_no) *file_no = 0;
    return 0;
}

int osPfsFindFile  (void *pfs, int company, int game, uint8_t *gname,
                    uint8_t *ename, int *file_no) {
    (void)pfs; (void)company; (void)game; (void)gname; (void)ename;
    if (file_no) *file_no = 0;
    return 0;
}

int osPfsChecker   (void *pfs) { (void)pfs; return 0; }
int osPfsInit      (void *mq, void *pfs, int channel) {
    (void)mq; (void)pfs; (void)channel; return 0;
}
int osPfsReadWriteFile(void *pfs, int file_no, uint8_t flag,
                       int offset, int size_in_bytes, uint8_t *data) {
    (void)pfs; (void)file_no; (void)flag; (void)offset; (void)size_in_bytes; (void)data;
    return 0;
}
int osPfsDeleteFile(void *pfs, int company, int game,
                    uint8_t *gname, uint8_t *ename) {
    (void)pfs; (void)company; (void)game; (void)gname; (void)ename;
    return 0;
}
int osPfsFreeBlocks(void *pfs, int *leftover) {
    (void)pfs;
    if (leftover) *leftover = 100;
    return 0;
}
int osPfsNumFiles  (void *pfs, int *max_files, int *files_used) {
    (void)pfs;
    if (max_files)  *max_files  = 16;
    if (files_used) *files_used = 0;
    return 0;
}

/* PFS subsystem-management stubs the decomp's file-select code touches. */
int osPfsInitPak  (void *mq, void *pfs, int channel) {
    (void)mq; (void)pfs; (void)channel; return 0;
}
int osPfsRepairId (void *pfs) { (void)pfs; return 0; }

int osPfsFileStat (void *pfs, int file_no, void *state) {
    (void)pfs; (void)file_no; (void)state; return 0;
}
int osPfsFileDelete(void *pfs, int file_no) {
    (void)pfs; (void)file_no; return 0;
}
int osPfsFileDataRead (void *pfs, int file_no, uint8_t *data,
                       int offset, int size) {
    (void)pfs; (void)file_no; (void)data; (void)offset; (void)size; return 0;
}
int osPfsFileDataWrite(void *pfs, int file_no, uint8_t *data,
                       int offset, int size) {
    (void)pfs; (void)file_no; (void)data; (void)offset; (void)size; return 0;
}
