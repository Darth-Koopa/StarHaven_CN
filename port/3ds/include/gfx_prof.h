/*
 * gfx_prof.h — renderer stage timers for profiling builds (-DPM3DS_STEP_PROF).
 * gfx_present.c logs the totals once a second as a GPROF line; in other
 * builds the macros are empty.
 */
#ifndef GFX_PROF_H
#define GFX_PROF_H

#include <stdint.h>

enum { GP_VTX, GP_TRI, GP_STATE, GP_TEX, GP_WALK, GP_BOTTOM, GP_FRAMEEND, GP_COMB, GP_FLUSH, GP_RENDER, GP_COUNT };

/* sampling profiler: where the main thread is (gfx_present.c samples it) */
enum { PH_WALK, PH_STATE, PH_DRAW, PH_TEX, PH_GAME, PH_WAIT, PH_BOTTOM, PH_END, PH_PRESENT,
       PH_COMB, PH_CTX, PH_FX,   /* combiner setup, citro3d context flush per draw, blend/depth/fog/scissor */
       PH_PUT,                   /* a triangle's GPU vertices and indices (the rest of a TRI op is walk) */
       PH_G_SCRIPTS, PH_G_MODE, PH_G_ENT, PH_G_GFX,   /* game step: scripts, game mode (world/battle), entities, building the lists */
       PH_SUB,                   /* from here on: finer game phases, logged on their own SAMPLE sub line */
       PH_W_ENC = PH_SUB, PH_W_NPCS, PH_W_PLAYER, PH_W_ITEMS, PH_W_FX, PH_W_MODELS, PH_W_CAM,   /* state_step_world */
       PH_R_ENT, PH_R_MODELS, PH_R_PLAYER, PH_R_NPCS, PH_R_WORKERS, PH_R_FX, PH_R_TASKS,      /* render_frame */
       PH_R_SORT,                /* execute_render_tasks' sort (the rest of it is r_tasks) */
       PH_COUNT };

#ifdef PM3DS_STEP_PROF
extern volatile uint8_t pm3ds_prof_phase, pm3ds_prof_op;
#define PROF_PHASE(p) (pm3ds_prof_phase = (uint8_t)(p))
#define PROF_OP(o) (pm3ds_prof_op = (uint8_t)(o))
#define PROF_SAVE(v) uint8_t v = pm3ds_prof_phase
#define PROF_RESTORE(v) (pm3ds_prof_phase = (v))
extern uint64_t pm3ds_gp_ticks[GP_COUNT];
extern uint64_t svcGetSystemTick(void);
#define GP_BEGIN(v) uint64_t v = svcGetSystemTick()
#define GP_END(v, i) (pm3ds_gp_ticks[i] += svcGetSystemTick() - (v))
#else
#define PROF_PHASE(p) (void)0
#define PROF_OP(o) (void)0
#define PROF_SAVE(v) (void)0
#define PROF_RESTORE(v) (void)0
#define GP_BEGIN(v) (void)0
#define GP_END(v, i) (void)0
#endif

#endif
