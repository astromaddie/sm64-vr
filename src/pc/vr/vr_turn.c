#include <math.h>
#include <string.h>

#include "vr_turn.h"

void vr_comfort_from(vr_comfort_t *out, const vr_settings_t *s) {
	memset(out, 0, sizeof(*out));
	sfvr_turn_config_default(&out->turn);
	int mode = (int)vr_settings_get(s, "turn_mode");
	out->turn.mode = mode == 1 ? SFVR_TURN_SMOOTH : (mode == 2 ? SFVR_TURN_OFF : SFVR_TURN_SNAP);
	out->turn.snap_angle_deg = (float)vr_settings_get(s, "snap_angle");
	out->turn.smooth_speed_dps = (float)vr_settings_get(s, "smooth_turn_speed");
	out->vignette = vr_settings_get(s, "vignette") != 0;
	out->vignette_strength = (float)vr_settings_get(s, "vignette_strength");
	out->move_relative = (int)vr_settings_get(s, "move_relative");
	out->height_offset = (float)vr_settings_get(s, "height_offset");
	out->seated = vr_settings_get(s, "seated") != 0;
	out->head_translation = (float)vr_settings_get(s, "head_translation");
	out->camera_shake = vr_settings_get(s, "camera_shake") != 0;
	out->world_scale = (float)vr_settings_get(s, "world_scale");
	out->level_pitch = vr_settings_get(s, "level_pitch") != 0;
	out->camera_closer = (float)vr_settings_get(s, "camera_closer");
	out->camera_lower = (float)vr_settings_get(s, "camera_lower");
	out->diag_turn_stick = (float)vr_settings_get(s, "diag_turn_stick");
}


// -----------------------------------------------------------------------------
// Angles

float vr_wrap_pi(float a) {
	if (!isfinite(a)) {
		return 0.0f;
	}
	const float two_pi = 2.0f * VR_TURN_PI;
	a = fmodf(a + VR_TURN_PI, two_pi);
	if (a < 0.0f) {
		a += two_pi;
	}
	return a - VR_TURN_PI;
}

int16_t vr_turn_angle_s16(float heading) {
	if (!isfinite(heading)) {
		return 0;
	}
	double units = (double)heading * (32768.0 / 3.14159265358979323846);
	long r = lrint(fmod(units, 65536.0));
	return (int16_t)(uint16_t)(r & 0xFFFF);
}

float vr_turn_s16_angle(int32_t units) {
	return (float)((double)(int16_t)(uint16_t)(units & 0xFFFF) * (3.14159265358979323846 / 32768.0));
}

void vr_turn_rotate_about(float p[3], const float pivot[3], float heading) {
	// A rotation about +Y by `heading`: +Z goes toward +X, as SM64's yaw
	float c = cosf(heading), s = sinf(heading);
	float dx = p[0] - pivot[0], dz = p[2] - pivot[2];
	p[0] = pivot[0] + dx * c + dz * s;
	p[2] = pivot[2] - dx * s + dz * c;
}


// -----------------------------------------------------------------------------
// The renderer's side

void vr_turn_reset(vr_turn_t *t) {
	memset(t, 0, sizeof(*t));
	sfvr_turn_reset(&t->stick);
}

static float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

