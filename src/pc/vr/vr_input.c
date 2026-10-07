#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr_input.h"
#include "vr_runtime_api.h"
#include "vr_log.h"
#include "vr_math.h"
#include "vr_xr.h"

// -----------------------------------------------------------------------------
// Shared state: the pad the game reads, recentre requests, haptics

static vr_gamepad_t sPad;
static bool sRecentreRequested = false;
static bool sHeightRequested = false;
static float sHandYaw[2];
static bool sHandValid[2];
static sfvr_haptic_queue sHaptics;

static struct {
    bool on;
    float strength;
    double until_s;
    double next_pulse_s;
    double duration_s; // requested; the end is set at the next frame's time
} sRumble;

const vr_gamepad_t *vr_input_gamepad(void) {
    return &sPad;
}

void vr_request_recentre(void) {
    sRecentreRequested = true;
}

bool vr_take_recentre_request(void) {
    bool r = sRecentreRequested;
    sRecentreRequested = false;
    return r;
}

void vr_request_height(void) {
    sHeightRequested = true;
}

bool vr_take_height_request(void) {
    bool r = sHeightRequested;
    sHeightRequested = false;
    return r;
}

bool vr_input_hand_yaw(int hand, float *yaw) {
    if (hand < 0 || hand > 1 || !sHandValid[hand]) {
        return false;
    }
    *yaw = sHandYaw[hand];
    return true;
}

void vr_haptic_rumble_play(float strength, float seconds) {
    sRumble.on = true;
    sRumble.strength = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
    sRumble.duration_s = seconds;
    sRumble.until_s = -1.0; // set at the next frame
    sRumble.next_pulse_s = 0.0;
}

void vr_haptic_rumble_stop(void) {
    sRumble.on = false;
}


// -----------------------------------------------------------------------------
// The mapping

static int16_t axis(float v) {
    if (v > 1.0f) {
        v = 1.0f;
    }
    if (v < -1.0f) {
        v = -1.0f;
    }
    return (int16_t)lrintf(v * 32767.0f);
}

void vr_pad_init(vr_pad_t *pad) {
    memset(pad, 0, sizeof(*pad));
    sfvr_view_config_default(&pad->view_config);
    sfvr_view_reset(&pad->view);
}

unsigned vr_pad_map(vr_pad_t *pad, const vr_pad_sample_t *s, double now_s, vr_gamepad_t *out) {
    memset(out, 0, sizeof(*out));
    out->live = true;

    unsigned events = sfvr_view_update(&pad->view, &pad->view_config, s->view ? 1 : 0, now_s);
    if (events & SFVR_VIEW_SHORT_PRESS) {
        pad->back_reads_left = 2;
    }
    if (events & SFVR_VIEW_RECENTRE) {
        vr_request_recentre();
        sfvr_haptic_event(&sHaptics, SFVR_HAND_BOTH, SFVR_HAPTIC_SYSTEM);
    }
    if (events & SFVR_VIEW_RECALIBRATE_HEIGHT) {
        // The standing height the head's height is measured from (vr_stage.h)
        vr_request_height();
        sfvr_haptic_event(&sHaptics, SFVR_HAND_BOTH, SFVR_HAPTIC_SYSTEM);
    }

    out->buttons[VR_PAD_A] = s->a;
    out->buttons[VR_PAD_B] = s->b;
    out->buttons[VR_PAD_X] = s->x;
    out->buttons[VR_PAD_Y] = s->y;
    out->buttons[VR_PAD_BACK] = pad->back_reads_left > 0;
    out->buttons[VR_PAD_START] = s->menu;
    out->buttons[VR_PAD_LEFTSTICK] = s->l_stick_press;
    out->buttons[VR_PAD_RIGHTSTICK] = s->r_stick_press;
    out->buttons[VR_PAD_LEFTSHOULDER] = s->l_bumper;
    out->buttons[VR_PAD_RIGHTSHOULDER] = s->r_bumper;
    out->buttons[VR_PAD_DPAD_UP] = s->dpad_up;
    out->buttons[VR_PAD_DPAD_DOWN] = s->dpad_down;
    out->buttons[VR_PAD_DPAD_LEFT] = s->dpad_left;
    out->buttons[VR_PAD_DPAD_RIGHT] = s->dpad_right;

    // SDL's sticks are y down; OpenXR's are y up
    out->left_x = axis(s->l_stick[0]);
    out->left_y = axis(-s->l_stick[1]);
    out->right_x = axis(s->r_stick[0]);
    out->right_y = axis(-s->r_stick[1]);
    out->left_trigger = (int16_t)lrintf((s->l_trigger < 0 ? 0 : s->l_trigger > 1 ? 1 : s->l_trigger) * 32767.0f);
    out->right_trigger = (int16_t)lrintf((s->r_trigger < 0 ? 0 : s->r_trigger > 1 ? 1 : s->r_trigger) * 32767.0f);
    out->buttons[VR_PAD_LTRIGGER] = out->left_trigger > VR_PAD_TRIGGER_THRESHOLD;
    out->buttons[VR_PAD_RTRIGGER] = out->right_trigger > VR_PAD_TRIGGER_THRESHOLD;
    return events;
}

