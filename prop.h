#pragma once

#include <cstdint>
#include <string>

struct Element {
  std::string dialogue;
  uint64_t prop;
};
/*
prop:
  bits 0-7   id
  bits 8-15  parent
  bit 16     hasRead
  bit 17     isQuote
  bit 18     isOnetime
  bit 19     vacant
  bits 20-23 scene
  bits 24-25 slot0Index
  bits 26-27 slot1Index
  bits 28-29 slot2Index
  bits 30-31 slot3Index
  bits 32-39 slot0 value
  bits 40-47 slot1 value
  bits 48-55 slot2 value
  bits 56-63 slot3 value

slot0Index:
  0: requiredReadId
  1: unlocks

slot1Index:
  0: child
  1: excludedReadId
  2: newRoot
  3: downUnlocks

slot2Index:
  0: timerSetter
  1: unlocks
  2: requiredUnlockFlags
  3: excludedReadId

slot3Index:
  0: timerTarget
  1: newScene
  2: excludedUnlockFlags
  3: eyelevelToggle

timerTarget slot value:
  bits 0-1 operator
  bits 2-3 timerId
  bits 4-7 count

timerSetter slot value:
  bits 0-1 timerId
  bits 2-3 vacant
  bits 4-5 action

eyelevelToggle slot value:
  bits 0-3 eyelevel
  bits 4-7 toggleEyelevel

downUnlocks slot value（slot1 index 3）:
  bits 0-7 mask（送信時に gUnlocks &= ~mask で指定bitだけを下げる）




'dialogue', '0xHHHH|HHHH|HHHHHHHH',
   data.txt の書き出しは prop を 2byte|2byte|4byte に区切り所定の位置に '|' を挿す。
   読み込み時は prop 内の '|' をすべて無視する（'|' なしの旧書式も読める）。
* */



// --- ヘルパー関数: プロパティのフィールド抽出 (new prop bit assignment準拠)
// ---
inline uint8_t getPropID(uint64_t prop) { return prop & 0xFF; }
inline uint8_t getPropParent(uint64_t prop) { return (prop >> 8) & 0xFF; }
inline bool getPropHasRead(uint64_t prop) { return (prop >> 16) & 0x1; }
inline bool getPropIsQuote(uint64_t prop) { return (prop >> 17) & 0x1; }
inline bool getPropIsOnetime(uint64_t prop) { return (prop >> 18) & 0x1; }
inline uint8_t getPropScene(uint64_t prop) { return (prop >> 20) & 0xF; }

// Slot 0: bits 32-39, index: bits 24-25
// (0: requiredReadId, 1: unlocks)
// The element with this ID in the current scene must already be read.
inline uint8_t getPropRequiredReadId(uint64_t prop) {
  return (((prop >> 24) & 0x3) == 0) ? ((prop >> 32) & 0xFF) : 0;
}
inline uint8_t getPropSlot0Unlocks(uint64_t prop) {
  return (((prop >> 24) & 0x3) == 1) ? ((prop >> 32) & 0xFF) : 0;
}

// Slot 1: bits 40-47, index: bits 26-27
// (0: child, 1: excludedReadId, 2: newRoot, 3: downUnlocks)
inline uint8_t getPropChild(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 0) ? ((prop >> 40) & 0xFF) : 0;
}
// The element with this ID in the current scene must not be read.
inline uint8_t getPropSlot1ExcludedReadId(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 1) ? ((prop >> 40) & 0xFF) : 0;
}
inline uint8_t getPropExcludedReadId1(uint64_t prop) {
  return getPropSlot1ExcludedReadId(prop);
}
inline uint8_t getPropExcludedReadId(uint64_t prop) {
  return getPropExcludedReadId1(prop);
}
inline uint8_t getPropNewRoot(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 2) ? ((prop >> 40) & 0xFF) : 0;
}
// downUnlocks mask（slot1 index 3・value bits 0-7）。送信時に gUnlocks &= ~mask で
// 指定したbitのフラグだけを下げる。value 0 は none（どのbitも下げない）。
inline uint8_t getPropSlot1DownUnlocks(uint64_t prop) {
  return (((prop >> 26) & 0x3) == 3) ? ((prop >> 40) & 0xFF) : 0;
}
inline uint8_t getPropDownUnlocks(uint64_t prop) {
  return getPropSlot1DownUnlocks(prop);
}

