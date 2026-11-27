#include "hud.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include "prop.h"
#include "document.h"
#include "graph.h"
#include "state.h"
#include "guitext.h"
#include "placeholders.h"
#include "layout.h"
#include "view.h"
#include "editor.h"
#include "props.h"





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
