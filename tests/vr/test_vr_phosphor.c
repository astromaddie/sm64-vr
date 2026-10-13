#include <math.h>

#include "sfvr_test.h"
#include "vr_math.h"
#include "vr_phosphor.h"

#define PI 3.14159265358979f

static void test_off(void) {
	float d[3] = { 0.3f, 0.1f, -1.0f }, m[3];
	vr_phosphor_mask(d, 0.25f, 0.0f, m);
	CHECK_NEAR(m[0], 1.0, 1e-6);
	CHECK_NEAR(m[1], 1.0, 1e-6);
	CHECK_NEAR(m[2], 1.0, 1e-6);
}

static void test_stripes(void) {
	// The red stripe is brightest dead ahead; green a third of a triad on, blue two thirds.
	float third = 0.25f / 3.0f * PI / 180.0f;
	float ahead[3] = { 0, 0, -1 }, on[3] = { sinf(third), 0, -cosf(third) }, r[3], g[3];
	vr_phosphor_mask(ahead, 0.25f, 1.0f, r);
	vr_phosphor_mask(on, 0.25f, 1.0f, g);
	CHECK(r[0] > r[1] && r[0] > r[2]);
	CHECK(g[1] > g[0] && g[1] > g[2]);
	CHECK_NEAR(r[0], g[1], 1e-3);
}

static void test_light_kept(void) {
	// Dimmer than the picture on average over heading and height, not by half
	double sum = 0;
	int count = 0;
	for (int i = 0; i < 400; i++) {
		for (int j = 0; j < 400; j++) {
			float a = (float)i * 1.0e-4f, h = (float)j * 1.3e-4f;
			float d[3] = { sinf(a), h, -cosf(a) }, m[3];
			vr_phosphor_mask(d, 0.25f, 1.0f, m);
			sum += (m[0] + m[1] + m[2]) / 3.0;
			count++;
		}
	}
	CHECK(sum / count > 0.55 && sum / count < 0.8);
}

static void test_constants(void) {
	// A pixel's direction through the constants is the eye's rotation of its tangent ray
	float q[4], fov[4] = { atanf(-1.2f), atanf(0.9f), atanf(1.0f), atanf(-1.1f) };
	vr_quat_from_yaw(0.7f, q);
	vr_phosphor_t c;
	vr_phosphor_constants(q, fov, 0.25f, 0.6f, &c);
	float u = 0.25f, v = 0.75f; // v from the bottom, as gl_FragCoord
	float ray[3] = { c.tan_left + (c.tan_right - c.tan_left) * u, c.tan_down + (c.tan_up - c.tan_down) * v, -1.0f };
	float want[3], got[3], m[9];
	vr_mat3_from_quat(q, m);
	vr_mat3_mulv(m, ray, want);
	vr_mat3_mulv(c.room, ray, got);
	for (int i = 0; i < 3; i++) {
		CHECK_NEAR(got[i], want[i], 1e-5);
	}
	// yawed 0.7 rad left: straight ahead of the eye (centre ray) looks left of room forward
	float fwd[3] = { 0, 0, -1 }, d[3];
	vr_mat3_mulv(c.room, fwd, d);
	CHECK_NEAR(d[0], -sinf(0.7f), 1e-5);
	CHECK_NEAR(d[2], -cosf(0.7f), 1e-5);
	CHECK_NEAR(c.tan_left, -1.2, 1e-5);
	CHECK_NEAR(c.tan_right, 0.9, 1e-5);
	CHECK_NEAR(c.tan_up, 1.0, 1e-5);
	CHECK_NEAR(c.tan_down, -1.1, 1e-5);
	CHECK_NEAR(c.strength, 0.6, 1e-6);
	CHECK_NEAR(c.triads_per_rad, 180.0 / (3.1415926 * 0.25), 1e-2);

	// strength and size are clamped to what the mask can use
	vr_phosphor_constants(q, fov, 0.25f, 3.0f, &c);
	CHECK_NEAR(c.strength, 1.0, 0.0);
	vr_phosphor_constants(q, fov, 0.25f, -1.0f, &c);
	CHECK_NEAR(c.strength, 0.0, 0.0);
}

int main(void) {
	RUN(test_off);
	RUN(test_stripes);
	RUN(test_light_kept);
	RUN(test_constants);
	return SFVR_TEST_RESULT("vr_phosphor");
}
