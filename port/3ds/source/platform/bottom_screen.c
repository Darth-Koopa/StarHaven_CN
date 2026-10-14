/*
 * bottom_screen.c — the bottom-screen menu, drawn with citro3d.
 *
 * Styled after Paper Mario's pause menu and built from the game's own art
 * (pause tab labels, status icons, partner portraits, world map) and its
 * dialogue font, all decoded once from the linked N64 data. Three tabs:
 *   Stats  location, chapter, HP/FP, BP, level, star points, coins,
 *          star pieces, star power, play time
 *   Map    the pause-menu world map with visited places and Mario
 *   Party  partner portraits (joined / not yet), current partner
 * plus two buttons: Options (top-screen view, frame rate, text speed,
 * fast-forward, controls, debug page; saved by platform/pm3ds_settings.c) and
 * a fast-forward toggle.
 *
 * In battle the tabs give way to a battle page: HP/FP/star power, the menu
 * the game is waiting on (commands, moves, items, targets) as buttons to tap,
 * and an A/B pad for action commands when no menu is open. Item and partner
 * lists in the world show the same way (n64_shim/touch_menu.c).
 *
 * Game data comes from n64_shim/hud_info.c through pm3ds_hud_info.h, so this
 * file needs no decomp headers.
 */
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pm3ds_brand.h"

#include "pm3ds_hud_info.h"
#include "pm3ds_rando.h"
#include "pm3ds_settings.h"
#include "pm3ds_touch_menu.h"

extern void pm3ds_set_view_mode(int mode);   /* 0 Original, 1 Fill, 2 Wide */
extern int pm3ds_view_mode(void);
static const char *const s_view_names[3] = { "Original", "Fill", "Wide" };
extern shaderProgram_s *pm3ds_gfx_program(void);
extern int pm3ds_gfx_uloc_screen(void);
extern const char *pm3ds_log_recent(int i);   /* 0 = newest */
extern char pm3ds_perf_text[3][64];

int pm3ds_console_debug;   /* Debug page active (kept for the logger's API) */


/* ---- colours (r, g, b, a) ---------------------------------------------- */
#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define COL_BG_TOP     RGBA(250, 196, 110, 255)
#define COL_BG_BOTTOM  RGBA(222, 130, 60, 255)
#define COL_PANEL      RGBA(255, 247, 222, 255)
#define COL_PANEL_EDGE RGBA(116, 72, 40, 255)
#define COL_SHADOW     RGBA(80, 40, 10, 90)
#define COL_TAB        RGBA(205, 160, 105, 255)
#define COL_TAB_ON     RGBA(255, 247, 222, 255)
#define COL_HP         RGBA(232, 56, 48, 255)
#define COL_FP         RGBA(64, 176, 232, 255)
#define COL_BAR_BACK   RGBA(150, 120, 90, 255)
#define COL_WHITE      RGBA(255, 255, 255, 255)
#define COL_BTN        RGBA(116, 72, 40, 230)

/* ---- GPU state ----------------------------------------------------------- */
typedef struct { float x, y, z, w; u8 r, g, b, a; float u0, v0, u1, v1; } vtx_t;
#define UI_MAX_VERTS 12000

static C3D_RenderTarget *s_target;
static C3D_Mtx s_mtx, s_mtx_top;   /* bottom (320x240) and top (400x240) pixel space */
static C3D_AttrInfo s_attr;
static C3D_BufInfo s_buf;
static vtx_t *s_vbuf;
static int s_vcount, s_batch_start;
static C3D_Tex *s_cur_tex;

/* textures: UI atlas (art + white + circle), font (two palettes), world map */
static C3D_Tex s_atlas, s_font, s_map;
static bool s_atlas_ok, s_font_ok, s_map_ok;
#define ATLAS_W 256
#define ATLAS_H 256
typedef struct { short x, y, w, h; } rect_t;
static rect_t s_img[UI_IMG_COUNT];
static rect_t s_white, s_circle;
static u8 s_glyph_w[256];
static int s_glyphs;
static C3D_Tex s_small;      /* libctru's 8x8 console font: crisp small text */
static bool s_small_ok;

/* ---- state --------------------------------------------------------------- */
enum { TAB_STATS, TAB_MAP, TAB_PARTY, TAB_DEBUG, TAB_OPTIONS, TAB_CONTROLS };
static int s_tab = TAB_STATS;
static pm3ds_hud_info_t s_info;
static unsigned s_frame;
static pm3ds_touch_menu_t s_tm;   /* the game's open list menu (pm3ds_touch_menu.h) */
static int s_tm_page, s_tm_kind_seen, s_tm_count_seen;
static float s_map_cx = -1, s_map_cy = -1;   /* map view centre in map pixels */
static bool s_map_follow = true;

/* ---- texture building ---------------------------------------------------- */
static u8 s_morton[64];

static void init_morton(void) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int m = 0;
            for (int b = 0; b < 3; b++) {
                m |= ((x >> b) & 1) << (2 * b);
                m |= ((y >> b) & 1) << (2 * b + 1);
            }
            s_morton[y * 8 + x] = (u8)m;
        }
}

static size_t tiled_index(int x, int gy, int w) {
    return (size_t)((gy & ~7) * w + (x & ~7) * 8) + s_morton[(gy & 7) * 8 + (x & 7)];
}

/* linear RGBA8 (R,G,B,A bytes, row 0 = top) -> tiled GPU_RGBA8, same flip
 * convention as the game renderer: texel (x, y) samples at (x/w, y/h) */
static void upload_rgba8(C3D_Tex *tex, const u8 *rgba, int w, int h) {
    u8 *tiled = (u8 *)malloc((size_t)w * h * 4);
    if (!tiled) return;
    for (int gy = 0; gy < h; gy++) {
        int sy = h - 1 - gy;
        for (int x = 0; x < w; x++) {
            const u8 *p = rgba + ((size_t)sy * w + x) * 4;
            u8 *d = tiled + tiled_index(x, gy, w) * 4;
            d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0];
        }
    }
    C3D_TexUpload(tex, tiled);
    free(tiled);
}

/* linear RGBA8 -> tiled GPU_RGB565 (the opaque world map) */
static void upload_rgb565(C3D_Tex *tex, const u8 *rgba, int w, int h) {
    u16 *tiled = (u16 *)malloc((size_t)w * h * 2);
    if (!tiled) return;
    for (int gy = 0; gy < h; gy++) {
        int sy = h - 1 - gy;
        for (int x = 0; x < w; x++) {
            const u8 *p = rgba + ((size_t)sy * w + x) * 4;
            tiled[tiled_index(x, gy, w)] = (u16)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
        }
    }
    C3D_TexUpload(tex, tiled);
    free(tiled);
}

static void rgba16(u16 v, u8 *d) {
    u8 r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31;
    d[0] = (u8)((r << 3) | (r >> 2)); d[1] = (u8)((g << 3) | (g >> 2)); d[2] = (u8)((b << 3) | (b >> 2));
    d[3] = (v & 1) ? 255 : 0;
}

static u16 be16(const void *p) { const u8 *b = (const u8 *)p; return (u16)((b[0] << 8) | b[1]); }

/* decode an N64 image into a block of an RGBA8 canvas cw pixels wide */
static void decode_image(const pm3ds_ui_image_t *im, u8 *canvas, int cw, int ox, int oy) {
    for (int y = 0; y < im->h; y++) {
        for (int x = 0; x < im->w; x++) {
            u8 *d = canvas + ((size_t)(oy + y) * cw + ox + x) * 4;
            int i = y * im->w + x;
            switch (im->fmt) {
                case UI_FMT_RGBA16: rgba16(be16(im->img + i * 2), d); break;
                case UI_FMT_RGBA32: memcpy(d, im->img + i * 4, 4); break;
                case UI_FMT_CI4: {
                    u8 b = im->img[i >> 1];
                    rgba16(be16(&im->pal[(i & 1) ? (b & 15) : (b >> 4)]), d);
                    break;
                }
                case UI_FMT_CI8: rgba16(be16(&im->pal[im->img[i]]), d); break;
                case UI_FMT_IA8: {
                    u8 b = im->img[i];
                    d[0] = d[1] = d[2] = (u8)((b >> 4) * 17);
                    d[3] = (u8)((b & 15) * 17);
                    break;
                }
                case UI_FMT_I4: {
                    u8 b = im->img[i >> 1];
                    u8 v = (u8)(((i & 1) ? (b & 15) : (b >> 4)) * 17);
                    d[0] = d[1] = d[2] = d[3] = v;
                    break;
                }
            }
        }
    }
}

static void build_atlas(void) {
    u8 *canvas = (u8 *)calloc(ATLAS_W * ATLAS_H, 4);
    if (!canvas) return;
    /* shelf packing in id order */
    int x = 0, y = 0, shelf = 0;
    for (int id = 0; id < UI_IMG_COUNT; id++) {
        pm3ds_ui_image_t im;
        s_img[id].w = 0;
        if (id == UI_IMG_WORLD_MAP || !pm3ds_ui_get_image(id, &im)) continue;
        if (x + im.w > ATLAS_W) { x = 0; y += shelf + 1; shelf = 0; }
        if (y + im.h > ATLAS_H - 34) break;
        decode_image(&im, canvas, ATLAS_W, x, y);
        s_img[id] = (rect_t){ (short)x, (short)y, (short)im.w, (short)im.h };
        x += im.w + 1;
        if (im.h > shelf) shelf = im.h;
    }
    /* a white block and an antialiased 32x32 circle (rounded corners) */
    int by = ATLAS_H - 32;
    s_white = (rect_t){ 0, (short)by, 8, 8 };
    for (int j = 0; j < 8; j++) memset(canvas + ((size_t)(by + j) * ATLAS_W) * 4, 255, 8 * 4);
    s_circle = (rect_t){ 16, (short)by, 32, 32 };
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 32; i++) {
            float dx = i + 0.5f - 16.0f, dy = j + 0.5f - 16.0f;
            float d = 16.0f - __builtin_sqrtf(dx * dx + dy * dy);
            float a = d >= 1.0f ? 1.0f : d <= 0.0f ? 0.0f : d;
            u8 *p = canvas + ((size_t)(by + j) * ATLAS_W + 16 + i) * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = (u8)(a * 255.0f);
        }
    /* a rebuild uploads into the same texture: the GPU may still be drawing
     * the last frame from it */
    if (s_atlas_ok || C3D_TexInit(&s_atlas, ATLAS_W, ATLAS_H, GPU_RGBA8)) {
        upload_rgba8(&s_atlas, canvas, ATLAS_W, ATLAS_H);
        C3D_TexSetFilter(&s_atlas, GPU_LINEAR, GPU_NEAREST);
        C3D_TexSetWrap(&s_atlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_atlas_ok = true;
    }
    free(canvas);
}

