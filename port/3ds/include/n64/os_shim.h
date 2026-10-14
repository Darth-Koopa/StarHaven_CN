#ifndef _OS_SHIM_H_
#define _OS_SHIM_H_

#include "os_types.h"
#include <3ds.h>

// ============================================================================
// OS Thread Shim
// ============================================================================

typedef struct OSThread {
    u32 id;
    Thread ctrThread;
} OSThread;

typedef void* OSMesg;
/* M8.7: layout must mirror include/PR/os_message.h (size 0x18) AND the
 * implementation in source/n64_shim/os_mesgqueue.c — decomp code
 * allocates these on the stack, so any size mismatch is a stack smash. */
typedef struct OSMesgQueue {
    s32 lock;       /* LightLock; decomp slot: mtqueue   */
    s32 spare;      /*            decomp slot: fullqueue */
    s32 validCount;
    s32 first;
    s32 msgCount;
    OSMesg* msg;
} OSMesgQueue;

typedef u32 OSTimer;
typedef u32 OSIntMask;
// M4.1: OSTime is u64 per upstream PR/os_time.h. src/bss declares
// `BSS OSTime __osCurrentTime;`; without this typedef it errors.
typedef u64 OSTime;

// Thread functions
void osCreateThread(OSThread* t, s32 id, void (*entry)(void*), void* arg, void* sp, s32 pri);
void osStartThread(OSThread* t);
void osStopThread(OSThread* t);
void osYieldThread(void);
OSThread* osGetThreadId(void);
void osSetThreadPri(OSThread* t, s32 pri);
s32 osGetThreadPri(OSThread* t);
void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 msgCount);
s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flags);
s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flags);

// Time / Counter
u32 osGetCount(void);
void osSetTime(u64 time);
// osGetTime is provided by libctru
void osSetTimer(OSTimer* t, u64 count, u64 interval, OSMesgQueue* mq, OSMesg msg);
void osStopTimer(OSTimer* t);

// Interrupt / Cache
void osSetIntMask(OSIntMask im);
OSIntMask osGetIntMask(void);
void osInvalDCache(void* vaddr, s32 nbytes);
void osInvalICache(void* vaddr, s32 nbytes);
void osWritebackDCache(void* vaddr, s32 nbytes);
void osWritebackDCacheAll(void);

// Memory
void* osVirtualToPhysical(void* vaddr);
void* osPhysicalToVirtual(u32 paddr);
// M12: identity on 3DS (host pointers; masking also made static DL initializers non-constant)
#define OS_K0_TO_PHYSICAL(addr) ((u32)(addr))
#define OS_PHYSICAL_TO_K0(addr) ((void*)(addr))

// String
void osSyncPrintf(const char* fmt, ...);

// Panic
void osPanic(const char* file, s32 line, const char* msg, ...);

// TV Type (N64 hardware feature, stubbed)
#define OS_TV_NTSC    0
#define OS_TV_PAL     1
#define OS_TV_MPAL    2
extern s32 osTvType;

// ============================================================================
// EEPROM / Save Data Shim -> 3DS ExtData
// ============================================================================

#define EEPROM_MAXBLOCKS  64
#define EEPROM_BLOCK_SIZE 8

s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, s32 nbytes);
s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, s32 nbytes);
s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer);
s32 osEepromWrite(OSMesgQueue* mq, u8 address, u8* buffer);
s32 osEepromProbe(OSMesgQueue* mq);

// ============================================================================
// Controller Pak (migrated to 3DS SD / ExtData)
// ============================================================================

typedef struct OSPfs {
    u32 dummy;
} OSPfs;

// M4.1: BLOCKSIZE / PFS_* / OS_NUM_EVENTS — these are libultra constants
// that src/bss/ uses to size BSS array allocations
// (`__osEventStateTab[OS_NUM_EVENTS]`, `data[BLOCKSIZE]` etc.) but the
// upstream PR/controller.h doesn't transitively include PR/os_pfs.h or
// PR/os_message.h where these live. Provide port-side defaults so
// src/bss compiles standalone. Types (OSPifRam, __OSInode, OSPri,
// __OSEventState) come from the upstream headers directly — defining
// them here would conflict.
#ifndef OS_NUM_EVENTS
#define OS_NUM_EVENTS 15
#endif
#ifndef PFS_FILE_NAME_LEN
#define PFS_FILE_NAME_LEN 16
#endif
#ifndef PFS_FILE_EXT_LEN
#define PFS_FILE_EXT_LEN 4
#endif
#ifndef PFS_INODE_DIST_MAP
#define PFS_INODE_DIST_MAP 0x80
#endif
#ifndef BLOCKSIZE
#define BLOCKSIZE 32
#endif

// OSPiHandle (used by DMA / PI functions)
typedef struct OSPiHandle {
    u32 dummy;
} OSPiHandle;

// OS Io message (used by PI DMA)
typedef struct OSIoMesgHdr {
    u8      pri;
    u8      dummy;
    u16     status;
    OSMesgQueue* retQueue;
} OSIoMesgHdr;

typedef struct OSIoMesg {
    OSIoMesgHdr hdr;
    void*       dramAddr;
    u32         devAddr;
    u32         size;
} OSIoMesg;

extern OSPiHandle* nuPiCartHandle;

typedef struct OSPfsState {
    u32 file_size;
    u8  game_name[16];
    u8  ext_name[4];
} OSPfsState;

s32 osPfsInitPak(OSMesgQueue* queue, OSPfs* pfs, int channel);
s32 osPfsRepairId(OSPfs* pfs);
s32 osPfsInit(OSMesgQueue* queue, OSPfs* pfs, int channel);
s32 osPfsFileStat(OSPfs* pfs, s32 fileNo, OSPfsState* state);
s32 osPfsFileDelete(OSPfs* pfs, s32 fileNo);
s32 osPfsFileDataRead(OSPfs* pfs, s32 fileNo, u8* data, s32 offset, s32 size);
s32 osPfsFileDataWrite(OSPfs* pfs, s32 fileNo, u8* data, s32 offset, s32 size);
s32 osPfsFreeBlocks(OSPfs* pfs, s32* returned);
s32 osPfsNumFiles(OSPfs* pfs, s32* maxFiles, s32* filesUsed);
s32 osPfsAllocateFile(OSPfs* pfs, u16 companyCode, u32 gameCode,
                      u8* gameName, u8* extName, s32 fileSizeInBytes,
                      s32* fileNo);
s32 osPfsReadWriteFile(OSPfs* pfs, s32 fileNo, u8 flag, s32 offset,
                       s32 size, u8* data);

// PI / DMA stubs
s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* ioMesg, s32 direction);
s32 osEPiReadIo(OSPiHandle* handle, u32 devAddr, u32* data);
s32 osEPiWriteIo(OSPiHandle* handle, u32 devAddr, u32 data);
OSPiHandle* osCartRomInit(void);

#endif
/* M14: libultra's K0_TO_PHYS (used by the audio engine) — identity on 3DS */
#ifndef K0_TO_PHYS
#define K0_TO_PHYS(x) ((u32)(x))
#endif
