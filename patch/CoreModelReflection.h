#ifndef CORE_MODEL_REFLECTION_H
#define CORE_MODEL_REFLECTION_H
#include "CoreModelNames.h"

// Legacy UObject/UField/UStruct layout, independently guarded by object headers,
// named property classes, scalar dimensions and enclosing structure sizes.
typedef struct { uint32_t offset;uint8_t mask;bool found; } CMFBool;
typedef struct {
    CMFBool visible,hidden;
    uint32_t forced_lod,predicted_lod,min_lod;
    bool forced_found,predicted_found,min_found;
    uint32_t reads,failures,fields,classes;
    bool truncated;
} CMFSchema;
static inline bool CMFRead(CMFSchema *s,CMRReader reader,uint64_t address,void *out,size_t size) {
    if (!CMRPointer(address,size)) return false;
    if (s->reads>=896) { s->truncated=true;return false; }s->reads++;
    if (reader(address,out,size)) return true;s->failures++;return false;
}
static inline void CMFCollect(uint64_t classAddress,CMRReader reader,CMNNames *names,CMFSchema *schema) {
    memset(schema,0,sizeof(*schema));uint64_t seenClasses[8]={0},seenFields[384]={0};uint32_t fields=0;
    for (unsigned level=0;classAddress&&level<8;level++) {
        for (unsigned j=0;j<level;j++) if (seenClasses[j]==classAddress) { schema->truncated=true;return; }
        seenClasses[level]=classAddress;uint8_t cls[80]={0};if (!CMFRead(schema,reader,classAddress,cls,sizeof(cls))) return;
        CMRHeader ch;memcpy(&ch,cls,sizeof(ch));if (!CMRHeaderCandidate(&ch)) return;
        uint64_t super=0,next=0;uint32_t size=0;memcpy(&super,cls+0x30,8);memcpy(&next,cls+0x38,8);memcpy(&size,cls+0x40,4);
        if (size<40||size>16384) return;schema->classes++;
        for (unsigned j=0;next&&j<192;j++) {
            if (fields>=384) { schema->truncated=true;return; }
            for (uint32_t k=0;k<fields;k++) if (seenFields[k]==next) { schema->truncated=true;return; }
            seenFields[fields++]=next;uint8_t field[116]={0};if (!CMFRead(schema,reader,next,field,sizeof(field))) break;
            CMRHeader h,propertyClass;memcpy(&h,field,sizeof(h));if (!CMRHeaderCandidate(&h)) break;schema->fields++;
            memcpy(&next,field+0x28,8);
            if (!CMFRead(schema,reader,h.class_address,&propertyClass,sizeof(propertyClass))||!CMRHeaderCandidate(&propertyClass)) continue;
            CMNAdd(names,reader,propertyClass.name_index);const char *type=CMNText(names,propertyClass.name_index);
            if (!type||(strcmp(type,"BoolProperty")&&strcmp(type,"IntProperty"))) continue;
            int32_t dim=0,element=0,offset=0;memcpy(&dim,field+0x30,4);memcpy(&element,field+0x34,4);memcpy(&offset,field+0x44,4);
            if (dim!=1||offset<40||(uint32_t)offset>=size||offset>=4096) continue;
            CMNAdd(names,reader,h.name_index);const char *name=CMNText(names,h.name_index);if (!name) continue;
            if (!strcmp(type,"BoolProperty")&&element==1&&field[0x70]==1&&field[0x71]==0&&field[0x73]&&!(field[0x73]&(field[0x73]-1))) {
                CMFBool value={(uint32_t)offset,field[0x73],true};
                if (!strcmp(name,"bVisible")) schema->visible=value;
                else if (!strcmp(name,"bHiddenInGame")) schema->hidden=value;
            } else if (!strcmp(type,"IntProperty")&&element==4&&(uint32_t)offset+4<=size) {
                if (!strcmp(name,"ForcedLodModel")) {schema->forced_lod=(uint32_t)offset;schema->forced_found=true;}
                else if (!strcmp(name,"PredictedLODLevel")) {schema->predicted_lod=(uint32_t)offset;schema->predicted_found=true;}
                else if (!strcmp(name,"MinLodModel")) {schema->min_lod=(uint32_t)offset;schema->min_found=true;}
            }
        }
        classAddress=super;
    }
    if (classAddress) schema->truncated=true;
}
static inline bool CMFVisibility(const CMFSchema *schema,const uint8_t *prefix,size_t size,bool *visible) {
    if (!schema->visible.found||!schema->hidden.found||schema->visible.offset>=size||schema->hidden.offset>=size) return false;
    *visible=(prefix[schema->visible.offset]&schema->visible.mask)&&!(prefix[schema->hidden.offset]&schema->hidden.mask);return true;
}
static inline uint32_t CMFLOD(const CMFSchema *schema,const uint8_t *prefix,size_t size,uint32_t count,bool *verified) {
    *verified=false;if (!count) return 0;
    int32_t value=0;
    if (schema->forced_found&&schema->forced_lod+4<=size) {memcpy(&value,prefix+schema->forced_lod,4);if (value>0&&(uint32_t)value<=count){*verified=true;return (uint32_t)value-1;}}
    if (schema->predicted_found&&schema->predicted_lod+4<=size) {memcpy(&value,prefix+schema->predicted_lod,4);if (value>=0&&(uint32_t)value<count){*verified=true;return (uint32_t)value;}}
    return 0; // Explicitly reported highest-detail fallback, not actual LOD proof.
}
#endif
