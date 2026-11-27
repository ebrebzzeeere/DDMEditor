#include "state.h"
#include <cstdint>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "prop.h"
#include "document.h"
#include "graph.h"
#include "guitext.h"




// グローバル変数
std::vector<Element> containerA;
std::vector<Element> containerB;
std::vector<Element> containerC;
std::vector<Element> displayList;
std::vector<Element> objectList;

std::vector<Element> flavorContainer;

size_t selectedIndex = 0;
size_t selectedObjectiveIndex = 0;
bool isObjectiveSelected = false;

bool needRedraw = true;
bool noContentState = false;

// --- 「移動する」コンテナ(id == 04)のトグル表示用 ---
// SDLK_Mの入力でdisplayListと「移動する」コンテナをトグル表示する。
// 入力システム(selectedIndex等の選択状態)は新規処理を新設せず流用し、
// トグル時にsuspend用の配列へ退避し、復元時にその値をそのまま戻す。
bool gMoveMode = false;
std::vector<Element> gSuspendedDisplayList;
std::vector<Element> gSuspendedObjectiveList;
size_t gSuspendedSelectedIndex = 0;
size_t gSuspendedSelectedObjectiveIndex = 0;
bool gSuspendedIsObjectiveSelected = false;
bool gSuspendedNoContentState = false;

SDL_Window *gWindow = nullptr;
SDL_Renderer *gRenderer = nullptr;

unsigned gScene = 0xe;
unsigned gRoot = 0;
unsigned gParent = 0;
unsigned gUnlocks = 0;
unsigned gEyelevel = 1;
unsigned gTick = 0;
unsigned gTicketNumber = 0;
unsigned gCallingNumber = 1031;

// タイマー 0-3
unsigned gTimers[4] = {0, 0, 0, 0};
unsigned &gTimer0 = gTimers[0];
unsigned &gTimer1 = gTimers[1];
unsigned &gTimer2 = gTimers[2];
unsigned &gTimer3 = gTimers[3];
bool gTimerRunning[4] = {false, false, false, false};
uint8_t gPendingTimerActions[4] = {0, 0, 0, 0};


// --- gTick秒読みタイマー用の状態 ---
// duration（1-3の乱数）を設定した時点のgTickを秒読み開始点として記録し、
// 現在gTickとの差がdurationに等しくなった時点で発火する。
unsigned gGTickDuration = 0; // 0: 未設定（初回呼び出しで抽選・設定する）
unsigned gGTickStamp = 0;    // duration設定時点のgTick（秒読み開始点）


Document gDocument;
Graph gGraph;
RoundTripReport gRoundTrip;
std::string gLoadedPath;
std::vector<std::string> gLoadWarnings;

ViewMode gViewMode = VIEW_GENERAL;
uint8_t gViewScene = 0;
bool gGraphEnabled = true;
bool gExportPanel = true;
bool gExportOnQuit = false;

// viewport: マウスドラッグで pan、ホイール / +/- で zoom
float gViewZoom = 1.f;
float gViewPanX = 0.f;
float gViewPanY = 0.f;
bool gDragging = false;
float gDragLastX = 0.f;
float gDragLastY = 0.f;

int gSelectedNode = -1;

EditorState gEditor;

bool gEditorTextInputActive = false; // editor 側の SDL_StartTextInput/StopTextInput の対称管理
bool gDocTerminatorPatched = false;  // raw が改行で終わらない場合に最終行 terminator を "\n" に補った記録

// slot option の表示名（read helpers の option 対応と同じ順。names[idx+1] = index idx の option）。
const char *const kSlotOptionNames0[] = {"none", "requiredReadId", "unlocks"};
const char *const kSlotOptionNames1[] = {"none", "child", "excludedReadId", "newRoot",
                                               "downUnlocks"};
const char *const kSlotOptionNames2[] = {"none", "timerSetter", "unlocks",
                                                "requiredUnlockFlags", "excludedReadId"};
const char *const kSlotOptionNames3[] = {"none", "timerTarget", "newScene",
                                                "excludedUnlockFlags", "eyelevelToggle"};
const char *const kTimerActionNames[] = {"none", "start", "stop", "reset"}; // timerSetter action 0=none,1=start,2=stop,3=reset
const char *const kTimerOpNames[] = {"none", "less", "more", "equal"};      // timerTarget operator 0=none(=timerTarget なし),1=less,2=more,3=equal

// 現在の option 表示名。value 0 は none（data.txt の既存慣習: index=0/value=0 で none を表す。
// helpers は「index が該当 option であれば value を返す」なので value 0 はどの option でも none）。
const char *propsOptionLabel(uint64_t prop, uint8_t slot) {
  if (getSlotValue(prop, slot) == 0) return "none";
  const uint8_t idx = getSlotIndex(prop, slot);
  if (idx + 1 > slotOptionCount(slot)) return "unknown";
  return slotOptionNames(slot)[idx + 1];
}