static void build_map(void) {
    pm3ds_ui_image_t im;
    if (!pm3ds_ui_get_image(UI_IMG_WORLD_MAP, &im)) return;
    u8 *canvas = (u8 *)calloc(512 * 512, 4);
    if (!canvas) return;
    decode_image(&im, canvas, 512, 0, 0);
    if (s_map_ok || C3D_TexInit(&s_map, 512, 512, GPU_RGB565)) {
        upload_rgb565(&s_map, canvas, 512, 512);
        C3D_TexSetFilter(&s_map, GPU_LINEAR, GPU_NEAREST);
        C3D_TexSetWrap(&s_map, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_map_ok = true;
    }
    free(canvas);
}

/* Release builds start without the game's images (pm3ds_rom.c puts them
 * back once the ROM is loaded), so the menu art is built again then. */
void pm3ds_bottom_screen_reload_art(void) {
    build_atlas();
    build_map();
}

/* font texture 256x512: glyphs 16 per row, standard palette in the top half,
 * white palette in the bottom half */
static void build_font(void) {
    pm3ds_ui_font_t f;
    if (!pm3ds_ui_get_font(&f)) return;
    u8 *canvas = (u8 *)calloc(256 * 512, 4);
    if (!canvas) return;
    s_glyphs = f.glyph_count < 176 ? f.glyph_count : 176;
    for (int half = 0; half < 2; half++) {
        const u16 *pal = f.palettes + (half == 0 ? 0x0A : 0x00) * 8;   /* MSG_PAL_STANDARD / MSG_PAL_WHITE */
        for (int g = 0; g < s_glyphs; g++) {
            pm3ds_ui_image_t im = { f.rasters + g * 128, pal, 16, 16, UI_FMT_CI4 };
            decode_image(&im, canvas, 256, (g % 16) * 16, half * 256 + (g / 16) * 16);
        }
    }
    for (int g = 0; g < s_glyphs; g++) s_glyph_w[g] = f.widths[g];
    if (C3D_TexInit(&s_font, 256, 512, GPU_RGBA8)) {
        upload_rgba8(&s_font, canvas, 256, 512);
        C3D_TexSetFilter(&s_font, GPU_NEAREST, GPU_LINEAR);
        C3D_TexSetWrap(&s_font, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_font_ok = true;
    }
    free(canvas);
}

/* the 8x8 console font (1 bpp, MSB = left pixel), 16x8 glyphs in 128x64 */
static void build_small_font(void) {
    PrintConsole *pc = consoleGetDefault();
    if (!pc || !pc->font.gfx) return;
    u8 *canvas = (u8 *)calloc(128 * 64, 4);
    if (!canvas) return;
    int n = pc->font.numChars < 128 ? pc->font.numChars : 128;
    for (int c = 0; c < n; c++) {
        const u8 *g = pc->font.gfx + c * 8;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                u8 *p = canvas + ((size_t)((c / 16) * 8 + y) * 128 + (c % 16) * 8 + x) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = (g[y] & (0x80 >> x)) ? 255 : 0;
            }
    }
    if (C3D_TexInit(&s_small, 128, 64, GPU_RGBA8)) {
        upload_rgba8(&s_small, canvas, 128, 64);
        C3D_TexSetFilter(&s_small, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&s_small, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        s_small_ok = true;
    }
    free(canvas);
}

/* ---- primitives ---------------------------------------------------------- */
static void flush(void) {
    if (s_vcount > s_batch_start) {
        C3D_DrawArrays(GPU_TRIANGLES, s_batch_start, s_vcount - s_batch_start);
    }
    s_batch_start = s_vcount;
}

static void use_tex(C3D_Tex *t) {
    if (t != s_cur_tex) {
        flush();
        s_cur_tex = t;
        C3D_TexBind(0, t);
    }
}

static void vert(float x, float y, u32 c, float u, float v) {
    vtx_t *o = &s_vbuf[s_vcount++];
    o->x = x; o->y = y; o->z = 0.0f; o->w = 1.0f;
    o->r = c & 255; o->g = (c >> 8) & 255; o->b = (c >> 16) & 255; o->a = c >> 24;
    o->u0 = u; o->v0 = v; o->u1 = o->v1 = 0.0f;
}

/* textured quad with a vertical colour gradient (ct top, cb bottom) */
static void quad2(C3D_Tex *t, float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1, u32 ct, u32 cb) {
    if (s_vcount + 6 > UI_MAX_VERTS) return;
    use_tex(t);
    vert(x0, y0, ct, u0, v0); vert(x1, y0, ct, u1, v0); vert(x0, y1, cb, u0, v1);
    vert(x1, y0, ct, u1, v0); vert(x1, y1, cb, u1, v1); vert(x0, y1, cb, u0, v1);
}

static void atlas_quad(rect_t r, float x0, float y0, float x1, float y1, u32 c) {
    quad2(&s_atlas, x0, y0, x1, y1, (float)r.x / ATLAS_W, (float)r.y / ATLAS_H,
          (float)(r.x + r.w) / ATLAS_W, (float)(r.y + r.h) / ATLAS_H, c, c);
}

static void gradient(float x0, float y0, float x1, float y1, u32 ct, u32 cb) {
    float u = (s_white.x + 4.0f) / ATLAS_W, v = (s_white.y + 4.0f) / ATLAS_H;
    quad2(&s_atlas, x0, y0, x1, y1, u, v, u, v, ct, cb);
}

static void fill(float x0, float y0, float x1, float y1, u32 c) { gradient(x0, y0, x1, y1, c, c); }

/* rounded rectangle from the circle's quarters */
static void round_rect(float x0, float y0, float x1, float y1, float r, u32 c) {
    float cu0 = (float)s_circle.x / ATLAS_W, cv0 = (float)s_circle.y / ATLAS_H;
    float cu1 = (float)(s_circle.x + s_circle.w) / ATLAS_W, cv1 = (float)(s_circle.y + s_circle.h) / ATLAS_H;
    float cum = (cu0 + cu1) / 2, cvm = (cv0 + cv1) / 2;
    if (r > (x1 - x0) / 2) r = (x1 - x0) / 2;
    if (r > (y1 - y0) / 2) r = (y1 - y0) / 2;
    quad2(&s_atlas, x0, y0, x0 + r, y0 + r, cu0, cv0, cum, cvm, c, c);
    quad2(&s_atlas, x1 - r, y0, x1, y0 + r, cum, cv0, cu1, cvm, c, c);
    quad2(&s_atlas, x0, y1 - r, x0 + r, y1, cu0, cvm, cum, cv1, c, c);
    quad2(&s_atlas, x1 - r, y1 - r, x1, y1, cum, cvm, cu1, cv1, c, c);
    fill(x0 + r, y0, x1 - r, y1, c);
    fill(x0, y0 + r, x0 + r, y1 - r, c);
    fill(x1 - r, y0 + r, x1, y1 - r, c);
}

/* a pause-menu style panel: shadow, brown rim, cream face */
static void panel(float x0, float y0, float x1, float y1, u32 face) {
    round_rect(x0 + 3, y0 + 3, x1 + 3, y1 + 3, 8, COL_SHADOW);
    round_rect(x0, y0, x1, y1, 8, COL_PANEL_EDGE);
    round_rect(x0 + 2, y0 + 2, x1 - 2, y1 - 2, 6, face);
}

static void image(int id, float x, float y, float scale, u32 c) {
    rect_t r = s_img[id];
    if (r.w == 0) return;
    atlas_quad(r, x, y, x + r.w * scale, y + r.h * scale, c);
}

/* ---- text (the game's font) ---------------------------------------------- */
static int glyph_of(char ch) {
    unsigned char c = (unsigned char)ch;
    if (c > 0x20 && c - 0x20 < s_glyphs) return c - 0x20;   /* past 0x7F: the game's accented letters */
    return -1;   /* space */
}

static float text_width(const char *s, float scale) {
    float w = 0;
    for (; *s; s++) {
        int g = glyph_of(*s);
        w += (g >= 0 && g < s_glyphs ? s_glyph_w[g] : 5) * scale;
    }
    return w;
}

/* white = 1 picks the white palette; c tints the result */
static float text(float x, float y, const char *s, float scale, int white, u32 c) {
    if (!s_font_ok) return x;
    for (; *s; s++) {
        int g = glyph_of(*s);
        if (g >= 0 && g < s_glyphs) {
            float gx = (g % 16) * 16.0f, gy = (white ? 256.0f : 0.0f) + (g / 16) * 16.0f;
            quad2(&s_font, x, y, x + 16 * scale, y + 16 * scale,
                  gx / 256.0f, gy / 512.0f, (gx + 16) / 256.0f, (gy + 16) / 512.0f, c, c);
            x += s_glyph_w[g] * scale;
        } else {
            x += 5 * scale;
        }
    }
    return x;
}

static void text_center(float cx, float y, const char *s, float scale, int white, u32 c) {
    text(cx - text_width(s, scale) / 2, y, s, scale, white, c);
}

/* 8x8 console-font text, 1:1; wraps at max_cols, returns the next line's y */
static float small_text(float x, float y, const char *str, int max_cols, u32 c) {
    if (!s_small_ok) return y + 9;
    int col = 0;
    float x0 = x;
    for (; *str; str++) {
        unsigned char ch = (unsigned char)*str;
        if (col >= max_cols) { col = 0; x = x0 + 16; y += 9; }   /* continuation lines indented */
        if (ch > 32 && ch < 128) {
            float gx = (ch % 16) * 8.0f, gy = (ch / 16) * 8.0f;
            quad2(&s_small, x, y, x + 8, y + 8, gx / 128.0f, gy / 64.0f, (gx + 8) / 128.0f, (gy + 8) / 64.0f, c, c);
        }
        x += 8; col++;
    }
    return y + 9;
}

static void bar(float x, float y, float w, float h, int v, int max, u32 col) {
    round_rect(x - 1, y - 1, x + w + 1, y + h + 1, h / 2 + 1, COL_PANEL_EDGE);
    round_rect(x, y, x + w, y + h, h / 2, COL_BAR_BACK);
    if (max > 0 && v > 0) {
        float fw = w * (v > max ? max : v) / (float)max;
        if (fw < h) fw = h;
        round_rect(x, y, x + fw, y + h, h / 2, col);
    }
}

/* ---- layout -------------------------------------------------------------- */
#define TAB_Y0 4
#define TAB_Y1 30
#define PANEL_Y0 28
#define PANEL_Y1 204
#define BTN_Y0 212
#define BTN_Y1 234
static const float s_tab_x[3][2] = { { 10, 100 }, { 115, 205 }, { 220, 310 } };
static const float s_btn_opt[2] = { 8, 104 };
static const float s_btn_items[2] = { 112, 208 };
static const float s_btn_ff[2] = { 216, 312 };

/* shortcut for the game (src/world/menus.c): 1 = item menu, 0x100 + id = switch partner */
volatile int pm3ds_touch_req;

static const char *chapter_label(int ch) {
    static const char *names[] = {
        "Prologue", "Chapter 1", "Chapter 2", "Chapter 3", "Chapter 4",
        "Chapter 5", "Chapter 6", "Chapter 7", "Chapter 8", "Epilogue",
    };
    return ch >= 0 && ch <= 9 ? names[ch] : "";
}

static void draw_tabs(void) {
    static const int labels[3] = { UI_IMG_LABEL_STATS, UI_IMG_LABEL_MAP, UI_IMG_LABEL_PARTY };
    for (int i = 0; i < 3; i++) {
        bool on = s_tab == i;
        float x0 = s_tab_x[i][0], x1 = s_tab_x[i][1];
        float y0 = on ? TAB_Y0 : TAB_Y0 + 3;
        round_rect(x0 + 2, y0 + 2, x1 + 2, TAB_Y1 + 6, 7, COL_SHADOW);
        round_rect(x0, y0, x1, TAB_Y1 + 6, 7, COL_PANEL_EDGE);
        round_rect(x0 + 2, y0 + 2, x1 - 2, TAB_Y1 + 6, 5, on ? COL_TAB_ON : COL_TAB);
        rect_t r = s_img[labels[i]];
        image(labels[i], (x0 + x1) / 2 - r.w / 2, y0 + 5, 1.0f, COL_WHITE);
    }
}

static void stat_line(float x, float y, int icon, const char *label, const char *value) {
    image(icon, x, y, 1.0f, COL_WHITE);
    text(x + 20, y, label, 1.0f, 0, COL_WHITE);
    text(306 - text_width(value, 1.0f), y, value, 1.0f, 0, COL_WHITE);
}

static void draw_stats(void) {
    const pm3ds_hud_info_t *h = &s_info;
    char buf[48];
    text_center(160, PANEL_Y0 + 7, h->location, 1.0f, 0, COL_WHITE);
    {
        extern unsigned int pm3ds_rando_seed(void);   /* n64_shim/rando.c; matches spoiler_<seed>.txt */
        char line[48];
        if (pm3ds_rando_seed())
            snprintf(line, sizeof line, "%s  -  seed %08X", chapter_label(h->chapter), pm3ds_rando_seed());
        else
            snprintf(line, sizeof line, "%s", chapter_label(h->chapter));
        text_center(160, PANEL_Y0 + 24, line, 1.0f, 0, RGBA(170, 120, 80, 255));
    }
    fill(20, PANEL_Y0 + 43, 300, PANEL_Y0 + 44, RGBA(200, 170, 130, 255));

    float y = PANEL_Y0 + 50;
    image(UI_IMG_HEART, 16, y, 1.0f, COL_WHITE);
    text(36, y, "HP", 1.0f, 0, COL_WHITE);
    snprintf(buf, sizeof buf, "%d/%d", h->hp, h->max_hp);
    text(146 - text_width(buf, 1.0f), y, buf, 1.0f, 0, COL_WHITE);
    bar(16, y + 18, 130, 7, h->hp, h->max_hp, COL_HP);

    image(UI_IMG_FLOWER, 16, y + 32, 1.0f, COL_WHITE);
    text(36, y + 32, "FP", 1.0f, 0, COL_WHITE);
    snprintf(buf, sizeof buf, "%d/%d", h->fp, h->max_fp);
    text(146 - text_width(buf, 1.0f), y + 32, buf, 1.0f, 0, COL_WHITE);
    bar(16, y + 50, 130, 7, h->fp, h->max_fp, COL_FP);

    image(UI_IMG_BP, 16, y + 66, 1.0f, COL_WHITE);
    text(36, y + 66, "BP", 1.0f, 0, COL_WHITE);
    snprintf(buf, sizeof buf, "%d", h->bp);
    text(146 - text_width(buf, 1.0f), y + 66, buf, 1.0f, 0, COL_WHITE);

    snprintf(buf, sizeof buf, "%d", h->level);
    stat_line(160, y, UI_IMG_MARIO, "Level", buf);
    snprintf(buf, sizeof buf, "%d", h->star_points);
    stat_line(160, y + 22, UI_IMG_STAR_POINT, "Star Points", buf);
    snprintf(buf, sizeof buf, "%d", h->coins);
    stat_line(160, y + 44, UI_IMG_COIN, "Coins", buf);
    snprintf(buf, sizeof buf, "%d", h->star_pieces);
    stat_line(160, y + 66, UI_IMG_STAR_PIECE, "Star Pieces", buf);

    /* star power: one star per bar */
    float sy = PANEL_Y1 - 24;
    if (h->max_star_power > 0) {
        float x = text(16, sy, "Star Power", 1.0f, 0, COL_WHITE) + 6;
        int full = h->star_power / 256;
        for (int i = 0; i < h->max_star_power && i < 7; i++)
            image(i < full ? UI_IMG_POW_STAR : UI_IMG_POW_EMPTY, x + i * 10, sy + 4, 1.0f, COL_WHITE);
    }
    unsigned sec = h->play_seconds;
    snprintf(buf, sizeof buf, "%u:%02u:%02u", sec / 3600, (sec / 60) % 60, sec % 60);
    float tw = text_width(buf, 1.0f);
    image(UI_IMG_TIME, 306 - tw - 20, sy, 1.0f, COL_WHITE);
    text(306 - tw, sy, buf, 1.0f, 0, COL_WHITE);
}

static void draw_map(void) {
    const pm3ds_hud_info_t *h = &s_info;
    float vx0 = 10, vy0 = PANEL_Y0 + 8, vx1 = 310, vy1 = PANEL_Y1 - 8;
    float vw = vx1 - vx0, vh = vy1 - vy0;
    if (s_map_follow && h->map_current >= 0) {
        s_map_cx = h->map_x[h->map_current];
        s_map_cy = h->map_y[h->map_current];
    }
    if (s_map_cx < 0) { s_map_cx = 160; s_map_cy = 160; }
    /* keep the view inside the 320x320 map */
    if (s_map_cx < vw / 2) s_map_cx = vw / 2;
    if (s_map_cx > 320 - vw / 2) s_map_cx = 320 - vw / 2;
    if (s_map_cy < vh / 2) s_map_cy = vh / 2;
    if (s_map_cy > 320 - vh / 2) s_map_cy = 320 - vh / 2;
    float mx0 = s_map_cx - vw / 2, my0 = s_map_cy - vh / 2;
    if (s_map_ok)
        quad2(&s_map, vx0, vy0, vx1, vy1, mx0 / 512.0f, my0 / 512.0f,
              (mx0 + vw) / 512.0f, (my0 + vh) / 512.0f, COL_WHITE, COL_WHITE);
    for (int i = 0; i < h->map_count; i++) {
        if (!h->map_visited[i]) continue;
        float x = vx0 + h->map_x[i] - mx0, y = vy0 + h->map_y[i] - my0;
        if (x < vx0 || x > vx1 || y < vy0 || y > vy1) continue;
        image(UI_IMG_MAP_LOCATION, x - 8, y - 8, 1.0f, RGBA(220, 80, 30, 255));
    }
    if (h->map_current >= 0) {
        float x = vx0 + h->map_x[h->map_current] - mx0, y = vy0 + h->map_y[h->map_current] - my0;
        int walk = (s_frame / 8) & 1 ? UI_IMG_MAP_WALK1 : UI_IMG_MAP_WALK0;
        if (x >= vx0 && x <= vx1 && y >= vy0 && y <= vy1) image(walk, x - 12, y - 20, 1.0f, COL_WHITE);
    }
    /* location banner */
    float tw = text_width(h->location, 1.0f);
    round_rect(160 - tw / 2 - 8, vy1 - 22, 160 + tw / 2 + 8, vy1 - 2, 8, RGBA(60, 36, 20, 210));
    text_center(160, vy1 - 21, h->location, 1.0f, 1, COL_WHITE);
    if (!s_map_follow) {
        round_rect(vx0 + 2, vy0 + 2, vx0 + 178, vy0 + 14, 5, RGBA(60, 36, 20, 200));
        small_text(vx0 + 6, vy0 + 4, "tap here to re-centre", 40, COL_WHITE);
    }
}

static bool in(int px, int py, float x0, float y0, float x1, float y1);
static void small_text_center(float cx, float y, const char *str, float scale, u32 c);

static void draw_party(void) {
    const pm3ds_hud_info_t *h = &s_info;
    static const int ids[PM3DS_HUD_PARTNERS] = { 1, 2, 3, 4, 9, 6, 7, 8 };
    static const char *names[PM3DS_HUD_PARTNERS] = {
        "Goombario", "Kooper", "Bombette", "Parakarry", "Bow", "Watt", "Sushie", "Lakilester",
    };
    for (int i = 0; i < PM3DS_HUD_PARTNERS; i++) {
        float cx = 46 + (i % 4) * 76, y = PANEL_Y0 + 12 + (i / 4) * 84;
        bool owned = (h->partners_owned >> ids[i]) & 1;
        bool active = h->partner == ids[i];
        if (active) round_rect(cx - 34, y - 4, cx + 34, y + 76, 8, RGBA(255, 214, 120, 255));
        image(owned ? UI_IMG_PARTNER_FIRST + i : UI_IMG_PARTNER_DISABLED_FIRST + i, cx - 24, y, 1.5f, COL_WHITE);
        text_center(cx, y + 52, owned ? names[i] : "???", 1.0f, 0,
                    owned ? COL_WHITE : RGBA(255, 255, 255, 140));
    }
    if (h->partners_owned) small_text_center(160, PANEL_Y1 - 12, "Tap a partner to switch", 1.0f, RGBA(150, 100, 60, 255));
}

/* Party tab: the partner whose portrait is at (px, py), or 0 */
static int partner_at(int px, int py) {
    static const int ids[PM3DS_HUD_PARTNERS] = { 1, 2, 3, 4, 9, 6, 7, 8 };
    for (int i = 0; i < PM3DS_HUD_PARTNERS; i++) {
        float cx = 46 + (i % 4) * 76, y = PANEL_Y0 + 12 + (i / 4) * 84;
        if (in(px, py, cx - 34, y - 4, cx + 34, y + 76)) return ids[i];
    }
    return 0;
}

static void draw_debug(void) {
    fill(0, 0, 320, 240, RGBA(16, 16, 24, 255));
    float y = 3;
    for (int i = 0; i < 3; i++) y = small_text(4, y, pm3ds_perf_text[i], 39, RGBA(255, 230, 120, 255));
    fill(4, y + 1, 316, y + 2, RGBA(90, 90, 110, 255));
    y += 5;
    /* newest lines last; show as many as fit above the Close button */
    int n = 0;
    while (n < 16 && pm3ds_log_recent(n)) n++;
    for (int i = n - 1; i >= 0 && y < 200; i--) {
        const char *l = pm3ds_log_recent(i);
        if (!strncmp(l, "[INFO ] ", 8)) l += 8;   /* drop the level tag for INFO */
        y = small_text(4, y, l, 39, RGBA(220, 220, 220, 255));
    }
    round_rect(250, 214, 316, 236, 8, COL_BTN);
    text_center(283, 217, "Back", 1.0f, 1, COL_WHITE);
}

/* ---- battle page and touch menus ------------------------------------------ */
/* the game's list menu as buttons: two columns, TM_ROWS rows per page */
#define TM_Y0 58
#define TM_ROW_H 28
#define TM_ROWS 5
#define TM_PER_PAGE (TM_ROWS * 2)
/* the pad shown in battle when no menu is open */
#define PAD_A_X 214
#define PAD_A_Y 118
#define PAD_A_R 58
#define PAD_B_X 104
#define PAD_B_Y 150
#define PAD_B_R 34
static const float s_pad_stick[4] = { 18, 62, 120, 104 };   /* x0 y0 x1 y1: hold the stick left */

static bool tm_open(void) { return s_tm.kind != PM3DS_TM_NONE && s_info.in_game; }
static bool battle_page(void) { return s_info.in_battle && s_tab <= TAB_PARTY; }

static int tm_pages(void) { return (s_tm.count + TM_PER_PAGE - 1) / TM_PER_PAGE; }

/* short lists get one column of wide buttons */
static void tm_entry_rect(int slot, float *x0, float *y0, float *x1, float *y1) {
    int col = slot / TM_ROWS, row = slot % TM_ROWS;
    *x0 = 12 + col * 152;
    *x1 = s_tm.count <= TM_ROWS ? 308 : *x0 + 144;
    *y0 = TM_Y0 + row * TM_ROW_H;
    *y1 = *y0 + TM_ROW_H - 4;
}

static void disc(float cx, float cy, float r, u32 c) { round_rect(cx - r, cy - r, cx + r, cy + r, r, c); }

static void draw_battle_header(void) {
    const pm3ds_hud_info_t *h = &s_info;
    char buf[24];
    panel(4, 2, 316, 30, COL_PANEL);
    image(UI_IMG_HEART, 12, 8, 1.0f, COL_WHITE);
    snprintf(buf, sizeof buf, "%d/%d", h->hp, h->max_hp);
    text(32, 7, buf, 1.0f, 0, COL_WHITE);
    image(UI_IMG_FLOWER, 92, 8, 1.0f, COL_WHITE);
    snprintf(buf, sizeof buf, "%d/%d", h->fp, h->max_fp);
    text(112, 7, buf, 1.0f, 0, COL_WHITE);
    if (h->max_star_power > 0) {
        int full = h->star_power / 256;
        for (int i = 0; i < h->max_star_power && i < 7; i++)
            image(i < full ? UI_IMG_POW_STAR : UI_IMG_POW_EMPTY, 176 + i * 10, 12, 1.0f, COL_WHITE);
    }
    image(UI_IMG_COIN, 252, 8, 1.0f, COL_WHITE);
    snprintf(buf, sizeof buf, "%d", h->coins);
    text(306 - text_width(buf, 1.0f), 7, buf, 1.0f, 0, COL_WHITE);
}

static void draw_tmenu(float y_top) {
    char buf[24];
    panel(4, y_top, 316, PANEL_Y1, COL_PANEL);
    /* keep the page on the game's cursor when a menu opens */
    if (s_tm.kind != s_tm_kind_seen || s_tm.count != s_tm_count_seen) {
        s_tm_kind_seen = s_tm.kind;
        s_tm_count_seen = s_tm.count;
        s_tm_page = s_tm.cursor >= 0 ? s_tm.cursor / TM_PER_PAGE : 0;
    }
    if (s_tm_page >= tm_pages()) s_tm_page = 0;
    const char *title = s_tm.kind == PM3DS_TM_TARGET && s_tm.all_targets ? "Targets: all" : s_tm.title;
    text_center(160, y_top + 6, title, 1.0f, 0, RGBA(170, 120, 80, 255));
    if (tm_pages() > 1) {
        round_rect(12, y_top + 5, 48, y_top + 23, 7, COL_BTN);
        text_center(30, y_top + 6, "<", 1.0f, 1, COL_WHITE);
        round_rect(272, y_top + 5, 308, y_top + 23, 7, COL_BTN);
        text_center(290, y_top + 6, ">", 1.0f, 1, COL_WHITE);
        snprintf(buf, sizeof buf, "%d/%d", s_tm_page + 1, tm_pages());
        small_text_center(260 - 16, y_top + 10, buf, 1.0f, RGBA(150, 100, 60, 255));
    }
    for (int slot = 0; slot < TM_PER_PAGE; slot++) {
        int i = s_tm_page * TM_PER_PAGE + slot;
        if (i >= s_tm.count) break;
        const pm3ds_tm_entry_t *e = &s_tm.e[i];
        float x0, y0, x1, y1;
        tm_entry_rect(slot, &x0, &y0, &x1, &y1);
        bool sel = i == s_tm.cursor;
        round_rect(x0 + 2, y0 + 2, x1 + 2, y1 + 2, 8, COL_SHADOW);
        round_rect(x0, y0, x1, y1, 8, COL_PANEL_EDGE);
        round_rect(x0 + 2, y0 + 2, x1 - 2, y1 - 2, 6,
                   !e->enabled ? RGBA(200, 185, 165, 255) : sel ? RGBA(255, 214, 120, 255) : COL_TAB_ON);
        /* cost on the right, then the name in what is left */
        float right = x1 - 6;
        buf[0] = 0;
        switch (e->cost_kind) {
            case PM3DS_TM_COST_FP: snprintf(buf, sizeof buf, "%d FP", e->cost); break;
            case PM3DS_TM_COST_SP: snprintf(buf, sizeof buf, "%d SP", e->cost); break;
            case PM3DS_TM_COST_COINS: snprintf(buf, sizeof buf, "%d c", e->cost); break;
            case PM3DS_TM_COST_STAR_PIECES: snprintf(buf, sizeof buf, "%d sp", e->cost); break;
            case PM3DS_TM_COST_HP: snprintf(buf, sizeof buf, "%d/%d", e->cost, e->max_hp); break;
        }
        u32 tc = e->enabled ? COL_WHITE : RGBA(255, 255, 255, 120);
        if (buf[0]) {
            float cw = strlen(buf) * 8.0f;
            small_text(right - cw, y0 + 8, buf, 12, e->cost_kind == PM3DS_TM_COST_HP ? RGBA(200, 40, 40, 255)
                                                                                       : RGBA(90, 60, 30, 255));
            right -= cw + 4;
        }
        float avail = right - (x0 + 6), w = text_width(e->name, 1.0f), sc = 1.0f;
        if (w > avail) sc = avail / w;
        if (sc < 0.6f) sc = 0.6f;
        text(x0 + 6, y0 + 3 + (1.0f - sc) * 8, e->name, sc, 0, tc);
    }
}

static void draw_pad(void) {
    panel(4, 34, 316, PANEL_Y1, COL_PANEL);
    bool a = pm3ds_touch_pad & 0x8000, b = pm3ds_touch_pad & 0x4000, st = pm3ds_touch_stick_x != 0;
    disc(PAD_A_X + 3, PAD_A_Y + 3, PAD_A_R, COL_SHADOW);
    disc(PAD_A_X, PAD_A_Y, PAD_A_R, RGBA(40, 70, 170, 255));
    disc(PAD_A_X, PAD_A_Y, PAD_A_R - 4, a ? RGBA(120, 170, 255, 255) : RGBA(70, 120, 230, 255));
    text_center(PAD_A_X, PAD_A_Y - 24, "A", 3.0f, 1, COL_WHITE);
    disc(PAD_B_X + 3, PAD_B_Y + 3, PAD_B_R, COL_SHADOW);
    disc(PAD_B_X, PAD_B_Y, PAD_B_R, RGBA(30, 120, 50, 255));
    disc(PAD_B_X, PAD_B_Y, PAD_B_R - 4, b ? RGBA(130, 220, 140, 255) : RGBA(60, 170, 80, 255));
    text_center(PAD_B_X, PAD_B_Y - 16, "B", 2.0f, 1, COL_WHITE);
    round_rect(s_pad_stick[0] + 2, s_pad_stick[1] + 2, s_pad_stick[2] + 2, s_pad_stick[3] + 2, 10, COL_SHADOW);
    round_rect(s_pad_stick[0], s_pad_stick[1], s_pad_stick[2], s_pad_stick[3], 10, COL_PANEL_EDGE);
    round_rect(s_pad_stick[0] + 2, s_pad_stick[1] + 2, s_pad_stick[2] - 2, s_pad_stick[3] - 2, 8,
               st ? RGBA(255, 196, 64, 255) : COL_TAB_ON);
    text_center((s_pad_stick[0] + s_pad_stick[2]) / 2, s_pad_stick[1] + 4, "< Stick", 1.0f, 0, COL_WHITE);
    small_text_center((s_pad_stick[0] + s_pad_stick[2]) / 2, s_pad_stick[1] + 26, "hold", 1.0f,
                      RGBA(150, 100, 60, 255));
    small_text_center(160, PANEL_Y1 - 14, "Action commands: tap or hold", 1.0f, RGBA(150, 100, 60, 255));
}

static void draw_battle(void) {
    draw_battle_header();
    if (tm_open()) draw_tmenu(34);
    else draw_pad();
}

/* the touch pad's buttons under the finger (held state, every frame) */
static void pad_held(int px, int py) {
    float dx, dy;
    if (tm_open() || !battle_page()) return;
    dx = px - PAD_A_X; dy = py - PAD_A_Y;
    if (dx * dx + dy * dy <= PAD_A_R * PAD_A_R) pm3ds_touch_pad |= 0x8000;
    dx = px - PAD_B_X; dy = py - PAD_B_Y;
    if (dx * dx + dy * dy <= PAD_B_R * PAD_B_R) pm3ds_touch_pad |= 0x4000;
    if (in(px, py, s_pad_stick[0], s_pad_stick[1], s_pad_stick[2], s_pad_stick[3])) pm3ds_touch_stick_x = -80;
}

/* a tap on the menu: true if it was used */
static bool tmenu_tap(int px, int py, float y_top) {
    if (!tm_open()) return false;
    if (tm_pages() > 1 && py >= y_top + 2 && py < y_top + 26) {
        if (px < 56) { s_tm_page = (s_tm_page + tm_pages() - 1) % tm_pages(); return true; }
        if (px >= 264) { s_tm_page = (s_tm_page + 1) % tm_pages(); return true; }
    }
    for (int slot = 0; slot < TM_PER_PAGE; slot++) {
        int i = s_tm_page * TM_PER_PAGE + slot;
        if (i >= s_tm.count) break;
        float x0, y0, x1, y1;
        tm_entry_rect(slot, &x0, &y0, &x1, &y1);
        if (in(px, py, x0, y0, x1, y1)) {
            pm3ds_tm_pick(s_tm.kind, i);
            return true;
        }
    }
    return false;
}

/* ---- Options and Controls pages ------------------------------------------ */
#define OPT_ROWS 7
#define OPT_Y0 8
#define OPT_ROW_H 24
static const char *const s_fps_names[2] = { "30 fps", "60 fps" };
static const char *const s_text_names[TEXT_SPEEDS] = { "Normal", "Fast", "Instant" };

static void opt_row(int i, const char *label, const char *value) {
    float y = OPT_Y0 + i * OPT_ROW_H;
    text(18, y + 4, label, 1.0f, 0, COL_WHITE);
    if (value) {
        float w = text_width(value, 1.0f) + 24;
        if (w < 96) w = 96;
        round_rect(302 - w, y + 1, 302, y + 23, 9, COL_PANEL_EDGE);
        round_rect(304 - w, y + 3, 300, y + 21, 7, RGBA(255, 214, 120, 255));
        text_center(302 - w / 2, y + 4, value, 1.0f, 0, COL_WHITE);
    } else {
        text(282, y + 4, ">", 1.0f, 0, COL_WHITE);
    }
}

static void draw_options(void) {
    char buf[16];
    panel(4, 4, 316, 204, COL_PANEL);
    opt_row(0, "Screen", s_view_names[pm3ds_cfg.view_mode % 3]);
    opt_row(1, "Frame rate", s_fps_names[pm3ds_cfg.fps60]);
    opt_row(2, "Text speed", s_text_names[pm3ds_cfg.text_speed % TEXT_SPEEDS]);
    snprintf(buf, sizeof buf, "%dx", pm3ds_cfg.ff_speed);
    opt_row(3, "Fast-forward speed", buf);
    opt_row(4, "Fast-forward button", pm3ds_cfg.ff_toggle ? "Toggle" : "Hold");
    opt_row(5, "Controls", NULL);
    opt_row(6, "Debug info", NULL);
    small_text(12, 221, PM3DS_APP_NAME " v" PM3DS_VERSION_STR, 40, COL_PANEL_EDGE);
    round_rect(250, 214, 316, 236, 8, COL_BTN);
    text_center(283, 217, "Back", 1.0f, 1, COL_WHITE);
}

/* every 3DS button and what it does; tap to change */
#define CTL_ROWS 9
#define CTL_Y0 10
#define CTL_ROW_H 21
static void draw_controls(void) {
    panel(4, 4, 316, 204, COL_PANEL);
    for (int k = 0; k < KEY_IDX_COUNT; k++) {
        float x = k < CTL_ROWS ? 12 : 164, y = CTL_Y0 + (k % CTL_ROWS) * CTL_ROW_H;
        text(x, y + 2, pm3ds_settings_key_name(k), 0.9f, 0, COL_WHITE);
        round_rect(x + 62, y, x + 144, y + 19, 7, COL_PANEL_EDGE);
        round_rect(x + 64, y + 2, x + 142, y + 17, 5, RGBA(255, 214, 120, 255));
        text_center(x + 103, y + 2, pm3ds_settings_action_name(pm3ds_cfg.keymap[k]), 0.9f, 0, COL_WHITE);
    }
    round_rect(8, 214, 108, 236, 8, COL_BTN);
    text_center(58, 217, "Defaults", 1.0f, 1, COL_WHITE);
    round_rect(250, 214, 316, 236, 8, COL_BTN);
    text_center(283, 217, "Back", 1.0f, 1, COL_WHITE);
}

static void button(const float x[2], const char *label) {
    round_rect(x[0] + 2, BTN_Y0 + 2, x[1] + 2, BTN_Y1 + 2, 10, COL_SHADOW);
    round_rect(x[0], BTN_Y0, x[1], BTN_Y1, 10, COL_PANEL_EDGE);
    round_rect(x[0] + 2, BTN_Y0 + 2, x[1] - 2, BTN_Y1 - 2, 8, COL_TAB_ON);
    text_center((x[0] + x[1]) / 2, BTN_Y0 + 3, label, 1.0f, 0, COL_WHITE);
}

/* the middle button: Items in the world; with a menu open it holds B (Back),
 * or Z (Swap Mario and partner) on the battle command reel */
static const char *mid_button(uint16_t *pad) {
    *pad = 0;
    if (tm_open()) {
        if (s_tm.kind == PM3DS_TM_WHEEL) { *pad = 0x2000; return "Swap"; }
        *pad = 0x4000;
        return "Back";
    }
    if (s_info.in_battle) return NULL;
    return s_info.in_game ? "Items" : NULL;
}

static void draw_buttons(void) {
    uint16_t mid_pad;
    const char *mid = mid_button(&mid_pad);
    button(s_btn_opt, "Options");
    if (mid) button(s_btn_items, mid);
    if (pm3ds_ff_button_on) {   /* lit while on */
        round_rect(s_btn_ff[0], BTN_Y0, s_btn_ff[1], BTN_Y1, 10, COL_PANEL_EDGE);
        round_rect(s_btn_ff[0] + 2, BTN_Y0 + 2, s_btn_ff[1] - 2, BTN_Y1 - 2, 8, RGBA(255, 196, 64, 255));
        text_center((s_btn_ff[0] + s_btn_ff[1]) / 2, BTN_Y0 + 3, "Fast >> on", 1.0f, 0, COL_WHITE);
    } else {
        button(s_btn_ff, "Fast >>");
    }
}

/* ---- new-game picker ------------------------------------------------------ */
/* The file menu (src/filemenu/filemenu_main.c) opens it when an empty file is
 * picked: Classic or Randomizer, then what the randomizer shuffles and the
 * seed. Drawn on both screens (the top one over the file menu); touch on the
 * bottom one, +Control Pad / A / B / Y / START on either. */
enum { NG_CLOSED, NG_MODE, NG_RANDO };
enum { NG_SND_NONE, NG_SND_MOVE, NG_SND_NEXT, NG_SND_BACK, NG_SND_TOGGLE };   /* for the file menu to play */
#define NG_TOGGLES 5
#define NG_ROW_SEED NG_TOGGLES
#define NG_ROW_START (NG_TOGGLES + 1)
#define NG_ROW_Y0 46
#define NG_ROW_H 21
static const struct { unsigned flag; const char *label, *help; } s_ng_toggles[NG_TOGGLES] = {
    { PM3DS_RANDO_COINS, "Coins & star pieces", "Coins and star pieces lying around are shuffled too." },
    { PM3DS_RANDO_KEYS, "Key items", "Key items move too, always somewhere you can reach before you need them." },
    { PM3DS_RANDO_SHOPS, "Shops", "Shops sell items from the world, and the world holds the shops' stock." },
    { PM3DS_RANDO_GIFTS, "Gifts", "What characters give you is shuffled too." },
    { PM3DS_RANDO_HIDDEN, "Keys in hidden spots", "Key items can also hide in hidden panels and blocks." },
};
static int s_ng_page, s_ng_cursor, s_ng_result, s_ng_sound;
static unsigned s_ng_flags, s_ng_seed;
static bool s_ng_fresh;   /* just opened: the press that opened it is still down */
static bool s_ng_hold;    /* closed, but the game sees no buttons until they are let go */
static bool s_ng_kbd;     /* type a seed: the keyboard opens from the tick, outside a frame */

void pm3ds_new_game_picker_open(void) {
    s_ng_page = NG_MODE;
    s_ng_cursor = (pm3ds_cfg.rando_flags & PM3DS_RANDO_ON) ? 1 : 0;
    s_ng_flags = pm3ds_cfg.rando_flags & ~PM3DS_RANDO_ON;
    s_ng_seed = 0;
    s_ng_result = 0;
    s_ng_fresh = true;
}

/* src/filemenu/filemenu_main.c, each frame: -1 while open, 1 once a game
 * mode is chosen, else 0; *sound: NG_SND_* to play */
int pm3ds_new_game_picker(int *sound) {
    *sound = s_ng_sound;
    s_ng_sound = NG_SND_NONE;
    if (s_ng_page != NG_CLOSED) return -1;
    int r = s_ng_result;
    s_ng_result = 0;
    return r;
}

/* n64_shim/os_cont.c: the game's buttons are the picker's for now */
bool pm3ds_new_game_picker_blocks_input(void) { return s_ng_page != NG_CLOSED || s_ng_hold; }

static void ng_close(int result, int sound) {
    s_ng_page = NG_CLOSED;
    s_ng_result = result;
    s_ng_sound = sound;
    s_ng_hold = true;
}

static void ng_start(bool rando) {
    unsigned flags = rando ? (s_ng_flags | PM3DS_RANDO_ON) : 0;
    pm3ds_rando_choose(flags, s_ng_seed);
    pm3ds_cfg.rando_flags = s_ng_flags | (rando ? PM3DS_RANDO_ON : 0);
    pm3ds_settings_save();
    ng_close(1, NG_SND_NEXT);
}

static SwkbdCallbackResult ng_seed_filter(void *user, const char **msg, const char *text, size_t len) {
    (void)user;
    for (size_t i = 0; i < len; i++) {
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f') ||
              (text[i] >= 'A' && text[i] <= 'F'))) {
            *msg = "A seed is up to 8 characters: 0-9 and A-F.";
            return SWKBD_CALLBACK_CONTINUE;
        }
    }
    return SWKBD_CALLBACK_OK;
}

