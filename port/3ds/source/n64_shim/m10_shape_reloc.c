/*
 * m10_shape_reloc.c -- relocate a decoded N64 map ShapeFile for use on 3DS.
 *
 * WHY
 * ---
 * decode_yay0() drops a map shape into gMapShapeData exactly as it sat in N64
 * RDRAM: every field BIG-ENDIAN, every pointer an ABSOLUTE N64 address baked
 * for the address the N64 loaded it at. Neither is usable on ARM/3DS.
 *
 * The one number this needs is that N64 load address. It is 0x80210000, and it
 * did not need guessing: src/heaps.c lays out N64 BSS by address and declares
 * gMapShapeData immediately after D_8020A000[0x6000], which ends exactly there.
 * Verified against live data (MAPSHAPE forensics, demo map):
 *     dsize=0x13AE0  root=0x80222DDC vtxTable=0x80213770 modelNames=0x80222E10
 *     colliderNames=0x80223150 zoneNames=0x802231F0
 *   -> offsets 0x12DDC / 0x3770 / 0x12E10 / 0x13150 / 0x131F0, all < dsize,
 * and rebasing modelNames yields real names ("o352", "moko_3", "o356", ...).
 * A depth-1 walk found numChildren=18 with 8/8 children carrying legal node
 * types (first = 5 = SHAPE_TYPE_GROUP). So both the rule and the struct
 * layouts are confirmed against real data, not inferred.
 *
 * WHAT IS AND IS NOT DONE HERE
 * ----------------------------
 * DONE: ShapeFileHeader, the ModelNode tree (ModelNode / ModelGroupData /
 *       ModelDisplayData), and the three name arrays.
 * NOT DONE, and it matters: the embedded DISPLAY LISTS. Each
 *       ModelDisplayData->displayList points at a Gfx[] whose command words
 *       carry their own absolute vertex/texture addresses. Those are NOT
 *       touched here, so geometry will not draw correctly yet -- this makes the
 *       map STRUCTURE traversable, not the map renderable. Counted and reported
 *       as `dls` so the gap is visible rather than silent.
 * DONE (M10.2): ModelNodeProperty's data union. TEXTURE_NAME properties hold
 *       char* pointers that load_texture_by_name strcmp's (model.c:2047); left
 *       raw they flooded ~9k unmapped reads at byte-swapped in-blob addresses
 *       (0x10322280 == bswap(0x80223210), one read per archive header per
 *       model, ending in Azahar's "unknown GPU address" abort). Rule: a data
 *       word that decodes to an in-blob N64 pointer is rebased, anything else
 *       is a plain 4-byte swap (ints, floats, bounding-box words). Scalars
 *       cannot collide with the pointer window: 0x8021xxxx..0x8022xxxx reads
 *       as a ~1e-38 denormal float or a ~-2.1e9 int, neither of which authored
 *       map data contains.
 *
 * Deliberately in its own translation unit: oversized diagnostic functions in
 * m6_load_engine_data_wrap.c reproducibly produced a 3DSX that Azahar refused
 * to load, with no structural explanation (see project memory).
 */

#include "pm3ds_log.h"

#include <stdint.h>
#include <stddef.h>

/* N64 address gMapShapeData was linked for. See header comment. */
#define PM3DS_SHAPE_N64_BASE 0x80210000u

/* include/model.h:152-155 */
#define SHAPE_TYPE_MODEL          2
#define SHAPE_TYPE_GROUP          5
#define SHAPE_TYPE_ROOT           7
#define SHAPE_TYPE_SPECIAL_GROUP 10

/* Bounded so a corrupt shape cannot spin forever. Real maps are far smaller. */
#define PM3DS_MAX_NODES 2048

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void wr_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Relocation state, file-static to keep every function's frame small. */
static uint8_t *g_blob;
static uint32_t g_dsize;
static uintptr_t g_host;          /* host address of blob[0] */
static uint32_t g_nodes, g_models, g_groups, g_dls, g_props, g_bad;
/* Full-width offsets: a u16 store truncated node offsets while dsize may reach
 * 0x40000, so two nodes exactly 0x10000 apart would alias and the second
 * subtree would silently stay big-endian. No shipped map collides (verified
 * over all 489 PAL shapes), but the wide store costs 4 KB of BSS and removes
 * the class. */
