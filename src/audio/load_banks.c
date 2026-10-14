#include "audio.h"
#include "audio/core.h"

void au_load_BK_headers(AuGlobals* globals, ALHeap* heap) {
    SBNFileEntry fileEntry;
    InitBankEntry buffer[INIT_BANK_BUFFER_SIZE];
    s32 i;

    au_read_rom(globals->bkFileListOffset, &buffer, globals->bkListLength);
#ifdef PM3DS_PORT_ROM_ADDRS
    { void pm3ds_au_swap_bank_list(InitBankEntry* e, s32 size); pm3ds_au_swap_bank_list(buffer, globals->bkListLength); }
#endif

    for (i = 0; i < ARRAY_COUNT(buffer); i++) {
        if (buffer[i].fileIndex == 0xFFFF) {
            break;
        }

        au_fetch_SBN_file(buffer[i].fileIndex, AU_FMT_BK, &fileEntry);
        au_load_BK_to_bank(fileEntry.offset, nullptr, buffer[i].bankIndex, buffer[i].bankSet);
    }
}
