#include "common.h"
#include "model.h"

typedef struct HitFile {
    /* 0x00 */ u32 collisionOffset;
    /* 0x04 */ u32 zoneOffset;
} HitFile; // size = 0x08

typedef struct ColliderBackupEntry {
    /* 0x00 */ s32 flags;
    /* 0x04 */ s16 parentModelIndex;
    /* 0x06 */ char pad_06[2];
} ColliderBackupEntry; // size = 0x08

typedef struct HitFileHeader {
    /* 0x00 */ s16 numColliders;
    /* 0x02 */ char pad_02[2];
    /* 0x04 */ s32 collidersOffset;
    /* 0x08 */ s16 numVertices;
    /* 0x0A */ char pad_0A[2];
    /* 0x0C */ s32 verticesOffset;
    /* 0x10 */ s16 boundingBoxesDataSize;
    /* 0x12 */ char pad_12[2];
    /* 0x14 */ s32 boundingBoxesOffset;
} HitFileHeader; // size = 0x18

typedef struct HitAssetCollider {
    /* 0x00 */ s16 boundingBoxOffset;
    /* 0x02 */ s16 nextSibling;
    /* 0x04 */ s16 firstChild;
    /* 0x06 */ s16 numTriangles;
    /* 0x08 */ s32 trianglesOffset;
} HitAssetCollider; // size = 0x0C

CollisionData gCollisionData;
CollisionData gZoneCollisionData;

BSS f32 gCollisionRayStartX;
BSS f32 gCollisionRayStartY;
BSS f32 gCollisionRayStartZ;
BSS f32 gCollisionRayDirX;
BSS f32 gCollisionRayDirY;
BSS f32 gCollisionRayDirZ;
BSS f32 gCollisionPointX;
BSS f32 gCollisionPointY;
BSS f32 gCollisionPointZ;
BSS f32 gCollisionRayLength;
BSS f32 gCollisionNormalX;
BSS f32 gCollisionNormalY;
BSS f32 gCollisionNormalZ;
BSS ColliderBackupEntry* gCollisionDataBackup;
BSS ColliderBackupEntry* gCollisionDataZoneBackup;

extern Vec3s gEntityColliderFaces[];
extern Vec3f gEntityColliderNormals[];

s32 collision_heap_create(void);
void* collision_heap_malloc(s32 size);
void collision_heap_free(void*);

void load_hit_data(s32 idx, HitFile* hit);
void _add_hit_vert_to_buffer(Vec3f** buf, Vec3f* vert, s32* bufSize);
s32 _get_hit_vert_index_from_buffer(Vec3f** buffer, Vec3f* vert, s32* bufferSize);

#if defined(_3DS) && !defined(PM3DS_COLLIDER_CHECK)
/* 3DS port: a broadphase for test_ray_colliders. Every ray used to read each
 * collider and follow its AABB pointer: two cache misses per collider, a few
 * hundred colliders, dozens of rays a step (player and NPCs), ~3 ms a step on
 * an Old 3DS. A coarse XZ grid lists the colliders whose box touches each
 * cell; a short ray tests only the colliders of its cells, in the original
 * order, so the result is the same. Colliders that move (parented to a model:
 * update_collider_transform rewrites their box) and very large ones are tested
 * by every ray. Rebuilt lazily after a collision load or when a collider first
 * moves. -DPM3DS_COLGRID_VERIFY checks every ray against the full scan. */
#include <stdlib.h>
#include <string.h>
#define COLGRID 1
#define CG_MAX_DIM 64       /* cells per axis */
#define CG_MAX_SPAN 1024    /* a collider over more cells is always tested */
#define CG_MAX_QUERY 16     /* a ray over more cells scans everything */
static struct {
    Collider* list;
    s32 num;
    u32 builtGen;
    s32 valid;
    f32 x0, z0, inv;
    s32 nx, nz;
    u32* start;             /* nx * nz + 1 */
    u16* items;
    u16* always;
    s32 numAlways;
    u32* bits;              /* candidate set, one bit per collider */
    u16* cand;              /* the same in ascending order */
    u8* moving;             /* per collider: parented to a model */
    s32 cap;
} sColGrid;
static u32 sColGridGen = 1;

static void colgrid_reset(void) {
    sColGridGen++;
    if (sColGrid.moving != nullptr) {
        memset(sColGrid.moving, 0, sColGrid.cap);
    }
}

static void colgrid_reserve(s32 num) {
    if (num <= sColGrid.cap) {
        return;
    }
    free(sColGrid.moving);
    free(sColGrid.bits);
    free(sColGrid.cand);
    sColGrid.cap = (num + 63) & ~63;
    sColGrid.moving = calloc(sColGrid.cap, 1);
    sColGrid.bits = malloc(sColGrid.cap / 8);
    sColGrid.cand = malloc(sColGrid.cap * sizeof(u16));
    if (sColGrid.moving == nullptr || sColGrid.bits == nullptr || sColGrid.cand == nullptr) {
        free(sColGrid.moving);
        free(sColGrid.bits);
        free(sColGrid.cand);
        sColGrid.moving = nullptr;
        sColGrid.bits = nullptr;
        sColGrid.cand = nullptr;
        sColGrid.cap = 0;
    }
}

static void colgrid_mark_moving(s32 id) {
    colgrid_reserve(gCollisionData.numColliders);
    if (id >= 0 && id < sColGrid.cap && !sColGrid.moving[id]) {
        sColGrid.moving[id] = true;
        sColGridGen++;
    }
}

static s32 colgrid_cell(f32 v, f32 origin, s32 dim) {
    s32 c;
    if (!(v > origin)) {
        return 0;
    }
    c = (s32)((v - origin) * sColGrid.inv);
    return c >= dim ? dim - 1 : c;
}