static uint32_t g_seen[PM3DS_MAX_NODES];   /* offsets already relocated */
static uint32_t g_seen_n;

/* Is this a plausible pointer into the original N64 blob? */
static int ptr_ok(uint32_t p, uint32_t *off_out) {
    if ((p >> 24) != 0x80) return 0;
    uint32_t off = p - PM3DS_SHAPE_N64_BASE;
    if (off >= g_dsize) return 0;
    *off_out = off;
    return 1;
}

/* Read a big-endian pointer field at `off`, rebase it to a host pointer, and
 * write it back little-endian. Returns 1 and the original blob offset if it was
 * a real in-blob pointer; 0 if it was null/out-of-range (left untouched). */
static int reloc_ptr(uint32_t off, uint32_t *target_off) {
    if (off + 4u > g_dsize) return 0;
    uint32_t raw = rd_be32(g_blob + off);
    uint32_t t;
    if (!ptr_ok(raw, &t)) return 0;
    wr_le32(g_blob + off, (uint32_t)(g_host + t));
    if (target_off) *target_off = t;
    return 1;
}

/* Byte-swap a big-endian scalar in place (no rebasing). */
static void swap_scalar(uint32_t off) {
    if (off + 4u > g_dsize) return;
    wr_le32(g_blob + off, rd_be32(g_blob + off));
}

static int already_seen(uint32_t off) {
    for (uint32_t i = 0; i < g_seen_n; i++) {
        if (g_seen[i] == off) return 1;
    }
    if (g_seen_n < PM3DS_MAX_NODES) g_seen[g_seen_n++] = off;
    return 0;
}

#ifdef PM3DS_RELOC_DLS
/* Display lists reached so far, so a DL shared by several models is only
 * relocated once (relocating twice would rebase already-host pointers). */
/* M12: u32 offsets (shapes reach 0x40000; u16 aliased lists past 64 KB). */
static uint32_t g_dl_seen[1024];
static uint32_t g_dl_seen_n;
static uint32_t g_dl_cmds;
static uint32_t g_vtx_swapped_n;

static int dl_already_seen(uint32_t off) {
    for (uint32_t i = 0; i < g_dl_seen_n; i++) {
        if (g_dl_seen[i] == off) return 1;
    }
    if (g_dl_seen_n < 1024u) g_dl_seen[g_dl_seen_n++] = off;
    return 0;
}

/* M12: Vtx data is big-endian too. Each Vtx is 16 bytes: s16 ob[3], u16 flag,
 * s16 tc[2] (six halfwords to swap) then u8 cn[4]. Several G_VTX commands
 * share vertex runs, so remember which 8-byte-aligned starts are done. */
static uint8_t g_vtx_done[0x40000 / 8 / 8];

static void swap_vtx_range(uint32_t off, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = off + i * 16u;
        if (v + 16u > g_dsize) return;
        uint32_t bit = v >> 3;
        if (g_vtx_done[bit >> 3] & (1u << (bit & 7))) continue;
        g_vtx_done[bit >> 3] |= (uint8_t)(1u << (bit & 7));
        for (uint32_t h = 0; h < 12u; h += 2) {
            uint8_t t = g_blob[v + h];
            g_blob[v + h] = g_blob[v + h + 1];
            g_blob[v + h + 1] = t;
        }
        g_vtx_swapped_n++;
    }
}

