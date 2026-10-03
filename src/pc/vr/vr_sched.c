#include <math.h>

#include "vr_sched.h"

// Display times within this of a tick boundary count as on it: predictedDisplayTime
// arrives in integer nanoseconds, so a frame on a boundary can land a few ulps either side.
#define VR_SCHED_EPSILON_S 1e-7

void vr_sched_init(vr_sched_t *s, double tick_s, int max_catch_up) {
    s->tick_s = tick_s > 0.0 ? tick_s : VR_SCHED_TICK_S;
    s->max_catch_up = max_catch_up >= 1 ? max_catch_up : 1;
    s->anchored = false;
    s->base = 0.0;
    s->newest = -1;
    s->ticks_run = 0;
    s->rebases = 0;
}

void vr_sched_reset(vr_sched_t *s) {
    s->anchored = false;
}

static double newest_time(const vr_sched_t *s) {
    return s->base + (double)s->newest * s->tick_s;
}

vr_sched_plan_t vr_sched_plan(vr_sched_t *s, double display_s) {
    vr_sched_plan_t plan = { 0, false };

    if (!s->anchored) {
        // The next tick to run becomes index 0 and is shown exactly at this frame
        s->anchored = true;
        s->base = display_s;
        s->newest = -1;
        s->rebases++;
        plan.ticks = 1;
        plan.rebased = true;
        return plan;
    }

    double behind = (display_s - newest_time(s)) / s->tick_s;
    double needed = ceil(behind - VR_SCHED_EPSILON_S / s->tick_s);
    if (needed <= 0.0) {
        return plan;
    }
    if (needed > (double)s->max_catch_up) {
        // Too far behind to catch up without visibly fast-forwarding the game:
        // slide the timeline so one tick lands exactly on this frame
        s->base = display_s - (double)(s->newest + 1) * s->tick_s;
        s->rebases++;
        plan.ticks = 1;
        plan.rebased = true;
        return plan;
    }
    plan.ticks = (int)needed;
    return plan;
}

void vr_sched_tick_ran(vr_sched_t *s) {
    s->newest++;
    s->ticks_run++;
}

float vr_sched_t_at(const vr_sched_t *s, double display_s) {
    if (!s->anchored || s->newest < 0) {
        return 1.0f;
    }
    double t = 1.0 - (newest_time(s) - display_s) / s->tick_s;
    if (!(t > 0.0)) {
        return 0.0f;
    }
    if (t > 1.0) {
        return 1.0f;
    }
    return (float)t;
}