void vr_pad_release(vr_pad_t *pad, vr_gamepad_t *out) {
    memset(out, 0, sizeof(*out));
    sfvr_view_reset(&pad->view);
    pad->back_reads_left = 0;
}

#define VR_ADJUST_DEADZONE 0.20f
#define VR_ADJUST_MAX_DT 0.03333333333333333
#define VR_ADJUST_SCALE_FACTOR_PER_S 1.5f
#define VR_ADJUST_CAMERA_PER_S 0.25f

static float adjustment_axis(float value) {
    float magnitude = fabsf(value);
    if (magnitude <= VR_ADJUST_DEADZONE) {
        return 0.0f;
    }
    float scaled = (magnitude - VR_ADJUST_DEADZONE) / (1.0f - VR_ADJUST_DEADZONE);
    return copysignf(scaled, value);
}

void vr_adjustment_init(vr_adjustment_t *state) {
    memset(state, 0, sizeof(*state));
}

bool vr_adjustment_apply(vr_adjustment_t *state, vr_pad_sample_t *sample,
    double display_s, bool eligible, float *world_scale,
    float *camera_closer, float *camera_lower) {
    double dt = 0.0;
    if (isfinite(display_s)) {
        if (state->display_time_valid && display_s >= state->last_display_s) {
            dt = display_s - state->last_display_s;
            if (dt > VR_ADJUST_MAX_DT) dt = VR_ADJUST_MAX_DT;
        }
        state->last_display_s = display_s;
        state->display_time_valid = true;
    } else {
        state->display_time_valid = false;
    }

    bool modifier = eligible && sample->l_grip;
    bool adjust = modifier && !sample->menu && !sample->view;
    bool changed = false;
    if (adjust && dt > 0.0) {
        float world = adjustment_axis(sample->r_stick[0]);
        float camera = adjustment_axis(sample->r_stick[1]);
        // world_scale is inverse apparent size: left increases units/metre
        // (smaller world), right decreases it (larger world). Exponential scale
        // steps feel proportional at every size. Vertical up moves the camera
        // farther and higher by reducing both coupled offsets.
        if (world != 0.0f) {
            float before = *world_scale;
            *world_scale *= expf(-world * logf(VR_ADJUST_SCALE_FACTOR_PER_S) * (float)dt);
            if (*world_scale < 20.0f) *world_scale = 20.0f;
            if (*world_scale > 400.0f) *world_scale = 400.0f;
            changed |= *world_scale != before;
        }
        if (camera != 0.0f) {
            float step = camera * VR_ADJUST_CAMERA_PER_S * (float)dt;
            float before_closer = *camera_closer;
            float before_lower = *camera_lower;
            *camera_closer -= step;
            *camera_lower -= step;
            if (*camera_closer < 0.0f) *camera_closer = 0.0f;
            if (*camera_closer > 0.8f) *camera_closer = 0.8f;
            if (*camera_lower < 0.0f) *camera_lower = 0.0f;
            if (*camera_lower > 1.0f) *camera_lower = 1.0f;
            changed |= *camera_closer != before_closer || *camera_lower != before_lower;
        }
    }

    if (modifier) {
        if (fabsf(sample->r_stick[0]) > VR_ADJUST_DEADZONE
            || fabsf(sample->r_stick[1]) > VR_ADJUST_DEADZONE) {
            state->right_waiting_for_neutral = true;
        }
        sample->r_stick[0] = sample->r_stick[1] = 0.0f;
    } else if (state->right_waiting_for_neutral) {
        if (fabsf(sample->r_stick[0]) <= VR_ADJUST_DEADZONE
            && fabsf(sample->r_stick[1]) <= VR_ADJUST_DEADZONE) {
            state->right_waiting_for_neutral = false;
        } else {
            sample->r_stick[0] = sample->r_stick[1] = 0.0f;
        }
    }
    return changed;
}

