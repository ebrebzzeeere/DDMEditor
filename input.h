#pragma once
// SDL_AppEvent のキー・テキスト・マウス入力ハンドラ（優先順位・遮断条件は仕様。handleAppEvent が全体のディスパッチ）。
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>

SDL_AppResult handleAppEvent(SDL_Event *event);