static void colgrid_build(void) {
    CollisionData* cd = &gCollisionData;
    s32 num = cd->numColliders;
    f32 minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    s32 i, x, z, total, cells;
    u32* fill;

    sColGrid.list = cd->colliderList;
    sColGrid.num = num;
    sColGrid.builtGen = sColGridGen;
    sColGrid.valid = false;
    free(sColGrid.start);
    free(sColGrid.items);
    free(sColGrid.always);
    sColGrid.start = nullptr;
    sColGrid.items = nullptr;
    sColGrid.always = nullptr;
    if (num <= 0 || cd->colliderList == nullptr) {
        return;
    }
    colgrid_reserve(num);
    if (sColGrid.cap < num) {
        return;
    }
    for (i = 0; i < num; i++) {
        Collider* c = &cd->colliderList[i];
        if (c->numTriangles == 0 || c->aabb == nullptr || sColGrid.moving[i]) {
            continue;
        }
        if (c->aabb->min.x < minX) minX = c->aabb->min.x;
        if (c->aabb->max.x > maxX) maxX = c->aabb->max.x;
        if (c->aabb->min.z < minZ) minZ = c->aabb->min.z;
        if (c->aabb->max.z > maxZ) maxZ = c->aabb->max.z;
    }
    if (!(maxX - minX >= 0.0f && maxX - minX < 1e7f && maxZ - minZ >= 0.0f && maxZ - minZ < 1e7f)) {
        return;
    }
    {
        f32 extent = maxX - minX > maxZ - minZ ? maxX - minX : maxZ - minZ;
        f32 cell = extent / CG_MAX_DIM;
        if (cell < 16.0f) {
            cell = 16.0f;
        }
        sColGrid.inv = 1.0f / cell;
    }
    sColGrid.x0 = minX;
    sColGrid.z0 = minZ;
    sColGrid.nx = (s32)((maxX - minX) * sColGrid.inv) + 1;
    sColGrid.nz = (s32)((maxZ - minZ) * sColGrid.inv) + 1;
    if (sColGrid.nx > CG_MAX_DIM) sColGrid.nx = CG_MAX_DIM;
    if (sColGrid.nz > CG_MAX_DIM) sColGrid.nz = CG_MAX_DIM;
    cells = sColGrid.nx * sColGrid.nz;
    sColGrid.start = calloc(cells + 1, sizeof(u32));
    sColGrid.always = malloc(num * sizeof(u16));
    if (sColGrid.start == nullptr || sColGrid.always == nullptr) {
        return;
    }

    // count, then fill (two passes over the same cell ranges)
    sColGrid.numAlways = 0;
    total = 0;
    for (i = 0; i < num; i++) {
        Collider* c = &cd->colliderList[i];
        s32 x0, x1, z0, z1;
        if (c->numTriangles == 0) {
            continue;
        }
        if (c->aabb == nullptr || sColGrid.moving[i]) {
            sColGrid.always[sColGrid.numAlways++] = i;
            continue;
        }
        x0 = colgrid_cell(c->aabb->min.x, sColGrid.x0, sColGrid.nx);
        x1 = colgrid_cell(c->aabb->max.x, sColGrid.x0, sColGrid.nx);
        z0 = colgrid_cell(c->aabb->min.z, sColGrid.z0, sColGrid.nz);
        z1 = colgrid_cell(c->aabb->max.z, sColGrid.z0, sColGrid.nz);
        if ((x1 - x0 + 1) * (z1 - z0 + 1) > CG_MAX_SPAN) {
            sColGrid.always[sColGrid.numAlways++] = i;
            continue;
        }
        for (z = z0; z <= z1; z++) {
            for (x = x0; x <= x1; x++) {
                sColGrid.start[z * sColGrid.nx + x + 1]++;
                total++;
            }
        }
    }
    for (i = 0; i < cells; i++) {
        sColGrid.start[i + 1] += sColGrid.start[i];
    }
    sColGrid.items = malloc((total > 0 ? total : 1) * sizeof(u16));
    fill = malloc(cells * sizeof(u32));
    if (sColGrid.items == nullptr || fill == nullptr) {
        free(fill);
        return;
    }
    memcpy(fill, sColGrid.start, cells * sizeof(u32));
    for (i = 0; i < num; i++) {
        Collider* c = &cd->colliderList[i];
        s32 x0, x1, z0, z1;
        if (c->numTriangles == 0 || c->aabb == nullptr || sColGrid.moving[i]) {
            continue;
        }
        x0 = colgrid_cell(c->aabb->min.x, sColGrid.x0, sColGrid.nx);
        x1 = colgrid_cell(c->aabb->max.x, sColGrid.x0, sColGrid.nx);
        z0 = colgrid_cell(c->aabb->min.z, sColGrid.z0, sColGrid.nz);
        z1 = colgrid_cell(c->aabb->max.z, sColGrid.z0, sColGrid.nz);
        if ((x1 - x0 + 1) * (z1 - z0 + 1) > CG_MAX_SPAN) {
            continue;
        }
        for (z = z0; z <= z1; z++) {
            for (x = x0; x <= x1; x++) {
                sColGrid.items[fill[z * sColGrid.nx + x]++] = i;
            }
        }
    }
    free(fill);
    sColGrid.valid = true;
}

/* The colliders a ray with this XZ box can touch, ascending in sColGrid.cand;
 * returns how many, or -1: test them all. */
static s32 colgrid_candidates(f32 min_x, f32 max_x, f32 min_z, f32 max_z) {
    s32 x0, x1, z0, z1, x, z, words, n;
    if (sColGrid.builtGen != sColGridGen || sColGrid.list != gCollisionData.colliderList ||
        sColGrid.num != gCollisionData.numColliders) {
        colgrid_build();
    }
    if (!sColGrid.valid) {
        return -1;
    }
    // NaN fails every compare here (and passes the original AABB test)
    if (!(min_x >= -1e30f && max_x <= 1e30f && min_z >= -1e30f && max_z <= 1e30f)) {
        return -1;
    }
    x0 = colgrid_cell(min_x, sColGrid.x0, sColGrid.nx);
    x1 = colgrid_cell(max_x, sColGrid.x0, sColGrid.nx);
    z0 = colgrid_cell(min_z, sColGrid.z0, sColGrid.nz);
    z1 = colgrid_cell(max_z, sColGrid.z0, sColGrid.nz);
    if ((x1 - x0 + 1) * (z1 - z0 + 1) > CG_MAX_QUERY) {
        return -1;
    }
    words = (sColGrid.num + 31) >> 5;
    memset(sColGrid.bits, 0, words * sizeof(u32));
    for (z = z0; z <= z1; z++) {
        for (x = x0; x <= x1; x++) {
            s32 cellIdx = z * sColGrid.nx + x;
            u32 k;
            for (k = sColGrid.start[cellIdx]; k < sColGrid.start[cellIdx + 1]; k++) {
                u32 id = sColGrid.items[k];
                sColGrid.bits[id >> 5] |= 1u << (id & 31);
            }
        }
    }
    for (x = 0; x < sColGrid.numAlways; x++) {
        u32 id = sColGrid.always[x];
        sColGrid.bits[id >> 5] |= 1u << (id & 31);
    }
    n = 0;
    for (x = 0; x < words; x++) {
        u32 m = sColGrid.bits[x];
        while (m != 0) {
            sColGrid.cand[n++] = (x << 5) + __builtin_ctz(m);
            m &= m - 1;
        }
    }
    return n;
}
#endif

void backup_map_collision_data(void) {
    CollisionData* collisionData;
    Collider* collider;
    ColliderBackupEntry* backupEntry;
    s32 i;

    collisionData = &gCollisionData;
    gCollisionDataBackup = general_heap_malloc(collisionData->numColliders * sizeof(ColliderBackupEntry));
    for (i = 0, backupEntry = gCollisionDataBackup; i < collisionData->numColliders; i++, backupEntry++) {
        collider = &collisionData->colliderList[i];
        backupEntry->flags = collider->flags;
        backupEntry->parentModelIndex = collider->parentModelIndex;
    }

    collisionData = &gZoneCollisionData;
    gCollisionDataZoneBackup = general_heap_malloc(collisionData->numColliders * sizeof(ColliderBackupEntry));
    for (i = 0, backupEntry = gCollisionDataZoneBackup; i < collisionData->numColliders; i++, backupEntry++) {
        collider = &collisionData->colliderList[i];
        backupEntry->flags = collider->flags;
        backupEntry->parentModelIndex = collider->parentModelIndex;
    }

    gZoneCollisionData.numColliders = 0;
}

void func_8005AF84(void) {
}

void func_8005AF8C(void) {
}

void initialize_collision(void) {
#ifdef COLGRID
    colgrid_reset();
#endif
    gCollisionData.numColliders = 0;
    gZoneCollisionData.numColliders = 0;
    collision_heap_create();
}

#ifdef _3DS
/* 3DS port (M12): hit files are raw big-endian. Swap one decoded file in
 * place, field by field, so load_hit_data below runs unmodified:
 *   HitFile           u32 collisionOffset, u32 zoneOffset
 *   HitFileHeader     s16,pad,s32, s16,pad,s32, s16,pad,s32
 *   bounding boxes    u32 words (Vec3f min/max + u32 flags)
 *   vertices          Vec3s
 *   HitAssetCollider  s16 x4, s32 trianglesOffset
 *   triangles         packed s32 per triangle                          */
static void hit_swap16(u8* p) { u8 t = p[0]; p[0] = p[1]; p[1] = t; }
static void hit_swap32(u8* p) { u8 t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }

