#define SDL_MAIN_USE_CALLBACKS 1
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "document.h"
#include "graph.h"
#include "state.h"
#include "guitext.h"
#include "game.h"
#include "console.h"
#include "layout.h"
#include "view.h"
#include "hud.h"
#include "input.h"
#include "app.h"
#include "selftest.h"






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

// SDL_AppEvent
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  return handleAppEvent(event);
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
    if (!runSelftest()) return SDL_APP_FAILURE;
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


// SDL_AppQuit
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  appQuit();
}
