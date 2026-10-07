// The SM64 rig (src/pc/vr/vr_cam.c) against the contract the compositor holds
// the app to: a point at LOCAL position q must land, in each eye's image, where
// the runtime's projection of q through that eye's submitted pose and fov puts
// it. The chain is the one Fast3D runs: modelview = model * camera view (baked by
// the game, here the camera from a lookat like mtxf_lookat), projection replaced
// by vr_eye_world_proj, row vectors against Mat4 memory.

#include <math.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_cam.h"
#include "vr_math.h"

#define DEG (3.14159265358979323846f / 180.0f)

// The Frame's left-eye fov (platform.md); the right eye mirrors it
static const float FRAME_FOV_L[4] = { -1.028f, 0.885f, 0.864f, -1.048f };
static const float FRAME_FOV_R[4] = { -0.885f, 1.028f, 0.864f, -1.048f };

// mtxf_lookat (src/engine/math_util.c) with roll 0, as Mat4 memory
static void game_lookat(const float from[3], const float to[3], float out[16]) {
    float back[3] = { from[0] - to[0], from[1] - to[1], from[2] - to[2] };
    float l = sqrtf(back[0] * back[0] + back[1] * back[1] + back[2] * back[2]);
    for (int i = 0; i < 3; i++) back[i] /= l;
    float dx = to[0] - from[0], dz = to[2] - from[2];
    float xz = sqrtf(dx * dx + dz * dz);
    dx = -dx / xz;
    dz = -dz / xz;
    float up[3] = { 0, 1, 0 };
    float right[3] = {
        up[1] * back[2] - up[2] * back[1],
        up[2] * back[0] - up[0] * back[2],
        up[0] * back[1] - up[1] * back[0]
    };
    l = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (int i = 0; i < 3; i++) right[i] /= l;
    float u[3] = {
        back[1] * right[2] - back[2] * right[1],
        back[2] * right[0] - back[0] * right[2],
        back[0] * right[1] - back[1] * right[0]
    };
    float m[4][4];
    m[0][0] = right[0]; m[1][0] = right[1]; m[2][0] = right[2];
    m[3][0] = -(from[0] * right[0] + from[1] * right[1] + from[2] * right[2]);
    m[0][1] = u[0]; m[1][1] = u[1]; m[2][1] = u[2];
    m[3][1] = -(from[0] * u[0] + from[1] * u[1] + from[2] * u[2]);
    m[0][2] = back[0]; m[1][2] = back[1]; m[2][2] = back[2];
    m[3][2] = -(from[0] * back[0] + from[1] * back[1] + from[2] * back[2]);
    m[0][3] = 0; m[1][3] = 0; m[2][3] = 0; m[3][3] = 1;
    memcpy(out, m, sizeof(m));
}

// What Fast3D does with Mat4 memory: v' = v * MV, then * P (row vectors)
static void fast3d_ndc(const float mv[16], const float p[16], const float v[3], float ndc[3]) {
    const float (*a)[4] = (const float (*)[4])mv;
    const float (*b)[4] = (const float (*)[4])p;
    float t[4], c[4];
    for (int j = 0; j < 4; j++) {
        t[j] = v[0] * a[0][j] + v[1] * a[1][j] + v[2] * a[2][j] + a[3][j];
    }
    for (int j = 0; j < 4; j++) {
        c[j] = t[0] * b[0][j] + t[1] * b[1][j] + t[2] * b[2][j] + t[3] * b[3][j];
    }
    for (int i = 0; i < 3; i++) {
        ndc[i] = c[i] / c[3];
    }
}

// The compositor's view of a LOCAL point: the eye's pose inverse, then its fov
static void runtime_ndc(const vr_view_t *view, const float q[3], float ndc[3]) {
    float rot[9], rt[9], d[3], e[3];
    vr_mat3_from_quat(view->pose.orientation, rot);
    vr_mat3_transpose(rot, rt);
    for (int i = 0; i < 3; i++) d[i] = q[i] - view->pose.position[i];
    vr_mat3_mulv(rt, d, e);
    float proj[16], ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    vr_projection_fov(view->fov, 5.0f, 20000.0f, proj);
    vr_project(ident, proj, e, ndc);
}

