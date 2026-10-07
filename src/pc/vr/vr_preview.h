#ifndef VR_PREVIEW_H
#define VR_PREVIEW_H

#include <stdbool.h>

#include "vr_types.h"

// The live preview behind the VR settings page (docs/VR_ARCHITECTURE.md section 10),
// without GL, DJUI or the game: when the shown camera cuts between SM64's pause
// overview and the play camera (a stage fade covers it), where the held menu turns
// while the page is open so the view it changes is not behind it, and the timeline of
// the unattended diagnostic that opens the page. The game side is small: camera.c holds
// the pause overview off while the page is open, so the eyes stay on Lakitu's last
// gameplay pose (a paused game does not update the camera, so that pose is the one
// from just before the pause), and publishes whether the overview was drawn.

// settings_page_side: where the page goes while it is open
typedef enum {
	VR_PREVIEW_SIDE_RIGHT,
	VR_PREVIEW_SIDE_LEFT,
	VR_PREVIEW_SIDE_CENTRE // where the pause menu opened, as before
} vr_preview_side_t;

// The page's width over the 2D layout's (djui_panel_vr.c's 640 of 1280)
#define VR_PREVIEW_PAGE_FRACTION 0.5f
// The clear angle from the view's forward to the page's inner edge (Mario and the
// scene around him stay in view)
#define VR_PREVIEW_MARGIN_DEG 10.0f
// How fast the menu moves to the side and back (it is a panel, not the world)
#define VR_PREVIEW_MENU_RATE_DPS 160.0f

typedef struct {
	bool stereo; // a stereo gameplay frame: fades and the menu's turn apply
	bool menu_up; // the pause menu is on the HUD quad
	bool page_open; // the VR settings page (any of its panels) is up
	bool overview; // the tick's camera is SM64's pause overview
	vr_preview_side_t side;
	float page_half; // the page's half-width as an angle, radians (vr_preview_page_half)
	float dt; // seconds since the last frame
} vr_preview_in_t;

typedef struct {
	bool fade; // the shown camera cut to or from the overview: start the stage fade now
	bool page_changed, overview_changed; // for the log
	float menu_yaw; // radians, positive turns the held menu to the left
} vr_preview_out_t;

typedef struct {
	bool valid;
	bool page_open, overview;
	float menu_yaw;
} vr_preview_t;

void vr_preview_reset(vr_preview_t *p);

// Once per rendered frame
vr_preview_out_t vr_preview_frame(vr_preview_t *p, const vr_preview_in_t *in);

// The page's half-width as an angle, for a HUD quad `hud_width` metres wide at `distance`
float vr_preview_page_half(float hud_width, float distance);

// Where the menu turns to with the page open (radians, positive left); 0 for the centre
float vr_preview_menu_target(vr_preview_side_t side, float page_half);

// The held menu's HUD quad: in front of the head it was placed from, turned `yaw` about
// the vertical through that head (so it stays `distance` away, facing the head), with
// the head's pitch and no roll (vr_hud_pose)
void vr_preview_menu_pose(const vr_pose_t *head, float yaw, float distance, vr_pose_t *out);

// The diagnostic diag_settings_preview = N: after N seconds of gameplay it pauses,
// opens the pause menu (the overview), then the page's Room tab (the gameplay view),
// steps world_scale and camera_closer, puts them back, closes the menus and resumes,
// asking for a named dump at each stage. Offsets from N, in seconds:
typedef enum {
	VR_PREVIEW_DIAG_NONE,
	VR_PREVIEW_DIAG_PAUSE, // 0: Start, SM64's pause (the overview after two frames)
	VR_PREVIEW_DIAG_PAUSE_MENU, // 1.5: the pause menu (R)
	VR_PREVIEW_DIAG_DUMP_PAUSED, // 3: dump "paused-"
	VR_PREVIEW_DIAG_OPEN_PAGE, // 4: the VR settings page, Room tab
	VR_PREVIEW_DIAG_DUMP_PAGE, // 6: dump "page-"
	VR_PREVIEW_DIAG_STEP, // 7: world_scale to Mario 0.8 m, camera_closer 0.4
	VR_PREVIEW_DIAG_DUMP_STEP, // 8: dump "step-"
	VR_PREVIEW_DIAG_RESTORE, // 9: both back
	VR_PREVIEW_DIAG_CLOSE, // 10: every menu closed
	VR_PREVIEW_DIAG_RESUME, // 10.5: Start, the game runs
	VR_PREVIEW_DIAG_DUMP_RESUMED, // 12: dump "resumed-"
	VR_PREVIEW_DIAG_DONE
} vr_preview_diag_step_t;

#define VR_PREVIEW_DIAG_SECONDS 12.0f // the run needs this long after N

typedef struct {
	bool started;
	float gameplay_s; // seconds of gameplay counted so far
	int next; // the next step
} vr_preview_diag_t;

// Once per game tick: `gameplay` is a stereo gameplay tick (not a menu, act select or
// warp); time only counts while it holds until the sequence has begun, then always.
// Returns the step due this tick, at most one per tick.
vr_preview_diag_step_t vr_preview_diag_tick(vr_preview_diag_t *d, float delay_s, bool gameplay, float dt);

// The step's name, for the log; the dump steps' file prefix ("paused-", ...) or NULL
const char *vr_preview_diag_name(vr_preview_diag_step_t step);
const char *vr_preview_diag_dump_tag(vr_preview_diag_step_t step);

#endif
