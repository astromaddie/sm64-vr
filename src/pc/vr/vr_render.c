#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

// The implementation is DynOS's (data/dynos_bin_tex.cpp)
#include "stb/stb_image_write.h"

#include "vr_render.h"
#include "vr.h"
#include "vr_cam.h"
#include "vr_game.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_math.h"
#include "vr_turn.h"
#include "pc/gfx/gfx_vr.h"

// The sky sphere's clip planes: its vertices sit 10000 units out (skybox.c)
#define VR_SKY_NEAR 100.0f
#define VR_SKY_FAR 30000.0f
// When a world projection is not a perspective the game made (never expected)
#define VR_FALLBACK_NEAR 5.0f
#define VR_FALLBACK_FAR 20000.0f

#define VR_GPU_RING 6
#define VR_CHECKED_MAX 32
#define VR_DUMP_SLOTS 4

typedef struct {
    GLuint pbo;
    GLsync fence;
    bool pending;
    char path[600];
    uint32_t width, height;
    int comp;
} vr_dump_slot_t;

// What the world projection callback needs for one eye
typedef struct {
    vr_game_cam_t cam;
    vr_eye_cam_t eye;
    float fov[4];
} vr_eye_ctx_t;

static struct {
    bool ready;
    vr_render_settings_t st;
    vr_provider_t provider;
    char dump_dir[600];
    vr_stage_t stage;

    // GL
    bool gl_ready;
    GLuint fbo;
    GLuint depth_rb[2]; // 0: eye size, 1: ui size
    uint32_t depth_w[2], depth_h[2];
    GLuint hud_tex[2]; // over black, over white
    uint32_t hud_w, hud_h;
    GLuint resolve_program, resolve_vao;
    GLint game_vao, game_vbo;
    GLuint checked[VR_CHECKED_MAX];
    int checked_count;
    vr_dump_slot_t dumps[VR_DUMP_SLOTS];

    // This frame
    int64_t now_ns;
    vr_view_t views[2];
    bool views_ok;
    vr_pose_t head;
    vr_eye_ctx_t eye_ctx[2];
    vr_game_cam_t cam;
    vr_pose_t hud_pose;
    float hud_width, hud_height;
    bool dump_this_frame;
    bool holding; // held behind a closed warp transition (full fade, no eyes)
    uint32_t frames;
    uint32_t worn_stereo_frames; // stereo frames from tracked views
    int last_synthetic; // -1 before the first frame

    vr_render_timing_t timing;
    vr_render_info_t info;

    // Comfort (docs/VR_ARCHITECTURE.md section 4): the keys, read live every frame
    vr_comfort_t comfort;
    vr_turn_t turn;
    vr_game_cam_t shown_cam; // the game camera as shown: turned by the queued turn, closer, lower
    vr_heading_t game_heading, shown_heading; // unwrapped, radians
    float shown_prev, game_prev; // last frame's headings (unwrapped)
    bool shown_prev_valid;
    float shown_rate, game_rate; // deg/s, jumps excluded
    float vignette; // 0..vignette_strength
    int64_t last_ns;
    unsigned int turn_updates_seen; // gVrCamTurn.updates at the last change
    double turn_updates_s; // display time of that change
    unsigned int turn_refused_seen;
    double cam_log_s, diag_log_s, limit_log_s;
    int snap_trace; // display frames left of a diag_turn_stick snap trace
    bool play; // stereo gameplay through the game camera: turning, closer/lower and the stick frame apply
    GLuint vignette_program;
    GLint vignette_tan_loc, vignette_amount_loc, vignette_size_loc;

    // GPU time of the eye and HUD passes (timing on): timestamp pairs in a ring,
    // read back a few frames later so nothing waits (lesson 10, lesson 53)
    GLuint gpu_query[VR_GPU_RING][2];
    bool gpu_pending[VR_GPU_RING];
    int gpu_slot;
    float gpu_last_ms; // the newest finished result; -1 before the first
} vrr;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

