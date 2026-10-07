#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vr_menu.h"
#include "vr_settings.h"

static const char *const turn_mode_choices[] = { "Snap", "Smooth", "Off" };
static const char *const move_relative_choices[] = { "Head", "Left hand", "Right hand" };
static const char *const cutscene_choices[] = { "3D cinema", "Immersive" };
static const char *const hand_choices[] = { "Right", "Left" };
static const char *const defaults_profile_choices[] = { "Standard", "Frame Personal" };
static const char *const snap_angle_choices[] = { "15 deg", "30 deg", "45 deg", "60 deg", "90 deg" };
static const double snap_angle_values[] = { 15, 30, 45, 60, 90 };
static const char *const page_side_choices[] = { "Right", "Left", "Centre" };
static const char *const refresh_choices[] = { "72 Hz", "90 Hz", "120 Hz", "144 Hz" };
static const double refresh_values[] = { 72, 90, 120, 144 };

#define CHOICE_ENUM(k, l, t, list) { .key = k, .label = l, .tab = t, .widget = VR_MENU_CHOICE, \
	.choices = list, .choice_count = (int)(sizeof(list) / sizeof(list[0])) }
#define CHOICE_VALUES(k, l, t, list, vals) { .key = k, .label = l, .tab = t, .widget = VR_MENU_CHOICE, \
	.choices = list, .choice_count = (int)(sizeof(list) / sizeof(list[0])), .choice_values = vals }
#define TOGGLE(k, l, t) { .key = k, .label = l, .tab = t, .widget = VR_MENU_TOGGLE }
#define SLIDER(k, l, t, lo_, hi_, step_, mul_, fmt) { .key = k, .label = l, .tab = t, .widget = VR_MENU_SLIDER, \
	.lo = lo_, .hi = hi_, .step = step_, .mul = mul_, .format = fmt }
// The registry's own range, 20 stops
#define SLIDER_RANGE(k, l, t, fmt) { .key = k, .label = l, .tab = t, .widget = VR_MENU_SLIDER, .format = fmt }

// Labels stay short: the label gets 60% of the panel's width in the DJUI font
static const vr_menu_entry_t entries[] = {
	// 1 Comfort
	CHOICE_ENUM("turn_mode", "Turning", VR_TAB_COMFORT, turn_mode_choices),
	CHOICE_VALUES("snap_angle", "Snap angle", VR_TAB_COMFORT, snap_angle_choices, snap_angle_values),
	SLIDER("smooth_turn_speed", "Smooth turn speed", VR_TAB_COMFORT, 30, 360, 15, 1, "%.0f deg/s"),
	TOGGLE("vignette", "Comfort vignette", VR_TAB_COMFORT),
	SLIDER("vignette_strength", "Vignette strength", VR_TAB_COMFORT, 0, 100, 5, 100, "%.0f%%"),
	CHOICE_ENUM("move_relative", "Move relative to", VR_TAB_COMFORT, move_relative_choices),
	CHOICE_ENUM("cutscenes", "Cutscenes", VR_TAB_COMFORT, cutscene_choices),
	TOGGLE("camera_shake", "Camera shake", VR_TAB_COMFORT),

	// 2 Controls
	CHOICE_ENUM("dominant_hand", "Dominant hand", VR_TAB_CONTROLS, hand_choices),
	CHOICE_ENUM("defaults_profile", "Defaults profile", VR_TAB_CONTROLS, defaults_profile_choices),
	SLIDER("haptics", "Haptics", VR_TAB_CONTROLS, 0, 100, 10, 100, "%.0f%%"),

	// 3 HUD and menus
	TOGGLE("hud_visible", "Show HUD", VR_TAB_HUD),
	SLIDER("hud_distance", "HUD distance", VR_TAB_HUD, 1, 6, 0.25, 1, "%.2f m"),
	SLIDER("hud_scale", "HUD size", VR_TAB_HUD, 50, 200, 5, 1, "%.0f%%"),
	SLIDER("panel_distance", "Menu distance", VR_TAB_HUD, 1, 6, 0.25, 1, "%.2f m"),
	CHOICE_ENUM("settings_page_side", "Settings page side", VR_TAB_HUD, page_side_choices),
	SLIDER_RANGE("cinema_distance", "Cinema distance", VR_TAB_HUD, "%.1f m"),
	SLIDER_RANGE("cinema_width", "Cinema width", VR_TAB_HUD, "%.1f m"),

	// 4 Display and performance. SteamVR's per-app rate wins (standard.md section 9); the
	// resolution is read when the swapchains are made, at launch.
	SLIDER("resolution_scale", "Resolution (restart)", VR_TAB_DISPLAY, 0.75, 1.5, 0.05, 1, "%.2fx"),
	CHOICE_VALUES("refresh_rate", "Rate (SteamVR wins)", VR_TAB_DISPLAY, refresh_choices, refresh_values),

	// 5 Accessibility and audio: nothing yet

	// 6 Room
	{ .key = "world_scale", .label = "Apparent size", .tab = VR_TAB_ROOM, .widget = VR_MENU_SLIDER,
		.lo = 25, .hi = 520, .step = 5, .invert_units = VR_MENU_MARIO_SIZE_PERCENT, .format = "%.0f%%" },
	SLIDER("star_select_scale", "Star select scale", VR_TAB_ROOM, 100, 1000, 25, 1, "%.0f"),
	SLIDER_RANGE("camera_closer", "Camera closer", VR_TAB_ROOM, "%.2f"),
	SLIDER_RANGE("camera_lower", "Camera lower", VR_TAB_ROOM, "%.2f"),
	SLIDER("height_offset", "Height offset", VR_TAB_ROOM, -1, 1, 0.05, 1, "%+.2f m"),
	TOGGLE("seated", "Seated mode", VR_TAB_ROOM),
	SLIDER("head_translation", "Head movement", VR_TAB_ROOM, 0, 200, 10, 100, "%.0f%%"),
	{ .label = "Recentre now", .tab = VR_TAB_ROOM, .widget = VR_MENU_BUTTON, .action = VR_MENU_ACTION_RECENTRE }
};
#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

