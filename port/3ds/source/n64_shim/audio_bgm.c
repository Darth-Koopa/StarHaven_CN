/*
 * audio_bgm.c — M5.2: minimal BGM player driving the M5.1b ring buffer.
 * HARDWARE-VERIFIED on real 3DS 2026-05-14: real Paper Mario .bgm
 * playing cleanly via NDSP stereo with no crackle.
 *
 * Replaces the M5.1c hardcoded melody with a real bytecode interpreter
 * for Paper Mario's .bgm sequence format. Hidden behind
 * PM3DS_AUDIO_STEP_BGM (mutually exclusive with PM3DS_AUDIO_STEP_DEMO
 * and PM3DS_AUDIO_STEP_MELODY).
 *
 * Scope:
 *   - Loads ONE .bgm file from romfs on first fill call.
 *   - Walks composition[0]'s opcode stream: handles END, PLAY_PHRASE,
 *     START_LOOP, END_LOOP, WAIT. Loops are unbounded (always re-enter
 *     until END).
 *   - Per phrase, runs all 4 tracks in parallel. Each track is a
 *     monophonic sine voice (no polyphony, no instrument bank).
 *   - Tempo from track-0's META_E0_MasterTempo command (BPM is the
 *     second arg byte). Default 180 BPM if no MasterTempo seen.
 *   - Per audio sample, runs the bytecode tick clock at
 *     tempo_BPM * BGM_DEFAULT_TICKS_PER_BEAT / 60 ticks per second.
 *
 * Not yet supported (M5.3+):
 *   - SBN bank instrument samples (so we use sine voices, not the
 *     game's real instrument sounds).
 *   - FC_Branch / FE_Detour (track-internal control flow).
 *   - Polyphony (one voice per track for now).
 *   - Tremolo / vibrato / per-voice envelope shapes from
 *     META_F0_TrackTremolo etc.
 *   - Pan / reverb / volume scaling (META_EA / EB / EC / E9): tracked
 *     but applied only as a global per-track amplitude.
 *
 * Architecture: stays inside the audio_dsp.c fill_demo seam. The BGM
 * interpreter writes 16-bit mono PCM into the buffer audio_step hands
 * us; everything else (NDSP voice config, double-buffer ring, refill
 * thread) is reused from M5.0/M5.1b/M5.1d.
 */

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "pm3ds_log.h"
#include "pm3ds_audio.h"

/* The audio_step ring is mono PCM16 @ 32 kHz (see audio_dsp.c). */
#define BGM_RATE_HZ              32000
#define BGM_TICKS_PER_BEAT       48     /* from src/audio.h BGM_DEFAULT_TICKS_PER_BEAT */
#define BGM_DEFAULT_TEMPO_BPM    180

/* Single .bgm we play. Hardcoded for now; future M5 work picks via API. */
#ifndef PM3DS_AUDIO_BGM_PATH
#define PM3DS_AUDIO_BGM_PATH "romfs:/assets/audio/11_134.bgm"
#endif

#define BGM_TRACK_COUNT          4

/* Per-note ADSR phases. Linear ramps in tick-space.
 * Attack: 1 tick (very fast, just enough to defuse a click).
 * Release: 3 ticks (smooth fade-out so the next note doesn't
 * collide with the previous note's tail). */
#define BGM_NOTE_ATTACK_TICKS   1
#define BGM_NOTE_RELEASE_TICKS  3

typedef enum {
    BGM_VOICE_OFF = 0,
    BGM_VOICE_ATTACK,
    BGM_VOICE_SUSTAIN,
    BGM_VOICE_RELEASE,
} BgmVoicePhase;

