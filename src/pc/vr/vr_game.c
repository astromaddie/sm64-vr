#include "vr_game.h"

struct VrTickInfo gVrTick;
bool gVrActive = false;
bool gVrBillboardHead = false;
float gVrBillboardRot[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
float gVrCylboardRot[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
float gVrInterpCamView[16];
bool gVrInterpCamValid = false;
