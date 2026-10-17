/*
 * audio_system.c — port replacement for src/audio/core/system.c (M14).
 *
 * The decomp's system.c is nusys plumbing: a scheduler client that waits
 * for retrace messages, hands each frame's command list to the RSP and
 * DMAs the result to the AI. On 3DS the NDSP refill thread pulls audio
 * instead: pm3ds_audio_engine_fill() runs alAudioFrame() (the real synth
 * driver), executes the command list on the software RSP (audio_alist.c)
 * and returns interleaved stereo s16 at HARDWARE_OUTPUT_RATE.
 *
 * Sample data DMA: the engine asks for wave data by cart address; the whole
 * baserom is resident, so the callback returns a pointer into it.
 */
#include "common.h"
#include "audio.h"
#include "audio/core.h"
#include "pm3ds_addr.h"
#include <stdio.h>
#include <3ds.h>

void pm3ds_log(int, const char*, ...);
#include "pm3ds_rom_stream.h"

/* Samples outside the pinned audio bank (none in the shipped layout) are
 * copied into a ring of buffers, as nusys did; each lives for a few frames. */
#define AU_DMA_BUFS 32
#define AU_DMA_BUF_SIZE 0x2000
static u8 sAuDmaBuf[AU_DMA_BUFS][AU_DMA_BUF_SIZE] __attribute__((aligned(8)));
static u32 sAuDmaNext;
void pm3ds_alist_run(const u32* cmds, int nwords);

u8 nuAuPreNMI = 0;
s32 nuAuDmaNext = 0;
u32 nuAuFrameCounter = 0;
u8 nuAuTaskStop = NU_AU_TASK_RUN;
u8 volatile AuSynUseStereo = true;
u16 AuInitialGlobalVolume;
s32 AlFrameSize;
s32 AlMinFrameSize;
ALHeap nuAuHeap;
AuSynDriver auSynDriver;
NUDMAState nuAuDmaState;

extern u8 AuHeapBase[AUDIO_HEAP_SIZE];

static Acmd* sCmdList;
static s16* sFrameOut;
static volatile s32 sEngineReady;

/* N64 RDRAM was 8 MB: the engine never expects a pointer it stores in a
 * 24-bit command field to wrap a 16 MB boundary. */
static void check_heap_window(void) {
    u32 lo = (u32)(uintptr_t)AuHeapBase, hi = lo + AUDIO_HEAP_SIZE - 1;
    if ((lo >> 24) != (hi >> 24)) {
        pm3ds_log(3, "audio: AuHeapBase %p..%p straddles a 16 MB boundary; "
                     "24-bit alist state pointers will break", (void*)lo, (void*)hi);
    }
}

s32 nuAuDmaCallBack(s32 addr, s32 len, void* state, u8 useDma) {
    if (!useDma) {
        return addr;    /* already a host pointer (data in RAM) */
    }
    u32 rom = pm3ds_addr_rom_offset((uintptr_t)addr, (size_t)len);
    if (rom == (u32)-1) {
        return addr;
    }
    const u8* pinned = pm3ds_rom_ptr(rom, (u32)len);
    if (pinned != nullptr) {
        return (s32)(uintptr_t)pinned;
    }
    if ((u32)len > AU_DMA_BUF_SIZE) {
        static s32 warned;
        if (warned++ < 4) {
            pm3ds_log(3, "audio: sample DMA of %d bytes at rom 0x%X is larger than a buffer", (int)len, (unsigned)rom);
        }
        len = AU_DMA_BUF_SIZE;
    }
    u8* buf = sAuDmaBuf[sAuDmaNext++ % AU_DMA_BUFS];
    pm3ds_rom_read(rom, buf, (u32)len);
    return (s32)(uintptr_t)buf;
}

ALDMAproc2 nuAuDmaNew(NUDMAState** state) {
    *state = &nuAuDmaState;
    return nuAuDmaCallBack;
}

void nuAuCleanDMABuffers(void) {
}

void nuAuPreNMIFuncSet(void (*func)(void)) {
    (void)func;
}

void alLink(ALLink* element, ALLink* after) {
    element->next = after->next;
    element->prev = after;
    if (after->next != nullptr) {
        after->next->prev = element;
    }
    after->next = element;
}

void alUnlink(ALLink* element) {
    if (element->next != nullptr) {
        element->next->prev = element->prev;
    }
    if (element->prev != nullptr) {
        element->prev->next = element->next;
    }
}

/* produce this many samples per alAudioFrame call (multiple of AUDIO_SAMPLES) */
#define PM3DS_AU_FRAME (AUDIO_SAMPLES * 3)

/* The engine half of an audio frame (sequencers, sound queues, building the
 * command list) runs on the game's core in a high-priority thread, like the
 * N64 audio manager thread: game code changes engine state from its own
 * thread, and the two may only interleave by preemption, never run truly in
 * parallel. The command list itself (the DSP work, most of the cost) then
 * runs on the mixer thread, which has New 3DS core 2 to itself. */