typedef struct {
    /* Bytecode pointer into the file buffer. NULL = inactive. */
    const uint8_t* read_pos;
    /* Saved read position for FE_Detour — restored on END (0x00). */
    const uint8_t* return_pos;
    /* Phrase base — for resetting the read_pos when restarting. */
    const uint8_t* phrase_base;
    uint32_t       phrase_track_info;  /* the original 32-bit track header */
    /* Remaining bytecode wait (in ticks) before the next opcode. */
    int            delay_ticks;
    /* Currently sounding note state. */
    BgmVoicePhase  voice_phase;
    int            note_ticks_remaining; /* ticks until release transition */
    int            release_ticks_remaining;
    float          note_freq_hz;
    float          note_phase;
    float          peak_amp;             /* target amplitude during sustain */
    float          cur_amp;              /* current per-tick amplitude */
    uint8_t        velocity;
    /* Per-track meta state from the META commands. */
    uint8_t        track_volume;        /* EC_TrackVolume (0..0x7F) */
    uint8_t        track_pan;           /* EA_InstrumentPan (0..0x7F, 0x40=center) */
    uint8_t        instrument_volume;   /* E9_InstrumentVolume (0..0x7F) */
    int8_t         coarse_tune;         /* ED_InstrumentCoarseTune (signed semitones) */
    int8_t         fine_tune;           /* EE_InstrumentFineTune (signed cents) */
    /* Per-track waveform — different shape per track index so a 4-way
     * mix is more distinguishable than four identical sines. */
    int            waveform;            /* 0=sine 1=triangle 2=saw 3=square */
} BgmTrack;

typedef struct {
    bool       loaded;
    bool       ended;
    /* File contents (heap-owned). */
    uint8_t*   data;
    size_t     size;
    /* Composition walker. */
    const uint8_t* comp_base;     /* pointer to composition[0]'s opcode stream */
    const uint8_t* comp_pos;      /* next opcode */
    /* Loop stack (composition-level). One level supported. */
    const uint8_t* loop_start;
    /* The active phrase (NULL = none — we keep advancing composition). */
    BgmTrack   tracks[BGM_TRACK_COUNT];
    /* Tempo. */
    int        tempo_bpm;
    /* Global volume (set by track 0 META_E1_MasterVolume). */
    uint8_t    master_volume;
    /* Sample-rate counter — when it hits zero, advance one bytecode tick. */
    int        samples_until_next_tick;
    int        samples_per_tick;
} BgmPlayer;

static BgmPlayer s_bgm;

/* ---------------------------------------------------------------- helpers */

static uint32_t be_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint16_t be_u16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static float pitch_to_hz(uint8_t bgm_pitch) {
    /* BGM pitch is a 7-bit semitone counter. Calibration: pitch 49 ≈
     * A4 = 440 Hz (rough; the game's real instruments override via
     * keybase). */
    return 440.0f * powf(2.0f, ((float)bgm_pitch - 49.0f) / 12.0f);
}

/* Apply a track's coarse + fine tune to a base frequency. */
static float apply_tune(float base_hz, int8_t coarse, int8_t fine) {
    /* coarse is semitones; fine is cents (1/100 semitone). */
    float semitones = (float)coarse + ((float)fine / 100.0f);
    return base_hz * powf(2.0f, semitones / 12.0f);
}

/* Cheap non-sine waveforms. Phase argument in radians, returns
 * sample in [-1, 1]. */
static float wave_sample(int waveform, float phase) {
    const float TWO_PI = 6.28318530717958647692f;
    /* Normalize phase into [0, TWO_PI). */
    float p = fmodf(phase, TWO_PI);
    if (p < 0.0f) p += TWO_PI;
    float t = p / TWO_PI;          /* normalized 0..1 */
    switch (waveform) {
        case 0: /* sine */
            return sinf(phase);
        case 1: /* triangle */
            return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        case 2: /* sawtooth (band-limited would be nicer but adequate) */
            return 2.0f * t - 1.0f;
        case 3: /* square */
            return (t < 0.5f) ? 0.7f : -0.7f;
        default:
            return sinf(phase);
    }
}

static void bgm_recompute_samples_per_tick(void) {
    /* ticks_per_second = (tempo_bpm / 60) * BGM_TICKS_PER_BEAT */
    int tps = (s_bgm.tempo_bpm * BGM_TICKS_PER_BEAT) / 60;
    if (tps <= 0) tps = 144;
    s_bgm.samples_per_tick = BGM_RATE_HZ / tps;
    if (s_bgm.samples_per_tick < 1) s_bgm.samples_per_tick = 1;
}

