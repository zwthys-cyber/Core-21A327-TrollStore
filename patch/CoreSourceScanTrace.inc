// Observe the native snapshot once per original scan (normally three seconds).
// No additional game reads, retries, list retention, or rendering changes.
// Only previously classified bow pointers are watched; absence is not identity
// verification or an atomic observation of the game's changing actor array.
#define CSS_WATCH_LIMIT 256
#define CSS_RECORD_LIMIT 50000
#include <stdlib.h>
typedef struct {uintptr_t begin,end,capacity;} CSSVector;
typedef CSSVector (*CSSSnapshotFunction)(uint64_t,size_t);
typedef struct {uint64_t actor;bool found;} CSSWatch;
typedef struct {
    uint64_t sequence,time,chunkReads,chunkFailures,singleReads,singleFailures,zeroPointers;
    double nativeStamp;
    size_t requested,returned,watchCount;
    bool stampValid,vectorValid,watchesCopied;
    CSSWatch watches[CSS_WATCH_LIMIT];
} CSSScan;
static pthread_mutex_t CSSMutex=PTHREAD_MUTEX_INITIALIZER;
static CSSWatch CSSWatches[CSS_WATCH_LIMIT];
static size_t CSSWatchCount;
static CSSScan CSSLatest;
static atomic_uint_fast64_t CSSSequence,CSSDropped;
static atomic_uint_fast64_t CSSObserverTotal,CSSObserverMaximum;
static NSDictionary *CSSReport; // CMReportQueue only
typedef struct {bool active;uint64_t chunkReads,chunkFailures,singleReads,singleFailures;} CSSReadTracking;
static _Thread_local CSSReadTracking CSSReads;
static CSSSnapshotFunction CSSOriginalSnapshot;