// LOCAL -> game world, the rig's own mapping (head_translation 1)
static void local_to_world(const vr_anchor_t *a, const vr_rig_t *rig, const vr_recentre_t *rc, const float q[3], float out[3]) {
    float rq[4], rr[9], d[3], l[3], w[3];
    vr_quat_from_yaw(-rc->yaw, rq);
    vr_mat3_from_quat(rq, rr);
    for (int i = 0; i < 3; i++) d[i] = q[i] - rc->origin[i];
    vr_mat3_mulv(rr, d, l);
    for (int i = 0; i < 3; i++) l[i] *= rig->world_scale;
    vr_mat3_mulv(a->rotation, l, w);
    for (int i = 0; i < 3; i++) out[i] = a->position[i] + w[i];
}

static void quat_axis(float ax, float ay, float az, float angle, float out[4]) {
    float s = sinf(angle * 0.5f);
    float l = sqrtf(ax * ax + ay * ay + az * az);
    out[0] = ax / l * s;
    out[1] = ay / l * s;
    out[2] = az / l * s;
    out[3] = cosf(angle * 0.5f);
}

typedef struct {
    vr_game_cam_t cam;
    vr_anchor_t anchor;
    vr_rig_t rig;
    vr_recentre_t rc;
    vr_view_t views[2];
    vr_pose_t head;
    vr_eye_cam_t eye[2];
    float game_proj[16];
} scene_t;

// A pitched game camera, a recentre at some head pose, then the head moved and
// turned, canted eyes, the Frame's asymmetric fovs
static void make_scene(scene_t *s, float head_yaw, float head_pitch, float cant) {
    memset(s, 0, sizeof(*s));
    float from[3] = { -1328.0f, 516.0f, 3318.0f }, to[3] = { -1300.0f, 300.0f, 2000.0f };
    float view[16];
    game_lookat(from, to, view);
    CHECK(vr_game_cam_from_view(view, &s->cam));
    s->rig.world_scale = 104.0f;
    s->rig.head_translation = 1.0f;
    s->rig.level_pitch = true;
    vr_anchor_from_cam(&s->cam, true, &s->anchor);
    s->rc.yaw = 0.3f;
    s->rc.origin[0] = 0.1f;
    s->rc.origin[1] = -0.2f;
    s->rc.origin[2] = 0.05f;

    float qy[4], qp[4], head_q[4];
    quat_axis(0, 1, 0, s->rc.yaw + head_yaw, qy);
    quat_axis(1, 0, 0, head_pitch, qp);
    vr_quat_mul(qy, qp, head_q);
    float head_pos[3] = { 0.25f, -0.1f, -0.15f };
    float rot[9];
    vr_mat3_from_quat(head_q, rot);
    for (int e = 0; e < 2; e++) {
        float off[3] = { e == 0 ? -0.032f : 0.032f, 0, 0 }, w[3];
        vr_mat3_mulv(rot, off, w);
        for (int i = 0; i < 3; i++) s->views[e].pose.position[i] = head_pos[i] + w[i];
        float qc[4];
        quat_axis(0, 1, 0, e == 0 ? cant : -cant, qc); // canted outward
        vr_quat_mul(head_q, qc, s->views[e].pose.orientation);
        memcpy(s->views[e].fov, e == 0 ? FRAME_FOV_L : FRAME_FOV_R, sizeof(s->views[e].fov));
    }
    vr_head_pose(s->views, &s->head);
    for (int e = 0; e < 2; e++) {
        vr_eye_from_pose(&s->anchor, &s->rig, &s->rc, &s->head, &s->views[e].pose, &s->eye[e]);
    }
    // The game's own projection: guPerspective(45, 4/3, 5, 20000)
    float f = 1.0f / tanf(22.5f * DEG), n = 5.0f, fr = 20000.0f;
    memset(s->game_proj, 0, sizeof(s->game_proj));
    s->game_proj[0] = f / (4.0f / 3.0f);
    s->game_proj[5] = f;
    s->game_proj[10] = (n + fr) / (n - fr);
    s->game_proj[11] = -1.0f;
    s->game_proj[14] = 2.0f * n * fr / (n - fr);
}

