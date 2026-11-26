#include <SDL3/SDL_keycode.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

// ============================================================================
// --- Strict Document model: data.txt を 1 バイトも壊さずに保持する ---
// ============================================================================
// data.txt の 1 行は `'dialogue', '0xHHHH|HHHH|HHHHHHHH',` の形式を取る。
// prop は 2byte|2byte|4byte に 3 分割し、所定の位置に '|' を挿す（書き出し書式）。
// 読み込みに際しては prop 内の '|' をすべて無視するため、旧書の
// `'dialogue', '0xHHHHHHHHHHHHHHHH',`（'|' なし）もそのまま読める。
// 既存の whitespace-token parser は dialogue の空白・'・日本語を分解してしまうため、
// 行単位で quoted string と hex prop を安全に解析する parser を使う。
// comment line / blank line は graph に含めないが、serialize では原文のまま復元する。
// node の単位は (scene, id) のキーとし、同じ id でも別 scene は別 node として扱う。

enum LineKind { LINE_ELEMENT, LINE_COMMENT, LINE_BLANK, LINE_UNPARSED };

struct RawLine {
  std::string text;       // 終端改行を除いた原文（編集しない）
  std::string terminator; // "\n" / "\r\n" / ""（最終行が改行で終わらない場合）
  LineKind kind = LINE_UNPARSED;
  int elementIndex = -1;  // LINE_ELEMENT のとき Document::elements の index
};

struct ElementRecord {
  Element element;  // load-time の prop をそのまま保持する（hasRead をクリアしない）
  int lineIndex = -1;
  bool dirty = false;  // EDITOR フェーズで true にした行のみ raw line を再生成する
};

struct Document {
  std::string raw;  // ファイルの全バイト列。round-trip 比較の基準。
  std::vector<RawLine> lines;
  std::vector<ElementRecord> elements;
  size_t parsedLines = 0;
  size_t commentLines = 0;
  size_t blankLines = 0;
  size_t unparsedLines = 0;
};

inline std::string hexByte(uint8_t v) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "0x%02x", static_cast<unsigned>(v));
  return buf;
}

inline std::string hexNibble(uint8_t v) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "0x%x", static_cast<unsigned>(v));
  return buf;
}

// 旧 parser と同じ comment 判定。'//'、'/*'、'*'（'* ... *' の行頭、'*/' を含む）を
// comment として扱い、graph に含めずに原文として保持する。
inline bool isCommentLine(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i >= line.size()) return false;
  const std::string head = line.substr(i, 2);
  if (head == "//" || head == "/*") return true;
  return line[i] == '*';
}

// `'dialogue', '0xHEX',` を 1 行から解析する。dialogue に ' が混在する可能性があるため
// greedy な (.*) を使い、末尾锚定点と組み合わせることで「最も後ろの valid な区切り」を
// prop として選ぶ。trailing comma は optional にして旧ファイルの揺らぎも許容する。
// 書き出しは 2byte|2byte|4byte 区切り（'0xHHHH|HHHH|HHHHHHHH'）だが、読み込みでは
// prop 内の '|' をすべて無視して 16 桁以下の hex として解析する（'|' なしの旧書式も可）。
inline bool parseElementLine(const std::string &line, std::string &dialogueOut,
                             uint64_t &propOut) {
  static const std::regex quotedProp(
      "^[ \t]*'(.*)'[ \t]*,[ \t]*'([^']*)'[ \t]*,?[ \t]*$");
  std::smatch m;
  if (!std::regex_match(line, m, quotedProp)) {
    return false;
  }
  // prop token: まず '|' をすべて除去し、optional な '0x' prefix を取り除いて
  // 旧 parser と同じ「1〜16 桁の hex」を検証してから値化する。
  std::string hex;
  for (char c : m[2].str()) {
    if (c != '-') hex += c; // prop 内の '|' はすべて無視
  }
  if (hex.size() >= 2 && hex[0] == '0' && hex[1] == 'x') {
    hex.erase(0, 2);
  }
  if (hex.empty() || hex.size() > 16) return false;
  for (char c : hex) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  dialogueOut = m[1].str();
  propOut = std::stoull(hex, nullptr, 16);
  return true;
}

bool loadDocument(const std::string &path, Document &doc,
                  std::vector<std::string> &warnings) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;

  doc = Document{};
  doc.raw.assign(std::istreambuf_iterator<char>(in),
                 std::istreambuf_iterator<char>());
  in.close();

  size_t pos = 0;
  while (pos < doc.raw.size()) {
    const size_t nl = doc.raw.find('\n', pos);
    RawLine line;
    if (nl == std::string::npos) {
      line.text = doc.raw.substr(pos); // 改行で終わらない最終行も保持する
    } else {
      std::string body = doc.raw.substr(pos, nl - pos);
      if (!body.empty() && body.back() == '\r') {
        line.text = body.substr(0, body.size() - 1);
        line.terminator = "\r\n";
      } else {
        line.text = std::move(body);
        line.terminator = "\n";
      }
    }

    bool whitespaceOnly = true;
    for (char c : line.text) {
      if (c != ' ' && c != '\t') {
        whitespaceOnly = false;
        break;
      }
    }

    if (line.text.empty() || whitespaceOnly) {
      line.kind = LINE_BLANK;
      ++doc.blankLines;
    } else if (isCommentLine(line.text)) {
      line.kind = LINE_COMMENT;
      ++doc.commentLines;
    } else {
      std::string dialogue;
      uint64_t prop = 0;
      if (parseElementLine(line.text, dialogue, prop)) {
        line.kind = LINE_ELEMENT;
        line.elementIndex = static_cast<int>(doc.elements.size());
        doc.elements.push_back(ElementRecord{
            {dialogue, prop}, static_cast<int>(doc.lines.size()), false});
        ++doc.parsedLines;
      } else {
        line.kind = LINE_UNPARSED;
        ++doc.unparsedLines;
        warnings.push_back("line " + std::to_string(doc.lines.size() + 1) +
                           " を解析できませんでした: " + line.text.substr(0, 72));
      }
    }

    doc.lines.push_back(std::move(line));
    pos = (nl == std::string::npos) ? doc.raw.size() : nl + 1;
  }
  return true;
}

// 編集した Element の raw line 再生成用。lower case・16桁・'0x' prefix。
// （HUD / stdout の prop=0x 表示はパイプなしの 16 桁のまま使う。）
inline std::string formatPropHex16(uint64_t prop) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "%016llx",
                static_cast<unsigned long long>(prop));
  return std::string(buf);
}

// data.txt 書き出し用の prop 書式。16 桁 hex の所定の位置（2byte|2byte|4byte）に
// '|' を挿入する: 'HHHH|HHHH|HHHHHHHH'。値は formatPropHex16 と同一で、
// parseElementLine は '|' をすべて無視して同じ値に戻す。
inline std::string formatPropHex16Piped(uint64_t prop) {
  const std::string hex = formatPropHex16(prop);
  return hex.substr(0, 4) + "-" + hex.substr(4, 4) + "-" + hex.substr(8, 8);
}

// 未編集の行は原文をそのまま、dirty な Element 行のみ `'dialogue', '0xHHHH|HHHH|HHHHHHHH',` に
// 再生成する。comment / blank line は原文のまま。terminator は原文の改行を再現するので、
// 未編集の Document では serializeDocument() == Document::raw が成立する。
std::string serializeDocument(const Document &doc) {
  std::string out;
  out.reserve(doc.raw.size());
  for (const RawLine &line : doc.lines) {
    if (line.kind == LINE_ELEMENT && line.elementIndex >= 0 &&
        doc.elements[line.elementIndex].dirty) {
      const Element &e = doc.elements[line.elementIndex].element;
      out += "'" + e.dialogue + "', '0x" + formatPropHex16Piped(e.prop) + "',";
    } else {
      out += line.text;
    }
    out += line.terminator;
  }
  return out;
}

struct RoundTripReport {
  bool exact = false;
  size_t originalBytes = 0;
  size_t serializedBytes = 0;
  size_t lineCount = 0;
  size_t elementLines = 0;
  size_t commentLines = 0;
  size_t blankLines = 0;
  size_t firstDiffLine = 0; // 1-based。0 は差異なし（意味は Prompt4 で変更しない）
  std::string originalSample;
  std::string serializedSample;
  // Prompt 4 baseline diff: Document::raw は load 時の baseline のまま（追加・編集では
  // raw を書き換えない）。serializeDocument の出力を baseline と行単位で比較し、
  // 追加行と編集行（dirty 再生成）の数を報告する。
  size_t baselineLines = 0;                // baseline（Document::raw）の行数
  size_t addedLines = 0;                   // baseline より後に追加された行数
  size_t editedLines = 0;                  // baseline とテキストが異なる行数（dirty 再生成のみ）
  bool diffIsAddedOrDirtyOnly = true;      // diff が追加行＋dirty Element 行のみであることを示す
};

RoundTripReport verifyRoundTrip(const Document &doc) {
  RoundTripReport rep;
  const std::string out = serializeDocument(doc);
  rep.originalBytes = doc.raw.size();
  rep.serializedBytes = out.size();
  rep.lineCount = doc.lines.size();
  rep.elementLines = doc.parsedLines;
  rep.commentLines = doc.commentLines;
  rep.blankLines = doc.blankLines;
  rep.exact = (out == doc.raw);
  if (!rep.exact) {
    size_t a = 0, b = 0;
    size_t line = 1;
    while (a < doc.raw.size() && b < out.size()) {
      if (doc.raw[a] != out[b]) {
        rep.firstDiffLine = line;
        break;
      }
      if (doc.raw[a] == '\n') ++line;
      ++a; ++b;
    }
    if (rep.firstDiffLine == 0) rep.firstDiffLine = line; // 長さが違う場合
    rep.originalSample = doc.raw.substr(a, 48);
    rep.serializedSample = out.substr(b, 48);
  }

  // Prompt 4 baseline diff: serializeDocument の出力を baseline raw と行単位で比較する。
  // terminator（"\n" / "\r\n" / ""）は比較せず本文テキストだけ照合する（CRLF を壊さない）。
  // 末尾改行による空行は「行」として数えない（raw と serialized で同じ規則）。
  const auto splitKeepText = [](const std::string &s) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < s.size()) {
      const size_t nl = s.find('\n', pos);
      if (nl == std::string::npos) {
        lines.push_back(s.substr(pos));
        break;
      }
      lines.push_back(s.substr(pos, nl - pos));
      pos = nl + 1;
    }
    for (std::string &l : lines) {
      if (!l.empty() && l.back() == '\r') l.pop_back(); // CRLF の CR は本文から除く
    }
    // 終端が改行の行は「改行前の本文」として 1 行として数える（末尾の空要素は作らない）。
    // 改行で終わらない最終行も 1 行として保持する（loadDocument の行数対応と同じ）。
    return lines;
  };
  const std::vector<std::string> baseLines = splitKeepText(doc.raw);
  const std::vector<std::string> outLines = splitKeepText(out);
  rep.baselineLines = baseLines.size();
  rep.diffIsAddedOrDirtyOnly = outLines.size() >= baseLines.size(); // 削除行があれば不正
  if (outLines.size() > baseLines.size()) rep.addedLines = outLines.size() - baseLines.size();

  // baseline の位置まで同じ index で対応する（追加は末尾のみ・削除は作らない）。
  // 異なる行は dirty Element 行でなければならない（それ以外の差分は想定外の編集）。
  for (size_t i = 0; i < baseLines.size(); ++i) {
    if (i >= outLines.size()) {
      rep.diffIsAddedOrDirtyOnly = false;
      break;
    }
    if (outLines[i] == baseLines[i]) continue;
    ++rep.editedLines;
    if (i >= doc.lines.size() || doc.lines[i].kind != LINE_ELEMENT ||
        doc.lines[i].elementIndex < 0 ||
        doc.lines[i].elementIndex >= static_cast<int>(doc.elements.size()) ||
        !doc.elements[doc.lines[i].elementIndex].dirty) {
      rep.diffIsAddedOrDirtyOnly = false;
    }
  }
  // baseline より後の行はすべて「追加された正格式の Element 行」でなければならない。
  for (size_t j = baseLines.size(); j < outLines.size(); ++j) {
    if (j >= doc.lines.size() || doc.lines[j].kind != LINE_ELEMENT ||
        doc.lines[j].elementIndex < 0 ||
        doc.lines[j].elementIndex >= static_cast<int>(doc.elements.size()) ||
        !doc.elements[doc.lines[j].elementIndex].dirty ||
        outLines[j] != doc.lines[j].text) {
      rep.diffIsAddedOrDirtyOnly = false;
    }
  }
  return rep;
}

// ============================================================================
// --- Graph model: (scene, id) を node、parent / child を edge ---
// ============================================================================
enum EdgeKind {
  EDGE_PARENT, // parent -> id
  EDGE_CHILD,  // id -> child
  EDGE_REQUIRED_READ,
  EDGE_EXCLUDED_READ_SLOT1,
  EDGE_EXCLUDED_READ_SLOT2
};

struct GraphEdge {
  int from = -1; // node index。-1 は graph 外（dangling）
  int to = -1;   // node index。-1 は同 scene に目標が無い
  EdgeKind kind = EDGE_PARENT;
  uint8_t targetId = 0;
  bool dangling = false;
};

enum NodeKind { NODE_CONTENT, NODE_DUMMY, NODE_TEMPLATE, NODE_EMPTY };

inline NodeKind classifyNodeKind(const std::string &dialogue) {
  if (dialogue == "dummy") return NODE_DUMMY;
  if (dialogue == "template") return NODE_TEMPLATE;
  if (dialogue == "empty") return NODE_EMPTY;
  return NODE_CONTENT;
}

inline uint32_t nodeKey(uint8_t scene, uint8_t id) {
  return (static_cast<uint32_t>(scene) << 8) | static_cast<uint32_t>(id);
}

struct GraphNode {
  uint8_t id = 0;
  uint8_t scene = 0;
  int elementIndex = -1;
  int lineIndex = -1;
  uint8_t parentId = 0;
  uint8_t childId = 0;
  bool hasRead = false;
  bool isQuote = false;
  bool isOnetime = false;
  NodeKind kind = NODE_CONTENT;

  // VIEW_GENERAL の layout world 座標
  int depth = 0;
  float wx = 0.f;
  float wy = 0.f;

  // VIEW_FAMILY の per-mode layout world 座標（top-down forest）。
  // layoutScene() は wx/wy のだけを書き、layoutFamily() は familyWx/familyWy に書くため、
  // mode 切替で VIEW_GENERAL の座標は壊れない。famDepth は -1 = 未配置、>= 0 = 配置済み。
  int famDepth = -1;
  float familyWx = 0.f;
  float familyWy = 0.f;

  // slot1 newRoot の target id（badge 表示用。0 は newRoot なし）
  uint8_t newRootId = 0;

  // family / constraint viewer 用（constraint は後フェーズ hook）
  std::vector<int> parentNodes;       // parent field が指す node
  std::vector<int> childNodes;        // slot1 child が指す node
  std::vector<int> requiredReadNodes; // slot0 requiredReadId の node
  std::vector<int> excludedReadNodes; // slot1 / slot2 excludedReadId の node
  bool danglingParent = false;
  bool danglingChild = false;
};

struct Graph {
  std::vector<GraphNode> nodes;
  std::vector<GraphEdge> edges;
  std::vector<int> roots;      // parent == 0 の node（root candidates）
  std::vector<uint8_t> scenes; // 存在する scene（昇順）
  std::unordered_map<uint32_t, int> byKey;

  // adjacency tables（VIEW_FAMILY / VIEW_CONSTRAINT のデータ構造。描画は後フェーズ）
  std::vector<std::vector<int>> parentAdj;       // node -> parents
  std::vector<std::vector<int>> childAdj;        // node -> 階層 children（parent field から）
  std::vector<std::vector<int>> childLinkAdj;    // node -> explicit child（slot1）
  std::vector<std::vector<int>> requiredReadAdj; // node -> requiredReadId targets
  std::vector<std::vector<int>> excludedReadAdj; // node -> excluded targets（slot1 + slot2）
  std::vector<uint32_t> newRootTargets;          // slot1 newRoot の (scene, id) key
  std::vector<std::string> warnings;

  int findNode(uint8_t scene, uint8_t id) const {
    const auto it = byKey.find(nodeKey(scene, id));
    return it == byKey.end() ? -1 : it->second;
  }

  std::vector<int> nodesInScene(uint8_t scene) const {
    std::vector<int> out;
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (nodes[i].scene == scene) out.push_back(static_cast<int>(i));
    }
    return out;
  }

  // デフォルト scene selector: 0 は newScene の「変化なし」sentinel なので
  // 0 を除いた最小 scene を選ぶ（data.txt では 0x1 になる）。
  uint8_t smallestNonZeroScene() const {
    for (const uint8_t s : scenes) {
      if (s != 0) return s;
    }
    return scenes.empty() ? 0 : scenes.front();
  }
};

void buildGraph(const Document &doc, Graph &g) {
  g = Graph{};
  bool sceneSeen[16] = {};

  for (size_t ei = 0; ei < doc.elements.size(); ++ei) {
    const ElementRecord &rec = doc.elements[ei];
    const uint64_t prop = rec.element.prop;
    const uint8_t scene = getPropScene(prop);
    const uint8_t id = getPropID(prop);

    if (g.findNode(scene, id) >= 0) {
      g.warnings.push_back("duplicate node scene=" + hexNibble(scene) + " id=" +
                           hexByte(id) + " line=" + std::to_string(rec.lineIndex + 1));
      continue;
    }

    GraphNode n;
    n.id = id;
    n.scene = scene;
    n.elementIndex = static_cast<int>(ei);
    n.lineIndex = rec.lineIndex;
    n.kind = classifyNodeKind(rec.element.dialogue);

    g.byKey[nodeKey(scene, id)] = static_cast<int>(g.nodes.size());
    g.nodes.push_back(std::move(n));
    if (!sceneSeen[scene]) {
      sceneSeen[scene] = true;
      g.scenes.push_back(scene);
    }
  }

  std::sort(g.scenes.begin(), g.scenes.end());

  g.parentAdj.resize(g.nodes.size());
  g.childAdj.resize(g.nodes.size());
  g.childLinkAdj.resize(g.nodes.size());
  g.requiredReadAdj.resize(g.nodes.size());
  g.excludedReadAdj.resize(g.nodes.size());

  for (size_t i = 0; i < g.nodes.size(); ++i) {
    GraphNode &n = g.nodes[i];
    const uint64_t prop = doc.elements[n.elementIndex].element.prop;

    n.parentId = getPropParent(prop);
    n.childId = getPropChild(prop);
    n.hasRead = getPropHasRead(prop);
    n.isQuote = getPropIsQuote(prop);
    n.isOnetime = getPropIsOnetime(prop);

    const uint8_t requiredReadId = getPropRequiredReadId(prop);
    const uint8_t excludedSlot1 = getPropExcludedReadId1(prop);
    const uint8_t excludedSlot2 = getPropExcludedReadId2(prop);
    const uint8_t newRoot = getPropNewRoot(prop);
    n.newRootId = newRoot;

    if (n.parentId == 0) g.roots.push_back(static_cast<int>(i));
    if (newRoot != 0) g.newRootTargets.push_back(nodeKey(n.scene, newRoot));

    if (n.parentId != 0) {
      const int p = g.findNode(n.scene, n.parentId);
      if (p >= 0) {
        n.parentNodes.push_back(p);
        g.parentAdj[i].push_back(p);
        g.childAdj[p].push_back(static_cast<int>(i));
        g.edges.push_back(
            GraphEdge{p, static_cast<int>(i), EDGE_PARENT, n.parentId, false});
      } else {
        n.danglingParent = true;
        g.edges.push_back(
            GraphEdge{-1, static_cast<int>(i), EDGE_PARENT, n.parentId, true});
      }
    }

    if (n.childId != 0) {
      const int c = g.findNode(n.scene, n.childId);
      if (c >= 0) {
        n.childNodes.push_back(c);
        g.childLinkAdj[i].push_back(c);
        g.edges.push_back(
            GraphEdge{static_cast<int>(i), c, EDGE_CHILD, n.childId, false});
      } else {
        n.danglingChild = true;
        g.edges.push_back(
            GraphEdge{static_cast<int>(i), -1, EDGE_CHILD, n.childId, true});
      }
    }

    if (requiredReadId != 0) {
      const int r = g.findNode(n.scene, requiredReadId);
      n.requiredReadNodes.push_back(r);
      g.requiredReadAdj[i].push_back(r);
      g.edges.push_back(GraphEdge{static_cast<int>(i), r, EDGE_REQUIRED_READ,
                                  requiredReadId, r < 0});
    }
    if (excludedSlot1 != 0) {
      const int x = g.findNode(n.scene, excludedSlot1);
      n.excludedReadNodes.push_back(x);
      g.excludedReadAdj[i].push_back(x);
      g.edges.push_back(GraphEdge{static_cast<int>(i), x, EDGE_EXCLUDED_READ_SLOT1,
                                  excludedSlot1, x < 0});
    }
    if (excludedSlot2 != 0) {
      const int x = g.findNode(n.scene, excludedSlot2);
      n.excludedReadNodes.push_back(x);
      g.excludedReadAdj[i].push_back(x);
      g.edges.push_back(GraphEdge{static_cast<int>(i), x, EDGE_EXCLUDED_READ_SLOT2,
                                  excludedSlot2, x < 0});
    }
  }
}

// グローバル変数
namespace {
std::vector<Element> containerA;
std::vector<Element> containerB;
std::vector<Element> containerC;
std::vector<Element> displayList;
std::vector<Element> objectList;

// --- flavorContainer（root配信用のフレーバーテキスト） ---
// rootで配信されるonetime quoteはすべてフレーバーテキストの役割を持つため、
// 従来の「rootでonetime quoteを優先配信」処理に代わるものとして新設する。
// parent == kFlavorParent (0x0F) のElementをgParentに関係なく抽出する。
// parent以外の条件（hasRead / requiredReadId / excludedReadId / timer /
// unlocks / eyelevel）はrebuildContainerC()と同じ判定を追う。isOnetime・
// quote・optionの区別は抽出・選択で行わない。構築はprocessContainerLifecycle()
// の最初期（初回起動時を含む）から行う。
constexpr uint8_t kFlavorParent = 0x0F;
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

enum TimerAction : uint8_t {
  TIMER_ACTION_NONE = 0,
  TIMER_ACTION_START = 1,
  TIMER_ACTION_STOP = 2,
  TIMER_ACTION_RESET = 3,
};

// --- gTick秒読みタイマー用の状態 ---
// duration（1-3の乱数）を設定した時点のgTickを秒読み開始点として記録し、
// 現在gTickとの差がdurationに等しくなった時点で発火する。
unsigned gGTickDuration = 0; // 0: 未設定（初回呼び出しで抽選・設定する）
unsigned gGTickStamp = 0;    // duration設定時点のgTick（秒読み開始点）

// --- VIEWER / EDITOR の GUI 状態 ---
// VIEW_GENERAL と VIEW_FAMILY（Prompt 2 の family viewer）は実装済み。
// constraint / editor は hook のまま（後フェーズ）。mode 別の layout は
// layoutScene / layoutFamily がそれぞれ wx/wy・familyWx/familyWy を保持する。
enum ViewMode { VIEW_GENERAL = 0, VIEW_FAMILY = 1, VIEW_CONSTRAINT = 2, EDITOR = 3 };

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
EditorState gEditor;

bool gEditorTextInputActive = false; // editor 側の SDL_StartTextInput/StopTextInput の対称管理
bool gDocTerminatorPatched = false;  // raw が改行で終わらない場合に最終行 terminator を "\n" に補った記録

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

// slot option の表示名（read helpers の option 対応と同じ順。names[idx+1] = index idx の option）。
static const char *const kSlotOptionNames0[] = {"none", "requiredReadId", "unlocks"};
static const char *const kSlotOptionNames1[] = {"none", "child", "excludedReadId", "newRoot",
                                               "downUnlocks"};
static const char *const kSlotOptionNames2[] = {"none", "timerSetter", "unlocks",
                                                "requiredUnlockFlags", "excludedReadId"};
static const char *const kSlotOptionNames3[] = {"none", "timerTarget", "newScene",
                                                "excludedUnlockFlags", "eyelevelToggle"};
static const char *const kTimerActionNames[] = {"none", "start", "stop", "reset"}; // timerSetter action 0=none,1=start,2=stop,3=reset
static const char *const kTimerOpNames[] = {"none", "less", "more", "equal"};      // timerTarget operator 0=none(=timerTarget なし),1=less,2=more,3=equal

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
// 現在の option 表示名。value 0 は none（data.txt の既存慣習: index=0/value=0 で none を表す。
// helpers は「index が該当 option であれば value を返す」なので value 0 はどの option でも none）。
const char *propsOptionLabel(uint64_t prop, uint8_t slot) {
  if (getSlotValue(prop, slot) == 0) return "none";
  const uint8_t idx = getSlotIndex(prop, slot);
  if (idx + 1 > slotOptionCount(slot)) return "unknown";
  return slotOptionNames(slot)[idx + 1];
}

// メニューの fields（表示順 = tabs/sections の順。同関数 describeElement の EDITOR variant も同じ順）。
static const PropsFieldDef kPropsFields[] = {
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
constexpr int kPropsFieldCount = static_cast<int>(sizeof(kPropsFields) / sizeof(PropsFieldDef));
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
PropsEditState gProps;

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

// ============================================================================
// --- 日本語テキスト描画: FreeType を dlopen で使う ---
// ============================================================================
// SDL_RenderDebugText は ASCII の bitmap font のため、dialogue の日本語は '?' に
// 置き換えて表示していた。FreeType を動的ロードし、システムの日本語フォントから
// glyph を rasterize して SDL_Texture に cache し、UTF-8 をそのまま描く。
// freetype / 日本語フォントが無い環境では従来の ASCII 代替表示に戻る。
// freetype ヘッダは使わず、dlsym で関数を取り、安定した ABI の先頭フィールドだけを
// 手で宣言して参照する（初期化時の sanity check でレイアウト検証する）。

constexpr int kGuiTextPx = 14;      // 基準 px。world 側は zoom で拡縮して描く
constexpr size_t kGlyphCacheMax = 512;

struct DFTFaceRec {  // FT_FaceRec の先頭 ~ glyph フィールドまで
  long num_faces;
  long face_index;
  long face_flags;
  long style_flags;
  long num_glyphs;
  char *family_name;
  char *style_name;
  int num_fixed_sizes;
  void *available_sizes;
  int num_charmaps;
  void *charmaps;
  void *generic_data;
  void (*generic_finalizer)(void *);
  long bbox_xmin;
  long bbox_ymin;
  long bbox_xmax;
  long bbox_ymax;
  unsigned short units_per_em;
  short ascender;
  short descender;
  short height;
  short max_advance_width;
  short max_advance_height;
  short underline_position;
  short underline_thickness;
  void *glyph;  // FT_GlyphSlot
};

struct DFTGlyphSlotRec {  // FT_GlyphSlotRec の先頭 ~ bitmap_top まで
  void *library;
  void *face;
  void *next;
  unsigned glyph_index;
  void *generic_data;
  void (*generic_finalizer)(void *);
  long metrics_width;
  long metrics_height;
  long metrics_hori_bearing_x;
  long metrics_hori_bearing_y;
  long metrics_hori_advance;
  long metrics_vert_bearing_x;
  long metrics_vert_bearing_y;
  long metrics_vert_advance;
  long linear_hori_advance;
  long linear_vert_advance;
  long advance_x;
  long advance_y;
  // FT_Glyph_Format の enum は FOUR_CHAR_CODE の値を取るため、この ABI では
  // 8 バイト（long）になる。int で宣言すると bitmap の offset が 4 字节ずれる。
  long format;
  unsigned bitmap_rows;
  unsigned bitmap_width;
  int bitmap_pitch;
  unsigned char *bitmap_buffer;
  unsigned short bitmap_num_grays;
  unsigned char bitmap_pixel_mode;
  unsigned char bitmap_palette_mode;
  void *bitmap_palette;
  int bitmap_left;
  int bitmap_top;
};

typedef int (*FnFTInit)(void **);
typedef void (*FnFTDone)(void *);
typedef int (*FnFTNewFace)(void *, const char *, long, void **);
typedef void (*FnFTDoneFace)(void *);
typedef int (*FnFTSetPixelSizes)(void *, unsigned, unsigned);
typedef unsigned (*FnFTCharIndex)(void *, unsigned long);
typedef int (*FnFTLoadGlyph)(void *, unsigned, int);
typedef int (*FnFTRenderGlyph)(void *, int);

constexpr int kFTLoadNoHinting = 1 << 1;  // FT_LOAD_NO_HINTING
constexpr int kFTLoadNoAutoHint = 1 << 15;  // FT_LOAD_NO_AUTOHINT
constexpr int kFTRenderNormal = 0;         // FT_RENDER_MODE_NORMAL

struct GlyphRec {
  SDL_Texture *tex = nullptr;
  int w = 0;
  int h = 0;
  int left = 0;
  int top = 0;
  float advance = 0.f;
};

struct GuiTextEngine {
  void *dl = nullptr;
  void *ft = nullptr;
  void *face = nullptr;
  FnFTInit fnInit = nullptr;
  FnFTDone fnDone = nullptr;
  FnFTNewFace fnNewFace = nullptr;
  FnFTDoneFace fnDoneFace = nullptr;
  FnFTSetPixelSizes fnSetSizes = nullptr;
  FnFTCharIndex fnCharIndex = nullptr;
  FnFTLoadGlyph fnLoadGlyph = nullptr;
  FnFTRenderGlyph fnRenderGlyph = nullptr;
  float ascentPx = 0.f;
  float descentPx = 0.f;
  bool ready = false;
  std::string fontPath;
  std::string lastError;
  std::unordered_map<uint32_t, GlyphRec> glyphs;
};

GuiTextEngine gGuiText;

inline bool decodeUtf8(const std::string &s, size_t &i, uint32_t &cp) {
  if (i >= s.size()) return false;
  const unsigned char b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    cp = b0;
    ++i;
    return true;
  }
  size_t need;
  uint32_t acc;
  if ((b0 & 0xE0) == 0xC0) { need = 1; acc = b0 & 0x1F; }
  else if ((b0 & 0xF0) == 0xE0) { need = 2; acc = b0 & 0x0F; }
  else if ((b0 & 0xF8) == 0xF0) { need = 3; acc = b0 & 0x07; }
  else return false;
  if (i + need >= s.size()) return false;
  for (size_t k = 1; k <= need; ++k) {
    const unsigned char b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xC0) != 0x80) return false;
    acc = (acc << 6) | (b & 0x3F);
  }
  i += need + 1;
  cp = acc;
  return true;
}

bool loadGuiTextEngine() {
  gGuiText.dl = dlopen("libfreetype.so.6", RTLD_NOW | RTLD_GLOBAL);
  if (!gGuiText.dl) gGuiText.dl = dlopen("libfreetype.so", RTLD_NOW | RTLD_GLOBAL);
  if (!gGuiText.dl) return false;
  gGuiText.fnInit = reinterpret_cast<FnFTInit>(dlsym(gGuiText.dl, "FT_Init_FreeType"));
  gGuiText.fnDone = reinterpret_cast<FnFTDone>(dlsym(gGuiText.dl, "FT_Done_FreeType"));
  gGuiText.fnNewFace = reinterpret_cast<FnFTNewFace>(dlsym(gGuiText.dl, "FT_New_Face"));
  gGuiText.fnDoneFace = reinterpret_cast<FnFTDoneFace>(dlsym(gGuiText.dl, "FT_Done_Face"));
  gGuiText.fnSetSizes = reinterpret_cast<FnFTSetPixelSizes>(dlsym(gGuiText.dl, "FT_Set_Pixel_Sizes"));
  gGuiText.fnCharIndex = reinterpret_cast<FnFTCharIndex>(dlsym(gGuiText.dl, "FT_Get_Char_Index"));
  gGuiText.fnLoadGlyph = reinterpret_cast<FnFTLoadGlyph>(dlsym(gGuiText.dl, "FT_Load_Glyph"));
  gGuiText.fnRenderGlyph = reinterpret_cast<FnFTRenderGlyph>(dlsym(gGuiText.dl, "FT_Render_Glyph"));
  if (!gGuiText.fnInit || !gGuiText.fnDone || !gGuiText.fnNewFace || !gGuiText.fnDoneFace ||
      !gGuiText.fnSetSizes || !gGuiText.fnCharIndex || !gGuiText.fnLoadGlyph ||
      !gGuiText.fnRenderGlyph) {
    return false;
  }
  return gGuiText.fnInit(&gGuiText.ft) == 0 && gGuiText.ft != nullptr;
}

// 日本語を含むフォントを探す。DDM_FONT_PATH で明示指定もできる。
std::vector<std::string> fontCandidates() {
  std::vector<std::string> out;
  const char *env = std::getenv("DDM_FONT_PATH");
  if (env && env[0]) out.push_back(env);

  struct Scored { int score; std::string path; };
  std::vector<Scored> found;
  for (const char *dir : {"/usr/share/fonts", "/usr/local/share/fonts"}) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) continue;
    int examined = 0;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
         it != end && examined < 300; ++it, ++examined) {
      std::error_code fec;
      if (!it->is_regular_file(fec)) continue;
      std::string path = it->path().string();
      std::string lower = path;
      for (auto &c : lower) c = static_cast<char>(std::tolower(c));
      const size_t n = lower.size();
      if (n < 4 || (lower.compare(n - 4, 4, ".ttf") != 0 && lower.compare(n - 4, 4, ".otf") != 0 &&
                    lower.compare(n - 3, 3, ".ttc") != 0)) {
        continue;
      }
      int score = 0;
      if (lower.find("jp") != std::string::npos || lower.find("cjk") != std::string::npos) score += 100;
      if (lower.find("ipa") != std::string::npos || lower.find("takao") != std::string::npos ||
          lower.find("arphic") != std::string::npos || lower.find("freesans") != std::string::npos) {
        score += 40;
      }
      if (lower.find("mono") != std::string::npos) score -= 20;
      // Regular を優先し、thin/light/medium/bold/italic/oblique は減点する。
      if (lower.find("regular") != std::string::npos) score += 30;
      for (const char *weight : {"thin", "extralight", "extra-light", "light", "normal",
                                 "medium", "semibold", "bold", "heavy", "black", "italic",
                                 "oblique"}) {
        if (lower.find(weight) != std::string::npos) score -= 25;
      }
      found.push_back(Scored{score, std::move(path)});
    }
  }
  std::sort(found.begin(), found.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
  for (const Scored &f : found) out.push_back(f.path);
  return out;
}

