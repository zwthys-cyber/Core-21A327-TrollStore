#ifndef CORE_MODEL_REFERENCES_H
#define CORE_MODEL_REFERENCES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bounded, untyped evidence for identifying references in the located component.
// Matching this header shape is not proof of a UObject class or mesh binding.
#define CMR_REFERENCE_LIMIT 12
#define CMR_READ_LIMIT 40
#define CMR_PREFIX_BYTES 4096
typedef bool (*CMRReader)(uint64_t, void *, size_t);
typedef struct {
    uint64_t vtable;
    uint32_t flags;
    int32_t internal_index;
    uint64_t class_address;
    uint32_t name_index, name_number;
    uint64_t outer_address;
} CMRHeader;
_Static_assert(sizeof(CMRHeader)==40, "Candidate UObject header layout");
typedef struct {
    uint64_t address;
    uint32_t source_offset, array_index;
    bool class_header_valid;
    bool class_size_hint_valid;
    uint32_t prefix_bytes, class_size_hint, class_alignment_hint;
    CMRHeader header, class_header;
    uint8_t prefix[CMR_PREFIX_BYTES];
} CMRReference;
typedef struct {
    uint32_t count, read_attempts, read_failures, rejected_headers, rejected_arrays;
    bool truncated;
    CMRReference references[CMR_REFERENCE_LIMIT];
} CMRReferences;

static bool CMRPointer(uint64_t address, size_t size) {
    return address>=0x100000000ULL && address<(1ULL<<39) && !(address&7) &&
        size && size<=4096 && size<=(1ULL<<39)-address;
}
static bool CMRHeaderCandidate(const CMRHeader *h) {
    return CMRPointer(h->vtable,8) && CMRPointer(h->class_address,40) &&
        h->internal_index>=0 && h->internal_index<(1<<27) &&
        h->name_index>0 && h->name_index<(1U<<27) && h->name_number<65536 &&
        (!h->outer_address || CMRPointer(h->outer_address,40));
}
static bool CMRRead(CMRReferences *out, CMRReader reader, uint64_t address, void *buffer, size_t size) {
    if (!CMRPointer(address,size)) return false;
    if (out->read_attempts>=CMR_READ_LIMIT) { out->truncated=true; return false; }
    out->read_attempts++;
    if (reader(address,buffer,size)) return true;
    out->read_failures++; return false;
}
static void CMRAdd(CMRReferences *out, CMRReader reader, uint64_t address, uint32_t offset, uint32_t arrayIndex) {
    if (!CMRPointer(address,sizeof(CMRHeader))) return;
    for (uint32_t i=0;i<out->count;i++) if (out->references[i].address==address) return;
    if (out->count>=CMR_REFERENCE_LIMIT || out->read_attempts>=CMR_READ_LIMIT) { out->truncated=true; return; }
    CMRReference *ref=&out->references[out->count];
    memset(ref,0,sizeof(*ref));
    if (!CMRRead(out,reader,address,&ref->header,sizeof(ref->header))) return;
    if (!CMRHeaderCandidate(&ref->header)) { out->rejected_headers++; return; }
    ref->address=address; ref->source_offset=offset; ref->array_index=arrayIndex;
    memcpy(ref->prefix,&ref->header,sizeof(ref->header));ref->prefix_bytes=sizeof(ref->header);
    // This is a layout hint observed in the captured root class, not semantic
    // type verification. Invalid hints leave only the already read header.
    uint8_t classPrefix[80]={0};
    if (CMRRead(out,reader,ref->header.class_address,classPrefix,sizeof(classPrefix))) {
        memcpy(&ref->class_header,classPrefix,sizeof(ref->class_header));
        ref->class_header_valid=CMRHeaderCandidate(&ref->class_header);
        memcpy(&ref->class_size_hint,classPrefix+0x40,4);memcpy(&ref->class_alignment_hint,classPrefix+0x44,4);
        uint32_t align=ref->class_alignment_hint,size=ref->class_size_hint;
        ref->class_size_hint_valid=ref->class_header_valid && size>=sizeof(CMRHeader) && size<=16384 &&
            align && align<=16 && !(align&(align-1));
        if (ref->class_size_hint_valid) {
            uint32_t desired=(offset==0x828 || offset==0x170) ? CMR_PREFIX_BYTES : 1024;
            if (desired>size) desired=size;
            if (CMRRead(out,reader,address,ref->prefix,desired)) ref->prefix_bytes=desired;
            else {
                // Core clears the destination before attempting a remote read.
                // Keep the independently validated header if this larger read
                // fails or partially writes the destination.
                memcpy(ref->prefix,&ref->header,sizeof(ref->header));
            }
        }
    }
    out->count++;
}
static void CMRCollect(const uint8_t *layout, size_t length, CMRReader reader, CMRReferences *out) {
    memset(out,0,sizeof(*out));
    if (!layout || length<2048 || length>4096 || !reader) return;
    // These slots were observed as references/array shapes in the v3 report.
    // Their semantic types remain unverified. No referenced object is scanned
    // for further pointers; only its own class header is sampled.
    const uint32_t slots[]={0x828,0x10};
    for (size_t i=0;i<sizeof(slots)/sizeof(slots[0]);i++) {
        uint64_t address=0; memcpy(&address,layout+slots[i],8);
        CMRAdd(out,reader,address,slots[i],UINT32_MAX);
    }
    const uint32_t arrays[]={0x170,0x7D0};
    for (size_t i=0;i<sizeof(arrays)/sizeof(arrays[0]);i++) {
        uint64_t address=0; int32_t count=0,capacity=0;
        memcpy(&address,layout+arrays[i],8);memcpy(&count,layout+arrays[i]+8,4);memcpy(&capacity,layout+arrays[i]+12,4);
        if (!count && !capacity) continue;
        if (count<1 || count>32 || capacity<count || capacity>128 || !CMRPointer(address,(size_t)count*8)) {
            out->rejected_arrays++; continue;
        }
        uint64_t entries[8]={0};size_t limit=arrays[i]==0x170?8:4;
        size_t take=(size_t)count<limit?(size_t)count:limit;
        if ((size_t)count>take) out->truncated=true;
        if (!CMRRead(out,reader,address,entries,take*8)) continue;
        for (size_t j=0;j<take;j++) CMRAdd(out,reader,entries[j],arrays[i],(uint32_t)j);
    }
}
#endif
