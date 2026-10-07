#ifdef SM64_VR

#include <stdio.h>
#include <string.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_vr.h"
#include "djui_panel_pause.h"
#include "game/camera.h"
#include "game/level_update.h"
#include "pc/vr/vr.h"
#include "pc/vr/vr_game.h"
#include "pc/vr/vr_log.h"
#include "pc/vr/vr_menu.h"
#include "pc/vr/vr_preview.h"

// A little wider than the game's own option panels (500 + padding): the labels carry units
// and the page is read on the HUD quad, where DJUI's 32 px text is about 1 degree tall
#define VR_PANEL_WIDTH 640.0f

#define MAX_ROWS 16

// One widget of the open tab. The widgets point at these values, so they live here.
static struct {
    size_t entry;
    unsigned int value; // choice index or slider stop
    bool on; // toggle
    struct DjuiBase* widget;
} sRows[MAX_ROWS];
static int sRowCount = 0;

static const char* sTabShortNames[VR_TAB_COUNT] = {
    [VR_TAB_COMFORT] = "Comfort",
    [VR_TAB_CONTROLS] = "Controls",
    [VR_TAB_HUD] = "HUD & menus",
    [VR_TAB_DISPLAY] = "Display",
    [VR_TAB_ACCESSIBILITY] = "Accessibility",
    [VR_TAB_ROOM] = "Room",
};

static void djui_panel_vr_widen(struct DjuiThreePanel* panel) {
    djui_base_set_size(&panel->base, VR_PANEL_WIDTH, panel->base.height.value);
}

// Closing a panel (back, or the whole menu) writes what was changed; a tab's rows go with it
static void djui_panel_vr_destroy(UNUSED struct DjuiBase* caller) {
    sRowCount = 0;
    vr_settings_flush();
}

// The page's first panel going (back from it, or the whole menu closing) closes the page
static void djui_panel_vr_root_destroy(struct DjuiBase* caller) {
    gVrSettingsPage.open = false;
    vr_logf("settings page: closed");
    djui_panel_vr_destroy(caller);
}

static void djui_panel_vr_slider_format(struct DjuiBase* base, unsigned int value, char* out, size_t size) {
    int row = (int)base->tag;
    if (row < 0 || row >= sRowCount) { out[0] = '\0'; return; }
    vr_menu_slider_text(vr_menu_entry(sRows[row].entry), (int)value, out, size);
}

static void djui_panel_vr_row_changed(struct DjuiBase* caller) {
    int row = (int)caller->tag;
    if (row < 0 || row >= sRowCount) { return; }
    const vr_menu_entry_t* e = vr_menu_entry(sRows[row].entry);
    switch (e->widget) {
        case VR_MENU_TOGGLE:
            vr_setting_set(e->key, sRows[row].on ? 1.0 : 0.0);
            break;
        case VR_MENU_CHOICE:
            vr_setting_set(e->key, vr_menu_choice_value(e, (int)sRows[row].value));
            break;
        case VR_MENU_SLIDER:
            vr_setting_set(e->key, vr_menu_slider_value(e, (int)sRows[row].value));
            break;
        default:
            break;
    }
}

// Shows the setting's current value in the row's widget
static void djui_panel_vr_row_refresh(int row) {
    const vr_menu_entry_t* e = vr_menu_entry(sRows[row].entry);
    double current = vr_setting_get(e->key);
    switch (e->widget) {
        case VR_MENU_TOGGLE: {
            struct DjuiCheckbox* checkbox = (struct DjuiCheckbox*)sRows[row].widget;
            sRows[row].on = current != 0;
            djui_base_set_visible(&checkbox->rectValue->base, sRows[row].on);
            break;
        }
        case VR_MENU_CHOICE:
            sRows[row].value = (unsigned int)vr_menu_choice_index(e, current);
            djui_selectionbox_update_value(sRows[row].widget);
            break;
        case VR_MENU_SLIDER:
            sRows[row].value = (unsigned int)vr_menu_slider_stop(e, current);
            djui_slider_update_value(sRows[row].widget);
            break;
        default:
            break;
    }
}