vr_turn_step_t vr_turn_frame(vr_turn_t *t, const sfvr_turn_config *cfg, float stick_x, float dt,
	const vr_turn_gate_t *gate) {
	vr_turn_step_t step = { 0.0f, VR_TURN_STEP_NONE, false, false };
	// sfvr: degrees, positive turning right; here positive turns left
	float deg = sfvr_turn_update(&t->stick, cfg, stick_x, dt);
	if (deg == 0.0f) {
		return step;
	}
	step.kind = (cfg && cfg->mode == SFVR_TURN_SMOOTH) ? VR_TURN_STEP_SMOOTH : VR_TURN_STEP_SNAP;
	if (!gate || !gate->open) {
		step.closed = true;
		return step;
	}
	float want = -deg * VR_TURN_DEG;
	float left = gate->limit_left > 0.0f ? gate->limit_left : 0.0f;
	float right = gate->limit_right > 0.0f ? gate->limit_right : 0.0f;
	float total = clampf(gate->queued + want, -right, left);
	float got = total - gate->queued;
	// The queue may already sit past a limit that moved: never turn further out
	if ((want > 0.0f && got < 0.0f) || (want < 0.0f && got > 0.0f)) {
		got = 0.0f;
	}
	if (fabsf(got) < fabsf(want) - 1e-6f) {
		step.limited = true;
		if (step.kind == VR_TURN_STEP_SNAP && fabsf(got) < VR_TURN_MIN_SNAP) {
			got = 0.0f;
		}
	}
	step.delta = got;
	return step;
}

float vr_turn_diag_stick(vr_turn_t *t, float deflection, float real_stick, double now_s, bool active) {
	if (deflection == 0.0f || !active) {
		return real_stick;
	}
	if (!t->diag_started) {
		t->diag_started = true;
		t->diag_start_s = now_s;
	}
	double phase = fmod(now_s - t->diag_start_s, 8.0);
	return phase < 4.0 ? clampf(deflection, -1.0f, 1.0f) : 0.0f;
}


// -----------------------------------------------------------------------------
// The game camera's side

float vr_turn_take(float *queued, bool allowed, float limit_left, float limit_right, float *refused) {
	float q = (queued && isfinite(*queued)) ? *queued : 0.0f;
	float took = 0.0f;
	if (allowed) {
		took = clampf(q, -(limit_right > 0.0f ? limit_right : 0.0f), limit_left > 0.0f ? limit_left : 0.0f);
	}
	if (refused) {
		*refused = q - took;
	}
	if (queued) {
		*queued = 0.0f;
	}
	return took;
}


// -----------------------------------------------------------------------------
// Movement

float vr_move_reference(int move_relative, float head_yaw, const float hand_yaw[2], const bool hand_valid[2],
	float recentre_yaw) {
	float yaw = head_yaw;
	if ((move_relative == 1 || move_relative == 2) && hand_yaw && hand_valid) {
		int hand = move_relative - 1;
		if (hand_valid[hand]) {
			yaw = hand_yaw[hand];
		}
	}
	return vr_wrap_pi(yaw - recentre_yaw);
}


// -----------------------------------------------------------------------------
// Vignette and heading rate

float vr_vignette_target(float yaw_rate_dps, float strength) {
	float r = fabsf(yaw_rate_dps);
	if (!isfinite(r) || !(strength > 0.0f)) {
		return 0.0f;
	}
	float t = clampf((r - VR_VIGNETTE_RATE_MIN) / (VR_VIGNETTE_RATE_FULL - VR_VIGNETTE_RATE_MIN), 0.0f, 1.0f);
	return t * clampf(strength, 0.0f, 1.0f);
}

float vr_vignette_step(float amount, float target, float strength, float dt) {
	if (!(dt > 0.0f)) {
		return amount;
	}
	float s = clampf(strength, 0.0f, 1.0f);
	if (s <= 0.0f) {
		return 0.0f;
	}
	if (target > amount) {
		amount += s * dt / VR_VIGNETTE_CLOSE_S;
		return amount > target ? target : amount;
	}
	amount -= s * dt / VR_VIGNETTE_OPEN_S;
	return amount < target ? target : amount;
}

float vr_heading_track(vr_heading_t *h, float raw) {
	if (!h->valid) {
		h->valid = true;
		h->unwrapped = raw;
	} else {
		h->unwrapped += vr_wrap_pi(raw - h->raw);
	}
	h->raw = raw;
	return h->unwrapped;
}

float vr_heading_rate(float prev, float cur, float dt) {
	float d = cur - prev;
	if (!(dt > 0.0f) || fabsf(d) > VR_TURN_JUMP) {
		return 0.0f;
	}
	return d / dt / VR_TURN_DEG;
}
