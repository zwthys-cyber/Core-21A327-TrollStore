static void CheckGroundItemControls(void) {
    Setup();CGIEnsure();NSUserDefaults *originalPreferences=CGIPreferences;
    NSString *suite=[@"CoreGroundItemsTest." stringByAppendingString:NSUUID.UUID.UUIDString];NSUserDefaults *preferences=[[NSUserDefaults alloc]initWithSuiteName:suite];CGIPreferences=preferences;
    assert(CGIReadSaved(preferences)==63);
    for(unsigned mask=0;mask<64;mask++){
        uint8_t sourceFilter=(uint8_t)(mask&1);Context[0x28]=sourceFilter;
        uintptr_t flags=CoreModelRenderDispatch(Context,TestBase+0xCDB08);assert(flags==(uintptr_t)CGIEditFlags);
        for(unsigned i=0;i<6;i++)CGIEditFlags[i]=(mask>>i)&1;
        errno=EAGAIN;unsigned originalCalls=OriginalCalls,frames=atomic_load(&CMLStats.frames);
        assert(CoreModelRenderDispatch(Context,TestBase+0xCDD48)==0&&errno==EAGAIN);
        assert(atomic_load(&CGIMask)==mask&&CGIReadSaved(preferences)==mask&&OriginalCalls==originalCalls&&atomic_load(&CMLStats.frames)==frames);
        NSUserDefaults *reloaded=[[NSUserDefaults alloc]initWithSuiteName:suite];assert(CGIReadSaved(reloaded)==mask);
        for(unsigned i=0;i<6;i++)assert(Context[CGIOffsets[i]]==((mask>>i)&1));
        assert(Context[0x28]==sourceFilter);
    }
    [preferences setInteger:999 forKey:CGIKey];assert(CGIReadSaved(preferences)==63);[preferences setObject:@"invalid" forKey:CGIKey];assert(CGIReadSaved(preferences)==63);
    [preferences removePersistentDomainForName:suite];CGIPreferences=originalPreferences;atomic_store(&CGIMask,63);for(unsigned i=0;i<6;i++)CGIEditFlags[i]=1;
    Setup();Context[0x20]=0;Draw();assert(!Reads&&!Fills&&!RawCalls&&!SceneLocks);
    for(unsigned i=0;i<6;i++)assert(Context[CGIOffsets[i]]==1);
    puts("PASS: six independent UI bridge preferences round-trip all 64 masks including all-off; isolated UserDefaults reload, invalid value fallback, mirror repair, unchanged errno/callback counts and fill-off zero game/UI work");
}
