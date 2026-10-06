// The stereo/mono policy (mode switches, recentre and standing height, fade,
// panel) and the [vr-perf] feed. No GL, no runtime.

#include <string.h>

#include "sfvr_test.h"
#include "vr_perf.h"
#include "vr_stage.h"

#define SEC 1000000000LL

static void test_fade_curve(void) {
	CHECK_NEAR(vr_fade_alpha(0), 1, 1e-6);
	CHECK_NEAR(vr_fade_alpha(0.249f), 1, 1e-6); // 0.25 s of black
	CHECK_NEAR(vr_fade_alpha(0.25f + 0.175f), 0.5, 1e-5); // then 0.35 s out
	CHECK_NEAR(vr_fade_alpha(0.6f), 0, 1e-6);
	CHECK_NEAR(vr_fade_alpha(5), 0, 1e-6);
}

static vr_view_t eye_at(float yaw, float x) {
	vr_view_t v;
	memset(&v, 0, sizeof(v));
	vr_quat_from_yaw(yaw, v.pose.orientation);
	v.pose.position[0] = x;
	v.pose.position[1] = 1.6f;
	return v;
}

static void test_mode_switch_fades_and_recentres(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	CHECK_INT(s.mode, VR_MODE_NONE);

	// the first frame counts as a switch (fade in from black)
	CHECK(vr_stage_set_mode(&s, VR_MODE_MONO, 1 * SEC));
	CHECK(!vr_stage_set_mode(&s, VR_MODE_MONO, 2 * SEC));
	CHECK_NEAR(vr_stage_fade_alpha(&s, 1 * SEC), 1, 1e-6);

	// mono -> stereo: a new fade, and a recentre pending
	vr_stage_t before = s;
	CHECK(vr_stage_set_mode(&s, VR_MODE_STEREO, 10 * SEC));
	CHECK_INT(s.mode_switches, before.mode_switches + 1);
	CHECK(s.recentre_pending);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC), 1, 1e-6);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC + 249000000), 1, 1e-6);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC + 425000000), 0.5, 1e-3);
	CHECK_NEAR(vr_stage_fade_alpha(&s, 10 * SEC + 700000000), 0, 1e-6);
	CHECK(!s.fade_active);

	// stereo -> mono also fades
	CHECK(vr_stage_set_mode(&s, VR_MODE_MONO, 20 * SEC));
	CHECK_NEAR(vr_stage_fade_alpha(&s, 20 * SEC + 100000000), 1, 1e-6);
}

static void test_recentre_applies_once_at_a_stereo_frame(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	vr_view_t v[2] = { eye_at(0.5f, -0.03f), eye_at(0.5f, 0.03f) };
	vr_stage_set_mode(&s, VR_MODE_STEREO, SEC);

	// nothing is applied until the stereo frame asks
	CHECK(!s.recentre_valid);
	CHECK(vr_stage_apply_recentre(&s, v));
	CHECK_NEAR(s.recentre.yaw, 0.5, 1e-5);
	CHECK_NEAR(s.recentre.origin[1], 1.6, 1e-5);
	CHECK(!s.recentre_pending);

	// a second call in a later frame does nothing: the offset holds while the head turns
	vr_view_t turned[2] = { eye_at(1.2f, -0.03f), eye_at(1.2f, 0.03f) };
	CHECK(!vr_stage_apply_recentre(&s, turned));
	CHECK_NEAR(s.recentre.yaw, 0.5, 1e-5);

	// a request (button, focus gain, reference-space change) re-applies it at the next frame
	vr_stage_request_recentre(&s);
	CHECK(vr_stage_apply_recentre(&s, turned));
	CHECK_NEAR(s.recentre.yaw, 1.2, 1e-5);
	CHECK_INT(s.recentres_applied, 2);

	// a request during mono frames waits for the next stereo frame
	vr_stage_set_mode(&s, VR_MODE_MONO, 5 * SEC);
	vr_stage_request_recentre(&s);
	CHECK(s.recentre_pending);
	vr_stage_set_mode(&s, VR_MODE_STEREO, 6 * SEC);
	CHECK(vr_stage_apply_recentre(&s, v));
	CHECK_NEAR(s.recentre.yaw, 0.5, 1e-5);
	CHECK(!vr_stage_apply_recentre(&s, v));
}

static vr_view_t eye_at_height(float yaw, float x, float y) {
	vr_view_t v = eye_at(yaw, x);
	v.pose.position[1] = y;
	return v;
}

