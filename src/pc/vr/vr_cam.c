#include <math.h>
#include <string.h>

#include "vr_cam.h"

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross3(const float a[3], const float b[3], float out[3]) {
    float r[3] = {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
    memcpy(out, r, sizeof(r));
}

static bool normalize3(float v[3]) {
    float len = sqrtf(dot3(v, v));
    if (!(len > 1e-6f)) {
        return false;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return true;
}

void vr_mat4_rigid_inverse(const float m[16], float out[16]) {
    // [R t] -> [R^T  -R^T t]
    float r[16];
    for (int c = 0; c < 3; c++) {
        for (int row = 0; row < 3; row++) {
            r[c * 4 + row] = m[row * 4 + c];
        }
        r[c * 4 + 3] = 0.0f;
    }
    for (int row = 0; row < 3; row++) {
        r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
    }
    r[15] = 1.0f;
    memcpy(out, r, sizeof(r));
}

bool vr_game_cam_from_view(const float view[16], vr_game_cam_t *out) {
    memcpy(out->view, view, sizeof(out->view));
    // The rows of the view's rotation are the camera axes in world space
    for (int i = 0; i < 3; i++) {
        out->right[i] = view[i * 4 + 0];
        out->up[i] = view[i * 4 + 1];
        out->back[i] = view[i * 4 + 2];
    }
    float rl = dot3(out->right, out->right);
    float ul = dot3(out->up, out->up);
    float bl = dot3(out->back, out->back);
    if (!(fabsf(rl - 1.0f) < 0.01f && fabsf(ul - 1.0f) < 0.01f && fabsf(bl - 1.0f) < 0.01f)) {
        return false;
    }
    vr_mat4_rigid_inverse(view, out->inv_view);
    out->position[0] = out->inv_view[12];
    out->position[1] = out->inv_view[13];
    out->position[2] = out->inv_view[14];
    return true;
}

void vr_anchor_from_cam(const vr_game_cam_t *cam, bool level_pitch, vr_anchor_t *out) {
    static const float world_up[3] = { 0.0f, 1.0f, 0.0f };
    float forward[3] = { -cam->back[0], -cam->back[1], -cam->back[2] };
    float right[3], up[3], back[3];

    if (level_pitch) {
        float f[3] = { forward[0], 0.0f, forward[2] };
        if (!normalize3(f)) {
            // Straight up or down: the heading is where the camera's up points (looking
            // down) or away from it (looking up)
            float s = forward[1] < 0.0f ? 1.0f : -1.0f;
            f[0] = cam->up[0] * s;
            f[1] = 0.0f;
            f[2] = cam->up[2] * s;
            if (!normalize3(f)) {
                f[0] = 0.0f;
                f[2] = -1.0f;
            }
        }
        memcpy(forward, f, sizeof(f));
        cross3(forward, world_up, right);
        normalize3(right);
        memcpy(up, world_up, sizeof(up));
    } else {
        // Keep pitch, drop roll
        cross3(forward, world_up, right);
        if (!normalize3(right)) {
            memcpy(right, cam->right, sizeof(right));
        }
        cross3(right, forward, up);
        normalize3(up);
    }
    back[0] = -forward[0];
    back[1] = -forward[1];
    back[2] = -forward[2];

    for (int i = 0; i < 3; i++) {
        out->rotation[0 + i] = right[i];
        out->rotation[3 + i] = up[i];
        out->rotation[6 + i] = back[i];
    }
    memcpy(out->position, cam->position, sizeof(out->position));
}

static void view_from(const float rot[9], const float pos[3], float out[16]) {
    // world -> eye: [R^T  -R^T p]
    for (int c = 0; c < 3; c++) {
        for (int row = 0; row < 3; row++) {
            out[c * 4 + row] = rot[row * 3 + c];
        }
        out[c * 4 + 3] = 0.0f;
    }
    for (int row = 0; row < 3; row++) {
        out[12 + row] = -(out[row] * pos[0] + out[4 + row] * pos[1] + out[8 + row] * pos[2]);
    }
    out[15] = 1.0f;
}

void vr_eye_from_pose(const vr_anchor_t *anchor, const vr_rig_t *rig, const vr_recentre_t *rc,
    const vr_pose_t *head, const vr_pose_t *pose, vr_eye_cam_t *out) {
    // Undo the recentre: a rotation by -yaw about +Y, around the recentre origin
    float rq[4], rc_rot[9];
    vr_quat_from_yaw(-rc->yaw, rq);
    vr_mat3_from_quat(rq, rc_rot);

    float head_delta[3], eye_delta[3], local[3];
    for (int i = 0; i < 3; i++) {
        head_delta[i] = head->position[i] - rc->origin[i];
        eye_delta[i] = pose->position[i] - head->position[i];
    }
    vr_mat3_mulv(rc_rot, head_delta, head_delta);
    vr_mat3_mulv(rc_rot, eye_delta, eye_delta);
    for (int i = 0; i < 3; i++) {
        local[i] = (head_delta[i] * rig->head_translation + eye_delta[i]) * rig->world_scale;
    }

    // Recentred local axes are the anchor's axes
    float world[3];
    vr_mat3_mulv(anchor->rotation, local, world);
    for (int i = 0; i < 3; i++) {
        out->position[i] = anchor->position[i] + world[i];
    }

    float pose_rot[9], recentred[9];
    vr_mat3_from_quat(pose->orientation, pose_rot);
    vr_mat3_mul(rc_rot, pose_rot, recentred);
    vr_mat3_mul(anchor->rotation, recentred, out->rotation);

    view_from(out->rotation, out->position, out->view);
}

bool vr_proj_is_perspective(const float proj[16]) {
    // Column 2 row 3 is -1 (w = -z) and the w row has no constant
    return fabsf(proj[11] + 1.0f) < 1e-3f && fabsf(proj[15]) < 1e-3f;
}

bool vr_game_proj_near_far(const float proj[16], float *near, float *far) {
    if (!vr_proj_is_perspective(proj)) {
        return false;
    }
    // guPerspective: a = (n + f) / (n - f), b = 2 n f / (n - f)
    float a = proj[10];
    float b = proj[14];
    if (fabsf(a - 1.0f) < 1e-9f || fabsf(a + 1.0f) < 1e-9f) {
        return false;
    }
    float n = b / (a - 1.0f);
    float f = b / (a + 1.0f);
    if (!(n > 0.0f) || !(f > n)) {
        return false;
    }
    *near = n;
    *far = f;
    return true;
}

void vr_eye_world_proj(const vr_game_cam_t *cam, const vr_eye_cam_t *eye, const float fov[4],
    float near, float far, float out[16]) {
    float proj[16], pv[16];
    vr_projection_fov(fov, near, far, proj);
    vr_mat4_mul(proj, eye->view, pv);
    vr_mat4_mul(pv, cam->inv_view, out);
}

void vr_eye_sky_proj(const vr_eye_cam_t *eye, const float fov[4], float near, float far, float out[16]) {
    float proj[16], rot[16];
    vr_projection_fov(fov, near, far, proj);
    memcpy(rot, eye->view, sizeof(rot));
    rot[12] = rot[13] = rot[14] = 0.0f;
    vr_mat4_mul(proj, rot, out);
}

// The 3x3 rotation of a view (world -> camera), column-major
static void view_rotation(const float view[16], float out[9]) {
    for (int c = 0; c < 3; c++) {
        for (int row = 0; row < 3; row++) {
            out[c * 3 + row] = view[c * 4 + row];
        }
    }
}

// Row-vector 3x3 whose rows are the given world vectors expressed in camera space
static void rows_in_cam(const vr_game_cam_t *cam, const float *const axes[3], float out[3][3]) {
    float rot[9];
    view_rotation(cam->view, rot);
    for (int i = 0; i < 3; i++) {
        float v[3];
        vr_mat3_mulv(rot, axes[i], v);
        out[i][0] = v[0];
        out[i][1] = v[1];
        out[i][2] = v[2];
    }
}

void vr_billboard_rotation(const vr_game_cam_t *cam, const vr_eye_cam_t *head, float out[3][3]) {
    const float *axes[3] = { &head->rotation[0], &head->rotation[3], &head->rotation[6] };
    rows_in_cam(cam, axes, out);
}

void vr_cylboard_rotation(const vr_game_cam_t *cam, const vr_eye_cam_t *head, float out[3][3]) {
    static const float world_up[3] = { 0.0f, 1.0f, 0.0f };
    float right[3] = { head->rotation[0], 0.0f, head->rotation[2] };
    if (!normalize3(right)) {
        // Looking straight up or down: the head's back gives the heading
        right[0] = head->rotation[8];
        right[1] = 0.0f;
        right[2] = -head->rotation[6];
        if (!normalize3(right)) {
            right[0] = 1.0f;
            right[2] = 0.0f;
        }
    }
    float back[3];
    cross3(right, world_up, back);
    const float *axes[3] = { right, world_up, back };
    rows_in_cam(cam, axes, out);
}
