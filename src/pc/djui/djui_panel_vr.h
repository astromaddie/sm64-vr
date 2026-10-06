#pragma once
#include "djui.h"

#ifdef SM64_VR
// The in-headset VR settings page (docs/VR_ARCHITECTURE.md section 10): a list of tabs,
// each a panel of the entries in src/pc/vr/vr_menu.c. Created from the pause menu.
void djui_panel_vr_create(struct DjuiBase* caller);

// diag_settings_preview (src/pc/vr/vr_preview.h): once per game tick, from the VR
// controller's read; adds the Start presses of the sequence to `buttons`
void djui_panel_vr_diag_tick(u16* buttons);
#endif
