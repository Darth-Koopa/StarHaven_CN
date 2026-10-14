/*
 * 3DS port: the real F3DEX2 GBI.
 *
 * Until M12 this was a hand-written stand-in whose 77 static gs* macros all
 * expanded to 0 and whose g* macros went through pm3ds_*_impl emitters, so
 * every static display list in the game was garbage. The upstream encodings
 * are plain integer packing into {w0, w1} and work unchanged on ARM32
 * (pointers are 32-bit, aggregate initializers with address constants are
 * fine), so the game now emits canonical F3DEX2 and the translator decodes
 * exactly what the N64 RSP would see. Old header: gbi_port_legacy.h.
 */
#ifndef PM3DS_PR_GBI_WRAPPER_H
#define PM3DS_PR_GBI_WRAPPER_H
#ifndef F3DEX_GBI_2
#define F3DEX_GBI_2 1
#endif
#ifndef _LANGUAGE_C
#define _LANGUAGE_C 1  /* upstream wraps its typedefs in this */
#define PM3DS_GBI_UNDEF_LANGUAGE_C
#endif
/* upstream gets these from its PR/mbi.h; the port's mbi.h does not carry them */
#ifndef G_ON
#define G_ON    (1)
#define G_OFF   (0)
#endif
#ifndef _SHIFTL
/* (int) first: the game passes float expressions (texrect s/t, positions),
 * and float -> unsigned saturates negatives to 0 on ARM, where the N64's
 * compiler wrapped them like an int (the darkness overlay's light hole sat
 * at s = 0, off-screen, so dark rooms had no light at all) */
#define _SHIFTL(v, s, w)    ((unsigned int) (((unsigned int)(int)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)    ((unsigned int)(((unsigned int)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include "../../../../include/PR/gbi.h"
/* 60 fps (n64_shim/gfx_interp.c): each G_MTX the game writes notes its call
 * site, so the renderer can pair it with the same matrix one step earlier */
void pm3ds_mtx_site(const void *gfx, const char *file, int line);
#undef gSPMatrix
#define gSPMatrix(pkt, m, p) {                                               \
    Gfx *_pm3ds_mg = (Gfx *)(pkt);                                           \
    gDma2p(_pm3ds_mg, G_MTX, (m), sizeof(Mtx), (p) ^ G_MTX_PUSH, 0);         \
    pm3ds_mtx_site(_pm3ds_mg, __FILE__, __LINE__);                           \
}
/* keep it scoped: upstream PR/os_*.h would otherwise redefine the port's OS types */
#ifdef PM3DS_GBI_UNDEF_LANGUAGE_C
#undef _LANGUAGE_C
#undef PM3DS_GBI_UNDEF_LANGUAGE_C
#endif
#endif
