#include "selftest.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>
#include "prop.h"
#include "document.h"
#include "graph.h"
#include "state.h"
#include "palette.h"
#include "guitext.h"
#include "game.h"
#include "layout.h"
#include "view.h"
#include "editor.h"
#include "props.h"
#include "hud.h"
#include "app.h"





// SDL callback の SDL_AppEvent は main.cpp の定義（SDL_main.h は main.cpp のみで include。
// SDL_main 実装は複数 TU で重複定義になるためここでは宣言のみ使う）。
// main.cpp の SDL_main.h 経由の宣言は extern "C"（C linkage）なので、この宣言も
// extern "C" で一致させる。C++ linkage で宣言すると mangaled 名が未解決になり
// Final.a の C++ SDL callback 定義が引き込まれてリンクが壊れる。
extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);


static void phase_roundtrip_baseline() {
    // --- Prompt 6 round-trip baseline phase（要件 2・FAIL 検知に格上げ + 強化検証） ---
    // load 直後の baseline を保存し（Document::raw は load 時のまま）、編集/追加後の diff が
    // dirty/added 行のみであることを各 phase で検証する（RoundTripReport の added/edited）。
    // terminator（"\n" / "\r\n" / ""）保持・正格式（quote・'0x'・lowercase16・2-2-4区切り（'-'）・
    // trailing comma）・hasRead/dummy prop の load-time 保持をここで確認する。期待値はその場で合成する。
    std::cout << "=== [ selftest round-trip baseline ] ===" << std::endl;
    {
      const RoundTripReport rtBase = verifyRoundTrip(gDocument);
      gRoundTrip = rtBase;
      const bool rtBaseOk = stPass(rtBase.exact, "round-trip baseline");
      std::cout << "round_trip_baseline exact=" << (rtBaseOk ? 1 : 0) << " bytes=" << rtBase.originalBytes
                << " serialized=" << rtBase.serializedBytes << " lines=" << rtBase.lineCount
                << " baseline_lines=" << rtBase.baselineLines << std::endl;

      // RawLine.terminator の保持: text + terminator の連結で raw 全バイト列が復元されること
      // （CRLF・改行なし最終行を壊さない）。
      std::string rebuilt;
      rebuilt.reserve(gDocument.raw.size());
      size_t crlfLines = 0, unterminated = 0;
      for (const RawLine &rl : gDocument.lines) {
        rebuilt += rl.text + rl.terminator;
        if (rl.terminator == "\r\n") ++crlfLines;
        else if (rl.terminator.empty()) ++unterminated;
      }
      const bool termOk = stPass(rebuilt == gDocument.raw, "round-trip baseline");
      std::cout << "terminator_preserved match=" << (termOk ? 1 : 0) << " crlf_lines=" << crlfLines
                << " unterminated_lines=" << unterminated << std::endl;

      // 正格式の规则（再生成・追加行と同じ规则で検証する）: quote 形式・trailing comma・
      // '0x' prefix・lowercase 16 桁・2byte-2byte-4byte の '-' 区切り。合成→再パースの往復を
      // その場で合成した値で確認する（区切りあり・なしの両形式が同一値に復元されること）。
      const uint64_t fmtSample = 0x001000000010021dULL; // id=0x21 parent=0x10（他 bit 0 の合成例）
      const std::string fmtHex = formatPropHex16(fmtSample);
      const std::string fmtPiped = formatPropHex16Piped(fmtSample);
      const std::string fmtLine = "'fmt probe', '0x" + fmtPiped + "',";
      std::string fmtD;
      uint64_t fmtP = 0;
      std::string fmtDLegacy;
      uint64_t fmtPLegacy = 0;
      const bool fmtOk = stPass(fmtHex.size() == 16 && fmtHex == "001000000010021d" &&
                                 fmtPiped == "0010-0000-0010021d" &&
                                 parseElementLine(fmtLine, fmtD, fmtP) && fmtP == fmtSample &&
                                 fmtD == "fmt probe" &&
                                 parseElementLine("'fmt probe', '0x" + fmtHex + "',",
                                                  fmtDLegacy, fmtPLegacy) &&
                                 fmtPLegacy == fmtSample && fmtDLegacy == "fmt probe",
                                 "round-trip baseline");
      std::cout << "canonical_format pipe_split_2_2_4_0x_comma match=" << (fmtOk ? 1 : 0)
                << " sample='" << fmtLine << "'" << std::endl;

      // hasRead をクリアしない・dummy 行の prop も原値を保持: containerA は load-time prop のまま。
      bool propsKept = gDocument.elements.size() == containerA.size();
      size_t hasReadCount = 0, dummyNonZero = 0;
      for (size_t i = 0; i < containerA.size(); ++i) {
        if (i >= gDocument.elements.size() ||
            containerA[i].prop != gDocument.elements[i].element.prop ||
            containerA[i].dialogue != gDocument.elements[i].element.dialogue) propsKept = false;
      }
      for (const ElementRecord &rec : gDocument.elements) {
        if (getPropHasRead(rec.element.prop)) ++hasReadCount;
        if (rec.element.dialogue == "dummy" && rec.element.prop != 0) ++dummyNonZero;
      }
      const bool keepOk = stPass(propsKept, "round-trip baseline");
      std::cout << "load_time_props_preserved match=" << (keepOk ? 1 : 0) << " hasRead_in_doc=" << hasReadCount
                << " dummy_nonzero_prop=" << dummyNonZero << std::endl;
    }
}

static void phase_selection() {
    // view scene の先頭ノードの中心をクリックし、pickNode が正しい node を
    // 選ぶことを確認する。続けて zoom/pan を適用した後も同じ node が選ぶことを
    // world/screen 変換の往復として確認する。
    const std::vector<int> inScene = gGraph.nodesInScene(gViewScene);
    if (inScene.empty()) stFail("selection"); // 検証対象の node が無い場合は FAIL（Prompt 6・要件 8）
    if (!inScene.empty()) {
      const int want = inScene[0];
      const GraphNode &n = gGraph.nodes[want];
      const float cx = n.wx + static_cast<float>(kNodeW) * 0.5f;
      const float cy = n.wy + static_cast<float>(kNodeH) * 0.5f;

      SDL_Event ev{};
      ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      ev.button.button = SDL_BUTTON_LEFT;
      ev.button.down = true;
      const SDL_FPoint before = worldToScreen(cx, cy);
      ev.button.x = before.x;
      ev.button.y = before.y;
      handlePointerEvent(&ev);

      // Prompt 6: selection の flag を FAIL 検知に格上げ（要件 8）。
      const bool selMatch1 = stPass(gSelectedNode == want, "selection");
      std::cout << "=== [ selftest selection ] ===" << std::endl;
      std::cout << "want idx=" << want << " scene=" << hexNibble(n.scene)
                << " id=" << hexByte(n.id) << " picked=" << gSelectedNode
                << " match=" << (selMatch1 ? 1 : 0) << std::endl;

      zoomAt(320.f, 120.f, 1.f); // wheel zoom in
      gViewPanX += 40.f;         // pan
      gViewPanY -= 25.f;
      const SDL_FPoint after = worldToScreen(cx, cy);
      ev.button.x = after.x;
      ev.button.y = after.y;
      gSelectedNode = -1;
      handlePointerEvent(&ev);
      const bool selMatch2 = stPass(gSelectedNode == want, "selection");
      std::cout << "after zoom/pan picked=" << gSelectedNode
                << " match=" << (selMatch2 ? 1 : 0) << std::endl;
      printSelectedElementDetails();

    }

}

static void phase_family() {
    // --- family phase（Prompt 2）: mode 1<->2 の往復で VIEW_GENERAL の座標が壊れないこと・
    // layoutFamily の決定性・active layout の pickNode・forest 数の data.txt 実測照合・
    // round-trip EXACT の維持・kMC 実色のヒストグラム ---
    std::cout << "=== [ selftest family ] ===" << std::endl;
    {
      std::vector<std::pair<float, float>> genPos(gGraph.nodes.size());
      for (size_t i = 0; i < gGraph.nodes.size(); ++i)
        genPos[i] = {gGraph.nodes[i].wx, gGraph.nodes[i].wy};

      gSelectedNode = -1;
      gViewMode = VIEW_FAMILY;
      applyActiveLayout();
      fitViewToScene();

      // 決定性: 同じ scene を 2回 layoutFamily → familyWx/familyWy が同一。
      std::vector<std::pair<float, float>> famA(gGraph.nodes.size(), {0.f, 0.f});
      for (int i : gGraph.nodesInScene(gViewScene))
        famA[i] = {gGraph.nodes[i].familyWx, gGraph.nodes[i].familyWy};
      layoutFamily(gViewScene);
      bool deterministic = true;
      for (int i : gGraph.nodesInScene(gViewScene)) {
        if (gGraph.nodes[i].familyWx != famA[i].first ||
            gGraph.nodes[i].familyWy != famA[i].second) deterministic = false;
      }
      // Prompt 6: family の flag を FAIL 検知に格上げ（要件 8・既存の期待値実行時計算はそのまま）。
      const bool famDetOk = stPass(deterministic, "family layout");
      std::cout << "family_layout_deterministic scene=" << hexNibble(gViewScene) << "="
                << (famDetOk ? 1 : 0) << std::endl;

      // pickNode が active layout（family 座標）で正しい node を選ぶこと。
      const std::vector<int> famScene = gGraph.nodesInScene(gViewScene);
      if (famScene.empty()) stFail("family layout");
      if (!famScene.empty()) {
        const int want = famScene[0];
        const GraphNode &n = gGraph.nodes[want];
        const SDL_FPoint s = worldToScreen(n.familyWx + static_cast<float>(kNodeW) * 0.5f,
                                           n.familyWy + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = s.x;
        ev.button.y = s.y;
        gSelectedNode = -1;
        handlePointerEvent(&ev);
        const bool famPickOk = stPass(gSelectedNode == want, "family layout");
        std::cout << "family pick want idx=" << want << " id=" << hexByte(n.id)
                  << " picked=" << gSelectedNode << " match=" << (famPickOk ? 1 : 0) << std::endl;
      }

      // forest 数の照合（scene 0/1/2/15）。期待値は prop の直接パースから作る（graph 構造を通さない）:
      //   rootLike = parent == 0 または parent が同 scene に無い node。
      //   weakly-connected component ごとに max(1, rootLike数) を forest 数として足す
      //   （root が無い cycle component は 1 tree。layout 側も同じ規則で並べる）。
      bool famForestOk = true; // Prompt 6: forest/placed の照合を全 scene で集約して FAIL 検知する
      for (uint8_t probe : gGraph.scenes) {
        if (probe != 0 && probe != 1 && probe != 2 && probe != 15) continue;
        std::unordered_map<uint8_t, uint8_t> parentById;
        for (const ElementRecord &rec : gDocument.elements) {
          const uint64_t prop = rec.element.prop;
          if (getPropScene(prop) == probe) parentById[getPropID(prop)] = getPropParent(prop);
        }
        std::vector<uint8_t> ids;
        for (const auto &kv : parentById) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end());
        std::unordered_map<uint8_t, int> indexOf;
        for (size_t i = 0; i < ids.size(); ++i) indexOf[ids[i]] = static_cast<int>(i);

        std::vector<int> dsu(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) dsu[i] = static_cast<int>(i);
        auto findRoot = [&dsu](int a) {
          while (dsu[a] != a) a = dsu[a];
          return a;
        };
        for (uint8_t id : ids) {
          const uint8_t parent = parentById[id];
          if (parent == 0) continue;
          const auto pit = indexOf.find(parent);
          if (pit == indexOf.end()) continue; // dangling parent は component を作らない
          const int ra = findRoot(indexOf[id]);
          const int rb = findRoot(pit->second);
          if (ra != rb) dsu[ra] = rb;
        }
        std::unordered_set<int> compReps;
        std::unordered_map<int, size_t> rootLikeInComp;
        for (uint8_t id : ids) {
          const int rep = findRoot(indexOf[id]);
          compReps.insert(rep);
          const uint8_t parent = parentById[id];
          // rootLike = parent == 0 または parent が同 scene に無い node（layout の pass 1 root）
          if (parent == 0 || indexOf.find(parent) == indexOf.end()) ++rootLikeInComp[rep];
        }
        size_t expectedTrees = 0;
        for (int rep : compReps) expectedTrees += std::max<size_t>(1, rootLikeInComp[rep]);

        size_t graphP0 = 0, graphDangling = 0;
        for (int i : gGraph.nodesInScene(probe)) {
          if (gGraph.nodes[i].parentId == 0) ++graphP0;
          if (gGraph.nodes[i].danglingParent) ++graphDangling;
        }
        layoutFamily(probe); // family 座標のだけ書く。他 scene（active の general 座標）は壊れない
        size_t layoutForest = 0, placedCnt = 0;
        for (int i : gGraph.nodesInScene(probe)) {
          if (gGraph.nodes[i].famDepth == 0) ++layoutForest;
          if (gGraph.nodes[i].famDepth >= 0) ++placedCnt;
        }
        const bool famMatch = expectedTrees == layoutForest && placedCnt == ids.size();
        famForestOk &= famMatch;
        std::cout << "scene=" << hexNibble(probe) << " nodes=" << ids.size()
                  << " roots_p0=" << graphP0 << " dangling_parent=" << graphDangling
                  << " expected_forest=" << expectedTrees << " layout_forest=" << layoutForest
                  << " placed=" << placedCnt << "/" << ids.size() << " match="
                  << (famMatch ? 1 : 0) << std::endl;
      }
      std::cout << "family_forest_all_scenes match=" << (stPass(famForestOk, "family layout") ? 1 : 0) << std::endl;

      // Prompt 6・要件 4: 構造 edge の区別（parent field edge と slot1 child edge）が壊れていないこと。
      // present/dangling の数を prop 直接パースの期待値（first-wins・buildGraph と同じ扱い）で照合し、
      // 矛盾 edge（child target の parent field が child 以外を指す＝family の yellow overlay）も照合する。
      size_t expParAll = 0, expParD = 0, expChiAll = 0, expChiD = 0, expConflict = 0;
      for (uint8_t probe : gGraph.scenes) {
        std::unordered_map<uint8_t, uint64_t> propById;
        for (const ElementRecord &rec : gDocument.elements) {
          if (getPropScene(rec.element.prop) != probe) continue;
          const uint8_t idS = getPropID(rec.element.prop);
          if (propById.find(idS) != propById.end()) continue; // duplicate は graph 側でも skip
          propById[idS] = rec.element.prop;
        }
        for (const auto &kv : propById) {
          const uint8_t parS = getPropParent(kv.second);
          if (parS != 0) {
            if (propById.find(parS) != propById.end()) ++expParAll;
            else ++expParD;
          }
          const uint8_t chiS = getPropChild(kv.second);
          if (chiS != 0) {
            if (propById.find(chiS) != propById.end()) {
              ++expChiAll;
              if (getPropParent(propById[chiS]) != kv.first) ++expConflict; // 矛盾（drawFamilyView の yellow）
            } else ++expChiD;
          }
        }
      }
      size_t gotParP = 0, gotParD = 0, gotChiP = 0, gotChiD = 0, gotConflict = 0;
      for (const GraphEdge &e : gGraph.edges) {
        if (e.kind == EDGE_PARENT) {
          if (e.dangling || e.from < 0) ++gotParD;
          else ++gotParP;
        } else if (e.kind == EDGE_CHILD) {
          if (e.dangling || e.to < 0) ++gotChiD;
          else {
            ++gotChiP;
            if (gGraph.nodes[e.from].parentId != gGraph.nodes[e.to].id) ++gotConflict;
          }
        }
      }
      const bool structOk = stPass(expParAll == gotParP && expParD == gotParD &&
                                   expChiAll == gotChiP && expChiD == gotChiD &&
                                   expConflict == gotConflict, "family layout");
      std::cout << "structure_edges parent present=" << gotParP << "/" << expParAll
                << " dangling=" << gotParD << "/" << expParD << " child present=" << gotChiP << "/" << expChiAll
                << " dangling=" << gotChiD << "/" << expChiD << " conflict=" << gotConflict << "/" << expConflict
                << " separated match=" << (structOk ? 1 : 0) << std::endl;

      // family mode の実描画 → pixel readback（active layout で描かれた kMC 実色を確認する）。
      if (gRenderer) {
        setSolid(gRenderer, kMCBase); // window background: mocha base #1e1e2e
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer); // VIEW_FAMILY の階層 forest + HUD（選択ノードは lavender）
        SDL_RenderPresent(gRenderer);
        SDL_Surface *famShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (famShot) {
          SDL_SaveBMP(famShot, "build/viewer_frame_family.bmp");
          std::cout << "family frame captured " << famShot->w << "x" << famShot->h
                    << " -> build/viewer_frame_family.bmp" << std::endl;
          // readback は driver 依存なので RGB24 に変換してから読む（既存の教訓と同じ）。
          SDL_Surface *famRgba = SDL_ConvertSurface(famShot, SDL_PIXELFORMAT_RGB24);
          if (famRgba) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < famRgba->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(famRgba->pixels) +
                                   static_cast<size_t>(y) * famRgba->pitch;
              for (int x = 0; x < famRgba->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) |
                       (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
              }
            }
            std::cout << "family bright pixels=" << bright << std::endl;
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "family kmc colors: sapphire=" << kmcCount(kMCSapphire)
                      << " sky=" << kmcCount(kMCSky) << " text=" << kmcCount(kMCText)
                      << " lavender=" << kmcCount(kMCLavender) << " peach=" << kmcCount(kMCPeach)
                      << " yellow=" << kmcCount(kMCYellow) << std::endl;
            // Prompt 6: family frame の PASS を FAIL 検知に格上げ。階層 edge=sapphire・explicit
            // child edge=peach の区別（期待値は prop 直接パース・first-wins）、HUD の text/subtext1
            // 出現を必須とする。矛盾 yellow（family 意味）は info（excluded slot2 の yellow と別意味）。
            std::unordered_map<uint8_t, uint64_t> mapCapF;
            for (const ElementRecord &rec : gDocument.elements) {
              if (getPropScene(rec.element.prop) != gViewScene) continue;
              const uint8_t idF = getPropID(rec.element.prop);
              if (mapCapF.find(idF) != mapCapF.end()) continue;
              mapCapF[idF] = rec.element.prop;
            }
            size_t expParF = 0, expChiF = 0, expConfF = 0;
            for (const auto &kv : mapCapF) {
              const uint8_t parF = getPropParent(kv.second);
              if (parF != 0 && mapCapF.find(parF) != mapCapF.end()) ++expParF;
              const uint8_t chiF = getPropChild(kv.second);
              if (chiF != 0 && mapCapF.find(chiF) != mapCapF.end()) {
                // drawFamilyView の意味体系: 矛盾 edge（child target の parent が別ノード）は
                // peach ではなく yellow で overlay される（Prompt 2 実装・凡例）。peach 実色
                // 要求は矛盾なしの child edge があるときのみ。矛盾は yellow の info として照合。
                if (getPropParent(mapCapF[chiF]) == kv.first) ++expChiF;
                else ++expConfF;
              }
            }
            bool famFrameOk = bright > 0 && kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0;
            if (expParF > 0) famFrameOk &= kmcCount(kMCSapphire) > 0;
            if (expChiF > 0) famFrameOk &= kmcCount(kMCPeach) > 0;
            if (expConfF > 0) famFrameOk &= kmcCount(kMCYellow) > 0; // 矛盾 yellow overlay の実色
            famFrameOk = stPass(famFrameOk, "family layout");
            std::cout << "family frame PASS bright=" << bright << " hierarchy_sapphire=" << kmcCount(kMCSapphire)
                      << "/" << expParF << " child_peach=" << kmcCount(kMCPeach) << "/" << expChiF
                      << " conflict_yellow=" << kmcCount(kMCYellow) << "/" << expConfF
                      << " text=" << kmcCount(kMCText) << " subtext1=" << kmcCount(kMCSubtext1)
                      << " ok=" << (famFrameOk ? 1 : 0) << std::endl;
            std::vector<std::pair<uint32_t, uint32_t>> famTop(hist.begin(), hist.end());
            std::sort(famTop.begin(), famTop.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min<size_t>(8, famTop.size()); ++i) {
              std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                        << famTop[i].first << " count=" << std::dec << famTop[i].second
                        << std::endl;
            }
            SDL_DestroySurface(famRgba);
          }
          SDL_DestroySurface(famShot);
        } else {
          stFail("family layout"); // Prompt 6: frame capture が取れない場合は FAIL（要件 8）
          std::cout << "family frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }

      // general mode に戻って（1<->2 の往復）VIEW_GENERAL の座標が壊れていないこと。
      gViewMode = VIEW_GENERAL;
      gSelectedNode = -1;
      applyActiveLayout();
      fitViewToScene();
      bool intact = true;
      for (size_t i = 0; i < gGraph.nodes.size(); ++i) {
        if (gGraph.nodes[i].wx != genPos[i].first || gGraph.nodes[i].wy != genPos[i].second)
          intact = false;
      }
      std::cout << "general_coords_intact_after_mode_round_trip="
                << (stPass(intact, "family layout") ? 1 : 0) << std::endl;

      // family viewer は Document を mutate しない: round-trip は EXACT のまま。
      const RoundTripReport rtFam = verifyRoundTrip(gDocument);
      gRoundTrip = rtFam;
      const bool rtFamOk = stPass(rtFam.exact, "family layout");
      std::cout << "round_trip_after_family_phase="
                << (rtFamOk ? "EXACT OK" : "MISMATCH") << std::endl;
    }

}

