// Original calls are synthetic; run the production forwarding and background
// stages together. The separate ARM64 test executes the real native helper.
static uint64_t ScanActors[4],ScanDestination[4];
static unsigned ScanOriginalCalls,ScanReadCalls,ScanFailure;
static bool ScanFailChunk;
static bool ScanReader(uint64_t address,void *out,size_t size) {
    ScanReadCalls++;assert(address>=0x700000000ULL&&size<=sizeof(ScanActors)&&address-0x700000000ULL<=sizeof(ScanActors)-size);
    if((size>8&&ScanFailChunk)||(size==8&&ScanFailure&&address==0x700000000ULL+(ScanFailure-1)*8)){errno=EAGAIN;return false;}
    memcpy(out,(const uint8_t *)ScanActors+(address-0x700000000ULL),size);errno=EINTR;return true;
}
static CSSVector ScanOriginal(uint64_t address,size_t count) {
    ScanOriginalCalls++;assert(address==0x700000000ULL&&count==4&&errno==EDOM);memset(ScanDestination,0,sizeof(ScanDestination));
    bool read=CoreSourceNativeDispatch(address,(uintptr_t)ScanDestination,sizeof(ScanActors),0,TestBase+0xD63E4);
    if(!read)for(size_t i=0;i<count;i++){uint64_t value=0;if(CoreSourceNativeDispatch(address+i*8,(uintptr_t)&value,8,0,TestBase+0xD6414))ScanDestination[i]=value;}
    errno=E2BIG;return (CSSVector){(uintptr_t)ScanDestination,(uintptr_t)(ScanDestination+4),(uintptr_t)(ScanDestination+4)};
}
static void RunSourceSnapshot(void) {
    CSSVector output={0};unsigned calls=ScanOriginalCalls;errno=EDOM;
    CoreSourceNativeDispatch(0x700000000ULL,4,0xDEADBEEF,(uintptr_t)&output,TestBase+0xD5408);
    assert(ScanOriginalCalls==calls+1&&errno==E2BIG&&output.begin==(uintptr_t)ScanDestination&&output.end==(uintptr_t)(ScanDestination+4)&&output.capacity==output.end);
}
static void CheckSourceScan(void) {
    BowSetup();BowSample();CMReadRemote=ScanReader;CSSOriginalSnapshot=ScanOriginal;
    ScanActors[0]=GameActor;ScanActors[1]=GameActor+0x1000;ScanActors[2]=GameActor+0x2000;ScanActors[3]=0;
    ScanOriginalCalls=ScanReadCalls=ScanFailure=0;ScanFailChunk=false;
    RunSourceSnapshot();assert(ScanReadCalls==1&&!memcmp(ScanActors,ScanDestination,sizeof(ScanActors)));
    CSSScan scan={0};assert(CSSCopy(&scan)&&scan.vectorValid&&scan.chunkReads==1&&!scan.chunkFailures&&scan.zeroPointers==1);
    assert([CSSState(&scan,true,true,ScanNativeStamp,GameActor) isEqual:@"present"]);
    // Native snapshot still contains both watched pointers; classified Core
    // source/cache lists lose them. This cannot be called a raw-list absence.
    uintptr_t begin=(uintptr_t)BowSources;memcpy(Context+0x3E0,&begin,8);begin=(uintptr_t)BowRecords;memcpy(Context+0x410,&begin,8);
    BowSample();assert([BowLastEvent(@"爆炸猎弓")[@"raw_actor_copy_state"] isEqual:@"present"]&&![BowLastEvent(@"爆炸猎弓")[@"source_present"] boolValue]);
    // Fully successful copy does not contain the watched bow pointer.
    ScanActors[0]=GameActor+0x9000;ScanNativeStamp++;RunSourceSnapshot();BowSample();
    assert([BowLastEvent(@"爆炸猎弓")[@"raw_actor_copy_state"] isEqual:@"not_found_in_completed_copy"]);
    // Failed chunk followed by successful individual reads is still complete.
    ScanFailChunk=true;ScanNativeStamp++;unsigned reads=ScanReadCalls;RunSourceSnapshot();assert(ScanReadCalls==reads+5);BowSample();
    assert([BowLastEvent(@"爆炸猎弓")[@"raw_actor_copy_state"] isEqual:@"not_found_in_completed_copy"]);
    assert([BVTReport[@"source_scan"][@"chunk_read_failures"] intValue]==1&&[BVTReport[@"source_scan"][@"fallback_pointer_read_failures"] intValue]==0);
    // A failed individual read leaves an unknown pointer, not proof of absence.
    ScanActors[0]=GameActor;ScanFailure=1;ScanNativeStamp++;RunSourceSnapshot();BowSample();
    assert([BowLastEvent(@"爆炸猎弓")[@"raw_actor_copy_state"] isEqual:@"unknown_after_read_failure"]&&[BowLastEvent(@"十字弩")[@"raw_actor_copy_state"] isEqual:@"present"]);
    // A newer attempted scan that never reached the snapshot invalidates the
    // old raw presence evidence, even if vectors/addresses are unchanged.
    ScanNativeStamp++;BowSample();assert([BowLastEvent(@"十字弩")[@"raw_actor_copy_state"] isEqual:@"latest_scan_not_observed"]);
    // Source pointer exists but is excluded by its type flag. Report it as a
    // separate stage rather than claiming the pointer vanished upstream.
    ScanFailure=0;RunSourceSnapshot();BowSources[0x11]=1;begin=(uintptr_t)BowSources+sizeof(BowSources);memcpy(Context+0x3E0,&begin,8);BowSample();
    NSDictionary *event=BowLastEvent(@"爆炸猎弓");assert([event[@"source_pointer_present_any_type"] boolValue]&&![event[@"source_present"] boolValue]&&[event[@"raw_actor_copy_state"] isEqual:@"present"]);
    // No waits on the render thread: contended diagnostics may be dropped,
    // while the original call and its three-word output still complete.
    pthread_mutex_lock(&CSSMutex);RunSourceSnapshot();pthread_mutex_unlock(&CSSMutex);assert(atomic_load(&CSSDropped));
    assert([CSSState(&scan,false,true,scan.nativeStamp,GameActor) isEqual:@"unknown_scan"]);
    assert([CSSState(&scan,true,true,scan.nativeStamp,GameActor+0xAA000) isEqual:@"not_watched"]);
    scan.vectorValid=false;assert([CSSState(&scan,true,true,scan.nativeStamp,GameActor) isEqual:@"incomplete_copy"]);
    scan.vectorValid=true;scan.watchesCopied=false;assert([CSSState(&scan,true,true,scan.nativeStamp,GameActor) isEqual:@"watch_copy_unavailable"]);
    puts("PASS: upstream snapshot/reader forwarding, exact indirect result, errno, raw-present versus filtered-source loss, completed absence versus read failure, failed newer scans, type exclusions and nonblocking contention; no extra game reads");
}