static void check_contract(scene_t *s) {
    float n, f;
    CHECK(vr_game_proj_near_far(s->game_proj, &n, &f));
    CHECK_NEAR(n, 5.0, 1e-3);
    CHECK_NEAR(f, 20000.0, 5.0);

    // Points around each eye, in LOCAL metres: ahead, off to the sides, near and far
    const float probes[][3] = {
        { 0.0f, 0.0f, -1.0f }, { 0.4f, 0.2f, -0.8f }, { -0.5f, -0.3f, -2.0f },
        { 0.1f, 0.0f, -12.0f }, { -2.0f, 1.0f, -6.0f }, { 0.3f, -0.4f, -0.4f }
    };
    for (int e = 0; e < 2; e++) {
        float proj[16];
        vr_eye_world_proj(&s->cam, &s->eye[e], s->views[e].fov, n, f, proj);
        float rot[9];
        vr_mat3_from_quat(s->views[e].pose.orientation, rot);
        for (size_t k = 0; k < sizeof(probes) / sizeof(probes[0]); k++) {
            float q[3], d[3];
            vr_mat3_mulv(rot, probes[k], d); // relative to the eye's own axes
            for (int i = 0; i < 3; i++) q[i] = s->views[e].pose.position[i] + d[i];
            float w[3], got[3], want[3];
            local_to_world(&s->anchor, &s->rig, &s->rc, q, w);
            fast3d_ndc(s->cam.view, proj, w, got); // model identity: modelview is the camera view
            runtime_ndc(&s->views[e], q, want);
            CHECK_NEAR(got[0], want[0], 2e-3);
            CHECK_NEAR(got[1], want[1], 2e-3);
        }
    }
}

static void test_contract_level(void) {
    scene_t s;
    make_scene(&s, 0.0f, 0.0f, 0.0f);
    check_contract(&s);
}

static void test_contract_turned_pitched_canted(void) {
    scene_t s;
    make_scene(&s, 40.0f * DEG, -25.0f * DEG, 3.0f * DEG);
    check_contract(&s);
    make_scene(&s, 170.0f * DEG, 30.0f * DEG, -2.0f * DEG);
    check_contract(&s);
}

static void test_eye_separation_is_ipd_once(void) {
    scene_t s;
    make_scene(&s, 0.4f, 0.1f, 0.0f);
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = s.eye[1].position[i] - s.eye[0].position[i];
    CHECK_NEAR(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 0.064 * 104.0, 1e-2);
    // head_translation scales the head, never the eyes
    s.rig.head_translation = 0.0f;
    vr_eye_cam_t a, b;
    vr_eye_from_pose(&s.anchor, &s.rig, &s.rc, &s.head, &s.views[0].pose, &a);
    vr_eye_from_pose(&s.anchor, &s.rig, &s.rc, &s.head, &s.views[1].pose, &b);
    for (int i = 0; i < 3; i++) d[i] = b.position[i] - a.position[i];
    CHECK_NEAR(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 0.064 * 104.0, 1e-2);
}

static void test_disparity_sign(void) {
    // A point straight ahead of the head lands right of centre in the left eye and
    // left of centre in the right eye (symmetric fov, so centre is ndc 0)
    scene_t s;
    make_scene(&s, 0.0f, 0.0f, 0.0f);
    static const float sym[4] = { -0.8f, 0.8f, 0.8f, -0.8f };
    float q[3] = { s.head.position[0], s.head.position[1], s.head.position[2] }, fwd[3] = { 0, 0, -3.0f }, rot[9], d[3];
    vr_mat3_from_quat(s.head.orientation, rot);
    vr_mat3_mulv(rot, fwd, d);
    for (int i = 0; i < 3; i++) q[i] += d[i];
    float w[3], ndc[2][3];
    local_to_world(&s.anchor, &s.rig, &s.rc, q, w);
    for (int e = 0; e < 2; e++) {
        float proj[16];
        vr_eye_world_proj(&s.cam, &s.eye[e], sym, 5.0f, 20000.0f, proj);
        fast3d_ndc(s.cam.view, proj, w, ndc[e]);
    }
    CHECK(ndc[0][0] > 0.0f);
    CHECK(ndc[1][0] < 0.0f);
    CHECK_NEAR(ndc[0][1], ndc[1][1], 1e-4); // no vertical disparity
}

