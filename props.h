#pragma once
// props menu 編集（selectedNode の prop fields・validation・undo）。設計文書コメントは props.cpp の定義側に原文のまま。
#include <string>
#include <utility>
#include <vector>

#include "state.h"

inline uint32_t propsNodeKeyOf(uint64_t prop) {
  return nodeKey(getPropScene(prop), getPropID(prop));
}

int propsResolveCurrentNode();
uint64_t propsCurrentProp();
void propsOpen(int nodeIndex);
void propsClose();
void propsCommit(const uint64_t newProp, const std::vector<std::string> &warnings);
std::vector<std::string> propsValidateEdit(const PropsFieldDef &f, uint64_t newProp);
std::vector<std::string> propsOptionSwitchWarnings(uint8_t slot, uint64_t newProp);
bool propsApplyFieldValue(int fieldIdx, uint64_t wantValue, bool absolute, int delta = 0);
void propsSetBoolField(int fieldIdx, bool v);
void propsToggleField(int fieldIdx);
bool propsSetOption(int fieldIdx, uint8_t pos);
void propsSetBoolValueCurrent(bool yes);
void propsNextField();
void propsUndo();
void propsRebuildGraph();
std::vector<std::pair<std::string, uint32_t>> propsMenuRows();