static void ng_keyboard(void) {
    SwkbdState kb;
    char buf[16] = "";
    if (s_ng_seed) snprintf(buf, sizeof buf, "%08X", s_ng_seed);
    swkbdInit(&kb, SWKBD_TYPE_WESTERN, 2, 8);
    swkbdSetHintText(&kb, "Seed (0-9, A-F)");
    swkbdSetInitialText(&kb, buf);
    swkbdSetValidation(&kb, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
    swkbdSetFilterCallback(&kb, ng_seed_filter, NULL);
    if (swkbdInputText(&kb, buf, sizeof buf) == SWKBD_BUTTON_CONFIRM) {
        s_ng_seed = (unsigned)strtoul(buf, NULL, 16);
        s_ng_sound = NG_SND_NEXT;
    }
}

static void ng_rando_activate(int row) {
    if (row < NG_TOGGLES) {
        s_ng_flags ^= s_ng_toggles[row].flag;
        s_ng_sound = NG_SND_TOGGLE;
    } else if (row == NG_ROW_SEED) {
        s_ng_kbd = true;
    } else {
        ng_start(true);
    }
}

static void ng_buttons(void) {
    static u32 prev;
    u32 held = hidKeysHeld() & ~KEY_TOUCH;
    u32 kd = held & ~prev;
    prev = held;
    if (s_ng_hold && held == 0) s_ng_hold = false;
    if (s_ng_page == NG_CLOSED) return;
    if (s_ng_fresh) {
        s_ng_fresh = false;
        return;
    }
    if (s_ng_page == NG_MODE) {
        if (kd & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN)) {
            s_ng_cursor ^= 1;
            s_ng_sound = NG_SND_MOVE;
        } else if (kd & (KEY_A | KEY_START)) {
            if (s_ng_cursor == 0) {
                ng_start(false);
            } else {
                s_ng_page = NG_RANDO;
                s_ng_cursor = NG_ROW_START;
                s_ng_sound = NG_SND_NEXT;
            }
        } else if (kd & KEY_B) {
            ng_close(0, NG_SND_BACK);
        }
        return;
    }
    if (kd & KEY_UP) {
        s_ng_cursor = (s_ng_cursor + NG_ROW_START) % (NG_ROW_START + 1);
        s_ng_sound = NG_SND_MOVE;
    } else if (kd & KEY_DOWN) {
        s_ng_cursor = (s_ng_cursor + 1) % (NG_ROW_START + 1);
        s_ng_sound = NG_SND_MOVE;
    } else if ((kd & (KEY_LEFT | KEY_RIGHT)) && s_ng_cursor < NG_TOGGLES) {
        ng_rando_activate(s_ng_cursor);
    } else if (kd & KEY_A) {
        ng_rando_activate(s_ng_cursor);
    } else if ((kd & KEY_Y) && s_ng_cursor == NG_ROW_SEED) {
        s_ng_seed = 0;
        s_ng_sound = NG_SND_TOGGLE;
    } else if (kd & KEY_START) {
        ng_start(true);
    } else if (kd & KEY_B) {
        s_ng_page = NG_MODE;
        s_ng_cursor = 1;
        s_ng_sound = NG_SND_BACK;
    }
}

