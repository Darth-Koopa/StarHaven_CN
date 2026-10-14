/* level_up_segment.c — compiles src/level_up.c (the level-up screen's images
 * and HudScripts). The N64 DMA'd it as an overlay; the port never built it,
 * so its HES_level_up_* symbols resolved to the weak m6 link stubs and the
 * level-up screen had nothing to draw. Included under a name that does not
 * collide with src/battle/level_up.c in the object list. */
#include "../../../../src/level_up.c"
