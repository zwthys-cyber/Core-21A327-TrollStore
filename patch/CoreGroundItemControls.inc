// Six dedicated app preferences. UI edits only this byte array, then commits
// one mask. Native collector mirrors do not use the old coupled enum storage.
// Do not use +0x28: it is still an upstream actor-filter setting.
static const unsigned CGIOffsets[6]={0x21,0x25,0x26,0x27,0x2A,0x29};
static uint8_t CGIEditFlags[6];
static atomic_uint CGIMask=63;
static NSUserDefaults *CGIPreferences;
static NSString *const CGIKey=@"CoreGroundItemsMaskV1";
static unsigned CGIReadSaved(NSUserDefaults *preferences) {
    id value=[preferences objectForKey:CGIKey];if(![value isKindOfClass:NSNumber.class])return 63;
    NSInteger mask=[value integerValue];return mask<0||mask>63?63:(unsigned)mask;
}
__attribute__((noinline)) static void CGIEnsure(void) {
    static dispatch_once_t once;dispatch_once(&once,^{CGIPreferences=NSUserDefaults.standardUserDefaults;unsigned mask=CGIReadSaved(CGIPreferences);
        for(unsigned i=0;i<6;i++)CGIEditFlags[i]=(mask>>i)&1;atomic_store(&CGIMask,mask);});
}
static void CGIApply(uintptr_t context) {
    if(!atomic_load(&CMLReady)||context!=CMLExpectedContext)return;
    unsigned mask=atomic_load(&CGIMask);uint8_t *bytes=(uint8_t *)context;
    for(unsigned i=0;i<6;i++)bytes[CGIOffsets[i]]=(mask>>i)&1;
}
__attribute__((noinline)) static uintptr_t CGIOpen(uintptr_t context) {
    CGIEnsure();CGIApply(context);return (uintptr_t)CGIEditFlags;
}
__attribute__((noinline)) static void CGICommit(uintptr_t context) {
    CGIEnsure();unsigned mask=0;for(unsigned i=0;i<6;i++)if(CGIEditFlags[i])mask|=1U<<i;
    atomic_store(&CGIMask,mask);CGIApply(context);[CGIPreferences setInteger:(NSInteger)mask forKey:CGIKey];
}
static NSDictionary *CGIStatus(void) {
    unsigned mask=atomic_load(&CGIMask);NSArray *names=@[@"爆炸猎弓",@"十字弩",@"止痛药",@"三级包",@"可乐",@"空投"];
    NSMutableDictionary *enabled=[NSMutableDictionary dictionary];for(unsigned i=0;i<6;i++)enabled[names[i]]=@((mask&(1U<<i))!=0);
    return @{@"independent":@YES,@"mask":@(mask),@"enabled":enabled,@"saved_preference":CGIKey};
}
