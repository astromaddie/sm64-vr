#ifndef VR_RUNTIME_API_H
#define VR_RUNTIME_API_H

#include <stdbool.h>

// Lightweight cross-module calls used by the input and renderer paths. Keep
// this header free of game graphics types so host tests can exercise input.
double vr_setting_get(const char *key);
bool vr_setting_set(const char *key, double value);
bool vr_render_adjustment_allowed(void);

#endif
