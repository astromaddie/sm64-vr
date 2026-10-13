#ifndef VR_GAME_H
#define VR_GAME_H

// What the game code and the VR renderer share (SM64_VR builds only). The game
// writes the tick's facts while it builds the display list; the renderer reads
// them when it replays it, and writes the head-facing state the next tick's
// billboards use. docs/VR_ARCHITECTURE.md sections 3 to 5.

#ifdef SM64_VR

#include <stdbool.h>

struct VrTickInfo {
    bool rendered_area; // render_game drew an area this tick (world segment present)
    bool gameplay_camera; // ... through a Lakitu camera node (geo_camera_main)
    bool in_main_menu; // the DJUI main menu is up (its background level renders)
    bool star_select; // the act selector: its fixed-camera star row renders in stereo
    unsigned char cutscene, camera_mode; // read-only snapshot from geo_camera_main
    short cutscene_timer, cutscene_shot; // shot-local timer: never used as the cinema gate
    float camera_fov; // vertical game-camera FOV, degrees (sky pass)
    bool transition; // a warp transition (iris, fade) is drawn this tick
    // The transition as a fade over the view: 0 = clear, 1 = covered by
    // transition_rgb. transition_hold: the game has stopped rendering behind a
    // closed transition (gWarpTransition.pauseRendering).
    float transition_alpha;
    unsigned char transition_rgb[3];
    bool transition_hold;
    bool pause_overview; // camera.c drew SM64's pause overview instead of Lakitu's view
};

// Written by render_game() for the display list of the newest tick
extern struct VrTickInfo gVrTick;

// The renderer is attached (a session exists): the game widens its culling to
// distance only, and its level-of-detail switches use distance, not depth.
extern bool gVrActive;

// Billboards face the head (the newest stereo frame's head centre) instead of
// the game camera. The rotations are the game's row-vector 3x3, in the camera
// space of the tick's camera.
extern bool gVrBillboardHead;
extern float gVrBillboardRot[3][3];
extern float gVrCylboardRot[3][3];

// The camera view the interpolated modelviews were built with
// (patch_mtx_interpolated), as the game's Mat4 memory. Valid for one XR frame.
extern float gVrInterpCamView[16];
extern bool gVrInterpCamValid;
// ... its focus (look-at point), and Mario (the turn pivot) at the same time
extern float gVrInterpCamFocus[3];
extern float gVrInterpPivot[3];

// Comfort turning and the stick's frame (src/pc/vr/vr_turn.h has the protocol;
// docs/VR_ARCHITECTURE.md section 4). Angles are headings in radians, positive
// turning left, the same sense as SM64's yaws.
struct VrCamTurn {
    // The renderer adds the stick's turn; the next camera update takes it all
    // (or refuses it) and turns the camera rig about Mario in one step
    float queued;
    // camera.c, every camera update: whether, and how far, the camera can turn
    bool allowed;
    float limit_left, limit_right;
    unsigned int updates;
    // Mario at the last two camera renders (the pivot; interpolated per frame)
    float pivot[3], pivot_prev[3];
    bool pivot_valid;
    float taken; // in total, for the log
    unsigned int refused; // camera updates that dropped a queued turn
    // The renderer, every frame: in stereo play Mario's stick frame is the
    // wearer's (the camera's yaw + move_yaw + queued), and Lakitu's shakes are off
    bool move_active;
    float move_yaw;
    bool no_shake;
    // camera_auto_rotate off: the camera turns only when the wearer turns it (camera.c)
    bool manual;
};
extern struct VrCamTurn gVrCamTurn;

// The VR settings page (djui_panel_vr.c; docs/VR_ARCHITECTURE.md section 10). While it
// is open camera.c holds the pause overview off, so the page is seen over the gameplay
// view it changes, and the pause shade is left out. hide_hud (the renderer, each frame):
// the page is open with hud_visible off, so the game's own HUD is left out behind it.
struct VrSettingsPage {
    bool open;
    bool hide_hud;
};
extern struct VrSettingsPage gVrSettingsPage;

#endif

#endif