static void ng_touch(int px, int py) {
    if (s_ng_page == NG_MODE) {
        if (in(px, py, 16, 60, 156, 170)) {
            s_ng_cursor = 0;
            ng_start(false);
        } else if (in(px, py, 164, 60, 304, 170)) {
            s_ng_page = NG_RANDO;
            s_ng_cursor = NG_ROW_START;
            s_ng_sound = NG_SND_NEXT;
        } else if (in(px, py, 8, 212, 88, 236)) {
            ng_close(0, NG_SND_BACK);
        }
        return;
    }
    if (in(px, py, 8, 212, 88, 236)) {
        s_ng_page = NG_MODE;
        s_ng_cursor = 1;
        s_ng_sound = NG_SND_BACK;
    } else if (in(px, py, 196, 212, 316, 236)) {
        ng_start(true);
    } else if (py >= NG_ROW_Y0 && py < NG_ROW_Y0 + (NG_ROW_SEED + 1) * NG_ROW_H) {
        s_ng_cursor = (py - NG_ROW_Y0) / NG_ROW_H;
        ng_rando_activate(s_ng_cursor);
    }
}

/* console-font text word-wrapped to 36 columns, each line centred on cx */
static void ng_help(float cx, float y, const char *str) {
    char line[40];
    while (*str) {
        int n = (int)strlen(str), cut = n;
        if (n > 36) {
            for (cut = 36; cut > 0 && str[cut] != ' '; cut--) {}
            if (cut == 0) cut = 36;
        }
        memcpy(line, str, cut);
        line[cut] = 0;
        small_text_center(cx, y, line, 1.0f, COL_PANEL_EDGE);
        y += 10;
        str += cut;
        while (*str == ' ') str++;
    }
}

