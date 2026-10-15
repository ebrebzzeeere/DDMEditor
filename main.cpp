/*
* 本当のことをいうと、rootにもどってきたとき、rootの選択肢は全部持っていて、
* onetimeQuoteがあったとき、onetimeQuoteを優先して配信する。
* その後、onetimeQuoteがなくなるまで、loopを回す。
* でも、特別なループは用意しないほうがいいと思う。
*
* あと、rootのとき、あたりまえだけど他のonetimeQuoteと競合するとtimerで有効になってるものは当たり前だけど普通に配信されない。
*/
#include <SDL3/SDL_keycode.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct Element {
  std::string text;
  uint64_t prop;
  /* prop bit assignment
  slot3 56-63 (0:timerTarget,1:newScene,2:excludedUnlockFlags,3:)
  slot2 48-55 (0:timerSetter,1:unlocks,2:requiredUnlockFlags,3:)
  slot1 40-47 (0:child,1:excludedReadId,2:newRoot,3: )
  slot0 32-39 (0:requiredReadId,1:,2:,3: )
  slot3Index 30,31
  slot2Index 28,29
  slot1Index 26,27
  slot0Index 24,25
  scene		20-23
  vacant	19
  isOnetime	18
  isQuote	17
  hasRead	16
  parent	8-15
  id		0-7

*/
/*
  timerTarget 8bit
  count 4-7
  timerId 2,3
  operator 0,1

  timerSetter 8bit
  action 4,5 // 01:start, 10:stop, 11:reset
  vacant 2,3
  timerId 0,1
  */

};

// --- ヘルパー関数: プロパティのフィールド抽出 (new prop bit assignment準拠) ---
inline uint8_t getPropID(uint64_t prop) { return prop & 0xFF; }
inline uint8_t getPropParent(uint64_t prop) { return (prop >> 8) & 0xFF; }
inline bool getPropHasRead(uint64_t prop) { return (prop >> 16) & 0x1; }
inline bool getPropIsQuote(uint64_t prop) { return (prop >> 17) & 0x1; }
inline bool getPropIsOnetime(uint64_t prop) { return (prop >> 18) & 0x1; }
inline uint8_t getPropScene(uint64_t prop) { return (prop >> 20) & 0xF; }

// Slot 0: bits 32-39, index: bits 24-25 (0: requiredReadId)
// The element with this ID in the current scene must already be read.
inline uint8_t getPropRequiredReadId(uint64_t prop) {
  return (((prop >> 24) & 0x3) == 0) ? ((prop >> 32) & 0xFF) : 0;
}

// Slot 1: bits 40-47, index: bits 26-27
// (0: child, 1: excludedReadId, 2: newRoot)
inline uint8_t getPropChild(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 0) ? ((prop >> 40) & 0xFF) : 0;
}
// The element with this ID in the current scene must not be read.
inline uint8_t getPropExcludedReadId(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 1) ? ((prop >> 40) & 0xFF) : 0;
}
inline uint8_t getPropNewRoot(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 2) ? ((prop >> 40) & 0xFF) : 0;
}

// Slot 2: bits 48-55, index: bits 28-29
// (0: timerSetter, 1: unlocks, 2: requiredUnlockFlags)
inline uint8_t getPropTimerSetter(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 0) ? ((prop >> 48) & 0xFF) : 0;
}
inline bool hasPropTimerSetter(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 0) && (((prop >> 52) & 0x3) != 0);
}
inline uint8_t getPropTimerSetterId(uint64_t prop) {
  return getPropTimerSetter(prop) & 0x3;
}
inline uint8_t getPropTimerSetterAction(uint64_t prop) {
  return (getPropTimerSetter(prop) >> 4) & 0x3;
}
inline uint8_t getPropUnlocks(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 1) ? ((prop >> 48) & 0xFF) : 0;
}
inline uint8_t getPropRequiredUnlockFlags(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 2) ? ((prop >> 48) & 0xFF) : 0;
}