static void test_level_anchor(void) {
    scene_t s;
    make_scene(&s, 0.0f, 0.0f, 0.0f);
    // The camera pitches down; the anchor keeps its heading and drops the pitch
    CHECK(s.cam.back[1] > 0.1f);
    CHECK_NEAR(s.anchor.rotation[4], 1.0, 1e-6); // up is world up
    float fwd_cam_h = sqrtf(s.cam.back[0] * s.cam.back[0] + s.cam.back[2] * s.cam.back[2]);
    CHECK_NEAR(s.anchor.rotation[6], s.cam.back[0] / fwd_cam_h, 1e-5);
    CHECK_NEAR(s.anchor.rotation[8], s.cam.back[2] / fwd_cam_h, 1e-5);
    CHECK_NEAR(s.anchor.position[0], -1328.0, 1e-2);
    CHECK_NEAR(s.anchor.position[1], 516.0, 1e-2);
}

static void test_recentred_head_sits_at_camera(void) {
    // With the head at the recentre origin facing the recentre yaw, the head
    // centre is the camera, facing the anchor's forward
    scene_t s;
    make_scene(&s, 0.0f, 0.0f, 0.0f);
    vr_pose_t head;
    vr_quat_from_yaw(s.rc.yaw, head.orientation);
    memcpy(head.position, s.rc.origin, sizeof(head.position));
    vr_eye_cam_t c;
    vr_eye_from_pose(&s.anchor, &s.rig, &s.rc, &head, &head, &c);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(c.position[i], s.anchor.position[i], 1e-3);
    }
    for (int i = 0; i < 9; i++) {
        CHECK_NEAR(c.rotation[i], s.anchor.rotation[i], 1e-5);
    }
}

static void test_sky_has_no_translation(void) {
    scene_t s;
    make_scene(&s, 0.2f, 0.1f, 0.0f);
    float a[16], b[16];
    vr_eye_sky_proj(&s.eye[0], s.views[0].fov, 100.0f, 30000.0f, a);
    // Move the eye: the sky does not change
    s.eye[0].view[12] += 500.0f;
    s.eye[0].view[13] -= 300.0f;
    vr_eye_sky_proj(&s.eye[0], s.views[0].fov, 100.0f, 30000.0f, b);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(a[i], b[i], 1e-6);
    }
}

static void test_billboard_faces_head(void) {
    scene_t s;
    make_scene(&s, 60.0f * DEG, 0.0f, 0.0f);
    vr_eye_cam_t head;
    vr_eye_from_pose(&s.anchor, &s.rig, &s.rc, &s.head, &s.head, &head);
    float bb[3][3];
    vr_billboard_rotation(&s.cam, &head, bb);
    // The sprite's +Z (row 2), taken to world through the camera, is the head's back
    float rot[9];
    for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) rot[c * 3 + r] = s.cam.view[c * 4 + r];
    float rt[9], z[3], w[3];
    vr_mat3_transpose(rot, rt);
    z[0] = bb[2][0]; z[1] = bb[2][1]; z[2] = bb[2][2];
    vr_mat3_mulv(rt, z, w);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(w[i], head.rotation[6 + i], 1e-4);
    }
    // Cylindrical: its up is the world's up
    vr_cylboard_rotation(&s.cam, &head, bb);
    z[0] = bb[1][0]; z[1] = bb[1][1]; z[2] = bb[1][2];
    vr_mat3_mulv(rt, z, w);
    CHECK_NEAR(w[0], 0.0, 1e-5);
    CHECK_NEAR(w[1], 1.0, 1e-5);
    CHECK_NEAR(w[2], 0.0, 1e-5);
}