static const char *const tab_names[VR_TAB_COUNT] = {
	[VR_TAB_COMFORT] = "Comfort",
	[VR_TAB_CONTROLS] = "Controls",
	[VR_TAB_HUD] = "HUD & menus",
	[VR_TAB_DISPLAY] = "Display & performance",
	[VR_TAB_ACCESSIBILITY] = "Accessibility & audio",
	[VR_TAB_ROOM] = "Room"
};

size_t vr_menu_entry_count(void) {
	return ENTRY_COUNT;
}

const vr_menu_entry_t *vr_menu_entry(size_t index) {
	return index < ENTRY_COUNT ? &entries[index] : NULL;
}

const char *vr_menu_tab_name(vr_menu_tab_t tab) {
	return (int)tab >= 0 && tab < VR_TAB_COUNT ? tab_names[tab] : "";
}

static const sfvr_setting_key *entry_key(const vr_menu_entry_t *e) {
	int index = e->key ? vr_settings_index(e->key) : -1;
	return index >= 0 ? vr_settings_key((size_t)index) : NULL;
}

bool vr_menu_entry_available(const vr_menu_entry_t *e) {
	if (!e) {
		return false;
	}
	if (e->widget == VR_MENU_BUTTON) {
		return true;
	}
	const sfvr_setting_key *k = entry_key(e);
	if (!k) {
		return false;
	}
	switch (e->widget) {
		case VR_MENU_TOGGLE:
			return k->type == SFVR_SETTING_BOOL;
		case VR_MENU_SLIDER:
			return k->type == SFVR_SETTING_FLOAT || k->type == SFVR_SETTING_INT;
		default:
			return vr_menu_choice_count(e) > 0 && (k->type == SFVR_SETTING_ENUM || k->type == SFVR_SETTING_INT || k->type == SFVR_SETTING_BOOL);
	}
}

size_t vr_menu_tab_entries(vr_menu_tab_t tab, size_t *out, size_t max) {
	size_t n = 0;
	for (size_t i = 0; i < ENTRY_COUNT; i++) {
		if (entries[i].tab == tab && vr_menu_entry_available(&entries[i])) {
			if (out && n < max) {
				out[n] = i;
			}
			n++;
		}
	}
	return n;
}

size_t vr_menu_tabs(vr_menu_tab_t *out, size_t max) {
	size_t n = 0;
	for (int t = 0; t < VR_TAB_COUNT; t++) {
		if (vr_menu_tab_entries((vr_menu_tab_t)t, NULL, 0) > 0) {
			if (out && n < max) {
				out[n] = (vr_menu_tab_t)t;
			}
			n++;
		}
	}
	return n;
}


// -----------------------------------------------------------------------------
// Choice

