// Comfort turning (src/pc/vr/vr_turn.c) and its protocol with the game camera:
// the stick's hysteresis and rates, the camera's limits, and a simulation of
// the renderer and camera.c taking turns in the same steps the game does
// (display frames at 120 Hz, camera ticks at 30 Hz, the camera node's
// interpolation), checking that a snap shows at once and only once.

#include <math.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_cam.h"
#include "vr_math.h"
#include "vr_turn.h"

#define DEG (VR_TURN_PI / 180.0f)

static sfvr_turn_config config(sfvr_turn_mode mode) {
	sfvr_turn_config c;
	sfvr_turn_config_default(&c);
	c.mode = mode;
	return c;
}

static vr_turn_gate_t open_gate(float queued) {
	vr_turn_gate_t g = { true, queued, VR_TURN_UNLIMITED, VR_TURN_UNLIMITED };
	return g;
}

static void test_snap_hysteresis(void) {
	vr_turn_t t;
	vr_turn_reset(&t);
	sfvr_turn_config c = config(SFVR_TURN_SNAP);
	vr_turn_gate_t g = open_gate(0.0f);

	// Arms at 70%: pushed right is a turn right, a negative heading
	CHECK_NEAR(vr_turn_frame(&t, &c, 0.69f, 0.01f, &g).delta, 0.0, 1e-9);
	vr_turn_step_t s = vr_turn_frame(&t, &c, 0.7f, 0.01f, &g);
	CHECK_NEAR(s.delta, -30.0f * DEG, 1e-6);
	CHECK_INT(s.kind, VR_TURN_STEP_SNAP);
	CHECK(!s.limited);
	// Held, then eased back to 40%: nothing; re-arms under 30%
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 0.4f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 0.75f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 0.29f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, -0.8f, 0.01f, &g).delta, 30.0f * DEG, 1e-6);

	// Other angles go through sfvr's normalisation
	vr_turn_reset(&t);
	c.snap_angle_deg = 44.0f;
	CHECK_NEAR(vr_turn_frame(&t, &c, -1.0f, 0.01f, &g).delta, 45.0f * DEG, 1e-6);

	// A closed gate turns nothing, but the stick is still read: held while the
	// gate opens, it does not snap until re-armed
	vr_turn_reset(&t);
	c = config(SFVR_TURN_SNAP);
	vr_turn_gate_t closed = { false, 0.0f, VR_TURN_UNLIMITED, VR_TURN_UNLIMITED };
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &closed).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 0.0f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &g).delta, -30.0f * DEG, 1e-6);

	// Off: nothing
	vr_turn_reset(&t);
	c = config(SFVR_TURN_OFF);
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &g).delta, 0.0, 1e-9);
}

static void test_smooth_rate(void) {
	vr_turn_t t;
	vr_turn_reset(&t);
	sfvr_turn_config c = config(SFVR_TURN_SMOOTH);
	vr_turn_gate_t g = open_gate(0.0f);
	// Full deflection for 1 s at 120 Hz: 120 degrees right
	double total = 0.0;
	for (int i = 0; i < 120; i++) {
		vr_turn_step_t s = vr_turn_frame(&t, &c, 1.0f, 1.0f / 120.0f, &g);
		CHECK_INT(s.kind, VR_TURN_STEP_SMOOTH);
		total += s.delta;
	}
	CHECK_NEAR(total, -120.0 * DEG, 1e-4);
	// 0.6: past the 0.2 deadzone, half the travel: 60 deg/s, left
	CHECK_NEAR(vr_turn_frame(&t, &c, -0.6f, 0.5f / 60.0f, &g).delta, 60.0f * DEG / 120.0f, 1e-6);
	// Deadzone, and a hitch capped at 0.1 s
	CHECK_NEAR(vr_turn_frame(&t, &c, 0.19f, 0.01f, &g).delta, 0.0, 1e-9);
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.5f, &g).delta, -12.0f * DEG, 1e-6);
	// Speed 240
	c.smooth_speed_dps = 240.0f;
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &g).delta, -2.4f * DEG, 1e-6);
}