static struct Thread_tag* sEngineThread;   /* libctru Thread; "Thread" is an EVT macro here */
static LightEvent sEngineRequest, sEngineDone;
static volatile s32 sEngineThreadRun;
static Acmd* volatile sEngineEnd;
static s32 sEngineFrameLen = PM3DS_AU_FRAME;   /* AUDIO_SAMPLES with the DSP's voices */
u64 pm3ds_t_audio_engine;

static void engine_thread_entry(void* arg) {
    (void)arg;
    for (;;) {
        LightEvent_Wait(&sEngineRequest);
        if (!sEngineThreadRun) {
            break;
        }
        __dmb();
        u64 t0 = svcGetSystemTick();
        s32 cmdLen = 0;
        sEngineEnd = alAudioFrame(sCmdList, &cmdLen, sFrameOut, sEngineFrameLen);
        pm3ds_t_audio_engine += svcGetSystemTick() - t0;
        __dmb();
        LightEvent_Signal(&sEngineDone);
    }
}

static void start_engine_thread(void) {
    LightEvent_Init(&sEngineRequest, RESET_ONESHOT);
    LightEvent_Init(&sEngineDone, RESET_ONESHOT);
    sEngineThreadRun = true;
    /* -2: the process's default core, where the game threads run; 0x18 puts
     * it above every game thread (0x18..0x3F) so it preempts them */
    sEngineThread = threadCreate(engine_thread_entry, nullptr, 0x10000, 0x18, -2, false);
    if (sEngineThread == nullptr) {
        sEngineThreadRun = false;
        pm3ds_log(3, "audio: engine thread spawn failed; running the engine on the mixer thread");
    }
}

/* after the mixer has stopped: no more requests can arrive */
void pm3ds_audio_engine_shutdown(void) {
    if (sEngineThread != nullptr) {
        sEngineThreadRun = false;
        LightEvent_Signal(&sEngineRequest);
        threadJoin(sEngineThread, U64_MAX);
        threadFree(sEngineThread);
        sEngineThread = nullptr;
    }
}

void create_audio_system(void) {
    ALConfig config;

    nuAuTaskStop = NU_AU_TASK_RUN;
    nuAuPreNMI = 0;
    check_heap_window();
    alHeapInit(&nuAuHeap, AuHeapBase, AUDIO_HEAP_SIZE);
    config.num_pvoice = 24;
    config.num_bus = 4;
    config.outputRate = HARDWARE_OUTPUT_RATE;
    config.unused_0C = 0;
    config.heap = &nuAuHeap;
    config.dmaNew = (ALDMANew2)nuAuDmaNew;
    AlFrameSize = PM3DS_AU_FRAME;
    AlMinFrameSize = AlFrameSize - AUDIO_MAX_SAMPLES;

    sCmdList = alHeapAlloc(config.heap, 1, AUDIO_COMMAND_LIST_BUFFER_SIZE);
    sFrameOut = alHeapAlloc(config.heap, 1, PM3DS_AU_FRAME * 4);

    au_driver_init(&auSynDriver, &config);
    au_engine_init(config.outputRate);
    start_engine_thread();
    __dmb();   /* the mixer on another core must see the engine state and thread first */
    sEngineReady = true;
    pm3ds_log(2, "audio: engine up (%d Hz, %d-sample frames, heap %p)",
              HARDWARE_OUTPUT_RATE, PM3DS_AU_FRAME, (void*)AuHeapBase);
}

/* FIFO between whole engine frames and arbitrary-sized NDSP refills. */
static s16 sFifo[PM3DS_AU_FRAME * 2];
static s32 sFifoPos = PM3DS_AU_FRAME;   /* start empty */