// The table's labels, unless the registry's enum has a different number of values
// (then the registry's own names, so an index never maps to the wrong value)
static bool choice_from_table(const vr_menu_entry_t *e) {
	const sfvr_setting_key *k = entry_key(e);
	if (!e->choices || e->choice_count <= 0) {
		return false;
	}
	return !(k && k->type == SFVR_SETTING_ENUM && sfvr_settings_enum_count(k) != e->choice_count);
}

int vr_menu_choice_count(const vr_menu_entry_t *e) {
	if (choice_from_table(e)) {
		return e->choice_count;
	}
	const sfvr_setting_key *k = entry_key(e);
	return k && k->type == SFVR_SETTING_ENUM ? sfvr_settings_enum_count(k) : 0;
}

const char *vr_menu_choice_label(const vr_menu_entry_t *e, int index) {
	if (index < 0 || index >= vr_menu_choice_count(e)) {
		return "";
	}
	return choice_from_table(e) ? e->choices[index] : entry_key(e)->enum_values[index];
}

double vr_menu_choice_value(const vr_menu_entry_t *e, int index) {
	int count = vr_menu_choice_count(e);
	if (count <= 0) {
		return 0;
	}
	index = index < 0 ? 0 : (index >= count ? count - 1 : index);
	return e->choice_values && choice_from_table(e) ? e->choice_values[index] : index;
}

int vr_menu_choice_index(const vr_menu_entry_t *e, double setting) {
	int count = vr_menu_choice_count(e);
	int best = 0;
	double best_distance = INFINITY;
	for (int i = 0; i < count; i++) {
		double d = fabs(vr_menu_choice_value(e, i) - setting);
		if (d < best_distance) {
			best_distance = d;
			best = i;
		}
	}
	return best;
}


// -----------------------------------------------------------------------------
// Slider

static void slider_range(const vr_menu_entry_t *e, double *lo, double *hi, double *step) {
	if (e->lo != e->hi) {
		*lo = e->lo;
		*hi = e->hi;
		*step = e->step > 0 ? e->step : (e->hi - e->lo) / 20.0;
		return;
	}
	const sfvr_setting_key *k = entry_key(e);
	*lo = k ? k->min : 0.0;
	*hi = k ? k->max : 1.0;
	*step = (*hi - *lo) / 20.0;
	if (k && k->type == SFVR_SETTING_INT) {
		*step = fmax(1.0, floor(*step + 0.5));
	}
}

static double to_display(const vr_menu_entry_t *e, double setting) {
	if (e->invert_units != 0) {
		return setting != 0 ? e->invert_units / setting : 0;
	}
	return setting * (e->mul != 0 ? e->mul : 1.0);
}

static double to_setting(const vr_menu_entry_t *e, double display) {
	if (e->invert_units != 0) {
		return display != 0 ? e->invert_units / display : 0;
	}
	return display / (e->mul != 0 ? e->mul : 1.0);
}

int vr_menu_slider_count(const vr_menu_entry_t *e) {
	double lo, hi, step;
	slider_range(e, &lo, &hi, &step);
	if (hi <= lo || step <= 0) {
		return 1;
	}
	return (int)floor((hi - lo) / step + 0.5) + 1;
}

double vr_menu_slider_display(const vr_menu_entry_t *e, int stop) {
	double lo, hi, step;
	slider_range(e, &lo, &hi, &step);
	int count = vr_menu_slider_count(e);
	stop = stop < 0 ? 0 : (stop >= count ? count - 1 : stop);
	// The last stop is exactly hi, whatever the step does to rounding
	return stop == count - 1 && count > 1 ? hi : lo + stop * step;
}

double vr_menu_slider_value(const vr_menu_entry_t *e, int stop) {
	double v = to_setting(e, vr_menu_slider_display(e, stop));
	// The registry's clamp is the last word (a table range can be wider than the key's)
	return e->key ? vr_settings_clamp(e->key, v) : v;
}

int vr_menu_slider_stop(const vr_menu_entry_t *e, double setting) {
	double lo, hi, step;
	slider_range(e, &lo, &hi, &step);
	int count = vr_menu_slider_count(e);
	if (count <= 1) {
		return 0;
	}
	double d = to_display(e, setting);
	int stop = (int)floor((d - lo) / step + 0.5);
	return stop < 0 ? 0 : (stop >= count ? count - 1 : stop);
}

void vr_menu_slider_text(const vr_menu_entry_t *e, int stop, char *out, size_t n) {
	snprintf(out, n, e->format ? e->format : "%g", vr_menu_slider_display(e, stop));
}