static void test_limits(void) {
	vr_turn_t t;
	vr_turn_reset(&t);
	sfvr_turn_config c = config(SFVR_TURN_SNAP);
	// The radial camera can turn 20 degrees right from here: the snap is cut
	vr_turn_gate_t g = { true, 0.0f, 100.0f * DEG, 20.0f * DEG };
	vr_turn_step_t s = vr_turn_frame(&t, &c, 1.0f, 0.01f, &g);
	CHECK_NEAR(s.delta, -20.0f * DEG, 1e-6);
	CHECK(s.limited);
	// What is already queued counts against the limit
	vr_turn_reset(&t);
	g.queued = -18.0f * DEG;
	s = vr_turn_frame(&t, &c, 1.0f, 0.01f, &g);
	CHECK_NEAR(s.delta, 0.0, 1e-9); // 2 degrees left: below the 5 degree minimum, dropped
	CHECK(s.limited);
	// Left is free
	vr_turn_reset(&t);
	s = vr_turn_frame(&t, &c, -1.0f, 0.01f, &g);
	CHECK_NEAR(s.delta, 30.0f * DEG, 1e-6);
	CHECK(!s.limited);
	// A queue past a limit that has moved never turns further out
	vr_turn_reset(&t);
	vr_turn_gate_t past = { true, -25.0f * DEG, 100.0f * DEG, 20.0f * DEG };
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.01f, &past).delta, 0.0, 1e-9);
	// Smooth turning stops at the limit
	vr_turn_reset(&t);
	c = config(SFVR_TURN_SMOOTH);
	vr_turn_gate_t sg = { true, 0.0f, 0.0f, 1.0f * DEG };
	CHECK_NEAR(vr_turn_frame(&t, &c, 1.0f, 0.1f, &sg).delta, -1.0f * DEG, 1e-6);
	CHECK(vr_turn_frame(&t, &c, -1.0f, 0.1f, &sg).delta == 0.0f);
}

static void test_take(void) {
	float q = 30.0f * DEG, refused = -1.0f;
	CHECK_NEAR(vr_turn_take(&q, true, VR_TURN_UNLIMITED, VR_TURN_UNLIMITED, &refused), 30.0f * DEG, 1e-7);
	CHECK(q == 0.0f);
	CHECK(refused == 0.0f);
	q = -30.0f * DEG;
	CHECK_NEAR(vr_turn_take(&q, true, 100.0f * DEG, 10.0f * DEG, &refused), -10.0f * DEG, 1e-7);
	CHECK_NEAR(refused, -20.0f * DEG, 1e-6);
	CHECK(q == 0.0f);
	q = 5.0f * DEG;
	CHECK(vr_turn_take(&q, false, VR_TURN_UNLIMITED, VR_TURN_UNLIMITED, &refused) == 0.0f);
	CHECK_NEAR(refused, 5.0f * DEG, 1e-7);
	CHECK(q == 0.0f);
}

// SM64: atan2s(z, x) is the yaw of (x, z); sins/coss
static float sm64_yaw(float x, float z) {
	return atan2f(x, z);
}

