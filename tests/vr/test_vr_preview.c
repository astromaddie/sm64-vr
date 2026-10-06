// The VR settings page over the gameplay view (vr_preview.h): the fade on the pause
// overview's cut, the held menu's turn aside, its pose, the diagnostic's timeline, and
// the page side setting. No GL, no DJUI, no game.

#include <math.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_math.h"
#include "vr_menu.h"
#include "vr_preview.h"
#include "vr_settings.h"

#define DEG (3.14159265358979323846f / 180.0f)

static vr_preview_in_t frame_in(bool menu_up, bool page_open, bool overview) {
	vr_preview_in_t in;
	memset(&in, 0, sizeof(in));
	in.stereo = true;
	in.menu_up = menu_up;
	in.page_open = page_open;
	in.overview = overview;
	in.side = VR_PREVIEW_SIDE_RIGHT;
	in.page_half = vr_preview_page_half(2.0f * 1.4f * tanf(20.0f * DEG), 1.4f); // hud_scale 100
	in.dt = 1.0f / 120.0f;
	return in;
}

// The page is 640 of 1280 px of a 40 degree HUD: about 20 degrees wide at any distance
static void test_page_half_width(void) {
	for (float d = 1.0f; d <= 6.0f; d += 1.25f) {
		float w = 2.0f * d * tanf(20.0f * DEG);
		CHECK_NEAR(vr_preview_page_half(w, d) / DEG, atanf(0.5f * tanf(20.0f * DEG)) / DEG, 1e-3);
		CHECK_NEAR(vr_preview_page_half(w, d) / DEG, 10.3, 0.05);
		// hud_scale 150: a wider page goes further out
		CHECK(vr_preview_page_half(1.5f * w, d) > vr_preview_page_half(w, d));
	}
	CHECK_NEAR(vr_preview_page_half(0, 1), 0, 1e-9);
	CHECK_NEAR(vr_preview_page_half(1, 0), 0, 1e-9);
}

// The inner edge sits 10 degrees from the view's forward, on the chosen side
static void test_menu_target(void) {
	float half = 10.3f * DEG;
	CHECK_NEAR(vr_preview_menu_target(VR_PREVIEW_SIDE_RIGHT, half) / DEG, -20.3, 1e-3);
	CHECK_NEAR(vr_preview_menu_target(VR_PREVIEW_SIDE_LEFT, half) / DEG, 20.3, 1e-3);
	CHECK_NEAR(vr_preview_menu_target(VR_PREVIEW_SIDE_CENTRE, half), 0, 1e-9);
	// inner edge: target minus the half-width
	CHECK_NEAR((fabsf(vr_preview_menu_target(VR_PREVIEW_SIDE_LEFT, half)) - half) / DEG, VR_PREVIEW_MARGIN_DEG, 1e-3);
	// still inside a comfortable head turn at the largest HUD (200%, page half about 20 degrees)
	float big = vr_preview_page_half(2.0f * 2.0f * 1.4f * tanf(20.0f * DEG), 1.4f);
	CHECK(fabsf(vr_preview_menu_target(VR_PREVIEW_SIDE_LEFT, big)) < 35.0f * DEG);
}

