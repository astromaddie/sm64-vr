// The refresh-rate fallback's window bookkeeping (vr_rate.c).

#include "sfvr_test.h"
#include "vr_rate.h"

// Feeds frames at `fps` for `seconds` from `*t`; returns the last non-NONE result.
static vr_rate_result_t run(vr_rate_t *r, double *t, double fps, double seconds, bool counting) {
	vr_rate_result_t last = VR_RATE_NONE;
	int frames = (int)(fps * seconds);
	for (int i = 0; i < frames; i++) {
		*t += 1.0 / fps;
		vr_rate_result_t res = vr_rate_frame(r, *t, counting);
		if (res != VR_RATE_NONE) {
			last = res;
		}
	}
	return last;
}

static void test_holds(void) {
	vr_rate_t r;
	double t = 100;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 120);
	CHECK_INT(run(&r, &t, 119.5, 10.5, true), VR_RATE_WINDOW_OK);
	CHECK_NEAR(r.last_fps, 119.5, 1.5);
	CHECK_INT(run(&r, &t, 108.5, 30, true), VR_RATE_WINDOW_OK); // 90.4% of 120: not below
	CHECK_INT(r.low_windows, 0);
}

static void test_two_low_windows_are_reported(void) {
	vr_rate_t r;
	double t = 0;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 120);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW); // 83%
	CHECK_INT(r.low_windows, 1);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_NOT_HELD);
	CHECK_INT(r.low_windows, 0);
}

static void test_reported_once(void) {
	vr_rate_t r;
	double t = 0;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 120);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_NOT_HELD);
	// still slow: never reported again, not even after a new target (the run's one report)
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW);
	vr_rate_set_target(&r, 90);
	CHECK_INT(run(&r, &t, 60, 10.1, true), VR_RATE_WINDOW_LOW);
	CHECK_INT(run(&r, &t, 60, 10.1, true), VR_RATE_WINDOW_LOW);
}

static void test_good_window_between_resets_the_run(void) {
	vr_rate_t r;
	double t = 0;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 120);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW);
	CHECK_INT(run(&r, &t, 120, 10.1, true), VR_RATE_WINDOW_OK);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW); // the count began again
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_NOT_HELD);
}

static void test_unworn_frames_do_not_count(void) {
	vr_rate_t r;
	double t = 0;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 90);
	CHECK_INT(run(&r, &t, 30, 60, false), VR_RATE_NONE); // a headset on the desk, however slow
	CHECK_INT(run(&r, &t, 60, 10.1, true), VR_RATE_WINDOW_LOW);
	CHECK_INT(run(&r, &t, 60, 5, false), VR_RATE_NONE); // taken off: the run ends
	CHECK_INT(r.low_windows, 0);
	CHECK_INT(run(&r, &t, 60, 10.1, true), VR_RATE_WINDOW_LOW);
}

static void test_window_length(void) {
	vr_rate_t r;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 72);
	for (int i = 0; i < 719; i++) {
		CHECK_INT(vr_rate_frame(&r, 5.0 + i / 72.0, true), VR_RATE_NONE); // 9.99 s
	}
	CHECK_INT(vr_rate_frame(&r, 15.0, true), VR_RATE_WINDOW_OK);
}

static void test_new_target_starts_over(void) {
	vr_rate_t r;
	double t = 0;
	vr_rate_init(&r);
	vr_rate_set_target(&r, 120);
	CHECK_INT(run(&r, &t, 100, 10.1, true), VR_RATE_WINDOW_LOW);
	vr_rate_set_target(&r, 90); // the fallback (or a change event) moved the display
	CHECK_INT(r.low_windows, 0);
	CHECK_INT(run(&r, &t, 89, 10.1, true), VR_RATE_WINDOW_OK);
}

static void test_no_target_watches_nothing(void) {
	vr_rate_t r;
	double t = 0;
	vr_rate_init(&r);
	CHECK_INT(run(&r, &t, 10, 60, true), VR_RATE_NONE);
}

int main(void) {
	RUN(test_holds);
	RUN(test_two_low_windows_are_reported);
	RUN(test_reported_once);
	RUN(test_good_window_between_resets_the_run);
	RUN(test_unworn_frames_do_not_count);
	RUN(test_window_length);
	RUN(test_new_target_starts_over);
	RUN(test_no_target_watches_nothing);
	return SFVR_TEST_RESULT("vr_rate");
}
