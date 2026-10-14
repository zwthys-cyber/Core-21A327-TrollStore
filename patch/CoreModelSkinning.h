#ifndef CORE_MODEL_SKINNING_H
#define CORE_MODEL_SKINNING_H
#include "CoreModelCaptureMath.h"
#include "CoreModelBinding.h"

// Layout established by v7 section ranges, triangle indices and weighted
// palette indices across 49 LODs. Asset bounds are not a position validator:
// three head resources have a full-body low LOD outside the head-only bounds.
#define CMS_SECTION_STRIDE 224
typedef struct { float m[3][4]; } CMSMatrix;
typedef struct {
    uint32_t material,first_index,triangle_count,first_vertex,vertex_count,bone_count;
    uint64_t bone_address;
} CMSSection;
typedef enum { CMS_OK=0,CMS_INVALID=1,CMS_UNKNOWN_BONE=2 } CMSResult;

static inline CMSMatrix CMSIdentity(void) {
    CMSMatrix m={{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};return m;
}
static inline CMSMatrix CMSMultiply(CMSMatrix a,CMSMatrix b) {
    CMSMatrix out={0};
    for (unsigned r=0;r<3;r++) {
        for (unsigned c=0;c<3;c++) for (unsigned k=0;k<3;k++) out.m[r][c]+=a.m[r][k]*b.m[k][c];
        out.m[r][3]=a.m[r][3];for (unsigned k=0;k<3;k++) out.m[r][3]+=a.m[r][k]*b.m[k][3];
    }
    return out;
}
static inline CMVec3 CMSPoint(CMSMatrix m,CMVec3 p) {
    const float v[3]={p.x,p.y,p.z};CMVec3 out={0};float *o=&out.x;
    for (unsigned r=0;r<3;r++) { o[r]=m.m[r][3];for (unsigned k=0;k<3;k++) o[r]+=m.m[r][k]*v[k]; }
    return out;
}
static inline bool CMSFromTransform(const CMTransform *t,CMSMatrix *out) {
    if (!CMValidTransform(t)) return false;
    float x=t->values[0],y=t->values[1],z=t->values[2],w=t->values[3];
    if (fabsf(x*x+y*y+z*z+w*w-1)>0.005f) return false;
    float q[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
        {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
        {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    for (unsigned r=0;r<3;r++) {
        for (unsigned c=0;c<3;c++) out->m[r][c]=q[r][c]*t->values[8+c];
        out->m[r][3]=t->values[4+r];
    }
    return true;
}
static inline bool CMSInverse(CMSMatrix a,CMSMatrix *out) {
    const float (*m)[4]=a.m;
    float det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if (!isfinite(det) || fabsf(det)<1e-12f) return false;
    out->m[0][0]=(m[1][1]*m[2][2]-m[1][2]*m[2][1])/det;
    out->m[0][1]=(m[0][2]*m[2][1]-m[0][1]*m[2][2])/det;
    out->m[0][2]=(m[0][1]*m[1][2]-m[0][2]*m[1][1])/det;
    out->m[1][0]=(m[1][2]*m[2][0]-m[1][0]*m[2][2])/det;
    out->m[1][1]=(m[0][0]*m[2][2]-m[0][2]*m[2][0])/det;
    out->m[1][2]=(m[0][2]*m[1][0]-m[0][0]*m[1][2])/det;
    out->m[2][0]=(m[1][0]*m[2][1]-m[1][1]*m[2][0])/det;
    out->m[2][1]=(m[0][1]*m[2][0]-m[0][0]*m[2][1])/det;
    out->m[2][2]=(m[0][0]*m[1][1]-m[0][1]*m[1][0])/det;
    for (unsigned r=0;r<3;r++) {
        out->m[r][3]=0;for (unsigned k=0;k<3;k++) out->m[r][3]-=out->m[r][k]*m[k][3];
        for (unsigned c=0;c<4;c++) if (!isfinite(out->m[r][c])) return false;
    }
    return true;
}
static inline bool CMSReferenceInverse(const uint8_t *info,const uint8_t *pose,uint32_t count,CMSMatrix *inverse) {
    if (!info || !pose || !inverse || !CMBHierarchy(info,count,16)) return false;
    CMSMatrix global[256];
    for (uint32_t i=0;i<count;i++) {
        CMTransform t;memcpy(&t,pose+i*48,sizeof(t));CMSMatrix local;int32_t parent=-1;memcpy(&parent,info+i*16+8,4);
        if (!CMSFromTransform(&t,&local)) return false;
        global[i]=parent<0 ? local : CMSMultiply(global[parent],local);
        if (!CMSInverse(global[i],&inverse[i])) return false;
    }
    return true;
}
static inline bool CMSBuildSkinMatrices(const CMSMatrix *inverse,uint32_t count,const uint8_t *pose,uint32_t poseCount,uint32_t poseStride,
                                        const int32_t *map,const CMTransform *component,CMSMatrix *skin,uint8_t *known) {
    if (!inverse || !pose || !skin || !known || !count || count>256 || !poseCount || poseCount>256 || (poseStride!=44 && poseStride!=48)) return false;
    CMSMatrix world;if (!CMSFromTransform(component,&world)) return false;
    memset(known,0,count);
    for (uint32_t i=0;i<count;i++) {
        int32_t index=map ? map[i] : (int32_t)i;
        if (index==-1) continue;
        if (index<0 || (uint32_t)index>=poseCount) return false;
        CMTransform t;memcpy(&t,pose+(size_t)index*poseStride,sizeof(t));CMSMatrix current;
        if (!CMSFromTransform(&t,&current)) return false;
        skin[i]=CMSMultiply(CMSMultiply(world,current),inverse[i]);known[i]=1;
    }
    return true;
}
static inline bool CMSParseSection(const uint8_t *prefix,size_t size,uint32_t slot,uint32_t vertices,uint32_t indices,CMSSection *out) {
    size_t offset=(size_t)slot*CMS_SECTION_STRIDE;
    if (!prefix || !out || offset>size || size-offset<CMS_SECTION_STRIDE) return false;
    const uint8_t *p=prefix+offset;uint16_t material=0;memcpy(&material,p,2);out->material=material;
    memcpy(&out->first_index,p+4,4);memcpy(&out->triangle_count,p+8,4);memcpy(&out->first_vertex,p+0x18,4);memcpy(&out->vertex_count,p+0x70,4);
    int32_t n=0,cap=0;memcpy(&out->bone_address,p+0x60,8);memcpy(&n,p+0x68,4);memcpy(&cap,p+0x6C,4);
    if (n<1 || n>256 || cap<n || cap>512 || !CMBRange(out->bone_address,(size_t)n*2) ||
        !out->vertex_count || out->first_vertex>vertices || out->vertex_count>vertices-out->first_vertex ||
        !out->triangle_count || out->first_index>indices || (uint64_t)out->triangle_count*3>indices-out->first_index) return false;
    out->bone_count=(uint32_t)n;return true;
}
static inline CMSResult CMSSkinVertex(const uint8_t *vertex,const uint8_t *weights,const uint16_t *palette,uint32_t paletteCount,
                                     const CMSMatrix *skin,const uint8_t *known,uint32_t boneCount,CMVec3 *out) {
    if (!vertex || !weights || !palette || !skin || !known || !out || !paletteCount || paletteCount>256 || !boneCount || boneCount>256) return CMS_INVALID;
    CMVec3 p;memcpy(&p,vertex+8,12);
    if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) || fabsf(p.x)>1e8f || fabsf(p.y)>1e8f || fabsf(p.z)>1e8f) return CMS_INVALID;
    unsigned sum=0;CMVec3 result={0};
    for (unsigned i=0;i<4;i++) {
        uint32_t weight=weights[4+i];sum+=weight;if (!weight) continue;
        if (weights[i]>=paletteCount || palette[weights[i]]>=boneCount) return CMS_INVALID;
        uint32_t bone=palette[weights[i]];if (!known[bone]) return CMS_UNKNOWN_BONE;
        CMVec3 q=CMSPoint(skin[bone],p);float factor=(float)weight/255;
        result.x+=q.x*factor;result.y+=q.y*factor;result.z+=q.z*factor;
    }
    if (sum!=255 || !isfinite(result.x) || !isfinite(result.y) || !isfinite(result.z)) return CMS_INVALID;
    *out=result;return CMS_OK;
}
#endif