static void test_projection_kinds(void) {
    scene_t s;
    make_scene(&s, 0, 0, 0);
    CHECK(vr_proj_is_perspective(s.game_proj));
    float ortho[16] = { 0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1 };
    CHECK(!vr_proj_is_perspective(ortho));
    float n, f;
    CHECK(!vr_game_proj_near_far(ortho, &n, &f));
}

// -----------------------------------------------------------------------------
// Comfort: the turn's orbit, camera_closer / camera_lower, height_offset

static float hdist3(const float a[3], const float b[3]) {
    return hypotf(a[0] - b[0], a[2] - b[2]);
}

static void eyes_for(scene_t *s, const vr_game_cam_t *shown) {
    vr_anchor_from_cam(shown, s->rig.level_pitch, &s->anchor);
    for (int e = 0; e < 2; e++) {
        vr_eye_from_pose(&s->anchor, &s->rig, &s->rc, &s->head, &s->views[e].pose, &s->eye[e]);
    }
}

static void test_orbit(void) {
    scene_t s;
    make_scene(&s, 0.2f, 0.1f, 0.0f);
    const float mario[3] = { -1300.0f, 160.0f, 2000.0f };
    vr_game_cam_t o;
    vr_game_cam_orbit(&s.cam, mario, 30.0f * DEG, &o);
    CHECK_NEAR(vr_game_cam_heading(&o) - vr_game_cam_heading(&s.cam), 30.0f * DEG, 1e-5);
    CHECK_NEAR(hdist3(o.position, mario), hdist3(s.cam.position, mario), 1e-2);
    CHECK_NEAR(o.position[1], s.cam.position[1], 1e-3);
    // A rigid camera: the view still inverts, its pitch is kept
    vr_game_cam_t check;
    CHECK(vr_game_cam_from_view(o.view, &check));
    CHECK_NEAR(o.back[1], s.cam.back[1], 1e-6);
    // Going on round: 12 x 30 degrees is where it started
    vr_game_cam_t r = s.cam;
    for (int k = 0; k < 12; k++) {
        vr_game_cam_orbit(&r, mario, 30.0f * DEG, &r);
    }
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(r.position[i], s.cam.position[i], 0.05);
        CHECK_NEAR(r.back[i], s.cam.back[i], 1e-4);
    }
    // 0 turns nothing, bit for bit
    vr_game_cam_orbit(&s.cam, mario, 0.0f, &o);
    CHECK(memcmp(&o, &s.cam, sizeof(o)) == 0);
    // Heading sense: positive turns left (as vr_yaw_of): a camera looking down
    // -Z ends up looking toward -X
    float from[3] = { 0, 100, 500 }, to[3] = { 0, 100, 0 }, view[16];
    game_lookat(from, to, view);
    vr_game_cam_t c;
    CHECK(vr_game_cam_from_view(view, &c));
    CHECK_NEAR(vr_game_cam_heading(&c), 0.0, 1e-6);
    vr_game_cam_orbit(&c, to, 90.0f * DEG, &o);
    CHECK_NEAR(-o.back[0], -1.0, 1e-5);
    CHECK_NEAR(o.position[0], 500.0, 1e-2); // circled to +X, looking back at the pivot
}