// Writes the start timestamp of this frame's passes, and picks up the oldest finished pair
static void gpu_begin(void) {
    if (!vrr.st.timing) {
        return;
    }
    if (!vrr.gpu_query[0][0]) {
        for (int i = 0; i < VR_GPU_RING; i++) {
            glGenQueries(2, vrr.gpu_query[i]);
        }
        vrr.gpu_last_ms = -1.0f;
    }
    // The slot about to be reused: its pair is VR_GPU_RING frames old
    int slot = vrr.gpu_slot;
    if (vrr.gpu_pending[slot]) {
        GLint ready = 0;
        glGetQueryObjectiv(vrr.gpu_query[slot][1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 a = 0, b = 0;
            glGetQueryObjectui64v(vrr.gpu_query[slot][0], GL_QUERY_RESULT, &a);
            glGetQueryObjectui64v(vrr.gpu_query[slot][1], GL_QUERY_RESULT, &b);
            vrr.gpu_last_ms = (float)((double)(b - a) * 1e-6);
        }
        vrr.gpu_pending[slot] = false;
    }
    glQueryCounter(vrr.gpu_query[slot][0], GL_TIMESTAMP);
    vrr.timing.gpu_ms = vrr.gpu_last_ms;
}

static void gpu_end(void) {
    if (!vrr.st.timing || !vrr.gpu_query[0][0]) {
        return;
    }
    glQueryCounter(vrr.gpu_query[vrr.gpu_slot][1], GL_TIMESTAMP);
    vrr.gpu_pending[vrr.gpu_slot] = true;
    vrr.gpu_slot = (vrr.gpu_slot + 1) % VR_GPU_RING;
}

const vr_render_timing_t *vr_render_timing(void) {
    return &vrr.timing;
}

const vr_render_info_t *vr_render_info(void) {
    return &vrr.info;
}

void vr_render_request_recentre(void) {
    vr_stage_request_recentre(&vrr.stage);
}

void vr_render_request_height(void) {
    vr_stage_request_height(&vrr.stage);
}


// -----------------------------------------------------------------------------
// GL objects

static const char *const RESOLVE_VS =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main(void) {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    v_uv = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// The HUD over black is premultiplied colour; over white it is that plus what
// the HUD left uncovered. Their difference is the coverage (lesson 6: the
// game's own framebuffer alpha is never read).
static const char *const RESOLVE_FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D over_black;\n"
    "uniform sampler2D over_white;\n"
    "out vec4 frag_color;\n"
    "void main(void) {\n"
    "    vec3 k = texture(over_black, v_uv).rgb;\n"
    "    vec3 w = texture(over_white, v_uv).rgb;\n"
    "    float a = clamp(1.0 - dot(w - k, vec3(1.0 / 3.0)), 0.0, 1.0);\n"
    "    vec3 c = a > 0.002 ? clamp(k / a, 0.0, 1.0) : vec3(0.0);\n"
    "    frag_color = vec4(c, a);\n"
    "}\n";

// The comfort vignette (vignette, vignette_strength): black outside a cone
// about the eye's own axis, so each eye centres on itself. `tans` are the
// eye's fov tangents (left, right, up, down); the clear cone's half-angle goes
// from 60 degrees at amount 0 to 22 at amount 1, with a 12 degree edge.
static const char *const VIGNETTE_FS =
    "#version 330 core\n"
    "uniform vec4 tans;\n"
    "uniform vec2 size;\n"
    "uniform float amount;\n"
    "out vec4 frag_color;\n"
    "void main(void) {\n"
    "    vec2 uv = gl_FragCoord.xy / size;\n"
    "    vec2 t = vec2(mix(tans.x, tans.y, uv.x), mix(tans.w, tans.z, uv.y));\n"
    "    float angle = atan(length(t));\n"
    "    float clear = radians(mix(60.0, 22.0, amount));\n"
    "    frag_color = vec4(0.0, 0.0, 0.0, smoothstep(clear, clear + radians(12.0), angle));\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        vr_logf("render: shader compile failed: %s", log);
    }
    return s;
}

static void gl_make(void) {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vrr.game_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vrr.game_vbo);

    glGenFramebuffers(1, &vrr.fbo);
    glGenRenderbuffers(2, vrr.depth_rb);
    glGenTextures(2, vrr.hud_tex);

    GLuint vs = compile(GL_VERTEX_SHADER, RESOLVE_VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, RESOLVE_FS);
    vrr.resolve_program = glCreateProgram();
    glAttachShader(vrr.resolve_program, vs);
    glAttachShader(vrr.resolve_program, fs);
    glLinkProgram(vrr.resolve_program);
    GLint ok = 0;
    glGetProgramiv(vrr.resolve_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        vr_logf("render: the HUD resolve program did not link");
    }
    glUseProgram(vrr.resolve_program);
    glUniform1i(glGetUniformLocation(vrr.resolve_program, "over_black"), 2);
    glUniform1i(glGetUniformLocation(vrr.resolve_program, "over_white"), 3);
    glUseProgram(0);
    glGenVertexArrays(1, &vrr.resolve_vao);

    GLuint vvs = compile(GL_VERTEX_SHADER, RESOLVE_VS);
    GLuint vfs = compile(GL_FRAGMENT_SHADER, VIGNETTE_FS);
    vrr.vignette_program = glCreateProgram();
    glAttachShader(vrr.vignette_program, vvs);
    glAttachShader(vrr.vignette_program, vfs);
    glLinkProgram(vrr.vignette_program);
    ok = 0;
    glGetProgramiv(vrr.vignette_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        vr_logf("render: the vignette program did not link");
        vrr.vignette_program = 0;
    } else {
        vrr.vignette_tan_loc = glGetUniformLocation(vrr.vignette_program, "tans");
        vrr.vignette_size_loc = glGetUniformLocation(vrr.vignette_program, "size");
        vrr.vignette_amount_loc = glGetUniformLocation(vrr.vignette_program, "amount");
    }
    glBindVertexArray((GLuint)vrr.game_vao);
    vrr.gl_ready = true;
    vr_logf("render: GL %s, game VAO %d VBO %d", (const char *)glGetString(GL_VERSION), vrr.game_vao, vrr.game_vbo);
}

// Fast3D's GL state that this file changes: put it back
static void gl_restore(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray((GLuint)vrr.game_vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)vrr.game_vbo);
    glActiveTexture(GL_TEXTURE0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_SCISSOR_TEST);
}

static void ensure_depth(int slot, uint32_t w, uint32_t h) {
    if (vrr.depth_w[slot] == w && vrr.depth_h[slot] == h) {
        return;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, vrr.depth_rb[slot]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)w, (GLsizei)h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    vrr.depth_w[slot] = w;
    vrr.depth_h[slot] = h;
}

static void ensure_hud_textures(uint32_t w, uint32_t h) {
    if (vrr.hud_w == w && vrr.hud_h == h) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, vrr.hud_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    vrr.hud_w = w;
    vrr.hud_h = h;
}

// Binds `image` (and a depth buffer, or none) as the target and clears it.
// Colour is copied as it is: the game's colours are gamma-encoded already and
// the swapchains are sRGB, so framebuffer sRGB conversion stays off (lesson 8).
static void bind_target(GLuint image, uint32_t w, uint32_t h, GLuint depth, const float clear[4]) {
    glBindFramebuffer(GL_FRAMEBUFFER, vrr.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    bool known = false;
    for (int i = 0; i < vrr.checked_count; i++) {
        if (vrr.checked[i] == image) {
            known = true;
            break;
        }
    }
    if (!known) {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            vr_logf("WARNING: framebuffer for image %u is incomplete (0x%x)", image, status);
        }
        if (vrr.checked_count < VR_CHECKED_MAX) {
            vrr.checked[vrr.checked_count++] = image;
        }
    }
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    glDepthMask(GL_TRUE);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0));
    glEnable(GL_SCISSOR_TEST);
}


