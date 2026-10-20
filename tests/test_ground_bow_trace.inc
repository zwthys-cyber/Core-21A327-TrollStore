// Real local-copy decoder and reporting path, with separate synthetic vectors.
static void BowSetup(void) {
    Setup();Context[0x20]=0;Context[0x21]=1;
    uintptr_t begin=(uintptr_t)BowSources,end=begin+sizeof(BowSources);memcpy(Context+0x3D8,&begin,8);memcpy(Context+0x3E0,&end,8);
    begin=(uintptr_t)BowRecords;end=begin+sizeof(BowRecords);memcpy(Context+0x408,&begin,8);memcpy(Context+0x410,&end,8);
    const char *names[]={"爆炸猎弓","十字弩","M416"};
    for(unsigned i=0;i<3;i++){uint64_t actor=GameActor+i*0x1000,metadata=(uintptr_t)BowMetas[i];float distance=10+i*5;
        memcpy(BowSources+i*32,&actor,8);memcpy(BowSources+i*32+8,&metadata,8);memcpy(BowRecords+i*88,&actor,8);memcpy(BowRecords+i*88+0x10,&metadata,8);memcpy(BowRecords+i*88+0x38,&distance,4);BowRecords[i*88+0x50]=1;
        size_t length=strlen(names[i]);memcpy(BowMetas[i]+0x18,names[i],length);BowMetas[i][0x2F]=(uint8_t)length;}
}
static NSDictionary *BowLastEvent(NSString *name) {
    for(NSDictionary *event in BVTEvents.reverseObjectEnumerator)if([event[@"name"] isEqual:name])return event;return nil;
}
static void BowSample(void) {Draw();BVTSample((uintptr_t)Context);assert(!Reads&&!Fills&&!RawCalls&&!SceneLocks);}
static void CheckGroundBowTrace(void) {
    BowSetup();BowSample();assert(BVTSamples==1&&[BVTReport[@"source_explosive_bows"] intValue]==1&&[BVTReport[@"source_crossbows"] intValue]==1&&[BVTReport[@"cache_at_or_below_20m"] intValue]==2&&BVTEvents.count==2);
    float distances[2]={30,50};for(unsigned i=0;i<2;i++)memcpy(BowRecords+i*88+0x38,&distances[i],4);TestTime+=1000000000ULL;BowSample();
    assert([BVTReport[@"cache_over_20m_through_600m"] intValue]==2&&[BVTReport[@"maximum_cached_distance_metres"] floatValue]==50&&[BowLastEvent(@"爆炸猎弓")[@"current_cached_distance_metres"] floatValue]==30);
    // Source still contains the same bows, but acquisition/cache disappears.
    uintptr_t begin=(uintptr_t)BowRecords,end=begin+sizeof(BowRecords);memcpy(Context+0x410,&begin,8);TestTime+=1000000000ULL;BowSample();
    NSDictionary *event=BowLastEvent(@"十字弩");assert([event[@"source_present"] boolValue]&&![event[@"cache_present"] boolValue]&&event[@"current_cached_distance_metres"]==NSNull.null&&[event[@"last_cached_distance_metres"] floatValue]==50&&[event[@"last_cached_age_ms"] intValue]==1000);
    begin=(uintptr_t)BowSources;memcpy(Context+0x3E0,&begin,8);TestTime+=1000000000ULL;BowSample();event=BowLastEvent(@"十字弩");assert(![event[@"source_present"] boolValue]&&![event[@"cache_present"] boolValue]&&event[@"current_cached_distance_metres"]==NSNull.null);
    // Cached bow is still present; only its native screen-validity flag changes.
    BowSetup();BowSample();BowRecords[0x50]=0;BowSample();assert([BVTReport[@"cache_explosive_bows"] intValue]==1&&![BowLastEvent(@"爆炸猎弓")[@"native_projection_valid"] boolValue]);
    // Metadata can be a heap-backed string. Arrow ammo/other classes never
    // enter bow counts, and unreadable metadata stays explicitly unknown.
    memcpy(BowLongNames[0],"爆炸猎弓",12);uint64_t pointer=(uintptr_t)BowLongNames[0],length=12,capacity=0x8000000000000100ULL;memcpy(BowMetas[0]+0x18,&pointer,8);memcpy(BowMetas[0]+0x20,&length,8);memcpy(BowMetas[0]+0x28,&capacity,8);BowSample();assert([BVTReport[@"cache_explosive_bows"] intValue]==1);
    pointer=0x123;memcpy(BowSources+8,&pointer,8);memcpy(BowRecords+0x10,&pointer,8);BowSample();event=BowLastEvent(@"爆炸猎弓");assert([BVTReport[@"metadata_decode_unknown"] intValue]>0&&[event[@"source_present"] boolValue]&&[event[@"cache_present"] boolValue]&&![event[@"classification_currently_verified"] boolValue]);
    uint64_t samples=BVTSamples;size_t events=BVTEvents.count;TraceBowMutate=true;BowSample();assert(BVTSamples==samples&&BVTBusy==1&&BVTEvents.count==events);
    end=(uintptr_t)BowRecords+sizeof(BowRecords)+1;memcpy(Context+0x410,&end,8);BowSample();assert(BVTSamples==samples&&BVTFailures==1&&BVTEvents.count==events);
    end=(uintptr_t)BowRecords+(BVT_RECORD_LIMIT+1ULL)*88;memcpy(Context+0x410,&end,8);BowSample();assert(BVTSamples==samples&&BVTFailures==2);
    BVTBytes=BVT_BYTE_LIMIT-8;uint8_t scratch[16];assert(!BVTRead((uintptr_t)BowMetas,scratch,sizeof(scratch))&&BVTBytes==BVT_BYTE_LIMIT-8);
    NSMutableDictionary *memo=[NSMutableDictionary dictionary];for(unsigned i=0;i<BVT_METADATA_LIMIT;i++)memo[@(i)]=@0;unsigned unknown=0;assert(BVTClassify((uintptr_t)BowMetas,memo,&unknown)==-1&&unknown==1);
    assert(BVTBucket(20)==1&&BVTBucket(20.01f)==2&&BVTBucket(50)==2&&BVTBucket(600)==2&&BVTBucket(600.01f)==3&&!BVTBucket(NAN));
    BowSetup();for(unsigned i=0;i<70;i++){float distance=i%2?30:10;memcpy(BowRecords+0x38,&distance,4);BowSample();}assert(BVTEvents.count==BVT_EVENT_LIMIT);
    NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];BVTWriteStatus([NSURL fileURLWithPath:path]);NSDictionary *report=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:path] options:0 error:NULL];NSArray *reportedEvents=report[@"stage_events"];
#ifdef CORE_GROUND_LATE_CAMERA
    assert([report[@"render_version"] intValue]==18);
#elif defined(CORE_GROUND_ESSENTIALS)
    assert([report[@"render_version"] intValue]==17);
#else
    assert([report[@"render_version"] intValue]==16);
#endif
    assert(reportedEvents.count==BVT_EVENT_LIMIT);[[NSFileManager defaultManager]removeItemAtPath:path error:NULL];
    puts("PASS: bow source/cache/projection transitions distinguish 20/30/50/600m; disappearing cache never becomes a current old position; short/long exact metadata, unknown metadata, overlap rejection, memory/vector/history limits and zero game-reader/UI work");
}