// Slot 3: bits 56-63, index: bits 30-31
// (0: timerTarget, 1: newScene, 2: excludedUnlockFlags)
// timerTarget: count (bits 4-7), timerId (bits 2-3), operator (bits 0-1)
// operator: 0b01: less (<), 0b10: more (>), 0b11: equal (==)
inline bool hasPropTimerTarget(uint64_t prop) {
  return (((prop >> 30) & 0x3) == 0) && (((prop >> 56) & 0x3) != 0);
}
inline uint8_t getPropTimerTarget(uint64_t prop) {
  return (((prop >> 30) & 0x3) == 0) ? ((prop >> 56) & 0xFF) : 0;
}
inline uint8_t getPropTimerTargetOp(uint64_t prop) {
  return ((prop >> 56) & 0x3);
}
inline uint8_t getPropTimerTargetId(uint64_t prop) {
  return ((prop >> 58) & 0x3);
}
inline uint8_t getPropTimerTargetCount(uint64_t prop) {
  return ((prop >> 60) & 0xF);
}
inline uint8_t getPropNewScene(uint64_t prop) {
  return (((prop >> 30) & 0x3) == 1) ? ((prop >> 56) & 0xFF) : 0;
}
inline uint8_t getPropExcludedUnlockFlags(uint64_t prop) {
  return (((prop >> 30) & 0x3) == 2) ? ((prop >> 56) & 0xFF) : 0;
}

inline uint8_t getSlotIndex(uint64_t prop, uint8_t slot) {
  return (prop >> (24 + slot * 2)) & 0x3;
}
inline uint8_t getSlotValue(uint64_t prop, uint8_t slot) {
  return (prop >> (32 + slot * 8)) & 0xFF;
}

// グローバル変数
std::vector<Element> containerA;
std::vector<Element> containerB;
std::vector<Element> containerC;
std::vector<Element> displayList;
std::vector<Element> objectiveList;

size_t selectedIndex = 0;
size_t selectedObjectiveIndex = 0;
bool isObjectiveSelected = false;

bool needRedraw = true;
bool noContentState = false;

SDL_Window *gWindow = nullptr;
SDL_Renderer *gRenderer = nullptr;

unsigned gScene = 3;
unsigned gRoot = 0;
unsigned gParent = 0;
unsigned gUnlocks = 0;

// タイマー 0-3
unsigned gTimers[4] = {0, 0, 0, 0};
unsigned &gTimer0 = gTimers[0];
unsigned &gTimer1 = gTimers[1];
unsigned &gTimer2 = gTimers[2];
unsigned &gTimer3 = gTimers[3];
bool gTimerRunning[4] = {false, false, false, false};
uint8_t gPendingTimerActions[4] = {0, 0, 0, 0};

enum TimerAction : uint8_t {
  TIMER_ACTION_NONE = 0,
  TIMER_ACTION_START = 1,
  TIMER_ACTION_STOP = 2,
  TIMER_ACTION_RESET = 3,
};

// containerB contains only elements in gScene.  ID 0 means that the
// corresponding read-state condition is not configured.
inline bool isReadInCurrentScene(uint8_t id) {
  for (const auto &item : containerB) {
    if (getPropID(item.prop) == id) {
      return getPropHasRead(item.prop);
    }
  }
  return false;
}

inline bool satisfiesRequiredReadId(uint8_t id) {
  return id == 0 || isReadInCurrentScene(id);
}

inline bool satisfiesExcludedReadId(uint8_t id) {
  return id == 0 || !isReadInCurrentScene(id);
}

// cond5: timerTarget の条件判定
inline bool evalCond5(uint64_t prop) {
  if (!hasPropTimerTarget(prop)) {
    return true; // timerTarget 未設定なら条件クリア
  }
  uint8_t op = getPropTimerTargetOp(prop);
  uint8_t timerId = getPropTimerTargetId(prop);
  uint8_t count = getPropTimerTargetCount(prop);

  // Stopped timers never satisfy a timerTarget, even if a stop action kept
  // their previous non-zero count.
  if (!gTimerRunning[timerId] || gTimers[timerId] == 0) {
    return false;
  }

  if (op == 0b01) { // less (<)
    return gTimers[timerId] < count;
  } else if (op == 0b10) { // more (>)
    return gTimers[timerId] > count;
  } else if (op == 0b11) { // equal (==)
    return gTimers[timerId] == count;
  }
  return false;
}