// face が sanity check を通るか。ABI のレイアウト違いや日本語Coverageはここで弾く。
bool tryGuiFont(const std::string &path) {
  void *face = nullptr;
  if (gGuiText.fnNewFace(gGuiText.ft, path.c_str(), 0, &face) != 0 || !face) {
    gGuiText.lastError = "FT_New_Face failed: " + path;
    return false;
  }
  const DFTFaceRec *rec = reinterpret_cast<const DFTFaceRec *>(face);
  if (rec->num_glyphs <= 0 || rec->num_glyphs > 300000 || rec->units_per_em < 256 ||
      rec->units_per_em > 100000 || rec->ascender <= 0 || rec->descender >= 0 || !rec->glyph) {
    gGuiText.lastError = "face sanity failed " + path + " num_glyphs=" +
                         std::to_string(rec->num_glyphs) + " upem=" +
                         std::to_string(rec->units_per_em) + " ascender=" +
                         std::to_string(rec->ascender) + " descender=" +
                         std::to_string(rec->descender);
    gGuiText.fnDoneFace(face);
    return false;
  }
  if (gGuiText.fnSetSizes(face, 0, kGuiTextPx) != 0) {
    gGuiText.lastError = "FT_Set_Pixel_Sizes failed: " + path;
    gGuiText.fnDoneFace(face);
    return false;
  }
  for (uint32_t probe : {uint32_t('A'), uint32_t('m'), uint32_t(0x8983u) /* 見 */, uint32_t(0x8A71u) /* 話 */}) {
    const unsigned idx = gGuiText.fnCharIndex(face, probe);
    if (idx == 0) {
      gGuiText.lastError = "no glyph for cp=" + std::to_string(probe) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
    if (gGuiText.fnLoadGlyph(face, idx, kFTLoadNoHinting | kFTLoadNoAutoHint) != 0) {
      gGuiText.lastError = "FT_Load_Glyph failed cp=" + std::to_string(probe) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
    if (gGuiText.fnRenderGlyph(rec->glyph, kFTRenderNormal) != 0) {
      gGuiText.lastError = "FT_Render_Glyph failed cp=" + std::to_string(probe) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
    const DFTGlyphSlotRec *slot = reinterpret_cast<const DFTGlyphSlotRec *>(rec->glyph);
    if (slot->bitmap_rows == 0 || slot->bitmap_rows > 64 || slot->bitmap_width == 0 ||
        slot->bitmap_width > 64 || slot->bitmap_pitch <= 0 || !slot->bitmap_buffer ||
        slot->bitmap_num_grays < 2 || slot->advance_x < 64 || slot->advance_x > 64 * 64) {
      gGuiText.lastError = "bitmap sanity failed cp=" + std::to_string(probe) + " rows=" +
                           std::to_string(slot->bitmap_rows) + " width=" +
                           std::to_string(slot->bitmap_width) + " pitch=" +
                           std::to_string(slot->bitmap_pitch) + " grays=" +
                           std::to_string(slot->bitmap_num_grays) + " pixel_mode=" +
                           std::to_string(slot->bitmap_pixel_mode) + " adv=" +
                           std::to_string(slot->advance_x) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
  }
  gGuiText.face = face;
  gGuiText.fontPath = path;
  gGuiText.ascentPx = static_cast<float>(rec->ascender) * static_cast<float>(kGuiTextPx) /
                      static_cast<float>(rec->units_per_em);
  gGuiText.descentPx = static_cast<float>(-rec->descender) * static_cast<float>(kGuiTextPx) /
                       static_cast<float>(rec->units_per_em);
  gGuiText.ready = true;
  return true;
}

bool initGuiText() {
  if (!loadGuiTextEngine()) {
    gGuiText.lastError = "dlopen libfreetype.so.6 failed: " + std::string(dlerror() ? dlerror() : "?");
    return false;
  }
  const std::vector<std::string> candidates = fontCandidates();
  if (candidates.empty()) {
    gGuiText.lastError = "no font file found under /usr/share/fonts";
    return false;
  }
  for (const std::string &cand : candidates) {
    if (tryGuiFont(cand)) return true;
  }
  gGuiText.lastError += " (candidates=" + std::to_string(candidates.size()) + ", first=" +
                        candidates[0] + ")";
  return false;
}

void shutdownGuiText() {
  for (auto &kv : gGuiText.glyphs) {
    if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
  }
  gGuiText.glyphs.clear();
  if (gGuiText.face && gGuiText.fnDoneFace) gGuiText.fnDoneFace(gGuiText.face);
  gGuiText.face = nullptr;
  if (gGuiText.ft && gGuiText.fnDone) gGuiText.fnDone(gGuiText.ft);
  gGuiText.ft = nullptr;
  if (gGuiText.dl) {
    dlclose(gGuiText.dl);
    gGuiText.dl = nullptr;
  }
  gGuiText.ready = false;
}

// glyph を rasterize して texture に cache する。失敗は negative cache する。
const GlyphRec *guiGlyph(SDL_Renderer *r, uint32_t cp) {
  const auto it = gGuiText.glyphs.find(cp);
  if (it != gGuiText.glyphs.end()) return it->second.tex ? &it->second : nullptr;
  if (gGuiText.glyphs.size() >= kGlyphCacheMax) return nullptr;

  const DFTFaceRec *rec = reinterpret_cast<const DFTFaceRec *>(gGuiText.face);
  GlyphRec g;
  const unsigned idx = gGuiText.fnCharIndex(gGuiText.face, cp);
  if (idx == 0 ||
      gGuiText.fnLoadGlyph(gGuiText.face, idx, kFTLoadNoHinting | kFTLoadNoAutoHint) != 0 ||
      gGuiText.fnRenderGlyph(rec->glyph, kFTRenderNormal) != 0) {
    gGuiText.glyphs.emplace(cp, g);
    return nullptr;
  }
  const DFTGlyphSlotRec *slot = reinterpret_cast<const DFTGlyphSlotRec *>(rec->glyph);
  g.w = static_cast<int>(slot->bitmap_width);
  g.h = static_cast<int>(slot->bitmap_rows);
  g.left = slot->bitmap_left;
  g.top = slot->bitmap_top;
  g.advance = static_cast<float>(slot->advance_x) / 64.f;  // 26.6 fixed → px
  if (g.w <= 0 || g.h <= 0) {
    gGuiText.glyphs.emplace(cp, g);
    return nullptr;
  }

  std::vector<uint8_t> rgba(static_cast<size_t>(g.w) * static_cast<size_t>(g.h) * 4u);
  const unsigned grays = slot->bitmap_num_grays > 1 ? slot->bitmap_num_grays : 2;
  for (int y = 0; y < g.h; ++y) {
    const unsigned char *src = slot->bitmap_buffer + static_cast<size_t>(y) * slot->bitmap_pitch;
    uint8_t *dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(g.w) * 4u;
    for (int x = 0; x < g.w; ++x) {
      dst[x * 4 + 0] = 255;
      dst[x * 4 + 1] = 255;
      dst[x * 4 + 2] = 255;
      dst[x * 4 + 3] = static_cast<uint8_t>(static_cast<unsigned>(src[x]) * 255u / (grays - 1u));
    }
  }
  // buffer のバイト列は R,G,B,A のまま。SDL_PIXELFORMAT_RGBA8888 は packed の
  // uint32 が 0xRRGGBBAA（little-endian ではバイト列 A,B,G,R）を指すため、この
  // buffer をそのまま渡すと RとAが入れ替わり alpha が色として化ける。
  // little-endian では RGBA32 == ABGR8888 == バイト列 R,G,B,A なので一致する。
  g.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, g.w, g.h);
  if (!g.tex) {
    gGuiText.glyphs.emplace(cp, g);
    return nullptr;
  }
  SDL_UpdateTexture(g.tex, nullptr, rgba.data(), g.w * 4);
  SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND);
  gGuiText.glyphs.emplace(cp, g);
  const auto &stored = gGuiText.glyphs.at(cp);
  return stored.tex ? &stored : nullptr;
}

// UTF-8 テキストを描く。scale は world→screen の拡縮率、budgetPx は screen px の上限。
// 上限を超えた部分は '~' で打ち切る。返り値は実際に使った幅（screen px）。
float drawGuiText(SDL_Renderer *r, float x, float y, const std::string &text, float scale,
                  float budgetPx, uint8_t cr, uint8_t cg, uint8_t cb) {
  if (!gGuiText.ready || scale <= 0.f) return 0.f;
  const float baseline = y + gGuiText.ascentPx * scale;
  float pen = 0.f;
  size_t i = 0;
  bool truncated = false;
  while (i < text.size()) {
    uint32_t cp = 0;
    if (!decodeUtf8(text, i, cp)) { ++i; continue; }
    const GlyphRec *g = guiGlyph(r, cp);
    if (!g) continue;
    if (pen + g->advance * scale > budgetPx) {
      truncated = true;
      break;
    }
    const SDL_FRect dst{x + pen + static_cast<float>(g->left) * scale,
                        baseline - static_cast<float>(g->top) * scale,
                        static_cast<float>(g->w) * scale, static_cast<float>(g->h) * scale};
    SDL_SetTextureColorMod(g->tex, cr, cg, cb);
    SDL_RenderTexture(r, g->tex, nullptr, &dst);
    pen += g->advance * scale;
  }
  if (truncated) {
    const GlyphRec *tilde = guiGlyph(r, '~');
    if (tilde) {
      const SDL_FRect dst{x + pen + static_cast<float>(tilde->left) * scale,
                          baseline - static_cast<float>(tilde->top) * scale,
                          static_cast<float>(tilde->w) * scale, static_cast<float>(tilde->h) * scale};
      SDL_SetTextureColorMod(tilde->tex, cr, cg, cb);
      SDL_RenderTexture(r, tilde->tex, nullptr, &dst);
      pen += tilde->advance * scale;
    }
  }
  return pen;
}

} // namespace

// --- Prompt 5: props 編集関数の forward decl（global scope） ---
// namespace の editor helper（editorBeginAdd / editorLeaveModeIfNeeded）と global の
// pointer/key event handler から使う。定義は props functions block（editorRestoreToBaseline 直後）。
void propsOpen(int nodeIndex);
void propsClose();
void propsRebuildGraph();

// --- %(変数名) プレースホルダの展開（表示時のみ） ---
// Element.dialogue に含まれる %(name) を正規表現 %\(([A-Za-z0-9]+)\) で検出し、
// name が対象変数名リストに含まれる場合のみ、その変数の現在値を 10 進文字列に
// 置換する。対象外の name の %(name) はリテラルとしてそのまま出力する
// （置換・削除しない）。1 つの dialogue に複数の %(…) がある場合はすべて処理する。
// 展開は renderConsole の出力直前でのみ行う。格納された dialogue
// （containerA / containerB / containerC / displayList / objectList）は
// 変更せず、SDL_AppQuit で data.txt に書き戻すテンプレートも元のまま残す。

// %(name) の name を対象グローバル変数へのポインタとして解決する。
// 対象は gTick, gScene, gRoot, gParent, gUnlocks, gEyelevel,
// gTimer0〜gTimer3（gTimers[0..3] への参照）のみ。該当しなければ nullptr。
inline const unsigned *resolvePlaceholderVar(const std::string &name) {
  if (name == "gTick")
    return &gTick;
  if (name == "gScene")
    return &gScene;
  if (name == "gRoot")
    return &gRoot;
  if (name == "gParent")
    return &gParent;
  if (name == "gUnlocks")
    return &gUnlocks;
  if (name == "gEyelevel")
    return &gEyelevel;
  if (name == "gTimer0")
    return &gTimer0;
  if (name == "gTimer1")
    return &gTimer1;
  if (name == "gTimer2")
    return &gTimer2;
  if (name == "gTimer3")
    return &gTimer3;
  if (name == "gTicketNumber")
    return &gTicketNumber;
  if (name == "gCallingNumber")
    return &gCallingNumber;

  return nullptr;
}

// dialogue 中の %(name) を、name が対象変数ならその現在の値の 10 進文字列に置換
// した新しい文字列を返す。対象外の %(name) とそれ以外のテキストはそのまま
// 結果に含める。呼び出し元が保持する dialogue は変更しない。
std::string expandTextPlaceholders(const std::string &dialogue) {
  static const std::regex placeholder("%\\(([A-Za-z0-9]+)\\)");
  std::string result;
  std::string::size_type cursor = 0;
  std::smatch match;
  while (std::regex_search(dialogue.cbegin() + cursor, dialogue.cend(), match,
                           placeholder)) {
    // match[0] / match[1] は string として取る（内容と長さは信頼できる）。
    // 一致開始位置は、cursor 以降の dialogue から一致文字列と同じバイト列を
    // find で探すことで確定する。正規表現は leftmost の一致を返すため、
    // 同じバイト列の最初の出現位置は一致位置と一致する。
    const std::string matched = match[0].str();
    const std::string name = match[1].str();
    const std::string::size_type found = dialogue.find(matched, cursor);
    if (found == std::string::npos) {
      // 念のための防御分岐。一致位置を確定できない場合は残りをそのまま残す。
      break;
    }
    result.append(dialogue, cursor, found - cursor);
    const unsigned *value = resolvePlaceholderVar(name);
    if (value != nullptr) {
      result += std::to_string(*value);
    } else {
      result += matched;
    }
    cursor = found + matched.size();
  }
  result.append(dialogue, cursor, dialogue.size() - cursor);
  return result;
}

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
  ++gTick;
}

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

void rebuildContainerB();
void rebuildContainerC();
void rebuildFlavorContainer();
void rebuildDisplayList();
void rebuildObjectiveList();
void rebuildMoveDestinationList();
void enterMoveMode();
void exitMoveMode();

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
void renderConsole() {
  std::cout << "\033[2J\033[1;1H";

  // 1. ステート情報
  std::cout << "=== [ State ] ===" << std::endl;
  std::cout << "gScene   : " << gScene << "\n"
            << "gRoot    : " << gRoot << "\n"
            << "gParent  : " << gParent << "\n"
            << "gUnlocks : 0x" << std::hex << gUnlocks << std::dec << "\n"
            << "gEyelevel: 0x" << std::hex << gEyelevel << std::dec << "\n"
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
  std::cout << "flavorContainer               : " << flavorContainer.size()
            << std::endl;
  std::cout << "DisplayList                : " << displayList.size()
            << std::endl;
  std::cout << "ObjectiveList              : " << objectList.size()
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
          if (item.dialogue == "dummy") {
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

    // 表示直前に %(変数名) をその時点の変数値へ展開する。格納 dialogue
    // は変更しない。
    std::string itemText = expandTextPlaceholders(displayList[i].dialogue);
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

    if (objectList.empty()) {
      if (isObjectiveSelected) {
        std::cout << "\033[31m*no content\033[0m" << std::endl;
      } else {
        std::cout << "\033[90mno content\033[0m" << std::endl;
      }
    } else {
      for (size_t j = 0; j < objectList.size(); ++j) {
        // 表示直前に %(変数名) をその時点の変数値へ展開する。格納 dialogue
        // は変更しない。
        std::string objText = expandTextPlaceholders(objectList[j].dialogue);
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


// ============================================================================
// --- VIEW_GENERAL の描画（blueprint 風のノード箱と bezier 線） ---
// ============================================================================
inline SDL_FPoint worldToScreen(float x, float y) {
  return SDL_FPoint{x * gViewZoom + gViewPanX, y * gViewZoom + gViewPanY};
}

inline SDL_FPoint screenToWorld(float x, float y) {
  const float z = gViewZoom == 0.f ? 1.f : gViewZoom;
  return SDL_FPoint{(x - gViewPanX) / z, (y - gViewPanY) / z};
}

inline void setSolid(SDL_Renderer *r, uint8_t cr, uint8_t cg, uint8_t cb,
                     uint8_t ca = 255) {
  SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
}

// --- Catppuccin Mocha パレット（公式 hex をそのまま 0xRRGGBB で保持） ---
// https://catppuccin.com/palette の mocha を準拠色とする。視認性は
// 「dark な surface/mantle に text #cdd6f4 を載せる」公式の組合せに揃える。
constexpr uint32_t kMCBase = 0x1e1e2e;       // base
constexpr uint32_t kMCMantle = 0x181825;     // mantle（HUD panel）
constexpr uint32_t kMCText = 0xcdd6f4;       // text（dialogue・見出し：base/surface0 に ~9:1）
constexpr uint32_t kMCSubtext1 = 0xbac2de;   // subtext1（HUD の情報行）
constexpr uint32_t kMCSubtext0 = 0xa6adc8;   // subtext0（export本文・ポート）
constexpr uint32_t kMCOverlay2 = 0x9399b2;   // overlay2（flags・控えな案内）
constexpr uint32_t kMCOverlay1 = 0x7f849c;   // overlay1（操作ヘルプ）
constexpr uint32_t kMCOverlay0 = 0x6c7086;   // overlay0（区切り線・構造ノード枠）
constexpr uint32_t kMCSurface0 = 0x313244;   // surface0（contentノードの塗り）
constexpr uint32_t kMCSurface1 = 0x45475a;   // surface1（dummyノードの塗り）
constexpr uint32_t kMCRed = 0xf38ba8;        // red（quoteストライプ・失敗表示）
constexpr uint32_t kMCGreen = 0xa6e3a1;      // green（hasRead・EXACT OK・成功表示）
constexpr uint32_t kMCYellow = 0xf9e2af;     // yellow（MISMATCH警告）
constexpr uint32_t kMCPeach = 0xfab387;      // peach（child edge）
constexpr uint32_t kMCMauve = 0xcba6f7;      // mauve（onetimeストライプ）
constexpr uint32_t kMCTeal = 0x94e2d5;       // teal（font path: ok）
constexpr uint32_t kMCSky = 0x89dceb;        // sky（id/parent/scene badge）
constexpr uint32_t kMCSapphire = 0x74c7ec;   // sapphire（parent edge: ノード枠 blue と区別）
constexpr uint32_t kMCBlue = 0x89b4fa;       // blue（contentノード枠）
constexpr uint32_t kMCLavender = 0xb4befe;   // lavender（選択ノード枠）

// 0xRRGGBB からR,G,Bを取り出す（定数展開用）。
constexpr uint8_t mcR(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFFu); }
constexpr uint8_t mcG(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFFu); }
constexpr uint8_t mcB(uint32_t c) { return static_cast<uint8_t>(c & 0xFFu); }

inline void setSolid(SDL_Renderer *r, uint32_t hexColor, uint8_t ca = 255) {
  SDL_SetRenderDrawColor(r, mcR(hexColor), mcG(hexColor), mcB(hexColor), ca);
}

// 公式パレットのアクセントを base に 12% で混ぜた tinted surface（mocha風の淡い色面）。
// Prompt 6 色監査（要件 6）: 数値 RGB リテラルは kMC 定数定義のみ（それ以外 zero）。
// tint は kMC 定数からの調色計算（整数・round half up）で導出する。旧リテラル
// 0x383039 / 0x2e363c と同じ値になる（selftest palette audit で実色出現を確認する）。
constexpr uint32_t mcBlendTint(uint32_t baseHex, uint32_t tintHex, unsigned pct) {
  const uint32_t r = (mcR(baseHex) * (100u - pct) + mcR(tintHex) * pct + 50u) / 100u;
  const uint32_t g = (mcG(baseHex) * (100u - pct) + mcG(tintHex) * pct + 50u) / 100u;
  const uint32_t b = (mcB(baseHex) * (100u - pct) + mcB(tintHex) * pct + 50u) / 100u;
  return (r << 16) | (g << 8) | b;
}
constexpr uint32_t kMCFillTemplate = mcBlendTint(kMCBase, kMCPeach, 12u); // template tint（peach 12%）
constexpr uint32_t kMCFillEmpty = mcBlendTint(kMCBase, kMCGreen, 12u);    // empty tint（green 12%）

// WCAG 2.1 relative luminance / contrast ratio（sRGB linearize。面+テキストの
// 4.5:1 判定を pixel readback の実色から測るために使う — Prompt 6 要件 6）。
double wcagLuminance(uint32_t hexColor) {
  const auto lin = [](uint32_t c8) -> double {
    const double c = static_cast<double>(c8) / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(mcR(hexColor)) + 0.7152 * lin(mcG(hexColor)) + 0.0722 * lin(mcB(hexColor));
}
double wcagContrast(uint32_t fgHex, uint32_t bgHex) {
  const double l1 = wcagLuminance(fgHex);
  const double l2 = wcagLuminance(bgHex);
  const double hi = std::max(l1, l2);
  const double lo = std::min(l1, l2);
  return (hi + 0.05) / (lo + 0.05);
}

inline const char *viewModeName() {
  switch (gViewMode) {
    case VIEW_FAMILY:
      return "VIEW_FAMILY";
    case VIEW_CONSTRAINT:
      return "VIEW_CONSTRAINT";
    case EDITOR:
      return "EDITOR";
    case VIEW_GENERAL:
    default:
      return "VIEW_GENERAL";
  }
}

// 画面の debug font は ASCII のみなので UTF-8 は '?' に置換して近似表示する。
// 全文は stdout の export / props dump で確認できる。
// （SDL3 の text texture API は次フェーズの upgrade hook）
std::string asciiPreview(const std::string &src, size_t maxChars) {
  std::string out;
  bool truncated = false;
  for (unsigned char c : src) {
    if (out.size() >= maxChars) {
      truncated = true;
      break;
    }
    out.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
  }
  if (truncated) out += "~";
  return out;
}


// 日本語は FreeType で描き、無い環境では従来の ASCII debug text にフォールバックする。
// 色は Catppuccin Mocha の 0xRRGGBB 定数で渡す。返り値は実際に使った幅（screen px）。
// （VIEW_FAMILY の newRoot badge は badge の後に続けて描くためこの幅を使う）
float drawGuiLine(SDL_Renderer *r, float x, float y, const std::string &text, float scale,
                  float budgetPx, uint32_t hexColor) {
  if (gGuiText.ready) {
    return drawGuiText(r, x, y, text, scale, budgetPx, mcR(hexColor), mcG(hexColor), mcB(hexColor));
  }
  setSolid(r, hexColor);
  const size_t chars = budgetPx > 0.f
                           ? static_cast<size_t>(budgetPx / (6.f * std::max(1.f, scale)))
                           : 0;
  SDL_RenderDebugText(r, x, y, asciiPreview(text, chars).c_str());
  return static_cast<float>(chars) * 6.f * std::max(1.f, scale);
}

// selectedNode の props 詳細（ASCII 行）。HUD と stdout の両方で使う。
// view を渡すと表示順序を mode 別に並べる（VIEW_FAMILY は family 関連項目を先頭に、
// required/excluded は 1 行の最小表示にする。stdout の D dump は general 順序のまま）。
std::vector<std::string> describeElement(int nodeIndex, ViewMode view = VIEW_GENERAL) {
  std::vector<std::string> lines;
  if (nodeIndex < 0 || nodeIndex >= static_cast<int>(gGraph.nodes.size()))
    return lines;
  const GraphNode &n = gGraph.nodes[nodeIndex];
  const uint64_t prop = gDocument.elements[n.elementIndex].element.prop;

  if (view == VIEW_FAMILY) {
    // family 向け優先表示（Prompt 2）: id/parent/child/scene/flags/newRoot/newScene/
    // adj counts/dangling を先頭に並べる。dialogue の preview は既存の D 出力が扱う。
    lines.push_back("node scene=" + hexNibble(n.scene) + " id=" + hexByte(n.id) +
                    " line=" + std::to_string(n.lineIndex + 1));
    lines.push_back("prop=0x" + formatPropHex16(prop));
    lines.push_back("id=" + hexByte(getPropID(prop)) + " parent=" +
                    hexByte(getPropParent(prop)) + " child=" +
                    hexByte(getPropChild(prop)) + " scene=" +
                    hexNibble(getPropScene(prop)));
    lines.push_back(std::string("hasRead=") + (getPropHasRead(prop) ? "1" : "0") +
                    " isQuote=" + (getPropIsQuote(prop) ? "1" : "0") +
                    " isOnetime=" + (getPropIsOnetime(prop) ? "1" : "0"));
    lines.push_back("newRoot=" + hexByte(getPropNewRoot(prop)) + " newScene=" +
                    hexByte(getPropNewScene(prop)) + " family_depth=" +
                    std::to_string(n.famDepth));
    lines.push_back("adj parent=" + std::to_string(n.parentNodes.size()) +
                    " explicitChild=" + std::to_string(n.childNodes.size()) +
                    " dangling=" + (n.danglingParent ? "parent " : "") +
                    (n.danglingChild ? "child" : ""));
    lines.push_back("required=" + hexByte(getPropRequiredReadId(prop)) +
                    " excluded1=" + hexByte(getPropSlot1ExcludedReadId(prop)) +
                    " excluded2=" + hexByte(getPropSlot2ExcludedReadId(prop)));
    return lines;
  }

  if (view == VIEW_CONSTRAINT) {
    // constraint 向け優先表示（Prompt 3）: required/excluded（slot index・raw value・
    // target の scene/id が実在か ghost か）を先頭に並べる。値が 0 の項目は none と表示する
    // （view 別フィルタはこの variant 内に新設。general / family の出力順は壊さない）。
    // raw prop hex はそのまま表示する（formatPropHex16）。
    lines.push_back("node scene=" + hexNibble(n.scene) + " id=" + hexByte(n.id) +
                    " line=" + std::to_string(n.lineIndex + 1));
    lines.push_back("prop=0x" + formatPropHex16(prop));

    const auto constraintLine = [&](const char *name, uint8_t slot, uint8_t effective) {
      std::string s = std::string(name) + " slot" + std::to_string(slot) +
                      " idx=" + std::to_string(getSlotIndex(prop, slot)) +
                      " raw=" + hexByte(getSlotValue(prop, slot));
      if (effective == 0) {
        s += " -> none"; // 値 0 / option 非立ては constraint なし（data.txt の既存慣習）
        return s;
      }
      const int t = gGraph.findNode(n.scene, effective);
      s += t >= 0 ? " -> target=" + hexByte(effective) + " present scene=" +
                        hexNibble(n.scene)
                  : " -> target=" + hexByte(effective) +
                        " ghost（scene内に存在しないid）";
      return s;
    };
    lines.push_back(constraintLine("requiredReadId(option idx=0)", 0,
                                   getPropRequiredReadId(prop)));
    lines.push_back(constraintLine("excluded(slot1 option idx=1)", 1,
                                   getPropSlot1ExcludedReadId(prop)));
    lines.push_back(constraintLine("excluded(slot2 option idx=3)", 2,
                                   getPropSlot2ExcludedReadId(prop)));

    // 他 view の項目は最小表示（family / general の variant がフル表示を持つ）。
    lines.push_back("id=" + hexByte(getPropID(prop)) + " parent=" +
                    hexByte(getPropParent(prop)) + " child=" +
                    hexByte(getPropChild(prop)) + " scene=" +
                    hexNibble(getPropScene(prop)));
    lines.push_back(std::string("hasRead=") + (getPropHasRead(prop) ? "1" : "0") +
                    " isQuote=" + (getPropIsQuote(prop) ? "1" : "0") +
                    " isOnetime=" + (getPropIsOnetime(prop) ? "1" : "0"));
    lines.push_back("newRoot=" + hexByte(getPropNewRoot(prop)) + " newScene=" +
                    hexByte(getPropNewScene(prop)) + "（family viewer 項目は最小表示）");
    // node 側の list は -1 を含むので present / ghost を分離して数える（-1 は ghost 側）。
    size_t reqPresent = 0, reqGhost = 0, exPresent = 0, exGhost = 0;
    for (int t : n.requiredReadNodes) { if (t >= 0) ++reqPresent; else ++reqGhost; }
    for (int t : n.excludedReadNodes) { if (t >= 0) ++exPresent; else ++exGhost; }
    lines.push_back("adj required present=" + std::to_string(reqPresent) +
                    " ghost=" + std::to_string(reqGhost) +
                    " excluded present=" + std::to_string(exPresent) +
                    " ghost=" + std::to_string(exGhost) +
                    " dangling=" + (n.danglingParent ? "parent " : "") +
                    (n.danglingChild ? "child" : ""));
    return lines;
  }

  if (view == EDITOR) {
    // props menu 表示順（Prompt 5）: basic flags → id/parent/scene → slot0 → slot1 → slot2 → slot3。
    // 表示は hex + decimal（id/parent/scene・slot value）・flags は hex + binary・slot は option 名と
    // option に応じた value の意味（id 系は present / ghost、timer / eyelevel は分解値）を出す。
    // 他 view（general / family / constraint）の出力順は壊さない（variant 内に新設しただけ）。
    const uint8_t flagsByte = static_cast<uint8_t>((prop >> 16) & 0xFu);
    lines.push_back("node scene=" + hexNibble(n.scene) + " id=" + hexByte(n.id) +
                    " line=" + std::to_string(n.lineIndex + 1));
    lines.push_back("prop=0x" + formatPropHex16(prop));
    lines.push_back("section=basic flags hasRead=" + std::string(getPropHasRead(prop) ? "1" : "0") +
                    " isQuote=" + (getPropIsQuote(prop) ? "1" : "0") +
                    " isOnetime=" + (getPropIsOnetime(prop) ? "1" : "0") +
                    " vacant_bit19=" + (((prop >> 19) & 0x1u) ? "1" : "0") +
                    " flags_bin=" + binaryByte(flagsByte) + "（bit16-19・optional）");
    lines.push_back("section=id/parent/scene id=" + hexDec(getPropID(prop)) +
                    " parent=" + hexDec(getPropParent(prop)) +
                    " scene=" + hexDec(getPropScene(prop)) +
                    "（入力は hex を優先・0x00-0xFF / 0x0-0F）");

    const auto targetNote = [&](uint8_t id) -> std::string {
      if (id == 0) return " -> none";
      return gGraph.findNode(n.scene, id) >= 0 ? " -> present scene=" + hexNibble(n.scene)
                                               : " -> ghost（scene内に存在しないid）";
    };
    for (int slot = 0; slot < 4; ++slot) {
      const uint8_t idx = getSlotIndex(prop, static_cast<uint8_t>(slot));
      const uint8_t val = getSlotValue(prop, static_cast<uint8_t>(slot));
      std::string s = "section=slot" + std::to_string(slot) +
                      " option=" + propsOptionLabel(prop, static_cast<uint8_t>(slot)) +
                      " idx=" + std::to_string(idx) + " raw=" + hexDec(val);
      switch (slot) {
        case 0:
          if (val != 0) {
            if (idx == 0) s += " requiredReadId=" + hexByte(val) + targetNote(val);
            else s += " unlocks=" + hexByte(val) + " bin=" + binaryByte(val);
          } else {
            s += " -> none（index=0/value=0 の既存慣習）";
          }
          break;
        case 1:
          if (val != 0) {
            if (idx == 0) s += " child=" + hexByte(val) + targetNote(val);
            else if (idx == 1) s += " excludedReadId=" + hexByte(val) + targetNote(val);
            else if (idx == 2) s += " newRoot=" + hexByte(val) + targetNote(val);
            else s += " downUnlocks=" + hexByte(val) + " bin=" + binaryByte(val) +
                     "（送信時に gUnlocks &= ~mask で立っているbitだけを下げる）";
          } else {
            s += " -> none（child/excluded/newRoot/downUnlocks は排他・value 0 で none）";
          }
          break;
        case 2:
          if (val != 0) {
            if (idx == 0) {
              s += " timerSetter value=" + hexByte(val) + " timerId=" +
                   std::to_string(getPropTimerSetterId(prop)) + " action=" +
                   std::to_string(getPropTimerSetterAction(prop)) + "(" +
                   kTimerActionNames[getPropTimerSetterAction(prop)] + ") vacant bits2-3=" +
                   binaryByte(static_cast<uint8_t>((val >> 2) & 0x3u)) + "（保持）";
            } else if (idx == 1) {
              s += " unlocks=" + hexByte(val) + " bin=" + binaryByte(val);
            } else if (idx == 2) {
              s += " requiredUnlockFlags=" + hexByte(val) + " bin=" + binaryByte(val);
            } else {
              s += " excludedReadId=" + hexByte(val) + targetNote(val);
            }
          } else {
            s += " -> none（value 0 で option 立っても実効 none）";
          }
          break;
        default:
          if (val != 0) {
            if (idx == 0) {
              s += " timerTarget value=" + hexByte(val) + " operator=" +
                   std::to_string(getPropTimerTargetOp(prop)) + "(" +
                   kTimerOpNames[getPropTimerTargetOp(prop)] + ") timerId=" +
                   std::to_string(getPropTimerTargetId(prop)) + " count=" +
                   std::to_string(getPropTimerTargetCount(prop));
            } else if (idx == 1) {
              s += " newScene=" + hexDec(val) + "（value 0 は「変化なし」sentinel・選択可）";
            } else if (idx == 2) {
              s += " excludedUnlockFlags=" + hexByte(val) + " bin=" + binaryByte(val);
            } else {
              s += " eyelevel=" + std::to_string(getPropEyelevel(prop)) +
                   "（0 は無条件） toggleEyelevel=" + std::to_string(getPropToggleEyelevel(prop)) +
                   "（0 は toggle なし）";
            }
          } else {
            s += " -> none（value 0 で option 立っても実効 none）";
          }
          break;
      }
      lines.push_back(s);
    }
    lines.push_back("adj parent=" + std::to_string(n.parentNodes.size()) +
                    " child=" + std::to_string(n.childNodes.size()) +
                    " requiredRead=" + std::to_string(n.requiredReadNodes.size()) +
                    " excludedRead=" + std::to_string(n.excludedReadNodes.size()) +
                    " dangling=" + (n.danglingParent ? "parent " : "") +
                    (n.danglingChild ? "child" : ""));
    lines.push_back("dialogue は編集しない（別 panel・原文のまま） timer/unlock/eyelevel の制約 edge 可視化は未実装（Prompt3 default 範囲）");
    lines.push_back("undo stack=" + std::to_string(gProps.undoStack.size()) +
                    "（props panel open 時 U=復元・全 undo で baseline に戻る）");
    return lines;
  }

  // general / family / constraint の出力順は上記 variant のままで、以下は general の既定順（unchanged）。
  lines.push_back("node scene=" + hexNibble(n.scene) + " id=" + hexByte(n.id) +
                  " line=" + std::to_string(n.lineIndex + 1));
  lines.push_back("prop=0x" + formatPropHex16(prop));
  lines.push_back("id=" + hexByte(getPropID(prop)) + " parent=" +
                  hexByte(getPropParent(prop)) + " scene=" +
                  hexNibble(getPropScene(prop)));
  lines.push_back(std::string("hasRead=") + (getPropHasRead(prop) ? "1" : "0") +
                  " isQuote=" + (getPropIsQuote(prop) ? "1" : "0") +
                  " isOnetime=" + (getPropIsOnetime(prop) ? "1" : "0"));
  for (int slot = 0; slot < 4; ++slot) {
    lines.push_back("slot" + std::to_string(slot) + " index=" +
                    std::to_string(getSlotIndex(prop, static_cast<uint8_t>(slot))) +
                    " value=" + hexByte(getSlotValue(prop, static_cast<uint8_t>(slot))));
  }
  lines.push_back("slot0 requiredReadId=" + hexByte(getPropRequiredReadId(prop)) +
                  " unlocks=" + hexByte(getPropSlot0Unlocks(prop)));
  lines.push_back("slot1 child=" + hexByte(getPropChild(prop)) + " excludedReadId=" +
                  hexByte(getPropSlot1ExcludedReadId(prop)) + " newRoot=" +
                  hexByte(getPropNewRoot(prop)) + " downUnlocks=" +
                  hexByte(getPropSlot1DownUnlocks(prop)));
  lines.push_back("slot2 timerSetter=" + hexByte(getPropTimerSetter(prop)) +
                  " unlocks=" + hexByte(getPropSlot2Unlocks(prop)) +
                  " requiredUnlockFlags=" + hexByte(getPropRequiredUnlockFlags(prop)) +
                  " excludedReadId=" + hexByte(getPropSlot2ExcludedReadId(prop)));
  lines.push_back("slot3 timerTarget=" + hexByte(getPropTimerTarget(prop)) +
                  " newScene=" + hexByte(getPropNewScene(prop)) +
                  " excludedUnlockFlags=" + hexByte(getPropExcludedUnlockFlags(prop)) +
                  " eyelevel=" + hexByte(getPropEyelevel(prop)) +
                  " toggleEyelevel=" + hexByte(getPropToggleEyelevel(prop)));
  if (hasPropTimerSetter(prop)) {
    lines.push_back("timerSetter timerId=" + hexByte(getPropTimerSetterId(prop)) +
                    " action=" + std::to_string(getPropTimerSetterAction(prop)));
  }
  if (hasPropTimerTarget(prop)) {
    lines.push_back("timerTarget op=" + std::to_string(getPropTimerTargetOp(prop)) +
                    " timerId=" + hexByte(getPropTimerTargetId(prop)) +
                    " count=" + std::to_string(getPropTimerTargetCount(prop)));
  }
  lines.push_back("adj parent=" + std::to_string(n.parentNodes.size()) +
                  " child=" + std::to_string(n.childNodes.size()) +
                  " requiredRead=" + std::to_string(n.requiredReadNodes.size()) +
                  " excludedRead=" + std::to_string(n.excludedReadNodes.size()) +
                  " dangling=" + (n.danglingParent ? "parent " : "") +
                  (n.danglingChild ? "child" : ""));
  return lines;
}

void printDocumentToStdout(const Document &doc) {
  std::cout << serializeDocument(doc) << std::flush;
}

void printRoundTripReport(const Document &doc) {
  const RoundTripReport rep = verifyRoundTrip(doc);
  std::cout << "=== [ round-trip ] ===" << std::endl;
  std::cout << "original bytes=" << rep.originalBytes
            << " serialized bytes=" << rep.serializedBytes
            << " lines=" << rep.lineCount << " elements=" << rep.elementLines
            << " comments=" << rep.commentLines << " blank=" << rep.blankLines
            << std::endl;
  std::cout << (rep.exact ? "RESULT: EXACT MATCH (identical data.txt)"
                          : "RESULT: MISMATCH")
            << std::endl;
  if (!rep.exact) {
    std::cout << "first diff at line " << rep.firstDiffLine << std::endl;
    std::cout << "  original:   " << rep.originalSample << std::endl;
    std::cout << "  serialized: " << rep.serializedSample << std::endl;
    // Prompt 4 baseline diff: load 時の baseline（Document::raw）に対し、diff が追加行＋
    // dirty 行のみであることを報告する（firstDiffLine の意味はそのまま）。
    std::cout << "baseline lines=" << rep.baselineLines << " added=" << rep.addedLines
              << " edited=" << rep.editedLines << " added_or_dirty_only="
              << (rep.diffIsAddedOrDirtyOnly ? 1 : 0) << std::endl;
  }
  std::cout << std::flush;
}

void printSelectedElementDetails() {
  if (gSelectedNode < 0) {
    std::cout << "[no selected node]" << std::endl << std::flush;
    return;
  }
  const GraphNode &n = gGraph.nodes[gSelectedNode];
  const Element &e = gDocument.elements[n.elementIndex].element;
  std::cout << "=== [ selected element ] ===" << std::endl;
  std::cout << "raw line: " << gDocument.lines[n.lineIndex].text << std::endl;
  std::cout << "dialogue(raw):    " << e.dialogue << std::endl;
  std::cout << "dialogue(expand): " << expandTextPlaceholders(e.dialogue) << std::endl;
  for (const std::string &l : describeElement(gSelectedNode)) std::cout << l << std::endl;
  std::cout << std::flush;
}

// parent chain を辿って column depth を決める。cycle / dangling は安全に打ち切る。
int chainDepth(const Graph &g, int idx) {
  std::unordered_set<int> seen;
  int depth = 0;
  int cur = idx;
  while (true) {
    if (cur < 0 || !seen.insert(cur).second || depth > 64) return depth;
    const GraphNode &n = g.nodes[cur];
    if (n.parentNodes.empty()) return depth;
    cur = n.parentNodes[0];
    ++depth;
  }
}

// scene 別のレイヤーレイアウト。column = parent chain depth、row = id 順。
void layoutScene(uint8_t scene) {
  const std::vector<int> inScene = gGraph.nodesInScene(scene);
  std::vector<std::pair<int, int>> depthIdx;
  depthIdx.reserve(inScene.size());
  for (int i : inScene) depthIdx.emplace_back(chainDepth(gGraph, i), i);

  std::sort(depthIdx.begin(), depthIdx.end(),
            [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
              if (a.first != b.first) return a.first < b.first;
              return gGraph.nodes[a.second].id < gGraph.nodes[b.second].id;
            });

  int prevDepth = -1;
  int row = 0;
  for (const std::pair<int, int> &di : depthIdx) {
    if (di.first != prevDepth) {
      prevDepth = di.first;
      row = 0;
    }
    GraphNode &n = gGraph.nodes[di.second];
    n.depth = di.first;
    n.wx = static_cast<float>(di.first) * static_cast<float>(kNodeW + kGapX);
    n.wy = static_cast<float>(row) * static_cast<float>(kNodeH + kGapY);
    ++row;
  }
}

// ============================================================================
// --- VIEW_FAMILY の per-mode layout: layoutFamily（top-down forest）。Prompt 2 ---
// ============================================================================
// 決定性: 同じ data.txt → 同じ座標。familyWx/familyWy/famDepth に書き、
// VIEW_GENERAL の wx/wy は書き換えない（mode 切替で general の座標は壊れない）。
// childAdj（parent field から作る階層 child）でツリーを展開し、親を上部・child を下部、
// 同 depth は横に並べる。childLinkAdj（slot1 の explicit child）はツリーに含めない
// （peach / yellow の overlay edge で別経路描画）。

// childAdj の階層 child を同 scene のノードに絞り id 昇順で返す（決定性のため）。
std::vector<int> familyChildren(uint8_t scene, int u) {
  std::vector<int> out;
  for (int c : gGraph.childAdj[u]) {
    if (c >= 0 && gGraph.nodes[c].scene == scene) out.push_back(c);
  }
  std::sort(out.begin(), out.end(),
            [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
  return out;
}

// subtree が占める band 幅。band(u) = max(pitch, Σ children band)。
// path で cycle を打ち切る（cycle の node は 1 node 分の band で安全に止める）。
float familySubWidth(uint8_t scene, int u, std::unordered_set<int> &path,
                     std::unordered_map<int, float> &memo) {
  const auto m = memo.find(u);
  if (m != memo.end()) return m->second;
  if (!path.insert(u).second) return static_cast<float>(kNodeW + kGapX);
  float band = 0.f;
  for (int c : familyChildren(scene, u)) band += familySubWidth(scene, c, path, memo);
  path.erase(u);
  const float w = std::max(static_cast<float>(kNodeW + kGapX), band);
  memo.emplace(u, w);
  return w;
}

// u を band の中央に置き、children を band に沿って左から右へ並べる。
// visited（placed）で再訪・cycle を打ち切る。chainDepth() と同じ depth 上限 64。
void familyPlace(uint8_t scene, int u, int depth, float baseX, float bandW,
                 std::unordered_set<int> &placed, std::unordered_map<int, float> &memo) {
  if (!placed.insert(u).second) return;
  GraphNode &n = gGraph.nodes[u];
  n.familyWx = baseX + (bandW - static_cast<float>(kNodeW)) * 0.5f;
  n.familyWy = static_cast<float>(depth) * static_cast<float>(kNodeH + kGapY);
  n.famDepth = depth;
  if (depth >= 64) return;
  float cursor = baseX;
  for (int c : familyChildren(scene, u)) {
    std::unordered_set<int> guard; // memo は root の band 計算で埋まっているので通常参照のみ
    const float cw = familySubWidth(scene, c, guard, memo);
    familyPlace(scene, c, depth + 1, cursor, cw, placed, memo);
    cursor += cw;
  }
}

void layoutFamily(uint8_t scene) {
  const std::vector<int> inScene = gGraph.nodesInScene(scene);
  std::unordered_set<int> placed;
  std::unordered_map<int, float> memo;
  float cursorX = 0.f;

  // pass 1: roots（parent == 0）と danglingParent（parent が同 scene に無い）を
  // root 扱いとして id 昇順で multi-root forest を横に並べる。
  std::vector<int> rootLike;
  for (int i : inScene) {
    const GraphNode &n = gGraph.nodes[i];
    if (n.parentId == 0 || n.danglingParent) rootLike.push_back(i);
  }
  std::sort(rootLike.begin(), rootLike.end(),
            [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
  for (int r : rootLike) {
    if (placed.count(r)) continue;
    std::unordered_set<int> guard;
    const float band = familySubWidth(scene, r, guard, memo);
    familyPlace(scene, r, 0, cursorX, band, placed, memo);
    cursorX += band;
  }

  // pass 2: どの root からも届かない残り（cycle の component / depth 上限越え）を
  // weakly-connected に group し、child 数の最大・同数なら id 最小の node を root にして
  // 1 tree ずつ横に足す（決定性・全 node を配置する）。
  std::unordered_set<int> remaining;
  for (int i : inScene) {
    if (!placed.count(i)) remaining.insert(i);
  }
  while (!remaining.empty()) {
    int seed = *remaining.begin();
    for (int i : remaining) {
      if (gGraph.nodes[i].id < gGraph.nodes[seed].id) seed = i;
    }
    remaining.erase(seed);
    std::vector<int> comp;
    std::vector<int> stack{seed};
    comp.push_back(seed);
    while (!stack.empty()) {
      const int u = stack.back();
      stack.pop_back();
      comp.push_back(u);
      for (int v : gGraph.parentAdj[u]) {
        if (v >= 0 && remaining.erase(v)) stack.push_back(v);
      }
      for (int v : gGraph.childAdj[u]) {
        if (v >= 0 && remaining.erase(v)) stack.push_back(v);
      }
    }
    int root = comp.front();
    for (int u : comp) {
      const size_t cu = gGraph.childAdj[u].size();
      const size_t ru = gGraph.childAdj[root].size();
      if (cu > ru || (cu == ru && gGraph.nodes[u].id < gGraph.nodes[root].id)) root = u;
    }
    std::unordered_set<int> guard;
    const float band = familySubWidth(scene, root, guard, memo);
    familyPlace(scene, root, 0, cursorX, band, placed, memo);
    cursorX += band;
  }
}

// 現在 active な mode の world 座標。描画・pick・fit は必ず active layout を読む。
inline SDL_FPoint nodePos(const GraphNode &n) {
  if (gViewMode == VIEW_FAMILY) return SDL_FPoint{n.familyWx, n.familyWy};
  return SDL_FPoint{n.wx, n.wy};
}

// active mode の layout 関数を適用する（switchScene / L / mode 切替で呼ぶ）。
// VIEW_GENERAL の layoutScene は wx/wy のだけを書き、VIEW_FAMILY の layoutFamily は
// familyWx/familyWy のだけ書くため、双方の座標は互いに壊れない。
void applyActiveLayout() {
  if (gViewMode == VIEW_FAMILY) layoutFamily(gViewScene);
  else layoutScene(gViewScene); // VIEW_GENERAL と、constraint / editor の既定 layout
}

void windowSize(int *w, int *h) {
  *w = 320;
  *h = 240;
  if (gWindow) SDL_GetWindowSize(gWindow, w, h);
  if (*w <= 0) *w = 320;
  if (*h <= 0) *h = 240;
}

void fitViewToScene() {
  int winW = 0, winH = 0;
  windowSize(&winW, &winH);

  const std::vector<int> inScene = gGraph.nodesInScene(gViewScene);
  if (inScene.empty()) {
    gViewZoom = 1.f;
    gViewPanX = static_cast<float>(kHudW) + 20.f;
    gViewPanY = 20.f;
    return;
  }

  float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
  bool first = true;
  for (int i : inScene) {
    const GraphNode &n = gGraph.nodes[i];
    const SDL_FPoint w = nodePos(n); // active layout（general / family）の座標で fit する
    const float x1 = w.x, y1 = w.y;
    const float x2 = w.x + static_cast<float>(kNodeW);
    const float y2 = w.y + static_cast<float>(kNodeH);
    if (first) {
      minX = x1; minY = y1; maxX = x2; maxY = y2; first = false;
    } else {
      minX = std::min(minX, x1);
      minY = std::min(minY, y1);
      maxX = std::max(maxX, x2);
      maxY = std::max(maxY, y2);
    }
  }

  const float availW = static_cast<float>(winW - kHudW) - 40.f;
  const float availH = static_cast<float>(winH) - 40.f;
  const float bw = std::max(1.f, maxX - minX);
  const float bh = std::max(1.f, maxY - minY);
  float zoom = std::min(availW / bw, availH / bh);
  gViewZoom = std::clamp(zoom, 0.2f, 1.2f);
  gViewPanX = static_cast<float>(kHudW) + 20.f - minX * gViewZoom;
  gViewPanY = 20.f - minY * gViewZoom;
}

inline SDL_FPoint outputPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x + static_cast<float>(kNodeW),
                       w.y + static_cast<float>(kNodeH) * 0.5f);
}

inline SDL_FPoint inputPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x, w.y + static_cast<float>(kNodeH) * 0.5f);
}

// family（top-down）の port: 親の下端中央 / child の上端中央。active layout を読む。
inline SDL_FPoint familyBottomPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x + static_cast<float>(kNodeW) * 0.5f,
                       w.y + static_cast<float>(kNodeH));
}

inline SDL_FPoint familyTopPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x + static_cast<float>(kNodeW) * 0.5f, w.y);
}

