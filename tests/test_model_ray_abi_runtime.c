#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../patch/CoreModelCaptureMath.h"

// This provider deliberately uses the native 96-byte contract, independently
// of sizeof(CMRayHit). A short declaration corrupts the following canary.
__attribute__((noinline)) static void NativeResult(void *out) {
    memset(out,0xA5,96);
    const float distance=321.25f;const uint32_t geometry=17;
    const uint64_t tail[2]={0x1FFFFFFFFULL,0xA5A55A5A11223344ULL};
    memcpy((uint8_t *)out+32,&distance,4);memcpy((uint8_t *)out+72,&geometry,4);memcpy((uint8_t *)out+80,tail,16);
}
__attribute__((noinline)) static CMRayHit NativeWrapper(void) {CMRayHit result;NativeResult(&result);return result;}
int main(void) {
    struct {uint64_t before[2];CMRayHit ray;uint64_t after[2];} guarded={0};
    guarded.before[0]=guarded.before[1]=guarded.after[0]=guarded.after[1]=0xC0DEF00DFEEDBEEFULL;
    NativeResult(&guarded.ray);
    assert(guarded.before[0]==0xC0DEF00DFEEDBEEFULL&&guarded.before[1]==0xC0DEF00DFEEDBEEFULL);
    assert(guarded.after[0]==0xC0DEF00DFEEDBEEFULL&&guarded.after[1]==0xC0DEF00DFEEDBEEFULL);
    assert(guarded.ray.far_distance==321.25f&&guarded.ray.geometry_id==17&&!((uintptr_t)&guarded.ray&15));
    CMRayHit returned=NativeWrapper();assert(returned.far_distance==321.25f&&returned.geometry_id==17&&!memcmp(returned.native_tail,guarded.ray.native_tail,16));
    CMTransform identity={{0,0,0,1,0,0,0,0,1,1,1}};CMVec3 world;assert(CMWorldPosition(&identity,&identity,&world));
    puts("PASS: independent 96-byte native writes preserve caller canaries; trailing writes reproduce the build-29 overwritten-handle value; 16-byte alignment and complete by-value return");return 0;
}
