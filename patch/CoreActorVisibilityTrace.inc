// Included after the live renderer helpers. The report queue samples only
// Core's existing local vectors; no remote game reads or extra draw work.
// Original-draw epochs reject samples overlapping the known collector path.
// Header checks/epochs are diagnostic guards, not an atomic game snapshot.
#define CAV_LIMIT 512
#define CAV_EVENTS 64
static atomic_uint_fast64_t CAVGeneration,CAVLastNativeEnd;
static atomic_uint CAVNativeActive;
static dispatch_source_t CAVTimer;
// These objects and counters belong exclusively to CMReportQueue.
static NSMutableDictionary<NSNumber *,NSMutableDictionary *> *CAVTargets;
static NSMutableArray<NSDictionary *> *CAVEvents;
static NSDictionary *CAVReport;
static uint64_t CAVSamples,CAVBusy,CAVFailures,CAVReadBytes,CAVTimeTotal,CAVTimeMaximum,CAVNextID,CAVCapacity;
static bool CAVCopyVector(uintptr_t context,unsigned offset,size_t stride,NSData **result) {
    uintptr_t bounds[2]={0};if(!CMSelfRead(context+offset,bounds,sizeof(bounds)))return false;
    if(bounds[1]<bounds[0]||(bounds[1]-bounds[0])%stride||(bounds[1]-bounds[0])/stride>CAV_LIMIT)return false;
    size_t bytes=bounds[1]-bounds[0];if(bytes&&bounds[0]<4096)return false;
    NSMutableData *copy=[NSMutableData dataWithLength:bytes];
    for(size_t p=0;p<bytes;){size_t n=bytes-p;if(n>16384)n=16384;
        if(!CMSelfRead(bounds[0]+p,(uint8_t *)copy.mutableBytes+p,n))return false;CAVReadBytes+=n;p+=n;}
    uintptr_t final[2]={0};if(!CMSelfRead(context+offset,final,sizeof(final))||memcmp(bounds,final,sizeof(bounds)))return false;
    *result=copy;return true;
}
static unsigned CAVBucket(float distance) {return !isfinite(distance)||distance<0?0:distance<=300?1:distance<=600?2:3;}
static void CAVSample(uintptr_t context) {
    uint64_t start=CMNow(),generation=atomic_load(&CAVGeneration),last=atomic_load(&CAVLastNativeEnd);
    if(!context||atomic_load(&CAVNativeActive)||!last||start<last||start-last>1000000000ULL){CAVBusy++;return;}
    @autoreleasepool {
        NSData *source=nil,*cache=nil;uint32_t team=0;uint8_t ready=0;
        bool copied=CMSelfRead(context+0x3A8,&team,4)&&CMSelfRead(context+0x3B8,&ready,1)&&
            CAVCopyVector(context,0x3C0,24,&source)&&CAVCopyVector(context,0x3F0,0x1F8,&cache);
        if(atomic_load(&CAVNativeActive)||generation!=atomic_load(&CAVGeneration)){CAVBusy++;return;}
        if(!copied||ready!=1){CAVFailures++;return;}
        if(!CAVTargets)CAVTargets=[NSMutableDictionary dictionary];if(!CAVEvents)CAVEvents=[NSMutableArray array];
        NSMutableDictionary<NSNumber *,NSDictionary *> *current=[NSMutableDictionary dictionary];
        const uint8_t *s=source.bytes,*c=cache.bytes;uint32_t sourceEnemies=0,cacheEnemies=0,near=0,far=0,over=0,invalid=0;
        // Cache first so a full tracking table favours known distance samples.
        for(size_t p=0;p<cache.length;p+=0x1F8){uint64_t actor=0;uint32_t other=0;float distance=0;
            memcpy(&actor,c+p,8);memcpy(&other,c+p+8,4);memcpy(&distance,c+p+0x40,4);
            if(!CMRemoteRange(actor,8)||other==team)continue;cacheEnemies++;unsigned bucket=CAVBucket(distance);
            if(bucket==1)near++;else if(bucket==2)far++;else if(bucket==3)over++;else invalid++;
            current[@(actor)]=@{@"cache":@YES,@"team":@(other),@"bot":@(c[p+0xC]!=0),@"bucket":@(bucket),@"distance":bucket?@(distance):(id)NSNull.null};}
        for(size_t p=0;p<source.length;p+=24){uint64_t actor=0;uint32_t other=0;memcpy(&actor,s+p,8);memcpy(&other,s+p+8,4);
            if(!CMRemoteRange(actor,8)||other<1||other>100||other==team)continue;sourceEnemies++;
            NSNumber *key=@(actor);NSMutableDictionary *entry=[current[key] mutableCopy]?:[NSMutableDictionary dictionary];entry[@"source"]=@YES;
            if(!entry[@"team"])entry[@"team"]=@(other);current[key]=entry;}
        uint64_t elapsed=CMNow()-start;CAVTimeTotal+=elapsed;if(elapsed>CAVTimeMaximum)CAVTimeMaximum=elapsed;CAVSamples++;
        NSMutableArray *keys=[NSMutableArray arrayWithArray:current.allKeys];for(NSNumber *key in CAVTargets)if(!current[key])[keys addObject:key];
        for(NSNumber *key in keys){NSDictionary *sample=current[key];NSMutableDictionary *old=CAVTargets[key];
            if(!old){if(CAVTargets.count>=CAV_LIMIT){CAVCapacity++;continue;}old=[NSMutableDictionary dictionaryWithDictionary:@{@"target_id":@(++CAVNextID)}];CAVTargets[key]=old;}
            bool inSource=[sample[@"source"] boolValue],inCache=[sample[@"cache"] boolValue];unsigned bucket=[sample[@"bucket"] unsignedIntValue];
            bool changed=!old[@"source_present"]||[old[@"source_present"] boolValue]!=inSource||[old[@"cache_present"] boolValue]!=inCache||[old[@"distance_bucket"] unsignedIntValue]!=bucket;
            old[@"source_present"]=@(inSource);old[@"cache_present"]=@(inCache);old[@"distance_bucket"]=@(bucket);
            if(sample){old[@"last_seen_ns"]=@(start);old[@"team"]=sample[@"team"];}
            if(inCache){if(bucket){old[@"last_cache_ns"]=@(start);old[@"last_cached_distance_metres"]=sample[@"distance"];}
                old[@"bot"]=sample[@"bot"];}
            if(changed){NSMutableDictionary *event=[NSMutableDictionary dictionaryWithDictionary:@{@"sample":@(CAVSamples),@"target_id":old[@"target_id"],@"source_present":@(inSource),@"cache_present":@(inCache),@"distance_bucket":@(bucket),@"current_cached_distance_metres":inCache?(sample[@"distance"]?:NSNull.null):(id)NSNull.null}];
                if(old[@"last_cached_distance_metres"]){event[@"last_cached_distance_metres"]=old[@"last_cached_distance_metres"];event[@"last_cached_age_ms"]=@((start-[old[@"last_cache_ns"] unsignedLongLongValue])/1000000ULL);}
                [CAVEvents addObject:event];if(CAVEvents.count>CAV_EVENTS)[CAVEvents removeObjectAtIndex:0];}
            if(!sample&&old[@"last_seen_ns"]&&start-[old[@"last_seen_ns"] unsignedLongLongValue]>60000000000ULL)[CAVTargets removeObjectForKey:key];}
        uint32_t sourceOnly=0;for(NSDictionary *entry in current.allValues)if([entry[@"source"] boolValue]&&![entry[@"cache"] boolValue])sourceOnly++;
        CAVReport=@{@"samples":@(CAVSamples),@"sample_monotonic_ns":@(start),@"native_cache_age_ms":@((start-last)/1000000ULL),
            @"source_records":@(source.length/24),@"source_enemy_candidates":@(sourceEnemies),@"source_without_cache":@(sourceOnly),
            @"cache_records":@(cache.length/0x1F8),@"cache_enemy_records":@(cacheEnemies),@"cache_0_to_300m":@(near),@"cache_300_to_600m":@(far),@"cache_over_600m":@(over),@"cache_invalid_distance":@(invalid),
            @"distance_bucket_legend":@[@"unknown or invalid",@"0 through 300m",@"over 300 through 600m",@"over 600m"],@"stage_events":[CAVEvents copy]};
    }
}
static NSDictionary *CAVStatus(void) {
    NSMutableDictionary *report=[CAVReport mutableCopy]?:[NSMutableDictionary dictionary];
    [report addEntriesFromDictionary:@{@"enabled":@YES,@"interval_ms":@1000,@"tracked_target_limit":@CAV_LIMIT,@"event_limit":@CAV_EVENTS,
        @"overlapping_or_stale_samples_skipped":@(CAVBusy),@"unavailable_samples":@(CAVFailures),@"local_vector_bytes_read":@(CAVReadBytes),@"sample_read_and_decode_total_ns":@(CAVTimeTotal),@"sample_read_and_decode_maximum_ns":@(CAVTimeMaximum),@"tracking_capacity_rejections":@(CAVCapacity),
        @"scope":@"background read-only local source/cache vectors; no remote game reads; known original-draw overlaps rejected; not an atomic game snapshot",
        @"target_id_semantics":@"process-local serial for a native actor pointer; pointer reuse is not game identity verification",
        @"missing_target_semantics":@"absence from a sampled Core list does not prove absence from the game or server; cached range is not a current position after cache disappearance"}];return report;
}
