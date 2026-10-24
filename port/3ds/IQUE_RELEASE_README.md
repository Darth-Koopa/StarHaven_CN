# Star Haven — iQue 3DS build

This ZIP contains two builds:

- `StarHaven_iQue.3dsx` — for the Homebrew Launcher.
- `StarHaven_iQue.cia` — for installation with a CIA installer such as FBI.

The game ROM is **not included**. Use your own original iQue China Paper Mario ROM at:

`/3ds/StarHaven/baserom.z64`

The program also embeds its small ROM-data mapping table in each executable's RomFS. That table is required at startup, but is not shipped as a separate loose `romdata_ique.bin` file in this ZIP. It does not contain the game ROM itself.

This iQue build is experimental. Please report startup, rendering, and save issues with the Actions build log and console log.
