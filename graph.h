#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "document.h"

// ============================================================================
// --- Graph model: (scene, id) を node、parent / child を edge ---
// ============================================================================
enum EdgeKind {
  EDGE_PARENT, // parent -> id
  EDGE_CHILD,  // id -> child
  EDGE_REQUIRED_READ,
  EDGE_EXCLUDED_READ_SLOT1,
  EDGE_EXCLUDED_READ_SLOT2
};

struct GraphEdge {
  int from = -1; // node index。-1 は graph 外（dangling）
  int to = -1;   // node index。-1 は同 scene に目標が無い
  EdgeKind kind = EDGE_PARENT;
  uint8_t targetId = 0;
  bool dangling = false;
};

enum NodeKind { NODE_CONTENT, NODE_DUMMY, NODE_TEMPLATE, NODE_EMPTY };

inline NodeKind classifyNodeKind(const std::string &dialogue) {
  if (dialogue == "dummy") return NODE_DUMMY;
  if (dialogue == "template") return NODE_TEMPLATE;
  if (dialogue == "empty") return NODE_EMPTY;
  return NODE_CONTENT;
}

inline uint32_t nodeKey(uint8_t scene, uint8_t id) {
  return (static_cast<uint32_t>(scene) << 8) | static_cast<uint32_t>(id);
}

struct GraphNode {
  uint8_t id = 0;
  uint8_t scene = 0;
  int elementIndex = -1;
  int lineIndex = -1;
  uint8_t parentId = 0;
  uint8_t childId = 0;
  bool hasRead = false;
  bool isQuote = false;
  bool isOnetime = false;
  NodeKind kind = NODE_CONTENT;

  // VIEW_GENERAL の layout world 座標
  int depth = 0;
  float wx = 0.f;
  float wy = 0.f;

  // VIEW_FAMILY の per-mode layout world 座標（top-down forest）。
  // layoutScene() は wx/wy のだけを書き、layoutFamily() は familyWx/familyWy に書くため、
  // mode 切替で VIEW_GENERAL の座標は壊れない。famDepth は -1 = 未配置、>= 0 = 配置済み。
  int famDepth = -1;
  float familyWx = 0.f;
  float familyWy = 0.f;

  // slot1 newRoot の target id（badge 表示用。0 は newRoot なし）
  uint8_t newRootId = 0;

  // family / constraint viewer 用（constraint は後フェーズ hook）
  std::vector<int> parentNodes;       // parent field が指す node
  std::vector<int> childNodes;        // slot1 child が指す node
  std::vector<int> requiredReadNodes; // slot0 requiredReadId の node
  std::vector<int> excludedReadNodes; // slot1 / slot2 excludedReadId の node
  bool danglingParent = false;
  bool danglingChild = false;
};

struct Graph {
  std::vector<GraphNode> nodes;
  std::vector<GraphEdge> edges;
  std::vector<int> roots;      // parent == 0 の node（root candidates）
  std::vector<uint8_t> scenes; // 存在する scene（昇順）
  std::unordered_map<uint32_t, int> byKey;

  // adjacency tables（VIEW_FAMILY / VIEW_CONSTRAINT のデータ構造。描画は後フェーズ）
  std::vector<std::vector<int>> parentAdj;       // node -> parents
  std::vector<std::vector<int>> childAdj;        // node -> 階層 children（parent field から）
  std::vector<std::vector<int>> childLinkAdj;    // node -> explicit child（slot1）
  std::vector<std::vector<int>> requiredReadAdj; // node -> requiredReadId targets
  std::vector<std::vector<int>> excludedReadAdj; // node -> excluded targets（slot1 + slot2）
  std::vector<uint32_t> newRootTargets;          // slot1 newRoot の (scene, id) key
  std::vector<std::string> warnings;

  int findNode(uint8_t scene, uint8_t id) const {
    const auto it = byKey.find(nodeKey(scene, id));
    return it == byKey.end() ? -1 : it->second;
  }

  std::vector<int> nodesInScene(uint8_t scene) const {
    std::vector<int> out;
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (nodes[i].scene == scene) out.push_back(static_cast<int>(i));
    }
    return out;
  }

  // デフォルト scene selector: 0 は newScene の「変化なし」sentinel なので
  // 0 を除いた最小 scene を選ぶ（data.txt では 0x1 になる）。
  uint8_t smallestNonZeroScene() const {
    for (const uint8_t s : scenes) {
      if (s != 0) return s;
    }
    return scenes.empty() ? 0 : scenes.front();
  }
};

void buildGraph(const Document &doc, Graph &g);