static void test_angles(void) {
	CHECK_INT(vr_turn_angle_s16(VR_TURN_PI / 2.0f), 0x4000);
	CHECK_INT(vr_turn_angle_s16(-VR_TURN_PI / 2.0f), -0x4000);
	CHECK_INT(vr_turn_angle_s16(30.0f * DEG), 5461);
	CHECK_INT(vr_turn_angle_s16(VR_TURN_PI), -0x8000);
	CHECK_INT(vr_turn_angle_s16(2.0f * VR_TURN_PI + 30.0f * DEG), 5461);
	CHECK_NEAR(vr_turn_s16_angle(0x4000), VR_TURN_PI / 2.0f, 1e-6);
	CHECK_NEAR(vr_turn_s16_angle(0xC000), -VR_TURN_PI / 2.0f, 1e-6);
	CHECK_NEAR(vr_wrap_pi(3.5f * VR_TURN_PI), -0.5f * VR_TURN_PI, 1e-5);

	// Rotating about a pivot adds to the SM64 yaw from the pivot, and keeps the distance
	float pivot[3] = { 100.0f, 7.0f, -50.0f };
	float a = 0.3f, d = 40.0f * DEG;
	float p[3] = { pivot[0] + 800.0f * sinf(a), 300.0f, pivot[2] + 800.0f * cosf(a) };
	vr_turn_rotate_about(p, pivot, d);
	CHECK_NEAR(sm64_yaw(p[0] - pivot[0], p[2] - pivot[2]), a + d, 1e-5);
	CHECK_NEAR(hypotf(p[0] - pivot[0], p[2] - pivot[2]), 800.0f, 1e-3);
	CHECK_NEAR(p[1], 300.0f, 1e-9);

	// The same sense as the view's heading (vr_yaw_of): rotating a camera's
	// forward by d turns its heading by d
	float fwd[3] = { 0.0f, 0.0f, -1.0f }, origin[3] = { 0, 0, 0 };
	vr_turn_rotate_about(fwd, origin, d);
	float q[4];
	vr_quat_from_yaw(d, q);
	float m[9], f2[3];
	vr_mat3_from_quat(q, m);
	const float minus_z[3] = { 0.0f, 0.0f, -1.0f };
	vr_mat3_mulv(m, minus_z, f2);
	CHECK_NEAR(fwd[0], f2[0], 1e-6);
	CHECK_NEAR(fwd[2], f2[2], 1e-6);
}

// SM64's update_mario_joystick_inputs with the VR offset (mario.c)
static int16_t intended_yaw(int stick_x, int stick_y, int16_t camera_yaw, float offset) {
	float a = sm64_yaw((float)stick_x, (float)-stick_y); // atan2s(-stickY, stickX)
	int16_t s = vr_turn_angle_s16(a);
	return (int16_t)(s + camera_yaw + vr_turn_angle_s16(offset));
}

// The world direction of an SM64 yaw, and the heading (vr_yaw_of) of a direction
static float heading_of_yaw(int16_t yaw) {
	float a = vr_turn_s16_angle(yaw);
	float x = sinf(a), z = cosf(a);
	return atan2f(-x, -z);
}

static void test_stick_frame(void) {
	// The camera behind Mario looks along -Z (its SM64 yaw 0, heading 0)
	const int16_t cam = 0;
	// Forward walks where the camera looks; right walks to the camera's right
	CHECK_NEAR(heading_of_yaw(intended_yaw(0, 64, cam, 0.0f)), 0.0, 1e-3);
	CHECK_NEAR(heading_of_yaw(intended_yaw(64, 0, cam, 0.0f)), -VR_TURN_PI / 2.0f, 1e-3);
	// The wearer looks 90 degrees left of the view's forward: forward walks there
	float head_yaw = 0.4f + VR_TURN_PI / 2.0f, recentre = 0.4f;
	float ref = vr_move_reference(0, head_yaw, NULL, NULL, recentre);
	CHECK_NEAR(ref, VR_TURN_PI / 2.0f, 1e-5);
	CHECK_NEAR(heading_of_yaw(intended_yaw(0, 64, cam, ref)), VR_TURN_PI / 2.0f, 1e-3);
	// A snap right of 30 degrees not yet taken by the camera counts too
	CHECK_NEAR(heading_of_yaw(intended_yaw(0, 64, cam, -30.0f * DEG)), -30.0f * DEG, 1e-3);
	// Once taken, the camera's yaw has it and the queue is empty: the same
	CHECK_NEAR(heading_of_yaw(intended_yaw(0, 64, vr_turn_angle_s16(-30.0f * DEG), 0.0f)), -30.0f * DEG, 1e-3);
	// A camera turned 135 degrees
	int16_t c135 = vr_turn_angle_s16(135.0f * DEG);
	CHECK_NEAR(heading_of_yaw(intended_yaw(0, 64, c135, 0.0f)), 135.0f * DEG, 1e-3);

	// Hands: left or right aim, the head when a hand has no pose
	float hands[2] = { 1.0f, -1.0f };
	bool valid[2] = { true, false };
	CHECK_NEAR(vr_move_reference(1, 0.2f, hands, valid, 0.0f), 1.0f, 1e-6);
	CHECK_NEAR(vr_move_reference(2, 0.2f, hands, valid, 0.0f), 0.2f, 1e-6);
	CHECK_NEAR(vr_move_reference(0, 0.2f, hands, valid, 0.0f), 0.2f, 1e-6);
	CHECK_NEAR(vr_move_reference(0, 3.0f, NULL, NULL, -3.0f), 6.0f - 2.0f * VR_TURN_PI, 1e-5);
}