static void hit_swap_section(u8* base, u32 size, u32 off) {
    HitFileHeader* h;
    HitAssetCollider* c;
    s32 i, j;

    if (off == 0 || off + sizeof(HitFileHeader) > size) {
        return;
    }
    h = (HitFileHeader*)(base + off);
    hit_swap16((u8*)&h->numColliders);
    hit_swap32((u8*)&h->collidersOffset);
    hit_swap16((u8*)&h->numVertices);
    hit_swap32((u8*)&h->verticesOffset);
    hit_swap16((u8*)&h->boundingBoxesDataSize);
    hit_swap32((u8*)&h->boundingBoxesOffset);

    if ((u32)h->boundingBoxesOffset + (u32)h->boundingBoxesDataSize * 4 <= size) {
        for (i = 0; i < h->boundingBoxesDataSize; i++) {
            hit_swap32(base + h->boundingBoxesOffset + i * 4);
        }
    }
    if ((u32)h->verticesOffset + (u32)h->numVertices * 6 <= size) {
        for (i = 0; i < h->numVertices * 3; i++) {
            hit_swap16(base + h->verticesOffset + i * 2);
        }
    }
    if ((u32)h->collidersOffset + (u32)h->numColliders * sizeof(HitAssetCollider) > size) {
        return;
    }
    for (i = 0, c = (HitAssetCollider*)(base + h->collidersOffset); i < h->numColliders; i++, c++) {
        hit_swap16((u8*)&c->boundingBoxOffset);
        hit_swap16((u8*)&c->nextSibling);
        hit_swap16((u8*)&c->firstChild);
        hit_swap16((u8*)&c->numTriangles);
        hit_swap32((u8*)&c->trianglesOffset);
        if ((u32)c->trianglesOffset + (u32)c->numTriangles * 4 <= size) {
            for (j = 0; j < c->numTriangles; j++) {
                hit_swap32(base + c->trianglesOffset + j * 4);
            }
        }
    }
}

static void hit_fix_endian(HitFile* hit, u32 size) {
    hit_swap32((u8*)&hit->collisionOffset);
    hit_swap32((u8*)&hit->zoneOffset);
    hit_swap_section((u8*)hit, size, hit->collisionOffset);
    if (hit->zoneOffset != hit->collisionOffset) {
        hit_swap_section((u8*)hit, size, hit->zoneOffset);
    }
}
#endif

#ifdef PM3DS_COLLIDER_CHECK
/* debug: first moment any world collider triangle points outside the heap */
void pm3ds_validate_collision(const char* where) {
    extern HeapNode heap_collisionHead;
    extern void pm3ds_log(int, const char*, ...);
    static s32 reported;
    u32 ch = (u32)&heap_collisionHead;
    if (reported || gCollisionData.colliderList == nullptr) {
        return;
    }
    for (s32 i = 0; i < gCollisionData.numColliders; i++) {
        Collider* c = &gCollisionData.colliderList[i];
        ColliderTriangle* t = c->triangleTable;
        for (s32 k = 0; k < c->numTriangles; k++, t++) {
            u32 a = (u32)t->v1, b = (u32)t->v2, d = (u32)t->v3;
            if (a - ch >= COLLISION_HEAP_SIZE || b - ch >= COLLISION_HEAP_SIZE || d - ch >= COLLISION_HEAP_SIZE) {
                reported = 1;
                pm3ds_log(3, "COLVALID bad at '%s': collider %d tri %d &tri=%p v=%08X %08X %08X",
                          where, i, k, (void*)t, a, b, d);
                return;
            }
        }
    }
}
#endif

void load_map_hit_asset(void) {
    u32 assetSize;
    MapSettings* map = get_current_map_settings();
    void* compressedData = load_asset_by_name(wMapHitName, &assetSize);
    HitFile* uncompressedData = heap_malloc(assetSize);

    decode_yay0(compressedData, uncompressedData);
    general_heap_free(compressedData);
#ifdef _3DS
    hit_fix_endian(uncompressedData, assetSize);
#endif

    map->hitAssetCollisionOffset = uncompressedData->collisionOffset;
    map->hitAssetZoneOffset = uncompressedData->zoneOffset;

    load_hit_data(0, uncompressedData); // Colliders
#ifdef PM3DS_COLLIDER_CHECK
    pm3ds_validate_collision("after load colliders");
#endif
    load_hit_data(1, uncompressedData); // Zones
#ifdef PM3DS_COLLIDER_CHECK
    pm3ds_validate_collision("after load zones");
#endif

    heap_free(uncompressedData);
}

void restore_map_collision_data(void) {
    CollisionData* collisionData;
    Collider* collider;
    ColliderBackupEntry* backupEntry;
    s32 i;

    load_map_hit_asset();

    collisionData = &gCollisionData;
    for (i = 0, backupEntry = gCollisionDataBackup; i < collisionData->numColliders; i++, backupEntry++) {
        collider = &collisionData->colliderList[i];
        collider->flags = backupEntry->flags;
        collider->parentModelIndex = backupEntry->parentModelIndex;

        if (collider->flags != -1 && collider->flags & COLLIDER_FLAG_HAS_MODEL_PARENT) {
            parent_collider_to_model(i, collider->parentModelIndex);
            update_collider_transform(i);
        }
    }

    collisionData = &gZoneCollisionData;
    for (i = 0, backupEntry = gCollisionDataZoneBackup; i < collisionData->numColliders; i++, backupEntry++) {
        collider = &collisionData->colliderList[i];
        collider->flags = backupEntry->flags;
        collider->parentModelIndex = backupEntry->parentModelIndex;
    }

    general_heap_free(gCollisionDataBackup);
    general_heap_free(gCollisionDataZoneBackup);
}

void load_battle_hit_asset(const char* hitName) {
#ifdef COLGRID
    colgrid_reset();
#endif
    if (hitName == nullptr) {
        gCollisionData.numColliders = 0;
    } else {
        u32 assetSize;
        MapSettings* map = get_current_map_settings();
        void* compressedData = load_asset_by_name(hitName, &assetSize);
        HitFile* uncompressedData = heap_malloc(assetSize);

        decode_yay0(compressedData, uncompressedData);
        general_heap_free(compressedData);
#ifdef _3DS
        hit_fix_endian(uncompressedData, assetSize);
#endif

        map->hitAssetCollisionOffset = uncompressedData->collisionOffset;

        load_hit_data(0, uncompressedData);

        heap_free(uncompressedData);
    }
}

