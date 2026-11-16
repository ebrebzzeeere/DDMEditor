/*
### 役所の札ギミック
- 役所のギミックは札のギミック。
- 札の番号によって、話せる役人が異なる。
- 偶数と奇数、素数によって挙動が変わる。
- 札の番号はおそらくシステム上のtickを使うとよいだろう。
- gTickのような変数を用意するのがいいと思う。

### 商館の見回りギミック
- 商館では階段を見回る係の者がおり、そいつに止められてしまう。
- しかし、どうしてかときたまそいつは持ち場を離れるので、
- その隙に階段を登ってしまおう。
- 持ち場を離れて2tickごとぐらいに戻ってくるぞ。

### 同意形成ゲーム
- このフロアでは、見ることで話し声が聞こえる。
- 連続で見ると声をひそめられる。
- 3回までなら、聞ける。あとはよく聞こえない。
- 立食パーティ風にお茶を渡して、交渉、ナンパ。
- 見るコマンドで事情を推察し、実際に話すコマンドで話す、お茶を渡す。
- 交渉を成立させていく。
- 3回交渉成立で、ショーメイ成功。拍手しながら階段を降りてくる。
-

---

### アイデア
見回りギミックをクリアして、2階へ。
2階でのギミックは？
コマンド成立前に、たくさんひっかけする。
同意形成ゲームで、交渉以外にナンパのギミックも考える。

---

### やること
- 札を取得した時、その時の札番号を保持し、examineする。
    - 札を取る07
    - 札番号を返す。
    - 札スロットに札番号を保存する。
    - N番だ。
    -

- カスタムファンクションクラスタ、Sceneごとに違う関数クラスタをbaseのmain
loopに挿入する。
- ファンクションクラスタ内にもddを設置する。
- ファンクションドリブンのdialogue.
- DialogueDataの拡張機能。かゆいところに手が届く機能。DDMを最大限活かす機能。

---

### プロンプト下書き
次の指示を出すプロンプトを出力してほしい。
**指示**
移動するコマンドを実装してほしい。
作業に際しては/home/herb1s/Projects/NEELA/DDMEditor/main.cpp,
/home/herb1s/Projects/NEELA/DDMEditor/dd.txtの2ファイルのみ読み込みしてよい、書き込みは前者のmain.cppのみに限る。
*      ____         _________
* ____|   |________|        |________ 不規則なパルス
*/

#include <SDL3/SDL_keycode.h>
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
  /* prop bit assignment
*  slot3 56-63 (0:timerTarget,	 1:newScene,	   2:excludedUnlockFlags,
3:eyelevelToggle)
*  slot2 48-55 (0:timerSetter,	 1:unlocks, 	   2:requiredUnlockFlags,
3:excludedReadId)
*  slot1 40-47 (0:child,	 1:excludedReadId, 2:newRoot,		  3: )
*  slot0 32-39 (0:requiredReadId,1:unlocks,	   2:,			  3: )
*  slot3Index 30,31
*  slot2Index 28,29
*  slot1Index 26,27
*  slot0Index 24,25
*  scene      20-23
*  vacant	19
*  isOnetime	18
*  isQuote	17
*  hasRead	16
*  parent	8-15
*  id		0-7

   *
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

    eyelevelToggle 8bit
    toggleEyelevel 4-7 // eyelevelの変更先
    eyelevel 0-3       // 自身のeyelevel
    */
};





/*
* もう本当に言うけど、このシステム本当に複雑すぎる。
* 頭使いすぎて、頭おかしくなる。
* 地頭で考えるの本当にアホすぎる。
*
*
*
* */