static void phase_constraint() {
    // --- constraint phase（Prompt 3）: scene ごとの制約 edge / ghost の数を、現行 gDocument の
    // prop 直接パースの期待値と実行時照合する（family phase の pattern を踏襲し、数をハードコードしない）。
    // mode 1->2->3->1 の往復（1->2 を含む）で他 mode の座標・選択・fit が壊れないこと、
    // constraint（general active layout のまま）での pickNode が正しい node を選ぶこと、
    // 実描画を build/viewer_frame_constraint.bmp に保存し RGB24 ヒストグラムで実色を確認する。
    // PASS 条件: required edge が 1件以上あれば teal の出現必須 + HUD の text/subtext1 出現。
    // red/yellow は excluded edge の分だけ出現することを info として報告する（PASS 条件にしない）。
    // round-trip は往復後も EXACT のまま（Document を mutate しない）。 ---
    std::cout << "=== [ selftest constraint ] ===" << std::endl;
    {
      std::vector<std::pair<float, float>> genPosC(gGraph.nodes.size());
      for (size_t i = 0; i < gGraph.nodes.size(); ++i)
        genPosC[i] = {gGraph.nodes[i].wx, gGraph.nodes[i].wy};
      const float zoom0 = gViewZoom, panX0 = gViewPanX, panY0 = gViewPanY;
      const uint8_t scene0 = gViewScene;

      // general mode で先に node を選択しておく（往復での選択保持を確認する）。
      int wantSel = -1;
      uint32_t wantKey = 0;
      const std::vector<int> inSceneC = gGraph.nodesInScene(gViewScene);
      if (inSceneC.empty()) stFail("constraint edges");
      if (!inSceneC.empty()) {
        wantSel = inSceneC[0];
        const GraphNode &n = gGraph.nodes[wantSel];
        wantKey = nodeKey(n.scene, n.id);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        const SDL_FPoint c = worldToScreen(n.wx + static_cast<float>(kNodeW) * 0.5f,
                                           n.wy + static_cast<float>(kNodeH) * 0.5f);
        ev.button.x = c.x;
        ev.button.y = c.y;
        gSelectedNode = -1;
        handlePointerEvent(&ev);
        std::cout << "constraint base pick idx=" << wantSel << " match="
                  << (stPass(gSelectedNode == wantSel, "constraint edges") ? 1 : 0) << std::endl;
      }

      // scene ごとの edge kind / ghost の数。期待値は prop の直接パースから実行時に算出する
      // （duplicate node は first-wins で扱う。buildGraph と同じ扱い。graph 構造は通さない）。
      std::unordered_map<uint8_t, ConstraintEdgeCounts> expectedByScene;
      bool countsMatch = true, resolveOk = true;
      size_t totalReq = 0, totalEx = 0;
      for (uint8_t probe : gGraph.scenes) {
        std::unordered_map<uint8_t, uint64_t> propById;
        for (const ElementRecord &rec : gDocument.elements) {
          const uint8_t id = getPropID(rec.element.prop);
          if (getPropScene(rec.element.prop) != probe) continue;
          if (propById.find(id) != propById.end()) continue; // duplicate node は graph 側でも skip
          propById[id] = rec.element.prop;
        }
        ConstraintEdgeCounts exp;
        for (const auto &kv : propById) {
          const uint8_t r = getPropRequiredReadId(kv.second);
          if (r != 0) {
            ++exp.required;
            if (propById.find(r) == propById.end()) ++exp.requiredGhost;
          }
          const uint8_t x1 = getPropSlot1ExcludedReadId(kv.second);
          if (x1 != 0) {
            ++exp.excludedSlot1;
            if (propById.find(x1) == propById.end()) ++exp.excludedSlot1Ghost;
          }
          const uint8_t x2 = getPropSlot2ExcludedReadId(kv.second);
          if (x2 != 0) {
            ++exp.excludedSlot2;
            if (propById.find(x2) == propById.end()) ++exp.excludedSlot2Ghost;
          }
        }
        const ConstraintEdgeCounts got = countConstraintEdges(probe);
        // present の node idx 解決: to>=0 の edge の target は同 scene の node で
        // id == edge.targetId でなければならない。
        for (const GraphEdge &e : gGraph.edges) {
          if (e.kind != EDGE_REQUIRED_READ && e.kind != EDGE_EXCLUDED_READ_SLOT1 &&
              e.kind != EDGE_EXCLUDED_READ_SLOT2) continue;
          if (e.from < 0 || gGraph.nodes[e.from].scene != probe || e.to < 0) continue;
          if (gGraph.nodes[e.to].scene != probe || gGraph.nodes[e.to].id != e.targetId)
            resolveOk = false;
        }
        const bool match = exp.required == got.required && exp.requiredGhost == got.requiredGhost &&
                           exp.excludedSlot1 == got.excludedSlot1 &&
                           exp.excludedSlot1Ghost == got.excludedSlot1Ghost &&
                           exp.excludedSlot2 == got.excludedSlot2 &&
                           exp.excludedSlot2Ghost == got.excludedSlot2Ghost;
        if (!match) countsMatch = false;
        expectedByScene[probe] = got;
        totalReq += got.required;
        totalEx += got.excludedSlot1 + got.excludedSlot2;
        std::cout << "scene=" << hexNibble(probe)
                  << " required=" << got.required << "/" << exp.required
                  << " required_ghost=" << got.requiredGhost << "/" << exp.requiredGhost
                  << " excluded_slot1=" << got.excludedSlot1 << "/" << exp.excludedSlot1
                  << " excluded_slot1_ghost=" << got.excludedSlot1Ghost << "/" << exp.excludedSlot1Ghost
                  << " excluded_slot2=" << got.excludedSlot2 << "/" << exp.excludedSlot2
                  << " excluded_slot2_ghost=" << got.excludedSlot2Ghost << "/" << exp.excludedSlot2Ghost
                  << " match=" << (match ? 1 : 0) << std::endl;
      }
      // Prompt 6: constraint の kind/ghost 照合・present 解決を FAIL 検知に格上げ（要件 8）。
      std::cout << "constraint_edge_counts match=" << (stPass(countsMatch, "constraint edges") ? 1 : 0)
                << " present_resolve_ok=" << (stPass(resolveOk, "constraint edges") ? 1 : 0) << std::endl;
      std::cout << "constraint totals required=" << totalReq << " excluded=" << totalEx;
      if (totalEx == 0) std::cout << " excluded_edges=0 (none in data.txt)";
      std::cout << std::endl;

      // mode 1->2->3 の往復（general -> family -> constraint）。constraint の既定 active layout は
      // general なので、同じ scene の fit は general の fit と同じ値になる（fit が壊れないことを確認）。
      gViewMode = VIEW_FAMILY;
      applyActiveLayout();
      fitViewToScene();
      gViewMode = VIEW_CONSTRAINT;
      applyActiveLayout();
      fitViewToScene();
      // Prompt 6: 往復（1->2->3->1）の fit・座標・選択保持を FAIL 検知に格上げ（要件 5・8）。
      const bool fitSame = stPass(gViewZoom == zoom0 && gViewPanX == panX0 && gViewPanY == panY0,
                                  "constraint edges");
      std::cout << "constraint_fit_matches_general=" << (fitSame ? 1 : 0) << std::endl;
      bool coordsIntactMid = true;
      for (size_t i = 0; i < gGraph.nodes.size(); ++i) {
        if (gGraph.nodes[i].wx != genPosC[i].first || gGraph.nodes[i].wy != genPosC[i].second)
          coordsIntactMid = false;
      }
      std::cout << "general_coords_intact_through_family_constraint="
                << (stPass(coordsIntactMid, "constraint edges") ? 1 : 0) << std::endl;
      const bool selKept =
          wantSel < 0 ||
          (gSelectedNode == wantSel &&
           gGraph.nodes[wantSel].scene == static_cast<uint8_t>(wantKey >> 8) &&
           gGraph.nodes[wantSel].id == static_cast<uint8_t>(wantKey & 0xFFu));
      std::cout << "selection_kept_through_mode_round_trip="
                << (stPass(selKept, "constraint edges") ? 1 : 0) << std::endl;

      // constraint の active layout（general の座標そのまま）で pickNode が正しい node を選ぶこと。
      if (wantSel >= 0) {
        const GraphNode &n = gGraph.nodes[wantSel];
        const SDL_FPoint c = worldToScreen(n.wx + static_cast<float>(kNodeW) * 0.5f,
                                           n.wy + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        gSelectedNode = -1;
        handlePointerEvent(&ev);
        const bool conPickOk = stPass(gSelectedNode == wantSel, "constraint edges");
        std::cout << "constraint pick want idx=" << wantSel << " picked=" << gSelectedNode
                  << " match=" << (conPickOk ? 1 : 0) << std::endl;
        gSelectedNode = wantSel;
      }

      // general に戻る（1<->3 の往復完了。1<->2 を含む）。座標・選択が intact であること。
      gViewMode = VIEW_GENERAL;
      applyActiveLayout();
      fitViewToScene();
      bool intact = true;
      for (size_t i = 0; i < gGraph.nodes.size(); ++i) {
        if (gGraph.nodes[i].wx != genPosC[i].first || gGraph.nodes[i].wy != genPosC[i].second)
          intact = false;
      }
      const bool selRestored = wantSel < 0 || gSelectedNode == wantSel;
      std::cout << "general_coords_intact_after_constraint_round_trip="
                << (stPass(intact, "constraint edges") ? 1 : 0) << " selection_restored="
                << (stPass(selRestored, "constraint edges") ? 1 : 0) << std::endl;

      // constraint mode の実描画。capture scene は現行 scene に required edge が 0 件なら
      // required が 1件以上の最小 scene を選ぶ（teal が view に出るため）。required がどこにも
      // 無い場合は teal を PASS 条件にしない。
      uint8_t captureScene = scene0;
      const auto curIt = expectedByScene.find(captureScene);
      if (curIt != expectedByScene.end() && curIt->second.required == 0) {
        for (uint8_t s : gGraph.scenes) {
          const auto it = expectedByScene.find(s);
          if (it != expectedByScene.end() && it->second.required > 0) {
            captureScene = s;
            break;
          }
        }
      }
      const auto capIt = expectedByScene.find(captureScene);
      const size_t captureReq = capIt != expectedByScene.end() ? capIt->second.required : 0;
      gViewScene = captureScene;
      gViewMode = VIEW_CONSTRAINT;
      applyActiveLayout();
      fitViewToScene();
      // captured frame で describeElement の VIEW_CONSTRAINT variant の優先表示（present / ghost /
      // none の line）を実描画で確認するため、captureScene の先頭 present-required node を
      // 决定的に選択する（選択は capture 専用。round-trip の選択検証は上で完了済み）。
      gSelectedNode = -1;
      for (int i : gGraph.nodesInScene(captureScene)) {
        const GraphNode &m = gGraph.nodes[i];
        const uint8_t rid = getPropRequiredReadId(gDocument.elements[m.elementIndex].element.prop);
        if (rid == 0 || gGraph.findNode(captureScene, rid) < 0) continue;
        const SDL_FPoint c = worldToScreen(m.wx + static_cast<float>(kNodeW) * 0.5f,
                                           m.wy + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        handlePointerEvent(&ev);
        if (gSelectedNode == i) {
          std::cout << "constraint capture pick idx=" << i << " id=" << hexByte(m.id)
                    << " required target present=1（constraint variant を HUD に描画）" << std::endl;
          break;
        }
        gSelectedNode = -1;
      }
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer); // general layout の node/parent-child edge + constraint overlay + HUD legend
        SDL_RenderPresent(gRenderer);
        SDL_Surface *conShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (conShot) {
          SDL_SaveBMP(conShot, "build/viewer_frame_constraint.bmp");
          std::cout << "constraint frame captured scene=" << hexNibble(captureScene) << " "
                    << conShot->w << "x" << conShot->h << " -> build/viewer_frame_constraint.bmp"
                    << std::endl;
          // readback は driver 依存なので RGB24 に変換してから読む（既存の教訓と同じ）。
          SDL_Surface *conRgba = SDL_ConvertSurface(conShot, SDL_PIXELFORMAT_RGB24);
          if (conRgba) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < conRgba->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(conRgba->pixels) +
                                   static_cast<size_t>(y) * conRgba->pitch;
              for (int x = 0; x < conRgba->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) |
                       (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
              }
            }
            std::cout << "constraint bright pixels=" << bright << std::endl;
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "constraint kmc colors: teal=" << kmcCount(kMCTeal)
                      << " red=" << kmcCount(kMCRed) << " yellow=" << kmcCount(kMCYellow)
                      << " text=" << kmcCount(kMCText) << " subtext1=" << kmcCount(kMCSubtext1)
                      << std::endl;
            // PASS 条件: required edge が 1件以上あれば teal の出現必須 + HUD の text/subtext1 出現。
            // red/yellow は excluded edge の分だけ出現することを info として報告する（PASS 条件にしない）。
            // Prompt 6: constraint frame の PASS を FAIL 検知に格上げ（required≥1 なら teal 必須・
            // HUD の text/subtext1 必須）。red/yellow は excluded の分の出現のみ info（red の
            // 既存出現は quote ストライプ由来と混同しない — Prompt 3 の教訓）。
            const bool tealPass = stPass(captureReq == 0 || kmcCount(kMCTeal) > 0, "constraint edges");
            std::cout << "constraint_required_edges=" << captureReq << " teal_present="
                      << (kmcCount(kMCTeal) > 0 ? 1 : 0) << " teal_pass=" << (tealPass ? 1 : 0)
                      << std::endl;
            const bool conHudOk = stPass(kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0, "constraint edges");
            std::cout << "constraint hud text_present=" << (kmcCount(kMCText) > 0 ? 1 : 0)
                      << " subtext1_present=" << (kmcCount(kMCSubtext1) > 0 ? 1 : 0)
                      << " ok=" << (conHudOk ? 1 : 0) << std::endl;
            if (capIt != expectedByScene.end()) {
              std::cout << "constraint info: red/yellow appear by amount of excluded edges "
                        << "(scene=" << hexNibble(captureScene)
                        << " ex_slot1=" << capIt->second.excludedSlot1
                        << " ex_slot2=" << capIt->second.excludedSlot2 << ")" << std::endl;
            }
            std::vector<std::pair<uint32_t, uint32_t>> conTop(hist.begin(), hist.end());
            std::sort(conTop.begin(), conTop.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min<size_t>(8, conTop.size()); ++i) {
              std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                        << conTop[i].first << " count=" << std::dec << conTop[i].second
                        << std::endl;
            }
            SDL_DestroySurface(conRgba);
          }
          SDL_DestroySurface(conShot);
        } else {
          stFail("constraint edges"); // Prompt 6: frame capture が取れない場合は FAIL（要件 8）
          std::cout << "constraint frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }

      // 元の scene と general mode に戻す（以降の font phase / switchScene は影響を受けない）。
      gViewScene = scene0;
      gViewMode = VIEW_GENERAL;
      applyActiveLayout();
      fitViewToScene();

      // constraint viewer は Document を mutate しない: round-trip は往復後も EXACT のまま。
      const RoundTripReport rtCon = verifyRoundTrip(gDocument);
      gRoundTrip = rtCon;
      const bool rtConOk = stPass(rtCon.exact, "constraint edges");
      std::cout << "round_trip_after_constraint_phase="
                << (rtConOk ? "EXACT OK" : "MISMATCH") << std::endl;
    }

}