// -----------------------------------------------------------------------------
// Dumps: copied into a PBO behind a fence, written by a later frame; nothing
// waits in a frame (no synchronous readback, lesson 10)

static void mkdirs(const char *path) {
    char buf[600];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

static void dump_write(vr_dump_slot_t *s) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
    const uint8_t *pixels = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (pixels) {
        uint8_t *out = malloc((size_t)s->width * s->height * s->comp);
        if (out) {
            for (uint32_t y = 0; y < s->height; y++) {
                const uint8_t *src = pixels + (size_t)(s->height - 1 - y) * s->width * 4; // GL is bottom-up
                uint8_t *dst = out + (size_t)y * s->width * s->comp;
                for (uint32_t x = 0; x < s->width; x++) {
                    for (int c = 0; c < s->comp; c++) {
                        dst[x * s->comp + c] = src[x * 4 + c];
                    }
                }
            }
            if (stbi_write_png(s->path, (int)s->width, (int)s->height, s->comp, out, (int)s->width * s->comp)) {
                vr_logf("dump: wrote %s (%ux%u)", s->path, s->width, s->height);
            } else {
                vr_logf("dump: could not write %s", s->path);
            }
            free(out);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glDeleteSync(s->fence);
    s->fence = 0;
    s->pending = false;
}

static void dump_poll(bool block) {
    for (int i = 0; i < VR_DUMP_SLOTS; i++) {
        vr_dump_slot_t *s = &vrr.dumps[i];
        if (!s->pending) {
            continue;
        }
        GLenum r = glClientWaitSync(s->fence, block ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, block ? 1000000000ull : 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED || block) {
            dump_write(s);
        }
    }
}

static void dump_queue(int slot, const char *name, GLuint image, uint32_t w, uint32_t h, int comp) {
    vr_dump_slot_t *s = &vrr.dumps[slot];
    if (s->pending) {
        dump_write(s);
    }
    if (!s->pbo) {
        glGenBuffers(1, &s->pbo);
    }
    snprintf(s->path, sizeof(s->path), "%s/%s", vrr.dump_dir, name);
    s->width = w;
    s->height = h;
    s->comp = comp;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, vrr.fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, NULL, GL_STREAM_READ);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    s->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    s->pending = true;
}

static void dump_state_log(const vr_layers_t *layers) {
    char path[640];
    snprintf(path, sizeof(path), "%s/vr-state.txt", vrr.dump_dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        vr_logf("dump: could not write %s", path);
        return;
    }
    const vr_render_settings_t *st = &vrr.st;
    fprintf(f, "frame %u (dump_frame %d, dump_worn_frame %d, tracked stereo frames %u)\n", vrr.frames, st->dump_frame,
        st->dump_worn_frame, vrr.worn_stereo_frames);
    fprintf(f, "mode %s%s%s\n", vrr.stage.mode == VR_MODE_STEREO ? "stereo" : "mono",
        vrr.stage.mode == VR_MODE_STEREO ? "" : " reason=", vrr.stage.mode == VR_MODE_STEREO ? "" : vrr.info.mono_reason);
    fprintf(f, "predicted_display_time_ns %lld\n", (long long)vrr.now_ns);
    fprintf(f, "settings world_scale=%.3f head_translation=%.3f level_pitch=%d hud_distance=%.3f hud_scale=%.1f force_mono=%d diag_menu_stereo=%d diag_yaw_deg=%.2f\n",
        st->rig.world_scale, st->rig.head_translation, st->rig.level_pitch, st->hud_distance, st->hud_scale,
        st->force_mono, st->diag_menu_stereo, st->diag_yaw * 180.0 / M_PI);
    fprintf(f, "comfort turn_mode=%d snap=%.0f smooth=%.0f queued_deg=%.2f shown_deg=%.2f game_deg=%.2f play=%d "
        "closer=%.2f lower=%.2f height_offset=%.2f seated=%d vignette=%.2f camera_shake=%d move_relative=%d move_yaw_deg=%.2f\n",
        (int)vrr.comfort.turn.mode, vrr.comfort.turn.snap_angle_deg, vrr.comfort.turn.smooth_speed_dps,
        gVrCamTurn.queued * 180.0 / M_PI, vrr.shown_heading.unwrapped * 180.0 / M_PI, vrr.game_heading.unwrapped * 180.0 / M_PI,
        vrr.play, vrr.comfort.camera_closer, vrr.comfort.camera_lower, vrr.comfort.height_offset, vrr.comfort.seated,
        vrr.vignette, vrr.comfort.camera_shake, vrr.comfort.move_relative, gVrCamTurn.move_yaw * 180.0 / M_PI);
    fprintf(f, "tick rendered_area=%d gameplay_camera=%d in_main_menu=%d transition=%d interp_cam=%d\n",
        gVrTick.rendered_area, gVrTick.gameplay_camera, gVrTick.in_main_menu, gVrTick.transition, gVrInterpCamValid);
    fprintf(f, "recentre yaw=%.5f origin=%.4f,%.4f,%.4f applied=%u switches=%u\n",
        vrr.stage.recentre.yaw, vrr.stage.recentre.origin[0], vrr.stage.recentre.origin[1], vrr.stage.recentre.origin[2],
        vrr.stage.recentres_applied, vrr.stage.mode_switches);
    fprintf(f, "head pos=%.4f,%.4f,%.4f quat=%.5f,%.5f,%.5f,%.5f yaw=%.5f\n",
        vrr.head.position[0], vrr.head.position[1], vrr.head.position[2],
        vrr.head.orientation[0], vrr.head.orientation[1], vrr.head.orientation[2], vrr.head.orientation[3],
        vr_yaw_of(vrr.head.orientation));
    for (int e = 0; e < 2; e++) {
        const vr_view_t *v = &vrr.views[e];
        fprintf(f, "eye%d pose pos=%.4f,%.4f,%.4f quat=%.5f,%.5f,%.5f,%.5f fov=%.4f,%.4f,%.4f,%.4f\n", e,
            v->pose.position[0], v->pose.position[1], v->pose.position[2],
            v->pose.orientation[0], v->pose.orientation[1], v->pose.orientation[2], v->pose.orientation[3],
            v->fov[0], v->fov[1], v->fov[2], v->fov[3]);
    }
    if (vrr.stage.mode == VR_MODE_STEREO) {
        fprintf(f, "game camera pos=%.2f,%.2f,%.2f back=%.4f,%.4f,%.4f\n",
            vrr.cam.position[0], vrr.cam.position[1], vrr.cam.position[2], vrr.cam.back[0], vrr.cam.back[1], vrr.cam.back[2]);
        for (int e = 0; e < 2; e++) {
            const vr_eye_cam_t *ec = &vrr.eye_ctx[e].eye;
            fprintf(f, "eye%d game pos=%.3f,%.3f,%.3f forward=%.4f,%.4f,%.4f tris=%u world_proj_loads=%u\n", e,
                ec->position[0], ec->position[1], ec->position[2], -ec->rotation[6], -ec->rotation[7], -ec->rotation[8],
                vrr.info.tris_eye[e], vrr.info.world_proj_loads[e]);
        }
        float d[3];
        for (int i = 0; i < 3; i++) {
            d[i] = vrr.eye_ctx[1].eye.position[i] - vrr.eye_ctx[0].eye.position[i];
        }
        fprintf(f, "eye separation %.3f game units (%.4f m)\n", sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]),
            sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / st->rig.world_scale);
        fprintf(f, "hud pos=%.4f,%.4f,%.4f quat=%.5f,%.5f,%.5f,%.5f size=%.4fx%.4f m tris=%u visible=%d\n",
            layers->hud_pose.position[0], layers->hud_pose.position[1], layers->hud_pose.position[2],
            layers->hud_pose.orientation[0], layers->hud_pose.orientation[1], layers->hud_pose.orientation[2],
            layers->hud_pose.orientation[3], layers->hud_width, layers->hud_height, vrr.info.tris_hud, layers->hud);
    } else {
        fprintf(f, "panel pos=%.4f,%.4f,%.4f width=%.3f m\n", layers->panel_pose.position[0],
            layers->panel_pose.position[1], layers->panel_pose.position[2], layers->panel_width);
    }
    fprintf(f, "fade_alpha %.3f\n", layers->fade_alpha);
    fprintf(f, "timing eye_ms=%.3f/%.3f hud_ms=%.3f panel_ms=%.3f\n",
        vrr.timing.eye_ms[0], vrr.timing.eye_ms[1], vrr.timing.hud_ms, vrr.timing.panel_ms);
    fclose(f);
    vr_logf("dump: wrote %s", path);
}