static void test_vignette(void) {
	CHECK_NEAR(vr_vignette_target(4.0f, 0.6f), 0.0, 1e-9);
	CHECK_NEAR(vr_vignette_target(-120.0f, 0.6f), 0.6, 1e-6);
	CHECK_NEAR(vr_vignette_target(17.5f, 0.6f), 0.3, 1e-6);
	CHECK_NEAR(vr_vignette_target(120.0f, 0.0f), 0.0, 1e-9);
	// Closes in 0.2 s, opens in 0.5 s
	float a = 0.0f;
	for (int i = 0; i < 24; i++) {
		a = vr_vignette_step(a, 0.6f, 0.6f, 1.0f / 120.0f);
	}
	CHECK_NEAR(a, 0.6, 1e-5);
	a = 0.0f;
	for (int i = 0; i < 12; i++) {
		a = vr_vignette_step(a, 0.6f, 0.6f, 1.0f / 120.0f);
	}
	CHECK_NEAR(a, 0.3, 1e-5);
	for (int i = 0; i < 30; i++) {
		a = vr_vignette_step(a, 0.0f, 0.6f, 1.0f / 120.0f);
	}
	CHECK_NEAR(a, 0.0, 1e-5);
	a = 0.6f;
	for (int i = 0; i < 30; i++) {
		a = vr_vignette_step(a, 0.0f, 0.6f, 1.0f / 120.0f);
	}
	CHECK_NEAR(a, 0.3, 1e-5);
}

static void test_heading_track(void) {
	vr_heading_t h;
	memset(&h, 0, sizeof(h));
	CHECK_NEAR(vr_heading_track(&h, 3.0f), 3.0, 1e-6);
	CHECK_NEAR(vr_heading_track(&h, -3.0f), 2.0f * VR_TURN_PI - 3.0f, 1e-5);
	CHECK_NEAR(vr_heading_rate(0.0f, 1.0f * DEG, 1.0f / 120.0f), 120.0, 1e-3);
	CHECK_NEAR(vr_heading_rate(0.0f, 30.0f * DEG, 1.0f / 120.0f), 0.0, 1e-9); // a snap is not motion
	CHECK_NEAR(vr_heading_rate(0.0f, 1.0f * DEG, 0.0f), 0.0, 1e-9);
}

static void test_diag_stick(void) {
	vr_turn_t t;
	vr_turn_reset(&t);
	CHECK_NEAR(vr_turn_diag_stick(&t, 0.0f, 0.3f, 10.0, true), 0.3, 1e-6);
	CHECK_NEAR(vr_turn_diag_stick(&t, 1.0f, 0.3f, 10.0, false), 0.3, 1e-6);
	CHECK_NEAR(vr_turn_diag_stick(&t, 1.0f, 0.3f, 10.0, true), 1.0, 1e-9);
	CHECK_NEAR(vr_turn_diag_stick(&t, 1.0f, 0.3f, 13.9, true), 1.0, 1e-9);
	CHECK_NEAR(vr_turn_diag_stick(&t, 1.0f, 0.3f, 14.1, true), 0.0, 1e-9);
	CHECK_NEAR(vr_turn_diag_stick(&t, 1.0f, 0.3f, 18.1, true), 1.0, 1e-9);
}