/* ---------------------------------------------------------------- track parser */

/* Per-track META opcode arg counts from src/audio/sfx_player.c
 * SeqCmdArgCounts[]. */
static const int8_t kSeqCmdArgCounts[32] = {
    2, 1, 1, 1, 4, 3, 2, 0,
    2, 1, 1, 1, 1, 1, 1, 2,
    3, 1, 1, 0, 2, 1, 3, 1,
    0, 0, 0, 0, 3, 3, 3, 3,
};

/* Read the next bytecode opcode for a track and decide what to do:
 *   - DELAY: set track->delay_ticks and return (caller continues).
 *   - NOTE:  start a note (active for `length` ticks) and set delay=0
 *            so the next call reads the next opcode.
 *   - META:  apply the side effect, advance past args, loop.
 *   - END:   restart from phrase_base (replay phrase forever).
 *
 * Stays inside the file buffer — bounds-checked against s_bgm.data + size. */
static void bgm_track_advance(BgmTrack* tr) {
    const uint8_t* end = s_bgm.data + s_bgm.size;
    while (tr->read_pos && tr->read_pos < end) {
        uint8_t op = *tr->read_pos++;
        if (op == 0x00) {
            /* END — if we were inside an FE_Detour, return; otherwise
             * restart the phrase (forever-loop for the M5.2 demo). */
            if (tr->return_pos) {
                tr->read_pos = tr->return_pos;
                tr->return_pos = NULL;
                continue;
            }
            tr->read_pos = tr->phrase_base + ((tr->phrase_track_info >> 16) & 0xFFFF);
            tr->voice_phase = BGM_VOICE_OFF;
            tr->note_ticks_remaining = 0;
            tr->release_ticks_remaining = 0;
            tr->cur_amp = 0.0f;
            continue;
        }
        if (op < 0x78) {
            tr->delay_ticks = op;
            return;
        }
        if (op < 0x80) {
            if (tr->read_pos >= end) return;
            uint8_t nxt = *tr->read_pos++;
            tr->delay_ticks = ((op & 7) << 8) + nxt + 0x78;
            return;
        }
        if (op < 0xD4) {
            /* NOTE */
            if (tr->read_pos + 2 > end) return;
            uint8_t pitch    = op & 0x7F;
            uint8_t velocity = *tr->read_pos++;
            uint8_t lo       = *tr->read_pos++;
            int length;
            if (lo < 0xC0) {
                length = lo;
            } else {
                if (tr->read_pos >= end) return;
                uint8_t hi = *tr->read_pos++;
                length = ((lo & ~0xC0) << 8) + hi + 0xC0;
            }
            tr->voice_phase = BGM_VOICE_ATTACK;
            /* Apply track's coarse + fine tune to base pitch. Real BGMs
             * use this to transpose whole tracks (ED FE = -2 semitones
             * is common). */
            tr->note_freq_hz = apply_tune(pitch_to_hz(pitch),
                                          tr->coarse_tune, tr->fine_tune);
            tr->note_ticks_remaining = length;
            tr->release_ticks_remaining = 0;
            tr->velocity = velocity;
            /* Don't reset phase — phase continuity across notes reduces
             * inter-note clicks. The amplitude ramp does the smoothing. */
            /* peak_amp = (velocity * track_vol * ins_vol * master_vol) / scale */
            float base = (float)velocity / 127.0f;
            float tv   = (float)tr->track_volume / 127.0f;
            float iv   = (float)tr->instrument_volume / 127.0f;
            float mv   = (float)s_bgm.master_volume / 127.0f;
            tr->peak_amp = base * tv * iv * mv * 0.35f;
            tr->cur_amp = 0.0f;
            tr->delay_ticks = 0;
            return;
        }
        if (op < 0xE0) {
            /* reserved opcode — treat as no-op */
            continue;
        }
        /* META command 0xE0..0xFF */
        int nargs = kSeqCmdArgCounts[op - 0xE0];
        if (tr->read_pos + nargs > end) return;
        const uint8_t* args = tr->read_pos;
        tr->read_pos += nargs;
        switch (op) {
            case 0xE0: /* MasterTempo: 2 args, low byte = BPM */
                if (nargs >= 2) {
                    s_bgm.tempo_bpm = args[1];
                    if (s_bgm.tempo_bpm == 0) s_bgm.tempo_bpm = BGM_DEFAULT_TEMPO_BPM;
                    bgm_recompute_samples_per_tick();
                }
                break;
            case 0xE1: /* MasterVolume */
                if (nargs >= 1) s_bgm.master_volume = args[0];
                break;
            case 0xE9: /* InstrumentVolume */
                if (nargs >= 1) tr->instrument_volume = args[0];
                break;
            case 0xEA: /* InstrumentPan */
                if (nargs >= 1) tr->track_pan = args[0];
                break;
            case 0xEC: /* TrackVolume */
                if (nargs >= 1) tr->track_volume = args[0];
                break;
            case 0xED: /* InstrumentCoarseTune (signed semitones) */
                if (nargs >= 1) tr->coarse_tune = (int8_t)args[0];
                break;
            case 0xEE: /* InstrumentFineTune (signed cents) */
                if (nargs >= 1) tr->fine_tune = (int8_t)args[0];
                break;
            case 0xFC: /* Branch — 3 byte big-endian offset within phrase */
                if (nargs >= 3) {
                    uint32_t off = ((uint32_t)args[0] << 16) |
                                   ((uint32_t)args[1] << 8)  |
                                    (uint32_t)args[2];
                    const uint8_t* dest = tr->phrase_base + off;
                    if (dest >= s_bgm.data && dest < end) {
                        tr->read_pos = dest;
                    }
                }
                break;
            case 0xFE: /* Detour — like Branch but saves return address */
                if (nargs >= 3) {
                    uint32_t off = ((uint32_t)args[0] << 16) |
                                   ((uint32_t)args[1] << 8)  |
                                    (uint32_t)args[2];
                    const uint8_t* dest = tr->phrase_base + off;
                    if (dest >= s_bgm.data && dest < end) {
                        tr->return_pos = tr->read_pos; /* return here on END */
                        tr->read_pos = dest;
                    }
                }
                break;
            default:
                break;
        }
        /* fall through — keep advancing until we hit a delay or note */
    }
}

