#ifndef VR_PATHS_H
#define VR_PATHS_H

#include <stdbool.h>
#include <stddef.h>

#define VR_DATA_DIR_NAME "SM64VR"

// $XDG_DATA_HOME/SM64VR/ with a trailing slash. XDG_DATA_HOME must be an
// absolute path, else (or when unset) it is $HOME/.local/share. Returns false
// when neither is usable or the result does not fit.
bool vr_paths_data_dir(const char *xdg_data_home, const char *home, char *out, size_t n);

// mkdir -p. True when the directory exists afterwards.
bool vr_paths_make_dir(const char *path);

// The data dir for this process, from the environment, created if missing.
// The ROM, saves, the game's config and vr.toml all live in it directly: it is
// the game's user path in the VR build. Returns "" when no directory could be determined.
const char *vr_paths_runtime_data_dir(void);

#endif