static void djui_panel_vr_defaults(UNUSED struct DjuiBase* caller) {
    for (int row = 0; row < sRowCount; row++) {
        vr_setting_reset(vr_menu_entry(sRows[row].entry)->key);
        djui_panel_vr_row_refresh(row);
    }
}

static void djui_panel_vr_button(struct DjuiBase* caller) {
    const vr_menu_entry_t* e = vr_menu_entry((size_t)caller->tag);
    if (e->action == VR_MENU_ACTION_RECENTRE) {
        vr_recentre_now();
    }
}

static void djui_panel_vr_add_row(struct DjuiBase* body, size_t entry) {
    const vr_menu_entry_t* e = vr_menu_entry(entry);

    if (e->widget == VR_MENU_BUTTON) {
        struct DjuiButton* button = djui_button_create(body, e->label, DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_button);
        button->base.tag = (s64)entry;
        return;
    }
    if (sRowCount >= MAX_ROWS) { return; }

    int row = sRowCount++;
    double current = vr_setting_get(e->key);
    sRows[row].entry = entry;

    switch (e->widget) {
        case VR_MENU_TOGGLE: {
            sRows[row].on = current != 0;
            struct DjuiCheckbox* checkbox = djui_checkbox_create(body, e->label, &sRows[row].on, djui_panel_vr_row_changed);
            sRows[row].widget = &checkbox->base;
            break;
        }
        case VR_MENU_CHOICE: {
            int count = vr_menu_choice_count(e);
            char* choices[16];
            if (count > 16) { count = 16; }
            for (int i = 0; i < count; i++) { choices[i] = (char*)vr_menu_choice_label(e, i); }
            sRows[row].value = (unsigned int)vr_menu_choice_index(e, current);
            struct DjuiSelectionbox* selectionbox = djui_selectionbox_create(body, e->label, choices, (u8)count, &sRows[row].value, djui_panel_vr_row_changed);
            sRows[row].widget = &selectionbox->base;
            break;
        }
        default: {
            int count = vr_menu_slider_count(e);
            sRows[row].value = (unsigned int)vr_menu_slider_stop(e, current);
            struct DjuiSlider* slider = djui_slider_create(body, e->label, &sRows[row].value, 0, (unsigned int)(count > 1 ? count - 1 : 1), djui_panel_vr_row_changed);
            sRows[row].widget = &slider->base;
            sRows[row].widget->tag = row;
            slider->format = djui_panel_vr_slider_format;
            djui_slider_update_value(&slider->base);
            break;
        }
    }
    sRows[row].widget->tag = row;
}

static void djui_panel_vr_tab_open(struct DjuiBase* caller, vr_menu_tab_t tab) {
    sRowCount = 0;

    struct DjuiThreePanel* panel = djui_panel_menu_create((char*)sTabShortNames[tab], false);
    djui_panel_vr_widen(panel);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        size_t entries[32];
        size_t count = vr_menu_tab_entries(tab, entries, 32);
        if (count > 32) { count = 32; }
        for (size_t i = 0; i < count; i++) {
            djui_panel_vr_add_row(body, entries[i]);
        }

        djui_button_create(body, "Defaults", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_defaults);
        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    struct DjuiPanel* p = djui_panel_add(caller, panel, NULL);
    if (p) { p->on_panel_destroy = djui_panel_vr_destroy; }
}

static void djui_panel_vr_tab_create(struct DjuiBase* caller) {
    djui_panel_vr_tab_open(caller, (vr_menu_tab_t)caller->tag);
}

void djui_panel_vr_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("VR settings", false);
    djui_panel_vr_widen(panel);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        vr_menu_tab_t tabs[VR_TAB_COUNT];
        size_t count = vr_menu_tabs(tabs, VR_TAB_COUNT);
        for (size_t i = 0; i < count; i++) {
            struct DjuiButton* button = djui_button_create(body, vr_menu_tab_name(tabs[i]), DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tab_create);
            button->base.tag = (s64)tabs[i];
        }

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    struct DjuiPanel* p = djui_panel_add(caller, panel, NULL);
    if (p) {
        p->on_panel_destroy = djui_panel_vr_root_destroy;
        gVrSettingsPage.open = true;
        vr_logf("settings page: open");
    }
}