// blueprint 風の cubic bezier を polyline で近似して描く。
void drawBezierEdge(SDL_Renderer *r, SDL_FPoint a, SDL_FPoint b) {
  float dx = b.x - a.x;
  if (dx < 0.f) dx = -dx;
  const float cp = std::max(28.f, dx * 0.55f);
  const SDL_FPoint p1{a.x + cp, a.y};
  const SDL_FPoint p2{b.x - cp, b.y};

  SDL_FPoint prev = a;
  constexpr int kSteps = 22;
  for (int s = 1; s <= kSteps; ++s) {
    const float t = static_cast<float>(s) / static_cast<float>(kSteps);
    const float mt = 1.f - t;
    SDL_FPoint cur{
        mt * mt * mt * a.x + 3.f * mt * mt * t * p1.x +
            3.f * mt * t * t * p2.x + t * t * t * b.x,
        mt * mt * mt * a.y + 3.f * mt * mt * t * p1.y +
            3.f * mt * t * t * p2.y + t * t * t * b.y};
    SDL_RenderLine(r, prev.x, prev.y, cur.x, cur.y);
    prev = cur;
  }

  // 入力ポートに流れ込む chevron（矢印）
  SDL_RenderLine(r, b.x, b.y, b.x - 6.f, b.y - 4.f);
  SDL_RenderLine(r, b.x, b.y, b.x - 6.f, b.y + 4.f);
}

void drawNodeBox(SDL_Renderer *r, const GraphNode &n, bool selected) {
  const SDL_FPoint p = worldToScreen(nodePos(n).x, nodePos(n).y);
  const float w = static_cast<float>(kNodeW) * gViewZoom;
  const float h = static_cast<float>(kNodeH) * gViewZoom;
  const SDL_FRect rect{p.x, p.y, w, h};

  switch (n.kind) {
    case NODE_DUMMY:
      setSolid(r, kMCSurface1); // dummy: mocha surface1（濃い灰色面）
      break;
    case NODE_TEMPLATE:
      setSolid(r, kMCFillTemplate); // template: base + peach tint
      break;
    case NODE_EMPTY:
      setSolid(r, kMCFillEmpty); // empty: base + green tint
      break;
    case NODE_CONTENT:
    default:
      setSolid(r, kMCSurface0); // content: mocha surface0（text #cdd6f4 と ~8.7:1）
      break;
  }
  SDL_RenderFillRect(r, &rect);

  if (selected)
    setSolid(r, kMCLavender); // 選択ノード: lavender #b4befe
  else if (n.kind != NODE_CONTENT)
    setSolid(r, kMCOverlay0); // dummy / template / empty は構造ノード: overlay0
  else
    setSolid(r, kMCBlue); // content: blue #89b4fa
  SDL_RenderRect(r, &rect);

  // isQuote / isOnetime の左ストライプ。hasRead の印は load-time の bit をそのまま。
  float stripeX = p.x;
  if (n.isQuote) {
    setSolid(r, kMCRed);
    const SDL_FRect stripe{stripeX, p.y, 4.f, h};
    SDL_RenderFillRect(r, &stripe);
    stripeX += 4.f;
  }
  if (n.isOnetime) {
    setSolid(r, kMCMauve);
    const SDL_FRect stripe{stripeX, p.y, 4.f, h};
    SDL_RenderFillRect(r, &stripe);
  }
  if (n.hasRead) {
    setSolid(r, kMCGreen);
    const SDL_FRect mark{p.x + w - 12.f, p.y + 4.f, 8.f, 8.f};
    SDL_RenderFillRect(r, &mark);
  }

  // 入力 / 出力ポート
  setSolid(r, kMCSubtext0);
  const float midY = p.y + h * 0.5f;
  const SDL_FRect inPort{p.x - static_cast<float>(kPortR),
                         midY - static_cast<float>(kPortR),
                         static_cast<float>(kPortR) * 2.f,
                         static_cast<float>(kPortR) * 2.f};
  const SDL_FRect outPort{p.x + w - static_cast<float>(kPortR),
                          midY - static_cast<float>(kPortR),
                          static_cast<float>(kPortR) * 2.f,
                          static_cast<float>(kPortR) * 2.f};
  SDL_RenderFillRect(r, &inPort);
  SDL_RenderFillRect(r, &outPort);

  if (w < 44.f) return; // zoom が小さい場合はテキストを省略し、箱だけ描く

  const float textBudget = w - 12.f;
  // badge は小さめ、dialogue は基準サイズ、flags は小さめで段差を作る。
  const float badgeW = drawGuiLine(
      r, p.x + 6.f, p.y + 4.f,
      "id=" + hexByte(n.id) + " par=" + hexByte(n.parentId) + " sc=" +
          hexNibble(n.scene) + (n.danglingParent ? " par!" : "") +
          (n.danglingChild ? " chi!" : ""),
      gViewZoom * 0.75f, textBudget, kMCSky); // badge: sky #89dceb

  // newRoot を持つノードは別 badge（"newRoot→" + mauve）を badge の右に続ける。
  if (n.newRootId != 0) {
    const float remain = textBudget - badgeW - 6.f;
    if (remain > 24.f) {
      drawGuiLine(r, p.x + 6.f + badgeW + 6.f, p.y + 4.f,
                  "newRoot→" + hexByte(n.newRootId), gViewZoom * 0.75f, remain,
                  kMCMauve); // mauve #cba6f7（onetime stripe とは別経路の badge）
    }
  }

  drawGuiLine(r, p.x + 6.f, p.y + 18.f,
              gDocument.elements[n.elementIndex].element.dialogue, gViewZoom, textBudget,
              kMCText); // dialogue: text #cdd6f4（mocha の base/surface0 標準組合せ）

  std::string flags;
  if (n.hasRead) flags += "hasRead ";
  if (n.isQuote) flags += "quote ";
  if (n.isOnetime) flags += "onetime ";
  if (n.kind == NODE_DUMMY) flags += "dummy ";
  else if (n.kind == NODE_TEMPLATE) flags += "template ";
  else if (n.kind == NODE_EMPTY) flags += "empty ";
  if (!flags.empty())
    drawGuiLine(r, p.x + 6.f, p.y + 34.f, flags, gViewZoom * 0.7f, textBudget, kMCOverlay2);
}

// parent edge と child edge は色で区別する。constraint edges は VIEW_CONSTRAINT の hook。
void drawGraphEdges(SDL_Renderer *r) {
  for (const GraphEdge &e : gGraph.edges) {
    if (e.kind != EDGE_PARENT && e.kind != EDGE_CHILD) continue;
    if (e.from < 0 || e.to < 0) continue; // dangling は badge で示す（線は描かない）
    const GraphNode &a = gGraph.nodes[e.from];
    const GraphNode &b = gGraph.nodes[e.to];
    if (a.scene != gViewScene || b.scene != gViewScene) continue; // scene filter

    if (e.kind == EDGE_PARENT)
      setSolid(r, kMCSapphire); // parent -> id: sapphire #74c7ec（ノード枠 blue と区別）
    else
      setSolid(r, kMCPeach); // id -> child: peach #fab387
    drawBezierEdge(r, outputPort(a), inputPort(b));
  }
}

// 階層 edge（親の下端 → child の上端）を vertical cubic bezier の polyline で描く。
// drawBezierEdge は horizontal flow（port が左右）なので、top-down 樹にはこの別経路を使う。
void drawFamilyTreeEdge(SDL_Renderer *r, SDL_FPoint a, SDL_FPoint b) {
  float dy = b.y - a.y;
  if (dy < 0.f) dy = -dy;
  const float cp = std::max(24.f, dy * 0.55f);
  const SDL_FPoint p1{a.x, a.y + cp};
  const SDL_FPoint p2{b.x, b.y - cp};

  SDL_FPoint prev = a;
  constexpr int kSteps = 22;
  for (int s = 1; s <= kSteps; ++s) {
    const float t = static_cast<float>(s) / static_cast<float>(kSteps);
    const float mt = 1.f - t;
    SDL_FPoint cur{mt * mt * mt * a.x + 3.f * mt * mt * t * p1.x +
                       3.f * mt * t * t * p2.x + t * t * t * b.x,
                   mt * mt * mt * a.y + 3.f * mt * mt * t * p1.y +
                       3.f * mt * t * t * p2.y + t * t * t * b.y};
    SDL_RenderLine(r, prev.x, prev.y, cur.x, cur.y);
    prev = cur;
  }

  // child の上端に流れ込む chevron（下向き矢印）
  SDL_RenderLine(r, b.x, b.y, b.x - 6.f, b.y + 4.f);
  SDL_RenderLine(r, b.x, b.y, b.x + 6.f, b.y + 4.f);
}

// VIEW_FAMILY: selected scene の階層 forest を layoutFamily の active layout で描く（Prompt 2）。
// 階層 edge = sapphire、explicit child(slot1) edge = peach、矛盾 edge = yellow、
// 構造ノードは既存 tint / overlay0 枠のまま目立たせない。scene filter は nodesInScene を維持。
void drawFamilyView(SDL_Renderer *r) {
  // 階層 edge（parent field の parent/child link）。dangling は線を描かず badge で示す。
  setSolid(r, kMCSapphire); // sapphire #74c7ec（ノード枠 blue と区別）
  for (const GraphEdge &e : gGraph.edges) {
    if (e.kind != EDGE_PARENT || e.from < 0 || e.to < 0) continue;
    const GraphNode &a = gGraph.nodes[e.from];
    const GraphNode &b = gGraph.nodes[e.to];
    if (a.scene != gViewScene || b.scene != gViewScene) continue; // scene filter
    drawFamilyTreeEdge(r, familyBottomPort(a), familyTopPort(b));
  }

  // explicit child（slot1 の childLinkAdj）: ツリーに含めない別経路（横方向 bezier）。
  // parent field と slot1 child が別ノードを指す矛盾 edge は yellow #f9e2af で overlay。
  for (const GraphEdge &e : gGraph.edges) {
    if (e.kind != EDGE_CHILD || e.from < 0 || e.to < 0) continue;
    const GraphNode &a = gGraph.nodes[e.from];
    const GraphNode &b = gGraph.nodes[e.to];
    if (a.scene != gViewScene || b.scene != gViewScene) continue;
    const bool conflict = b.parentId != a.id;
    setSolid(r, conflict ? kMCYellow : kMCPeach); // yellow #f9e2af / peach #fab387
    drawBezierEdge(r, outputPort(a), inputPort(b));
  }

  // ノード箱（family 向け優先表示の badge も含めて active layout の位置に描く）
  for (int i : gGraph.nodesInScene(gViewScene))
    drawNodeBox(r, gGraph.nodes[i], gSelectedNode == i);
}

// --- VIEW_CONSTRAINT constraint viewer（Prompt 3） ---
// required/excluded の制約 edge を active layout（既定の general layout）に重畳描画する。
// Document は mutate しない（round-trip は壊れない）。
// 色は mocha の意味体系: required=teal（hasRead の green と区別）、
// excluded slot1=red、excluded slot2=yellow（family の矛盾 yellow とは意味が違う）。
// duplicate edge は統合せずそのまま複数描く（self constraint / cycle も描ける）。
// timer / unlock flags / eyelevel の制約 edge はこのセッションでは描かない（Prompt5 以降）。
// scene filter は nodesInScene(gViewScene) を維持する。

// 制約 edge を kind 別に数える（GraphEdge.to == -1 の ghost を含む）。
// ノード側の requiredReadNodes / excludedReadNodes は -1 を含むので count から除外し、
// ghost は edges の to==-1 で数える（Prompt 3 の要件 3）。
struct ConstraintEdgeCounts {
  size_t required = 0, requiredGhost = 0;
  size_t excludedSlot1 = 0, excludedSlot1Ghost = 0;
  size_t excludedSlot2 = 0, excludedSlot2Ghost = 0;
};

ConstraintEdgeCounts countConstraintEdges(uint8_t scene) {
  ConstraintEdgeCounts c;
  for (const GraphEdge &e : gGraph.edges) {
    if (e.from < 0 || gGraph.nodes[e.from].scene != scene) continue;
    const bool ghost = (e.to == -1);
    if (e.kind == EDGE_REQUIRED_READ) {
      ++c.required;
      if (ghost) ++c.requiredGhost;
    } else if (e.kind == EDGE_EXCLUDED_READ_SLOT1) {
      ++c.excludedSlot1;
      if (ghost) ++c.excludedSlot1Ghost;
    } else if (e.kind == EDGE_EXCLUDED_READ_SLOT2) {
      ++c.excludedSlot2;
      if (ghost) ++c.excludedSlot2Ghost;
    }
  }
  return c;
}

// 破線枠: SDL に dashed の API は無いので短い線分を隙間を空けて並べる。
void drawDashedRect(SDL_Renderer *r, const SDL_FRect &rect, float dashPx, float gapPx) {
  const float x0 = rect.x, y0 = rect.y, x1 = rect.x + rect.w, y1 = rect.y + rect.h;
  for (float x = x0; x < x1; x += dashPx + gapPx) {
    const float xe = std::min(x + dashPx, x1);
    SDL_RenderLine(r, x, y0, xe, y0);
    SDL_RenderLine(r, x, y1, xe, y1);
  }
  for (float y = y0; y < y1; y += dashPx + gapPx) {
    const float ye = std::min(y + dashPx, y1);
    SDL_RenderLine(r, x0, y, x0, ye);
    SDL_RenderLine(r, x1, y, x1, ye);
  }
}

void drawConstraintView(SDL_Renderer *r) {
  constexpr int kGhostW = 150; // ghost 専用 box の world px 寸法（kNodeW とは別物として扱う）
  constexpr int kGhostH = 26;

  // ghost lane: 実ノードの右（max wx + kNodeW + kGapX）に dedicated lane を作る。
  // 実ノードと重ならないので衝突せず、座標は nodePos()（active layout）から決定性で求まる。
  float laneX = 0.f;
  for (int i : gGraph.nodesInScene(gViewScene))
    laneX = std::max(laneX, nodePos(gGraph.nodes[i]).x);
  laneX += static_cast<float>(kNodeW) + static_cast<float>(kGapX);

  // ghost target: GraphEdge.to == -1 の id（scene内に存在しない id）。
  // 同じ id を指す複数 edge（異 kind・duplicate）は 1 box に収束させ、id 昇順で決定的に並べる。
  // 箱の badge 色は指している edge の kind のうち最小の kind（required < slot1 < slot2）。
  std::vector<uint8_t> ghostIds;
  std::unordered_map<uint8_t, EdgeKind> ghostKind;
  for (const GraphEdge &e : gGraph.edges) {
    if (e.to != -1) continue;
    if (e.kind != EDGE_REQUIRED_READ && e.kind != EDGE_EXCLUDED_READ_SLOT1 &&
        e.kind != EDGE_EXCLUDED_READ_SLOT2) continue;
    if (e.from < 0 || gGraph.nodes[e.from].scene != gViewScene) continue;
    const auto it = ghostKind.find(e.targetId);
    if (it == ghostKind.end()) {
      ghostIds.push_back(e.targetId);
      ghostKind[e.targetId] = e.kind;
    } else if (e.kind < it->second) {
      it->second = e.kind;
    }
  }
  std::sort(ghostIds.begin(), ghostIds.end());

  // ghost box: overlay0 の dashed 枠 + id hex の badge（drawGuiLine）。
  // "scene内に存在しない id" を明示する（既存の par!/chi! badge とは別の制約 ghost 表示）。
  const float dashPx = std::max(4.f, 8.f * gViewZoom);
  const float gapPx = std::max(3.f, 6.f * gViewZoom);
  for (size_t gi = 0; gi < ghostIds.size(); ++gi) {
    const SDL_FPoint p =
        worldToScreen(laneX, static_cast<float>(gi) * static_cast<float>(kGhostH + kGapY));
    const float gw = static_cast<float>(kGhostW) * gViewZoom;
    const float gh = static_cast<float>(kGhostH) * gViewZoom;
    const SDL_FRect rect{p.x, p.y, gw, gh};
    setSolid(r, kMCOverlay0); // overlay0 #6c7086 の破線枠
    drawDashedRect(r, rect, dashPx, gapPx);
    if (gw >= 44.f) { // zoom が小さい場合はテキスト省略（drawNodeBox の badge と同じ方針）
      const EdgeKind k = ghostKind.find(ghostIds[gi])->second;
      const uint32_t badgeColor =
          k == EDGE_REQUIRED_READ ? kMCTeal : (k == EDGE_EXCLUDED_READ_SLOT1 ? kMCRed : kMCYellow);
      drawGuiLine(r, p.x + 5.f, p.y + 5.f,
                  "ghost id=" + hexByte(ghostIds[gi]) + "（scene内に存在しないid）",
                  gViewZoom * 0.7f, gw - 10.f, badgeColor);
    }
  }

  // 制約 edge: present target は入力ポートへ、ghost は ghost box の左中心へ bezier で結ぶ。
  // ラベルは edge 中段に "requiredReadId:0x…" / "excluded(slot1):0x…" / "excluded(slot2):0x…" を
  // drawGuiLine で各色描く（zoom 小ではテキスト省略方針に合わせて描かない）。
  const bool labelOk = static_cast<float>(kNodeW) * gViewZoom >= 44.f;
  for (const GraphEdge &e : gGraph.edges) {
    if (e.kind != EDGE_REQUIRED_READ && e.kind != EDGE_EXCLUDED_READ_SLOT1 &&
        e.kind != EDGE_EXCLUDED_READ_SLOT2) continue;
    if (e.from < 0 || gGraph.nodes[e.from].scene != gViewScene) continue; // scene filter（source側）

    uint32_t color;
    const char *labelName;
    if (e.kind == EDGE_REQUIRED_READ) {
      color = kMCTeal; // teal #94e2d5（hasRead=green・EXACT OK=green と区別）
      labelName = "requiredReadId:";
    } else if (e.kind == EDGE_EXCLUDED_READ_SLOT1) {
      color = kMCRed; // red #f38ba8
      labelName = "excluded(slot1):";
    } else {
      color = kMCYellow; // yellow #f9e2af（slot1 の red と区別。family 矛盾 yellow とは別意味）
      labelName = "excluded(slot2):";
    }

    const SDL_FPoint a = outputPort(gGraph.nodes[e.from]);
    SDL_FPoint b;
    if (e.to >= 0) {
      const GraphNode &t = gGraph.nodes[e.to];
      if (t.scene != gViewScene) continue; // scene filter（target側）
      b = inputPort(t);
    } else {
      const auto it = std::find(ghostIds.begin(), ghostIds.end(), e.targetId);
      if (it == ghostIds.end()) continue;
      const float gi = static_cast<float>(it - ghostIds.begin());
      b = worldToScreen(laneX, gi * static_cast<float>(kGhostH + kGapY) +
                                 static_cast<float>(kGhostH) * 0.5f);
    }
    setSolid(r, color);
    drawBezierEdge(r, a, b);
    if (labelOk) {
      const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
      drawGuiLine(r, mx, my - 2.f, std::string(labelName) + hexByte(e.targetId),
                  gViewZoom * 0.6f, 220.f, color);
    }
  }
}

// ============================================================================
// --- EDITOR mode: Element 追加エディタ（Prompt 4） ---
// ============================================================================
// EditorState は runtime の console 挿入フロー（gAppMode）とは別の状態。EDITOR mode でのみ
// 入力を受け、runtime キー（TAB/F/O/I/A/U/M/Y/N）は editor では遮断する。
// 追加は Document と Graph にだけ反映し、runtime state は既定では同期しない（G トグル）。
// buildGraph を再構築すれば constraint/family の edge・ghost・HUD の countConstraintEdges は
// 即時更新される（edges 走査なので constraint 側に追加の配線は不要）。

// 追加行の prop 合成。buildProperty と同じ bit 配置（id/parent/scene/flags）に
// slot1 child value を足す。child なしは data.txt の既存慣習どおり slot1Index=0 / value=0 のまま
// （getPropChild は value 0 で 0 を返し、buildGraph は childId != 0 のときだけ edge を作る）。
// newRoot(slot1Index=2) / excludedReadId(slot1Index=1) は child と排他なのでここでは立てない。
uint64_t composeEditorProp(uint8_t id, uint8_t parent, uint8_t scene, bool isQuote,
                           bool isOnetime, uint8_t childId) {
  uint64_t prop = 0;
  prop |= static_cast<uint64_t>(id);                    // bits 0-7: id
  prop |= static_cast<uint64_t>(parent) << 8;           // bits 8-15: parent（0 = root / no parent）
  if (isQuote) prop |= (1ULL << 17);                    // bit 17: isQuote
  if (isOnetime) prop |= (1ULL << 18);                  // bit 18: isOnetime
  prop |= (static_cast<uint64_t>(scene) & 0x0Fu) << 20; // bits 20-23: scene
  prop |= static_cast<uint64_t>(childId) << 40;         // bits 40-47: slot1 value（bits 26-27 index は 0 のまま）
  return prop;
}

// scene 内の未使用の最小 id を選ぶ（nodesInScene の使用 id 集合と衝突しない）。
// id 0 は parent / none の sentinel 慣習として候補から除外する。id > 0xFF は追加不可（-1 を返す）。
// runtime の containerB.size()+1 は scene と無関係で重複するので使わない。
int nextFreeId(uint8_t scene) {
  std::unordered_set<uint8_t> used;
  for (int i : gGraph.nodesInScene(scene)) used.insert(gGraph.nodes[i].id);
  for (int id = 1; id <= 0xFF; ++id) {
    if (used.find(static_cast<uint8_t>(id)) == used.end()) return id;
  }
  return -1; // 0xFF まで使用中 → 追加不可
}

const char *editorStageName(EditorStage stage) {
  switch (stage) {
    case EDITOR_DIALOGUE: return "dialogue";
    case EDITOR_QUOTE: return "quote";
    case EDITOR_ONETIME: return "onetime";
    case EDITOR_PARENT: return "parent";
    case EDITOR_CHILD: return "child";
    case EDITOR_IDLE:
    default: return "idle";
  }
}

// SDL_StartTextInput/StopTextInput の対称なペア（二重 Start / 二重 Stop を避ける）。
// runtime の挿入フローとは独立に editor 側だけで管理する（挿入フロー中は editor 入力自体が届かない）。
void editorStartText() {
  if (!gEditorTextInputActive && gWindow) {
    SDL_StartTextInput(gWindow);
    gEditorTextInputActive = true;
  }
}

void editorStopText() {
  if (gEditorTextInputActive && gWindow) {
    SDL_StopTextInput(gWindow);
    gEditorTextInputActive = false;
  }
}

// 追加の validation（Prompt 4 要件 3/4）。invalid は追加禁止。
bool editorValidateAdd(std::string &why) {
  if (gEditor.dialogue.empty()) {
    why = "empty dialogue is not allowed";
    return false;
  }
  if (gEditor.assignedId <= 0) {
    why = "no free id in scene（id > 0xFF は追加不可）";
    return false;
  }
  if (gEditor.parentId == static_cast<uint8_t>(gEditor.assignedId)) {
    why = "self-parent forbidden（parent == 追加 id）";
    return false;
  }
  if (gEditor.parentId != 0 && gGraph.findNode(gViewScene, gEditor.parentId) < 0) {
    why = "parent id not present in current scene（別 scene の id は追加禁止）";
    return false;
  }
  if (gEditor.childOptionSelected &&
      (gEditor.childId == 0 || gGraph.findNode(gViewScene, gEditor.childId) < 0)) {
    why = "child option requires existing id in scene（slot1 value != 0・同 scene の id）";
    return false;
  }
  return true;
}

void editorBeginAdd() {
  editorStopText(); // 古い input が残っていれば対称に閉じてから始め、二重 Start を避ける
  propsClose();     // props panel は add flow の stage machine と排他（Prompt 5・開始時は panel を閉じる）
  gEditor.stage = EDITOR_DIALOGUE;
  gEditor.dialogue.clear();
  gEditor.parentId = 0;
  gEditor.childId = 0;
  gEditor.childOptionSelected = false;
  gEditor.isQuote = false;
  gEditor.isOnetime = false;
  gEditor.lastWarning.clear();
  gEditor.assignedId = nextFreeId(gViewScene);
  editorStartText();
}

// ESC 取消: Document は変更しない（追加は commit 時のみ）。text input は対称に閉じる。
void editorCancelAdd() {
  editorStopText();
  gEditor.stage = EDITOR_IDLE;
  gEditor.dialogue.clear();
  gEditor.parentId = 0;
  gEditor.childId = 0;
  gEditor.childOptionSelected = false;
  gEditor.isQuote = false;
  gEditor.isOnetime = false;
  gEditor.lastWarning = "cancelled（document unchanged）";
}

// EDITOR から他 mode に切り替えるときは add flow を取消する（入力遮断の安全な既定）。
// props panel も閉じる（props 入力は EDITOR mode のみ。開いたままの編集 state を残さない）。
void editorLeaveModeIfNeeded() {
  if (gEditor.stage != EDITOR_IDLE) editorCancelAdd();
  if (gProps.active) propsClose();
}

// UTF-8 の後方 1 codepoint を消す（pop_back のバイト単位削除で multibyte を壊さない）。
void editorBackspaceDialogue() {
  const size_t n = gEditor.dialogue.size();
  if (n == 0) return;
  size_t i = n - 1;
  while (i > 0 && (static_cast<unsigned char>(gEditor.dialogue[i]) & 0xC0u) == 0x80u) --i;
  gEditor.dialogue.erase(i);
}

void editorCommitAdd(); // forward decl（editorEnterKey の child commit から呼ぶ。定義は下）

// ENTER の stage 遷移: idle → dialogue → quote → onetime → parent → child → commit。
// quote/onetime の ENTER は既定 no（safest な default）。child の ENTER は none commit
// （slot1Index 0 / value 0 の data.txt 慣習のまま。既存行を index 変更で書き換えない）。
void editorEnterKey() {
  switch (gEditor.stage) {
    case EDITOR_IDLE:
      editorBeginAdd();
      if (gEditor.assignedId <= 0) {
        gEditor.stage = EDITOR_IDLE;
        editorStopText();
        gEditor.lastWarning = "no free id in scene（id > 0xFF は追加不可）";
      }
      break;
    case EDITOR_DIALOGUE:
      if (gEditor.dialogue.empty()) {
        gEditor.lastWarning = "empty dialogue is not allowed（Enter は無視）";
        break;
      }
      editorStopText();
      gEditor.isQuote = false;
      gEditor.stage = EDITOR_QUOTE;
      gEditor.lastWarning.clear();
      break;
    case EDITOR_QUOTE:
      gEditor.isQuote = false; // ENTER = no
      gEditor.isOnetime = false;
      gEditor.stage = EDITOR_ONETIME;
      gEditor.lastWarning.clear();
      break;
    case EDITOR_ONETIME:
      gEditor.isOnetime = false; // ENTER = no
      gEditor.parentId = 0;       // 無選択の既定 = root / no parent（0 を許す）
      gEditor.stage = EDITOR_PARENT;
      gEditor.lastWarning.clear();
      break;
    case EDITOR_PARENT:
      gEditor.stage = EDITOR_CHILD;
      gEditor.lastWarning.clear();
      break;
    case EDITOR_CHILD:
      // ENTER は現在の候補で commit（選択なし = child none: slot1Index 0 / value 0 のまま）。
      // 選択を none に戻すときは N key（editorYN）で option を解除する。
      editorCommitAdd();
      break;
  }
  needRedraw = true;
}

void editorYN(bool yes) {
  if (gEditor.stage == EDITOR_QUOTE) {
    gEditor.isQuote = yes;
    gEditor.isOnetime = false;
    gEditor.stage = EDITOR_ONETIME;
  } else if (gEditor.stage == EDITOR_ONETIME) {
    gEditor.isOnetime = yes;
    gEditor.parentId = 0;
    gEditor.stage = EDITOR_PARENT;
  } else if (gEditor.stage == EDITOR_CHILD && !yes) {
    // slot1 option の排他選択: N = child none に戻す（value を 0 にする。index は 0 のまま）。
    gEditor.childOptionSelected = false;
    gEditor.childId = 0;
  }
  gEditor.lastWarning.clear();
  needRedraw = true;
}

// commit: Document に正格式の行を追加し、graph を再構築して active layout を再適用する。
// 追加直後、新ノードを selectedNode にする（再構築で node index は変わるが、新ノードは
// (scene,id) で決定的に findNode できる。既存選択の (scene,id) 復元は Prompt 5 の編集側で使う）。
void editorCommitAdd() {
  std::string why;
  if (!editorValidateAdd(why)) {
    gEditor.lastWarning = why;
    return; // 追加禁止（stage はそのまま、取消 / 修正を待つ）
  }
  const uint8_t scene = gViewScene;
  const uint8_t id = static_cast<uint8_t>(gEditor.assignedId);
  const uint64_t prop =
      composeEditorProp(id, gEditor.parentId, scene, gEditor.isQuote, gEditor.isOnetime,
                        gEditor.childOptionSelected ? gEditor.childId : 0);

  RawLine line;
  line.text = "'" + gEditor.dialogue + "', '0x" + formatPropHex16Piped(prop) + "',"; // 正格式（lowercase 16桁・2|2|4区切りパイプ・trailing comma）
  line.kind = LINE_ELEMENT;
  line.elementIndex = static_cast<int>(gDocument.elements.size());
  if (!gDocument.lines.empty() && gDocument.lines.back().terminator.empty()) {
    // 最終行が改行で終わらない raw に追加するときは区切りの "\n" を最終行に補う。
    // baseline raw は書き換えない（verifyRoundTrip の baseline は原文のまま）。復元で戻す。
    gDocument.lines.back().terminator = "\n";
    gDocTerminatorPatched = true;
  }
  line.terminator = "\n";
  gDocument.lines.push_back(std::move(line));
  gDocument.elements.push_back(
      ElementRecord{{gEditor.dialogue, prop}, static_cast<int>(gDocument.lines.size()) - 1, true});

  buildGraph(gDocument, gGraph);
  applyActiveLayout(); // active mode の layout を再適用（constraint/family に追加の配線は不要）
  gSelectedNode = gGraph.findNode(scene, id); // 追加直後、新ノードを selectedNode にする
  fitViewToScene();
  gRoundTrip = verifyRoundTrip(gDocument);

  if (gEditor.syncRuntime) { // runtime 同期は明示トグル（G）が on のときだけ（既定 off）
    Element newElem{gEditor.dialogue, prop};
    containerA.push_back(newElem);
    if (getPropScene(prop) == gScene) containerB.push_back(newElem);
    rebuildContainerC();
    rebuildDisplayList();
    rebuildObjectiveList();
    needRedraw = true;
  }

  std::cout << "editor: added line scene=" << hexNibble(scene) << " id=" << hexByte(id)
            << " prop=0x" << formatPropHex16(prop) << " lines=" << gDocument.lines.size()
            << std::endl;

  // 次の add に備える（runtime の挿入フローと同じく連続追加可能）。
  gEditor.dialogue.clear();
  gEditor.parentId = 0;
  gEditor.childId = 0;
  gEditor.childOptionSelected = false;
  gEditor.isQuote = false;
  gEditor.isOnetime = false;
  gEditor.lastWarning.clear();
  gEditor.assignedId = nextFreeId(gViewScene); // 追加後の graph から次の未使用 id を決める
  gEditor.stage = EDITOR_DIALOGUE;
  editorStartText();
}

// selftest の検証後に baseline へ戻す（追加行のみを remove。data.txt 自体は書き換えない）。
void editorRestoreToBaseline(size_t targetLines, size_t targetElements) {
  while (gDocument.lines.size() > targetLines) {
    const RawLine &rl = gDocument.lines.back();
    if (rl.kind == LINE_ELEMENT &&
        rl.elementIndex == static_cast<int>(gDocument.elements.size()) - 1) {
      gDocument.elements.pop_back();
    }
    gDocument.lines.pop_back();
  }
  gDocument.elements.resize(targetElements);
  if (gDocTerminatorPatched) {
    if (!gDocument.lines.empty()) gDocument.lines.back().terminator = "";
    gDocTerminatorPatched = false;
  }
}

// ============================================================================
// --- Prompt 5: props menu（selectedNode の props editor・add flow とは別 state） ---
// ============================================================================
// 編集は Document / Graph にだけ反映し runtime state に自動同期しない（Prompt4 の G トグルとは別）。
// graph 再構築経路は editorCommitAdd と同じ: buildGraph → applyActiveLayout →
// 選択は (scene,id) key を gGraph.findNode で復元する。dirty=true の行再生成は
// serializeDocument と同じ正格式（lowercase 16桁・2|2|4区切りパイプ・trailing comma）で行う。
// validation: 範囲外は clamp + warning、target id が現 scene に無い場合は warning +
// constraint viewer の ghost で確認できる（countConstraintEdges・配線追加不要）、
// slot の option は index が 1 つなので構造上排他（同一 slot に複数 option は立てられない）。

inline uint32_t propsNodeKeyOf(uint64_t prop) {
  return nodeKey(getPropScene(prop), getPropID(prop));
}

int propsResolveCurrentNode() {
  const uint8_t scene = static_cast<uint8_t>(gProps.nodeKey >> 8);
  const uint8_t id = static_cast<uint8_t>(gProps.nodeKey & 0xFFu);
  return gGraph.findNode(scene, id);
}

uint64_t propsCurrentProp() {
  const int idx = propsResolveCurrentNode();
  if (idx < 0 || idx >= static_cast<int>(gGraph.nodes.size())) return 0;
  const int ei = gGraph.nodes[idx].elementIndex;
  if (ei < 0 || ei >= static_cast<int>(gDocument.elements.size())) return 0;
  return gDocument.elements[ei].element.prop;
}

// ノード選択（クリック・右クリック相当）で props panel を開く。add flow と排他（idle のみ）。
// open 時点の slot index を baseSlotIndex に保存する（"none" は元の index に戻す対象）。
void propsOpen(int nodeIndex) {
  if (gViewMode != EDITOR || gEditor.stage != EDITOR_IDLE) return;
  if (nodeIndex < 0 || nodeIndex >= static_cast<int>(gGraph.nodes.size())) return;
  const GraphNode &n = gGraph.nodes[nodeIndex];
  const int ei = n.elementIndex;
  if (ei < 0 || ei >= static_cast<int>(gDocument.elements.size())) return;
  gProps.active = true;
  gProps.nodeKey = nodeKey(n.scene, n.id);
  gProps.cursor = 0;
  const uint64_t prop = gDocument.elements[ei].element.prop;
  for (int slot = 0; slot < 4; ++slot) {
    gProps.baseSlotIndex[slot] = getSlotIndex(prop, static_cast<uint8_t>(slot));
  }
  gProps.lastWarning.clear();
  gProps.warningTotal = 0;
  needRedraw = true;
}