static void phase_editor() {
    // --- editor phase（Prompt 4）: EDITOR の Element 追加 GUI を検証する。
    // 期待値は実行時に gDocument の prop 直接パースから算出する（数をハードコードしない）。
    // nextFreeId の重複なし / 空 dialogue 拒否 / ESC 取消で Document が元に戻る /
    // 追加行の正格式（lowercase 16桁 + trailing comma）と再パース / child なし設定は
    // getPropChild()==0 / 追加後の graph・family・constraint の出現（countConstraintEdges と
    // 追加行の合成 prop を含む期待値で照合・match=1）/ baseline diff は追加行のみ /
    // runtime 同期は既定 off で container は変わらない / 検証後は baseline に戻す
    // （data.txt 自体は書き換えない）。 ---
    std::cout << "=== [ selftest editor ] ===" << std::endl;
    {
      const size_t baseLines = gDocument.lines.size();
      const size_t baseElements = gDocument.elements.size();
      const size_t baseBytes = gDocument.raw.size();
      const size_t baseNodes = gGraph.nodes.size();
      const size_t baseEdges = gGraph.edges.size();
      const size_t baseRoots = gGraph.roots.size();
      const size_t baseContA = containerA.size();
      const size_t baseContB = containerB.size();

      // scene 別の使用 id 集合と未使用の最小 id（>=1・>0xFF 不可）を prop 直接パースから算出する。
      const auto freeIdExpect = [&](uint8_t probe) {
        std::unordered_set<uint8_t> used;
        for (const ElementRecord &rec : gDocument.elements) {
          if (getPropScene(rec.element.prop) != probe) continue;
          used.insert(getPropID(rec.element.prop));
        }
        int freeId = -1;
        for (int id = 1; id <= 0xFF; ++id) {
          if (used.find(static_cast<uint8_t>(id)) == used.end()) { freeId = id; break; }
        }
        return std::make_pair(used, freeId);
      };
      bool nextFreeOk = true;
      for (uint8_t probe : gGraph.scenes) {
        const auto exp = freeIdExpect(probe);
        const int got = nextFreeId(probe);
        const bool unique = got < 0 || exp.first.find(static_cast<uint8_t>(got)) == exp.first.end();
        const bool ok = unique && got == exp.second;
        if (!ok) nextFreeOk = false;
        std::cout << "editor scene=" << hexNibble(probe) << " used_ids=" << exp.first.size()
                  << " expected_free=" << exp.second << " nextFreeId=" << got
                  << " no_collision=" << (unique ? 1 : 0) << " match=" << (ok ? 1 : 0) << std::endl;
      }
      std::cout << "next_free_id_no_duplicates=" << (nextFreeOk ? 1 : 0) << std::endl;

      // 制約 edge の数: family/constraint phase と同じ pattern で、現行 gDocument の prop
      // 直接パース（追加行の合成 prop を含む・duplicate は first-wins）の期待値と照合する。
      const auto verifyConstraintCounts = [&](const char *tag) {
        bool ok = true;
        for (uint8_t probe : gGraph.scenes) {
          std::unordered_map<uint8_t, uint64_t> propById;
          for (const ElementRecord &rec : gDocument.elements) {
            if (getPropScene(rec.element.prop) != probe) continue;
            const uint8_t id = getPropID(rec.element.prop);
            if (propById.find(id) != propById.end()) continue; // duplicate は graph 側でも skip
            propById[id] = rec.element.prop;
          }
          ConstraintEdgeCounts exp;
          for (const auto &kv : propById) {
            const uint8_t r = getPropRequiredReadId(kv.second);
            if (r != 0) { ++exp.required; if (propById.find(r) == propById.end()) ++exp.requiredGhost; }
            const uint8_t x1 = getPropSlot1ExcludedReadId(kv.second);
            if (x1 != 0) { ++exp.excludedSlot1; if (propById.find(x1) == propById.end()) ++exp.excludedSlot1Ghost; }
            const uint8_t x2 = getPropSlot2ExcludedReadId(kv.second);
            if (x2 != 0) { ++exp.excludedSlot2; if (propById.find(x2) == propById.end()) ++exp.excludedSlot2Ghost; }
          }
          const ConstraintEdgeCounts got = countConstraintEdges(probe);
          const bool m = exp.required == got.required && exp.requiredGhost == got.requiredGhost &&
                         exp.excludedSlot1 == got.excludedSlot1 &&
                         exp.excludedSlot1Ghost == got.excludedSlot1Ghost &&
                         exp.excludedSlot2 == got.excludedSlot2 &&
                         exp.excludedSlot2Ghost == got.excludedSlot2Ghost;
          if (!m) ok = false;
          std::cout << tag << " scene=" << hexNibble(probe)
                    << " required=" << got.required << "/" << exp.required
                    << " excluded=" << (got.excludedSlot1 + got.excludedSlot2) << "/"
                    << (exp.excludedSlot1 + exp.excludedSlot2) << " match=" << (m ? 1 : 0) << std::endl;
        }
        return ok;
      };

      auto postKey = [](SDL_Keycode k) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = k;
        ev.key.down = true;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postText = [](const char *t) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = t;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postClickNode = [](int idx) {
        const SDL_FPoint c = worldToScreen(
            nodePos(gGraph.nodes[idx]).x + static_cast<float>(kNodeW) * 0.5f,
            nodePos(gGraph.nodes[idx]).y + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        SDL_AppEvent(nullptr, &ev);
      };

      postKey(SDLK_4); // EDITOR mode（active layout は general のまま。新 layout は作らない）
      const bool modeOk = gViewMode == EDITOR;
      std::cout << "editor_mode_active=" << (modeOk ? 1 : 0) << std::endl;

      // ESC 取消: add state 中は取消優先（選択解除ではない）。Document は元に戻る。
      postKey(SDLK_RETURN); // add flow 開始（dialogue stage・text input Start）
      const bool started = gEditor.stage == EDITOR_DIALOGUE && gEditorTextInputActive;
      postText("取消テスト utf8 test");
      postKey(SDLK_ESCAPE);
      const bool cancelOk = gEditor.stage == EDITOR_IDLE && !gEditorTextInputActive &&
                            gDocument.lines.size() == baseLines &&
                            gDocument.elements.size() == baseElements &&
                            gDocument.raw.size() == baseBytes && gGraph.nodes.size() == baseNodes &&
                            serializeDocument(gDocument) == gDocument.raw;
      std::cout << "editor_start_ok=" << (started ? 1 : 0)
                << " editor_cancel_document_restores=" << (cancelOk ? 1 : 0) << std::endl;

      // 空 dialogue は不可（追加しない）。
      postKey(SDLK_RETURN);
      postKey(SDLK_RETURN); // 空のまま ENTER → dialogue stage に留まる
      const bool emptyRejected = gEditor.stage == EDITOR_DIALOGUE && gDocument.lines.size() == baseLines;
      std::cout << "editor_empty_dialogue_rejected=" << (emptyRejected ? 1 : 0) << std::endl;

      postKey(SDLK_ESCAPE); // 空の add flow を取消

      // 1件目の追加: dialogue → quote(n) → onetime(n) → parent(root none) → child(none) → commit。
      const uint8_t addScene = gViewScene;
      const auto exp1 = freeIdExpect(addScene);
      postKey(SDLK_RETURN); // add flow 開始
      const bool assignedOk = gEditor.assignedId == exp1.second && gEditorTextInputActive;
      const std::string dialogue1 = "selftest editor 追加テスト line テスト";
      postText(dialogue1.c_str()); // UTF-8・空白を含む入力をそのまま保持
      postKey(SDLK_RETURN); // dialogue 確定 → quote（text input は対称に Stop）
      const bool quoteStageOk = gEditor.stage == EDITOR_QUOTE && !gEditorTextInputActive;
      postKey(SDLK_N); // isQuote = no → onetime
      postKey(SDLK_N); // isOnetime = no → parent
      postKey(SDLK_RETURN); // parent 無選択（0 = root/no parent 許す）で commit 決定 → child
      postKey(SDLK_RETURN); // child none（slot1Index 0 / value 0 の data.txt 慣習）で commit
      const bool addedOne = gDocument.lines.size() == baseLines + 1 &&
                            gDocument.elements.size() == baseElements + 1;
      const RawLine &addedLine = gDocument.lines.back();
      std::string reDialogue;
      uint64_t reProp = 0;
      const bool reparsed = addedLine.kind == LINE_ELEMENT &&
                            parseElementLine(addedLine.text, reDialogue, reProp);
      const uint64_t expectedProp1 =
          composeEditorProp(static_cast<uint8_t>(exp1.second), 0, addScene, false, false, 0);
      const std::string expectedText1 = "'" + dialogue1 + "', '0x" + formatPropHex16Piped(expectedProp1) + "',";
      const bool formatOk = addedLine.text == expectedText1 && reparsed && reProp == expectedProp1 &&
                            reDialogue == dialogue1 && getPropParent(reProp) == 0 &&
                            getPropChild(reProp) == 0 && getPropID(reProp) == static_cast<uint8_t>(exp1.second) &&
                            getPropScene(reProp) == addScene && getSlotIndex(reProp, 1) == 0 &&
                            getSlotValue(reProp, 1) == 0;
      std::cout << "editor_add1 id=" << exp1.second << " scene=" << hexNibble(addScene)
                << " assigned_ok=" << (assignedOk ? 1 : 0) << " quote_stage_stop_text_ok=" << (quoteStageOk ? 1 : 0)
                << " added_line=" << (addedOne ? 1 : 0)
                << " format_exact_pipe_split_lower16_comma=" << (addedLine.text == expectedText1 ? 1 : 0)
                << " reparse_ok=" << (reparsed ? 1 : 0) << " prop_ok=" << (formatOk ? 1 : 0)
                << " child_none_getPropChild=0" << std::endl;

      const int newIdx = gGraph.findNode(addScene, static_cast<uint8_t>(exp1.second));
      const int rootDelta = static_cast<int>(gGraph.roots.size()) - static_cast<int>(baseRoots);
      const bool graphOk = gGraph.nodes.size() == baseNodes + 1 && newIdx >= 0 &&
                           gGraph.nodes[newIdx].elementIndex == static_cast<int>(gDocument.elements.size()) - 1 &&
                           rootDelta == 1 && gGraph.edges.size() == baseEdges; // 追加 prop は parent/child/constraint なし → edge 増 0
      std::cout << "editor_graph nodes_delta=" << static_cast<int>(gGraph.nodes.size()) - static_cast<int>(baseNodes)
                << " edges_delta=" << static_cast<int>(gGraph.edges.size()) - static_cast<int>(baseEdges)
                << " roots_delta=" << rootDelta << " new_node_idx=" << newIdx
                << " match=" << (graphOk ? 1 : 0) << std::endl;

      // family に新ノードが現れる（layoutFamily で全 node 配置・決定性は family phase 側で検証済み）。
      layoutFamily(addScene);
      const std::vector<int> famScene = gGraph.nodesInScene(addScene);
      size_t famPlaced = 0;
      for (int i : famScene) { if (gGraph.nodes[i].famDepth >= 0) ++famPlaced; }
      const bool familyOk = newIdx >= 0 && gGraph.nodes[newIdx].famDepth >= 0 && famPlaced == famScene.size();
      std::cout << "editor_new_node_in_family placed=" << famPlaced << "/" << famScene.size()
                << " match=" << (familyOk ? 1 : 0) << std::endl;

      // constraint: 追加行の合成 prop（required/excluded=0）を含む期待値と照合（match=1）。
      const bool constraintOk1 = verifyConstraintCounts("editor constraint after add1");
      std::cout << "constraint_edges_after_editor_add match=" << (constraintOk1 ? 1 : 0) << std::endl;

      // 追加直後の選択は新ノード（再構築で index は変わるが (scene,id) で決定的に解決される）。
      const bool selectionOk = gSelectedNode == newIdx && newIdx >= 0 &&
                               gGraph.nodes[gSelectedNode].scene == addScene &&
                               gGraph.nodes[gSelectedNode].id == static_cast<uint8_t>(exp1.second);
      std::cout << "editor_selection_new_node=" << (selectionOk ? 1 : 0) << std::endl;

      // baseline diff: 追加行のみ（edited=0・added_or_dirty_only=1・baselineLines は load 時の行数）。
      const RoundTripReport rtAdd1 = verifyRoundTrip(gDocument);
      gRoundTrip = rtAdd1;
      const bool rtOk1 = !rtAdd1.exact && rtAdd1.addedLines == 1 && rtAdd1.editedLines == 0 &&
                         rtAdd1.diffIsAddedOrDirtyOnly && rtAdd1.baselineLines == baseLines;
      std::cout << "editor_baseline_diff added=" << rtAdd1.addedLines << " edited=" << rtAdd1.editedLines
                << " added_or_dirty_only=" << (rtAdd1.diffIsAddedOrDirtyOnly ? 1 : 0)
                << " baseline_lines=" << rtAdd1.baselineLines << " document_lines=" << rtAdd1.lineCount
                << " bytes=" << rtAdd1.originalBytes << "→" << rtAdd1.serializedBytes
                << " firstDiff=" << rtAdd1.firstDiffLine << " match=" << (rtOk1 ? 1 : 0) << std::endl;

      // runtime 同期は既定 off。G トグルは on/off 往復しても container に触らない（commit 時のみ）。
      postKey(SDLK_G);
      postKey(SDLK_G);
      const bool runtimeOk = containerA.size() == baseContA && containerB.size() == baseContB && !gEditor.syncRuntime;
      std::cout << "editor_runtime_sync_default_off containers_unchanged=" << (runtimeOk ? 1 : 0) << std::endl;

      // 2件目の追加: parent / child をノードクリックで選ぶ（child あり: slot1Index 0 / value = child id）。
      // commit 後の stage は dialogue（text input Start 済み）。assignedId は 1件目を除いた nextFreeId。
      const auto exp2 = freeIdExpect(addScene);
      const bool id2Ok = gEditor.stage == EDITOR_DIALOGUE && gEditor.assignedId == exp2.second;
      const std::string dialogue2 = "parent-child test line";
      postText(dialogue2.c_str());
      postKey(SDLK_RETURN);
      postKey(SDLK_N);
      postKey(SDLK_N); // → parent stage
      std::vector<int> byId = gGraph.nodesInScene(addScene);
      std::sort(byId.begin(), byId.end(),
                [](int a, int b) { return gGraph.nodes[a].id < gGraph.nodes[b].id; });
      int parentPick = -1, childPick = -1;
      for (int i : byId) {
        if (gGraph.nodes[i].id == 0) continue;
        if (parentPick < 0) parentPick = i;
        else if (childPick < 0) childPick = i;
      }
      if (parentPick >= 0) postClickNode(parentPick);
      const uint8_t parentId = parentPick >= 0 ? gGraph.nodes[parentPick].id : 0;
      const bool parentSelOk = parentPick >= 0 && gEditor.parentId == parentId &&
                               parentId != static_cast<uint8_t>(exp2.second); // 自己 id 禁止（new id は未使用なので別 node）
      postKey(SDLK_RETURN); // parent 値で commit 決定 → child stage
      if (childPick >= 0) postClickNode(childPick);
      const uint8_t childId = childPick >= 0 ? gGraph.nodes[childPick].id : 0;
      const bool childSelOk = childPick >= 0 && gEditor.childOptionSelected &&
                             gEditor.childId == childId && childId != 0;
      postKey(SDLK_RETURN); // commit（child あり設定）
      const RawLine &addedLine2 = gDocument.lines.back();
      std::string reDialogue2;
      uint64_t reProp2 = 0;
      const bool reparsed2 = addedLine2.kind == LINE_ELEMENT &&
                             parseElementLine(addedLine2.text, reDialogue2, reProp2);
      const uint64_t expectedProp2 =
          composeEditorProp(static_cast<uint8_t>(exp2.second), parentId, addScene, false, false, childId);
      const bool prop2Ok = reparsed2 && reProp2 == expectedProp2 && reDialogue2 == dialogue2 &&
                           getPropParent(reProp2) == parentId && getPropChild(reProp2) == childId &&
                           getSlotIndex(reProp2, 1) == 0 && getSlotValue(reProp2, 1) == childId &&
                           addedLine2.text == "'" + dialogue2 + "', '0x" + formatPropHex16Piped(expectedProp2) + "',";
      const int newIdx2 = gGraph.findNode(addScene, static_cast<uint8_t>(exp2.second));
      bool childEdgeOk = false;
      if (newIdx2 >= 0 && childPick >= 0) {
        for (const GraphEdge &e : gGraph.edges) {
          if (e.kind == EDGE_CHILD && e.from == newIdx2 && e.to == childPick && e.targetId == childId) {
            childEdgeOk = true;
            break;
          }
        }
      }
      const bool childAdjOk = newIdx2 >= 0 && gGraph.childLinkAdj[newIdx2].size() == 1;
      const bool addedTwo = gDocument.lines.size() == baseLines + 2 &&
                            gDocument.elements.size() == baseElements + 2 &&
                            gGraph.nodes.size() == baseNodes + 2 &&
                            gGraph.roots.size() == baseRoots + 1; // add1 は parent=0 で root+1、add2 は parent!=0 で roots 増 0
      const RoundTripReport rtAdd2 = verifyRoundTrip(gDocument);
      gRoundTrip = rtAdd2;
      const bool rtOk2 = !rtAdd2.exact && rtAdd2.addedLines == 2 && rtAdd2.editedLines == 0 &&
                         rtAdd2.diffIsAddedOrDirtyOnly;
      const bool constraintOk2 = verifyConstraintCounts("editor constraint after add2");
      std::cout << "editor_add2 parent=" << hexByte(parentId) << " child=" << hexByte(childId)
                << " id2_ok=" << (id2Ok ? 1 : 0) << " parent_sel_ok=" << (parentSelOk ? 1 : 0)
                << " child_sel_ok=" << (childSelOk ? 1 : 0) << " prop_reparse_ok=" << (prop2Ok ? 1 : 0)
                << " child_edge=" << (childEdgeOk ? 1 : 0) << " child_adj=" << (childAdjOk ? 1 : 0)
                << " added_two=" << (addedTwo ? 1 : 0) << " baseline_diff_added_only=" << (rtOk2 ? 1 : 0)
                << " constraint_match=" << (constraintOk2 ? 1 : 0) << std::endl;

      // EDITOR の実描画（preview panel + HUD editor 凡例）→ build/viewer_frame_editor.bmp。
      // PASS 条件: HUD/panel の text と subtext1 の出現 + preview 進行色 sky（id/parent/child 値行）。
      // green/yellow は commit ready / warning の info（PASS 条件にしない）。
      bool frameOk = false;
      gSelectedNode = newIdx2;
      applyActiveLayout();
      fitViewToScene();
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer); // general layout の node/edge + drawEditorOverlay の preview + HUD
        SDL_RenderPresent(gRenderer);
        SDL_Surface *edShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (edShot) {
          SDL_SaveBMP(edShot, "build/viewer_frame_editor.bmp");
          std::cout << "editor frame captured scene=" << hexNibble(addScene) << " " << edShot->w << "x" << edShot->h
                    << " -> build/viewer_frame_editor.bmp" << std::endl;
          // readback は driver 依存なので RGB24 に変換してから読む（既存の教訓と同じ）。
          SDL_Surface *edRgba = SDL_ConvertSurface(edShot, SDL_PIXELFORMAT_RGB24);
          if (edRgba) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < edRgba->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(edRgba->pixels) +
                                   static_cast<size_t>(y) * edRgba->pitch;
              for (int x = 0; x < edRgba->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) |
                       (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
              }
            }
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "editor bright pixels=" << bright << std::endl;
            std::cout << "editor kmc colors: sky=" << kmcCount(kMCSky) << " text=" << kmcCount(kMCText)
                      << " subtext1=" << kmcCount(kMCSubtext1) << " subtext0=" << kmcCount(kMCSubtext0)
                      << " green=" << kmcCount(kMCGreen) << " yellow=" << kmcCount(kMCYellow)
                      << " lavender=" << kmcCount(kMCLavender) << std::endl;
            frameOk = kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0 && kmcCount(kMCSky) > 0;
            std::cout << "editor frame text/subtext1/sky present=" << (frameOk ? 1 : 0) << std::endl;
            std::vector<std::pair<uint32_t, uint32_t>> edTop(hist.begin(), hist.end());
            std::sort(edTop.begin(), edTop.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min<size_t>(8, edTop.size()); ++i) {
              std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                        << edTop[i].first << " count=" << std::dec << edTop[i].second << std::endl;
            }
            SDL_DestroySurface(edRgba);
          }
          SDL_DestroySurface(edShot);
        } else {
          std::cout << "editor frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }

      // 検証後に baseline に戻す（追加 2 行を remove・graph 再構築・round-trip EXACT 復元）。
      editorRestoreToBaseline(baseLines, baseElements);
      buildGraph(gDocument, gGraph);
      applyActiveLayout();
      fitViewToScene();
      gSelectedNode = -1;
      const RoundTripReport rtBack = verifyRoundTrip(gDocument);
      gRoundTrip = rtBack;
      const bool restoredOk = rtBack.exact && gDocument.raw.size() == baseBytes &&
                              gDocument.lines.size() == baseLines &&
                              gDocument.elements.size() == baseElements &&
                              gGraph.nodes.size() == baseNodes && gGraph.edges.size() == baseEdges &&
                              gGraph.roots.size() == baseRoots && !gDocTerminatorPatched &&
                              containerA.size() == baseContA && containerB.size() == baseContB &&
                              serializeDocument(gDocument) == gDocument.raw;
      std::cout << "editor_restore_baseline exact=" << (rtBack.exact ? 1 : 0) << " match=" << (restoredOk ? 1 : 0) << std::endl;

      // general に戻る（mode key 1 は add flow を取消し、text input は対称に閉じる）。
      postKey(SDLK_1);
      const bool exitOk = gViewMode == VIEW_GENERAL && gEditor.stage == EDITOR_IDLE && !gEditorTextInputActive;
      std::cout << "editor_mode_back_general_state_cleared=" << (exitOk ? 1 : 0) << std::endl;

      // Prompt 6・要件 5: editor の add state では runtime の console 挿入（TAB）が遮断され、
      // general 復帰後は TAB で挿入 flow が開き ESC で対称に閉じる（Start/Stop・console 入力
      // と GUI キーの衝突がないこと）。既存の遮断規則・cancelInsertMode をそのまま検証する。
      postKey(SDLK_4);
      postKey(SDLK_TAB); // EDITOR では editorMode ガードで insert flow に触らない
      const bool runtimeBlocked = gAppMode == MODE_NORMAL && gEditor.stage == EDITOR_IDLE;
      postKey(SDLK_1);
      postKey(SDLK_TAB);
      const bool insertStarted = gAppMode == MODE_INSERT_TEXT;
      postKey(SDLK_ESCAPE); // 挿入モードの ESC は cancelInsertMode（既存経路・text input Stop 対称）
      const bool insertCancelled = gAppMode == MODE_NORMAL && gEditor.stage == EDITOR_IDLE;
      std::cout << "editor_runtime_key_blocked=" << (runtimeBlocked ? 1 : 0) << " insert_flow_symmetry="
                << (insertStarted && insertCancelled ? 1 : 0) << std::endl;

      // Prompt 6: editor の全 flag を FAIL 検知に格上げ（要件 3・8）。期待値計算は Prompt 4 のまま。
      const bool editorAllOk = stPass(nextFreeOk && modeOk && started && cancelOk && emptyRejected &&
                                     assignedOk && quoteStageOk && addedOne && formatOk && graphOk && familyOk &&
                                     constraintOk1 && selectionOk && rtOk1 && runtimeOk && id2Ok && parentSelOk &&
                                     childSelOk && prop2Ok && childEdgeOk && childAdjOk && addedTwo && rtOk2 &&
                                     constraintOk2 && frameOk && restoredOk && exitOk && runtimeBlocked &&
                                     insertStarted && insertCancelled, "editor add");
      std::cout << "editor_phase_all_flags=" << (editorAllOk ? 1 : 0) << std::endl;
      std::cout << "round_trip_after_editor_phase=" << (rtBack.exact ? "EXACT OK" : "MISMATCH") << std::endl;
    }

}

