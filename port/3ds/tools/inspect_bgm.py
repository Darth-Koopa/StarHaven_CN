#!/usr/bin/env python3
"""
M5.2 prep: BGM file format inspector.

Dumps the structure of a Paper Mario .bgm file (the custom binary
sequence format the decomp's bgm_player.c interprets at runtime).
Cross-referenced with src/audio.h's BGMHeader / BGMFileInfo /
BGMCompositionCommands and src/audio/bgm_player.c's
au_bgm_player_read_composition + au_bgm_load_phrase.

Usage:
    python3 port/3ds/tools/inspect_bgm.py assets/pal/audio/11_134.bgm

Output sections:
    HEADER      — magic / size / name / instrument & drum counts
    COMPOSITION — the u32 opcode stream interpreted by
                  au_bgm_player_read_composition. Each command's
                  top 4 bits are the BGM_COMP_* opcode; the rest is
                  operand (e.g. a 16-bit phrase byte offset).
    INSTRUMENTS — BGMInstrumentInfo entries (bankPatch, vol, pan, ...)
    DRUMS       — BGMDrumInfo entries
    PHRASE TABLE — for each PLAY_PHRASE in the composition stream,
                  list the 4 track entries it references.

The eventual M5.2 MSEQ interpreter port reads exactly this byte stream;
this tool is a debugging aid to peek at what's there.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

# From src/audio.h BGMCompositionCommands. Top 4 bits of the u32 opcode.
BGM_COMP = {
    0: "END",
    1: "PLAY_PHRASE",
    3: "START_LOOP",
    4: "WAIT",
    5: "END_LOOP",
    6: "END_COND_LOOP_FALSE",
    7: "END_COND_LOOP_TRUE",
}

# Per-track byte-stream opcodes from src/audio/bgm_player.c (the dispatch
# at ~line 1140). Notes encode pitch in the low 7 bits, then velocity
# and length follow. Delays are 1- or 2-byte values. Meta commands
# (0xE0..0xFF) have a fixed arg count from SeqCmdArgCounts[] (in
# src/audio/sfx_player.c).
TRACK_SEQ_CMD_NAMES = [
    "E0_MasterTempo",        "E1_MasterVolume",       "E2_MasterDetune",
    "E3_unknown",            "E4_MasterTempoFade",    "E5_MasterVolumeFade",
    "E6_MasterEffect",       "E7_NOP",                "E8_TrackOverridePatch",
    "E9_InstrumentVolume",   "EA_InstrumentPan",      "EB_InstrumentReverb",
    "EC_TrackVolume",        "ED_InstrumentCoarseTune","EE_InstrumentFineTune",
    "EF_TrackDetune",        "F0_TrackTremolo",       "F1_TrackTremoloRate",
    "F2_TrackTremoloDepth",  "F3_TrackTremoloStop",   "F4_SubTrackRandomPan",
    "F5_UseInstrument",      "F6_InstrumentVolumeLerp","F7_ReverbType",
    "F8_NOP", "F9_NOP", "FA_NOP", "FB_NOP",
    "FC_Branch",             "FD_EventTrigger",       "FE_Detour",
    "FF_Special",
]

TRACK_SEQ_CMD_ARGS = [
    2, 1, 1, 1, 4, 3, 2, 0,
    2, 1, 1, 1, 1, 1, 1, 2,
    3, 1, 1, 0, 2, 1, 3, 1,
    0, 0, 0, 0, 3, 3, 3, 3,
]


def pitch_to_note_name(pitch: int) -> str:
    """Pitch 0 corresponds to the bgm_player's note numbering. The exact
    keybase mapping depends on the instrument, but for display we treat
    pitch as MIDI-like semitones above some reference."""
    names = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
    octave = pitch // 12
    return f"{names[pitch % 12]}{octave}"


def decode_track(buf: bytes, start: int, max_bytes: int = 256) -> list:
    """Walk the per-track byte stream from `start`. Returns a list of
    (offset, raw_bytes, mnemonic, args_dict)."""
    out = []
    p = start
    end = min(len(buf), start + max_bytes)
    while p < end:
        if p >= len(buf):
            break
        op = buf[p]
        op_off = p
        p += 1
        if op == 0x00:
            out.append((op_off, [op], "END_OR_RETURN", {}))
            break
        elif op < 0x78:
            out.append((op_off, [op], "DELAY_SHORT", {"ticks": op}))
        elif op < 0x80:
            # long delay: ((op & 7) << 8) + next + 0x78
            if p >= len(buf):
                out.append((op_off, [op], "DELAY_LONG_TRUNC", {}))
                break
            nxt = buf[p]
            p += 1
            ticks = ((op & 7) << 8) + nxt + 0x78
            out.append((op_off, [op, nxt], "DELAY_LONG", {"ticks": ticks}))
        elif op < 0xD4:
            # note: op = 0x80 + pitch (pitch = op & 0x7F)
            if p + 2 > len(buf):
                out.append((op_off, [op], "NOTE_TRUNC", {}))
                break
            pitch = op & 0x7F
            velocity = buf[p]; p += 1
            length_lo = buf[p]; p += 1
            raw = [op, velocity, length_lo]
            length = length_lo
            if length_lo >= 0xC0:
                if p >= len(buf):
                    out.append((op_off, raw, "NOTE_TRUNC_LEN", {}))
                    break
                length_hi = buf[p]; p += 1
                raw.append(length_hi)
                length = ((length_lo & ~0xC0) << 8) + length_hi + 0xC0
            out.append((op_off, raw, "NOTE",
                        {"pitch": pitch, "name": pitch_to_note_name(pitch),
                         "vel": velocity, "len": length}))
        elif op < 0xE0:
            # 0xD4..0xDF — reserved in this version
            out.append((op_off, [op], f"RESERVED_{op:02X}", {}))
        else:
            idx = op - 0xE0
            nargs = TRACK_SEQ_CMD_ARGS[idx]
            args = []
            for _ in range(nargs):
                if p >= len(buf):
                    break
                args.append(buf[p]); p += 1
            raw = [op] + args
            name = TRACK_SEQ_CMD_NAMES[idx]
            out.append((op_off, raw, f"META_{name}", {"args": args}))
            # FC_Branch / FE_Detour change control flow but we don't follow
            # them in the inspector; we just continue linearly.
    return out


def be_u32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def be_u16(buf: bytes, off: int) -> int:
    return struct.unpack_from(">H", buf, off)[0]


def be_s32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">i", buf, off)[0]


def dump_header(buf: bytes) -> dict:
    sig = buf[0:4]
    size = be_s32(buf, 4)
    name = buf[8:12].decode("ascii", errors="replace").rstrip()
    # 0x0C: pad
    info_off = 0x10
    timing = buf[info_off]
    compositions = [be_u16(buf, info_off + 4 + i * 2) for i in range(4)]
    drums_off = be_u16(buf, info_off + 0xC)
    drum_count = be_u16(buf, info_off + 0xE)
    ins_off = be_u16(buf, info_off + 0x10)
    ins_count = be_u16(buf, info_off + 0x12)
    return dict(
        sig=sig,
        size=size,
        name=name,
        timing_preset=timing,
        compositions=compositions,
        drums_off=drums_off,
        drum_count=drum_count,
        ins_off=ins_off,
        ins_count=ins_count,
    )


def decode_composition(buf: bytes, comp_off: int, max_words: int = 64) -> list:
    """Walk the u32 composition opcode stream until BGM_COMP_END (0) or
    max_words. Returns a list of (offset, raw_u32, opcode_name, operand)."""
    out = []
    p = comp_off
    for _ in range(max_words):
        if p + 4 > len(buf):
            break
        word = be_u32(buf, p)
        if word == 0:
            out.append((p, word, "END", 0))
            break
        opcode = (word >> 28) & 0xF
        operand = word & 0x0FFFFFFF
        name = BGM_COMP.get(opcode, f"OPCODE_{opcode:X}")
        out.append((p, word, name, operand))
        p += 4
    return out


def dump_phrase(buf: bytes, phrase_off_relative_to_comp: int, comp_start: int) -> list:
    """A phrase is 4 consecutive u32 track-info entries. Each entry's top
    16 bits are a byte offset (from the phrase start) to the track's
    bgm_player_read byte stream; the lower 16 bits encode polyphony /
    instrument / link flags (see au_bgm_load_phrase)."""
    phrase_off = comp_start + (phrase_off_relative_to_comp << 2)
    tracks = []
    for i in range(4):
        if phrase_off + 4 * (i + 1) > len(buf):
            tracks.append((i, 0, 0, 0))
            continue
        info = be_u32(buf, phrase_off + i * 4)
        read_off = (info >> 16) & 0xFFFF
        flags = info & 0xFFFF
        polyphony = (info >> 13) & 0x7
        tracks.append((i, info, read_off, flags))
    return phrase_off, tracks


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = Path(sys.argv[1])
    buf = path.read_bytes()
    print(f"=== {path} ({len(buf)} bytes) ===\n")

    h = dump_header(buf)
    print("HEADER:")
    print(f"  signature       : {h['sig'].decode('ascii', errors='replace')}")
    print(f"  size            : {h['size']:#08x} ({h['size']} bytes)")
    print(f"  name            : '{h['name']}'")
    print(f"  timingPreset    : {h['timing_preset']}")
    print(f"  compositions[4] : {[hex(c) for c in h['compositions']]} (offsets / 4)")
    print(f"  drums   @ {h['drums_off']:#06x} × {h['drum_count']}")
    print(f"  instruments @ {h['ins_off']:#06x} × {h['ins_count']}")
    print()

    # Walk each non-zero composition.
    for ci, comp_off_div4 in enumerate(h["compositions"]):
        if comp_off_div4 == 0:
            continue
        comp_off = comp_off_div4 << 2
        print(f"COMPOSITION[{ci}] @ offset {comp_off:#x}:")
        cmds = decode_composition(buf, comp_off)
        for off, raw, name, operand in cmds:
            print(f"  {off:#06x}: {raw:#010x}  {name:<22}  operand={operand:#x}")
        print()

        # Find PLAY_PHRASE commands and dump the 4-track-entry block each
        # references. Phrase offset is u16 stored in `operand & 0xFFFF`.
        for off, raw, name, operand in cmds:
            if name != "PLAY_PHRASE":
                continue
            phrase_off_units = operand & 0xFFFF
            phrase_off, tracks = dump_phrase(buf, phrase_off_units, comp_off)
            print(f"  PHRASE @ comp_off+{phrase_off_units<<2:#x} = {phrase_off:#x}:")
            for ti, info, read_off, flags in tracks:
                marker = " <empty>" if read_off == 0 else ""
                print(f"    track[{ti}]: info={info:#010x}  bgm_read_off={read_off:#06x}  flags={flags:#06x}{marker}")
            print()

            # Dump each non-empty track's byte stream.
            for ti, info, read_off, flags in tracks:
                if read_off == 0:
                    continue
                track_start = phrase_off + read_off
                print(f"    --- TRACK[{ti}] BYTECODE @ {track_start:#x} ---")
                ops = decode_track(buf, track_start, max_bytes=128)
                for off, raw, name, args in ops:
                    raw_hex = " ".join(f"{b:02x}" for b in raw)
                    arg_str = ""
                    if "ticks" in args:
                        arg_str = f"ticks={args['ticks']}"
                    elif name == "NOTE":
                        arg_str = f"{args['name']:<4}  vel={args['vel']:#x}  len={args['len']}"
                    elif "args" in args and args["args"]:
                        arg_str = "args=" + ",".join(f"{a:#x}" for a in args["args"])
                    print(f"      {off:#06x}: {raw_hex:<18}  {name:<22}  {arg_str}")
                print()

    # Instrument table dump.
    if h["ins_off"] and h["ins_count"]:
        print(f"INSTRUMENTS @ {h['ins_off']:#x}:")
        for i in range(h["ins_count"]):
            off = h["ins_off"] + i * 8
            if off + 8 > len(buf):
                break
            bank_patch = be_u16(buf, off + 0)
            vol = buf[off + 2]
            pan = buf[off + 3]
            reverb = buf[off + 4]
            coarse = buf[off + 5]
            fine = buf[off + 6]
            print(
                f"  ins[{i}]: bank={bank_patch>>8:#04x} patch={bank_patch&0xFF:#04x}  "
                f"vol={vol:#x}  pan={pan-128 if pan>127 else pan:+}  rev={reverb}  "
                f"coarse={coarse-256 if coarse>127 else coarse:+}  fine={fine-256 if fine>127 else fine:+}"
            )
        print()

    # Drum table dump.
    if h["drums_off"] and h["drum_count"]:
        print(f"DRUMS @ {h['drums_off']:#x}:")
        for i in range(h["drum_count"]):
            off = h["drums_off"] + i * 12
            if off + 12 > len(buf):
                break
            bank_patch = be_u16(buf, off + 0)
            keybase = be_u16(buf, off + 2)
            vol = buf[off + 4]
            pan = buf[off + 5]
            reverb = buf[off + 6]
            print(
                f"  drum[{i}]: bank={bank_patch>>8:#04x} patch={bank_patch&0xFF:#04x}  "
                f"keybase={keybase}  vol={vol:#x}  pan={pan-128 if pan>127 else pan:+}  rev={reverb}"
            )
        print()


if __name__ == "__main__":
    main()
