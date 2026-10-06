// The system layer and the virtual pad (src/pc/vr/vr_input.c, vr_pad_map):
// standard.md section 1. No runtime.

#include <string.h>

#include "sfvr_test.h"
#include "vr_input.h"

static void drain(void) {
	while (vr_take_recentre_request() || vr_take_height_request()) {
	}
}

static vr_pad_sample_t sample_view(bool down) {
	vr_pad_sample_t s;
	memset(&s, 0, sizeof(s));
	s.view = down;
	return s;
}

static void test_view_short_press_is_back(void) {
	vr_pad_t pad;
	vr_gamepad_t out;
	vr_pad_init(&pad);
	drain();
	vr_pad_sample_t up = sample_view(false), down = sample_view(true);
	vr_pad_map(&pad, &up, 0.0, &out);
	vr_pad_map(&pad, &down, 0.1, &out);
	CHECK(!out.buttons[VR_PAD_BACK]);
	unsigned ev = vr_pad_map(&pad, &up, 0.4, &out);
	CHECK(ev & SFVR_VIEW_SHORT_PRESS);
	CHECK(out.buttons[VR_PAD_BACK]);
	CHECK(!vr_take_recentre_request());
	CHECK(!vr_take_height_request());
}

static void test_view_holds_recentre_then_height(void) {
	vr_pad_t pad;
	vr_gamepad_t out;
	vr_pad_init(&pad);
	drain();
	vr_pad_sample_t up = sample_view(false), down = sample_view(true);
	vr_pad_map(&pad, &up, 0.0, &out);
	vr_pad_map(&pad, &down, 0.1, &out);
	vr_pad_map(&pad, &down, 0.9, &out);
	CHECK(!vr_take_recentre_request());
	// 1 s: a recentre that keeps the standing height
	unsigned ev = vr_pad_map(&pad, &down, 1.15, &out);
	CHECK(ev & SFVR_VIEW_RECENTRE);
	CHECK(vr_take_recentre_request());
	CHECK(!vr_take_height_request());
	vr_pad_map(&pad, &down, 2.0, &out);
	CHECK(!vr_take_recentre_request());
	// 3 s: the standing height too
	ev = vr_pad_map(&pad, &down, 3.2, &out);
	CHECK(ev & SFVR_VIEW_RECALIBRATE_HEIGHT);
	CHECK(vr_take_height_request());
	// Released after a hold: no Back
	vr_pad_map(&pad, &up, 3.5, &out);
	CHECK(!out.buttons[VR_PAD_BACK]);
}

static void test_menu_is_start_and_sticks(void) {
	vr_pad_t pad;
	vr_gamepad_t out;
	vr_pad_init(&pad);
	vr_pad_sample_t s;
	memset(&s, 0, sizeof(s));
	s.menu = true;
	s.r_stick[0] = 1.0f;
	s.l_stick[1] = 1.0f;
	s.dpad_left = true;
	vr_pad_map(&pad, &s, 0.0, &out);
	CHECK(out.buttons[VR_PAD_START]);
	CHECK(out.buttons[VR_PAD_DPAD_LEFT]);
	CHECK_INT(out.right_x, 32767); // the turn reads it (controller_vr.c keeps it from the game)
	CHECK_INT(out.left_y, -32767); // SDL's y is down
}

int main(void) {
	RUN(test_view_short_press_is_back);
	RUN(test_view_holds_recentre_then_height);
	RUN(test_menu_is_start_and_sticks);
	return SFVR_TEST_RESULT("vr_input");
}