// -----------------------------------------------------------------------------
// The protocol, simulated

// mtxf_lookat with roll 0, as Mat4 memory
static void lookat(const float from[3], const float to[3], float out[16]) {
	float back[3] = { from[0] - to[0], from[1] - to[1], from[2] - to[2] };
	float l = sqrtf(back[0] * back[0] + back[1] * back[1] + back[2] * back[2]);
	for (int i = 0; i < 3; i++) back[i] /= l;
	float right[3] = { back[2], 0.0f, -back[0] }; // up x back with up = +Y
	float rl = sqrtf(right[0] * right[0] + right[2] * right[2]);
	right[0] /= rl;
	right[2] /= rl;
	float up[3] = { back[1] * right[2] - back[2] * right[1], back[2] * right[0] - back[0] * right[2],
		back[0] * right[1] - back[1] * right[0] };
	for (int i = 0; i < 3; i++) {
		out[i * 4 + 0] = right[i];
		out[i * 4 + 1] = up[i];
		out[i * 4 + 2] = back[i];
		out[i * 4 + 3] = 0.0f;
	}
	out[12] = -(right[0] * from[0] + right[1] * from[1] + right[2] * from[2]);
	out[13] = -(up[0] * from[0] + up[1] * from[1] + up[2] * from[2]);
	out[14] = -(back[0] * from[0] + back[1] * from[1] + back[2] * from[2]);
	out[15] = 1.0f;
}

// The game's side, in camera.c's and the renderer's order
typedef struct {
	float mario[3]; // Mario this tick
	float pos[3], focus[3]; // Lakitu
	float node_pos[3], node_focus[3], node_prev_pos[3], node_prev_focus[3]; // the camera node
	float pivot[3], pivot_prev[3]; // Mario at the last two renders (gVrCamTurn.pivot)
	float applied, applied_pivot[3]; // sVrTurnApplied
	float queued; // gVrCamTurn.queued
	float own; // the game camera's own turn per tick (auto-rotation), radians
	float walk[3]; // Mario's motion per tick
} sim_t;

static void sim_init(sim_t *s, float own_dps) {
	memset(s, 0, sizeof(*s));
	float m[3] = { 1000.0f, 0.0f, -2000.0f };
	float p[3] = { m[0] + 300.0f, 400.0f, m[2] + 740.0f };
	float f[3] = { m[0], 125.0f, m[2] };
	memcpy(s->mario, m, sizeof(m));
	memcpy(s->pivot, m, sizeof(m));
	memcpy(s->pivot_prev, m, sizeof(m));
	memcpy(s->pos, p, sizeof(p));
	memcpy(s->node_pos, p, sizeof(p));
	memcpy(s->node_prev_pos, p, sizeof(p));
	memcpy(s->focus, f, sizeof(f));
	memcpy(s->node_focus, f, sizeof(f));
	memcpy(s->node_prev_focus, f, sizeof(f));
	s->own = own_dps / 30.0f * DEG;
	s->walk[0] = 6.0f;
	s->walk[2] = -9.0f;
}

