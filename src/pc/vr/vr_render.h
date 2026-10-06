#ifndef VR_RENDER_H
#define VR_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include <PR/gbi.h>

#include "vr_stage.h"
#include "vr_types.h"

// The stereo renderer (docs/VR_ARCHITECTURE.md sections 3 and 4). Once per XR
// frame it replays the newest tick's display list, already interpolated for
// this frame's display time, through Fast3D (gfx_vr_run_pass):
//
//   stereo  per eye into the eye swapchains, with the eye's projection put in
//           place of the game's; the 2D segment twice (over black, over white)
//           and the alpha solved into the HUD swapchain, a quad layer
//   mono    the whole frame once into the panel swapchain, a world-locked quad
//
// It asks a provider for images to draw into. vr.c is the provider for the
// runtime.

typedef enum {
    VR_SURFACE_EYE0,
    VR_SURFACE_EYE1,
    VR_SURFACE_HUD, // ui_width x ui_height, with alpha
    VR_SURFACE_PANEL, // ui_width x ui_height
    VR_SURFACE_COUNT
} vr_surface_t;

typedef struct {
    uint32_t image; // a GL texture name
    uint32_t width, height;
} vr_target_t;

typedef struct {
    void *user;
    bool (*acquire)(void *user, vr_surface_t surface, vr_target_t *out);
    void (*release)(void *user, vr_surface_t surface);
} vr_provider_t;

// After the GL context and Fast3D exist. `dump_dir` may be NULL.
void vr_render_init(const vr_render_settings_t *settings, const vr_provider_t *provider, const char *dump_dir);
void vr_render_shutdown(void);

// Applied at the next stereo frame; the panel is re-placed at the next mono one.
void vr_render_request_recentre(void);
// The same, taking the standing height again (vr_stage_request_height).
void vr_render_request_height(void);

// One XR frame. `renders` is false in standby: nothing is drawn and `layers`
// comes back empty. Fills `layers` with what to submit.
void vr_render_frame(Gfx *dl, const vr_frame_t *frame, bool renders, vr_layers_t *layers);

typedef struct {
    float eye_ms[2]; // CPU time of each eye's replay (submission, not GPU)
    float hud_ms;
    float panel_ms;
    float gpu_ms; // GPU time of the eye and HUD passes of a frame a few frames back; < 0 when unknown
} vr_render_timing_t;
const vr_render_timing_t *vr_render_timing(void);

typedef struct {
    vr_mode_t mode;
    uint32_t frames;
    uint32_t tris_eye[2];
    uint32_t tris_hud;
    uint32_t world_proj_loads[2];
    float fade_alpha;
    const char *mono_reason; // why the last mono frame was mono
} vr_render_info_t;
const vr_render_info_t *vr_render_info(void);

#endif
