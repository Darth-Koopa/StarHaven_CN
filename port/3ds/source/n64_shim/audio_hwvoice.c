/*
 * audio_hwvoice.c — the synth's 24 voices on the DSP's 24 channels (Old 3DS).
 *
 * The software mixer (audio_alist.c) decodes, resamples and envelope-mixes
 * every voice on the CPU: up to a third of the Old 3DS system core, where
 * the app only gets 30% (audio chugs in busy scenes). The DSP does all three
 * in hardware, so on the Old 3DS each voice plays on its own NDSP channel:
 *
 *   - a sample is converted once, VADPCM -> PCM16 in linear memory, the
 *     first time a voice plays it, and kept in a cache sized at start-up
 *     (least recently used samples that no voice plays are dropped);
 *   - a note queues the sample's wave buffers: the start up to the loop
 *     end, then the loop (repeating forever, or loopCount times followed by
 *     the tail), or the whole sample if it has no loop;
 *   - every audio frame (184 samples) the engine advances the voice as
 *     before (src/audio/core/pull_voice.c au_pull_voice_hw) and this file
 *     sets the channel's rate from the pitch and its left/right volume from
 *     the envelope ramp, the dry send and the bus gain, the same ramp the
 *     software ENVMIXER runs per sample.
 *
 * Reverb (the wet send) and the channel delay effect are not played: the
 * Old 3DS left reverb off already. The engine is paced by the clock
 * (audio_system.c pm3ds_audio_engine_tick_hw) instead of by a stream.
 */
#include "common.h"
#include "audio.h"
#include "audio/core.h"
#include "pm3ds_addr.h"
#include "pm3ds_rom_stream.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

void pm3ds_log(int, const char*, ...);

bool pm3ds_au_hwvoices;       /* set by audio_dsp.c before the engine starts */
u32 pm3ds_hwv_clock;          /* engine frames run (audio_system.c) */

#define HWV_VOICES   24
#define HWV_SAMPLES  384      /* cache slots */
#define HWV_LOOP_BUFS 6       /* a finite loop repeated this many times at most */
#define HWV_CACHE_MAX (6u * 1024 * 1024)

typedef struct {
    const void* key;          /* the instrument's wavData */
    u32 keyLen;
    s16* pcm;                 /* linear memory */
    u32 nsamples;
    u32 lastUse;              /* pm3ds_hwv_clock */
    u8 users;                 /* voices playing it */
} HwSample;

typedef struct {
    HwSample* smp;
    ndspWaveBuf wb[HWV_LOOP_BUFS + 2];
    s64 vol[2], step[2], target[2];   /* the ENVMIXER ramp, 16.16 per sample */
    f32 rate, mixL, mixR;             /* last values sent to the channel */
    s8 pending;                       /* a note (re)started since the last frame */
    s8 active;                        /* the channel plays this voice's note */
    s8 endless;                       /* a looping wave buffer is queued: only a clear stops it */
} HwVoice;

static HwSample sSamples[HWV_SAMPLES];
static HwVoice sVoices[HWV_VOICES];
static u32 sCacheBytes, sCacheCap;
static u32 sStatNotes, sStatConverted, sStatFailed;

void pm3ds_hwv_init(void) {
    u32 freeLinear = linearSpaceFree();
    sCacheCap = freeLinear / 3;
    if (sCacheCap > HWV_CACHE_MAX) {
        sCacheCap = HWV_CACHE_MAX;
    }
    for (s32 ch = 0; ch < HWV_VOICES; ch++) {
        float mix[12] = { 0 };
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_POLYPHASE);   /* nearer the N64 resampler's 4-tap filter than linear */
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetRate(ch, (float)HARDWARE_OUTPUT_RATE);
        ndspChnSetMix(ch, mix);
        sVoices[ch].rate = -1.0f;
    }
    pm3ds_log(2, "audio: voices on the DSP's channels, sample cache %u KB (linear free %u KB)",
              (unsigned)(sCacheCap >> 10), (unsigned)(freeLinear >> 10));
}

/* linear memory the sample cache may still take (the texture cache leaves it) */
u32 pm3ds_hwv_reserve(void) {
    return pm3ds_au_hwvoices && sCacheCap > sCacheBytes ? sCacheCap - sCacheBytes : 0;
}