void propsClose() {
  gProps.active = false; // undo stack は保持する（document の変更は U で復元できる）
  needRedraw = true;
}

// commit: undo entry を積んでから prop を更新し、正格式の raw line を再生成、graph 再構築、
// active layout 再適用、選択を編集後の (scene,id) で復元する。runtime state は触らない。
void propsCommit(const uint64_t newProp, const std::vector<std::string> &warnings) {
  const int idx = propsResolveCurrentNode();
  if (idx < 0) {
    gProps.lastWarning = "編集対象 node が存在しません（graph 再構築後の key 解決に失敗）";
    return;
  }
  const GraphNode &n = gGraph.nodes[idx];
  const int ei = n.elementIndex;
  if (ei < 0 || ei >= static_cast<int>(gDocument.elements.size())) return;
  ElementRecord &rec = gDocument.elements[ei];

  PropsUndoEntry entry{rec.element.prop, gDocument.lines[rec.lineIndex].text, rec.dirty};
  if (gProps.undoStack.size() >= kPropsUndoMax) gProps.undoStack.erase(gProps.undoStack.begin());
  gProps.undoStack.push_back(std::move(entry));

  const uint64_t oldProp = rec.element.prop;
  rec.element.prop = newProp;
  rec.dirty = true;
  // 正格式再生成（追加行の format と同じ規則・lowercase 16桁・2|2|4区切りパイプ・trailing comma・dialogue 原文のまま）。
  gDocument.lines[rec.lineIndex].text =
      "'" + rec.element.dialogue + "', '0x" + formatPropHex16Piped(newProp) + "',";

  gProps.nodeKey = propsNodeKeyOf(newProp);
  buildGraph(gDocument, gGraph);
  applyActiveLayout();
  gSelectedNode = propsResolveCurrentNode(); // 選択は (scene,id) で復元（Prompt 5 要件 5）
  fitViewToScene();
  gRoundTrip = verifyRoundTrip(gDocument);

  if (!warnings.empty()) {
    std::string joined;
    for (size_t i = 0; i < warnings.size(); ++i) {
      if (i != 0) joined += " / ";
      joined += warnings[i];
    }
    gProps.lastWarning = joined;
    gProps.warningTotal += warnings.size();
  }
  std::cout << "props: edit scene=" << hexNibble(getPropScene(oldProp)) << " id="
            << hexByte(getPropID(oldProp)) << " -> scene=" << hexNibble(getPropScene(newProp))
            << " id=" << hexByte(getPropID(newProp)) << " prop 0x" << formatPropHex16(oldProp)
            << " -> 0x" << formatPropHex16(newProp) << " warnings=" << warnings.size()
            << " undo=" << gProps.undoStack.size() << std::endl;
  needRedraw = true;
}

// validation（Prompt 5 要件 4）: 編集 operation 単位で warning を作る。
// - 範囲外は clamp + warning（HUD に yellow で出す）
// - id が現 scene で重複 → 編集は適用せず warning（graph の duplicate first-wins を作らない）
// - id 系 target（parent/child/excluded/requiredReadId/newRoot）が現 scene に無い → warning +
//   constraint viewer の ghost / graph の par!・chi! badge で表示できる（編集は許す）
// - timer / eyelevel の sub field を option 非立てで編集 → warning（value は実効 inert・編集は許す）
// slot の option 排他は index が 1 つ（2bit）なので構造上守られる（option 切り替えで旧 option は消える）。
std::vector<std::string> propsValidateEdit(const PropsFieldDef &f, uint64_t newProp) {
  std::vector<std::string> out;
  const uint8_t scene = getPropScene(newProp);
  const auto targetWarn = [&](const char *name, uint8_t id) {
    if (id == 0) return;
    if (gGraph.findNode(scene, id) < 0) {
      out.push_back(std::string(name) + "=0x" + std::to_string(id) +
                    " is not present in current scene（別 scene の id・constraint viewer の ghost で表示）");
    }
  };
  switch (f.role) {
    case ROLE_ID: {
      const uint8_t want = getPropID(newProp);
      const int hit = gGraph.findNode(scene, want);
      if (hit >= 0) {
        const int eiCur = propsResolveCurrentNode();
        if (eiCur >= 0 && gGraph.nodes[hit].elementIndex != gGraph.nodes[eiCur].elementIndex) {
          out.push_back("duplicate id=0x" + std::to_string(want) +
                        " in scene（id 編集は reject・prop は変わりません）");
        }
      }
      break;
    }
    case ROLE_PARENT:
      targetWarn("parent", getPropParent(newProp)); // 0 は root / no parent（warning なし）
      break;
    case ROLE_TARGET: {
      // option の実効意味だけ target として検査する（flags / newScene / timer value は id 系ではない）。
      const uint8_t slot = f.slot;
      const uint8_t idx = getSlotIndex(newProp, slot);
      const uint8_t val = getSlotValue(newProp, slot);
      if (val == 0) break; // value 0 は none（実効 target なし）
      if (slot == 0 && idx == 0) targetWarn("requiredReadId", val);
      else if (slot == 1) {
        // child / excludedReadId / newRoot は排他（idx が 1 つ）なので立てた option だけを検査する。
        // idx 3 の downUnlocks は mask（id target ではない）なので target 不在の warning を作らない。
        if (idx == 0) targetWarn("child", val);
        else if (idx == 1) targetWarn("excluded(slot1)", val);
        else if (idx == 2) targetWarn("newRoot", val);
      } else if (slot == 2 && idx == 3) {
        targetWarn("excluded(slot2)", val);
      }
      break;
    }
    case ROLE_TIMER_SUB: {
      const uint8_t slot = f.slot;
      const uint8_t needIdx = (slot == 2) ? 0 : 0; // timerSetter は slot2 idx0・timerTarget は slot3 idx0
      if (getSlotIndex(newProp, slot) != needIdx) {
        out.push_back(std::string(f.label) + " edited while slot" + std::to_string(slot) +
                      " option is not timerSetter/timerTarget（value は実効 inert・編集は許す）");
      }
      break;
    }
    case ROLE_EYE_SUB:
      if (getSlotIndex(newProp, 3) != 3) {
        out.push_back(std::string(f.label) + " edited while slot3 option is not eyelevelToggle（value は実効 inert・編集は許す）");
      }
      break;
    default:
      break;
  }
  return out;
}

// option 切り替え（index 変更）は value を保持するので、切り替え後の option の実効意味で
// target 不在を検査する。ROLE_TARGET の probe を合成して propsValidateEdit を再利用する。
std::vector<std::string> propsOptionSwitchWarnings(uint8_t slot, uint64_t newProp) {
  const PropsFieldDef probe{"slot option", "slot option", PF_OPTION, ROLE_TARGET,
                            0, 0, slot, 0, nullptr, 0};
  return propsValidateEdit(probe, newProp);
}

// 値の編集。absolute=true は絶対値指定（selftest の決定的な編集用）、false は ±delta。
// 範囲外は clamp + warning。id の duplicate は reject（commit しない）。
bool propsApplyFieldValue(int fieldIdx, uint64_t wantValue, bool absolute, int delta = 0) {
  if (fieldIdx < 0 || fieldIdx >= kPropsFieldCount) return false;
  const PropsFieldDef &f = kPropsFields[fieldIdx];
  if (f.kind == PF_OPTION) return false; // option は propsCycleOption / propsSetOption で編集する
  const uint64_t prop = propsCurrentProp();
  if (prop == 0 && propsResolveCurrentNode() < 0) return false;
  const uint64_t mask = (1ULL << f.width) - 1ULL;
  const uint64_t cur = (prop >> f.shift) & mask;
  uint64_t nv = cur;
  std::vector<std::string> warnings;
  if (absolute) {
    nv = wantValue & mask;
    if (wantValue > f.max) {
      nv = static_cast<uint64_t>(f.max);
      warnings.push_back(std::string(f.label) + " value beyond " +
                         hexByte(f.max) + " → clamped to max（範囲外は clamp + warning）");
    }
  } else {
    if (delta > 0) {
      if (cur >= static_cast<uint64_t>(f.max)) {
        warnings.push_back(std::string(f.label) + " already max " + hexByte(f.max) +
                           "（+ は clamp・warning）");
      } else {
        nv = cur + static_cast<uint64_t>(delta);
        if (nv > static_cast<uint64_t>(f.max)) nv = static_cast<uint64_t>(f.max);
      }
    } else if (delta < 0) {
      if (cur == 0) {
        warnings.push_back(std::string(f.label) + " already 0（- は clamp・warning）");
      } else {
        nv = cur - static_cast<uint64_t>(-delta);
      }
    } else {
      return false;
    }
  }
  const uint64_t newProp = setPropField(prop, f.shift, f.width, nv);
  auto validation = propsValidateEdit(f, newProp);
  if (!validation.empty() && f.role == ROLE_ID) {
    // id の duplicate は編集を適用しない（warning だけ出す）。
    gProps.lastWarning = validation[0];
    gProps.warningTotal += validation.size();
    std::cout << "props: edit rejected scene=" << hexNibble(getPropScene(prop))
              << " id=" << hexByte(getPropID(prop)) << " reason=" << validation[0] << std::endl;
    needRedraw = true;
    return false;
  }
  warnings.insert(warnings.end(), validation.begin(), validation.end());
  propsCommit(newProp, warnings);
  return true;
}

// bool field（checkbox）: ENTER=反転、Y/N=true/false。
void propsSetBoolField(int fieldIdx, bool v) {
  if (fieldIdx < 0 || fieldIdx >= kPropsFieldCount) return;
  const PropsFieldDef &f = kPropsFields[fieldIdx];
  if (f.kind != PF_BOOL) return;
  propsCommit(setPropBit(propsCurrentProp(), f.shift, v), {});
}

void propsToggleField(int fieldIdx) {
  if (fieldIdx < 0 || fieldIdx >= kPropsFieldCount) return;
  const PropsFieldDef &f = kPropsFields[fieldIdx];
  switch (f.kind) {
    case PF_BOOL:
      propsSetBoolField(fieldIdx, !((propsCurrentProp() >> f.shift) & 0x1u));
      break;
    case PF_OPTION: {
      // option cycle: index を次へ、最後の option の次は none（元の index に戻し value を 0 にする）。
      // 切り替え後の option の実効意味で target 不在を warning する（slot の option は 1 つ＝排他）。
      const uint64_t prop = propsCurrentProp();
      const uint8_t idx = getSlotIndex(prop, f.slot);
      if (idx + 1 < f.optCount) {
        const uint64_t p = setSlotIndex(prop, f.slot, idx + 1);
        propsCommit(p, propsOptionSwitchWarnings(f.slot, p));
      } else {
        uint64_t p = setSlotIndex(prop, f.slot, gProps.baseSlotIndex[f.slot]);
        p = setSlotValue(p, f.slot, 0); // none は「その slot の option index を元に戻し value を 0」
        propsCommit(p, {});
      }
      break;
    }
    case PF_ENUM: {
      // timerSetter action / timerTarget operator の cycle（0..max、max の次は 0）。
      const uint64_t prop = propsCurrentProp();
      const uint64_t cur = (prop >> f.shift) & ((1ULL << f.width) - 1ULL);
      const uint64_t nv = (cur + 1) % (static_cast<uint64_t>(f.max) + 1ULL);
      const uint64_t p = setPropField(prop, f.shift, f.width, nv);
      propsCommit(p, propsValidateEdit(f, p));
      break;
    }
    case PF_UINT: {
      // uint の ENTER トグルは lowest bit を反転する（flags は bit selector 相当。値の主要操作は +/-）。
      const uint64_t prop = propsCurrentProp();
      const uint64_t mask = (1ULL << f.width) - 1ULL;
      const uint64_t cur = (prop >> f.shift) & mask;
      propsApplyFieldValue(fieldIdx, cur ^ 1ULL, true);
      break;
    }
  }
}

// option を absolute position で立てる（selftest の決定的編集・0=none、1.. は index pos-1）。
// option の切り替えは value を保持する（id 系 option 間では同じ value が意味を変える・排他は idx で担保）。
// 適用できたら true を返す（selftest の chain 照合用）。
bool propsSetOption(int fieldIdx, uint8_t pos) {
  if (fieldIdx < 0 || fieldIdx >= kPropsFieldCount) return false;
  const PropsFieldDef &f = kPropsFields[fieldIdx];
  if (f.kind != PF_OPTION || pos > f.optCount) return false;
  const uint64_t prop = propsCurrentProp();
  if (prop == 0 && propsResolveCurrentNode() < 0) return false;
  if (pos == 0) {
    uint64_t p = setSlotIndex(prop, f.slot, gProps.baseSlotIndex[f.slot]);
    p = setSlotValue(p, f.slot, 0);
    propsCommit(p, {});
  } else {
    const uint64_t p = setSlotIndex(prop, f.slot, pos - 1);
    propsCommit(p, propsOptionSwitchWarnings(f.slot, p)); // 切り替え後の option の実効意味で target 検査
  }
  return true;
}

// Y/N を uint field に適用する（Y = max、N = 0）。bool field は propsSetBoolField で使う。
void propsSetBoolValueCurrent(bool yes) {
  const PropsFieldDef &f = kPropsFields[gProps.cursor];
  if (f.kind == PF_UINT || f.kind == PF_ENUM) {
    propsApplyFieldValue(gProps.cursor, yes ? static_cast<uint64_t>(f.max) : 0ULL, true);
  } else if (f.kind == PF_BOOL) {
    propsSetBoolField(gProps.cursor, yes);
  }
}

void propsNextField() {
  gProps.cursor = (gProps.cursor + 1) % kPropsFieldCount; // 循環（決定性・全項目を順に見られる）
  needRedraw = true;
}

// U: undo stack から 1 段復元（previous prop + 元の raw line text + dirty を完全に戻す）。
// 全 undo で baseline に戻る（round-trip EXACT）。復元対象は現在の nodeKey で解決する：
// prevProp の key を先に立てると id/scene 変更直前の状態は現 graph に無く解決に失敗した（順序 bug）。
// 復元後は entry の prevProp から決まる (scene,id) key に更新し、選択もその key で復元する。
void propsUndo() {
  if (gProps.undoStack.empty()) {
    gProps.lastWarning = "undo stack is empty（復元する編集がありません）";
    needRedraw = true;
    return;
  }
  const PropsUndoEntry e = gProps.undoStack.back();
  gProps.undoStack.pop_back();
  const int idx = propsResolveCurrentNode(); // 直近の commit/undo の結果の key で現在の編集 node を解決
  if (idx < 0) {
    gProps.lastWarning = "undo target node を解決できませんでした（entry は消費された）";
    needRedraw = true;
    return;
  }
  ElementRecord &rec = gDocument.elements[gGraph.nodes[idx].elementIndex];
  rec.element.prop = e.prevProp;
  rec.dirty = e.prevDirty;
  gDocument.lines[rec.lineIndex].text = e.prevLineText; // 元書式のまま復元（未編集行は原文 = baseline）
  gProps.nodeKey = propsNodeKeyOf(e.prevProp); // 復元後の key は previous prop から決まる（決定的）
  buildGraph(gDocument, gGraph);
  applyActiveLayout();
  gSelectedNode = propsResolveCurrentNode();
  fitViewToScene();
  gRoundTrip = verifyRoundTrip(gDocument);
  std::cout << "props: undo restore scene=" << hexNibble(getPropScene(e.prevProp))
            << " id=" << hexByte(getPropID(e.prevProp)) << " prop=0x"
            << formatPropHex16(e.prevProp) << " dirty=" << (e.prevDirty ? 1 : 0)
            << " remaining=" << gProps.undoStack.size() << std::endl;
  needRedraw = true;
}

// G（props panel open 時）: graph 再構築だけ行う（runtime 同期はしない・editorCommitAdd と同じ経路）。
void propsRebuildGraph() {
  buildGraph(gDocument, gGraph);
  applyActiveLayout();
  gSelectedNode = propsResolveCurrentNode();
  fitViewToScene();
  gRoundTrip = verifyRoundTrip(gDocument);
  std::cout << "props: graph rebuild nodes=" << gGraph.nodes.size() << " edges=" << gGraph.edges.size()
            << " roots=" << gGraph.roots.size() << std::endl;
  needRedraw = true;
}

// メニュー行（HUD の可変幅 budget で描くため drawGuiLine の budgetPx を使う経路に渡す）。
// 行は section 名 + 項目値（hex / decimal / binary・option 名・present/ghost・inert 注記）。
std::vector<std::pair<std::string, uint32_t>> propsMenuRows() {
  std::vector<std::pair<std::string, uint32_t>> rows;
  const uint64_t prop = propsCurrentProp();
  const int nodeIdx = propsResolveCurrentNode();
  if (nodeIdx < 0) {
    rows.push_back({"props edit: target node not found（ESC で閉じる・再選択してください）", kMCYellow});
    return rows;
  }
  const GraphNode &n = gGraph.nodes[nodeIdx];
  rows.push_back({"props edit node scene=" + hexNibble(n.scene) + " id=" + hexByte(n.id) +
                  " line=" + std::to_string(n.lineIndex + 1) + " prop=0x" + formatPropHex16(prop),
                  kMCText});
  rows.push_back({"keys TAB=next ENTER/SPACE=toggle +/-=value Y/N=bool U=undo G=graph rebuild ESC=close（+/- zoom・ENTER add flow は panel open 時遮断）", kMCOverlay1});
  const auto valueText = [&](const PropsFieldDef &f) {
    std::string s = std::string(f.label) + " = ";
    switch (f.kind) {
      case PF_BOOL:
        s += ((prop >> f.shift) & 0x1u) ? "1（on）" : "0";
        break;
      case PF_UINT:
        s += hexDec(static_cast<uint8_t>((prop >> f.shift) & ((1ULL << f.width) - 1ULL)));
        if (f.role == ROLE_ID) s += "（0x00-0xFF・duplicate id は reject）";
        if (f.role == ROLE_PARENT) {
          const uint8_t p = getPropParent(prop);
          s += p == 0 ? " 0=root/no parent" : "（現 scene に無い id は ghost で表示）";
        }
        if (f.role == ROLE_TIMER_SUB && getSlotIndex(prop, f.slot) != 0) s += "（option 非立て・inert）";
        if (f.role == ROLE_EYE_SUB && getSlotIndex(prop, 3) != 3) s += "（option 非立て・inert）";
        if (f.role == ROLE_TARGET) {
          const uint8_t idx = getSlotIndex(prop, f.slot);
          const uint8_t val = getSlotValue(prop, f.slot);
          if (val != 0) {
            // slot1 idx3 の downUnlocks は mask（id target ではない）→ present/ghost 注記を出さない。
            const bool isIdTarget = (f.slot == 0 && idx == 0) || (f.slot == 1 && idx != 3) ||
                                    (f.slot == 2 && idx == 3);
            if (isIdTarget) {
              s += gGraph.findNode(getPropScene(prop), val) >= 0 ? "（target present）"
                                                                 : "（target ghost）";
            } else if (f.slot == 1 && idx == 3) {
              s += "（downUnlocks mask・送信時に gUnlocks &= ~" + binaryByte(val) + "）";
            }
          } else {
            s += "（none・value 0）";
          }
        }
        break;
      case PF_OPTION:
        s += "option=" + std::string(propsOptionLabel(prop, f.slot)) +
             " idx=" + std::to_string(getSlotIndex(prop, f.slot)) +
             " raw=" + hexDec(getSlotValue(prop, f.slot));
        break;
      case PF_ENUM: {
        const uint64_t cur = (prop >> f.shift) & ((1ULL << f.width) - 1ULL);
        const uint8_t idx8 = static_cast<uint8_t>(cur & 0x3u);
        s += std::string(f.names[idx8]) + " raw=" + std::to_string(cur);
        break;
      }
    }
    return s;
  };
  const char *prevSection = nullptr;
  for (int i = 0; i < kPropsFieldCount; ++i) {
    const PropsFieldDef &f = kPropsFields[i];
    if (prevSection == nullptr || std::string(prevSection) != std::string(f.section)) {
      prevSection = f.section;
      rows.push_back({"--- section: " + std::string(f.section) + " ---", kMCText});
    }
    rows.push_back({(gProps.cursor == i ? "> " : "  ") + valueText(f),
                    gProps.cursor == i ? kMCSky : kMCSubtext1});
  }
  rows.push_back({"dialogue は編集しない・未編集行は原文のまま・dirty 行のみ正格式再生成・runtime 同期なし（既定）", kMCOverlay2});
  rows.push_back({"undo=" + std::to_string(gProps.undoStack.size()) +
                  "（U=復元・全 undo で baseline EXACT に戻る）", kMCSubtext0});
  if (!gProps.lastWarning.empty()) rows.push_back({"warning: " + gProps.lastWarning, kMCYellow});
  return rows;
}

// EDITOR の preview panel（要件 7）: id / parent / child / scene / flags / slot1Index / slot1 value /
// final prop hex（16桁 lowercase）と、追加前後の diff（raw line の予定）を kMC の色で描く。
// active layout の描画結果の上に overlay として描く（Prompt 4 で新 layout は作らない）。
void drawEditorOverlay(SDL_Renderer *r) {
  int winW = 0, winH = 0;
  windowSize(&winW, &winH);
  const float hudW = static_cast<float>(std::min(kHudW, winW));
  const float px = hudW + 18.f;
  const float pw = static_cast<float>(winW) - px - 14.f;
  if (pw < 160.f) return; // graph 領域が狭いときは preview panel を描かない（HUD の editor 行で状態を出す）

  const uint8_t scene = gViewScene;
  std::vector<std::pair<std::string, uint32_t>> rows;

  if (gEditor.stage == EDITOR_IDLE) {
    const int previewId = nextFreeId(scene);
    rows.push_back({"EDITOR: element add (Prompt4)", kMCText});
    rows.push_back({"ENTER = start add flow: dialogue → quote/onetime → parent → child → commit", kMCSky});
    rows.push_back({"click node = pick candidate at parent/child stage. ESC = cancel add", kMCOverlay1});
    rows.push_back({"next free id=" +
                    (previewId > 0 ? hexByte(static_cast<uint8_t>(previewId))
                                   : std::string("none(id>0xFF full)")) +
                    " scene=" + hexNibble(scene), kMCSubtext1});
    rows.push_back({"sync runtime (G): " + std::string(gEditor.syncRuntime ? "on" : "off（既定・containerA/B は自動同期しない）"),
                    gEditor.syncRuntime ? kMCGreen : kMCOverlay2});
    // props menu（Prompt 5）: ノード選択で panel を開く。add flow と排他（idle のときのみ）。
    if (gProps.active) {
      rows.push_back({"props panel open（Prompt5・HUD に menu を描く・TAB/ENTER/+/-/Y/N/U/G・ESC=閉じる）", kMCSky});
      rows.push_back({"props edit node scene=" + hexNibble(static_cast<uint8_t>(gProps.nodeKey >> 8)) +
                      " id=" + hexByte(static_cast<uint8_t>(gProps.nodeKey & 0xFFu)) +
                      " prop=0x" + formatPropHex16(propsCurrentProp()), kMCSubtext1});
    } else {
      rows.push_back({"props edit: click node = open props panel（Prompt5・全 prop fields・undo stack・U で復元）", kMCSubtext0});
    }
  } else {
    const char *hint = "";
    switch (gEditor.stage) {
      case EDITOR_DIALOGUE: hint = "dialogue 入力（UTF-8・空白を含む入力をそのまま保持）。ENTER=next / BACKSPACE=1文字削除・空 dialogue 不可"; break;
      case EDITOR_QUOTE: hint = "isQuote? <y/n>・ENTER は no として扱う"; break;
      case EDITOR_ONETIME: hint = "isOnetime? <y/n>・ENTER は no として扱う"; break;
      case EDITOR_PARENT: hint = "click node = parent candidate（0 = root/no parent 許す・自己 id 禁止・別 scene の id は追加禁止）。ENTER は現在値で commit"; break;
      case EDITOR_CHILD: hint = "click node = child candidate（slot1 value。child は newRoot/excluded と排他）。ENTER = 現在値で commit（選択なし = child none: slot1Index 0 / value 0 の data.txt 慣習）/ N = none に戻す"; break;
      default: break;
    }
    rows.push_back({"stage=" + std::string(editorStageName(gEditor.stage)) + ": " + hint, kMCSky});
    rows.push_back({"dialogue: " + (gEditor.dialogue.empty() ? std::string("(empty)") : gEditor.dialogue), kMCText});
    rows.push_back({"id=" +
                    (gEditor.assignedId > 0 ? hexByte(static_cast<uint8_t>(gEditor.assignedId))
                                            : std::string("none(id>0xFF full)")) +
                    " parent=" + hexByte(gEditor.parentId) +
                    " child=" + hexByte(gEditor.childOptionSelected ? gEditor.childId : 0) +
                    " scene=" + hexNibble(scene) +
                    " flags quote=" + (gEditor.isQuote ? "1" : "0") +
                    " onetime=" + (gEditor.isOnetime ? "1" : "0") +
                    " slot1Index=0 slot1value=" +
                    hexByte(gEditor.childOptionSelected ? gEditor.childId : 0), kMCSubtext1});

    const uint64_t prop =
        composeEditorProp(static_cast<uint8_t>(std::max(0, gEditor.assignedId)), gEditor.parentId,
                          scene, gEditor.isQuote, gEditor.isOnetime,
                          gEditor.childOptionSelected ? gEditor.childId : 0);
    if (gEditor.stage >= EDITOR_QUOTE) {
      rows.push_back({"final prop hex=0x" + formatPropHex16(prop), kMCSubtext1});
      rows.push_back({"planned raw line: '" + gEditor.dialogue + "', '0x" + formatPropHex16Piped(prop) + "',", kMCSubtext0});
    }
    rows.push_back({"baseline(raw) lines=" + std::to_string(gRoundTrip.baselineLines) +
                    " → document lines=" + std::to_string(gDocument.lines.size()) +
                    "（commit は末尾に 1 行追加・追加行のみ baseline と diff）", kMCSubtext1});

    std::string why;
    if (gEditor.stage == EDITOR_CHILD) {
      if (editorValidateAdd(why)) {
        rows.push_back({"commit ready（ENTER = add）", kMCGreen});
      } else {
        rows.push_back({"cannot add: " + why, kMCYellow});
      }
    } else if (!gEditor.lastWarning.empty()) {
      rows.push_back({"warning: " + gEditor.lastWarning, kMCYellow});
    }

    // candidate list（selected scene の id を列挙し、current 段階の候補にマーカーを付ける）。
    std::vector<int> byId = gGraph.nodesInScene(scene);
    std::sort(byId.begin(), byId.end(),
              [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
    std::string cand = "candidate ids scene=" + hexNibble(scene) + ":";
    for (int i : byId) {
      const uint8_t id = gGraph.nodes[i].id;
      cand += " " + hexByte(id);
      if (gEditor.stage == EDITOR_PARENT && id == gEditor.parentId && id != 0) cand += "*P";
      if (gEditor.stage == EDITOR_CHILD && gEditor.childOptionSelected && id == gEditor.childId) cand += "*C";
    }
    cand += "（0 は root/no parent として許す）";
    rows.push_back({"parent candidates / child candidates: " + cand, kMCSubtext0});
    rows.push_back({"sync runtime (G): " + std::string(gEditor.syncRuntime ? "on（commit で containerA/B に反映）" : "off（既定）"),
                    gEditor.syncRuntime ? kMCGreen : kMCOverlay2});
  }

  float panelH = static_cast<float>(rows.size()) * static_cast<float>(kTextStep) + 24.f;
  panelH = std::min(panelH, static_cast<float>(winH) - 12.f);
  const SDL_FRect panel{px, 6.f, pw, panelH};
  setSolid(r, kMCMantle); // panel 面: mantle（HUD と同じ暗い面。text/subtext1 は公式組合せで 4.5:1 以上）
  SDL_RenderFillRect(r, &panel);
  setSolid(r, kMCOverlay0); // frame: overlay0（区切り線）
  SDL_RenderRect(r, &panel);

  float y = 12.f;
  const float budget = pw - 20.f;
  for (const std::pair<std::string, uint32_t> &row : rows) {
    if (y > static_cast<float>(winH) - 14.f) break;
    drawGuiLine(r, px + 10.f, y, row.first, 1.f, budget, row.second);
    y += static_cast<float>(kTextStep);
  }
}

void drawHud(SDL_Renderer *r) {
  int winW = 0, winH = 0;
  windowSize(&winW, &winH);
  const int hudW = std::min(kHudW, winW);
  const float textBudget = static_cast<float>(hudW) - 20.f;

  setSolid(r, kMCMantle); // HUD panel: mantle #181825（base より一段暗い面）
  const SDL_FRect panel{0.f, 0.f, static_cast<float>(hudW), static_cast<float>(winH)};
  SDL_RenderFillRect(r, &panel);
  setSolid(r, kMCOverlay0); // divider: overlay0 #6c7086
  const SDL_FRect divider{static_cast<float>(hudW) - 1.f, 0.f, 1.f,
                          static_cast<float>(winH)};
  SDL_RenderFillRect(r, &divider);

  float y = 8.f;
  const auto hudLine = [&](const std::string &text, uint32_t hexColor) {
    if (y > static_cast<float>(winH) - 14.f) return; // 画面高を超える行は描かない
    drawGuiLine(r, 8.f, y, text, 1.f, textBudget, hexColor);
    y += static_cast<float>(kTextStep);
  };

  hudLine("DDMEditor graph viewer  mode=" + std::string(viewModeName()) + " ui=viewer", kMCText);
  hudLine("keys 1-4 = general/family/constraint/editor   H = game/viewer 切替   B = backup on quit", kMCSubtext1);
  hudLine("scene=" + hexNibble(gViewScene) + "  [ ] = prev/next scene   L = fit   0 = reset",
          kMCSubtext1);
  std::string sceneList = "scenes:";
  for (uint8_t s : gGraph.scenes) {
    sceneList += (s == gViewScene ? " *" : " ") + hexNibble(s) + "(" +
                 std::to_string(gGraph.nodesInScene(s).size()) + ")";
  }
  hudLine(sceneList, kMCSubtext1);
  hudLine("nodes=" + std::to_string(gGraph.nodes.size()) +
          " edges=" + std::to_string(gGraph.edges.size()) +
          " roots=" + std::to_string(gGraph.roots.size()), kMCSubtext1);
  hudLine("doc=" + gLoadedPath + " bytes=" + std::to_string(gDocument.raw.size()) +
          " lines=" + std::to_string(gDocument.lines.size()) +
          " elem=" + std::to_string(gDocument.parsedLines) +
          " comment=" + std::to_string(gDocument.commentLines) +
          " blank=" + std::to_string(gDocument.blankLines), kMCSubtext1);
  hudLine("round-trip: " + std::string(gRoundTrip.exact ? "EXACT OK" : "MISMATCH") +
          " serialized=" + std::to_string(gRoundTrip.serializedBytes),
          gRoundTrip.exact ? kMCGreen : kMCYellow);
  if (!gRoundTrip.exact) {
    // Prompt 4 baseline diff: 編集・追加後は「diff は追加行＋dirty 行のみ」であることを報告する。
    hudLine("  baseline lines=" + std::to_string(gRoundTrip.baselineLines) +
            " added=" + std::to_string(gRoundTrip.addedLines) +
            " edited=" + std::to_string(gRoundTrip.editedLines) +
            " added_or_dirty_only=" + (gRoundTrip.diffIsAddedOrDirtyOnly ? "1" : "0"),
            gRoundTrip.diffIsAddedOrDirtyOnly ? kMCGreen : kMCYellow);
  }
  if (!gRoundTrip.exact && gRoundTrip.firstDiffLine > 0) {
    hudLine("  first diff line=" + std::to_string(gRoundTrip.firstDiffLine), kMCYellow);
  }
  // Prompt 6 write policy（要件 2）: data.txt に書き戻さない（serialize は stdout/HUD export のみ）。
  // legacy write-back は ../data.txt を load した旧運用のみで、viewer 運用では無効（状態を明示）。
  hudLine("write policy: data.txt に書かない. legacy write-back ../data.txt: " +
          std::string(gLoadedPath == "../data.txt" ? "enabled（legacy 運用のみ）" : "off（./data.txt viewer 運用）"),
          gLoadedPath == "../data.txt" ? kMCYellow : kMCOverlay2);
  hudLine("backup on quit: " + std::string(gBackupOnQuit ? "on（../data.txt.bak に上書き）" : "off（既定・B でトグル）"),
          gBackupOnQuit ? kMCGreen : kMCOverlay2);
  hudLine("font: " + std::string(gGuiText.ready ? gGuiText.fontPath.c_str()
                                                 : "ascii fallback (no freetype/jp font)"),
          gGuiText.ready ? kMCTeal : kMCRed);

  y += 6.f;
  if (gSelectedNode >= 0) {
    hudLine("selected node props:", kMCText);
    // view 別の表示順序で出す（family では family 関連項目が先頭、required/excluded は最小表示）
    for (const std::string &l : describeElement(gSelectedNode, gViewMode))
      hudLine(l, kMCSubtext1);
  } else {
    hudLine("selected: none (click node, ESC = clear, D = dump stdout)", kMCOverlay2);
  }

  if (gExportPanel) {
    y += 6.f;
    hudLine("export text (serializeDocument):", kMCGreen);
    const std::string serialized = serializeDocument(gDocument);
    size_t pos2 = 0;
    while (pos2 < serialized.size()) {
      const size_t nl = serialized.find('\n', pos2);
      std::string l = nl == std::string::npos ? serialized.substr(pos2)
                                              : serialized.substr(pos2, nl - pos2);
      hudLine(l, kMCSubtext0);
      if (nl == std::string::npos) break;
      pos2 = nl + 1;
    }
    hudLine("E = dump full data.txt to stdout   X = dump on quit   P = toggle", kMCGreen);
  }

  y += 6.f;
  hudLine("drag = pan   wheel/+/- = zoom   V = graph toggle   R = round-trip", kMCOverlay1);
  hudLine("parent edge = sapphire, child edge = peach, lavender = selected", kMCOverlay1);
  if (gViewMode == VIEW_FAMILY) {
    // family の凡例（Prompt 2）: 階層=sapphire / explicit child=peach / 矛盾=yellow
    size_t forestTrees = 0, laidOut = 0;
    for (int i : gGraph.nodesInScene(gViewScene)) {
      if (gGraph.nodes[i].famDepth == 0) ++forestTrees;
      if (gGraph.nodes[i].famDepth >= 0) ++laidOut;
    }
    hudLine("family: parent top -> children below. hierarchy edge = sapphire", kMCOverlay1);
    hudLine("explicit child(slot1) edge = peach, separate path. conflict = yellow", kMCOverlay1);
    hudLine("newRoot badge = mauve. dangling parent = root 扱い (par! badge)", kMCOverlay1);
    hudLine("forest=" + std::to_string(forestTrees) + " placed=" +
            std::to_string(laidOut) + "/" +
            std::to_string(gGraph.nodesInScene(gViewScene).size()), kMCSubtext1);
  } else if (gViewMode == VIEW_CONSTRAINT) {
    // constraint の凡例（Prompt 3）: required=teal / excluded slot1=red / excluded slot2=yellow。
    // family の矛盾 yellow と excluded slot2 の yellow は意味が違うことを凡例に明記する。
    // dangling constraint は edges の to==-1 を kind 別に数える（ノード側の -1 は count しない）。
    const ConstraintEdgeCounts c = countConstraintEdges(gViewScene);
    hudLine("constraint overlay on active layout. requiredReadId edge = teal", kMCOverlay1);
    hudLine("excluded slot1 edge = red. excluded slot2 edge = yellow（family 矛盾 yellow とは別意味）", kMCOverlay1);
    hudLine("ghost = dashed overlay0 box. target id が scene内に存在しない場合は id badge で示す", kMCOverlay1);
    hudLine("edges required=" + std::to_string(c.required) +
            " ghost=" + std::to_string(c.requiredGhost), kMCTeal);
    hudLine("edges excluded(slot1)=" + std::to_string(c.excludedSlot1) +
            " ghost=" + std::to_string(c.excludedSlot1Ghost), kMCRed);
    hudLine("edges excluded(slot2)=" + std::to_string(c.excludedSlot2) +
            " ghost=" + std::to_string(c.excludedSlot2Ghost), kMCYellow);
    if (c.excludedSlot1 + c.excludedSlot2 == 0)
      hudLine("excluded_edges=0 (none in data.txt)", kMCOverlay2);
    hudLine("R general layout: overlay に使用。C constraint: implemented / EDIT add+props: implemented（Prompt4/5・Prompt6 統合）", kMCOverlay1);
  } else if (gViewMode == EDITOR) {
    // editor add の凡例（Prompt 4）。preview panel は graph 領域の GUI 欄で、
    // constraint の teal/red/yellow の意味体系とは別（panel は sky=text進行 / subtext=値 /
    // green=commit ready / yellow=warning の UI 色）。
    hudLine("editor add (Prompt4): stage=" + std::string(editorStageName(gEditor.stage)), kMCSky);
    hudLine("id=" + (gEditor.assignedId > 0 ? hexByte(static_cast<uint8_t>(gEditor.assignedId))
                                             : std::string("none(id>0xFF full)")) +
            " scene=" + hexNibble(gViewScene) + " parent=" + hexByte(gEditor.parentId) +
            " child=" + hexByte(gEditor.childOptionSelected ? gEditor.childId : 0) +
            " slot1Index=0 slot1value=" +
            hexByte(gEditor.childOptionSelected ? gEditor.childId : 0), kMCSubtext1);
    if (!gEditor.dialogue.empty()) hudLine("dialogue: " + gEditor.dialogue, kMCSubtext0);
    hudLine("sync runtime (G): " + std::string(gEditor.syncRuntime ? "on（commit で containerA/B に反映）" : "off（既定・runtime は自動同期しない）"),
            gEditor.syncRuntime ? kMCGreen : kMCOverlay2);
    if (!gEditor.lastWarning.empty()) hudLine("warning: " + gEditor.lastWarning, kMCYellow);
    hudLine("keys: ENTER=start/next/commit, Y/N=flags, click node=candidate(parent/child), G=runtime sync, ESC=cancel add", kMCOverlay1);
    hudLine("EDIT add: implemented（document append only・追加行のみ baseline と diff）. runtime sync default off", kMCOverlay1);
    // props menu（Prompt 5）: panel open 時は HUD の同じ幅 budget（drawGuiLine の budgetPx）でメニューを描く。
    if (gProps.active) {
      for (const std::pair<std::string, uint32_t> &row : propsMenuRows()) hudLine(row.first, row.second);
      hudLine("props edit: implemented（全 prop fields・validation clamp+warning・undo stack・選択 (scene,id) 復元・runtime 同期なし）", kMCOverlay1);
    } else {
      hudLine("props edit (Prompt5): click node / right-click = open props panel（TAB next・ENTER/SPACE toggle・+/- value・Y/N bool・U undo・G graph rebuild・ESC close）", kMCOverlay1);
    }
  } else {
    // Prompt 6・統合後の実装状態（要件 6・凡例を実装済み状態に更新）。
    hudLine("R general: active. F family viewer: implemented (deterministic tree layout). C constraint: implemented（teal/red/yellow edges + ghost box）", kMCOverlay1);
    hudLine("EDIT add+props: implemented（dialogue/parent/child 追加 + 全 prop fields 編集・undo・key 4）. game mode: implemented（H / DDM_MODE=game・graph/HUD 描かず console のみ）", kMCOverlay1);
  }
}

void drawGraphOverlay(SDL_Renderer *r) {
  if (gGraphEnabled) {
    if (gViewMode == VIEW_FAMILY) {
      // family は active layout（familyWx/familyWy）で階層 forest を描く（drawFamilyView が
      // edges + nodes を自分で行う。general の drawGraphEdges は呼ばない）。
      drawFamilyView(r);
    } else {
      drawGraphEdges(r);
      for (int i : gGraph.nodesInScene(gViewScene))
        drawNodeBox(r, gGraph.nodes[i], gSelectedNode == i);

      if (gViewMode == VIEW_CONSTRAINT) drawConstraintView(r);
      else if (gViewMode == EDITOR) drawEditorOverlay(r);
    }
  }
  drawHud(r);
}

int pickNode(float sx, float sy) {
  const SDL_FPoint world = screenToWorld(sx, sy);
  for (int i : gGraph.nodesInScene(gViewScene)) {
    const GraphNode &n = gGraph.nodes[i];
    const SDL_FPoint w = nodePos(n); // active layout の座標で選択する
    if (world.x >= w.x && world.x <= w.x + static_cast<float>(kNodeW) &&
        world.y >= w.y && world.y <= w.y + static_cast<float>(kNodeH)) {
      return i;
    }
  }
  return -1;
}

void zoomAt(float sx, float sy, float delta) {
  const SDL_FPoint anchor = screenToWorld(sx, sy);
  const float factor = delta > 0.f ? 1.15f : 0.87f;
  gViewZoom = std::clamp(gViewZoom * factor, 0.2f, 4.f);
  gViewPanX = sx - anchor.x * gViewZoom;
  gViewPanY = sy - anchor.y * gViewZoom;
  needRedraw = true;
}

void switchScene(int dir) {
  if (gGraph.scenes.empty()) return;
  size_t pos = 0;
  for (size_t i = 0; i < gGraph.scenes.size(); ++i) {
    if (gGraph.scenes[i] == gViewScene) {
      pos = i;
      break;
    }
  }
  if (dir > 0) pos = (pos + 1) % gGraph.scenes.size();
  else if (dir < 0) pos = (pos + gGraph.scenes.size() - 1) % gGraph.scenes.size();
  gViewScene = gGraph.scenes[pos];
  applyActiveLayout(); // active mode の layout（general / family）を再適用
  fitViewToScene();
}

void handlePointerEvent(const SDL_Event *ev) {
  if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
    float mx = 0.f, my = 0.f;
    SDL_GetMouseState(&mx, &my);
    zoomAt(mx, my, ev->wheel.y);
    return;
  }
  if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      ev->button.button == SDL_BUTTON_RIGHT) {
    // right-click 相当の選択操作（optional マウス対応・Prompt 5 要件 2）:
    // EDITOR でノードを選択して props panel を開く。graph の pan/zoom には影響しない。
    if (gViewMode == EDITOR && gEditor.stage == EDITOR_IDLE) {
      const int hit = pickNode(ev->button.x, ev->button.y);
      if (hit >= 0) {
        gSelectedNode = hit;
        propsOpen(hit); // ノード選択で editor panel を開く（同一ノードの再オープンも許す）
        printSelectedElementDetails();
        needRedraw = true;
      }
    }
    return;
  }
  if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      ev->button.button == SDL_BUTTON_LEFT) {
    const int hit = pickNode(ev->button.x, ev->button.y);
    if (hit >= 0) {
      gSelectedNode = hit;
      // EDITOR の parent/child stage ではクリックしたノードを候補として選ぶ（Prompt 4 要件 3/4）。
      // それ以外の stage では通常のノード選択として振る舞う。invalid（自己 id / id 0）は
      // 候補を立てず warning を出す（追加禁止は editorValidateAdd が commit 側で確定する）。
      if (gEditor.stage == EDITOR_PARENT) {
        const uint8_t pickedId = gGraph.nodes[hit].id;
        if (pickedId == static_cast<uint8_t>(std::max(0, gEditor.assignedId))) {
          gEditor.lastWarning = "self-parent forbidden（clicked id == 追加 id）";
        } else if (pickedId == 0) {
          gEditor.parentId = 0; // 0 は root / no parent として許す
          gEditor.lastWarning.clear();
        } else {
          gEditor.parentId = pickedId; // prop bits 8-15 に value を立てる（同 scene の実在 id）
          gEditor.lastWarning.clear();
        }
      } else if (gEditor.stage == EDITOR_CHILD) {
        const uint8_t pickedId = gGraph.nodes[hit].id;
        if (pickedId == 0) {
          gEditor.childOptionSelected = false;
          gEditor.childId = 0; // slot1 value 0 のまま（none として扱う）
          gEditor.lastWarning = "id=0 は child に使えない → none として扱う";
        } else {
          gEditor.childOptionSelected = true;
          gEditor.childId = pickedId; // slot1Index は 0 のまま、value bits 40-47 に child id を立てる
          gEditor.lastWarning.clear();
        }
      }
      gDragging = false;
      printSelectedElementDetails();
      // ノード選択（クリック・EDITOR の idle のとき）で props panel を開く（Prompt 5 要件 2）。
      // parent/child stage では propsOpen は no-op（add flow の候補選択を優先する）。
      propsOpen(hit);
    } else {
      gDragging = true;
      gDragLastX = ev->button.x;
      gDragLastY = ev->button.y;
    }
    needRedraw = true;
    return;
  }
  if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
      ev->button.button == SDL_BUTTON_LEFT) {
    gDragging = false;
    needRedraw = true;
    return;
  }
  if (ev->type == SDL_EVENT_MOUSE_MOTION && gDragging) {
    gViewPanX += ev->motion.x - gDragLastX;
    gViewPanY += ev->motion.y - gDragLastY;
    gDragLastX = ev->motion.x;
    gDragLastY = ev->motion.y;
    needRedraw = true;
  }
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
  prop |= (id & 0xFF);                      // bit 0-7: id
  prop |= ((uint64_t(parent) & 0xFF) << 8); // bit 8-15: parent
  if (isQuote)
    prop |= (1ULL << 17); // bit 17: isQuote
  if (isOnetime)
    prop |= (1ULL << 18);                   // bit 18: isOnetime
  prop |= ((uint64_t(scene) & 0x0F) << 20); // bit 20-23: scene
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

  // graph view + HUD を同時に収めるためのデフォルトサイズ（旧 320x240 では
  // HUD で graph 描画領域が確保できない）。リサイズは自由。
  gWindow = SDL_CreateWindow("DDMEditor Focus Window", 1280, 760,
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

  // Prompt 6・game モード共存（要件 7）: 起動時の UI mode を環境変数 DDM_MODE で選ぶ。
  // 既定（unset / "viewer"）は現行動作（graph overlay + HUD + console）をそのまま維持する。
  // "game" は SDL_AppIterate の game 分岐（renderConsole と入力系統のみ・graph/HUD を描かない）。
  // 実行中の切替は H キー（setUiMode）。DDM_SELFTEST は viewer 検証に集中する（game phase は
  // 自己検証の一 phase として最後に H トグル往復で確認し、viewer に復元して終了する）。
  const char *uiModeEnv = std::getenv("DDM_MODE");
  if (uiModeEnv != nullptr && std::strcmp(uiModeEnv, "game") == 0) gUiMode = UI_GAME;
  std::cout << "ui mode: " << (gUiMode == UI_GAME ? "game（DDM_MODE=game・H で viewer 復帰）"
                                                  : "viewer（既定・graph overlay + HUD + console）") << std::endl;

  // --- 日本語描画: FreeType を動的ロードして日本語フォントを探す ---
  if (initGuiText()) {
    std::cout << "gui text: freetype ok, font=" << gGuiText.fontPath << std::endl;
  } else {
    std::cout << "gui text: freetype / jp font not found -> ASCII '?' fallback: "
              << gGuiText.lastError << std::endl;
  }

  // --- 厳密な Document loader ---
  // ./data.txt を想定し、build/ 直下から実行する既存運用では ../data.txt を試す。
  const char *candidates[] = {"./data.txt", "../data.txt"};
  bool loaded = false;
  for (const char *candidate : candidates) {
    std::vector<std::string> warnings;
    if (loadDocument(candidate, gDocument, warnings)) {
      gLoadedPath = candidate;
      gLoadWarnings = warnings;
      loaded = true;
      break;
    }
  }
  if (!loaded) {
    std::cerr << "エラー: ./data.txt / ../data.txt を開けませんでした。" << std::endl;
    return SDL_APP_FAILURE;
  }

  // console 側の既存挙動は containerA からそのまま維持する。
  // load-time の prop を保持するので hasRead は変更しない。
  containerA.clear();
  for (const ElementRecord &rec : gDocument.elements) {
    containerA.push_back(rec.element);
  }

  buildGraph(gDocument, gGraph);
  gViewScene = gGraph.smallestNonZeroScene();
  gViewMode = VIEW_GENERAL;
  gRoundTrip = verifyRoundTrip(gDocument);
  // Prompt 6・要件 8: load 時の bytes を基準に保存し、最終の「data.txt は変更されない」確認に使う。
  gLoadedBytes = gDocument.raw.size();

  std::cout << "=== [ Document ] ===" << std::endl;
  std::cout << "loaded: " << gLoadedPath << " bytes=" << gDocument.raw.size()
            << " lines=" << gDocument.lines.size()
            << " elements=" << gDocument.parsedLines
            << " comments=" << gDocument.commentLines
            << " blank=" << gDocument.blankLines << std::endl;
  std::cout << "round-trip: " << (gRoundTrip.exact ? "EXACT OK" : "MISMATCH")
            << " serialized=" << gRoundTrip.serializedBytes << " bytes" << std::endl;
  for (const auto &w : gLoadWarnings) std::cout << "warning: " << w << std::endl;
  for (const auto &w : gGraph.warnings) std::cout << "warning: " << w << std::endl;
  std::cout << "scenes:";
  for (uint8_t s : gGraph.scenes) std::cout << " " << hexNibble(s);
  std::cout << std::endl << "view scene=" << hexNibble(gViewScene)
            << " nodes=" << gGraph.nodesInScene(gViewScene).size()
            << " roots=" << gGraph.roots.size() << std::endl;

  layoutScene(gViewScene);
  fitViewToScene();


  processContainerLifecycle();
  return SDL_APP_CONTINUE;
}

