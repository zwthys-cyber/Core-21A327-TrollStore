#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../patch/CoreCosmeticsPage.h"
static unsigned begins, ends, controls, actions;
static bool visible, clicked, nextEnabled, requested;
static CCPUIVec position, dimensions;
static char title[256];
static void Cursor(const CCPUIVec *p) { position = *p; }
static bool Begin(const char *text, const CCPUIVec *size, int childFlags, int windowFlags) {
    assert(childFlags == 0 && windowFlags == 0x10);
    assert(!strstr(text, "###")); // This custom header draws its entire label.
    begins++; dimensions = *size; snprintf(title, sizeof(title), "%s", text); return visible;
}
static void End(void) { ends++; }
static void Space(const CCPUIVec *gap) { assert(gap->x == 0 && gap->y > 0); }
static bool Checkbox(const char *text, bool *enabled, float width) {
    assert(strstr(text, "至尊金龙外套") && width == 0); controls++;
    if (clicked) *enabled = nextEnabled;
    return clicked;
}
static void Action(bool enabled) { actions++; requested = enabled; }
int main(void) {
    CCPUI ui = {Cursor,Begin,End,Space,Checkbox};
    visible = false;
    CCPDrawCosmeticsPage(ui, 1, CCPMenuIdle, false, Action);
    assert(begins == 1 && ends == 1 && controls == 0 && actions == 0);
    visible = true; clicked = true; nextEnabled = true;
    CCPDrawCosmeticsPage(ui, 2, CCPMenuIdle, false, Action);
    assert(actions == 1 && requested && controls == 1 && begins == ends);
    assert(position.x == 340 && position.y == 76 && dimensions.x == 952 && dimensions.y == 450);
    nextEnabled = false;
    CCPDrawCosmeticsPage(ui, 1, CCPMenuApplied, true, Action);
    assert(actions == 2 && !requested && strstr(title, "试穿中") && begins == ends);
    clicked = false;
    CCPDrawCosmeticsPage(ui, 1, CCPMenuWaiting, true, Action);
    assert(strstr(title, "等待 Core") && actions == 2);
    CCPDrawCosmeticsPage(ui, 1, CCPMenuRestored, false, Action);
    assert(strstr(title, "已恢复白 T") && begins == ends);
    CCPDrawCosmeticsPage(ui, 1, CCPMenuWrongOutfit, false, Action);
    assert(strstr(title, "初始白 T") && begins == ends);
    puts("PASS: native page clipping, balanced child lifetime, scaled layout, visible status and queued apply/restore actions");
    return 0;
}
