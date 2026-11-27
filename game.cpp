#include "game.h"
#include <cstdint>
#include <random>
#include <vector>
#include "prop.h"
#include "state.h"
#include "guitext.h"




// --- gTick秒読みタイマー ---
// sendProperty()内からの呼び出しを想定する。
// 初回呼び出しで1-3のdurationを抽選し、その時点のgTickを秒読み開始点として
// 記録する（この時点では発火しない）。以後の呼び出しで、現在gTickと開始点の
// 差がdurationに等しくなった時点で発火し（true）、新たなdurationを抽選して
// 開始点を現在のgTickへ更新し、秒読みを再開する。
// gTickがシーン変更/newRootでリセットされた場合（現在値 < 開始点）は、
// 現在のgTickを新しい開始点として秒読みをやり直す。
bool gTickTimer() {
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<unsigned> dist(1, 3);

  if (gGTickDuration == 0) {
    gGTickDuration = dist(rng);
    gGTickStamp = gTick;
    return false;
  }

  if (gTick < gGTickStamp) { // gTickリセット検出: 開始点を更新して続行
    gGTickStamp = gTick;
    return false;
  }

  if (gTick - gGTickStamp == gGTickDuration) {
    gGTickDuration = dist(rng); // 発火後、次のdurationを設定し秒読みを再開
    gGTickStamp = gTick;
    return true;
  }
  return false;
}

bool gimmick(uint64_t prop) {
  // 本当はswitch(gScene)を入れて汎用的な関数クラスタにしたい。

  if (gTickTimer()) {
    gUnlocks |= 0x1;
    ++gCallingNumber;
  } else {
    gUnlocks &= ~1ULL;
  }

  switch (prop) {
  case 0x0010000000100a07ULL: // 札を取る。
    gTicketNumber = gTick + 1032;
    return true;
    break;
  default:
    return false;
    break;
  }
  /*
   * このなかでやることは多い。書いていく。
   * 札ギミックの時
   *
   * 札を取る
   * 札の番号を生成して返す、保持する。
   * もう一度札を持っていたらチェックして、取り替えるかどうか聞く。
   *
   */
}

