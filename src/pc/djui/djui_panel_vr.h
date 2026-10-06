#pragma once
#include "djui.h"

#ifdef SM64_VR
// The in-headset VR settings page (docs/VR_ARCHITECTURE.md section 10): a list of tabs,
// each a panel of the entries in src/pc/vr/vr_menu.c. Created from the pause menu.
void djui_panel_vr_create(struct DjuiBase* caller);
#endif
