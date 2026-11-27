#pragma once
// レイアウト: layoutScene / layoutFamily（top-down forest）/ active layout 適用 / fit。
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL.h>

#include "state.h"
#include "graph.h"

// 現在 active な mode の world 座標。描画・pick・fit は必ず active layout を読む。
inline SDL_FPoint nodePos(const GraphNode &n) {
  if (gViewMode == VIEW_FAMILY) return SDL_FPoint{n.familyWx, n.familyWy};
  return SDL_FPoint{n.wx, n.wy};
}

int chainDepth(const Graph &g, int idx);
void layoutScene(uint8_t scene);
float familySubWidth(uint8_t scene, int u, std::unordered_set<int> &path,
                     std::unordered_map<int, float> &memo);
void familyPlace(uint8_t scene, int u, int depth, float baseX, float bandW,
                 std::unordered_set<int> &placed, std::unordered_map<int, float> &memo);
void layoutFamily(uint8_t scene);
void applyActiveLayout();
void windowSize(int *w, int *h);
void fitViewToScene();
