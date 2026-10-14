#ifndef _GU_H_
#define _GU_H_

// 3DS stub for N64 gu.h (graphics utility / matrix functions)
#include "PR/ultratypes.h"
#include "PR/gbi.h"

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#define M_PI        3.14159265358979323846
#define M_DTOR      (3.14159265358979323846/180.0)

#define FTOFIX32(x) (long)((x) * (float)0x00010000)
#define FIX32TOF(x) ((float)(x) * (1.0f / (float)0x00010000))
#define FTOFRAC8(x) ((int) MIN(((x) * (128.0f)), 127.0f) & 0xff)

#define FILTER_WRAP 0
#define FILTER_CLAMP 1

#define RAND(x) (guRandom()%x)

typedef struct {
    unsigned char   *base;
    int             fmt, siz;
    int             xsize, ysize;
    int             lsize;
    int             addr;
    int             w, h;
    int             s, t;
} Image;

typedef struct {
    float   col[3];
    float   pos[3];
    float   a1, a2;
} PositionalLight;

// LookAt and Hilite are referenced by gu* function declarations below
// LookAt / Hilite come from upstream PR/gbi.h (M12 GBI swap).

// Matrix functions (declarations - implementations in math_impl.c or src/os/*.c)
extern void guMtxIdentF(float mf[4][4]);
extern void guMtxIdent(Mtx* m);
extern void guMtxCatF(float m[4][4], float n[4][4], float r[4][4]);
extern void guMtxCatL(Mtx* m, Mtx* n, Mtx* r);
extern void guMtxF2L(float mf[4][4], Mtx* m);
extern void guMtxL2F(float mf[4][4], Mtx* m);
extern void guMtxXFML(Mtx* m, float x, float y, float z, float* ox, float* oy, float* oz);
extern void guMtxXFMF(float mf[4][4], float x, float y, float z, float* ox, float* oy, float* oz);
extern void guOrthoF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale);
extern void guOrtho(Mtx* m, float l, float r, float b, float t, float n, float f, float scale);
extern void guPerspectiveF(float mf[4][4], u16 *perspNorm, float fovy, float aspect, float near, float far, float scale);
extern void guPerspective(Mtx* m, u16 *perspNorm, float fovy, float aspect, float near, float far, float scale);
// M4 wave 12: declarations referenced by src/curtains.c et al.
extern void guFrustumF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale);
extern void guFrustum(Mtx* m, float l, float r, float b, float t, float n, float f, float scale);
extern void guPositionF(float mf[4][4], float r, float p, float h, float s, float x, float y, float z);
extern void guPosition(Mtx* m, float r, float p, float h, float s, float x, float y, float z);
extern void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
extern void guLookAt(Mtx* m, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
extern void guLookAtReflectF(float mf[4][4], LookAt* l, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
extern void guLookAtReflect(Mtx* m, LookAt* l, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
extern void guLookAtHiliteF(float mf[4][4], LookAt* l, Hilite* h, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp, float xl1, float yl1, float zl1, float xl2, float yl2, float zl2, int twidth, int theight);
extern void guLookAtHilite(Mtx* m, LookAt* l, Hilite* h, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp, float xl1, float yl1, float zl1, float xl2, float yl2, float zl2, int twidth, int theight);
extern void guRotateF(float mf[4][4], float a, float x, float y, float z);
extern void guRotate(Mtx* m, float a, float x, float y, float z);
extern void guRotateRPYF(float mf[4][4], float r, float p, float h);
extern void guRotateRPY(Mtx* m, float r, float p, float h);
extern void guScaleF(float mf[4][4], float x, float y, float z);
extern void guScale(Mtx* m, float x, float y, float z);
extern void guTranslateF(float mf[4][4], float x, float y, float z);
extern void guTranslate(Mtx* m, float x, float y, float z);
extern void guNormalize(float* x, float* y, float* z);
extern int  guLoadTextureBlockMipMap(Gfx **glist, unsigned char *tbuf, Image *im, unsigned char startTile, unsigned char pal, unsigned char cms, unsigned char cmt, unsigned char masks, unsigned char maskt, unsigned char shifts, unsigned char shiftt, unsigned char cfs, unsigned char cft);
extern int  guGetDPLoadTextureTileSz(int ult, int lrt);
extern void guDPLoadTextureTile(Gfx *glistp, void *timg, int texl_fmt, int texl_size, int txwd, int txht, int tilewd, int tileht, int tuls, int tult, int tlrs, int tlrt, int tile, int cms, int cmt, int masks, int maskt, int shifts, int shiftt);
extern void guDPLoadTextureTile_4b(Gfx *glistp, void *timg, int texl_fmt, int txwd, int txht, int tilewd, int tileht, int tuls, int tult, int tlrs, int tlrt, int tile, int cms, int cmt, int masks, int maskt, int shifts, int shiftt);

extern int guRandom(void);

#endif

