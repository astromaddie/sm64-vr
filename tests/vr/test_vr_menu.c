#include <string.h>

#include "sfvr_test.h"
#include "vr_menu.h"
#include "vr_settings.h"

static bool is_diagnostic(const char *key) {
	static const char *const diagnostics[] = {
		"force_render", "diag_yaw", "diag_hand_yaw", "diag_two_handed", "diag_walk_speed", "dump_frame",
		"dump_worn_frame", "probe_seconds", "timing", "timing_gpu", "force_mono", "fov_mode", "level_pitch",
		"diag_menu_stereo", "diag_rate_unworn", "panel_width", NULL
	};
	for (int i = 0; diagnostics[i]; i++) {
		if (strcmp(key, diagnostics[i]) == 0) {
			return true;
		}
	}
	return false;
}

static const vr_menu_entry_t *find(const char *key) {
	for (size_t i = 0; i < vr_menu_entry_count(); i++) {
		const vr_menu_entry_t *e = vr_menu_entry(i);
		if (e->key && strcmp(e->key, key) == 0) {
			return e;
		}
	}
	return NULL;
}

static void test_table(void) {
	size_t total = vr_menu_entry_count();
	CHECK(total > 20);
	CHECK(vr_menu_entry(total) == NULL);

	int buttons = 0;
	for (size_t i = 0; i < total; i++) {
		const vr_menu_entry_t *e = vr_menu_entry(i);
		CHECK(e->label && e->label[0]);
		CHECK(strlen(e->label) <= 22); // the label gets 60% of the panel
		CHECK((int)e->tab >= 0 && e->tab < VR_TAB_COUNT);
		if (e->widget == VR_MENU_BUTTON) {
			buttons++;
			CHECK(e->key == NULL);
			CHECK(e->action != VR_MENU_ACTION_NONE);
			CHECK(vr_menu_entry_available(e));
			continue;
		}
		CHECK(e->key != NULL);
		CHECK(!is_diagnostic(e->key)); // diagnostics never appear in the menu

		// available exactly when the key is registered with a type the widget can show
		int index = vr_settings_index(e->key);
		const sfvr_setting_key *k = index >= 0 ? vr_settings_key((size_t)index) : NULL;
		bool fits = false;
		if (k) {
			fits = e->widget == VR_MENU_TOGGLE ? k->type == SFVR_SETTING_BOOL
				: e->widget == VR_MENU_SLIDER ? (k->type == SFVR_SETTING_FLOAT || k->type == SFVR_SETTING_INT)
				: vr_menu_choice_count(e) > 0;
		}
		CHECK(vr_menu_entry_available(e) == fits);
	}
	CHECK_INT(buttons, 1); // Recentre now
	CHECK(!vr_menu_entry_available(NULL));

	// the keys the page is built for that this tree has are present
	const char *have[] = {
		"turn_mode", "snap_angle", "smooth_turn_speed", "vignette", "vignette_strength", "move_relative",
		"camera_shake", "camera_auto_rotate", "dominant_hand", "defaults_profile", "haptics", "hud_visible", "hud_distance", "hud_scale", "panel_distance",
		"resolution_scale", "refresh_rate", "world_scale", "star_select_scale", "height_offset", "seated",
		"head_translation"
	};
	for (size_t i = 0; i < sizeof(have) / sizeof(have[0]); i++) {
		const vr_menu_entry_t *e = find(have[i]);
		CHECK(e != NULL);
		CHECK(e && vr_menu_entry_available(e));
	}
}

