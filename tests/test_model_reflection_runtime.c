#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelReflection.h"
static const uint64_t Root=0x200000000ULL,Parent=0x200010000ULL,Forced=0x300000000ULL,Visible=0x300010000ULL,Hidden=0x300020000ULL,Bool=0x400000000ULL,Int=0x400010000ULL;
static bool Cycle,WrongType;
static CMRHeader Header(uint32_t name,uint64_t cls){CMRHeader h={0};h.vtable=0x100001000ULL;h.internal_index=1;h.class_address=cls;h.name_index=name;return h;}
static bool Reader(uint64_t a,void *out,size_t size){memset(out,0,size);
    if((a==Root||a==Parent)&&size==80){CMRHeader h=Header(10,Bool);memcpy(out,&h,40);uint64_t super=a==Root?Parent:0,children=a==Root?Forced:Visible;uint32_t bytes=4096;memcpy((uint8_t *)out+0x30,&super,8);memcpy((uint8_t *)out+0x38,&children,8);memcpy((uint8_t *)out+0x40,&bytes,4);return true;}
    if((a==Bool||a==Int)&&size==40){CMRHeader h=Header(a==Bool?1:2,Bool);memcpy(out,&h,40);return true;}
    if((a==Forced||a==Visible||a==Hidden)&&size==116){bool boolean=a!=Forced;CMRHeader h=Header(a==Forced?5:a==Visible?3:4,boolean&&!WrongType?Bool:Int);memcpy(out,&h,40);uint64_t next=a==Visible?(Cycle?Visible:Hidden):0;uint32_t dim=1,element=boolean?1:4,offset=a==Forced?0x900:a==Visible?0x100:0x101;memcpy((uint8_t *)out+0x28,&next,8);memcpy((uint8_t *)out+0x30,&dim,4);memcpy((uint8_t *)out+0x34,&element,4);memcpy((uint8_t *)out+0x44,&offset,4);((uint8_t *)out)[0x70]=1;((uint8_t *)out)[0x72]=((uint8_t *)out)[0x73]=1;return true;}
    return false;
}
int main(void){uint8_t empty[16]={0};CMRReferences refs;CMRCollect(empty,sizeof(empty),Reader,&refs);CMNNames names={0};const char *text[]={"BoolProperty","IntProperty","bVisible","bHiddenInGame","ForcedLodModel"};names.count=5;for(unsigned i=0;i<5;i++){names.names[i].index=i+1;names.names[i].valid=true;strcpy(names.names[i].text,text[i]);}
    CMFSchema schema;CMFCollect(Root,Reader,&names,&schema);assert(schema.classes==2&&schema.fields==3&&schema.visible.found&&schema.hidden.found&&schema.forced_found&&!schema.truncated);
    uint8_t state[4096]={0};state[0x100]=1;bool visible=false;assert(CMFVisibility(&schema,state,sizeof(state),&visible)&&visible);state[0x101]=1;assert(CMFVisibility(&schema,state,sizeof(state),&visible)&&!visible);
    int32_t lod=2;memcpy(state+0x900,&lod,4);bool verified=false;assert(CMFLOD(&schema,state,sizeof(state),3,&verified)==1&&verified);
    lod=99;memcpy(state+0x900,&lod,4);assert(CMFLOD(&schema,state,sizeof(state),3,&verified)==0&&!verified);
    Cycle=true;CMFCollect(Root,Reader,&names,&schema);assert(schema.truncated&&!schema.hidden.found);Cycle=false;WrongType=true;CMFCollect(Root,Reader,&names,&schema);assert(!schema.visible.found&&!CMFVisibility(&schema,state,sizeof(state),&visible));
    puts("PASS: guarded legacy property reflection; superclass fields; visibility bits; one-based forced LOD; out-of-range fallback remains unverified; field cycles and mismatched property types reject");return 0;}