// 指定されたverbIdをparentとして持つ有効なobjectが存在するか判定
bool hasValidObjective(unsigned verbId) {
  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = getPropParent(prop);
    bool hasRead = getPropHasRead(prop);
    uint8_t requiredReadId = getPropRequiredReadId(prop);
    uint8_t excludedReadId1 = getPropExcludedReadId1(prop);
    uint8_t excludedReadId2 = getPropExcludedReadId2(prop);
    uint8_t eyelevel = getPropEyelevel(prop);
    bool isOnetime = getPropIsOnetime(item.prop);

    bool cond1 = (parent == verbId);
    bool cond2 = satisfiesRequiredReadId(requiredReadId);
    bool cond3 = !hasRead;
    cond3 |= !isOnetime;
    /*
     * TODO not isOnetimeなら、cond3は常時false。ただし、hasReadフラグは立てる。
     */
    bool cond4_1 = satisfiesExcludedReadId(excludedReadId1);
    bool cond4_2 = satisfiesExcludedReadId(excludedReadId2);
    bool cond4 = cond4_1 && cond4_2;
    bool cond5 = evalCond5(prop);
    bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
                 getPropRequiredUnlockFlags(prop);
    bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
                 (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
                     getPropExcludedUnlockFlags(prop);
    bool cond8 = (gEyelevel == eyelevel) || (eyelevel == 0x0);

    if (cond1 && cond2 && cond3 && cond4 && cond5 && cond6 && cond7 && cond8) {
      return true;
    }
  }
  return false;
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
    uint8_t excludedReadId1 = getPropExcludedReadId1(prop);
    uint8_t excludedReadId2 = getPropExcludedReadId2(prop);
    uint8_t eyelevel = getPropEyelevel(prop);
    bool isOnetime = getPropIsOnetime(item.prop);

    bool cond1 = (parent == gParent);
    bool cond2 = satisfiesRequiredReadId(requiredReadId);
    bool cond3 = !hasRead;
    cond3 |= !isOnetime;
    bool cond4_1 = satisfiesExcludedReadId(excludedReadId1);
    bool cond4_2 = satisfiesExcludedReadId(excludedReadId2);
    bool cond4 = cond4_1 && cond4_2;
    bool cond5 = evalCond5(prop);
    bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
                 getPropRequiredUnlockFlags(prop);
    bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
                 (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
                     getPropExcludedUnlockFlags(prop);
    bool cond8 = (gEyelevel == eyelevel) || (eyelevel == 0x0);

    if (cond1 && cond2 && cond3 && cond4 && cond5 && cond6 && cond7 && cond8) {
      containerC.push_back(item);
      // 子にdummyがいたら、最優先で処理される。
      if (item.dialogue == "dummy") {
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

// flavorContainerの構築。root配信用のフレーバーテキストを、gParentに関係なく
// parent == kFlavorParent (0x0F) のElementから抽出する。親条件（cond1）を
// gParent一致からkFlavorParent一致に置き換えたそれ以外の条件は
// rebuildContainerC()と同じ判定を使う。isOnetime / isQuoteは条件に含めない。
// 構築はprocessContainerLifecycle()から行われる。初回起動（SDL_AppInit）でも
// この経路を通るため、root状態への最初の配信からflavorが選ばれる。
// sendProperty()も送信後に同じ経路を通るため、追加の再構築は不要だが、
// 送信後の状態での再構築を明示するためsendProperty()内でも呼ばれる。
void rebuildFlavorContainer() {
  flavorContainer.clear();
  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = getPropParent(prop);
    bool hasRead = getPropHasRead(prop);
    uint8_t requiredReadId = getPropRequiredReadId(prop);
    uint8_t excludedReadId1 = getPropExcludedReadId1(prop);
    uint8_t excludedReadId2 = getPropExcludedReadId2(prop);
    uint8_t eyelevel = getPropEyelevel(prop);
    bool isOnetime = getPropIsOnetime(item.prop);

    bool cond1 = (parent == kFlavorParent); // gParentに関係なくフレーバー親のみ
    bool cond2 = satisfiesRequiredReadId(requiredReadId);
    bool cond3 = !hasRead;
    cond3 |= !isOnetime;
    bool cond4_1 = satisfiesExcludedReadId(excludedReadId1);
    bool cond4_2 = satisfiesExcludedReadId(excludedReadId2);
    bool cond4 = cond4_1 && cond4_2;
    bool cond5 = evalCond5(prop);
    bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
                 getPropRequiredUnlockFlags(prop);
    bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
                 (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
                     getPropExcludedUnlockFlags(prop);
    bool cond8 = (gEyelevel == eyelevel) || (eyelevel == 0x0);

    if (cond1 && cond2 && cond3 && cond4 && cond5 && cond6 && cond7 && cond8) {
      flavorContainer.push_back(item);
    }
  }
}

void rebuildDisplayList() {
  displayList.clear();
  selectedIndex = 0;

  // root状態でflavorContainerに要素があれば、isOnetime・quote・optionの区別や
  // containerCに依存せず、その中から一つだけランダムに選んで表示する。
  // （従来この関数で行っていた「rootでonetime・quoteを優先配信」処理は
  // flavorContainerの役割に統合するため削除した。）
  if (gParent == gRoot && !flavorContainer.empty()) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, flavorContainer.size() - 1);
    displayList.push_back(flavorContainer[dist(gen)]);
    return;
  }

  // flavorContainerが空のときは通常通り、containerCからdisplayListを構成する。
  if (!containerC.empty()) {
    std::random_device rd;
    std::mt19937 gen(rd());

    if (containerC.size() >= 5) {
      std::vector<Element> temp = containerC;
      std::shuffle(temp.begin(), temp.end(), gen);
      displayList.assign(temp.begin(), temp.begin() + 4);
    } else {
      displayList = containerC;
    }
  }
}

void rebuildObjectiveList() {
  objectList.clear();
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
        if (item.dialogue == "dummy") {
          verbId = getPropID(item.prop);
        } else {
          objectList.push_back(item);
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
      uint8_t excludedReadId1 = getPropExcludedReadId1(prop);
      uint8_t excludedReadId2 = getPropExcludedReadId2(prop);
      uint8_t eyelevel = getPropEyelevel(prop);
      bool isOnetime = getPropIsOnetime(item.prop);

      bool cond1 = (parent == verbId);
      bool cond2 = satisfiesRequiredReadId(requiredReadId);
      bool cond3 = !hasRead;
      cond3 |= !isOnetime;
      bool cond4_1 = satisfiesExcludedReadId(excludedReadId1);
      bool cond4_2 = satisfiesExcludedReadId(excludedReadId2);
      bool cond4 = cond4_1 && cond4_2;
      bool cond5 = evalCond5(prop);
      bool cond6 = (gUnlocks & getPropRequiredUnlockFlags(prop)) ==
                   getPropRequiredUnlockFlags(prop);
      bool cond7 = getPropExcludedUnlockFlags(prop) == 0 ||
                   (gUnlocks & getPropExcludedUnlockFlags(prop)) !=
                       getPropExcludedUnlockFlags(prop);
      bool cond8 = (gEyelevel == eyelevel) || (eyelevel == 0x0);

      if (cond1 && cond2 && cond3 && cond4 && cond5 && cond6 && cond7 &&
          cond8) {
        // dummyがいたら、dummyを親にしてやりなおし。
        if (item.dialogue == "dummy") {
          if (hasPropTimerTarget(item.prop)) {
            uint8_t timerId = getPropTimerTargetId(item.prop);
            resetTimer(timerId);
          }
          verbId = getPropID(item.prop);
          hasDummy = true;
          break;
        }
        objectList.push_back(item);
      }
    }
  } while (hasDummy);
}

// --- 「移動する」コンテナ(id == 04)のトグル表示 ---
// 移動先リストはdisplayListと同じように毎回、parent == 04のElementから
// 構成する。gScene・gUnlocksをはじめとする変数は一切編集しない。
// 2段目はobjectList/selectedObjectiveIndexをそのまま流用するため、
// 入力システムの別処理は新設しない。
void rebuildMoveDestinationList() {
  objectList.clear();
  for (const auto &item : containerB) {
    if (getPropParent(item.prop) == 0x04) {
      objectList.push_back(item);
    }
  }
}

// トグル前に、inputシステム(displayList / objectListと
// selectedIndex・選択状態)の値をsuspend用の配列へ退避し、
// 「移動する」コンテナの表示に切り替える。
void enterMoveMode() {
  // 「移動する」(id == 04)の要素がないシーンではトグルしない。
  Element marker;
  bool foundMarker = false;
  for (const auto &item : containerB) {
    if (getPropID(item.prop) == 0x04) {
      marker = item;
      foundMarker = true;
      break;
    }
  }
  if (!foundMarker) {
    return;
  }

  // 退避。復元時にそのまま戻すため、ここでは一切変更しない。
  gSuspendedDisplayList = displayList;
  gSuspendedObjectiveList = objectList;
  gSuspendedSelectedIndex = selectedIndex;
  gSuspendedSelectedObjectiveIndex = selectedObjectiveIndex;
  gSuspendedIsObjectiveSelected = isObjectiveSelected;
  gSuspendedNoContentState = noContentState;

  gMoveMode = true;

  // 「移動する」モード中は常にisObjectiveSelected == trueとする。
  // 1段目は見る・話す・行く(verb)とは別の欄として「移動する」(id == 04)
  // のみを表示し、selectedIndexは常にその要素を指す。
  selectedIndex = 0;
  selectedObjectiveIndex = 0;
  isObjectiveSelected = true;
  displayList.clear();
  displayList.push_back(marker);

  // 2段目: 移動先リストを毎回parent == 04のElementから構成する。
  rebuildMoveDestinationList();
}

// 「移動する」表示を解除し、退避していたinputシステムの値をそのまま
// 戻して、トグル前の表示をそのまま再表示する。
void exitMoveMode() {
  gMoveMode = false;
  displayList = gSuspendedDisplayList;
  objectList = gSuspendedObjectiveList;
  selectedIndex = gSuspendedSelectedIndex;
  selectedObjectiveIndex = gSuspendedSelectedObjectiveIndex;
  isObjectiveSelected = gSuspendedIsObjectiveSelected;
  noContentState = gSuspendedNoContentState;
}

void processContainerLifecycle() {
  rebuildContainerB();
  rebuildContainerC();
  isObjectiveSelected = false;

  // flavorContainerもcontainerB / containerCと同じタイミングで構成する。
  // 初回起動（SDL_AppInitからのこの呼び出し）でも構成されるため、最初の
  // root配信からflavorが候補になる。
  rebuildFlavorContainer();

  if (containerC.empty()) {
    noContentState = true;
    displayList.clear();
    objectList.clear();
  } else {
    noContentState = false;
    rebuildDisplayList();
    rebuildObjectiveList();
  }
}

void sendProperty() {
  // 「移動する」トグル表示中: 移動先リストはdisplayListと同じように
  // parent == 04のElementから毎回構成するが、送信は行わず、
  // gScene・gUnlocksをはじめとする変数は一切編集しない。
  if (gMoveMode) {
    rebuildMoveDestinationList();
    needRedraw = true;
    return;
  }

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
        gEyelevel = 0;
        rebuildContainerC();
      }
    }

    noContentState = false;
    // no contentからのroot復帰後なので、ここでflavorContainerを再構築する。
    rebuildFlavorContainer();
    rebuildDisplayList();
    rebuildObjectiveList();
    // if (gParent == gRoot) {
    //   advanceTimers();
    // }
    needRedraw = true;
    return;
  }

  if (displayList.empty() || selectedIndex >= displayList.size())
    return;

  Element selected;
  if (isObjectiveSelected) {
    if (objectList.empty()) {
      // no content を選択して送信する場合のダミー要素
      // (prop=0でルート遷移等の効果)
      selected = {"no content", 0ULL};
    } else {
      if (selectedObjectiveIndex >= objectList.size())
        return;
      selected = objectList[selectedObjectiveIndex];
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

  if (gimmick(prop)) {
    return;
  }

  unsigned id = getPropID(prop);
  unsigned parent = getPropParent(prop);
  unsigned child = getPropChild(prop);
  unsigned newRoot = getPropNewRoot(prop);
  unsigned newScene = getPropNewScene(prop);
  bool isOnetime = getPropIsOnetime(prop);
  unsigned unlocks0 = getPropSlot0Unlocks(prop);
  unsigned unlocks2 = getPropSlot2Unlocks(prop);
  unsigned downUnlocks1 = getPropSlot1DownUnlocks(prop);
  uint8_t toggleEyelevel = getPropToggleEyelevel(prop);
  bool hasTimerSetter = hasPropTimerSetter(prop);
  uint8_t timerSetterId = getPropTimerSetterId(prop);
  uint8_t timerSetterAction = getPropTimerSetterAction(prop);
  if (hasTimerSetter && timerSetterAction != TIMER_ACTION_NONE) {
    gPendingTimerActions[timerSetterId] = timerSetterAction;
  }

  //// Remember the condition that made the selected element sendable.  A
  //// successful timerTarget is one-shot and consumes its timer.
  // bool consumesTimerTarget = hasPropTimerTarget(prop) && evalCond5(prop);
  // uint8_t timerTargetId = getPropTimerTargetId(prop);

  // if (consumesTimerTarget) {
  //   resetTimer(timerTargetId);
  // }

  if (child) {
    for (const auto &item : containerB) {
      if (getPropID(item.prop) == child) {
        if (item.dialogue == "dummy") {
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

  // Even if not isOnetime, mark as hasRead but always stream.
  // if (isOnetime) {
  for (auto &item : containerA) {
    if (item.dialogue == selected.dialogue && item.prop == selected.prop) {
      item.prop |= (1ULL << 16); // hasRead (bit 16)
      break;
    }
  }
  //}

  if (newScene != 0) {
    gScene = newScene;
    gRoot = 0;
    gParent = 0;
    gUnlocks = 0;
    gEyelevel = 0;
    gTick = 0;
  } else {
    gUnlocks |= unlocks0 | unlocks2;
    // downUnlocks（slot1 index 3）: unlocks の後に適用するので、同じbit_up/down が
    // 競合した場合は down が勝つ（指定したbitのフラグだけを下げる）。
    gUnlocks &= ~downUnlocks1;

    if (toggleEyelevel != 0x0) {
      gEyelevel = toggleEyelevel;
    }

    if (newRoot != 0) {
      gRoot = newRoot;
      gUnlocks = 0x0; // リセットする。
      gEyelevel = 0;
      gTick = 0; // とりあえずリセットしておく。
    }
  }

  processContainerLifecycle();

  // 送信後の状態（newScene/newRootを含む）での再構築。flavorContainerは
  // processContainerLifecycle()内で既に構成されているが、送信後の状態での
  // 再構築を明示するためここで再度再構築する。root状態でのみflavorが
  // displayListを置き換える。
  rebuildFlavorContainer();
  rebuildDisplayList();
  rebuildObjectiveList();

  // A timer counts completed dialogue cycles, not individual selections in a
  // verb/object chain.  Advance only after returning to the current root.
  // flavor（parent == kFlavorParent）の消費はverb・objectを経由しないため、
  // tickを消費しない。
  if (noContentState &&
      parent != gRoot &&
      parent != kFlavorParent) { // progress timer when verb to quote loop completed.
    advanceTimers();
  }
  needRedraw = true;
}


