#ifndef CINEMA_GAME_PATH_H
#define CINEMA_GAME_PATH_H

#include "pc/gfx/gfx_vr.h"

// Production interpolation and Fast3D matrix/viewport consumers, driven with
// an empty scene. No copied projection formula or mock projection interception.
bool cinema_game_projection(float previous_fov, float fov, float aspect, float delta, float out[16]);
void cinema_fast3d_load(const struct GfxVrPass *pass, const float source[16], const Vp *viewport,
    float projection[16], float rect[4], struct GfxVrPassStats *stats);

#endif