// -----------------------------------------------------------------------------
// Passes

static void world_proj(const float game_proj[16], float out[16], void *user) {
    const vr_eye_ctx_t *c = user;
    float n, f;
    if (!vr_game_proj_near_far(game_proj, &n, &f)) {
        n = VR_FALLBACK_NEAR;
        f = VR_FALLBACK_FAR;
    }
    vr_eye_world_proj(&c->cam, &c->eye, c->fov, n, f, out);
}

// Over the eye image just drawn (bound), only while the vignette shows
static void draw_vignette(const float fov[4], uint32_t w, uint32_t h, float amount) {
    if (!vrr.vignette_program) {
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    glUseProgram(vrr.vignette_program);
    glUniform4f(vrr.vignette_tan_loc, tanf(fov[0]), tanf(fov[1]), tanf(fov[2]), tanf(fov[3]));
    glUniform2f(vrr.vignette_size_loc, (float)w, (float)h);
    glUniform1f(vrr.vignette_amount_loc, amount);
    glBindVertexArray(vrr.resolve_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(0);
    gl_restore();
    glDepthMask(GL_TRUE);
}

static bool replay_eyes(Gfx *dl, vr_layers_t *layers) {
    bool ok = true;
    for (int e = 0; e < 2; e++) {
        double t0 = now_ms();
        vr_target_t t;
        if (!vrr.provider.acquire(vrr.provider.user, (vr_surface_t)(VR_SURFACE_EYE0 + e), &t)) {
            ok = false;
            continue;
        }
        ensure_depth(0, t.width, t.height);
        static const float black[4] = { 0, 0, 0, 1 };
        bind_target(t.image, t.width, t.height, vrr.depth_rb[0], black);

        struct GfxVrPass pass;
        memset(&pass, 0, sizeof(pass));
        pass.kind = GFX_VR_PASS_EYE;
        pass.width = t.width;
        pass.height = t.height;
        pass.world_proj = world_proj;
        pass.user = &vrr.eye_ctx[e];
        vr_eye_sky_proj(&vrr.eye_ctx[e].eye, vrr.eye_ctx[e].fov, VR_SKY_NEAR, VR_SKY_FAR, pass.sky_proj);
        gfx_vr_run_pass(dl, &pass);
        vrr.info.tris_eye[e] = gfx_vr_pass_stats()->tris_drawn;
        vrr.info.world_proj_loads[e] = gfx_vr_pass_stats()->world_proj_loads;
        if (vrr.vignette > 0.001f) {
            draw_vignette(vrr.eye_ctx[e].fov, t.width, t.height, vrr.vignette);
        }

        if (vrr.dump_this_frame) {
            dump_queue(e, e == 0 ? "vr-eye0.png" : "vr-eye1.png", t.image, t.width, t.height, 3);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vrr.provider.release(vrr.provider.user, (vr_surface_t)(VR_SURFACE_EYE0 + e));
        vrr.timing.eye_ms[e] = (float)(now_ms() - t0);
    }
    layers->projection = ok;
    if (ok) {
        memcpy(layers->views, vrr.views, sizeof(layers->views));
    }
    return ok;
}

static void replay_hud(Gfx *dl, vr_layers_t *layers) {
    double t0 = now_ms();
    vr_target_t t;
    if (!vrr.provider.acquire(vrr.provider.user, VR_SURFACE_HUD, &t)) {
        return;
    }
    ensure_hud_textures(t.width, t.height);
    ensure_depth(1, t.width, t.height);

    // The 2D over black, then over white
    uint32_t tris = 0;
    for (int i = 0; i < 2; i++) {
        float v = i == 0 ? 0.0f : 1.0f;
        const float clear[4] = { v, v, v, 1 };
        bind_target(vrr.hud_tex[i], t.width, t.height, vrr.depth_rb[1], clear);
        struct GfxVrPass pass;
        memset(&pass, 0, sizeof(pass));
        pass.kind = GFX_VR_PASS_HUD;
        pass.width = t.width;
        pass.height = t.height;
        gfx_vr_run_pass(dl, &pass);
        tris = gfx_vr_pass_stats()->tris_drawn;
    }
    vrr.info.tris_hud = tris;

    // Solve for the alpha into the swapchain image
    static const float none[4] = { 0, 0, 0, 0 };
    bind_target(t.image, t.width, t.height, 0, none);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(vrr.resolve_program);
    glBindVertexArray(vrr.resolve_vao);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, vrr.hud_tex[0]);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, vrr.hud_tex[1]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    gl_restore();

    if (vrr.dump_this_frame) {
        dump_queue(2, "vr-hud.png", t.image, t.width, t.height, 4);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    vrr.provider.release(vrr.provider.user, VR_SURFACE_HUD);

    layers->hud = vrr.st.hud_visible;
    layers->hud_pose = vrr.hud_pose;
    layers->hud_width = vrr.hud_width;
    layers->hud_height = vrr.hud_height;
    vrr.timing.hud_ms = (float)(now_ms() - t0);
}

static void replay_mono(Gfx *dl, vr_layers_t *layers) {
    double t0 = now_ms();
    vr_target_t t;
    if (!vrr.provider.acquire(vrr.provider.user, VR_SURFACE_PANEL, &t)) {
        return;
    }
    ensure_depth(1, t.width, t.height);
    static const float black[4] = { 0, 0, 0, 1 };
    bind_target(t.image, t.width, t.height, vrr.depth_rb[1], black);
    struct GfxVrPass pass;
    memset(&pass, 0, sizeof(pass));
    pass.kind = GFX_VR_PASS_PANEL;
    pass.width = t.width;
    pass.height = t.height;
    gfx_vr_run_pass(dl, &pass);

    if (vrr.dump_this_frame) {
        dump_queue(3, "vr-panel.png", t.image, t.width, t.height, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    vrr.provider.release(vrr.provider.user, VR_SURFACE_PANEL);

    layers->panel = true;
    layers->panel_width = vrr.st.panel_width;
    vr_stage_panel_pose(&vrr.stage, vrr.views, vrr.views_ok, vrr.st.panel_distance, &layers->panel_pose);
    vrr.timing.panel_ms = (float)(now_ms() - t0);
}


// -----------------------------------------------------------------------------
// The frame

// -----------------------------------------------------------------------------
// Comfort: turning, the shown camera, the stick's frame, the vignette
// (docs/VR_ARCHITECTURE.md section 4; the protocol is in vr_turn.h)

#define VR_RAD_DEG (180.0f / VR_TURN_PI)

static void cam_logf(const char *fmt, ...) VR_PRINTF_LIKE(1, 2);
static void cam_logf(const char *fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    printf("[vr-cam] %s\n", line);
    fflush(stdout);
}

// The keys of the comfort set and the rig, live: the settings page may change them
static void comfort_refresh(void) {
    const vr_settings_t *settings = vr_settings();
    if (settings == NULL) {
        return;
    }
    vr_comfort_from(&vrr.comfort, settings);
    vrr.st.rig.world_scale = vrr.comfort.world_scale;
    vrr.st.rig.head_translation = vrr.comfort.head_translation;
    vrr.st.rig.level_pitch = vrr.comfort.level_pitch;
    vrr.st.rig.height_offset = vrr.comfort.height_offset;
    vrr.stage.seated = vrr.comfort.seated;
    gVrCamTurn.no_shake = !vrr.comfort.camera_shake;
}

// Once per frame, before the eyes: the stick's turn into the queue, then the
// game camera as shown (turned about Mario by what is queued, then closer and
// lower), the headings for the log and the vignette, and the stick's frame
static void comfort_frame(bool stereo, bool hold, float dt) {
    double now_s = (double)vrr.now_ns * 1e-9;
    vrr.play = stereo && !hold && gVrTick.gameplay_camera && !gVrTick.star_select && !gVrTick.in_main_menu;

    // The gate: the game camera can take a turn, and still updates (a pause
    // or a cutscene that stops camera updates closes it)
    if (gVrCamTurn.updates != vrr.turn_updates_seen) {
        vrr.turn_updates_seen = gVrCamTurn.updates;
        vrr.turn_updates_s = now_s;
    }
    double since_update = now_s - vrr.turn_updates_s;
    if (gVrCamTurn.queued != 0.0f && since_update > 0.3) {
        cam_logf("turn: %.1f deg queued, but the camera has not updated for %.2f s; dropped",
            gVrCamTurn.queued * VR_RAD_DEG, since_update);
        gVrCamTurn.queued = 0.0f;
    }
    if (gVrCamTurn.refused != vrr.turn_refused_seen) {
        vrr.turn_refused_seen = gVrCamTurn.refused;
        cam_logf("turn: a camera update could not take the queued turn (mode or state changed)");
    }
    vr_turn_gate_t gate = {
        .open = vrr.play && since_update < 0.2 && gVrCamTurn.allowed,
        .queued = gVrCamTurn.queued,
        .limit_left = gVrCamTurn.limit_left,
        .limit_right = gVrCamTurn.limit_right
    };
    const vr_gamepad_t *pad = vr_input_gamepad();
    float stick = pad->live ? (float)pad->right_x / 32767.0f : 0.0f;
    stick = vr_turn_diag_stick(&vrr.turn, vrr.comfort.diag_turn_stick, stick, now_s, vrr.play);
    vr_turn_step_t step = vr_turn_frame(&vrr.turn, &vrr.comfort.turn, stick, dt, &gate);
    if (step.delta != 0.0f) {
        gVrCamTurn.queued += step.delta;
    }
    if (step.closed && vrr.play && now_s - vrr.limit_log_s > 2.0) {
        vrr.limit_log_s = now_s;
        cam_logf("turn: ignored: %s", gVrCamTurn.allowed ? "the camera has not updated lately"
            : "this camera takes no turn (an authored, swimming, flying or C-up camera, a cutscene or pause)");
    } else if (step.kind == VR_TURN_STEP_SNAP && !step.closed) {
        if (step.delta != 0.0f) {
            cam_logf("turn: snap %s %.0f deg%s (shown %.1f deg, game camera %.1f deg)", step.delta > 0.0f ? "left" : "right",
                fabsf(step.delta) * VR_RAD_DEG, step.limited ? ", cut by the camera's limit" : "",
                vrr.shown_heading.unwrapped * VR_RAD_DEG, vrr.game_heading.unwrapped * VR_RAD_DEG);
            if (vrr.comfort.diag_turn_stick != 0.0f) {
                vrr.snap_trace = 12;
            }
        } else if (now_s - vrr.limit_log_s > 2.0) {
            vrr.limit_log_s = now_s;
            cam_logf("turn: snap dropped: the camera can turn %.0f deg left, %.0f deg right from here",
                gVrCamTurn.limit_left * VR_RAD_DEG, gVrCamTurn.limit_right * VR_RAD_DEG);
        }
    }

    if (!stereo || hold) {
        gVrCamTurn.move_active = false;
        vrr.vignette = 0.0f;
        vrr.shown_prev_valid = false;
        return;
    }

    // The camera as shown
    float orbit = vrr.play ? gVrCamTurn.queued : 0.0f;
    vr_game_cam_orbit(&vrr.cam, gVrInterpPivot, orbit, &vrr.shown_cam);
    if (vrr.play) {
        float at[3] = { gVrInterpCamFocus[0], gVrInterpCamFocus[1], gVrInterpCamFocus[2] };
        if (orbit != 0.0f) {
            vr_turn_rotate_about(at, gVrInterpPivot, orbit);
        }
        vr_game_cam_third_person(&vrr.shown_cam, at, vrr.comfort.camera_lower, vrr.comfort.camera_closer, &vrr.shown_cam);
    }

    // Headings: the log, the vignette
    float game = vr_heading_track(&vrr.game_heading, vr_game_cam_heading(&vrr.cam));
    float shown = vr_heading_track(&vrr.shown_heading, vr_game_cam_heading(&vrr.shown_cam));
    vrr.shown_rate = vrr.shown_prev_valid ? vr_heading_rate(vrr.shown_prev, shown, dt) : 0.0f;
    vrr.game_rate = vrr.shown_prev_valid ? vr_heading_rate(vrr.game_prev, game, dt) : 0.0f;
    vrr.shown_prev = shown;
    vrr.game_prev = game;
    vrr.shown_prev_valid = true;

    float target = (vrr.comfort.vignette && vrr.play) ? vr_vignette_target(vrr.shown_rate, vrr.comfort.vignette_strength) : 0.0f;
    vrr.vignette = vrr.comfort.vignette ? vr_vignette_step(vrr.vignette, target, vrr.comfort.vignette_strength, dt) : 0.0f;

    // The stick's frame (mario.c): the head's or a hand's yaw from the view's forward
    float hand_yaw[2] = { 0.0f, 0.0f };
    bool hand_valid[2];
    for (int h = 0; h < 2; h++) {
        hand_valid[h] = vr_input_hand_yaw(h, &hand_yaw[h]);
    }
    gVrCamTurn.move_yaw = vr_move_reference(vrr.comfort.move_relative, vr_yaw_of(vrr.head.orientation),
        hand_yaw, hand_valid, vrr.stage.recentre.yaw);
    gVrCamTurn.move_active = vrr.play;

    // [vr-cam]: the shown heading against the game camera's
    if (vrr.snap_trace > 0) {
        vrr.snap_trace--;
        cam_logf("snap +%d frames: shown %.1f deg, game camera %.1f deg, queued %.1f deg", 12 - vrr.snap_trace,
            shown * VR_RAD_DEG, game * VR_RAD_DEG, gVrCamTurn.queued * VR_RAD_DEG);
    }
    if (vrr.comfort.diag_turn_stick != 0.0f && vrr.play && now_s - vrr.diag_log_s >= 0.25) {
        vrr.diag_log_s = now_s;
        cam_logf("diag: stick %.2f, shown %.1f deg (%.1f deg/s), game camera %.1f deg (%.1f deg/s), queued %.1f deg, vignette %.2f",
            stick, shown * VR_RAD_DEG, vrr.shown_rate, game * VR_RAD_DEG, vrr.game_rate, gVrCamTurn.queued * VR_RAD_DEG, vrr.vignette);
    }
    if (now_s - vrr.cam_log_s >= 10.0) {
        vrr.cam_log_s = now_s;
        cam_logf("shown %.1f deg, game camera %.1f deg, taken %.0f deg in all, gate %s (%.0f left, %.0f right), "
            "world_scale %.0f, closer %.2f, lower %.2f, height %+.2f m%s, turn %s",
            shown * VR_RAD_DEG, game * VR_RAD_DEG, gVrCamTurn.taken * VR_RAD_DEG, gate.open ? "open" : "closed",
            fminf(gVrCamTurn.limit_left * VR_RAD_DEG, 999.0f), fminf(gVrCamTurn.limit_right * VR_RAD_DEG, 999.0f),
            vrr.st.rig.world_scale, vrr.comfort.camera_closer, vrr.comfort.camera_lower, vrr.comfort.height_offset,
            vrr.comfort.seated ? " seated" : "",
            vrr.comfort.turn.mode == SFVR_TURN_SNAP ? "snap" : (vrr.comfort.turn.mode == SFVR_TURN_SMOOTH ? "smooth" : "off"));
    }
}

// Why a frame cannot be stereo; NULL when it can
static const char *mono_reason(void) {
    if (!vrr.views_ok) {
        return "no tracked views";
    }
    if (vrr.st.force_mono) {
        return "force_mono";
    }
    if (!gVrTick.rendered_area) {
        return "no area rendered";
    }
    if (!gVrTick.gameplay_camera && !gVrTick.star_select) {
        return "not a gameplay camera (menu, intro)";
    }
    if (gVrTick.in_main_menu && !vrr.st.diag_menu_stereo) {
        return "main menu";
    }
    if (!gVrInterpCamValid) {
        return "no interpolated camera";
    }
    return NULL;
}

void vr_render_init(const vr_render_settings_t *settings, const vr_provider_t *provider, const char *dump_dir) {
    memset(&vrr, 0, sizeof(vrr));
    vrr.st = *settings;
    vrr.provider = *provider;
    snprintf(vrr.dump_dir, sizeof(vrr.dump_dir), "%s", dump_dir ? dump_dir : "");
    vr_stage_init(&vrr.stage);
    vrr.ready = true;
    vrr.last_synthetic = -1;
    vrr.info.mono_reason = "";
    vr_turn_reset(&vrr.turn);
    memset(&gVrCamTurn, 0, sizeof(gVrCamTurn));
    vr_logf("render: 2D layout %dx%d, world_scale %.1f units/m, level_pitch %d, hud %.2f m at %.0f%%, panel %.2f m x %.2f m",
        settings->ui_width, settings->ui_height, settings->rig.world_scale, settings->rig.level_pitch,
        settings->hud_distance, settings->hud_scale, settings->panel_distance, settings->panel_width);
}

void vr_render_shutdown(void) {
    if (!vrr.ready) {
        return;
    }
    if (vrr.gl_ready) {
        dump_poll(true);
    }
    vrr.ready = false;
    gVrBillboardHead = false;
}

void vr_render_frame(Gfx *dl, const vr_frame_t *frame, bool renders, vr_layers_t *layers) {
    memset(layers, 0, sizeof(*layers));
    if (!vrr.ready || dl == NULL) {
        return;
    }
    if (vrr.gl_ready) {
        dump_poll(false);
    }
    if (!renders) {
        // Standby: nothing is drawn; the stage keeps its state
        return;
    }
    if (!vrr.gl_ready) {
        gl_make();
    }

    float dt = vrr.last_ns != 0 ? (float)((double)(frame->predicted_display_time - vrr.last_ns) * 1e-9) : 0.0f;
    dt = dt < 0.0f ? 0.0f : (dt > 0.1f ? 0.1f : dt);
    vrr.last_ns = frame->predicted_display_time;
    vrr.now_ns = frame->predicted_display_time;
    vrr.views_ok = frame->views_valid;
    comfort_refresh();
    vr_view_t raw_views[2];
    memcpy(raw_views, frame->views, sizeof(raw_views));
    if (vrr.views_ok) {
        memcpy(vrr.views, frame->views, sizeof(vrr.views));
        // diag_yaw turns the head after the recentre (which takes the raw head),
        // so the turn stays visible
        if (vrr.st.diag_yaw != 0.0f) {
            vr_views_apply_yaw(vrr.views, vrr.st.diag_yaw);
        }
        vr_head_pose(vrr.views, &vrr.head);
    }
    vrr.frames++;
    vrr.dump_this_frame = vrr.st.dump_frame > 0 && vrr.frames == (uint32_t)vrr.st.dump_frame && vrr.dump_dir[0];

    // force_render's synthetic head and the tracked head are different heads:
    // recentre when one takes over from the other (putting the headset on
    // during an unattended run)
    if (vrr.views_ok && (int)frame->synthetic != vrr.last_synthetic) {
        if (vrr.last_synthetic >= 0) {
            vr_logf("render: views now %s; recentring", frame->synthetic ? "synthetic (standby)" : "tracked");
        }
        vrr.last_synthetic = frame->synthetic;
        vr_stage_request_height(&vrr.stage);
    }
    memset(&vrr.timing, 0, sizeof(vrr.timing));
    vrr.timing.gpu_ms = -1.0f;

    const char *reason = mono_reason();
    if (reason == NULL && !vr_game_cam_from_view(gVrInterpCamView, &vrr.cam)) {
        reason = "degenerate camera";
    }
    bool stereo = reason == NULL;
    // Behind a closed warp transition the game draws no area: in stereo that
    // is a held full fade in the transition's colour, not a switch to the panel
    bool hold = !stereo && vrr.views_ok && gVrTick.transition_hold && !gVrTick.in_main_menu
        && vrr.stage.mode == VR_MODE_STEREO;
    if (hold != vrr.holding) {
        vrr.holding = hold;
        vr_logf("render: %s", hold ? "holding behind a closed transition" : "transition hold released");
    }
    if (!hold && vr_stage_set_mode(&vrr.stage, stereo ? VR_MODE_STEREO : VR_MODE_MONO, vrr.now_ns)) {
        vr_logf("render: %s%s%s", stereo ? "stereo" : "mono", stereo ? "" : ": ", stereo ? "" : reason);
    }
    vrr.info.mono_reason = stereo || hold ? "" : reason;

    if (stereo && !frame->synthetic) {
        vrr.worn_stereo_frames++;
        if (vrr.st.dump_worn_frame > 0 && vrr.worn_stereo_frames == (uint32_t)vrr.st.dump_worn_frame && vrr.dump_dir[0]) {
            vrr.dump_this_frame = true;
        }
    }
    if (vrr.dump_this_frame) {
        mkdirs(vrr.dump_dir);
    }

    if (stereo && !hold) {
        bool height = vrr.stage.height_pending || !vrr.stage.recentre_valid || vrr.stage.seated;
        if (vr_stage_apply_recentre(&vrr.stage, raw_views)) {
            vr_logf("render: recentred (yaw %.1f deg, standing height %s %.2f m)", vrr.stage.recentre.yaw * 180.0 / M_PI,
                height ? "taken:" : "kept:", vrr.stage.recentre.origin[1]);
        }
    }
    comfort_frame(stereo, hold, dt);

    if (hold) {
        gVrBillboardHead = false;
    } else if (stereo) {
        // The anchor is the game camera as shown: turned by the queued comfort
        // turn, then closer and lower (comfort_frame)
        vr_anchor_t anchor;
        vr_anchor_from_cam(&vrr.shown_cam, vrr.st.rig.level_pitch, &anchor);
        vr_eye_cam_t head_cam;
        // The star select is a small fixed scene: its own scale makes it a diorama
        vr_rig_t rig = vrr.st.rig;
        if (gVrTick.star_select && vrr.st.star_select_scale > 0.0f) {
            rig.world_scale = vrr.st.star_select_scale;
        }
        vr_eye_from_pose(&anchor, &rig, &vrr.stage.recentre, &vrr.head, &vrr.head, &head_cam);
        for (int e = 0; e < 2; e++) {
            vr_eye_ctx_t *c = &vrr.eye_ctx[e];
            c->cam = vrr.cam;
            vr_eye_from_pose(&anchor, &rig, &vrr.stage.recentre, &vrr.head, &vrr.views[e].pose, &c->eye);
            memcpy(c->fov, vrr.views[e].fov, sizeof(c->fov));
        }

        // The next tick's billboards face the head
        vr_billboard_rotation(&vrr.cam, &head_cam, gVrBillboardRot);
        vr_cylboard_rotation(&vrr.cam, &head_cam, gVrCylboardRot);
        gVrBillboardHead = true;

        vr_hud_pose(&vrr.head, vrr.st.hud_distance, &vrr.hud_pose);
        vr_hud_size(vrr.st.hud_distance, vrr.st.hud_scale, (float)vrr.st.ui_width / (float)vrr.st.ui_height,
            &vrr.hud_width, &vrr.hud_height);

        gpu_begin();
        if (replay_eyes(dl, layers)) {
            replay_hud(dl, layers);
        }
        gpu_end();

        // What the eyes were rendered with, every 10 s at 72 Hz: tracked poses
        // and fovs, and the eye positions in the game (the IPD in game units)
        if (vrr.frames % 720 == 1) {
            const vr_view_t *l = &vrr.views[0], *r = &vrr.views[1];
            float d[3];
            for (int i = 0; i < 3; i++) {
                d[i] = vrr.eye_ctx[1].eye.position[i] - vrr.eye_ctx[0].eye.position[i];
            }
            vr_logf("views %s: L pos=%.4f,%.4f,%.4f q=%.4f,%.4f,%.4f,%.4f fov=%.3f,%.3f,%.3f,%.3f | "
                "R pos=%.4f,%.4f,%.4f q=%.4f,%.4f,%.4f,%.4f fov=%.3f,%.3f,%.3f,%.3f | eye sep %.2f units, cam %.0f,%.0f,%.0f",
                frame->synthetic ? "synthetic" : "tracked",
                l->pose.position[0], l->pose.position[1], l->pose.position[2],
                l->pose.orientation[0], l->pose.orientation[1], l->pose.orientation[2], l->pose.orientation[3],
                l->fov[0], l->fov[1], l->fov[2], l->fov[3],
                r->pose.position[0], r->pose.position[1], r->pose.position[2],
                r->pose.orientation[0], r->pose.orientation[1], r->pose.orientation[2], r->pose.orientation[3],
                r->fov[0], r->fov[1], r->fov[2], r->fov[3],
                sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]),
                vrr.cam.position[0], vrr.cam.position[1], vrr.cam.position[2]);
        }
    } else {
        gVrBillboardHead = false;
        replay_mono(dl, layers);
    }

    gl_restore();
    gfx_vr_restore_dimensions();

    // The stage's mode-switch fade is black; a warp transition fades in its own colour
    float stage_alpha = vr_stage_fade_alpha(&vrr.stage, vrr.now_ns);
    float trans_alpha = (stereo || hold) ? gVrTick.transition_alpha : 0.0f;
    layers->fade_alpha = stage_alpha > trans_alpha ? stage_alpha : trans_alpha;
    for (int i = 0; i < 3; i++) {
        layers->fade_rgb[i] = trans_alpha > stage_alpha ? gVrTick.transition_rgb[i] / 255.0f : 0.0f;
    }
    vrr.info.mode = vrr.stage.mode;
    vrr.info.frames = vrr.frames;
    vrr.info.fade_alpha = layers->fade_alpha;
    if (vrr.dump_this_frame) {
        dump_state_log(layers);
    }
}