/* Per-tick update: drive the ADSR envelope phases + the delay-then-
 * advance bytecode reader. Each track runs independently. */
static void bgm_tick(void) {
    for (int t = 0; t < BGM_TRACK_COUNT; t++) {
        BgmTrack* tr = &s_bgm.tracks[t];
        if (!tr->read_pos) continue;

        /* ADSR. Attack and release are linear amplitude ramps in
         * tick-space; sustain holds amplitude at peak. */
        switch (tr->voice_phase) {
            case BGM_VOICE_ATTACK:
                tr->cur_amp += tr->peak_amp / (float)BGM_NOTE_ATTACK_TICKS;
                if (tr->cur_amp >= tr->peak_amp) {
                    tr->cur_amp = tr->peak_amp;
                    tr->voice_phase = BGM_VOICE_SUSTAIN;
                }
                break;
            case BGM_VOICE_SUSTAIN:
                if (tr->note_ticks_remaining > 0) {
                    tr->note_ticks_remaining--;
                    if (tr->note_ticks_remaining == 0) {
                        tr->voice_phase = BGM_VOICE_RELEASE;
                        tr->release_ticks_remaining = BGM_NOTE_RELEASE_TICKS;
                    }
                }
                break;
            case BGM_VOICE_RELEASE:
                tr->release_ticks_remaining--;
                tr->cur_amp -= tr->peak_amp / (float)BGM_NOTE_RELEASE_TICKS;
                if (tr->release_ticks_remaining <= 0 || tr->cur_amp <= 0.0f) {
                    tr->voice_phase = BGM_VOICE_OFF;
                    tr->cur_amp = 0.0f;
                }
                break;
            case BGM_VOICE_OFF:
            default:
                break;
        }

        if (tr->delay_ticks > 0) {
            tr->delay_ticks--;
        }
        if (tr->delay_ticks == 0) {
            bgm_track_advance(tr);
        }
    }
}

