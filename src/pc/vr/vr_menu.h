#ifndef VR_MENU_H
#define VR_MENU_H

#include <stdbool.h>
#include <stddef.h>

// The content of the in-headset VR settings page (standard.md section 7), without
// DJUI: which entry goes on which tab, what widget it uses, and how a widget's
// position maps to a setting value and back. src/pc/djui/djui_panel_vr.c builds the
// panels from this table. An entry whose key the registry does not have is left out
// (other features add their keys in parallel), and so is a tab left with no entries.
// Diagnostics have no entry, by design.

typedef enum {
	VR_TAB_COMFORT,
	VR_TAB_CONTROLS,
	VR_TAB_HUD,
	VR_TAB_DISPLAY,
	VR_TAB_ACCESSIBILITY, // no entries yet: the tab is skipped
	VR_TAB_ROOM,
	VR_TAB_COUNT
} vr_menu_tab_t;

typedef enum {
	VR_MENU_TOGGLE, // a bool key
	VR_MENU_CHOICE, // a selection box: an enum key, or a list of values of an int key
	VR_MENU_SLIDER, // stops between two display values
	VR_MENU_BUTTON // an action, no key
} vr_menu_widget_t;

typedef enum {
	VR_MENU_ACTION_NONE,
	VR_MENU_ACTION_RECENTRE
} vr_menu_action_t;

typedef struct {
	const char *key; // registry name; NULL for a button
	const char *label;
	vr_menu_tab_t tab;
	vr_menu_widget_t widget;

	// CHOICE: the labels; the values (NULL: the enum index is the value)
	const char *const *choices;
	int choice_count;
	const double *choice_values;

	// SLIDER: stops from lo to hi in display units, `step` apart. display = setting * mul,
	// or `invert_units` / setting when invert_units is set (world_scale as a height).
	// lo == hi means the registry's range in 20 stops.
	double lo, hi, step;
	double mul; // 0 means 1
	double invert_units; // 0 means no
	const char *format; // printf for one double, the display value

	vr_menu_action_t action; // BUTTON
} vr_menu_entry_t;

// Mario is 161 game units tall: world_scale is units per metre
#define VR_MENU_MARIO_UNITS 161.0

size_t vr_menu_entry_count(void);
const vr_menu_entry_t *vr_menu_entry(size_t index);
const char *vr_menu_tab_name(vr_menu_tab_t tab);

// Is the entry usable now: a button, or a registered key of a type the widget can show
bool vr_menu_entry_available(const vr_menu_entry_t *e);

// Fills `out` (up to `max`) with the indices of the tab's available entries, in order;
// returns how many there are (which can exceed `max`)
size_t vr_menu_tab_entries(vr_menu_tab_t tab, size_t *out, size_t max);

// The tabs that have entries, in order; returns how many
size_t vr_menu_tabs(vr_menu_tab_t *out, size_t max);

// CHOICE
int vr_menu_choice_count(const vr_menu_entry_t *e);
const char *vr_menu_choice_label(const vr_menu_entry_t *e, int index);
int vr_menu_choice_index(const vr_menu_entry_t *e, double setting); // the nearest choice
double vr_menu_choice_value(const vr_menu_entry_t *e, int index);

// SLIDER: stops are numbered 0 .. count - 1
int vr_menu_slider_count(const vr_menu_entry_t *e);
double vr_menu_slider_display(const vr_menu_entry_t *e, int stop); // in display units
double vr_menu_slider_value(const vr_menu_entry_t *e, int stop); // the setting to store
int vr_menu_slider_stop(const vr_menu_entry_t *e, double setting); // the nearest stop; clamped
void vr_menu_slider_text(const vr_menu_entry_t *e, int stop, char *out, size_t n);

#endif
