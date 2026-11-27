#pragma once
// ゲームロジック: 配信条件・タイマー・gimmick・コンテナ再構築・move mode・lifecycle・sendProperty。
#include "state.h"

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
  ++gTick;
}

void rebuildContainerB();
void rebuildContainerC();
void rebuildFlavorContainer();
void rebuildDisplayList();
void rebuildObjectiveList();
void rebuildMoveDestinationList();
void enterMoveMode();
void exitMoveMode();

bool gTickTimer();
bool gimmick(uint64_t prop);
bool hasValidObjective(unsigned verbId);
void processContainerLifecycle();
void sendProperty();
