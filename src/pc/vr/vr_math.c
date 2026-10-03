#include <math.h>
#include <string.h>

#include "vr_math.h"

#define PI_D 3.14159265358979323846

// Never atan2f here: the game defines its own atan2f (src/engine/math_util.c, a
// table lookup at 16-bit angle precision), and the linker resolves every call to it.

static void rotate(const float q[4], const float v[3], float out[3]) {
	// v' = v + 2w (qv x v) + 2 qv x (qv x v)
	float c1[3] = {
		q[1] * v[2] - q[2] * v[1],
		q[2] * v[0] - q[0] * v[2],
		q[0] * v[1] - q[1] * v[0]
	};
	float t[3] = { 2 * c1[0], 2 * c1[1], 2 * c1[2] };
	out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
	out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
	out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

float vr_yaw_of(const float q[4]) {
	float forward_in[3] = {0, 0, -1};
	float f[3];
	rotate(q, forward_in, f);
	if (f[0] * f[0] + f[2] * f[2] > 1e-6f) {
		return (float)atan2(-f[0], -f[2]);
	}

	float right_in[3] = {1, 0, 0};
	float r[3];
	rotate(q, right_in, r);
	return (float)atan2(-r[2], r[0]);
}

void vr_quat_from_yaw(float yaw, float out[4]) {
	out[0] = 0;
	out[1] = sinf(yaw * 0.5f);
	out[2] = 0;
	out[3] = cosf(yaw * 0.5f);
}

void vr_panel_pose(const vr_pose_t *head, float distance, vr_pose_t *out) {
	float yaw = vr_yaw_of(head->orientation);
	vr_quat_from_yaw(yaw, out->orientation);
	out->position[0] = head->position[0] - sinf(yaw) * distance;
	out->position[1] = head->position[1];
	out->position[2] = head->position[2] - cosf(yaw) * distance;
}


// -----------------------------------------------------------------------------
// Small matrix helpers (3x3 column-major: m[col * 3 + row])

void vr_quat_mul(const float a[4], const float b[4], float out[4]) {
	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

void vr_quat_normalize(float q[4]) {
	float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (len < 1e-9f) {
		q[0] = q[1] = q[2] = 0;
		q[3] = 1;
		return;
	}
	for (int i = 0; i < 4; i++) {
		q[i] /= len;
	}
}

void vr_mat3_from_quat(const float q[4], float m[9]) {
	float x = q[0], y = q[1], z = q[2], w = q[3];
	m[0] = 1 - 2 * (y * y + z * z);
	m[1] = 2 * (x * y + z * w);
	m[2] = 2 * (x * z - y * w);
	m[3] = 2 * (x * y - z * w);
	m[4] = 1 - 2 * (x * x + z * z);
	m[5] = 2 * (y * z + x * w);
	m[6] = 2 * (x * z + y * w);
	m[7] = 2 * (y * z - x * w);
	m[8] = 1 - 2 * (x * x + y * y);
}

void vr_mat3_mul(const float a[9], const float b[9], float out[9]) {
	float r[9];
	for (int c = 0; c < 3; c++) {
		for (int row = 0; row < 3; row++) {
			float sum = 0;
			for (int k = 0; k < 3; k++) {
				sum += a[k * 3 + row] * b[c * 3 + k];
			}
			r[c * 3 + row] = sum;
		}
	}
	memcpy(out, r, sizeof(r));
}

void vr_mat3_transpose(const float m[9], float out[9]) {
	float r[9];
	for (int c = 0; c < 3; c++) {
		for (int row = 0; row < 3; row++) {
			r[c * 3 + row] = m[row * 3 + c];
		}
	}
	memcpy(out, r, sizeof(r));
}

void vr_mat3_mulv(const float m[9], const float v[3], float out[3]) {
	float r[3];
	for (int row = 0; row < 3; row++) {
		r[row] = m[row] * v[0] + m[3 + row] * v[1] + m[6 + row] * v[2];
	}
	memcpy(out, r, sizeof(r));
}


void vr_head_pose(const vr_view_t views[2], vr_pose_t *out) {
	const float *a = views[0].pose.orientation;
	const float *b = views[1].pose.orientation;
	float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	float sign = dot < 0 ? -1.0f : 1.0f;
	for (int i = 0; i < 4; i++) {
		out->orientation[i] = a[i] + sign * b[i];
	}
	vr_quat_normalize(out->orientation);
	for (int i = 0; i < 3; i++) {
		out->position[i] = 0.5f * (views[0].pose.position[i] + views[1].pose.position[i]);
	}
}

void vr_recentre_from_head(const vr_pose_t *head, vr_recentre_t *out) {
	out->yaw = vr_yaw_of(head->orientation);
	memcpy(out->origin, head->position, sizeof(out->origin));
}

void vr_views_apply_yaw(vr_view_t views[2], float yaw) {
	vr_pose_t head;
	vr_head_pose(views, &head);

	float yq[4], yaw_rot[9];
	vr_quat_from_yaw(yaw, yq);
	vr_mat3_from_quat(yq, yaw_rot);

	for (int e = 0; e < 2; e++) {
		float d[3];
		for (int i = 0; i < 3; i++) {
			d[i] = views[e].pose.position[i] - head.position[i];
		}
		vr_mat3_mulv(yaw_rot, d, d);
		for (int i = 0; i < 3; i++) {
			views[e].pose.position[i] = head.position[i] + d[i];
		}
		float q[4];
		vr_quat_mul(yq, views[e].pose.orientation, q);
		vr_quat_normalize(q);
		memcpy(views[e].pose.orientation, q, sizeof(q));
	}
}


// -----------------------------------------------------------------------------
// Projection

void vr_projection_tan(float left, float right, float up, float down, float near, float far, float out[16]) {
	float w = right - left;
	float h = up - down;
	float d = far - near;
	memset(out, 0, 16 * sizeof(float));
	out[0] = 2.0f / w;
	out[5] = 2.0f / h;
	out[8] = (right + left) / w;
	out[9] = (up + down) / h;
	out[10] = -(far + near) / d;
	out[11] = -1.0f;
	out[14] = -2.0f * far * near / d;
}

void vr_projection_fov(const float fov[4], float near, float far, float out[16]) {
	vr_projection_tan(tanf(fov[0]), tanf(fov[1]), tanf(fov[2]), tanf(fov[3]), near, far, out);
}

void vr_mat4_mul(const float a[16], const float b[16], float out[16]) {
	float r[16];
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			float sum = 0;
			for (int k = 0; k < 4; k++) {
				sum += a[k * 4 + row] * b[c * 4 + k];
			}
			r[c * 4 + row] = sum;
		}
	}
	memcpy(out, r, sizeof(r));
}