// standard.md section 1: a recentre takes the head's yaw and horizontal
// position and keeps the standing height (ducking then lowers the view); the
// first one, View held 3 s and `seated` take the height too
static void test_recentre_keeps_standing_height(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	vr_stage_set_mode(&s, VR_MODE_STEREO, SEC);
	vr_view_t standing[2] = { eye_at_height(0.2f, 0.47f, 1.6f), eye_at_height(0.2f, 0.53f, 1.6f) };
	CHECK(vr_stage_apply_recentre(&s, standing));
	CHECK_NEAR(s.recentre.origin[1], 1.6, 1e-5);

	// Crouched 0.4 m and moved: the 1 s recentre keeps the standing height
	vr_view_t crouched[2] = { eye_at_height(-0.7f, 1.17f, 1.2f), eye_at_height(-0.7f, 1.23f, 1.2f) };
	vr_stage_request_recentre(&s);
	CHECK(vr_stage_apply_recentre(&s, crouched));
	CHECK_NEAR(s.recentre.yaw, -0.7, 1e-5);
	CHECK_NEAR(s.recentre.origin[0], 1.2, 1e-5);
	CHECK_NEAR(s.recentre.origin[1], 1.6, 1e-5);
	// A mode switch's recentre keeps it too
	vr_stage_set_mode(&s, VR_MODE_MONO, 2 * SEC);
	vr_stage_set_mode(&s, VR_MODE_STEREO, 3 * SEC);
	CHECK(vr_stage_apply_recentre(&s, crouched));
	CHECK_NEAR(s.recentre.origin[1], 1.6, 1e-5);

	// View held 3 s: the height is taken again
	vr_stage_request_height(&s);
	CHECK(s.recentre_pending);
	CHECK(vr_stage_apply_recentre(&s, crouched));
	CHECK_NEAR(s.recentre.origin[1], 1.2, 1e-5);
	CHECK(!s.height_pending);
	vr_stage_request_recentre(&s);
	CHECK(vr_stage_apply_recentre(&s, standing));
	CHECK_NEAR(s.recentre.origin[1], 1.2, 1e-5);

	// Seated: every recentre takes it
	s.seated = true;
	vr_stage_request_recentre(&s);
	CHECK(vr_stage_apply_recentre(&s, standing));
	CHECK_NEAR(s.recentre.origin[1], 1.6, 1e-5);
	vr_stage_request_recentre(&s);
	CHECK(vr_stage_apply_recentre(&s, crouched));
	CHECK_NEAR(s.recentre.origin[1], 1.2, 1e-5);
}

static void test_panel_is_placed_then_held(void) {
	vr_stage_t s;
	vr_stage_init(&s);
	vr_view_t v[2] = { eye_at(0, -0.03f), eye_at(0, 0.03f) };
	vr_pose_t p;

	vr_stage_panel_pose(&s, v, true, 2.0f, &p);
	CHECK_NEAR(p.position[0], 0, 1e-5);
	CHECK_NEAR(p.position[1], 1.6, 1e-5);
	CHECK_NEAR(p.position[2], -2.0, 1e-5);

	// the head turns: the panel stays where it was put
	vr_view_t turned[2] = { eye_at(1.0f, -0.03f), eye_at(1.0f, 0.03f) };
	vr_stage_panel_pose(&s, turned, true, 2.0f, &p);
	CHECK_NEAR(p.position[2], -2.0, 1e-5);

	// a recentre request puts it in front of the head again
	vr_stage_request_recentre(&s);
	vr_stage_panel_pose(&s, turned, true, 2.0f, &p);
	CHECK(p.position[2] > -1.9f);
	CHECK_NEAR(vr_yaw_of(p.orientation), 1.0, 1e-5);

	// with no head pose it is straight ahead of the origin and stays pending
	vr_stage_t t;
	vr_stage_init(&t);
	vr_stage_panel_pose(&t, v, false, 2.0f, &p);
	CHECK_NEAR(p.position[2], -2.0, 1e-5);
	CHECK(t.panel_pending);
}

static void test_perf_line(void) {
	vr_perf_t p;
	vr_perf_init(&p);
	char line[256];
	const float eyes[2] = { 2.2f, 2.1f };
	int emitted = 0;
	int64_t period = 11111111; // 90 Hz
	int64_t display = 100 * SEC;
	for (int i = 0; i < 1000; i++) {
		double now = i * (1.0 / 90.0);
		// frame 300 takes two periods: one missed
		display += (i == 300) ? 2 * period : period;
		if (vr_perf_frame(&p, now, display, period, 1.0f, 4.4f, 0.5f, eyes, -1.0f, line, sizeof(line))) {
			emitted++;
			CHECK(strncmp(line, "[vr-perf] fps=", 14) == 0);
			CHECK(strstr(line, "missed=1") != NULL);
			CHECK(strstr(line, "tick=1.00 replay=4.40 hud=0.50") != NULL);
			CHECK(strstr(line, "eye=2.20/2.10ms") != NULL);
			CHECK(strstr(line, "cpu=5.90ms") != NULL);
			CHECK(strstr(line, "gpu=n/a") != NULL);
		}
	}
	CHECK_INT(emitted, 1); // 1000 frames at 90 Hz is 11 s: one 10 s window
}

int main(void) {
	RUN(test_fade_curve);
	RUN(test_mode_switch_fades_and_recentres);
	RUN(test_recentre_applies_once_at_a_stereo_frame);
	RUN(test_recentre_keeps_standing_height);
	RUN(test_panel_is_placed_then_held);
	RUN(test_perf_line);
	return SFVR_TEST_RESULT("vr_stage");
}
