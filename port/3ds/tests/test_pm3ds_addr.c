#include <stdint.h>
#include <stdio.h>
#include "pm3ds_addr.h"

static int check(int condition, const char *message) {
    if (condition) return 1;
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
}

int main(void) {
    int ok = 1;
    const uintptr_t icon_base = (uintptr_t)PM3DS_ROM(0x215390u);

    ok &= check(pm3ds_addr_is_rom_tagged(icon_base),
                "iQue icon segment base must be recognized as tagged cart ROM");
    ok &= check(pm3ds_addr_is_texture_source(icon_base),
                "tagged ROM must be accepted as an RDP texture/TLUT source");
    ok &= check(pm3ds_addr_rom_offset(icon_base, 512) == 0x215390u,
                "tagged icon address must decode to the expected ROM offset");
    ok &= check(pm3ds_addr_is_texture_source(icon_base + 0x201u),
                "texture/TLUT sources must not require 4-byte alignment");
    ok &= check(pm3ds_addr_is_texture_source(0x08000002u),
                "2-byte-aligned app-heap palette pointer must be accepted");
    ok &= check(pm3ds_addr_is_texture_source(0x00100003u),
                "unaligned in-image texture data must be accepted");
    ok &= check(pm3ds_addr_rom_offset(0x00100003u, 16) == (uint32_t)-1,
                "in-image pointers must not be interpreted as raw ROM offsets");
    ok &= check(!pm3ds_addr_is_texture_source(0x50000000u),
                "unmapped address should not be accepted as a texture source");
    if (!ok) return 1;
    puts("pm3ds address/TLUT tests passed");
    return 0;
}
