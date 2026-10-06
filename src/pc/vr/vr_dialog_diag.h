#ifndef VR_DIALOG_DIAG_H
#define VR_DIALOG_DIAG_H

#ifdef SM64_VR
struct Camera;
// Config/env-only, one-shot reproduction through the real dialog camera/text
// path. Called once per simulation update, never from an eye replay.
void vr_dialog_diag_tick(struct Camera *camera);
#endif

#endif