/* a pill button; lit: the cursor is on it */
static void ng_button(float x0, float y0, float x1, float y1, const char *label, bool lit) {
    if (lit) round_rect(x0 - 3, y0 - 3, x1 + 3, y1 + 3, 11, RGBA(255, 196, 64, 255));
    round_rect(x0, y0, x1, y1, 8, COL_BTN);
    text_center((x0 + x1) / 2, y0 + 3, label, 1.0f, 1, COL_WHITE);
}

/* the picker at width w (top 400, bottom 320), centred */
static void draw_new_game(float w) {
    float dx = (w - 320) / 2;
    char buf[16];
    if (w > 320) fill(0, 0, w, 240, RGBA(40, 20, 0, 190));   /* dim the file menu behind it */
    panel(dx + 4, 4, dx + 316, 204, COL_PANEL);
    if (s_ng_page == NG_MODE) {
        static const char *const names[2] = { "Classic", "Randomizer" };
        static const char *const desc[2][2] = { { "The original", "adventure" }, { "Items & badges", "in new places" } };
        text_center(dx + 160, 12, "New game", 1.3f, 0, COL_WHITE);
        small_text_center(dx + 160, 38, "How do you want to play?", 1.0f, COL_PANEL_EDGE);
        for (int i = 0; i < 2; i++) {
            float x0 = dx + (i ? 164 : 16), x1 = x0 + 140;
            bool lit = s_ng_cursor == i;
            if (lit) round_rect(x0 - 4, 56, x1 + 4, 174, 12, RGBA(255, 196, 64, 255));
            round_rect(x0, 60, x1, 170, 10, COL_PANEL_EDGE);
            round_rect(x0 + 2, 62, x1 - 2, 168, 8, lit ? RGBA(255, 214, 120, 255) : COL_TAB);
            text_center((x0 + x1) / 2, 86, names[i], 1.2f, 0, COL_WHITE);
            small_text_center((x0 + x1) / 2, 124, desc[i][0], 1.0f, COL_PANEL_EDGE);
            small_text_center((x0 + x1) / 2, 136, desc[i][1], 1.0f, COL_PANEL_EDGE);
        }
        small_text_center(dx + 160, 186, "A: choose    B: back", 1.0f, COL_PANEL_EDGE);
        ng_button(dx + 8, 212, dx + 88, 234, "Back", false);
        return;
    }
    text_center(dx + 160, 8, "Randomizer", 1.2f, 0, COL_WHITE);
    small_text_center(dx + 160, 34, "Items and badges are always shuffled.", 1.0f, COL_PANEL_EDGE);
    for (int row = 0; row <= NG_ROW_SEED; row++) {
        float y = NG_ROW_Y0 + row * NG_ROW_H;
        const char *value;
        if (row < NG_TOGGLES) {
            value = (s_ng_flags & s_ng_toggles[row].flag) ? "On" : "Off";
        } else {
            if (s_ng_seed) snprintf(buf, sizeof buf, "%08X", s_ng_seed);
            value = s_ng_seed ? buf : "Random";
        }
        /* hidden spots only matter for shuffled key items */
        bool idle = row < NG_TOGGLES && s_ng_toggles[row].flag == PM3DS_RANDO_HIDDEN && !(s_ng_flags & PM3DS_RANDO_KEYS);
        u32 tint = idle ? RGBA(255, 255, 255, 110) : COL_WHITE;
        if (s_ng_cursor == row) round_rect(dx + 10, y, dx + 310, y + NG_ROW_H, 8, RGBA(255, 214, 120, 150));
        text(dx + 18, y + 2, row < NG_TOGGLES ? s_ng_toggles[row].label : "Seed", 1.0f, 0, tint);
        float vw = text_width(value, 1.0f) + 24;
        if (vw < 72) vw = 72;
        bool on = row < NG_TOGGLES && (s_ng_flags & s_ng_toggles[row].flag);
        round_rect(dx + 302 - vw, y + 1, dx + 302, y + NG_ROW_H - 1, 9, COL_PANEL_EDGE);
        round_rect(dx + 304 - vw, y + 3, dx + 300, y + NG_ROW_H - 3, 7,
                   (row == NG_ROW_SEED || on) && !idle ? RGBA(255, 214, 120, 255) : RGBA(205, 190, 170, 255));
        text_center(dx + 302 - vw / 2, y + 2, value, 1.0f, 0, tint);
    }
    const char *help = s_ng_cursor < NG_TOGGLES ? s_ng_toggles[s_ng_cursor].help
                       : s_ng_cursor == NG_ROW_SEED ? "A: type a seed, Y: random. Same seed and settings, same game."
                                                    : "START or A: begin. B: back.";
    ng_help(dx + 160, 176, help);
    ng_button(dx + 8, 212, dx + 88, 234, "Back", false);
    ng_button(dx + 196, 212, dx + 312, 234, "Start", s_ng_cursor == NG_ROW_START);
}

