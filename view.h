#pragma once
// viewport 変換・props 表示（describeElement）・graph/family/constraint 描画・視点操作。
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "state.h"
#include "graph.h"
#include "palette.h"
#include "layout.h"
#include "guitext.h"

inline SDL_FPoint worldToScreen(float x, float y) {
  return SDL_FPoint{x * gViewZoom + gViewPanX, y * gViewZoom + gViewPanY};
}

inline SDL_FPoint screenToWorld(float x, float y) {
  const float z = gViewZoom == 0.f ? 1.f : gViewZoom;
  return SDL_FPoint{(x - gViewPanX) / z, (y - gViewPanY) / z};
}

inline void setSolid(SDL_Renderer *r, uint8_t cr, uint8_t cg, uint8_t cb,
                     uint8_t ca = 255) {
  SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
}

inline void setSolid(SDL_Renderer *r, uint32_t hexColor, uint8_t ca = 255) {
  SDL_SetRenderDrawColor(r, mcR(hexColor), mcG(hexColor), mcB(hexColor), ca);
}

// 公式パレットのアクセントを base に 12% で混ぜた tinted surface（mocha風の淡い色面）。
// Prompt 6 色監査（要件 6）: 数値 RGB リテラルは kMC 定数定義のみ（それ以外 zero）。
// tint は kMC 定数からの調色計算（整数・round half up）で導出する。旧リテラル

inline const char *viewModeName() {
  switch (gViewMode) {
    case VIEW_FAMILY:
      return "VIEW_FAMILY";
    case VIEW_CONSTRAINT:
      return "VIEW_CONSTRAINT";
    case EDITOR:
      return "EDITOR";
    case VIEW_GENERAL:
    default:
      return "VIEW_GENERAL";
  }
}

// 画面の debug font は ASCII のみなので UTF-8 は '?' に置換して近似表示する。
// 全文は stdout の export / props dump で確認できる。

inline SDL_FPoint outputPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x + static_cast<float>(kNodeW),
                       w.y + static_cast<float>(kNodeH) * 0.5f);
}

inline SDL_FPoint inputPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x, w.y + static_cast<float>(kNodeH) * 0.5f);
}

// family（top-down）の port: 親の下端中央 / child の上端中央。active layout を読む。
inline SDL_FPoint familyBottomPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x + static_cast<float>(kNodeW) * 0.5f,
                       w.y + static_cast<float>(kNodeH));
}

inline SDL_FPoint familyTopPort(const GraphNode &n) {
  const SDL_FPoint w = nodePos(n);
  return worldToScreen(w.x + static_cast<float>(kNodeW) * 0.5f, w.y);
}

// blueprint 風の cubic bezier を polyline で近似して描く。

struct ConstraintEdgeCounts {
  size_t required = 0, requiredGhost = 0;
  size_t excludedSlot1 = 0, excludedSlot1Ghost = 0;
  size_t excludedSlot2 = 0, excludedSlot2Ghost = 0;
};


std::string asciiPreview(const std::string &src, size_t maxChars);
float drawGuiLine(SDL_Renderer *r, float x, float y, const std::string &text, float scale,
                  float budgetPx, uint32_t hexColor);
std::vector<std::string> describeElement(int nodeIndex, ViewMode view = VIEW_GENERAL);
void drawBezierEdge(SDL_Renderer *r, SDL_FPoint a, SDL_FPoint b);
void drawNodeBox(SDL_Renderer *r, const GraphNode &n, bool selected);
void drawGraphEdges(SDL_Renderer *r);
void drawFamilyTreeEdge(SDL_Renderer *r, SDL_FPoint a, SDL_FPoint b);
void drawFamilyView(SDL_Renderer *r);
ConstraintEdgeCounts countConstraintEdges(uint8_t scene);
void drawDashedRect(SDL_Renderer *r, const SDL_FRect &rect, float dashPx, float gapPx);
void drawConstraintView(SDL_Renderer *r);
int pickNode(float sx, float sy);
void zoomAt(float sx, float sy, float delta);
void switchScene(int dir);
void handlePointerEvent(const SDL_Event *ev);
