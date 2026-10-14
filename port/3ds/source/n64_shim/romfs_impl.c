#include "ultra64.h"
#include "n64/romfs.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pm3ds_log.h"

// ============================================================================
// ROMFS Asset Loading Implementation
// ============================================================================
//
// On N64, assets are DMA'd from cartridge ROM at runtime.
// On 3DS, we pre-extract assets and load from romfs:/ or SD card.
// ============================================================================

bool pm3ds_asset_exists(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp) {
        fclose(fp);
        return true;
    }
    return false;
}

void* pm3ds_load_asset(const char* name) {
    // Try romfs first
    char romfsPath[256];
    snprintf(romfsPath, sizeof(romfsPath), "romfs:/assets/%s", name);

    FILE* fp = fopen(romfsPath, "rb");
    if (!fp) {
        // Fallback to SD card
        char sdPath[256];
        snprintf(sdPath, sizeof(sdPath), "/3ds/PaperMario/assets/%s", name);
        fp = fopen(sdPath, "rb");
    }

    if (!fp) {
        pm3ds_log(PM3DS_LOG_WARN, "[ASSET] Failed to load: %s", name);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    void* buf = linearAlloc(size);
    if (buf) {
        fread(buf, 1, size, fp);
    } else {
        pm3ds_log(PM3DS_LOG_WARN, "[ASSET] Out of memory for: %s (%ld bytes)", name, size);
    }

    fclose(fp);
    return buf;
}

void* pm3ds_load_segment(const char* segment_name) {
    char path[256];
    snprintf(path, sizeof(path), "segments/%s.bin", segment_name);
    return pm3ds_load_asset(path);
}

void* pm3ds_load_sprite(const char* sprite_name) {
    char path[256];
    snprintf(path, sizeof(path), "sprites/%s.bin", sprite_name);
    return pm3ds_load_asset(path);
}

void* pm3ds_load_background(const char* bg_name) {
    char path[256];
    snprintf(path, sizeof(path), "backgrounds/%s.bin", bg_name);
    return pm3ds_load_asset(path);
}

// Decomp-facing helpers carried over from os_impl.c. The real DMA path
// is wired up by the romfs / baserom milestone; here we log the request
// so we can see what segments boot would have pulled in.
void pm3ds_dma_copy_segment(const char* segment_name) {
    (void)segment_name;
    pm3ds_log(PM3DS_LOG_INFO, "[DMA] Loading segment: %s", segment_name);
}

void* pm3ds_load_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    void* buf = malloc(size);
    if (buf) {
        fread(buf, 1, size, fp);
    }
    fclose(fp);
    return buf;
}
