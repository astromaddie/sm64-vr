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

#endif

#endif