static void phase_props() {
    // --- Prompt 5 props menu phase（全 prop fields の編集・slot options・validation・undo） ---
    // 期待値はすべて現行 gDocument の prop 直接パースから実行時に算出する（数をハードコードしない）。
    // 検証後は undo stack をすべて pop して baseline に戻る（data.txt 自体は書き換えない）。
    std::cout << "=== [ selftest props ] ===" << std::endl;
    {
      const size_t baseLines = gDocument.lines.size();
      const size_t baseElements = gDocument.elements.size();
      const size_t baseBytes = gDocument.raw.size();
      const size_t baseNodes = gGraph.nodes.size();
      const size_t baseEdges = gGraph.edges.size();
      const size_t baseRoots = gGraph.roots.size();
      const size_t baseContA = containerA.size();
      const size_t baseContB = containerB.size();
      std::vector<uint64_t> origProps;
      std::vector<bool> origDirty;
      std::vector<std::string> origText;
      for (const ElementRecord &rec : gDocument.elements) {
        origProps.push_back(rec.element.prop);
        origDirty.push_back(rec.dirty);
      }
      for (const RawLine &rl : gDocument.lines) origText.push_back(rl.text);

      auto postKey = [](SDL_Keycode k) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = k;
        ev.key.down = true;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postClickNode = [](int idx) {
        const SDL_FPoint c = worldToScreen(
            nodePos(gGraph.nodes[idx]).x + static_cast<float>(kNodeW) * 0.5f,
            nodePos(gGraph.nodes[idx]).y + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        SDL_AppEvent(nullptr, &ev);
      };
      auto postClickNodeRight = [](int idx) {
        const SDL_FPoint c = worldToScreen(
            nodePos(gGraph.nodes[idx]).x + static_cast<float>(kNodeW) * 0.5f,
            nodePos(gGraph.nodes[idx]).y + static_cast<float>(kNodeH) * 0.5f);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.button = SDL_BUTTON_RIGHT;
        ev.button.down = true;
        ev.button.x = c.x;
        ev.button.y = c.y;
        SDL_AppEvent(nullptr, &ev);
      };

      // 現行 elements から scene 別の propById を first-wins で作る（buildGraph と同じ扱い）。
      const auto sceneMap = [&](uint8_t probe) {
        std::unordered_map<uint8_t, uint64_t> m;
        for (const ElementRecord &rec : gDocument.elements) {
          if (getPropScene(rec.element.prop) != probe) continue;
          const uint8_t id = getPropID(rec.element.prop);
          if (m.find(id) == m.end()) m[id] = rec.element.prop;
        }
        return m;
      };
      // constraint kind/ghost 数を prop 直接パースの期待値と照合（editor/constraint phase と同じ pattern）。
      const auto verifyConstraintCounts = [&](const char *tag) {
        bool ok = true;
        for (uint8_t probe : gGraph.scenes) {
          const auto propById = sceneMap(probe);
          ConstraintEdgeCounts exp;
          for (const auto &kv : propById) {
            const uint8_t r = getPropRequiredReadId(kv.second);
            if (r != 0) { ++exp.required; if (propById.find(r) == propById.end()) ++exp.requiredGhost; }
            const uint8_t x1 = getPropSlot1ExcludedReadId(kv.second);
            if (x1 != 0) { ++exp.excludedSlot1; if (propById.find(x1) == propById.end()) ++exp.excludedSlot1Ghost; }
            const uint8_t x2 = getPropSlot2ExcludedReadId(kv.second);
            if (x2 != 0) { ++exp.excludedSlot2; if (propById.find(x2) == propById.end()) ++exp.excludedSlot2Ghost; }
          }
          const ConstraintEdgeCounts got = countConstraintEdges(probe);
          const bool m = exp.required == got.required && exp.requiredGhost == got.requiredGhost &&
                         exp.excludedSlot1 == got.excludedSlot1 &&
                         exp.excludedSlot1Ghost == got.excludedSlot1Ghost &&
                         exp.excludedSlot2 == got.excludedSlot2 &&
                         exp.excludedSlot2Ghost == got.excludedSlot2Ghost;
          if (!m) ok = false;
          std::cout << tag << " scene=" << hexNibble(probe)
                    << " required=" << got.required << "/" << exp.required
                    << " excluded=" << (got.excludedSlot1 + got.excludedSlot2) << "/"
                    << (exp.excludedSlot1 + exp.excludedSlot2) << " ghost="
                    << (got.requiredGhost + got.excludedSlot1Ghost + got.excludedSlot2Ghost) << "/"
                    << (exp.requiredGhost + exp.excludedSlot1Ghost + exp.excludedSlot2Ghost)
                    << " match=" << (m ? 1 : 0) << std::endl;
        }
        return ok;
      };

      // A) write helpers → read helpers の往復（合成 prop・全 fields・各 option 1回ずつ・逆配置厳密一致）。
      // cdOk は timerSetter/timerTarget/eyelevel の合成・分解が現行 helpers と一致するかの別判定（Prompt 5 検証項目）。
      bool rtOk = true;
      bool cdOk = true;
      {
        uint64_t p = 0;
        p = setPropField(p, 0, 8, 0x5A); p = setPropField(p, 8, 8, 0x2B); p = setPropField(p, 20, 4, 0x3);
        rtOk &= getPropID(p) == 0x5A && getPropParent(p) == 0x2B && getPropScene(p) == 0x3;
        p = setPropBit(p, 16, true); p = setPropBit(p, 17, true); p = setPropBit(p, 18, true);
        rtOk &= getPropHasRead(p) && getPropIsQuote(p) && getPropIsOnetime(p);
        p = setPropBit(p, 16, false); rtOk &= !getPropHasRead(p) && getPropIsQuote(p);
        rtOk &= clearPropBits(p, (0x3ULL << 24)) == (p & ~(0x3ULL << 24));
        // slot0: requiredReadId / unlocks（index 切替で value は共有、read は index に一致する option だけ返す）
        p = setSlotIndex(p, 0, 0); p = setSlotValue(p, 0, 0x77);
        rtOk &= getPropRequiredReadId(p) == 0x77 && getPropSlot0Unlocks(p) == 0;
        p = setSlotIndex(p, 0, 1);
        rtOk &= getPropSlot0Unlocks(p) == 0x77 && getPropRequiredReadId(p) == 0;
        // slot1: child / excluded / newRoot（排他・index は 1 つ）
        p = setSlotIndex(p, 1, 0); p = setSlotValue(p, 1, 0x12);
        rtOk &= getPropChild(p) == 0x12 && getPropSlot1ExcludedReadId(p) == 0 && getPropNewRoot(p) == 0;
        p = setSlotIndex(p, 1, 1);
        rtOk &= getPropSlot1ExcludedReadId(p) == 0x12 && getPropChild(p) == 0 && getPropNewRoot(p) == 0;
        p = setSlotIndex(p, 1, 2);
        rtOk &= getPropNewRoot(p) == 0x12 && getPropChild(p) == 0 && getPropSlot1ExcludedReadId(p) == 0;
        // slot1 index 3: downUnlocks（mask・他の option は 0・unlocks 側にも漏れない）
        p = setSlotIndex(p, 1, 3);
        rtOk &= getPropSlot1DownUnlocks(p) == 0x12 && getPropDownUnlocks(p) == 0x12 &&
                getPropNewRoot(p) == 0 && getPropChild(p) == 0 &&
                getPropSlot1ExcludedReadId(p) == 0;
        // slot2: timerSetter（vacant bits2-3 保持）/ unlocks / requiredUnlockFlags / excluded
        const uint64_t tvac = 0x0C; // value bits2-3 = 0b11（vacant・保持される）
        uint64_t tv = editTimerSetterValue(tvac, 2, 3); // timerId(bits0-1)=2, action(bits4-5)=3
        const bool tvCompose = (tv & 0x3) == 2 && ((tv >> 4) & 0x3) == 3 && (tv & 0x0C) == tvac; // value bit 配置（read の分解と一致）
        rtOk &= tvCompose;
        cdOk &= tvCompose;
        p = setSlotIndex(p, 2, 0); p = setSlotValue(p, 2, tv);
        rtOk &= getPropTimerSetter(p) == tv && hasPropTimerSetter(p);
        cdOk &= getPropTimerSetter(p) == tv && hasPropTimerSetter(p);
        uint64_t tv2 = editTimerSetterValue(getSlotValue(p, 2), 1, 2); // 部分編集で vacant 保持
        p = setSlotValue(p, 2, tv2);
        const bool tvDecode = getPropTimerSetterId(p) == 1 && getPropTimerSetterAction(p) == 2 &&
                              (getSlotValue(p, 2) & 0x0C) == tvac && getPropTimerSetter(p) == tv2;
        rtOk &= tvDecode;
        cdOk &= tvDecode;
        p = setSlotIndex(p, 2, 1);
        rtOk &= getPropSlot2Unlocks(p) == getSlotValue(p, 2) &&
                getPropUnlocks(p) == (getPropSlot0Unlocks(p) | getSlotValue(p, 2));
        p = setSlotIndex(p, 2, 2);
        rtOk &= getPropRequiredUnlockFlags(p) == getSlotValue(p, 2) && !hasPropTimerSetter(p);
        p = setSlotIndex(p, 2, 3);
        rtOk &= getPropSlot2ExcludedReadId(p) == getSlotValue(p, 2) && getPropExcludedReadId2(p) == getSlotValue(p, 2);
        // slot3: timerTarget / newScene / excludedUnlockFlags / eyelevelToggle
        const uint64_t tqv = editTimerTargetValue(0, 2, 3, 0x0A); // op=more, timerId=3, count=10
        // value byte 単体は prop の slot value bits と同じ配置（getProp* は prop bit を読むので、
        // 合成値そのものは raw bit で照合する。prop に立てた後の分解は下の往復で検証する）。
        const bool tqCompose = (tqv & 0x3) == 2 && ((tqv >> 2) & 0x3) == 3 && ((tqv >> 4) & 0xF) == 0x0A;
        rtOk &= tqCompose;
        cdOk &= tqCompose;
        p = setSlotIndex(p, 3, 0); p = setSlotValue(p, 3, tqv);
        const bool ttDecode = hasPropTimerTarget(p) && getPropTimerTarget(p) == tqv &&
                              getPropTimerTargetOp(p) == 2 && getPropTimerTargetId(p) == 3 &&
                              getPropTimerTargetCount(p) == 0x0A;
        rtOk &= ttDecode;
        cdOk &= ttDecode;
        const uint64_t tqv0 = editTimerTargetValue(tqv, 0, 3, 0x0A); // operator 0 = timerTarget なし（hasPropTimerTarget は op!=0 を前提）
        p = setSlotValue(p, 3, tqv0);
        const bool ttNone = !hasPropTimerTarget(p) && getPropTimerTargetOp(p) == 0 && getSlotValue(p, 3) == tqv0;
        rtOk &= ttNone;
        cdOk &= ttNone;
        p = setSlotIndex(p, 3, 1);
        rtOk &= getPropNewScene(p) == getSlotValue(p, 3) && !hasPropTimerTarget(p);
        p = setSlotIndex(p, 3, 2);
        rtOk &= getPropExcludedUnlockFlags(p) == getSlotValue(p, 3);
        const uint64_t ev = editEyelevelToggleValue(0, 0x0F, 0x0A); // eyelevel=15, toggle=10
        p = setSlotIndex(p, 3, 3); p = setSlotValue(p, 3, ev);
        const bool evDecode = getPropEyelevel(p) == 0x0F && getPropToggleEyelevel(p) == 0x0A && getSlotValue(p, 3) == ev;
        rtOk &= evDecode;
        cdOk &= evDecode;
        std::cout << "props helpers roundtrip prop=" << formatPropHex16(p) << std::endl;
      }
      std::cout << "props_helper_roundtrip_all_fields match=" << (rtOk ? 1 : 0) << std::endl;
      std::cout << "props timerSetter/timerTarget/eyelevel compose-decode match=" << (cdOk ? 1 : 0) << std::endl;

      // B) 対象ノードを選ぶ（view scene の content node・id>=1・id+1 未使用・parent!=id・slot 値 0 を優先）。
      const uint8_t testScene = gViewScene;
      const auto mapTest = sceneMap(testScene);
      int targetIdx = -1;
      uint64_t targetProp0 = 0;
      for (int i : gGraph.nodesInScene(testScene)) {
        const GraphNode &n = gGraph.nodes[i];
        if (n.kind != NODE_CONTENT) continue;
        if (n.id < 1 || n.id >= 0xFF) continue;
        if (mapTest.find(static_cast<uint8_t>(n.id + 1)) != mapTest.end()) continue;
        const uint64_t prop = gDocument.elements[n.elementIndex].element.prop;
        if (getPropParent(prop) == n.id) continue; // self-parent を作らない
        bool slotsClean = true;
        for (int slot = 0; slot < 4; ++slot) {
          if (getSlotIndex(prop, static_cast<uint8_t>(slot)) != 0 || getSlotValue(prop, static_cast<uint8_t>(slot)) != 0) slotsClean = false;
        }
        if (!slotsClean) continue;
        targetIdx = i;
        targetProp0 = prop;
        break;
      }
      bool targetFound = targetIdx >= 0;
      if (!targetFound) {
        for (int i : gGraph.nodesInScene(testScene)) {
          const GraphNode &n = gGraph.nodes[i];
          if (n.kind != NODE_CONTENT) continue;
          if (n.id < 1 || n.id >= 0xFF) continue;
          if (mapTest.find(static_cast<uint8_t>(n.id + 1)) != mapTest.end()) continue;
          const uint64_t prop = gDocument.elements[n.elementIndex].element.prop;
          if (getPropParent(prop) == n.id) continue;
          targetIdx = i;
          targetProp0 = prop;
          break;
        }
        targetFound = targetIdx >= 0;
      }
      std::cout << "props target scene=" << hexNibble(testScene) << " id="
                << (targetFound ? hexByte(static_cast<uint8_t>(gGraph.nodes[targetIdx].id)) : std::string("none"))
                << " prop0=0x" << formatPropHex16(targetProp0) << " found=" << (targetFound ? 1 : 0) << std::endl;

      postKey(SDLK_4); // EDITOR mode（props は EDITOR・idle のときのみ受理）
      const bool modeOk = gViewMode == EDITOR && gEditor.stage == EDITOR_IDLE && !gProps.active;

      // ノード選択（クリック）で props panel を開く。right-click でも開ける（optional マウス）。
      gSelectedNode = -1;
      postClickNode(targetIdx);
      const uint8_t targetId0 = targetIdx >= 0 ? gGraph.nodes[targetIdx].id : 0;
      const bool openedByClick = gProps.active && gSelectedNode == targetIdx &&
                                 gProps.nodeKey == nodeKey(testScene, targetId0);
      gProps.warningTotal = 0; // propsOpen は reset するが、念のため基準を明示（期待値照合の起点）
      size_t warnMark = gProps.warningTotal;
      const auto warnDeltaOk = [&](size_t expected) {
        const size_t got = gProps.warningTotal - warnMark;
        warnMark = gProps.warningTotal;
        return got == expected;
      };
      postClickNodeRight(targetIdx); // right-click も panel を開く（同一ノードの再オープン）
      const bool openedByRight = gProps.active && gProps.nodeKey == nodeKey(testScene, targetId0);

      // describeElement の EDITOR variant（props 表示順序）: 現行 prop から期待する項目文字列をその場で作り照合する。
      bool variantOk = true;
      {
        const auto lines = describeElement(gSelectedNode, EDITOR);
        const uint64_t prop = propsCurrentProp();
        const auto has = [&](const std::string &needle) {
          for (const std::string &l : lines) if (l.find(needle) != std::string::npos) return true;
          return false;
        };
        variantOk = has("prop=0x" + formatPropHex16(prop)) &&
                    has("section=basic flags") && has("flags_bin=" + binaryByte(static_cast<uint8_t>((prop >> 16) & 0xFu))) &&
                    has("section=id/parent/scene id=" + hexDec(getPropID(prop))) &&
                    has("parent=" + hexDec(getPropParent(prop))) && has("scene=" + hexDec(getPropScene(prop))) &&
                    has("section=slot0 option=" + std::string(propsOptionLabel(prop, 0))) &&
                    has("section=slot1 option=" + std::string(propsOptionLabel(prop, 1))) &&
                    has("section=slot2 option=" + std::string(propsOptionLabel(prop, 2))) &&
                    has("section=slot3 option=" + std::string(propsOptionLabel(prop, 3))) &&
                    lines.size() >= 10;
        // 他 view の出力順は壊さない（general variant は snapshot の先頭形式のまま・field 全文字列を保持）。
        const auto generalLines = describeElement(gSelectedNode, VIEW_GENERAL);
        variantOk &= generalLines.size() >= 8 &&
                    generalLines[0].find("node scene=") != std::string::npos &&
                    generalLines[1].find("prop=0x") != std::string::npos &&
                    generalLines[4].find("slot0 index=") != std::string::npos; // general の slot dump 順（既存: node/prop/id/flags→slot0..3）
        std::cout << "props variant lines=" << lines.size() << " general lines=" << generalLines.size() << std::endl;
      }
      std::cout << "props_open click=" << (openedByClick ? 1 : 0) << " right=" << (openedByRight ? 1 : 0)
                << " mode_active=" << (modeOk ? 1 : 0) << " variant_ok=" << (variantOk ? 1 : 0) << std::endl;

      // C) キー経路: TAB=次項目, ENTER=トグル, U=undo, Y/N=bool（panel open のとき優先・衝突しない）
      const uint64_t propBefore = propsCurrentProp();
      postKey(SDLK_TAB);
      const bool tabOk = gProps.cursor == 1; // isQuote フィールド
      postKey(SDLK_RETURN);
      const bool enterToggleOk = getPropIsQuote(propsCurrentProp()) != getPropIsQuote(propBefore);
      // 編集した RawLine が再パースでき、prop が同一であること（ ENTER トグル直後の行）。
      bool reparseOk = true;
      {
        const int idxNow = propsResolveCurrentNode();
        const RawLine &rl = gDocument.lines[gGraph.nodes[idxNow].lineIndex];
        std::string reD; uint64_t reP = 0;
        reparseOk = rl.kind == LINE_ELEMENT && parseElementLine(rl.text, reD, reP) && reP == propsCurrentProp() &&
                    rl.text == "'" + gDocument.elements[gGraph.nodes[idxNow].elementIndex].element.dialogue +
                               "', '0x" + formatPropHex16Piped(propsCurrentProp()) + "',";
      }
      postKey(SDLK_U); // undo（旧 prop を完全復元・line text/dirty も戻る）
      const bool undoRestoreOk = propsCurrentProp() == propBefore && gProps.undoStack.empty();
      gProps.cursor = 0; // hasRead フィールド（Y/N は current field の bool を true/false にする・key 経路）
      postKey(SDLK_Y);
      const bool yOk = getPropHasRead(propsCurrentProp()) == true;
      postKey(SDLK_N);
      const bool nOk = getPropHasRead(propsCurrentProp()) == false;
      // 循環: TAB を fieldCount 回で cursor が 0 に戻る（決定性）。
      for (int i = 0; i < kPropsFieldCount; ++i) postKey(SDLK_TAB);
      const bool tabWrapOk = gProps.cursor == 0;
      std::cout << "props key flow tab=" << (tabOk ? 1 : 0) << " enter_toggle=" << (enterToggleOk ? 1 : 0)
                << " reparse=" << (reparseOk ? 1 : 0) << " undo_restore=" << (undoRestoreOk ? 1 : 0)
                << " y=" << (yOk ? 1 : 0) << " n=" << (nOk ? 1 : 0) << " tab_wrap=" << (tabWrapOk ? 1 : 0) << std::endl;
      // Y/N の編集は undo entry を 2 段積む。undo は U で復元できることを最後に全 undo で確認するが、
      // ここでは 1 段目の復元（hasRead=false の元値）を確認する。
      postKey(SDLK_U);
      postKey(SDLK_U);
      const bool ynUndoOk = propsCurrentProp() == propBefore && gProps.undoStack.empty();
      std::cout << "props yn_undo_restore=" << (ynUndoOk ? 1 : 0) << std::endl;
      warnMark = gProps.warningTotal; // キー編集の Y/N・ENTER は warning を作らない（期待 0）
      bool keyNoWarnOk = warnDeltaOk(0);

      // D) programmatic edits（決定的・期待値は prop 直接パースから実行時に算出）。
      const int fId = 4, fParent = 5, fScene = 6;
      const int fSlot0Opt = 7, fSlot0Val = 8, fSlot1Opt = 9, fSlot1Val = 10;
      const int fSlot2Opt = 11, fSlot2Val = 12, fSlot2TimerId = 13, fSlot2Action = 14;
      const int fSlot3Opt = 15, fSlot3Val = 16, fSlot3Op = 17, fSlot3TimerId = 18, fSlot3Count = 19;
      const int fEye = 20, fEyeToggle = 21;
      const auto curScene = [&]() { return static_cast<uint8_t>(gProps.nodeKey >> 8); };
      const auto curId = [&]() { return static_cast<uint8_t>(gProps.nodeKey & 0xFFu); };
      // 現 scene で未使用の最小 id（!=0）をその場で算出する（cross-scene target の期待値用）。
      const auto absentNow = [&]() {
        const auto m = sceneMap(curScene());
        for (int cand = 1; cand <= 0xFF; ++cand) {
          if (m.find(static_cast<uint8_t>(cand)) == m.end()) return static_cast<uint8_t>(cand);
        }
        return static_cast<uint8_t>(0);
      };
      bool editsOk = true;
      size_t expectedTotal = 0; // validation warning の期待値総数（各 op の期待値をその場で加算）

      // O1: id → id+1（未使用・duplicate なし → warning 0）。正格式・再パース・baseline diff（edited のみ）。
      const uint8_t nextId = static_cast<uint8_t>(targetId0 + 1);
      bool o1 = propsApplyFieldValue(fId, nextId, true);
      o1 &= getPropID(propsCurrentProp()) == nextId && warnDeltaOk(0);
      const RoundTripReport rtEdit1 = verifyRoundTrip(gDocument);
      gRoundTrip = rtEdit1;
      const bool rtEditedOk = !rtEdit1.exact && rtEdit1.editedLines == 1 && rtEdit1.addedLines == 0 &&
                              rtEdit1.diffIsAddedOrDirtyOnly && rtEdit1.baselineLines == baseLines;
      std::cout << "props O1 id->" << hexByte(nextId) << " applied=" << (o1 ? 1 : 0)
                << " baseline_edited_only=" << (rtEditedOk ? 1 : 0) << std::endl;
      editsOk &= o1 && rtEditedOk;

      // O2: id absolute 0x100 → clamp warning（範囲外は clamp + warning）。duplicate は現行 map から算出。
      const auto mapAfterId = sceneMap(testScene);
      bool dup0xFF = false;
      if (mapAfterId.find(0xFF) != mapAfterId.end()) {
        const int hit = gGraph.findNode(testScene, 0xFF);
        dup0xFF = hit >= 0 && gGraph.nodes[hit].elementIndex != gGraph.nodes[propsResolveCurrentNode()].elementIndex;
      }
      const size_t expected2 = 1 + (dup0xFF ? 1 : 0);
      propsApplyFieldValue(fId, 0x100, true);
      const uint8_t afterId = getPropID(propsCurrentProp());
      bool o2 = warnDeltaOk(expected2) && afterId == (dup0xFF ? nextId : 0xFF);
      expectedTotal += expected2;
      std::cout << "props O2 clamp id want=" << hexByte(0xFF) << " got=" << hexByte(afterId)
                << " dup_reject=" << (dup0xFF ? 1 : 0) << " warn expected=" << expected2
                << " match=" << (o2 ? 1 : 0) << std::endl;
      editsOk &= o2;

      // O3/O4: parent → 現 scene の実在 id（warning 0）→ 現 scene に無い id（warning・ghost/par! 表示）。
      const auto mapCur3 = sceneMap(curScene());
      uint8_t presentId = 0;
      for (const auto &kv : mapCur3) {
        if (kv.first != 0 && kv.first != curId()) { presentId = kv.first; break; }
      }
      bool o3 = propsApplyFieldValue(fParent, presentId, true) && getPropParent(propsCurrentProp()) == presentId &&
                warnDeltaOk(0);
      const uint8_t absentA = absentNow();
      bool o4 = propsApplyFieldValue(fParent, absentA, true) && getPropParent(propsCurrentProp()) == absentA &&
                warnDeltaOk(absentA != 0 ? 1 : 0);
      expectedTotal += absentA != 0 ? 1 : 0;
      std::cout << "props O3 parent present=" << hexByte(presentId) << " ok=" << (o3 ? 1 : 0)
                << " O4 parent absent=" << hexByte(absentA) << " warn=" << (absentA != 0 ? 1 : 0)
                << " ok=" << (o4 ? 1 : 0) << std::endl;
      editsOk &= o3 && o4;

      // O5: scene 移動（現在 id が無い scene へ・warning 0）。無ければ skip（flag=1 のまま）。
      uint8_t otherScene = 0xFF;
      for (uint8_t s : gGraph.scenes) {
        if (s == curScene()) continue;
        const auto m = sceneMap(s);
        if (m.find(curId()) == m.end()) { otherScene = s; break; }
      }
      bool o5Skipped = otherScene == 0xFF;
      bool o5 = o5Skipped;
      if (!o5Skipped) {
        o5 = propsApplyFieldValue(fScene, otherScene, true) && getPropScene(propsCurrentProp()) == otherScene &&
             gProps.nodeKey == nodeKey(otherScene, curId()) && propsResolveCurrentNode() >= 0 &&
             warnDeltaOk(0);
      }
      std::cout << "props O5 scene move target=" << hexNibble(otherScene) << " moved=" << (o5Skipped ? 0 : 1)
                << " ok=" << (o5 ? 1 : 0) << std::endl;
      editsOk &= o5;

      // O6: hasRead Y/N（programmatic・warning 0・load-time prop の他 bit を壊さない）。
      bool o6 = true;
      const uint64_t beforeO6 = propsCurrentProp();
      propsSetBoolField(0, true);
      // 他 bit を壊さない照合: bit16 を mask して clearPropBits で除き、残りの bits が一致するか（bit16 だけを立てる）。
      o6 &= getPropHasRead(propsCurrentProp()) && clearPropBits(propsCurrentProp(), 1ULL << 16) == clearPropBits(beforeO6, 1ULL << 16);
      propsSetBoolField(0, false);
      o6 &= !getPropHasRead(propsCurrentProp()) && warnDeltaOk(0);
      std::cout << "props O6 hasRead toggle ok=" << (o6 ? 1 : 0) << std::endl;
      editsOk &= o6;

      // O7/O8/O9: slot0 requiredReadId → present（warning 0）→ absent（warning 1・ghost match）→ none（元 index 復元・value 0）。
      uint8_t present0 = 0;
      {
        const auto m = sceneMap(curScene());
        for (const auto &kv : m) { if (kv.first != 0 && kv.first != curId()) { present0 = kv.first; break; } }
      }
      bool o7 = propsSetOption(fSlot0Opt, 1) && getSlotIndex(propsCurrentProp(), 0) == 0 && warnDeltaOk(0);
      o7 &= propsApplyFieldValue(fSlot0Val, present0, true) && getPropRequiredReadId(propsCurrentProp()) == present0 &&
           warnDeltaOk(0);
      bool constraintAfterPresent = verifyConstraintCounts("props after O7 required present");
      const uint8_t absentB = absentNow();
      bool o8 = propsApplyFieldValue(fSlot0Val, absentB, true) && getPropRequiredReadId(propsCurrentProp()) == absentB &&
                warnDeltaOk(absentB != 0 ? 1 : 0);
      expectedTotal += absentB != 0 ? 1 : 0;
      const auto mapO8 = sceneMap(curScene());
      const ConstraintEdgeCounts gotO8 = countConstraintEdges(curScene());
      bool ghostOk = o8 && absentB != 0 && gotO8.requiredGhost >= 1 && mapO8.find(absentB) == mapO8.end();
      bool constraintAfterGhost = verifyConstraintCounts("props after O8 cross-scene ghost");
      bool o9 = propsSetOption(fSlot0Opt, 0) && getSlotIndex(propsCurrentProp(), 0) == gProps.baseSlotIndex[0] &&
                getSlotValue(propsCurrentProp(), 0) == 0 && getPropRequiredReadId(propsCurrentProp()) == 0 &&
                warnDeltaOk(0);
      bool constraintBack0 = verifyConstraintCounts("props after O9 none");
      std::cout << "props O7 required present=" << hexByte(present0) << " ok=" << (o7 ? 1 : 0) << " constraint match=" << (constraintAfterPresent ? 1 : 0)
                << " O8 absent=" << hexByte(absentB) << " warn=" << (absentB != 0 ? 1 : 0) << " ghost=" << (ghostOk ? 1 : 0) << " constraint match=" << (constraintAfterGhost ? 1 : 0)
                << " O9 none idx=" << std::to_string(gProps.baseSlotIndex[0]) << " ok=" << (o9 ? 1 : 0) << " match=" << (constraintBack0 ? 1 : 0) << std::endl;
      editsOk &= o7 && o8 && ghostOk && constraintAfterPresent && constraintAfterGhost && constraintBack0;

      // O10/O11/O12: slot1 child（実在・child edge 出現）→ excluded（排他・child==0）→ absent（ghost）→ none。
      uint8_t present1 = 0;
      {
        const auto m = sceneMap(curScene());
        for (const auto &kv : m) { if (kv.first != 0 && kv.first != curId()) { present1 = kv.first; break; } }
      }
      bool o10 = propsSetOption(fSlot1Opt, 1) && getPropChild(propsCurrentProp()) == 0 && warnDeltaOk(0);
      o10 &= propsApplyFieldValue(fSlot1Val, present1, true) && getPropChild(propsCurrentProp()) == present1 &&
             warnDeltaOk(0);
      const int targetNow = propsResolveCurrentNode();
      const int childIdx = gGraph.findNode(curScene(), present1);
      bool childEdgeOk = targetNow >= 0 && childIdx >= 0 &&
                         gGraph.childLinkAdj[targetNow].size() == 1 && gGraph.childLinkAdj[targetNow][0] == childIdx;
      bool o11Excl = propsSetOption(fSlot1Opt, 2) && getPropChild(propsCurrentProp()) == 0 &&
                     getPropSlot1ExcludedReadId(propsCurrentProp()) == present1 && warnDeltaOk(0);
      const uint8_t absentC = absentNow();
      bool o11Abs = propsApplyFieldValue(fSlot1Val, absentC, true) && getPropSlot1ExcludedReadId(propsCurrentProp()) == absentC &&
                    getPropChild(propsCurrentProp()) == 0 && warnDeltaOk(absentC != 0 ? 1 : 0);
      expectedTotal += absentC != 0 ? 1 : 0;
      bool constraintExclGhost = verifyConstraintCounts("props after O11 excluded ghost");
      bool o12 = propsSetOption(fSlot1Opt, 0) && getSlotIndex(propsCurrentProp(), 1) == gProps.baseSlotIndex[1] &&
                 getSlotValue(propsCurrentProp(), 1) == 0 && getPropChild(propsCurrentProp()) == 0 &&
                 getPropSlot1ExcludedReadId(propsCurrentProp()) == 0 && getPropNewRoot(propsCurrentProp()) == 0 &&
                 warnDeltaOk(0);
      bool constraintBack1 = verifyConstraintCounts("props after O12 none");
      // family/constraint の edge は再構築だけで即時更新される（確認だけ・配線追加不要）。
      const uint8_t famScene = curScene();
      layoutFamily(famScene);
      size_t famPlaced = 0;
      for (int i : gGraph.nodesInScene(famScene)) if (gGraph.nodes[i].famDepth >= 0) ++famPlaced;
      const bool famOk = propsResolveCurrentNode() >= 0 && gGraph.nodes[propsResolveCurrentNode()].famDepth >= 0 &&
                         famPlaced == gGraph.nodesInScene(famScene).size();
      std::cout << "props O10 child=" << hexByte(present1) << " edge=" << (childEdgeOk ? 1 : 0) << " ok=" << (o10 ? 1 : 0)
                << " O11 excluded exclusive child0=" << (o11Excl ? 1 : 0) << " absent ghost warn=1 ok=" << (o11Abs ? 1 : 0)
                << " constraint match=" << (constraintExclGhost ? 1 : 0)
                << " O12 none ok=" << (o12 ? 1 : 0) << " match=" << (constraintBack1 ? 1 : 0) << std::endl;
      editsOk &= o10 && childEdgeOk && o11Excl && o11Abs && constraintExclGhost && o12 && constraintBack1 && famOk;

      // O13/O14: slot2 timerSetter（option 立て→部分編集・decode 一致・hasPropTimerSetter）→ none → inert warning。
      bool o13 = propsSetOption(fSlot2Opt, 1) && getSlotIndex(propsCurrentProp(), 2) == 0 && warnDeltaOk(0);
      o13 &= propsApplyFieldValue(fSlot2TimerId, 1, true) && getPropTimerSetterId(propsCurrentProp()) == 1 && warnDeltaOk(0);
      o13 &= propsApplyFieldValue(fSlot2Action, 1, true) && getPropTimerSetterAction(propsCurrentProp()) == 1 &&
             hasPropTimerSetter(propsCurrentProp()) && warnDeltaOk(0);
      const uint64_t tvState = getSlotValue(propsCurrentProp(), 2);
      const bool tvDecodeOk = getPropTimerSetter(propsCurrentProp()) == tvState && (tvState & 0x0C) == 0;
      // O14: none 復元（value 0・option は元 index に戻る）→ value bits の編集は option に一致すれば warning なし、
      // 一致しなければ inert warning（base index が option idx と違う場合の期待値はその場で算出）。
      const size_t inert2Exp = gProps.baseSlotIndex[2] != 0 ? 1 : 0;
      bool o14 = propsSetOption(fSlot2Opt, 0) && getSlotValue(propsCurrentProp(), 2) == 0 &&
                 !hasPropTimerSetter(propsCurrentProp()) && warnDeltaOk(0);
      o14 &= propsApplyFieldValue(fSlot2TimerId, 1, true) && getSlotValue(propsCurrentProp(), 2) == 0x01 &&
             !hasPropTimerSetter(propsCurrentProp()) && warnDeltaOk(inert2Exp);
      expectedTotal += inert2Exp;
      o14 &= propsSetOption(fSlot2Opt, 0) && getSlotValue(propsCurrentProp(), 2) == 0 && warnDeltaOk(0);
      std::cout << "props O13 timerSetter id=1 action=1 decode=" << (tvDecodeOk ? 1 : 0) << " ok=" << (o13 ? 1 : 0)
                << " O14 none+value edit inert expected=" << inert2Exp << " ok=" << (o14 ? 1 : 0) << std::endl;
      editsOk &= o13 && tvDecodeOk && o14;

      // O15/O16: slot3 timerTarget（op=2・timerId・count・decode 一致）→ none → inert。eyelevelToggle（eye/toggle 一致）。
      bool o15 = propsSetOption(fSlot3Opt, 1) && getSlotIndex(propsCurrentProp(), 3) == 0 && warnDeltaOk(0);
      o15 &= propsApplyFieldValue(fSlot3Op, 2, true) && getPropTimerTargetOp(propsCurrentProp()) == 2 && warnDeltaOk(0);
      o15 &= propsApplyFieldValue(fSlot3TimerId, 1, true) && getPropTimerTargetId(propsCurrentProp()) == 1 && warnDeltaOk(0);
      o15 &= propsApplyFieldValue(fSlot3Count, 3, true) && getPropTimerTargetCount(propsCurrentProp()) == 3 &&
             hasPropTimerTarget(propsCurrentProp()) && warnDeltaOk(0);
      bool o15none = propsSetOption(fSlot3Opt, 0) && !hasPropTimerTarget(propsCurrentProp()) &&
                     getSlotValue(propsCurrentProp(), 3) == 0 && warnDeltaOk(0);
      const size_t inert3Exp = gProps.baseSlotIndex[3] != 0 ? 1 : 0;
      o15none &= propsApplyFieldValue(fSlot3Count, 2, true) && getSlotValue(propsCurrentProp(), 3) == 0x20 &&
                 !hasPropTimerTarget(propsCurrentProp()) && warnDeltaOk(inert3Exp);
      expectedTotal += inert3Exp;
      o15none &= propsSetOption(fSlot3Opt, 0) && getSlotValue(propsCurrentProp(), 3) == 0 && warnDeltaOk(0);
      bool o16 = propsSetOption(fSlot3Opt, 4) && getSlotIndex(propsCurrentProp(), 3) == 3 && warnDeltaOk(0);
      o16 &= propsApplyFieldValue(fEye, 3, true) && getPropEyelevel(propsCurrentProp()) == 3 && warnDeltaOk(0);
      o16 &= propsApplyFieldValue(fEyeToggle, 5, true) && getPropToggleEyelevel(propsCurrentProp()) == 5 && warnDeltaOk(0);
      o16 &= propsSetOption(fSlot3Opt, 0) && getPropEyelevel(propsCurrentProp()) == 0 &&
             getPropToggleEyelevel(propsCurrentProp()) == 0 && warnDeltaOk(0);
      std::cout << "props O15 timerTarget op=2 id=1 count=3 ok=" << (o15 ? 1 : 0) << " none inert ok=" << (o15none ? 1 : 0)
                << " O16 eyelevel=3 toggle=5 ok=" << (o16 ? 1 : 0) << std::endl;
      editsOk &= o15 && o15none && o16;

      // eye level の option 非立て編集（validation の inert warning・期待値は base idx から算出）。
      const size_t eyeInertExp = gProps.baseSlotIndex[3] != 3 ? 1 : 0;
      const uint8_t eyeEffExpect = gProps.baseSlotIndex[3] == 3 ? 2 : 0; // option に一致する場合は実効値 2・非立てなら 0
      bool eyeInert = propsApplyFieldValue(fEye, 2, true) && getSlotValue(propsCurrentProp(), 3) == 0x02 &&
                      getPropEyelevel(propsCurrentProp()) == eyeEffExpect && warnDeltaOk(eyeInertExp);
      expectedTotal += eyeInertExp;
      eyeInert &= propsSetOption(fSlot3Opt, 0) && getSlotValue(propsCurrentProp(), 3) == 0 &&
                  !hasPropTimerTarget(propsCurrentProp()) && warnDeltaOk(0);
      std::cout << "props eyelevel inert edit expected=" << eyeInertExp << " ok=" << (eyeInert ? 1 : 0) << std::endl;
      editsOk &= eyeInert;

      // 各 option を 1 回ずつ立てて読む（real doc・warning 0・none 復元）。
      bool optionsOk = true;
      {
        uint8_t presentF = 0;
        const auto m = sceneMap(curScene());
        for (const auto &kv : m) { if (kv.first != 0 && kv.first != curId()) { presentF = kv.first; break; } }
        // slot0 unlocks
        optionsOk &= propsSetOption(fSlot0Opt, 2) && getSlotIndex(propsCurrentProp(), 0) == 1 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot0Val, 0x22, true) && getPropSlot0Unlocks(propsCurrentProp()) == 0x22 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot0Opt, 0) && getPropSlot0Unlocks(propsCurrentProp()) == 0 && warnDeltaOk(0);
        // slot1 newRoot
        optionsOk &= propsSetOption(fSlot1Opt, 3) && getPropNewRoot(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot1Val, presentF, true) && getPropNewRoot(propsCurrentProp()) == presentF &&
                     gGraph.newRootTargets.size() >= 1 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot1Opt, 0) && getPropNewRoot(propsCurrentProp()) == 0 && warnDeltaOk(0);
        // slot1 downUnlocks（value は mask・id target 検査がないので warning 0）
        optionsOk &= propsSetOption(fSlot1Opt, 4) && getSlotIndex(propsCurrentProp(), 1) == 3 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot1Val, 0x2A, true) &&
                     getPropSlot1DownUnlocks(propsCurrentProp()) == 0x2A &&
                     getPropNewRoot(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot1Opt, 0) && getPropSlot1DownUnlocks(propsCurrentProp()) == 0 &&
                     getSlotValue(propsCurrentProp(), 1) == 0 && warnDeltaOk(0);
        // slot2 unlocks / requiredUnlockFlags / excluded
        optionsOk &= propsSetOption(fSlot2Opt, 2) && getSlotIndex(propsCurrentProp(), 2) == 1 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot2Val, 0x11, true) && getPropSlot2Unlocks(propsCurrentProp()) == 0x11 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 0) && getPropSlot2Unlocks(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 3) && getSlotIndex(propsCurrentProp(), 2) == 2 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot2Val, 0x25, true) && getPropRequiredUnlockFlags(propsCurrentProp()) == 0x25 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 0) && getPropRequiredUnlockFlags(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 4) && getSlotIndex(propsCurrentProp(), 2) == 3 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot2Val, presentF, true) && getPropSlot2ExcludedReadId(propsCurrentProp()) == presentF && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot2Opt, 0) && getPropSlot2ExcludedReadId(propsCurrentProp()) == 0 && warnDeltaOk(0);
        // slot3 newScene（value 0 は「変化なし」sentinel・選択可）/ excludedUnlockFlags
        optionsOk &= propsSetOption(fSlot3Opt, 2) && getSlotIndex(propsCurrentProp(), 3) == 1 && getPropNewScene(propsCurrentProp()) == 0 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot3Opt, 0) && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot3Opt, 3) && getSlotIndex(propsCurrentProp(), 3) == 2 && warnDeltaOk(0);
        optionsOk &= propsApplyFieldValue(fSlot3Val, 0x29, true) && getPropExcludedUnlockFlags(propsCurrentProp()) == 0x29 && warnDeltaOk(0);
        optionsOk &= propsSetOption(fSlot3Opt, 0) && getPropExcludedUnlockFlags(propsCurrentProp()) == 0 && warnDeltaOk(0);
        std::cout << "props options each-set-once match=" << (optionsOk ? 1 : 0) << std::endl;
      }
      editsOk &= optionsOk;

      // validation warning 総数の照合: 各 op でその場で算出した期待値（範囲外 clamp / duplicate id reject /
      // cross-scene target / option 非立て inert）の合計と gProps.warningTotal が一致すること。
      // 期待値はすべて prop 直接パースから実行時に計算している（数をハードコードしない）。
      const size_t gotWarnTotal = gProps.warningTotal;
      const bool warnTotalOk = gotWarnTotal == expectedTotal;
      std::cout << "props validation warnings got=" << gotWarnTotal << " expected=" << expectedTotal
                << " match=" << (warnTotalOk ? 1 : 0) << std::endl;
      editsOk &= warnTotalOk;

      // G キー: graph 再構築のみ（runtime state は触らない・選択 (scene,id) 復元）。
      const size_t nodesBeforeG = gGraph.nodes.size();
      const size_t edgesBeforeG = gGraph.edges.size();
      postKey(SDLK_G);
      const bool rebuildOk = gGraph.nodes.size() == nodesBeforeG && gGraph.edges.size() == edgesBeforeG &&
                             propsResolveCurrentNode() >= 0 && containerA.size() == baseContA &&
                             containerB.size() == baseContB;
      std::cout << "props G graph rebuild ok=" << (rebuildOk ? 1 : 0) << " runtime untouched=1" << std::endl;
      editsOk &= rebuildOk;

      // Prompt 6・要件 5: scene 切替（[ ] は共通経路・props の TAB/ENTER/+/-/Y/N/U/G/ESC 優先を
      // 通過する）でも props panel・nodeKey・(scene,id) 解決は壊れないこと。切替往復で
      // panel open / nodeKey / 解決がそのまま維持されることを確認する（document は触らない）。
      const uint8_t sceneBeforeSw = gViewScene;
      postKey(SDLK_LEFTBRACKET);
      const bool panelSwOn = gProps.active && propsResolveCurrentNode() >= 0 &&
                             gProps.nodeKey == nodeKey(curScene(), curId());
      postKey(SDLK_RIGHTBRACKET);
      const bool panelSwOk = stPass(panelSwOn && gViewScene == sceneBeforeSw && gProps.active &&
                                   propsResolveCurrentNode() >= 0 &&
                                   gProps.nodeKey == nodeKey(curScene(), curId()), "props edit");
      std::cout << "props_panel_survives_scene_switch ok=" << (panelSwOk ? 1 : 0) << std::endl;
      editsOk &= panelSwOk;

      // real props panel の描画 → build/viewer_frame_props.bmp（PASS: text/subtext1/sky・yellow は info）。
      bool frameOk = false;
      {
        const uint8_t nodeScene = curScene();
        gViewScene = nodeScene;
        applyActiveLayout();
        fitViewToScene();
        gSelectedNode = propsResolveCurrentNode();
        if (gRenderer) {
          setSolid(gRenderer, kMCBase);
          SDL_RenderClear(gRenderer);
          drawGraphOverlay(gRenderer); // node/edge + drawEditorOverlay + props panel（HUD menu rows）
          SDL_RenderPresent(gRenderer);
          SDL_Surface *shot = SDL_RenderReadPixels(gRenderer, nullptr);
          if (shot) {
            SDL_SaveBMP(shot, "build/viewer_frame_props.bmp");
            std::cout << "props frame captured scene=" << hexNibble(nodeScene) << " " << shot->w << "x" << shot->h
                      << " -> build/viewer_frame_props.bmp" << std::endl;
            SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24);
            if (rgba) {
              std::map<uint32_t, uint32_t> hist;
              uint32_t bright = 0;
              for (int y = 0; y < rgba->h; ++y) {
                const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) +
                                     static_cast<size_t>(y) * rgba->pitch;
                for (int x = 0; x < rgba->w; ++x) {
                  const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                  if (std::max(rr, std::max(rg, rb)) < 120) continue;
                  ++bright;
                  ++hist[(static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) | static_cast<uint32_t>(rb)];
                }
              }
              const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
                const auto it = hist.find(hexColor);
                return it == hist.end() ? 0u : it->second;
              };
              std::cout << "props bright pixels=" << bright << std::endl;
              std::cout << "props kmc colors: sky=" << kmcCount(kMCSky) << " text=" << kmcCount(kMCText)
                        << " subtext1=" << kmcCount(kMCSubtext1) << " subtext0=" << kmcCount(kMCSubtext0)
                        << " yellow=" << kmcCount(kMCYellow) << " lavender=" << kmcCount(kMCLavender)
                        << " teal=" << kmcCount(kMCTeal) << std::endl;
              frameOk = kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0 && kmcCount(kMCSky) > 0;
              std::cout << "props frame text/subtext1/sky present=" << (frameOk ? 1 : 0) << " yellow(info)=" << kmcCount(kMCYellow) << std::endl;
              SDL_DestroySurface(rgba);
            }
            SDL_DestroySurface(shot);
          } else {
            std::cout << "props frame capture unavailable: " << SDL_GetError() << std::endl;
          }
        }
      }
      editsOk &= frameOk;

      // ESC キー: panel を閉じる（選択解除より優先・selection は維持・text input は対称のまま）。
      postKey(SDLK_ESCAPE);
      const bool escCloseOk = !gProps.active && gSelectedNode >= 0 && !gEditorTextInputActive;
      std::cout << "props ESC close panel ok=" << (escCloseOk ? 1 : 0) << " selection kept=" << (gSelectedNode >= 0 ? 1 : 0) << std::endl;
      editsOk &= escCloseOk;

      // 全 undo: stack を空になるまで pop → baseline 完全復元（prop・line text・dirty・graph・round-trip EXACT）。
      const uint32_t origKey = nodeKey(testScene, targetId0);
      const int origElement = gDocument.lines[gGraph.nodes[targetIdx].lineIndex].elementIndex;
      size_t undoCount = 0;
      while (!gProps.undoStack.empty()) { propsUndo(); ++undoCount; }
      gViewScene = testScene;
      applyActiveLayout();
      fitViewToScene();
      bool restoreOk = true;
      restoreOk &= undoCount >= 1; // 1段以上の undo が復元に到達する
      restoreOk &= propsCurrentProp() == targetProp0 && gProps.nodeKey == origKey;
      const int restoredIdx = propsResolveCurrentNode();
      restoreOk &= restoredIdx >= 0 && gGraph.nodes[restoredIdx].elementIndex == origElement;
      restoreOk &= serializeDocument(gDocument) == gDocument.raw;
      const RoundTripReport rtUndoBack = verifyRoundTrip(gDocument);
      gRoundTrip = rtUndoBack;
      restoreOk &= rtUndoBack.exact && gDocument.raw.size() == baseBytes && gDocument.lines.size() == baseLines &&
                   gDocument.elements.size() == baseElements;
      for (size_t i = 0; i < gDocument.elements.size(); ++i) {
        restoreOk &= gDocument.elements[i].element.prop == origProps[i] && gDocument.elements[i].dirty == origDirty[i];
      }
      for (size_t i = 0; i < gDocument.lines.size(); ++i) {
        restoreOk &= gDocument.lines[i].text == origText[i];
      }
      restoreOk &= gGraph.nodes.size() == baseNodes && gGraph.edges.size() == baseEdges &&
                   gGraph.roots.size() == baseRoots && gSelectedNode == restoredIdx &&
                   containerA.size() == baseContA && containerB.size() == baseContB &&
                   !gDocTerminatorPatched && !gProps.undoStack.empty() == false;
      // Prompt 6: undo phase を FAIL 検知に格上げ（要件 3・8）。undo stack 上限（kPropsUndoMax,
      // Prompt 6 で 256 に引き上げ）に達していないことも確認する — 満杯だと全 undo で baseline に
      // 戻らないため（Prompt 5 の観測）。期待値は元の prop/line/dirty/graph を実行時比較で算出。
      const bool undoHeadroomOk = undoCount < kPropsUndoMax;
      const bool undoPhaseOk = stPass(restoreOk && undoHeadroomOk, "undo");
      std::cout << "props undo undo_count=" << undoCount << " restore baseline exact=" << (rtUndoBack.exact ? 1 : 0)
                << " match=" << (restoreOk ? 1 : 0) << " undo_limit=" << kPropsUndoMax
                << " headroom=" << (undoHeadroomOk ? 1 : 0) << " ok=" << (undoPhaseOk ? 1 : 0) << std::endl;
      editsOk &= restoreOk && undoHeadroomOk;

      // general に戻る（props panel は mode 切替で閉じる・document は元のまま）。
      postKey(SDLK_1);
      const bool exitOk = gViewMode == VIEW_GENERAL && gEditor.stage == EDITOR_IDLE && !gProps.active &&
                          !gEditorTextInputActive && serializeDocument(gDocument) == gDocument.raw;
      std::cout << "props mode back general state cleared=" << (exitOk ? 1 : 0) << std::endl;
      editsOk &= exitOk;

      // Prompt 6: props の全 flag を FAIL 検知に格上げ（要件 3・8）。期待値は Prompt 5 の
      // 実行時計算（prop 直接パース）のまま。undo phase の復元は "undo" として別 phase 集約。
      const bool propsAllOk = stPass(targetFound && rtOk && cdOk && modeOk && openedByClick && openedByRight && variantOk &&
                                     tabOk && enterToggleOk && reparseOk && undoRestoreOk && yOk && nOk && tabWrapOk &&
                                     ynUndoOk && keyNoWarnOk && editsOk, "props edit");
      std::cout << "props_phase_all_flags=" << (propsAllOk ? 1 : 0) << std::endl;
      std::cout << "round_trip_after_props_phase=" << (rtUndoBack.exact ? "EXACT OK" : "MISMATCH") << std::endl;
    }
}

