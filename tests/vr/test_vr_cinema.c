// The render test checks the game's own atan2f against libc's on purpose
#define VR_ALLOW_ATAN2F 1
// The production cinema maths, classification/latch, stage transitions and
// OpenXR layer builder. No graphics context or runtime is required.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_cinema.h"
#include "vr_compose.h"

#ifdef VR_CINEMA_RENDER_TEST
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "vr_game.h"
#include "vr_preview.h"
#include "vr_render.h"
#include "pc/gfx/gfx_vr.h"
#include "vr.h"
#include "vr_input.h"
#include "vr_settings.h"
#include "cinema_game_path.h"
#endif

#define SEC 1000000000LL
#define HANDLE(n) ((void *)(uintptr_t)(n))

static vr_view_t eyes[2];

static void tracked_eyes(float yaw) {
    memset(eyes, 0, sizeof(eyes));
    float q[4], rot[9];
    vr_quat_from_yaw(yaw, q);
    vr_mat3_from_quat(q, rot);
    for (int e = 0; e < 2; e++) {
        memcpy(eyes[e].pose.orientation, q, sizeof(q));
        float offset = e ? 0.0345f : -0.0345f;
        for (int i = 0; i < 3; i++) {
            eyes[e].pose.position[i] = rot[i] * offset;
        }
        eyes[e].pose.position[1] += 1.6f;
        eyes[e].fov[0] = eyes[e].fov[3] = -0.9f;
        eyes[e].fov[1] = eyes[e].fov[2] = 0.9f;
    }
}

static void test_classification(void) {
    CHECK(!vr_cinema_cutscene(0));
    // Local action/warp feedback deliberately stays in gameplay stereo.
    const unsigned stereo[] = {130, 131, 134, 135, 139, 140, 141, 149, 151,
        152, 153, 154, 155, 156, 157, 158, 159, 160, 169, 170, 179, 180, 181};
    for (unsigned i = 0; i < sizeof(stereo) / sizeof(stereo[0]); i++) {
        CHECK(!vr_cinema_cutscene(stereo[i]));
    }
    const unsigned cinema[] = {133, 142, 143, 144, 145, 147, 150, 161, 162, 163,
        164, 165, 166, 167, 168, 171, 172, 173, 174, 175, 176, 177, 178, 182};
    for (unsigned i = 0; i < sizeof(cinema) / sizeof(cinema[0]); i++) {
        CHECK(vr_cinema_cutscene(cinema[i]));
        CHECK(strlen(vr_cinema_cutscene_reason(cinema[i])) > 8);
    }
    CHECK(vr_cinema_cutscene(200)); // mod-defined scripted camera, safe default
}

static void test_latch(void) {
    vr_cinema_latch_t s = {0};
    CHECK(!vr_cinema_latch(&s, true, false, SEC));
    CHECK(!vr_cinema_latch(&s, true, false, SEC + VR_CINEMA_ENTER_NS - 1));
    CHECK(vr_cinema_latch(&s, true, false, SEC + VR_CINEMA_ENTER_NS));
    CHECK(vr_cinema_latch(&s, false, false, 2 * SEC));
    CHECK(vr_cinema_latch(&s, false, false, 2 * SEC + VR_CINEMA_EXIT_NS - 1));
    CHECK(vr_cinema_latch(&s, true, false, 2 * SEC + VR_CINEMA_EXIT_NS)); // gap cancelled
    CHECK(vr_cinema_latch(&s, false, false, 3 * SEC));
    CHECK(vr_cinema_latch(&s, false, true, 4 * SEC)); // warp hold does not end cinema
    CHECK(vr_cinema_latch(&s, false, false, 5 * SEC));
    CHECK(!vr_cinema_latch(&s, false, false, 5 * SEC + VR_CINEMA_EXIT_NS));
    CHECK(!vr_cinema_latch(&s, true, false, 10 * SEC));
    CHECK(!vr_cinema_latch(&s, true, false, 9 * SEC)); // backwards clock re-arms
    CHECK(vr_cinema_latch(&s, true, false, 9 * SEC + VR_CINEMA_ENTER_NS));

    // The same timing at every display rate, never N render frames.
    const int rates[] = {72, 90, 120};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        memset(&s, 0, sizeof(s));
        int64_t period = SEC / rates[r];
        int n = 0;
        while (!vr_cinema_latch(&s, true, false, SEC + n * period)) {
            CHECK(n * period < VR_CINEMA_ENTER_NS);
            n++;
        }
        CHECK(n * period >= VR_CINEMA_ENTER_NS);
        CHECK(n * period < VR_CINEMA_ENTER_NS + period);
        n = 0;
        while (vr_cinema_latch(&s, false, false, 2 * SEC + n * period)) {
            CHECK(n * period < VR_CINEMA_EXIT_NS);
            n++;
        }
        CHECK(n * period < VR_CINEMA_EXIT_NS + period);
    }
}

