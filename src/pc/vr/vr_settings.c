// fileno and fsync for the atomic save
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vr_settings.h"
#include "vr_log.h"
#include "vr_paths.h"

static const char *const third_person_move_values[] = { "face_move", NULL };

// Keys that only this port has. The standard keys live in the sfvr registry.
static const sfvr_setting_key port_keys[] = {
	// Game units per metre. Mario is 161 units tall; at 104 he is 1.55 m, life
	// size (docs/VR_ARCHITECTURE.md section 4). Larger values make a diorama
	// (400: Mario 0.4 m), smaller ones a giant's world (20: Mario 8 m). Live.
	{ "world_scale", SFVR_SETTING_FLOAT, 104.0, 20.0, 400.0, NULL },
	// The third-person head origin (after DK64): closer moves it along the line
	// from the game camera to its look-at point (0..0.8 of the way), lower then
	// moves the height left toward the look-at point's (0..1). Live.
	{ "camera_closer", SFVR_SETTING_FLOAT, 0.0, 0.0, 0.8, NULL },
	{ "camera_lower", SFVR_SETTING_FLOAT, 0.0, 0.0, 1.0, NULL },
	// standard.md section 5: Mario turns to walk where the stick points,
	// relative to the view. SM64 has no aiming, so face_camera does not apply.
	{ "third_person_move", SFVR_SETTING_ENUM, 0.0, 0.0, 0.0, third_person_move_values },
	// Diagnostic: right stick X held at this deflection for 4 s, released for
	// 4 s, in stereo play (a snap a hold, or the smooth turn), with [vr-cam]
	// lines of the shown and the game camera's heading. 0 = off.
	{ "diag_turn_stick", SFVR_SETTING_FLOAT, 0.0, -1.0, 1.0, NULL },
	// The anchor drops the game camera's pitch: looking level in the headset is
	// level in the game, and the head supplies pitch.
	{ "level_pitch", SFVR_SETTING_BOOL, 1.0, 0.0, 1.0, NULL },
	// Game units per metre on the star select screen. Its camera sits about
	// 1300 units from the star row: at 300 the row reads as a diorama about
	// 4 m away rather than 1.5 m stars 12 m away at life size.
	{ "star_select_scale", SFVR_SETTING_FLOAT, 300.0, 10.0, 2000.0, NULL },
	// Show the whole frame on the mono panel, even in gameplay.
	{ "force_mono", SFVR_SETTING_BOOL, 0.0, 0.0, 1.0, NULL },
	// Scripted cameras: stereo depth inside a world-locked screen by default.
	{ "cutscenes", SFVR_SETTING_INT, 0.0, 0.0, 1.0, NULL },
	{ "cinema_distance", SFVR_SETTING_FLOAT, 3.0, 1.0, 10.0, NULL },
	{ "cinema_width", SFVR_SETTING_FLOAT, 3.6, 0.5, 10.0, NULL },
	// Starts on the first available world camera (including the main-menu
	// background), using predicted display time. 0 disables the diagnostic.
	{ "diag_cinema_seconds", SFVR_SETTING_FLOAT, 0.0, 0.0, 120.0, NULL },
	// An inverted view fov from the runtime (vr_xr.h): 0 = as given,
	// 1 = negate all four angles, 2 = swap left/right and up/down,
	// 3 = always the Frame's full fov measured by the earlier ports (default;
	// worn 2026-10-03: no black border).
	{ "fov_mode", SFVR_SETTING_INT, 3.0, 0.0, 3.0, NULL },
	// Diagnostic: render the main menu's background level in stereo (with the
	// menu on the HUD quad), for unattended eye dumps without input.
	{ "diag_menu_stereo", SFVR_SETTING_BOOL, 0.0, 0.0, 1.0, NULL },
	// Diagnostic: count the 10 s windows of the refresh-rate check without a wearer
	// (unattended runs), so the check can be watched on the device.
	{ "diag_rate_unworn", SFVR_SETTING_BOOL, 0.0, 0.0, 1.0, NULL },
	// Diagnostic: dump the Nth stereo frame rendered from tracked views (a worn
	// headset), as dump_frame does for any frame. 0 = off.
	{ "dump_worn_frame", SFVR_SETTING_INT, 0.0, 0.0, 1000000.0, NULL },
	// The flat panel for menus and transitions: a world-locked quad.
	{ "panel_distance", SFVR_SETTING_FLOAT, 2.0, 0.8, 20.0, NULL },
	{ "panel_width", SFVR_SETTING_FLOAT, 2.2, 0.5, 10.0, NULL }
};
#define PORT_KEY_COUNT (sizeof(port_keys) / sizeof(port_keys[0]))