static void phase_font() {
    std::cout << "=== [ selftest font ] ===" << std::endl;
    std::cout << "gui text: " << (gGuiText.ready ? "READY" : "ASCII fallback")
              << " font=" << gGuiText.fontPath << " err=" << gGuiText.lastError << std::endl;
    if (gGuiText.ready && gRenderer) {
      // 日本語 glyph が rasterize できているか（texture と bitmap 寸法を確認する）。
      // Prompt 6: ready の環境で glyph が rasterize できなかったら FAIL（ascii fallback
      // 環境は描画側の PASS 検証で扱う — 環境制約は失敗にしない）。
      bool glyphsAllOk = true;
      for (uint32_t cp : {0x8983u /*見*/, 0x8A71u /*話*/, 0x308Bu /*る*/, 0x5C31u /*札*/}) {
        const GlyphRec *g = guiGlyph(gRenderer, cp);
        if (!g) glyphsAllOk = false;
        std::cout << "glyph cp=" << cp << " drawn=" << (g ? 1 : 0) << " w=" << (g ? g->w : 0)
                  << " h=" << (g ? g->h : 0) << " adv=" << (g ? g->advance : 0.f) << std::endl;
      }
      stPass(glyphsAllOk, "font");
    }

    // 画面を実際に描いて pixel を読み出し、日本語テキストが描画されているか検証する。
    if (gRenderer) {
      setSolid(gRenderer, kMCBase); // window background: mocha base #1e1e2e
      SDL_RenderClear(gRenderer);
      drawGraphOverlay(gRenderer);
      SDL_RenderPresent(gRenderer);
      SDL_Surface *shot = SDL_RenderReadPixels(gRenderer, nullptr);
      if (shot) {
        SDL_SaveBMP(shot, "build/viewer_frame.bmp");
        // Prompt 6・要件 8: mode 別 frame の general を保存する（viewer_frame_<mode>. の規則）。
        SDL_SaveBMP(shot, "build/viewer_frame_general.bmp");
        std::cout << "frame captured " << shot->w << "x" << shot->h <<
                  " -> build/viewer_frame.bmp / build/viewer_frame_general.bmp" << std::endl;
        // 読み出し surface のレイアウトは driver 依存なので、確定した RGB24 に
        // 変換してから pixel を読む（RGBA8888/XRGB8888 の取り違えを防ぐ）。
        SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24);
        if (rgba) {
          // 明い pixel（テキスト・線・枠）の色をヒストグラムで集める。
          // 背景(mocha base #1e1e2e)と HUD panel(mantle #181825)は max<120 で除く。
          std::map<uint32_t, uint32_t> hist;
          uint32_t bright = 0;
          uint32_t hudText = 0;
          uint32_t nodeText = 0;
          const int hudW = std::min(kHudW, rgba->w);
          for (int y = 0; y < rgba->h; ++y) {
            const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) +
                                 static_cast<size_t>(y) * rgba->pitch;
            for (int x = 0; x < rgba->w; ++x) {
              const uint8_t r = row[x * 3 + 0], g = row[x * 3 + 1], b = row[x * 3 + 2];
              if (std::max(r, std::max(g, b)) < 120) continue;
              ++bright;
              const uint32_t key = (static_cast<uint32_t>(r) << 16) |
                                   (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
              ++hist[key];
              if (x >= 4 && x < hudW - 4) ++hudText;
              else if (x >= hudW) ++nodeText;
            }
          }
          std::cout << "bright pixels=" << bright << " hud_region=" << hudText
                    << " graph_region=" << nodeText << std::endl;
          std::vector<std::pair<uint32_t, uint32_t> > top(hist.begin(), hist.end());
          std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
          for (size_t i = 0; i < std::min<size_t>(8, top.size()); ++i) {
            std::cout << "  color 0x" << std::hex << std::setw(6) << std::setfill('0')
                      << top[i].first << " count=" << std::dec << top[i].second << std::endl;
          }
          // Prompt 6・要件 8: general frame の PASS を FAIL 検知に格上げ。HUD の text/subtext1 出現を
          // 必須とし、parent field edge=sapphire・child edge=peach の区別は capture scene の
          // 期待値（prop 直接パース・first-wins）があるときのみ実色を要求する（family/constraint
          // frame と同じ方針。red の出現は quote ストライプ由来・lavender は選択 info）。
          const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
            const auto it = hist.find(hexColor);
            return it == hist.end() ? 0u : it->second;
          };
          std::unordered_map<uint8_t, uint64_t> mapCapG;
          for (const ElementRecord &rec : gDocument.elements) {
            if (getPropScene(rec.element.prop) != gViewScene) continue;
            const uint8_t idG = getPropID(rec.element.prop);
            if (mapCapG.find(idG) != mapCapG.end()) continue;
            mapCapG[idG] = rec.element.prop;
          }
          size_t expParG = 0, expChiG = 0;
          for (const auto &kv : mapCapG) {
            const uint8_t parG = getPropParent(kv.second);
            if (parG != 0 && mapCapG.find(parG) != mapCapG.end()) ++expParG;
            const uint8_t chiG = getPropChild(kv.second);
            if (chiG != 0 && mapCapG.find(chiG) != mapCapG.end()) ++expChiG;
          }
          bool genFrameOk = bright > 0 && kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0;
          if (expParG > 0) genFrameOk &= kmcCount(kMCSapphire) > 0;
          if (expChiG > 0) genFrameOk &= kmcCount(kMCPeach) > 0;
          genFrameOk = stPass(genFrameOk, "font");
          std::cout << "general frame PASS bright=" << bright << " sapphire=" << kmcCount(kMCSapphire) << "/" << expParG
                    << " peach=" << kmcCount(kMCPeach) << "/" << expChiG << " text=" << kmcCount(kMCText)
                    << " subtext1=" << kmcCount(kMCSubtext1) << " ok=" << (genFrameOk ? 1 : 0) << std::endl;
          SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(shot);
      } else {
        stFail("font"); // Prompt 6: frame capture が取れない場合は FAIL（要件 8）
        std::cout << "frame capture unavailable: " << SDL_GetError() << std::endl;
      }
    }

    switchScene(+1);
    std::cout << "switchScene(+1) -> scene=" << hexNibble(gViewScene)
              << " nodes=" << gGraph.nodesInScene(gViewScene).size() << std::endl;

}

