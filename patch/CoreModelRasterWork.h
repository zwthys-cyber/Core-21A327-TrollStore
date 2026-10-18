#ifndef CORE_MODEL_RASTER_WORK_H
#define CORE_MODEL_RASTER_WORK_H
#include "CoreModelPixels.h"
#include <dispatch/dispatch.h>

typedef struct {
    const CMPVertex *points;
    const uint32_t *indices;
    const uint8_t *valid;
    size_t triangle_count;
    uint32_t vertices;
} CMPRasterMesh;

static inline uint64_t CMPRasterMeshRows(CMPMask *mask,CMPCoverage *coverage,const CMPRasterMesh *meshes,size_t count) {
    uint64_t triangles=0;
    for(size_t j=0;j<count;j++){
        const CMPRasterMesh *mesh=&meshes[j];
        for(size_t i=0;i<mesh->triangle_count;i++){
            uint32_t a=mesh->indices[i*3],b=mesh->indices[i*3+1],c=mesh->indices[i*3+2];
            if(a>=mesh->vertices||b>=mesh->vertices||c>=mesh->vertices||!mesh->valid[a]||!mesh->valid[b]||!mesh->valid[c])continue;
            CMPVertex t[3]={mesh->points[a],mesh->points[b],mesh->points[c]};if(CMPRasterTriangleCovered(mask,t,coverage))triangles++;
        }
    }
    return triangles;
}

// Every pixel keeps its original float operations and triangle order. Tasks
// own disjoint whole rows of both depth pixels and coverage words. No native
// game reads, geometry queries, Objective-C objects or ImGui calls run here.
static inline uint64_t CMPRasterMeshWork(CMPMask *mask,CMPCoverage *coverage,const CMPRasterMesh *meshes,size_t count,unsigned workers) {
    if(workers<2||mask->height<2)return CMPRasterMeshRows(mask,coverage,meshes,count);
    uint32_t coveredStorage[2]={0};uint64_t triangleStorage[2]={0};uint32_t *covered=coveredStorage;uint64_t *triangles=triangleStorage;
    int height=mask->height;
    dispatch_apply(2,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^(size_t worker){
        int first=(int)((size_t)height*worker/2),last=(int)((size_t)height*(worker+1)/2);
        CMPMask rows={mask->x,mask->y+first,mask->width,last-first,mask->pixels+(size_t)first*mask->width};
        CMPCoverage bits={coverage->words+(size_t)first*coverage->words_per_row,coverage->words_per_row,0};
        triangles[worker]=CMPRasterMeshRows(&rows,&bits,meshes,count);covered[worker]=bits.count;
    });
    coverage->count+=covered[0]+covered[1];return triangles[0];
}
#endif