/*
 * Relocate an embedded F3DEX2 display list in place.
 *
 * Each Gfx is two BIG-ENDIAN words. Both need swapping, and for the commands
 * that carry an address, w1 must additionally be rebased into host memory.
 * Without this the model tree is traversable but every draw references N64
 * addresses, which is why geometry could not render even with the node tree
 * relocated.
 *
 * Address-carrying F3DEX2 opcodes: G_VTX(0x01), G_BRANCH_Z(0x04),
 * G_MOVEMEM(0xDC), G_MOVEWORD(0xDB, segment bases), G_DL(0xDE),
 * G_SETTIMG(0xFD), G_SETZIMG(0xFE), G_SETCIMG(0xFF). Everything else is
 * byte-swapped only. Terminates on G_ENDDL(0xDF).
 */
static void reloc_dl(uint32_t off, int depth) {
    if (depth > 8 || off + 8u > g_dsize) return;
    if (dl_already_seen(off)) return;

    for (uint32_t n = 0; n < 8192u; n++) {
        uint32_t p = off + n * 8u;
        if (p + 8u > g_dsize) return;

        uint32_t w0 = rd_be32(g_blob + p);
        uint32_t w1 = rd_be32(g_blob + p + 4);
        uint8_t  op = (uint8_t)(w0 >> 24);

        wr_le32(g_blob + p, w0);

        uint32_t t;
        int carries_addr = (op == 0x01 || op == 0x04 || op == 0xDC || op == 0xDB ||
                            op == 0xDE || op == 0xFD || op == 0xFE || op == 0xFF);
        if (carries_addr && ptr_ok(w1, &t)) {
            wr_le32(g_blob + p + 4, (uint32_t)(g_host + t));
            if (op == 0xDE) reloc_dl(t, depth + 1);   /* nested/branched list */
            if (op == 0x01) swap_vtx_range(t, (w0 >> 12) & 0xFFu);
        } else {
            wr_le32(g_blob + p + 4, w1);
        }
        g_dl_cmds++;

        if (op == 0xDF) return;                        /* G_ENDDL */
    }
}
#endif /* PM3DS_RELOC_DLS */

/* Relocate one ModelNode (0x14) and everything it owns.
 *   0x00 type  0x04 displayData*  0x08 numProperties  0x0C propertyList*  0x10 groupData*
 * Iterative over children via explicit recursion on a bounded depth: the tree is
 * shallow, and the node cap plus the seen-list stop cycles. */
/* M12: an N64 Mtx is 16 s16 integer halves then 16 u16 fraction halves,
 * big-endian. The port's Mtx (gu_impl.c, read by the translator and by
 * guMtxL2F) is 16 row-major native int32 s15.16 values. Same 64 bytes, so
 * convert in place. Matrices can be shared between groups: convert once. */
static uint32_t g_mtx_done[64];
static uint32_t g_mtx_done_n;

static void convert_mtx(uint32_t off) {
    if (off + 64u > g_dsize || (off & 3u)) return;
    for (uint32_t i = 0; i < g_mtx_done_n; i++) if (g_mtx_done[i] == off) return;
    if (g_mtx_done_n < 64u) g_mtx_done[g_mtx_done_n++] = off;
    int32_t out[16];
    for (int k = 0; k < 16; k++) {
        const uint8_t *ip = g_blob + off + (uint32_t)k * 2u;
        const uint8_t *fp = g_blob + off + 32u + (uint32_t)k * 2u;
        uint32_t ip16 = ((uint32_t)ip[0] << 8) | ip[1];
        uint32_t fp16 = ((uint32_t)fp[0] << 8) | fp[1];
        out[k] = (int32_t)((ip16 << 16) | fp16);
    }
    for (int k = 0; k < 16; k++) wr_le32(g_blob + off + (uint32_t)k * 4u, (uint32_t)out[k]);
}

