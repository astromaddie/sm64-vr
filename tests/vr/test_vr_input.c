// The system layer and the virtual pad (src/pc/vr/vr_input.c, vr_pad_map):
// standard.md section 1. No runtime.

#include <string.h>

#include "sfvr_test.h"
#include "vr_input.h"
#include "vr_runtime_api.h"
#include "vr_xr.h"

static void drain(void) {
	while (vr_take_recentre_request() || vr_take_height_request()) {
	}
}

static vr_pad_sample_t sample_view(bool down) {
	vr_pad_sample_t s;
	memset(&s, 0, sizeof(s));
	s.view = down;
	return s;
}

static void test_view_short_press_is_back(void) {
	vr_pad_t pad;
	vr_gamepad_t out;
	vr_pad_init(&pad);
	drain();
	vr_pad_sample_t up = sample_view(false), down = sample_view(true);
	vr_pad_map(&pad, &up, 0.0, &out);
	vr_pad_map(&pad, &down, 0.1, &out);
	CHECK(!out.buttons[VR_PAD_BACK]);
	unsigned ev = vr_pad_map(&pad, &up, 0.4, &out);
	CHECK(ev & SFVR_VIEW_SHORT_PRESS);
	CHECK(out.buttons[VR_PAD_BACK]);
	CHECK(!vr_take_recentre_request());
	CHECK(!vr_take_height_request());
}

static void test_view_holds_recentre_then_height(void) {
	vr_pad_t pad;
	vr_gamepad_t out;
	vr_pad_init(&pad);
	drain();
	vr_pad_sample_t up = sample_view(false), down = sample_view(true);
	vr_pad_map(&pad, &up, 0.0, &out);
	vr_pad_map(&pad, &down, 0.1, &out);
	vr_pad_map(&pad, &down, 0.9, &out);
	CHECK(!vr_take_recentre_request());
	// 1 s: a recentre that keeps the standing height
	unsigned ev = vr_pad_map(&pad, &down, 1.15, &out);
	CHECK(ev & SFVR_VIEW_RECENTRE);
	CHECK(vr_take_recentre_request());
	CHECK(!vr_take_height_request());
	vr_pad_map(&pad, &down, 2.0, &out);
	CHECK(!vr_take_recentre_request());
	// 3 s: the standing height too
	ev = vr_pad_map(&pad, &down, 3.2, &out);
	CHECK(ev & SFVR_VIEW_RECALIBRATE_HEIGHT);
	CHECK(vr_take_height_request());
	// Released after a hold: no Back
	vr_pad_map(&pad, &up, 3.5, &out);
	CHECK(!out.buttons[VR_PAD_BACK]);
}

static void test_menu_is_start_and_sticks(void) {
	vr_pad_t pad;
	vr_gamepad_t out;
	vr_pad_init(&pad);
	vr_pad_sample_t s;
	memset(&s, 0, sizeof(s));
	s.menu = true;
	s.r_stick[0] = 1.0f;
	s.l_stick[1] = 1.0f;
	s.dpad_left = true;
	vr_pad_map(&pad, &s, 0.0, &out);
	CHECK(out.buttons[VR_PAD_START]);
	CHECK(out.buttons[VR_PAD_DPAD_LEFT]);
	CHECK_INT(out.right_x, 32767); // the turn reads it (controller_vr.c keeps it from the game)
	CHECK_INT(out.left_y, -32767); // SDL's y is down
}


static void test_frame_grip_binding_is_distinct_and_frame_only(void) {
    const vr_input_profile_paths_t *profiles = NULL;
    CHECK_INT(vr_input_profiles(&profiles), 3);
    int frame_grip = 0, frame_bumper = 0;
    for (int i = 0; i < profiles[0].count; i++) {
        if (strcmp(profiles[0].paths[i], "/user/hand/left/input/squeeze/click") == 0) frame_grip++;
        if (strcmp(profiles[0].paths[i], "/user/hand/left/input/bumper/click") == 0) frame_bumper++;
    }
    CHECK_INT(frame_grip, 1);
    CHECK_INT(frame_bumper, 1);
    // Index and Touch keep the existing squeeze-as-bumper fallback; they do
    // not acquire the new click grip, which would double-map the modifier.
    for (int p = 1; p < 3; p++) {
        int fallback_squeeze = 0;
        for (int i = 0; i < profiles[p].count; i++) {
            CHECK(strcmp(profiles[p].paths[i], "/user/hand/left/input/squeeze/click") != 0);
            if (strcmp(profiles[p].paths[i], "/user/hand/left/input/squeeze/value") == 0) fallback_squeeze++;
        }
        CHECK_INT(fallback_squeeze, 1);
    }
}