inline void resetTimer(uint8_t timerId) {
  gTimers[timerId] = 0;
  gTimerRunning[timerId] = false;
}

// This function is called only when a dialogue cycle has returned to its
// root.  timerSetter actions collected during that cycle are applied here.
// A running timer wraps from 15 to 0 and stops.
inline void advanceTimers() {
  for (uint8_t timerId = 0; timerId < 4; ++timerId) {
    uint8_t action = gPendingTimerActions[timerId];

    if (action == TIMER_ACTION_STOP) {
      gTimerRunning[timerId] = false;
    } else if (action == TIMER_ACTION_RESET) {
      resetTimer(timerId);
    } else {
      if (action == TIMER_ACTION_START) {
        gTimerRunning[timerId] = true;
      }

      if (gTimerRunning[timerId]) {
        if (gTimers[timerId] == 15) {
          resetTimer(timerId);
        } else {
          ++gTimers[timerId];
        }
      }
    }

    gPendingTimerActions[timerId] = TIMER_ACTION_NONE;
  }
}

void rebuildContainerB();
void rebuildContainerC();
void rebuildDisplayList();
void rebuildObjectiveList();

// 指定されたverbIdをparentとして持つ有効なobjectiveが存在するか判定
bool hasValidObjective(unsigned verbId) {
  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = getPropParent(prop);
    bool hasRead = getPropHasRead(prop);
    uint8_t requiredReadId = getPropRequiredReadId(prop);
    uint8_t excludedReadId = getPropExcludedReadId(prop);

    bool cond1 = (parent == verbId);
    bool cond2 = satisfiesRequiredReadId(requiredReadId);
    bool cond3 = !hasRead;
    bool cond4 = satisfiesExcludedReadId(excludedReadId);
    bool cond5 = evalCond5(prop);
    // bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
    //              getPropRequiredUnlockFlags(prop);
    // bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
    //              (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
    //                  getPropExcludedUnlockFlags(prop);

    if (cond1 && cond2 && cond3 && cond4 && cond5) {
      return true;
    }
  }
  return false;
}
void renderConsole() {
  std::cout << "\033[2J\033[1;1H";

  // 1. ステート情報
  std::cout << "=== [ State ] ===" << std::endl;
  std::cout << "gScene   : " << gScene << "\n"
            << "gRoot    : " << gRoot << "\n"
            << "gParent  : " << gParent << "\n"
            << "gUnlocks : 0x" << std::hex << gUnlocks << std::dec << "\n"
            << "gTimers  : [0]:" << gTimer0 << " [1]:" << gTimer1
            << " [2]:" << gTimer2 << " [3]:" << gTimer3 << "\n\n";

  // 2. コンテナ内部状態のデバッグ表示
  std::cout << "=== [ Containers Status ] ===" << std::endl;
  std::cout << "Container A (Total Loaded) : " << containerA.size()
            << std::endl;
  std::cout << "Container B (Scene Matched): " << containerB.size()
            << std::endl;
  std::cout << "Container C (Cond Matched) : " << containerC.size()
            << std::endl;
  std::cout << "DisplayList                : " << displayList.size()
            << std::endl;
  std::cout << "ObjectiveList              : " << objectiveList.size()
            << std::endl;
  std::cout << "-------------------------------------------\n" << std::endl;

  if (noContentState || displayList.empty()) {
    std::cout << "\033[33m[no content]\033[0m" << std::endl;
    std::cout << "\n(Press 'f' to continue / fallback)" << std::endl;
    std::cout << std::flush;
    return;
  }

  std::cout << "=== [ Selection ] ===" << std::endl;

  // --- 1段目: Verb の表示 ---
  for (size_t i = 0; i < displayList.size(); ++i) {
    bool isSelectedVerb = (i == selectedIndex);
    unsigned verbId = getPropID(displayList[i].prop);
    unsigned verbChild = getPropChild(displayList[i].prop);

    bool hasObj = false;
    if (verbChild) {
      for (const auto &item : containerB) {
        if (getPropID(item.prop) == verbChild) {
          if (item.text == "dummy") {
            hasObj = hasValidObjective(getPropID(item.prop));
          } else {
            hasObj = true;
          }
          break;
        }
      }
    } else {
      hasObj = hasValidObjective(verbId);
    }

    std::string itemText = displayList[i].text;
    if (!hasObj) {
      itemText = "[" + itemText + "]";
    }

    if (isSelectedVerb && !isObjectiveSelected) {
      std::cout << "\033[31m*" << itemText << "\033[0m ";
    } else if (isSelectedVerb && isObjectiveSelected) {
      std::cout << "\033[33m" << itemText << "\033[0m ";
    } else {
      std::cout << "\033[37m" << itemText << "\033[0m ";
    }
  }
  std::cout << std::endl;

  // --- 2段目: Objective の表示 ---
  if (gRoot == gParent) {

    if (objectiveList.empty()) {
      if (isObjectiveSelected) {
        std::cout << "\033[31m*no content\033[0m" << std::endl;
      } else {
        std::cout << "\033[90mno content\033[0m" << std::endl;
      }
    } else {
      for (size_t j = 0; j < objectiveList.size(); ++j) {
        std::string objText = objectiveList[j].text;
        if (objText.empty()) {
          objText = "[空]";
        }

        if (j == selectedObjectiveIndex && isObjectiveSelected) {
          std::cout << "\033[31m*" << objText << "\033[0m ";
        } else {
          std::cout << "\033[37m" << objText << "\033[0m ";
        }
      }
      std::cout << std::endl;
    }
  }
  std::cout << "\n(Focus Window -> 'i': Left, 'o': Right, 'a': Select "
               "Objective, 'u': Select Verb, 'f': Send, 'q': Quit)"
            << std::endl;
  std::cout << std::flush;
}
void rebuildContainerB() {
  containerB.clear();
  for (const auto &item : containerA) {
    unsigned scene = getPropScene(item.prop);
    if (scene == gScene) {
      containerB.push_back(item);
    }
  }
}