static void phase_palette_audit() {
    // --- Prompt 6 palette audit phase（要件 6・色と視認性の監査） ---
    // 数値 RGB リテラル監査（kMC 定数定義以外 zero）・SDL_RenderDebugText は fallback のみ・
    // glyph texture は RGBA32・readback は RGB24 変換後。面+テキストの WCAG を pixel readback の
    // 実色で測り text/subtext1 の組合せが 4.5:1 以上であることを確認する（mocha 公式組合せ）。
    std::cout << "=== [ selftest palette audit ] ===" << std::endl;
    {
      // 監査対象はリファクタリング後の複数 TU を含む（区切り・描画コードの移動先に
      // needle が現れるため、存在するソースファイルを順に走査して集約する）。
      std::vector<std::string> srcPaths;
      for (const char *dir : {"./", "../"}) {
        for (const char *name : {"main.cpp", "guitext.cpp", "palette.h", "view.cpp", "selftest.cpp"}) {
          const std::string cand = std::string(dir) + name;
          std::ifstream probe(cand);
          if (probe.is_open()) srcPaths.push_back(cand);
        }
      }
      const bool srcOk = stPass(!srcPaths.empty(), "palette audit");
      int kmcDefs = 0, litOutside = 0, debugUses = 0, debugGuardOk = 0;
      int rgba32 = 0, rgba8888 = 0, rgb24 = 0;
      // 監査対象の needle を分割して作る（分割後のリテラルは監査コード自身に
      // 連結形では現れないので、この phase のコードが自己を counting しない）。
      const std::string debugNeedle = std::string("SDL_") + "RenderDebugText(";
      const std::string rgba8888Needle = std::string("SDL_PIXELFORMAT_RGBA") + "8888";
      const std::string rgba32Needle = std::string("SDL_PIXELFORMAT_RGBA") + "32";
      const std::string rgb24Needle = std::string("SDL_PIXELFORMAT_RGB") + "24";
      const auto isHexChar = [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
      };
      int lastDebugLine = -1, lastReadyGuardLine = -1;
      if (srcOk) {
        for (const std::string &srcPath : srcPaths) {
        std::ifstream src(srcPath);
        std::string line;
        int lineNo = 0;
        while (std::getline(src, line)) {
          ++lineNo;
          const size_t slash = line.find("//");
          if (slash != std::string::npos) line = line.substr(0, slash); // line comment は監査対象外
          const bool kmcDefLine = line.find("constexpr uint32_t kMC") != std::string::npos;
          if (kmcDefLine) ++kmcDefs;
          size_t p = line.find("0x");
          while (p != std::string::npos) {
            size_t q = p + 2;
            int n = 0;
            while (q < line.size() && isHexChar(line[q])) { ++n; ++q; }
            // 6桁ちょうどの 0xRRGGBB リテラル（kMC 定数定義以外）を数える。
            // longer hex literals（prop 値・bit mask・%016llx 出力）は対象にしない。
            if (n == 6 && !kmcDefLine) ++litOutside;
            p = line.find("0x", p + 2);
          }
          if (line.find(debugNeedle) != std::string::npos) {
            ++debugUses;
            lastDebugLine = lineNo;
            if (lastReadyGuardLine > 0 && lineNo - lastReadyGuardLine <= 20) debugGuardOk = 1;
          }
          if (line.find("gGuiText.ready") != std::string::npos) lastReadyGuardLine = lineNo;
          if (line.find(rgba32Needle) != std::string::npos) ++rgba32;
          if (line.find(rgba8888Needle) != std::string::npos) ++rgba8888;
          if (line.find(rgb24Needle) != std::string::npos) ++rgb24;
        }
        }
      }
      const bool litOk = srcOk && kmcDefs > 0 && litOutside == 0;
      const bool apiOk = debugUses <= 1 && debugGuardOk == 1 && rgba8888 == 0 && rgba32 > 0 && rgb24 > 0;
      stPass(litOk, "palette audit");
      stPass(apiOk, "palette audit");
      std::cout << "palette literals: kmc_definitions=" << kmcDefs << " numeric_rgb_literals_outside_kmc_defs="
                << litOutside << " match=" << (litOk ? 1 : 0) << " (grep 相当・line comment は除く)" << std::endl;
      std::cout << "text/api guards: debugtext_code_uses=" << debugUses << " fallback_only=" << debugGuardOk
                << " rgba8888=" << rgba8888 << " rgba32=" << rgba32 << " rgb24_readback=" << rgb24
                << " match=" << (apiOk ? 1 : 0) << std::endl;

      // WCAG を pixel readback の実色で測る（要件 6）。合成 band（face 面 + text/subtext1 行）を
      // 描いて build/viewer_frame_palette.bmp に保存し、face 面の色は直接 pixel、text/subtext1 の
      // 実色は bright ヒストグラムで確認してから contrast ratio を計算する（4.5:1 以上）。
      const uint32_t faces6[] = {kMCBase, kMCMantle, kMCSurface0, kMCSurface1, kMCFillTemplate, kMCFillEmpty};
      const uint32_t fg2[] = {kMCText, kMCSubtext1};
      const size_t faceCount = sizeof(faces6) / sizeof(faces6[0]);
      bool wcagOk = false;
      if (gRenderer) {
        int winW = 0, winH = 0;
        windowSize(&winW, &winH);
        const float bandH = static_cast<float>(winH) / static_cast<float>(faceCount);
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        for (size_t i = 0; i < faceCount; ++i) {
          const SDL_FRect band{0.f, static_cast<float>(i) * bandH, static_cast<float>(winW), bandH + 1.f};
          setSolid(gRenderer, faces6[i]);
          SDL_RenderFillRect(gRenderer, &band);
          drawGuiLine(gRenderer, 10.f, static_cast<float>(i) * bandH + 8.f, "contrast audit row text", 1.f,
                      static_cast<float>(winW) - 20.f, fg2[0]);
          drawGuiLine(gRenderer, 10.f, static_cast<float>(i) * bandH + 24.f, "contrast audit row subtext1", 1.f,
                      static_cast<float>(winW) - 20.f, fg2[1]);
        }
        SDL_RenderPresent(gRenderer);
        SDL_Surface *paShot = SDL_RenderReadPixels(gRenderer, nullptr);
        if (paShot) {
          SDL_SaveBMP(paShot, "build/viewer_frame_palette.bmp");
          std::cout << "palette frame captured " << paShot->w << "x" << paShot->h << " -> build/viewer_frame_palette.bmp" << std::endl;
          SDL_Surface *paRgb = SDL_ConvertSurface(paShot, SDL_PIXELFORMAT_RGB24);
          if (paRgb) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < paRgb->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(paRgb->pixels) +
                                   static_cast<size_t>(y) * paRgb->pitch;
              for (int x = 0; x < paRgb->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) |
                       static_cast<uint32_t>(rb)];
              }
            }
            const auto kmcCount = [&](uint32_t hexColor) -> uint32_t {
              const auto it = hist.find(hexColor);
              return it == hist.end() ? 0u : it->second;
            };
            std::cout << "palette bright=" << bright << " text=" << kmcCount(kMCText)
                      << " subtext1=" << kmcCount(kMCSubtext1) << std::endl;
            wcagOk = bright > 0 && kmcCount(kMCText) > 0 && kmcCount(kMCSubtext1) > 0;
            for (size_t i = 0; i < faceCount; ++i) {
              // face 面の pixel を直接読む（band 上部・text 行から離れた右端）。readback の実値で測る。
              const int sy = static_cast<int>(static_cast<float>(i) * bandH) + 2;
              const int sx = paRgb->w - 6;
              const uint8_t *row = static_cast<const uint8_t *>(paRgb->pixels) +
                                   static_cast<size_t>(sy) * paRgb->pitch;
              const uint32_t observed = (static_cast<uint32_t>(row[sx * 3 + 0]) << 16) |
                                       (static_cast<uint32_t>(row[sx * 3 + 1]) << 8) |
                                       static_cast<uint32_t>(row[sx * 3 + 2]);
              const bool faceOk = observed == faces6[i];
              const double rText = wcagContrast(fg2[0], observed);
              const double rSub = wcagContrast(fg2[1], observed);
              wcagOk &= faceOk && rText >= 4.5 && rSub >= 4.5;
              std::cout << "wcag face=" << std::hex << std::setw(6) << std::setfill('0') << faces6[i] << std::dec
                        << " observed_pixel=" << (faceOk ? 1 : 0) << " text_contrast=" << rText
                        << " subtext1_contrast=" << rSub << " min>=4.5=" << (rText >= 4.5 && rSub >= 4.5 ? 1 : 0)
                        << std::endl;
            }
            SDL_DestroySurface(paRgb);
          }
          SDL_DestroySurface(paShot);
        } else {
          std::cout << "palette frame capture unavailable: " << SDL_GetError() << std::endl;
        }
      }
      std::cout << "palette wcag ok=" << (stPass(wcagOk, "palette audit") ? 1 : 0) << std::endl;
    }
}