static vr_pad_sample_t adjustment_sample(float left_x, float right_x, float right_y) {
    vr_pad_sample_t s;
    memset(&s, 0, sizeof(s));
    s.l_grip = true;
    s.l_stick[0] = left_x;
    s.r_stick[0] = right_x;
    s.r_stick[1] = right_y;
    return s;
}

static void test_adjustment_scale_camera_directions_and_consumption(void) {
    vr_adjustment_t state;
    vr_adjustment_init(&state);
    float scale = 104.0f, closer = 0.2f, lower = 0.4f;
    vr_pad_sample_t s = adjustment_sample(-1.0f, -1.0f, 1.0f);
    CHECK(!vr_adjustment_apply(&state, &s, 1.0, true, &scale, &closer, &lower)); // first timestamp anchors
    s = adjustment_sample(-1.0f, -1.0f, 1.0f);
    CHECK(vr_adjustment_apply(&state, &s, 1.01, true, &scale, &closer, &lower));
    CHECK_NEAR(scale, 104.0 * pow(1.5, 0.01), 1e-3); // left shrinks the world; inverse units/metre rises
    CHECK_NEAR(closer, 0.1975, 1e-5);
    CHECK_NEAR(lower, 0.3975, 1e-5);
    CHECK_NEAR(s.l_stick[0], -1, 1e-9); // movement remains available
    CHECK_NEAR(s.r_stick[0], 0, 1e-9);
    CHECK_NEAR(s.r_stick[1], 0, 1e-9);
    vr_pad_t pad;
    vr_gamepad_t out;
    vr_pad_init(&pad);
    vr_pad_map(&pad, &s, 1.01, &out);
    CHECK_INT(out.left_x, -32767); // the left stick still moves Mario
    CHECK_INT(out.right_x, 0); // turn/C-button paths receive neutral right stick
    CHECK_INT(out.right_y, 0);

    s = adjustment_sample(1.0f, 1.0f, -1.0f);
    CHECK(vr_adjustment_apply(&state, &s, 1.02, true, &scale, &closer, &lower));
    CHECK_NEAR(scale, 104.0, 1e-3); // right reverses left's scale step
    CHECK_NEAR(closer, 0.2, 1e-5); // down moves closer/lower
    CHECK_NEAR(lower, 0.4, 1e-5);
}

static void test_adjustment_rate_deadzone_and_stall_bound(void) {
    vr_adjustment_t a, b;
    vr_adjustment_init(&a);
    vr_adjustment_init(&b);
    vr_adjustment_t c;
    vr_adjustment_init(&c);
    float scale_a = 104, close_a = 0, lower_a = 0;
    float scale_b = 104, close_b = 0, lower_b = 0;
    float scale_c = 104, close_c = 0, lower_c = 0;
    vr_pad_sample_t sa = adjustment_sample(0, -1, 0);
    vr_pad_sample_t sb = sa;
    vr_pad_sample_t sc = sa;
    vr_adjustment_apply(&a, &sa, 2.0, true, &scale_a, &close_a, &lower_a);
    vr_adjustment_apply(&b, &sb, 2.0, true, &scale_b, &close_b, &lower_b);
    vr_adjustment_apply(&c, &sc, 2.0, true, &scale_c, &close_c, &lower_c);
    for (int i = 1; i <= 120; i++) {
        sa = adjustment_sample(0, -1, 0);
        vr_adjustment_apply(&a, &sa, 2.0 + i / 120.0, true, &scale_a, &close_a, &lower_a);
    }
    for (int i = 1; i <= 90; i++) {
        sb = adjustment_sample(0, -1, 0);
        vr_adjustment_apply(&b, &sb, 2.0 + i / 90.0, true, &scale_b, &close_b, &lower_b);
    }
    for (int i = 1; i <= 72; i++) {
        sc = adjustment_sample(0, -1, 0);
        vr_adjustment_apply(&c, &sc, 2.0 + i / 72.0, true, &scale_c, &close_c, &lower_c);
    }
    CHECK_NEAR(scale_a, scale_b, 1e-3);
    CHECK_NEAR(scale_a, scale_c, 1e-3);

    vr_adjustment_t dead;
    vr_adjustment_init(&dead);
    float scale = 104, closer = 0, lower = 0;
    vr_pad_sample_t s = adjustment_sample(0, -0.19f, 0.19f);
    vr_adjustment_apply(&dead, &s, 1.0, true, &scale, &closer, &lower);
    CHECK(!vr_adjustment_apply(&dead, &s, 1.01, true, &scale, &closer, &lower));
    CHECK_NEAR(scale, 104, 1e-6);
    CHECK_NEAR(closer, 0, 1e-6);

    s = adjustment_sample(0, -1, 0);
    vr_adjustment_apply(&dead, &s, 10.0, true, &scale, &closer, &lower);
    CHECK_NEAR(scale, 104.0 * pow(1.5, 1.0 / 30.0), 1e-3); // stalled display contributes at most 1/30 s

    vr_adjustment_t limits;
    vr_adjustment_init(&limits);
    float at_min = 20, at_max_close = 0.8f, at_max_lower = 1.0f;
    s = adjustment_sample(0, 1, -1);
    vr_adjustment_apply(&limits, &s, 3.0, true, &at_min, &at_max_close, &at_max_lower);
    s = adjustment_sample(0, 1, -1);
    CHECK(!vr_adjustment_apply(&limits, &s, 3.01, true, &at_min, &at_max_close, &at_max_lower));
    CHECK_NEAR(at_min, 20, 1e-6);
    CHECK_NEAR(at_max_close, 0.8, 1e-6);
    CHECK_NEAR(at_max_lower, 1, 1e-6);
    CHECK_NEAR(s.r_stick[0], 0, 1e-6); // limit does not re-enable turn/C-button input
    CHECK_NEAR(s.r_stick[1], 0, 1e-6);
}