void rebuildContainerC() {
  containerC.clear();
  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = getPropParent(prop);
    bool hasRead = getPropHasRead(prop);
    uint8_t requiredReadId = getPropRequiredReadId(prop);
    uint8_t excludedReadId = getPropExcludedReadId(prop);

    bool cond1 = (parent == gParent);
    bool cond2 = satisfiesRequiredReadId(requiredReadId);
    bool cond3 = !hasRead;
    bool cond4 = satisfiesExcludedReadId(excludedReadId);
    bool cond5 = evalCond5(prop);
    // bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
    //              getPropRequiredUnlockFlags(prop);
    // bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
    //              (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
    //                  getPropExcludedUnlockFlags(prop);

    if (cond1 && cond2 && cond3 && cond4 && cond5) {
      containerC.push_back(item);
      // 子にdummyがいたら、最優先で処理される。
      if (item.text == "dummy") {
        if (hasPropTimerTarget(item.prop)) {
          uint8_t timerId = getPropTimerTargetId(item.prop);
          resetTimer(timerId);
        }
        gParent = getPropID(item.prop);
        rebuildContainerC();
        return;
      }
    }
  }
}

void rebuildDisplayList() {
  displayList.clear();
  selectedIndex = 0;

  if (!containerC.empty()) {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<Element> candidates;
    for (const auto &item : containerC) {
      bool isOnetime = getPropIsOnetime(item.prop);
      bool isQuote = getPropIsQuote(item.prop);
      if (isOnetime && isQuote) {
        candidates.push_back(item);
      }
    }

    if (!candidates.empty()) {
      std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
      displayList.push_back(candidates[dist(gen)]);
    } else if (containerC.size() >= 5) {
      std::vector<Element> temp = containerC;
      std::shuffle(temp.begin(), temp.end(), gen);
      displayList.assign(temp.begin(), temp.begin() + 4);
    } else {
      displayList = containerC;
    }
  }
}

