#include "console.h"
#include <iostream>
#include <string>
#include <SDL3/SDL.h>
#include "prop.h"
#include "document.h"
#include "state.h"
#include "guitext.h"
#include "placeholders.h"
#include "game.h"




void renderConsole() {
  std::cout << "\033[2J\033[1;1H";

  // 1. ステート情報
  std::cout << "=== [ State ] ===" << std::endl;
  std::cout << "gScene   : " << gScene << "\n"
            << "gRoot    : " << gRoot << "\n"
            << "gParent  : " << gParent << "\n"
            << "gUnlocks : 0x" << std::hex << gUnlocks << std::dec << "\n"
            << "gEyelevel: 0x" << std::hex << gEyelevel << std::dec << "\n"
            << "gTimers  : [0]:" << gTimer0 << " [1]:" << gTimer1
            << " [2]:" << gTimer2 << " [3]:" << gTimer3 << "\n\n";

  // 2. コンテナ内部状態のデバッグ表示
  std::cout << "=== [ Containers Status ] ===" << std::endl;
  std::cout << "Container A (Total Loaded) : " << containerA.size()
            << std::endl;
  std::cout << "Container B (Scene Matched): " << containerB.size()
            << std::endl;
  std::cout << "Container C (Cond Matched) : " << containerC.size()
            << std::endl;
  std::cout << "flavorContainer               : " << flavorContainer.size()
            << std::endl;
  std::cout << "DisplayList                : " << displayList.size()
            << std::endl;
  std::cout << "ObjectiveList              : " << objectList.size()
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
    unsigned verbId = getPropID(displayList[i].prop);
    unsigned verbChild = getPropChild(displayList[i].prop);

    bool hasObj = false;
    if (verbChild) {
      for (const auto &item : containerB) {
        if (getPropID(item.prop) == verbChild) {
          if (item.dialogue == "dummy") {
            hasObj = hasValidObjective(getPropID(item.prop));
          } else {
            hasObj = true;
          }
          break;
        }
      }
    } else {
      hasObj = hasValidObjective(verbId);
    }

    // 表示直前に %(変数名) をその時点の変数値へ展開する。格納 dialogue
    // は変更しない。
    std::string itemText = expandTextPlaceholders(displayList[i].dialogue);
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
  if (gRoot == gParent) {

    if (objectList.empty()) {
      if (isObjectiveSelected) {
        std::cout << "\033[31m*no content\033[0m" << std::endl;
      } else {
        std::cout << "\033[90mno content\033[0m" << std::endl;
      }
    } else {
      for (size_t j = 0; j < objectList.size(); ++j) {
        // 表示直前に %(変数名) をその時点の変数値へ展開する。格納 dialogue
        // は変更しない。
        std::string objText = expandTextPlaceholders(objectList[j].dialogue);
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
  }
  std::cout << "\n(Focus Window -> 'i': Left, 'o': Right, 'a': Select "
               "Objective, 'u': Select Verb, 'f': Send, 'q': Quit)"
            << std::endl;
  std::cout << std::flush;
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