void load_hit_data(s32 idx, HitFile* hit) {
    s32 collisionOffset;
    MapSettings* map;
    CollisionData* collisionData;
    HitFileHeader* assetCollisionData;
    Vec3f* vertices;
    Vec3s* assetVertices;
    u32* boundingBox;
    u32* assetBoundingBox;
    Collider* collider;
    HitAssetCollider* assetCollider;
    ColliderTriangle* triangle;
    s32* trianglePacked;
    s16 numTriangles;
    s32 i,j;
    f32 e13_y, e21_z, e13_z, e21_y, e21_x, e13_x, normalX, normalY, normalZ, coeff;


    assetCollisionData = nullptr;
    collisionData = nullptr;

    map = get_current_map_settings();

    switch (idx) {
        case 0: // Colliders
            collisionOffset = map->hitAssetCollisionOffset;
            if (collisionOffset == 0) {
                return;
            }

            assetCollisionData = (HitFileHeader*)((void*)hit + collisionOffset);
            collisionData = &gCollisionData;
#ifdef COLGRID
            colgrid_reset();
#endif
            break;
        case 1: // Zones
            collisionOffset = map->hitAssetZoneOffset;
            if (collisionOffset == 0) {
                return;
            }

            assetCollisionData = (HitFileHeader*)((void*)hit + collisionOffset);
            collisionData = &gZoneCollisionData;
            break;
    }

    assetBoundingBox = (u32*)((void*)hit + assetCollisionData->boundingBoxesOffset);
    collisionData->aabbs = collision_heap_malloc(assetCollisionData->boundingBoxesDataSize * 4);
    for (i = 0, boundingBox = (u32*)(collisionData->aabbs); i < assetCollisionData->boundingBoxesDataSize;
        assetBoundingBox++, boundingBox++, i++) {
        *boundingBox = *assetBoundingBox;
    }

    assetVertices = (Vec3s*)((void*)hit + assetCollisionData->verticesOffset);
    collisionData->vertices = collision_heap_malloc(assetCollisionData->numVertices * sizeof(Vec3f));
    for (i = 0, vertices = collisionData->vertices; i < assetCollisionData->numVertices;
        vertices++, assetVertices++, i++) {
        vertices->x = assetVertices->x;
        vertices->y = assetVertices->y;
        vertices->z = assetVertices->z;
    }

    assetCollider = (HitAssetCollider*)((void*)hit + assetCollisionData->collidersOffset);
    collider = collisionData->colliderList = collision_heap_malloc(assetCollisionData->numColliders * sizeof(Collider));
    collisionData->numColliders = assetCollisionData->numColliders;
    for (i = 0; i < assetCollisionData->numColliders; assetCollider++, collider++, i++) {
        collider->flags = 0;
        collider->nextSibling = assetCollider->nextSibling;
        collider->firstChild = assetCollider->firstChild;
        collider->numTriangles = assetCollider->numTriangles;

        numTriangles = collider->numTriangles;

        if (numTriangles) {
            collider->triangleTable = triangle = collision_heap_malloc(assetCollider->numTriangles * sizeof(ColliderTriangle));

            if (assetCollider->boundingBoxOffset < 0) {
                collider->aabb = nullptr;
            } else {
                collider->aabb = (ColliderBoundingBox*)((u32*)(collisionData->aabbs) + assetCollider->boundingBoxOffset);

                if (idx == 0) {
                    collider->aabb->min.x -= 1;
                    collider->aabb->min.y -= 1;
                    collider->aabb->min.z -= 1;
                    collider->aabb->max.x += 1;
                    collider->aabb->max.y += 1;
                    collider->aabb->max.z += 1;
                    collider->flags = collider->aabb->flagsForCollider;
                }
            }

            trianglePacked = (s32*)((void*)hit + assetCollider->trianglesOffset);

            for (j = 0; j < assetCollider->numTriangles; trianglePacked++, triangle++, j++) {
                Vec3f* v1 = triangle->v1 = &collisionData->vertices[(*trianglePacked) & 0x3FF];
                Vec3f* v2 = triangle->v2 = &collisionData->vertices[(*trianglePacked >> 10) & 0x3FF];
                Vec3f* v3 = triangle->v3 = &collisionData->vertices[(*trianglePacked >> 20) & 0x3FF];
                triangle->oneSided = (*trianglePacked >> 30) & 1;

                triangle->e13.x = v3->x - v1->x;
                triangle->e13.y = v3->y - v1->y;
                triangle->e13.z = v3->z - v1->z;
                triangle->e21.x = v1->x - v2->x;
                triangle->e21.y = v1->y - v2->y;
                triangle->e21.z = v1->z - v2->z;
                triangle->e32.x = v2->x - v3->x;
                triangle->e32.y = v2->y - v3->y;
                triangle->e32.z = v2->z - v3->z;

                e13_x = triangle->e13.x;
                e13_y = triangle->e13.y;
                e13_z = triangle->e13.z;
                e21_x = triangle->e21.x;
                e21_y = triangle->e21.y;
                e21_z = triangle->e21.z;

                // cross product
                normalX = e13_y * e21_z - e13_z * e21_y;
                normalY = e13_z * e21_x - e13_x * e21_z;
                normalZ = e13_x * e21_y - e13_y * e21_x;
                coeff = SQ(normalX) + SQ(normalY) + SQ(normalZ);

                if (coeff != 0) {
                    coeff = 1.0f / sqrtf(coeff);
                } else {
                    coeff = 0.0f;
                }

                triangle->normal.x = normalX * coeff;
                triangle->normal.y = normalY * coeff;
                triangle->normal.z = normalZ * coeff;
            }
        }
    }
}

void parent_collider_to_model(s16 colliderID, s16 modelIndex) {
#ifdef PM3DS_PORT_ROM_ADDRS
    /* 3DS port: collision load deferred (M10.3) — see load_hit_data. */
    if (gCollisionData.colliderList == NULL) {
        return;
    }
#endif
    Collider* collider;
    ColliderTriangle* triangle;
    s32 i;
    Vec3f** vertexBuffer;
    Vec3f** vertexPtr;
    s32 vertexBufferSize;
    Vec3f* vertexTable;
    Vec3f* vertex;

#ifdef PM3DS_COLLIDER_CHECK
    {
        extern void pm3ds_log(int, const char*, ...);
        Collider* c = &gCollisionData.colliderList[colliderID];
        pm3ds_log(2, "PARENTCOL collider %d of %d -> model %d tris=%d tt=%p",
                  colliderID, gCollisionData.numColliders, modelIndex,
                  colliderID < gCollisionData.numColliders ? c->numTriangles : -1,
                  colliderID < gCollisionData.numColliders ? (void*)c->triangleTable : nullptr);
    }
#endif
    collider = &gCollisionData.colliderList[colliderID];
    collider->parentModelIndex = modelIndex;
    collider->flags |= COLLIDER_FLAG_HAS_MODEL_PARENT;
#ifdef COLGRID
    colgrid_mark_moving(colliderID);
#endif

    vertexBuffer = collision_heap_malloc(collider->numTriangles * sizeof(Vec3f));
    vertexBufferSize = 0;
    vertexPtr = vertexBuffer;

    for (i = 0, triangle = collider->triangleTable; i < collider->numTriangles; i++, triangle++) {
        _add_hit_vert_to_buffer(vertexBuffer, triangle->v1, &vertexBufferSize);
        _add_hit_vert_to_buffer(vertexBuffer, triangle->v2, &vertexBufferSize);
        _add_hit_vert_to_buffer(vertexBuffer, triangle->v3, &vertexBufferSize);
    }

    collider->numVertices = vertexBufferSize;
    collider->vertexTable = collision_heap_malloc(vertexBufferSize * 2 * sizeof(Vec3f));
    for (i = 0, vertexTable = collider->vertexTable; i < vertexBufferSize; vertexPtr++, vertexTable += 2, i++) {
        vertex = *vertexPtr;
        vertexTable[0].x = vertexTable[1].x = vertex->x;
        vertexTable[0].y = vertexTable[1].y = vertex->y;
        vertexTable[0].z = vertexTable[1].z = vertex->z;
    }

    vertexTable = collider->vertexTable;
    for (i = 0, triangle = collider->triangleTable; i < collider->numTriangles; triangle++, i++) {
        triangle->v1 = &vertexTable[_get_hit_vert_index_from_buffer(vertexBuffer, triangle->v1, &vertexBufferSize) * 2];
        triangle->v2 = &vertexTable[_get_hit_vert_index_from_buffer(vertexBuffer, triangle->v2, &vertexBufferSize) * 2];
        triangle->v3 = &vertexTable[_get_hit_vert_index_from_buffer(vertexBuffer, triangle->v3, &vertexBufferSize) * 2];
    }

    collision_heap_free(vertexBuffer);
#ifdef PM3DS_COLLIDER_CHECK
    pm3ds_validate_collision("after parent_collider_to_model");
#endif
}

void _add_hit_vert_to_buffer(Vec3f** buf, Vec3f* vert, s32* bufSize) {
    s32 i;

    for (i = 0; i < *bufSize; i++) {
        if (buf[i] == vert) {
            break;
        }
    }

    if (i == *bufSize) {
        buf[i] = vert;
        (*bufSize)++;
    }
}

s32 _get_hit_vert_index_from_buffer(Vec3f** buffer, Vec3f* vert, s32* bufferSize) {
    s32 i;

    for (i = 0; i < *bufferSize; i++) {
        if (*buffer == vert) {
            break;
        }
        buffer++;
    }

    return i;
}

