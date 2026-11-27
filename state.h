#pragma once
// 状態・型・enum・定数の宣言。定義は state.cpp（設計文書コメントは対応する定義・型側に原文のまま移動）。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "prop.h"
#include "document.h"
#include "graph.h"

// --- flavorContainer（root配信用のフレーバーテキスト） ---
// rootで配信されるonetime quoteはすべてフレーバーテキストの役割を持つため、
// 従来の「rootでonetime quoteを優先配信」処理に代わるものとして新設する。
// parent == kFlavorParent (0x0F) のElementをgParentに関係なく抽出する。
// parent以外の条件（hasRead / requiredReadId / excludedReadId / timer /
// unlocks / eyelevel）はrebuildContainerC()と同じ判定を追う。isOnetime・
// quote・optionの区別は抽出・選択で行わない。構築はprocessContainerLifecycle()
// の最初期（初回起動時を含む）から行う。
constexpr uint8_t kFlavorParent = 0x0F;

enum TimerAction : uint8_t {
  TIMER_ACTION_NONE = 0,
  TIMER_ACTION_START = 1,
  TIMER_ACTION_STOP = 2,
  TIMER_ACTION_RESET = 3,
};

// --- VIEWER / EDITOR の GUI 状態 ---
// VIEW_GENERAL と VIEW_FAMILY（Prompt 2 の family viewer）は実装済み。
// constraint / editor は hook のまま（後フェーズ）。mode 別の layout は
// layoutScene / layoutFamily がそれぞれ wx/wy・familyWx/familyWy を保持する。
enum ViewMode { VIEW_GENERAL = 0, VIEW_FAMILY = 1, VIEW_CONSTRAINT = 2, EDITOR = 3 };

// --- EDITOR mode の GUI state（Prompt 4・EditorState） ---
// runtime の console 挿入フロー（gAppMode / MODE_INSERT_TEXT / QUOTE / ONETIME）とは
// 別 state にする。gViewMode が EDITOR のときのみ入力を受け、それ以外は遮断する。
// START/STOP text input は対称に扱う（二重 Start を避けるため flag で管理する）。
enum EditorStage {
  EDITOR_IDLE = 0,   // add flow 非実行（ヘルプ＋次の id 候補の preview のみ）
  EDITOR_DIALOGUE,   // dialogue 入力（UTF-8・空白を含む入力をそのまま保持。空 dialogue 不可）
  EDITOR_QUOTE,      // isQuote? y/n（ENTER は no として扱う）
  EDITOR_ONETIME,    // isOnetime? y/n（ENTER は no として扱う）
  EDITOR_PARENT,     // ノードクリックで parent 候補を選ぶ（0 = root/no parent 許す・自己 id 禁止）
  EDITOR_CHILD,      // ノードクリックで child 候補を選ぶ（ENTER = none: slot1Index0/value0）
};

struct EditorState {
  EditorStage stage = EDITOR_IDLE;
  std::string dialogue;
  int assignedId = 0;      // id 候補（add 開始時の nextFreeId）。-1 は scene の id が 0xFFまで使用中
  uint8_t parentId = 0;    // prop bits 8-15。0 = root / no parent（既定として許す）
  uint8_t childId = 0;     // prop bits 40-47 slot1 value。childOptionSelected=false のときは none(value 0)
  bool childOptionSelected = false; // slot1 の child option（newRoot idx2 / excluded idx1 と排他）
  bool isQuote = false;    // bit 17
  bool isOnetime = false;  // bit 18
  bool syncRuntime = false; // G トグル（既定 off）。on のときだけ containerA/B に反映する
  std::string lastWarning;
};

// --- Prompt 5: props 編集 state（add flow の stage machine とは別の state） ---
// 受理は EDITOR mode・gEditor.stage == EDITOR_IDLE のときだけ（add flow と排他）。
// キー（panel open 時）: TAB=次項目, ENTER/SPACE=トグル(option cycle / bool 反転 / uint 最低bit),
// +/-=値 ±1, Y/N=bool（true/false・uint は max/0）, U=undo, G=graph 再構築, ESC=閉じる。
// 衝突回避: panel open 時は +/-（zoom）と ENTER/ESC（add flow・選択解除）を props 操作に優先する。
// TAB / U / Y / N は editorMode で遮断済みの runtime キーなので GUI 側に流用できる。
// G は add flow の runtime 同期トグルと state 排他で両立する（panel open 時は graph 再構築）。
enum PropsFieldKind { PF_BOOL = 0, PF_UINT, PF_OPTION, PF_ENUM };
enum PropsFieldRole { ROLE_PLAIN = 0, ROLE_ID, ROLE_PARENT, ROLE_TARGET, ROLE_TIMER_SUB, ROLE_EYE_SUB };
struct PropsFieldDef {
  const char *section;      // tab/section 名（basic flags → id/parent/scene → slot0 → slot1 → slot2 → slot3）
  const char *label;        // メニュー行の項目名
  PropsFieldKind kind;
  PropsFieldRole role;      // validation の種別（duplicate id / cross-scene target / option not set）
  int shift;                // prop の bit shift（PF_OPTION は index bits・PF_ENUM/PF_UINT は value bits）
  int width;                // bit 幅
  uint8_t slot;             // PF_OPTION / slot value / sub field の slot番号（PF_BOOL・id系は 0 unused）
  uint8_t max;              // clamp 上限（PF_OPTION は optCount-1 の option idx・PF_ENUM は enum max）
  const char *const *names; // PF_OPTION / PF_ENUM の表示名（先頭は none）
  uint8_t optCount;         // PF_OPTION: none を除いた option 数（index 0..optCount-1）
};


