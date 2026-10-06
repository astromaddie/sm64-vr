#ifndef VR_CINEMA_H
#define VR_CINEMA_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_cam.h"

// SM64 camera ids, kept independent of game headers for host tests. camera.c
// asserts these against camera.h when publishing the display list's facts.
#define VR_CINEMA_CUTSCENES(X) \
    X(DOOR_PULL, 130, false, "short local door action") \
    X(DOOR_PUSH, 131, false, "short local door action") \
    X(ENTER_CANNON, 133, true, "scripted swoop into the cannon") \
    X(ENTER_PAINTING, 134, false, "short warp covered by the warp fade") \
    X(DEATH_EXIT, 135, false, "short respawn covered by the warp fade") \
    X(DOOR_WARP, 139, false, "short door warp covered by the warp fade") \
    X(DOOR_PULL_MODE, 140, false, "short local door action") \
    X(DOOR_PUSH_MODE, 141, false, "short local door action") \
    X(INTRO_PEACH, 142, true, "Peach letter and Lakitu camera tour") \
    X(DANCE_ROTATE, 143, true, "reward camera orbits Mario") \
    X(ENTER_BOWSER_ARENA, 144, true, "boss introduction and dialogue") \
    X(0F_UNUSED, 145, true, "unused scripted camera; safe cinema fallback") \
    X(UNUSED_EXIT, 147, true, "unused scripted camera; safe cinema fallback") \
    X(SLIDING_DOORS_OPEN, 149, false, "short local door action") \
    X(PREPARE_CANNON, 150, true, "remote cannon reveal") \
    X(UNLOCK_KEY_DOOR, 151, false, "short local key-door action") \
    X(STANDING_DEATH, 152, false, "local death feedback") \
    X(DEATH_ON_STOMACH, 153, false, "local death feedback") \
    X(DEATH_ON_BACK, 154, false, "local death feedback") \
    X(QUICKSAND_DEATH, 155, false, "local death feedback") \
    X(SUFFOCATION_DEATH, 156, false, "local death feedback") \
    X(EXIT_BOWSER_SUCC, 157, false, "short exit warp; key dance is separate") \
    X(EXIT_BOWSER_DEATH, 158, false, "unused short exit warp") \
    X(WATER_DEATH, 159, false, "local death; absent from play_cutscene switch") \
    X(EXIT_PAINTING_SUCC, 160, false, "short exit warp; star dance is separate") \
    X(CAP_SWITCH_PRESS, 161, true, "scripted switch reveal and dialogue") \
    X(DIALOG, 162, true, "scripted focus on a speaking object") \
    X(RACE_DIALOG, 163, true, "scripted race dialogue focus") \
    X(ENTER_PYRAMID_TOP, 164, true, "scripted pyramid entrance camera") \
    X(DANCE_FLY_AWAY, 165, true, "reward camera flies away") \
    X(DANCE_CLOSEUP, 166, true, "reward camera close-up") \
    X(KEY_DANCE, 167, true, "Bowser key-grab reward camera") \
    X(SSL_PYRAMID_EXPLODE, 168, true, "unused remote pyramid reveal") \
    X(EXIT_SPECIAL_SUCC, 169, false, "short exit warp") \
    X(NONPAINTING_DEATH, 170, false, "short respawn warp") \
    X(READ_MESSAGE, 171, true, "scripted sign or message focus") \
    X(ENDING, 172, true, "scripted ending camera sequence") \
    X(STAR_SPAWN, 173, true, "remote star reveal") \
    X(GRAND_STAR, 174, true, "final Bowser defeat and flight") \
    X(DANCE_DEFAULT, 175, true, "reward camera orbits Mario") \
    X(RED_COIN_STAR_SPAWN, 176, true, "remote star reveal") \
    X(END_WAVING, 177, true, "scripted ending tableau") \
    X(CREDITS, 178, true, "scripted credits camera tours") \
    X(EXIT_WATERFALL, 179, false, "short warp landing feedback") \
    X(EXIT_FALL_WMOTR, 180, false, "short warp landing feedback") \
    X(ENTER_POOL, 181, false, "short local pool warp") \
    X(PALETTE_EDITOR, 182, true, "scripted character preview camera")

enum {
#define VR_CINEMA_ID(name, id, cinema, why) VR_CS_##name = id,
    VR_CINEMA_CUTSCENES(VR_CINEMA_ID)
#undef VR_CINEMA_ID
};

bool vr_cinema_cutscene(unsigned id); // unknown nonzero ids default to cinema
const char *vr_cinema_cutscene_reason(unsigned id);

// Display-time hysteresis, independent of refresh rate. Entry hides the brief
// qualification period with black; exit survives short gaps between shots.
#define VR_CINEMA_ENTER_NS 50000000LL
#define VR_CINEMA_EXIT_NS 400000000LL
typedef struct {
    bool active, pending, pending_valid;
    int64_t pending_since_ns;
} vr_cinema_latch_t;
bool vr_cinema_latch(vr_cinema_latch_t *s, bool want, bool hold, int64_t now_ns);

typedef struct {
    vr_pose_t pose; // yaw-only, LOCAL space, fixed until entry/recentre
    float width, height; // the 16:9 picture, metres
} vr_cinema_screen_t;

void vr_cinema_place(const vr_pose_t *head, float distance, float width, vr_cinema_screen_t *out);
// Same screen orientation, each tracked eye position, atan2 of screen corners.
// False if an eye has walked through the screen: show black instead.
bool vr_cinema_views(const vr_cinema_screen_t *screen, const vr_view_t eyes[2], vr_view_t out[2]);
// The complete authored HUD image in a black band below the picture. Preserves
// its 16:9 aspect, including large letter/dialogue boxes; no cropping or roll.
void vr_cinema_band(const vr_cinema_screen_t *screen, vr_pose_t *pose, float *width, float *height);

// Virtual parallel camera pair. The near clip is the zero-parallax plane;
// everything surviving clipping lies behind the screen. Infinity disparity
// on the screen is at most 60% of the runtime IPD, avoiding divergent eyes.
// No tracked head heading, comfort turn offset or world_scale enters this rig.
// Preserves the interpolated vertical FOV/lens centre and depth mapping while
// adapting horizontal scale to the fixed 16:9 picture, regardless of source aspect.
bool vr_cinema_game_eye(const vr_game_cam_t *cam, const float game_proj[16],
    float ipd, float screen_width, int eye, vr_eye_cam_t *out, float fov[4], float projection[16]);
// `projection` may be NULL. Otherwise it is the Fast3D replacement projection:
// off-axis game projection * camera-space eye shift. Cancelling the identical
// camera rotations analytically avoids roundoff at large world coordinates.

#endif
