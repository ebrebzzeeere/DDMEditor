#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "prop.h"

// ============================================================================
// --- Strict Document model: data.txt を 1 バイトも壊さずに保持する ---
// ============================================================================
// data.txt の 1 行は `'dialogue', '0xHHHH-HHHH-HHHHHHHH',` の形式を取る。
// prop は 2byte-2byte-4byte に 3 分割し、所定の位置に '-' を挿す（書き出し書式）。
// 読み込みに際しては prop 内の '-' をすべて無視するため、旧書の
// `'dialogue', '0xHHHHHHHHHHHHHHHH',`（'-' なし）もそのまま読める。
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
// 書き出しは 2byte-2byte-4byte 区切り（'0xHHHH-HHHH-HHHHHHHH'）だが、読み込みでは
// prop 内の '-' をすべて無視して 16 桁以下の hex として解析する（'-' なしの旧書式も可）。
inline bool parseElementLine(const std::string &line, std::string &dialogueOut,
                             uint64_t &propOut) {
  static const std::regex quotedProp(
      "^[ \t]*'(.*)'[ \t]*,[ \t]*'([^']*)'[ \t]*,?[ \t]*$");
  std::smatch m;
  if (!std::regex_match(line, m, quotedProp)) {
    return false;
  }
  // prop token: まず '-' をすべて除去し、optional な '0x' prefix を取り除いて
  // 旧 parser と同じ「1〜16 桁の hex」を検証してから値化する。
  std::string hex;
  for (char c : m[2].str()) {
    if (c != '-') hex += c; // prop 内の '-' はすべて無視
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

// 編集した Element の raw line 再生成用。lower case・16桁・'0x' prefix。
// （HUD / stdout の prop=0x 表示は区切りなしの 16 桁のまま使う。）
inline std::string formatPropHex16(uint64_t prop) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "%016llx",
                static_cast<unsigned long long>(prop));
  return std::string(buf);
}

// data.txt 書き出し用の prop 書式。16 桁 hex の所定の位置（2byte-2byte-4byte）に
// '-' を挿入する: 'HHHH-HHHH-HHHHHHHH'。値は formatPropHex16 と同一で、
// parseElementLine は '-' をすべて無視して同じ値に戻す。
inline std::string formatPropHex16Piped(uint64_t prop) {
  const std::string hex = formatPropHex16(prop);
  return hex.substr(0, 4) + "-" + hex.substr(4, 4) + "-" + hex.substr(8, 8);
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

bool loadDocument(const std::string &path, Document &doc,
                  std::vector<std::string> &warnings);
std::string serializeDocument(const Document &doc);
RoundTripReport verifyRoundTrip(const Document &doc);
void printDocumentToStdout(const Document &doc);
void printRoundTripReport(const Document &doc);
void backupData(const std::string &srcPath = "../data.txt",
                const std::string &dstPath = "../data.txt.bak");