static void test_adjustment_gates_and_release_neutralization(void) {
    vr_adjustment_t state;
    vr_adjustment_init(&state);
    float scale = 104, closer = 0.2f, lower = 0.3f;
    vr_pad_sample_t s = adjustment_sample(-1, 1, 1);
    CHECK(!vr_adjustment_apply(&state, &s, 1.0, false, &scale, &closer, &lower));
    CHECK_NEAR(scale, 104, 1e-6);
    CHECK_NEAR(s.l_stick[0], -1, 1e-6); // title/cinema gate leaves ordinary controls intact

    // Menu and View remain system actions and prevent a settings change.
    state.display_time_valid = true;
    state.last_display_s = 1.0;
    s = adjustment_sample(-1, 1, 1);
    s.menu = true;
    CHECK(!vr_adjustment_apply(&state, &s, 1.01, true, &scale, &closer, &lower));
    CHECK_NEAR(scale, 104, 1e-6);
    CHECK_NEAR(s.r_stick[0], 0, 1e-6);
    s = adjustment_sample(-1, 0, 0);
    s.view = true;
    state.last_display_s = 1.01;
    CHECK(!vr_adjustment_apply(&state, &s, 1.02, true, &scale, &closer, &lower));
    CHECK_NEAR(scale, 104, 1e-6);

    // Grip consumes the right stick. A still-deflected direction cannot become
    // a turn/C-button after release; centering re-arms it.
    state.last_display_s = 1.02;
    s = adjustment_sample(0, 1, 0);
    vr_adjustment_apply(&state, &s, 1.03, true, &scale, &closer, &lower);
    CHECK(state.right_waiting_for_neutral);
    s = adjustment_sample(0, 0.8f, 0);
    s.l_grip = false;
    vr_adjustment_apply(&state, &s, 1.04, true, &scale, &closer, &lower);
    CHECK_NEAR(s.r_stick[0], 0, 1e-6);
    CHECK_NEAR(s.l_stick[0], 0, 1e-6);
    CHECK(state.right_waiting_for_neutral);
    s.r_stick[0] = 0.1f;
    vr_adjustment_apply(&state, &s, 1.05, true, &scale, &closer, &lower);
    CHECK(!state.right_waiting_for_neutral);
    CHECK_NEAR(s.r_stick[0], 0.1, 1e-6);
}