static void pose_view(const vr_pose_t *pose, float view[16]) {
    float rot[9], transform[16] = {0};
    vr_mat3_from_quat(pose->orientation, rot);
    for (int c = 0; c < 3; c++) {
        for (int row = 0; row < 3; row++) {
            transform[c * 4 + row] = rot[c * 3 + row];
        }
        transform[12 + c] = pose->position[c];
    }
    transform[15] = 1;
    vr_mat4_rigid_inverse(transform, view);
}

static void test_screen_corners(void) {
    const float yaw[] = {0, 1.5707963f, -1.5707963f, 3.14159265f};
    for (unsigned y = 0; y < sizeof(yaw) / sizeof(yaw[0]); y++) {
        tracked_eyes(yaw[y]);
        vr_pose_t head;
        vr_head_pose(eyes, &head);
        head.position[0] += 1.2f;
        head.position[2] -= 0.7f;
        vr_cinema_screen_t screen;
        vr_cinema_place(&head, 3, 3.6f, &screen);
        // Translation, head pitch and roll do not move/roll the anchored screen.
        for (int e = 0; e < 2; e++) {
            eyes[e].pose.position[0] += 1.4f;
            eyes[e].pose.position[1] += 0.15f;
            eyes[e].pose.position[2] -= 0.8f;
            eyes[e].pose.orientation[0] = 0.4f;
            eyes[e].pose.orientation[2] = 0.3f;
            vr_quat_normalize(eyes[e].pose.orientation);
        }
        vr_view_t submitted[2];
        CHECK(vr_cinema_views(&screen, eyes, submitted));
        float rot[9];
        vr_mat3_from_quat(screen.pose.orientation, rot);
        for (int e = 0; e < 2; e++) {
            CHECK_NEAR(submitted[e].pose.orientation[0], 0, 1e-6);
            CHECK_NEAR(submitted[e].pose.orientation[2], 0, 1e-6);
            float view[16], proj[16];
            pose_view(&submitted[e].pose, view);
            vr_projection_fov(submitted[e].fov, 0.05f, 100, proj);
            for (int x = -1; x <= 1; x += 2) {
                for (int y_corner = -1; y_corner <= 1; y_corner += 2) {
                    float local[3] = {x * screen.width / 2, y_corner * screen.height / 2, 0};
                    float point[3], ndc[3];
                    vr_mat3_mulv(rot, local, point);
                    for (int i = 0; i < 3; i++) {
                        point[i] += screen.pose.position[i];
                    }
                    CHECK(vr_project(view, proj, point, ndc) > 0);
                    CHECK_NEAR(ndc[0], x, 2e-5);
                    CHECK_NEAR(ndc[1], y_corner, 2e-5);
                }
            }
        }
        vr_pose_t band;
        float width, height;
        vr_cinema_band(&screen, &band, &width, &height);
        CHECK_NEAR(width / height, 16.0 / 9.0, 1e-5); // glyph aspect preserved
        CHECK(band.position[1] + height / 2 < screen.pose.position[1] - screen.height / 2);
        CHECK_NEAR(band.position[0], screen.pose.position[0], 1e-6);
        CHECK_NEAR(band.position[2], screen.pose.position[2], 1e-6);
        CHECK_NEAR(band.orientation[2], 0, 1e-6);
        // Behind the plane is rejected, with no partially overwritten output.
        memcpy(eyes[1].pose.position, screen.pose.position, sizeof(screen.pose.position));
        vr_view_t before[2];
        memcpy(before, submitted, sizeof(before));
        CHECK(!vr_cinema_views(&screen, eyes, submitted));
        CHECK(memcmp(before, submitted, sizeof(before)) == 0);
    }
}

static void test_positive_parallax(void) {
    vr_pose_t pose = {.orientation = {0, 0.6f, 0, 0.8f}, .position = {1234, 340, -987}};
    float view[16];
    pose_view(&pose, view);
    vr_game_cam_t cam;
    CHECK(vr_game_cam_from_view(view, &cam));
    const float near_values[] = {5, 100, 300};
    for (unsigned n = 0; n < sizeof(near_values) / sizeof(near_values[0]); n++) {
        float near = near_values[n], far = 20000;
        float game_proj[16];
        // Off-centre source projection too: test more than a symmetric fixture.
        vr_projection_tan(-0.8f, 1.0f, 0.5f, -0.6f, near, far, game_proj);
        vr_eye_cam_t eye[2];
        float fov[2][4], proj[2][16];
        for (int e = 0; e < 2; e++) {
            CHECK(vr_cinema_game_eye(&cam, game_proj, 0.069f, 3.6f, e, &eye[e], fov[e], proj[e]));
        }
        // The game's modelview already includes cam.view: feed camera-space
        // points to the replacement projection, as Fast3D actually does.
        const float depth[] = {near, near * 1.1f, near * 5, 19000};
        for (unsigned d = 0; d < sizeof(depth) / sizeof(depth[0]); d++) {
            float point[3] = {0.1f * depth[d], -0.03f * depth[d], -depth[d]};
            float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            float ndc[2][3];
            vr_project(identity, proj[0], point, ndc[0]);
            vr_project(identity, proj[1], point, ndc[1]);
            float disparity = (ndc[1][0] - ndc[0][0]) * 3.6f / 2;
            CHECK(disparity >= -2e-5); // never crossed/negative parallax
            CHECK(disparity <= 0.6f * 0.069f + 2e-5); // no divergent infinity
            CHECK_NEAR(ndc[0][1], ndc[1][1], 2e-5);
            if (d == 0) {
                CHECK_NEAR(disparity, 0, 2e-5); // near clip = screen plane
            } else {
                CHECK(disparity > 0);
            }
        }
        // The original depth mapping (fog and clipping) survives.
        CHECK_NEAR(proj[0][10], game_proj[10], 1e-6);
        CHECK_NEAR(proj[0][14], game_proj[14], 3e-3);
        CHECK(!vr_cinema_game_eye(&cam, game_proj, 0.069f, 0, 0, &eye[0], fov[0], NULL));
        CHECK(!vr_cinema_game_eye(&cam, game_proj, NAN, 3.6f, 0, &eye[0], fov[0], NULL));
    }
}