constexpr size_t kPropsUndoMax = 256; // Prompt 6: 上限 64→256。stack が満杯になると「全 undo で
// baseline に戻る」が成立しないため（Prompt 5 実測 55 entry・未到達）。長時間編集でも満杯にしない。

struct PropsUndoEntry {
  uint64_t prevProp;        // 編集直前の prop
  std::string prevLineText; // 編集直前の raw line text（元書式のまま・undo で完全復元するため）
  bool prevDirty;           // 編集直前の dirty flag
};
struct PropsEditState {
  bool active = false;
  uint32_t nodeKey = 0;               // 編集対象 node の (scene,id) key（編集後も現在の key に更新・復元は findNode）
  int cursor = 0;                     // fields の現在項目 index（TAB で循環）
  uint8_t baseSlotIndex[4] = {0, 0, 0, 0}; // panel open 時点の slot index（"none" は元の index に戻す対象）
  std::vector<PropsUndoEntry> undoStack;    // node key + previous prop（1段以上・U で復元）
  std::string lastWarning;
  size_t warningTotal = 0;            // panel open からの warning イベント数（selftest の期待値照合用）
};

// --- Prompt 6: 最終統合 state（game モード共存 / write policy トグル / selftest FAIL 検知） ---
// アーキテクチャ整理（要件 1・現行 identifier への map。再実装はしない）:
//   Document       = gDocument（RawLine text+terminator の原文・ElementRecord prop+dirty・
//                    comment/blank 保持）+ loadDocument/parseElementLine/serializeDocument
//   GraphIndex     = gGraph（adjacency・edges・roots・scenes・warnings）+ per-mode layout
//                    （layoutScene=general wx/wy・layoutFamily=familyWx/familyWy/famDepth）
//                    + active layout の一本化（nodePos()/applyActiveLayout()）+
//                    selectedNode は (scene,id) key（nodeKey()/findNode）で復元
//   ViewModes      = gViewMode（VIEW_GENERAL/FAMILY/CONSTRAINT/EDITOR）+ describeElement の
//                    view 別 props 表示順序・フィルタ（general/family/constraint/EDITOR variant）
//   EditorState    = gEditor（add flow・validation warning）+ gProps（props menu・warning・undo stack）
//   Serialization  = serializeDocument（未編集行=原文・dirty 行=正格式）+ verifyRoundTrip の
//                    baseline diff（added/edited・added_or_dirty_only・splitKeepText の行数规则）
//   UiMode         = gUiMode（UI_VIEWER=現行の graph overlay + HUD + console /
//                    UI_GAME=renderConsole と入力系統のみ・graph/HUD を描かない）。
//                    起動は DDM_MODE=viewer|game（既定=現行動作）、実行時は H キーで切替。
//   write policy   = ファイル write はしない（data.txt に書き戻さない）。legacy の
//                    ../data.txt write-back は load 元が ../ の legacy 運用のみ（HUD に状態明示）。
//                    quit の backupData は既定 off（B トグル）。
//   selftest FAIL  = gSelftestFailures に failed phase 名を集約。最終で "FAIL <phase>" を
//                    stdout に出し SDL_APP_FAILURE で終了する（期待値は実行時計算のまま）。
enum UiMode { UI_VIEWER = 0, UI_GAME = 1 };

// --- グローバル状態の extern 宣言（定義は state.cpp。internal linkage → external のみ変化、動作同じ） ---
extern std::vector<Element> containerA;
extern std::vector<Element> containerB;
extern std::vector<Element> containerC;
extern std::vector<Element> displayList;
extern std::vector<Element> objectList;
extern std::vector<Element> flavorContainer;

extern size_t selectedIndex;
extern size_t selectedObjectiveIndex;
extern bool isObjectiveSelected;