static void test_adjustment_preserves_view_and_menu_pad_actions(void) {
    vr_adjustment_t adjustment;
    vr_pad_t pad;
    vr_gamepad_t out;
    vr_adjustment_init(&adjustment);
    vr_pad_init(&pad);
    vr_pad_sample_t s = adjustment_sample(-1, 1, 1);
    s.menu = true;
    s.view = true;
    float scale = 104, closer = 0, lower = 0;
    vr_adjustment_apply(&adjustment, &s, 1.0, true, &scale, &closer, &lower);
    CHECK(s.menu && s.view); // the modifier does not consume either system button
    vr_pad_sample_t released = {0};
    vr_pad_map(&pad, &released, 0.0, &out);
    vr_pad_map(&pad, &s, 0.1, &out);
    CHECK(out.buttons[VR_PAD_START]);
    vr_pad_map(&pad, &released, 0.4, &out);
    CHECK(out.buttons[VR_PAD_BACK]); // View still makes the ordinary Back pulse
}


// A small fake OpenXR action runtime exercises vr_input_create/update rather
// than a copied binding fixture. The game's setting functions and renderer gate
// are the only host-boundary stubs.
#define FAKE_ACTIONS 32
#define FAKE_PATHS 96
static struct {
    char names[FAKE_ACTIONS][XR_MAX_ACTION_NAME_SIZE];
    XrActionType types[FAKE_ACTIONS];
    int action_count;
    struct { char path[XR_MAX_PATH_LENGTH]; XrPath id; } paths[FAKE_PATHS];
    int path_count;
    bool booleans[FAKE_ACTIONS];
    XrVector2f vectors[FAKE_ACTIONS];
    bool grip_bound, bumper_bound, names_unique;
    bool allow_adjustment, sync_ok;
    float world_scale, camera_closer, camera_lower;
    int settings_writes, haptic_stops;
} fake_input;

static int fake_action_index(XrAction action) {
    int index = (int)(uintptr_t)action - 1;
    return index >= 0 && index < fake_input.action_count ? index : -1;
}

static const char *fake_path_string(XrPath path) {
    for (int i = 0; i < fake_input.path_count; i++) {
        if (fake_input.paths[i].id == path) return fake_input.paths[i].path;
    }
    return "";
}

static XrResult XRAPI_PTR fake_string_to_path(XrInstance instance, const char *string, XrPath *out) {
    (void)instance;
    for (int i = 0; i < fake_input.path_count; i++) {
        if (strcmp(fake_input.paths[i].path, string) == 0) { *out = fake_input.paths[i].id; return XR_SUCCESS; }
    }
    if (fake_input.path_count >= FAKE_PATHS) return XR_ERROR_LIMIT_REACHED;
    int i = fake_input.path_count++;
    snprintf(fake_input.paths[i].path, sizeof(fake_input.paths[i].path), "%s", string);
    fake_input.paths[i].id = (XrPath)(uintptr_t)(100 + i);
    *out = fake_input.paths[i].id;
    return XR_SUCCESS;
}