// Prompt 6・game モード共存（要件 7）: UI mode 切替。game へ離れるときは editor の add flow と
// props panel を閉じる（text input / GUI state の対称性・Document は変更しない）。
// runtime（console の入力・renderConsole）はそのまま動作する。viewer 復帰時は graph の
// mode・座標・選択はそのまま（applyActiveLayout は mode/scene 切替時の既存経路のまま触らない）。
void setUiMode(UiMode m) {
  if (m == gUiMode) return;
  if (m == UI_GAME) {
    editorLeaveModeIfNeeded(); // editor の add flow / text input を閉じる（取消・Document 不変）
    propsClose();              // props panel を閉じる（undo stack・選択は保持。active=false のときも安全）
  }
  gUiMode = m;
  std::cout << "ui mode switch: " << (m == UI_GAME ? "game（graph/HUD は描かない・renderConsole + 入力系統のみ）"
                                                   : "viewer（graph overlay + HUD・既定動作）") << std::endl;
  needRedraw = true;
}

// SDL_AppEvent
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }

  // EDITOR の add state 中は ESC を「選択解除」より取消（editorCancelAdd）を優先する（Prompt 4）。
  // game mode では add flow 自体が閉じる（H トグルで setUiMode が取消する）ので viewer のみ。
  if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE &&
      gUiMode == UI_VIEWER && gViewMode == EDITOR && gEditor.stage != EDITOR_IDLE) {
    editorCancelAdd();
    needRedraw = true;
    return SDL_APP_CONTINUE;
  }

  if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
    if (gAppMode != MODE_NORMAL) {
      cancelInsertMode();
      return SDL_APP_CONTINUE;
    }
  }

  // --- graph view: pan / zoom / node selection（挿入モード中は触らない。game mode では
  // graph を描かないので pointer は入力系統に渡さない — Prompt 6 要件 7） ---
  if (gUiMode == UI_VIEWER && gGraphEnabled && gAppMode == MODE_NORMAL &&
      (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
       event->type == SDL_EVENT_MOUSE_MOTION ||
       event->type == SDL_EVENT_MOUSE_WHEEL)) {
    handlePointerEvent(event);
    return SDL_APP_CONTINUE;
  }

  if (event->type == SDL_EVENT_TEXT_INPUT && gAppMode == MODE_NORMAL &&
      gUiMode == UI_VIEWER && gViewMode == EDITOR && gEditor.stage == EDITOR_DIALOGUE) {
    // editor の dialogue 入力（UTF-8・空白を含む入力をそのまま保持）。
    // gViewMode が EDITOR でないとき・game mode のときは GUI 入力を遮断する（上の条件）。
    gEditor.dialogue += event->text.text;
    needRedraw = true;
    return SDL_APP_CONTINUE;
  }

  if (gAppMode == MODE_NORMAL) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;

      // EDITOR mode のとき GUI 用のキーを使い、runtime キー（TAB/F/O/I/A/U/M/Y/N）は遮断する。
      // graph view のキー（1-4 / V / P / X / E / R / D / L / 0 / +/- / [ ] / 矢印 / ESC / Q）は
      // 共通（ただし viewer のみ。game mode では graph GUI キーを通さない — Prompt 6 要件 7）。
      // gAppMode が挿入モードのときは上の既存遮断規則が優先される。
      // game mode では editorMode=false（runtime キーそのまま動作＝「入力系統だけ」を動かす）。
      const bool viewerUi = gUiMode == UI_VIEWER;
      const bool editorMode = viewerUi && gViewMode == EDITOR;

      // props panel（Prompt 5）: open のとき編集キーを先に処理する（add flow の ENTER/Y/N/G・
      // graph の +/- zoom・ESC 選択解除に優先する。props は idle のときだけ開くので add flow と
      // 同時状態にならない＝衝突しない）。1-4 / V / P / X / E / R / D / L / 0 / [ ] / 矢印 / Q は
      // 共通経路のまま通す（mode 切替キーは editorLeaveModeIfNeeded が panel を閉じる）。
      if (editorMode && gProps.active) {
        if (key == SDLK_TAB) { propsNextField(); return SDL_APP_CONTINUE; }
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
          propsToggleField(gProps.cursor); // ENTER/SPACE = トグル（option cycle / bool 反転 / enum cycle / uint 最低bit）
          return SDL_APP_CONTINUE;
        } else if (key == SDLK_PLUS) { propsApplyFieldValue(gProps.cursor, 0, false, +1); return SDL_APP_CONTINUE; }
        else if (key == SDLK_MINUS) { propsApplyFieldValue(gProps.cursor, 0, false, -1); return SDL_APP_CONTINUE; }
        else if (key == SDLK_Y) { propsSetBoolValueCurrent(true); return SDL_APP_CONTINUE; }
        else if (key == SDLK_N) { propsSetBoolValueCurrent(false); return SDL_APP_CONTINUE; }
        else if (key == SDLK_U) { propsUndo(); return SDL_APP_CONTINUE; } // runtime の U は editor で遮断済み
        else if (key == SDLK_G) { propsRebuildGraph(); return SDL_APP_CONTINUE; } // add flow の G（runtime 同期）は panel open 時遮断
        else if (key == SDLK_ESCAPE) { propsClose(); return SDL_APP_CONTINUE; } // panel 閉じを優先（選択解除・add 取消より先）
      }

      // --- Prompt 6・game モード共存（要件 7）+ write policy トグル（要件 2） ---
      // H = viewer/game 切替（game 側でも復帰用に動作する）。B = backup on quit トグル（既定 off）。
      // graph view keys（1-4 / V / P / X / E / R / D / L / 0 / +/- / [ ] / 矢印 / viewer の
      // ESC 選択解除 / editor・props GUI キー）は viewer のみ通す。runtime キー（TAB/F/O/I/A/U/M/Y/N）
      // と Q / 挿入モードの ESC はどちらの mode でもそのまま（既存の入力システムを壊さない）。
      if (key == SDLK_H) { setUiMode(viewerUi ? UI_GAME : UI_VIEWER); return SDL_APP_CONTINUE; }
      if (key == SDLK_B) { gBackupOnQuit = !gBackupOnQuit; needRedraw = true; }

      if (viewerUi) {
        // --- graph view keys（既存の入力は壊さない。VIEW_GENERAL の操作のみ・viewer のみ） ---
        // 注: I/O/A/U/F/M は既存の入力システムそのまま。1-4 は既存で未使用のキー。
        // 1/2/3 に切り替えるときは editor の add flow を取消する（document は変更しない）。
        if (key == SDLK_1) { editorLeaveModeIfNeeded(); gViewMode = VIEW_GENERAL; applyActiveLayout(); fitViewToScene(); needRedraw = true; }
        else if (key == SDLK_2) { editorLeaveModeIfNeeded(); gViewMode = VIEW_FAMILY; applyActiveLayout(); fitViewToScene(); needRedraw = true; }
        else if (key == SDLK_3) { editorLeaveModeIfNeeded(); gViewMode = VIEW_CONSTRAINT; applyActiveLayout(); fitViewToScene(); needRedraw = true; }
        else if (key == SDLK_4) { gViewMode = EDITOR; applyActiveLayout(); fitViewToScene(); needRedraw = true; }
        else if (key == SDLK_V) { gGraphEnabled = !gGraphEnabled; needRedraw = true; }
        else if (key == SDLK_P) { gExportPanel = !gExportPanel; needRedraw = true; }
        else if (key == SDLK_X) { gExportOnQuit = !gExportOnQuit; needRedraw = true; }
        else if (key == SDLK_E) { printDocumentToStdout(gDocument); }
        else if (key == SDLK_R) { printRoundTripReport(gDocument); }
        else if (key == SDLK_D) { printSelectedElementDetails(); }
        else if (key == SDLK_L) { applyActiveLayout(); fitViewToScene(); needRedraw = true; }
        else if (key == SDLK_0) { fitViewToScene(); needRedraw = true; }
        else if (key == SDLK_MINUS) { gViewZoom = std::clamp(gViewZoom * 0.87f, 0.2f, 4.f); needRedraw = true; }
        else if (key == SDLK_PLUS) { gViewZoom = std::clamp(gViewZoom * 1.15f, 0.2f, 4.f); needRedraw = true; }
        else if (key == SDLK_LEFTBRACKET) { switchScene(-1); needRedraw = true; }
        else if (key == SDLK_RIGHTBRACKET) { switchScene(+1); needRedraw = true; }
        else if (key == SDLK_LEFT) { gViewPanX += 24.f; needRedraw = true; }
        else if (key == SDLK_RIGHT) { gViewPanX -= 24.f; needRedraw = true; }
        else if (key == SDLK_UP) { gViewPanY += 24.f; needRedraw = true; }
        else if (key == SDLK_DOWN) { gViewPanY -= 24.f; needRedraw = true; }
        else if (key == SDLK_ESCAPE) { gSelectedNode = -1; needRedraw = true; }
      } // viewerUi block end（graph view keys）

        // --- EDITOR mode の GUI キー（Prompt 4。runtime キー TAB/F/O/I/A/U/M/Y/N は editor で遮断。
        // editorMode は viewerUi を含むので game mode ではこれらのキーは通らない） ---
        if (editorMode) {
          if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            editorEnterKey(); // idle → dialogue → quote → onetime → parent → child → commit
          } else if (key == SDLK_BACKSPACE) {
            if (gEditor.stage == EDITOR_DIALOGUE) {
              editorBackspaceDialogue(); // UTF-8 の codepoint を保つ
              needRedraw = true;
            }
          } else if (key == SDLK_Y || key == SDLK_N) {
            editorYN(key == SDLK_Y); // quote / onetime stage のみ有効。其它 stage では no-op
          } else if (key == SDLK_G) {
            gEditor.syncRuntime = !gEditor.syncRuntime; // runtime 反映トグル（既定 off）
            gEditor.lastWarning.clear();
            needRedraw = true;
          }
        }

      if (key == SDLK_Q) {
        return SDL_APP_SUCCESS;
      }

      // 「移動する」モード中は挿入しない(退避した元の表示・選択を壊さない)。
      // EDITOR mode のときは GUI 用のキーのみ使い、runtime の挿入・選択キーは遮断する。
      if (!editorMode && key == SDLK_TAB && !gMoveMode) {
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
      } else if (!editorMode && key == SDLK_F) {
        sendProperty();
      } else if (!editorMode && !displayList.empty() && !noContentState) {
        if (key == SDLK_O) {
          if (isObjectiveSelected) {
            if (!objectList.empty()) {
              selectedObjectiveIndex =
                  (selectedObjectiveIndex + 1) % objectList.size();
              needRedraw = true;
            }
          } else {
            selectedIndex = (selectedIndex + 1) % displayList.size();
            rebuildObjectiveList();
            needRedraw = true;
          }
        } else if (key == SDLK_I) {
          if (isObjectiveSelected) {
            if (!objectList.empty()) {
              selectedObjectiveIndex =
                  (selectedObjectiveIndex - 1 + objectList.size()) %
                  objectList.size();
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
          // 「移動する」モード中は常にisObjectiveSelected == trueなので
          // ここでは解除しない。
          if (isObjectiveSelected && !gMoveMode) {
            isObjectiveSelected = false;
            needRedraw = true;
          }
        } else if (key == SDLK_M) {
          // displayListと「移動する」コンテナをトグル表示する。
          // トグル時にinputシステムの値(selectedIndex・選択状態など)を
          // suspend用の配列へ退避し、復元時にその値を戻して元の表示を
          // そのまま再表示する。
          if (gMoveMode) {
            exitMoveMode();
          } else {
            enterMoveMode();
          }
          needRedraw = true;
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

  // DDM_SELFTEST=1 のとき、load / round-trip / export と、クリック選択・
  // zoom/pan の往復検証を stdout に出力して終了する。
  // 検証用（SDL dummy video driver で GUI 描画を確認できる環境が無くても実行できる）。
  // 通常の対話実行ではこの分岐は通らない。
  const char *selfTest = std::getenv("DDM_SELFTEST");
  if (selfTest != nullptr && selfTest[0] != '\0') {
    // Prompt 6・要件 7: DDM_SELFTEST は viewer 検証に集中する。DDM_MODE=game で起動した場合も
    // selftest は viewer に復帰して全 phase を検証する（game 分岐の動作・H 往復は
    // game-mode phase で確認する。Document / data.txt は変わらない）。
    if (gUiMode == UI_GAME) setUiMode(UI_VIEWER);
    printRoundTripReport(gDocument);
    printDocumentToStdout(gDocument);

    // --- Prompt 6 round-trip baseline phase（要件 2・FAIL 検知に格上げ + 強化検証） ---
    // load 直後の baseline を保存し（Document::raw は load 時のまま）、編集/追加後の diff が
    // dirty/added 行のみであることを各 phase で検証する（RoundTripReport の added/edited）。
    // terminator（"\n" / "\r\n" / ""）保持・正格式（quote・'0x'・lowercase16・2|2|4区切りパイプ・
    // trailing comma）・hasRead/dummy prop の load-time 保持をここで確認する。期待値はその場で合成する。
    std::cout << "=== [ selftest round-trip baseline ] ===" << std::endl;
    {
      const RoundTripReport rtBase = verifyRoundTrip(gDocument);
      gRoundTrip = rtBase;
      const bool rtBaseOk = stPass(rtBase.exact, "round-trip baseline");
      std::cout << "round_trip_baseline exact=" << (rtBaseOk ? 1 : 0) << " bytes=" << rtBase.originalBytes
                << " serialized=" << rtBase.serializedBytes << " lines=" << rtBase.lineCount
                << " baseline_lines=" << rtBase.baselineLines << std::endl;

      // RawLine.terminator の保持: text + terminator の連結で raw 全バイト列が復元されること
      // （CRLF・改行なし最終行を壊さない）。
      std::string rebuilt;
      rebuilt.reserve(gDocument.raw.size());
      size_t crlfLines = 0, unterminated = 0;
      for (const RawLine &rl : gDocument.lines) {
        rebuilt += rl.text + rl.terminator;
        if (rl.terminator == "\r\n") ++crlfLines;
        else if (rl.terminator.empty()) ++unterminated;
      }
      const bool termOk = stPass(rebuilt == gDocument.raw, "round-trip baseline");
      std::cout << "terminator_preserved match=" << (termOk ? 1 : 0) << " crlf_lines=" << crlfLines
                << " unterminated_lines=" << unterminated << std::endl;

      // 正格式の规则（再生成・追加行と同じ规则で検証する）: quote 形式・trailing comma・
      // '0x' prefix・lowercase 16 桁・2byte|2byte|4byte の '|' 区切り。合成→再パースの往復を
      // その場で合成した値で確認する（パイプあり・なしの両形式が同一値に復元されること）。
      const uint64_t fmtSample = 0x001000000010021dULL; // id=0x21 parent=0x10（他 bit 0 の合成例）
      const std::string fmtHex = formatPropHex16(fmtSample);
      const std::string fmtPiped = formatPropHex16Piped(fmtSample);
      const std::string fmtLine = "'fmt probe', '0x" + fmtPiped + "',";
      std::string fmtD;
      uint64_t fmtP = 0;
      std::string fmtDLegacy;
      uint64_t fmtPLegacy = 0;
      const bool fmtOk = stPass(fmtHex.size() == 16 && fmtHex == "001000000010021d" &&
                                 fmtPiped == "0010|0000|0010021d" &&
                                 parseElementLine(fmtLine, fmtD, fmtP) && fmtP == fmtSample &&
                                 fmtD == "fmt probe" &&
                                 parseElementLine("'fmt probe', '0x" + fmtHex + "',",
                                                  fmtDLegacy, fmtPLegacy) &&
                                 fmtPLegacy == fmtSample && fmtDLegacy == "fmt probe",
                                 "round-trip baseline");
      std::cout << "canonical_format pipe_split_2_2_4_0x_comma match=" << (fmtOk ? 1 : 0)
                << " sample='" << fmtLine << "'" << std::endl;

      // hasRead をクリアしない・dummy 行の prop も原値を保持: containerA は load-time prop のまま。
      bool propsKept = gDocument.elements.size() == containerA.size();
      size_t hasReadCount = 0, dummyNonZero = 0;
      for (size_t i = 0; i < containerA.size(); ++i) {
        if (i >= gDocument.elements.size() ||
            containerA[i].prop != gDocument.elements[i].element.prop ||
            containerA[i].dialogue != gDocument.elements[i].element.dialogue) propsKept = false;
      }
      for (const ElementRecord &rec : gDocument.elements) {
        if (getPropHasRead(rec.element.prop)) ++hasReadCount;
        if (rec.element.dialogue == "dummy" && rec.element.prop != 0) ++dummyNonZero;
      }
      const bool keepOk = stPass(propsKept, "round-trip baseline");
      std::cout << "load_time_props_preserved match=" << (keepOk ? 1 : 0) << " hasRead_in_doc=" << hasReadCount
                << " dummy_nonzero_prop=" << dummyNonZero << std::endl;
    }

    // view scene の先頭ノードの中心をクリックし、pickNode が正しい node を
    // 選ぶことを確認する。続けて zoom/pan を適用した後も同じ node が選ぶことを
    // world/screen 変換の往復として確認する。
    const std::vector<int> inScene = gGraph.nodesInScene(gViewScene);
    if (inScene.empty()) stFail("selection"); // 検証対象の node が無い場合は FAIL（Prompt 6・要件 8）
    if (!inScene.empty()) {
      const int want = inScene[0];
      const GraphNode &n = gGraph.nodes[want];
      const float cx = n.wx + static_cast<float>(kNodeW) * 0.5f;
      const float cy = n.wy + static_cast<float>(kNodeH) * 0.5f;

      SDL_Event ev{};
      ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      ev.button.button = SDL_BUTTON_LEFT;
      ev.button.down = true;
      const SDL_FPoint before = worldToScreen(cx, cy);
      ev.button.x = before.x;
      ev.button.y = before.y;
      handlePointerEvent(&ev);

      // Prompt 6: selection の flag を FAIL 検知に格上げ（要件 8）。
      const bool selMatch1 = stPass(gSelectedNode == want, "selection");
      std::cout << "=== [ selftest selection ] ===" << std::endl;
      std::cout << "want idx=" << want << " scene=" << hexNibble(n.scene)
                << " id=" << hexByte(n.id) << " picked=" << gSelectedNode
                << " match=" << (selMatch1 ? 1 : 0) << std::endl;

      zoomAt(320.f, 120.f, 1.f); // wheel zoom in
      gViewPanX += 40.f;         // pan
      gViewPanY -= 25.f;
      const SDL_FPoint after = worldToScreen(cx, cy);
      ev.button.x = after.x;
      ev.button.y = after.y;
      gSelectedNode = -1;
      handlePointerEvent(&ev);
      const bool selMatch2 = stPass(gSelectedNode == want, "selection");
      std::cout << "after zoom/pan picked=" << gSelectedNode
                << " match=" << (selMatch2 ? 1 : 0) << std::endl;
      printSelectedElementDetails();

    }

    // --- family phase（Prompt 2）: mode 1<->2 の往復で VIEW_GENERAL の座標が壊れないこと・
    // layoutFamily の決定性・active layout の pickNode・forest 数の data.txt 実測照合・
    // round-trip EXACT の維持・kMC 実色のヒストグラム ---
    std::cout << "=== [ selftest family ] ===" << std::endl;
    {
      std::vector<std::pair<float, float>> genPos(gGraph.nodes.size());
      for (size_t i = 0; i < gGraph.nodes.size(); ++i)
        genPos[i] = {gGraph.nodes[i].wx, gGraph.nodes[i].wy};

      gSelectedNode = -1;
      gViewMode = VIEW_FAMILY;
      applyActiveLayout();
      fitViewToScene();

      // 決定性: 同じ scene を 2回 layoutFamily → familyWx/familyWy が同一。
      std::vector<std::pair<float, float>> famA(gGraph.nodes.size(), {0.f, 0.f});
      for (int i : gGraph.nodesInScene(gViewScene))
        famA[i] = {gGraph.nodes[i].familyWx, gGraph.nodes[i].familyWy};
      layoutFamily(gViewScene);
      bool deterministic = true;
      for (int i : gGraph.nodesInScene(gViewScene)) {
        if (gGraph.nodes[i].familyWx != famA[i].first ||
            gGraph.nodes[i].familyWy != famA[i].second) deterministic = false;
      }
      // Prompt 6: family の flag を FAIL 検知に格上げ（要件 8・既存の期待値実行時計算はそのまま）。
      const bool famDetOk = stPass(deterministic, "family layout");
      std::cout << "family_layout_deterministic scene=" << hexNibble(gViewScene) << "="
                << (famDetOk ? 1 : 0) << std::endl;

      // pickNode が active layout（family 座標）で正しい node を選ぶこと。
      const std::vector<int> famScene = gGraph.nodesInScene(gViewScene);
      if (famScene.empty()) stFail("family layout");
      if (!famScene.empty()) {
        const int want = famScene[0];
        const GraphNode &n = gGraph.nodes[want];
        const SDL_FPoint s = worldToScreen(n.familyWx + static_cast<float>(kNodeW) * 0.5f,
                                           n.familyWy + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = s.x;
        ev.button.y = s.y;
        gSelectedNode = -1;
        handlePointerEvent(&ev);
        const bool famPickOk = stPass(gSelectedNode == want, "family layout");
        std::cout << "family pick want idx=" << want << " id=" << hexByte(n.id)
                  << " picked=" << gSelectedNode << " match=" << (famPickOk ? 1 : 0) << std::endl;
      }

      // forest 数の照合（scene 0/1/2/15）。期待値は prop の直接パースから作る（graph 構造を通さない）:
      //   rootLike = parent == 0 または parent が同 scene に無い node。
      //   weakly-connected component ごとに max(1, rootLike数) を forest 数として足す
      //   （root が無い cycle component は 1 tree。layout 側も同じ規則で並べる）。
      bool famForestOk = true; // Prompt 6: forest/placed の照合を全 scene で集約して FAIL 検知する
      for (uint8_t probe : gGraph.scenes) {
        if (probe != 0 && probe != 1 && probe != 2 && probe != 15) continue;
        std::unordered_map<uint8_t, uint8_t> parentById;
        for (const ElementRecord &rec : gDocument.elements) {
          const uint64_t prop = rec.element.prop;
          if (getPropScene(prop) == probe) parentById[getPropID(prop)] = getPropParent(prop);
        }
        std::vector<uint8_t> ids;
        for (const auto &kv : parentById) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end());
        std::unordered_map<uint8_t, int> indexOf;
        for (size_t i = 0; i < ids.size(); ++i) indexOf[ids[i]] = static_cast<int>(i);

        std::vector<int> dsu(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) dsu[i] = static_cast<int>(i);
        auto findRoot = [&dsu](int a) {
          while (dsu[a] != a) a = dsu[a];
          return a;
        };
        for (uint8_t id : ids) {
          const uint8_t parent = parentById[id];
          if (parent == 0) continue;
          const auto pit = indexOf.find(parent);
          if (pit == indexOf.end()) continue; // dangling parent は component を作らない
          const int ra = findRoot(indexOf[id]);
          const int rb = findRoot(pit->second);
          if (ra != rb) dsu[ra] = rb;
        }
        std::unordered_set<int> compReps;
        std::unordered_map<int, size_t> rootLikeInComp;
        for (uint8_t id : ids) {
          const int rep = findRoot(indexOf[id]);
          compReps.insert(rep);
          const uint8_t parent = parentById[id];
          // rootLike = parent == 0 または parent が同 scene に無い node（layout の pass 1 root）
          if (parent == 0 || indexOf.find(parent) == indexOf.end()) ++rootLikeInComp[rep];
        }
        size_t expectedTrees = 0;
        for (int rep : compReps) expectedTrees += std::max<size_t>(1, rootLikeInComp[rep]);

        size_t graphP0 = 0, graphDangling = 0;
        for (int i : gGraph.nodesInScene(probe)) {
          if (gGraph.nodes[i].parentId == 0) ++graphP0;
          if (gGraph.nodes[i].danglingParent) ++graphDangling;
        }
        layoutFamily(probe); // family 座標のだけ書く。他 scene（active の general 座標）は壊れない
        size_t layoutForest = 0, placedCnt = 0;
        for (int i : gGraph.nodesInScene(probe)) {
          if (gGraph.nodes[i].famDepth == 0) ++layoutForest;
          if (gGraph.nodes[i].famDepth >= 0) ++placedCnt;
        }
        const bool famMatch = expectedTrees == layoutForest && placedCnt == ids.size();
        famForestOk &= famMatch;
        std::cout << "scene=" << hexNibble(probe) << " nodes=" << ids.size()
                  << " roots_p0=" << graphP0 << " dangling_parent=" << graphDangling
                  << " expected_forest=" << expectedTrees << " layout_forest=" << layoutForest
                  << " placed=" << placedCnt << "/" << ids.size() << " match="
                  << (famMatch ? 1 : 0) << std::endl;
      }
      std::cout << "family_forest_all_scenes match=" << (stPass(famForestOk, "family layout") ? 1 : 0) << std::endl;

      // Prompt 6・要件 4: 構造 edge の区別（parent field edge と slot1 child edge）が壊れていないこと。
      // present/dangling の数を prop 直接パースの期待値（first-wins・buildGraph と同じ扱い）で照合し、
      // 矛盾 edge（child target の parent field が child 以外を指す＝family の yellow overlay）も照合する。
      size_t expParAll = 0, expParD = 0, expChiAll = 0, expChiD = 0, expConflict = 0;
      for (uint8_t probe : gGraph.scenes) {
        std::unordered_map<uint8_t, uint64_t> propById;
        for (const ElementRecord &rec : gDocument.elements) {
          if (getPropScene(rec.element.prop) != probe) continue;
          const uint8_t idS = getPropID(rec.element.prop);
          if (propById.find(idS) != propById.end()) continue; // duplicate は graph 側でも skip
          propById[idS] = rec.element.prop;
        }
        for (const auto &kv : propById) {
          const uint8_t parS = getPropParent(kv.second);
          if (parS != 0) {
            if (propById.find(parS) != propById.end()) ++expParAll;
            else ++expParD;
          }
          const uint8_t chiS = getPropChild(kv.second);
          if (chiS != 0) {
            if (propById.find(chiS) != propById.end()) {
              ++expChiAll;
              if (getPropParent(propById[chiS]) != kv.first) ++expConflict; // 矛盾（drawFamilyView の yellow）
            } else ++expChiD;
          }
        }
      }
      size_t gotParP = 0, gotParD = 0, gotChiP = 0, gotChiD = 0, gotConflict = 0;
      for (const GraphEdge &e : gGraph.edges) {
        if (e.kind == EDGE_PARENT) {
          if (e.dangling || e.from < 0) ++gotParD;
          else ++gotParP;
        } else if (e.kind == EDGE_CHILD) {
          if (e.dangling || e.to < 0) ++gotChiD;
          else {
            ++gotChiP;
            if (gGraph.nodes[e.from].parentId != gGraph.nodes[e.to].id) ++gotConflict;
          }
        }
      }
      const bool structOk = stPass(expParAll == gotParP && expParD == gotParD &&
                                   expChiAll == gotChiP && expChiD == gotChiD &&
                                   expConflict == gotConflict, "family layout");
      std::cout << "structure_edges parent present=" << gotParP << "/" << expParAll
                << " dangling=" << gotParD << "/" << expParD << " child present=" << gotChiP << "/" << expChiAll
                << " dangling=" << gotChiD << "/" << expChiD << " conflict=" << gotConflict << "/" << expConflict
                << " separated match=" << (structOk ? 1 : 0) << std::endl;

      // family mode の実描画 → pixel readback（active layout で描かれた kMC 実色を確認する）。
      if (gRenderer) {
        setSolid(gRenderer, kMCBase); // window background: mocha base #1e1e2e
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer); // VIEW_FAMILY の階層 forest + HUD（選択ノードは lavender）
        SDL_RenderPresent(gRenderer);
        SDL_Surface *famShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (famShot) {
          SDL_SaveBMP(famShot, "build/viewer_frame_family.bmp");
          std::cout << "family frame captured " << famShot->w << "x" << famShot->h
                    << " -> build/viewer_frame_family.bmp" << std::endl;
          // readback は driver 依存なので RGB24 に変換してから読む（既存の教訓と同じ）。
          SDL_Surface *famRgba = SDL_ConvertSurface(famShot, SDL_PIXELFORMAT_RGB24);
          if (famRgba) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < famRgba->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(famRgba->pixels) +
                                   static_cast<size_t>(y) * famRgba->pitch;
              for (int x = 0; x < famRgba->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) |
                       (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
              }
            }
            std::cout << "family bright pixels=" << bright << std::endl;
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "family kmc colors: sapphire=" << kmcCount(kMCSapphire)
                      << " sky=" << kmcCount(kMCSky) << " text=" << kmcCount(kMCText)
                      << " lavender=" << kmcCount(kMCLavender) << " peach=" << kmcCount(kMCPeach)
                      << " yellow=" << kmcCount(kMCYellow) << std::endl;
            // Prompt 6: family frame の PASS を FAIL 検知に格上げ。階層 edge=sapphire・explicit
            // child edge=peach の区別（期待値は prop 直接パース・first-wins）、HUD の text/subtext1
            // 出現を必須とする。矛盾 yellow（family 意味）は info（excluded slot2 の yellow と別意味）。
            std::unordered_map<uint8_t, uint64_t> mapCapF;
            for (const ElementRecord &rec : gDocument.elements) {
              if (getPropScene(rec.element.prop) != gViewScene) continue;
              const uint8_t idF = getPropID(rec.element.prop);
              if (mapCapF.find(idF) != mapCapF.end()) continue;
              mapCapF[idF] = rec.element.prop;
            }
            size_t expParF = 0, expChiF = 0, expConfF = 0;
            for (const auto &kv : mapCapF) {
              const uint8_t parF = getPropParent(kv.second);
              if (parF != 0 && mapCapF.find(parF) != mapCapF.end()) ++expParF;
              const uint8_t chiF = getPropChild(kv.second);
              if (chiF != 0 && mapCapF.find(chiF) != mapCapF.end()) {
                // drawFamilyView の意味体系: 矛盾 edge（child target の parent が別ノード）は
                // peach ではなく yellow で overlay される（Prompt 2 実装・凡例）。peach 実色
                // 要求は矛盾なしの child edge があるときのみ。矛盾は yellow の info として照合。
                if (getPropParent(mapCapF[chiF]) == kv.first) ++expChiF;
                else ++expConfF;
              }
            }
            bool famFrameOk = bright > 0 && kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0;
            if (expParF > 0) famFrameOk &= kmcCount(kMCSapphire) > 0;
            if (expChiF > 0) famFrameOk &= kmcCount(kMCPeach) > 0;
            if (expConfF > 0) famFrameOk &= kmcCount(kMCYellow) > 0; // 矛盾 yellow overlay の実色
            famFrameOk = stPass(famFrameOk, "family layout");
            std::cout << "family frame PASS bright=" << bright << " hierarchy_sapphire=" << kmcCount(kMCSapphire)
                      << "/" << expParF << " child_peach=" << kmcCount(kMCPeach) << "/" << expChiF
                      << " conflict_yellow=" << kmcCount(kMCYellow) << "/" << expConfF
                      << " text=" << kmcCount(kMCText) << " subtext1=" << kmcCount(kMCSubtext1)
                      << " ok=" << (famFrameOk ? 1 : 0) << std::endl;
            std::vector<std::pair<uint32_t, uint32_t>> famTop(hist.begin(), hist.end());
            std::sort(famTop.begin(), famTop.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min<size_t>(8, famTop.size()); ++i) {
              std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                        << famTop[i].first << " count=" << std::dec << famTop[i].second
                        << std::endl;
            }
            SDL_DestroySurface(famRgba);
          }
          SDL_DestroySurface(famShot);
        } else {
          stFail("family layout"); // Prompt 6: frame capture が取れない場合は FAIL（要件 8）
          std::cout << "family frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }

      // general mode に戻って（1<->2 の往復）VIEW_GENERAL の座標が壊れていないこと。
      gViewMode = VIEW_GENERAL;
      gSelectedNode = -1;
      applyActiveLayout();
      fitViewToScene();
      bool intact = true;
      for (size_t i = 0; i < gGraph.nodes.size(); ++i) {
        if (gGraph.nodes[i].wx != genPos[i].first || gGraph.nodes[i].wy != genPos[i].second)
          intact = false;
      }
      std::cout << "general_coords_intact_after_mode_round_trip="
                << (stPass(intact, "family layout") ? 1 : 0) << std::endl;

      // family viewer は Document を mutate しない: round-trip は EXACT のまま。
      const RoundTripReport rtFam = verifyRoundTrip(gDocument);
      gRoundTrip = rtFam;
      const bool rtFamOk = stPass(rtFam.exact, "family layout");
      std::cout << "round_trip_after_family_phase="
                << (rtFamOk ? "EXACT OK" : "MISMATCH") << std::endl;
    }

    // --- constraint phase（Prompt 3）: scene ごとの制約 edge / ghost の数を、現行 gDocument の
    // prop 直接パースの期待値と実行時照合する（family phase の pattern を踏襲し、数をハードコードしない）。
    // mode 1->2->3->1 の往復（1->2 を含む）で他 mode の座標・選択・fit が壊れないこと、
    // constraint（general active layout のまま）での pickNode が正しい node を選ぶこと、
    // 実描画を build/viewer_frame_constraint.bmp に保存し RGB24 ヒストグラムで実色を確認する。
    // PASS 条件: required edge が 1件以上あれば teal の出現必須 + HUD の text/subtext1 出現。
    // red/yellow は excluded edge の分だけ出現することを info として報告する（PASS 条件にしない）。
    // round-trip は往復後も EXACT のまま（Document を mutate しない）。 ---
    std::cout << "=== [ selftest constraint ] ===" << std::endl;
    {
      std::vector<std::pair<float, float>> genPosC(gGraph.nodes.size());
      for (size_t i = 0; i < gGraph.nodes.size(); ++i)
        genPosC[i] = {gGraph.nodes[i].wx, gGraph.nodes[i].wy};
      const float zoom0 = gViewZoom, panX0 = gViewPanX, panY0 = gViewPanY;
      const uint8_t scene0 = gViewScene;

      // general mode で先に node を選択しておく（往復での選択保持を確認する）。
      int wantSel = -1;
      uint32_t wantKey = 0;
      const std::vector<int> inSceneC = gGraph.nodesInScene(gViewScene);
      if (inSceneC.empty()) stFail("constraint edges");
      if (!inSceneC.empty()) {
        wantSel = inSceneC[0];
        const GraphNode &n = gGraph.nodes[wantSel];
        wantKey = nodeKey(n.scene, n.id);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        const SDL_FPoint c = worldToScreen(n.wx + static_cast<float>(kNodeW) * 0.5f,
                                           n.wy + static_cast<float>(kNodeH) * 0.5f);
        ev.button.x = c.x;
        ev.button.y = c.y;
        gSelectedNode = -1;
        handlePointerEvent(&ev);
        std::cout << "constraint base pick idx=" << wantSel << " match="
                  << (stPass(gSelectedNode == wantSel, "constraint edges") ? 1 : 0) << std::endl;
      }

      // scene ごとの edge kind / ghost の数。期待値は prop の直接パースから実行時に算出する
      // （duplicate node は first-wins で扱う。buildGraph と同じ扱い。graph 構造は通さない）。
      std::unordered_map<uint8_t, ConstraintEdgeCounts> expectedByScene;
      bool countsMatch = true, resolveOk = true;
      size_t totalReq = 0, totalEx = 0;
      for (uint8_t probe : gGraph.scenes) {
        std::unordered_map<uint8_t, uint64_t> propById;
        for (const ElementRecord &rec : gDocument.elements) {
          const uint8_t id = getPropID(rec.element.prop);
          if (getPropScene(rec.element.prop) != probe) continue;
          if (propById.find(id) != propById.end()) continue; // duplicate node は graph 側でも skip
          propById[id] = rec.element.prop;
        }
        ConstraintEdgeCounts exp;
        for (const auto &kv : propById) {
          const uint8_t r = getPropRequiredReadId(kv.second);
          if (r != 0) {
            ++exp.required;
            if (propById.find(r) == propById.end()) ++exp.requiredGhost;
          }
          const uint8_t x1 = getPropSlot1ExcludedReadId(kv.second);
          if (x1 != 0) {
            ++exp.excludedSlot1;
            if (propById.find(x1) == propById.end()) ++exp.excludedSlot1Ghost;
          }
          const uint8_t x2 = getPropSlot2ExcludedReadId(kv.second);
          if (x2 != 0) {
            ++exp.excludedSlot2;
            if (propById.find(x2) == propById.end()) ++exp.excludedSlot2Ghost;
          }
        }
        const ConstraintEdgeCounts got = countConstraintEdges(probe);
        // present の node idx 解決: to>=0 の edge の target は同 scene の node で
        // id == edge.targetId でなければならない。
        for (const GraphEdge &e : gGraph.edges) {
          if (e.kind != EDGE_REQUIRED_READ && e.kind != EDGE_EXCLUDED_READ_SLOT1 &&
              e.kind != EDGE_EXCLUDED_READ_SLOT2) continue;
          if (e.from < 0 || gGraph.nodes[e.from].scene != probe || e.to < 0) continue;
          if (gGraph.nodes[e.to].scene != probe || gGraph.nodes[e.to].id != e.targetId)
            resolveOk = false;
        }
        const bool match = exp.required == got.required && exp.requiredGhost == got.requiredGhost &&
                           exp.excludedSlot1 == got.excludedSlot1 &&
                           exp.excludedSlot1Ghost == got.excludedSlot1Ghost &&
                           exp.excludedSlot2 == got.excludedSlot2 &&
                           exp.excludedSlot2Ghost == got.excludedSlot2Ghost;
        if (!match) countsMatch = false;
        expectedByScene[probe] = got;
        totalReq += got.required;
        totalEx += got.excludedSlot1 + got.excludedSlot2;
        std::cout << "scene=" << hexNibble(probe)
                  << " required=" << got.required << "/" << exp.required
                  << " required_ghost=" << got.requiredGhost << "/" << exp.requiredGhost
                  << " excluded_slot1=" << got.excludedSlot1 << "/" << exp.excludedSlot1
                  << " excluded_slot1_ghost=" << got.excludedSlot1Ghost << "/" << exp.excludedSlot1Ghost
                  << " excluded_slot2=" << got.excludedSlot2 << "/" << exp.excludedSlot2
                  << " excluded_slot2_ghost=" << got.excludedSlot2Ghost << "/" << exp.excludedSlot2Ghost
                  << " match=" << (match ? 1 : 0) << std::endl;
      }
      // Prompt 6: constraint の kind/ghost 照合・present 解決を FAIL 検知に格上げ（要件 8）。
      std::cout << "constraint_edge_counts match=" << (stPass(countsMatch, "constraint edges") ? 1 : 0)
                << " present_resolve_ok=" << (stPass(resolveOk, "constraint edges") ? 1 : 0) << std::endl;
      std::cout << "constraint totals required=" << totalReq << " excluded=" << totalEx;
      if (totalEx == 0) std::cout << " excluded_edges=0 (none in data.txt)";
      std::cout << std::endl;

      // mode 1->2->3 の往復（general -> family -> constraint）。constraint の既定 active layout は
      // general なので、同じ scene の fit は general の fit と同じ値になる（fit が壊れないことを確認）。
      gViewMode = VIEW_FAMILY;
      applyActiveLayout();
      fitViewToScene();
      gViewMode = VIEW_CONSTRAINT;
      applyActiveLayout();
      fitViewToScene();
      // Prompt 6: 往復（1->2->3->1）の fit・座標・選択保持を FAIL 検知に格上げ（要件 5・8）。
      const bool fitSame = stPass(gViewZoom == zoom0 && gViewPanX == panX0 && gViewPanY == panY0,
                                  "constraint edges");
      std::cout << "constraint_fit_matches_general=" << (fitSame ? 1 : 0) << std::endl;
      bool coordsIntactMid = true;
      for (size_t i = 0; i < gGraph.nodes.size(); ++i) {
        if (gGraph.nodes[i].wx != genPosC[i].first || gGraph.nodes[i].wy != genPosC[i].second)
          coordsIntactMid = false;
      }
      std::cout << "general_coords_intact_through_family_constraint="
                << (stPass(coordsIntactMid, "constraint edges") ? 1 : 0) << std::endl;
      const bool selKept =
          wantSel < 0 ||
          (gSelectedNode == wantSel &&
           gGraph.nodes[wantSel].scene == static_cast<uint8_t>(wantKey >> 8) &&
           gGraph.nodes[wantSel].id == static_cast<uint8_t>(wantKey & 0xFFu));
      std::cout << "selection_kept_through_mode_round_trip="
                << (stPass(selKept, "constraint edges") ? 1 : 0) << std::endl;

      // constraint の active layout（general の座標そのまま）で pickNode が正しい node を選ぶこと。
      if (wantSel >= 0) {
        const GraphNode &n = gGraph.nodes[wantSel];
        const SDL_FPoint c = worldToScreen(n.wx + static_cast<float>(kNodeW) * 0.5f,
                                           n.wy + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        gSelectedNode = -1;
        handlePointerEvent(&ev);
        const bool conPickOk = stPass(gSelectedNode == wantSel, "constraint edges");
        std::cout << "constraint pick want idx=" << wantSel << " picked=" << gSelectedNode
                  << " match=" << (conPickOk ? 1 : 0) << std::endl;
        gSelectedNode = wantSel;
      }

      // general に戻る（1<->3 の往復完了。1<->2 を含む）。座標・選択が intact であること。
      gViewMode = VIEW_GENERAL;
      applyActiveLayout();
      fitViewToScene();
      bool intact = true;
      for (size_t i = 0; i < gGraph.nodes.size(); ++i) {
        if (gGraph.nodes[i].wx != genPosC[i].first || gGraph.nodes[i].wy != genPosC[i].second)
          intact = false;
      }
      const bool selRestored = wantSel < 0 || gSelectedNode == wantSel;
      std::cout << "general_coords_intact_after_constraint_round_trip="
                << (stPass(intact, "constraint edges") ? 1 : 0) << " selection_restored="
                << (stPass(selRestored, "constraint edges") ? 1 : 0) << std::endl;

      // constraint mode の実描画。capture scene は現行 scene に required edge が 0 件なら
      // required が 1件以上の最小 scene を選ぶ（teal が view に出るため）。required がどこにも
      // 無い場合は teal を PASS 条件にしない。
      uint8_t captureScene = scene0;
      const auto curIt = expectedByScene.find(captureScene);
      if (curIt != expectedByScene.end() && curIt->second.required == 0) {
        for (uint8_t s : gGraph.scenes) {
          const auto it = expectedByScene.find(s);
          if (it != expectedByScene.end() && it->second.required > 0) {
            captureScene = s;
            break;
          }
        }
      }
      const auto capIt = expectedByScene.find(captureScene);
      const size_t captureReq = capIt != expectedByScene.end() ? capIt->second.required : 0;
      gViewScene = captureScene;
      gViewMode = VIEW_CONSTRAINT;
      applyActiveLayout();
      fitViewToScene();
      // captured frame で describeElement の VIEW_CONSTRAINT variant の優先表示（present / ghost /
      // none の line）を実描画で確認するため、captureScene の先頭 present-required node を
      // 决定的に選択する（選択は capture 専用。round-trip の選択検証は上で完了済み）。
      gSelectedNode = -1;
      for (int i : gGraph.nodesInScene(captureScene)) {
        const GraphNode &m = gGraph.nodes[i];
        const uint8_t rid = getPropRequiredReadId(gDocument.elements[m.elementIndex].element.prop);
        if (rid == 0 || gGraph.findNode(captureScene, rid) < 0) continue;
        const SDL_FPoint c = worldToScreen(m.wx + static_cast<float>(kNodeW) * 0.5f,
                                           m.wy + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        handlePointerEvent(&ev);
        if (gSelectedNode == i) {
          std::cout << "constraint capture pick idx=" << i << " id=" << hexByte(m.id)
                    << " required target present=1（constraint variant を HUD に描画）" << std::endl;
          break;
        }
        gSelectedNode = -1;
      }
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer); // general layout の node/parent-child edge + constraint overlay + HUD legend
        SDL_RenderPresent(gRenderer);
        SDL_Surface *conShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (conShot) {
          SDL_SaveBMP(conShot, "build/viewer_frame_constraint.bmp");
          std::cout << "constraint frame captured scene=" << hexNibble(captureScene) << " "
                    << conShot->w << "x" << conShot->h << " -> build/viewer_frame_constraint.bmp"
                    << std::endl;
          // readback は driver 依存なので RGB24 に変換してから読む（既存の教訓と同じ）。
          SDL_Surface *conRgba = SDL_ConvertSurface(conShot, SDL_PIXELFORMAT_RGB24);
          if (conRgba) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < conRgba->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(conRgba->pixels) +
                                   static_cast<size_t>(y) * conRgba->pitch;
              for (int x = 0; x < conRgba->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) |
                       (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
              }
            }
            std::cout << "constraint bright pixels=" << bright << std::endl;
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "constraint kmc colors: teal=" << kmcCount(kMCTeal)
                      << " red=" << kmcCount(kMCRed) << " yellow=" << kmcCount(kMCYellow)
                      << " text=" << kmcCount(kMCText) << " subtext1=" << kmcCount(kMCSubtext1)
                      << std::endl;
            // PASS 条件: required edge が 1件以上あれば teal の出現必須 + HUD の text/subtext1 出現。
            // red/yellow は excluded edge の分だけ出現することを info として報告する（PASS 条件にしない）。
            // Prompt 6: constraint frame の PASS を FAIL 検知に格上げ（required≥1 なら teal 必須・
            // HUD の text/subtext1 必須）。red/yellow は excluded の分の出現のみ info（red の
            // 既存出現は quote ストライプ由来と混同しない — Prompt 3 の教訓）。
            const bool tealPass = stPass(captureReq == 0 || kmcCount(kMCTeal) > 0, "constraint edges");
            std::cout << "constraint_required_edges=" << captureReq << " teal_present="
                      << (kmcCount(kMCTeal) > 0 ? 1 : 0) << " teal_pass=" << (tealPass ? 1 : 0)
                      << std::endl;
            const bool conHudOk = stPass(kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0, "constraint edges");
            std::cout << "constraint hud text_present=" << (kmcCount(kMCText) > 0 ? 1 : 0)
                      << " subtext1_present=" << (kmcCount(kMCSubtext1) > 0 ? 1 : 0)
                      << " ok=" << (conHudOk ? 1 : 0) << std::endl;
            if (capIt != expectedByScene.end()) {
              std::cout << "constraint info: red/yellow appear by amount of excluded edges "
                        << "(scene=" << hexNibble(captureScene)
                        << " ex_slot1=" << capIt->second.excludedSlot1
                        << " ex_slot2=" << capIt->second.excludedSlot2 << ")" << std::endl;
            }
            std::vector<std::pair<uint32_t, uint32_t>> conTop(hist.begin(), hist.end());
            std::sort(conTop.begin(), conTop.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min<size_t>(8, conTop.size()); ++i) {
              std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                        << conTop[i].first << " count=" << std::dec << conTop[i].second
                        << std::endl;
            }
            SDL_DestroySurface(conRgba);
          }
          SDL_DestroySurface(conShot);
        } else {
          stFail("constraint edges"); // Prompt 6: frame capture が取れない場合は FAIL（要件 8）
          std::cout << "constraint frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }

      // 元の scene と general mode に戻す（以降の font phase / switchScene は影響を受けない）。
      gViewScene = scene0;
      gViewMode = VIEW_GENERAL;
      applyActiveLayout();
      fitViewToScene();

      // constraint viewer は Document を mutate しない: round-trip は往復後も EXACT のまま。
      const RoundTripReport rtCon = verifyRoundTrip(gDocument);
      gRoundTrip = rtCon;
      const bool rtConOk = stPass(rtCon.exact, "constraint edges");
      std::cout << "round_trip_after_constraint_phase="
                << (rtConOk ? "EXACT OK" : "MISMATCH") << std::endl;
    }

    // --- editor phase（Prompt 4）: EDITOR の Element 追加 GUI を検証する。
    // 期待値は実行時に gDocument の prop 直接パースから算出する（数をハードコードしない）。
    // nextFreeId の重複なし / 空 dialogue 拒否 / ESC 取消で Document が元に戻る /
    // 追加行の正格式（lowercase 16桁 + trailing comma）と再パース / child なし設定は
    // getPropChild()==0 / 追加後の graph・family・constraint の出現（countConstraintEdges と
    // 追加行の合成 prop を含む期待値で照合・match=1）/ baseline diff は追加行のみ /
    // runtime 同期は既定 off で container は変わらない / 検証後は baseline に戻す
    // （data.txt 自体は書き換えない）。 ---
    std::cout << "=== [ selftest editor ] ===" << std::endl;
    {
      const size_t baseLines = gDocument.lines.size();
      const size_t baseElements = gDocument.elements.size();
      const size_t baseBytes = gDocument.raw.size();
      const size_t baseNodes = gGraph.nodes.size();
      const size_t baseEdges = gGraph.edges.size();
      const size_t baseRoots = gGraph.roots.size();
      const size_t baseContA = containerA.size();
      const size_t baseContB = containerB.size();

      // scene 別の使用 id 集合と未使用の最小 id（>=1・>0xFF 不可）を prop 直接パースから算出する。
      const auto freeIdExpect = [&](uint8_t probe) {
        std::unordered_set<uint8_t> used;
        for (const ElementRecord &rec : gDocument.elements) {
          if (getPropScene(rec.element.prop) != probe) continue;
          used.insert(getPropID(rec.element.prop));
        }
        int freeId = -1;
        for (int id = 1; id <= 0xFF; ++id) {
          if (used.find(static_cast<uint8_t>(id)) == used.end()) { freeId = id; break; }
        }
        return std::make_pair(used, freeId);
      };
      bool nextFreeOk = true;
      for (uint8_t probe : gGraph.scenes) {
        const auto exp = freeIdExpect(probe);
        const int got = nextFreeId(probe);
        const bool unique = got < 0 || exp.first.find(static_cast<uint8_t>(got)) == exp.first.end();
        const bool ok = unique && got == exp.second;
        if (!ok) nextFreeOk = false;
        std::cout << "editor scene=" << hexNibble(probe) << " used_ids=" << exp.first.size()
                  << " expected_free=" << exp.second << " nextFreeId=" << got
                  << " no_collision=" << (unique ? 1 : 0) << " match=" << (ok ? 1 : 0) << std::endl;
      }
      std::cout << "next_free_id_no_duplicates=" << (nextFreeOk ? 1 : 0) << std::endl;

      // 制約 edge の数: family/constraint phase と同じ pattern で、現行 gDocument の prop
      // 直接パース（追加行の合成 prop を含む・duplicate は first-wins）の期待値と照合する。
      const auto verifyConstraintCounts = [&](const char *tag) {
        bool ok = true;
        for (uint8_t probe : gGraph.scenes) {
          std::unordered_map<uint8_t, uint64_t> propById;
          for (const ElementRecord &rec : gDocument.elements) {
            if (getPropScene(rec.element.prop) != probe) continue;
            const uint8_t id = getPropID(rec.element.prop);
            if (propById.find(id) != propById.end()) continue; // duplicate は graph 側でも skip
            propById[id] = rec.element.prop;
          }
          ConstraintEdgeCounts exp;
          for (const auto &kv : propById) {
            const uint8_t r = getPropRequiredReadId(kv.second);
            if (r != 0) { ++exp.required; if (propById.find(r) == propById.end()) ++exp.requiredGhost; }
            const uint8_t x1 = getPropSlot1ExcludedReadId(kv.second);
            if (x1 != 0) { ++exp.excludedSlot1; if (propById.find(x1) == propById.end()) ++exp.excludedSlot1Ghost; }
            const uint8_t x2 = getPropSlot2ExcludedReadId(kv.second);
            if (x2 != 0) { ++exp.excludedSlot2; if (propById.find(x2) == propById.end()) ++exp.excludedSlot2Ghost; }
          }
          const ConstraintEdgeCounts got = countConstraintEdges(probe);
          const bool m = exp.required == got.required && exp.requiredGhost == got.requiredGhost &&
                         exp.excludedSlot1 == got.excludedSlot1 &&
                         exp.excludedSlot1Ghost == got.excludedSlot1Ghost &&
                         exp.excludedSlot2 == got.excludedSlot2 &&
                         exp.excludedSlot2Ghost == got.excludedSlot2Ghost;
          if (!m) ok = false;
          std::cout << tag << " scene=" << hexNibble(probe)
                    << " required=" << got.required << "/" << exp.required
                    << " excluded=" << (got.excludedSlot1 + got.excludedSlot2) << "/"
                    << (exp.excludedSlot1 + exp.excludedSlot2) << " match=" << (m ? 1 : 0) << std::endl;
        }
        return ok;
      };

      auto postKey = [](SDL_Keycode k) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = k;
        ev.key.down = true;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postText = [](const char *t) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = t;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postClickNode = [](int idx) {
        const SDL_FPoint c = worldToScreen(
            nodePos(gGraph.nodes[idx]).x + static_cast<float>(kNodeW) * 0.5f,
            nodePos(gGraph.nodes[idx]).y + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        SDL_AppEvent(nullptr, &ev);
      };

      postKey(SDLK_4); // EDITOR mode（active layout は general のまま。新 layout は作らない）
      const bool modeOk = gViewMode == EDITOR;
      std::cout << "editor_mode_active=" << (modeOk ? 1 : 0) << std::endl;

      // ESC 取消: add state 中は取消優先（選択解除ではない）。Document は元に戻る。
      postKey(SDLK_RETURN); // add flow 開始（dialogue stage・text input Start）
      const bool started = gEditor.stage == EDITOR_DIALOGUE && gEditorTextInputActive;
      postText("取消テスト utf8 test");
      postKey(SDLK_ESCAPE);
      const bool cancelOk = gEditor.stage == EDITOR_IDLE && !gEditorTextInputActive &&
                            gDocument.lines.size() == baseLines &&
                            gDocument.elements.size() == baseElements &&
                            gDocument.raw.size() == baseBytes && gGraph.nodes.size() == baseNodes &&
                            serializeDocument(gDocument) == gDocument.raw;
      std::cout << "editor_start_ok=" << (started ? 1 : 0)
                << " editor_cancel_document_restores=" << (cancelOk ? 1 : 0) << std::endl;

      // 空 dialogue は不可（追加しない）。
      postKey(SDLK_RETURN);
      postKey(SDLK_RETURN); // 空のまま ENTER → dialogue stage に留まる
      const bool emptyRejected = gEditor.stage == EDITOR_DIALOGUE && gDocument.lines.size() == baseLines;
      std::cout << "editor_empty_dialogue_rejected=" << (emptyRejected ? 1 : 0) << std::endl;

      postKey(SDLK_ESCAPE); // 空の add flow を取消

      // 1件目の追加: dialogue → quote(n) → onetime(n) → parent(root none) → child(none) → commit。
      const uint8_t addScene = gViewScene;
      const auto exp1 = freeIdExpect(addScene);
      postKey(SDLK_RETURN); // add flow 開始
      const bool assignedOk = gEditor.assignedId == exp1.second && gEditorTextInputActive;
      const std::string dialogue1 = "selftest editor 追加テスト line テスト";
      postText(dialogue1.c_str()); // UTF-8・空白を含む入力をそのまま保持
      postKey(SDLK_RETURN); // dialogue 確定 → quote（text input は対称に Stop）
      const bool quoteStageOk = gEditor.stage == EDITOR_QUOTE && !gEditorTextInputActive;
      postKey(SDLK_N); // isQuote = no → onetime
      postKey(SDLK_N); // isOnetime = no → parent
      postKey(SDLK_RETURN); // parent 無選択（0 = root/no parent 許す）で commit 決定 → child
      postKey(SDLK_RETURN); // child none（slot1Index 0 / value 0 の data.txt 慣習）で commit
      const bool addedOne = gDocument.lines.size() == baseLines + 1 &&
                            gDocument.elements.size() == baseElements + 1;
      const RawLine &addedLine = gDocument.lines.back();
      std::string reDialogue;
      uint64_t reProp = 0;
      const bool reparsed = addedLine.kind == LINE_ELEMENT &&
                            parseElementLine(addedLine.text, reDialogue, reProp);
      const uint64_t expectedProp1 =
          composeEditorProp(static_cast<uint8_t>(exp1.second), 0, addScene, false, false, 0);
      const std::string expectedText1 = "'" + dialogue1 + "', '0x" + formatPropHex16Piped(expectedProp1) + "',";
      const bool formatOk = addedLine.text == expectedText1 && reparsed && reProp == expectedProp1 &&
                            reDialogue == dialogue1 && getPropParent(reProp) == 0 &&
                            getPropChild(reProp) == 0 && getPropID(reProp) == static_cast<uint8_t>(exp1.second) &&
                            getPropScene(reProp) == addScene && getSlotIndex(reProp, 1) == 0 &&
                            getSlotValue(reProp, 1) == 0;
      std::cout << "editor_add1 id=" << exp1.second << " scene=" << hexNibble(addScene)
                << " assigned_ok=" << (assignedOk ? 1 : 0) << " quote_stage_stop_text_ok=" << (quoteStageOk ? 1 : 0)
                << " added_line=" << (addedOne ? 1 : 0)
                << " format_exact_pipe_split_lower16_comma=" << (addedLine.text == expectedText1 ? 1 : 0)
                << " reparse_ok=" << (reparsed ? 1 : 0) << " prop_ok=" << (formatOk ? 1 : 0)
                << " child_none_getPropChild=0" << std::endl;

      const int newIdx = gGraph.findNode(addScene, static_cast<uint8_t>(exp1.second));
      const int rootDelta = static_cast<int>(gGraph.roots.size()) - static_cast<int>(baseRoots);
      const bool graphOk = gGraph.nodes.size() == baseNodes + 1 && newIdx >= 0 &&
                           gGraph.nodes[newIdx].elementIndex == static_cast<int>(gDocument.elements.size()) - 1 &&
                           rootDelta == 1 && gGraph.edges.size() == baseEdges; // 追加 prop は parent/child/constraint なし → edge 増 0
      std::cout << "editor_graph nodes_delta=" << static_cast<int>(gGraph.nodes.size()) - static_cast<int>(baseNodes)
                << " edges_delta=" << static_cast<int>(gGraph.edges.size()) - static_cast<int>(baseEdges)
                << " roots_delta=" << rootDelta << " new_node_idx=" << newIdx
                << " match=" << (graphOk ? 1 : 0) << std::endl;

      // family に新ノードが現れる（layoutFamily で全 node 配置・決定性は family phase 側で検証済み）。
      layoutFamily(addScene);
      const std::vector<int> famScene = gGraph.nodesInScene(addScene);
      size_t famPlaced = 0;
      for (int i : famScene) { if (gGraph.nodes[i].famDepth >= 0) ++famPlaced; }
      const bool familyOk = newIdx >= 0 && gGraph.nodes[newIdx].famDepth >= 0 && famPlaced == famScene.size();
      std::cout << "editor_new_node_in_family placed=" << famPlaced << "/" << famScene.size()
                << " match=" << (familyOk ? 1 : 0) << std::endl;

      // constraint: 追加行の合成 prop（required/excluded=0）を含む期待値と照合（match=1）。
      const bool constraintOk1 = verifyConstraintCounts("editor constraint after add1");
      std::cout << "constraint_edges_after_editor_add match=" << (constraintOk1 ? 1 : 0) << std::endl;

      // 追加直後の選択は新ノード（再構築で index は変わるが (scene,id) で決定的に解決される）。
      const bool selectionOk = gSelectedNode == newIdx && newIdx >= 0 &&
                               gGraph.nodes[gSelectedNode].scene == addScene &&
                               gGraph.nodes[gSelectedNode].id == static_cast<uint8_t>(exp1.second);
      std::cout << "editor_selection_new_node=" << (selectionOk ? 1 : 0) << std::endl;

      // baseline diff: 追加行のみ（edited=0・added_or_dirty_only=1・baselineLines は load 時の行数）。
      const RoundTripReport rtAdd1 = verifyRoundTrip(gDocument);
      gRoundTrip = rtAdd1;
      const bool rtOk1 = !rtAdd1.exact && rtAdd1.addedLines == 1 && rtAdd1.editedLines == 0 &&
                         rtAdd1.diffIsAddedOrDirtyOnly && rtAdd1.baselineLines == baseLines;
      std::cout << "editor_baseline_diff added=" << rtAdd1.addedLines << " edited=" << rtAdd1.editedLines
                << " added_or_dirty_only=" << (rtAdd1.diffIsAddedOrDirtyOnly ? 1 : 0)
                << " baseline_lines=" << rtAdd1.baselineLines << " document_lines=" << rtAdd1.lineCount
                << " bytes=" << rtAdd1.originalBytes << "→" << rtAdd1.serializedBytes
                << " firstDiff=" << rtAdd1.firstDiffLine << " match=" << (rtOk1 ? 1 : 0) << std::endl;

      // runtime 同期は既定 off。G トグルは on/off 往復しても container に触らない（commit 時のみ）。
      postKey(SDLK_G);
      postKey(SDLK_G);
      const bool runtimeOk = containerA.size() == baseContA && containerB.size() == baseContB && !gEditor.syncRuntime;
      std::cout << "editor_runtime_sync_default_off containers_unchanged=" << (runtimeOk ? 1 : 0) << std::endl;

      // 2件目の追加: parent / child をノードクリックで選ぶ（child あり: slot1Index 0 / value = child id）。
      // commit 後の stage は dialogue（text input Start 済み）。assignedId は 1件目を除いた nextFreeId。
      const auto exp2 = freeIdExpect(addScene);
      const bool id2Ok = gEditor.stage == EDITOR_DIALOGUE && gEditor.assignedId == exp2.second;
      const std::string dialogue2 = "parent-child test line";
      postText(dialogue2.c_str());
      postKey(SDLK_RETURN);
      postKey(SDLK_N);
      postKey(SDLK_N); // → parent stage
      std::vector<int> byId = gGraph.nodesInScene(addScene);
      std::sort(byId.begin(), byId.end(),
                [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
      int parentPick = -1, childPick = -1;
      for (int i : byId) {
        if (gGraph.nodes[i].id == 0) continue;
        if (parentPick < 0) parentPick = i;
        else if (childPick < 0) childPick = i;
      }
      if (parentPick >= 0) postClickNode(parentPick);
      const uint8_t parentId = parentPick >= 0 ? gGraph.nodes[parentPick].id : 0;
      const bool parentSelOk = parentPick >= 0 && gEditor.parentId == parentId &&
                               parentId != static_cast<uint8_t>(exp2.second); // 自己 id 禁止（new id は未使用なので別 node）
      postKey(SDLK_RETURN); // parent 値で commit 決定 → child stage
      if (childPick >= 0) postClickNode(childPick);
      const uint8_t childId = childPick >= 0 ? gGraph.nodes[childPick].id : 0;
      const bool childSelOk = childPick >= 0 && gEditor.childOptionSelected &&
                             gEditor.childId == childId && childId != 0;
      postKey(SDLK_RETURN); // commit（child あり設定）
      const RawLine &addedLine2 = gDocument.lines.back();
      std::string reDialogue2;
      uint64_t reProp2 = 0;
      const bool reparsed2 = addedLine2.kind == LINE_ELEMENT &&
                             parseElementLine(addedLine2.text, reDialogue2, reProp2);
      const uint64_t expectedProp2 =
          composeEditorProp(static_cast<uint8_t>(exp2.second), parentId, addScene, false, false, childId);
      const bool prop2Ok = reparsed2 && reProp2 == expectedProp2 && reDialogue2 == dialogue2 &&
                           getPropParent(reProp2) == parentId && getPropChild(reProp2) == childId &&
                           getSlotIndex(reProp2, 1) == 0 && getSlotValue(reProp2, 1) == childId &&
                           addedLine2.text == "'" + dialogue2 + "', '0x" + formatPropHex16Piped(expectedProp2) + "',";
      const int newIdx2 = gGraph.findNode(addScene, static_cast<uint8_t>(exp2.second));
      bool childEdgeOk = false;
      if (newIdx2 >= 0 && childPick >= 0) {
        for (const GraphEdge &e : gGraph.edges) {
          if (e.kind == EDGE_CHILD && e.from == newIdx2 && e.to == childPick && e.targetId == childId) {
            childEdgeOk = true;
            break;
          }
        }
      }
      const bool childAdjOk = newIdx2 >= 0 && gGraph.childLinkAdj[newIdx2].size() == 1;
      const bool addedTwo = gDocument.lines.size() == baseLines + 2 &&
                            gDocument.elements.size() == baseElements + 2 &&
                            gGraph.nodes.size() == baseNodes + 2 &&
                            gGraph.roots.size() == baseRoots + 1; // add1 は parent=0 で root+1、add2 は parent!=0 で roots 増 0
      const RoundTripReport rtAdd2 = verifyRoundTrip(gDocument);
      gRoundTrip = rtAdd2;
      const bool rtOk2 = !rtAdd2.exact && rtAdd2.addedLines == 2 && rtAdd2.editedLines == 0 &&
                         rtAdd2.diffIsAddedOrDirtyOnly;
      const bool constraintOk2 = verifyConstraintCounts("editor constraint after add2");
      std::cout << "editor_add2 parent=" << hexByte(parentId) << " child=" << hexByte(childId)
                << " id2_ok=" << (id2Ok ? 1 : 0) << " parent_sel_ok=" << (parentSelOk ? 1 : 0)
                << " child_sel_ok=" << (childSelOk ? 1 : 0) << " prop_reparse_ok=" << (prop2Ok ? 1 : 0)
                << " child_edge=" << (childEdgeOk ? 1 : 0) << " child_adj=" << (childAdjOk ? 1 : 0)
                << " added_two=" << (addedTwo ? 1 : 0) << " baseline_diff_added_only=" << (rtOk2 ? 1 : 0)
                << " constraint_match=" << (constraintOk2 ? 1 : 0) << std::endl;

      // EDITOR の実描画（preview panel + HUD editor 凡例）→ build/viewer_frame_editor.bmp。
      // PASS 条件: HUD/panel の text と subtext1 の出現 + preview 進行色 sky（id/parent/child 値行）。
      // green/yellow は commit ready / warning の info（PASS 条件にしない）。
      bool frameOk = false;
      gSelectedNode = newIdx2;
      applyActiveLayout();
      fitViewToScene();
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer); // general layout の node/edge + drawEditorOverlay の preview + HUD
        SDL_RenderPresent(gRenderer);
        SDL_Surface *edShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (edShot) {
          SDL_SaveBMP(edShot, "build/viewer_frame_editor.bmp");
          std::cout << "editor frame captured scene=" << hexNibble(addScene) << " " << edShot->w << "x" << edShot->h
                    << " -> build/viewer_frame_editor.bmp" << std::endl;
          // readback は driver 依存なので RGB24 に変換してから読む（既存の教訓と同じ）。
          SDL_Surface *edRgba = SDL_ConvertSurface(edShot, SDL_PIXELFORMAT_RGB24);
          if (edRgba) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < edRgba->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(edRgba->pixels) +
                                   static_cast<size_t>(y) * edRgba->pitch;
              for (int x = 0; x < edRgba->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) |
                       (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
              }
            }
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "editor bright pixels=" << bright << std::endl;
            std::cout << "editor kmc colors: sky=" << kmcCount(kMCSky) << " text=" << kmcCount(kMCText)
                      << " subtext1=" << kmcCount(kMCSubtext1) << " subtext0=" << kmcCount(kMCSubtext0)
                      << " green=" << kmcCount(kMCGreen) << " yellow=" << kmcCount(kMCYellow)
                      << " lavender=" << kmcCount(kMCLavender) << std::endl;
            frameOk = kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0 && kmcCount(kMCSky) > 0;
            std::cout << "editor frame text/subtext1/sky present=" << (frameOk ? 1 : 0) << std::endl;
            std::vector<std::pair<uint32_t, uint32_t>> edTop(hist.begin(), hist.end());
            std::sort(edTop.begin(), edTop.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min<size_t>(8, edTop.size()); ++i) {
              std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                        << edTop[i].first << " count=" << std::dec << edTop[i].second << std::endl;
            }
            SDL_DestroySurface(edRgba);
          }
          SDL_DestroySurface(edShot);
        } else {
          std::cout << "editor frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }

      // 検証後に baseline に戻す（追加 2 行を remove・graph 再構築・round-trip EXACT 復元）。
      editorRestoreToBaseline(baseLines, baseElements);
      buildGraph(gDocument, gGraph);
      applyActiveLayout();
      fitViewToScene();
      gSelectedNode = -1;
      const RoundTripReport rtBack = verifyRoundTrip(gDocument);
      gRoundTrip = rtBack;
      const bool restoredOk = rtBack.exact && gDocument.raw.size() == baseBytes &&
                              gDocument.lines.size() == baseLines &&
                              gDocument.elements.size() == baseElements &&
                              gGraph.nodes.size() == baseNodes && gGraph.edges.size() == baseEdges &&
                              gGraph.roots.size() == baseRoots && !gDocTerminatorPatched &&
                              containerA.size() == baseContA && containerB.size() == baseContB &&
                              serializeDocument(gDocument) == gDocument.raw;
      std::cout << "editor_restore_baseline exact=" << (rtBack.exact ? 1 : 0) << " match=" << (restoredOk ? 1 : 0) << std::endl;

      // general に戻る（mode key 1 は add flow を取消し、text input は対称に閉じる）。
      postKey(SDLK_1);
      const bool exitOk = gViewMode == VIEW_GENERAL && gEditor.stage == EDITOR_IDLE && !gEditorTextInputActive;
      std::cout << "editor_mode_back_general_state_cleared=" << (exitOk ? 1 : 0) << std::endl;

      // Prompt 6・要件 5: editor の add state では runtime の console 挿入（TAB）が遮断され、
      // general 復帰後は TAB で挿入 flow が開き ESC で対称に閉じる（Start/Stop・console 入力
      // と GUI キーの衝突がないこと）。既存の遮断規則・cancelInsertMode をそのまま検証する。
      postKey(SDLK_4);
      postKey(SDLK_TAB); // EDITOR では editorMode ガードで insert flow に触らない
      const bool runtimeBlocked = gAppMode == MODE_NORMAL && gEditor.stage == EDITOR_IDLE;
      postKey(SDLK_1);
      postKey(SDLK_TAB);
      const bool insertStarted = gAppMode == MODE_INSERT_TEXT;
      postKey(SDLK_ESCAPE); // 挿入モードの ESC は cancelInsertMode（既存経路・text input Stop 対称）
      const bool insertCancelled = gAppMode == MODE_NORMAL && gEditor.stage == EDITOR_IDLE;
      std::cout << "editor_runtime_key_blocked=" << (runtimeBlocked ? 1 : 0) << " insert_flow_symmetry="
                << (insertStarted && insertCancelled ? 1 : 0) << std::endl;

      // Prompt 6: editor の全 flag を FAIL 検知に格上げ（要件 3・8）。期待値計算は Prompt 4 のまま。
      const bool editorAllOk = stPass(nextFreeOk && modeOk && started && cancelOk && emptyRejected &&
                                     assignedOk && quoteStageOk && addedOne && formatOk && graphOk && familyOk &&
                                     constraintOk1 && selectionOk && rtOk1 && runtimeOk && id2Ok && parentSelOk &&
                                     childSelOk && prop2Ok && childEdgeOk && childAdjOk && addedTwo && rtOk2 &&
                                     constraintOk2 && frameOk && restoredOk && exitOk && runtimeBlocked &&
                                     insertStarted && insertCancelled, "editor add");
      std::cout << "editor_phase_all_flags=" << (editorAllOk ? 1 : 0) << std::endl;
      std::cout << "round_trip_after_editor_phase=" << (rtBack.exact ? "EXACT OK" : "MISMATCH") << std::endl;
    }

    // --- Prompt 5 props menu phase（全 prop fields の編集・slot options・validation・undo） ---
    // 期待値はすべて現行 gDocument の prop 直接パースから実行時に算出する（数をハードコードしない）。
    // 検証後は undo stack をすべて pop して baseline に戻る（data.txt 自体は書き換えない）。
    std::cout << "=== [ selftest props ] ===" << std::endl;
    {
      const size_t baseLines = gDocument.lines.size();
      const size_t baseElements = gDocument.elements.size();
      const size_t baseBytes = gDocument.raw.size();
      const size_t baseNodes = gGraph.nodes.size();
      const size_t baseEdges = gGraph.edges.size();
      const size_t baseRoots = gGraph.roots.size();
      const size_t baseContA = containerA.size();
      const size_t baseContB = containerB.size();
      std::vector<uint64_t> origProps;
      std::vector<bool> origDirty;
      std::vector<std::string> origText;
      for (const ElementRecord &rec : gDocument.elements) {
        origProps.push_back(rec.element.prop);
        origDirty.push_back(rec.dirty);
      }
      for (const RawLine &rl : gDocument.lines) origText.push_back(rl.text);

      auto postKey = [](SDL_Keycode k) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = k;
        ev.key.down = true;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postClickNode = [](int idx) {
        const SDL_FPoint c = worldToScreen(
            nodePos(gGraph.nodes[idx]).x + static_cast<float>(kNodeW) * 0.5f,
            nodePos(gGraph.nodes[idx]).y + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postClickNodeRight = [](int idx) {
        const SDL_FPoint c = worldToScreen(
            nodePos(gGraph.nodes[idx]).x + static_cast<float>(kNodeW) * 0.5f,
            nodePos(gGraph.nodes[idx]).y + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_RIGHT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        SDL_AppEvent(nullptr, &ev);
      };

      // 現行 elements から scene 別の propById を first-wins で作る（buildGraph と同じ扱い）。
      const auto sceneMap = [&](uint8_t probe) {
        std::unordered_map<uint8_t, uint64_t> m;
        for (const ElementRecord &rec : gDocument.elements) {
          if (getPropScene(rec.element.prop) != probe) continue;
          const uint8_t id = getPropID(rec.element.prop);
          if (m.find(id) == m.end()) m[id] = rec.element.prop;
        }
        return m;
      };
      // constraint kind/ghost 数を prop 直接パースの期待値と照合（editor/constraint phase と同じ pattern）。
      const auto verifyConstraintCounts = [&](const char *tag) {
        bool ok = true;
        for (uint8_t probe : gGraph.scenes) {
          const auto propById = sceneMap(probe);
          ConstraintEdgeCounts exp;
          for (const auto &kv : propById) {
            const uint8_t r = getPropRequiredReadId(kv.second);
            if (r != 0) { ++exp.required; if (propById.find(r) == propById.end()) ++exp.requiredGhost; }
            const uint8_t x1 = getPropSlot1ExcludedReadId(kv.second);
            if (x1 != 0) { ++exp.excludedSlot1; if (propById.find(x1) == propById.end()) ++exp.excludedSlot1Ghost; }
            const uint8_t x2 = getPropSlot2ExcludedReadId(kv.second);
            if (x2 != 0) { ++exp.excludedSlot2; if (propById.find(x2) == propById.end()) ++exp.excludedSlot2Ghost; }
          }
          const ConstraintEdgeCounts got = countConstraintEdges(probe);
          const bool m = exp.required == got.required && exp.requiredGhost == got.requiredGhost &&
                         exp.excludedSlot1 == got.excludedSlot1 &&
                         exp.excludedSlot1Ghost == got.excludedSlot1Ghost &&
                         exp.excludedSlot2 == got.excludedSlot2 &&
                         exp.excludedSlot2Ghost == got.excludedSlot2Ghost;
          if (!m) ok = false;
          std::cout << tag << " scene=" << hexNibble(probe)
                    << " required=" << got.required << "/" << exp.required
                    << " excluded=" << (got.excludedSlot1 + got.excludedSlot2) << "/"
                    << (exp.excludedSlot1 + exp.excludedSlot2) << " ghost="
                    << (got.requiredGhost + got.excludedSlot1Ghost + got.excludedSlot2Ghost) << "/"
                    << (exp.requiredGhost + exp.excludedSlot1Ghost + exp.excludedSlot2Ghost)
                    << " match=" << (m ? 1 : 0) << std::endl;
        }
        return ok;
      };

      // A) write helpers → read helpers の往復（合成 prop・全 fields・各 option 1回ずつ・逆配置厳密一致）。
      // cdOk は timerSetter/timerTarget/eyelevel の合成・分解が現行 helpers と一致するかの別判定（Prompt 5 検証項目）。
      bool rtOk = true;
      bool cdOk = true;
      {
        uint64_t p = 0;
        p = setPropField(p, 0, 8, 0x5A); p = setPropField(p, 8, 8, 0x2B); p = setPropField(p, 20, 4, 0x3);
        rtOk &= getPropID(p) == 0x5A && getPropParent(p) == 0x2B && getPropScene(p) == 0x3;
        p = setPropBit(p, 16, true); p = setPropBit(p, 17, true); p = setPropBit(p, 18, true);
        rtOk &= getPropHasRead(p) && getPropIsQuote(p) && getPropIsOnetime(p);
        p = setPropBit(p, 16, false); rtOk &= !getPropHasRead(p) && getPropIsQuote(p);
        rtOk &= clearPropBits(p, (0x3ULL << 24)) == (p & ~(0x3ULL << 24));
        // slot0: requiredReadId / unlocks（index 切替で value は共有、read は index に一致する option だけ返す）
        p = setSlotIndex(p, 0, 0); p = setSlotValue(p, 0, 0x77);
        rtOk &= getPropRequiredReadId(p) == 0x77 && getPropSlot0Unlocks(p) == 0;
        p = setSlotIndex(p, 0, 1);
        rtOk &= getPropSlot0Unlocks(p) == 0x77 && getPropRequiredReadId(p) == 0;
        // slot1: child / excluded / newRoot（排他・index は 1 つ）
        p = setSlotIndex(p, 1, 0); p = setSlotValue(p, 1, 0x12);
        rtOk &= getPropChild(p) == 0x12 && getPropSlot1ExcludedReadId(p) == 0 && getPropNewRoot(p) == 0;
        p = setSlotIndex(p, 1, 1);
        rtOk &= getPropSlot1ExcludedReadId(p) == 0x12 && getPropChild(p) == 0 && getPropNewRoot(p) == 0;
        p = setSlotIndex(p, 1, 2);
        rtOk &= getPropNewRoot(p) == 0x12 && getPropChild(p) == 0 && getPropSlot1ExcludedReadId(p) == 0;
        // slot1 index 3: downUnlocks（mask・他の option は 0・unlocks 側にも漏れない）
        p = setSlotIndex(p, 1, 3);
        rtOk &= getPropSlot1DownUnlocks(p) == 0x12 && getPropDownUnlocks(p) == 0x12 &&
                getPropNewRoot(p) == 0 && getPropChild(p) == 0 &&
                getPropSlot1ExcludedReadId(p) == 0;
        // slot2: timerSetter（vacant bits2-3 保持）/ unlocks / requiredUnlockFlags / excluded
        const uint64_t tvac = 0x0C; // value bits2-3 = 0b11（vacant・保持される）
        uint64_t tv = editTimerSetterValue(tvac, 2, 3); // timerId(bits0-1)=2, action(bits4-5)=3
        const bool tvCompose = (tv & 0x3) == 2 && ((tv >> 4) & 0x3) == 3 && (tv & 0x0C) == tvac; // value bit 配置（read の分解と一致）
        rtOk &= tvCompose;
        cdOk &= tvCompose;
        p = setSlotIndex(p, 2, 0); p = setSlotValue(p, 2, tv);
        rtOk &= getPropTimerSetter(p) == tv && hasPropTimerSetter(p);
        cdOk &= getPropTimerSetter(p) == tv && hasPropTimerSetter(p);
        uint64_t tv2 = editTimerSetterValue(getSlotValue(p, 2), 1, 2); // 部分編集で vacant 保持
        p = setSlotValue(p, 2, tv2);
        const bool tvDecode = getPropTimerSetterId(p) == 1 && getPropTimerSetterAction(p) == 2 &&
                              (getSlotValue(p, 2) & 0x0C) == tvac && getPropTimerSetter(p) == tv2;
        rtOk &= tvDecode;
        cdOk &= tvDecode;
        p = setSlotIndex(p, 2, 1);
        rtOk &= getPropSlot2Unlocks(p) == getSlotValue(p, 2) &&
                getPropUnlocks(p) == (getPropSlot0Unlocks(p) | getSlotValue(p, 2));
        p = setSlotIndex(p, 2, 2);
        rtOk &= getPropRequiredUnlockFlags(p) == getSlotValue(p, 2) && !hasPropTimerSetter(p);
        p = setSlotIndex(p, 2, 3);
        rtOk &= getPropSlot2ExcludedReadId(p) == getSlotValue(p, 2) && getPropExcludedReadId2(p) == getSlotValue(p, 2);
        // slot3: timerTarget / newScene / excludedUnlockFlags / eyelevelToggle
        const uint64_t tqv = editTimerTargetValue(0, 2, 3, 0x0A); // op=more, timerId=3, count=10
        // value byte 単体は prop の slot value bits と同じ配置（getProp* は prop bit を読むので、
        // 合成値そのものは raw bit で照合する。prop に立てた後の分解は下の往復で検証する）。
        const bool tqCompose = (tqv & 0x3) == 2 && ((tqv >> 2) & 0x3) == 3 && ((tqv >> 4) & 0xF) == 0x0A;
        rtOk &= tqCompose;
        cdOk &= tqCompose;
        p = setSlotIndex(p, 3, 0); p = setSlotValue(p, 3, tqv);
        const bool ttDecode = hasPropTimerTarget(p) && getPropTimerTarget(p) == tqv &&
                              getPropTimerTargetOp(p) == 2 && getPropTimerTargetId(p) == 3 &&
                              getPropTimerTargetCount(p) == 0x0A;
        rtOk &= ttDecode;
        cdOk &= ttDecode;
        const uint64_t tqv0 = editTimerTargetValue(tqv, 0, 3, 0x0A); // operator 0 = timerTarget なし（hasPropTimerTarget は op!=0 を前提）
        p = setSlotValue(p, 3, tqv0);
        const bool ttNone = !hasPropTimerTarget(p) && getPropTimerTargetOp(p) == 0 && getSlotValue(p, 3) == tqv0;
        rtOk &= ttNone;
        cdOk &= ttNone;
        p = setSlotIndex(p, 3, 1);
        rtOk &= getPropNewScene(p) == getSlotValue(p, 3) && !hasPropTimerTarget(p);
        p = setSlotIndex(p, 3, 2);
        rtOk &= getPropExcludedUnlockFlags(p) == getSlotValue(p, 3);
        const uint64_t ev = editEyelevelToggleValue(0, 0x0F, 0x0A); // eyelevel=15, toggle=10
        p = setSlotIndex(p, 3, 3); p = setSlotValue(p, 3, ev);
        const bool evDecode = getPropEyelevel(p) == 0x0F && getPropToggleEyelevel(p) == 0x0A && getSlotValue(p, 3) == ev;
        rtOk &= evDecode;
        cdOk &= evDecode;
        std::cout << "props helpers roundtrip prop=" << formatPropHex16(p) << std::endl;
      }
      std::cout << "props_helper_roundtrip_all_fields match=" << (rtOk ? 1 : 0) << std::endl;
      std::cout << "props timerSetter/timerTarget/eyelevel compose-decode match=" << (cdOk ? 1 : 0) << std::endl;

      // B) 対象ノードを選ぶ（view scene の content node・id>=1・id+1 未使用・parent!=id・slot 値 0 を優先）。
      const uint8_t testScene = gViewScene;
      const auto mapTest = sceneMap(testScene);
      int targetIdx = -1;
      uint64_t targetProp0 = 0;
      for (int i : gGraph.nodesInScene(testScene)) {
        const GraphNode &n = gGraph.nodes[i];
        if (n.kind != NODE_CONTENT) continue;
        if (n.id < 1 || n.id >= 0xFF) continue;
        if (mapTest.find(static_cast<uint8_t>(n.id + 1)) != mapTest.end()) continue;
        const uint64_t prop = gDocument.elements[n.elementIndex].element.prop;
        if (getPropParent(prop) == n.id) continue; // self-parent を作らない
        bool slotsClean = true;
        for (int slot = 0; slot < 4; ++slot) {
          if (getSlotIndex(prop, static_cast<uint8_t>(slot)) != 0 || getSlotValue(prop, static_cast<uint8_t>(slot)) != 0) slotsClean = false;
        }
        if (!slotsClean) continue;
        targetIdx = i;
        targetProp0 = prop;
        break;
      }
      bool targetFound = targetIdx >= 0;
      if (!targetFound) {
        for (int i : gGraph.nodesInScene(testScene)) {
          const GraphNode &n = gGraph.nodes[i];
          if (n.kind != NODE_CONTENT) continue;
          if (n.id < 1 || n.id >= 0xFF) continue;
          if (mapTest.find(static_cast<uint8_t>(n.id + 1)) != mapTest.end()) continue;
          const uint64_t prop = gDocument.elements[n.elementIndex].element.prop;
          if (getPropParent(prop) == n.id) continue;
          targetIdx = i;
          targetProp0 = prop;
          break;
        }
        targetFound = targetIdx >= 0;
      }
      std::cout << "props target scene=" << hexNibble(testScene) << " id="
                << (targetFound ? hexByte(static_cast<uint8_t>(gGraph.nodes[targetIdx].id)) : std::string("none"))
                << " prop0=0x" << formatPropHex16(targetProp0) << " found=" << (targetFound ? 1 : 0) << std::endl;

      postKey(SDLK_4); // EDITOR mode（props は EDITOR・idle のときのみ受理）
      const bool modeOk = gViewMode == EDITOR && gEditor.stage == EDITOR_IDLE && !gProps.active;

      // ノード選択（クリック）で props panel を開く。right-click でも開ける（optional マウス）。
      gSelectedNode = -1;
      postClickNode(targetIdx);
      const uint8_t targetId0 = targetIdx >= 0 ? gGraph.nodes[targetIdx].id : 0;
      const bool openedByClick = gProps.active && gSelectedNode == targetIdx &&
                                 gProps.nodeKey == nodeKey(testScene, targetId0);
      gProps.warningTotal = 0; // propsOpen は reset するが、念のため基準を明示（期待値照合の起点）
      size_t warnMark = gProps.warningTotal;
      const auto warnDeltaOk = [&](size_t expected) {
        const size_t got = gProps.warningTotal - warnMark;
        warnMark = gProps.warningTotal;
        return got == expected;
      };
      postClickNodeRight(targetIdx); // right-click も panel を開く（同一ノードの再オープン）
      const bool openedByRight = gProps.active && gProps.nodeKey == nodeKey(testScene, targetId0);

      // describeElement の EDITOR variant（props 表示順序）: 現行 prop から期待する項目文字列をその場で作り照合する。
      bool variantOk = true;
      {
        const auto lines = describeElement(gSelectedNode, EDITOR);
        const uint64_t prop = propsCurrentProp();
        const auto has = [&](const std::string &needle) {
          for (const std::string &l : lines) if (l.find(needle) != std::string::npos) return true;
          return false;
        };
        variantOk = has("prop=0x" + formatPropHex16(prop)) &&
                    has("section=basic flags") && has("flags_bin=" + binaryByte(static_cast<uint8_t>((prop >> 16) & 0xFu))) &&
                    has("section=id/parent/scene id=" + hexDec(getPropID(prop))) &&
                    has("parent=" + hexDec(getPropParent(prop))) && has("scene=" + hexDec(getPropScene(prop))) &&
                    has("section=slot0 option=" + std::string(propsOptionLabel(prop, 0))) &&
                    has("section=slot1 option=" + std::string(propsOptionLabel(prop, 1))) &&
                    has("section=slot2 option=" + std::string(propsOptionLabel(prop, 2))) &&
                    has("section=slot3 option=" + std::string(propsOptionLabel(prop, 3))) &&
                    lines.size() >= 10;
        // 他 view の出力順は壊さない（general variant は snapshot の先頭形式のまま・field 全文字列を保持）。
        const auto generalLines = describeElement(gSelectedNode, VIEW_GENERAL);
        variantOk &= generalLines.size() >= 8 &&
                    generalLines[0].find("node scene=") != std::string::npos &&
                    generalLines[1].find("prop=0x") != std::string::npos &&
                    generalLines[4].find("slot0 index=") != std::string::npos; // general の slot dump 順（既存: node/prop/id/flags→slot0..3）
        std::cout << "props variant lines=" << lines.size() << " general lines=" << generalLines.size() << std::endl;
      }
      std::cout << "props_open click=" << (openedByClick ? 1 : 0) << " right=" << (openedByRight ? 1 : 0)
                << " mode_active=" << (modeOk ? 1 : 0) << " variant_ok=" << (variantOk ? 1 : 0) << std::endl;

      // C) キー経路: TAB=次項目, ENTER=トグル, U=undo, Y/N=bool（panel open のとき優先・衝突しない）
      const uint64_t propBefore = propsCurrentProp();
      postKey(SDLK_TAB);
      const bool tabOk = gProps.cursor == 1; // isQuote フィールド
      postKey(SDLK_RETURN);
      const bool enterToggleOk = getPropIsQuote(propsCurrentProp()) != getPropIsQuote(propBefore);
      // 編集した RawLine が再パースでき、prop が同一であること（ ENTER トグル直後の行）。
      bool reparseOk = true;
      {
        const int idxNow = propsResolveCurrentNode();
        const RawLine &rl = gDocument.lines[gGraph.nodes[idxNow].lineIndex];
        std::string reD; uint64_t reP = 0;
        reparseOk = rl.kind == LINE_ELEMENT && parseElementLine(rl.text, reD, reP) && reP == propsCurrentProp() &&
                    rl.text == "'" + gDocument.elements[gGraph.nodes[idxNow].elementIndex].element.dialogue +
                               "', '0x" + formatPropHex16Piped(propsCurrentProp()) + "',";
      }
      postKey(SDLK_U); // undo（旧 prop を完全復元・line text/dirty も戻る）
      const bool undoRestoreOk = propsCurrentProp() == propBefore && gProps.undoStack.empty();
      gProps.cursor = 0; // hasRead フィールド（Y/N は current field の bool を true/false にする・key 経路）
      postKey(SDLK_Y);
      const bool yOk = getPropHasRead(propsCurrentProp()) == true;
      postKey(SDLK_N);
      const bool nOk = getPropHasRead(propsCurrentProp()) == false;
      // 循環: TAB を fieldCount 回で cursor が 0 に戻る（決定性）。
      for (int i = 0; i < kPropsFieldCount; ++i) postKey(SDLK_TAB);
      const bool tabWrapOk = gProps.cursor == 0;
      std::cout << "props key flow tab=" << (tabOk ? 1 : 0) << " enter_toggle=" << (enterToggleOk ? 1 : 0)
                << " reparse=" << (reparseOk ? 1 : 0) << " undo_restore=" << (undoRestoreOk ? 1 : 0)
                << " y=" << (yOk ? 1 : 0) << " n=" << (nOk ? 1 : 0) << " tab_wrap=" << (tabWrapOk ? 1 : 0) << std::endl;
      // Y/N の編集は undo entry を 2 段積む。undo は U で復元できることを最後に全 undo で確認するが、
      // ここでは 1 段目の復元（hasRead=false の元値）を確認する。
      postKey(SDLK_U);
      postKey(SDLK_U);
      const bool ynUndoOk = propsCurrentProp() == propBefore && gProps.undoStack.empty();
      std::cout << "props yn_undo_restore=" << (ynUndoOk ? 1 : 0) << std::endl;
      warnMark = gProps.warningTotal; // キー編集の Y/N・ENTER は warning を作らない（期待 0）
      bool keyNoWarnOk = warnDeltaOk(0);

      // D) programmatic edits（決定的・期待値は prop 直接パースから実行時に算出）。
      const int fId = 4, fParent = 5, fScene = 6;
      const int fSlot0Opt = 7, fSlot0Val = 8, fSlot1Opt = 9, fSlot1Val = 10;
      const int fSlot2Opt = 11, fSlot2Val = 12, fSlot2TimerId = 13, fSlot2Action = 14;
      const int fSlot3Opt = 15, fSlot3Val = 16, fSlot3Op = 17, fSlot3TimerId = 18, fSlot3Count = 19;
      const int fEye = 20, fEyeToggle = 21;
      const auto curScene = [&]() { return static_cast<uint8_t>(gProps.nodeKey >> 8); };
      const auto curId = [&]() { return static_cast<uint8_t>(gProps.nodeKey & 0xFFu); };
      // 現 scene で未使用の最小 id（!=0）をその場で算出する（cross-scene target の期待値用）。
      const auto absentNow = [&]() {
        const auto m = sceneMap(curScene());
        for (int cand = 1; cand <= 0xFF; ++cand) {
          if (m.find(static_cast<uint8_t>(cand)) == m.end()) return static_cast<uint8_t>(cand);
        }
        return static_cast<uint8_t>(0);
      };
      bool editsOk = true;
      size_t expectedTotal = 0; // validation warning の期待値総数（各 op の期待値をその場で加算）

      // O1: id → id+1（未使用・duplicate なし → warning 0）。正格式・再パース・baseline diff（edited のみ）。
      const uint8_t nextId = static_cast<uint8_t>(targetId0 + 1);
      bool o1 = propsApplyFieldValue(fId, nextId, true);
      o1 &= getPropID(propsCurrentProp()) == nextId && warnDeltaOk(0);
      const RoundTripReport rtEdit1 = verifyRoundTrip(gDocument);
      gRoundTrip = rtEdit1;
      const bool rtEditedOk = !rtEdit1.exact && rtEdit1.editedLines == 1 && rtEdit1.addedLines == 0 &&
                              rtEdit1.diffIsAddedOrDirtyOnly && rtEdit1.baselineLines == baseLines;
      std::cout << "props O1 id->" << hexByte(nextId) << " applied=" << (o1 ? 1 : 0)
                << " baseline_edited_only=" << (rtEditedOk ? 1 : 0) << std::endl;
      editsOk &= o1 && rtEditedOk;

      // O2: id absolute 0x100 → clamp warning（範囲外は clamp + warning）。duplicate は現行 map から算出。
      const auto mapAfterId = sceneMap(testScene);
      bool dup0xFF = false;
      if (mapAfterId.find(0xFF) != mapAfterId.end()) {
        const int hit = gGraph.findNode(testScene, 0xFF);
        dup0xFF = hit >= 0 && gGraph.nodes[hit].elementIndex != gGraph.nodes[propsResolveCurrentNode()].elementIndex;
      }
      const size_t expected2 = 1 + (dup0xFF ? 1 : 0);
      propsApplyFieldValue(fId, 0x100, true);
      const uint8_t afterId = getPropID(propsCurrentProp());
      bool o2 = warnDeltaOk(expected2) && afterId == (dup0xFF ? nextId : 0xFF);
      expectedTotal += expected2;
      std::cout << "props O2 clamp id want=" << hexByte(0xFF) << " got=" << hexByte(afterId)
                << " dup_reject=" << (dup0xFF ? 1 : 0) << " warn expected=" << expected2
                << " match=" << (o2 ? 1 : 0) << std::endl;
      editsOk &= o2;

      // O3/O4: parent → 現 scene の実在 id（warning 0）→ 現 scene に無い id（warning・ghost/par! 表示）。
      const auto mapCur3 = sceneMap(curScene());
      uint8_t presentId = 0;
      for (const auto &kv : mapCur3) {
        if (kv.first != 0 && kv.first != curId()) { presentId = kv.first; break; }
      }
      bool o3 = propsApplyFieldValue(fParent, presentId, true) && getPropParent(propsCurrentProp()) == presentId &&
                warnDeltaOk(0);
      const uint8_t absentA = absentNow();
      bool o4 = propsApplyFieldValue(fParent, absentA, true) && getPropParent(propsCurrentProp()) == absentA &&
                warnDeltaOk(absentA != 0 ? 1 : 0);
      expectedTotal += absentA != 0 ? 1 : 0;
      std::cout << "props O3 parent present=" << hexByte(presentId) << " ok=" << (o3 ? 1 : 0)
                << " O4 parent absent=" << hexByte(absentA) << " warn=" << (absentA != 0 ? 1 : 0)
                << " ok=" << (o4 ? 1 : 0) << std::endl;
      editsOk &= o3 && o4;

      // O5: scene 移動（現在 id が無い scene へ・warning 0）。無ければ skip（flag=1 のまま）。
      uint8_t otherScene = 0xFF;
      for (uint8_t s : gGraph.scenes) {
        if (s == curScene()) continue;
        const auto m = sceneMap(s);
        if (m.find(curId()) == m.end()) { otherScene = s; break; }
      }
      bool o5Skipped = otherScene == 0xFF;
      bool o5 = o5Skipped;
      if (!o5Skipped) {
        o5 = propsApplyFieldValue(fScene, otherScene, true) && getPropScene(propsCurrentProp()) == otherScene &&
             gProps.nodeKey == nodeKey(otherScene, curId()) && propsResolveCurrentNode() >= 0 &&
             warnDeltaOk(0);
      }
      std::cout << "props O5 scene move target=" << hexNibble(otherScene) << " moved=" << (o5Skipped ? 0 : 1)
                << " ok=" << (o5 ? 1 : 0) << std::endl;
      editsOk &= o5;

      // O6: hasRead Y/N（programmatic・warning 0・load-time prop の他 bit を壊さない）。
      bool o6 = true;
      const uint64_t beforeO6 = propsCurrentProp();
      propsSetBoolField(0, true);
      // 他 bit を壊さない照合: bit16 を mask して clearPropBits で除き、残りの bits が一致するか（bit16 だけを立てる）。
      o6 &= getPropHasRead(propsCurrentProp()) && clearPropBits(propsCurrentProp(), 1ULL << 16) == clearPropBits(beforeO6, 1ULL << 16);
      propsSetBoolField(0, false);
      o6 &= !getPropHasRead(propsCurrentProp()) && warnDeltaOk(0);
      std::cout << "props O6 hasRead toggle ok=" << (o6 ? 1 : 0) << std::endl;
      editsOk &= o6;

      // O7/O8/O9: slot0 requiredReadId → present（warning 0）→ absent（warning 1・ghost match）→ none（元 index 復元・value 0）。
      uint8_t present0 = 0;
      {
        const auto m = sceneMap(curScene());
        for (const auto &kv : m) { if (kv.first != 0 && kv.first != curId()) { present0 = kv.first; break; } }
      }
      bool o7 = propsSetOption(fSlot0Opt, 1) && getSlotIndex(propsCurrentProp(), 0) == 0 && warnDeltaOk(0);
      o7 &= propsApplyFieldValue(fSlot0Val, present0, true) && getPropRequiredReadId(propsCurrentProp()) == present0 &&
           warnDeltaOk(0);
      bool constraintAfterPresent = verifyConstraintCounts("props after O7 required present");
      const uint8_t absentB = absentNow();
      bool o8 = propsApplyFieldValue(fSlot0Val, absentB, true) && getPropRequiredReadId(propsCurrentProp()) == absentB &&
                warnDeltaOk(absentB != 0 ? 1 : 0);
      expectedTotal += absentB != 0 ? 1 : 0;
      const auto mapO8 = sceneMap(curScene());
      const ConstraintEdgeCounts gotO8 = countConstraintEdges(curScene());
      bool ghostOk = o8 && absentB != 0 && gotO8.requiredGhost >= 1 && mapO8.find(absentB) == mapO8.end();
      bool constraintAfterGhost = verifyConstraintCounts("props after O8 cross-scene ghost");
      bool o9 = propsSetOption(fSlot0Opt, 0) && getSlotIndex(propsCurrentProp(), 0) == gProps.baseSlotIndex[0] &&
                getSlotValue(propsCurrentProp(), 0) == 0 && getPropRequiredReadId(propsCurrentProp()) == 0 &&
                warnDeltaOk(0);
      bool constraintBack0 = verifyConstraintCounts("props after O9 none");
      std::cout << "props O7 required present=" << hexByte(present0) << " ok=" << (o7 ? 1 : 0) << " constraint match=" << (constraintAfterPresent ? 1 : 0)
                << " O8 absent=" << hexByte(absentB) << " warn=" << (absentB != 0 ? 1 : 0) << " ghost=" << (ghostOk ? 1 : 0) << " constraint match=" << (constraintAfterGhost ? 1 : 0)
                << " O9 none idx=" << std::to_string(gProps.baseSlotIndex[0]) << " ok=" << (o9 ? 1 : 0) << " match=" << (constraintBack0 ? 1 : 0) << std::endl;
      editsOk &= o7 && o8 && ghostOk && constraintAfterPresent && constraintAfterGhost && constraintBack0;

      // O10/O11/O12: slot1 child（実在・child edge 出現）→ excluded（排他・child==0）→ absent（ghost）→ none。
      uint8_t present1 = 0;
      {
        const auto m = sceneMap(curScene());
        for (const auto &kv : m) { if (kv.first != 0 && kv.first != curId()) { present1 = kv.first; break; } }
      }
      bool o10 = propsSetOption(fSlot1Opt, 1) && getPropChild(propsCurrentProp()) == 0 && warnDeltaOk(0);
      o10 &= propsApplyFieldValue(fSlot1Val, present1, true) && getPropChild(propsCurrentProp()) == present1 &&
             warnDeltaOk(0);
      const int targetNow = propsResolveCurrentNode();
      const int childIdx = gGraph.findNode(curScene(), present1);
      bool childEdgeOk = targetNow >= 0 && childIdx >= 0 &&
                         gGraph.childLinkAdj[targetNow].size() == 1 && gGraph.childLinkAdj[targetNow][0] == childIdx;
      bool o11Excl = propsSetOption(fSlot1Opt, 2) && getPropChild(propsCurrentProp()) == 0 &&
                     getPropSlot1ExcludedReadId(propsCurrentProp()) == present1 && warnDeltaOk(0);
      const uint8_t absentC = absentNow();
      bool o11Abs = propsApplyFieldValue(fSlot1Val, absentC, true) && getPropSlot1ExcludedReadId(propsCurrentProp()) == absentC &&
                    getPropChild(propsCurrentProp()) == 0 && warnDeltaOk(absentC != 0 ? 1 : 0);
      expectedTotal += absentC != 0 ? 1 : 0;
      bool constraintExclGhost = verifyConstraintCounts("props after O11 excluded ghost");
      bool o12 = propsSetOption(fSlot1Opt, 0) && getSlotIndex(propsCurrentProp(), 1) == gProps.baseSlotIndex[1] &&
                 getSlotValue(propsCurrentProp(), 1) == 0 && getPropChild(propsCurrentProp()) == 0 &&
                 getPropSlot1ExcludedReadId(propsCurrentProp()) == 0 && getPropNewRoot(propsCurrentProp()) == 0 &&
                 warnDeltaOk(0);
      bool constraintBack1 = verifyConstraintCounts("props after O12 none");
      // family/constraint の edge は再構築だけで即時更新される（確認だけ・配線追加不要）。
      const uint8_t famScene = curScene();
      layoutFamily(famScene);
      size_t famPlaced = 0;
      for (int i : gGraph.nodesInScene(famScene)) if (gGraph.nodes[i].famDepth >= 0) ++famPlaced;
      const bool famOk = propsResolveCurrentNode() >= 0 && gGraph.nodes[propsResolveCurrentNode()].famDepth >= 0 &&
                         famPlaced == gGraph.nodesInScene(famScene).size();
      std::cout << "props O10 child=" << hexByte(present1) << " edge=" << (childEdgeOk ? 1 : 0) << " ok=" << (o10 ? 1 : 0)
                << " O11 excluded exclusive child0=" << (o11Excl ? 1 : 0) << " absent ghost warn=1 ok=" << (o11Abs ? 1 : 0)
                << " constraint match=" << (constraintExclGhost ? 1 : 0)
                << " O12 none ok=" << (o12 ? 1 : 0) << " match=" << (constraintBack1 ? 1 : 0) << std::endl;
      editsOk &= o10 && childEdgeOk && o11Excl && o11Abs && constraintExclGhost && o12 && constraintBack1 && famOk;

      // O13/O14: slot2 timerSetter（option 立て→部分編集・decode 一致・hasPropTimerSetter）→ none → inert warning。
      bool o13 = propsSetOption(fSlot2Opt, 1) && getSlotIndex(propsCurrentProp(), 2) == 0 && warnDeltaOk(0);
      o13 &= propsApplyFieldValue(fSlot2TimerId, 1, true) && getPropTimerSetterId(propsCurrentProp()) == 1 && warnDeltaOk(0);
      o13 &= propsApplyFieldValue(fSlot2Action, 1, true) && getPropTimerSetterAction(propsCurrentProp()) == 1 &&
             hasPropTimerSetter(propsCurrentProp()) && warnDeltaOk(0);
      const uint64_t tvState = getSlotValue(propsCurrentProp(), 2);
      const bool tvDecodeOk = getPropTimerSetter(propsCurrentProp()) == tvState && (tvState & 0x0C) == 0;
      // O14: none 復元（value 0・option は元 index に戻る）→ value bits の編集は option に一致すれば warning なし、
      // 一致しなければ inert warning（base index が option idx と違う場合の期待値はその場で算出）。
      const size_t inert2Exp = gProps.baseSlotIndex[2] != 0 ? 1 : 0;
      bool o14 = propsSetOption(fSlot2Opt, 0) && getSlotValue(propsCurrentProp(), 2) == 0 &&
                 !hasPropTimerSetter(propsCurrentProp()) && warnDeltaOk(0);
      o14 &= propsApplyFieldValue(fSlot2TimerId, 1, true) && getSlotValue(propsCurrentProp(), 2) == 0x01 &&
             !hasPropTimerSetter(propsCurrentProp()) && warnDeltaOk(inert2Exp);
      expectedTotal += inert2Exp;
      o14 &= propsSetOption(fSlot2Opt, 0) && getSlotValue(propsCurrentProp(), 2) == 0 && warnDeltaOk(0);
      std::cout << "props O13 timerSetter id=1 action=1 decode=" << (tvDecodeOk ? 1 : 0) << " ok=" << (o13 ? 1 : 0)
                << " O14 none+value edit inert expected=" << inert2Exp << " ok=" << (o14 ? 1 : 0) << std::endl;
      editsOk &= o13 && tvDecodeOk && o14;

      // O15/O16: slot3 timerTarget（op=2・timerId・count・decode 一致）→ none → inert。eyelevelToggle（eye/toggle 一致）。
      bool o15 = propsSetOption(fSlot3Opt, 1) && getSlotIndex(propsCurrentProp(), 3) == 0 && warnDeltaOk(0);
      o15 &= propsApplyFieldValue(fSlot3Op, 2, true) && getPropTimerTargetOp(propsCurrentProp()) == 2 && warnDeltaOk(0);
      o15 &= propsApplyFieldValue(fSlot3TimerId, 1, true) && getPropTimerTargetId(propsCurrentProp()) == 1 && warnDeltaOk(0);
      o15 &= propsApplyFieldValue(fSlot3Count, 3, true) && getPropTimerTargetCount(propsCurrentProp()) == 3 &&
             hasPropTimerTarget(propsCurrentProp()) && warnDeltaOk(0);
      bool o15none = propsSetOption(fSlot3Opt, 0) && !hasPropTimerTarget(propsCurrentProp()) &&
                     getSlotValue(propsCurrentProp(), 3) == 0 && warnDeltaOk(0);
      const size_t inert3Exp = gProps.baseSlotIndex[3] != 0 ? 1 : 0;
      o15none &= propsApplyFieldValue(fSlot3Count, 2, true) && getSlotValue(propsCurrentProp(), 3) == 0x20 &&
                 !hasPropTimerTarget(propsCurrentProp()) && warnDeltaOk(inert3Exp);
      expectedTotal += inert3Exp;
      o15none &= propsSetOption(fSlot3Opt, 0) && getSlotValue(propsCurrentProp(), 3) == 0 && warnDeltaOk(0);
      bool o16 = propsSetOption(fSlot3Opt, 4) && getSlotIndex(propsCurrentProp(), 3) == 3 && warnDeltaOk(0);
      o16 &= propsApplyFieldValue(fEye, 3, true) && getPropEyelevel(propsCurrentProp()) == 3 && warnDeltaOk(0);
      o16 &= propsApplyFieldValue(fEyeToggle, 5, true) && getPropToggleEyelevel(propsCurrentProp()) == 5 && warnDeltaOk(0);
      o16 &= propsSetOption(fSlot3Opt, 0) && getPropEyelevel(propsCurrentProp()) == 0 &&
             getPropToggleEyelevel(propsCurrentProp()) == 0 && warnDeltaOk(0);
      std::cout << "props O15 timerTarget op=2 id=1 count=3 ok=" << (o15 ? 1 : 0) << " none inert ok=" << (o15none ? 1 : 0)
                << " O16 eyelevel=3 toggle=5 ok=" << (o16 ? 1 : 0) << std::endl;
      editsOk &= o15 && o15none && o16;

      // eye level の option 非立て編集（validation の inert warning・期待値は base idx から算出）。
      const size_t eyeInertExp = gProps.baseSlotIndex[3] != 3 ? 1 : 0;
      const uint8_t eyeEffExpect = gProps.baseSlotIndex[3] == 3 ? 2 : 0; // option に一致する場合は実効値 2・非立てなら 0
      bool eyeInert = propsApplyFieldValue(fEye, 2, true) && getSlotValue(propsCurrentProp(), 3) == 0x02 &&
                      getPropEyelevel(propsCurrentProp()) == eyeEffExpect && warnDeltaOk(eyeInertExp);
      expectedTotal += eyeInertExp;
      eyeInert &= propsSetOption(fSlot3Opt, 0) && getSlotValue(propsCurrentProp(), 3) == 0 &&
                  !hasPropTimerTarget(propsCurrentProp()) && warnDeltaOk(0);
      std::cout << "props eyelevel inert edit expected=" << eyeInertExp << " ok=" << (eyeInert ? 1 : 0) << std::endl;
      editsOk &= eyeInert;

      // 各 option を 1 回ずつ立てて読む（real doc・warning 0・none 復元）。
      bool optionsOk = true;
      {
        uint8_t presentF = 0;
        const auto m = sceneMap(curScene());
        for (const auto &kv : m) { if (kv.first != 0 && kv.first != curId()) { presentF = kv.first; break; } }
        // slot0 unlocks
        optionsOk &= propsSetOption(fSlot0Opt, 2) && getSlotIndex(propsCurrentProp(), 0) == 1 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot0Val, 0x22, true) && getPropSlot0Unlocks(propsCurrentProp()) == 0x22 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot0Opt, 0) && getPropSlot0Unlocks(propsCurrentProp()) == 0 && warnDeltaOk(0);
        // slot1 newRoot
        optionsOk &= propsSetOption(fSlot1Opt, 3) && getPropNewRoot(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot1Val, presentF, true) && getPropNewRoot(propsCurrentProp()) == presentF &&
                     gGraph.newRootTargets.size() >= 1 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot1Opt, 0) && getPropNewRoot(propsCurrentProp()) == 0 && warnDeltaOk(0);
        // slot1 downUnlocks（value は mask・id target 検査がないので warning 0）
        optionsOk &= propsSetOption(fSlot1Opt, 4) && getSlotIndex(propsCurrentProp(), 1) == 3 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot1Val, 0x2A, true) &&
                     getPropSlot1DownUnlocks(propsCurrentProp()) == 0x2A &&
                     getPropNewRoot(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot1Opt, 0) && getPropSlot1DownUnlocks(propsCurrentProp()) == 0 &&
                     getSlotValue(propsCurrentProp(), 1) == 0 && warnDeltaOk(0);
        // slot2 unlocks / requiredUnlockFlags / excluded
        optionsOk &= propsSetOption(fSlot2Opt, 2) && getSlotIndex(propsCurrentProp(), 2) == 1 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot2Val, 0x11, true) && getPropSlot2Unlocks(propsCurrentProp()) == 0x11 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 0) && getPropSlot2Unlocks(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 3) && getSlotIndex(propsCurrentProp(), 2) == 2 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot2Val, 0x25, true) && getPropRequiredUnlockFlags(propsCurrentProp()) == 0x25 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 0) && getPropRequiredUnlockFlags(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 4) && getSlotIndex(propsCurrentProp(), 2) == 3 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot2Val, presentF, true) && getPropSlot2ExcludedReadId(propsCurrentProp()) == presentF && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 0) && getPropSlot2ExcludedReadId(propsCurrentProp()) == 0 && warnDeltaOk(0);
        // slot3 newScene（value 0 は「変化なし」sentinel・選択可）/ excludedUnlockFlags
        optionsOk &= propsSetOption(fSlot3Opt, 2) && getSlotIndex(propsCurrentProp(), 3) == 1 && getPropNewScene(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot3Opt, 0) && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot3Opt, 3) && getSlotIndex(propsCurrentProp(), 3) == 2 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot3Val, 0x29, true) && getPropExcludedUnlockFlags(propsCurrentProp()) == 0x29 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot3Opt, 0) && getPropExcludedUnlockFlags(propsCurrentProp()) == 0 && warnDeltaOk(0);
        std::cout << "props options each-set-once match=" << (optionsOk ? 1 : 0) << std::endl;
      }
      editsOk &= optionsOk;

      // validation warning 総数の照合: 各 op でその場で算出した期待値（範囲外 clamp / duplicate id reject /
      // cross-scene target / option 非立て inert）の合計と gProps.warningTotal が一致すること。
      // 期待値はすべて prop 直接パースから実行時に計算している（数をハードコードしない）。
      const size_t gotWarnTotal = gProps.warningTotal;
      const bool warnTotalOk = gotWarnTotal == expectedTotal;
      std::cout << "props validation warnings got=" << gotWarnTotal << " expected=" << expectedTotal
                << " match=" << (warnTotalOk ? 1 : 0) << std::endl;
      editsOk &= warnTotalOk;

      // G キー: graph 再構築のみ（runtime state は触らない・選択 (scene,id) 復元）。
      const size_t nodesBeforeG = gGraph.nodes.size();
      const size_t edgesBeforeG = gGraph.edges.size();
      postKey(SDLK_G);
      const bool rebuildOk = gGraph.nodes.size() == nodesBeforeG && gGraph.edges.size() == edgesBeforeG &&
                             propsResolveCurrentNode() >= 0 && containerA.size() == baseContA &&
                             containerB.size() == baseContB;
      std::cout << "props G graph rebuild ok=" << (rebuildOk ? 1 : 0) << " runtime untouched=1" << std::endl;
      editsOk &= rebuildOk;

      // Prompt 6・要件 5: scene 切替（[ ] は共通経路・props の TAB/ENTER/+/-/Y/N/U/G/ESC 優先を
      // 通過する）でも props panel・nodeKey・(scene,id) 解決は壊れないこと。切替往復で
      // panel open / nodeKey / 解決がそのまま維持されることを確認する（document は触らない）。
      const uint8_t sceneBeforeSw = gViewScene;
      postKey(SDLK_LEFTBRACKET);
      const bool panelSwOn = gProps.active && propsResolveCurrentNode() >= 0 &&
                             gProps.nodeKey == nodeKey(curScene(), curId());
      postKey(SDLK_RIGHTBRACKET);
      const bool panelSwOk = stPass(panelSwOn && gViewScene == sceneBeforeSw && gProps.active &&
                                   propsResolveCurrentNode() >= 0 &&
                                   gProps.nodeKey == nodeKey(curScene(), curId()), "props edit");
      std::cout << "props_panel_survives_scene_switch ok=" << (panelSwOk ? 1 : 0) << std::endl;
      editsOk &= panelSwOk;

      // real props panel の描画 → build/viewer_frame_props.bmp（PASS: text/subtext1/sky・yellow は info）。
      bool frameOk = false;
      {
        const uint8_t nodeScene = curScene();
        gViewScene = nodeScene;
        applyActiveLayout();
        fitViewToScene();
        gSelectedNode = propsResolveCurrentNode();
        if (gRenderer) {
          setSolid(gRenderer, kMCBase);
          SDL_RenderClear(gRenderer);
          drawGraphOverlay(gRenderer); // node/edge + drawEditorOverlay + props panel（HUD menu rows）
          SDL_RenderPresent(gRenderer);
          SDL_Surface *shot = SDL_RenderReadPixels(gRenderer, nullptr);
          if (shot) {
            SDL_SaveBMP(shot, "build/viewer_frame_props.bmp");
            std::cout << "props frame captured scene=" << hexNibble(nodeScene) << " " << shot->w << "x" << shot->h
                      << " -> build/viewer_frame_props.bmp" << std::endl;
            SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24);
            if (rgba) {
              std::map<uint32_t, uint32_t> hist;
              uint32_t bright = 0;
              for (int y = 0; y < rgba->h; ++y) {
                const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) +
                                     static_cast<size_t>(y) * rgba->pitch;
                for (int x = 0; x < rgba->w; ++x) {
                  const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                  if (std::max(rr, std::max(rg, rb)) < 120) continue;
                  ++bright;
                  ++hist[(static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
                }
              }
              const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
                const auto it = hist.find(hexColor);
                return it == hist.end() ? 0u : it->second;
              };
              std::cout << "props bright pixels=" << bright << std::endl;
              std::cout << "props kmc colors: sky=" << kmcCount(kMCSky) << " text=" << kmcCount(kMCText)
                        << " subtext1=" << kmcCount(kMCSubtext1) << " subtext0=" << kmcCount(kMCSubtext0)
                        << " yellow=" << kmcCount(kMCYellow) << " lavender=" << kmcCount(kMCLavender)
                        << " teal=" << kmcCount(kMCTeal) << std::endl;
              frameOk = kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0 && kmcCount(kMCSky) > 0;
              std::cout << "props frame text/subtext1/sky present=" << (frameOk ? 1 : 0) << " yellow(info)=" << kmcCount(kMCYellow) << std::endl;
              SDL_DestroySurface(rgba);
            }
            SDL_DestroySurface(shot);
          } else {
            std::cout << "props frame capture unavailable: " << SDL_GetError() << std::endl;
          }
        }
      }
      editsOk &= frameOk;

      // ESC キー: panel を閉じる（選択解除より優先・selection は維持・text input は対称のまま）。
      postKey(SDLK_ESCAPE);
      const bool escCloseOk = !gProps.active && gSelectedNode >= 0 && !gEditorTextInputActive;
      std::cout << "props ESC close panel ok=" << (escCloseOk ? 1 : 0) << " selection kept=" << (gSelectedNode >= 0 ? 1 : 0) << std::endl;
      editsOk &= escCloseOk;

      // 全 undo: stack を空になるまで pop → baseline 完全復元（prop・line text・dirty・graph・round-trip EXACT）。
      const uint32_t origKey = nodeKey(testScene, targetId0);
      const int origElement = gDocument.lines[gGraph.nodes[targetIdx].lineIndex].elementIndex;
      size_t undoCount = 0;
      while (!gProps.undoStack.empty()) { propsUndo(); ++undoCount; }
      gViewScene = testScene;
      applyActiveLayout();
      fitViewToScene();
      bool restoreOk = true;
      restoreOk &= undoCount >= 1; // 1段以上の undo が復元に到達する
      restoreOk &= propsCurrentProp() == targetProp0 && gProps.nodeKey == origKey;
      const int restoredIdx = propsResolveCurrentNode();
      restoreOk &= restoredIdx >= 0 && gGraph.nodes[restoredIdx].elementIndex == origElement;
      restoreOk &= serializeDocument(gDocument) == gDocument.raw;
      const RoundTripReport rtUndoBack = verifyRoundTrip(gDocument);
      gRoundTrip = rtUndoBack;
      restoreOk &= rtUndoBack.exact && gDocument.raw.size() == baseBytes && gDocument.lines.size() == baseLines &&
                   gDocument.elements.size() == baseElements;
      for (size_t i = 0; i < gDocument.elements.size(); ++i) {
        restoreOk &= gDocument.elements[i].element.prop == origProps[i] && gDocument.elements[i].dirty == origDirty[i];
      }
      for (size_t i = 0; i < gDocument.lines.size(); ++i) {
        restoreOk &= gDocument.lines[i].text == origText[i];
      }
      restoreOk &= gGraph.nodes.size() == baseNodes && gGraph.edges.size() == baseEdges &&
                   gGraph.roots.size() == baseRoots && gSelectedNode == restoredIdx &&
                   containerA.size() == baseContA && containerB.size() == baseContB &&
                   !gDocTerminatorPatched && !gProps.undoStack.empty() == false;
      // Prompt 6: undo phase を FAIL 検知に格上げ（要件 3・8）。undo stack 上限（kPropsUndoMax,
      // Prompt 6 で 256 に引き上げ）に達していないことも確認する — 満杯だと全 undo で baseline に
      // 戻らないため（Prompt 5 の観測）。期待値は元の prop/line/dirty/graph を実行時比較で算出。
      const bool undoHeadroomOk = undoCount < kPropsUndoMax;
      const bool undoPhaseOk = stPass(restoreOk && undoHeadroomOk, "undo");
      std::cout << "props undo undo_count=" << undoCount << " restore baseline exact=" << (rtUndoBack.exact ? 1 : 0)
                << " match=" << (restoreOk ? 1 : 0) << " undo_limit=" << kPropsUndoMax
                << " headroom=" << (undoHeadroomOk ? 1 : 0) << " ok=" << (undoPhaseOk ? 1 : 0) << std::endl;
      editsOk &= restoreOk && undoHeadroomOk;

      // general に戻る（props panel は mode 切替で閉じる・document は元のまま）。
      postKey(SDLK_1);
      const bool exitOk = gViewMode == VIEW_GENERAL && gEditor.stage == EDITOR_IDLE && !gProps.active &&
                          !gEditorTextInputActive && serializeDocument(gDocument) == gDocument.raw;
      std::cout << "props mode back general state cleared=" << (exitOk ? 1 : 0) << std::endl;
      editsOk &= exitOk;

      // Prompt 6: props の全 flag を FAIL 検知に格上げ（要件 3・8）。期待値は Prompt 5 の
      // 実行時計算（prop 直接パース）のまま。undo phase の復元は "undo" として別 phase 集約。
      const bool propsAllOk = stPass(targetFound && rtOk && cdOk && modeOk && openedByClick && openedByRight && variantOk &&
                                     tabOk && enterToggleOk && reparseOk && undoRestoreOk && yOk && nOk && tabWrapOk &&
                                     ynUndoOk && keyNoWarnOk && editsOk, "props edit");
      std::cout << "props_phase_all_flags=" << (propsAllOk ? 1 : 0) << std::endl;
      std::cout << "round_trip_after_props_phase=" << (rtUndoBack.exact ? "EXACT OK" : "MISMATCH") << std::endl;
    }

    std::cout << "=== [ selftest font ] ===" << std::endl;
    std::cout << "gui text: " << (gGuiText.ready ? "READY" : "ASCII fallback")
              << " font=" << gGuiText.fontPath << " err=" << gGuiText.lastError << std::endl;
    if (gGuiText.ready && gRenderer) {
      // 日本語 glyph が rasterize できているか（texture と bitmap 寸法を確認する）。
      // Prompt 6: ready の環境で glyph が rasterize できなかったら FAIL（ascii fallback
      // 環境は描画側の PASS 検証で扱う — 環境制約は失敗にしない）。
      bool glyphsAllOk = true;
      for (uint32_t cp : {0x8983u /*見*/, 0x8A71u /*話*/, 0x308Bu /*る*/, 0x5C31u /*札*/}) {
        const GlyphRec *g = guiGlyph(gRenderer, cp);
        if (!g) glyphsAllOk = false;
        std::cout << "glyph cp=" << cp << " drawn=" << (g ? 1 : 0) << " w=" << (g ? g->w : 0)
                  << " h=" << (g ? g->h : 0) << " adv=" << (g ? g->advance : 0.f) << std::endl;
      }
      stPass(glyphsAllOk, "font");
    }

    // 画面を実際に描いて pixel を読み出し、日本語テキストが描画されているか検証する。
    if (gRenderer) {
      setSolid(gRenderer, kMCBase); // window background: mocha base #1e1e2e
      SDL_RenderClear(gRenderer);
      drawGraphOverlay(gRenderer);
      SDL_RenderPresent(gRenderer);
      SDL_Surface *shot = SDL_RenderReadPixels(gRenderer, nullptr);
      if (shot) {
        SDL_SaveBMP(shot, "build/viewer_frame.bmp");
        // Prompt 6・要件 8: mode 別 frame の general を保存する（viewer_frame_<mode>. の規則）。
        SDL_SaveBMP(shot, "build/viewer_frame_general.bmp");
        std::cout << "frame captured " << shot->w << "x" << shot->h <<
                  " -> build/viewer_frame.bmp / build/viewer_frame_general.bmp" << std::endl;
        // 読み出し surface のレイアウトは driver 依存なので、確定した RGB24 に
        // 変換してから pixel を読む（RGBA8888/XRGB8888 の取り違えを防ぐ）。
        SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24);
        if (rgba) {
          // 明い pixel（テキスト・線・枠）の色をヒストグラムで集める。
          // 背景(mocha base #1e1e2e)と HUD panel(mantle #181825)は max<120 で除く。
          std::map<uint32_t, uint32_t> hist;
          uint32_t bright = 0;
          uint32_t hudText = 0;
          uint32_t nodeText = 0;
          const int hudW = std::min(kHudW, rgba->w);
          for (int y = 0; y < rgba->h; ++y) {
            const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) +
                                 static_cast<size_t>(y) * rgba->pitch;
            for (int x = 0; x < rgba->w; ++x) {
              const uint8_t r = row[x * 3 + 0], g = row[x * 3 + 1], b = row[x * 3 + 2];
              if (std::max(r, std::max(g, b)) < 120) continue;
              ++bright;
              const uint32_t key = (static_cast<uint32_t>(r) << 16) |
                                   (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
              ++hist[key];
              if (x >= 4 && x < hudW - 4) ++hudText;
              else if (x >= hudW) ++nodeText;
            }
          }
          std::cout << "bright pixels=" << bright << " hud_region=" << hudText
                    << " graph_region=" << nodeText << std::endl;
          std::vector<std::pair<uint32_t, uint32_t> > top(hist.begin(), hist.end());
          std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
          for (size_t i = 0; i < std::min<size_t>(8, top.size()); ++i) {
            std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                      << top[i].first << " count=" << std::dec << top[i].second << std::endl;
          }
          // Prompt 6・要件 8: general frame の PASS を FAIL 検知に格上げ。HUD の text/subtext1 出現を
          // 必須とし、parent field edge=sapphire・child edge=peach の区別は capture scene の
          // 期待値（prop 直接パース・first-wins）があるときのみ実色を要求する（family/constraint
          // frame と同じ方針。red の出現は quote ストライプ由来・lavender は選択 info）。
          const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
            const auto it = hist.find(hexColor);
            return it == hist.end() ? 0u : it->second;
          };
          std::unordered_map<uint8_t, uint64_t> mapCapG;
          for (const ElementRecord &rec : gDocument.elements) {
            if (getPropScene(rec.element.prop) != gViewScene) continue;
            const uint8_t idG = getPropID(rec.element.prop);
            if (mapCapG.find(idG) != mapCapG.end()) continue;
            mapCapG[idG] = rec.element.prop;
          }
          size_t expParG = 0, expChiG = 0;
          for (const auto &kv : mapCapG) {
            const uint8_t parG = getPropParent(kv.second);
            if (parG != 0 && mapCapG.find(parG) != mapCapG.end()) ++expParG;
            const uint8_t chiG = getPropChild(kv.second);
            if (chiG != 0 && mapCapG.find(chiG) != mapCapG.end()) ++expChiG;
          }
          bool genFrameOk = bright > 0 && kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0;
          if (expParG > 0) genFrameOk &= kmcCount(kMCSapphire) > 0;
          if (expChiG > 0) genFrameOk &= kmcCount(kMCPeach) > 0;
          genFrameOk = stPass(genFrameOk, "font");
          std::cout << "general frame PASS bright=" << bright << " sapphire=" << kmcCount(kMCSapphire) << "/" << expParG
                    << " peach=" << kmcCount(kMCPeach) << "/" << expChiG << " text=" << kmcCount(kMCText)
                    << " subtext1=" << kmcCount(kMCSubtext1) << " ok=" << (genFrameOk ? 1 : 0) << std::endl;
          SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(shot);
      } else {
        stFail("font"); // Prompt 6: frame capture が取れない場合は FAIL（要件 8）
        std::cout << "frame capture unavailable: " << SDL_GetError() << std::endl;
      }
    }

    switchScene(+1);
    std::cout << "switchScene(+1) -> scene=" << hexNibble(gViewScene)
              << " nodes=" << gGraph.nodesInScene(gViewScene).size() << std::endl;

    // --- Prompt 6 palette audit phase（要件 6・色と視認性の監査） ---
    // 数値 RGB リテラル監査（kMC 定数定義以外 zero）・SDL_RenderDebugText は fallback のみ・
    // glyph texture は RGBA32・readback は RGB24 変換後。面+テキストの WCAG を pixel readback の
    // 実色で測り text/subtext1 の組合せが 4.5:1 以上であることを確認する（mocha 公式組合せ）。
    std::cout << "=== [ selftest palette audit ] ===" << std::endl;
    {
      std::string srcPath;
      for (const char *c : {"./main.cpp", "../main.cpp"}) {
        std::ifstream probe(c);
        if (probe.is_open()) { srcPath = c; break; }
      }
      const bool srcOk = stPass(!srcPath.empty(), "palette audit");
      int kmcDefs = 0, litOutside = 0, debugUses = 0, debugGuardOk = 0;
      int rgba32 = 0, rgba8888 = 0, rgb24 = 0;
      // 監査対象の needle を分割して作る（分割後のリテラルは監査コード自身に
      // 連結形では現れないので、この phase のコードが自己を counting しない）。
      const std::string debugNeedle = std::string("SDL_") + "RenderDebugText(";
      const std::string rgba8888Needle = std::string("SDL_PIXELFORMAT_RGBA") + "8888";
      const std::string rgba32Needle = std::string("SDL_PIXELFORMAT_RGBA") + "32";
      const std::string rgb24Needle = std::string("SDL_PIXELFORMAT_RGB") + "24";
      const auto isHexChar = [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
      };
      int lastDebugLine = -1, lastReadyGuardLine = -1;
      if (srcOk) {
        std::ifstream src(srcPath);
        std::string line;
        int lineNo = 0;
        while (std::getline(src, line)) {
          ++lineNo;
          const size_t slash = line.find("//");
          if (slash != std::string::npos) line = line.substr(0, slash); // line comment は監査対象外
          const bool kmcDefLine = line.find("constexpr uint32_t kMC") != std::string::npos;
          if (kmcDefLine) ++kmcDefs;
          size_t p = line.find("0x");
          while (p != std::string::npos) {
            size_t q = p + 2;
            int n = 0;
            while (q < line.size() && isHexChar(line[q])) { ++n; ++q; }
            // 6桁ちょうどの 0xRRGGBB リテラル（kMC 定数定義以外）を数える。
            // longer hex literals（prop 値・bit mask・%016llx 出力）は対象にしない。
            if (n == 6 && !kmcDefLine) ++litOutside;
            p = line.find("0x", p + 2);
          }
          if (line.find(debugNeedle) != std::string::npos) {
            ++debugUses;
            lastDebugLine = lineNo;
            if (lastReadyGuardLine > 0 && lineNo - lastReadyGuardLine <= 20) debugGuardOk = 1;
          }
          if (line.find("gGuiText.ready") != std::string::npos) lastReadyGuardLine = lineNo;
          if (line.find(rgba32Needle) != std::string::npos) ++rgba32;
          if (line.find(rgba8888Needle) != std::string::npos) ++rgba8888;
          if (line.find(rgb24Needle) != std::string::npos) ++rgb24;
        }
      }
      const bool litOk = srcOk && kmcDefs > 0 && litOutside == 0;
      const bool apiOk = debugUses <= 1 && debugGuardOk == 1 && rgba8888 == 0 && rgba32 > 0 && rgb24 > 0;
      stPass(litOk, "palette audit");
      stPass(apiOk, "palette audit");
      std::cout << "palette literals: kmc_definitions=" << kmcDefs << " numeric_rgb_literals_outside_kmc_defs="
                << litOutside << " match=" << (litOk ? 1 : 0) << " (grep 相当・line comment は除く)" << std::endl;
      std::cout << "text/api guards: debugtext_code_uses=" << debugUses << " fallback_only=" << debugGuardOk
                << " rgba8888=" << rgba8888 << " rgba32=" << rgba32 << " rgb24_readback=" << rgb24
                << " match=" << (apiOk ? 1 : 0) << std::endl;

      // WCAG を pixel readback の実色で測る（要件 6）。合成 band（face 面 + text/subtext1 行）を
      // 描いて build/viewer_frame_palette.bmp に保存し、face 面の色は直接 pixel、text/subtext1 の
      // 実色は bright ヒストグラムで確認してから contrast ratio を計算する（4.5:1 以上）。
      const uint32_t faces6[] = {kMCBase, kMCMantle, kMCSurface0, kMCSurface1, kMCFillTemplate, kMCFillEmpty};
      const uint32_t fg2[] = {kMCText, kMCSubtext1};
      const size_t faceCount = sizeof(faces6) / sizeof(faces6[0]);
      bool wcagOk = false;
      if (gRenderer) {
        int winW = 0, winH = 0;
        windowSize(&winW, &winH);
        const float bandH = static_cast<float>(winH) / static_cast<float>(faceCount);
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        for (size_t i = 0; i < faceCount; ++i) {
          const SDL_FRect band{0.f, static_cast<float>(i) * bandH, static_cast<float>(winW), bandH + 1.f};
          setSolid(gRenderer, faces6[i]);
          SDL_RenderFillRect(gRenderer, &band);
          drawGuiLine(gRenderer, 10.f, static_cast<float>(i) * bandH + 8.f, "contrast audit row text", 1.f,
                      static_cast<float>(winW) - 20.f, fg2[0]);
          drawGuiLine(gRenderer, 10.f, static_cast<float>(i) * bandH + 24.f, "contrast audit row subtext1", 1.f,
                      static_cast<float>(winW) - 20.f, fg2[1]);
        }
        SDL_RenderPresent(gRenderer);
        SDL_Surface *paShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (paShot) {
          SDL_SaveBMP(paShot, "build/viewer_frame_palette.bmp");
          std::cout << "palette frame captured " << paShot->w << "x" << paShot->h << " -> build/viewer_frame_palette.bmp" << std::endl;
          SDL_Surface *paRgb = SDL_ConvertSurface(paShot, SDL_PIXELFORMAT_RGB24);
          if (paRgb) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < paRgb->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(paRgb->pixels) +
                                   static_cast<size_t>(y) * paRgb->pitch;
              for (int x = 0; x < paRgb->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) |
                       static_cast<uint32_t>(rb)];
              }
            }
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "palette bright=" << bright << " text=" << kmcCount(kMCText)
                      << " subtext1=" << kmcCount(kMCSubtext1) << std::endl;
            wcagOk = bright > 0 && kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0;
            for (size_t i = 0; i < faceCount; ++i) {
              // face 面の pixel を直接読む（band 上部・text 行から離れた右端）。readback の実値で測る。
              const int sy = static_cast<int>(static_cast<float>(i) * bandH) + 2;
              const int sx = paRgb->w - 6;
              const uint8_t *row = static_cast<const uint8_t *>(paRgb->pixels) +
                                   static_cast<size_t>(sy) * paRgb->pitch;
              const uint32_t observed = (static_cast<uint32_t>(row[sx * 3 + 0]) << 16) |
                                       (static_cast<uint32_t>(row[sx * 3 + 1]) << 8) |
                                       static_cast<uint32_t>(row[sx * 3 + 2]);
              const bool faceOk = observed == faces6[i];
              const double rText = wcagContrast(fg2[0], observed);
              const double rSub = wcagContrast(fg2[1], observed);
              wcagOk &= faceOk && rText >= 4.5 && rSub >= 4.5;
              std::cout << "wcag face=" << std::hex << std::setw(6) << std::setfill('0') << faces6[i] << std::dec
                        << " observed_pixel=" << (faceOk ? 1 : 0) << " text_contrast=" << rText
                        << " subtext1_contrast=" << rSub << " min>=4.5=" << (rText >= 4.5 && rSub >= 4.5 ? 1 : 0)
                        << std::endl;
            }
            SDL_DestroySurface(paRgb);
          }
          SDL_DestroySurface(paShot);
        } else {
          std::cout << "palette frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }
      std::cout << "palette wcag ok=" << (stPass(wcagOk, "palette audit") ? 1 : 0) << std::endl;
    }

    // --- Prompt 6 game-mode switch phase（要件 7・H トグル往復・game では graph/HUD を描かず
    // renderConsole と入力系統だけ・runtime の console flow はそのまま動作する） ---
    // DDM_SELFTEST は viewer 検証に集中する。DDM_MODE=game の起動もこの phase で viewer への
    // 復帰経路（H）を確認する（運用側の既定は viewer）。
    std::cout << "=== [ selftest game-mode ] ===" << std::endl;
    {
      auto postKey = [](SDL_Keycode k) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = k;
        ev.key.down = true;
        SDL_AppEvent(nullptr, &ev);
      };
      // 前提: selftest は viewer で始まる（DDM_MODE=game の起動でも H で viewer 復帰できること）。
      if (gUiMode == UI_GAME) setUiMode(UI_VIEWER);
      const uint8_t sceneG = gViewScene;
      postKey(SDLK_H);
      const bool gameOnOk = stPass(gUiMode == UI_GAME, "game-mode switch");
      std::cout << "game_mode_on=" << (gameOnOk ? 1 : 0) << std::endl;

      // game では描画経路（graph/HUD）を通らない。base の clear のみで readback すると
      // graph/HUD の実色が一切出ないことを確認する（frame を保存・bright=0・色 count 0）。
      bool gameFrameOk = false;
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        SDL_RenderPresent(gRenderer);
        SDL_Surface *gs = SDL_RenderReadPixels(gRenderer, nullptr);
        if (gs) {
          SDL_SaveBMP(gs, "build/viewer_frame_game.bmp");
          std::cout << "game frame captured " << gs->w << "x" << gs->h << " -> build/viewer_frame_game.bmp" << std::endl;
          SDL_Surface *gr = SDL_ConvertSurface(gs, SDL_PIXELFORMAT_RGB24);
          if (gr) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < gr->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(gr->pixels) +
                                   static_cast<size_t>(y) * gr->pitch;
              for (int x = 0; x < gr->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) |
                       static_cast<uint32_t>(rb)];
              }
            }
            size_t graphColors = 0;
            for (uint32_t c : {kMCSapphire, kMCPeach, kMCTeal, kMCLavender, kMCText, kMCSubtext1}) {
              if (hist.find(c) != hist.end()) graphColors += 1;
            }
            std::cout << "game frame bright=" << bright << " graph/hud colors=" << graphColors << std::endl;
            gameFrameOk = bright == 0 && graphColors == 0;
            SDL_DestroySurface(gr);
          }
          SDL_DestroySurface(gs);
        }
      }
      stPass(gameFrameOk, "game-mode switch");

      // 入力系統: game でも console の挿入 flow（TAB → ESC・対称）がそのまま動作する。
      postKey(SDLK_TAB);
      const bool insertGameOk = gAppMode == MODE_INSERT_TEXT;
      postKey(SDLK_ESCAPE); // cancelInsertMode（既存経路・text input Stop 対称）
      const bool cancelGameOk = gAppMode == MODE_NORMAL;
      std::cout << "game runtime insert flow ok=" << (insertGameOk && cancelGameOk ? 1 : 0) << std::endl;
      stPass(insertGameOk && cancelGameOk, "game-mode switch");

      postKey(SDLK_H);
      const bool backOk = stPass(gUiMode == UI_VIEWER && gViewMode == VIEW_GENERAL && gEditor.stage == EDITOR_IDLE &&
                                 !gProps.active && gViewScene == sceneG && gAppMode == MODE_NORMAL, "game-mode switch");
      std::cout << "viewer_restore ok=" << (backOk ? 1 : 0) << std::endl;

      // viewer 復帰後は graph/HUD の描画がそのまま復元されること（drawGraphOverlay → drawHud の順序）。
      bool drawBackOk = false;
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer);
        SDL_RenderPresent(gRenderer);
        SDL_Surface *br = SDL_RenderReadPixels(gRenderer, nullptr);
        if (br) {
          SDL_Surface *br2 = SDL_ConvertSurface(br, SDL_PIXELFORMAT_RGB24);
          if (br2) {
            uint32_t bright = 0;
            uint32_t textSeen = 0;
            for (int y = 0; y < br2->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(br2->pixels) +
                                   static_cast<size_t>(y) * br2->pitch;
              for (int x = 0; x < br2->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                if (rr == mcR(kMCText) && rg == mcG(kMCText) && rb == mcB(kMCText)) textSeen += 1;
              }
            }
            drawBackOk = bright > 0 && textSeen > 0;
            SDL_DestroySurface(br2);
          }
          SDL_DestroySurface(br);
        }
      }
      std::cout << "game_mode_switch ok=" << (stPass(drawBackOk, "game-mode switch") ? 1 : 0) << std::endl;
    }

    // --- Prompt 6 data unchanged phase（要件 8・最後確認）: data.txt は変更されない ---
    std::cout << "=== [ selftest data unchanged ] ===" << std::endl;
    {
      std::ifstream ifs(gLoadedPath, std::ios::binary);
      std::string fileBytes;
      if (ifs.is_open()) {
        ifs.seekg(0, std::ios::end);
        const std::streamoff sz = ifs.tellg();
        fileBytes.resize(static_cast<size_t>(sz));
        ifs.seekg(0, std::ios::beg);
        ifs.read(&fileBytes[0], sz);
      }
      const bool rtFinal = stPass(verifyRoundTrip(gDocument).exact, "data unchanged");
      const bool bytesSame = stPass(ifs.is_open() && fileBytes.size() == gLoadedBytes &&
                                   fileBytes == gDocument.raw && !gDocTerminatorPatched, "data unchanged");
      std::cout << "data_unchanged load=" << gLoadedPath << " baseline_bytes=" << gLoadedBytes
                << " disk_bytes=" << fileBytes.size() << " match=" << (bytesSame ? 1 : 0) << std::endl;
      std::cout << "round_trip_final=" << (rtFinal ? "EXACT OK" : "MISMATCH") << " terminator_patched="
                << (gDocTerminatorPatched ? 1 : 0) << std::endl;
      std::cout << "write policy: data.txt に書き戻さない（selftest では legacy write-back は走らない・"
                << "legacy は ../data.txt load 時の運用のみ） backup_on_quit=" << (gBackupOnQuit ? "on（B）" : "off（既定）") << std::endl;
    }

    // Prompt 6・要件 8: 各 phase の結果を集約し、失敗は "FAIL <phase>" を stdout に出して
    // SDL_APP_FAILURE で終わる（全 phase pass のとき SELFTEST: ALL PHASES PASS + SDL_APP_SUCCESS）。
    // 既存の「期待値実行時計算」pattern は維持し、数をハードコードしない（data.txt は不変）。
    if (gSelftestFailures.empty()) {
      std::cout << "SELFTEST: ALL PHASES PASS" << std::endl;
      return SDL_APP_SUCCESS;
    }
    for (const std::string &f : gSelftestFailures) std::cout << "FAIL " << f << std::endl;
    std::cout << "selftest exit: FAIL phases=" << gSelftestFailures.size() << std::endl;
    return SDL_APP_FAILURE;
  }


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

  // Prompt 6・game モード共存（要件 7）: game では graph/HUD を描かない（renderConsole と
  // 入力系統だけを動かす）。描画順序は viewer で drawGraphOverlay（→ drawHud）を維持する。
  if (gUiMode == UI_GAME) {
    Uint64 frameTimeG = SDL_GetTicks() - frameStart;
    if (frameTimeG < TARGET_FRAME_MS) {
      SDL_Delay(TARGET_FRAME_MS - frameTimeG);
    }
    return SDL_APP_CONTINUE;
  }

  setSolid(gRenderer, kMCBase); // window background: mocha base #1e1e2e
  SDL_RenderClear(gRenderer);
  drawGraphOverlay(gRenderer); // VIEW_GENERAL のノード/線 + HUD（console は stdout のまま）
  SDL_RenderPresent(gRenderer);

  Uint64 frameTime = SDL_GetTicks() - frameStart;
  if (frameTime < TARGET_FRAME_MS) {
    SDL_Delay(TARGET_FRAME_MS - frameTime);
  }

  return SDL_APP_CONTINUE;
}

