#ifdef SM64_VR
#include "sm64.h"
#include "game/camera.h"
#include "game/area.h"
#include "game/ingame_menu.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/hardcoded.h"
#include "pc/djui/djui.h"
#include "vr.h"
#include "vr_dialog_diag.h"
#include "vr_game.h"
#include "vr_log.h"

extern u8 sObjectCutscene; // camera.c's pending object request; do not displace one

void vr_dialog_diag_tick(struct Camera *camera) {
    static bool started, active;
    static unsigned warmup, remaining;
    static s16 previous_mode;
    static struct Camera *owner;
    static struct Object *previous_focus;
    double seconds = vr_setting_get("diag_dialog_cutscene_seconds");
    if (active) {
        bool completed = camera == owner && camera->cutscene == 0 && gCutsceneFocus == gMarioObject;
        if (completed || (camera == owner && camera->cutscene == CUTSCENE_DIALOG
            && (seconds <= 0 || --remaining == 0))) {
            reset_dialog_render_state();
            camera->cutscene = 0;
            camera->mode = previous_mode;
            gLakituState.mode = previous_mode;
            gCutsceneFocus = previous_focus;
            clear_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_DIALOG);
            active = false;
            vr_logf("dialog diagnostic: finished; restored camera mode=%u", camera->mode);
        } else if (camera != owner || camera->cutscene != CUTSCENE_DIALOG) {
            active = false; // a warp/another real cutscene takes ownership
            vr_logf("dialog diagnostic: interrupted; no camera state restored");
        } else {
            // Course triggers may select another mode before play_cutscene.
            // Keep the diagnostic's real boss branch active for its duration.
            camera->mode = gLakituState.mode = CAMERA_MODE_BOSS_FIGHT;
        }
        return;
    }
    if (started || seconds <= 0 || !gVrActive) { return; }
    if (gDjuiInMainMenu || gWarpTransition.isActive || !gMarioObject
        || camera->cutscene != 0 || sObjectCutscene != 0 || get_dialog_id() != DIALOG_NONE) {
        warmup = 0;
        return;
    }
    if (++warmup < 30) { return; } // a full second of settled live gameplay
    started = active = true;
    remaining = (unsigned)(seconds * 30.0);
    if (remaining < 1) { remaining = 1; }
    owner = camera;
    previous_mode = camera->mode;
    previous_focus = gCutsceneFocus;
    // Keep the current scene and camera pose. Mario is a safe speaking-object
    // stand-in; the camera runs the actual boss-mode CUTSCENE_DIALOG branch
    // and the authored King Whomp dialogue, with no spawn/warp/save edits.
    camera->mode = gLakituState.mode = CAMERA_MODE_BOSS_FIGHT;
    cutscene_object_with_dialog(CUTSCENE_DIALOG, gMarioObject, gBehaviorValues.dialogs.KingWhompDialog);
    start_cutscene(camera, get_cutscene_from_mario_status(camera));
    vr_logf("dialog diagnostic: armed for %.2f s cutscene=%u camera_mode=%u (Mario focus, King Whomp text)",
        seconds, CUTSCENE_DIALOG, CAMERA_MODE_BOSS_FIGHT);
}
#endif
