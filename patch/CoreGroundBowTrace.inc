// CMReportQueue only. Inspect Core's existing local item vectors with safe
// copies, never the game reader, render list or a retained screen position.
#define BVT_RECORD_LIMIT 8192
#define BVT_METADATA_LIMIT 256
#define BVT_TARGET_LIMIT 256
#define BVT_EVENT_LIMIT 32
#define BVT_BYTE_LIMIT (2*1024*1024)
static NSMutableDictionary<NSNumber *,NSMutableDictionary *> *BVTTargets;
static NSMutableArray<NSDictionary *> *BVTEvents;
static NSDictionary *BVTReport;
static uint64_t BVTSamples,BVTBusy,BVTFailures,BVTNextID,BVTBytes,BVTTotalBytes,BVTTimeMaximum,BVTCapacity;
static bool BVTRead(uintptr_t address,void *out,size_t size) {
    if(size>BVT_BYTE_LIMIT-BVTBytes)return false;BVTBytes+=size;return CMSelfRead(address,out,size);
}
static bool BVTVector(uintptr_t context,unsigned offset,size_t stride,NSData **out) {
    uintptr_t bounds[2]={0};if(!BVTRead(context+offset,bounds,sizeof(bounds)))return false;
    if(bounds[1]<bounds[0]||(bounds[1]-bounds[0])%stride||(bounds[1]-bounds[0])/stride>BVT_RECORD_LIMIT)return false;
    size_t size=bounds[1]-bounds[0];if(size&&bounds[0]<4096)return false;
    NSMutableData *copy=[NSMutableData dataWithLength:size];
    for(size_t p=0;p<size;){size_t n=size-p;if(n>16384)n=16384;if(!BVTRead(bounds[0]+p,(uint8_t *)copy.mutableBytes+p,n))return false;p+=n;}
    uintptr_t final[2]={0};if(!BVTRead(context+offset,final,sizeof(final))||memcmp(bounds,final,sizeof(bounds)))return false;
    *out=copy;return true;
}
static int BVTClassify(uint64_t metadata,NSMutableDictionary *memo,unsigned *unknown) {
    NSNumber *key=@(metadata),*known=memo[key];if(known)return known.intValue;
    if(memo.count>=BVT_METADATA_LIMIT){(*unknown)++;return -1;}
    uint8_t raw[48]={0};int result=-1;
    if(metadata>=4096&&metadata<=UINTPTR_MAX-sizeof(raw)&&BVTRead((uintptr_t)metadata,raw,sizeof(raw))){
        const uint8_t *text=raw+0x18;uint64_t count=raw[0x2f],address=0;uint8_t extra[12];
        if(count&0x80){memcpy(&address,raw+0x18,8);memcpy(&count,raw+0x20,8);
            if(count==9||count==12){if(address<4096||address>UINTPTR_MAX-count||!BVTRead((uintptr_t)address,extra,(size_t)count))goto done;text=extra;}}
        result=count==12&&!memcmp(text,"爆炸猎弓",12)?1:count==9&&!memcmp(text,"十字弩",9)?2:0;
    }
done:
    memo[key]=@(result);if(result<0)(*unknown)++;return result;
}
static unsigned BVTBucket(float distance) {return !isfinite(distance)||distance<0?0:distance<=20?1:distance<=600?2:3;}
static void BVTSample(uintptr_t context) {
    uint64_t start=CMNow(),generation=atomic_load(&CAVGeneration),last=atomic_load(&CAVLastNativeEnd);
    if(!context||atomic_load(&CAVNativeActive)||!last||start<last||start-last>1000000000ULL){BVTBusy++;return;}
    BVTBytes=0;
    @autoreleasepool {
        NSData *source=nil,*cache=nil;uint8_t enabled=0,ready=0;
        bool copied=BVTRead(context+0x21,&enabled,1)&&BVTRead(context+0x3B8,&ready,1)&&BVTVector(context,0x3D8,32,&source)&&BVTVector(context,0x408,0x58,&cache);
#ifdef CORE_GROUND_ITEM_CONTROLS
        uint8_t explosiveEnabled=enabled,crossbowEnabled=0;copied=copied&&BVTRead(context+0x25,&crossbowEnabled,1);enabled=enabled||crossbowEnabled;
#endif
        if(!copied||ready!=1){BVTFailures++;return;}
        NSMutableDictionary *memo=[NSMutableDictionary dictionary],*current=[NSMutableDictionary dictionary];NSMutableSet *sourceSeen=[NSMutableSet set],*cacheSeen=[NSMutableSet set];unsigned unknown=0,sourceCounts[2]={0},cacheCounts[2]={0},within20=0,beyond20=0,projected=0;float maximum=0;
        if(!BVTTargets)BVTTargets=[NSMutableDictionary dictionary];if(!BVTEvents)BVTEvents=[NSMutableArray array];
        const uint8_t *s=source.bytes,*c=cache.bytes;
        for(size_t p=0;p<cache.length;p+=0x58){uint64_t actor=0,metadata=0;float distance=0;memcpy(&actor,c+p,8);memcpy(&metadata,c+p+0x10,8);memcpy(&distance,c+p+0x38,4);
            if(!CMRemoteRange(actor,8)||c[p+8]||c[p+0x4D])continue;NSNumber *key=@(actor);[cacheSeen addObject:key];int kind=BVTClassify(metadata,memo,&unknown);
            if(kind<=0&&!BVTTargets[key])continue;unsigned bucket=BVTBucket(distance);bool verified=kind>0;
            if(verified){cacheCounts[kind-1]++;if(bucket==1)within20++;else if(bucket==2)beyond20++;if(c[p+0x50])projected++;if(bucket&&distance>maximum)maximum=distance;}
            current[key]=[@{@"cache":@YES,@"kind":@(kind),@"classified":@(verified),@"projected":@(c[p+0x50]!=0),@"bucket":@(bucket),@"distance":bucket?@(distance):(id)NSNull.null} mutableCopy];}
        for(size_t p=0;p<source.length;p+=32){uint64_t actor=0,metadata=0;memcpy(&actor,s+p,8);memcpy(&metadata,s+p+8,8);if(!CMRemoteRange(actor,8)||s[p+0x10]||s[p+0x11])continue;
            NSNumber *key=@(actor);[sourceSeen addObject:key];int kind=BVTClassify(metadata,memo,&unknown);if(kind>0)sourceCounts[kind-1]++;
            if(kind<=0&&!BVTTargets[key]&&!current[key])continue;NSMutableDictionary *entry=current[key]?:[NSMutableDictionary dictionary];entry[@"source"]=@YES;if(kind>0){entry[@"kind"]=@(kind);entry[@"classified"]=@YES;}current[key]=entry;}
        if(atomic_load(&CAVNativeActive)||generation!=atomic_load(&CAVGeneration)){BVTBusy++;return;}
        BVTSamples++;BVTTotalBytes+=BVTBytes;
        NSMutableSet *keys=[NSMutableSet setWithArray:current.allKeys];[keys addObjectsFromArray:BVTTargets.allKeys];
        for(NSNumber *key in keys){NSMutableDictionary *old=BVTTargets[key];NSDictionary *sample=current[key];int kind=[sample[@"kind"] intValue];
            if(!old){if(BVTTargets.count>=BVT_TARGET_LIMIT){BVTCapacity++;continue;}old=[NSMutableDictionary dictionaryWithDictionary:@{@"target_id":@(++BVTNextID)}];BVTTargets[key]=old;}
            if(kind>0)old[@"name"]=kind==1?@"爆炸猎弓":@"十字弩";
            bool inSource=[sourceSeen containsObject:key],inCache=[cacheSeen containsObject:key],validProjection=[sample[@"projected"] boolValue],classified=[sample[@"classified"] boolValue];unsigned bucket=[sample[@"bucket"] unsignedIntValue];
            bool changed=!old[@"source_present"]||[old[@"source_present"] boolValue]!=inSource||[old[@"cache_present"] boolValue]!=inCache||[old[@"native_projection_valid"] boolValue]!=validProjection||[old[@"classification_currently_verified"] boolValue]!=classified||[old[@"distance_bucket"] unsignedIntValue]!=bucket;
            if(sample){old[@"last_seen_ns"]=@(start);if(inCache&&bucket){old[@"last_cached_distance_metres"]=sample[@"distance"];old[@"last_cache_ns"]=@(start);}}
            old[@"source_present"]=@(inSource);old[@"cache_present"]=@(inCache);old[@"native_projection_valid"]=@(validProjection);old[@"classification_currently_verified"]=@(classified);old[@"distance_bucket"]=@(bucket);
            if(changed){NSMutableDictionary *event=[NSMutableDictionary dictionaryWithDictionary:@{@"sample":@(BVTSamples),@"target_id":old[@"target_id"],@"name":old[@"name"]?:@"previously classified bow pointer",@"source_present":@(inSource),@"cache_present":@(inCache),@"classification_currently_verified":@(classified),@"native_projection_valid":@(validProjection),@"distance_bucket":@(bucket),@"current_cached_distance_metres":inCache?(sample[@"distance"]?:NSNull.null):(id)NSNull.null}];
                if(old[@"last_cached_distance_metres"]){event[@"last_cached_distance_metres"]=old[@"last_cached_distance_metres"];event[@"last_cached_age_ms"]=@((start-[old[@"last_cache_ns"] unsignedLongLongValue])/1000000ULL);}[BVTEvents addObject:event];if(BVTEvents.count>BVT_EVENT_LIMIT)[BVTEvents removeObjectAtIndex:0];}
            if(!inSource&&!inCache&&old[@"last_seen_ns"]&&start-[old[@"last_seen_ns"] unsignedLongLongValue]>60000000000ULL)[BVTTargets removeObjectForKey:key];}
        uint64_t elapsed=CMNow()-start;if(elapsed>BVTTimeMaximum)BVTTimeMaximum=elapsed;
        BVTReport=@{@"samples":@(BVTSamples),@"sample_monotonic_ns":@(start),@"native_cache_age_ms":@((start-last)/1000000ULL),@"bow_switch_enabled":@(enabled!=0),@"source_records":@(source.length/32),@"cache_records":@(cache.length/0x58),@"metadata_decode_unknown":@(unknown),@"metadata_objects_sampled":@(memo.count),
#ifdef CORE_GROUND_ITEM_CONTROLS
            @"explosive_bow_switch_enabled":@(explosiveEnabled!=0),@"crossbow_switch_enabled":@(crossbowEnabled!=0),
#endif
            @"source_explosive_bows":@(sourceCounts[0]),@"source_crossbows":@(sourceCounts[1]),@"cache_explosive_bows":@(cacheCounts[0]),@"cache_crossbows":@(cacheCounts[1]),@"cache_at_or_below_20m":@(within20),@"cache_over_20m_through_600m":@(beyond20),@"cache_native_projection_valid":@(projected),@"maximum_cached_distance_metres":@(maximum),@"sample_local_bytes":@(BVTBytes)};
    }
}
static NSDictionary *BVTStatus(bool events) {
#ifdef CORE_GROUND_LATE_CAMERA
    const int renderVersion=18;
#elif defined(CORE_GROUND_ESSENTIALS)
    const int renderVersion=17;
#else
    const int renderVersion=16;
#endif
    NSMutableDictionary *out=[BVTReport mutableCopy]?:[NSMutableDictionary dictionary];uint64_t sample=[out[@"sample_monotonic_ns"] unsignedLongLongValue],now=CMNow();
    [out addEntriesFromDictionary:@{@"enabled":@YES,@"render_version":@(renderVersion),@"date":NSDate.date.description,@"last_sample_age_ms":sample&&now>=sample?@((now-sample)/1000000ULL):(id)NSNull.null,@"overlap_or_stale_samples_skipped":@(BVTBusy),@"unavailable_samples":@(BVTFailures),@"local_bytes_total":@(BVTTotalBytes),@"sample_time_maximum_ns":@(BVTTimeMaximum),@"tracking_capacity_rejections":@(BVTCapacity),@"record_limit":@BVT_RECORD_LIMIT,@"metadata_limit":@BVT_METADATA_LIMIT,@"local_sample_byte_limit":@BVT_BYTE_LIMIT,
        @"scope":@"background safe copies of Core local ground source/cache vectors; no game reads, no render mutations, no retained screen positions; header/generation checks do not create an atomic game snapshot",
        @"source_age_semantics":@"source acquisition retains its original throttle; sample time is not the source game frame age",
        @"target_id_semantics":@"process-local serial for a previously classified actor pointer; pointer reuse is not game identity verification; absent source data does not prove game/server absence",
        @"unknown_metadata_semantics":@"unreadable or over-budget metadata is unknown, not proof that there are no bows",
        @"distance_bucket_legend":@[@"unknown or invalid",@"0 through 20m",@"over 20 through 600m",@"over 600m"]}];
    if(events)out[@"stage_events"]=[BVTEvents copy]?:@[];return out;
}
static void BVTWriteStatus(NSURL *url) {
    NSData *json=[NSJSONSerialization dataWithJSONObject:BVTStatus(true) options:NSJSONWritingSortedKeys error:NULL];if(json.length&&json.length<131072)[json writeToURL:url options:NSDataWritingAtomic error:NULL];
}
