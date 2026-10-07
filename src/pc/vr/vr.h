#ifndef VR_H
#define VR_H

#include <stdbool.h>
#include <stdint.h>

#include <PR/gbi.h>

#include "vr_types.h"
#include "vr_settings.h"
#include "vr_runtime_api.h"

// The VR host as pc_main.c sees it (SM64_VR builds only;
// docs/VR_ARCHITECTURE.md section 3). Per XR frame:
//
//   vr_frame_begin()       xrWaitFrame + xrBeginFrame + xrLocateViews; actions
//   vr_frame_ticks()       how many 30 Hz ticks the display-time scheduler wants
//                          now (0 while the sim is frozen); run them, calling
//                          vr_frame_tick_ran() after each
//   vr_frame_interp_t()    the interpolation delta for this frame's display time
//   patch_interpolations() once, then
//   vr_frame_render(dl)    the replay into the swapchains
//   vr_frame_end()         xrEndFrame with the layers the replay produced

// The data dir ($XDG_DATA_HOME/SM64VR/, trailing slash), created if missing:
// the game's user path in the VR build (ROM, saves, config) and vr.toml's home.
const char *vr_data_dir(void);

// Reads vr.toml, creates the XR instance and session on the SDL window's
// current GL context. False (with the reason logged) when VR is unavailable;
// the game then runs flat.
bool vr_init(void *sdl_window);
void vr_shutdown(void);

bool vr_enabled(void);
bool vr_exit_requested(void);
const vr_settings_t *vr_settings(void);

// Runtime settings, for the in-headset page (docs/VR_ARCHITECTURE.md section 10).
// A change is clamped like the file's values, applied on the next XR frame (the
// renderer's settings are rebuilt, and everything that reads vr_settings() or
// vr_setting_get() each frame sees it) and written to vr.toml shortly after the
// last change (vr_settings_flush() writes at once). All of these are for the game thread.
bool vr_setting_reset(const char *key); // back to the default
void vr_settings_flush(void); // save now if anything changed
unsigned vr_settings_revision(void); // counts changes, for code that caches a setting
void vr_recentre_now(void); // as holding L View: the next stereo frame recentres on the head
// Diagnostics: the next rendered frame is dumped like dump_frame, its files named `tag`vr-eye0.png ...
void vr_request_dump(const char *tag);

// The size the game's 2D layout uses (the window size it sees in VR).
void vr_ui_size(int *width, int *height);

// vr.probe_seconds: left eye red, right eye blue for that long. Call after
// vr_init() and before the game loop.
void vr_probe(void);

// False when no frame was begun (the session is not running yet): run nothing,
// draw nothing. When true, vr_frame_end() must follow.
bool vr_frame_begin(void);
int vr_frame_ticks(void);
void vr_frame_tick_ran(void);
// CPU time of a tick, for the [vr-perf] line
void vr_frame_add_tick_ms(double ms);
float vr_frame_interp_t(void);
void vr_frame_render(Gfx *dl);
void vr_frame_end(void);

#endif