void update_collider_transform(s16 colliderID) {
#ifdef PM3DS_PORT_ROM_ADDRS
    if (gCollisionData.colliderList == NULL) {
        return;
    }
#endif
    Collider* collider;
    struct Model* model;
    Matrix4f matrix;
    s32 i;
    Vec3f* vertexTable;
    f32 min_x, min_y, min_z, max_x, max_y, max_z;
    ColliderTriangle* triangle;
    f32 e13_y, e21_z, e13_z, e21_y, e21_x, e13_x, normalX, normalY, normalZ, coeff;

    collider = &gCollisionData.colliderList[colliderID];
    model = get_model_from_list_index(collider->parentModelIndex);
#ifdef COLGRID
    colgrid_mark_moving(colliderID);
#endif

    if (model->bakedMtx == nullptr) {
        copy_matrix(model->userTransformMtx, matrix);
    } else {
        guMtxL2F(matrix, (Mtx*)model->bakedMtx);
        guMtxCatF(model->userTransformMtx, matrix, matrix);
    }

    triangle = collider->triangleTable;
    vertexTable = collider->vertexTable;

    min_x = min_y = min_z = 999999.9f;
    max_x = max_y = max_z = -999999.9f;

    for (i = 0; i < collider->numVertices; vertexTable += 2, i++) {
        guMtxXFMF(matrix, vertexTable[1].x, vertexTable[1].y, vertexTable[1].z, &vertexTable[0].x, &vertexTable[0].y, &vertexTable[0].z);

        if (vertexTable[0].x < min_x)
            min_x = vertexTable[0].x;
        if (vertexTable[0].x > max_x)
            max_x = vertexTable[0].x;
        if (vertexTable[0].y < min_y)
            min_y = vertexTable[0].y;
        if (vertexTable[0].y > max_y)
            max_y = vertexTable[0].y;
        if (vertexTable[0].z < min_z)
            min_z = vertexTable[0].z;
        if (vertexTable[0].z > max_z)
            max_z = vertexTable[0].z;
    }

    collider->aabb->min.x = min_x;
    collider->aabb->min.y = min_y;
    collider->aabb->min.z = min_z;
    collider->aabb->max.x = max_x;
    collider->aabb->max.y = max_y;
    collider->aabb->max.z = max_z;

    for (i = 0; i < collider->numTriangles; triangle++, i++) {
        Vec3f* v1 = triangle->v1;
        Vec3f* v2 = triangle->v2;
        Vec3f* v3 = triangle->v3;

        triangle->e13.x = v3->x - v1->x;
        triangle->e13.y = v3->y - v1->y;
        triangle->e13.z = v3->z - v1->z;
        triangle->e21.x = v1->x - v2->x;
        triangle->e21.y = v1->y - v2->y;
        triangle->e21.z = v1->z - v2->z;
        triangle->e32.x = v2->x - v3->x;
        triangle->e32.y = v2->y - v3->y;
        triangle->e32.z = v2->z - v3->z;

        e13_x = triangle->e13.x;
        e13_y = triangle->e13.y;
        e13_z = triangle->e13.z;
        e21_x = triangle->e21.x;
        e21_y = triangle->e21.y;
        e21_z = triangle->e21.z;

        // vector product
        normalX = e13_y * e21_z - e13_z * e21_y;
        normalY = e13_z * e21_x - e13_x * e21_z;
        normalZ = e13_x * e21_y - e13_y * e21_x;
        coeff  = SQ(normalX) + SQ(normalY) + SQ(normalZ);

        if (coeff != 0) {
            coeff = 1.0f / sqrtf(coeff);
        } else {
            coeff = 0.0f;
        }

        triangle->normal.x = normalX * coeff;
        triangle->normal.y = normalY * coeff;
        triangle->normal.z = normalZ * coeff;
    }
}

s32 get_collider_flags(s32 colliderID) {
    if (colliderID & COLLISION_WITH_ENTITY_BIT) {
        return 0;
    } else {
        return gCollisionData.colliderList[colliderID].flags;
    }
}

void get_flat_collider_normal(s32 colliderID, f32* x, f32* y, f32* z) {
    ColliderTriangle* triangle = &gCollisionData.colliderList[colliderID].triangleTable[0];

    *x = triangle->normal.x;
    *y = triangle->normal.y;
    *z = triangle->normal.z;
}

void get_collider_center(s32 colliderID, f32* x, f32* y, f32* z) {
#ifdef PM3DS_PORT_ROM_ADDRS
    if (gCollisionData.colliderList == NULL) {
        *x = *y = *z = 0.0f;
        return;
    }
#endif
    ColliderBoundingBox* aabb = gCollisionData.colliderList[colliderID].aabb;

    *x = (aabb->min.x + aabb->max.x) * 0.5f;
    *y = (aabb->min.y + aabb->max.y) * 0.5f;
    *z = (aabb->min.z + aabb->max.z) * 0.5f;
}

s32 test_ray_triangle_general(ColliderTriangle* triangle, Vec3f* vertices) {
    f32 distToTrianglePlane;
    f32 cosAngle;
    Vec3f* v1;
    Vec3f* v2;
    Vec3f* v3;

    if (triangle->normal.x == 0 &&
        triangle->normal.y == 0 &&
        triangle->normal.z == 0)
        return false;

    v1 = triangle->v1;
    v2 = triangle->v2;
    v3 = triangle->v3;

    distToTrianglePlane = triangle->normal.x * (gCollisionRayStartX - v1->x) +
                          triangle->normal.y * (gCollisionRayStartY - v1->y) +
                          triangle->normal.z * (gCollisionRayStartZ - v1->z);

    if (triangle->oneSided) {
        if (distToTrianglePlane < 0) {
            return false;
        }

        if (triangle->normal.x * gCollisionRayDirX + triangle->normal.y * gCollisionRayDirY + triangle->normal.z * gCollisionRayDirZ >= 0) {
            return false;
        }

        if ((gCollisionRayStartX - v1->x) * (triangle->e13.z * gCollisionRayDirY - triangle->e13.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v1->y) * (triangle->e13.x * gCollisionRayDirZ - triangle->e13.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v1->z) * (triangle->e13.y * gCollisionRayDirX - triangle->e13.x * gCollisionRayDirY) < 0)
        {
            return false;
        }

        if ((gCollisionRayStartX - v2->x) * (triangle->e21.z * gCollisionRayDirY - triangle->e21.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v2->y) * (triangle->e21.x * gCollisionRayDirZ - triangle->e21.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v2->z) * (triangle->e21.y * gCollisionRayDirX - triangle->e21.x * gCollisionRayDirY) < 0)
        {
            return false;
        }

        if ((gCollisionRayStartX - v3->x) * (triangle->e32.z * gCollisionRayDirY - triangle->e32.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v3->y) * (triangle->e32.x * gCollisionRayDirZ - triangle->e32.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v3->z) * (triangle->e32.y * gCollisionRayDirX - triangle->e32.x * gCollisionRayDirY) < 0)
        {
            return false;
        }
    } else {
        if ((triangle->normal.x * gCollisionRayDirX + triangle->normal.y * gCollisionRayDirY + triangle->normal.z * gCollisionRayDirZ) * distToTrianglePlane >= 0) {
            return false;
        }

        if (((gCollisionRayStartX - v1->x) * (triangle->e13.z * gCollisionRayDirY - triangle->e13.y * gCollisionRayDirZ) +
             (gCollisionRayStartY - v1->y) * (triangle->e13.x * gCollisionRayDirZ - triangle->e13.z * gCollisionRayDirX) +
             (gCollisionRayStartZ - v1->z) * (triangle->e13.y * gCollisionRayDirX - triangle->e13.x * gCollisionRayDirY)
            ) * distToTrianglePlane < 0)
        {
            return false;
        }

        if (((gCollisionRayStartX - v2->x) * (triangle->e21.z * gCollisionRayDirY - triangle->e21.y * gCollisionRayDirZ) +
             (gCollisionRayStartY - v2->y) * (triangle->e21.x * gCollisionRayDirZ - triangle->e21.z * gCollisionRayDirX) +
             (gCollisionRayStartZ - v2->z) * (triangle->e21.y * gCollisionRayDirX - triangle->e21.x * gCollisionRayDirY)
            ) * distToTrianglePlane < 0)
        {
            return false;
        }

        if (((gCollisionRayStartX - v3->x) * (triangle->e32.z * gCollisionRayDirY - triangle->e32.y * gCollisionRayDirZ) +
             (gCollisionRayStartY - v3->y) * (triangle->e32.x * gCollisionRayDirZ - triangle->e32.z * gCollisionRayDirX) +
             (gCollisionRayStartZ - v3->z) * (triangle->e32.y * gCollisionRayDirX - triangle->e32.x * gCollisionRayDirY)
            ) * distToTrianglePlane < 0)
        {
            return false;
        }
    }

    cosAngle = triangle->normal.x * gCollisionRayDirX + triangle->normal.y * gCollisionRayDirY + triangle->normal.z * gCollisionRayDirZ;
    if (gCollisionRayLength >= 0 && gCollisionRayLength <= -distToTrianglePlane / cosAngle) {
        return false;
    }

    gCollisionRayLength = -distToTrianglePlane  / cosAngle;

    gCollisionPointX = gCollisionRayStartX + gCollisionRayDirX * gCollisionRayLength;
    gCollisionPointY = gCollisionRayStartY + gCollisionRayDirY * gCollisionRayLength;
    gCollisionPointZ = gCollisionRayStartZ + gCollisionRayDirZ * gCollisionRayLength;

    gCollisionNormalX = triangle->normal.x;
    gCollisionNormalY = triangle->normal.y;
    gCollisionNormalZ = triangle->normal.z;

    return true;
}

