#ifndef CORE_MODEL_PIXELS_H
#define CORE_MODEL_PIXELS_H
#include "CoreModelCaptureMath.h"
#include <stdlib.h>

// Exact pixel centres and perspective-correct surface positions. No bone-hit
// interpolation, majority voting, reduced-resolution mask or colour guessing.
typedef struct { CMVec3 position,forward,right,up;float width,height,focal; } CMPView;
typedef struct { CMVec2 xy;CMVec3 world;float inverse_depth; } CMPVertex;
// Pixel ray + interpolated depth recover the surface position. Avoid storing
// three redundant world coordinates in every framebuffer pixel.
typedef struct { float inverse_depth;uint8_t colour; } CMPPixel;
_Static_assert(sizeof(CMPPixel)==8,"Full-resolution depth/colour pixel");
typedef struct { int x,y,width,height;CMPPixel *pixels; } CMPMask;
typedef struct {int x,y,width,height;uint8_t colour;} CMPRectangle;
typedef bool (*CMPQuery)(void *,const CMVec3 *,const CMVec3 *,bool *);
typedef void (*CMPSpan)(void *,int,int,int,uint8_t);
static inline float CMPDot(CMVec3 a,CMVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline bool CMPMakeView(const uint8_t raw[52],float width,float height,CMPView *out) {
    float values[13];memcpy(values,raw,sizeof(values));
    if (!isfinite(width)||!isfinite(height)||width<1||height<1||width>8192||height>8192) return false;
    for (unsigned i=0;i<3;i++) if (!isfinite(values[i])||!isfinite(values[6+i])) return false;
    if (!isfinite(values[12])||values[12]<=1||values[12]>=179) return false;
    const float radians=0.01745329251994329577f;
    float sp=sinf(values[6]*radians),cp=cosf(values[6]*radians),sy=sinf(values[7]*radians),cy=cosf(values[7]*radians),sr=sinf(values[8]*radians),cr=cosf(values[8]*radians);
    out->position=(CMVec3){values[0],values[1],values[2]};out->forward=(CMVec3){cp*cy,cp*sy,sp};
    out->right=(CMVec3){sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,-sr*cp};out->up=(CMVec3){-(cr*sp*cy+sr*sy),cy*sr-cr*sp*sy,cr*cp};
    out->width=width;out->height=height;out->focal=width*0.5f/tanf(values[12]*radians*0.5f);
    return isfinite(out->focal)&&out->focal>0;
}
static inline bool CMPProject(const CMPView *v,CMVec3 world,CMPVertex *out) {
    CMVec3 delta={world.x-v->position.x,world.y-v->position.y,world.z-v->position.z};float depth=CMPDot(delta,v->forward);
    if (!isfinite(depth)||depth<1) return false;
    out->world=world;out->inverse_depth=1/depth;out->xy=(CMVec2){v->width*0.5f+CMPDot(delta,v->right)*v->focal/depth,v->height*0.5f-CMPDot(delta,v->up)*v->focal/depth};
    return isfinite(out->xy.x)&&isfinite(out->xy.y)&&fabsf(out->xy.x)<65536&&fabsf(out->xy.y)<65536;
}
static inline float CMPEdge(CMVec2 a,CMVec2 b,float x,float y) { return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); }
static inline bool CMPWorldAtPixel(const CMPView *view,int x,int y,float inverseDepth,CMVec3 *world) {
    if(!isfinite(inverseDepth)||inverseDepth<=0||!isfinite(view->focal)||view->focal<=0)return false;
    float horizontal=(x+.5f-view->width*.5f)/view->focal,vertical=(view->height*.5f-y-.5f)/view->focal,depth=1/inverseDepth;
    *world=(CMVec3){view->position.x+(view->forward.x+horizontal*view->right.x+vertical*view->up.x)*depth,
        view->position.y+(view->forward.y+horizontal*view->right.y+vertical*view->up.y)*depth,
        view->position.z+(view->forward.z+horizontal*view->right.z+vertical*view->up.z)*depth};
    return isfinite(world->x)&&isfinite(world->y)&&isfinite(world->z);
}
static inline bool CMPRasterTriangle(CMPMask *mask,const CMPVertex t[3]) {
    if (!mask->pixels||mask->width<=0||mask->height<=0) return false;
    float area=CMPEdge(t[0].xy,t[1].xy,t[2].xy.x,t[2].xy.y);
    if (!isfinite(area)||fabsf(area)<1e-7f) return false;
    for (unsigned i=0;i<3;i++) if (!isfinite(t[i].inverse_depth)||t[i].inverse_depth<=0||!isfinite(t[i].world.x)||!isfinite(t[i].world.y)||!isfinite(t[i].world.z)) return false;
    float minX=fminf(t[0].xy.x,fminf(t[1].xy.x,t[2].xy.x)),maxX=fmaxf(t[0].xy.x,fmaxf(t[1].xy.x,t[2].xy.x));
    float minY=fminf(t[0].xy.y,fminf(t[1].xy.y,t[2].xy.y)),maxY=fmaxf(t[0].xy.y,fmaxf(t[1].xy.y,t[2].xy.y));
    int x0=(int)fmaxf(mask->x,ceilf(minX-0.5f)),x1=(int)fminf(mask->x+mask->width-1,floorf(maxX-0.5f));
    int y0=(int)fmaxf(mask->y,ceilf(minY-0.5f)),y1=(int)fminf(mask->y+mask->height-1,floorf(maxY-0.5f));
    for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
        float a=CMPEdge(t[1].xy,t[2].xy,x+0.5f,y+0.5f)/area,b=CMPEdge(t[2].xy,t[0].xy,x+0.5f,y+0.5f)/area,c=1-a-b;
        if (a<-1e-6f||b<-1e-6f||c<-1e-6f) continue;
        float depth=a*t[0].inverse_depth+b*t[1].inverse_depth+c*t[2].inverse_depth;
        CMPPixel *p=&mask->pixels[(size_t)(y-mask->y)*mask->width+(x-mask->x)];
        if (!isfinite(depth)||depth<=p->inverse_depth) continue;
        p->inverse_depth=depth;p->colour=0;
    }
    return true;
}
static inline bool CMPClassify(CMPMask *mask,const CMPView *view,CMPQuery query,void *context,uint32_t *white,uint32_t *red) {
    *white=*red=0;
    for (int y=0;y<mask->height;y++) for (int x=0;x<mask->width;x++) {
        CMPPixel *p=&mask->pixels[(size_t)y*mask->width+x];if (!p->inverse_depth) continue;
        CMVec3 world;bool blocked=false;if(!CMPWorldAtPixel(view,mask->x+x,mask->y+y,p->inverse_depth,&world)||!query(context,&view->position,&world,&blocked))return false;
        p->colour=blocked ? 2 : 1;if (blocked) (*red)++;else (*white)++;
    }
    return true;
}
static inline uint32_t CMPSpans(const CMPMask *mask,CMPSpan emit,void *context,uint32_t limit) {
    uint32_t count=0;
    for (int y=0;y<mask->height;y++) for (int x=0;x<mask->width;) {
        uint8_t colour=mask->pixels[(size_t)y*mask->width+x].colour;if (!colour) { x++;continue; }
        int start=x;do { x++; } while (x<mask->width&&mask->pixels[(size_t)y*mask->width+x].colour==colour);
        if (++count>limit) return count;if (emit) emit(context,mask->x+start,mask->y+y,x-start,colour);
    }
    return count;
}
// Merge identical horizontal runs in adjacent rows. The resulting rectangles
// reproduce every classified pixel exactly; no dilation, sampling or averaging.
static inline uint32_t CMPRectangles(const CMPMask *mask,CMPRectangle *out,uint32_t capacity) {
    if(!mask->pixels||!out||mask->width<1||mask->width>8192||mask->height<1||mask->height>8192)return capacity+1;
    uint32_t *storage=malloc((size_t)mask->width*2*sizeof(uint32_t));if(!storage)return capacity+1;
    uint32_t *previous=storage,*current=storage+mask->width,previousCount=0,count=0;
    for(int y=0;y<mask->height;y++){
        uint32_t currentCount=0,cursor=0;
        for(int x=0;x<mask->width;){uint8_t colour=mask->pixels[(size_t)y*mask->width+x].colour;if(!colour){x++;continue;}
            int start=x;do{x++;}while(x<mask->width&&mask->pixels[(size_t)y*mask->width+x].colour==colour);
            int absoluteX=mask->x+start,width=x-start;while(cursor<previousCount&&out[previous[cursor]].x<absoluteX)cursor++;
            uint32_t index=count;
            if(cursor<previousCount){CMPRectangle *r=&out[previous[cursor]];if(r->x==absoluteX&&r->width==width&&r->colour==colour){index=previous[cursor];r->height++;}}
            if(index==count){if(count==capacity){free(storage);return capacity+1;}out[count++]=(CMPRectangle){absoluteX,mask->y+y,width,1,colour};}
            current[currentCount++]=index;
        }
        uint32_t *swap=previous;previous=current;current=swap;previousCount=currentCount;
    }
    free(storage);return count;
}
#endif