// -----------------------------------------------------------------------------
// diag_settings_preview: the page over the gameplay view, unattended

static vr_preview_diag_t sDiag;
static double sDiagSaved[2];
static const char* const sDiagKeys[2] = { "world_scale", "camera_closer" };

// Moves the open tab's row of `key` to the stop nearest `setting`, as the pad would
static bool djui_panel_vr_diag_set_row(const char* key, double setting) {
    for (int row = 0; row < sRowCount; row++) {
        const vr_menu_entry_t* e = vr_menu_entry(sRows[row].entry);
        if (e->key && strcmp(e->key, key) == 0 && e->widget == VR_MENU_SLIDER) {
            sRows[row].value = (unsigned int)vr_menu_slider_stop(e, setting);
            djui_slider_update_value(sRows[row].widget);
            djui_panel_vr_row_changed(sRows[row].widget);
            return true;
        }
    }
    return false;
}

void djui_panel_vr_diag_tick(u16* buttons) {
    float delay = (float)vr_setting_get("diag_settings_preview");
    bool gameplay = gVrTick.rendered_area && gVrTick.gameplay_camera && !gVrTick.in_main_menu
        && !gVrTick.star_select && !gVrTick.transition && !gVrTick.cutscene;
    vr_preview_diag_step_t step = vr_preview_diag_tick(&sDiag, delay, gameplay, 1.0f / 30.0f);
    if (step == VR_PREVIEW_DIAG_NONE) {
        return;
    }
    switch (step) {
        case VR_PREVIEW_DIAG_PAUSE:
        case VR_PREVIEW_DIAG_RESUME:
            *buttons |= START_BUTTON;
            break;
        case VR_PREVIEW_DIAG_PAUSE_MENU:
            djui_panel_pause_create(NULL);
            break;
        case VR_PREVIEW_DIAG_OPEN_PAGE:
            if (gDjuiPanelPauseCreated && !gVrSettingsPage.open) {
                djui_panel_vr_create(NULL);
                djui_panel_vr_tab_open(NULL, VR_TAB_ROOM);
            }
            break;
        case VR_PREVIEW_DIAG_STEP:
            for (int i = 0; i < 2; i++) { sDiagSaved[i] = vr_setting_get(sDiagKeys[i]); }
            if (!djui_panel_vr_diag_set_row("world_scale", VR_MENU_MARIO_UNITS / 0.8)
                || !djui_panel_vr_diag_set_row("camera_closer", 0.4)) {
                vr_logf("settings page: diag: the Room tab is not open; nothing stepped");
            }
            break;
        case VR_PREVIEW_DIAG_RESTORE:
            // The exact values, not the nearest stops, then the rows show them
            for (int i = 0; i < 2; i++) { vr_setting_set(sDiagKeys[i], sDiagSaved[i]); }
            for (int row = 0; row < sRowCount; row++) { djui_panel_vr_row_refresh(row); }
            break;
        case VR_PREVIEW_DIAG_CLOSE:
            if (gDjuiPanelPauseCreated) { djui_panel_shutdown(); }
            break;
        default:
            break;
    }
    const char* tag = vr_preview_diag_dump_tag(step);
    if (tag) { vr_request_dump(tag); }
    vr_logf("settings page: diag: %s (play mode %d, pause camera %d, overview %d, pause menu %d, page %d, "
        "world_scale %.1f, camera_closer %.2f)", vr_preview_diag_name(step), sCurrPlayMode,
        (gCameraMovementFlags & CAM_MOVE_PAUSE_SCREEN) != 0, gVrTick.pause_overview, gDjuiPanelPauseCreated,
        gVrSettingsPage.open, vr_setting_get("world_scale"), vr_setting_get("camera_closer"));
}

#endif