// The overview's cut fades in stereo, both ways; nothing else does
static void test_fade_on_overview_cut(void) {
	vr_preview_t p;
	vr_preview_reset(&p);

	// the first frame is not a cut, even on the overview
	vr_preview_in_t in = frame_in(false, false, true);
	vr_preview_out_t out = vr_preview_frame(&p, &in);
	CHECK(!out.fade && !out.overview_changed && !out.page_changed);

	// gameplay -> overview (Start) -> page open (overview held off) -> back -> resume
	in = frame_in(false, false, false);
	out = vr_preview_frame(&p, &in);
	CHECK(out.fade && out.overview_changed);
	out = vr_preview_frame(&p, &in);
	CHECK(!out.fade);
	in = frame_in(true, false, true);
	out = vr_preview_frame(&p, &in);
	CHECK(out.fade);
	// the page opens on the tick before the camera comes back: no fade for the page alone
	in = frame_in(true, true, true);
	out = vr_preview_frame(&p, &in);
	CHECK(!out.fade && out.page_changed);
	in = frame_in(true, true, false);
	out = vr_preview_frame(&p, &in);
	CHECK(out.fade && out.overview_changed && !out.page_changed);
	in = frame_in(true, false, false);
	out = vr_preview_frame(&p, &in);
	CHECK(!out.fade && out.page_changed);
	in = frame_in(true, false, true);
	CHECK(vr_preview_frame(&p, &in).fade);
	in = frame_in(false, false, false);
	CHECK(vr_preview_frame(&p, &in).fade);

	// on the mono panel the mode switch has its own fade
	in = frame_in(false, false, true);
	in.stereo = false;
	out = vr_preview_frame(&p, &in);
	CHECK(!out.fade && out.overview_changed);
}

// The held menu turns aside at a panel's pace while the page is open, and back
static void test_menu_turns_aside(void) {
	vr_preview_t p;
	vr_preview_reset(&p);
	vr_preview_in_t in = frame_in(true, false, false);
	CHECK_NEAR(vr_preview_frame(&p, &in).menu_yaw, 0, 1e-9);

	in = frame_in(true, true, false);
	float target = vr_preview_menu_target(in.side, in.page_half);
	CHECK(target < 0.0f); // right
	float yaw = vr_preview_frame(&p, &in).menu_yaw;
	CHECK_NEAR(yaw, -VR_PREVIEW_MENU_RATE_DPS * DEG * in.dt, 1e-6);
	int frames = 1;
	while (fabsf(yaw - target) > 1e-6f && frames < 1000) {
		float before = yaw;
		yaw = vr_preview_frame(&p, &in).menu_yaw;
		CHECK(yaw <= before + 1e-7f && yaw >= target - 1e-7f); // monotonic, never past
		frames++;
	}
	CHECK_NEAR(yaw, target, 1e-6);
	// about 20 degrees at 160 per second
	CHECK_NEAR(frames * in.dt, fabsf(target) / (VR_PREVIEW_MENU_RATE_DPS * DEG), 2 * in.dt);

	// the side changes live
	in.side = VR_PREVIEW_SIDE_LEFT;
	for (int i = 0; i < 120; i++) {
		yaw = vr_preview_frame(&p, &in).menu_yaw;
	}
	CHECK_NEAR(yaw, -target, 1e-6);

	// back to the pause menu: home again
	in = frame_in(true, false, false);
	for (int i = 0; i < 120; i++) {
		yaw = vr_preview_frame(&p, &in).menu_yaw;
	}
	CHECK_NEAR(yaw, 0, 1e-6);

	// centre: no turn at all
	in = frame_in(true, true, false);
	in.side = VR_PREVIEW_SIDE_CENTRE;
	CHECK_NEAR(vr_preview_frame(&p, &in).menu_yaw, 0, 1e-9);
}

// Under a fade the menu jumps; with the menu gone the HUD is unturned at once
static void test_menu_jumps_under_fade_and_resets(void) {
	vr_preview_t p;
	vr_preview_reset(&p);
	vr_preview_in_t in = frame_in(true, false, true);
	vr_preview_frame(&p, &in);
	in = frame_in(true, true, false); // the page and the camera's cut in one frame
	vr_preview_out_t out = vr_preview_frame(&p, &in);
	CHECK(out.fade);
	CHECK_NEAR(out.menu_yaw, vr_preview_menu_target(in.side, in.page_half), 1e-6);

	// Start closes every menu: the gameplay HUD follows the head, unturned, that frame
	in = frame_in(false, false, false);
	CHECK_NEAR(vr_preview_frame(&p, &in).menu_yaw, 0, 1e-9);

	// not stereo: unturned
	in = frame_in(true, true, false);
	in.stereo = false;
	CHECK_NEAR(vr_preview_frame(&p, &in).menu_yaw, 0, 1e-9);
}