static void test_tabs(void) {
	CHECK_STR(vr_menu_tab_name(VR_TAB_COMFORT), "Comfort");
	CHECK_STR(vr_menu_tab_name(VR_TAB_CONTROLS), "Controls");
	CHECK_STR(vr_menu_tab_name(VR_TAB_HUD), "HUD & menus");
	CHECK_STR(vr_menu_tab_name(VR_TAB_DISPLAY), "Display & performance");
	CHECK_STR(vr_menu_tab_name(VR_TAB_ACCESSIBILITY), "Accessibility & audio");
	CHECK_STR(vr_menu_tab_name(VR_TAB_ROOM), "Room");
	CHECK(VR_TAB_COMFORT < VR_TAB_CONTROLS && VR_TAB_CONTROLS < VR_TAB_HUD && VR_TAB_HUD < VR_TAB_DISPLAY
		&& VR_TAB_DISPLAY < VR_TAB_ACCESSIBILITY && VR_TAB_ACCESSIBILITY < VR_TAB_ROOM);

	// tabs in the standard's order, with the empty accessibility tab left out
	vr_menu_tab_t tabs[VR_TAB_COUNT];
	size_t count = vr_menu_tabs(tabs, VR_TAB_COUNT);
	CHECK_INT(count, 5);
	CHECK_INT(tabs[0], VR_TAB_COMFORT);
	CHECK_INT(tabs[1], VR_TAB_CONTROLS);
	CHECK_INT(tabs[2], VR_TAB_HUD);
	CHECK_INT(tabs[3], VR_TAB_DISPLAY);
	CHECK_INT(tabs[4], VR_TAB_ROOM);
	CHECK_INT(vr_menu_tab_entries(VR_TAB_ACCESSIBILITY, NULL, 0), 0);

	// the Room tab ends with the recentre button; the order within a tab is the table's
	size_t room[32];
	size_t n = vr_menu_tab_entries(VR_TAB_ROOM, room, 32);
	CHECK(n >= 6 && n <= 32);
	CHECK(vr_menu_entry(room[n - 1])->widget == VR_MENU_BUTTON);
	CHECK_STR(vr_menu_entry(room[0])->key, "world_scale");

	// a tab with a page full of rows must fit on the screen: at most 8 rows and 2 buttons
	for (int t = 0; t < VR_TAB_COUNT; t++) {
		CHECK(vr_menu_tab_entries((vr_menu_tab_t)t, NULL, 0) <= 9);
	}

	// a short buffer still counts everything
	CHECK_INT(vr_menu_tab_entries(VR_TAB_COMFORT, room, 2), vr_menu_tab_entries(VR_TAB_COMFORT, NULL, 0));
}

// Every available widget maps its positions to values the registry accepts unchanged,
// and a value back to the position it came from
static void test_value_mapping_round_trips(void) {
	for (size_t i = 0; i < vr_menu_entry_count(); i++) {
		const vr_menu_entry_t *e = vr_menu_entry(i);
		if (!vr_menu_entry_available(e) || e->widget == VR_MENU_BUTTON) {
			continue;
		}
		const sfvr_setting_key *k = vr_settings_key((size_t)vr_settings_index(e->key));
		if (e->widget == VR_MENU_SLIDER) {
			int count = vr_menu_slider_count(e);
			CHECK(count >= 2 && count <= 120);
			for (int stop = 0; stop < count; stop++) {
				double v = vr_menu_slider_value(e, stop);
				// Float keys hold float precision: 0.8 comes back as 0.80000001
				CHECK(v >= k->min - 1e-6 && v <= k->max + 1e-6);
				CHECK_INT(vr_menu_slider_stop(e, v), stop);
				if (k->type == SFVR_SETTING_INT) {
					CHECK_NEAR(v, floor(v + 0.5), 1e-9);
				}
			}
			CHECK_INT(vr_menu_slider_stop(e, k->min - 100000), vr_menu_slider_stop(e, vr_menu_slider_value(e, 0)));
			CHECK_INT(vr_menu_slider_stop(e, k->def) >= 0, 1);
		}
		else if (e->widget == VR_MENU_CHOICE) {
			int count = vr_menu_choice_count(e);
			CHECK(count >= 2 && count <= 16);
			for (int c = 0; c < count; c++) {
				CHECK(vr_menu_choice_label(e, c)[0] != '\0');
				double v = vr_menu_choice_value(e, c);
				CHECK_NEAR(vr_settings_clamp(e->key, v), v, 1e-9);
				CHECK_INT(vr_menu_choice_index(e, v), c);
			}
		}
	}
}