static void test_picture_aspect(void) {
    const float aspects[] = {1, 4.f / 3.f, 16.f / 9.f, 21.f / 9.f};
    const float degrees[] = {29, 45, 80};
    vr_game_cam_t cam;
    float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    CHECK(vr_game_cam_from_view(identity, &cam));
    for (unsigned a = 0; a < sizeof(aspects) / sizeof(aspects[0]); a++) {
        for (unsigned d = 0; d < sizeof(degrees) / sizeof(degrees[0]); d++) {
            float ty = tanf(degrees[d] * 3.14159265358979323846f / 360.f);
            float source[16];
            vr_projection_tan(-ty * aspects[a], ty * aspects[a], ty, -ty, 100, 20000, source);
            source[8] = 0.1f; // preserve an authored asymmetric lens centre
            source[9] = -0.05f;
            for (int e = 0; e < 2; e++) {
                vr_eye_cam_t eye;
                float fov[4], projection[16];
                CHECK(vr_cinema_game_eye(&cam, source, 0.069f, 3.6f, e, &eye, fov, projection));
                CHECK_NEAR(projection[5] / projection[0], 16.f / 9.f, 1e-5);
                CHECK_NEAR(projection[5], source[5], 1e-6);
                CHECK_NEAR(projection[9], source[9], 1e-6);
                CHECK_NEAR(projection[10], source[10], 1e-6);
                CHECK_NEAR(projection[14], source[14], 1e-6);
                // Equal camera-space lengths have equal lengths on the
                // physical picture, even for a mod's non-16:9 projection.
                CHECK_NEAR(projection[0] * 3.6f, projection[5] * 2.025f, 1e-5);
            }
        }
    }
}

static void test_cinema_frame_and_transitions(void) {
    tracked_eyes(0.7f);
    vr_stage_t stage;
    vr_stage_init(&stage);
    vr_cinema_latch_t latch = {0};
    vr_stage_set_mode(&stage, VR_MODE_STEREO, SEC);
    bool requested = vr_cinema_cutscene(VR_CS_INTRO_PEACH);
    CHECK(!vr_cinema_latch(&latch, requested, false, 2 * SEC));
    CHECK(vr_cinema_latch(&latch, requested, false, 2 * SEC + VR_CINEMA_ENTER_NS));
    CHECK(vr_stage_set_mode(&stage, VR_MODE_CINEMA, 2 * SEC + VR_CINEMA_ENTER_NS));
    vr_cinema_screen_t screen;
    vr_stage_cinema_screen(&stage, eyes, 3, 3.6f, &screen);
    vr_layers_t l = {.projection = true, .hud = true};
    CHECK(vr_cinema_views(&screen, eyes, l.views));
    vr_cinema_band(&screen, &l.hud_pose, &l.hud_width, &l.hud_height);

    // Real composer + real xr layer helpers, deliberately different from the
    // runtime's head orientation/FOV. A per-eye quad cannot pass this check.
    vr_xr_t xr = {0};
    xr.local_space = HANDLE(0x31);
    for (int e = 0; e < 2; e++) {
        xr.eye[e].handle = HANDLE(0x41 + e);
        xr.eye[e].width = xr.eye[e].height = 2160;
        xr.frame.views[e] = eyes[e];
    }
    vr_swapchain_t hud = {.handle = HANDLE(0x51), .width = 1280, .height = 720};
    vr_compose_t composition;
    CHECK_INT(vr_compose_layers(&composition, &xr, &l, &hud, NULL, NULL), 2);
    CHECK_INT(composition.list[0]->type, XR_TYPE_COMPOSITION_LAYER_PROJECTION);
    for (int e = 0; e < 2; e++) {
        const XrCompositionLayerProjectionView *pv = &composition.projection_views[e];
        CHECK_NEAR(pv->pose.position.x, eyes[e].pose.position[0], 1e-6);
        CHECK_NEAR(pv->pose.position.y, eyes[e].pose.position[1], 1e-6);
        CHECK_NEAR(pv->pose.position.z, eyes[e].pose.position[2], 1e-6);
        CHECK_NEAR(pv->pose.orientation.y, screen.pose.orientation[1], 1e-6);
        CHECK_NEAR(pv->fov.angleLeft, l.views[e].fov[0], 1e-6);
        CHECK_NEAR(pv->fov.angleRight, l.views[e].fov[1], 1e-6);
        CHECK_NEAR(pv->fov.angleUp, l.views[e].fov[2], 1e-6);
        CHECK_NEAR(pv->fov.angleDown, l.views[e].fov[3], 1e-6);
        CHECK(pv->subImage.swapchain == xr.eye[e].handle);
    }
    CHECK(composition.hud.pose.position.y + composition.hud.size.height / 2 < screen.pose.position[1] - screen.height / 2);

    // Camera cuts/timer resets and a one-tick gap never re-anchor or fade.
    uint32_t switches = stage.mode_switches;
    CHECK(vr_cinema_latch(&latch, false, false, 3 * SEC));
    CHECK(vr_cinema_latch(&latch, vr_cinema_cutscene(VR_CS_CREDITS), false, 3 * SEC + SEC / 30));
    CHECK(!vr_stage_set_mode(&stage, VR_MODE_CINEMA, 3 * SEC));
    tracked_eyes(-1.2f);
    vr_cinema_screen_t held;
    vr_stage_cinema_screen(&stage, eyes, 3, 3.6f, &held);
    CHECK(memcmp(&held, &screen, sizeof(screen)) == 0);
    CHECK_INT(stage.mode_switches, switches);
    vr_stage_request_recentre(&stage);
    vr_stage_cinema_screen(&stage, eyes, 3, 3.6f, &held);
    CHECK_NEAR(vr_yaw_of(held.pose.orientation), -1.2, 1e-5);

    // Every mode change shares exactly the stage's 0.25 + 0.35 s fade.
    const vr_mode_t modes[] = {VR_MODE_MONO, VR_MODE_STEREO, VR_MODE_CINEMA, VR_MODE_STEREO, VR_MODE_MONO};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        int64_t now = (10 + i) * SEC;
        CHECK(vr_stage_set_mode(&stage, modes[i], now));
        CHECK_NEAR(vr_stage_fade_alpha(&stage, now + 249000000), 1, 1e-6);
        CHECK_NEAR(vr_stage_fade_alpha(&stage, now + 425000000), 0.5, 1e-5);
        CHECK_NEAR(vr_stage_fade_alpha(&stage, now + 600000000), 0, 1e-6);
    }
}