// The turned quad keeps its distance from the head it was placed from, faces it, and
// keeps the head's pitch; positive yaw is to the left
static void test_menu_pose(void) {
	vr_pose_t head;
	memset(&head, 0, sizeof(head));
	head.position[0] = 0.2f;
	head.position[1] = 1.6f;
	head.position[2] = -0.1f;
	float yq[4], pq[4] = { sinf(-0.1f), 0, 0, cosf(-0.1f) }; // 11.5 degrees down
	vr_quat_from_yaw(0.4f, yq);
	vr_quat_mul(yq, pq, head.orientation);

	vr_pose_t base, left, right;
	vr_preview_menu_pose(&head, 0.0f, 1.4f, &base);
	vr_hud_pose(&head, 1.4f, &right); // unturned: exactly the HUD's own placement
	for (int i = 0; i < 3; i++) {
		CHECK_NEAR(base.position[i], right.position[i], 1e-6);
	}
	vr_preview_menu_pose(&head, 20.0f * DEG, 1.4f, &left);
	vr_preview_menu_pose(&head, -20.0f * DEG, 1.4f, &right);
	CHECK_NEAR(vr_yaw_of(left.orientation), 0.4f + 20.0f * DEG, 1e-4);
	CHECK_NEAR(vr_yaw_of(right.orientation), 0.4f - 20.0f * DEG, 1e-4);
	for (int k = 0; k < 2; k++) {
		const vr_pose_t *q = k ? &right : &left;
		float d[3];
		for (int i = 0; i < 3; i++) {
			d[i] = q->position[i] - head.position[i];
		}
		CHECK_NEAR(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1.4f, 1e-4);
		CHECK_NEAR(q->position[1], base.position[1], 1e-5); // the same pitch
		// it faces the head: its forward (-Z) points from the head to it
		float m[9];
		vr_mat3_from_quat(q->orientation, m);
		for (int i = 0; i < 3; i++) {
			CHECK_NEAR(-m[6 + i], d[i] / 1.4f, 1e-4);
		}
	}
	// left of the unturned quad, in the head's own frame: the head faces yaw 0.4
	float side_l = (left.position[0] - base.position[0]) * cosf(0.4f) - (left.position[2] - base.position[2]) * sinf(0.4f);
	float side_r = (right.position[0] - base.position[0]) * cosf(0.4f) - (right.position[2] - base.position[2]) * sinf(0.4f);
	CHECK(side_l < -0.3f);
	CHECK(side_r > 0.3f);
}

// The diagnostic: nothing until N seconds of gameplay, then each step once, in order, on time
static void test_diag_timeline(void) {
	vr_preview_diag_t d;
	memset(&d, 0, sizeof(d));
	const float dt = 1.0f / 30.0f;

	// off
	CHECK_INT(vr_preview_diag_tick(&d, 0.0f, true, dt), VR_PREVIEW_DIAG_NONE);
	// menus and warps do not count
	for (int i = 0; i < 600; i++) {
		CHECK_INT(vr_preview_diag_tick(&d, 5.0f, false, dt), VR_PREVIEW_DIAG_NONE);
	}
	float t = 0.0f;
	vr_preview_diag_step_t step = VR_PREVIEW_DIAG_NONE;
	int ticks = 0;
	while (step == VR_PREVIEW_DIAG_NONE && ticks < 1000) {
		step = vr_preview_diag_tick(&d, 5.0f, true, dt);
		ticks++;
	}
	CHECK_INT(step, VR_PREVIEW_DIAG_PAUSE);
	CHECK_NEAR(ticks * dt, 5.0, dt + 1e-4);

	// after that the clock runs through pause and menus (gameplay false)
	static const float expected[] = { 0, 0, 1.5f, 3, 4, 6, 7, 8, 9, 10, 10.5f, 12, 12 };
	int next = VR_PREVIEW_DIAG_PAUSE_MENU;
	for (int i = 1; i < 30 * 20; i++) {
		t = i * dt;
		step = vr_preview_diag_tick(&d, 5.0f, false, dt);
		if (step != VR_PREVIEW_DIAG_NONE) {
			CHECK_INT(step, next);
			CHECK_NEAR(t, expected[step], dt + 1e-4);
			next++;
		}
	}
	CHECK_INT(next, VR_PREVIEW_DIAG_DONE + 1);
	CHECK(expected[VR_PREVIEW_DIAG_DONE] <= VR_PREVIEW_DIAG_SECONDS);
	// never again
	CHECK_INT(vr_preview_diag_tick(&d, 5.0f, true, dt), VR_PREVIEW_DIAG_NONE);
}

