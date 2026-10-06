#include <math.h>
#include <string.h>

#include "vr_cinema.h"

bool vr_cinema_cutscene(unsigned id) {
    switch (id) {
        case 0: return false;
#define VR_CINEMA_CASE(name, id, cinema, why) case id: return cinema;
        VR_CINEMA_CUTSCENES(VR_CINEMA_CASE)
#undef VR_CINEMA_CASE
        default: return true;
    }
}

const char *vr_cinema_cutscene_reason(unsigned id) {
    switch (id) {
        case 0: return "player-controlled camera mode";
#define VR_CINEMA_REASON(name, id, cinema, why) case id: return why;
        VR_CINEMA_CUTSCENES(VR_CINEMA_REASON)
#undef VR_CINEMA_REASON
        default: return "unknown scripted camera; safe cinema fallback";
    }
}

bool vr_cinema_latch(vr_cinema_latch_t *s, bool want, bool hold, int64_t now_ns) {
    if (hold) {
        // A closed warp or unavailable camera must not consume hysteresis.
        s->pending_valid = false;
        return s->active;
    }
    if (want == s->active) {
        s->pending_valid = false;
        return s->active;
    }
    if (!s->pending_valid || want != s->pending || now_ns < s->pending_since_ns) {
        s->pending = want;
        s->pending_valid = true;
        s->pending_since_ns = now_ns;
    }
    int64_t delay = want ? VR_CINEMA_ENTER_NS : VR_CINEMA_EXIT_NS;
    if (now_ns - s->pending_since_ns >= delay) {
        s->active = want;
        s->pending_valid = false;
    }
    return s->active;
}

void vr_cinema_place(const vr_pose_t *head, float distance, float width, vr_cinema_screen_t *out) {
    vr_panel_pose(head, distance, &out->pose);
    out->width = width;
    out->height = width * (9.0f / 16.0f);
}

bool vr_cinema_views(const vr_cinema_screen_t *s, const vr_view_t eyes[2], vr_view_t out[2]) {
    float rot[9], inv[9];
    vr_mat3_from_quat(s->pose.orientation, rot);
    vr_mat3_transpose(rot, inv);
    vr_view_t views[2];
    for (int e = 0; e < 2; e++) {
        float p[3];
        for (int i = 0; i < 3; i++) {
            p[i] = eyes[e].pose.position[i] - s->pose.position[i];
        }
        vr_mat3_mulv(inv, p, p);
        if (!(p[2] > 0.1f) || !(s->width > 0.0f) || !(s->height > 0.0f)) {
            return false;
        }
        views[e].pose = eyes[e].pose;
        memcpy(views[e].pose.orientation, s->pose.orientation, sizeof(s->pose.orientation));
        // SM64 exports an atan2f with game-axis semantics. Use libc's double
        // atan2, as vr_math does: otherwise the submitted screen is reciprocal
        // aspect (9:16), even though the game picture was drawn for 16:9.
        views[e].fov[0] = (float)atan2(-s->width * 0.5f - p[0], p[2]);
        views[e].fov[1] = (float)atan2( s->width * 0.5f - p[0], p[2]);
        views[e].fov[2] = (float)atan2( s->height * 0.5f - p[1], p[2]);
        views[e].fov[3] = (float)atan2(-s->height * 0.5f - p[1], p[2]);
    }
    memcpy(out, views, sizeof(views));
    return true;
}

void vr_cinema_band(const vr_cinema_screen_t *s, vr_pose_t *pose, float *width, float *height) {
    *width = s->width * 0.55f;
    *height = *width * (9.0f / 16.0f);
    *pose = s->pose;
    // The screen is yaw-only; its up is world up. A 2 cm gap separates the
    // band's top from the picture's bottom. Both surfaces share one depth.
    pose->position[1] -= 0.5f * (s->height + *height) + 0.02f;
}

bool vr_cinema_game_eye(const vr_game_cam_t *cam, const float proj[16],
    float ipd, float width, int eye, vr_eye_cam_t *out, float fov[4], float projection[16]) {
    float near, far;
    if (!vr_game_proj_near_far(proj, &near, &far) || !(proj[0] > 0.0f) || !(proj[5] > 0.0f)
        || !(width > 0.0f) || !isfinite(ipd) || ipd < 0.0f || (eye != 0 && eye != 1)) {
        return false;
    }
    // A square eye texture is mapped onto our fixed 16:9 picture. Keep the
    // actual interpolated vertical FOV and lens centre, but adapt horizontal
    // scale to that surface; the game's 4:3/mod/window aspect cannot leak in.
    float xscale = proj[5] * (9.0f / 16.0f);
    float left = (proj[8] - 1.0f) / xscale;
    float right = (proj[8] + 1.0f) / xscale;
    float up = (proj[9] + 1.0f) / proj[5];
    float down = (proj[9] - 1.0f) / proj[5];
    float separation = 0.6f * ipd * (right - left) * near / width;
    float offset = (eye == 0 ? -0.5f : 0.5f) * separation;
    for (int i = 0; i < 3; i++) {
        out->position[i] = cam->position[i] + offset * cam->right[i];
        out->rotation[i] = cam->right[i];
        out->rotation[3 + i] = cam->up[i];
        out->rotation[6 + i] = cam->back[i];
    }
    memcpy(out->view, cam->view, sizeof(out->view));
    out->view[12] -= offset;
    float shift = offset / near;
    fov[0] = atanf(left - shift);
    fov[1] = atanf(right - shift);
    fov[2] = atanf(up);
    fov[3] = atanf(down);
    if (projection) {
        memcpy(projection, proj, 16 * sizeof(float));
        projection[0] = xscale;
        projection[8] -= xscale * shift;
        projection[12] -= xscale * offset;
    }
    return true;
}