static void draw_menu(void) {
    gradient(0, 0, 320, 240, COL_BG_TOP, COL_BG_BOTTOM);
    if (s_ng_page != NG_CLOSED) {
        draw_new_game(320);
        return;
    }
    if (s_tab == TAB_DEBUG) {
        draw_debug();
        return;
    }
    if (s_tab == TAB_OPTIONS) {
        draw_options();
        return;
    }
    if (s_tab == TAB_CONTROLS) {
        draw_controls();
        return;
    }
    if (!s_info.in_game) {
        panel(20, 60, 300, 170, COL_PANEL);
        text_center(160, 78, PM3DS_APP_NAME, 2.0f, 0, COL_WHITE);
        text_center(160, 124, s_info.demo ? "- Demo -" : "Press START to play", 1.0f, 0, COL_WHITE);
        small_text_center(160, 154, "v" PM3DS_VERSION_STR, 1.0f, COL_PANEL_EDGE);
        draw_buttons();
        return;
    }
    if (battle_page()) {
        draw_battle();
        draw_buttons();
        return;
    }
    if (tm_open() && s_tab <= TAB_PARTY) {   /* an item or partner list in the world */
        draw_tabs();
        draw_tmenu(PANEL_Y0);
        draw_buttons();
        return;
    }
    draw_tabs();
    panel(4, PANEL_Y0, 316, PANEL_Y1, COL_PANEL);
    switch (s_tab) {
        case TAB_STATS: draw_stats(); break;
        case TAB_MAP:   draw_map();   break;
        case TAB_PARTY: draw_party(); break;
    }
    draw_buttons();
}

