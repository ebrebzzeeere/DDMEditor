#include <SDL3/SDL_keycode.h>
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
  /* prop bit assignment
  excFlags	56-63
  reqFlags	48-55
  unlocks	40-47
  scene		36-39
  flagsFormat	35 // If 1 read req/excFlags as Flags, nor as id to check
  hasRead	34
  isOnetime	33
  isQuote	32
  newRoot	24-31
  child 	16-23
  parent	8-15
  id		0-7
  */
};

// グローバル変数
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

SDL_Window *gWindow = nullptr;
SDL_Renderer *gRenderer = nullptr;

unsigned gScene = 2;
unsigned gRoot = 16;
unsigned gParent = 16;
unsigned gUnlocks = 0;

void rebuildContainerB();
void rebuildContainerC();
void rebuildDisplayList();
void rebuildObjectiveList();

// 指定されたverbIdをparentとして持つ有効なobjectiveが存在するか判定
bool hasValidObjective(unsigned verbId) {
  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = (prop >> 8) & 0xFF;
    unsigned hasRead = (prop >> 34) & 0x1;
    unsigned reqFlags = (prop >> 48) & 0xFF;
    unsigned excFlags = (prop >> 56) & 0xFF;
    unsigned flagsFormat = (prop >> 35) & 0x1;

    bool cond1 = (parent == verbId);
    bool cond2 = ((gUnlocks & reqFlags) == reqFlags);
    bool cond3 = (hasRead == 0);
    bool cond4 = !((gUnlocks & excFlags) == excFlags) || excFlags == 0;

    if (flagsFormat == 0 && (reqFlags != 0x0 || excFlags != 0x0)) {

      unsigned checkCompletion = 0x0;
      if (reqFlags == 0) {
        checkCompletion |= 0x1;
      }
      if (excFlags == 0) {
        checkCompletion |= 0x2;
      }

      for (const auto &subItem : containerB) { // check! nested for loop.
        if (item.text == subItem.text) {
          continue;
        }
        uint64_t subProp = subItem.prop;
        if ((subProp & 0xff) == reqFlags) {
          cond2 = (subProp >> 34) & 0x1;
          checkCompletion |= 0x1;
        }
        if ((subProp & 0xff) == excFlags) {
          cond4 = !((subProp >> 34) & 0x1);
          checkCompletion |= 0x2;
        }
        if (checkCompletion == 0x3) {
          break;
        }
      }
    }

    if (cond1 && cond2 && cond3 && cond4) {
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
    unsigned verbId = displayList[i].prop & 0xFF;
    unsigned verbChild = (displayList[i].prop >> 16) & 0xFF;

    bool hasObj = false;
    if (verbChild) {
      for (const auto &item : containerB) {
        if ((item.prop & 0xff) == verbChild) {
          if (item.text == "dummy") {
            hasObj = hasValidObjective(item.prop & 0xff);
          } else {
            hasObj = true;
          }
          break;
        }
      }
    } else {
      hasObj = hasValidObjective(verbId);
    }

    std::string itemText = displayList[i].text;
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
  if (objectiveList.empty()) {
    if (isObjectiveSelected) {
      std::cout << "\033[31m*no content\033[0m" << std::endl;
    } else {
      std::cout << "\033[90mno content\033[0m" << std::endl;
    }
  } else {
    for (size_t j = 0; j < objectiveList.size(); ++j) {
      std::string objText = objectiveList[j].text;
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

  std::cout << "\n(Focus Window -> 'i': Left, 'o': Right, 'a': Select "
               "Objective, 'u': Select Verb, 'f': Send, 'q': Quit)"
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

    unsigned parent = (prop >> 8) & 0xFF;
    unsigned child = (prop >> 16) & 0xff;
    unsigned hasRead = (prop >> 34) & 0x1;
    unsigned reqFlags = (prop >> 48) & 0xFF;
    unsigned excFlags = (prop >> 56) & 0xFF;
    unsigned flagsFormat = (prop >> 35) & 0x1;

    bool cond1 = (parent == gParent);
    bool cond2 = ((gUnlocks & reqFlags) == reqFlags);
    bool cond3 = (hasRead == 0);
    bool cond4 = !((gUnlocks & excFlags) == excFlags) || excFlags == 0;

    if (flagsFormat == 0 && (reqFlags != 0x0 || excFlags != 0x0)) {

      unsigned checkCompletion = 0x0;
      if (reqFlags == 0) {
        checkCompletion |= 0x1;
      }
      if (excFlags == 0) {
        checkCompletion |= 0x2;
      }

      for (const auto &subItem : containerB) { // check! nested for loop.
        if (item.text == subItem.text) {
          continue;
        }

        uint64_t subProp = subItem.prop;
        if ((subProp & 0xff) == reqFlags) {
          cond2 = (subProp >> 34) & 0x1;
          checkCompletion |= 0x1;
        }
        if ((subProp & 0xff) == excFlags) {
          cond4 = !((subProp >> 34) & 0x1);
          checkCompletion |= 0x2;
        }
        if (checkCompletion == 0x3) {
          break;
        }
      }
    }

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

    std::vector<Element> candidates;
    for (const auto &item : containerC) {
      bool isOnetime = (item.prop >> 33) & 0x1;
      bool isQuote = (item.prop >> 32) & 0x1;
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
  bool isVerbQuote = displayList[selectedIndex].prop & 0x100000000;
  if (isVerbQuote) {
    return;
  }
  unsigned verbId = displayList[selectedIndex].prop & 0xFF;
  unsigned verbChild = (displayList[selectedIndex].prop >> 16) & 0xFF;
  if (verbChild) {
    for (const auto &item : containerB) {
      if ((item.prop & 0xff) == verbChild) {
        if (item.text == "dummy") {
          verbId = item.prop & 0xff;
        } else {
          objectiveList.push_back(item);
          return;
        }
        break;
      }
    }
  }

  for (const auto &item : containerB) {
    uint64_t prop = item.prop;

    unsigned parent = (prop >> 8) & 0xFF;
    unsigned hasRead = (prop >> 34) & 0x1;
    unsigned reqFlags = (prop >> 48) & 0xFF;
    unsigned excFlags = (prop >> 56) & 0xFF;
    unsigned flagsFormat = (prop >> 35) & 0x1;

    bool cond1 = (parent == verbId);
    bool cond2 = ((gUnlocks & reqFlags) == reqFlags);
    bool cond3 = (hasRead == 0);
    bool cond4 = !((gUnlocks & excFlags) == excFlags) || excFlags == 0;

    if (flagsFormat == 0 && (reqFlags != 0x0 || excFlags != 0x0)) {

      unsigned checkCompletion = 0x0;
      if (reqFlags == 0) {
        checkCompletion |= 0x1;
      }
      if (excFlags == 0) {
        checkCompletion |= 0x2;
      }

      for (const auto &subItem : containerB) { // check! nested for loop.
        if (item.text == subItem.text) {
          continue;
        }

        uint64_t subProp = subItem.prop;
        if ((subProp & 0xff) == reqFlags) {
          cond2 = (subProp >> 34) & 0x1;
          checkCompletion |= 0x1;
        }
        if ((subProp & 0xff) == excFlags) {
          cond4 = !((subProp >> 34) & 0x1);
          checkCompletion |= 0x2;
        }
        if (checkCompletion == 0x3) {
          break;
        }
      }
    }

    if (cond1 && cond2 && cond3 && cond4) {
      objectiveList.push_back(item);
    }
  }
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
    rebuildObjectiveList();
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
    bool isQuote = (verb.prop >> 32) & 0x1;
    bool isOnRoot = ((verb.prop >> 8) & 0xff) == gRoot;
    if (!isQuote && isOnRoot) {
      return;
    }
    selected = verb;
  }

  uint64_t prop = selected.prop;

  unsigned id = prop & 0xFF;            // bit 0-7
  unsigned parent = (prop >> 8) & 0xFF; // bit 8-15
  unsigned child = (prop >> 16) & 0xff;
  unsigned newRoot = (prop >> 24) & 0xFF; // bit 24-31
  unsigned newScene = 0x0;
  bool isOnetime = (prop >> 33) & 0x1;    // bit 33
  unsigned unlocks = (prop >> 40) & 0xFF; // bit 40-47

  if (child) {
    for (const auto &item : containerB) {
      if ((item.prop & 0xff) == child) {
        if (item.text == "dummy") {
          gParent = item.prop & 0xff;
        }
      }
    }
  } else {
    gParent = id;
  }

  if (isOnetime) {
    for (auto &item : containerA) {
      if (item.text == selected.text && item.prop == selected.prop) {
        item.prop |= (1ULL << 34);
        break;
      }
    }
  }

  if ((selected.prop >> 56) == 0xff) {
    newScene = newRoot;
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
      gUnlocks = 0x0; // リセットする。
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

// 挿入する要素の親IDを保持する変数
unsigned gInsertParent = 0;

uint64_t buildProperty(unsigned id, unsigned parent, unsigned scene,
                       bool isQuote, bool isOnetime) {
  uint64_t prop = 0;
  prop |= (id & 0xFF);                      // bit 0-7: id
  prop |= ((uint64_t(parent) & 0xFF) << 8); // bit 8-15: parent
  if (isQuote)
    prop |= (1ULL << 32); // bit 32: isQuote
  if (isOnetime)
    prop |= (1ULL << 33);                   // bit 33: isOnetime
  prop |= ((uint64_t(scene) & 0x0F) << 36); // bit 36-39: scene
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

  gWindow = SDL_CreateWindow("DDMEditor Focus Window", 320, 240,
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

  std::string file_path = "../dd.txt";
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "エラー: " << file_path << " を開けませんでした。"
              << std::endl;
    return SDL_APP_FAILURE;
  }

  std::string token;
  std::string current_text;
  bool is_text = true;

  while (file >> token) {
    if (token.rfind("//", 0) == 0 || token.rfind("/*", 0) == 0 ||
        token.rfind("*", 0) == 0) {
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
    if (clean_token.empty())
      continue;

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

      if (key == SDLK_TAB) {
        gAppMode = MODE_INSERT_TEXT;
        gInputText.clear();

        // Objective段を選択中なら VerbのID を親に、そうでなければ現在の
        // gParent を親にする
        if (isObjectiveSelected && !displayList.empty()) {
          gInsertParent = displayList[selectedIndex].prop & 0xFF;
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
          if (!isObjectiveSelected) {
            isObjectiveSelected = true;
            selectedObjectiveIndex = 0;
            needRedraw = true;
          }
        } else if (key == SDLK_U) {
          // Objective選択中の場合、Verbの段に移動
          if (isObjectiveSelected) {
            isObjectiveSelected = false;
            needRedraw = true;
          }
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

  Uint64 frameTime = SDL_GetTicks() - frameStart;
  if (frameTime < TARGET_FRAME_MS) {
    SDL_Delay(TARGET_FRAME_MS - frameTime);
  }

  return SDL_APP_CONTINUE;
}

// SDL_AppQuit
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  std::ofstream ofs("../dd.txt");
  if (ofs.is_open()) {
    for (auto item : containerA) {
      if (item.text == "dummy") {
      } else {
        item.prop &= ~(1ULL << 34);
      }

      ofs << "'" << item.text << "', '0x" << std::hex << std::setw(16)
          << std::setfill('0') << item.prop << "',\n";
    }
    ofs.close();
  }

  if (gRenderer)
    SDL_DestroyRenderer(gRenderer);
  if (gWindow)
    SDL_DestroyWindow(gWindow);
}