s32 test_ray_triangle_down(ColliderTriangle* triangle, Vec3f* vertices) {
    f32 distToTrianglePlane, cosAngle;
    Vec3f* v1;
    Vec3f* v2;
    Vec3f* v3;

    if (triangle->normal.x == 0 && triangle->normal.y == 0 && triangle->normal.z == 0) {
        return false;
    }

    v1 = triangle->v1;
    v2 = triangle->v2;
    v3 = triangle->v3;

    distToTrianglePlane = triangle->normal.x * (gCollisionRayStartX - v1->x) +
                          triangle->normal.y * (gCollisionRayStartY - v1->y) +
                          triangle->normal.z * (gCollisionRayStartZ - v1->z);

    if (triangle->oneSided) {
        if (distToTrianglePlane < 0) {
            return false;
        }

        if (triangle->normal.y <= 0)
            return false;

        if ((gCollisionRayStartZ - v1->z) * triangle->e13.x - (gCollisionRayStartX - v1->x) * triangle->e13.z < 0) {
            return false;
        }

        if ((gCollisionRayStartZ - v2->z) * triangle->e21.x - (gCollisionRayStartX - v2->x) * triangle->e21.z < 0) {
            return false;
        }

        if ((gCollisionRayStartZ - v3->z) * triangle->e32.x - (gCollisionRayStartX - v3->x) * triangle->e32.z < 0) {
            return false;
        }
    } else {
        if (triangle->normal.y * distToTrianglePlane <= 0) {
            return false;
        }

        if (((gCollisionRayStartZ - v1->z) * triangle->e13.x - (gCollisionRayStartX - v1->x) * triangle->e13.z) * distToTrianglePlane < 0) {
            return false;
        }

        if (((gCollisionRayStartZ - v2->z) * triangle->e21.x - (gCollisionRayStartX - v2->x) * triangle->e21.z) * distToTrianglePlane < 0) {
            return false;
        }

        if (((gCollisionRayStartZ - v3->z) * triangle->e32.x - (gCollisionRayStartX - v3->x) * triangle->e32.z) * distToTrianglePlane < 0) {
            return false;
        }
    }

    cosAngle = -triangle->normal.y;
    if (gCollisionRayLength >= 0 && gCollisionRayLength <= -distToTrianglePlane / cosAngle) {
        return false;
    }

    gCollisionRayLength = -distToTrianglePlane  / cosAngle;

    gCollisionPointX = gCollisionRayStartX;
    gCollisionPointY = gCollisionRayStartY - gCollisionRayLength;
    gCollisionPointZ = gCollisionRayStartZ;

    gCollisionNormalX = triangle->normal.x;
    gCollisionNormalY = triangle->normal.y;
    gCollisionNormalZ = triangle->normal.z;

    return true;
}

s32 test_ray_triangle_horizontal(ColliderTriangle* triangle, Vec3f *vertices) {
    f32 distToTrianglePlane, cosAngle;
    Vec3f* v1;
    Vec3f* v2;
    Vec3f* v3;

    if (triangle->normal.x == 0 && triangle->normal.y == 0 && triangle->normal.z == 0) {
        return false;
    }

    v1 = triangle->v1;
    v2 = triangle->v2;
    v3 = triangle->v3;

    distToTrianglePlane = triangle->normal.x * (gCollisionRayStartX - v1->x) +
                          triangle->normal.y * (gCollisionRayStartY - v1->y) +
                          triangle->normal.z * (gCollisionRayStartZ - v1->z);

    if (triangle->oneSided) {
        if (distToTrianglePlane < 0) {
            return false;
        }

        if (triangle->normal.x * gCollisionRayDirX + triangle->normal.z * gCollisionRayDirZ >= 0) {
            return false;
        }

        if ((gCollisionRayStartX - v1->x) * (-triangle->e13.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v1->y) * (triangle->e13.x * gCollisionRayDirZ - triangle->e13.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v1->z) * (triangle->e13.y * gCollisionRayDirX) < 0)
        {
            return false;
        }

        if ((gCollisionRayStartX - v2->x) * (-triangle->e21.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v2->y) * (triangle->e21.x * gCollisionRayDirZ - triangle->e21.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v2->z) * (triangle->e21.y * gCollisionRayDirX) < 0)
        {
            return false;
        }

        if ((gCollisionRayStartX - v3->x) * (-triangle->e32.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v3->y) * (triangle->e32.x * gCollisionRayDirZ - triangle->e32.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v3->z) * (triangle->e32.y * gCollisionRayDirX) < 0)
        {
            return false;
        }
    } else {
        if ((triangle->normal.x * gCollisionRayDirX + triangle->normal.z * gCollisionRayDirZ) * distToTrianglePlane >= 0)
        {
            return false;
        }

        if (((gCollisionRayStartX - v1->x) * (-triangle->e13.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v1->y) * (triangle->e13.x * gCollisionRayDirZ - triangle->e13.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v1->z) * (triangle->e13.y * gCollisionRayDirX)) * distToTrianglePlane < 0)
        {
            return false;
        }

        if (((gCollisionRayStartX - v2->x) * (-triangle->e21.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v2->y) * (triangle->e21.x * gCollisionRayDirZ - triangle->e21.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v2->z) * (triangle->e21.y * gCollisionRayDirX)) * distToTrianglePlane < 0)
        {
            return false;
        }

        if (((gCollisionRayStartX - v3->x) * (-triangle->e32.y * gCollisionRayDirZ) +
            (gCollisionRayStartY - v3->y) * (triangle->e32.x * gCollisionRayDirZ - triangle->e32.z * gCollisionRayDirX) +
            (gCollisionRayStartZ - v3->z) * (triangle->e32.y * gCollisionRayDirX)) * distToTrianglePlane < 0)
        {
            return false;
        }
    }

    cosAngle = triangle->normal.x * gCollisionRayDirX + triangle->normal.z * gCollisionRayDirZ;
    if (gCollisionRayLength >= 0 && gCollisionRayLength <= -distToTrianglePlane / cosAngle) {
        return false;
    }

    gCollisionRayLength = -distToTrianglePlane  / cosAngle;

    gCollisionPointX = gCollisionRayStartX + gCollisionRayDirX * gCollisionRayLength;
    gCollisionPointY = gCollisionRayStartY;
    gCollisionPointZ = gCollisionRayStartZ + gCollisionRayDirZ * gCollisionRayLength;

    gCollisionNormalX = triangle->normal.x;
    gCollisionNormalY = triangle->normal.y;
    gCollisionNormalZ = triangle->normal.z;

    return true;
}

