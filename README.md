# Star Haven

A native Nintendo 3DS port of **Paper Mario** (Nintendo 64), built from the
[Paper Mario decompilation](https://github.com/pmret/papermario). The game's own C code runs
on the 3DS; nothing is emulated.

<p align="center">
  <img src="port/3ds/docs/screenshots/field.png" width="380" alt="Koopa Village on the top screen, the Stats tab on the bottom screen">
  <img src="port/3ds/docs/screenshots/battle.png" width="380" alt="A battle, with Mario's commands as buttons on the bottom screen">
</p>

**Star Haven contains no game data.** You need your own Paper Mario ROM (EU or US).

## Install

<img src="port/3ds/docs/screenshots/install-qr.png" align="right" width="180" alt="QR code for the latest StarHaven.cia">

1. On a **New 3DS / New 2DS XL** with custom firmware, open **FBI → Remote Install →
   Scan QR Code** and scan the code on the right. It always installs the newest release.
   You can also download `StarHaven.cia` or `StarHaven.3dsx` from
   [Releases](../../releases/latest).
2. Copy your ROM to the SD card as `/3ds/StarHaven/baserom.z64` (`.z64`, `.n64` and `.v64`
   all work).
3. Start Star Haven from the HOME menu.

Full instructions: [the player readme](port/3ds/RELEASE_README.md).

## Features

- EU and US versions in one package
- 60 fps (in-between frames are added), or the original 30
- Fast-forward and faster text
- Original, Fill or Wide screen, the 3D slider, remappable buttons
- A touch bottom screen: stats, world map, party, items, and the battle menus as buttons
- A randomizer, chosen when you start a new file, with a seed you can share

| | | |
|:---:|:---:|:---:|
| <img src="port/3ds/docs/screenshots/map.png" width="260"> | <img src="port/3ds/docs/screenshots/party.png" width="260"> | <img src="port/3ds/docs/screenshots/options.png" width="260"> |
| World map | Party: tap a partner to switch | Options |
| <img src="port/3ds/docs/screenshots/new-game.png" width="260"> | <img src="port/3ds/docs/screenshots/randomizer.png" width="260"> | |
| New file: Classic or Randomizer | What the randomizer shuffles | |

## Building

See [port/3ds/README.md](port/3ds/README.md). Everything builds in a Docker image with
devkitARM, libctru and citro3d, and needs your own ROMs in `ver/pal/` and `ver/us/`.

## Layout

| | |
|---|---|
| `port/3ds/` | the 3DS port: platform layer, renderer, audio, tools, packaging |
| `src/`, `include/`, `ver/` | the decompilation, with port changes under `#ifdef _3DS` |
| `DECOMP_README.md` | the decompilation's own readme |

## Legal

Star Haven is a fan project. It is not affiliated with or endorsed by Nintendo. *Paper Mario*
is a trademark of Nintendo. Release packages contain no Nintendo game data: images, models,
sounds and text are read from the player's own ROM at startup. The screenshots show the game
running on the port.
