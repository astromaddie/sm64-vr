// The display-time scheduler (src/pc/vr/vr_sched.c): one 30 Hz tick per 1/30 s
// of display time at any refresh rate, t from the display time, bounded catch-up.

#include "sfvr_test.h"
#include "vr_sched.h"

// Runs `frames` XR frames at `hz` from display time t0; returns the ticks run
// and the largest step in displayed game time between frames, in ticks.
static long run(vr_sched_t *s, double hz, int frames, double t0, double *max_step, double *min_step) {
    long ticks = 0;
    double last_game = -1.0;
    *max_step = 0.0;
    *min_step = 1e9;
    for (int i = 0; i < frames; i++) {
        double r = t0 + i / hz;
        vr_sched_plan_t p = vr_sched_plan(s, r);
        for (int k = 0; k < p.ticks; k++) {
            vr_sched_tick_ran(s);
            ticks++;
        }
        // displayed game time: newest tick index - 1 + t
        double game = (double)s->newest - 1.0 + vr_sched_t_at(s, r);
        if (last_game >= 0.0) {
            double step = game - last_game;
            if (step > *max_step) *max_step = step;
            if (step < *min_step) *min_step = step;
        }
        last_game = game;
    }
    return ticks;
}

static void test_rate_holds(void) {
    const double rates[] = { 72.0, 90.0, 120.0, 144.0, 60.0 };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        vr_sched_t s;
        vr_sched_init(&s, VR_SCHED_TICK_S, VR_SCHED_MAX_CATCH_UP);
        double maxs, mins;
        long ticks = run(&s, rates[i], (int)(rates[i] * 60.0), 1000.0, &maxs, &mins);
        // 60 s of display time: 1800 ticks (plus the anchoring one)
        CHECK(ticks >= 1800 && ticks <= 1801);
        // Displayed game time advances by exactly one display period per frame
        double expect = 30.0 / rates[i];
        CHECK_NEAR(maxs, expect, 1e-4);
        CHECK_NEAR(mins, expect, 1e-4);
        CHECK_INT(s.rebases, 1);
    }
}

static void test_t_range(void) {
    vr_sched_t s;
    vr_sched_init(&s, VR_SCHED_TICK_S, VR_SCHED_MAX_CATCH_UP);
    vr_sched_plan_t p = vr_sched_plan(&s, 5.0);
    CHECK(p.rebased);
    CHECK_INT(p.ticks, 1);
    vr_sched_tick_ran(&s);
    CHECK_NEAR(vr_sched_t_at(&s, 5.0), 1.0, 1e-6); // anchored: the tick is shown exactly now
    p = vr_sched_plan(&s, 5.0 + 0.5 / 30.0);
    CHECK_INT(p.ticks, 1);
    vr_sched_tick_ran(&s);
    CHECK_NEAR(vr_sched_t_at(&s, 5.0 + 0.5 / 30.0), 0.5, 1e-5);
    p = vr_sched_plan(&s, 5.0 + 0.9 / 30.0);
    CHECK_INT(p.ticks, 0);
    CHECK_NEAR(vr_sched_t_at(&s, 5.0 + 0.9 / 30.0), 0.9, 1e-5);
}

static void test_catch_up_bounded(void) {
    vr_sched_t s;
    vr_sched_init(&s, VR_SCHED_TICK_S, VR_SCHED_MAX_CATCH_UP);
    vr_sched_plan(&s, 1.0);
    vr_sched_tick_ran(&s);
    // a 2-tick hitch is caught up
    vr_sched_plan_t p = vr_sched_plan(&s, 1.0 + 2.0 / 30.0);
    CHECK_INT(p.ticks, 2);
    CHECK(!p.rebased);
    vr_sched_tick_ran(&s);
    vr_sched_tick_ran(&s);
    // a 1 s stall re-anchors: one tick, shown exactly at this frame, never a fast-forward
    p = vr_sched_plan(&s, 2.1);
    CHECK_INT(p.ticks, 1);
    CHECK(p.rebased);
    vr_sched_tick_ran(&s);
    CHECK_NEAR(vr_sched_t_at(&s, 2.1), 1.0, 1e-6);
    CHECK_INT(s.rebases, 2);
}

static void test_reset(void) {
    vr_sched_t s;
    vr_sched_init(&s, VR_SCHED_TICK_S, VR_SCHED_MAX_CATCH_UP);
    vr_sched_plan(&s, 1.0);
    vr_sched_tick_ran(&s);
    vr_sched_reset(&s); // standby or focus loss
    vr_sched_plan_t p = vr_sched_plan(&s, 50.0);
    CHECK(p.rebased);
    CHECK_INT(p.ticks, 1);
}

int main(void) {
    RUN(test_rate_holds);
    RUN(test_t_range);
    RUN(test_catch_up_bounded);
    RUN(test_reset);
    return SFVR_TEST_RESULT("test_vr_sched");
}
