#include <assert.h>
#include <stdio.h>
#include "../patch/CoreModelReflection.h"
static const uint64_t Root=0x200000000ULL,Parent=0x200010000ULL,Middle=0x200020000ULL,Forced=0x300000000ULL,Visible=0x300010000ULL,Hidden=0x300020000ULL,HideSkin=0x300030000ULL,Bool=0x400000000ULL,Int=0x400010000ULL,Dummy=0x500000000ULL,Function=0x600000000ULL,ReturnValue=0x600010000ULL,Code=0x700000004ULL;
static bool Cycle,WrongType,NativeBool,Crowded,BadReturn,LateGetter;
static unsigned TypeReads;
static const uint64_t FunctionClass=0x400020000ULL;
static CMRHeader Header(uint32_t name,uint64_t cls){CMRHeader h={0};h.vtable=0x100001000ULL;h.internal_index=1;h.class_address=cls;h.name_index=name;return h;}
static bool Reader(uint64_t a,void *out,size_t size){memset(out,0,size);
    if((a==Root||a==Parent||a==Middle)&&size==80){CMRHeader h=Header(a==Root?7:a==Parent?8:12,Bool);memcpy(out,&h,40);uint64_t super=a==Root?(Crowded?Middle:Parent):a==Middle?Parent:0,children=a==Root?(Crowded||LateGetter?Dummy:Forced):a==Middle?Dummy+192*256:Visible;uint32_t bytes=4096;memcpy((uint8_t *)out+0x30,&super,8);memcpy((uint8_t *)out+0x38,&children,8);memcpy((uint8_t *)out+0x40,&bytes,4);return true;}
    if((a==Bool||a==Int||a==FunctionClass)&&size==40){TypeReads++;CMRHeader h=Header(a==Bool?1:a==Int?2:9,Bool);memcpy(out,&h,40);return true;}
    if((a==Forced||a==Visible||a==Hidden||a==HideSkin)&&size==116){bool boolean=a!=Forced;CMRHeader h=Header(a==Forced?5:a==Visible?3:a==Hidden?4:6,boolean&&!WrongType?Bool:Int);memcpy(out,&h,40);uint64_t next=a==Visible?(Cycle?Visible:Hidden):a==Hidden?HideSkin:0;uint32_t dim=1,element=boolean?1:4,offset=a==Forced?0x900:a==Visible?0x100:a==Hidden?0x101:0x102;memcpy((uint8_t *)out+0x28,&next,8);memcpy((uint8_t *)out+0x30,&dim,4);memcpy((uint8_t *)out+0x34,&element,4);memcpy((uint8_t *)out+0x44,&offset,4);((uint8_t *)out)[0x70]=1;((uint8_t *)out)[0x72]=1;((uint8_t *)out)[0x73]=a==HideSkin?2:a==Visible&&NativeBool?255:1;return true;}
    if(a>=Dummy&&a<Dummy+384*256&&(a-Dummy)%256==0&&size==116){unsigned i=(unsigned)((a-Dummy)/256);CMRHeader h=Header(999,Int);memcpy(out,&h,40);uint64_t next=LateGetter?(i==209?Function:a+256):(i==191||i==383?0:a+256);uint32_t dim=1,element=4,offset=0xA00;memcpy((uint8_t *)out+0x28,&next,8);memcpy((uint8_t *)out+0x30,&dim,4);memcpy((uint8_t *)out+0x34,&element,4);memcpy((uint8_t *)out+0x44,&offset,4);return true;}
    if(a==Function&&size==116){CMRHeader h=Header(10,FunctionClass);h.outer_address=Root;memcpy(out,&h,40);return true;}
    if(a==Function&&size==192){CMRHeader h=Header(10,Bool);h.outer_address=Root;memcpy(out,&h,40);uint32_t bytes=4,native=0x400;memcpy((uint8_t *)out+0x38,&ReturnValue,8);memcpy((uint8_t *)out+0x40,&bytes,4);memcpy((uint8_t *)out+0x88,&native,4);memcpy((uint8_t *)out+0xB0,&Code,8);return true;}
    if(a==ReturnValue&&size==116){CMRHeader h=Header(11,Int);h.outer_address=Function;memcpy(out,&h,40);uint32_t dim=1,element=4;uint64_t flags=BadReturn?0x80:0x480;memcpy((uint8_t *)out+0x30,&dim,4);memcpy((uint8_t *)out+0x34,&element,4);memcpy((uint8_t *)out+0x38,&flags,8);return true;}
    if(a==(Code&~7ULL)&&size==128){uint32_t body[]={0xB9493409,0xB9000049,0xD65F03C0};memcpy((uint8_t *)out+4,body,sizeof(body));return true;}
    return false;
}
int main(void){uint8_t empty[16]={0};CMRReferences refs;CMRCollect(empty,sizeof(empty),Reader,&refs);CMNNames names={0};const char *text[]={"BoolProperty","IntProperty","bVisible","bHiddenInGame","ForcedLodModel","bHideSkin","SkinnedMeshComponent","SceneComponent","Function","GetPredictedLODLevel","ReturnValue","GameDerivedComponent"};names.count=12;for(unsigned i=0;i<12;i++){names.names[i].index=i+1;names.names[i].valid=true;strcpy(names.names[i].text,text[i]);}
    CMFSchema schema;CMFCollect(Root,Reader,&names,&schema);assert(schema.classes==2&&schema.fields==4&&schema.visible.found&&schema.hidden.found&&schema.hide_skin.found&&schema.forced_found&&!schema.truncated);
    uint8_t state[4096]={0};state[0x100]=1;bool visible=false;assert(CMFVisibility(&schema,state,sizeof(state),&visible)&&visible);state[0x101]=1;assert(CMFVisibility(&schema,state,sizeof(state),&visible)&&!visible);
    state[0x101]=0;state[0x102]=2;assert(CMFVisibility(&schema,state,sizeof(state),&visible)&&!visible);state[0x102]=0;
    NativeBool=true;CMFCollect(Root,Reader,&names,&schema);state[0x100]=2;assert(schema.visible.mask==255&&CMFVisibility(&schema,state,sizeof(state),&visible)&&visible);NativeBool=false;
    int32_t lod=2;memcpy(state+0x900,&lod,4);bool verified=false;assert(CMFLOD(&schema,state,sizeof(state),3,&verified)==1&&verified);
    lod=99;memcpy(state+0x900,&lod,4);assert(CMFLOD(&schema,state,sizeof(state),3,&verified)==0&&!verified);
    Cycle=true;CMFCollect(Root,Reader,&names,&schema);assert(schema.truncated&&!schema.hidden.found);Cycle=false;WrongType=true;CMFCollect(Root,Reader,&names,&schema);assert(!schema.visible.found&&!CMFVisibility(&schema,state,sizeof(state),&visible));
    WrongType=false;Crowded=true;CMFCollect(Root,Reader,&names,&schema);assert(schema.truncated&&schema.fields==384&&schema.visible.found&&schema.hidden.found&&schema.hide_skin.found);Crowded=false;
    LateGetter=true;TypeReads=0;CMFScan scan;CMFBegin(&scan,Root);unsigned steps=0;
    do{uint32_t fields=scan.schema.fields,reads=scan.schema.reads;bool done=CMFStep(&scan,Reader,&names,16);steps++;
        assert(scan.schema.fields-fields<=16&&scan.schema.reads-reads<=32);if(done)break;assert(steps<40);
    }while(true);
    schema=scan.schema;assert(steps>12&&schema.scan_finished&&!schema.truncated&&schema.fields==214&&schema.class_fields[0]==211&&schema.class_fields[1]==3&&schema.predicted_found&&schema.predicted_lod==0x934&&TypeReads==4);LateGetter=false;
    memset(&schema,0,sizeof(schema));assert(CMFNativeLODGetter(Function,Root,4096,Reader,&names,&schema)&&schema.predicted_found&&schema.predicted_source==2&&schema.predicted_lod==0x934&&schema.predicted_getter==Code);
    BadReturn=true;assert(!CMFNativeLODGetter(Function,Root,4096,Reader,&names,&schema));BadReturn=false;assert(!CMFNativeLODGetter(Function,Parent,4096,Reader,&names,&schema));
    uint32_t body[]={0xD503237F,0xF9401028,0xF100011F,0x9A880508,0xF9001028,0xB9493409,0xB9000049,0xD65F0FFF},offset=0;
    assert(CMFDecodeLODGetter((const uint8_t *)body,sizeof(body),4096,&offset)&&offset==0x934);
    body[2]=0xAA0103E0;assert(!CMFDecodeLODGetter((const uint8_t *)body,sizeof(body),4096,&offset));
    assert(!CMFDecodeLODGetter((const uint8_t *)(body+5),12,128,&offset));body[6]=0xB9000029;assert(!CMFDecodeLODGetter((const uint8_t *)(body+5),12,4096,&offset));
    puts("PASS: guarded legacy property reflection; bounded resumable steps and cached property classes; typed LOD getter beyond field 192; superclass visibility survives 384 unrelated fields; exact ARM64 decoding and strict return/owner checks; one-based forced LOD; cycles reject");return 0;}