// メニューの fields（表示順 = tabs/sections の順。同関数 describeElement の EDITOR variant も同じ順）。
const PropsFieldDef kPropsFields[] = {
  {"basic flags", "hasRead(bit16)", PF_BOOL, ROLE_PLAIN, 16, 1, 0, 1, nullptr, 0},
  {"basic flags", "isQuote(bit17)", PF_BOOL, ROLE_PLAIN, 17, 1, 0, 1, nullptr, 0},
  {"basic flags", "isOnetime(bit18)", PF_BOOL, ROLE_PLAIN, 18, 1, 0, 1, nullptr, 0},
  {"basic flags", "vacant(bit19・optional・reader なし)", PF_BOOL, ROLE_PLAIN, 19, 1, 0, 1, nullptr, 0},
  {"id/parent/scene", "id(bits0-7)", PF_UINT, ROLE_ID, 0, 8, 0, 0xFF, nullptr, 0},
  {"id/parent/scene", "parent(bits8-15)", PF_UINT, ROLE_PARENT, 8, 8, 0, 0xFF, nullptr, 0},
  {"id/parent/scene", "scene(bits20-23)", PF_UINT, ROLE_PLAIN, 20, 4, 0, 0x0F, nullptr, 0},
  {"slot0", "slot0 option(none/requiredReadId/unlocks)", PF_OPTION, ROLE_PLAIN, 24, 2, 0, 0x3, kSlotOptionNames0, 2},
  {"slot0", "slot0 value(bits32-39)", PF_UINT, ROLE_TARGET, 32, 8, 0, 0xFF, nullptr, 0},
  {"slot1", "slot1 option(none/child/excluded/newRoot/downUnlocks・排他)", PF_OPTION, ROLE_PLAIN, 26, 2, 1, 0x3, kSlotOptionNames1, 4},
  {"slot1", "slot1 value(bits40-47)", PF_UINT, ROLE_TARGET, 40, 8, 1, 0xFF, nullptr, 0},
  {"slot2", "slot2 option(none/timerSetter/unlocks/reqUnlock/excluded)", PF_OPTION, ROLE_PLAIN, 28, 2, 2, 0x3, kSlotOptionNames2, 4},
  {"slot2", "slot2 value(bits48-55)", PF_UINT, ROLE_TARGET, 48, 8, 2, 0xFF, nullptr, 0},
  {"slot2", "timerSetter timerId(value bits0-1)", PF_UINT, ROLE_TIMER_SUB, 48, 2, 2, 3, nullptr, 0},
  {"slot2", "timerSetter action(value bits4-5)", PF_ENUM, ROLE_TIMER_SUB, 52, 2, 2, 3, kTimerActionNames, 0},
  {"slot3", "slot3 option(none/timerTarget/newScene/exclUnlockFlags/eyelevel)", PF_OPTION, ROLE_PLAIN, 30, 2, 3, 0x3, kSlotOptionNames3, 4},
  {"slot3", "slot3 value(bits56-63)", PF_UINT, ROLE_TARGET, 56, 8, 3, 0xFF, nullptr, 0},
  {"slot3", "timerTarget operator(value bits0-1)", PF_ENUM, ROLE_TIMER_SUB, 56, 2, 3, 3, kTimerOpNames, 0},
  {"slot3", "timerTarget timerId(value bits2-3)", PF_UINT, ROLE_TIMER_SUB, 58, 2, 3, 3, nullptr, 0},
  {"slot3", "timerTarget count(value bits4-7)", PF_UINT, ROLE_TIMER_SUB, 60, 4, 3, 0x0F, nullptr, 0},
  {"slot3", "eyelevel(value bits0-3)", PF_UINT, ROLE_EYE_SUB, 56, 4, 3, 0x0F, nullptr, 0},
  {"slot3", "toggleEyelevel(value bits4-7)", PF_UINT, ROLE_EYE_SUB, 60, 4, 3, 0x0F, nullptr, 0},
};
const int kPropsFieldCount = static_cast<int>(sizeof(kPropsFields) / sizeof(PropsFieldDef));
PropsEditState gProps;

UiMode gUiMode = UI_VIEWER; // 既定は現行動作（viewer overlay + console）。DDM_MODE / H で切替
bool gBackupOnQuit = false; // quit の backupData（../data.txt.bak 上書き）既定 off・B でトグル
size_t gLoadedBytes = 0;    // load 時の data bytes（最終の「data.txt は変更されない」照合基準）
std::vector<std::string> gSelftestFailures; // selftest の failed phase 集約（重複は 1 件）

// selftest FAIL 検知（Prompt 6・要件 8）: 各 phase の flag をここで集約し、最終で FAIL を出す。
void stFail(const char *phase) {
  for (const std::string &f : gSelftestFailures)
    if (f == phase) return; // 同じ phase の多重失敗は 1 件として集約する
  gSelftestFailures.push_back(phase);
}
// ok が false のとき phase を FAIL に集約する。ok をそのまま返す（flag 計算の途中に埋め込める）。
bool stPass(bool ok, const char *phase) {
  if (!ok) stFail(phase);
  return ok;
}



// --- アプリケーション状態 ---
AppMode gAppMode = MODE_NORMAL;
std::string gInputText = "";
bool gInputIsQuote = false;
bool gInputIsOnetime = false;

// 挿入する要素の親IDを保持する変数
unsigned gInsertParent = 0;
