/*
 * os_event.c — osSetEventMesg routing table.
 * Fidelity tier: MINIMAL — only events we actually post are wired
 *                (VI retrace, PI/SI complete, AI). Others are recorded
 *                but never fire at this milestone.
 */

#include <stddef.h>

typedef void *OSMesg;
typedef struct OSMesgQueue OSMesgQueue;
int osSendMesg(OSMesgQueue *, OSMesg, int);

#define OS_EVENT_SW1        0
#define OS_EVENT_SW2        1
#define OS_EVENT_CART       2
#define OS_EVENT_COUNTER    3
#define OS_EVENT_SP         4
#define OS_EVENT_SI         5
#define OS_EVENT_AI         6
#define OS_EVENT_VI         7
#define OS_EVENT_PI         8
#define OS_EVENT_DP         9
#define OS_EVENT_CPU_BREAK  10
#define OS_EVENT_SP_BREAK   11
#define OS_EVENT_FAULT      12
#define OS_EVENT_THREADSTATUS 13
#define OS_EVENT_PRENMI     14
#define OS_NUM_EVENTS       15

typedef struct {
    OSMesgQueue *mq;
    OSMesg       msg;
} event_slot_t;

static event_slot_t g_events[OS_NUM_EVENTS];

void osSetEventMesg(int event, OSMesgQueue *mq, OSMesg msg) {
    if (event < 0 || event >= OS_NUM_EVENTS) return;
    g_events[event].mq  = mq;
    g_events[event].msg = msg;
}

/* Posted by the shim's internal sources (VI retrace thread, PI completion,
 * etc.). Safe to call when nothing is registered — silently drops. */
void pm3ds_event_post(int event) {
    if (event < 0 || event >= OS_NUM_EVENTS) return;
    if (!g_events[event].mq) return;
    osSendMesg(g_events[event].mq, g_events[event].msg, /*NOBLOCK*/ 0);
}