// Slot 2: bits 48-55, index: bits 28-29
// (0: timerSetter, 1: unlocks, 2: requiredUnlockFlags, 3: excludedReadId)
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
inline uint8_t getPropSlot2Unlocks(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 1) ? ((prop >> 48) & 0xFF) : 0;
}
inline uint8_t getPropUnlocks(uint64_t prop) {
  return getPropSlot0Unlocks(prop) | getPropSlot2Unlocks(prop);
}
inline uint8_t getPropRequiredUnlockFlags(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 2) ? ((prop >> 48) & 0xFF) : 0;
}
inline uint8_t getPropSlot2ExcludedReadId(uint64_t prop) {
  return (((prop >> 28) & 0x3) == 3) ? ((prop >> 48) & 0xFF) : 0;
}
inline uint8_t getPropExcludedReadId2(uint64_t prop) {
  return getPropSlot2ExcludedReadId(prop);
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
inline uint8_t getPropEyelevel(uint64_t prop) {
  return (((prop >> 30) & 0x3) == 3) ? ((prop >> 56) & 0x0F) : 0;
}
inline uint8_t getPropToggleEyelevel(uint64_t prop) {
  return (((prop >> 30) & 0x3) == 3) ? ((prop >> 60) & 0x0F) : 0;
}

inline uint8_t getSlotIndex(uint64_t prop, uint8_t slot) {
  return (prop >> (24 + slot * 2)) & 0x3;
}
inline uint8_t getSlotValue(uint64_t prop, uint8_t slot) {
  return (prop >> (32 + slot * 8)) & 0xFF;
}

// ============================================================================
// --- Prompt 5: read helpers と対になる書き込みヘルパー（setProp / setSlot） ---
// ============================================================================
// 現行 read の逆 bit 配置に厳密に一致させる（Prompt 5 要件 1）:
//   timerSetter value = timerId(bits0-1) | action(bits4-5<<4)（vacant bits2-3 は保持する）
//   timerTarget  value = operator(bits0-1) | timerId(bits2-3<<2) | count(bits4-7<<4)
//   eyelevelToggle value = eyelevel(bits0-3) | toggleEyelevel(bits4-7<<4)
//   flags/unlocks/downUnlocks/required/excluded = bits0-7 の mask、
//   newScene/child/excludedReadId/requiredReadId = 素の 8bit value。
inline uint64_t setPropBit(uint64_t prop, int bit, bool v) {
  return v ? (prop | (1ULL << bit)) : (prop & ~(1ULL << bit));
}
inline uint64_t clearPropBits(uint64_t prop, uint64_t mask) { return prop & ~mask; }
inline uint64_t setPropField(uint64_t prop, int shift, int width, uint64_t v) {
  const uint64_t widthMask = (1ULL << width) - 1ULL;
  return (prop & ~(widthMask << shift)) | ((v & widthMask) << shift);
}
inline uint64_t setSlotIndex(uint64_t prop, uint8_t slot, uint8_t idx) {
  return setPropField(prop, 24 + static_cast<int>(slot) * 2, 2, idx & 0x3u);
}
inline uint64_t setSlotValue(uint64_t prop, uint8_t slot, uint8_t v) {
  return setPropField(prop, 32 + static_cast<int>(slot) * 8, 8, v);
}

// slot value byte の部分フィールドだけ編集する（他 sub-bit・vacant bit は保持する）。
// 合成は read helpers の分解と一致する（getPropTimerSetterId = value&0x3, action = value>>4）。
inline uint64_t editTimerSetterValue(uint64_t cur, uint8_t timerId, uint8_t action) {
  uint64_t v = setPropField(cur, 0, 2, timerId);
  v = setPropField(v, 4, 2, action);
  return v;
}
inline uint64_t editTimerTargetValue(uint64_t cur, uint8_t op, uint8_t timerId, uint8_t count) {
  uint64_t v = setPropField(cur, 0, 2, op);
  v = setPropField(v, 2, 2, timerId);
  v = setPropField(v, 4, 4, count);
  return v;
}
inline uint64_t editEyelevelToggleValue(uint64_t cur, uint8_t eyelevel, uint8_t toggle) {
  uint64_t v = setPropField(cur, 0, 4, eyelevel);
  v = setPropField(v, 4, 4, toggle);
  return v;
}

uint64_t buildProperty(unsigned id, unsigned parent, unsigned scene,
                       bool isQuote, bool isOnetime);
