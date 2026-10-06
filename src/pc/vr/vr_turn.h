#ifndef VR_TURN_H
#define VR_TURN_H

#include <stdbool.h>
#include <stdint.h>

#include "sfvr/sfvr_turn.h"
#include "vr_settings.h"

// Comfort turning, the movement frame and the rest of the comfort set
// (docs/VR_ARCHITECTURE.md section 4). Pure: no GL, no OpenXR, no game
// headers; the game side (camera.c, mario.c) calls the helpers marked so.
//
// ANGLES
//
// Every angle here is a heading in radians, positive turning LEFT
// (counter-clockwise seen from above), as vr_yaw_of(). SM64's s16 yaws turn the
// same way (0x4000 takes +Z to +X, a rotation about +Y), so a heading delta d
// is vr_turn_angle_s16(d) SM64 units. A camera's SM64 yaw (Mario to camera,
// c->yaw) equals the heading of the direction it looks.
//
// THE TURN PROTOCOL
//
// SM64's camera eases to a new yaw (Lakitu flies a chord to his goal), so a
// written yaw alone reads as a slow swing (lesson 94). Instead:
//
//   1. The renderer adds the stick's turn to gVrCamTurn.queued at once and
//      shows it at once: the eye's anchor is the game camera orbited about
//      Mario by `queued` (vr_game_cam_orbit).
//   2. The next camera update (camera.c) takes what is queued
//      (vr_turn_take) and turns the whole camera rig about Mario by it in one
//      step: Lakitu's current and goal position and focus, the camera's yaws,
//      the mode's own yaw (radial sModeOffsetYaw, 8-direction
//      s8DirModeYawOffset) and the camera node's previous position, so the
//      interpolation between ticks is turned too. Nothing eases.
//   3. The queue is empty again, so the orbit is gone in the same frame the
//      game camera shows the turn: the shown heading never moves twice.
//
// The game camera's own turns (C buttons, auto-rotation, walls, cuts) are
// never compensated: they show as the game makes them.

#define VR_TURN_PI 3.14159265358979323846f
#define VR_TURN_DEG (VR_TURN_PI / 180.0f)

// A limit for camera modes without one
#define VR_TURN_UNLIMITED 1000.0f
// A snap cut by a camera's limit to less than this is dropped
#define VR_TURN_MIN_SNAP (5.0f * VR_TURN_DEG)
// A shown heading step larger than this in one frame is a cut or a snap, not motion
#define VR_TURN_JUMP (8.0f * VR_TURN_DEG)

// The comfort keys, read live every frame (the settings page may change them)
typedef struct {
	sfvr_turn_config turn; // turn_mode, snap_angle, smooth_turn_speed
	bool vignette;
	float vignette_strength;
	int move_relative; // 0 head, 1 left hand, 2 right hand
	float height_offset; // metres
	bool seated;
	float head_translation;
	bool camera_shake;
	float world_scale;
	bool level_pitch;
	float camera_closer; // 0..0.8
	float camera_lower; // 0..1
	float diag_turn_stick; // 0 = off
} vr_comfort_t;

void vr_comfort_from(vr_comfort_t *out, const vr_settings_t *s);

// -----------------------------------------------------------------------------
// Angles

float vr_wrap_pi(float a); // to -pi..pi
int16_t vr_turn_angle_s16(float heading); // radians to SM64 units, wrapped
float vr_turn_s16_angle(int32_t units);

// Rotates `p` about the vertical line through `pivot` by `heading` (in place).
// Game side (camera.c).
void vr_turn_rotate_about(float p[3], const float pivot[3], float heading);

// -----------------------------------------------------------------------------
// The renderer's side

typedef struct {
	bool open; // stereo play and the game camera can take a turn now
	float queued; // asked for, not yet taken
	float limit_left, limit_right; // what the game camera can take now (>= 0)
} vr_turn_gate_t;

enum { VR_TURN_STEP_NONE, VR_TURN_STEP_SNAP, VR_TURN_STEP_SMOOTH };

typedef struct {
	float delta; // to add to the queue
	int kind; // VR_TURN_STEP_*
	bool limited; // the camera's limit cut (or dropped) it
	bool closed; // the stick turned, but the gate was closed: nothing turns
} vr_turn_step_t;

typedef struct {
	sfvr_turn_state stick;
	bool diag_started;
	double diag_start_s;
} vr_turn_t;

void vr_turn_reset(vr_turn_t *t);

// One display frame. The stick goes through the sfvr helper even with the
// gate closed (so a stick held while it opens does not snap until it is
// re-armed), but only an open gate turns. The delta keeps queued + delta
// within [-limit_right, limit_left]; a snap cut below VR_TURN_MIN_SNAP is
// dropped.
vr_turn_step_t vr_turn_frame(vr_turn_t *t, const sfvr_turn_config *cfg, float stick_x, float dt,
	const vr_turn_gate_t *gate);

// diag_turn_stick: the right stick X held at `deflection` for 4 s, released
// for 4 s, while `active` (stereo play); the real stick otherwise.
float vr_turn_diag_stick(vr_turn_t *t, float deflection, float real_stick, double now_s, bool active);

// -----------------------------------------------------------------------------
// The game camera's side (camera.c)

// What a camera update takes: everything queued when it can turn, within its
// limits. Empties the queue; what it could not take goes to *refused.
float vr_turn_take(float *queued, bool allowed, float limit_left, float limit_right, float *refused);

// -----------------------------------------------------------------------------
// Movement (move_relative)

// The movement reference's heading relative to the view's forward: the head's
// (0) or a hand's aim (1 left, 2 right) yaw, minus the recentre yaw. A hand
// without a pose falls back to the head.
float vr_move_reference(int move_relative, float head_yaw, const float hand_yaw[2], const bool hand_valid[2],
	float recentre_yaw);

// -----------------------------------------------------------------------------
// Vignette and heading rate

#define VR_VIGNETTE_CLOSE_S 0.2f
#define VR_VIGNETTE_OPEN_S 0.5f
#define VR_VIGNETTE_RATE_MIN 5.0f // deg/s: slower artificial rotation shows nothing
#define VR_VIGNETTE_RATE_FULL 30.0f // deg/s: the full strength from here

// The vignette an artificial rotation rate asks for (0..strength)
float vr_vignette_target(float yaw_rate_dps, float strength);

// Moves `amount` toward `target`: closing at strength per 0.2 s, opening at
// strength per 0.5 s. Returns the new amount.
float vr_vignette_step(float amount, float target, float strength, float dt);

typedef struct {
	bool valid;
	float raw; // last wrapped heading
	float unwrapped;
} vr_heading_t;

// Unwrapped heading for a new wrapped one
float vr_heading_track(vr_heading_t *h, float raw);

// deg/s between two unwrapped headings; 0 for a jump (a cut or a snap) or no time
float vr_heading_rate(float prev, float cur, float dt);

#endif
