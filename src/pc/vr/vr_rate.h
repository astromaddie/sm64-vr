#ifndef VR_RATE_H
#define VR_RATE_H

#include <stdbool.h>

// Is the display rate held? (standard.md section 9) While the headset is worn, two windows of
// 10 s in a row in which the frame rate stays below 90% of the display's rate mean it isn't;
// that is reported once per run, with a recommendation to lower the title's per-app rate in
// SteamVR (the app can't change the rate itself). Pure bookkeeping; vr.c feeds it frames.

#define VR_RATE_WINDOW_S 10.0
#define VR_RATE_LOW_FRACTION 0.9
#define VR_RATE_LOW_WINDOWS 2

typedef struct {
	int target_hz; // the rate the display should be running at; 0 = nothing to watch
	bool started; // a window is open
	double start_s;
	unsigned frames;
	int low_windows; // consecutive windows below the threshold
	bool reported; // VR_RATE_NOT_HELD was returned once; it never is again
	double last_fps; // of the window that closed last
} vr_rate_t;

typedef enum {
	VR_RATE_NONE = 0, // the window is still open
	VR_RATE_WINDOW_OK, // a window closed at or above the threshold
	VR_RATE_WINDOW_LOW, // a window closed below it; one more such window is allowed
	VR_RATE_NOT_HELD // the second low window in a row, the first time: say so once
} vr_rate_result_t;

void vr_rate_init(vr_rate_t *r);

// The display rate changed (a request, or the runtime's change event): start over.
void vr_rate_set_target(vr_rate_t *r, int target_hz);

// One frame shown. `counting` is false when the frame does not count (headset not worn,
// standby, a loading hitch the caller wants to skip): the open window is dropped and the
// run of low windows ends. now_s is a monotonic clock in seconds.
vr_rate_result_t vr_rate_frame(vr_rate_t *r, double now_s, bool counting);

#endif
