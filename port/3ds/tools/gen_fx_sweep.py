#!/usr/bin/env python3
"""Generate port/3ds/source/n64_shim/fx_sweep_table.inc for PM3DS_FX_SWEEP.

One EVT script per (effect, subtype) pair, with the arguments of a real
PlayEffect(...) call from the game's scripts (src/). Position variables
LVar0-2 are filled with a spot beside Mario by FXS_PROLOGUE, LVar3-5 with a
second spot, LVar6-E with 30; anything else that isn't a literal (map vars,
array vars, local defines, expressions) becomes 0 for the subtype and 30
elsewhere. Effects that no script spawns get the hand-picked calls in EXTRA.
usage: gen_fx_sweep.py   (run from the repo root)
"""
import os, re, sys

SRC = 'src'
OUT = 'port/3ds/source/n64_shim/fx_sweep_table.inc'

LITERAL = re.compile(r'^(-?\d+|-?0x[0-9A-Fa-f]+|Float\(-?[\d.]+f?\)|LVar[0-9A-F]|true|false)$')
GLOBAL_ENUM = re.compile(r'^(FX_[A-Z0-9_]+|EMOTE_[A-Z0-9_]+)$')

# effects no script spawns: (subtype, args after the subtype), see PlayEffect_impl
EXTRA = {
    'EFFECT_3D': '0, LVar0, LVar1, LVar2, LVar3, LVar4, LVar5, 30, LVarF',
    'EFFECT_46': '0, (Bytecode) &gPlayerStatus, Float(1.0), 60',
    'EFFECT_86': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_ATTACK_RESULT_TEXT': '0, LVar0, LVar1, LVar2, Float(3.0), 60',
    'EFFECT_BREAKING_JUNK': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_CHAPTER_CHANGE': '1, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_CLOUD_TRAIL': '0, LVar0, LVar1, LVar2',
    'EFFECT_DAMAGE_INDICATOR': '0, LVar0, LVar1, LVar2, Float(10.0), Float(-55.0), 7, LVarF',
    'EFFECT_DEBUFF': '0, LVar0, LVar1, LVar2',
    'EFFECT_DISABLE_X': '0, LVar0, LVar1, LVar2, 1',
    'EFFECT_FALLING_LEAVES': '0, LVar0, LVar1, LVar2',
    'EFFECT_FIRE_FLOWER': '0, LVar0, LVar1, LVar2, 60',
    'EFFECT_FLOATING_ROCK': '0, LVar0, LVar1, LVar2, LVar1, Float(-0.5), Float(-0.1), Float(1.0), 60',
    'EFFECT_FLOWER_SPLASH': 'LVar0, LVar1, LVar2, Float(0.0)',
    'EFFECT_FLOWER_TRAIL': '0, LVar0, LVar1, LVar2, Float(0.0), Float(0.0)',
    'EFFECT_FOOTPRINT': 'LVar0, LVar1, LVar2, Float(0.0), Float(1.0)',
    'EFFECT_GREEN_IMPACT': '0, LVar0, LVar1, LVar2, Float(1.0)',
    'EFFECT_ICE_PILLAR': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_ICE_SHARD': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_LIL_OINK': '0, LVar0, LVar1, LVar2, Float(0.0), 0',
    'EFFECT_PARTNER_BUFF': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_PEACH_STAR_BEAM': '0, LVar0, LVar1, LVar2, Float(1.0), 0',
    'EFFECT_PINK_SPARKLES': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_QUIZMO_ASSISTANT': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_QUIZMO_STAGE': '0, LVar0, LVar1, LVar2',
    'EFFECT_RISING_BUBBLE': '0, LVar0, LVar1, LVar2, Float(0.0)',
    'EFFECT_SLEEP_BUBBLE': '0, LVar0, LVar1, LVar2, Float(1.0), Float(0.0), LVarF',
    'EFFECT_SPIKY_WHITE_AURA': '0, LVar0, LVar1, LVar2, 60',
    'EFFECT_SPIRIT_CARD': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_STAR_OUTLINE': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_STARS_BURST': '0, LVar0, LVar1, LVar2, Float(1.0), Float(1.0), 10',
    'EFFECT_STAR_SPIRITS_ENERGY': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_STARS_SPREAD': '0, LVar0, LVar1, LVar2, 10, 10',
    'EFFECT_STEAM_BURST': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_TATTLE_WINDOW': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_THROW_SPINY': '0, LVar0, LVar1, LVar2, LVar3, LVar4, LVar5, Float(1.0), 30',
    'EFFECT_THUNDERBOLT_RING': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_UNDERWATER': '0, Float(-50.0), Float(20.0), Float(0.0), Float(1.0), 60',
    'EFFECT_WATER_BLOCK': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
    'EFFECT_WATER_FOUNTAIN': '0, LVar0, LVar1, LVar2, Float(1.0), 60',
}


