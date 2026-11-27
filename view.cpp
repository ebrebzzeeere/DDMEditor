#include "view.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>
#include "prop.h"
#include "document.h"
#include "graph.h"
#include "state.h"
#include "palette.h"
#include "guitext.h"
#include "game.h"
#include "layout.h"
#include "props.h"
#include "hud.h"





// ============================================================================
// --- VIEW_GENERAL の描画（blueprint 風のノード箱と bezier 線） ---
// ============================================================================

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
std::vector<std::string> describeElement(int nodeIndex, ViewMode view) {
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
