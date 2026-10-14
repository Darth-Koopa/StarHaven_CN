#ifndef _ROMFS_H_
#define _ROMFS_H_

#include "os_types.h"

// ============================================================================
// ROMFS Asset Loading for 3DS Paper Mario Port
// ============================================================================
//
// On N64, assets are loaded via DMA from cartridge ROM using segment-relative
// addresses. On 3DS, we extract assets at build time and embed them in the
// romfs filesystem (mounted at "romfs:/").
//
// This header provides helpers to load original game assets from romfs.
// ============================================================================

// Base path for game assets in romfs
#define PM3DS_ROMFS_BASE    "romfs:/assets"

// Asset type directories
#define PM3DS_ROMFS_SPRITES    PM3DS_ROMFS_BASE "/sprites"
#define PM3DS_ROMFS_BACKGROUNDS PM3DS_ROMFS_BASE "/backgrounds"
#define PM3DS_ROMFS_MAPS       PM3DS_ROMFS_BASE "/maps"
#define PM3DS_ROMFS_AUDIO      PM3DS_ROMFS_BASE "/audio"
#define PM3DS_ROMFS_FONTS      PM3DS_ROMFS_BASE "/fonts"
#define PM3DS_ROMFS_MSG        PM3DS_ROMFS_BASE "/msg"

// DMA segment names (used by original code's DMA_COPY_SEGMENT macro)
#define PM3DS_SEGMENT_ENGINE1   "engine1"
#define PM3DS_SEGMENT_ENGINE2   "engine2"
#define PM3DS_SEGMENT_ENGINE4   "engine4"
#define PM3DS_SEGMENT_EVT       "evt"
#define PM3DS_SEGMENT_ENTITY    "entity"
#define PM3DS_SEGMENT_FONT_WIDTH "font_width"

// Load a raw asset file from romfs or SD card
void* pm3ds_load_asset(const char* name);

// Load a specific segment into memory
void* pm3ds_load_segment(const char* segment_name);

// Load a sprite by name
void* pm3ds_load_sprite(const char* sprite_name);

// Load a background by name
void* pm3ds_load_background(const char* bg_name);

// Check if an asset exists in romfs
bool pm3ds_asset_exists(const char* path);

#endif