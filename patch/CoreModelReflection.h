#ifndef CORE_MODEL_REFLECTION_H
#define CORE_MODEL_REFLECTION_H
#include "CoreModelNames.h"
#include "CoreModelReferences.h"

// Legacy UObject/UField/UStruct layout, independently guarded by object headers,
// named property classes, scalar dimensions and enclosing structure sizes.
typedef struct { uint32_t offset;uint8_t mask;bool found; } CMFBool;
typedef struct {
    CMFBool visible,hidden,hide_skin;
    uint32_t forced_lod,predicted_lod,min_lod;
    bool forced_found,predicted_found,min_found;
    uint32_t predicted_source; // 1: named IntProperty; 2: proven native getter.
    uint64_t predicted_getter;
    uint32_t reads,failures,fields,classes;
    uint32_t name_reads,name_failures,steps,class_fields[12];
    bool truncated,scan_finished,field_limit_reached,read_limit_reached,name_limit_reached;
    char class_names[12][48];
} CMFSchema;
static inline bool CMFRead(CMFSchema *s,CMRReader reader,uint64_t address,void *out,size_t size) {
    if (!CMRPointer(address,size)) return false;
    if (s->reads>=896) { s->truncated=true;s->read_limit_reached=true;return false; }s->reads++;
    if (reader(address,out,size)) return true;s->failures++;return false;
}
// Accept only a complete straight-line ARM64 getter: optional pointer-auth
// entry, the exact generated FFrame::Code increment, one int32 load from this,
// one store to RESULT_PARAM and return. Calls, branches, register clobbers and
// larger functions remain unknown. This reads code; it never calls game code.
static inline bool CMFDecodeLODGetter(const uint8_t *code,size_t bytes,uint32_t ownerSize,uint32_t *offset) {
    size_t p=0;uint32_t instruction=0;
#define CMF_WORD() (memcpy(&instruction,code+p,4),instruction)
    if(bytes<12)return false;
    if(CMF_WORD()==0xD503245F||instruction==0xD503201F)p+=4;
    if(p+4>bytes)return false;bool authenticated=CMF_WORD()==0xD503237F;if(authenticated)p+=4;
    if(p+16<=bytes&&CMF_WORD()==0xF9401028){const uint32_t frame[4]={0xF9401028,0xF100011F,0x9A880508,0xF9001028};if(memcmp(code+p,frame,16))return false;p+=16;}
    if(p+12>bytes)return false;uint32_t load=CMF_WORD(),reg=load&31;
    if((load&0xFFC003E0U)!=0xB9400000U||reg<3||reg>17)return false;
    uint32_t field=((load>>10)&4095)*4;p+=4;
    if(CMF_WORD()!=(0xB9000040U|reg))return false;p+=4;
    uint32_t end=CMF_WORD();
    if(authenticated){if(end==0xD50323FF){p+=4;if(p+4>bytes||CMF_WORD()!=0xD65F03C0)return false;}else if(end!=0xD65F0FFF)return false;}
    else if(end!=0xD65F03C0)return false;
    if(field<40||field+4>ownerSize||field+4>4096)return false;*offset=field;return true;
#undef CMF_WORD
}
static inline bool CMFNativeLODGetter(uint64_t address,uint64_t owner,uint32_t ownerSize,CMRReader reader,CMNNames *names,CMFSchema *s) {
    uint8_t function[192]={0},property[116]={0},code[128]={0};uint64_t child=0,entry=0,next=0,flags=0;uint32_t functionFlags=0,size=0;
    if(!CMFRead(s,reader,address,function,sizeof(function)))return false;
    CMRHeader h;memcpy(&h,function,sizeof(h));if(h.outer_address!=owner)return false;
    memcpy(&child,function+0x38,8);memcpy(&size,function+0x40,4);memcpy(&functionFlags,function+0x88,4);memcpy(&entry,function+0xB0,8);
    if(size!=4||!(functionFlags&0x400)||entry<0x100000000ULL||entry>=(1ULL<<39)||entry&3||!CMFRead(s,reader,child,property,sizeof(property)))return false;
    CMRHeader ret,cls;memcpy(&ret,property,sizeof(ret));if(!CMRHeaderCandidate(&ret)||ret.outer_address!=address||!CMFRead(s,reader,ret.class_address,&cls,sizeof(cls))||!CMRHeaderCandidate(&cls))return false;
    uint32_t dim=0,element=0,offset=0;memcpy(&dim,property+0x30,4);memcpy(&element,property+0x34,4);memcpy(&flags,property+0x38,8);memcpy(&offset,property+0x44,4);memcpy(&next,property+0x28,8);
    CMNAdd(names,reader,ret.name_index);CMNAdd(names,reader,cls.name_index);const char *name=CMNText(names,ret.name_index),*type=CMNText(names,cls.name_index);
    if(!name||strcmp(name,"ReturnValue")||!type||strcmp(type,"IntProperty")||dim!=1||element!=4||offset||next||(flags&0x480)!=0x480)return false;
    if(!CMFRead(s,reader,entry&~7ULL,code,sizeof(code))||!CMFDecodeLODGetter(code+(entry&7),sizeof(code)-(entry&7),ownerSize,&offset))return false;
    s->predicted_lod=offset;s->predicted_found=true;s->predicted_source=2;s->predicted_getter=entry;return true;
}
typedef struct {
    CMFSchema schema;
    uint64_t next_class,classes[12],children[12],seen_fields[384];
    uint32_t sizes[12],seen_count,rank,level,type_count;
    uint8_t priority[12];
    struct { uint64_t address;uint32_t name; } types[32];
    bool chain_finished,done;
} CMFScan;
static inline void CMFBegin(CMFScan *scan,uint64_t classAddress) {memset(scan,0,sizeof(*scan));scan->next_class=classAddress;}
static inline void CMFProcessField(CMFScan *scan,uint64_t address,uint32_t level,CMRReader reader,CMNNames *names) {
    CMFSchema *schema=&scan->schema;uint8_t field[116]={0};uint32_t size=scan->sizes[level];
    if(!CMFRead(schema,reader,address,field,sizeof(field))){schema->truncated=true;scan->children[level]=0;return;}
    CMRHeader h;memcpy(&h,field,sizeof(h));if(!CMRHeaderCandidate(&h)){schema->truncated=true;scan->children[level]=0;return;}
    schema->fields++;schema->class_fields[level]++;memcpy(&scan->children[level],field+0x28,8);
    uint32_t typeName=0;bool cached=false;
    for(uint32_t i=0;i<scan->type_count;i++)if(scan->types[i].address==h.class_address){typeName=scan->types[i].name;cached=true;break;}
    if(!cached){CMRHeader propertyClass;if(!CMFRead(schema,reader,h.class_address,&propertyClass,sizeof(propertyClass))||!CMRHeaderCandidate(&propertyClass)){schema->truncated=true;return;}
        typeName=propertyClass.name_index;if(scan->type_count<32){scan->types[scan->type_count].address=h.class_address;scan->types[scan->type_count++].name=typeName;}}
    CMNAdd(names,reader,typeName);const char *type=CMNText(names,typeName);
    if(type&&!strcmp(type,"Function")&&scan->priority[level]==1&&!schema->predicted_found){CMNAdd(names,reader,h.name_index);const char *name=CMNText(names,h.name_index);
        if(name&&!strcmp(name,"GetPredictedLODLevel"))CMFNativeLODGetter(address,scan->classes[level],size,reader,names,schema);}
    if(!type||(strcmp(type,"BoolProperty")&&strcmp(type,"IntProperty")))return;
    int32_t dim=0,element=0,offset=0;memcpy(&dim,field+0x30,4);memcpy(&element,field+0x34,4);memcpy(&offset,field+0x44,4);
    if(dim!=1||offset<40||(uint32_t)offset>=size||offset>=4096)return;
    CMNAdd(names,reader,h.name_index);const char *name=CMNText(names,h.name_index);if(!name)return;
    if(!strcmp(type,"BoolProperty")&&element==1&&field[0x70]==1&&field[0x71]==0&&field[0x73]&&(field[0x73]==255||!(field[0x73]&(field[0x73]-1)))){
        CMFBool value={(uint32_t)offset,field[0x73],true};
        if(!strcmp(name,"bVisible")&&!schema->visible.found)schema->visible=value;
        else if(!strcmp(name,"bHiddenInGame")&&!schema->hidden.found)schema->hidden=value;
        else if(!strcmp(name,"bHideSkin")&&!schema->hide_skin.found)schema->hide_skin=value;
    }else if(!strcmp(type,"IntProperty")&&element==4&&(uint32_t)offset+4<=size){
        if(!strcmp(name,"ForcedLodModel")){schema->forced_lod=(uint32_t)offset;schema->forced_found=true;}
        else if(!strcmp(name,"PredictedLODLevel")){schema->predicted_lod=(uint32_t)offset;schema->predicted_found=true;schema->predicted_source=1;schema->predicted_getter=0;}
        else if(!strcmp(name,"MinLodModel")){schema->min_lod=(uint32_t)offset;schema->min_found=true;}
    }
}
// Each step consumes at most `units` class/field nodes. Resume the same cursor
// next frame rather than permanently caching the result of a timed-out scan.
// SceneComponent and SkinnedMeshComponent retain priority within the unchanged
// 384-field / 896-header-read session caps; no separate 192-field owner cutoff.
static inline bool CMFStep(CMFScan *scan,CMRReader reader,CMNNames *names,uint32_t units) {
    if(scan->done)return true;CMFSchema *s=&scan->schema;s->steps++;
    while(units&&!scan->done){
        if(!scan->chain_finished){
            uint32_t level=s->classes;
            if(!scan->next_class||level==12){if(scan->next_class)s->truncated=true;scan->chain_finished=true;continue;}
            units--;uint64_t address=scan->next_class;
            for(uint32_t j=0;j<level;j++)if(scan->classes[j]==address){s->truncated=true;scan->done=true;break;}if(scan->done)break;
            uint8_t cls[80]={0};if(!CMFRead(s,reader,address,cls,sizeof(cls))){s->truncated=true;scan->done=true;break;}
            CMRHeader h;memcpy(&h,cls,sizeof(h));uint32_t size=0;memcpy(&size,cls+0x40,4);
            if(!CMRHeaderCandidate(&h)||size<40||size>16384){s->truncated=true;scan->done=true;break;}
            scan->classes[level]=address;scan->sizes[level]=size;memcpy(&scan->children[level],cls+0x38,8);memcpy(&scan->next_class,cls+0x30,8);s->classes++;
            CMNAdd(names,reader,h.name_index);const char *name=CMNText(names,h.name_index);scan->priority[level]=2;
            if(name){size_t length=strlen(name);if(length>47)length=47;memcpy(s->class_names[level],name,length);if(!strcmp(name,"SceneComponent"))scan->priority[level]=0;else if(!strcmp(name,"SkinnedMeshComponent"))scan->priority[level]=1;}
            continue;
        }
        if(scan->rank==3){scan->done=true;break;}
        if(scan->level>=s->classes){scan->level=0;scan->rank++;continue;}
        uint32_t level=scan->level;uint64_t next=scan->children[level];
        if(scan->priority[level]!=scan->rank||!next){scan->level++;continue;}
        if(scan->seen_count==384){s->truncated=true;s->field_limit_reached=true;scan->done=true;break;}
        for(uint32_t j=0;j<scan->seen_count;j++)if(scan->seen_fields[j]==next){s->truncated=true;scan->done=true;break;}if(scan->done)break;
        units--;scan->seen_fields[scan->seen_count++]=next;CMFProcessField(scan,next,level,reader,names);
        if(s->read_limit_reached)scan->done=true;
    }
    s->name_reads=names->read_attempts;s->name_failures=names->read_failures;s->name_limit_reached=names->truncated;
    if(s->name_limit_reached||s->name_failures)s->truncated=true;s->scan_finished=scan->done;return scan->done;
}
static inline void CMFCollect(uint64_t classAddress,CMRReader reader,CMNNames *names,CMFSchema *schema) {
    CMFScan scan;CMFBegin(&scan,classAddress);while(!CMFStep(&scan,reader,names,16)){}*schema=scan.schema;
}
static inline bool CMFVisibility(const CMFSchema *schema,const uint8_t *prefix,size_t size,bool *visible) {
    if (!schema->visible.found||!schema->hidden.found||schema->visible.offset>=size||schema->hidden.offset>=size) return false;
    if(schema->hide_skin.found&&schema->hide_skin.offset>=size)return false;
    *visible=(prefix[schema->visible.offset]&schema->visible.mask)&&!(prefix[schema->hidden.offset]&schema->hidden.mask)&&
        !(schema->hide_skin.found&&(prefix[schema->hide_skin.offset]&schema->hide_skin.mask));return true;
}
static inline uint32_t CMFLOD(const CMFSchema *schema,const uint8_t *prefix,size_t size,uint32_t count,bool *verified) {
    *verified=false;if (!count) return 0;
    int32_t value=0;
    if (schema->forced_found&&schema->forced_lod+4<=size) {memcpy(&value,prefix+schema->forced_lod,4);if (value>0&&(uint32_t)value<=count){*verified=true;return (uint32_t)value-1;}}
    if (schema->predicted_found&&schema->predicted_lod+4<=size) {memcpy(&value,prefix+schema->predicted_lod,4);if (value>=0&&(uint32_t)value<count){*verified=true;return (uint32_t)value;}}
    return 0; // Explicitly reported highest-detail fallback, not actual LOD proof.
}
#endif