static vr_pad_t *sPadState = NULL;

void vr_input_pad_consumed(void) {
    if (sPadState && sPadState->back_reads_left > 0) {
        sPadState->back_reads_left--;
        if (sPadState->back_reads_left == 0) {
            sPad.buttons[VR_PAD_BACK] = false;
        }
    }
}


// -----------------------------------------------------------------------------
// Actions and bindings

enum {
    ACT_A,
    ACT_B,
    ACT_X,
    ACT_Y,
    ACT_L_BUMPER,
    ACT_R_BUMPER,
    ACT_MENU,
    ACT_VIEW,
    ACT_L_GRIP,
    ACT_L_STICK_PRESS,
    ACT_R_STICK_PRESS,
    ACT_DPAD_UP,
    ACT_DPAD_DOWN,
    ACT_DPAD_LEFT,
    ACT_DPAD_RIGHT,
    ACT_L_TRIGGER,
    ACT_R_TRIGGER,
    ACT_L_STICK,
    ACT_R_STICK,
    ACT_HAPTIC_LEFT,
    ACT_HAPTIC_RIGHT,
    ACT_L_AIM,
    ACT_R_AIM,
    ACT_COUNT
};

static const struct {
    const char *name;
    const char *label;
    XrActionType type;
} action_defs[ACT_COUNT] = {
    [ACT_A] = { "a", "A (jump, select)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_B] = { "b", "B (punch, back)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_X] = { "x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_Y] = { "y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_L_BUMPER] = { "l_bumper", "Left bumper (L)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_R_BUMPER] = { "r_bumper", "Right bumper (R, camera)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_MENU] = { "menu", "Menu (Start, pause)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_VIEW] = { "view", "View (back, hold to recentre)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_L_GRIP] = { "left_grip", "Left grip (hold for live settings)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_L_STICK_PRESS] = { "l_stick_press", "Left stick click (Z)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_R_STICK_PRESS] = { "r_stick_press", "Right stick click", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_DPAD_UP] = { "dpad_up", "D-pad up", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_DPAD_DOWN] = { "dpad_down", "D-pad down", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_DPAD_LEFT] = { "dpad_left", "D-pad left (C left)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_DPAD_RIGHT] = { "dpad_right", "D-pad right (C right)", XR_ACTION_TYPE_BOOLEAN_INPUT },
    [ACT_L_TRIGGER] = { "l_trigger", "Left trigger (Z)", XR_ACTION_TYPE_FLOAT_INPUT },
    [ACT_R_TRIGGER] = { "r_trigger", "Right trigger (R)", XR_ACTION_TYPE_FLOAT_INPUT },
    [ACT_L_STICK] = { "l_stick", "Left stick (move)", XR_ACTION_TYPE_VECTOR2F_INPUT },
    [ACT_R_STICK] = { "r_stick", "Right stick (turn; C up, C down)", XR_ACTION_TYPE_VECTOR2F_INPUT },
    [ACT_HAPTIC_LEFT] = { "haptic_left", "Haptics (left)", XR_ACTION_TYPE_VIBRATION_OUTPUT },
    [ACT_HAPTIC_RIGHT] = { "haptic_right", "Haptics (right)", XR_ACTION_TYPE_VIBRATION_OUTPUT },
    [ACT_L_AIM] = { "l_aim", "Left hand (move_relative)", XR_ACTION_TYPE_POSE_INPUT },
    [ACT_R_AIM] = { "r_aim", "Right hand (move_relative)", XR_ACTION_TYPE_POSE_INPUT }
};

typedef struct {
    int action;
    const char *path;
} binding_t;

#define PROFILE_FRAME "/interaction_profiles/valve/frame_controller_valve"
#define PROFILE_INDEX "/interaction_profiles/valve/index_controller"
#define PROFILE_TOUCH "/interaction_profiles/oculus/touch_controller"

#define L_ "/user/hand/left/"
#define R_ "/user/hand/right/"

// Steam Frame: a split gamepad. Right hand: A B X Y, Menu, bumper, trigger,
// stick. Left hand: D-pad, View, bumper, trigger, stick. The same paths as
// WipEout VR, F-Zero X VR and Halo CE VR; checked against the profile's
// component list by tests/vr/test_vr_input.c (lesson 33).
static const binding_t frame_bindings[] = {
    { ACT_L_STICK, L_ "input/thumbstick" },
    { ACT_R_STICK, R_ "input/thumbstick" },
    { ACT_A, R_ "input/a/click" },
    { ACT_B, R_ "input/b/click" },
    { ACT_X, R_ "input/x/click" },
    { ACT_Y, R_ "input/y/click" },
    { ACT_L_BUMPER, L_ "input/bumper/click" },
    { ACT_R_BUMPER, R_ "input/bumper/click" },
    { ACT_L_TRIGGER, L_ "input/trigger/value" },
    { ACT_R_TRIGGER, R_ "input/trigger/value" },
    { ACT_MENU, R_ "input/menu/click" },
    { ACT_VIEW, L_ "input/view/click" },
    { ACT_L_GRIP, L_ "input/squeeze/click" },
    { ACT_L_STICK_PRESS, L_ "input/thumbstick/click" },
    { ACT_R_STICK_PRESS, R_ "input/thumbstick/click" },
    { ACT_DPAD_UP, L_ "input/dpad_up/click" },
    { ACT_DPAD_DOWN, L_ "input/dpad_down/click" },
    { ACT_DPAD_LEFT, L_ "input/dpad_left/click" },
    { ACT_DPAD_RIGHT, L_ "input/dpad_right/click" },
    { ACT_HAPTIC_LEFT, L_ "output/haptic" },
    { ACT_HAPTIC_RIGHT, R_ "output/haptic" },
    { ACT_L_AIM, L_ "input/aim/pose" },
    { ACT_R_AIM, R_ "input/aim/pose" }
};

// Index: no View, no Menu, no bumpers, no D-pad. The grips are the bumpers,
// left A stands in for View and left B for Menu, the right stick click is X.
static const binding_t index_bindings[] = {
    { ACT_L_STICK, L_ "input/thumbstick" },
    { ACT_R_STICK, R_ "input/thumbstick" },
    { ACT_A, R_ "input/a/click" },
    { ACT_B, R_ "input/b/click" },
    { ACT_X, R_ "input/thumbstick/click" },
    { ACT_L_BUMPER, L_ "input/squeeze/value" },
    { ACT_R_BUMPER, R_ "input/squeeze/value" },
    { ACT_L_TRIGGER, L_ "input/trigger/value" },
    { ACT_R_TRIGGER, R_ "input/trigger/value" },
    { ACT_MENU, L_ "input/b/click" },
    { ACT_VIEW, L_ "input/a/click" },
    { ACT_L_STICK_PRESS, L_ "input/thumbstick/click" },
    { ACT_HAPTIC_LEFT, L_ "output/haptic" },
    { ACT_HAPTIC_RIGHT, R_ "output/haptic" },
    { ACT_L_AIM, L_ "input/aim/pose" },
    { ACT_R_AIM, R_ "input/aim/pose" }
};

// Touch: left X is X, left Y stands in for View, the left Menu button is Menu.
static const binding_t touch_bindings[] = {
    { ACT_L_STICK, L_ "input/thumbstick" },
    { ACT_R_STICK, R_ "input/thumbstick" },
    { ACT_A, R_ "input/a/click" },
    { ACT_B, R_ "input/b/click" },
    { ACT_X, L_ "input/x/click" },
    { ACT_L_BUMPER, L_ "input/squeeze/value" },
    { ACT_R_BUMPER, R_ "input/squeeze/value" },
    { ACT_L_TRIGGER, L_ "input/trigger/value" },
    { ACT_R_TRIGGER, R_ "input/trigger/value" },
    { ACT_MENU, L_ "input/menu/click" },
    { ACT_VIEW, L_ "input/y/click" },
    { ACT_L_STICK_PRESS, L_ "input/thumbstick/click" },
    { ACT_R_STICK_PRESS, R_ "input/thumbstick/click" },
    { ACT_HAPTIC_LEFT, L_ "output/haptic" },
    { ACT_HAPTIC_RIGHT, R_ "output/haptic" },
    { ACT_L_AIM, L_ "input/aim/pose" },
    { ACT_R_AIM, R_ "input/aim/pose" }
};

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

int vr_input_profiles(const vr_input_profile_paths_t **out) {
    static const char *frame_paths[COUNT_OF(frame_bindings)];
    static const char *index_paths[COUNT_OF(index_bindings)];
    static const char *touch_paths[COUNT_OF(touch_bindings)];
    static vr_input_profile_paths_t profiles[3];
    for (int i = 0; i < COUNT_OF(frame_bindings); i++) {
        frame_paths[i] = frame_bindings[i].path;
    }
    for (int i = 0; i < COUNT_OF(index_bindings); i++) {
        index_paths[i] = index_bindings[i].path;
    }
    for (int i = 0; i < COUNT_OF(touch_bindings); i++) {
        touch_paths[i] = touch_bindings[i].path;
    }
    profiles[0] = (vr_input_profile_paths_t){ PROFILE_FRAME, frame_paths, COUNT_OF(frame_bindings) };
    profiles[1] = (vr_input_profile_paths_t){ PROFILE_INDEX, index_paths, COUNT_OF(index_bindings) };
    profiles[2] = (vr_input_profile_paths_t){ PROFILE_TOUCH, touch_paths, COUNT_OF(touch_bindings) };
    *out = profiles;
    return 3;
}

struct vr_input {
    XrActionSet set;
    XrAction action[ACT_COUNT];
    XrSpace aim_space[2]; // the hands' aim poses (move_relative); XR_NULL_HANDLE when unavailable
    vr_pad_t pad;
    vr_adjustment_t adjustment;
    bool active; // the last update had focus
};

static XrPath to_path(struct vr_xr *xr, const char *name) {
    XrPath path = XR_NULL_PATH;
    if (XR_FAILED(xr->api.string_to_path(xr->instance, name, &path))) {
        return XR_NULL_PATH;
    }
    return path;
}

static bool suggest(struct vr_xr *xr, vr_input_t *in, const char *profile, const binding_t *bindings, int count) {
    XrActionSuggestedBinding list[ACT_COUNT];
    uint32_t n = 0;
    for (int i = 0; i < count && n < ACT_COUNT; i++) {
        XrPath path = to_path(xr, bindings[i].path);
        if (path != XR_NULL_PATH) {
            list[n].action = in->action[bindings[i].action];
            list[n].binding = path;
            n++;
        }
    }
    XrInteractionProfileSuggestedBinding sb = {
        .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
        .interactionProfile = to_path(xr, profile),
        .countSuggestedBindings = n,
        .suggestedBindings = list
    };
    XrResult r = xr->api.suggest_interaction_profile_bindings(xr->instance, &sb);
    // A profile the runtime refuses is not fatal: the others still apply (lesson 33: log each)
    vr_logf("input: bindings %s: %s (%d)", profile, XR_SUCCEEDED(r) ? "suggested" : "rejected", (int)r);
    return XR_SUCCEEDED(r);
}

vr_input_t *vr_input_create(struct vr_xr *xr) {
    sfvr_haptic_queue_clear(&sHaptics);
    memset(&sPad, 0, sizeof(sPad));
    if (!xr->api.create_action_set || xr->session == XR_NULL_HANDLE) {
        return NULL;
    }
    vr_input_t *in = calloc(1, sizeof(*in));
    if (!in) {
        return NULL;
    }
    vr_pad_init(&in->pad);
    vr_adjustment_init(&in->adjustment);

    XrActionSetCreateInfo asci = { .type = XR_TYPE_ACTION_SET_CREATE_INFO };
    snprintf(asci.actionSetName, sizeof(asci.actionSetName), "sm64");
    snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName), "Super Mario 64");
    if (XR_FAILED(xr->api.create_action_set(xr->instance, &asci, &in->set))) {
        vr_logf("input: xrCreateActionSet failed; no VR input");
        free(in);
        return NULL;
    }

    for (int i = 0; i < ACT_COUNT; i++) {
        XrActionCreateInfo aci = { .type = XR_TYPE_ACTION_CREATE_INFO, .actionType = action_defs[i].type };
        snprintf(aci.actionName, sizeof(aci.actionName), "%s", action_defs[i].name);
        snprintf(aci.localizedActionName, sizeof(aci.localizedActionName), "%s", action_defs[i].label);
        if (XR_FAILED(xr->api.create_action(in->set, &aci, &in->action[i]))) {
            vr_logf("input: xrCreateAction %s failed; no VR input", action_defs[i].name);
            xr->api.destroy_action_set(in->set);
            free(in);
            return NULL;
        }
    }

    // The Frame profile first, and only when its extension is enabled; then the fallbacks
    int suggested = 0;
    if (xr->ext_frame_controller) {
        suggested += suggest(xr, in, PROFILE_FRAME, frame_bindings, COUNT_OF(frame_bindings));
    }
    suggested += suggest(xr, in, PROFILE_INDEX, index_bindings, COUNT_OF(index_bindings));
    suggested += suggest(xr, in, PROFILE_TOUCH, touch_bindings, COUNT_OF(touch_bindings));
    if (suggested == 0) {
        vr_logf("input: the runtime accepted no interaction profile; no VR input");
    }

    XrSessionActionSetsAttachInfo attach = {
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets = 1,
        .actionSets = &in->set
    };
    if (XR_FAILED(xr->api.attach_session_action_sets(xr->session, &attach))) {
        vr_logf("input: xrAttachSessionActionSets failed; no VR input");
        xr->api.destroy_action_set(in->set);
        free(in);
        return NULL;
    }

    // The hands' aim poses, for move_relative = left/right. Optional: the
    // function comes from the runtime directly (it is not in the host's table)
    PFN_xrCreateActionSpace create_action_space = NULL;
    if (xr->api.get_instance_proc_addr) {
        xr->api.get_instance_proc_addr(xr->instance, "xrCreateActionSpace", (PFN_xrVoidFunction *)&create_action_space);
    }
    for (int hand = 0; hand < 2; hand++) {
        in->aim_space[hand] = XR_NULL_HANDLE;
        if (!create_action_space || !xr->api.locate_space) {
            continue;
        }
        XrActionSpaceCreateInfo sci = {
            .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
            .action = in->action[hand == 0 ? ACT_L_AIM : ACT_R_AIM],
            .subactionPath = XR_NULL_PATH,
            .poseInActionSpace = { .orientation = { 0, 0, 0, 1 }, .position = { 0, 0, 0 } }
        };
        if (XR_FAILED(create_action_space(xr->session, &sci, &in->aim_space[hand]))) {
            in->aim_space[hand] = XR_NULL_HANDLE;
            vr_logf("input: no aim pose space for the %s hand", hand == 0 ? "left" : "right");
        }
    }

    sPadState = &in->pad;
    vr_logf("input: action set attached (%d actions, %d profiles%s)", ACT_COUNT, suggested,
        xr->ext_frame_controller ? ", frame first" : ", no frame profile");
    return in;
}

