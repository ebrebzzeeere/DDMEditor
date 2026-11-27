#include "editor.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <SDL3/SDL.h>
#include "prop.h"
#include "document.h"
#include "graph.h"
#include "state.h"
#include "guitext.h"
#include "game.h"
#include "layout.h"
#include "props.h"





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
  line.text = "'" + gEditor.dialogue + "', '0x" + formatPropHex16Piped(prop) + "',"; // 正格式（lowercase 16桁・2-2-4区切り（'-'）・trailing comma）
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
