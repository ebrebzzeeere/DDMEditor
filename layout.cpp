#include "layout.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include "graph.h"
#include "state.h"
#include "guitext.h"




// parent chain を辿って column depth を決める。cycle / dangling は安全に打ち切る。
int chainDepth(const Graph &g, int idx) {
  std::unordered_set<int> seen;
  int depth = 0;
  int cur = idx;
  while (true) {
    if (cur < 0 || !seen.insert(cur).second || depth > 64) return depth;
    const GraphNode &n = g.nodes[cur];
    if (n.parentNodes.empty()) return depth;
    cur = n.parentNodes[0];
    ++depth;
  }
}

// scene 別のレイヤーレイアウト。column = parent chain depth、row = id 順。
void layoutScene(uint8_t scene) {
  const std::vector<int> inScene = gGraph.nodesInScene(scene);
  std::vector<std::pair<int, int>> depthIdx;
  depthIdx.reserve(inScene.size());
  for (int i : inScene) depthIdx.emplace_back(chainDepth(gGraph, i), i);

  std::sort(depthIdx.begin(), depthIdx.end(),
            [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
              if (a.first != b.first) return a.first < b.first;
              return gGraph.nodes[a.second].id < gGraph.nodes[b.second].id;
            });

  int prevDepth = -1;
  int row = 0;
  for (const std::pair<int, int> &di : depthIdx) {
    if (di.first != prevDepth) {
      prevDepth = di.first;
      row = 0;
    }
    GraphNode &n = gGraph.nodes[di.second];
    n.depth = di.first;
    n.wx = static_cast<float>(di.first) * static_cast<float>(kNodeW + kGapX);
    n.wy = static_cast<float>(row) * static_cast<float>(kNodeH + kGapY);
    ++row;
  }
}

// ============================================================================
// --- VIEW_FAMILY の per-mode layout: layoutFamily（top-down forest）。Prompt 2 ---
// ============================================================================
// 決定性: 同じ data.txt → 同じ座標。familyWx/familyWy/famDepth に書き、
// VIEW_GENERAL の wx/wy は書き換えない（mode 切替で general の座標は壊れない）。
// childAdj（parent field から作る階層 child）でツリーを展開し、親を上部・child を下部、
// 同 depth は横に並べる。childLinkAdj（slot1 の explicit child）はツリーに含めない
// （peach / yellow の overlay edge で別経路描画）。

