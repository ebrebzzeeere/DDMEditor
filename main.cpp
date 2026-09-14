#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct Element {
  std::string text;
  uint64_t prop;
};

// グローバル変数
std::vector<Element> containerA;
std::vector<Element> containerB;
std::vector<Element> containerC;
std::vector<Element> displayList;

size_t selectedIndex = 0;
bool needRedraw = true;
bool noContentState = false;

SDL_Window *gWindow = nullptr;
SDL_Renderer *gRenderer = nullptr;

unsigned gScene = 0;
unsigned gRoot = 0;
unsigned gParent = 0;
unsigned gUnlocks = 0;

void rebuildContainerB();
void rebuildContainerC();
void rebuildDisplayList();

void renderConsole() {
  std::cout << "\033[2J\033[1;1H";

  // 1. ステート情報
  std::cout << "=== [ State ] ===" << std::endl;
  std::cout << "gScene   : " << gScene << "\n"
            << "gRoot    : " << gRoot << "\n"
            << "gParent  : " << gParent << "\n"
            << "gUnlocks : 0x" << std::hex << gUnlocks << std::dec << "\n\n";

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
  std::cout << "-------------------------------------------\n" << std::endl;

  if (noContentState || displayList.empty()) {
    std::cout << "\033[33m[no content]\033[0m" << std::endl;
    std::cout << "\n(Press 'f' to continue / fallback)" << std::endl;
    std::cout << std::flush;
    return;
  }

  std::cout << "=== [ Selection ] ===" << std::endl;
  for (size_t i = 0; i < displayList.size(); ++i) {
    if (i == selectedIndex) {
      std::cout << "\033[31m[" << displayList[i].text << "]\033[0m ";
    } else {
      std::cout << "\033[37m[" << displayList[i].text << "]\033[0m ";
    }
  }
  std::cout << std::endl;
  std::cout << "\n(Focus Window -> 'o': Right, 'a': Left, 'f': Send, 'q': Quit)"
            << std::endl;
  std::cout << std::flush;
}

void rebuildContainerB() {
  containerB.clear();
  for (const auto &item : containerA) {
    unsigned scene = (item.prop >> 36) & 0xF;
    if (scene == gScene) {
      containerB.push_back(item);
    }
  }
}

void rebuildContainerC() {
  containerC.clear();
  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = (prop >> 8) & 0xFF; // bit 24-31
    unsigned hasRead = (prop >> 34) & 0x1;
    unsigned reqFlags = (prop >> 48) & 0xFF;
    unsigned excFlags = (prop >> 56) & 0xFF;

    bool cond1 = (parent == gParent);
    bool cond2 = ((gUnlocks & reqFlags) == reqFlags);
    bool cond3 = (hasRead == 0);
    bool cond4 = ((gUnlocks & excFlags) == 0);

    if (cond1 && cond2 && cond3 && cond4) {
      containerC.push_back(item);
    }
  }
}

void rebuildDisplayList() {
  displayList.clear();
  selectedIndex = 0;

  if (!containerC.empty()) {
    std::random_device rd;
    std::mt19937 gen(rd());

    bool hasOnetimeQuote = false;
    for (const auto &item : containerC) {
      bool isOnetime = (item.prop >> 33) & 0x1;
      bool isQuote = (item.prop >> 32) & 0x1;
      if (isOnetime && isQuote) {
        hasOnetimeQuote = true;
        break;
      }
    }

    if (hasOnetimeQuote) {
      std::uniform_int_distribution<size_t> dist(0, containerC.size() - 1);
      displayList.push_back(containerC[dist(gen)]);
    } else if (containerC.size() >= 5) {
      std::vector<Element> temp = containerC;
      std::shuffle(temp.begin(), temp.end(), gen);
      displayList.assign(temp.begin(), temp.begin() + 4);
    } else {
      displayList = containerC;
    }
  }
}

void processContainerLifecycle() {
  rebuildContainerB();
  rebuildContainerC();

  if (containerC.empty()) {
    noContentState = true;
    displayList.clear();
  } else {
    noContentState = false;
    rebuildDisplayList();
  }
}

void sendProperty() {
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
        rebuildContainerC();
      }
    }

    noContentState = false;
    rebuildDisplayList();
    needRedraw = true;
    return;
  }

  if (displayList.empty() || selectedIndex >= displayList.size())
    return;

  Element selected = displayList[selectedIndex];
  uint64_t prop = selected.prop;

  unsigned id = prop & 0xFF;              // bit 0-7
  unsigned parent = (prop >> 8) & 0xFF;  // bit 24-31
  unsigned newRoot = (prop >> 16) & 0xFF; // bit 16-23
  unsigned unlocks = (prop >> 24) & 0xFF; // bit 24-31
  bool isOnetime = (prop >> 33) & 0x1;    // bit 33
  unsigned newScene = (prop >> 40) & 0xFF;// bit 40-47

  gParent = id;

  if (unlocks == 0 && newRoot != 0 && newRoot <= 8) {
    unlocks = 1u << (newRoot - 1);
  }

  if (isOnetime) {
    for (auto &item : containerA) {
      if (item.text == selected.text && item.prop == selected.prop) {
        item.prop |= (1ULL << 34);
        break;
      }
    }
  }

  if (newScene != 0) {
    gScene = newScene;
    gRoot = 0;
    gParent = 0;
    gUnlocks = 0;
  } else {
    gUnlocks |= unlocks;

    if (newRoot != 0) {
      gRoot = newRoot;
    }
  }

  processContainerLifecycle();
  needRedraw = true;
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

