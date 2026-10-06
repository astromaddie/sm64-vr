#ifndef GFX_VR_H
#define GFX_VR_H

// The Fast3D side of the stereo renderer (SM64_VR builds only;
// docs/VR_ARCHITECTURE.md sections 3 and 4).
//
// The game builds one display list per 30 Hz tick. The VR renderer replays it
// several times per XR frame: once per eye, twice for the HUD (over black and
// over white) or once for the mono panel. Markers in the display list say which
// segment a command belongs to; each pass draws only the segments it wants and
// still runs every state command, so no pass depends on another.

#ifdef SM64_VR

#include <stdbool.h>
#include <stdint.h>
#include <PR/gbi.h>

// A display-list marker: an extension opcode with the segment in w1.
#define G_VR_MARKER 0x14

enum GfxVrSegment {
    GFX_VR_SEG_SETUP,    // before the first marker: frame setup, loading screens
    GFX_VR_SEG_WORLD,    // the area's 3D scene (geo_process_root)
    GFX_VR_SEG_SKY_FLAT, // the game's screen-space skybox tiles
    GFX_VR_SEG_SKY_VR,   // the sky sphere at infinity (skybox.c, VR only)
    GFX_VR_SEG_HUD,      // everything 2D after the scene: HUD, dialogs, pause, DJUI
    GFX_VR_SEG_TRANSITION, // the flat warp transition (iris, fade); stereo shows a fade quad instead
    GFX_VR_SEG_COUNT
};

#define gSPVrMarker(pkt, seg)                                  \
{                                                              \
    Gfx *_g = (Gfx *)(pkt);                                    \
    _g->words.w0 = _SHIFTL(G_VR_MARKER, 24, 8);                \
    _g->words.w1 = (uintptr_t)(seg);                           \
}

enum GfxVrPassKind {
    GFX_VR_PASS_FLAT,  // not a VR pass: everything, as the flat game
    GFX_VR_PASS_EYE,   // world and VR sky, with the eye's projection
    GFX_VR_PASS_HUD,   // the 2D after the scene only
    GFX_VR_PASS_PANEL  // the whole frame as the flat game draws it (mono panel)
};

struct GfxVrPass {
    enum GfxVrPassKind kind;
    uint32_t width, height; // the target; viewports and scissors scale to it
    bool cinema; // EYE: the world fills the fixed 16:9 picture, independent of game viewport overrides
    // EYE: turns the game's world projection (a perspective, Mat4 memory) into
    // the eye's replacement. Called once per world projection load.
    void (*world_proj)(const float game_proj[16], float out[16], void *user);
    void *user;
    float sky_proj[16]; // EYE: the projection of the VR sky segment (Mat4 memory)
    const float *sky_proj_live; // optional: refreshed by world_proj for interpolated cinema FOVs
};

struct GfxVrPassStats {
    uint32_t tris_drawn;
    uint32_t tris_dropped;
    uint32_t world_proj_loads; // world projections replaced (EYE)
    uint32_t markers;
    bool saw_world;
};

// Replays `commands` as one pass into whatever framebuffer is bound. The
// caller binds and clears the target first. Window and swap are not touched.
void gfx_vr_run_pass(Gfx *commands, const struct GfxVrPass *pass);
const struct GfxVrPassStats *gfx_vr_pass_stats(void);

// GL state the VR renderer changed behind Fast3D's back (program, blend,
// depth, viewport): re-apply what Fast3D believes before its next draw.
void gfx_vr_resync_state(void);

// The window's dimensions again (after passes that used other target sizes).
void gfx_vr_restore_dimensions(void);

#endif // SM64_VR

#endif