/* ---- input --------------------------------------------------------------- */
static int s_tab_before_options = TAB_STATS;

static bool in(int px, int py, float x0, float y0, float x1, float y1) {
    return px >= x0 && px < x1 && py >= y0 && py < y1;
}

static void handle_touch(void) {
    /* own edge detection: the input shim also scans HID, which can eat the
     * one-scan hidKeysDown() edge before this runs */
    static bool was_down;
    static int last_x, last_y, drag_dist;
    touchPosition tp = { 0, 0 };
    bool down = (hidKeysHeld() & KEY_TOUCH) != 0;
    if (down) hidTouchRead(&tp);
    bool pressed = down && !was_down;
    was_down = down;

    if (down && !pressed && s_tab == TAB_MAP && s_info.in_game) {   /* drag the map */
        int dx = tp.px - last_x, dy = tp.py - last_y;
        if (in(last_x, last_y, 10, PANEL_Y0 + 8, 310, PANEL_Y1 - 8) && (dx || dy)) {
            drag_dist += abs(dx) + abs(dy);
            if (drag_dist > 4) {
                s_map_follow = false;
                s_map_cx -= dx;
                s_map_cy -= dy;
            }
        }
    }
    if (down) { last_x = tp.px; last_y = tp.py; }

    /* held buttons: the battle pad and Back/Swap */
    pm3ds_touch_pad = 0;
    pm3ds_touch_stick_x = 0;
    if (down && s_tab <= TAB_PARTY && s_info.in_game) {
        uint16_t mid_pad;
        pad_held(tp.px, tp.py);
        if (mid_button(&mid_pad) && mid_pad && in(tp.px, tp.py, s_btn_items[0], BTN_Y0, s_btn_items[1], BTN_Y1))
            pm3ds_touch_pad |= mid_pad;
    }
    if (!pressed) return;
    drag_dist = 0;
    if (s_ng_page != NG_CLOSED) {
        ng_touch(tp.px, tp.py);
        return;
    }

    bool back = in(tp.px, tp.py, 250, 214, 316, 236);
    if (s_tab == TAB_DEBUG) {
        if (back) s_tab = TAB_OPTIONS;
        return;
    }
    if (s_tab == TAB_OPTIONS) {
        if (back) {
            s_tab = s_tab_before_options;
            return;
        }
        int row = (tp.py - OPT_Y0) / OPT_ROW_H;
        if (tp.py < OPT_Y0 || row >= OPT_ROWS) return;
        switch (row) {
            case 0: pm3ds_cfg.view_mode = (pm3ds_cfg.view_mode + 1) % 3; break;   /* Original -> Fill -> Wide */
            case 1: pm3ds_cfg.fps60 = !pm3ds_cfg.fps60; break;
            case 2: pm3ds_cfg.text_speed = (pm3ds_cfg.text_speed + 1) % TEXT_SPEEDS; break;
            case 3: pm3ds_cfg.ff_speed = pm3ds_cfg.ff_speed >= 4 ? 2 : pm3ds_cfg.ff_speed + 1; break;
            case 4: pm3ds_cfg.ff_toggle = !pm3ds_cfg.ff_toggle; break;
            case 5: s_tab = TAB_CONTROLS; return;
            case 6: s_tab = TAB_DEBUG; return;
        }
        pm3ds_settings_save();
        return;
    }
    if (s_tab == TAB_CONTROLS) {
        if (back) {
            s_tab = TAB_OPTIONS;
        } else if (in(tp.px, tp.py, 8, 214, 108, 236)) {
            pm3ds_settings_default_keymap(pm3ds_cfg.keymap);
            pm3ds_settings_save();
        } else if (tp.py >= CTL_Y0 && tp.py < CTL_Y0 + CTL_ROWS * CTL_ROW_H) {
            int k = (tp.py - CTL_Y0) / CTL_ROW_H + (tp.px >= 160 ? CTL_ROWS : 0);
            if (k < KEY_IDX_COUNT) {
                pm3ds_cfg.keymap[k] = (uint8_t)((pm3ds_cfg.keymap[k] + 1) % ACT_COUNT);
                pm3ds_settings_save();
            }
        }
        return;
    }
    if (in(tp.px, tp.py, s_btn_opt[0], BTN_Y0, s_btn_opt[1], BTN_Y1)) {
        s_tab_before_options = s_tab;
        s_tab = TAB_OPTIONS;
    } else if (in(tp.px, tp.py, s_btn_ff[0], BTN_Y0, s_btn_ff[1], BTN_Y1)) {
        pm3ds_ff_button_on = !pm3ds_ff_button_on;
    } else if (s_info.in_game && in(tp.px, tp.py, s_btn_items[0], BTN_Y0, s_btn_items[1], BTN_Y1)) {
        uint16_t mid_pad;
        const char *mid = mid_button(&mid_pad);
        if (mid && !mid_pad) pm3ds_touch_req = 1;   /* Items; Back/Swap are held above */
    } else if (battle_page()) {
        tmenu_tap(tp.px, tp.py, 34);
    } else if (s_info.in_game && s_tab <= TAB_PARTY && tm_open() && tp.py >= PANEL_Y0) {
        tmenu_tap(tp.px, tp.py, PANEL_Y0);
    } else if (s_info.in_game && s_tab == TAB_PARTY && partner_at(tp.px, tp.py)) {
        int id = partner_at(tp.px, tp.py);
        if ((s_info.partners_owned >> id) & 1) pm3ds_touch_req = 0x100 + id;
    } else if (s_info.in_game && tp.py < TAB_Y1 + 2) {
        for (int i = 0; i < 3; i++)
            if (in(tp.px, tp.py, s_tab_x[i][0], 0, s_tab_x[i][1], TAB_Y1 + 2)) s_tab = i;
        if (s_tab == TAB_MAP) s_map_follow = true;
    } else if (s_tab == TAB_MAP && in(tp.px, tp.py, 10, PANEL_Y0 + 8, 160, PANEL_Y0 + 22)) {
        s_map_follow = true;
    }
}

