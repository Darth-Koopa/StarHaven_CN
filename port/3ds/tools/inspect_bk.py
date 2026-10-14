#!/usr/bin/env python3
"""Inspect a Paper Mario BK (instrument bank) file.

BK format (big-endian, derived from src/audio.h::BKHeader and
src/audio/core/engine.c::au_load_BK_to_bank):

  0x00 u16 signature      = 'BK'   (0x424B)
  0x02 u16 pad
  0x04 s32 size
  0x08 s32 name           (4-char ASCII tag)
  0x0C u16 format         = 'CR' / 'DR' / 'SR'
  0x0E u8  swizzled
  0x0F u8  pad
  0x10 u16 pad (the struct has weird overlapping offsets in decomp)
  0x12 u16 instruments[16]      -- file-relative offset to each Instrument
                                   record. 0 = empty slot.
  0x32 u16 instrumentsLength
  0x34 u16 loopStatesStart
  0x36 u16 loopStatesLength
  0x38 u16 predictorsStart
  0x3A u16 predictorsLength
  0x3C u16 envelopesStart
  0x3E u16 envelopesLength

Instrument record (0x30 bytes, file-internal pointers are file-relative):

  0x00 u32 wavData            -- offset relative to whole-file start
                                 (NOT relative to BKFileBuffer base — wave
                                 data lives at bkFileOffset + this)
  0x04 u32 wavDataLength
  0x08 u32 loopState          -- offset to ADPCM_STATE (16 s16 samples)
  0x0C s32 loopStart
  0x10 s32 loopEnd
  0x14 s32 loopCount
  0x18 u32 predictor          -- offset to VADPCM codebook (s16 *)
  0x1C u16 codebookSize       -- number of predictor entries
  0x1E u16 keyBase            -- MIDI note this sample is tuned to
                                 (the pivot for pitch shifting)
  0x20 u32 sampleRate         -- Hz, unioned with f32 pitchRatio after swizzle
  0x24 u8  type               -- codec/format flag
  0x25 u8  useDma
  0x26..0x2B pad
  0x2C u32 envelopes          -- offset to EnvelopePreset

Usage: inspect_bk.py path/to/file.bk
"""

import struct
import sys
from pathlib import Path


def be_u16(b, o): return struct.unpack_from('>H', b, o)[0]
def be_s16(b, o): return struct.unpack_from('>h', b, o)[0]
def be_u32(b, o): return struct.unpack_from('>I', b, o)[0]
def be_s32(b, o): return struct.unpack_from('>i', b, o)[0]


def decode(path: Path) -> int:
    data = path.read_bytes()
    sig = be_u16(data, 0)
    if sig != 0x424B:
        print(f"ERROR: signature 0x{sig:04X} != 'BK' (0x424B)")
        return 1

    name = data[8:12].decode('ascii', errors='replace')
    fmt = data[0x0C:0x0E].decode('ascii', errors='replace')
    size = be_s32(data, 4)
    swizzled = data[0x0E]

    instr_offsets = [be_u16(data, 0x12 + 2 * i) for i in range(16)]
    instr_len = be_u16(data, 0x32)
    loop_start_off = be_u16(data, 0x34)
    loop_len = be_u16(data, 0x36)
    pred_start = be_u16(data, 0x38)
    pred_len = be_u16(data, 0x3A)
    env_start = be_u16(data, 0x3C)
    env_len = be_u16(data, 0x3E)

    print(f"BK '{name}' format={fmt!r} swizzled={swizzled} declared_size=0x{size:X}")
    print(f"  file size on disk    : 0x{len(data):X} ({len(data)} bytes)")
    print(f"  instrumentsLength    : 0x{instr_len:X}")
    print(f"  loopStates  @ 0x{loop_start_off:04X} len 0x{loop_len:X}")
    print(f"  predictors  @ 0x{pred_start:04X} len 0x{pred_len:X}")
    print(f"  envelopes   @ 0x{env_start:04X} len 0x{env_len:X}")

    filled = sum(1 for o in instr_offsets if o)
    print(f"  instruments filled   : {filled} / 16")

    for slot, off in enumerate(instr_offsets):
        if off == 0:
            continue
        if off + 0x30 > len(data):
            print(f"  slot {slot:2}: offset 0x{off:04X} truncated")
            continue
        wav      = be_u32(data, off + 0x00)
        wav_len  = be_u32(data, off + 0x04)
        loopst   = be_u32(data, off + 0x08)
        ls       = be_s32(data, off + 0x0C)
        le       = be_s32(data, off + 0x10)
        lc       = be_s32(data, off + 0x14)
        pred     = be_u32(data, off + 0x18)
        cbk_sz   = be_u16(data, off + 0x1C)
        key_base = be_u16(data, off + 0x1E)
        sr       = be_u32(data, off + 0x20)
        ctype    = data[off + 0x24]
        env_off  = be_u32(data, off + 0x2C)
        print(f"  slot {slot:2}: instOff=0x{off:04X}"
              f" wav@0x{wav:06X} wlen=0x{wav_len:X}"
              f" key={key_base} rate={sr}Hz"
              f" loop[{ls}..{le}]x{lc}"
              f" pred@0x{pred:04X} cbk={cbk_sz}"
              f" env@0x{env_off:04X} type={ctype}")

    return 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    sys.exit(decode(Path(sys.argv[1])))