void vr_input_destroy(vr_input_t *in, struct vr_xr *xr) {
    if (!in) {
        return;
    }
    vr_pad_release(&in->pad, &sPad);
    sPadState = NULL;
    for (int hand = 0; hand < 2; hand++) {
        if (in->aim_space[hand] != XR_NULL_HANDLE && xr->api.destroy_space) {
            xr->api.destroy_space(in->aim_space[hand]);
        }
        sHandValid[hand] = false;
    }
    if (in->set != XR_NULL_HANDLE) {
        xr->api.destroy_action_set(in->set);
    }
    free(in);
}

static bool get_bool(struct vr_xr *xr, vr_input_t *in, int action) {
    XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO, .action = in->action[action] };
    XrActionStateBoolean st = { .type = XR_TYPE_ACTION_STATE_BOOLEAN };
    return XR_SUCCEEDED(xr->api.get_action_state_boolean(xr->session, &gi, &st)) && st.isActive && st.currentState;
}

static float get_float(struct vr_xr *xr, vr_input_t *in, int action) {
    XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO, .action = in->action[action] };
    XrActionStateFloat st = { .type = XR_TYPE_ACTION_STATE_FLOAT };
    if (XR_SUCCEEDED(xr->api.get_action_state_float(xr->session, &gi, &st)) && st.isActive) {
        return st.currentState;
    }
    return 0.0f;
}

