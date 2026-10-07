#ifndef VR_INPUT_H
#define VR_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "sfvr/sfvr_haptics.h"
#include "sfvr/sfvr_adjust.h"
#include "sfvr/sfvr_view.h"

// OpenXR actions as a virtual gamepad, plus the system layer and haptics
// (docs/VR_ARCHITECTURE.md section 6).
//
// The controllers become an Xbox-layout gamepad in SDL GameController terms:
// the same button indices and axis ranges as controller_sdl.c reads. The game's
// controller interface reads it (controller_vr.c, one more ControllerAPI next to
// the SDL pad and the keyboard), through the game's own gamepad bindings
// (configKeyA... in the 0x1000 range), so the controls menu, DJUI navigation and
// the prompts stay valid and nothing injects keys. This header needs no OpenXR
// header.

struct vr_xr;

// SDL_CONTROLLER_BUTTON_* indices, and controller_sdl.c's trigger buttons
enum {
    VR_PAD_A = 0,
    VR_PAD_B = 1,
    VR_PAD_X = 2,
    VR_PAD_Y = 3,
    VR_PAD_BACK = 4,
    VR_PAD_START = 6,
    VR_PAD_LEFTSTICK = 7,
    VR_PAD_RIGHTSTICK = 8,
    VR_PAD_LEFTSHOULDER = 9,
    VR_PAD_RIGHTSHOULDER = 10,
    VR_PAD_DPAD_UP = 11,
    VR_PAD_DPAD_DOWN = 12,
    VR_PAD_DPAD_LEFT = 13,
    VR_PAD_DPAD_RIGHT = 14,
    VR_PAD_LTRIGGER = 0x1A, // VK_LTRIGGER - VK_BASE_SDL_GAMEPAD
    VR_PAD_RTRIGGER = 0x1B,
    VR_PAD_BUTTONS = 32
};

// Analog triggers count as pressed above this (controller_sdl.c's AXIS_THRESHOLD)
#define VR_PAD_TRIGGER_THRESHOLD (30 * 256)

// The virtual gamepad of one XR frame
typedef struct {
    bool buttons[VR_PAD_BUTTONS];
    int16_t left_x, left_y, right_x, right_y; // SDL ranges: -32768..32767, y down
    int16_t left_trigger, right_trigger; // 0..32767
    bool live; // focused, synced; false: everything is released
} vr_gamepad_t;

// The latest frame's pad. controller_vr.c reads it once per game tick.
const vr_gamepad_t *vr_input_gamepad(void);

// The game read the pad: a short View press shows BACK for two reads, so a
// 30 Hz tick cannot miss a press made between two of them.
void vr_input_pad_consumed(void);

// L View held 1 s asks for a recentre (the head's yaw and horizontal
// position), held 3 s for one that takes the standing height again; the
// renderer applies them at a frame boundary.
void vr_request_recentre(void);
bool vr_take_recentre_request(void);
void vr_request_height(void);
bool vr_take_height_request(void);

// The yaw (vr_yaw_of) of a hand's aim pose (0 left, 1 right) in LOCAL space at
// the latest frame's display time; false when it is not tracked.
bool vr_input_hand_yaw(int hand, float *yaw);

// The game's rumble (osMotorStart / osMotorStop through controller_rumble_*):
// 40 ms pulses on both hands at `strength` while it lasts (the `rumble` event).
void vr_haptic_rumble_play(float strength, float seconds);
void vr_haptic_rumble_stop(void);

// -----------------------------------------------------------------------------
// The mapping (pure; tested without a runtime)

// The controllers' state of one frame, in their own terms
typedef struct {
    bool a, b, x, y;
    bool l_bumper, r_bumper;
    bool menu; // R Menu
    bool view; // L View, the raw level; the system layer makes BACK from it
    bool l_grip; // Steam Frame left grip modifier (not the fallback bumper/squeeze mapping)
    bool l_stick_press, r_stick_press;
    bool dpad_up, dpad_down, dpad_left, dpad_right;
    float l_trigger, r_trigger; // 0..1
    float l_stick[2], r_stick[2]; // x right, y up, -1..1
} vr_pad_sample_t;

typedef struct {
    sfvr_view_button view;
    sfvr_view_config view_config;
    int back_reads_left; // reads the BACK pulse stays down for
} vr_pad_t;

// Live settings shortcut. While eligible and left grip is held, right stick X
// changes world_scale and right stick Y changes camera_closer/camera_lower.
// The right stick is consumed; a deflected right stick remains suppressed after
// grip release until it returns to the deadzone, so turn/C-button helpers do
// not see a false neutral followed by a still-held direction.
typedef sfvr_adjust_state vr_adjustment_t;
void vr_adjustment_init(vr_adjustment_t *state);
bool vr_adjustment_apply(vr_adjustment_t *state, vr_pad_sample_t *sample,
    double predicted_display_s, bool eligible, float *world_scale,
    float *camera_closer, float *camera_lower);

void vr_pad_init(vr_pad_t *pad);

// One frame with focus: the system layer (L View through sfvr_view at now_s),
// then the gamepad. Returns the SFVR_VIEW_* events of the frame. The 1 s hold
// calls vr_request_recentre(), the 3 s hold vr_request_height(); each queues
// the `system` buzz on both hands.
unsigned vr_pad_map(vr_pad_t *pad, const vr_pad_sample_t *sample, double now_s, vr_gamepad_t *out);

// Focus lost or session stopped: everything released, the View press cancelled
// without an event.
void vr_pad_release(vr_pad_t *pad, vr_gamepad_t *out);

// -----------------------------------------------------------------------------
// The OpenXR side

typedef struct vr_input vr_input_t;

// Creates the action set and its actions, suggests the bindings (the Frame
// profile only when XR_VALVE_frame_controller_interaction is enabled, then
// Index and Touch) and attaches the set to the session. NULL when the action
// set could not be made; the game then has no VR input.
vr_input_t *vr_input_create(struct vr_xr *xr);
void vr_input_destroy(vr_input_t *in, struct vr_xr *xr);

// Once per frame, after xrBeginFrame: xrSyncActions, the pad, the system layer,
// the rumble pulses and the haptics flush (scaled by `haptics`, 0..1). Without
// focus everything is released and nothing buzzes. Safe with `in` NULL.
void vr_input_update(vr_input_t *in, struct vr_xr *xr, float haptics);

// The component paths suggested for one profile, for the binding check
typedef struct {
    const char *profile;
    const char *const *paths;
    int count;
} vr_input_profile_paths_t;
int vr_input_profiles(const vr_input_profile_paths_t **out);

#endif