static void test_choices(void) {
	const vr_menu_entry_t *turn = find("turn_mode");
	CHECK_INT(vr_menu_choice_count(turn), 3);
	CHECK_STR(vr_menu_choice_label(turn, 0), "Snap");
	CHECK_STR(vr_menu_choice_label(turn, 1), "Smooth");
	CHECK_STR(vr_menu_choice_label(turn, 2), "Off");
	CHECK_STR(vr_menu_choice_label(turn, 3), "");
	CHECK_NEAR(vr_menu_choice_value(turn, 2), 2, 1e-9);
	CHECK_NEAR(vr_menu_choice_value(turn, 9), 2, 1e-9); // clamped to the last

	// snap angle picks from a list, and a value in between (or from a file) lands on the nearest
	const vr_menu_entry_t *snap = find("snap_angle");
	CHECK_INT(vr_menu_choice_count(snap), 5);
	CHECK_NEAR(vr_menu_choice_value(snap, 0), 15, 1e-9);
	CHECK_NEAR(vr_menu_choice_value(snap, 4), 90, 1e-9);
	CHECK_INT(vr_menu_choice_index(snap, 30), 1);
	CHECK_INT(vr_menu_choice_index(snap, 52), 2);
	CHECK_INT(vr_menu_choice_index(snap, 55), 3);

	const vr_menu_entry_t *rate = find("refresh_rate");
	CHECK_INT(vr_menu_choice_index(rate, 120), 2);
	CHECK_INT(vr_menu_choice_index(rate, 80), 0); // 80 is not offered: the nearest below
	CHECK(strstr(rate->label, "SteamVR") != NULL); // standard.md section 9: the per-app rate wins

	const vr_menu_entry_t *hand = find("dominant_hand");
	CHECK_STR(vr_menu_choice_label(hand, 0), "Right"); // the registry's default
	CHECK_STR(vr_menu_choice_label(find("move_relative"), 2), "Right hand");
	const vr_menu_entry_t *profile = find("defaults_profile");
	CHECK_STR(vr_menu_choice_label(profile, 0), "Standard");
	CHECK_STR(vr_menu_choice_label(profile, 1), "Frame Personal");
}

static void test_sliders(void) {
	char text[32];

	// World scale is units/metre; the settings row shows apparent size relative
	// to Mario's 104 units/metre baseline.
	const vr_menu_entry_t *world = find("world_scale");
	CHECK_STR(world->label, "Apparent size");
	int stop = vr_menu_slider_stop(world, 104); // the default
	CHECK_NEAR(vr_menu_slider_display(world, stop), 100, 1e-9);
	vr_menu_slider_text(world, stop, text, sizeof(text));
	CHECK_STR(text, "100%");
	CHECK_NEAR(vr_menu_slider_value(world, stop), 104, 1e-9);
	CHECK_NEAR(vr_menu_slider_display(world, 0), 25, 1e-9);
	CHECK_NEAR(vr_menu_slider_value(world, 0), 416, 1e-9); // preserve native upper units/metre bound
	int last = vr_menu_slider_count(world) - 1;
	CHECK_NEAR(vr_menu_slider_display(world, last), 520, 1e-9);
	CHECK_NEAR(vr_menu_slider_value(world, last), 20, 1e-9); // preserve wider native apparent-size range

	// percent keys: haptics 0.6 shows as 60%
	const vr_menu_entry_t *haptics = find("haptics");
	stop = vr_menu_slider_stop(haptics, 0.6);
	vr_menu_slider_text(haptics, stop, text, sizeof(text));
	CHECK_STR(text, "60%");
	CHECK_NEAR(vr_menu_slider_value(haptics, stop), 0.6, 1e-6);
	CHECK_NEAR(vr_menu_slider_value(haptics, vr_menu_slider_count(haptics) - 1), 1.0, 1e-9);

	const vr_menu_entry_t *hud = find("hud_scale");
	vr_menu_slider_text(hud, vr_menu_slider_stop(hud, 100), text, sizeof(text));
	CHECK_STR(text, "100%");
	const vr_menu_entry_t *res = find("resolution_scale");
	vr_menu_slider_text(res, vr_menu_slider_stop(res, 1.25), text, sizeof(text));
	CHECK_STR(text, "1.25x");
	const vr_menu_entry_t *offset = find("height_offset");
	vr_menu_slider_text(offset, vr_menu_slider_stop(offset, 0), text, sizeof(text));
	CHECK_STR(text, "+0.00 m");

	// a value outside the slider's span shows at the end it is nearest, and changes nothing until moved
	CHECK_INT(vr_menu_slider_stop(hud, 5), 0);
	CHECK_INT(vr_menu_slider_stop(hud, 500), vr_menu_slider_count(hud) - 1);
	const vr_menu_entry_t *dist = find("hud_distance"); // registry up to 20 m, the page up to 6 m
	CHECK_INT(vr_menu_slider_stop(dist, 15), vr_menu_slider_count(dist) - 1);

	// out-of-range stops are clamped
	CHECK_NEAR(vr_menu_slider_display(hud, -3), 50, 1e-9);
	CHECK_NEAR(vr_menu_slider_display(hud, 9999), 200, 1e-9);
}

int main(void) {
	RUN(test_table);
	RUN(test_tabs);
	RUN(test_value_mapping_round_trips);
	RUN(test_choices);
	RUN(test_sliders);
	return SFVR_TEST_RESULT("vr_menu");
}