s32 test_ray_colliders(s32 ignoreFlags, f32 startX, f32 startY, f32 startZ, f32 dirX, f32 dirY, f32 dirZ,
                       f32* hitX, f32* hitY, f32* hitZ, f32* hitDepth, f32* hitNx, f32* hitNy, f32* hitNz) {
    Collider* collider;
    CollisionData* collisionData;
    ColliderTriangle* triangle;
    s32 i, j;
    s32 colliderID;
    f32 min_x, min_y, min_z, max_x, max_y, max_z;

    if (dirX == 0 && dirY == 0 && dirZ == 0) {
        return 0;
    }

    collisionData = &gCollisionData;
    gCollisionRayDirX = dirX;
    gCollisionRayDirY = dirY;
    gCollisionRayDirZ = dirZ;
    gCollisionRayStartX = startX;
    gCollisionRayStartY = startY;
    gCollisionRayStartZ = startZ;
    gCollisionRayLength = *hitDepth;
    colliderID = NO_COLLIDER;

    if (dirX < 0) {
        min_x = startX + dirX * gCollisionRayLength;
        max_x = startX;
    } else {
        min_x = startX;
        max_x = startX + dirX * gCollisionRayLength;
    }

    if (dirY < 0) {
        min_y = startY + dirY * gCollisionRayLength;
        max_y = startY;
    } else {
        min_y = startY;
        max_y = startY + dirY * gCollisionRayLength;
    }

    if (dirZ < 0) {
        min_z = startZ + dirZ * gCollisionRayLength;
        max_z = startZ;
    } else {
        min_z = startZ;
        max_z = startZ + dirZ * gCollisionRayLength;
    }

#ifdef COLGRID
    s32 numCand = colgrid_candidates(min_x, max_x, min_z, max_z), ci;
    const u16* order = numCand < 0 ? nullptr : sColGrid.cand;
#ifdef PM3DS_COLGRID_VERIFY
    if (order != nullptr) {   // every collider the full scan would test must be a candidate
        static s32 rays, bad;
        rays++;
        for (i = 0; i < collisionData->numColliders; i++) {
            collider = &collisionData->colliderList[i];
            if ((collider->flags & ignoreFlags) || collider->numTriangles == 0 ||
                max_x < collider->aabb->min.x || min_x > collider->aabb->max.x ||
                max_z < collider->aabb->min.z || min_z > collider->aabb->max.z ||
                max_y < collider->aabb->min.y || min_y > collider->aabb->max.y) {
                continue;
            }
            if (!(sColGrid.bits[i >> 5] & (1u << (i & 31))) && bad++ < 8) {
                extern void pm3ds_log(int, const char*, ...);
                pm3ds_log(3, "COLGRID miss: collider %d of %d, ray x %d..%d z %d..%d", i, collisionData->numColliders,
                          (s32)min_x, (s32)max_x, (s32)min_z, (s32)max_z);
            }
        }
        if ((rays & 4095) == 0) {
            extern void pm3ds_log(int, const char*, ...);
            s32 nmov = 0;
            for (i = 0; i < collisionData->numColliders; i++) nmov += sColGrid.moving[i];
            pm3ds_log(2, "COLGRID rays=%d misses=%d colliders=%d candidates=%d always=%d moving=%d grid=%dx%d", rays, bad,
                      collisionData->numColliders, numCand, sColGrid.numAlways, nmov, sColGrid.nx, sColGrid.nz);
        }
    }
#endif
    if (order == nullptr) {
        numCand = collisionData->numColliders;
    }
    for (ci = 0; ci < numCand; ci++) {
        i = order != nullptr ? order[ci] : ci;
#else
    for (i = 0; i < collisionData->numColliders; i++) {
#endif
        collider = &collisionData->colliderList[i];

#ifdef PM3DS_COLLIDER_CHECK
        if (collider->numTriangles != 0) {
            u32 a = (u32)collider->aabb;
            extern HeapNode heap_collisionHead;
            u32 ch = (u32)&heap_collisionHead, bh = (u32)&heap_battleHead;
            if (!((a >= ch && a < ch + COLLISION_HEAP_SIZE) || (a >= bh && a < bh + BATTLE_HEAP_SIZE))) {
                extern void pm3ds_log(int, const char*, ...);
                static s32 n;
                if (n++ < 6) {
                    pm3ds_log(3, "COLCHECK ray: %s collider %d/%d aabb=%08X tri=%d list=%p ra=%p",
                              collisionData == &gCollisionData ? "world" : collisionData == &gZoneCollisionData ? "zone" : "other",
                              i, collisionData->numColliders, a, collider->numTriangles,
                              (void*)collisionData->colliderList, __builtin_return_address(0));
                }
                continue;
            }
            {
                ColliderTriangle* t = collider->triangleTable;
                for (s32 k = 0; k < collider->numTriangles; k++, t++) {
                    u32 p1 = (u32)t->v1, p3 = (u32)t->v3;
                    if (p1 < ch || p1 >= ch + COLLISION_HEAP_SIZE || p3 < ch || p3 >= ch + COLLISION_HEAP_SIZE) {
                        extern void pm3ds_log(int, const char*, ...);
                        static s32 nt;
                        if (nt++ < 4) {
                            pm3ds_log(3, "COLCHECK tri: collider %d tri %d/%d v1=%08X v3=%08X tt=%p flags=%08X vtab=%p",
                                      i, k, collider->numTriangles, p1, p3, (void*)collider->triangleTable,
                                      collider->flags, (void*)collider->vertexTable);
                        }
                        break;
                    }
                }
            }
        }
#endif
        if ((collider->flags & ignoreFlags) ||
            collider->numTriangles == 0     ||
            max_x < collider->aabb->min.x   ||
            min_x > collider->aabb->max.x   ||
            max_z < collider->aabb->min.z   ||
            min_z > collider->aabb->max.z   ||
            max_y < collider->aabb->min.y   ||
            min_y > collider->aabb->max.y)
        {
            continue;
        }

        triangle = collider->triangleTable;
        if (gCollisionRayDirX == 0 && gCollisionRayDirZ == 0 && gCollisionRayDirY == -1.0) {
            for (j = 0; j < collider->numTriangles; j++) {
                if (test_ray_triangle_down(triangle++, collisionData->vertices)) {
                    colliderID = i;
                }
            }
        } else if (gCollisionRayDirY == 0) {
            for (j = 0; j < collider->numTriangles; j++) {
                if (test_ray_triangle_horizontal(triangle++, collisionData->vertices)) {
                    colliderID = i;
                }
            }
        } else {
            for (j = 0; j < collider->numTriangles; j++) {
                if (test_ray_triangle_general(triangle++, collisionData->vertices)) {
                    colliderID = i;
                }
            }
        }
    }

    if (colliderID > NO_COLLIDER) {
        *hitX = gCollisionPointX;
        *hitY = gCollisionPointY;
        *hitZ = gCollisionPointZ;
        *hitDepth = gCollisionRayLength;
        *hitNx = gCollisionNormalX;
        *hitNy = gCollisionNormalY;
        *hitNz = gCollisionNormalZ;
        return colliderID;
    } else {
        return colliderID;
    }
}

s32 test_ray_zones(f32 startX, f32 startY, f32 startZ, f32 dirX, f32 dirY, f32 dirZ,
                f32* hitX, f32* hitY, f32* hitZ, f32* hitDepth, f32* hitNx, f32* hitNy, f32* hitNz) {
    Collider* collider;
    CollisionData* collisionData;
    ColliderTriangle* triangle;
    s32 i, j;
    s32 colliderID;

    collisionData = &gZoneCollisionData;
    gCollisionRayDirX = dirX;
    gCollisionRayDirY = dirY;
    gCollisionRayDirZ = dirZ;
    gCollisionRayStartX = startX;
    gCollisionRayStartY = startY;
    gCollisionRayStartZ = startZ;
    gCollisionRayLength = *hitDepth;
    colliderID = NO_COLLIDER;

    for (i = 0; i < collisionData->numColliders; i++) {
        collider = &collisionData->colliderList[i];

        if (collider->flags & COLLIDER_FLAG_IGNORE_PLAYER)
            continue;

        if (collider->numTriangles == 0 || collider->aabb == nullptr)
            continue;

        triangle = collider->triangleTable;
        for (j = 0; j < collider->numTriangles; j++) {
            if (test_ray_triangle_down(triangle++, collisionData->vertices)) {
                colliderID = i;
            }
        }
    }

    if (colliderID > NO_COLLIDER) {
        *hitX = gCollisionPointX;
        *hitY = gCollisionPointY;
        *hitZ = gCollisionPointZ;
        *hitDepth = gCollisionRayLength;
        *hitNx = gCollisionNormalX;
        *hitNy = gCollisionNormalY;
        *hitNz = gCollisionNormalZ;
        return colliderID;
    } else {
        return colliderID;
    }
}

f32 test_ray_collider_horizontal(s32 ignoreFlags, s32 colliderID, f32 x, f32 y, f32 z, f32 length, f32 yaw) {
    CollisionData* collisionData = &gCollisionData;
    f32 cosTheta;
    f32 sinTheta;
    Collider* collider;
    ColliderTriangle* triangleTable;
    s32 i;
    f32 ret;

    sin_cos_rad(DEG_TO_RAD(yaw), &sinTheta, &cosTheta);
    collider = &collisionData->colliderList[colliderID];

    gCollisionRayDirY = 0;
    gCollisionRayStartX = x;
    gCollisionRayStartY = y;
    gCollisionRayStartZ = z;
    gCollisionRayLength = length;
    gCollisionRayDirX = sinTheta;
    gCollisionRayDirZ = -cosTheta;
    ret = -1.0f;

    if (!(collider->flags & ignoreFlags) && collider->numTriangles != 0) {
        triangleTable = collider->triangleTable;

        for (i = 0; i < collider->numTriangles; i++)
            if (test_ray_triangle_horizontal(triangleTable++, collisionData->vertices))
                ret = gCollisionRayLength;
    }

    return ret;
}

s32 test_ray_entities(f32 startX, f32 startY, f32 startZ, f32 dirX, f32 dirY, f32 dirZ,
                      f32* hitX, f32* hitY, f32* hitZ, f32* hitDepth, f32* hitNx, f32* hitNy, f32* hitNz) {
    f32 hitDepthDown, hitDepthHoriz;
    s32 type;
    s32 i, j;
    Entity* entity;
    Matrix4f tempMatrix1;
    Matrix4f tempMatrix2;
    Vec3f boxVertices[8];
    ColliderTriangle entityTriangle;
    s32 entityIndex;
    f32 h;
    f32 aabbX, aabbZ;
    s32 hasCollision;
    f32 dist, dist2;
    ColliderTriangle *triangle = &entityTriangle;

    enum {
        ENTITY_TEST_ANY     = 0,
        ENTITY_TEST_DOWN    = 1,
        ENTITY_TEST_LATERAL = 2,
    };

    entityIndex = -1;
    type = ENTITY_TEST_ANY;
    hitDepthDown = hitDepthHoriz = *hitDepth;

    if (dirX == 0 && dirZ == 0 && dirY < 0) {
        hitDepthHoriz = 0;
        type = ENTITY_TEST_DOWN;
    } else if (dirY == 0) {
        hitDepthDown = 0;
        type = ENTITY_TEST_LATERAL;
    }

    gCollisionRayLength = -1;
    triangle->oneSided = true;
    for (i = 0; i < MAX_ENTITIES; i++) {
        entity = get_entity_by_index(i);

        if (entity == nullptr || (entity->flags & (ENTITY_FLAG_SKIP_UPDATE | ENTITY_FLAG_DISABLE_COLLISION))) {
            continue;
        }

        dist = hitDepthHoriz + entity->effectiveSize;
        if (startX > entity->pos.x + dist || startX < entity->pos.x - dist) {
            continue;
        }

        if (startZ > entity->pos.z + dist || startZ < entity->pos.z - dist) {
            continue;
        }

        switch (type) {
            case ENTITY_TEST_ANY:
            case ENTITY_TEST_DOWN:
                dist = entity->pos.y;
                dist2 = hitDepthDown + entity->effectiveSize * 2;
                if (dist + dist2 < startY || startY < dist - dist2) {
                    continue;
                }
                break;
            case ENTITY_TEST_LATERAL:
                dist = entity->pos.y;
                dist2 = entity->effectiveSize * 2;
                if (dist + dist2 < startY || startY < dist - dist2) {
                    continue;
                }
                break;
        }

        aabbX = entity->aabb.x / 2;
        aabbZ = entity->aabb.z / 2;

        boxVertices[1].x = boxVertices[2].x = boxVertices[5].x = boxVertices[6].x = -aabbX;
        boxVertices[0].x = boxVertices[3].x = boxVertices[4].x = boxVertices[7].x = aabbX;
        boxVertices[0].y = boxVertices[1].y = boxVertices[2].y = boxVertices[3].y = 0;
        boxVertices[4].y = boxVertices[5].y = boxVertices[6].y = boxVertices[7].y = entity->aabb.y;
        boxVertices[0].z = boxVertices[1].z = boxVertices[4].z = boxVertices[5].z = aabbZ;
        boxVertices[2].z = boxVertices[3].z = boxVertices[6].z = boxVertices[7].z = -aabbZ;

        guMtxXFMF(entity->inverseTransformMatrix, dirX, dirY, dirZ, &gCollisionRayDirX, &gCollisionRayDirY, &gCollisionRayDirZ);
        guMtxXFMF(entity->inverseTransformMatrix, startX - entity->pos.x, startY - entity->pos.y,
                  startZ - entity->pos.z, &gCollisionRayStartX, &gCollisionRayStartY, &gCollisionRayStartZ);

        for (j = 0; j < 12; j++) {
            Vec3f* v1 = triangle->v1 = &boxVertices[gEntityColliderFaces[j].x];
            Vec3f* v2 = triangle->v2 = &boxVertices[gEntityColliderFaces[j].y];
            Vec3f* v3 = triangle->v3 = &boxVertices[gEntityColliderFaces[j].z];
            triangle->e13.x = v3->x - v1->x;
            triangle->e13.y = v3->y - v1->y;
            triangle->e13.z = v3->z - v1->z;
            triangle->e21.x = v1->x - v2->x;
            triangle->e21.y = v1->y - v2->y;
            triangle->e21.z = v1->z - v2->z;
            triangle->e32.x = v2->x - v3->x;
            triangle->e32.y = v2->y - v3->y;
            triangle->e32.z = v2->z - v3->z;
            triangle->normal.x = gEntityColliderNormals[j].x;
            triangle->normal.y = gEntityColliderNormals[j].y;
            triangle->normal.z = gEntityColliderNormals[j].z;

            if (hasCollision = test_ray_triangle_general(&entityTriangle, boxVertices)) {
                break;
            }
        }

        if (hasCollision && gCollisionRayLength < *hitDepth) {
            entityIndex = i;
            *hitDepth = gCollisionRayLength;

            switch (type) {
                case ENTITY_TEST_ANY:
                    hitDepthDown = gCollisionRayLength;
                    hitDepthHoriz = gCollisionRayLength;
                    break;
                case ENTITY_TEST_DOWN:
                    hitDepthDown = gCollisionRayLength;
                    break;
                case ENTITY_TEST_LATERAL:
                    hitDepthHoriz = gCollisionRayLength;
                    break;
            }

            guRotateF(tempMatrix1, entity->rot.x, 1.0f, 0.0f, 0.0f);
            guRotateF(tempMatrix2, entity->rot.z, 0.0f, 0.0f, 1.0f);
            guMtxCatF(tempMatrix1, tempMatrix2, tempMatrix1);
            guRotateF(tempMatrix2, entity->rot.y, 0.0f, 1.0f, 0.0f);
            guMtxCatF(tempMatrix1, tempMatrix2, tempMatrix1);
            guTranslateF(tempMatrix2, entity->pos.x, entity->pos.y, entity->pos.z);
            guMtxCatF(tempMatrix1, tempMatrix2, tempMatrix1);
            guMtxXFMF(tempMatrix1, gCollisionPointX, gCollisionPointY, gCollisionPointZ, hitX, hitY, hitZ);

            h = 1.0f / sqrtf(SQ(gCollisionNormalX) + SQ(gCollisionNormalY) + SQ(gCollisionNormalZ));
            *hitNx = gCollisionNormalX * h;
            *hitNy = gCollisionNormalY * h;
            *hitNz = gCollisionNormalZ * h;
        }
    }

    return entityIndex;
}