#define ENV_PREFIX "SM64"


size_t vr_settings_count(void) {
	return sfvr_settings_count() + PORT_KEY_COUNT;
}

const sfvr_setting_key *vr_settings_key(size_t index) {
	size_t standard = sfvr_settings_count();
	if (index < standard) {
		return sfvr_settings_at(index);
	}
	index -= standard;
	return index < PORT_KEY_COUNT ? &port_keys[index] : NULL;
}

int vr_settings_index(const char *name) {
	if (!name) {
		return -1;
	}
	for (size_t i = 0; i < vr_settings_count(); i++) {
		if (strcmp(vr_settings_key(i)->name, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

void vr_settings_defaults(vr_settings_t *s) {
	for (size_t i = 0; i < VR_SETTINGS_MAX_KEYS; i++) {
		s->values[i] = s->file_values[i] = 0;
	}
	for (size_t i = 0; i < vr_settings_count(); i++) {
		s->values[i] = s->file_values[i] = vr_settings_key(i)->def;
	}
}

void vr_settings_free(vr_settings_t *s) {
	free(s->raw);
	s->raw = NULL;
	s->raw_len = 0;
}


// -----------------------------------------------------------------------------
// TOML subset: `version = N`, `[table]`, `key = value`, `# comment`.

typedef struct {
	const char *key;
	size_t key_len;
	const char *val;
	size_t val_len;
} kv_t;

enum { LINE_OTHER, LINE_TABLE, LINE_KEY };

static bool is_space(char c) {
	return c == ' ' || c == '\t' || c == '\r';
}

static bool is_bare_key_char(char c) {
	return isalnum((unsigned char)c) || c == '_' || c == '-';
}

static int classify_line(const char *s, size_t len, kv_t *kv) {
	size_t i = 0;
	while (i < len && is_space(s[i])) {
		i++;
	}
	if (i >= len || s[i] == '#') {
		return LINE_OTHER;
	}

	if (s[i] == '[') {
		size_t start = ++i;
		while (i < len && s[i] != ']') {
			i++;
		}
		if (i >= len) {
			return LINE_OTHER;
		}
		size_t end = i;
		while (start < end && is_space(s[start])) {
			start++;
		}
		while (end > start && is_space(s[end - 1])) {
			end--;
		}
		kv->key = s + start;
		kv->key_len = end - start;
		return LINE_TABLE;
	}

	size_t key_start = i;
	while (i < len && is_bare_key_char(s[i])) {
		i++;
	}
	size_t key_end = i;
	while (i < len && is_space(s[i])) {
		i++;
	}
	if (key_end == key_start || i >= len || s[i] != '=') {
		return LINE_OTHER;
	}
	i++;
	while (i < len && is_space(s[i])) {
		i++;
	}

	// The value runs to an unquoted '#'
	size_t val_start = i;
	bool quoted = false;
	for (; i < len; i++) {
		if (s[i] == '\\' && quoted) {
			i++;
		}
		else if (s[i] == '"') {
			quoted = !quoted;
		}
		else if (s[i] == '#' && !quoted) {
			break;
		}
	}
	size_t val_end = i;
	while (val_end > val_start && is_space(s[val_end - 1])) {
		val_end--;
	}

	kv->key = s + key_start;
	kv->key_len = key_end - key_start;
	kv->val = s + val_start;
	kv->val_len = val_end - val_start;
	return LINE_KEY;
}

static bool same_value(double a, double b) {
	// Floats go through the registry's float clamp: 0.35 reads back as 0.3499999
	return fabs(a - b) <= 1e-6 * fmax(1.0, fabs(a));
}

// Reads `val` as a value for key `k`. Returns false when it is not valid (the
// caller uses the default). `clamped` is set when it was valid but out of range.
static bool value_parse(const sfvr_setting_key *k, const char *val, size_t len, double *out, bool *clamped) {
	char buf[64];
	*clamped = false;

	if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
		val++;
		len -= 2;
	}
	if (len == 0 || len >= sizeof(buf)) {
		return false;
	}
	memcpy(buf, val, len);
	buf[len] = '\0';

	if (k->type == SFVR_SETTING_BOOL) {
		int b = sfvr_settings_parse_bool(buf, -1);
		if (b < 0) {
			return false;
		}
		*out = b;
		return true;
	}

	if (k->type == SFVR_SETTING_ENUM) {
		int index = sfvr_settings_parse_enum(k, buf);
		const char *name = k->enum_values[index];
		const char *a = buf;
		while (is_space(*a)) {
			a++;
		}
		size_t n = strlen(name);
		if (strlen(a) < n) {
			return false;
		}
		for (size_t i = 0; i < n; i++) {
			if (tolower((unsigned char)a[i]) != name[i]) {
				return false;
			}
		}
		for (const char *rest = a + n; *rest; rest++) {
			if (!is_space(*rest)) {
				return false;
			}
		}
		*out = index;
		return true;
	}

	char *end = NULL;
	double v = strtod(buf, &end);
	if (end == buf || isnan(v)) {
		return false;
	}
	while (*end && is_space(*end)) {
		end++;
	}
	if (*end) {
		return false;
	}

	double result;
	if (k->type == SFVR_SETTING_INT) {
		double rounded = floor(v + 0.5);
		if (rounded > 2147483647.0 || rounded < -2147483648.0) {
			rounded = rounded > 0 ? 2147483647.0 : -2147483648.0;
		}
		result = sfvr_settings_clamp_int(k, (int)rounded);
	}
	else {
		result = sfvr_settings_clamp_float(k, (float)v);
	}
	*clamped = !same_value(result, v);
	*out = result;
	return true;
}

static int value_render(const sfvr_setting_key *k, double v, char *out, size_t n) {
	switch (k->type) {
		case SFVR_SETTING_BOOL:
			return snprintf(out, n, "%s", v != 0 ? "true" : "false");
		case SFVR_SETTING_INT:
			return snprintf(out, n, "%d", (int)v);
		case SFVR_SETTING_ENUM: {
			int count = sfvr_settings_enum_count(k);
			int index = (int)v;
			if (index < 0 || index >= count) {
				index = (int)k->def;
			}
			return snprintf(out, n, "\"%s\"", k->enum_values[index]);
		}
		default: {
			int len = snprintf(out, n, "%.6g", v);
			if (len > 0 && (size_t)len + 2 < n && !strpbrk(out, ".en")) {
				strcat(out, ".0"); // TOML floats need a dot
				len += 2;
			}
			return len;
		}
	}
}


// -----------------------------------------------------------------------------
// Parse

static bool name_is(const kv_t *kv, const char *name) {
	return kv->key_len == strlen(name) && memcmp(kv->key, name, kv->key_len) == 0;
}

static bool key_name_index(const kv_t *kv, int *index) {
	char name[64];
	if (kv->key_len >= sizeof(name)) {
		return false;
	}
	memcpy(name, kv->key, kv->key_len);
	name[kv->key_len] = '\0';
	*index = vr_settings_index(name);
	return *index >= 0;
}

// Calls fn for each line. `table` is the current table name ("" at the top).
typedef void (*line_fn)(void *user, const char *line, size_t len, int kind, const kv_t *kv, const char *table);

static void each_line(const char *text, size_t len, line_fn fn, void *user) {
	char table[64] = "";
	size_t pos = 0;
	while (pos < len) {
		const char *line = text + pos;
		const char *nl = memchr(line, '\n', len - pos);
		size_t line_len = nl ? (size_t)(nl - line) : len - pos;
		pos += line_len + (nl ? 1 : 0);

		kv_t kv = {0};
		int kind = classify_line(line, line_len, &kv);
		if (kind == LINE_TABLE) {
			size_t n = kv.key_len < sizeof(table) - 1 ? kv.key_len : sizeof(table) - 1;
			memcpy(table, kv.key, n);
			table[n] = '\0';
		}
		fn(user, line, line_len, kind, &kv, table);
	}
}

typedef struct {
	vr_settings_t *s;
	bool seen[VR_SETTINGS_MAX_KEYS];
	int repairs;
} parse_state_t;

static void parse_line(void *user, const char *line, size_t len, int kind, const kv_t *kv, const char *table) {
	(void)line; (void)len;
	parse_state_t *p = user;
	if (kind != LINE_KEY) {
		return;
	}

	if (table[0] == '\0' && name_is(kv, "version")) {
		char number[16];
		size_t n = kv->val_len < sizeof(number) - 1 ? kv->val_len : sizeof(number) - 1;
		memcpy(number, kv->val, n);
		number[n] = '\0';
		p->s->file_version = atoi(number);
		return;
	}

	int index;
	if (strcmp(table, "vr") != 0 || !key_name_index(kv, &index)) {
		return; // not ours: kept as is
	}
	if (p->seen[index]) {
		p->repairs++; // a duplicate; the first one wins
		return;
	}
	p->seen[index] = true;

	double v;
	bool clamped;
	if (!value_parse(vr_settings_key(index), kv->val, kv->val_len, &v, &clamped)) {
		p->repairs++; // the default stays
	}
	else {
		p->s->file_values[index] = v;
		p->repairs += clamped;
	}
}

// Schema 1 wrote the old display defaults (90 Hz, resolution_scale 1.0) into every file, so
// a file at those values never chose them. Moving to schema 2 takes such a file to the
// new defaults (120 Hz, 1.25: about 2160 px an eye, standard.md section 9). A value the
// user changed stays. The version in the file is rewritten after this, so it runs once.
static void migrate(vr_settings_t *s, const bool *seen) {
	if (s->file_version >= 2) {
		return;
	}
	int rate = vr_settings_index("refresh_rate");
	int scale = vr_settings_index("resolution_scale");
	if (rate >= 0 && seen[rate] && s->file_values[rate] == 90.0) {
		s->file_values[rate] = vr_settings_key((size_t)rate)->def;
	}
	if (scale >= 0 && seen[scale] && fabs(s->file_values[scale] - 1.0) < 1e-9) {
		s->file_values[scale] = vr_settings_key((size_t)scale)->def;
	}
}

int vr_settings_parse(vr_settings_t *s, const char *text, size_t len) {
	for (size_t i = 0; i < vr_settings_count(); i++) {
		s->file_values[i] = vr_settings_key(i)->def;
	}
	s->file_version = 0;

	free(s->raw);
	s->raw = malloc(len + 1);
	if (s->raw) {
		memcpy(s->raw, text, len);
		s->raw[len] = '\0';
		s->raw_len = len;
	}
	else {
		s->raw_len = 0;
	}

	parse_state_t p = { .s = s };
	each_line(text, len, parse_line, &p);

	migrate(s, p.seen);

	for (size_t i = 0; i < vr_settings_count(); i++) {
		if (!p.seen[i]) {
			p.repairs++;
		}
	}
	if (s->file_version < SFVR_SETTINGS_SCHEMA) {
		p.repairs++;
	}
	return p.repairs;
}


// -----------------------------------------------------------------------------
// Format

typedef struct {
	char **lines;
	size_t len;
	size_t cap;
} lines_t;

static bool lines_add_at(lines_t *l, size_t at, const char *text, size_t n) {
	if (l->len == l->cap) {
		size_t cap = l->cap ? l->cap * 2 : 64;
		char **lines = realloc(l->lines, cap * sizeof(char *));
		if (!lines) {
			return false;
		}
		l->lines = lines;
		l->cap = cap;
	}
	char *copy = malloc(n + 1);
	if (!copy) {
		return false;
	}
	memcpy(copy, text, n);
	copy[n] = '\0';
	memmove(&l->lines[at + 1], &l->lines[at], (l->len - at) * sizeof(char *));
	l->lines[at] = copy;
	l->len++;
	return true;
}

static void lines_free(lines_t *l) {
	for (size_t i = 0; i < l->len; i++) {
		free(l->lines[i]);
	}
	free(l->lines);
}

typedef struct {
	const vr_settings_t *s;
	lines_t out;
	bool seen[VR_SETTINGS_MAX_KEYS];
	bool have_version;
	bool have_vr_table;
	size_t vr_insert_at; // index in out after the last key (or the header) of [vr]
	char failed;
} format_state_t;

static void format_line(void *user, const char *line, size_t len, int kind, const kv_t *kv, const char *table) {
	format_state_t *f = user;
	const vr_settings_t *s = f->s;
	char text[160];

	if (kind == LINE_TABLE && strcmp(table, "vr") == 0 && !f->have_vr_table) {
		f->have_vr_table = true;
		f->vr_insert_at = f->out.len + 1;
	}

	if (kind == LINE_KEY && table[0] == '\0' && name_is(kv, "version")) {
		f->have_version = true;
		if (s->file_version < SFVR_SETTINGS_SCHEMA) {
			snprintf(text, sizeof(text), "version = %d", SFVR_SETTINGS_SCHEMA);
			f->failed |= !lines_add_at(&f->out, f->out.len, text, strlen(text));
			return;
		}
	}
	else if (kind == LINE_KEY && strcmp(table, "vr") == 0) {
		int index;
		if (key_name_index(kv, &index)) {
			if (f->seen[index]) {
				return; // drop a duplicate
			}
			f->seen[index] = true;

			double v;
			bool clamped;
			const sfvr_setting_key *k = vr_settings_key(index);
			bool keep = value_parse(k, kv->val, kv->val_len, &v, &clamped) && !clamped && same_value(v, s->file_values[index]);
			if (!keep) {
				int n = snprintf(text, sizeof(text), "%s = ", k->name);
				value_render(k, s->file_values[index], text + n, sizeof(text) - n);
				f->failed |= !lines_add_at(&f->out, f->out.len, text, strlen(text));
				f->vr_insert_at = f->out.len;
				return;
			}
		}
	}

	f->failed |= !lines_add_at(&f->out, f->out.len, line, len);
	if (kind == LINE_KEY && strcmp(table, "vr") == 0) {
		f->vr_insert_at = f->out.len;
	}
}

char *vr_settings_format(const vr_settings_t *s, size_t *out_len) {
	format_state_t f = { .s = s };
	const char *raw = s->raw ? s->raw : "";
	size_t raw_len = s->raw ? s->raw_len : 0;

	if (raw_len == 0) {
		const char *header = "# SM64VR VR settings. SM64_VR_<KEY> environment variables override them.";
		f.failed |= !lines_add_at(&f.out, 0, header, strlen(header));
	}
	each_line(raw, raw_len, format_line, &f);

	if (!f.have_version) {
		char text[32];
		snprintf(text, sizeof(text), "version = %d", SFVR_SETTINGS_SCHEMA);
		size_t at = 0;
		// after a leading comment block, so a header comment stays on top
		while (at < f.out.len && f.out.lines[at][0] == '#') {
			at++;
		}
		f.failed |= !lines_add_at(&f.out, at, text, strlen(text));
		if (f.have_vr_table) {
			f.vr_insert_at++;
		}
	}

	if (!f.have_vr_table) {
		if (f.out.len > 0 && f.out.lines[f.out.len - 1][0] != '\0') {
			f.failed |= !lines_add_at(&f.out, f.out.len, "", 0);
		}
		f.failed |= !lines_add_at(&f.out, f.out.len, "[vr]", 4);
		f.vr_insert_at = f.out.len;
	}

	for (size_t i = 0; i < vr_settings_count(); i++) {
		if (f.seen[i]) {
			continue;
		}
		char text[160];
		const sfvr_setting_key *k = vr_settings_key(i);
		int n = snprintf(text, sizeof(text), "%s = ", k->name);
		value_render(k, s->file_values[i], text + n, sizeof(text) - n);
		f.failed |= !lines_add_at(&f.out, f.vr_insert_at++, text, strlen(text));
	}

	size_t total = 0;
	for (size_t i = 0; i < f.out.len; i++) {
		total += strlen(f.out.lines[i]) + 1;
	}
	char *result = f.failed ? NULL : malloc(total + 1);
	if (result) {
		char *p = result;
		for (size_t i = 0; i < f.out.len; i++) {
			size_t n = strlen(f.out.lines[i]);
			memcpy(p, f.out.lines[i], n);
			p += n;
			*p++ = '\n';
		}
		*p = '\0';
		if (out_len) {
			*out_len = total;
		}
	}
	lines_free(&f.out);
	return result;
}


// -----------------------------------------------------------------------------
// Environment, files

static const char *default_getenv(const char *name) {
	return getenv(name);
}

int vr_settings_apply_env(vr_settings_t *s, const char *(*getenv_fn)(const char *)) {
	if (!getenv_fn) {
		getenv_fn = default_getenv;
	}

	int applied = 0;
	for (size_t i = 0; i < vr_settings_count(); i++) {
		const sfvr_setting_key *k = vr_settings_key(i);
		char name[96];
		if (!sfvr_settings_env_name(ENV_PREFIX, k->name, name, sizeof(name))) {
			continue;
		}
		const char *text = getenv_fn(name);
		if (!text || !text[0]) {
			continue;
		}

		double v;
		bool clamped;
		if (value_parse(k, text, strlen(text), &v, &clamped)) {
			s->values[i] = v;
			applied++;
			vr_logf("settings: %s=%s from the environment", name, text);
		}
		else {
			vr_logf("settings: ignoring %s=%s (not a valid value)", name, text);
		}
	}
	return applied;
}

static char *read_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	size_t cap = 4096, used = 0;
	char *buf = malloc(cap);
	while (buf) {
		size_t n = fread(buf + used, 1, cap - used, f);
		used += n;
		if (used < cap) {
			break;
		}
		cap *= 2;
		char *bigger = realloc(buf, cap);
		if (!bigger) {
			free(buf);
			buf = NULL;
		}
		else {
			buf = bigger;
		}
	}
	fclose(f);
	*len = used;
	return buf;
}

bool vr_settings_save(const vr_settings_t *s) {
	size_t len = 0;
	char *text = vr_settings_format(s, &len);
	if (!text) {
		return false;
	}

	char tmp[sizeof(s->path) + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", s->path);

	bool ok = false;
	FILE *f = fopen(tmp, "wb");
	if (f) {
		ok = fwrite(text, 1, len, f) == len;
		ok = (fflush(f) == 0) && ok;
		ok = (fsync(fileno(f)) == 0) && ok;
		ok = (fclose(f) == 0) && ok;
		ok = ok && rename(tmp, s->path) == 0;
		if (!ok) {
			remove(tmp);
		}
	}
	free(text);
	return ok;
}

bool vr_settings_load(vr_settings_t *s, const char *path) {
	memset(s, 0, sizeof(*s));
	snprintf(s->path, sizeof(s->path), "%s", path);
	vr_settings_defaults(s);

	size_t len = 0;
	char *text = read_file(path, &len);
	bool ok = true;
	int repairs;
	if (text) {
		repairs = vr_settings_parse(s, text, len);
		free(text);
	}
	else {
		repairs = vr_settings_parse(s, "", 0);
	}

	for (size_t i = 0; i < vr_settings_count(); i++) {
		s->values[i] = s->file_values[i];
	}

	if (repairs > 0) {
		if (vr_settings_save(s)) {
			vr_logf("settings: wrote %s (%d added or repaired)", path, repairs);
		}
		else {
			vr_logf("settings: could not write %s; using the values in memory", path);
			ok = false;
		}
	}
	vr_settings_apply_env(s, NULL);
	return ok;
}

double vr_settings_get(const vr_settings_t *s, const char *name) {
	int index = vr_settings_index(name);
	return index >= 0 ? s->values[index] : 0.0;
}

double vr_settings_clamp(const char *name, double value) {
	int index = vr_settings_index(name);
	if (index < 0) {
		return value;
	}
	const sfvr_setting_key *k = vr_settings_key(index);
	if (k->type == SFVR_SETTING_INT || k->type == SFVR_SETTING_ENUM) {
		if (isnan(value)) {
			return k->def;
		}
		value = floor(value + 0.5);
		value = value > 2147483647.0 ? 2147483647.0 : (value < -2147483648.0 ? -2147483648.0 : value);
		return sfvr_settings_clamp_int(k, (int)value);
	}
	if (k->type == SFVR_SETTING_BOOL) {
		return value != 0;
	}
	return sfvr_settings_clamp_float(k, (float)value);
}

bool vr_settings_set(vr_settings_t *s, const char *name, double value) {
	int index = vr_settings_index(name);
	if (index < 0) {
		return false;
	}
	value = vr_settings_clamp(name, value);
	bool changed = !same_value(s->values[index], value) || !same_value(s->file_values[index], value);
	s->values[index] = s->file_values[index] = value;
	return changed;
}

double vr_settings_default(const char *name) {
	int index = vr_settings_index(name);
	return index >= 0 ? vr_settings_key((size_t)index)->def : 0.0;
}

bool vr_settings_reset(vr_settings_t *s, const char *name) {
	return vr_settings_set(s, name, vr_settings_default(name));
}