static void test_settings(void) {
    vr_settings_t settings = {0};
    vr_settings_defaults(&settings);
    vr_render_settings_t render;
    vr_render_settings_from(&render, &settings);
    CHECK_INT(render.cutscenes, 0);
    CHECK_NEAR(render.cinema_distance, 3, 1e-6);
    CHECK_NEAR(render.cinema_width, 3.6, 1e-6);
    CHECK_NEAR(render.diag_cinema_seconds, 0, 1e-6);
    CHECK_NEAR(vr_settings_get(&settings, "diag_dialog_cutscene_seconds"), 0, 0);
    const char *text = "version = 2\n[vr]\ncutscenes = 1\ncinema_distance = 2.5\ncinema_width = 4.2\ndiag_cinema_seconds = 12\n";
    vr_settings_parse(&settings, text, strlen(text));
    char *saved = vr_settings_format(&settings, NULL);
    CHECK(saved != NULL);
    vr_settings_t again = {0};
    vr_settings_defaults(&again);
    CHECK_INT(vr_settings_parse(&again, saved, strlen(saved)), 0);
    for (size_t i = 0; i < vr_settings_count(); i++) {
        again.values[i] = again.file_values[i];
    }
    vr_render_settings_from(&render, &again);
    CHECK_INT(render.cutscenes, 1);
    CHECK_NEAR(render.cinema_distance, 2.5, 1e-6);
    CHECK_NEAR(render.cinema_width, 4.2, 1e-6);
    CHECK_NEAR(render.diag_cinema_seconds, 12, 1e-6);
    vr_settings_set(&again, "cutscenes", 7);
    vr_settings_set(&again, "cinema_distance", -100);
    vr_settings_set(&again, "cinema_width", 100);
    vr_settings_set(&again, "diag_cinema_seconds", 500);
    vr_settings_set(&again, "diag_dialog_cutscene_seconds", 500);
    CHECK_NEAR(vr_settings_get(&again, "cutscenes"), 1, 0);
    CHECK_NEAR(vr_settings_get(&again, "cinema_distance"), 1, 0);
    CHECK_NEAR(vr_settings_get(&again, "cinema_width"), 10, 0);
    CHECK_NEAR(vr_settings_get(&again, "diag_cinema_seconds"), 120, 0);
    CHECK_NEAR(vr_settings_get(&again, "diag_dialog_cutscene_seconds"), 120, 0);
    free(saved);
    vr_settings_free(&settings);
    vr_settings_free(&again);
}

#ifdef VR_CINEMA_RENDER_TEST
// A synthetic retained-list producer implements only the renderer's existing
// pass contract. The routing, targets, camera callback, HUD resolve and layer
// submission below are the real vr_render.c/vr_compose.c, under Mesa GL.
static GLuint render_images[VR_SURFACE_COUNT], triangle_program, triangle_vao;
static struct GfxVrPassStats render_stats;
static bool fail_right_eye;
static float source_projection[16];
static Vp source_viewport = {{{640, 480, 511, 0}, {640, 480, 511, 0}}};