extern bool needRedraw;
extern bool noContentState;

extern bool gMoveMode;
extern std::vector<Element> gSuspendedDisplayList;
extern std::vector<Element> gSuspendedObjectiveList;
extern size_t gSuspendedSelectedIndex;
extern size_t gSuspendedSelectedObjectiveIndex;
extern bool gSuspendedIsObjectiveSelected;
extern bool gSuspendedNoContentState;

extern SDL_Window *gWindow;
extern SDL_Renderer *gRenderer;

extern unsigned gScene;
extern unsigned gRoot;
extern unsigned gParent;
extern unsigned gUnlocks;
extern unsigned gEyelevel;
extern unsigned gTick;
extern unsigned gTicketNumber;
extern unsigned gCallingNumber;

extern unsigned gTimers[4];
extern unsigned &gTimer0;
extern unsigned &gTimer1;
extern unsigned &gTimer2;
extern unsigned &gTimer3;
extern bool gTimerRunning[4];
extern uint8_t gPendingTimerActions[4];

extern unsigned gGTickDuration;
extern unsigned gGTickStamp;

extern Document gDocument;
extern Graph gGraph;
extern RoundTripReport gRoundTrip;
extern std::string gLoadedPath;
extern std::vector<std::string> gLoadWarnings;

extern ViewMode gViewMode;
extern uint8_t gViewScene;
extern bool gGraphEnabled;
extern bool gExportPanel;
extern bool gExportOnQuit;

extern float gViewZoom;
extern float gViewPanX;
extern float gViewPanY;
extern bool gDragging;
extern float gDragLastX;
extern float gDragLastY;

extern int gSelectedNode;

extern EditorState gEditor;
extern bool gEditorTextInputActive;
extern bool gDocTerminatorPatched;

extern const char *const kSlotOptionNames0[];
extern const char *const kSlotOptionNames1[];
extern const char *const kSlotOptionNames2[];
extern const char *const kSlotOptionNames3[];
extern const char *const kTimerActionNames[];
extern const char *const kTimerOpNames[];
inline const char *const *slotOptionNames(uint8_t slot) {
  switch (slot) {
    case 0: return kSlotOptionNames0;
    case 1: return kSlotOptionNames1;
    case 2: return kSlotOptionNames2;
    default: return kSlotOptionNames3;
  }
}
inline uint8_t slotOptionCount(uint8_t slot) {
  switch (slot) {
    case 0: return 2;
    case 1: return 4; // child / excludedReadId / newRoot / downUnlocks（index 0-3）
    case 2: return 4;
    default: return 4;
  }
}

extern const PropsFieldDef kPropsFields[];
extern const int kPropsFieldCount;

extern PropsEditState gProps;

extern UiMode gUiMode;
extern bool gBackupOnQuit;
extern size_t gLoadedBytes;
extern std::vector<std::string> gSelftestFailures;

void stFail(const char *phase);
bool stPass(bool ok, const char *phase);
const char *propsOptionLabel(uint64_t prop, uint8_t slot);

// 表示ヘルパー: id/parent/scene は hex と decimal の両方（入力は hex を優先）。flags は hex + binary。
inline std::string hexDec(uint8_t v) {
  return hexByte(v) + " dec=" + std::to_string(v);
}
inline std::string binaryByte(uint8_t v) {
  char buf[12];
  std::snprintf(buf, sizeof(buf), "0b%d%d%d%d%d%d%d%d",
                (static_cast<unsigned>(v) >> 7) & 1u, (static_cast<unsigned>(v) >> 6) & 1u,
                (static_cast<unsigned>(v) >> 5) & 1u, (static_cast<unsigned>(v) >> 4) & 1u,
                (static_cast<unsigned>(v) >> 3) & 1u, (static_cast<unsigned>(v) >> 2) & 1u,
                (static_cast<unsigned>(v) >> 1) & 1u, static_cast<unsigned>(v) & 1u);
  return buf;
}

// blueprint 風の寸法と色（world 単位）
// blueprint 風の寸法と色（world 単位）
constexpr int kNodeW = 240;
constexpr int kNodeH = 60;
constexpr int kGapX = 90;
constexpr int kGapY = 26;
constexpr int kHudW = 420;
constexpr int kPortR = 4;
constexpr int kTextStep = 18;
// --- アプリケーション状態 ---
enum AppMode {
  MODE_NORMAL,
  MODE_INSERT_TEXT,
  MODE_INSERT_QUOTE,
  MODE_INSERT_ONETIME
};

extern AppMode gAppMode;
extern std::string gInputText;
extern bool gInputIsQuote;
extern bool gInputIsOnetime;
extern unsigned gInsertParent;