static void reloc_node(uint32_t off, int depth) {
    if (depth > 32 || g_nodes >= PM3DS_MAX_NODES) return;
    if (off + 0x14u > g_dsize) { g_bad++; return; }
    if (already_seen(off)) return;
    g_nodes++;

    uint32_t type = rd_be32(g_blob + off + 0x00);
    swap_scalar(off + 0x00);

    if (type == SHAPE_TYPE_MODEL) g_models++;
    else if (type == SHAPE_TYPE_GROUP || type == SHAPE_TYPE_ROOT ||
             type == SHAPE_TYPE_SPECIAL_GROUP) g_groups++;
    else { g_bad++; return; }        /* not a node: stop before writing garbage */

    /* displayData -> ModelDisplayData { Gfx* displayList; pad }. We rebase the
     * displayList pointer so the struct is consistent, but the Gfx[] CONTENTS
     * are not relocated (see header). */
    uint32_t dd_off;
    if (reloc_ptr(off + 0x04, &dd_off)) {
        if (dd_off + 8u <= g_dsize) {
            uint32_t dl_off;
            if (reloc_ptr(dd_off + 0x00, &dl_off)) {
                g_dls++;
#ifdef PM3DS_RELOC_DLS
                reloc_dl(dl_off, 0);
#endif
            }
        }
    }

    uint32_t nprops = rd_be32(g_blob + off + 0x08);
    swap_scalar(off + 0x08);
    uint32_t pl_off;
    if (reloc_ptr(off + 0x0C, &pl_off) && nprops <= 512u) {
        /* ModelNodeProperty is 0xC: key, dataType, data-union. All three words
         * are big-endian; the union additionally holds a POINTER for some
         * properties (TEXTURE_NAME feeds strcmp in load_texture_by_name). If
         * the data word decodes to an in-blob pointer, rebase it; otherwise it
         * is a scalar (int/float/bounding-box word) and gets a plain swap.
         * See the header comment for why scalars cannot false-match. */
        for (uint32_t i = 0; i < nprops; i++) {
            uint32_t p = pl_off + i * 0xCu;
            if (p + 0xCu > g_dsize) { g_bad++; break; }
            swap_scalar(p + 0x00);
            swap_scalar(p + 0x04);
            if (!reloc_ptr(p + 0x08, NULL)) swap_scalar(p + 0x08);
            g_props++;
        }
    }

    /* groupData -> ModelGroupData 0x14:
     *   0x00 transformMatrix* 0x04 lightingGroup* 0x08 numLights
     *   0x0C numChildren     0x10 childList** */
    uint32_t gd_off;
    if (!reloc_ptr(off + 0x10, &gd_off)) return;
    if (gd_off + 0x14u > g_dsize) { g_bad++; return; }

    uint32_t mtx_off;
    if (reloc_ptr(gd_off + 0x00, &mtx_off)) convert_mtx(mtx_off);   /* transformMatrix */
    reloc_ptr(gd_off + 0x04, NULL);     /* lightingGroup   */
    swap_scalar(gd_off + 0x08);         /* numLights       */

    uint32_t nchild = rd_be32(g_blob + gd_off + 0x0C);
    swap_scalar(gd_off + 0x0C);
    uint32_t cl_off;
    if (!reloc_ptr(gd_off + 0x10, &cl_off)) return;
    if (nchild > 512u) { g_bad++; return; }

    for (uint32_t c = 0; c < nchild; c++) {
        uint32_t e = cl_off + c * 4u;
        if (e + 4u > g_dsize) { g_bad++; break; }
        uint32_t child_off;
        uint32_t raw = rd_be32(g_blob + e);
        if (!ptr_ok(raw, &child_off)) { g_bad++; continue; }
        wr_le32(g_blob + e, (uint32_t)(g_host + child_off));
        reloc_node(child_off, depth + 1);
    }
}

/* Relocate a char* array (modelNames / colliderNames / zoneNames). Entries run
 * until one stops looking like an in-blob pointer, or until `limit_off` (the
 * next array's start; 0 = no limit). The three arrays sit back-to-back in
 * authored data, so without the limit the first walk runs through all three
 * and the later walks then report 0 ("m/c/z=256/0/0") — functionally fine but
 * useless for forensics. The name STRINGS that follow can never false-match:
 * their first byte is ASCII (< 0x80), so rd_be32 never yields an 0x80 top byte. */