static void phase_game_mode() {
    // --- Prompt 6 game-mode switch phase（要件 7・H トグル往復・game では graph/HUD を描かず
    // renderConsole と入力系統だけ・runtime の console flow はそのまま動作する） ---
    // DDM_SELFTEST は viewer 検証に集中する。DDM_MODE=game の起動もこの phase で viewer への
    // 復帰経路（H）を確認する（運用側の既定は viewer）。
    std::cout << "=== [ selftest game-mode ] ===" << std::endl;
    {
      auto postKey = [](SDL_Keycode k) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = k;
        ev.key.down = true;
        SDL_AppEvent(nullptr, &ev);
      };
      // 前提: selftest は viewer で始まる（DDM_MODE=game の起動でも H で viewer 復帰できること）。
      if (gUiMode == UI_GAME) setUiMode(UI_VIEWER);
      const uint8_t sceneG = gViewScene;
      postKey(SDLK_H);
      const bool gameOnOk = stPass(gUiMode == UI_GAME, "game-mode switch");
      std::cout << "game_mode_on=" << (gameOnOk ? 1 : 0) << std::endl;

      // game では描画経路（graph/HUD）を通らない。base の clear のみで readback すると
      // graph/HUD の実色が一切出ないことを確認する（frame を保存・bright=0・色 count 0）。
      bool gameFrameOk = false;
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        SDL_RenderPresent(gRenderer);
        SDL_Surface *gs = SDL_RenderReadPixels(gRenderer, nullptr);
        if (gs) {
          SDL_SaveBMP(gs, "build/viewer_frame_game.bmp");
          std::cout << "game frame captured " << gs->w << "x" << gs->h << " -> build/viewer_frame_game.bmp" << std::endl;
          SDL_Surface *gr = SDL_ConvertSurface(gs, SDL_PIXELFORMAT_RGB24);
          if (gr) {
            std::map<uint32_t, uint32_t> hist;
            uint32_t bright = 0;
            for (int y = 0; y < gr->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(gr->pixels) +
                                   static_cast<size_t>(y) * gr->pitch;
              for (int x = 0; x < gr->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                ++hist[(static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) |
                       static_cast<uint32_t>(rb)];
              }
            }
            size_t graphColors = 0;
            for (uint32_t c : {kMCSapphire, kMCPeach, kMCTeal, kMCLavender, kMCText, kMCSubtext1}) {
              if (hist.find(c) != hist.end()) graphColors += 1;
            }
            std::cout << "game frame bright=" << bright << " graph/hud colors=" << graphColors << std::endl;
            gameFrameOk = bright == 0 && graphColors == 0;
            SDL_DestroySurface(gr);
          }
          SDL_DestroySurface(gs);
        }
      }
      stPass(gameFrameOk, "game-mode switch");

      // 入力系統: game でも console の挿入 flow（TAB → ESC・対称）がそのまま動作する。
      postKey(SDLK_TAB);
      const bool insertGameOk = gAppMode == MODE_INSERT_TEXT;
      postKey(SDLK_ESCAPE); // cancelInsertMode（既存経路・text input Stop 対称）
      const bool cancelGameOk = gAppMode == MODE_NORMAL;
      std::cout << "game runtime insert flow ok=" << (insertGameOk && cancelGameOk ? 1 : 0) << std::endl;
      stPass(insertGameOk && cancelGameOk, "game-mode switch");

      postKey(SDLK_H);
      const bool backOk = stPass(gUiMode == UI_VIEWER && gViewMode == VIEW_GENERAL && gEditor.stage == EDITOR_IDLE &&
                                 !gProps.active && gViewScene == sceneG && gAppMode == MODE_NORMAL, "game-mode switch");
      std::cout << "viewer_restore ok=" << (backOk ? 1 : 0) << std::endl;

      // viewer 復帰後は graph/HUD の描画がそのまま復元されること（drawGraphOverlay → drawHud の順序）。
      bool drawBackOk = false;
      if (gRenderer) {
        setSolid(gRenderer, kMCBase);
        SDL_RenderClear(gRenderer);
        drawGraphOverlay(gRenderer);
        SDL_RenderPresent(gRenderer);
        SDL_Surface *br = SDL_RenderReadPixels(gRenderer, nullptr);
        if (br) {
          SDL_Surface *br2 = SDL_ConvertSurface(br, SDL_PIXELFORMAT_RGB24);
          if (br2) {
            uint32_t bright = 0;
            uint32_t textSeen = 0;
            for (int y = 0; y < br2->h; ++y) {
              const uint8_t *row = static_cast<const uint8_t *>(br2->pixels) +
                                   static_cast<size_t>(y) * br2->pitch;
              for (int x = 0; x < br2->w; ++x) {
                const uint8_t rr = row[x * 3 + 0], rg = row[x * 3 + 1], rb = row[x * 3 + 2];
                if (std::max(rr, std::max(rg, rb)) < 120) continue;
                ++bright;
                if (rr == mcR(kMCText) && rg == mcG(kMCText) && rb == mcB(kMCText)) textSeen += 1;
              }
            }
            drawBackOk = bright > 0 && textSeen > 0;
            SDL_DestroySurface(br2);
          }
          SDL_DestroySurface(br);
        }
      }
      std::cout << "game_mode_switch ok=" << (stPass(drawBackOk, "game-mode switch") ? 1 : 0) << std::endl;
    }

}