/* the instrument's wave bytes, wherever they live (as nuAuDmaCallBack finds them) */
static const u8* wave_bytes(Instrument* ins, u8** owned) {
    *owned = nullptr;
    if (!ins->useDma) {
        return ins->wavData;
    }
    u32 rom = pm3ds_addr_rom_offset((uintptr_t)ins->wavData, ins->wavDataLength);
    if (rom == (u32)-1) {
        return ins->wavData;
    }
    const u8* pinned = pm3ds_rom_ptr(rom, ins->wavDataLength);
    if (pinned != nullptr) {
        return pinned;
    }
    *owned = malloc(ins->wavDataLength);
    if (*owned != nullptr) {
        pm3ds_rom_read(rom, *owned, ins->wavDataLength);
    }
    return *owned;
}

static inline s16 clamp16(s32 v) {
    return v < -32768 ? -32768 : v > 32767 ? 32767 : (s16)v;
}

/* VADPCM, order 2: the same decode as audio_alist.c's ADPCM command, from the
 * start of the sample with a zero history */
static void decode_vadpcm(const u8* in, u32 nframes, const s16* book, u32 npred, s16* out) {
    s16 last[16];
    memset(last, 0, sizeof(last));
    for (u32 f = 0; f < nframes; f++) {
        u8 code = *in++;
        s32 scale = code >> 4;
        u32 pred = (code & 0xF) < npred ? (code & 0xF) : 0;
        const s16* book1 = book + (pred << 4);
        const s16* book2 = book1 + 8;
        s32 rshift = scale < 12 ? 12 - scale : 0;
        s16 frame[16];
        for (s32 i = 0; i < 8; i++) {
            u8 b = *in++;
            frame[i * 2] = (s16)((s16)((u16)(b & 0xF0) << 8) >> rshift);
            frame[i * 2 + 1] = (s16)((s16)((u16)(b & 0x0F) << 12) >> rshift);
        }
        for (s32 half = 0; half < 2; half++) {
            const s16* src = frame + half * 8;
            s16 l1 = half ? last[6] : last[14];
            s16 l2 = half ? last[7] : last[15];
            s16 res[8];
            for (s32 i = 0; i < 8; i++) {
                s32 acc = (s32)src[i] << 11;
                acc += book1[i] * l1 + book2[i] * l2;
                for (s32 j = 0; j < i; j++) {
                    acc += book2[i - 1 - j] * src[j];
                }
                res[i] = clamp16(acc >> 11);
            }
            memcpy(last + half * 8, res, sizeof(res));
        }
        memcpy(out, last, sizeof(last));
        out += 16;
    }
}

static void evict(HwSample* s) {
    linearFree(s->pcm);
    sCacheBytes -= s->nsamples * sizeof(s16);
    memset(s, 0, sizeof(*s));
}

static HwSample* sample_get(Instrument* ins) {
    HwSample* freeSlot = nullptr;
    for (s32 i = 0; i < HWV_SAMPLES; i++) {
        HwSample* s = &sSamples[i];
        if (s->pcm == nullptr) {
            if (freeSlot == nullptr) {
                freeSlot = s;
            }
        } else if (s->key == ins->wavData && s->keyLen == ins->wavDataLength) {
            s->lastUse = pm3ds_hwv_clock;
            return s;
        }
    }

    bool adpcm = ins->type == AL_ADPCM_WAVE;
    u32 nframes = ins->wavDataLength / ADPCMFBYTES;
    u32 nsamples = adpcm ? nframes * ADPCMFSIZE : ins->wavDataLength / 2;
    u32 bytes = nsamples * sizeof(s16);
    if (nsamples == 0 || bytes > sCacheCap) {
        return nullptr;
    }
    /* make room: drop the least recently used samples no voice plays (and that
     * stopped a few frames ago, so the DSP has finished reading their tail) */
    while (sCacheBytes + bytes > sCacheCap || freeSlot == nullptr) {
        HwSample* lru = nullptr;
        for (s32 i = 0; i < HWV_SAMPLES; i++) {
            HwSample* s = &sSamples[i];
            if (s->pcm != nullptr && s->users == 0 && pm3ds_hwv_clock - s->lastUse > 4 &&
                (lru == nullptr || s->lastUse < lru->lastUse)) {
                lru = s;
            }
        }
        if (lru == nullptr) {
            static s32 warned;
            if (warned++ < 4) {
                pm3ds_log(3, "audio: sample cache full (%u KB in use), a voice stays silent",
                          (unsigned)(sCacheBytes >> 10));
            }
            return nullptr;
        }
        evict(lru);
        if (freeSlot == nullptr) {
            freeSlot = lru;
        }
    }

    s16* pcm = linearAlloc(bytes);
    if (pcm == nullptr) {
        return nullptr;
    }
    u8* owned;
    const u8* src = wave_bytes(ins, &owned);
    if (src == nullptr) {
        linearFree(pcm);
        return nullptr;
    }
    if (adpcm) {
        u32 npred = ins->codebookSize / (2 * 8 * sizeof(s16));
        decode_vadpcm(src, nframes, ins->predictor, npred, pcm);
    } else {
        /* raw 16-bit samples are big-endian on the cart */
        for (u32 i = 0; i < nsamples; i++) {
            pcm[i] = ins->useDma ? (s16)((src[2 * i] << 8) | src[2 * i + 1]) : ((const s16*)src)[i];
        }
        static s32 logged;
        if (!logged++) {
            pm3ds_log(2, "audio: first raw 16-bit sample (%u samples)", (unsigned)nsamples);
        }
    }
    free(owned);
    DSP_FlushDataCache(pcm, bytes);

    sStatConverted++;
    freeSlot->key = ins->wavData;
    freeSlot->keyLen = ins->wavDataLength;
    freeSlot->pcm = pcm;
    freeSlot->nsamples = nsamples;
    freeSlot->lastUse = pm3ds_hwv_clock;
    freeSlot->users = 0;
    sCacheBytes += bytes;
    return freeSlot;
}