float vr_project(const float view[16], const float proj[16], const float point[3], float ndc[3]) {
	float v[4], c[4];
	for (int row = 0; row < 4; row++) {
		v[row] = view[row] * point[0] + view[4 + row] * point[1] + view[8 + row] * point[2] + view[12 + row];
	}
	for (int row = 0; row < 4; row++) {
		c[row] = proj[row] * v[0] + proj[4 + row] * v[1] + proj[8 + row] * v[2] + proj[12 + row] * v[3];
	}
	float inv_w = c[3] == 0.0f ? 1.0f : 1.0f / c[3];
	ndc[0] = c[0] * inv_w;
	ndc[1] = c[1] * inv_w;
	ndc[2] = c[2] * inv_w;
	return c[3];
}


// -----------------------------------------------------------------------------
// The HUD quad

void vr_pose_level(const vr_pose_t *head, vr_pose_t *out) {
	float m[9];
	vr_mat3_from_quat(head->orientation, m);
	// forward is -Z, so it is minus the third column
	float up = -m[7];
	if (up > 1.0f) up = 1.0f;
	if (up < -1.0f) up = -1.0f;
	float pitch = asinf(up);

	float yq[4], pq[4];
	vr_quat_from_yaw(vr_yaw_of(head->orientation), yq);
	pq[0] = sinf(pitch * 0.5f);
	pq[1] = 0;
	pq[2] = 0;
	pq[3] = cosf(pitch * 0.5f);
	vr_quat_mul(yq, pq, out->orientation);
	vr_quat_normalize(out->orientation);
	memcpy(out->position, head->position, sizeof(out->position));
}

void vr_hud_pose(const vr_pose_t *head, float distance, vr_pose_t *out) {
	vr_pose_t level;
	vr_pose_level(head, &level);

	float m[9];
	vr_mat3_from_quat(level.orientation, m);
	memcpy(out->orientation, level.orientation, sizeof(out->orientation));
	for (int i = 0; i < 3; i++) {
		out->position[i] = head->position[i] - m[6 + i] * distance;
	}
}

void vr_hud_size(float distance, float scale_percent, float aspect, float *width, float *height) {
	const float half_angle = (float)(20.0 * PI_D / 180.0);
	float w = 2.0f * distance * tanf(half_angle) * scale_percent / 100.0f;
	*width = w;
	*height = aspect > 0 ? w / aspect : w;
}