def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def calls():
    pat = re.compile(r'PlayEffect\((EFFECT_[A-Z0-9_]+)((?:[^()]|\([^()]*\))*)\)')
    for root, _, files in os.walk(SRC):
        for f in sorted(files):
            if not f.endswith('.c'):
                continue
            path = os.path.join(root, f)
            text = open(path, errors='replace').read()
            for m in pat.finditer(text):
                rest = m.group(2).lstrip(',').strip()
                yield m.group(1), split_args(rest) if rest else [], path


# PlayEffect_impl argument layout: where x/y/z sit (default: right after the subtype)
NO_SUBTYPE = {'EFFECT_00', 'EFFECT_BIG_SMOKE_PUFF', 'EFFECT_FLOWER_SPLASH', 'EFFECT_CLOUD_PUFF',
              'EFFECT_FOOTPRINT', 'EFFECT_FLOATING_FLOWER', 'EFFECT_SNOWFLAKE'}
POS_AT = {'EFFECT_EMOTE': 2, 'EFFECT_SNOWFALL': None}
# model/collider IDs of the map the call came from: meaningless anywhere else
SKIP = {'EFFECT_BOMBETTE_BREAKING'}


def normalize(eff, args):
    out, unknown = [], 0
    first = 0 if eff in NO_SUBTYPE else POS_AT.get(eff, 1)
    for i, a in enumerate(args):
        if first is not None and first <= i < first + 3:
            out.append(f'LVar{i - first}')   # always beside Mario, whatever the map used
        elif eff == 'EFFECT_EMOTE' and i == 1:
            out.append('0')                  # Npc*: none, use the position
        elif (LITERAL.match(a) or GLOBAL_ENUM.match(a)) and not a.startswith('LVar'):
            out.append(a)
        elif a.startswith('LVar') and i > 0:
            # a script variable: keep it (the prologue gives it a value) but prefer
            # calls that pass a literal here (e.g. a scale, not whatever LVar3 holds)
            unknown += 1
            out.append(a)
        else:
            unknown += 1
            out.append('0' if i == 0 else '30')   # (a variable subtype would index tables with 30)
    return out, unknown


best = {}
for eff, args, path in calls():
    if eff in SKIP:
        continue
    norm, unknown = normalize(eff, args)
    key = (eff, '' if eff in NO_SUBTYPE or not norm else norm[0])
    if key not in best or unknown < best[key][1]:
        best[key] = (norm, unknown, path)

entries = [(eff, sub, ', '.join(norm), os.path.relpath(path, SRC))
           for (eff, sub), (norm, _, path) in sorted(best.items())]
for eff, args in sorted(EXTRA.items()):
    entries.append((eff, '' if eff in NO_SUBTYPE else args.split(',')[0].strip(), args, 'hand-picked'))

with open(OUT, 'w') as f:
    f.write('/* generated by port/3ds/tools/gen_fx_sweep.py -- do not edit */\n')
    for i, (eff, sub, args, origin) in enumerate(entries):
        f.write(f'static EvtScript fxs_{i} = {{ FXS_PROLOGUE PlayEffect({eff}, {args}) Return End }};  /* {origin} */\n')
    f.write('static const struct { EvtScript* script; const char* name; } fx_sweep_table[] = {\n')
    for i, (eff, sub, args, origin) in enumerate(entries):
        f.write(f'    {{ &fxs_{i}, "{(eff[7:] + " " + sub).strip()}" }},\n')
    f.write('};\n')
print(len(entries), 'entries ->', OUT)