static void test_diag_names(void) {
	CHECK_STR(vr_preview_diag_dump_tag(VR_PREVIEW_DIAG_DUMP_PAUSED), "paused-");
	CHECK_STR(vr_preview_diag_dump_tag(VR_PREVIEW_DIAG_DUMP_PAGE), "page-");
	CHECK_STR(vr_preview_diag_dump_tag(VR_PREVIEW_DIAG_DUMP_STEP), "step-");
	CHECK_STR(vr_preview_diag_dump_tag(VR_PREVIEW_DIAG_DUMP_RESUMED), "resumed-");
	CHECK(vr_preview_diag_dump_tag(VR_PREVIEW_DIAG_OPEN_PAGE) == NULL);
	for (int s = 0; s <= VR_PREVIEW_DIAG_DONE; s++) {
		CHECK(vr_preview_diag_name((vr_preview_diag_step_t)s)[0] != '\0');
	}
}

// settings_page_side: right by default, on the HUD tab; the diagnostic is in no tab
static void test_settings(void) {
	static vr_settings_t s;
	memset(&s, 0, sizeof(s));
	vr_settings_defaults(&s);
	CHECK_INT((int)vr_settings_get(&s, "settings_page_side"), VR_PREVIEW_SIDE_RIGHT);
	CHECK_NEAR(vr_settings_get(&s, "diag_settings_preview"), 0, 1e-9);
	CHECK(vr_settings_set(&s, "settings_page_side", VR_PREVIEW_SIDE_CENTRE));
	CHECK_INT((int)vr_settings_get(&s, "settings_page_side"), VR_PREVIEW_SIDE_CENTRE);

	size_t entries[32];
	size_t n = vr_menu_tab_entries(VR_TAB_HUD, entries, 32);
	bool found = false;
	for (size_t i = 0; i < n; i++) {
		const vr_menu_entry_t *e = vr_menu_entry(entries[i]);
		if (e->key && strcmp(e->key, "settings_page_side") == 0) {
			found = true;
			CHECK_INT(vr_menu_choice_count(e), 3);
			CHECK_STR(vr_menu_choice_label(e, VR_PREVIEW_SIDE_RIGHT), "Right");
			CHECK_STR(vr_menu_choice_label(e, VR_PREVIEW_SIDE_LEFT), "Left");
			CHECK_STR(vr_menu_choice_label(e, VR_PREVIEW_SIDE_CENTRE), "Centre");
		}
		CHECK(strcmp(vr_menu_entry(entries[i])->key ? vr_menu_entry(entries[i])->key : "", "diag_settings_preview") != 0);
	}
	CHECK(found);
}

int main(void) {
	RUN(test_page_half_width);
	RUN(test_menu_target);
	RUN(test_fade_on_overview_cut);
	RUN(test_menu_turns_aside);
	RUN(test_menu_jumps_under_fade_and_resets);
	RUN(test_menu_pose);
	RUN(test_diag_timeline);
	RUN(test_diag_names);
	RUN(test_settings);
	return SFVR_TEST_RESULT("vr_preview");
}