static void release_sample(HwVoice* v) {
    if (v->smp != nullptr) {
        if (v->smp->users > 0) {
            v->smp->users--;
        }
        v->smp->lastUse = pm3ds_hwv_clock;
        v->smp = nullptr;
    }
}

static void add_buf(ndspWaveBuf* wb, s32 ch, const s16* data, u32 n, bool looping) {
    memset(wb, 0, sizeof(*wb));
    wb->data_pcm16 = (s16*)data;
    wb->nsamples = n;
    wb->looping = looping;
    ndspChnWaveBufAdd(ch, wb);
}

static void start_note(HwVoice* v, s32 ch, Instrument* ins) {
    ndspChnWaveBufClear(ch);
    release_sample(v);
    v->active = false;
    v->endless = false;
    if (ins == nullptr) {
        return;
    }
    HwSample* s = sample_get(ins);
    sStatNotes++;
    if (s == nullptr) {
        sStatFailed++;
        return;
    }
    v->smp = s;
    s->users++;

    u32 total = s->nsamples;
    u32 ls = (u32)ins->loopStart, le = (u32)ins->loopEnd;
    s32 count = ins->loopCount;
    const s16* pcm = s->pcm;
    if (le != 0 && count != 0 && le <= total && ls < le) {
        s32 b = 0;
        add_buf(&v->wb[b++], ch, pcm, le, false);
        if (count < 0 || count > HWV_LOOP_BUFS) {
            add_buf(&v->wb[b++], ch, pcm + ls, le - ls, true);
            v->endless = true;
        } else {
            for (s32 k = 0; k < count; k++) {
                add_buf(&v->wb[b++], ch, pcm + ls, le - ls, false);
            }
            if (total > le) {
                add_buf(&v->wb[b++], ch, pcm + le, total - le, false);
            }
        }
    } else {
        add_buf(&v->wb[0], ch, pcm, total, false);
    }
#ifdef PM3DS_HWV_DEBUG
    {   /* debug: the first notes */
        static s32 n;
        if (n++ < 200)
            pm3ds_log(2, "HWV ch%d ins=%p n=%u loop=%u..%u x%d ratio=%.3f bus=%d", (int)ch, (void*)ins, (unsigned)total,
                      (unsigned)ls, (unsigned)le, (int)count, 0.0, 0);
    }
#endif
    v->rate = -1.0f;   /* send rate and volume with this frame */
    v->mixL = v->mixR = -1.0f;
    v->active = true;
}

/* a note starts (start_voice_params, set_wavetable) or is stopped */
void pm3ds_hwv_note(u8 voiceIdx, s32 start) {
    if (!pm3ds_au_hwvoices || voiceIdx >= HWV_VOICES) {
        return;
    }
    HwVoice* v = &sVoices[voiceIdx];
    if (start) {
        v->pending = true;
    } else {
        v->pending = false;
        if (v->active || v->endless) {
            ndspChnWaveBufClear(voiceIdx);
        }
        v->endless = false;
        v->active = false;
        release_sample(v);
    }
}

