# Star Haven

A native Nintendo 3DS port of **Paper Mario** (Nintendo 64), built from the community
decompilation. The game's own code runs on the 3DS; nothing is emulated.

**Star Haven contains no game data.** You need your own copy of the game: Star Haven reads
your ROM from the SD card when it starts.

## What you need

- A **New Nintendo 3DS, New 3DS XL, New 2DS XL** (or New 3DS LL) for 60 fps. The original
  3DS and 2DS run it at about 30 fps, with a few extras off to get there (no reverb, no 3D,
  shadows only under Mario and your partner), and only the CIA: the `.3dsx` doesn't get
  enough memory there.
- Custom firmware (Luma3DS) and a CIA installer such as FBI, or the Homebrew Launcher for
  the `.3dsx`.
- A dump of **Paper Mario** as `.z64`, `.n64` or `.v64`:
  - Europe (En, Fr, De, Es), the 48 MB or the 64 MB dump, or
  - USA.

  The Japanese version is not supported.
- For sound: the DSP firmware dump at `/3ds/dspfirm.cdc`. Most custom-firmware setups have
  it; if yours doesn't, run DSP1 once.

## Installing

1. Install `StarHaven.cia` with FBI (or put `StarHaven.3dsx` in `/3ds/` for the Homebrew
   Launcher).
2. Copy your ROM to the SD card as `/3ds/StarHaven/baserom.z64`. Any file ending in
   `.z64`, `.n64` or `.v64` in that folder works too.
3. Start Star Haven. Loading the ROM takes a few seconds.

If the ROM is missing, unreadable or a version Star Haven doesn't know, both screens say
what's wrong and how to fix it.

**Updating from 1.0.0 or 1.0.1:** 1.0.2 has a new title ID (the old one was also used by
other homebrew, and installing either one removed the other), so it installs next to the old
version. Delete the old Star Haven in FBI (Titles) or System Settings → Data Management.
Your saves are on the SD card and are kept.

## Controls

| 3DS | Game |
|---|---|
| Circle Pad | Move |
| A / B | A / B |
| L | Z |
| R | R |
| Select | L |
| Start | Start |
| X | C-Down (partner ability) |
| Y, ZL | C-Left |
| D-Pad, C-Stick | C buttons |
| ZR (hold) | Fast-forward |

Every button can be changed under **Options → Controls**. The 3D slider works (not on the
original 3DS and 2DS).

## Bottom screen

- **Stats, Map, Party** tabs. Tap a partner on the Party tab to switch to them.
- **Tattle**: every enemy Goombario has tattled so far. Tap one to read the whole tattle.
- **Found**: for each area, how many star pieces, badges, hidden blocks and panels, and item
  spots you have collected (gifts from characters are not counted; a randomizer file shows
  only the totals).
- **Save** on the Stats tab, tapped twice, saves the game wherever the pause menu could open.
  Loading puts you back on the spot on a map with a save block, and at the entrance you came
  in by anywhere else.
- **Items** opens the item menu.
- In battle: your HP, FP and Star Power, the battle menus as buttons to tap, and an A/B pad
  for action commands.
- **Fast >>** turns fast-forward on and off.
- **Options**:
  - **Screen**: *Original* (4:3), *Fill* or *Wide*.
  - **3D depth**: how deep the 3D slider goes, from *Low* to *Deepest*.
  - **Frame rate**: 60 fps (in-between frames are added) or the original 30 fps. With the 3D
    slider up, 60 fps holds where there is time to draw both eyes twice and drops to 30
    elsewhere; battles stay at 30.
  - **Text speed**: Normal, Fast or Instant.
  - **Fast-forward**: speed (2x to 4x), and whether the button is held or toggles.
  - **Update check**: when Star Haven starts, it asks GitHub whether a newer version is out
    and, if so, says so on the bottom screen (*Skip* stops it mentioning that version). It
    downloads and installs nothing. Off here, from the next start.
  - **Controls**: remap any button.

Settings are remembered.

## Randomizer

When you pick an empty file, Star Haven asks how you want to play:

- **Classic**: the original game.
- **Randomizer**: items and badges are found in new places. The story keeps its normal order.
  You can also shuffle:
  - coins and star pieces,
  - key items, which always land somewhere you can reach before you need them,
  - shop stock,
  - gifts from characters,
  - key items into hidden panels and hidden blocks (hard to find without the spoiler log).

  Leave the **Seed** on *Random*, or type one in to replay or share a game: the same seed
  with the same settings gives the same game.

Each randomized game writes a spoiler log to `/3ds/StarHaven/spoiler_<seed>.txt` listing
where everything went. The seed is shown on the Stats tab.

## Files on the SD card (`/3ds/StarHaven/`)

| File | |
|---|---|
| `baserom.z64` | your ROM |
| `save.fla` | your save files (`save_us.fla` with the US version) |
| `port.cfg` | settings |
| `spoiler_<seed>.txt` | randomizer spoiler logs |
| `log.txt` | warnings, useful in bug reports |

## Known issues

- A few N64 graphics effects are approximations.

## Legal

Star Haven is a fan project. It is not affiliated with or endorsed by Nintendo. *Paper Mario*
is a trademark of Nintendo. Star Haven ships no Nintendo game data: images, models, sounds and
text are read from your own ROM at startup. Don't ask for ROMs.

Built on the work of the [Paper Mario decompilation project](https://github.com/pmret/papermario),
[devkitPro](https://devkitpro.org) (libctru, citro3d) and, for the randomizer's world logic, the
[Paper Mario Randomizer](https://github.com/icebound777/PMR-SeedGenerator) (MIT).
