/*
 * audio_endian.c — byte-swap Paper Mario's audio files after they are read
 * from the (big-endian) cart, so the engine's structs read correctly (M14).
 *
 * Each routine is called right after the au_read_rom() that loads that file
 * type (src/audio/core/engine.c, src/audio/load_banks.c). Byte streams
 * (sequence/track data, envelope command lists, ADPCM samples) are left
 * alone; the engine assembles any multi-byte argument inside them from
 * individual bytes. SEF sound tables are ragged and are swapped where they
 * are read (sfx_player.c) instead.
 */
#include "common.h"
#include "audio.h"

static inline void sw16(void* p) {
    u8* b = p;
    u8 t = b[0];
    b[0] = b[1];
    b[1] = t;
}

static inline void sw32(void* p) {
    u32 v;
    memcpy(&v, p, 4);
    v = __builtin_bswap32(v);
    memcpy(p, &v, 4);
}

void pm3ds_au_swap16s(void* p, s32 n) {
    for (s32 i = 0; i < n; i++) {
        sw16((u8*)p + i * 2);
    }
}

void pm3ds_au_swap32s(void* p, s32 n) {
    for (s32 i = 0; i < n; i++) {
        sw32((u8*)p + i * 4);
    }
}

void pm3ds_au_swap_sbn_header(SBNHeader* h) {
    sw32(&h->mdata.signature);
    sw32(&h->mdata.size);
    sw32(&h->fileListOffset);
    sw32(&h->numEntries);
    sw32(&h->fullFileSize);
    sw32(&h->versionOffset);
    sw32(&h->INIToffset);
}

void pm3ds_au_swap_init_header(INITHeader* h) {
    sw32(&h->mdata.signature);
    sw32(&h->mdata.size);
    pm3ds_au_swap16s(&h->bankListOffset, 6);
}

void pm3ds_au_swap_per_header(PERHeader* h) {
    sw32(&h->mdata.signature);
    sw32(&h->mdata.size);
}

/* PER data: BGMDrumInfo (12 bytes: u16 bankPatch, u16 keyBase, bytes) */
void pm3ds_au_swap_drums(void* p, s32 size) {
    for (s32 o = 0; o + 12 <= size; o += 12) {
        sw16((u8*)p + o);
        sw16((u8*)p + o + 2);
    }
}

/* PRG data: BGMInstrumentInfo (8 bytes: u16 bankPatch, bytes) */
void pm3ds_au_swap_instruments(void* p, s32 size) {
    for (s32 o = 0; o + 8 <= size; o += 8) {
        sw16((u8*)p + o);
    }
}

void pm3ds_au_swap_bank_list(InitBankEntry* e, s32 size) {
    for (s32 i = 0; i < size / (s32)sizeof(*e); i++) {
        sw16(&e[i].fileIndex);
    }
}

void pm3ds_au_swap_bk_header(BKHeader* h) {
    sw16(&h->signature);
    sw32(&h->size);
    sw32(&h->name);
    sw16(&h->format);
    pm3ds_au_swap16s(h->instruments, 16);
    pm3ds_au_swap16s(&h->instrumetsLength, 8);
}

/* Full CR bank: header + Instrument records + the loop states, codebooks and
 * envelope tables they reference (still file-relative offsets here; the
 * engine's swizzle turns them into pointers afterwards). Several instruments
 * can share a block, so remember what has been swapped. */