static void test_third_person(void) {
    const float e[3] = { 0.0f, 600.0f, 1000.0f }, a[3] = { 0.0f, 200.0f, 0.0f };
    float p[3];
    vr_third_person_origin(e, a, 0.0f, 0.5f, p);
    CHECK_NEAR(p[2], 500.0, 1e-3);
    CHECK_NEAR(p[1], 400.0, 1e-3);
    vr_third_person_origin(e, a, 1.0f, 0.0f, p);
    CHECK_NEAR(p[1], 200.0, 1e-3);
    CHECK_NEAR(p[2], 1000.0, 1e-3);
    // P.y = A.y + (1 - C)(1 - L)(E.y - A.y)
    vr_third_person_origin(e, a, 0.25f, 0.3f, p);
    CHECK_NEAR(p[1], 200.0f + 0.7f * 0.75f * 400.0f, 1e-3);
    CHECK_NEAR(p[2], 700.0, 1e-3);
    // Clamped: closer stops at 0.8, lower at 1
    vr_third_person_origin(e, a, 2.0f, 5.0f, p);
    CHECK_NEAR(p[2], 200.0, 1e-3);
    CHECK_NEAR(p[1], 200.0, 1e-3);

    // Negative values extend away from the native origin, including up.
    vr_third_person_origin(e, a, 0.0f, -1.0f, p);
    CHECK_NEAR(p[2], 2000.0, 1e-3);
    CHECK_NEAR(p[1], 1000.0, 1e-3);
    vr_third_person_origin(e, a, -1.0f, 0.0f, p);
    CHECK_NEAR(p[2], 1000.0, 1e-3);
    CHECK_NEAR(p[1], 1000.0, 1e-3);

    // The camera moved there keeps its heading; lowered, it looks up toward A
    float view[16];
    game_lookat(e, a, view);
    vr_game_cam_t c, t;
    CHECK(vr_game_cam_from_view(view, &c));
    vr_game_cam_third_person(&c, a, 0.5f, 0.3f, &t);
    vr_third_person_origin(e, a, 0.5f, 0.3f, p);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(t.position[i], p[i], 1e-3);
    }
    CHECK_NEAR(vr_game_cam_heading(&t), vr_game_cam_heading(&c), 1e-5);
    // It still looks straight at A (the camera looked at A before)
    float d[3] = { a[0] - p[0], a[1] - p[1], a[2] - p[2] };
    float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(-t.back[i], d[i] / l, 1e-4);
    }
    CHECK(fabsf(t.right[1]) < 1e-6f); // no roll
    // Closer only: the same direction, nearer
    vr_game_cam_third_person(&c, a, 0.0f, 0.5f, &t);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(t.back[i], c.back[i], 1e-5);
    }
    // Both 0: unchanged, bit for bit
    vr_game_cam_third_person(&c, a, 0.0f, 0.0f, &t);
    CHECK(memcmp(&t, &c, sizeof(t)) == 0);
}

static bool trace_halfway(const float source[3], const float proposed[3], float hit[3], void *context) {
    (void)context;
    for (int i = 0; i < 3; i++) hit[i] = source[i] + 0.5f * (proposed[i] - source[i]);
    return true;
}

static void test_third_person_los_clamps_only_shown_camera(void) {
    const float e[3] = { 0.0f, 600.0f, 1000.0f }, a[3] = { 0.0f, 200.0f, 0.0f };
    float requested_closer = -1.0f, requested_lower = -1.0f;
    float proposed[3], delta[3];
    vr_game_cam_t source, shown;
    float view[16];
    game_lookat(e, a, view);
    CHECK(vr_game_cam_from_view(view, &source));
    vr_game_cam_third_person(&source, a, requested_lower, requested_closer, &shown);
    memcpy(proposed, shown.position, sizeof(proposed));

    vr_game_cam_third_person_constrained(&source, a, requested_lower, requested_closer,
        trace_halfway, NULL, 5.0f, &shown);
    float length2 = 0.0f;
    for (int i = 0; i < 3; i++) {
        delta[i] = proposed[i] - source.position[i];
        length2 += delta[i] * delta[i];
    }
    float safe_t = 0.5f - 5.0f / sqrtf(length2);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(shown.position[i], source.position[i] + safe_t * delta[i], 1e-3);
    }
    CHECK_NEAR(requested_closer, -1.0, 0.0); // LOS limits the shown camera, not settings
    CHECK_NEAR(requested_lower, -1.0, 0.0);
    CHECK_NEAR(vr_game_cam_heading(&shown), vr_game_cam_heading(&source), 1e-5);
}