// childAdj の階層 child を同 scene のノードに絞り id 昇順で返す（決定性のため）。
std::vector<int> familyChildren(uint8_t scene, int u) {
  std::vector<int> out;
  for (int c : gGraph.childAdj[u]) {
    if (c >= 0 && gGraph.nodes[c].scene == scene) out.push_back(c);
  }
  std::sort(out.begin(), out.end(),
            [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
  return out;
}

// subtree が占める band 幅。band(u) = max(pitch, Σ children band)。
// path で cycle を打ち切る（cycle の node は 1 node 分の band で安全に止める）。
float familySubWidth(uint8_t scene, int u, std::unordered_set<int> &path,
                     std::unordered_map<int, float> &memo) {
  const auto m = memo.find(u);
  if (m != memo.end()) return m->second;
  if (!path.insert(u).second) return static_cast<float>(kNodeW + kGapX);
  float band = 0.f;
  for (int c : familyChildren(scene, u)) band += familySubWidth(scene, c, path, memo);
  path.erase(u);
  const float w = std::max(static_cast<float>(kNodeW + kGapX), band);
  memo.emplace(u, w);
  return w;
}

// u を band の中央に置き、children を band に沿って左から右へ並べる。
// visited（placed）で再訪・cycle を打ち切る。chainDepth() と同じ depth 上限 64。
void familyPlace(uint8_t scene, int u, int depth, float baseX, float bandW,
                 std::unordered_set<int> &placed, std::unordered_map<int, float> &memo) {
  if (!placed.insert(u).second) return;
  GraphNode &n = gGraph.nodes[u];
  n.familyWx = baseX + (bandW - static_cast<float>(kNodeW)) * 0.5f;
  n.familyWy = static_cast<float>(depth) * static_cast<float>(kNodeH + kGapY);
  n.famDepth = depth;
  if (depth >= 64) return;
  float cursor = baseX;
  for (int c : familyChildren(scene, u)) {
    std::unordered_set<int> guard; // memo は root の band 計算で埋まっているので通常参照のみ
    const float cw = familySubWidth(scene, c, guard, memo);
    familyPlace(scene, c, depth + 1, cursor, cw, placed, memo);
    cursor += cw;
  }
}

void layoutFamily(uint8_t scene) {
  const std::vector<int> inScene = gGraph.nodesInScene(scene);
  std::unordered_set<int> placed;
  std::unordered_map<int, float> memo;
  float cursorX = 0.f;

  // pass 1: roots（parent == 0）と danglingParent（parent が同 scene に無い）を
  // root 扱いとして id 昇順で multi-root forest を横に並べる。
  std::vector<int> rootLike;
  for (int i : inScene) {
    const GraphNode &n = gGraph.nodes[i];
    if (n.parentId == 0 || n.danglingParent) rootLike.push_back(i);
  }
  std::sort(rootLike.begin(), rootLike.end(),
            [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
  for (int r : rootLike) {
    if (placed.count(r)) continue;
    std::unordered_set<int> guard;
    const float band = familySubWidth(scene, r, guard, memo);
    familyPlace(scene, r, 0, cursorX, band, placed, memo);
    cursorX += band;
  }

  // pass 2: どの root からも届かない残り（cycle の component / depth 上限越え）を
  // weakly-connected に group し、child 数の最大・同数なら id 最小の node を root にして
  // 1 tree ずつ横に足す（決定性・全 node を配置する）。
  std::unordered_set<int> remaining;
  for (int i : inScene) {
    if (!placed.count(i)) remaining.insert(i);
  }
  while (!remaining.empty()) {
    int seed = *remaining.begin();
    for (int i : remaining) {
      if (gGraph.nodes[i].id < gGraph.nodes[seed].id) seed = i;
    }
    remaining.erase(seed);
    std::vector<int> comp;
    std::vector<int> stack{seed};
    comp.push_back(seed);
    while (!stack.empty()) {
      const int u = stack.back();
      stack.pop_back();
      comp.push_back(u);
      for (int v : gGraph.parentAdj[u]) {
        if (v >= 0 && remaining.erase(v)) stack.push_back(v);
      }
      for (int v : gGraph.childAdj[u]) {
        if (v >= 0 && remaining.erase(v)) stack.push_back(v);
      }
    }
    int root = comp.front();
    for (int u : comp) {
      const size_t cu = gGraph.childAdj[u].size();
      const size_t ru = gGraph.childAdj[root].size();
      if (cu > ru || (cu == ru && gGraph.nodes[u].id < gGraph.nodes[root].id)) root = u;
    }
    std::unordered_set<int> guard;
    const float band = familySubWidth(scene, root, guard, memo);
    familyPlace(scene, root, 0, cursorX, band, placed, memo);
    cursorX += band;
  }
}


// active mode の layout 関数を適用する（switchScene / L / mode 切替で呼ぶ）。
// VIEW_GENERAL の layoutScene は wx/wy のだけを書き、VIEW_FAMILY の layoutFamily は
// familyWx/familyWy のだけ書くため、双方の座標は互いに壊れない。
void applyActiveLayout() {
  if (gViewMode == VIEW_FAMILY) layoutFamily(gViewScene);
  else layoutScene(gViewScene); // VIEW_GENERAL と、constraint / editor の既定 layout
}

void windowSize(int *w, int *h) {
  *w = 320;
  *h = 240;
  if (gWindow) SDL_GetWindowSize(gWindow, w, h);
  if (*w <= 0) *w = 320;
  if (*h <= 0) *h = 240;
}


void fitViewToScene() {
  int winW = 0, winH = 0;
  windowSize(&winW, &winH);

  const std::vector<int> inScene = gGraph.nodesInScene(gViewScene);
  if (inScene.empty()) {
    gViewZoom = 1.f;
    gViewPanX = static_cast<float>(kHudW) + 20.f;
    gViewPanY = 20.f;
    return;
  }

  float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
  bool first = true;
  for (int i : inScene) {
    const GraphNode &n = gGraph.nodes[i];
    const SDL_FPoint w = nodePos(n); // active layout（general / family）の座標で fit する
    const float x1 = w.x, y1 = w.y;
    const float x2 = w.x + static_cast<float>(kNodeW);
    const float y2 = w.y + static_cast<float>(kNodeH);
    if (first) {
      minX = x1; minY = y1; maxX = x2; maxY = y2; first = false;
    } else {
      minX = std::min(minX, x1);
      minY = std::min(minY, y1);
      maxX = std::max(maxX, x2);
      maxY = std::max(maxY, y2);
    }
  }

  const float availW = static_cast<float>(winW - kHudW) - 40.f;
  const float availH = static_cast<float>(winH) - 40.f;
  const float bw = std::max(1.f, maxX - minX);
  const float bh = std::max(1.f, maxY - minY);
  float zoom = std::min(availW / bw, availH / bh);
  gViewZoom = std::clamp(zoom, 0.2f, 1.2f);
  gViewPanX = static_cast<float>(kHudW) + 20.f - minX * gViewZoom;
  gViewPanY = 20.f - minY * gViewZoom;
}
