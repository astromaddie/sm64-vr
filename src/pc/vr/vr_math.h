#ifndef VR_MATH_H
#define VR_MATH_H

#include "vr_types.h"

// Pure maths shared by the stereo renderer: no GL, no OpenXR, no game headers.
// Lifted from the WipEout VR port (src/vr/vr_math.c), without its game camera.
//
// OpenXR (LOCAL space and every eye or head pose): right-handed, +X right,
// +Y up, -Z forward, metres. Matrices are column-major like GL: a 4x4 is
// m[col * 4 + row], a 3x3 is m[col * 3 + row]. Quaternions are (x, y, z, w).

// Rotation about +Y (heading) of an orientation: the angle of its forward
// vector (-Z) in the horizontal plane, positive turning left. Falls back to the
// right vector when looking straight up or down.
float vr_yaw_of(const float q[4]);

// Quaternion for a rotation of `yaw` radians about +Y.
void vr_quat_from_yaw(float yaw, float out[4]);

void vr_quat_mul(const float a[4], const float b[4], float out[4]);
void vr_quat_normalize(float q[4]);
void vr_mat3_from_quat(const float q[4], float m[9]);
void vr_mat3_mul(const float a[9], const float b[9], float out[9]);
void vr_mat3_transpose(const float m[9], float out[9]);
void vr_mat3_mulv(const float m[9], const float v[3], float out[3]);

// A world-locked panel `distance` metres ahead of the head (horizontally, at
// head height), facing back at it, levelled: only the head's heading counts.
void vr_panel_pose(const vr_pose_t *head, float distance, vr_pose_t *out);

// Recentre: the head's yaw and position when it happened. The head is then
// treated as sitting at the game camera, facing where the camera faces.
typedef struct {
    float yaw; // radians
    float origin[3]; // head position in LOCAL space, metres
} vr_recentre_t;

// The head centre: midway between the eyes, orientation the normalised sum of both.
void vr_head_pose(const vr_view_t views[2], vr_pose_t *out);

void vr_recentre_from_head(const vr_pose_t *head, vr_recentre_t *out);

// Rotates `views` about the vertical axis through the head centre (diag_yaw).
void vr_views_apply_yaw(vr_view_t views[2], float yaw);

// GL clip volume (z in -1..1) from the tangents of the frustum edges at distance 1.
void vr_projection_tan(float left, float right, float up, float down, float near, float far, float out[16]);

// From an OpenXR fov (angles in radians: left, right, up, down; left and down negative).
void vr_projection_fov(const float fov[4], float near, float far, float out[16]);

// out = a * b
void vr_mat4_mul(const float a[16], const float b[16], float out[16]);

// Clip-space position of a point, then the divide by w: ndc[0..2]. Returns w.
float vr_project(const float view[16], const float proj[16], const float point[3], float ndc[3]);

// The head's orientation with its roll removed (yaw then pitch only).
void vr_pose_level(const vr_pose_t *head, vr_pose_t *out);

// The HUD quad: `distance` metres along the head's forward, facing the head,
// following its yaw and pitch and never its roll.
void vr_hud_pose(const vr_pose_t *head, float distance, vr_pose_t *out);

// Width and height in metres: 2 * distance * tan(20 degrees) * scale_percent / 100
// wide (40 degrees at 100%), `aspect` = width / height.
void vr_hud_size(float distance, float scale_percent, float aspect, float *width, float *height);

// The game links its own atan2f (src/engine/math_util.c: SM64's argument
// convention and a 16-bit table), so in the game binary every atan2f call in VR
// code resolves to it, while host tests get libc's: the cinema frustum came out
// 9:16 on the device only. Use (float)atan2 instead; this makes atan2f an error.
#if defined(__GNUC__) && !defined(VR_ALLOW_ATAN2F)
#include <math.h>
#pragma GCC poison atan2f
#endif

#endif