// The rendering contract holds for an anchor turned, nearer and lower: the
// game's baked camera is still the one undone, the shown one places the eyes
static void test_contract_with_comfort(void) {
    scene_t s;
    make_scene(&s, 0.6f, -0.2f, 2.0f * DEG);
    const float mario[3] = { -1300.0f, 160.0f, 2000.0f }, at[3] = { -1300.0f, 300.0f, 2000.0f };
    vr_game_cam_t shown;
    vr_game_cam_orbit(&s.cam, mario, -45.0f * DEG, &shown);
    vr_game_cam_third_person(&shown, at, 0.4f, 0.5f, &shown);
    s.rig.height_offset = 0.25f;
    eyes_for(&s, &shown);
    // check_contract's LOCAL -> world mapping has no height offset: add it to the anchor
    s.anchor.position[1] += 0.25f * s.rig.world_scale;
    check_contract(&s);
}

static void test_height_and_ipd(void) {
    scene_t s;
    make_scene(&s, 0.3f, 0.0f, 0.0f);
    vr_eye_cam_t base[2];
    memcpy(base, s.eye, sizeof(base));
    s.rig.height_offset = 0.3f;
    eyes_for(&s, &s.cam);
    for (int e = 0; e < 2; e++) {
        CHECK_NEAR(s.eye[e].position[1] - base[e].position[1], 0.3 * 104.0, 1e-3);
        CHECK_NEAR(s.eye[e].position[0], base[e].position[0], 1e-6);
        CHECK(memcmp(s.eye[e].rotation, base[e].rotation, sizeof(base[e].rotation)) == 0);
    }
    // The eye separation is the runtime's IPD times world_scale, whatever the
    // turn, closer, lower and height do
    const float mario[3] = { -1300.0f, 160.0f, 2000.0f };
    const float scales[3] = { 20.0f, 104.0f, 400.0f };
    for (int k = 0; k < 3; k++) {
        s.rig.world_scale = scales[k];
        vr_game_cam_t shown;
        vr_game_cam_orbit(&s.cam, mario, 1.0f, &shown);
        vr_game_cam_third_person(&shown, mario, 0.7f, 0.6f, &shown);
        eyes_for(&s, &shown);
        float d[3];
        for (int i = 0; i < 3; i++) d[i] = s.eye[1].position[i] - s.eye[0].position[i];
        CHECK_NEAR(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 0.064 * scales[k], 1e-2 * scales[k] / 104.0);
    }
}

// Every comfort setting at its default leaves the rig bit for bit as it was
static void test_default_parity(void) {
    scene_t s;
    make_scene(&s, 0.7f, -0.3f, 1.0f * DEG);
    vr_eye_cam_t before[2];
    memcpy(before, s.eye, sizeof(before));
    vr_anchor_t anchor_before = s.anchor;
    const float mario[3] = { -1300.0f, 160.0f, 2000.0f }, at[3] = { -1300.0f, 300.0f, 2000.0f };
    vr_game_cam_t shown;
    vr_game_cam_orbit(&s.cam, mario, 0.0f, &shown); // nothing queued
    vr_game_cam_third_person(&shown, at, 0.0f, 0.0f, &shown); // closer 0, lower 0
    s.rig.height_offset = 0.0f;
    eyes_for(&s, &shown);
    CHECK(memcmp(&s.anchor, &anchor_before, sizeof(anchor_before)) == 0);
    CHECK(memcmp(s.eye, before, sizeof(before)) == 0);
}

int main(void) {
    RUN(test_contract_level);
    RUN(test_contract_turned_pitched_canted);
    RUN(test_eye_separation_is_ipd_once);
    RUN(test_disparity_sign);
    RUN(test_level_anchor);
    RUN(test_recentred_head_sits_at_camera);
    RUN(test_sky_has_no_translation);
    RUN(test_billboard_faces_head);
    RUN(test_projection_kinds);
    RUN(test_orbit);
    RUN(test_third_person);
    RUN(test_third_person_los_clamps_only_shown_camera);
    RUN(test_contract_with_comfort);
    RUN(test_height_and_ipd);
    RUN(test_default_parity);
    return SFVR_TEST_RESULT("test_vr_cam");
}
