#include "props.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "prop.h"
#include "document.h"
#include "graph.h"
#include "state.h"
#include "palette.h"
#include "guitext.h"
#include "layout.h"





// ============================================================================
// --- Prompt 5: props menu（selectedNode の props editor・add flow とは別 state） ---
// ============================================================================

// 編集は Document / Graph にだけ反映し runtime state に自動同期しない（Prompt4 の G トグルとは別）。
// graph 再構築経路は editorCommitAdd と同じ: buildGraph → applyActiveLayout →
// 選択は (scene,id) key を gGraph.findNode で復元する。dirty=true の行再生成は
// serializeDocument と同じ正格式（lowercase 16桁・2-2-4区切り（'-'）・trailing comma）で行う。
// validation: 範囲外は clamp + warning、target id が現 scene に無い場合は warning +
// constraint viewer の ghost で確認できる（countConstraintEdges・配線追加不要）、
// slot の option は index が 1 つなので構造上排他（同一 slot に複数 option は立てられない）。

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
  // 正格式再生成（追加行の format と同じ規則・lowercase 16桁・2-2-4区切り（'-'）・trailing comma・dialogue 原文のまま）。
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
bool propsApplyFieldValue(int fieldIdx, uint64_t wantValue, bool absolute, int delta) {
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

