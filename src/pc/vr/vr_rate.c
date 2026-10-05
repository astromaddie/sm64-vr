#include "vr_rate.h"

void vr_rate_init(vr_rate_t *r) {
	r->target_hz = 0;
	r->started = false;
	r->start_s = 0;
	r->frames = 0;
	r->low_windows = 0;
	r->last_fps = 0;
}

void vr_rate_set_target(vr_rate_t *r, int target_hz) {
	r->target_hz = target_hz;
	r->started = false;
	r->frames = 0;
	r->low_windows = 0;
}

vr_rate_result_t vr_rate_frame(vr_rate_t *r, double now_s, bool counting) {
	if (!counting || r->target_hz <= 0) {
		r->started = false;
		r->frames = 0;
		r->low_windows = 0;
		return VR_RATE_NONE;
	}
	if (!r->started) {
		r->started = true;
		r->start_s = now_s;
		r->frames = 0;
	}
	r->frames++;
	double elapsed = now_s - r->start_s;
	if (elapsed < VR_RATE_WINDOW_S) {
		return VR_RATE_NONE;
	}

	r->last_fps = (double)r->frames / elapsed;
	r->started = false; // the next frame opens the next window
	r->frames = 0;
	if (r->last_fps >= VR_RATE_LOW_FRACTION * (double)r->target_hz) {
		r->low_windows = 0;
		return VR_RATE_WINDOW_OK;
	}
	if (++r->low_windows >= VR_RATE_LOW_WINDOWS) {
		r->low_windows = 0;
		return VR_RATE_FALL_BACK;
	}
	return VR_RATE_WINDOW_LOW;
}
