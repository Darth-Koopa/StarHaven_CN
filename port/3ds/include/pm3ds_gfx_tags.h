#ifndef PM3DS_GFX_TAGS_H
#define PM3DS_GFX_TAGS_H
/*
 * G_NOOP tags (gDPNoOpTag) game code emits to tell the 3DS display-list
 * translator something the N64 command stream cannot say.
 */

/* The next G_SETCIMG is draw_box's render-to-texture target for a rotating
 * or scaling window: render the contents into a GPU texture and serve the
 * window's texture loads from that address out of it (gfx_present.c). */
#define PM3DS_GFX_TAG_BOX_RTT 0x50335254u   /* 'P3RT' */

/* The next G_SETCIMG names an image the game fills with a copy of the
 * framebuffer drawn so far (underwater.c copies it with texture rectangles
 * that read the framebuffer). The port copies the GPU's render target into a
 * texture instead, and texture loads from that image sample it. */
#define PM3DS_GFX_TAG_SCREEN_COPY 0x50335343u   /* 'P3SC' */

#endif
