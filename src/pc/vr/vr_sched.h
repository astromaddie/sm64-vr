#ifndef VR_SCHED_H
#define VR_SCHED_H

#include <stdbool.h>
#include <stdint.h>

// The display-time scheduler: places every XR frame on the game's fixed 30 Hz
// tick timeline by its predictedDisplayTime (after the F-Zero X port's
// gdx_vr_scheduler, docs/VR_ARCHITECTURE.md section 3).
//
// Timeline: tick n's state is shown at T(n) = base + n * tick. A frame shown at
// display time r needs the newest simulated tick N to satisfy T(N) >= r, and is
// rendered at t = 1 - (T(N) - r) / tick, between tick N-1 (t = 0) and tick N
// (t = 1). sm64coopdx's interpolation (patch_interpolations(delta)) takes that t
// as its delta.
//
// Pure policy: no OpenXR, no clock reads, no game state. Over any span without a
// rebase exactly one tick runs per `tick` seconds of display time, whatever the
// refresh rate, so the sim rate never follows the display rate. A frame that
// would need more than max_catch_up ticks re-anchors the timeline instead of
// fast-forwarding the game, and reports `rebased` (the caller cuts interpolation).

#define VR_SCHED_TICK_S (1.0 / 30.0) // FRAMERATE in pc_main.c
#define VR_SCHED_MAX_CATCH_UP 2

typedef struct {
    double tick_s;
    int max_catch_up;
    bool anchored;
    double base; // display time of tick index 0 on the current timeline, seconds
    int64_t newest; // index of the newest tick run on the current timeline
    int64_t ticks_run; // since init
    uint32_t rebases;
} vr_sched_t;

typedef struct {
    int ticks; // ticks to run before this frame is rendered (0..max_catch_up)
    bool rebased; // the timeline was re-anchored: cut interpolation
} vr_sched_plan_t;

void vr_sched_init(vr_sched_t *s, double tick_s, int max_catch_up);

// Forget the timeline: the next plan re-anchors on its display time. Call when
// the sim resumes after standby, focus loss or a stopped session.
void vr_sched_reset(vr_sched_t *s);

// Once per XR frame, after xrWaitFrame: how many ticks must run first.
vr_sched_plan_t vr_sched_plan(vr_sched_t *s, double display_s);

// One tick completed.
void vr_sched_tick_ran(vr_sched_t *s);

// The interpolation t in [0, 1] for a frame shown at display_s.
float vr_sched_t_at(const vr_sched_t *s, double display_s);

#endif
