#pragma once
// EDITOR mode の Element 追加フロー（Document/Graph に反映、runtime 同期は G トグル）。設計文書コメントは editor.cpp の定義側に原文のまま。
#include <string>

#include "state.h"

uint64_t composeEditorProp(uint8_t id, uint8_t parent, uint8_t scene, bool isQuote,
                           bool isOnetime, uint8_t childId);
int nextFreeId(uint8_t scene);
const char *editorStageName(EditorStage stage);
void editorStartText();
void editorStopText();
bool editorValidateAdd(std::string &why);
void editorBeginAdd();
void editorCancelAdd();
void editorLeaveModeIfNeeded();
void editorBackspaceDialogue();
void editorEnterKey();
void editorYN(bool yes);
void editorCommitAdd();
void editorRestoreToBaseline(size_t targetLines, size_t targetElements);