static void ramp_advance(HwVoice* v, s32 k, s32 n) {
    v->vol[k] += v->step[k] * n;
    bool reached = v->step[k] <= 0 ? v->vol[k] <= v->target[k] : v->vol[k] >= v->target[k];
    if (reached) {
        v->vol[k] = v->target[k];
        v->step[k] = 0;
    }
}

/* about every 10 s of audio: what the DSP voices did */
void pm3ds_hwv_stats(void) {
    s32 active = 0, orphans = 0;
    for (s32 i = 0; i < HWV_VOICES; i++) {
        active += sVoices[i].active;
        orphans += !sVoices[i].active && ndspChnIsPlaying(i);   /* sounding with no voice behind it */
    }
    pm3ds_log(2, "audio: dsp voices: %u notes, %u samples converted, %u failed, cache %u/%u KB, %d playing, %d orphaned",
              (unsigned)sStatNotes, (unsigned)sStatConverted, (unsigned)sStatFailed, (unsigned)(sCacheBytes >> 10),
              (unsigned)(sCacheCap >> 10), (int)active, (int)orphans);
}

/* one audio frame of a playing voice (au_pull_voice_hw) */
void pm3ds_hwv_frame(AuPVoice* pvoice, s32 restartRamp) {
    s32 ch = (s32)(pvoice - gSynDriverPtr->pvoices);
    if (ch < 0 || ch >= HWV_VOICES) {
        return;
    }
    HwVoice* v = &sVoices[ch];
    AuEnvMixer* env = &pvoice->envMixer;

    if (v->pending) {
        v->pending = false;
        start_note(v, ch, pvoice->decoder.instrument);
    }
    if (restartRamp) {
        /* what the ENVMIXER's A_INIT takes from the SETVOL commands */
        s32 lrate = (s32)(((u32)(u16)env->lratm << 16) | (u16)env->lratl);
        s32 rrate = (s32)(((u32)(u16)env->rratm << 16) | (u16)env->rratl);
        v->vol[0] = (s64)env->cvolL << 16;
        v->vol[1] = (s64)env->cvolR << 16;
        v->target[0] = (s64)env->ltgt << 16;
        v->target[1] = (s64)env->rtgt << 16;
        v->step[0] = lrate / 8;
        v->step[1] = rrate / 8;
    }
    ramp_advance(v, 0, AUDIO_SAMPLES);
    ramp_advance(v, 1, AUDIO_SAMPLES);

    if (v->active) {
        if (v->smp != nullptr) {
            v->smp->lastUse = pm3ds_hwv_clock;
        }
        f32 rate = pvoice->resampler.ratio * (f32)gSynDriverPtr->outputRate;
        if (rate != v->rate) {
            v->rate = rate;
            ndspChnSetRate(ch, rate);
        }
        /* sample * vol * dry (envmixer), * bus gain (bus mixdown), * global volume */
        f32 gain = (f32)env->dryamt * (1.0f / 32768.0f);
        if (pvoice->busID < gSynDriverPtr->num_bus) {
            gain *= (f32)gSynDriverPtr->fxBus[pvoice->busID].gain * (1.0f / 32768.0f);
        }
        if (AuUseGlobalVolume) {
            gain *= (f32)AuGlobalVolume * (1.0f / 32768.0f);
        }
        f32 l = (f32)(s32)(v->vol[0] >> 16) * (1.0f / 32768.0f) * gain;
        f32 r = (f32)(s32)(v->vol[1] >> 16) * (1.0f / 32768.0f) * gain;
        if (l != v->mixL || r != v->mixR) {
            float mix[12] = { 0 };
            mix[0] = l;
            mix[1] = r;
            ndspChnSetMix(ch, mix);
            v->mixL = l;
            v->mixR = r;
        }
    }

    if (env->motion == AL_STOPPED) {
        /* the sample ran out: the DSP plays its last few samples by itself.
         * A looping one never runs out there: stop the channel, or the loop
         * plays on at its last volume (Forever Forest's crows and wolves kept
         * going in Toad Town, GitHub #38, #46) */
        if (v->endless) {
            ndspChnWaveBufClear(ch);
            v->endless = false;
        }
        v->active = false;
        release_sample(v);
    }
}