static void sim_tick(sim_t *s) {
	// Objects first: Mario moves
	float before[3];
	memcpy(before, s->mario, sizeof(before));
	for (int i = 0; i < 3; i++) {
		s->mario[i] += s->walk[i];
	}
	// update_camera: vr_camera_take_turn, about Mario at the last render
	float refused;
	float turn = vr_turn_take(&s->queued, true, VR_TURN_UNLIMITED, VR_TURN_UNLIMITED, &refused);
	if (turn != 0.0f) {
		vr_turn_rotate_about(s->pos, s->pivot, turn);
		vr_turn_rotate_about(s->focus, s->pivot, turn);
		s->applied += turn;
		memcpy(s->applied_pivot, s->pivot, sizeof(s->pivot));
	}
	// The mode: its own turn about Mario, then following him
	vr_turn_rotate_about(s->pos, before, s->own);
	vr_turn_rotate_about(s->focus, before, s->own);
	for (int i = 0; i < 3; i++) {
		s->pos[i] += s->walk[i];
		s->focus[i] += s->walk[i];
	}
	// The render: geo_process_camera, then update_graph_node_camera
	memcpy(s->node_prev_pos, s->node_pos, sizeof(s->node_pos));
	memcpy(s->node_prev_focus, s->node_focus, sizeof(s->node_focus));
	if (s->applied != 0.0f) {
		vr_turn_rotate_about(s->node_prev_pos, s->applied_pivot, s->applied);
		vr_turn_rotate_about(s->node_prev_focus, s->applied_pivot, s->applied);
		s->applied = 0.0f;
	}
	memcpy(s->node_pos, s->pos, sizeof(s->pos));
	memcpy(s->node_focus, s->focus, sizeof(s->focus));
	memcpy(s->pivot_prev, s->pivot, sizeof(s->pivot));
	memcpy(s->pivot, s->mario, sizeof(s->mario));
}

// patch_mtx_interpolated, then the renderer's shown camera
static void sim_view(const sim_t *s, float alpha, vr_game_cam_t *shown, float pivot[3]) {
	float pos[3], focus[3], view[16];
	for (int i = 0; i < 3; i++) {
		pos[i] = s->node_prev_pos[i] + (s->node_pos[i] - s->node_prev_pos[i]) * alpha;
		focus[i] = s->node_prev_focus[i] + (s->node_focus[i] - s->node_prev_focus[i]) * alpha;
		pivot[i] = s->pivot_prev[i] + (s->pivot[i] - s->pivot_prev[i]) * alpha;
	}
	lookat(pos, focus, view);
	vr_game_cam_t cam;
	CHECK(vr_game_cam_from_view(view, &cam));
	vr_game_cam_orbit(&cam, pivot, s->queued, shown);
}

static float hdist(const float a[3], const float b[3]) {
	return hypotf(a[0] - b[0], a[2] - b[2]);
}

// A snap at frame 10, a smooth turn right at 120 deg/s from frame 40 to 99
// (and a smooth turn left from 140 to 159), against the same run without
// turning; the game camera also turns by itself at `own_dps` and Mario walks.
// Every frame the shown heading is the unturned one plus everything asked so
// far: a snap shows whole in its own frame, the camera taking it a tick later
// adds nothing (no double turn, no swing), and the eye stays on its orbit
static void run_protocol(float own_dps) {
	sim_t turned, plain;
	sim_init(&turned, own_dps);
	sim_init(&plain, own_dps);
	float asked = 0.0f;
	float prev_pos[3] = { 0 }, prev_plain[3] = { 0 };
	int worst_frame = -1;
	float worst = 0.0f;
	for (int f = 0; f < 200; f++) {
		if (f % 4 == 0) {
			sim_tick(&turned);
			sim_tick(&plain);
		}
		float alpha = (float)(f % 4 + 1) / 4.0f;
		// The renderer adds the stick's turn, then draws (comfort_frame)
		float request = 0.0f;
		if (f == 10) {
			request = -30.0f * DEG;
		} else if (f >= 40 && f < 100) {
			request = -1.0f * DEG; // 120 deg/s at 120 Hz
		} else if (f >= 140 && f < 160) {
			request = 1.0f * DEG;
		}
		turned.queued += request;
		asked += request;

		vr_game_cam_t a, b;
		float pa[3], pb[3];
		sim_view(&turned, alpha, &a, pa);
		sim_view(&plain, alpha, &b, pb);
		float err = vr_wrap_pi(vr_game_cam_heading(&a) - vr_game_cam_heading(&b) - asked);
		if (fabsf(err) > worst) {
			worst = fabsf(err);
			worst_frame = f;
		}
		// On the orbit: the same distance from Mario as without turning
		CHECK_NEAR(hdist(a.position, pa), hdist(b.position, pb), 0.05);
		CHECK_NEAR(a.position[1], b.position[1], 1e-3);
		// No pop: in frames without a request the eye moves about Mario exactly
		// as far as in the plain run (its offset from Mario turns rigidly)
		float ra[3], rb[3];
		for (int i = 0; i < 3; i++) {
			ra[i] = a.position[i] - pa[i];
			rb[i] = b.position[i] - pb[i];
		}
		if (f > 0 && request == 0.0f) {
			float da = sqrtf((ra[0] - prev_pos[0]) * (ra[0] - prev_pos[0]) + (ra[2] - prev_pos[2]) * (ra[2] - prev_pos[2]));
			float db = sqrtf((rb[0] - prev_plain[0]) * (rb[0] - prev_plain[0]) + (rb[2] - prev_plain[2]) * (rb[2] - prev_plain[2]));
			CHECK(fabsf(da - db) < 0.05f);
		}
		memcpy(prev_pos, ra, sizeof(prev_pos));
		memcpy(prev_plain, rb, sizeof(prev_plain));
	}
	CHECK(worst < 3e-4f);
	if (worst >= 3e-4f) {
		printf("  worst heading error %.6f rad at frame %d\n", worst, worst_frame);
	}
	// Everything asked was taken by the game camera
	CHECK_NEAR(turned.queued, 0.0, 1e-9);
	CHECK_NEAR(asked, -30.0f * DEG - 60.0f * DEG + 20.0f * DEG, 1e-4);
}

