#include <math.h>

#include "vr_math.h"
#include "vr_phosphor.h"

#define PI_F 3.1415926f

static float triads_per_rad(float triad_deg) {
	return 180.0f / (PI_F * (triad_deg > 0.01f ? triad_deg : 0.01f));
}

static float clamp01(float v) {
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

void vr_phosphor_constants(const float orientation[4], const float fov[4], float triad_deg,
	float strength, vr_phosphor_t *out) {
	vr_mat3_from_quat(orientation, out->room);
	out->tan_left = tanf(fov[0]);
	out->tan_right = tanf(fov[1]);
	out->tan_up = tanf(fov[2]);
	out->tan_down = tanf(fov[3]);
	out->triads_per_rad = triads_per_rad(triad_deg);
	out->strength = clamp01(strength);
}

void vr_phosphor_mask(const float d[3], float triad_deg, float strength, float out[3]) {
	float tpr = triads_per_rad(triad_deg);
	float flat = d[0] * d[0] + d[2] * d[2];
	float heading = (float)atan2(d[0], -d[2]);
	float height = d[1] / sqrtf(flat > 1e-6f ? flat : 1e-6f);
	float scan = 0.5f + 0.5f * cosf(6.2831853f * height * tpr * VR_PHOSPHOR_SCAN_PER_TRIAD);
	float rows = (1.0f + (scan - 1.0f) * VR_PHOSPHOR_SCAN_DEPTH) * VR_PHOSPHOR_GAIN;
	float s = clamp01(strength);
	for (int i = 0; i < 3; i++) {
		float stripe = 0.5f + 0.5f * cosf(6.2831853f * (heading * tpr - (float)i / 3.0f));
		out[i] = 1.0f + (stripe * rows - 1.0f) * s;
	}
}
