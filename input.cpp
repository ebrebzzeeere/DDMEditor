#include "input.h"
#include <algorithm>
#include <cstdint>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_keycode.h>
#include "prop.h"
#include "document.h"
#include "state.h"
#include "guitext.h"
#include "game.h"
#include "console.h"
#include "layout.h"
#include "view.h"
#include "editor.h"
#include "props.h"
#include "hud.h"
#include "app.h"





static bool handlePropsKeys(bool editorMode, SDL_Keycode key) {
      // props panel（Prompt 5）: open のとき編集キーを先に処理する（add flow の ENTER/Y/N/G・
      // graph の +/- zoom・ESC 選択解除に優先する。props は idle のときだけ開くので add flow と
      // 同時状態にならない＝衝突しない）。1-4 / V / P / X / E / R / D / L / 0 / [ ] / 矢印 / Q は
      // 共通経路のまま通す（mode 切替キーは editorLeaveModeIfNeeded が panel を閉じる）。
  if (editorMode && gProps.active) {
        if (key == SDLK_TAB) { propsNextField(); return true; }
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
          propsToggleField(gProps.cursor); // ENTER/SPACE = トグル（option cycle / bool 反転 / enum cycle / uint 最低bit）
          return true;
        } else if (key == SDLK_PLUS) { propsApplyFieldValue(gProps.cursor, 0, false, +1); return true; }
        else if (key == SDLK_MINUS) { propsApplyFieldValue(gProps.cursor, 0, false, -1); return true; }
        else if (key == SDLK_Y) { propsSetBoolValueCurrent(true); return true; }
        else if (key == SDLK_N) { propsSetBoolValueCurrent(false); return true; }
        else if (key == SDLK_U) { propsUndo(); return true; } // runtime の U は editor で遮断済み
        else if (key == SDLK_G) { propsRebuildGraph(); return true; } // add flow の G（runtime 同期）は panel open 時遮断
        else if (key == SDLK_ESCAPE) { propsClose(); return true; } // panel 閉じを優先（選択解除・add 取消より先）
  }
  return false;
}

static void handleViewerKeys(SDL_Keycode key) {
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
}

static void handleEditorGuiKeys(SDL_Keycode key, bool editorMode) {
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
}

static void handleRuntimeKeys(SDL_Keycode key, bool editorMode) {
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

static SDL_AppResult handleInsertTextEvent(SDL_Event *event) {
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
  return SDL_APP_CONTINUE;
}

static SDL_AppResult handleInsertQuoteEvent(SDL_Event *event) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;
      if (key == SDLK_Y || key == SDLK_N) {
        gInputIsQuote = (key == SDLK_Y);
        gAppMode = MODE_INSERT_ONETIME;
        needRedraw = true;
      }
    }
    return SDL_APP_CONTINUE;
  return SDL_APP_CONTINUE;
}

static SDL_AppResult handleInsertOnetimeEvent(SDL_Event *event) {
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
  return SDL_APP_CONTINUE;
}

SDL_AppResult handleAppEvent(SDL_Event *event) {
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

      if (handlePropsKeys(editorMode, key)) return SDL_APP_CONTINUE;

      // --- Prompt 6・game モード共存（要件 7）+ write policy トグル（要件 2） ---
      // H = viewer/game 切替（game 側でも復帰用に動作する）。B = backup on quit トグル（既定 off）。
      // graph view keys（1-4 / V / P / X / E / R / D / L / 0 / +/- / [ ] / 矢印 / viewer の
      // ESC 選択解除 / editor・props GUI キー）は viewer のみ通す。runtime キー（TAB/F/O/I/A/U/M/Y/N）
      // と Q / 挿入モードの ESC はどちらの mode でもそのまま（既存の入力システムを壊さない）。
      if (key == SDLK_H) { setUiMode(viewerUi ? UI_GAME : UI_VIEWER); return SDL_APP_CONTINUE; }
      if (key == SDLK_B) { gBackupOnQuit = !gBackupOnQuit; needRedraw = true; }

      if (viewerUi) handleViewerKeys(key);

      if (editorMode) handleEditorGuiKeys(key, editorMode);

      if (key == SDLK_Q) {
        return SDL_APP_SUCCESS;
      }

      handleRuntimeKeys(key, editorMode);
    }
    return SDL_APP_CONTINUE;
  }

  if (gAppMode == MODE_INSERT_TEXT) return handleInsertTextEvent(event);
  if (gAppMode == MODE_INSERT_QUOTE) return handleInsertQuoteEvent(event);
  if (gAppMode == MODE_INSERT_ONETIME) return handleInsertOnetimeEvent(event);
  return SDL_APP_CONTINUE;
}