static bool render_acquire(void *user, vr_surface_t surface, vr_target_t *out) {
    (void)user;
    if (surface == VR_SURFACE_EYE1 && fail_right_eye) {
        return false;
    }
    out->image = render_images[surface];
    out->width = surface <= VR_SURFACE_EYE1 ? 512 : 320;
    out->height = surface <= VR_SURFACE_EYE1 ? 512 : 180;
    return true;
}

static void render_release(void *user, vr_surface_t surface) { (void)user; (void)surface; }

// The pause menu's DJUI flag (src/pc/djui/djui_panel_pause.c): no menu in these frames
bool gDjuiPanelPauseCreated = false;

// The runtime's settings, pad and hand poses (vr.c, vr_input.c): defaults, a
// centred pad and no tracked hands, so the comfort step stays idle
const vr_settings_t *vr_settings(void) {
    static vr_settings_t s;
    static bool made;
    if (!made) {
        vr_settings_defaults(&s);
        made = true;
    }
    return &s;
}

const vr_gamepad_t *vr_input_gamepad(void) {
    static vr_gamepad_t pad;
    return &pad;
}

bool vr_input_hand_yaw(int hand, float *yaw) {
    (void)hand;
    (void)yaw;
    return false;
}

void gfx_vr_run_pass(Gfx *dl, const struct GfxVrPass *pass) {
    (void)dl;
    memset(&render_stats, 0, sizeof(render_stats));
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    if (pass->kind == GFX_VR_PASS_EYE) {
        float projection[16];
        float rect[4];
        cinema_fast3d_load(pass, source_projection, &source_viewport, projection, rect, &render_stats);
        CHECK_INT(render_stats.world_proj_loads, 1);
        if (pass->cinema) {
            CHECK_NEAR(rect[0], 0, 0);
            CHECK_NEAR(rect[1], 0, 0);
            CHECK_NEAR(rect[2], pass->width, 0);
            CHECK_NEAR(rect[3], pass->height, 0);
        }
        glViewport((int)rect[0], (int)rect[1], (int)rect[2], (int)rect[3]);
        if (pass->sky_proj_live) {
            CHECK_NEAR(pass->sky_proj_live[0], projection[0], 1e-5);
            CHECK_NEAR(pass->sky_proj_live[5], projection[5], 1e-5);
            CHECK_NEAR(pass->sky_proj_live[8], projection[8], 1e-5);
            CHECK_NEAR(pass->sky_proj_live[9], projection[9], 1e-5);
        }
        glUseProgram(triangle_program);
        glUniformMatrix4fv(glGetUniformLocation(triangle_program, "projection"), 1, GL_FALSE, projection);
        glBindVertexArray(triangle_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glUseProgram(0);
    } else if (pass->kind == GFX_VR_PASS_HUD) {
        // An authored box with margins, over the renderer's black/white clears.
        glEnable(GL_SCISSOR_TEST);
        glScissor(pass->width / 3, pass->height / 4, pass->width / 3, pass->height / 4);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    } else {
        glClearColor(0.1f, 0.2f, 0.4f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    render_stats.tris_drawn = 1;
}

const struct GfxVrPassStats *gfx_vr_pass_stats(void) { return &render_stats; }
void gfx_vr_restore_dimensions(void) {}
// Dumps are disabled in this bounded host check; no game image writer needed.
int stbi_write_png(const char *path, int w, int h, int comp, const void *pixels, int stride) {
    (void)path; (void)w; (void)h; (void)comp; (void)pixels; (void)stride;
    return 0;
}

static GLuint render_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    CHECK_INT(ok, GL_TRUE);
    return shader;
}

static void render_init(const vr_render_settings_t *settings) {
    vr_provider_t provider = {.acquire = render_acquire, .release = render_release};
    vr_render_init(settings, &provider, NULL);
}

static vr_layers_t render_at(int64_t now) {
    vr_frame_t frame = {.predicted_display_time = now, .views_valid = true, .should_render = true};
    memcpy(frame.views, eyes, sizeof(eyes));
    Gfx dl = {0};
    vr_layers_t layers;
    vr_render_frame(&dl, &frame, true, &layers);
    CHECK_INT(glGetError(), GL_NO_ERROR);
    return layers;
}

static void check_black_eyes(void) {
    unsigned char *pixels = malloc(512 * 512 * 4);
    CHECK(pixels != NULL);
    for (int e = 0; e < 2; e++) {
        glBindTexture(GL_TEXTURE_2D, render_images[e]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned nonblack = 0;
        for (int i = 0; i < 512 * 512; i++) {
            nonblack += pixels[4 * i] + pixels[4 * i + 1] + pixels[4 * i + 2];
        }
        CHECK_INT(nonblack, 0);
    }
    free(pixels);
}

static void check_square_picture(const vr_layers_t *layers) {
    unsigned char *pixels = malloc(512 * 512 * 4);
    CHECK(pixels != NULL);
    for (int e = 0; e < 2; e++) {
        glBindTexture(GL_TEXTURE_2D, render_images[e]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        int x0 = 512, y0 = 512, x1 = -1, y1 = -1;
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < 512; x++) {
                if (pixels[4 * (512 * y + x)] > 100) {
                    if (x < x0) { x0 = x; }
                    if (x > x1) { x1 = x; }
                    if (y < y0) { y0 = y; }
                    if (y > y1) { y1 = y; }
                }
            }
        }
        CHECK(x1 > x0 && y1 > y0);
        const float *fov = layers->views[e].fov;
        float picture_aspect = (tanf(fov[1]) - tanf(fov[0])) / (tanf(fov[2]) - tanf(fov[3]));
        CHECK_NEAR(picture_aspect, 16.f / 9.f, 1e-5);
        float visible_aspect = (float)(x1 - x0 + 1) / (y1 - y0 + 1) * picture_aspect;
        CHECK_NEAR(visible_aspect, 1, 0.025); // pixel rounding at the widest FOV
    }
    free(pixels);
}

static void check_dialog_projection_variants(void) {
    // Signs, NPCs, Bob-omb/Whomp boss dialogue and Bowser introductions all
    // share the production cinema branch; modes use camera.h's actual ids.
    const unsigned ids[] = {VR_CS_READ_MESSAGE, VR_CS_DIALOG, VR_CS_DIALOG, VR_CS_DIALOG,
        VR_CS_DIALOG, VR_CS_ENTER_BOWSER_ARENA, VR_CS_INTRO_PEACH};
    const unsigned modes[] = {1, 1, 4, 8, 11, 11, 1};
    const float aspects[] = {1, 4.f / 3.f, 16.f / 9.f, 21.f / 9.f};
    const float delta[] = {0, 0.5f, 1};
    const Vp viewports[] = {
        {{{640,480,511,0}, {640,480,511,0}}},
        {{{640,320,511,0}, {640,480,511,0}}}, // letterbox viewport
        {{{320,400,511,0}, {560,520,511,0}}}  // shifted/narrow viewport
    };
    unsigned frames = 0;
    // Confirm this executable links the game's colliding symbol. A libc-only
    // fixture was the reason the original screen-corner tests passed.
    float (*volatile game_atan2f)(float, float) = atan2f;
    CHECK(fabsf(game_atan2f(1, 2) - (float)atan2(1, 2)) > 0.5f);
    for (unsigned c = 0; c < sizeof(ids) / sizeof(ids[0]); c++) {
        gVrTick.cutscene = ids[c];
        gVrTick.camera_mode = modes[c];
        gVrTick.camera_fov = 15; // deliberately unlike the retained/interpolated matrix
        for (unsigned a = 0; a < sizeof(aspects) / sizeof(aspects[0]); a++) {
            for (unsigned d = 0; d < sizeof(delta) / sizeof(delta[0]); d++) {
                bool valid = cinema_game_projection(29, 80, aspects[a], delta[d], source_projection);
                CHECK(valid); // real patch_mtx_interpolated must retain the MATRIX address
                if (!valid) { continue; }
                for (unsigned v = 0; v < sizeof(viewports) / sizeof(viewports[0]); v++) {
                    source_viewport = viewports[v];
                    vr_layers_t layers = render_at(SEC + 700000000 + frames * 1000000);
                    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
                    CHECK_INT(vr_render_info()->world_proj_loads[0], 1);
                    CHECK_INT(vr_render_info()->world_proj_loads[1], 1);
                    check_square_picture(&layers);
                    frames++;
                }
            }
        }
    }
    source_viewport = viewports[0];
    printf("cinema production path: %u stereo frames, 7 cutscene/mode pairs, 4 aspects, 3 interpolated FOVs, 3 viewports\n", frames);
}

static void test_renderer_routing_and_images(void) {
    CHECK_INT(SDL_Init(SDL_INIT_VIDEO), 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("cinema host check", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    if (!window) { SDL_Quit(); return; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    CHECK(context != NULL);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return; }
    GLuint game_vao, game_vbo;
    glGenVertexArrays(1, &game_vao);
    glGenBuffers(1, &game_vbo);
    glBindVertexArray(game_vao);
    glBindBuffer(GL_ARRAY_BUFFER, game_vbo);

    const char *vs = "#version 330 core\nuniform mat4 projection;\nvoid main(){\n"
        "vec3 v[6]=vec3[6](vec3(-150,-150,-1000),vec3(150,-150,-1000),vec3(150,150,-1000),"
        "vec3(-150,-150,-1000),vec3(150,150,-1000),vec3(-150,150,-1000));\n"
        "gl_Position=projection*vec4(v[gl_VertexID],1);}\n";
    const char *fs = "#version 330 core\nout vec4 c;\nvoid main(){c=vec4(0.9,0.2,0.1,1);}\n";
    GLuint vertex = render_shader(GL_VERTEX_SHADER, vs), fragment = render_shader(GL_FRAGMENT_SHADER, fs);
    triangle_program = glCreateProgram();
    glAttachShader(triangle_program, vertex);
    glAttachShader(triangle_program, fragment);
    glLinkProgram(triangle_program);
    GLint linked;
    glGetProgramiv(triangle_program, GL_LINK_STATUS, &linked);
    CHECK_INT(linked, GL_TRUE);
    glGenVertexArrays(1, &triangle_vao);
    glGenTextures(VR_SURFACE_COUNT, render_images);
    for (int s = 0; s < VR_SURFACE_COUNT; s++) {
        int w = s <= VR_SURFACE_EYE1 ? 512 : 320, h = s <= VR_SURFACE_EYE1 ? 512 : 180;
        glBindTexture(GL_TEXTURE_2D, render_images[s]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    vr_projection_tan(-0.7363797f, 0.7363797f, 0.4142136f, -0.4142136f, 100, 20000, source_projection);
    float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    memcpy(gVrInterpCamView, identity, sizeof(identity));
    gVrInterpCamValid = true;
    memset(&gVrTick, 0, sizeof(gVrTick));
    gVrTick.rendered_area = gVrTick.gameplay_camera = true;
    gVrTick.cutscene = VR_CS_INTRO_PEACH;
    gVrTick.camera_fov = 45;
    tracked_eyes(0.7f);
    vr_settings_t settings = {0};
    vr_settings_defaults(&settings);
    vr_render_settings_t render;
    vr_render_settings_from(&render, &settings);
    render.hud_visible = false;
    render_init(&render);
    render_at(SEC); // entry qualifies under black
    vr_layers_t layers = render_at(SEC + 60000000);
    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
    CHECK(layers.projection && layers.hud && !layers.panel);
    vr_pose_t head;
    vr_head_pose(eyes, &head);
    vr_cinema_screen_t screen;
    vr_cinema_place(&head, 3, 3.6f, &screen);
    vr_view_t expected[2];
    CHECK(vr_cinema_views(&screen, eyes, expected));
    CHECK(memcmp(layers.views, expected, sizeof(expected)) == 0);
    CHECK_NEAR(layers.hud_width, 3.6 * 0.55, 1e-5);
    CHECK(layers.hud_pose.position[1] + layers.hud_height / 2 < screen.pose.position[1] - screen.height / 2);

    // Readbacks occur only outside the production frame, in this host test.
    unsigned char *left = malloc(512 * 512 * 4), *right = malloc(512 * 512 * 4);
    CHECK(left && right);
    glBindTexture(GL_TEXTURE_2D, render_images[0]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, left);
    glBindTexture(GL_TEXTURE_2D, render_images[1]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, right);
    CHECK(memcmp(left, right, 512 * 512 * 4) != 0); // two real GL eye images
    free(left); free(right);
    unsigned char band[320 * 180 * 4];
    glBindTexture(GL_TEXTURE_2D, render_images[VR_SURFACE_HUD]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, band);
    CHECK_INT(band[3], 255); // black margins are opaque, not transparent overlays
    CHECK_INT(band[0], 0);
    CHECK_INT(band[4 * (320 * 60 + 150)], 255); // authored white box survives the resolve
    check_square_picture(&layers);
    check_dialog_projection_variants();

    // A closed warp keeps cinema but clears both projection images to black.
    gVrTick.rendered_area = false;
    gVrTick.transition_hold = true;
    gVrTick.transition_alpha = 1;
    layers = render_at(2 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
    CHECK(layers.projection && !layers.panel && !layers.hud);
    CHECK_NEAR(layers.fade_alpha, 1, 0);
    check_black_eyes();
    gVrTick.rendered_area = true;
    gVrTick.transition_hold = false;
    gVrTick.transition_alpha = 0;
    layers = render_at(3 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
    CHECK_NEAR(layers.fade_alpha, 0, 0); // hold release/camera cut has no new fade
    gVrTick.cutscene = 0;
    render_at(4 * SEC);
    layers = render_at(4 * SEC + SEC / 30);
    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
    gVrTick.cutscene = VR_CS_CREDITS;
    // The interpolated projection differs from the tick's 45-degree fact.
    vr_projection_tan(-1.0264005f, 1.0264005f, 0.5773503f, -0.5773503f, 100, 20000, source_projection);
    layers = render_at(4 * SEC + 2 * SEC / 30);
    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
    CHECK_NEAR(layers.fade_alpha, 0, 0);

    // Act select and mono menus take their real production routing branches.
    gVrTick.star_select = true;
    gVrTick.gameplay_camera = false;
    layers = render_at(5 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_STEREO);
    CHECK(layers.projection && !layers.panel);
    // Act select wins even if a mod/stale tick also marks a gameplay camera
    // while the previous scripted-camera id is still present.
    gVrTick.gameplay_camera = true;
    layers = render_at(5 * SEC + 700000000);
    CHECK_INT(vr_render_info()->mode, VR_MODE_STEREO);
    CHECK(layers.projection && !layers.panel);
    CHECK_NEAR(layers.fade_alpha, 0, 0);
    gVrTick.star_select = false;
    gVrTick.gameplay_camera = true;
    gVrTick.in_main_menu = true;
    layers = render_at(6 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_MONO);
    CHECK(layers.projection && layers.panel);
    check_black_eyes();
    fail_right_eye = true;
    layers = render_at(7 * SEC);
    CHECK(!layers.projection && !layers.panel);
    fail_right_eye = false;
    vr_render_shutdown();

    // The setting really suppresses cinema, even on the Peach intro.
    gVrTick.in_main_menu = false;
    gVrTick.cutscene = VR_CS_INTRO_PEACH;
    render.cutscenes = 1;
    render_init(&render);
    render_at(10 * SEC);
    layers = render_at(11 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_STEREO);
    CHECK(memcmp(layers.views, eyes, sizeof(eyes)) == 0);
    vr_render_shutdown();
    // Force cinema on the existing menu world without game input, then expire.
    gVrTick.in_main_menu = true;
    render.cutscenes = 0;
    render.diag_cinema_seconds = 1;
    render_init(&render);
    render_at(12 * SEC);
    layers = render_at(12 * SEC + 60000000);
    CHECK_INT(vr_render_info()->mode, VR_MODE_CINEMA);
    CHECK(!vr_render_adjustment_allowed()); // authored cinema never accepts the modifier
    CHECK(layers.projection && !layers.panel);
    layers = render_at(14 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_MONO);
    CHECK(layers.projection && layers.panel);
    vr_render_shutdown();

    // The VR settings page over the gameplay view (section 10): SM64's pause overview
    // cut fades both ways, the page's menu turns aside (right by default) under that
    // fade and eases home, and hud_visible off leaves the game's HUD out behind it.
    gVrTick.in_main_menu = false;
    gVrTick.cutscene = 0;
    render.diag_cinema_seconds = 0;
    render_init(&render);
    render_at(20 * SEC);
    layers = render_at(21 * SEC);
    CHECK_INT(vr_render_info()->mode, VR_MODE_STEREO);
    CHECK(vr_render_adjustment_allowed()); // live gameplay
    CHECK_NEAR(layers.fade_alpha, 0, 0);
    float head_yaw = vr_yaw_of(head.orientation);
    gDjuiPanelPauseCreated = true;
    gVrTick.pause_overview = true;
    layers = render_at(22 * SEC);
    CHECK(!vr_render_adjustment_allowed()); // ordinary pause overview
    CHECK_NEAR(layers.fade_alpha, 1, 0); // into the overview
    CHECK(layers.hud);
    CHECK_NEAR(vr_yaw_of(layers.hud_pose.orientation), head_yaw, 1e-4);
    CHECK(!gVrSettingsPage.hide_hud);
    layers = render_at(23 * SEC);
    CHECK_NEAR(layers.fade_alpha, 0, 0);
    gVrSettingsPage.open = true;
    layers = render_at(24 * SEC); // the page is up; the camera comes back on the next tick
    CHECK_NEAR(layers.fade_alpha, 0, 0);
    gVrTick.pause_overview = false;
    layers = render_at(24 * SEC + SEC / 30);
    CHECK(vr_render_adjustment_allowed()); // settings page preview over gameplay
    CHECK_NEAR(layers.fade_alpha, 1, 0); // back to the play camera, faded
    float target = vr_preview_menu_target(VR_PREVIEW_SIDE_RIGHT, vr_preview_page_half(layers.hud_width, render.hud_distance));
    CHECK(target < -15.0f * (float)M_PI / 180.0f);
    CHECK_NEAR(vr_yaw_of(layers.hud_pose.orientation), head_yaw + target, 1e-3);
    CHECK(gVrSettingsPage.hide_hud); // hud_visible is off in these settings
    CHECK(layers.hud); // the page itself stays
    gVrSettingsPage.open = false; // Back to the pause menu: home again at a panel's pace
    layers = render_at(25 * SEC);
    CHECK(!vr_render_adjustment_allowed()); // ordinary pause menu remains gated
    float yaw = vr_yaw_of(layers.hud_pose.orientation);
    CHECK(yaw > head_yaw + target + 1e-3f && yaw < head_yaw - 1e-3f);
    for (int i = 1; i <= 4; i++) {
        layers = render_at(25 * SEC + i * SEC / 10);
    }
    CHECK_NEAR(vr_yaw_of(layers.hud_pose.orientation), head_yaw, 1e-4);
    CHECK(!gVrSettingsPage.hide_hud);
    gDjuiPanelPauseCreated = false;
    vr_render_shutdown();
    CHECK_INT(glGetError(), GL_NO_ERROR);
    glDeleteTextures(VR_SURFACE_COUNT, render_images);
    glDeleteProgram(triangle_program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    glDeleteVertexArrays(1, &triangle_vao);
    glDeleteVertexArrays(1, &game_vao);
    glDeleteBuffers(1, &game_vbo);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
#endif

int main(void) {
    RUN(test_classification);
    RUN(test_latch);
    RUN(test_screen_corners);
    RUN(test_positive_parallax);
    RUN(test_picture_aspect);
    RUN(test_cinema_frame_and_transitions);
    RUN(test_settings);
#ifdef VR_CINEMA_RENDER_TEST
    RUN(test_renderer_routing_and_images);
#endif
    return SFVR_TEST_RESULT("vr_cinema");
}