static void get_vec2(struct vr_xr *xr, vr_input_t *in, int action, float out[2]) {
    XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO, .action = in->action[action] };
    XrActionStateVector2f st = { .type = XR_TYPE_ACTION_STATE_VECTOR2F };
    out[0] = out[1] = 0.0f;
    if (XR_SUCCEEDED(xr->api.get_action_state_vector2f(xr->session, &gi, &st)) && st.isActive) {
        out[0] = st.currentState.x;
        out[1] = st.currentState.y;
    }
}

static bool sync_and_sample(struct vr_xr *xr, vr_input_t *in, vr_pad_sample_t *s) {
    XrActiveActionSet active = { .actionSet = in->set, .subactionPath = XR_NULL_PATH };
    XrActionsSyncInfo si = { .type = XR_TYPE_ACTIONS_SYNC_INFO, .countActiveActionSets = 1, .activeActionSets = &active };
    // XR_SESSION_NOT_FOCUSED and XR_SESSION_LOSS_PENDING are success codes with every action inactive
    if (xr->api.sync_actions(xr->session, &si) != XR_SUCCESS) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->a = get_bool(xr, in, ACT_A);
    s->b = get_bool(xr, in, ACT_B);
    s->x = get_bool(xr, in, ACT_X);
    s->y = get_bool(xr, in, ACT_Y);
    s->l_bumper = get_bool(xr, in, ACT_L_BUMPER);
    s->r_bumper = get_bool(xr, in, ACT_R_BUMPER);
    s->menu = get_bool(xr, in, ACT_MENU);
    s->view = get_bool(xr, in, ACT_VIEW);
    s->l_grip = get_bool(xr, in, ACT_L_GRIP);
    s->l_stick_press = get_bool(xr, in, ACT_L_STICK_PRESS);
    s->r_stick_press = get_bool(xr, in, ACT_R_STICK_PRESS);
    s->dpad_up = get_bool(xr, in, ACT_DPAD_UP);
    s->dpad_down = get_bool(xr, in, ACT_DPAD_DOWN);
    s->dpad_left = get_bool(xr, in, ACT_DPAD_LEFT);
    s->dpad_right = get_bool(xr, in, ACT_DPAD_RIGHT);
    s->l_trigger = get_float(xr, in, ACT_L_TRIGGER);
    s->r_trigger = get_float(xr, in, ACT_R_TRIGGER);
    get_vec2(xr, in, ACT_L_STICK, s->l_stick);
    get_vec2(xr, in, ACT_R_STICK, s->r_stick);
    return true;
}