/* ---- public -------------------------------------------------------------- */
void pm3ds_bottom_screen_init(void) {
    pm3ds_settings_load();

    init_morton();
    s_target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, -1);
    if (s_target)
        C3D_RenderTargetSetOutput(s_target, GFX_BOTTOM, GFX_LEFT,
                                  GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                  GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                                  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    s_vbuf = (vtx_t *)linearAlloc(sizeof(vtx_t) * UI_MAX_VERTS);
    AttrInfo_Init(&s_attr);
    AttrInfo_AddLoader(&s_attr, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(&s_attr, 1, GPU_UNSIGNED_BYTE, 4);
    AttrInfo_AddLoader(&s_attr, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&s_attr, 3, GPU_FLOAT, 2);
    BufInfo_Init(&s_buf);
    BufInfo_Add(&s_buf, s_vbuf, sizeof(vtx_t), 4, 0x3210);
    /* bottom-screen pixel space -> rotated PICA clip space (320x240) */
    Mtx_Zeros(&s_mtx);
    s_mtx.r[0].y = -1.0f / 120.0f; s_mtx.r[0].w = 1.0f;
    s_mtx.r[1].x = -1.0f / 160.0f; s_mtx.r[1].w = 1.0f;
    s_mtx.r[2].z = 0.5f;           s_mtx.r[2].w = -0.5f;
    s_mtx.r[3].w = 1.0f;
    /* the same for the top screen (400x240), where the boot screens draw too */
    s_mtx_top = s_mtx;
    s_mtx_top.r[1].x = -1.0f / 200.0f;
    build_atlas();
    build_map();
    build_small_font();
}

/* once per presented frame (input + state) */
void pm3ds_bottom_screen_tick(unsigned frame) {
    s_frame = frame;
    ng_buttons();
    handle_touch();
    if (s_ng_kbd) {
        s_ng_kbd = false;
        ng_keyboard();
    }
    pm3ds_console_debug = s_tab == TAB_DEBUG;
    if (!s_font_ok && (frame % 30) == 0) build_font();
    if (frame % 5 == 0) pm3ds_hud_query(&s_info);
    pm3ds_tm_get(&s_tm);
}

/* GPU state for a UI pass on target t with pixel matrix m; false if the
 * renderer is not up. after: a pass earlier in the same frame drew from the
 * vertex buffer, so its vertices stay and this one appends. */
static bool begin_ui_on(C3D_RenderTarget *t, const C3D_Mtx *m, bool after) {
    if (!t || !s_vbuf || !s_atlas_ok) return false;
    if (after) flush();
    C3D_FrameDrawOn(t);
    C3D_BindProgram(pm3ds_gfx_program());
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, pm3ds_gfx_uloc_screen(), m);
    C3D_SetAttrInfo(&s_attr);
    C3D_SetBufInfo(&s_buf);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));

    if (!after) s_vcount = 0;
    s_batch_start = s_vcount;
    s_cur_tex = NULL;
    return true;
}

static void end_ui(void) {
    flush();
    GSPGPU_FlushDataCache(s_vbuf, sizeof(vtx_t) * (u32)s_vcount);
}

/* inside a citro3d frame, after the top screen; top, top_right: its render
 * targets (top_right NULL without 3D), for the new-game picker */
void pm3ds_bottom_screen_render(C3D_RenderTarget *top, C3D_RenderTarget *top_right) {
    bool after = false;
    if (s_ng_page != NG_CLOSED) {
        C3D_RenderTarget *eyes[2] = { top, top_right };
        for (int i = 0; i < 2; i++) {
            if (eyes[i] && begin_ui_on(eyes[i], &s_mtx_top, after)) {
                draw_new_game(400);
                after = true;
            }
        }
    }
    if (!begin_ui_on(s_target, &s_mtx, after)) {
        if (after) end_ui();
        return;
    }
    draw_menu();
    end_ui();
}

/* 8x8 console-font text at an integer scale, centred on cx */
static void small_text_center(float cx, float y, const char *str, float scale, u32 c) {
    if (!s_small_ok) return;
    float x = cx - strlen(str) * 4.0f * scale;
    for (; *str; str++, x += 8 * scale) {
        unsigned char ch = (unsigned char)*str;
        if (ch > 32 && ch < 128) {
            float gx = (ch % 16) * 8.0f, gy = (ch / 16) * 8.0f;
            quad2(&s_small, x, y, x + 8 * scale, y + 8 * scale,
                  gx / 128.0f, gy / 64.0f, (gx + 8) / 128.0f, (gy + 8) / 64.0f, c, c);
        }
    }
}

static const char *s_loading_label;

/* what the progress bar is for; NULL: loading the game data */
void pm3ds_bottom_screen_loading_label(const char *label) { s_loading_label = label; }

/* Boot screens, drawn on both screens so the top one is never just black.
 * The game's font comes from the ROM, so these use the console font.
 * w: 400 (top) or 320 (bottom). */
static void loading_panel(float w, int permille) {
    float cx = w / 2, dx = (w - 320) / 2;
    gradient(0, 0, w, 240, COL_BG_TOP, COL_BG_BOTTOM);
    panel(36 + dx, 62, 284 + dx, 178, COL_PANEL);
    small_text_center(cx, 82, PM3DS_APP_NAME_CAPS, 2.0f, COL_PANEL_EDGE);
    small_text_center(cx, 112, s_loading_label ? s_loading_label : "Loading game data", 1.0f, COL_PANEL_EDGE);
    bar(60 + dx, 132, 200, 10, permille, 1000, COL_FP);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", permille / 10);
    small_text_center(cx, 152, pct, 1.0f, COL_PANEL_EDGE);
}

/* lines[0] is the headline: twice the size where it fits */
static void message_panel(float w, const char *const *lines, int n) {
    float cx = w / 2, y = 52;
    gradient(0, 0, w, 240, COL_BG_TOP, COL_BG_BOTTOM);
    panel(10, 10, w - 10, 230, COL_PANEL);
    small_text_center(cx, 22, PM3DS_APP_NAME_CAPS, 2.0f, COL_PANEL_EDGE);
    for (int i = 0; i < n && i < 14; i++) {
        float s = (i == 0 && strlen(lines[i]) * 16.0f <= w - 40) ? 2.0f : 1.0f;
        small_text_center(cx, y, lines[i], s, COL_PANEL_EDGE);
        y += 4 + 8 * s;
    }
}

/* Boot: progress of the ROM load, inside a citro3d frame; top: the top
 * screen's render target */
void pm3ds_ui_loading(C3D_RenderTarget *top, int permille) {
    bool after = false;
    if (top && begin_ui_on(top, &s_mtx_top, false)) {
        loading_panel(400, permille);
        after = true;
    }
    if (!begin_ui_on(s_target, &s_mtx, after)) {
        if (after) end_ui();
        return;
    }
    loading_panel(320, permille);
    end_ui();
}

/* Boot problems (no ROM, wrong ROM): the same text on both screens */
void pm3ds_ui_message(C3D_RenderTarget *top, const char *const *lines, int n) {
    bool after = false;
    if (top && begin_ui_on(top, &s_mtx_top, false)) {
        message_panel(400, lines, n);
        after = true;
    }
    if (!begin_ui_on(s_target, &s_mtx, after)) {
        if (after) end_ui();
        return;
    }
    message_panel(320, lines, n);
    end_ui();
}