void pm3ds_au_swap_bk_file(BKFileBuffer* file, s32 size) {
    u8* base = (u8*)file;
    u32 done[64];
    s32 ndone = 0;
    #define ONCE(off) ({ s32 _seen = 0; for (s32 _k = 0; _k < ndone; _k++) if (done[_k] == (u32)(off)) _seen = 1; \
                         if (!_seen && ndone < 64) done[ndone++] = (u32)(off); !_seen; })

    pm3ds_au_swap_bk_header(&file->header);
    for (s32 i = 0; i < 16; i++) {
        u16 io = file->header.instruments[i];
        if (io == 0 || io + sizeof(Instrument) > (u32)size || !ONCE(io)) {
            continue;
        }
        Instrument* in = (Instrument*)(base + io);
        sw32(&in->wavData);
        sw32(&in->wavDataLength);
        sw32(&in->loopState);
        sw32(&in->loopStart);
        sw32(&in->loopEnd);
        sw32(&in->loopCount);
        sw32(&in->predictor);
        sw16(&in->codebookSize);
        sw16(&in->keyBase);
        sw32(&in->sampleRate);
        sw32(&in->envelopes);

        u32 lo = (u32)(uintptr_t)in->loopState;
        if (lo != 0 && lo + 32 <= (u32)size && ONCE(lo)) {
            pm3ds_au_swap16s(base + lo, 16);
        }
        u32 po = (u32)(uintptr_t)in->predictor;
        if (po != 0 && po + in->codebookSize <= (u32)size && ONCE(po)) {
            pm3ds_au_swap16s(base + po, in->codebookSize / 2);
        }
        u32 eo = (u32)(uintptr_t)in->envelopes;
        if (eo != 0 && eo + 4 <= (u32)size && ONCE(eo)) {
            EnvelopePreset* env = (EnvelopePreset*)(base + eo);
            s32 n = env->count;
            if (eo + 4 + n * 4 <= (u32)size) {
                pm3ds_au_swap16s(env->offsets, n * 2);
            }
        }
    }
    #undef ONCE
}

/* BGM: header, drum/instrument tables, composition streams and the phrase
 * track tables they reference (16 u32 each, relative to the composition). */
void pm3ds_au_swap_bgm(BGMHeader* bgm, s32 size) {
    u8* base = (u8*)bgm;
    BGMFileInfo* info = &bgm->info;
    u32 phrases[128];
    s32 nphrases = 0;

    sw32(&bgm->signature);
    sw32(&bgm->size);
    sw32(&bgm->name);
    pm3ds_au_swap16s(info->compositions, 8);

    if (info->drums != 0 && (info->drums << 2) + info->drumCount * 12 <= size) {
        pm3ds_au_swap_drums(base + (info->drums << 2), info->drumCount * 12);
    }
    if (info->instruments != 0 && (info->instruments << 2) + info->instrumentCount * 8 <= size) {
        pm3ds_au_swap_instruments(base + (info->instruments << 2), info->instrumentCount * 8);
    }
    for (s32 c = 0; c < 4; c++) {
        u32 co = (u32)info->compositions[c] << 2;
        if (co == 0 || co >= (u32)size) {
            continue;
        }
        for (u32 p = co; p + 4 <= (u32)size; p += 4) {
            sw32(base + p);
            u32 cmd;
            memcpy(&cmd, base + p, 4);
            if (cmd == 0) {
                break;
            }
            if ((cmd >> 12) == (1 << 16)) {   /* BGM_COMP_PLAY_PHRASE */
                u32 ph = co + ((cmd & 0xFFFF) << 2);
                s32 seen = 0;
                for (s32 k = 0; k < nphrases; k++) {
                    if (phrases[k] == ph) {
                        seen = 1;
                    }
                }
                if (!seen && ph + 64 <= (u32)size && nphrases < 128) {
                    phrases[nphrases++] = ph;
                    pm3ds_au_swap32s(base + ph, 16);
                }
            }
        }
    }
}

void pm3ds_au_swap_mseq(MSEQHeader* m, s32 size) {
    sw32(&m->signature);
    sw32(&m->size);
    sw32(&m->name);
    sw16(&m->trackSettingsOffset);
    sw16(&m->dataStart);
    if (m->trackSettingsOffset != 0) {
        MSEQTrackData* t = (MSEQTrackData*)((u8*)m + m->trackSettingsOffset);
        for (s32 i = 0; i < m->trackSettingsCount && (u8*)(t + 1) <= (u8*)m + size; i++, t++) {
            sw16(&t->time);
            sw16(&t->delta);
            sw16(&t->goal);
        }
    }
}

void pm3ds_au_swap_sef_header(SEFHeader* h) {
    sw32(&h->mdata.signature);
    sw32(&h->mdata.size);
    sw32(&h->name);
    pm3ds_au_swap16s(h->sections, 9);
}