/* ---------------------------------------------------------------- composition */

static void bgm_load_phrase(uint16_t phrase_off_words) {
    const uint8_t* phrase_base = s_bgm.comp_base + (phrase_off_words << 2);
    if (phrase_base + 16 > s_bgm.data + s_bgm.size) return;

    for (int t = 0; t < BGM_TRACK_COUNT; t++) {
        BgmTrack* tr = &s_bgm.tracks[t];
        uint32_t info = be_u32(phrase_base + t * 4);
        uint32_t off  = (info >> 16) & 0xFFFF;
        if (off == 0) {
            tr->read_pos = NULL;
            continue;
        }
        tr->phrase_base       = phrase_base;
        tr->phrase_track_info = info;
        tr->read_pos          = phrase_base + off;
        tr->return_pos        = NULL;
        tr->delay_ticks       = 0;
        tr->voice_phase       = BGM_VOICE_OFF;
        tr->note_ticks_remaining = 0;
        tr->release_ticks_remaining = 0;
        tr->cur_amp           = 0.0f;
        tr->peak_amp          = 0.0f;
        tr->track_volume      = 0x7F;
        tr->track_pan         = 0x40;
        tr->instrument_volume = 0x7F;
        tr->coarse_tune       = 0;
        tr->fine_tune         = 0;
        tr->waveform          = t;     /* one shape per track index */
        bgm_track_advance(tr);
    }
}

static void bgm_advance_composition(void) {
    const uint8_t* end = s_bgm.data + s_bgm.size;
    while (s_bgm.comp_pos + 4 <= end) {
        uint32_t cmd = be_u32(s_bgm.comp_pos);
        s_bgm.comp_pos += 4;
        if (cmd == 0) {
            /* COMP_END — loop the whole composition. */
            s_bgm.comp_pos = s_bgm.comp_base;
            s_bgm.loop_start = NULL;
            continue;
        }
        uint8_t opcode = (cmd >> 28) & 0xF;
        uint32_t operand = cmd & 0x0FFFFFFF;
        switch (opcode) {
            case 1: /* PLAY_PHRASE */
                bgm_load_phrase((uint16_t)(operand & 0xFFFF));
                return;
            case 3: /* START_LOOP */
                s_bgm.loop_start = s_bgm.comp_pos;
                break;
            case 4: /* WAIT */
                return;
            case 5: /* END_LOOP — jump back unconditionally (M5.2 simplification) */
                if (s_bgm.loop_start) s_bgm.comp_pos = s_bgm.loop_start;
                break;
            default:
                /* Conditional loops / unknown — fall through. */
                break;
        }
    }
}

/* ---------------------------------------------------------------- loader */

