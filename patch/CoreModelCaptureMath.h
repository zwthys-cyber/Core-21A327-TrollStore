#ifndef CORE_MODEL_CAPTURE_MATH_H
#define CORE_MODEL_CAPTURE_MATH_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

typedef struct { float x, y, z; } CMVec3;
typedef struct { float x, y; } CMVec2;
typedef struct { float values[11]; } CMTransform;

static bool CMValidTransform(const CMTransform *t) {
    for (unsigned i = 0; i < 11; i++) {
        if (i == 7) continue; // FTransform's translation padding is not a value.
        if (!isfinite(t->values[i]) || fabsf(t->values[i]) > 1e8f) return false;
    }
    float norm = 0;
    for (unsigned i = 0; i < 4; i++) norm += t->values[i] * t->values[i];
    return norm > 0.25f && norm < 4.0f;
}

static CMVec3 CMCross(CMVec3 a, CMVec3 b) {
    return (CMVec3){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

static bool CMWorldPosition(const CMTransform *component, const CMTransform *bone, CMVec3 *out) {
    if (!CMValidTransform(component) || !CMValidTransform(bone)) return false;
    CMVec3 scaled = {bone->values[4]*component->values[8],
                     bone->values[5]*component->values[9],
                     bone->values[6]*component->values[10]};
    CMVec3 q = {component->values[0], component->values[1], component->values[2]};
    CMVec3 cross = CMCross(q, scaled);
    CMVec3 second = CMCross(q, cross);
    float w = component->values[3];
    *out = (CMVec3){scaled.x+2*(w*cross.x+second.x)+component->values[4],
                   scaled.y+2*(w*cross.y+second.y)+component->values[5],
                   scaled.z+2*(w*cross.z+second.z)+component->values[6]};
    return isfinite(out->x) && isfinite(out->y) && isfinite(out->z);
}

// The analyzed Core query returns an 80-byte RayHit via ARM64's x8 return buffer.
// tfar and geometry ID are the fields read below; unused fields are not exported.
typedef struct {
    float origin[3], near_distance, direction[3], time, far_distance;
    uint32_t mask, ray_id, flags;
    float normal[3], u, v;
    uint32_t primitive_id, geometry_id, instance_id;
} CMRayHit;
_Static_assert(sizeof(CMTransform) == 44, "FTransform read size");
_Static_assert(sizeof(CMRayHit) == 80, "Core ray return ABI");
_Static_assert(offsetof(CMRayHit, far_distance) == 32, "Core ray distance field");
_Static_assert(offsetof(CMRayHit, geometry_id) == 72, "Core ray hit field");

#endif
