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

#endif
