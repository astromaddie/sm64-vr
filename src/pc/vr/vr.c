#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "vr.h"
#include "vr_compose.h"
#include "vr_game.h"
#include "vr_gl.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_math.h"
#include "vr_paths.h"
#include "vr_perf.h"
#include "vr_rate.h"
#include "vr_render.h"
#include "vr_sched.h"
#include "vr_stage.h"
#include "vr_xr.h"

#define VR_APP_NAME "SM64VR"
#define VR_PROBE_SESSION_WAIT_MS 20000
#define VR_SHUTDOWN_WAIT_MS 1000

static struct {
    bool enabled;
    vr_settings_t settings;
    vr_render_settings_t render_settings;
    vr_xr_t xr;
    char dump_dir[1100];
    vr_input_t *input; // the action set; NULL when it could not be made
    vr_sched_t sched;

    // Surfaces besides the eyes: the HUD and the mono panel (ui size) and the fade (tiny)
    vr_swapchain_t hud;
    vr_swapchain_t panel;
    vr_swapchain_t fade;
    uint32_t recentres_seen;
    uint32_t focus_gains_seen;

    // What this frame submits
    vr_layers_t layers;
    bool layers_valid;
    bool want_projection; // the probe
    vr_compose_t compose;
    XrCompositionLayerProjection probe_layer;
    XrCompositionLayerProjectionView probe_views[2];

    // The frame's plan
    bool probing; // the probe runs no game: no ticks are planned
    int ticks_planned;
    bool sim_was_active;
    uint64_t ticks_total;

    vr_perf_t perf;
    double tick_ms;
    vr_rate_t rate; // is the display rate held? (reported once)

    // Runtime settings changes waiting to be written
    bool settings_dirty;
    double settings_changed_s;
    unsigned settings_revision;
} vr;

static float setting(const char *name) {
    return (float)vr_settings_get(&vr.settings, name);
}

bool vr_enabled(void) {
    return vr.enabled;
}

bool vr_exit_requested(void) {
    return vr.enabled && vr.xr.exit_requested;
}

const vr_settings_t *vr_settings(void) {
    return &vr.settings;
}

// A change made on the in-headset page is written once the page has been quiet for this long
#define VR_SETTINGS_SAVE_DELAY_S 0.5