void rebuildObjectiveList() {
  objectiveList.clear();
  selectedObjectiveIndex = 0;

  if (displayList.empty() || selectedIndex >= displayList.size()) {
    return;
  }
  bool isVerbQuote = getPropIsQuote(displayList[selectedIndex].prop);
  if (isVerbQuote) {
    return;
  }
  unsigned verbId = getPropID(displayList[selectedIndex].prop);
  unsigned verbChild = getPropChild(displayList[selectedIndex].prop);
  if (verbChild) {
    for (const auto &item : containerB) {
      if (getPropID(item.prop) == verbChild) {
        if (item.text == "dummy") {
          verbId = getPropID(item.prop);
        } else {
          objectiveList.push_back(item);
          return;
        }
        break;
      }
    }
  }
  bool hasDummy = false;
  do {
    hasDummy = false;
    for (const auto &item : containerB) {
      uint64_t prop = item.prop;

      unsigned parent = getPropParent(prop);
      bool hasRead = getPropHasRead(prop);
      uint8_t requiredReadId = getPropRequiredReadId(prop);
      uint8_t excludedReadId = getPropExcludedReadId(prop);

      bool cond1 = (parent == verbId);
      bool cond2 = satisfiesRequiredReadId(requiredReadId);
      bool cond3 = !hasRead;
      bool cond4 = satisfiesExcludedReadId(excludedReadId);
      bool cond5 = evalCond5(prop);
      // bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
      //              getPropRequiredUnlockFlags(prop);
      // bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
      //              (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
      //                  getPropExcludedUnlockFlags(prop);

      if (cond1 && cond2 && cond3 && cond4 && cond5) {
        // dummyがいたら、dummyを親にしてやりなおし。
        if (item.text == "dummy") {
          if (hasPropTimerTarget(item.prop)) {
            uint8_t timerId = getPropTimerTargetId(item.prop);
            resetTimer(timerId);
          }
          verbId = getPropID(item.prop);
          hasDummy = true;
          break;
        }
        objectiveList.push_back(item);
      }
    }
  } while (hasDummy);
}

void processContainerLifecycle() {
  rebuildContainerB();
  rebuildContainerC();
  isObjectiveSelected = false;

  if (containerC.empty()) {
    noContentState = true;
    displayList.clear();
    objectiveList.clear();
  } else {
    noContentState = false;
    rebuildDisplayList();
    rebuildObjectiveList();
  }
}

