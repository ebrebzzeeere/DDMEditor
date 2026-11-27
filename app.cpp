#include "app.h"
#include <fstream>
#include <iostream>
#include <SDL3/SDL.h>
#include "document.h"
#include "state.h"
#include "guitext.h"
#include "editor.h"
#include "props.h"





// Prompt 6・game モード共存（要件 7）: UI mode 切替。game へ離れるときは editor の add flow と
// props panel を閉じる（text input / GUI state の対称性・Document は変更しない）。
// runtime（console の入力・renderConsole）はそのまま動作する。viewer 復帰時は graph の
// mode・座標・選択はそのまま（applyActiveLayout は mode/scene 切替時の既存経路のまま触らない）。
void setUiMode(UiMode m) {
  if (m == gUiMode) return;
  if (m == UI_GAME) {
    editorLeaveModeIfNeeded(); // editor の add flow / text input を閉じる（取消・Document 不変）
    propsClose();              // props panel を閉じる（undo stack・選択は保持。active=false のときも安全）
  }
  gUiMode = m;
  std::cout << "ui mode switch: " << (m == UI_GAME ? "game（graph/HUD は描かない・renderConsole + 入力系統のみ）"
                                                   : "viewer（graph overlay + HUD・既定動作）") << std::endl;
  needRedraw = true;
}



// SDL_AppQuit の write policy / backup / legacy write-back / teardown（薄い SDL callback は main.cpp に残す）。
void appQuit() {
  // Prompt 6 write policy（要件 2）: quit の backupData は既定 off（B トグル）。on のときだけ
  // ../data.txt を ../data.txt.bak に上書きする。data.txt 自身には一切書き込まない
  // （write は main.cpp のみ・serialize は stdout / HUD export のまま）。
  if (gBackupOnQuit) {
    backupData("../data.txt", "../data.txt.bak");
  } else {
    std::cout << "backup on quit: off（既定・B トグルで on・data.txt は書き換えない）" << std::endl;
  }

  // write は行わない。round-trip は画面の export 欄と stdout で提示する。
  if (gExportOnQuit) {
    printDocumentToStdout(gDocument);
  }

  // 従来の書き戻しは、load元が ../data.txt の既存運用（build/ 直下から実行）でのみ
  // 維持する。./data.txt をloadした運用では data.txt を書き換えないため書かない。
  // 書き出しは新正格式（'0xHHHH-HHHH-HHHHHHHH'・2-2-4区切り（'-'））で行う。
  if (gLoadedPath == "../data.txt") {
    std::ofstream ofs("../data.txt");
    if (ofs.is_open()) {
      for (auto item : containerA) {
        if (item.dialogue != "dummy") {
          item.prop &= ~(1ULL << 16); // clear hasRead (bit 16)
        }

        ofs << "'" << item.dialogue << "', '0x" << formatPropHex16Piped(item.prop) << "',\n";
      }
      ofs.close();
    }
  }

  shutdownGuiText(); // glyph texture は renderer より先に破棄する

  if (gRenderer)
    SDL_DestroyRenderer(gRenderer);
  if (gWindow)
    SDL_DestroyWindow(gWindow);
}
