// buildGraph の定義側（Document を受け取る純関数）。型・inline helper は graph.h。
#include "graph.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include "prop.h"
#include "document.h"
#include "state.h"
#include "guitext.h"
#include "game.h"
#include "props.h"




void buildGraph(const Document &doc, Graph &g) {
  g = Graph{};
  bool sceneSeen[16] = {};

  for (size_t ei = 0; ei < doc.elements.size(); ++ei) {
    const ElementRecord &rec = doc.elements[ei];
    const uint64_t prop = rec.element.prop;
    const uint8_t scene = getPropScene(prop);
    const uint8_t id = getPropID(prop);

    if (g.findNode(scene, id) >= 0) {
      g.warnings.push_back("duplicate node scene=" + hexNibble(scene) + " id=" +
                           hexByte(id) + " line=" + std::to_string(rec.lineIndex + 1));
      continue;
    }

    GraphNode n;
    n.id = id;
    n.scene = scene;
    n.elementIndex = static_cast<int>(ei);
    n.lineIndex = rec.lineIndex;
    n.kind = classifyNodeKind(rec.element.dialogue);

    g.byKey[nodeKey(scene, id)] = static_cast<int>(g.nodes.size());
    g.nodes.push_back(std::move(n));
    if (!sceneSeen[scene]) {
      sceneSeen[scene] = true;
      g.scenes.push_back(scene);
    }
  }

  std::sort(g.scenes.begin(), g.scenes.end());

  g.parentAdj.resize(g.nodes.size());
  g.childAdj.resize(g.nodes.size());
  g.childLinkAdj.resize(g.nodes.size());
  g.requiredReadAdj.resize(g.nodes.size());
  g.excludedReadAdj.resize(g.nodes.size());

  for (size_t i = 0; i < g.nodes.size(); ++i) {
    GraphNode &n = g.nodes[i];
    const uint64_t prop = doc.elements[n.elementIndex].element.prop;

    n.parentId = getPropParent(prop);
    n.childId = getPropChild(prop);
    n.hasRead = getPropHasRead(prop);
    n.isQuote = getPropIsQuote(prop);
    n.isOnetime = getPropIsOnetime(prop);

    const uint8_t requiredReadId = getPropRequiredReadId(prop);
    const uint8_t excludedSlot1 = getPropExcludedReadId1(prop);
    const uint8_t excludedSlot2 = getPropExcludedReadId2(prop);
    const uint8_t newRoot = getPropNewRoot(prop);
    n.newRootId = newRoot;

    if (n.parentId == 0) g.roots.push_back(static_cast<int>(i));
    if (newRoot != 0) g.newRootTargets.push_back(nodeKey(n.scene, newRoot));

    if (n.parentId != 0) {
      const int p = g.findNode(n.scene, n.parentId);
      if (p >= 0) {
        n.parentNodes.push_back(p);
        g.parentAdj[i].push_back(p);
        g.childAdj[p].push_back(static_cast<int>(i));
        g.edges.push_back(
            GraphEdge{p, static_cast<int>(i), EDGE_PARENT, n.parentId, false});
      } else {
        n.danglingParent = true;
        g.edges.push_back(
            GraphEdge{-1, static_cast<int>(i), EDGE_PARENT, n.parentId, true});
      }
    }

    if (n.childId != 0) {
      const int c = g.findNode(n.scene, n.childId);
      if (c >= 0) {
        n.childNodes.push_back(c);
        g.childLinkAdj[i].push_back(c);
        g.edges.push_back(
            GraphEdge{static_cast<int>(i), c, EDGE_CHILD, n.childId, false});
      } else {
        n.danglingChild = true;
        g.edges.push_back(
            GraphEdge{static_cast<int>(i), -1, EDGE_CHILD, n.childId, true});
      }
    }

    if (requiredReadId != 0) {
      const int r = g.findNode(n.scene, requiredReadId);
      n.requiredReadNodes.push_back(r);
      g.requiredReadAdj[i].push_back(r);
      g.edges.push_back(GraphEdge{static_cast<int>(i), r, EDGE_REQUIRED_READ,
                                  requiredReadId, r < 0});
    }
    if (excludedSlot1 != 0) {
      const int x = g.findNode(n.scene, excludedSlot1);
      n.excludedReadNodes.push_back(x);
      g.excludedReadAdj[i].push_back(x);
      g.edges.push_back(GraphEdge{static_cast<int>(i), x, EDGE_EXCLUDED_READ_SLOT1,
                                  excludedSlot1, x < 0});
    }
    if (excludedSlot2 != 0) {
      const int x = g.findNode(n.scene, excludedSlot2);
      n.excludedReadNodes.push_back(x);
      g.excludedReadAdj[i].push_back(x);
      g.edges.push_back(GraphEdge{static_cast<int>(i), x, EDGE_EXCLUDED_READ_SLOT2,
                                  excludedSlot2, x < 0});
    }
  }
}
