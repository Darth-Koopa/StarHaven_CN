/*
 * step_prof.c — where a slow game step spends its time (debug builds with
 * -DPM3DS_STEP_PROF). Code marks the end of a stretch with pm3ds_prof_mark();
 * a step over PROF_SLOW_MS logs its marks and the shared counters.
 */
#ifdef PM3DS_STEP_PROF
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "pm3ds_log.h"

#define PROF_SLOW_MS 20.0f
#define PROF_MARKS   16

uint64_t pm3ds_prof_yay0_ticks, pm3ds_prof_sprite_ticks;
extern uint64_t pm3ds_prof_inval_ticks;
extern uint32_t pm3ds_prof_inval_calls;
extern unsigned pm3ds_prof_tex_entries(void);
static uint64_t s_inval_0;
static uint32_t s_invc_0;
static uint64_t s_step_t0, s_last_mark;
static uint64_t s_yay0_0, s_sprite_0;
static uint32_t s_rom_ms_0, s_rom_miss_0;
static struct { const char *label; int arg; float ms; } s_marks[PROF_MARKS];
static int s_nmarks;

extern void pm3ds_rom_stream_stats(u32 *hits, u32 *misses, u32 *miss_ms);

static float ms(uint64_t ticks) { return (float)ticks * 1000.0f / SYSCLOCK_ARM11; }

void pm3ds_prof_step_begin(void) {
    u32 h, m, mms;
    pm3ds_rom_stream_stats(&h, &m, &mms);
    s_rom_ms_0 = mms;
    s_rom_miss_0 = m;
    s_yay0_0 = pm3ds_prof_yay0_ticks;
    s_sprite_0 = pm3ds_prof_sprite_ticks;
    s_inval_0 = pm3ds_prof_inval_ticks;
    s_invc_0 = pm3ds_prof_inval_calls;
    s_step_t0 = s_last_mark = svcGetSystemTick();
    s_nmarks = 0;
}

void pm3ds_prof_mark(const char *label, int arg) {
    uint64_t now = svcGetSystemTick();
    if (s_nmarks < PROF_MARKS) {
        s_marks[s_nmarks].label = label;
        s_marks[s_nmarks].arg = arg;
        s_marks[s_nmarks].ms = ms(now - s_last_mark);
        s_nmarks++;
    }
    s_last_mark = now;
}

void pm3ds_prof_step_end(int game_mode) {
    uint64_t now = svcGetSystemTick();
    float total = ms(now - s_step_t0);
    if (total < PROF_SLOW_MS) return;
    u32 h, m, mms;
    pm3ds_rom_stream_stats(&h, &m, &mms);
    char buf[200];
    int n = snprintf(buf, sizeof buf, "SLOWSTEP %.1fms mode=%d rom=%lums/%lu yay0=%.1f inval=%.1f/%lu tex=%u |", total, game_mode,
                     (unsigned long)(mms - s_rom_ms_0), (unsigned long)(m - s_rom_miss_0),
                     ms(pm3ds_prof_yay0_ticks - s_yay0_0), ms(pm3ds_prof_inval_ticks - s_inval_0),
                     (unsigned long)(pm3ds_prof_inval_calls - s_invc_0), pm3ds_prof_tex_entries());
    for (int i = 0; i < s_nmarks && n > 0 && n < (int)sizeof buf - 24; i++)
        n += snprintf(buf + n, sizeof buf - n, " %s%d=%.1f", s_marks[i].label, s_marks[i].arg, s_marks[i].ms);
    pm3ds_log(PM3DS_LOG_INFO, "%s", buf);
}
#endif
