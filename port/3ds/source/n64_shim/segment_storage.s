@ segment_storage.s — real BSS storage for N64 ROM segments the port
@ needs at runtime. Overrides the 1-byte weak stubs in m42_rom_stubs.c
@ for these specific segments. Other segments still default to the
@ weak stubs (no data, but link succeeds).
@
@ Layout: each segment is a contiguous block of <size> bytes with
@ _ROM_START and _ROM_END markers framing it. Code that does
@ `dma_copy(seg_ROM_START, seg_ROM_END, dest)` then sees a real
@ size and copies actual bytes (filled in from baserom by
@ pm3ds_pi_set_baserom).

    .section .bss.pm3ds_segments, "aw", %nobits
    .align 4

    @ Logos segment: 0x1B000 bytes (108 KiB) at PM3DS_ROM_LOGOS, copied in by
    @ os_pi.c
    .global logos_ROM_START
    .global logos_ROM_END
logos_ROM_START:
    .zero 0x1B000
logos_ROM_END:
    .zero 1

    @ M8.8: mapfs (the world/asset filesystem) is ~10 MB — far too big to
    @ mirror into BSS like the logos segment above. But load_asset_by_name
    @ only needs mapfs_ROM_START's VALUE as a ROM offset: it does
    @ dma_copy(mapfs_ROM_START + 0x20, ...) to read the asset-table header,
    @ and dma_copy's range-(d) translation maps ROM offsets [0x500000,
    @ 0x4000000) into the loaded 64 MB baserom buffer. So we define these
    @ as ABSOLUTE symbols equal to the real ROM offsets, overriding the
    @ weak `void mapfs_ROM_START(void){}` stub in m6_link_stubs.c.
    @ The decomp declares them `extern Addr` (u8[]), so C takes their
    @ ADDRESS — which is exactly the absolute value set. They're set in
    @ os_pi.c, from pm3ds_rom_layout.h (the offsets differ per version).