void sendProperty() {
  if (noContentState) {
    rebuildContainerB();
    rebuildContainerC();

    if (containerC.empty()) {
      gParent = gRoot;
      rebuildContainerC();

      if (containerC.empty()) {
        gRoot = 0;
        gParent = 0;
        gUnlocks = 0;
        rebuildContainerC();
      }
    }

    noContentState = false;
    rebuildDisplayList();
    rebuildObjectiveList();
    if (gParent == gRoot) {
      advanceTimers();
    }
    needRedraw = true;
    return;
  }

  if (displayList.empty() || selectedIndex >= displayList.size())
    return;

  Element selected;
  if (isObjectiveSelected) {
    if (objectiveList.empty()) {
      // no content を選択して送信する場合のダミー要素
      // (prop=0でルート遷移等の効果)
      selected = {"no content", 0ULL};
    } else {
      if (selectedObjectiveIndex >= objectiveList.size())
        return;
      selected = objectiveList[selectedObjectiveIndex];
    }
  } else {
    // Verb選択中：isQuote == true の場合のみ送信許可
    // あとRootにいなければverbも送信できるようにする。
    // -> verb かつ onRootなら送信できない。
    Element verb = displayList[selectedIndex];
    bool isQuote = getPropIsQuote(verb.prop);
    bool isOnRoot = (getPropParent(verb.prop) == gRoot);
    if (!isQuote && isOnRoot) {
      return;
    }
    selected = verb;
  }

  uint64_t prop = selected.prop;

  unsigned id = getPropID(prop);
  unsigned parent = getPropParent(prop);
  unsigned child = getPropChild(prop);
  unsigned newRoot = getPropNewRoot(prop);
  unsigned newScene = getPropNewScene(prop);
  bool isOnetime = getPropIsOnetime(prop);
  unsigned unlocks = getPropUnlocks(prop);
  bool hasTimerSetter = hasPropTimerSetter(prop);
  uint8_t timerSetterId = getPropTimerSetterId(prop);
  uint8_t timerSetterAction = getPropTimerSetterAction(prop);
  if (hasTimerSetter && timerSetterAction != TIMER_ACTION_NONE) {
    gPendingTimerActions[timerSetterId] = timerSetterAction;
  }

  // Remember the condition that made the selected element sendable.  A
  // successful timerTarget is one-shot and consumes its timer.
  bool consumesTimerTarget = hasPropTimerTarget(prop) && evalCond5(prop);
  uint8_t timerTargetId = getPropTimerTargetId(prop);

  if (consumesTimerTarget) {
    resetTimer(timerTargetId);
  }

  if (child) {
    for (const auto &item : containerB) {
      if (getPropID(item.prop) == child) {
        if (item.text == "dummy") {
          gParent = getPropID(item.prop);
        } else {
          containerC.clear();
          containerC.push_back(item);
          isObjectiveSelected = false;
          noContentState = false;
          rebuildDisplayList();
          rebuildObjectiveList();

          needRedraw = true;
          return;
        }
      }
    }
  } else {
    gParent = id;
  }

  if (isOnetime) {
    for (auto &item : containerA) {
      if (item.text == selected.text && item.prop == selected.prop) {
        item.prop |= (1ULL << 16); // hasRead (bit 16)
        break;
      }
    }
  }

  if (newScene != 0) {
    gScene = newScene;
    gRoot = 0;
    gParent = 0;
    gUnlocks = 0;
  } else {
    gUnlocks |= unlocks;

    if (newRoot != 0) {
      gRoot = newRoot;
      gUnlocks = 0x0; // リセットする。
    }
  }

  processContainerLifecycle();
  // A timer counts completed dialogue cycles, not individual selections in a
  // verb/objective chain.  Advance only after returning to the current root.
  if (gParent == gRoot) {
    advanceTimers();
  }
  needRedraw = true;
}

// --- アプリケーション状態 ---
enum AppMode {
  MODE_NORMAL,
  MODE_INSERT_TEXT,
  MODE_INSERT_QUOTE,
  MODE_INSERT_ONETIME
};

AppMode gAppMode = MODE_NORMAL;
std::string gInputText = "";
bool gInputIsQuote = false;
bool gInputIsOnetime = false;

// 挿入する要素の親IDを保持する変数
unsigned gInsertParent = 0;

uint64_t buildProperty(unsigned id, unsigned parent, unsigned scene,
                       bool isQuote, bool isOnetime) {
  uint64_t prop = 0;
  prop |= (id & 0xFF);                          // bit 0-7: id
  prop |= ((uint64_t(parent) & 0xFF) << 8);     // bit 8-15: parent
  if (isQuote)
    prop |= (1ULL << 17);                       // bit 17: isQuote
  if (isOnetime)
    prop |= (1ULL << 18);                       // bit 18: isOnetime
  prop |= ((uint64_t(scene) & 0x0F) << 20);     // bit 20-23: scene
  return prop;
}

void cancelInsertMode() {
  gInputText.clear();
  gInputIsQuote = false;
  gInputIsOnetime = false;
  gAppMode = MODE_NORMAL;

  SDL_StopTextInput(gWindow);
  rebuildContainerC();
  rebuildDisplayList();
  rebuildObjectiveList();
  needRedraw = true;
}

