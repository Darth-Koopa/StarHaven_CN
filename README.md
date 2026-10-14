# Star Haven

A native Nintendo 3DS port of **Paper Mario** (Nintendo 64), built from the
[Paper Mario decompilation](https://github.com/pmret/papermario). The game's own C code runs
on the 3DS; nothing is emulated.

- EU and US versions in one package
- 60 fps (in-between frames), or the original 30
- Fast-forward and faster text
- Touch bottom screen: stats, map, party, items, battle menus
- Original, Fill or Wide screen, 3D slider, remappable buttons
- A randomizer, chosen per save file

**Star Haven contains no game data.** You need your own ROM of the game; see
[the player readme](port/3ds/RELEASE_README.md) for what you need and how to install.
Downloads are on the [Releases](../../releases) page.

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
sounds and text are read from the player's own ROM at startup.
