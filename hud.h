#pragma once
// HUD・editor overlay・graph overlay・selected element の stdout 表示。表示項目・色・文言は仕様。
#include <string>

#include "state.h"
#include "view.h"
#include "placeholders.h"

void printSelectedElementDetails();
void drawEditorOverlay(SDL_Renderer *r);
void drawHud(SDL_Renderer *r);
void drawGraphOverlay(SDL_Renderer *r);
