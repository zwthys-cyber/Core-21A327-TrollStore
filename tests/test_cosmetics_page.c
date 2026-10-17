#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../patch/CoreCosmeticsPage.h"
static unsigned begins, ends, controls, buttons, actions, cursors;
static bool visible, nextEnabled, requested;
static int clickedPreset = -1;
static char clickedButton;
static uint32_t requestedPreset, shown[4];
static CCPUIVec origin, dimensions;
static char title[256];
static void Cursor(const CCPUIVec *p) { if (!cursors++) origin = *p; }
static bool Begin(const char *text, const CCPUIVec *size, int childFlags, int windowFlags) {
    assert(childFlags == 0 && windowFlags == 0x10 && !strstr(text, "###"));
    begins++; dimensions = *size; snprintf(title, sizeof(title), "%s", text); return visible;
}
static void End(void) { ends++; }
static void Space(const CCPUIVec *gap) { assert(gap->x == 0 && gap->y > 0); }
static bool Checkbox(const char *text, bool *enabled, float width) {
    assert(width == 0 && controls < 4);
    unsigned i = 0;
    while (i < CCP_PRESET_COUNT && strcmp(text, CCPAppearancePresets[i].label)) i++;
    assert(i < CCP_PRESET_COUNT); shown[controls++] = i;
    if ((int)i == clickedPreset) { *enabled = nextEnabled; return true; }
    return false;
}
static bool Button(const char *icon, const char *label, bool selected) {
    assert((!selected || icon[0] == 'v' || icon[0] == 't' || icon[0] == 'g' || icon[0] == 'o' || icon[0] == 'k') && ((icon[0] == 'p' && !strcmp(label, "上一页")) ||
        (icon[0] == 'n' && !strcmp(label, "下一页")) || (icon[0] == 'c' && !strcmp(label, "换分类")) ||
        (icon[0] == 'v' && !strcmp(label, "采集车辆")) || (icon[0] == 's' && !strcmp(label, "停止采集")) ||
        (icon[0] == 'g' && !strcmp(label, "探测游戏进程")) ||
        (icon[0] == 'o' && !strcmp(label, "尝试打开游戏")) ||
        (icon[0] == 'k' && !strcmp(label, "核对远程进程号")) ||
        (icon[0] == 't' && !strcmp(label, "宾利试换15秒")) || (icon[0] == 'r' && !strcmp(label, "恢复原车"))));
    buttons++; return icon[0] == clickedButton;
}
static void Action(uint32_t preset, bool enabled) { actions++; requestedPreset = preset; requested = enabled; }
static void Draw(float scale, CCPMenuStatus status, bool enabled, unsigned selected, unsigned *category, unsigned *page) {
    CCPUI ui = {Cursor,Begin,End,Space,Checkbox,Button};
    cursors = controls = buttons = 0;
    CCPDrawCosmeticsPage(ui, scale, status, enabled, selected, category, page, Action);
    assert(begins == ends);
}
int main(void) {
    unsigned category = 0, page = 0;
    visible = false;
    Draw(1, CCPMenuIdle, false, 0, &category, &page);
    assert(controls == 0 && buttons == 0 && actions == 0);
    visible = true; clickedPreset = 0; nextEnabled = true;
    Draw(2, CCPMenuIdle, false, 0, &category, &page);
    assert(actions == 1 && requested && requestedPreset == 0 && controls == 4 && buttons == 3);
    assert(origin.x == 340 && origin.y == 76 && dimensions.x == 952 && dimensions.y == 450);
    nextEnabled = false;
    Draw(1, CCPMenuApplied, true, 0, &category, &page);
    assert(actions == 2 && !requested && requestedPreset == 0 && strstr(title, "试穿中"));
    clickedPreset = -1; clickedButton = 'n';
    Draw(1, CCPMenuIdle, false, 0, &category, &page); assert(page == 1);
    clickedButton = 0;
    Draw(1, CCPMenuIdle, false, 0, &category, &page); assert(shown[0] == 4 && controls == 4);
    page = 0; clickedButton = 'p';
    Draw(1, CCPMenuIdle, false, 0, &category, &page); assert(page == 14);
    clickedButton = 'c';
    Draw(1, CCPMenuIdle, false, 0, &category, &page); assert(category == 1 && page == 0);
    clickedButton = 0;
    Draw(1, CCPMenuWaiting, true, 0, &category, &page);
    assert(strstr(title, "头发") && strstr(title, "等待角色") && CCPAppearancePresets[shown[0]].slot == 2);
    Draw(1, CCPMenuRestored, false, 0, &category, &page); assert(strstr(title, "已恢复原外观"));
    for (category = 0; category < 5; category++) {
        page = 0; clickedButton = 'p';
        Draw(1, CCPMenuIdle, false, 0, &category, &page);
        clickedButton = 0;
        Draw(1, CCPMenuIdle, false, 0, &category, &page);
        assert(controls > 0 && controls <= 4 && buttons == 3);
    }
    category = 5; page = 0; clickedButton = 'v'; unsigned previousActions = actions;
    Draw(1, CCPMenuVehicleWaiting, false, 0, &category, &page);
    assert(controls == 0 && buttons == 6 && actions == previousActions + 1);
    assert(requested && requestedPreset == CCP_VEHICLE_CAPTURE_INDEX && strstr(title, "车辆适配"));
    clickedButton = 's';
    Draw(1, CCPMenuVehicleWaiting, true, CCP_VEHICLE_CAPTURE_INDEX, &category, &page);
    assert(!requested && requestedPreset == CCP_VEHICLE_CAPTURE_INDEX);
    clickedButton = 't';
    previousActions = actions;
    Draw(1, CCPMenuVehicleTrialApplied, true, CCP_VEHICLE_TRIAL_INDEX, &category, &page);
    assert(actions == previousActions && buttons == 7); // Removed ineffective r9 apply action.
    clickedButton = 'r';
    Draw(1, CCPMenuVehicleRestoring, true, CCP_VEHICLE_TRIAL_INDEX, &category, &page);
    assert(!requested && requestedPreset == CCP_VEHICLE_TRIAL_INDEX && strstr(title, "正在恢复原车"));
    clickedButton = 0;
    Draw(1, CCPMenuVehicleCaptured, false, CCP_VEHICLE_CAPTURE_INDEX, &category, &page);
    assert(strstr(title, "车辆报告已保存"));
    clickedButton = 'g';
    previousActions = actions;
    Draw(1, CCPMenuProcessChecking, false, 0, &category, &page);
    assert(actions == previousActions + 1 && requested && requestedPreset == CCP_PROCESS_PROBE_INDEX);
    assert(strstr(title, "正在检查进程"));
    clickedButton = 0;
    Draw(1, CCPMenuProcessVisible, false, CCP_PROCESS_PROBE_INDEX, &category, &page);
    assert(strstr(title, "游戏进程可见"));
    Draw(1, CCPMenuProcessAbsent, false, 0, &category, &page);
    assert(strstr(title, "未找到游戏进程"));
    clickedButton = 'k';
    previousActions = actions;
    Draw(1, CCPMenuExecChecking, false, 0, &category, &page);
    assert(actions == previousActions + 1 && requested && requestedPreset == CCP_REMOTE_EXEC_INDEX);
    assert(strstr(title, "正在核对进程号") && buttons == 6);
    clickedButton = 0;
    Draw(1, CCPMenuExecMatched, false, CCP_REMOTE_EXEC_INDEX, &category, &page);
    assert(strstr(title, "远程进程号一致"));
    Draw(1, CCPMenuExecMismatch, false, 0, &category, &page);
    assert(strstr(title, "远程进程号未对上"));
    Draw(1, CCPMenuExecBlocked, false, 0, &category, &page);
    assert(strstr(title, "远程会话未就绪"));
    clickedButton = 'c';
    Draw(1, CCPMenuIdle, false, 0, &category, &page); assert(category == 0 && page == 0);
    puts("PASS: bounded preset pagination, category navigation, clipping, scaled native layout and apply/restore selection");
    return 0;
}