static uint32_t reloc_name_array(uint32_t arr_off, uint32_t limit_off) {
    uint32_t n = 0;
    for (; n < 512u; n++) {
        uint32_t e = arr_off + n * 4u;
        if (e + 4u > g_dsize) break;
        if (limit_off != 0u && e >= limit_off) break;
        uint32_t raw = rd_be32(g_blob + e);
        uint32_t t;
        if (!ptr_ok(raw, &t)) break;
        wr_le32(g_blob + e, (uint32_t)(g_host + t));
    }
    return n;
}

/*
 * Entry point. `blob` is &gMapShapeData, `dsize` the Yay0 decompressed size.
 * Relocates in place. Safe to call once per decode; calling twice would double-
 * relocate, so the caller must not.
 */
void pm3ds_shape_relocate(void *blob, uint32_t dsize) {
    g_blob = (uint8_t *)blob;
    g_dsize = dsize;
    g_host = (uintptr_t)blob;
    g_nodes = g_models = g_groups = g_dls = g_props = g_bad = 0;
    g_seen_n = 0;
#ifdef PM3DS_RELOC_DLS
    g_dl_seen_n = 0; g_dl_cmds = 0; g_vtx_swapped_n = 0;
    for (uint32_t i = 0; i < sizeof(g_vtx_done); i++) g_vtx_done[i] = 0;
#endif
    g_mtx_done_n = 0;

    if (dsize < 0x20u || dsize > 0x40000u) {
        pm3ds_log(PM3DS_LOG_WARN, "shape-reloc: implausible dsize 0x%X — skipped",
                  (unsigned)dsize);
        return;
    }

    /* ShapeFileHeader 0x20: root, vertexTable, modelNames, colliderNames, zoneNames */
    uint32_t root_off = 0;
    int have_root = reloc_ptr(0x00, &root_off);
    reloc_ptr(0x04, NULL);                       /* vertexTable */

    uint32_t names_off = 0, colliders_off = 0, zones_off = 0;
    int have_m = reloc_ptr(0x08, &names_off);
    int have_c = reloc_ptr(0x0C, &colliders_off);
    int have_z = reloc_ptr(0x10, &zones_off);
    uint32_t n_models = 0, n_coll = 0, n_zones = 0;
    if (have_m) n_models = reloc_name_array(names_off,
                    (have_c && colliders_off > names_off) ? colliders_off : 0u);
    if (have_c) n_coll   = reloc_name_array(colliders_off,
                    (have_z && zones_off > colliders_off) ? zones_off : 0u);
    if (have_z) n_zones  = reloc_name_array(zones_off, 0u);

    if (have_root) reloc_node(root_off, 0);

    pm3ds_log(PM3DS_LOG_INFO,
              "shape-reloc: nodes=%u (model=%u group=%u) props=%u dls=%u bad=%u "
              "names m/c/z=%u/%u/%u",
              (unsigned)g_nodes, (unsigned)g_models, (unsigned)g_groups,
              (unsigned)g_props, (unsigned)g_dls, (unsigned)g_bad,
              (unsigned)n_models, (unsigned)n_coll, (unsigned)n_zones);
#ifdef PM3DS_RELOC_DLS
    pm3ds_log(PM3DS_LOG_INFO,
              "shape-reloc: display lists RELOCATED — %u unique lists, %u Gfx commands "
              "byte-swapped and rebased; %u vertices, %u matrices converted",
              (unsigned)g_dl_seen_n, (unsigned)g_dl_cmds, (unsigned)g_vtx_swapped_n,
              (unsigned)g_mtx_done_n);
#else
    if (g_dls) {
        pm3ds_log(PM3DS_LOG_WARN,
                  "shape-reloc: %u display lists rebased but their Gfx CONTENTS are "
                  "NOT relocated — geometry will not draw correctly yet",
                  (unsigned)g_dls);
    }
#endif
}