typedef struct {
    struct vr_xr *xr;
    vr_input_t *in;
} haptic_context_t;

static void apply_haptic(int hand, float amplitude, float seconds, void *user) {
    haptic_context_t *ctx = user;
    XrHapticActionInfo hai = {
        .type = XR_TYPE_HAPTIC_ACTION_INFO,
        .action = ctx->in->action[hand == SFVR_HAND_LEFT ? ACT_HAPTIC_LEFT : ACT_HAPTIC_RIGHT]
    };
    XrHapticVibration vib = {
        .type = XR_TYPE_HAPTIC_VIBRATION,
        .amplitude = amplitude,
        .duration = (XrDuration)llround((double)seconds * 1e9),
        .frequency = XR_FREQUENCY_UNSPECIFIED
    };
    XrResult r = ctx->xr->api.apply_haptic_feedback(ctx->xr->session, &hai, (const XrHapticBaseHeader *)&vib);
    if (XR_FAILED(r)) {
        vr_logf("input: haptic failed (%d)", (int)r);
    }
}

static void stop_haptics(struct vr_xr *xr, vr_input_t *in) {
    for (int hand = 0; hand < 2; hand++) {
        XrHapticActionInfo hai = {
            .type = XR_TYPE_HAPTIC_ACTION_INFO,
            .action = in->action[hand == SFVR_HAND_LEFT ? ACT_HAPTIC_LEFT : ACT_HAPTIC_RIGHT]
        };
        xr->api.stop_haptic_feedback(xr->session, &hai);
    }
}