static void test_protocol_still_camera(void) {
	run_protocol(0.0f);
}

static void test_protocol_with_the_game_turning(void) {
	// Auto-rotation behind Mario runs at about 32 deg/s (0xC0 a tick)
	run_protocol(31.6f);
	run_protocol(-31.6f);
}

static void test_comfort_from_settings(void) {
	vr_settings_t s;
	vr_settings_defaults(&s);
	vr_comfort_t c;
	vr_comfort_from(&c, &s);
	CHECK_INT(c.turn.mode, SFVR_TURN_SNAP);
	CHECK_NEAR(c.turn.snap_angle_deg, 30, 1e-6);
	CHECK_NEAR(c.turn.smooth_speed_dps, 120, 1e-6);
	CHECK_NEAR(c.turn.arm_threshold, 0.7, 1e-6);
	CHECK_NEAR(c.turn.rearm_threshold, 0.3, 1e-6);
	CHECK(!c.vignette);
	CHECK_NEAR(c.vignette_strength, 0.6, 1e-6);
	CHECK_INT(c.move_relative, 0);
	CHECK(!c.camera_shake);
	CHECK(!c.seated);
	CHECK_NEAR(c.height_offset, 0, 1e-9);
	CHECK_NEAR(c.head_translation, 1, 1e-9);
	CHECK_NEAR(c.world_scale, 104, 1e-6);
	CHECK(c.level_pitch);
	CHECK_NEAR(c.camera_closer, 0, 1e-9);
	CHECK_NEAR(c.camera_lower, 0, 1e-9);
	CHECK_NEAR(c.diag_turn_stick, 0, 1e-9);
	vr_settings_set(&s, "turn_mode", 1);
	vr_settings_set(&s, "camera_closer", 0.3);
	vr_comfort_from(&c, &s);
	CHECK_INT(c.turn.mode, SFVR_TURN_SMOOTH);
	CHECK_NEAR(c.camera_closer, 0.3, 1e-6);
	vr_settings_set(&s, "turn_mode", 2);
	vr_comfort_from(&c, &s);
	CHECK_INT(c.turn.mode, SFVR_TURN_OFF);
}

int main(void) {
	RUN(test_comfort_from_settings);
	RUN(test_snap_hysteresis);
	RUN(test_smooth_rate);
	RUN(test_limits);
	RUN(test_take);
	RUN(test_angles);
	RUN(test_stick_frame);
	RUN(test_vignette);
	RUN(test_heading_track);
	RUN(test_diag_stick);
	RUN(test_protocol_still_camera);
	RUN(test_protocol_with_the_game_turning);
	return SFVR_TEST_RESULT("vr_turn");
}