// SDL_AppInit
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
    return SDL_APP_FAILURE;
  }

  gWindow = SDL_CreateWindow("DDMEditor Focus Window", 320, 240,
                             SDL_WINDOW_RESIZABLE);
  if (!gWindow) {
    std::cerr << "Window Creation Error: " << SDL_GetError() << std::endl;
    return SDL_APP_FAILURE;
  }

  gRenderer = SDL_CreateRenderer(gWindow, nullptr);
  if (!gRenderer) {
    std::cerr << "Renderer Creation Error: " << SDL_GetError() << std::endl;
    return SDL_APP_FAILURE;
  }
  SDL_SetRenderVSync(gRenderer, 1);

  std::string file_path = "../dd.txt";
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "エラー: " << file_path << " を開けませんでした。"
              << std::endl;
    return SDL_APP_FAILURE;
  }

  std::string token;
  std::string current_text;
  bool is_text = true;

  while (file >> token) {
    if (token.rfind("//", 0) == 0 || token.rfind("/*", 0) == 0 ||
        token.rfind("*", 0) == 0) {
      std::string dummy;
      std::getline(file, dummy);
      continue;
    }

    std::string clean_token = "";
    for (char c : token) {
      if (c != '\'' && c != ',' && c != ' ' && c != '\r' && c != '\n') {
        clean_token += c;
      }
    }
    if (clean_token.empty())
      continue;

    if (is_text) {
      current_text = clean_token;
      is_text = false;
    } else {
      try {
        uint64_t prop = std::stoull(clean_token, nullptr, 16);
        containerA.push_back({current_text, prop});
        is_text = true;
      } catch (const std::exception &) {
        is_text = true;
        continue;
      }
    }
  }
  file.close();

  processContainerLifecycle();
  return SDL_APP_CONTINUE;
}

