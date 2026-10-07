#include <math.h>
#include <string.h>

#include "vr_preview.h"
#include "vr_math.h"

#define DEG (3.14159265358979323846f / 180.0f)

void vr_preview_reset(vr_preview_t *p) {
	memset(p, 0, sizeof(*p));
}

float vr_preview_page_half(float hud_width, float distance) {
	if (!(hud_width > 0.0f) || !(distance > 0.0f)) {
		return 0.0f;
	}
	return atanf(0.5f * VR_PREVIEW_PAGE_FRACTION * hud_width / distance);
}

float vr_preview_menu_target(vr_preview_side_t side, float page_half) {
	float yaw = VR_PREVIEW_MARGIN_DEG * DEG + page_half;
	switch (side) {
		case VR_PREVIEW_SIDE_LEFT: return yaw;
		case VR_PREVIEW_SIDE_RIGHT: return -yaw;
		default: return 0.0f;
	}
}

void vr_preview_menu_pose(const vr_pose_t *head, float yaw, float distance, vr_pose_t *out) {
	vr_pose_t turned = *head;
	if (yaw != 0.0f) {
		float yq[4];
		vr_quat_from_yaw(yaw, yq);
		vr_quat_mul(yq, head->orientation, turned.orientation);
		vr_quat_normalize(turned.orientation);
	}
	vr_hud_pose(&turned, distance, out);
}

vr_preview_out_t vr_preview_frame(vr_preview_t *p, const vr_preview_in_t *in) {
	vr_preview_out_t out;
	memset(&out, 0, sizeof(out));
	if (p->valid) {
		out.page_changed = in->page_open != p->page_open;
		out.overview_changed = in->overview != p->overview;
	}
	// The overview is a camera 6000 units out over the area: going to it or back is a
	// jump of the whole world, so it is faded like a mode switch. Outside stereo the
	// mode switch has its own fade.
	out.fade = in->stereo && out.overview_changed;
	p->valid = true;
	p->page_open = in->page_open;
	p->overview = in->overview;

	if (!in->menu_up || !in->stereo) {
		// The menu is gone (or on the mono panel): the gameplay HUD follows the head, unturned
		p->menu_yaw = 0.0f;
	} else {
		float target = in->page_open ? vr_preview_menu_target(in->side, in->page_half) : 0.0f;
		float step = VR_PREVIEW_MENU_RATE_DPS * DEG * (in->dt > 0.0f ? in->dt : 0.0f);
		float d = target - p->menu_yaw;
		if (out.fade || fabsf(d) <= step) {
			p->menu_yaw = target; // under the fade's black nothing needs to move
		} else {
			p->menu_yaw += d > 0.0f ? step : -step;
		}
	}
	out.menu_yaw = p->menu_yaw;
	return out;
}

static const struct {
	float at;
	const char *name;
	const char *dump;
} diag_steps[] = {
	[VR_PREVIEW_DIAG_NONE] = { 0.0f, "none", NULL },
	[VR_PREVIEW_DIAG_PAUSE] = { 0.0f, "pause (Start)", NULL },
	[VR_PREVIEW_DIAG_PAUSE_MENU] = { 1.5f, "pause menu (R)", NULL },
	[VR_PREVIEW_DIAG_DUMP_PAUSED] = { 3.0f, "dump the pause screen", "paused-" },
	[VR_PREVIEW_DIAG_OPEN_PAGE] = { 4.0f, "open the VR settings page, Room tab", NULL },
	[VR_PREVIEW_DIAG_DUMP_PAGE] = { 6.0f, "dump the page", "page-" },
	[VR_PREVIEW_DIAG_STEP] = { 7.0f, "step world_scale and camera_closer", NULL },
	[VR_PREVIEW_DIAG_DUMP_STEP] = { 8.0f, "dump the stepped view", "step-" },
	[VR_PREVIEW_DIAG_RESTORE] = { 9.0f, "put the settings back", NULL },
	[VR_PREVIEW_DIAG_CLOSE] = { 10.0f, "close the menus", NULL },
	[VR_PREVIEW_DIAG_RESUME] = { 10.5f, "resume (Start)", NULL },
	[VR_PREVIEW_DIAG_DUMP_RESUMED] = { 12.0f, "dump the resumed game", "resumed-" },
	[VR_PREVIEW_DIAG_DONE] = { 12.0f, "done", NULL },
};

vr_preview_diag_step_t vr_preview_diag_tick(vr_preview_diag_t *d, float delay_s, bool gameplay, float dt) {
	if (!(delay_s > 0.0f) || d->next > VR_PREVIEW_DIAG_DONE) {
		return VR_PREVIEW_DIAG_NONE;
	}
	if (!d->started) {
		if (!gameplay) {
			return VR_PREVIEW_DIAG_NONE;
		}
		d->gameplay_s += dt;
		if (d->gameplay_s < delay_s) {
			return VR_PREVIEW_DIAG_NONE;
		}
		// The sequence's own clock: paused and menu ticks count from here on
		d->started = true;
		d->gameplay_s = 0.0f;
		d->next = VR_PREVIEW_DIAG_PAUSE + 1;
		return VR_PREVIEW_DIAG_PAUSE;
	}
	d->gameplay_s += dt;
	if (d->gameplay_s + 1e-4f >= diag_steps[d->next].at) {
		return (vr_preview_diag_step_t)d->next++;
	}
	return VR_PREVIEW_DIAG_NONE;
}

const char *vr_preview_diag_name(vr_preview_diag_step_t step) {
	return (int)step >= 0 && step <= VR_PREVIEW_DIAG_DONE ? diag_steps[step].name : "";
}

const char *vr_preview_diag_dump_tag(vr_preview_diag_step_t step) {
	return (int)step >= 0 && step <= VR_PREVIEW_DIAG_DONE ? diag_steps[step].dump : NULL;
}
