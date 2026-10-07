#ifndef VR_CAM_H
#define VR_CAM_H

#include <stdbool.h>

#include "vr_math.h"
#include "vr_types.h"

// The SM64 rig: the game camera as the anchor, the head on top of it, and the
// matrices the Fast3D replay needs per eye (docs/VR_ARCHITECTURE.md section 4).
// Pure: no GL, no OpenXR, no game headers.
//
// CONVENTIONS
//
// The game (sm64 decomp, sm64coopdx): world +Y up, right-handed. Its Mat4 is
// float[4][4] used with row vectors (v' = v * M, translation in M[3][0..2]).
// The memory of such a Mat4 is exactly the column-major array of the
// column-vector matrix M^T, so every float[16] here is "column-major,
// column-vector" and can be memcpy'd to and from a Mat4 / Mtx unchanged. A
// product A * B here is the game's B * A.
//
// mtxf_lookat() builds the camera's view: camera space +X right, +Y up, +Z back
// (it looks down -Z), the same axes as an OpenXR pose. guPerspective() is the
// GL clip convention (z in -1..1, w = -z).
//
// Every world modelview the game emits is model * camera view, with the camera
// view baked in on the CPU. An eye pass therefore keeps the modelviews and
// replaces the projection with eye_proj * eye_view * inverse(camera_view); the
// camera view cancels and each vertex lands in the eye's clip space.

typedef struct {
    float world_scale; // game units per metre
    float head_translation; // scale of the head centre's displacement; eye offsets are never scaled
    bool level_pitch; // the anchor drops the camera's pitch (the head supplies it)
    float height_offset; // metres the eyes are raised (world up), times world_scale; 0 = none
} vr_rig_t;

// The game camera of a frame, from the view matrix its modelviews use.
typedef struct {
    float view[16]; // world -> camera
    float inv_view[16]; // camera -> world
    float position[3]; // world
    float right[3], up[3], back[3]; // camera axes in world
} vr_game_cam_t;

// False when the matrix has no usable rotation (a degenerate camera).
bool vr_game_cam_from_view(const float view[16], vr_game_cam_t *out);

// The anchor: the camera's position, its heading, its pitch unless the rig
// levels it, never its roll. rotation's columns are the anchor's right, up and
// back in world space.
typedef struct {
    float rotation[9];
    float position[3];
} vr_anchor_t;

void vr_anchor_from_cam(const vr_game_cam_t *cam, bool level_pitch, vr_anchor_t *out);

// The heading of the camera's forward (radians, positive turning left, as
// vr_yaw_of). Straight up or down: from its up vector.
float vr_game_cam_heading(const vr_game_cam_t *cam);

// The comfort turn (vr_turn.h): the camera orbited about the vertical line
// through `pivot` by `heading` (positive left), position and axes. 0 copies it
// unchanged. Game units throughout: no scale applies.
void vr_game_cam_orbit(const vr_game_cam_t *cam, const float pivot[3], float heading, vr_game_cam_t *out);

// The third-person head origin (camera_closer, camera_lower): positive closer
// moves E toward focus A; negative closer extends farther. Positive lower
// moves the remaining height toward A; negative lower extends higher. Zero
// reproduces E. Limits are closer -1..0.8 and lower -1..1.
void vr_third_person_origin(const float eye[3], const float at[3], float lower, float closer, float out[3]);

// Optional native line-of-sight check. The callback tests the segment from the
// source to proposed camera origin and returns the first hit position.
typedef bool (*vr_cam_los_fn)(const float source[3], const float proposed[3], float hit[3], void *context);

// The camera moved to that origin. Its heading stays; its pitch turns by as
// much as the line to A did (lowering looks up at A). Both 0: copied unchanged.
// No roll ever.
void vr_game_cam_third_person(const vr_game_cam_t *cam, const float at[3], float lower, float closer,
    vr_game_cam_t *out);
void vr_game_cam_third_person_constrained(const vr_game_cam_t *cam, const float at[3],
    float lower, float closer, vr_cam_los_fn los, void *context, float clearance, vr_game_cam_t *out);

// An eye (or the head centre) in the game's world.
typedef struct {
    float position[3]; // game units
    float rotation[9]; // eye axes in world (columns: right, up, back)
    float view[16]; // world -> eye
} vr_eye_cam_t;

// The camera for an OpenXR pose (an eye, or the head centre) on the anchor.
// `head` is the head centre of the same frame: its displacement from the
// recentre origin is scaled by head_translation, the offset of `pose` from the
// head centre is not, so the IPD comes only from the runtime's eye poses. Both
// are scaled by world_scale into game units. With the head at the recentre
// origin and facing the recentre yaw, the head centre sits at the camera and
// looks where the anchor looks. height_offset then raises every eye alike.
void vr_eye_from_pose(const vr_anchor_t *anchor, const vr_rig_t *rig, const vr_recentre_t *rc,
    const vr_pose_t *head, const vr_pose_t *pose, vr_eye_cam_t *out);

// near and far of a game perspective projection (guPerspective). False when
// `proj` is not a perspective projection.
bool vr_game_proj_near_far(const float proj[16], float *near, float *far);

// True for a perspective projection (w = -z), false for an orthographic one.
bool vr_proj_is_perspective(const float proj[16]);

// The replacement for the game's world projection in an eye pass:
// eye_proj(fov, near, far) * eye->view * cam->inv_view.
void vr_eye_world_proj(const vr_game_cam_t *cam, const vr_eye_cam_t *eye, const float fov[4],
    float near, float far, float out[16]);

// The sky at infinity: eye_proj * the rotation of eye->view (no translation).
// Sky vertices are directions in world axes.
void vr_eye_sky_proj(const vr_eye_cam_t *eye, const float fov[4], float near, float far, float out[16]);

// The rotation mtxf_billboard() puts in its matrix so a sprite faces `head`
// instead of the game camera: the head's axes in the camera's space, as the
// game's row-vector 3x3 (out[row][col]).
void vr_billboard_rotation(const vr_game_cam_t *cam, const vr_eye_cam_t *head, float out[3][3]);

// The cylindrical variant (mtxf_cylboard): the sprite keeps the world's up and
// turns about it to face the head's heading.
void vr_cylboard_rotation(const vr_game_cam_t *cam, const vr_eye_cam_t *head, float out[3][3]);

// Inverse of a rigid transform (rotation + translation), column-major.
void vr_mat4_rigid_inverse(const float m[16], float out[16]);

#endif
