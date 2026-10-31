#ifndef PM3DS_ROM_STREAM_H
#define PM3DS_ROM_STREAM_H
/* The cart ROM read from the player's file on demand (n64_shim/rom_stream.c). */
#include <stdbool.h>
#include <stdint.h>

enum { PM3DS_ORDER_Z64, PM3DS_ORDER_V64, PM3DS_ORDER_N64, PM3DS_ORDER_UNKNOWN };

bool pm3ds_rom_stream_open(const char *path, int order, uint32_t size);
bool pm3ds_rom_stream_active(void);
uint32_t pm3ds_rom_stream_size(void);
/* copy of [off, off+len) in .z64 order; zeros past the game's data */
void pm3ds_rom_read(uint32_t off, void *dst, uint32_t len);
/* keep [off, off+len) in RAM; the pointer stays valid and may be modified */
uint8_t *pm3ds_rom_pin_big(uint32_t off, uint32_t len);   /* frees cache memory for it if needed */
uint8_t *pm3ds_rom_pin(uint32_t off, uint32_t len);
/* pointer into a pinned region covering [off, off+len), or NULL */
const uint8_t *pm3ds_rom_ptr(uint32_t off, uint32_t len);
void pm3ds_rom_stream_stats(uint32_t *hits, uint32_t *misses, uint32_t *miss_ms);

#endif