static void render_audio_frame(void) {
    extern u64 pm3ds_t_audio;
    Acmd* end;
    if (sEngineThread != nullptr) {
        LightEvent_Signal(&sEngineRequest);
        LightEvent_Wait(&sEngineDone);
        __dmb();
        end = sEngineEnd;
    } else {
        s32 cmdLen = 0;
        u64 te = svcGetSystemTick();
        end = alAudioFrame(sCmdList, &cmdLen, sFrameOut, PM3DS_AU_FRAME);
        pm3ds_t_audio_engine += svcGetSystemTick() - te;
    }
    u64 t0 = svcGetSystemTick();
    s32 nwords = (s32)((end - sCmdList) * 2);
    if (nwords > 0) {
        pm3ds_alist_run((const u32*)sCmdList, nwords);
    }
    pm3ds_t_audio += svcGetSystemTick() - t0;
    memcpy(sFifo, sFrameOut, sizeof(sFifo));
    sFifoPos = 0;
    nuAuFrameCounter++;
    {   /* one stat line every ~10 s of audio */
        extern unsigned pm3ds_alist_cmds;
        static s32 peak;
        for (s32 i = 0; i < PM3DS_AU_FRAME * 2; i++) {
            s32 v = sFrameOut[i] < 0 ? -sFrameOut[i] : sFrameOut[i];
            if (v > peak) {
                peak = v;
            }
        }
        if (nuAuFrameCounter % 580 == 0) {
            pm3ds_log(2, "audio: frame %u cmds=%u (last list %d words) peak=%d bgmA=%d",
                      (unsigned)nuAuFrameCounter, pm3ds_alist_cmds, (int)nwords, (int)peak,
                      gBGMPlayerA != nullptr ? gBGMPlayerA->masterState : -1);
            peak = 0;
        }
    }
#ifdef PM3DS_AUDIO_WAVDUMP
    {   /* first PM3DS_AUDIO_WAVDUMP seconds of output -> sdmc:/pm3ds_audio.wav */
        static FILE* f;
        static u32 bytes;
        static s32 done;
        if (!done) {
            if (f == nullptr) {
                f = fopen("sdmc:/pm3ds_audio.wav", "wb");
                if (f != nullptr) {
                    static const u8 hdr[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                        0x00,0x7D,0,0, 0x00,0xF4,0x01,0, 4,0,16,0,'d','a','t','a',0,0,0,0 };
                    fwrite(hdr, 1, sizeof(hdr), f);
                }
            }
            if (f != nullptr) {
                fwrite(sFrameOut, 1, PM3DS_AU_FRAME * 4, f);
                bytes += PM3DS_AU_FRAME * 4;
                if (bytes >= (u32)HARDWARE_OUTPUT_RATE * 4 * PM3DS_AUDIO_WAVDUMP) {
                    u32 riff = bytes + 36;
                    fseek(f, 4, SEEK_SET);  fwrite(&riff, 4, 1, f);
                    fseek(f, 40, SEEK_SET); fwrite(&bytes, 4, 1, f);
                    fclose(f);
                    f = nullptr;
                    done = true;
                    pm3ds_log(2, "audio: wrote %u bytes to sdmc:/pm3ds_audio.wav", (unsigned)bytes);
                }
            }
        }
    }
#endif
}

/* Old 3DS: the DSP plays the voices (audio_hwvoice.c), so nothing pulls
 * mixed audio; run an engine frame (AUDIO_SAMPLES) whenever the clock says
 * one is due. Called every ~2 ms from the audio thread. */
void pm3ds_audio_engine_tick_hw(void) {
    extern u32 pm3ds_hwv_clock;
    static u64 t0, done;
    if (!sEngineReady || nuAuTaskStop != NU_AU_TASK_RUN) {
        return;
    }
    u64 now = svcGetSystemTick();
    if (t0 == 0) {
        t0 = now;
    }
    u64 due = (now - t0) * HARDWARE_OUTPUT_RATE / SYSCLOCK_ARM11 / AUDIO_SAMPLES;
    if (due > done + 8) {
        done = due - 8;   /* far behind (the HOME menu, a long stall): no burst */
    }
    sEngineFrameLen = AUDIO_SAMPLES;
    while (done < due) {
        if (sEngineThread != nullptr) {
            LightEvent_Signal(&sEngineRequest);
            LightEvent_Wait(&sEngineDone);
            __dmb();
        } else {
            s32 cmdLen = 0;
            u64 te = svcGetSystemTick();
            alAudioFrame(sCmdList, &cmdLen, sFrameOut, AUDIO_SAMPLES);
            pm3ds_t_audio_engine += svcGetSystemTick() - te;
        }
        pm3ds_hwv_clock++;
        nuAuFrameCounter++;
        if (nuAuFrameCounter % 1740 == 0) {
            extern void pm3ds_hwv_stats(void);
            pm3ds_hwv_stats();
        }
        done++;
    }
}

/* Called from the NDSP/CSND refill thread (and the frame-loop fallback):
 * fill `nsamples` stereo frames. The engine is not reentrant, so serialize. */
static LightLock sFillLock;
static s32 sFillLockInit;

void pm3ds_audio_engine_fill(s16* out, s32 nsamples) {
    if (!sEngineReady || nuAuTaskStop != NU_AU_TASK_RUN) {
        memset(out, 0, nsamples * 2 * sizeof(s16));
        return;
    }
    if (!sFillLockInit) {
        LightLock_Init(&sFillLock);
        sFillLockInit = true;
    }
    LightLock_Lock(&sFillLock);
    while (nsamples > 0) {
        if (sFifoPos >= PM3DS_AU_FRAME) {
            render_audio_frame();
        }
        s32 n = PM3DS_AU_FRAME - sFifoPos;
        if (n > nsamples) {
            n = nsamples;
        }
        memcpy(out, &sFifo[sFifoPos * 2], n * 2 * sizeof(s16));
        out += n * 2;
        sFifoPos += n;
        nsamples -= n;
    }
    LightLock_Unlock(&sFillLock);
}