uint64_t buildProperty(unsigned id, unsigned parent, unsigned scene, bool isQuote, bool isOnetime) {
  uint64_t prop = 0;
  prop |= (id & 0xFF);                               // bit 0-7: id
  prop |= ((uint64_t(parent) & 0xFF) << 8);         // bit 24-31: parent
  if (isQuote)   prop |= (1ULL << 32);               // bit 32: isQuote
  if (isOnetime) prop |= (1ULL << 33);               // bit 33: isOnetime
  prop |= ((uint64_t(scene) & 0x0F) << 36);          // bit 36-39: scene
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
  needRedraw = true;
}

// SDL_AppInit
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
    return SDL_APP_FAILURE;
  }

  // 画面作成
  gWindow = SDL_CreateWindow("DDMEditor Focus Window", 320, 240, 0);
  if (!gWindow) {
    std::cerr << "Window Creation Error: " << SDL_GetError() << std::endl;
    return SDL_APP_FAILURE;
  }

  // レンダラー作成（VSyncを有効化して無制限ルーピングを防止）
  gRenderer = SDL_CreateRenderer(gWindow, nullptr);
  if (!gRenderer) {
    std::cerr << "Renderer Creation Error: " << SDL_GetError() << std::endl;
    return SDL_APP_FAILURE;
  }
  SDL_SetRenderVSync(gRenderer, 1);

  std::string file_path = "../dd.txt";
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "エラー: " << file_path << " を開けませんでした。" << std::endl;
    return SDL_APP_FAILURE;
  }

  std::string token;
  std::string current_text;
  bool is_text = true;

  while (file >> token) {
    if (token.rfind("//", 0) == 0 || token.rfind("/*", 0) == 0 || token.rfind("*", 0) == 0) {
      std::string dummy;
      std::getline(file, dummy);
      continue;
    }

    std::string clean_token = "";
    for (char c : token) {
      if (c != '\'' && c != ',' && c != ' ' && c != '\r' && c != '\n') {
        clean_token += c;
      }
    }
    if (clean_token.empty()) continue;

    if (is_text) {
      current_text = clean_token;
      is_text = false;
    } else {
      try {
        uint64_t prop = std::stoull(clean_token, nullptr, 16);
        containerA.push_back({current_text, prop});
        is_text = true;
      } catch (const std::exception &) {
        is_text = true;
        continue;
      }
    }
  }
  file.close();

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

  if (gAppMode == MODE_NORMAL) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
      SDL_Keycode key = event->key.key;

      if (key == SDLK_Q) {
        return SDL_APP_SUCCESS;
      }

      if (key == SDLK_I) {
        gAppMode = MODE_INSERT_TEXT;
        gInputText.clear();
        SDL_StartTextInput(gWindow);
        needRedraw = true;
      } else if (key == SDLK_F) {
        sendProperty();
      } else if (!displayList.empty() && !noContentState) {
        size_t total = displayList.size();
        if (key == SDLK_O) {
          selectedIndex = (selectedIndex + 1) % total;
          needRedraw = true;
        } else if (key == SDLK_A) {
          selectedIndex = (selectedIndex - 1 + total) % total;
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

        unsigned id = containerA.size() + 1;
        uint64_t newProp = buildProperty(id, gParent, gScene, gInputIsQuote, gInputIsOnetime);

        Element newElem = { gInputText, newProp };
        containerA.push_back(newElem);
        containerB.push_back(newElem);

        rebuildContainerC();

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
  // ターゲット: 30 FPS (1フレームにつき 33ms)
  constexpr Uint64 TARGET_FRAME_MS = 33;
  Uint64 frameStart = SDL_GetTicks();

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
  SDL_RenderPresent(gRenderer);

  // 30 FPS 制御のためのスリープ計算
  Uint64 frameTime = SDL_GetTicks() - frameStart;
  if (frameTime < TARGET_FRAME_MS) {
    SDL_Delay(TARGET_FRAME_MS - frameTime);
  }

  return SDL_APP_CONTINUE;
}

// SDL_AppQuit（終了時の自動上書き保存）
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  std::ofstream ofs("../dd.txt");
  if (ofs.is_open()) {
    for (auto item : containerA) {
      // bit 34 (hasRead) を 0 にクリア
      item.prop &= ~(1ULL << 34);

      ofs << "'" << item.text << "', '0x"
          << std::hex << std::setw(16) << std::setfill('0') << item.prop << "',\n";
    }
    ofs.close();
  }

  if (gRenderer) SDL_DestroyRenderer(gRenderer);
  if (gWindow) SDL_DestroyWindow(gWindow);
}
