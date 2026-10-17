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
               CCPMenuVehicleRestoring, CCPMenuVehicleDataRestored,
               CCPMenuProcessChecking, CCPMenuProcessVisible, CCPMenuProcessAbsent,
               CCPMenuGameOpening, CCPMenuGameOpenReturned, CCPMenuGameOpenBlocked,
               CCPMenuExecChecking, CCPMenuExecMatched, CCPMenuExecMismatch, CCPMenuExecBlocked } CCPMenuStatus;
#define CCP_VEHICLE_CAPTURE_INDEX ((uint32_t)CCP_PRESET_COUNT)
#define CCP_VEHICLE_TRIAL_INDEX ((uint32_t)CCP_PRESET_COUNT + 1)
#define CCP_PROCESS_PROBE_INDEX ((uint32_t)CCP_PRESET_COUNT + 2)
#define CCP_GAME_OPEN_INDEX ((uint32_t)CCP_PRESET_COUNT + 3)
#define CCP_REMOTE_EXEC_INDEX ((uint32_t)CCP_PRESET_COUNT + 4)
static inline void CCPDrawCosmeticsPage(CCPUI ui, float scale, CCPMenuStatus status,
    bool enabled, uint32_t selected, unsigned *category, unsigned *page, void (*action)(uint32_t, bool)) {
    const char *states[] = {
        "15 秒试穿", "等待角色", "试穿中", "已恢复原外观",
        "请先穿戴对应物品", "请查看报告", "等待恢复", "正在读取车辆", "车辆报告已保存",
        "等待当前载具", "车皮入队中", "请先坐上载具", "正在恢复原车", "原车数据已恢复",
        "正在检查进程", "游戏进程可见", "未找到游戏进程",
        "正在打开游戏", "打开调用已返回", "隔离状态挡住打开",
        "正在核对进程号", "远程进程号一致", "远程进程号未对上", "远程会话未就绪"
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
            // Primary product actions first — must not share a hit rect with diagnostics.
            CCPUIVec row = {8 * scale, 28 * scale}; ui.cursor(&row);
            if (ui.button("t", "装目标车皮", enabled && selected == CCP_VEHICLE_TRIAL_INDEX))
                action(CCP_VEHICLE_TRIAL_INDEX, true);
            row.x = 155 * scale; ui.cursor(&row);
            if (ui.button("r", "恢复原车", enabled && selected == CCP_VEHICLE_TRIAL_INDEX))
                action(CCP_VEHICLE_TRIAL_INDEX, false);
            // Diagnostics only. Never auto-enqueue from these paths.
            row.x = 8 * scale; row.y = 70 * scale; ui.cursor(&row);
            if (ui.button("v", "采集车辆", enabled && selected == CCP_VEHICLE_CAPTURE_INDEX))
                action(CCP_VEHICLE_CAPTURE_INDEX, true);
            row.x = 155 * scale; row.y = 70 * scale; ui.cursor(&row);
            if (ui.button("g", "探测游戏进程", enabled && selected == CCP_PROCESS_PROBE_INDEX))
                action(CCP_PROCESS_PROBE_INDEX, true);
            row.x = 8 * scale; row.y = 112 * scale; ui.cursor(&row);
            if (ui.button("o", "尝试打开游戏", enabled && selected == CCP_GAME_OPEN_INDEX))
                action(CCP_GAME_OPEN_INDEX, true);
            row.x = 155 * scale; row.y = 112 * scale; ui.cursor(&row);
            if (ui.button("k", "核对远程进程号", enabled && selected == CCP_REMOTE_EXEC_INDEX))
                action(CCP_REMOTE_EXEC_INDEX, true);
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
