// Include these translation units to reach their retained interpolation state
// and matrix/viewport consumers without adding test hooks to production code.
// The rest of each engine is discarded by --gc-sections.
#include "src/game/rendering_graph_node.c"

// The host GL producer supplies the pass's draws; retain the real Fast3D matrix
// and viewport consumers under separate names, without replacing their code.
#define gfx_vr_run_pass cinema_unused_run_pass
#define gfx_vr_pass_stats cinema_unused_pass_stats
#define gfx_vr_restore_dimensions cinema_unused_restore_dimensions
#include "src/pc/gfx/gfx_pc.c"
#undef gfx_vr_run_pass
#undef gfx_vr_pass_stats
#undef gfx_vr_restore_dimensions

#include "cinema_game_path.h"

unsigned int configDrawDistance, configInterpolationMode;
u32 gGlobalTimer;
Gfx *gDisplayListHead;
struct LakituState gLakituState;
u8 gInterpolatingSurfaces;

bool get_first_person_enabled(void) { return false; }

// Empty-scene branches must never silently stand in for untested game draws.
void make_viewport_clip_rect(Vp *viewport) { (void)viewport; abort(); }
Gfx *create_shadow_below_xyz(f32 x, f32 y, f32 z, s16 scale, u8 solidity, s8 type) {
    (void)x; (void)y; (void)z; (void)scale; (void)solidity; (void)type;
    abort();
}

bool cinema_game_projection(float previous_fov, float fov, float aspect, float delta, float out[16]) {
    (void)LuaActionHookTypeArgName; // upstream header table, unused by this empty scene
    bool camera_valid = gVrInterpCamValid;
    struct GraphNodePerspective node = {0};
    struct GrowingArray empty = {0};
    Mtx matrix;
    Gfx command;
    node.fnNode.node.type = GRAPH_NODE_TYPE_PERSPECTIVE;
    node.prevFov = previous_fov;
    node.fov = fov;
    node.near = 100;
    node.far = 20000;
    sPerspectiveNode = &node;
    sPerspectiveMtx = &matrix;
    sPerspectivePos = &command;
    sPerspectiveAspect = aspect;
    sShadowInterp = sMtxTbl = &empty;
    patch_mtx_interpolated(delta);
    gVrInterpCamValid = camera_valid;
    bool valid = command.words.w1 == (uintptr_t)&matrix;
    memcpy(out, (const void *)command.words.w1, 16 * sizeof(float));
    sPerspectiveNode = NULL;
    sPerspectiveMtx = NULL;
    sPerspectivePos = NULL;
    sShadowInterp = sMtxTbl = NULL;
    return valid;
}

void cinema_fast3d_load(const struct GfxVrPass *pass, const float source[16], const Vp *viewport,
    float projection[16], float rect[4], struct GfxVrPassStats *stats) {
    sVrPass = *pass;
    sVrSeg = GFX_VR_SEG_WORLD;
    sVrWorldProj = false;
    memset(&sVrStats, 0, sizeof(sVrStats));
    gfx_vr_set_dimensions(pass->width, pass->height, pass->kind == GFX_VR_PASS_EYE);
    gfx_sp_reset();
    mtxf_identity(rsp.modelview_matrix_stack[0]);
    gfx_calc_and_set_viewport(&viewport->vp);
    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH, (const int32_t *)source);
    memcpy(projection, rsp.P_matrix, 16 * sizeof(float));
    rect[0] = rdp.viewport.x;
    rect[1] = rdp.viewport.y;
    rect[2] = rdp.viewport.width;
    rect[3] = rdp.viewport.height;
    *stats = sVrStats;
    sVrPass.kind = GFX_VR_PASS_FLAT;
}