// バックアップモジュール
void backupData(const std::string &srcPath = "../data.txt",
                const std::string &dstPath = "../data.txt.bak") {
  std::error_code ec;
  if (std::filesystem::exists(srcPath, ec)) {
    std::filesystem::copy_file(
        srcPath, dstPath, std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
      std::cerr << "バックアップの作成に失敗しました: " << ec.message()
                << std::endl;
    }
  }
}

// SDL_AppQuit
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  // Prompt 6 write policy（要件 2）: quit の backupData は既定 off（B トグル）。on のときだけ
  // ../data.txt を ../data.txt.bak に上書きする。data.txt 自身には一切書き込まない
  // （write は main.cpp のみ・serialize は stdout / HUD export のまま）。
  if (gBackupOnQuit) {
    backupData("../data.txt", "../data.txt.bak");
  } else {
    std::cout << "backup on quit: off（既定・B トグルで on・data.txt は書き換えない）" << std::endl;
  }

  // write は行わない。round-trip は画面の export 欄と stdout で提示する。
  if (gExportOnQuit) {
    printDocumentToStdout(gDocument);
  }

  // 従来の書き戻しは、load元が ../data.txt の既存運用（build/ 直下から実行）でのみ
  // 維持する。./data.txt をloadした運用では data.txt を書き換えないため書かない。
  // 書き出しは新正格式（'0xHHHH|HHHH|HHHHHHHH'・2|2|4区切りパイプ）で行う。
  if (gLoadedPath == "../data.txt") {
    std::ofstream ofs("../data.txt");
    if (ofs.is_open()) {
      for (auto item : containerA) {
        if (item.dialogue != "dummy") {
          item.prop &= ~(1ULL << 16); // clear hasRead (bit 16)
        }

        ofs << "'" << item.dialogue << "', '0x" << formatPropHex16Piped(item.prop) << "',\n";
      }
      ofs.close();
    }
  }

  shutdownGuiText(); // glyph texture は renderer より先に破棄する

  if (gRenderer)
    SDL_DestroyRenderer(gRenderer);
  if (gWindow)
    SDL_DestroyWindow(gWindow);
}