static XrResult XRAPI_PTR fake_create_action_set(XrInstance instance, const XrActionSetCreateInfo *info, XrActionSet *out) {
    (void)instance; (void)info; *out = (XrActionSet)(uintptr_t)1; return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_destroy_action_set(XrActionSet set) { (void)set; return XR_SUCCESS; }
static XrResult XRAPI_PTR fake_create_action(XrActionSet set, const XrActionCreateInfo *info, XrAction *out) {
    (void)set;
    int i = fake_input.action_count++;
    if (i >= FAKE_ACTIONS) return XR_ERROR_LIMIT_REACHED;
    snprintf(fake_input.names[i], sizeof(fake_input.names[i]), "%s", info->actionName);
    fake_input.types[i] = info->actionType;
    *out = (XrAction)(uintptr_t)(i + 1);
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_suggest(XrInstance instance, const XrInteractionProfileSuggestedBinding *info) {
    (void)instance;
    if (strcmp(fake_path_string(info->interactionProfile), "/interaction_profiles/valve/frame_controller_valve") == 0) {
        for (uint32_t i = 0; i < info->countSuggestedBindings; i++) {
            const char *path = fake_path_string(info->suggestedBindings[i].binding);
            int action = fake_action_index(info->suggestedBindings[i].action);
            if (strcmp(path, "/user/hand/left/input/squeeze/click") == 0 && action >= 0
                && strcmp(fake_input.names[action], "left_grip") == 0) fake_input.grip_bound = true;
            if (strcmp(path, "/user/hand/left/input/bumper/click") == 0 && action >= 0
                && strcmp(fake_input.names[action], "l_bumper") == 0) fake_input.bumper_bound = true;
        }
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_attach(XrSession session, const XrSessionActionSetsAttachInfo *info) {
    (void)session; (void)info; return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_sync(XrSession session, const XrActionsSyncInfo *info) {
    (void)session; (void)info; return fake_input.sync_ok ? XR_SUCCESS : XR_ERROR_CALL_ORDER_INVALID;
}
static XrResult XRAPI_PTR fake_get_bool(XrSession session, const XrActionStateGetInfo *info, XrActionStateBoolean *out) {
    (void)session; int i = fake_action_index(info->action);
    out->isActive = i >= 0; out->currentState = i >= 0 && fake_input.booleans[i];
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_get_float(XrSession session, const XrActionStateGetInfo *info, XrActionStateFloat *out) {
    (void)session; (void)info; out->isActive = true; out->currentState = 0; return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_get_vec2(XrSession session, const XrActionStateGetInfo *info, XrActionStateVector2f *out) {
    (void)session; int i = fake_action_index(info->action);
    out->isActive = i >= 0; out->currentState = i >= 0 ? fake_input.vectors[i] : (XrVector2f){0,0};
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR fake_stop_haptic(XrSession session, const XrHapticActionInfo *info) {
    (void)session; (void)info; fake_input.haptic_stops++; return XR_SUCCESS;
}

// These symbols are the existing public runtime API, stubbed at the host
// boundary while vr_input_create/update and the actual pad mapper remain real.
double vr_setting_get(const char *key) {
    if (strcmp(key, "world_scale") == 0) return fake_input.world_scale;
    if (strcmp(key, "camera_closer") == 0) return fake_input.camera_closer;
    if (strcmp(key, "camera_lower") == 0) return fake_input.camera_lower;
    return 0;
}
bool vr_setting_set(const char *key, double value) {
    float *slot = strcmp(key, "world_scale") == 0 ? &fake_input.world_scale
        : strcmp(key, "camera_closer") == 0 ? &fake_input.camera_closer
        : strcmp(key, "camera_lower") == 0 ? &fake_input.camera_lower : NULL;
    if (!slot || *slot == (float)value) return false;
    float min = strcmp(key, "world_scale") == 0 ? 20.0f : 0.0f;
    float max = strcmp(key, "world_scale") == 0 ? 400.0f
        : strcmp(key, "camera_closer") == 0 ? 0.8f : 1.0f;
    *slot = value < min ? min : value > max ? max : (float)value;
    fake_input.settings_writes++;
    return true;
}
bool vr_render_adjustment_allowed(void) { return fake_input.allow_adjustment; }

static int fake_find_action(const char *name) {
    for (int i = 0; i < fake_input.action_count; i++) if (strcmp(fake_input.names[i], name) == 0) return i;
    return -1;
}

static void test_fake_xr_grip_to_live_adjustment_production_path(void) {
    memset(&fake_input, 0, sizeof(fake_input));
    fake_input.sync_ok = fake_input.allow_adjustment = true;
    fake_input.world_scale = 104.0f;
    fake_input.camera_closer = 0.2f;
    fake_input.camera_lower = 0.4f;
    vr_xr_t xr;
    memset(&xr, 0, sizeof(xr));
    xr.instance = (XrInstance)(uintptr_t)0x10;
    xr.session = (XrSession)(uintptr_t)0x20;
    xr.session_running = xr.focused = xr.ext_frame_controller = true;
    xr.frame.predicted_display_time = 1000000000;
    xr.api.string_to_path = fake_string_to_path;
    xr.api.create_action_set = fake_create_action_set;
    xr.api.destroy_action_set = fake_destroy_action_set;
    xr.api.create_action = fake_create_action;
    xr.api.suggest_interaction_profile_bindings = fake_suggest;
    xr.api.attach_session_action_sets = fake_attach;
    xr.api.sync_actions = fake_sync;
    xr.api.get_action_state_boolean = fake_get_bool;
    xr.api.get_action_state_float = fake_get_float;
    xr.api.get_action_state_vector2f = fake_get_vec2;
    xr.api.stop_haptic_feedback = fake_stop_haptic;

    vr_input_t *input = vr_input_create(&xr);
    CHECK(input != NULL);
    CHECK(fake_input.grip_bound);
    CHECK(fake_input.bumper_bound);
    int grip = fake_find_action("left_grip");
    CHECK(grip >= 0);
    if (grip >= 0) CHECK_INT(fake_input.types[grip], XR_ACTION_TYPE_BOOLEAN_INPUT);
    CHECK(fake_input.action_count > 0);
    fake_input.names_unique = true;
    for (int i = 0; i < fake_input.action_count; i++) {
        for (int j = i + 1; j < fake_input.action_count; j++) {
            if (strcmp(fake_input.names[i], fake_input.names[j]) == 0) fake_input.names_unique = false;
        }
    }
    CHECK(fake_input.names_unique);

    grip = fake_find_action("left_grip");
    int left = fake_find_action("l_stick");
    int right = fake_find_action("r_stick");
    CHECK(grip >= 0 && left >= 0 && right >= 0);
    fake_input.booleans[grip] = true;
    fake_input.vectors[left] = (XrVector2f){0.5f, -0.25f};
    fake_input.vectors[right] = (XrVector2f){-1.0f, 1.0f};
    vr_input_update(input, &xr, 0.6f); // first display timestamp anchors
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK_NEAR(fake_input.world_scale, 104.0 * pow(1.5, 0.01), 1e-3);
    CHECK_NEAR(fake_input.camera_closer, 0.1975, 1e-5);
    CHECK_NEAR(fake_input.camera_lower, 0.3975, 1e-5);
    CHECK_INT(fake_input.settings_writes, 3);
    float untouched_closer = fake_input.camera_closer;
    float untouched_lower = fake_input.camera_lower;
    fake_input.vectors[right] = (XrVector2f){-0.5f, 0.0f};
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK_INT(fake_input.settings_writes, 4); // only world_scale changed
    CHECK_NEAR(fake_input.camera_closer, untouched_closer, 0);
    CHECK_NEAR(fake_input.camera_lower, untouched_lower, 0);
    const vr_gamepad_t *pad = vr_input_gamepad();
    CHECK(pad->live);
    CHECK(pad->left_x > 0); // left movement still reaches the production pad mapper
    CHECK_INT(pad->right_x, 0);
    CHECK_INT(pad->right_y, 0);

    // A neutral frame re-arms the right stick after the modifier. Outside the
    // gameplay gate, it is again the ordinary turn input and settings stay put.
    fake_input.booleans[grip] = false;
    fake_input.vectors[right] = (XrVector2f){0.0f, 0.0f};
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    fake_input.allow_adjustment = false;
    fake_input.vectors[right] = (XrVector2f){-1.0f, 1.0f};
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK_INT(vr_input_gamepad()->right_x, -32767);
    CHECK_INT(fake_input.settings_writes, 4);

    // Holding grip while the right stick remains deflected consumes it until centered.
    fake_input.allow_adjustment = true;
    fake_input.booleans[grip] = true;
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    fake_input.booleans[grip] = false;
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK_INT(vr_input_gamepad()->right_x, 0);
    fake_input.vectors[right] = (XrVector2f){0.0f, 0.0f};
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    fake_input.vectors[right] = (XrVector2f){1.0f, 0.0f};
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK_INT(vr_input_gamepad()->right_x, 32767);

    // Focus loss releases all buttons and clears the transient suppression state.
    fake_input.booleans[grip] = true;
    xr.focused = false;
    vr_input_update(input, &xr, 0.6f);
    CHECK(!vr_input_gamepad()->live);
    CHECK(fake_input.haptic_stops > 0);
    xr.focused = true;
    fake_input.booleans[grip] = false;
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK_INT(vr_input_gamepad()->right_x, 32767);

    fake_input.sync_ok = false;
    xr.frame.predicted_display_time += 10000000;
    vr_input_update(input, &xr, 0.6f);
    CHECK(!vr_input_gamepad()->live);
    vr_input_destroy(input, &xr);
}

int main(void) {
	RUN(test_view_short_press_is_back);
	RUN(test_view_holds_recentre_then_height);
	RUN(test_menu_is_start_and_sticks);
	RUN(test_frame_grip_binding_is_distinct_and_frame_only);
	RUN(test_adjustment_scale_camera_directions_and_consumption);
	RUN(test_adjustment_rate_deadzone_and_stall_bound);
	RUN(test_adjustment_gates_and_release_neutralization);
	RUN(test_adjustment_preserves_view_and_menu_pad_actions);
	RUN(test_fake_xr_grip_to_live_adjustment_production_path);
	return SFVR_TEST_RESULT("vr_input");
}