static double now_seconds(void) {
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

double vr_setting_get(const char *key) {
    return vr_settings_get(&vr.settings, key);
}

bool vr_setting_set(const char *key, double value) {
    if (!vr.enabled || !vr_settings_set(&vr.settings, key, value)) {
        return false;
    }
    // The renderer's copy is rebuilt from the registry values, so a key another feature
    // adds to vr_render_settings_from() is picked up with no change here
    vr_render_settings_from(&vr.render_settings, &vr.settings);
    vr_render_apply_settings(&vr.render_settings);
    vr.settings_revision++;
    vr.settings_dirty = true;
    vr.settings_changed_s = now_seconds();
    return true;
}

bool vr_setting_reset(const char *key) {
    return vr_setting_set(key, vr_settings_default(key));
}

void vr_settings_flush(void) {
    if (!vr.settings_dirty) {
        return;
    }
    vr.settings_dirty = false;
    if (!vr.settings.path[0]) {
        return; // no data dir: the values live in memory only
    }
    if (!vr_settings_save(&vr.settings)) {
        vr_logf("settings: could not write %s; the changes stay in memory", vr.settings.path);
    }
}

unsigned vr_settings_revision(void) {
    return vr.settings_revision;
}

void vr_recentre_now(void) {
    vr_request_recentre();
}

const char *vr_data_dir(void) {
    return vr_paths_runtime_data_dir();
}

void vr_ui_size(int *width, int *height) {
    *width = vr.render_settings.ui_width > 0 ? vr.render_settings.ui_width : VR_UI_WIDTH;
    *height = vr.render_settings.ui_height > 0 ? vr.render_settings.ui_height : VR_UI_HEIGHT;
}


// -----------------------------------------------------------------------------
// The renderer's provider: the runtime's swapchains

static bool frame_renders(void) {
    return vr.xr.frame_open && (vr.xr.frame.should_render || vr.xr.config.force_render);
}

static vr_swapchain_t *surface_swapchain(vr_surface_t surface) {
    switch (surface) {
        case VR_SURFACE_EYE0: return &vr.xr.eye[0];
        case VR_SURFACE_EYE1: return &vr.xr.eye[1];
        case VR_SURFACE_HUD: return &vr.hud;
        case VR_SURFACE_PANEL: return &vr.panel;
        default: return NULL;
    }
}

static bool provider_acquire(void *user, vr_surface_t surface, vr_target_t *out) {
    (void)user;
    vr_swapchain_t *sc = surface_swapchain(surface);
    if (!sc) {
        return false;
    }
    if (sc->handle == XR_NULL_HANDLE && (surface == VR_SURFACE_HUD || surface == VR_SURFACE_PANEL)) {
        if (!vr_xr_swapchain_create(&vr.xr, sc, (uint32_t)vr.render_settings.ui_width, (uint32_t)vr.render_settings.ui_height)) {
            return false;
        }
    }
    uint32_t index;
    if (sc->handle == XR_NULL_HANDLE || !vr_xr_swapchain_acquire(&vr.xr, sc, &index)) {
        return false;
    }
    out->image = sc->images[index].image;
    out->width = sc->width;
    out->height = sc->height;
    return true;
}

static void provider_release(void *user, vr_surface_t surface) {
    (void)user;
    vr_swapchain_t *sc = surface_swapchain(surface);
    if (sc) {
        vr_xr_swapchain_release(&vr.xr, sc);
    }
}


// -----------------------------------------------------------------------------
// Init and shutdown

bool vr_init(void *sdl_window) {
    memset(&vr, 0, sizeof(vr));
    vr_render_settings_t defaults;
    memset(&defaults, 0, sizeof(defaults));

    const char *dir = vr_paths_runtime_data_dir();
    if (dir[0]) {
        char path[1100];
        snprintf(path, sizeof(path), "%svr.toml", dir);
        vr_settings_load(&vr.settings, path);
        vr_logf("data dir %s", dir);
    } else {
        vr_logf("no data dir (XDG_DATA_HOME and HOME are unset); using default settings");
        vr_settings_defaults(&vr.settings);
        vr_settings_apply_env(&vr.settings, NULL);
    }
    vr_render_settings_from(&vr.render_settings, &vr.settings);

    if (!vr_gl_init(sdl_window)) {
        vr_logf("VR unavailable: no usable GL binding; running flat");
        return false;
    }

    vr_xr_api_t api;
    vr_xr_api_loader(&api);
    vr_xr_config_t config = {
        .app_name = VR_APP_NAME,
        .resolution_scale = setting("resolution_scale"),
        .refresh_rate = (int)setting("refresh_rate"),
        .force_render = setting("force_render") != 0,
        .fov_mode = (int)setting("fov_mode")
    };
    if (!vr_xr_instance_create(&vr.xr, &api, &config)) {
        vr_logf("VR unavailable: no OpenXR runtime or headset; running flat");
        vr_gl_shutdown();
        return false;
    }

    int major, minor;
    vr_gl_version(&major, &minor);
    uint32_t min_major = XR_VERSION_MAJOR(vr.xr.gl_min_version);
    uint32_t min_minor = XR_VERSION_MINOR(vr.xr.gl_min_version);
    vr_logf("GL context %d.%d; the runtime needs %u.%u", major, minor, min_major, min_minor);
    if ((uint32_t)major < min_major || ((uint32_t)major == min_major && (uint32_t)minor < min_minor)) {
        vr_logf("VR unavailable: the runtime needs GL %u.%u, the context is %d.%d; running flat", min_major, min_minor, major, minor);
        vr_xr_destroy(&vr.xr);
        vr_gl_shutdown();
        return false;
    }

    if (!vr_xr_session_create(&vr.xr, vr_gl_graphics_binding())) {
        vr_logf("VR unavailable: the session could not be created; running flat");
        vr_xr_destroy(&vr.xr);
        vr_gl_shutdown();
        return false;
    }

    vr_logf("session created: swapchain format 0x%llx (%s)", (long long)vr.xr.color_format, vr.xr.color_srgb ? "sRGB" : "linear RGBA8");
    for (int e = 0; e < 2; e++) {
        vr_logf("eye %d swapchain %ux%u (%u px base x resolution_scale %.2f; runtime recommends %ux%u, maximum %ux%u); the game renders into it directly",
            e, vr.xr.eye[e].width, vr.xr.eye[e].height, VR_EYE_BASE_PX, config.resolution_scale,
            vr.xr.eye_recommended[e][0], vr.xr.eye_recommended[e][1], vr.xr.eye_max[e][0], vr.xr.eye_max[e][1]);
    }
    if (vr.xr.color_format != VR_GL_SRGB8_ALPHA8 && vr.xr.color_format != VR_GL_RGBA8) {
        vr_logf("WARNING: the swapchain format has no alpha channel, so the HUD quad cannot be translucent");
    }
    vr.input = vr_input_create(&vr.xr);
    vr_sched_init(&vr.sched, VR_SCHED_TICK_S, VR_SCHED_MAX_CATCH_UP);

    if (dir[0]) {
        snprintf(vr.dump_dir, sizeof(vr.dump_dir), "%svr-dumps", dir);
    }
    vr_provider_t provider = { .user = NULL, .acquire = provider_acquire, .release = provider_release };
    vr_render_init(&vr.render_settings, &provider, vr.dump_dir[0] ? vr.dump_dir : NULL);
    vr_perf_init(&vr.perf);
    vr_rate_init(&vr.rate);
    vr.enabled = true;
    gVrActive = true;
    vr_logf("VR mode on (force_render %d, diag_yaw %.1f, dump_frame %d, timing %d)",
        config.force_render, setting("diag_yaw"), (int)setting("dump_frame"), (int)setting("timing"));
    return true;
}

void vr_shutdown(void) {
    if (!vr.enabled) {
        return;
    }
    vr.enabled = false;
    gVrActive = false;

    // A clean exit: ask, then keep polling until the runtime has stopped the session
    vr_xr_request_exit(&vr.xr);
    uint32_t waited = 0;
    while (vr.xr.session_running && waited < VR_SHUTDOWN_WAIT_MS) {
        vr_xr_poll_events(&vr.xr);
        SDL_Delay(5);
        waited += 5;
    }

    vr_render_shutdown();
    vr_input_destroy(vr.input, &vr.xr);
    vr.input = NULL;
    vr_xr_swapchain_destroy(&vr.xr, &vr.hud);
    vr_xr_swapchain_destroy(&vr.xr, &vr.panel);
    vr_xr_swapchain_destroy(&vr.xr, &vr.fade);
    vr_xr_destroy(&vr.xr);
    vr_gl_shutdown();
    vr_settings_flush();
    vr_settings_free(&vr.settings);
    vr_logf("VR shut down after %llu ticks", (unsigned long long)vr.ticks_total);
}


// -----------------------------------------------------------------------------
// Frame loop

bool vr_frame_begin(void) {
    vr_xr_poll_events(&vr.xr);
    if (vr.settings_dirty && now_seconds() - vr.settings_changed_s >= VR_SETTINGS_SAVE_DELAY_S) {
        vr_settings_flush();
    }

    // A system recentre (the runtime moved the LOCAL origin), focus gained
    // (maybe another wearer) or the input layer's request (L View held): the
    // renderer recentres at the next stereo frame boundary
    bool recentre = vr_take_recentre_request();
    if (vr.recentres_seen != vr.xr.recentre_requests || vr.focus_gains_seen != vr.xr.focus_gains) {
        vr.recentres_seen = vr.xr.recentre_requests;
        vr.focus_gains_seen = vr.xr.focus_gains;
        recentre = true;
    }
    if (recentre) {
        vr_render_request_recentre();
    }

    vr.layers_valid = false;
    vr.want_projection = false;
    vr.ticks_planned = 0;
    bool begun = vr_xr_frame_begin(&vr.xr);
    // Actions after xrBeginFrame; with no frame, no focus or no session this releases every input
    vr_input_update(vr.input, &vr.xr, setting("haptics"));
    if (!begun) {
        return false;
    }

    // The sim runs while the frame is shown and the app has focus (or under
    // force_render); otherwise it is frozen and the timeline re-anchors when it resumes
    bool active = vr_xr_sim_active(&vr.xr) && !vr.probing;
    if (active) {
        double display_s = (double)vr.xr.frame.predicted_display_time * 1e-9;
        vr_sched_plan_t plan = vr_sched_plan(&vr.sched, display_s);
        vr.ticks_planned = plan.ticks;
        if (plan.rebased && vr.sched.rebases > 1) {
            vr_logf("scheduler: timeline re-anchored (%u so far)", vr.sched.rebases);
        }
    } else if (vr.sim_was_active) {
        vr_sched_reset(&vr.sched);
        vr_logf("sim frozen (shouldRender %d, focused %d)", vr.xr.frame.should_render, vr.xr.focused);
    }
    if (active && !vr.sim_was_active) {
        vr_logf("sim running (shouldRender %d, focused %d, force_render %d)",
            vr.xr.frame.should_render, vr.xr.focused, vr.xr.config.force_render);
    }
    vr.sim_was_active = active;
    vr.tick_ms = 0;
    return true;
}

int vr_frame_ticks(void) {
    return vr.ticks_planned;
}

void vr_frame_tick_ran(void) {
    vr_sched_tick_ran(&vr.sched);
    vr.ticks_total++;
}

float vr_frame_interp_t(void) {
    return vr_sched_t_at(&vr.sched, (double)vr.xr.frame.predicted_display_time * 1e-9);
}

void vr_frame_render(Gfx *dl) {
    vr_render_frame(dl, &vr.xr.frame, frame_renders(), &vr.layers);
    vr.layers_valid = true;
}

// Clears the fade swapchain's image to `rgb` at `alpha`: the quad the compositor blends over everything
static bool fade_render(float alpha, const float rgb[3]) {
    if (vr.fade.handle == XR_NULL_HANDLE && !vr_xr_swapchain_create(&vr.xr, &vr.fade, 4, 4)) {
        return false;
    }
    uint32_t index;
    if (!vr_xr_swapchain_acquire(&vr.xr, &vr.fade, &index)) {
        return false;
    }
    // The quad's alpha is premultiplied (no UNPREMULTIPLIED flag on the fade layer)
    vr_gl_clear_image(vr.fade.images[index].image, vr.fade.width, vr.fade.height,
        rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha, alpha);
    vr_xr_swapchain_release(&vr.xr, &vr.fade);
    return true;
}

static void rate_update(void);

void vr_frame_end(void) {
    const XrCompositionLayerBaseHeader *probe[1];
    const XrCompositionLayerBaseHeader *const *layers = NULL;
    uint32_t count = 0;

    if (vr.layers_valid) {
        bool fade = vr.layers.fade_alpha > VR_FADE_MIN_ALPHA && fade_render(vr.layers.fade_alpha, vr.layers.fade_rgb);
        count = vr_compose_layers(&vr.compose, &vr.xr, &vr.layers, &vr.hud, &vr.panel, fade ? &vr.fade : NULL);
        layers = vr.compose.list;
    } else if (vr.want_projection) {
        // The probe's eyes
        vr_xr_projection_layer(&vr.xr, &vr.probe_layer, vr.probe_views);
        probe[0] = (const XrCompositionLayerBaseHeader *)&vr.probe_layer;
        layers = probe;
        count = 1;
    }
    vr_xr_frame_end(&vr.xr, layers, count);
    rate_update();

    if (vr.render_settings.timing && vr.layers_valid) {
        const vr_render_timing_t *t = vr_render_timing();
        char line[256];
        double now_s = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
        float replay_ms = t->eye_ms[0] + t->eye_ms[1] + t->panel_ms;
        if (vr_perf_frame(&vr.perf, now_s, vr.xr.frame.predicted_display_time, vr.xr.frame.predicted_display_period,
                (float)vr.tick_ms, replay_ms, t->hud_ms, t->eye_ms, t->gpu_ms, line, sizeof(line))) {
            const vr_render_info_t *info = vr_render_info();
            printf("%s ticks=%llu mode=%s tris=%u/%u hud_tris=%u\n", line, (unsigned long long)vr.ticks_total,
                info->mode == VR_MODE_STEREO ? "stereo" : "mono", info->tris_eye[0], info->tris_eye[1], info->tris_hud);
            fflush(stdout);
        }
    }
}

// Is the display rate held? (standard.md section 9) The app can't change the rate on the Frame:
// SteamVR runs the title at its per-app rate. While worn, two 10 s windows in a row below 90% of the
// display's rate are reported once, with the per-app rate to set instead. The display rate is the
// runtime's frame period. diag_rate_unworn also counts frames without a wearer, for unattended runs.
static void rate_update(void) {
    int64_t period = vr.xr.frame.predicted_display_period;
    int target = period > 0 ? (int)(1e9 / (double)period + 0.5) : vr_xr_target_refresh_rate(&vr.xr);
    if (target != vr.rate.target_hz) {
        vr_rate_set_target(&vr.rate, target);
    }
    bool worn = vr.xr.frame.should_render && vr.xr.focused;
    bool unworn_diag = setting("diag_rate_unworn") != 0 && (vr.xr.frame.should_render || vr.xr.config.force_render);
    bool counting = vr.xr.session_running && (worn || unworn_diag);
    double now_s = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    switch (vr_rate_frame(&vr.rate, now_s, counting)) {
        case VR_RATE_WINDOW_OK:
            vr_logf("refresh rate: %.1f fps over %.0f s at %d Hz (frame period %.2f ms)", vr.rate.last_fps,
                VR_RATE_WINDOW_S, vr.rate.target_hz, (double)period * 1e-6);
            break;
        case VR_RATE_WINDOW_LOW:
            vr_logf("refresh rate: %.1f fps over %.0f s at %d Hz is below %.0f%%", vr.rate.last_fps, VR_RATE_WINDOW_S,
                vr.rate.target_hz, VR_RATE_LOW_FRACTION * 100.0);
            break;
        case VR_RATE_NOT_HELD: {
            int hz = vr.rate.target_hz;
            vr_logf("refresh rate: the frame rate is not held: %.1f fps over %.0f s at %d Hz, below %.0f%% for two windows in a row",
                vr.rate.last_fps, VR_RATE_WINDOW_S, hz, VR_RATE_LOW_FRACTION * 100.0);
            if (hz > 72) {
                vr_logf("refresh rate: the app can't change the rate; set the SM64 title to %d Hz in SteamVR's per-app settings "
                    "(or lower resolution_scale)", hz > 90 ? 90 : 72);
            } else {
                vr_logf("refresh rate: already at the lowest rate; lower resolution_scale");
            }
            break;
        }
        default:
            break;
    }
}

void vr_frame_add_tick_ms(double ms) {
    vr.tick_ms += ms;
}


// -----------------------------------------------------------------------------
// Probe: left eye red, right eye blue, before any game code

static bool eye_acquire(int eye, uint32_t *gl_image, uint32_t *width, uint32_t *height) {
    uint32_t index;
    if (!vr.enabled || eye < 0 || eye > 1 || !vr_xr_swapchain_acquire(&vr.xr, &vr.xr.eye[eye], &index)) {
        return false;
    }
    *gl_image = vr.xr.eye[eye].images[index].image;
    *width = vr.xr.eye[eye].width;
    *height = vr.xr.eye[eye].height;
    return true;
}

void vr_probe(void) {
    float seconds = setting("probe_seconds");
    if (!vr.enabled || seconds <= 0) {
        return;
    }

    vr_logf("probe: %.1f s, left eye red, right eye blue", seconds);
    vr_logf("probe: swapchain format 0x%llx (%s), eye size %ux%u (recommended %ux%u), graphics binding opengl_xlib",
        (long long)vr.xr.color_format, vr.xr.color_srgb ? "sRGB" : "linear RGBA8",
        vr.xr.eye[0].width, vr.xr.eye[0].height, vr.xr.eye_recommended[0][0], vr.xr.eye_recommended[0][1]);

    vr.probing = true;
    uint32_t start = SDL_GetTicks();
    uint32_t running_since = 0;
    bool running = false;
    bool standby_logged = false;
    uint32_t frames = 0;
    uint32_t rendered = 0;

    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                vr.probing = false;
                return;
            }
        }
        uint32_t now = SDL_GetTicks();

        if (!vr_frame_begin()) {
            if (vr.xr.exit_requested) {
                break;
            }
            if (!running && now - start > VR_PROBE_SESSION_WAIT_MS) {
                vr_logf("probe: the session did not start within %d s", VR_PROBE_SESSION_WAIT_MS / 1000);
                break;
            }
            SDL_Delay(5);
            continue;
        }

        if (!running) {
            running = true;
            running_since = now;
            vr_logf("probe: session running");
        }
        bool done = (now - running_since) >= (uint32_t)(seconds * 1000.0f);
        frames++;

        if (!done && frame_renders() && vr.xr.frame.views_valid) {
            for (int eye = 0; eye < 2; eye++) {
                uint32_t image, w, h;
                if (eye_acquire(eye, &image, &w, &h)) {
                    vr_gl_clear_image(image, w, h, eye == 0 ? 1.0f : 0.0f, 0.0f, eye == 0 ? 0.0f : 1.0f, 1.0f);
                    vr_xr_swapchain_release(&vr.xr, &vr.xr.eye[eye]);
                }
            }
            vr.want_projection = true;
            rendered++;
        } else if (!done && !vr.xr.frame.should_render && !standby_logged) {
            standby_logged = true;
            vr_logf("probe: standby (shouldRender is false); the loop keeps running, nothing is drawn");
        }

        vr_frame_end();
        if (done) {
            break;
        }
    }

    vr.probing = false;
    vr_logf("probe: done, %u frames, %u with both eyes drawn", frames, rendered);
}
