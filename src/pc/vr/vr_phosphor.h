#ifndef VR_PHOSPHOR_H
#define VR_PHOSPHOR_H

#include "vr_types.h"

// The phosphor mask (docs/VR_ARCHITECTURE.md section 4): red, green and blue
// stripes and scanlines over the stereo eye images, read from the direction a
// pixel looks in, in the room, so both eyes see the same stripe the same way.
// Pure maths: no GL. The fragment shader in vr_render.c mirrors vr_phosphor_mask.

#define VR_PHOSPHOR_GAIN 2.0f
#define VR_PHOSPHOR_SCAN_DEPTH 0.65f
#define VR_PHOSPHOR_SCAN_PER_TRIAD (1.0f / 1.5f)

typedef struct {
	float room[9]; // room-from-eye rotation, column-major (m[col * 3 + row])
	float tan_left, tan_right, tan_up, tan_down; // the fov edges at distance 1 (tangents)
	float triads_per_rad; // 180 / (pi * triad width in degrees)
	float strength; // 0..1
} vr_phosphor_t;

// From the eye's orientation in the room (the pose submitted for it) and its
// fov (angles in radians: left, right, up, down; left and down negative).
void vr_phosphor_constants(const float orientation[4], const float fov[4], float triad_deg,
	float strength, vr_phosphor_t *out);

// CPU mirror of the shader for a room direction `d`: the factor the mask puts
// on the light of each of R, G, B (1 at strength 0). The gamma-encoded eye
// image is scaled by its square root.
void vr_phosphor_mask(const float d[3], float triad_deg, float strength, float out[3]);

#endif
