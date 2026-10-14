#ifndef CORE_MODEL_NAMES_H
#define CORE_MODEL_NAMES_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Mirrors the pinned Core D6794 name lookup without calling its mutable cache.
// The table slot and entry text offset come from Core's live profile, not a
// guessed game offset. Text is accepted only when terminated and ASCII.
#define CMN_NAME_LIMIT 256
#define CMN_CHUNK_LIMIT 64
#define CMN_READ_LIMIT 640
#define CMN_TEXT_BYTES 256
typedef bool (*CMNReader)(uint64_t,void *,size_t);
typedef struct {
    uint32_t index;
    uint64_t entry_address;
    bool valid;
    char text[CMN_TEXT_BYTES];
} CMNName;
typedef struct {
    uint64_t table_address;
    uint32_t text_offset,count,chunk_count,read_attempts,read_failures,rejected_text;
    bool available,truncated;
    struct { uint32_t index;uint64_t address; } chunks[CMN_CHUNK_LIMIT];
    CMNName names[CMN_NAME_LIMIT];
} CMNNames;
static inline const char *CMNText(const CMNNames *out,uint32_t index) {
    for (uint32_t i=0;i<out->count;i++) if (out->names[i].index==index && out->names[i].valid) return out->names[i].text;
    return NULL;
}
static inline bool CMNRange(uint64_t address,size_t size) {
    return address>=0x100000000ULL && address<(1ULL<<39) && size &&
        size<=CMN_TEXT_BYTES && size<=(1ULL<<39)-address;
}
static inline bool CMNRead(CMNNames *out,CMNReader reader,uint64_t address,void *buffer,size_t size) {
    if (!CMNRange(address,size)) return false;
    if (out->read_attempts>=CMN_READ_LIMIT) { out->truncated=true;return false; }
    out->read_attempts++;
    if (reader(address,buffer,size)) return true;
    out->read_failures++;return false;
}
static inline void CMNBegin(CMNNames *out,CMNReader reader,uint64_t tableSlot,uint64_t textOffset) {
    memset(out,0,sizeof(*out));
    if (!reader || (tableSlot&7) || textOffset>512) return;
    out->text_offset=(uint32_t)textOffset;
    if (CMNRead(out,reader,tableSlot,&out->table_address,8) && !(out->table_address&7) && CMNRange(out->table_address,8))
        out->available=true;
}
static inline void CMNAdd(CMNNames *out,CMNReader reader,uint32_t index) {
    if (!out->available || index>=(1U<<27)) return;
    for (uint32_t i=0;i<out->count;i++) if (out->names[i].index==index) return;
    if (out->count>=CMN_NAME_LIMIT) { out->truncated=true;return; }
    CMNName *name=&out->names[out->count++];name->index=index;
    uint32_t chunkIndex=index>>14;uint64_t chunk=0;
    bool cached=false;
    for (uint32_t i=0;i<out->chunk_count;i++) if (out->chunks[i].index==chunkIndex) {
        chunk=out->chunks[i].address;cached=true;break;
    }
    if (!cached) {
        if (out->chunk_count>=CMN_CHUNK_LIMIT) { out->truncated=true;return; }
        if (!CMNRead(out,reader,out->table_address+(uint64_t)chunkIndex*8,&chunk,8)) return;
        out->chunks[out->chunk_count].index=chunkIndex;
        out->chunks[out->chunk_count++].address=chunk;
    }
    if ((chunk&7) || !CMNRange(chunk,8) ||
        !CMNRead(out,reader,chunk+(uint64_t)(index&0x3FFF)*8,&name->entry_address,8) ||
        !CMNRange(name->entry_address,8) || (name->entry_address&7)) return;
    char text[CMN_TEXT_BYTES]={0};
    if (!CMNRead(out,reader,name->entry_address+out->text_offset,text,sizeof(text))) return;
    size_t length=0;
    while (length<sizeof(text) && text[length]) {
        unsigned char c=(unsigned char)text[length];
        if (c<32 || c>126) { out->rejected_text++;return; }
        length++;
    }
    if (!length || length==sizeof(text)) { out->rejected_text++;return; }
    memcpy(name->text,text,length+1);name->valid=true;
}
#endif