static void phase_data_unchanged() {
    // --- Prompt 6 data unchanged phase（要件 8・最後確認）: data.txt は変更されない ---
    std::cout << "=== [ selftest data unchanged ] ===" << std::endl;
    {
      std::ifstream ifs(gLoadedPath, std::ios::binary);
      std::string fileBytes;
      if (ifs.is_open()) {
        ifs.seekg(0, std::ios::end);
        const std::streamoff sz = ifs.tellg();
        fileBytes.resize(static_cast<size_t>(sz));
        ifs.seekg(0, std::ios::beg);
        ifs.read(&fileBytes[0], sz);
      }
      const bool rtFinal = stPass(verifyRoundTrip(gDocument).exact, "data unchanged");
      const bool bytesSame = stPass(ifs.is_open() && fileBytes.size() == gLoadedBytes &&
                                   fileBytes == gDocument.raw && !gDocTerminatorPatched, "data unchanged");
      std::cout << "data_unchanged load=" << gLoadedPath << " baseline_bytes=" << gLoadedBytes
                << " disk_bytes=" << fileBytes.size() << " match=" << (bytesSame ? 1 : 0) << std::endl;
      std::cout << "round_trip_final=" << (rtFinal ? "EXACT OK" : "MISMATCH") << " terminator_patched="
                << (gDocTerminatorPatched ? 1 : 0) << std::endl;
      std::cout << "write policy: data.txt に書き戻さない（selftest では legacy write-back は走らない・"
                << "legacy は ../data.txt load 時の運用のみ） backup_on_quit=" << (gBackupOnQuit ? "on（B）" : "off（既定）") << std::endl;
    }
}

bool runSelftest() {
    // Prompt 6・要件 7: DDM_SELFTEST は viewer 検証に集中する。DDM_MODE=game で起動した場合も
    // selftest は viewer に復帰して全 phase を検証する（game 分岐の動作・H 往復は
    // game-mode phase で確認する。Document / data.txt は変わらない）。
    if (gUiMode == UI_GAME) setUiMode(UI_VIEWER);
    printRoundTripReport(gDocument);
    printDocumentToStdout(gDocument);

  phase_roundtrip_baseline();
  phase_selection();
  phase_family();
  phase_constraint();
  phase_editor();
  phase_props();
  phase_font();
  phase_palette_audit();
  phase_game_mode();
  phase_data_unchanged();

    // Prompt 6・要件 8: 各 phase の結果を集約し、失敗は "FAIL <phase>" を stdout に出して
    // SDL_APP_FAILURE で終わる（全 phase pass のとき SELFTEST: ALL PHASES PASS + SDL_APP_SUCCESS）。
    // 既存の「期待値実行時計算」pattern は維持し、数をハードコードしない（data.txt は不変）。
    if (gSelftestFailures.empty()) {
      std::cout << "SELFTEST: ALL PHASES PASS" << std::endl;
      return true;
    }
    for (const std::string &f : gSelftestFailures) std::cout << "FAIL " << f << std::endl;
    std::cout << "selftest exit: FAIL phases=" << gSelftestFailures.size() << std::endl;
    return false;
}