static bool bgm_load_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        pm3ds_log(PM3DS_LOG_WARN, "audio_bgm: fopen('%s') failed", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0x24) { fclose(f); return false; }

    s_bgm.data = (uint8_t*)malloc(sz);
    if (!s_bgm.data) { fclose(f); return false; }
    if (fread(s_bgm.data, 1, sz, f) != (size_t)sz) {
        free(s_bgm.data); s_bgm.data = NULL;
        fclose(f);
        return false;
    }
    fclose(f);
    s_bgm.size = (size_t)sz;

    /* Header: BGMHeader (0x24 bytes). */
    if (memcmp(s_bgm.data, "BGM ", 4) != 0) {
        pm3ds_log(PM3DS_LOG_WARN, "audio_bgm: bad magic in %s", path);
        return false;
    }
    uint16_t comp0 = be_u16(s_bgm.data + 0x14);  /* compositions[0] */
    if (comp0 == 0) {
        pm3ds_log(PM3DS_LOG_WARN, "audio_bgm: composition[0] is empty");
        return false;
    }
    s_bgm.comp_base    = s_bgm.data + (comp0 << 2);
    s_bgm.comp_pos     = s_bgm.comp_base;
    s_bgm.tempo_bpm    = BGM_DEFAULT_TEMPO_BPM;
    s_bgm.master_volume = 0x7F;
    bgm_recompute_samples_per_tick();
    s_bgm.samples_until_next_tick = s_bgm.samples_per_tick;
    s_bgm.loaded = true;

    /* Advance to first PLAY_PHRASE so tracks are primed. */
    bgm_advance_composition();
    /* M5.2.9 root cause revised: NOT a stdio race — the 0x800 audio
     * thread stack was overflowing inside vfprintf when called from
     * inside bgm_load_file's deep nested call chain. Bumping the
     * stack to 0x4000 in audio_dsp.c fixes it; this log can stay. */
    pm3ds_log(PM3DS_LOG_INFO,
              "audio_bgm: loaded '%s' (%u bytes), tempo %d BPM, %d samples/tick",
              path, (unsigned)s_bgm.size, s_bgm.tempo_bpm, s_bgm.samples_per_tick);
    return true;
}

/* ---------------------------------------------------------------- fill */

void pm3ds_audio_bgm_fill(int16_t* out, int nsamples) {
    if (!s_bgm.loaded) {
        if (!bgm_load_file(PM3DS_AUDIO_BGM_PATH)) {
            memset(out, 0, nsamples * sizeof(int16_t));
            return;
        }
    }

    for (int i = 0; i < nsamples; i++) {
        /* M5.2.6 stereo mix: per-voice pan distributes amplitude
         * between L and R using an equal-power crossfade (cos/sin) so
         * total energy is preserved as a voice moves across the field.
         * Pan: 0 = full left, 0x40 = centre, 0x7F = full right. Each
         * track also uses its configured waveform (sine/triangle/saw/
         * square per track index) so a 4-voice chord is audibly
         * distinguishable from a stacked-sine flat sound. */
        float mixL = 0.0f, mixR = 0.0f;
        for (int t = 0; t < BGM_TRACK_COUNT; t++) {
            BgmTrack* tr = &s_bgm.tracks[t];
            if (tr->voice_phase == BGM_VOICE_OFF) continue;
            float step = 2.0f * 3.14159265358979323846f
                       * tr->note_freq_hz / (float)BGM_RATE_HZ;
            float s = wave_sample(tr->waveform, tr->note_phase) * tr->cur_amp;
            float pan_norm = (float)tr->track_pan / 127.0f;
            if (pan_norm < 0.0f) pan_norm = 0.0f;
            if (pan_norm > 1.0f) pan_norm = 1.0f;
            float angle = pan_norm * 1.57079632679f;  /* π/2 */
            mixL += s * cosf(angle);
            mixR += s * sinf(angle);
            tr->note_phase += step;
            if (tr->note_phase > 32768.0f) {
                tr->note_phase = fmodf(tr->note_phase,
                                       2.0f * 3.14159265358979323846f);
            }
        }
        /* clip to int16 range, per channel */
        if (mixL > 0.95f) mixL = 0.95f;
        if (mixL < -0.95f) mixL = -0.95f;
        if (mixR > 0.95f) mixR = 0.95f;
        if (mixR < -0.95f) mixR = -0.95f;
        out[i * 2 + 0] = (int16_t)(mixL * 32767.0f);  /* L */
        out[i * 2 + 1] = (int16_t)(mixR * 32767.0f);  /* R */

        /* Advance bytecode tick clock. */
        if (--s_bgm.samples_until_next_tick <= 0) {
            bgm_tick();
            /* If a phrase ran out (all tracks went NULL) walk the composition
             * for the next PLAY_PHRASE. */
            bool any = false;
            for (int t = 0; t < BGM_TRACK_COUNT; t++) {
                if (s_bgm.tracks[t].read_pos) { any = true; break; }
            }
            if (!any) bgm_advance_composition();
            s_bgm.samples_until_next_tick = s_bgm.samples_per_tick;
        }
    }
}