static int CSSCompare(const void *a,const void *b) {
    uint64_t x=((const CSSWatch *)a)->actor,y=((const CSSWatch *)b)->actor;return (x>y)-(x<y);
}
// CMReportQueue only; keep locks bounded and avoid ever waiting on the draw.
static void CSSPublishWatches(NSDictionary<NSNumber *,NSMutableDictionary *> *targets) {
    CSSWatch watches[CSS_WATCH_LIMIT];size_t count=0;
    for(NSNumber *key in targets){if(count==CSS_WATCH_LIMIT)break;watches[count++]=(CSSWatch){.actor=key.unsignedLongLongValue};}
    qsort(watches,count,sizeof(*watches),CSSCompare);
    if(pthread_mutex_trylock(&CSSMutex)){atomic_fetch_add(&CSSDropped,1);return;}
    memcpy(CSSWatches,watches,count*sizeof(*watches));CSSWatchCount=count;pthread_mutex_unlock(&CSSMutex);
}
static bool CSSRead(uint64_t address,void *out,size_t size,bool single) {
    bool result=CMReadRemote(address,out,size); // original reader, exactly once
    if(CSSReads.active){if(single){CSSReads.singleReads++;if(!result)CSSReads.singleFailures++;}
        else{CSSReads.chunkReads++;if(!result)CSSReads.chunkFailures++;}}
    return result;
}
__attribute__((noinline)) static void CSSSnapshot(uint64_t address,size_t count,CSSVector *out) {
    int incoming=errno;CSSScan scan={0};scan.time=CMNow();scan.requested=count;scan.sequence=atomic_fetch_add(&CSSSequence,1)+1;
    scan.stampValid=CMSelfRead(CMBase+0xC4E158,&scan.nativeStamp,sizeof(scan.nativeStamp))&&isfinite(scan.nativeStamp);
    if(!pthread_mutex_trylock(&CSSMutex)){scan.watchCount=CSSWatchCount;scan.watchesCopied=true;memcpy(scan.watches,CSSWatches,scan.watchCount*sizeof(*scan.watches));pthread_mutex_unlock(&CSSMutex);}
    CSSReadTracking parent=CSSReads;CSSReads=(CSSReadTracking){.active=true};
    CSSSnapshotFunction original=CSSOriginalSnapshot ?: (CSSSnapshotFunction)CMCallable(CMBase+0xD632C);
    uint64_t originalStart=CMNow();errno=incoming;CSSVector vector=original(address,count);int forwarded=errno;uint64_t originalEnd=CMNow();
    scan.chunkReads=CSSReads.chunkReads;scan.chunkFailures=CSSReads.chunkFailures;scan.singleReads=CSSReads.singleReads;scan.singleFailures=CSSReads.singleFailures;CSSReads=parent;
    // A plain C 24-byte aggregate uses the same ARM64 indirect-result ABI as
    // this native std::vector. Transfer its three words; do not destroy it.
    memcpy(out,&vector,sizeof(vector));
    if(vector.end>=vector.begin&&(vector.end-vector.begin)%8==0&&vector.capacity>=vector.end){
        scan.returned=(vector.end-vector.begin)/8;
        scan.vectorValid=scan.returned==count&&count<=CSS_RECORD_LIMIT&&(!count||vector.begin>=4096);
        if(scan.vectorValid){const uint64_t *actors=(const void *)vector.begin;
            for(size_t i=0;i<count;i++){uint64_t actor=actors[i];if(!actor)scan.zeroPointers++;
                CSSWatch key={.actor=actor};CSSWatch *watch=bsearch(&key,scan.watches,scan.watchCount,sizeof(*scan.watches),CSSCompare);if(watch)watch->found=true;}}
    }
    if(!pthread_mutex_trylock(&CSSMutex)){CSSLatest=scan;pthread_mutex_unlock(&CSSMutex);}else atomic_fetch_add(&CSSDropped,1);
    uint64_t end=CMNow(),total=end>=scan.time?end-scan.time:0,originalTime=originalEnd>=originalStart?originalEnd-originalStart:0,overhead=total>=originalTime?total-originalTime:0;
    atomic_fetch_add(&CSSObserverTotal,overhead);CMLMaximum(&CSSObserverMaximum,overhead);
    errno=forwarded;
}
// Copy once during an accepted background sample. Native generation checks in
// BVTSample reject overlap; matching the original scan timestamp also rejects
// a newer attempted scan that failed before reaching the snapshot helper.
static bool CSSCopy(CSSScan *scan) {
    if(pthread_mutex_trylock(&CSSMutex))return false;*scan=CSSLatest;pthread_mutex_unlock(&CSSMutex);return scan->sequence!=0;
}
static NSString *CSSState(const CSSScan *scan,bool copied,bool stampRead,double stamp,uint64_t actor) {
    if(!copied||!stampRead||!scan->stampValid)return @"unknown_scan";
    if(scan->nativeStamp!=stamp)return @"latest_scan_not_observed";
    if(!scan->vectorValid)return @"incomplete_copy";
    if(!scan->watchesCopied)return @"watch_copy_unavailable";
    CSSWatch key={.actor=actor};const CSSWatch *watch=bsearch(&key,scan->watches,scan->watchCount,sizeof(*scan->watches),CSSCompare);
    if(!watch)return @"not_watched";if(watch->found)return @"present";
    return scan->singleFailures?@"unknown_after_read_failure":@"not_found_in_completed_copy";
}
static NSDictionary *CSSStatus(const CSSScan *scan,bool copied,bool stampRead,double stamp,uint64_t now) {
    return @{@"enabled":@YES,@"snapshot_available":@(copied),@"matches_latest_native_scan":@(copied&&stampRead&&scan->stampValid&&scan->nativeStamp==stamp),
        @"scan_sequence":@(scan->sequence),@"scan_age_ms":scan->sequence&&now>=scan->time?@((now-scan->time)/1000000ULL):(id)NSNull.null,
        @"requested_records":@(scan->requested),@"returned_records":@(scan->returned),@"complete_vector":@(scan->vectorValid),@"watched_targets":@(scan->watchCount),
        @"chunk_reads":@(scan->chunkReads),@"chunk_read_failures":@(scan->chunkFailures),@"fallback_pointer_reads":@(scan->singleReads),@"fallback_pointer_read_failures":@(scan->singleFailures),@"zero_pointers":@(scan->zeroPointers),
        @"diagnostic_lock_drops":@(atomic_load(&CSSDropped)),@"observer_time_total_ns":@(atomic_load(&CSSObserverTotal)),@"observer_time_maximum_ns":@(atomic_load(&CSSObserverMaximum)),@"record_limit":@CSS_RECORD_LIMIT,@"watch_limit":@CSS_WATCH_LIMIT,
        @"scope":@"existing native actor snapshot and its original reads only; no additional game reads or retries; original scan throttle and drawing unchanged",
        @"absence_semantics":@"not_found_in_completed_copy only refers to a watched process-local pointer in a non-atomic native copy; does not prove absence from the game/server; pointer reuse is not identity verification"};
}