// SDL_AppEvent
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }

  if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
    if (gAppMode != MODE_NORMAL) {
      cancelInsertMode();
      return SDL_APP_CONTINUE;
    }
  }

  if (gAppMode == MODE_NORMAL) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;

      if (key == SDLK_Q) {
        return SDL_APP_SUCCESS;
      }

      if (key == SDLK_TAB) {
        gAppMode = MODE_INSERT_TEXT;
        gInputText.clear();

        // Objective段を選択中なら VerbのID を親に、そうでなければ現在の
        // gParent を親にする
        if (isObjectiveSelected && !displayList.empty()) {
          gInsertParent = getPropID(displayList[selectedIndex].prop);
        } else {
          gInsertParent = gParent;
        }

        SDL_StartTextInput(gWindow);
        needRedraw = true;
      } else if (key == SDLK_F) {
        sendProperty();
      } else if (!displayList.empty() && !noContentState) {
        if (key == SDLK_O) {
          if (isObjectiveSelected) {
            if (!objectiveList.empty()) {
              selectedObjectiveIndex =
                  (selectedObjectiveIndex + 1) % objectiveList.size();
              needRedraw = true;
            }
          } else {
            selectedIndex = (selectedIndex + 1) % displayList.size();
            rebuildObjectiveList();
            needRedraw = true;
          }
        } else if (key == SDLK_I) {
          if (isObjectiveSelected) {
            if (!objectiveList.empty()) {
              selectedObjectiveIndex =
                  (selectedObjectiveIndex - 1 + objectiveList.size()) %
                  objectiveList.size();
              needRedraw = true;
            }
          } else {
            selectedIndex =
                (selectedIndex - 1 + displayList.size()) % displayList.size();
            rebuildObjectiveList();
            needRedraw = true;
          }
        } else if (key == SDLK_A) {
          // Verb選択中の場合、Objectiveの段に移動（Objectiveが空でも移動可能に変更）
          if (!isObjectiveSelected && gParent == gRoot) {
            isObjectiveSelected = true;
            selectedObjectiveIndex = 0;
            needRedraw = true;
          }
        } else if (key == SDLK_U) {
          // Objective選択中の場合、Verbの段に移動
          if (isObjectiveSelected) {
            isObjectiveSelected = false;
            needRedraw = true;
          }
        }
      }
    }
    return SDL_APP_CONTINUE;
  }

  if (gAppMode == MODE_INSERT_TEXT) {
    if (event->type == SDL_EVENT_TEXT_INPUT) {
      gInputText += event->text.text;
      needRedraw = true;
    } else if (event->type == SDL_EVENT_KEY_DOWN) {
      if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) {
        if (!gInputText.empty()) {
          SDL_StopTextInput(gWindow);
          gAppMode = MODE_INSERT_QUOTE;
          needRedraw = true;
        }
      } else if (event->key.key == SDLK_BACKSPACE) {
        if (!gInputText.empty()) {
          gInputText.pop_back();
          needRedraw = true;
        }
      }
    }
    return SDL_APP_CONTINUE;
  }

  if (gAppMode == MODE_INSERT_QUOTE) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;
      if (key == SDLK_Y || key == SDLK_N) {
        gInputIsQuote = (key == SDLK_Y);
        gAppMode = MODE_INSERT_ONETIME;
        needRedraw = true;
      }
    }
    return SDL_APP_CONTINUE;
  }

  if (gAppMode == MODE_INSERT_ONETIME) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;
      if (key == SDLK_Y || key == SDLK_N) {
        gInputIsOnetime = (key == SDLK_Y);

        unsigned id = containerB.size() + 1;
        uint64_t newProp = buildProperty(id, gInsertParent, gScene,
                                         gInputIsQuote, gInputIsOnetime);

        Element newElem = {gInputText, newProp};
        containerA.push_back(newElem);
        containerB.push_back(newElem);

        // 追加された要素を即座に反映させるためリストを更新
        rebuildContainerC();
        rebuildDisplayList();
        rebuildObjectiveList();

        gInputText.clear();
        gAppMode = MODE_INSERT_TEXT;
        SDL_StartTextInput(gWindow);
        needRedraw = true;
      }
    }
    return SDL_APP_CONTINUE;
  }

  return SDL_APP_CONTINUE;
}

// SDL_AppIterate
SDL_AppResult SDL_AppIterate(void *appstate) {
  constexpr Uint64 TARGET_FRAME_MS = 33;
  Uint64 frameStart = SDL_GetTicks();

  if (needRedraw) {
    renderConsole();

    if (gAppMode == MODE_INSERT_TEXT) {
      std::cout << "\r[INSERT] Text: " << gInputText << "_" << std::flush;
    } else if (gAppMode == MODE_INSERT_QUOTE) {
      std::cout << "\r[INSERT] isQuote? <y/n>: " << std::flush;
    } else if (gAppMode == MODE_INSERT_ONETIME) {
      std::cout << "\r[INSERT] isOnetime? <y/n>: " << std::flush;
    }

    needRedraw = false;
  }

  SDL_SetRenderDrawColor(gRenderer, 30, 30, 30, 255);
  SDL_RenderClear(gRenderer);
  SDL_RenderPresent(gRenderer);

  Uint64 frameTime = SDL_GetTicks() - frameStart;
  if (frameTime < TARGET_FRAME_MS) {
    SDL_Delay(TARGET_FRAME_MS - frameTime);
  }

  return SDL_APP_CONTINUE;
}

// SDL_AppQuit
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  std::ofstream ofs("../dd.txt");
  if (ofs.is_open()) {
    for (auto item : containerA) {
      if (item.text != "dummy") {
        item.prop &= ~(1ULL << 16); // clear hasRead (bit 16)
      }

      ofs << "'" << item.text << "', '0x" << std::hex << std::setw(16)
          << std::setfill('0') << item.prop << "',\n";
    }
    ofs.close();
  }

  if (gRenderer)
    SDL_DestroyRenderer(gRenderer);
  if (gWindow)
    SDL_DestroyWindow(gWindow);
}