/*
Elementを作るのを補助するGUIソフトを作ってほしい。
ただし、./main.cpp, ./dd.txtのみreadしてよい、writeはしてはいけない。その他のファイルのread, writeは不可。
作業にあたっては、事前にElement構造体とそのpropのprop bit assignmentの仕様を十分に把握すること。
まず、dd.txtをアップロードしたら、idとparentを参照して、ノードを構成してほしい。(ビューワ機能)
Unreal Engineのblueprintやblenderのノードのような、Element同士が線でつながっているようなGUIにしてほしい。
Elementを追加するときは、テキストウィンドウにdialogueを入力して、ノードを追加し、
GUI操作で、parentを選択したい。また、同様にchildも選択できるようにしたい。(エディタ機能)
まずは、dd.txtをビューワ機能で表示するところから始めてほしい。そのうえで、全く同じdd.txtを出力できることを目指してほしい。
ビューワにも種類を設け、parentとchildによってヒエラルキーを可視化するファミリービューワ
requiredReadId, excludeReadIdを可視化したconstraintビューワを実装してほしい。
その他、エディタ機能では適宜メニューから、その他のスロットのオプションを選択出来るようにしてほしい。
私に仕様を確認することなく、一気に完成させてほしい。不十分な機能は追ってフィードバックすることを前提に開発してほしい。
*
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
// (0: child, 1: excludedReadId, 2: newRoot)
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
// --- Strict Document model: dd.txt を 1 バイトも壊さずに保持する ---
// ============================================================================
// dd.txt の 1 行は `'dialogue', '0xHHHHHHHHHHHHHHHH',` の形式を取る。
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
inline bool parseElementLine(const std::string &line, std::string &dialogueOut,
                             uint64_t &propOut) {
  static const std::regex quotedHex(
      "^[ \t]*'(.*)'[ \t]*,[ \t]*'0x([0-9a-fA-F]{1,16})'[ \t]*,?[ \t]*$");
  static const std::regex quotedBareHex(
      "^[ \t]*'(.*)'[ \t]*,[ \t]*'([0-9a-fA-F]{1,16})'[ \t]*,?[ \t]*$");
  std::smatch m;
  if (!std::regex_match(line, m, quotedHex) &&
      !std::regex_match(line, m, quotedBareHex)) {
    return false;
  }
  dialogueOut = m[1].str();
  propOut = std::stoull(m[2].str(), nullptr, 16);
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
inline std::string formatPropHex16(uint64_t prop) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "%016llx",
                static_cast<unsigned long long>(prop));
  return std::string(buf);
}

// 未編集の行は原文をそのまま、dirty な Element 行のみ `'dialogue', '0xHEX',` に
// 再生成する。comment / blank line は原文のまま。terminator は原文の改行を再現するので、
// 未編集の Document では serializeDocument() == Document::raw が成立する。
std::string serializeDocument(const Document &doc) {
  std::string out;
  out.reserve(doc.raw.size());
  for (const RawLine &line : doc.lines) {
    if (line.kind == LINE_ELEMENT && line.elementIndex >= 0 &&
        doc.elements[line.elementIndex].dirty) {
      const Element &e = doc.elements[line.elementIndex].element;
      out += "'" + e.dialogue + "', '0x" + formatPropHex16(e.prop) + "',";
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
  size_t firstDiffLine = 0; // 1-based。0 は差異なし
  std::string originalSample;
  std::string serializedSample;
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

  // family / constraint viewer 用（このセッションでは描画しない）
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
  // 0 を除いた最小 scene を選ぶ（dd.txt では 0x1 になる）。
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
std::vector<Element> objectiveList;

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

unsigned gScene = 1;
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
// このセッションでは VIEW_GENERAL のみ実装し、family / constraint / editor は
// hook だけ置く。
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
  g.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, g.w, g.h);
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
// --- %(変数名) プレースホルダの展開（表示時のみ） ---
// Element.dialogue に含まれる %(name) を正規表現 %\(([A-Za-z0-9]+)\) で検出し、
// name が対象変数名リストに含まれる場合のみ、その変数の現在値を 10 進文字列に
// 置換する。対象外の name の %(name) はリテラルとしてそのまま出力する
// （置換・削除しない）。1 つの dialogue に複数の %(…) がある場合はすべて処理する。
// 展開は renderConsole の出力直前でのみ行う。格納された dialogue
// （containerA / containerB / containerC / displayList / objectiveList）は
// 変更せず、SDL_AppQuit で dd.txt に書き戻すテンプレートも元のまま残す。

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

// 指定されたverbIdをparentとして持つ有効なobjectiveが存在するか判定
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

    if (objectiveList.empty()) {
      if (isObjectiveSelected) {
        std::cout << "\033[31m*no content\033[0m" << std::endl;
      } else {
        std::cout << "\033[90mno content\033[0m" << std::endl;
      }
    } else {
      for (size_t j = 0; j < objectiveList.size(); ++j) {
        // 表示直前に %(変数名) をその時点の変数値へ展開する。格納 dialogue
        // は変更しない。
        std::string objText = expandTextPlaceholders(objectiveList[j].dialogue);
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
        if (item.dialogue == "dummy") {
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
        objectiveList.push_back(item);
      }
    }
  } while (hasDummy);
}

// --- 「移動する」コンテナ(id == 04)のトグル表示 ---
// 移動先リストはdisplayListと同じように毎回、parent == 04のElementから
// 構成する。gScene・gUnlocksをはじめとする変数は一切編集しない。
// 2段目はobjectiveList/selectedObjectiveIndexをそのまま流用するため、
// 入力システムの別処理は新設しない。
void rebuildMoveDestinationList() {
  objectiveList.clear();
  for (const auto &item : containerB) {
    if (getPropParent(item.prop) == 0x04) {
      objectiveList.push_back(item);
    }
  }
}

// トグル前に、inputシステム(displayList / objectiveListと
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
  gSuspendedObjectiveList = objectiveList;
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
  objectiveList = gSuspendedObjectiveList;
  selectedIndex = gSuspendedSelectedIndex;
  selectedObjectiveIndex = gSuspendedSelectedObjectiveIndex;
  isObjectiveSelected = gSuspendedIsObjectiveSelected;
  noContentState = gSuspendedNoContentState;
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
  // A timer counts completed dialogue cycles, not individual selections in a
  // verb/objective chain.  Advance only after returning to the current root.
  if (noContentState &&
      parent != gRoot) { // progress timer when verb to quote loop completed.
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
void drawGuiLine(SDL_Renderer *r, float x, float y, const std::string &text, float scale,
                 float budgetPx, uint8_t cr, uint8_t cg, uint8_t cb) {
  if (gGuiText.ready) {
    drawGuiText(r, x, y, text, scale, budgetPx, cr, cg, cb);
    return;
  }
  setSolid(r, cr, cg, cb);
  const size_t chars = budgetPx > 0.f
                           ? static_cast<size_t>(budgetPx / (6.f * std::max(1.f, scale)))
                           : 0;
  SDL_RenderDebugText(r, x, y, asciiPreview(text, chars).c_str());
}

// selectedNode の props 詳細（ASCII 行）。HUD と stdout の両方で使う。
std::vector<std::string> describeElement(int nodeIndex) {
  std::vector<std::string> lines;
  if (nodeIndex < 0 || nodeIndex >= static_cast<int>(gGraph.nodes.size()))
    return lines;
  const GraphNode &n = gGraph.nodes[nodeIndex];
  const uint64_t prop = gDocument.elements[n.elementIndex].element.prop;

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
                  hexByte(getPropNewRoot(prop)));
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
  std::cout << (rep.exact ? "RESULT: EXACT MATCH (identical dd.txt)"
                          : "RESULT: MISMATCH")
            << std::endl;
  if (!rep.exact) {
    std::cout << "first diff at line " << rep.firstDiffLine << std::endl;
    std::cout << "  original:   " << rep.originalSample << std::endl;
    std::cout << "  serialized: " << rep.serializedSample << std::endl;
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
    const float x1 = n.wx, y1 = n.wy;
    const float x2 = n.wx + static_cast<float>(kNodeW);
    const float y2 = n.wy + static_cast<float>(kNodeH);
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
  return worldToScreen(n.wx + static_cast<float>(kNodeW),
                       n.wy + static_cast<float>(kNodeH) * 0.5f);
}

inline SDL_FPoint inputPort(const GraphNode &n) {
  return worldToScreen(n.wx, n.wy + static_cast<float>(kNodeH) * 0.5f);
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
  const SDL_FPoint p = worldToScreen(n.wx, n.wy);
  const float w = static_cast<float>(kNodeW) * gViewZoom;
  const float h = static_cast<float>(kNodeH) * gViewZoom;
  const SDL_FRect rect{p.x, p.y, w, h};

  switch (n.kind) {
    case NODE_DUMMY:
      setSolid(r, 52, 52, 56);
      break;
    case NODE_TEMPLATE:
      setSolid(r, 58, 50, 40);
      break;
    case NODE_EMPTY:
      setSolid(r, 46, 52, 46);
      break;
    case NODE_CONTENT:
    default:
      setSolid(r, 38, 52, 74);
      break;
  }
  SDL_RenderFillRect(r, &rect);

  if (selected)
    setSolid(r, 255, 255, 255);
  else if (n.kind != NODE_CONTENT)
    setSolid(r, 140, 140, 140); // dummy / template / empty は構造ノードとして別色
  else
    setSolid(r, 118, 160, 205);
  SDL_RenderRect(r, &rect);

  // isQuote / isOnetime の左ストライプ。hasRead の印は load-time の bit をそのまま。
  float stripeX = p.x;
  if (n.isQuote) {
    setSolid(r, 210, 90, 90);
    const SDL_FRect stripe{stripeX, p.y, 4.f, h};
    SDL_RenderFillRect(r, &stripe);
    stripeX += 4.f;
  }
  if (n.isOnetime) {
    setSolid(r, 190, 120, 220);
    const SDL_FRect stripe{stripeX, p.y, 4.f, h};
    SDL_RenderFillRect(r, &stripe);
  }
  if (n.hasRead) {
    setSolid(r, 90, 200, 120);
    const SDL_FRect mark{p.x + w - 12.f, p.y + 4.f, 8.f, 8.f};
    SDL_RenderFillRect(r, &mark);
  }

  // 入力 / 出力ポート
  setSolid(r, 200, 200, 200);
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
  drawGuiLine(r, p.x + 6.f, p.y + 4.f,
              "id=" + hexByte(n.id) + " par=" + hexByte(n.parentId) + " sc=" +
                  hexNibble(n.scene) + (n.danglingParent ? " par!" : "") +
                  (n.danglingChild ? " chi!" : ""),
              gViewZoom * 0.75f, textBudget, 200, 225, 255);

  drawGuiLine(r, p.x + 6.f, p.y + 18.f,
              gDocument.elements[n.elementIndex].element.dialogue, gViewZoom, textBudget,
              235, 235, 235);

  setSolid(r, 180, 180, 180);
  std::string flags;
  if (n.hasRead) flags += "hasRead ";
  if (n.isQuote) flags += "quote ";
  if (n.isOnetime) flags += "onetime ";
  if (n.kind == NODE_DUMMY) flags += "dummy ";
  else if (n.kind == NODE_TEMPLATE) flags += "template ";
  else if (n.kind == NODE_EMPTY) flags += "empty ";
  if (!flags.empty())
    drawGuiLine(r, p.x + 6.f, p.y + 34.f, flags, gViewZoom * 0.7f, textBudget, 180, 180, 180);
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
      setSolid(r, 96, 176, 240); // parent -> id: blue
    else
      setSolid(r, 236, 168, 64); // id -> child: orange
    drawBezierEdge(r, outputPort(a), inputPort(b));
  }
}

// --- 後フェーズ hook（このセッションでは描画しない） ---
// VIEW_FAMILY: gGraph.parentAdj / childAdj / childLinkAdj / roots でツリーを描く。
void drawFamilyView(SDL_Renderer *r) { (void)r; }
// VIEW_CONSTRAINT: gGraph.requiredReadAdj / excludedReadAdj（slot1 + slot2）を描く。
void drawConstraintView(SDL_Renderer *r) { (void)r; }
// EDITOR: ElementRecord.dirty を立てて serializeDocument() で再生成する。
void drawEditorOverlay(SDL_Renderer *r) { (void)r; }

void drawHud(SDL_Renderer *r) {
  int winW = 0, winH = 0;
  windowSize(&winW, &winH);
  const int hudW = std::min(kHudW, winW);
  const float textBudget = static_cast<float>(hudW) - 20.f;

  setSolid(r, 22, 24, 28);
  const SDL_FRect panel{0.f, 0.f, static_cast<float>(hudW), static_cast<float>(winH)};
  SDL_RenderFillRect(r, &panel);
  setSolid(r, 120, 140, 170);
  const SDL_FRect divider{static_cast<float>(hudW) - 1.f, 0.f, 1.f,
                          static_cast<float>(winH)};
  SDL_RenderFillRect(r, &divider);

  float y = 8.f;
  const auto hudLine = [&](const std::string &text, uint8_t cr, uint8_t cg, uint8_t cb) {
    if (y > static_cast<float>(winH) - 14.f) return; // 画面高を超える行は描かない
    drawGuiLine(r, 8.f, y, text, 1.f, textBudget, cr, cg, cb);
    y += static_cast<float>(kTextStep);
  };

  hudLine("DDMEditor graph viewer  mode=" + std::string(viewModeName()), 220, 235, 250);
  hudLine("keys 1-4 = general/family/constraint/editor", 190, 205, 225);
  hudLine("scene=" + hexNibble(gViewScene) + "  [ ] = prev/next scene   L = fit   0 = reset",
          190, 205, 225);
  std::string sceneList = "scenes:";
  for (uint8_t s : gGraph.scenes) {
    sceneList += (s == gViewScene ? " *" : " ") + hexNibble(s) + "(" +
                 std::to_string(gGraph.nodesInScene(s).size()) + ")";
  }
  hudLine(sceneList, 190, 205, 225);
  hudLine("nodes=" + std::to_string(gGraph.nodes.size()) +
          " edges=" + std::to_string(gGraph.edges.size()) +
          " roots=" + std::to_string(gGraph.roots.size()), 190, 205, 225);
  hudLine("doc=" + gLoadedPath + " bytes=" + std::to_string(gDocument.raw.size()) +
          " lines=" + std::to_string(gDocument.lines.size()) +
          " elem=" + std::to_string(gDocument.parsedLines) +
          " comment=" + std::to_string(gDocument.commentLines) +
          " blank=" + std::to_string(gDocument.blankLines), 190, 205, 225);
  hudLine("round-trip: " + std::string(gRoundTrip.exact ? "EXACT OK" : "MISMATCH") +
          " serialized=" + std::to_string(gRoundTrip.serializedBytes),
          gRoundTrip.exact ? 150, 235, 170 : 240, 200, 120);
  if (!gRoundTrip.exact && gRoundTrip.firstDiffLine > 0) {
    hudLine("  first diff line=" + std::to_string(gRoundTrip.firstDiffLine), 240, 200, 120);
  }
  hudLine("font: " + std::string(gGuiText.ready ? gGuiText.fontPath.c_str()
                                                 : "ascii fallback (no freetype/jp font)"),
          160, 200, 170);

  y += 6.f;
  if (gSelectedNode >= 0) {
    hudLine("selected node props:", 255, 255, 255);
    for (const std::string &l : describeElement(gSelectedNode))
      hudLine(l, 215, 225, 235);
  } else {
    hudLine("selected: none (click node, ESC = clear, D = dump stdout)", 180, 180, 180);
  }

  if (gExportPanel) {
    y += 6.f;
    hudLine("export text (serializeDocument):", 200, 240, 200);
    const std::string serialized = serializeDocument(gDocument);
    size_t pos2 = 0;
    while (pos2 < serialized.size()) {
      const size_t nl = serialized.find('\n', pos2);
      std::string l = nl == std::string::npos ? serialized.substr(pos2)
                                              : serialized.substr(pos2, nl - pos2);
      hudLine(l, 190, 210, 190);
      if (nl == std::string::npos) break;
      pos2 = nl + 1;
    }
    hudLine("E = dump full dd.txt to stdout   X = dump on quit   P = toggle", 200, 240, 200);
  }

  y += 6.f;
  hudLine("drag = pan   wheel/+/- = zoom   V = graph toggle   R = round-trip", 160, 170, 180);
  hudLine("parent edge = blue, child edge = orange. grey = dummy/template/empty", 160, 170, 180);
  hudLine("R/F/C/EDIT: phase hook, not implemented in this session", 160, 170, 180);
}

void drawGraphOverlay(SDL_Renderer *r) {
  if (gGraphEnabled) {
    drawGraphEdges(r);
    for (int i : gGraph.nodesInScene(gViewScene))
      drawNodeBox(r, gGraph.nodes[i], gSelectedNode == i);

    if (gViewMode == VIEW_FAMILY) drawFamilyView(r);
    else if (gViewMode == VIEW_CONSTRAINT) drawConstraintView(r);
    else if (gViewMode == EDITOR) drawEditorOverlay(r);
  }
  drawHud(r);
}

int pickNode(float sx, float sy) {
  const SDL_FPoint world = screenToWorld(sx, sy);
  for (int i : gGraph.nodesInScene(gViewScene)) {
    const GraphNode &n = gGraph.nodes[i];
    if (world.x >= n.wx && world.x <= n.wx + static_cast<float>(kNodeW) &&
        world.y >= n.wy && world.y <= n.wy + static_cast<float>(kNodeH)) {
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
  layoutScene(gViewScene);
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
      ev->button.button == SDL_BUTTON_LEFT) {
    const int hit = pickNode(ev->button.x, ev->button.y);
    if (hit >= 0) {
      gSelectedNode = hit;
      gDragging = false;
      printSelectedElementDetails();
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

  // --- 日本語描画: FreeType を動的ロードして日本語フォントを探す ---
  if (initGuiText()) {
    std::cout << "gui text: freetype ok, font=" << gGuiText.fontPath << std::endl;
  } else {
    std::cout << "gui text: freetype / jp font not found -> ASCII '?' fallback: "
              << gGuiText.lastError << std::endl;
  }

  // --- 厳密な Document loader ---
  // ./dd.txt を想定し、build/ 直下から実行する既存運用では ../dd.txt を試す。
  const char *candidates[] = {"./dd.txt", "../dd.txt"};
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
    std::cerr << "エラー: ./dd.txt / ../dd.txt を開けませんでした。" << std::endl;
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

  // --- graph view: pan / zoom / node selection（挿入モード中は触らない） ---
  if (gGraphEnabled && gAppMode == MODE_NORMAL &&
      (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
       event->type == SDL_EVENT_MOUSE_MOTION ||
       event->type == SDL_EVENT_MOUSE_WHEEL)) {
    handlePointerEvent(event);
    return SDL_APP_CONTINUE;
  }

  if (gAppMode == MODE_NORMAL) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;

        // --- graph view keys（既存の入力は壊さない。VIEW_GENERAL の操作のみ） ---
        // 注: I/O/A/U/F/M は既存の入力システムそのまま。1-4 は既存で未使用のキー。
        if (key == SDLK_1) { gViewMode = VIEW_GENERAL; needRedraw = true; }
        else if (key == SDLK_2) { gViewMode = VIEW_FAMILY; needRedraw = true; }
        else if (key == SDLK_3) { gViewMode = VIEW_CONSTRAINT; needRedraw = true; }
        else if (key == SDLK_4) { gViewMode = EDITOR; needRedraw = true; }
        else if (key == SDLK_V) { gGraphEnabled = !gGraphEnabled; needRedraw = true; }
        else if (key == SDLK_P) { gExportPanel = !gExportPanel; needRedraw = true; }
        else if (key == SDLK_X) { gExportOnQuit = !gExportOnQuit; needRedraw = true; }
        else if (key == SDLK_E) { printDocumentToStdout(gDocument); }
        else if (key == SDLK_R) { printRoundTripReport(gDocument); }
        else if (key == SDLK_D) { printSelectedElementDetails(); }
        else if (key == SDLK_L) { layoutScene(gViewScene); fitViewToScene(); needRedraw = true; }
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

      if (key == SDLK_Q) {
        return SDL_APP_SUCCESS;
      }

      // 「移動する」モード中は挿入しない(退避した元の表示・選択を壊さない)。
      if (key == SDLK_TAB && !gMoveMode) {
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
    printRoundTripReport(gDocument);
    printDocumentToStdout(gDocument);

    // view scene の先頭ノードの中心をクリックし、pickNode が正しい node を
    // 選ぶことを確認する。続けて zoom/pan を適用した後も同じ node が選ぶことを
    // world/screen 変換の往復として確認する。
    const std::vector<int> inScene = gGraph.nodesInScene(gViewScene);
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

      std::cout << "=== [ selftest selection ] ===" << std::endl;
      std::cout << "want idx=" << want << " scene=" << hexNibble(n.scene)
                << " id=" << hexByte(n.id) << " picked=" << gSelectedNode
                << " match=" << (gSelectedNode == want ? 1 : 0) << std::endl;

      zoomAt(320.f, 120.f, 1.f); // wheel zoom in
      gViewPanX += 40.f;         // pan
      gViewPanY -= 25.f;
      const SDL_FPoint after = worldToScreen(cx, cy);
      ev.button.x = after.x;
      ev.button.y = after.y;
      gSelectedNode = -1;
      handlePointerEvent(&ev);
      std::cout << "after zoom/pan picked=" << gSelectedNode
                << " match=" << (gSelectedNode == want ? 1 : 0) << std::endl;
      printSelectedElementDetails();

    }

    std::cout << "=== [ selftest font ] ===" << std::endl;
    std::cout << "gui text: " << (gGuiText.ready ? "READY" : "ASCII fallback")
              << " font=" << gGuiText.fontPath << " err=" << gGuiText.lastError << std::endl;
    if (gGuiText.ready && gRenderer) {
      // 日本語 glyph が rasterize できているか（texture と bitmap 寸法を確認する）。
      for (uint32_t cp : {0x8983u /*見*/, 0x8A71u /*話*/, 0x308Bu /*る*/, 0x5C31u /*札*/}) {
        const GlyphRec *g = guiGlyph(gRenderer, cp);
        std::cout << "glyph cp=" << cp << " drawn=" << (g ? 1 : 0) << " w=" << (g ? g->w : 0)
                  << " h=" << (g ? g->h : 0) << " adv=" << (g ? g->advance : 0.f) << std::endl;
      }
    }

    // 画面を実際に描いて pixel を読み出し、日本語テキストが描画されているか検証する。
    if (gRenderer) {
      SDL_SetRenderDrawColor(gRenderer, 30, 30, 30, 255);
      SDL_RenderClear(gRenderer);
      drawGraphOverlay(gRenderer);
      SDL_RenderPresent(gRenderer);
      SDL_Surface *shot = SDL_RenderReadPixels(gRenderer, nullptr);
      if (shot) {
        SDL_SaveBMP(shot, "build/viewer_frame.bmp");
        std::cout << "frame captured " << shot->w << "x" << shot->h <<
                  " -> build/viewer_frame.bmp" << std::endl;
        // 読み出し surface のレイアウトは driver 依存なので、確定した RGB24 に
        // 変換してから pixel を読む（RGBA8888/XRGB8888 の取り違えを防ぐ）。
        SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24);
        if (rgba) {
          // 明い pixel（テキスト・線・枠）の色をヒストグラムで集める。
          // 背景(30,30,30)と HUD panel(22,24,28)は除く。
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
          SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(shot);
      } else {
        std::cout << "frame capture unavailable: " << SDL_GetError() << std::endl;
      }
    }

    switchScene(+1);
    std::cout << "switchScene(+1) -> scene=" << hexNibble(gViewScene)
              << " nodes=" << gGraph.nodesInScene(gViewScene).size() << std::endl;
    return SDL_APP_SUCCESS;
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

  SDL_SetRenderDrawColor(gRenderer, 30, 30, 30, 255);
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
void backupData(const std::string &srcPath = "../dd.txt",
                const std::string &dstPath = "../dd.txt.bak") {
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
  backupData("../dd.txt", "../dd.txt.bak");

  // write は行わない。round-trip は画面の export 欄と stdout で提示する。
  if (gExportOnQuit) {
    printDocumentToStdout(gDocument);
  }

  // 従来の書き戻しは、load元が ../dd.txt の既存運用（build/ 直下から実行）でのみ
  // 維持する。./dd.txt をloadした運用では dd.txt を書き換えないため書かない。
  if (gLoadedPath == "../dd.txt") {
    std::ofstream ofs("../dd.txt");
    if (ofs.is_open()) {
      for (auto item : containerA) {
        if (item.dialogue != "dummy") {
          item.prop &= ~(1ULL << 16); // clear hasRead (bit 16)
        }

        ofs << "'" << item.dialogue << "', '0x" << std::hex << std::setw(16)
            << std::setfill('0') << item.prop << "',\n";
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
