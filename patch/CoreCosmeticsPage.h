#ifndef CORE_COSMETICS_PAGE_H
#define CORE_COSMETICS_PAGE_H
#include <stdbool.h>
#include <stdio.h>
#include "CoreCosmeticsCatalog.h"
#include "CoreCosmeticsVehicleCatalog.h"
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
               CCPMenuVehicleRestoring, CCPMenuVehicleDataRestored,
               CCPMenuProcessChecking, CCPMenuProcessVisible, CCPMenuProcessAbsent,
               CCPMenuGameOpening, CCPMenuGameOpenReturned, CCPMenuGameOpenBlocked,
               CCPMenuExecChecking, CCPMenuExecMatched, CCPMenuExecMismatch, CCPMenuExecBlocked } CCPMenuStatus;
#define CCP_VEHICLE_PRESET_BASE ((uint32_t)CCP_PRESET_COUNT)
#define CCP_VEHICLE_CAPTURE_INDEX (CCP_VEHICLE_PRESET_BASE + (uint32_t)CCP_VEHICLE_PRESET_COUNT)
#define CCP_PROCESS_PROBE_INDEX (CCP_VEHICLE_CAPTURE_INDEX + 1)
#define CCP_GAME_OPEN_INDEX (CCP_VEHICLE_CAPTURE_INDEX + 2)
#define CCP_REMOTE_EXEC_INDEX (CCP_VEHICLE_CAPTURE_INDEX + 3)
static inline void CCPDrawCosmeticsPage(CCPUI ui, float scale, CCPMenuStatus status,
    bool enabled, uint32_t selected, unsigned *category, unsigned *page, void (*action)(uint32_t, bool)) {
    const char *states[] = {
        "选择外观", "等待角色", "已应用", "已恢复原外观",
        "请先穿戴对应物品", "请查看报告", "等待恢复", "正在读取车辆", "车辆报告已保存",
        "等待当前载具", "车皮已提交", "当前原车不兼容", "正在恢复原车", "原车数据已恢复",
        "正在检查进程", "游戏进程可见", "未找到游戏进程",
        "正在打开游戏", "打开调用已返回", "隔离状态挡住打开",
        "正在核对进程号", "远程进程号一致", "远程进程号未对上", "远程会话未就绪"
    };
    static const uint32_t slots[] = {5,2,6,7,11,0};
    static const char *categories[] = {"外套", "头发", "裤子", "鞋子", "降落伞", "车辆适配"};
    *category %= 6;
    unsigned count = *category == 5 ? (unsigned)CCP_VEHICLE_PRESET_COUNT : 0;
    if (*category != 5)
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
            for (unsigned i = *page * 4; i < count && i < (*page + 1) * 4; i++) {
                uint32_t menuIndex = CCP_VEHICLE_PRESET_BASE + i;
                // `enabled` means an owned change is active, not "menu busy".
                bool checked = enabled && selected == menuIndex;
                char label[256];
                snprintf(label, sizeof(label), "%s [%u] → %s [%u]##vehicle-%u",
                         CCPVehiclePresets[i].sourceLabel, CCPVehiclePresets[i].sourceItem,
                         CCPVehiclePresets[i].targetLabel, CCPVehiclePresets[i].targetItem, i);
                if (ui.checkbox(label, &checked, 0)) action(menuIndex, checked);
            }
        }
        unsigned index = 0;
        for (unsigned i = 0; i < CCP_PRESET_COUNT; i++) {
            if (CCPAppearancePresets[i].slot != slots[*category]) continue;
            unsigned position = index++;
            if (position < *page * 4 || position >= (*page + 1) * 4) continue;
            bool checked = enabled && selected == i;
            char label[192];
            snprintf(label, sizeof(label), "%s##appearance-%u", CCPAppearancePresets[i].label, i);
            if (ui.checkbox(label, &checked, 0)) action(i, checked);
        }
        CCPUIVec nav = {8 * scale, 167 * scale}; ui.cursor(&nav);
        if (ui.button("p", "上一页", false)) *page = (*page + pages - 1) % pages;
        nav.x = 155 * scale; ui.cursor(&nav);
        if (ui.button("n", "下一页", false)) *page = (*page + 1) % pages;
        nav.x = 302 * scale; ui.cursor(&nav);
        if (ui.button("c", "换分类", false)) { *category = (*category + 1) % 6; *page = 0; }
    }
    // Native BeginChild/EndChild must remain balanced even for a clipped page.
    ui.end();
}
#endif