void vr_input_update(vr_input_t *in, struct vr_xr *xr, float haptics) {
    if (!in) {
        return;
    }
    vr_pad_sample_t sample;
    bool live = xr->session_running && xr->focused && sync_and_sample(xr, in, &sample);
    if (!live) {
        // Focus loss or session stop: the game sees every input released, nothing
        // buzzes, and a held View press does not count
        sHandValid[0] = sHandValid[1] = false;
        vr_pad_release(&in->pad, &sPad);
        vr_adjustment_init(&in->adjustment);
        sfvr_haptic_queue_clear(&sHaptics);
        sRumble.on = false;
        if (in->active && xr->session != XR_NULL_HANDLE) {
            stop_haptics(xr, in);
        }
        in->active = false;
        return;
    }
    in->active = true;

    double now_s = (double)xr->frame.predicted_display_time * 1e-9;
    float world_scale = (float)vr_setting_get("world_scale");
    float camera_closer = (float)vr_setting_get("camera_closer");
    float camera_lower = (float)vr_setting_get("camera_lower");
    float old_world_scale = world_scale;
    float old_camera_closer = camera_closer;
    float old_camera_lower = camera_lower;
    if (vr_adjustment_apply(&in->adjustment, &sample, now_s,
            vr_render_adjustment_allowed(), &world_scale, &camera_closer, &camera_lower)) {
        if (world_scale != old_world_scale) vr_setting_set("world_scale", world_scale);
        if (camera_closer != old_camera_closer) vr_setting_set("camera_closer", camera_closer);
        if (camera_lower != old_camera_lower) vr_setting_set("camera_lower", camera_lower);
    }
    vr_pad_map(&in->pad, &sample, now_s, &sPad);

    // The hands' aim yaws (move_relative)
    for (int hand = 0; hand < 2; hand++) {
        sHandValid[hand] = false;
        if (in->aim_space[hand] == XR_NULL_HANDLE || xr->local_space == XR_NULL_HANDLE) {
            continue;
        }
        XrSpaceLocation loc = { .type = XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xr->api.locate_space(in->aim_space[hand], xr->local_space, xr->frame.predicted_display_time, &loc))
            && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            const float q[4] = { loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w };
            sHandYaw[hand] = vr_yaw_of(q);
            sHandValid[hand] = true;
        }
    }

    // The game's rumble as 40 ms pulses on both hands while it lasts
    if (sRumble.on) {
        if (sRumble.until_s < 0.0) {
            sRumble.until_s = now_s + sRumble.duration_s;
        }
        if (now_s >= sRumble.until_s) {
            sRumble.on = false;
        } else if (now_s >= sRumble.next_pulse_s) {
            sfvr_haptic_submit(&sHaptics, SFVR_HAND_BOTH, sRumble.strength, SFVR_RUMBLE_PULSE_SECONDS);
            sRumble.next_pulse_s = now_s + SFVR_RUMBLE_PULSE_SECONDS;
        }
    }

    haptic_context_t ctx = { xr, in };
    sfvr_haptic_flush(&sHaptics, haptics, apply_haptic, &ctx);
}
