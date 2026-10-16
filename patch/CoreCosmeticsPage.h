#ifndef CORE_COSMETICS_PAGE_H
#define CORE_COSMETICS_PAGE_H
#include <stdbool.h>
#include <stdio.h>
#include "CoreCosmeticsCatalog.h"
typedef struct { float x, y; } CCPUIVec;
typedef struct {
    void (*cursor)(const CCPUIVec *);
    bool (*begin)(const char *, const CCPUIVec *, int, int);
    void (*end)(void);
    void (*space)(const CCPUIVec *);
    bool (*checkbox)(const char *, bool *, float);
    bool (*button)(const char *, const char *, bool);
} CCPUI;
typedef enum { CCPMenuIdle, CCPMenuWaiting, CCPMenuApplied, CCPMenuRestored,
               CCPMenuWrongOutfit, CCPMenuFailed, CCPMenuRestorePending,
               CCPMenuVehicleWaiting, CCPMenuVehicleCaptured,
               CCPMenuVehicleTrialWaiting, CCPMenuVehicleTrialApplied, CCPMenuVehicleWrongCar,
               CCPMenuVehicleRestoring, CCPMenuVehicleDataRestored } CCPMenuStatus;
#define CCP_VEHICLE_CAPTURE_INDEX ((uint32_t)CCP_PRESET_COUNT)
#define CCP_VEHICLE_TRIAL_INDEX ((uint32_t)CCP_PRESET_COUNT + 1)
static inline void CCPDrawCosmeticsPage(CCPUI ui, float scale, CCPMenuStatus status,
    bool enabled, uint32_t selected, unsigned *category, unsigned *page, void (*action)(uint32_t, bool)) {
    const char *states[] = {
        "15 秒试穿", "等待角色", "试穿中", "已恢复原外观",
        "请先穿戴对应物品", "请查看报告", "等待恢复", "正在读取车辆", "车辆报告已保存",
        "等待4座敞篷跑车", "车辆限时试换", "请驾驶4座敞篷跑车", "正在恢复原车", "原车数据已恢复"
    };
    static const uint32_t slots[] = {5,2,6,7,11,0};
    static const char *categories[] = {"外套", "头发", "裤子", "鞋子", "降落伞", "车辆适配"};
    *category %= 6;
    unsigned count = 0;
    for (unsigned i = 0; i < CCP_PRESET_COUNT; i++) count += CCPAppearancePresets[i].slot == slots[*category];
    unsigned pages = count ? (count + 3) / 4 : 1;
    *page %= pages;
    if ((unsigned)status >= sizeof(states) / sizeof(states[0])) status = CCPMenuFailed;
    char title[192];
    snprintf(title, sizeof(title), "%s · %u/%u · %s", categories[*category], *page + 1, pages, states[status]);
    CCPUIVec origin = {170 * scale, 38 * scale}, size = {476 * scale, 225 * scale};
    ui.cursor(&origin);
    bool visible = ui.begin(title, &size, 0, 0x10);
    if (visible) {
        CCPUIVec gap = {0, 8 * scale}; ui.space(&gap);
        if (*category == 5) {
            CCPUIVec row = {8 * scale, 28 * scale}; ui.cursor(&row);
            if (ui.button("v", "采集车辆", enabled && selected == CCP_VEHICLE_CAPTURE_INDEX))
                action(CCP_VEHICLE_CAPTURE_INDEX, true);
            row.x = 155 * scale; ui.cursor(&row);
            if (ui.button("s", "停止采集", false)) action(CCP_VEHICLE_CAPTURE_INDEX, false);
            row.x = 8 * scale; row.y = 70 * scale; ui.cursor(&row);
            if (ui.button("t", "宾利试换15秒", enabled && selected == CCP_VEHICLE_TRIAL_INDEX))
                action(CCP_VEHICLE_TRIAL_INDEX, true);
            row.x = 250 * scale; ui.cursor(&row);
            if (ui.button("r", "恢复原车", false)) action(CCP_VEHICLE_TRIAL_INDEX, false);
        }
        unsigned index = 0;
        for (unsigned i = 0; i < CCP_PRESET_COUNT; i++) {
            if (CCPAppearancePresets[i].slot != slots[*category]) continue;
            unsigned position = index++;
            if (position < *page * 4 || position >= (*page + 1) * 4) continue;
            bool checked = enabled && selected == i;
            if (ui.checkbox(CCPAppearancePresets[i].label, &checked, 0)) action(i, checked);
        }
        CCPUIVec nav = {8 * scale, 167 * scale}; ui.cursor(&nav);
        if (*category != 5 && ui.button("p", "上一页", false)) *page = (*page + pages - 1) % pages;
        nav.x = 155 * scale; ui.cursor(&nav);
        if (*category != 5 && ui.button("n", "下一页", false)) *page = (*page + 1) % pages;
        nav.x = 302 * scale; ui.cursor(&nav);
        if (ui.button("c", "换分类", false)) { *category = (*category + 1) % 6; *page = 0; }
    }
    // Native BeginChild/EndChild must remain balanced even for a clipped page.
    ui.end();
}
#endif
