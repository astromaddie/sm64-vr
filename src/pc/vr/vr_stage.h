#ifndef VR_STAGE_H
#define VR_STAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_cam.h"
#include "vr_cinema.h"
#include "vr_math.h"
#include "vr_settings.h"
#include "vr_types.h"

// The stereo/mono policy of the renderer, without GL or OpenXR: which mode a
// frame is in, when to recentre, the black fade on a mode switch, where the
// mono panel hangs, and what the frame submits. The inputs are the frame's
// views and the runtime's display time, so tests drive it with numbers.

// The settings the renderer reads, taken once when it is attached.
typedef struct {
	vr_rig_t rig; // world_scale, head_translation, level_pitch
	float star_select_scale; // game units per metre on the act selector (its own diorama scale)
	float hud_distance; // metres
	float hud_scale; // percent
	int hud_mode; // 0 near, 1 far, 2 leash (only near is built)
	bool hud_visible;
	bool force_mono;
	bool diag_menu_stereo; // the main menu's background level renders in stereo
	float panel_distance;
	float panel_width;
	int cutscenes; // 0 cinema_3d, 1 immersive
	float cinema_distance, cinema_width;
	float diag_cinema_seconds; // force cinema on the first available 3D scene
	float diag_yaw; // radians
	int dump_frame; // 0 = off
	int dump_worn_frame; // 0 = off; counts stereo frames from tracked views only
	bool timing;
	int ui_width, ui_height; // the 2D layout size: the window size the game sees, and the HUD and panel textures
} vr_render_settings_t;

// 16:9, like the default window; the HUD's 40 degrees get 32 pixels per degree
#define VR_UI_WIDTH 1280
#define VR_UI_HEIGHT 720

void vr_render_settings_from(vr_render_settings_t *out, const vr_settings_t *s);

typedef enum {
	VR_MODE_NONE, // before the first frame
	VR_MODE_MONO, // the game's own camera and projection, replayed once into the world-locked panel
	VR_MODE_STEREO, // gameplay: eyes into the projection layer, 2D into the HUD quad
	VR_MODE_CINEMA // scripted game-camera stereo on a world-locked projection screen
} vr_mode_t;

const char *vr_mode_name(vr_mode_t mode);

// What one frame submits to the compositor. Poses are LOCAL space, metres.
typedef struct {
	bool projection;
	vr_view_t views[2]; // exactly what the eyes were rendered with
	bool hud;
	vr_pose_t hud_pose;
	float hud_width, hud_height;
	bool panel;
	vr_pose_t panel_pose;
	float panel_width;
	float fade_alpha; // 0: no fade quad; else a quad at this alpha over everything
	float fade_rgb[3]; // the fade quad's colour (black, or a warp transition's)
} vr_layers_t;

#define VR_FADE_HOLD_S 0.25f
#define VR_FADE_OUT_S 0.35f

// Alpha of the transition fade `seconds` after the switch: 1 for the hold,
// then linearly to 0.
float vr_fade_alpha(float seconds);

typedef struct {
	vr_mode_t mode;
	bool recentre_pending; // applied at the next stereo frame
	bool panel_pending; // the mono panel is re-placed at the next mono frame
	bool recentre_valid;
	vr_recentre_t recentre; // origin[1] is the standing height (see below)
	bool height_pending; // the next recentre takes the head's height too
	bool seated; // every recentre takes the height (the renderer sets it from `seated`)
	bool panel_valid;
	vr_pose_t panel_pose;
	bool cinema_valid, cinema_pending;
	vr_cinema_screen_t cinema_screen;
	bool fade_active;
	int64_t fade_start_ns;
	uint32_t mode_switches;
	uint32_t recentres_applied;
} vr_stage_t;

void vr_stage_init(vr_stage_t *s);

// A recentre was asked for (the input layer, focus gain, a reference-space
// change): applies at the next stereo frame, and the panel is re-placed.
void vr_stage_request_recentre(vr_stage_t *s);

// The same with the standing height taken again (standard.md section 1: View
// held 3 s; also focus gained and the runtime's recentre). A recentre takes
// the head's yaw and horizontal position; it keeps the standing height, from
// which the head's height moves the view (ducking lowers it), except for the
// first one, one asked for here, or any with `seated` set.
void vr_stage_request_height(vr_stage_t *s);

// The frame's mode. A change starts the black fade and requests a recentre.
// Returns true on a change.
bool vr_stage_set_mode(vr_stage_t *s, vr_mode_t mode, int64_t now_ns);

// The same black fade without a mode change or a recentre: the shown camera jumps
// within a mode (SM64's pause overview, section 10).
void vr_stage_fade(vr_stage_t *s, int64_t now_ns);

// Takes the standing height from this frame's head and nothing else (yaw and the
// horizontal origin stay): `seated` switched on. False before the first recentre.
bool vr_stage_take_height(vr_stage_t *s, const vr_view_t views[2]);

// Applies a pending recentre (or the first one) from this frame's head, at the
// start of a stereo frame. Returns true when it did.
bool vr_stage_apply_recentre(vr_stage_t *s, const vr_view_t views[2]);

// Where the mono panel is this frame: placed in front of the head when it is
// pending, then fixed in the world. With no head pose yet it is straight ahead
// of the origin.
void vr_stage_panel_pose(vr_stage_t *s, const vr_view_t views[2], bool views_valid, float distance, vr_pose_t *out);

// Anchors only on entry/recentre. Mid-cutscene camera cuts never call this a
// mode change, and neither the game camera nor a comfort turn moves the screen.
void vr_stage_cinema_screen(vr_stage_t *s, const vr_view_t views[2], float distance, float width,
	vr_cinema_screen_t *out);

// The transition fade's alpha for this frame's display time; 0 once it is over.
float vr_stage_fade_alpha(vr_stage_t *s, int64_t now_ns);

#endif
