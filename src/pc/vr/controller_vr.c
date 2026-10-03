// The VR controllers as one more ControllerAPI (SM64_VR builds only;
// docs/VR_ARCHITECTURE.md section 6). They read like an SDL GameController:
// the same virtual keys (VK_BASE_SDL_GAMEPAD + SDL_CONTROLLER_BUTTON_*), the
// same gamepad bindings (configKey*), the same stick shaping and DJUI key
// events as controller_sdl.c, so the controls menu and the menus work as they
// do with a gamepad, and the pad merges with the SDL pad and the keyboard in
// osContGetReadData().

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include <ultra64.h>

#include "pc/controller/controller_api.h"
#include "pc/controller/controller_sdl.h"
#include "pc/configfile.h"
#include "pc/djui/djui.h"
#include "pc/djui/djui_panel_pause.h"

#include "vr.h"
#include "vr_input.h"

#define MAX_VR_BINDS 32

static u32 sBinds[MAX_VR_BINDS][2];
static u32 sNumBinds = 0;
static bool sButtons[VR_PAD_BUTTONS];
static u32 sLastButton = VK_INVALID;

static void add_binds(u32 mask, const unsigned int *btns) {
    for (u32 i = 0; i < MAX_BINDS; i++) {
        if (btns[i] >= VK_BASE_SDL_GAMEPAD && btns[i] < VK_BASE_SDL_GAMEPAD + VR_PAD_BUTTONS && sNumBinds < MAX_VR_BINDS) {
            sBinds[sNumBinds][0] = btns[i] - VK_BASE_SDL_GAMEPAD;
            sBinds[sNumBinds][1] = mask;
            sNumBinds++;
        }
    }
}

static void controller_vr_bind(void) {
    memset(sBinds, 0, sizeof(sBinds));
    sNumBinds = 0;
    add_binds(A_BUTTON, configKeyA);
    add_binds(B_BUTTON, configKeyB);
    add_binds(X_BUTTON, configKeyX);
    add_binds(Y_BUTTON, configKeyY);
    add_binds(Z_TRIG, configKeyZ);
    add_binds(STICK_UP, configKeyStickUp);
    add_binds(STICK_LEFT, configKeyStickLeft);
    add_binds(STICK_DOWN, configKeyStickDown);
    add_binds(STICK_RIGHT, configKeyStickRight);
    add_binds(U_CBUTTONS, configKeyCUp);
    add_binds(L_CBUTTONS, configKeyCLeft);
    add_binds(D_CBUTTONS, configKeyCDown);
    add_binds(R_CBUTTONS, configKeyCRight);
    add_binds(L_TRIG, configKeyL);
    add_binds(R_TRIG, configKeyR);
    add_binds(START_BUTTON, configKeyStart);
    add_binds(U_JPAD, configKeyDUp);
    add_binds(D_JPAD, configKeyDDown);
    add_binds(L_JPAD, configKeyDLeft);
    add_binds(R_JPAD, configKeyDRight);
}

static void controller_vr_init(void) {
    controller_vr_bind();
}

static void update_button(int i, bool down) {
    bool pressed = !sButtons[i] && down;
    bool released = sButtons[i] && !down;
    sButtons[i] = down;
    if (pressed) {
        sLastButton = (u32)i;
        djui_panel_pause_disconnect_key_update(VK_BASE_SDL_GAMEPAD + i);
        djui_interactable_on_key_down(VK_BASE_SDL_GAMEPAD + i);
    }
    if (released) {
        djui_interactable_on_key_up(VK_BASE_SDL_GAMEPAD + i);
    }
}

// controller_sdl.c's stick shaping: radial deadzone, square-ish range
static void update_analog_stick(s8 *stick_x, s8 *stick_y, int16_t input_x, int16_t input_y) {
    float magnitude_sq = (float)(input_x * input_x) + (float)(input_y * input_y);
    float deadzone = configStickDeadzone * DEADZONE_STEP;
    if (magnitude_sq > (deadzone * deadzone)) {
        float magnitude = sqrtf(magnitude_sq);
        float dir_x = (float)input_x / magnitude;
        float dir_y = (float)input_y / magnitude;
        float scale = 1.f / fmaxf(fabsf(dir_x), fabsf(dir_y));
        float max_magnitude = 0x8000 * scale;
        magnitude -= deadzone;
        magnitude *= max_magnitude / (max_magnitude - deadzone);
        magnitude /= 0x100;
        magnitude = fminf(magnitude, scale * 127.f);
        *stick_x = dir_x * magnitude;
        *stick_y = -dir_y * magnitude;
    }
}

static void controller_vr_read(OSContPad *pad) {
    if (!vr_enabled()) {
        return;
    }
    const vr_gamepad_t *vp = vr_input_gamepad();
    for (int i = 0; i < VR_PAD_BUTTONS; i++) {
        update_button(i, vp->live && vp->buttons[i]);
    }
    vr_input_pad_consumed();
    if (!vp->live) {
        return;
    }

    u32 buttons_down = 0;
    for (u32 i = 0; i < sNumBinds; i++) {
        if (sButtons[sBinds[i][0]]) {
            buttons_down |= sBinds[i][1];
        }
    }
    pad->button |= buttons_down;

    const u32 xstick = buttons_down & STICK_XMASK;
    const u32 ystick = buttons_down & STICK_YMASK;
    if (xstick == STICK_LEFT) {
        pad->stick_x = -128;
    } else if (xstick == STICK_RIGHT) {
        pad->stick_x = 127;
    }
    if (ystick == STICK_DOWN) {
        pad->stick_y = -128;
    } else if (ystick == STICK_UP) {
        pad->stick_y = 127;
    }

    if (vp->right_x < -0x4000) { pad->button |= L_CBUTTONS; }
    if (vp->right_x > 0x4000) { pad->button |= R_CBUTTONS; }
    if (vp->right_y < -0x4000) { pad->button |= U_CBUTTONS; }
    if (vp->right_y > 0x4000) { pad->button |= D_CBUTTONS; }

    // Merge: the VR sticks win only when they are pushed
    s8 sx = 0, sy = 0, ex = 0, ey = 0;
    update_analog_stick(&sx, &sy, vp->left_x, vp->left_y);
    update_analog_stick(&ex, &ey, vp->right_x, vp->right_y);
    if (sx != 0 || sy != 0) {
        pad->stick_x = sx;
        pad->stick_y = sy;
    }
    if (ex != 0 || ey != 0) {
        pad->ext_stick_x = ex;
        pad->ext_stick_y = ey;
    }
}

static u32 controller_vr_rawkey(void) {
    if (sLastButton != VK_INVALID) {
        u32 r = sLastButton;
        sLastButton = VK_INVALID;
        return r;
    }
    return VK_INVALID;
}

static void controller_vr_rumble_play(float strength, float seconds) {
    vr_haptic_rumble_play(strength, seconds);
}

static void controller_vr_rumble_stop(void) {
    vr_haptic_rumble_stop();
}

static void controller_vr_shutdown(void) {
    memset(sButtons, 0, sizeof(sButtons));
}

struct ControllerAPI controller_vr = {
    VK_BASE_SDL_GAMEPAD,
    controller_vr_init,
    controller_vr_read,
    controller_vr_rawkey,
    controller_vr_rumble_play,
    controller_vr_rumble_stop,
    controller_vr_bind,
    controller_vr_shutdown
};
