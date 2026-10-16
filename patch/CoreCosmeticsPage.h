#ifndef CORE_COSMETICS_PAGE_H
#define CORE_COSMETICS_PAGE_H
#include <stdbool.h>
typedef struct { float x, y; } CCPUIVec;
typedef struct {
    void (*cursor)(const CCPUIVec *);
    bool (*begin)(const char *, const CCPUIVec *, int, int);
    void (*end)(void);
    void (*space)(const CCPUIVec *);
    bool (*checkbox)(const char *, bool *, float);
} CCPUI;
typedef enum { CCPMenuIdle, CCPMenuWaiting, CCPMenuApplied, CCPMenuRestored,
               CCPMenuWrongOutfit, CCPMenuFailed, CCPMenuRestorePending } CCPMenuStatus;
static inline void CCPDrawCosmeticsPage(CCPUI ui, float scale, CCPMenuStatus status,
                                        bool enabled, void (*action)(bool)) {
    const char *titles[] = {
        "内核美化 · 15 秒试穿###CoreCosmetics",
        "内核美化 · 等待 Core 和角色就绪###CoreCosmetics",
        "内核美化 · 试穿中，关闭开关可恢复###CoreCosmetics",
        "内核美化 · 已恢复白 T 恤###CoreCosmetics",
        "内核美化 · 请先穿回初始白 T 恤###CoreCosmetics",
        "内核美化 · 未执行成功，请查看报告###CoreCosmetics",
        "内核美化 · 等待恢复，请关闭开关重试###CoreCosmetics"
    };
    if ((unsigned)status >= sizeof(titles) / sizeof(titles[0])) status = CCPMenuFailed;
    CCPUIVec origin = {170 * scale, 38 * scale}, size = {476 * scale, 225 * scale};
    ui.cursor(&origin);
    bool visible = ui.begin(titles[status], &size, 0, 0x10);
    if (visible) {
        CCPUIVec gap = {0, 8 * scale}; ui.space(&gap);
        if (ui.checkbox("试穿至尊金龙外套（15 秒自动恢复）", &enabled, 0)) action(enabled);
    }
    // Native BeginChild/EndChild must remain balanced even for a clipped page.
    ui.end();
}
#endif
