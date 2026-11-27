#pragma once
// %(変数名) プレースホルダ展開。状態値の参照のみ（格納された dialogue は変更しない）。
#include <string>

#include "state.h"

// --- %(変数名) プレースホルダの展開（表示時のみ） ---
// Element.dialogue に含まれる %(name) を正規表現 %\(([A-Za-z0-9]+)\) で検出し、
// name が対象変数名リストに含まれる場合のみ、その変数の現在値を 10 進文字列に
// 置換する。対象外の name の %(name) はリテラルとしてそのまま出力する
// （置換・削除しない）。1 つの dialogue に複数の %(…) がある場合はすべて処理する。
// 展開は renderConsole の出力直前でのみ行う。格納された dialogue
// （containerA / containerB / containerC / displayList / objectList）は
// 変更せず、SDL_AppQuit で data.txt に書き戻すテンプレートも元のまま残す。

// %(name) の name を対象グローバル変数へのポインタとして解決する。
// 対象は gTick, gScene, gRoot, gParent, gUnlocks, gEyelevel,
// gTimer0〜gTimer3（gTimers[0..3] への参照）のみ。該当しなければ nullptr。
inline const unsigned *resolvePlaceholderVar(const std::string &name) {
  if (name == "gTick")
    return &gTick;
  if (name == "gScene")
    return &gScene;
  if (name == "gRoot")
    return &gRoot;
  if (name == "gParent")
    return &gParent;
  if (name == "gUnlocks")
    return &gUnlocks;
  if (name == "gEyelevel")
    return &gEyelevel;
  if (name == "gTimer0")
    return &gTimer0;
  if (name == "gTimer1")
    return &gTimer1;
  if (name == "gTimer2")
    return &gTimer2;
  if (name == "gTimer3")
    return &gTimer3;
  if (name == "gTicketNumber")
    return &gTicketNumber;
  if (name == "gCallingNumber")
    return &gCallingNumber;

  return nullptr;
}

// dialogue 中の %(name) を、name が対象変数ならその現在の値の 10 進文字列に置換
// した新しい文字列を返す。対象外の %(name) とそれ以外のテキストはそのまま


std::string expandTextPlaceholders(const std::string &dialogue);
