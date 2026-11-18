# DDMEditor GUI プロンプト v2（Prompt5 完了・Prompt 6 実行準備版）

セッション1が出力した Prompt1〜Prompt6 のうち、**Prompt1 はセッション2で main.cpp に反映済み**、
**Prompt2（family viewer）は session-42e6def2 で実装・実機検証済み**（2026-11-17、selftest 全 phase pass・exit=0）、
**Prompt3（constraint viewer）は 2026-11-17 のセッション（DDMEditor repo）で実装・実機検証済み**（selftest の
selection / family / constraint / font 全 phase pass・exit=0・round-trip EXACT 維持）、
**Prompt4（editor add）は 2026-11-18 のセッション（同 repo）で実装・実機検証済み**です（selftest の
selection / family / constraint / **editor** / font 全 phase pass・`editor_phase_all_flags=1`・exit=0・
round-trip EXACT 維持・data.txt 不変）。**Prompt5（props menu / slot options）は 2026-11-18 の session-213fbdb3 で
実装し、同 session がトークン切れ（turn end reason=max-tokens）で中断したため、継続セッション（同 repo）で
selftest の props phase の残り fail を修正して完遂・実機検証済み**です（selftest の selection / family /
constraint / editor / **props** / font 全 phase pass・`props_phase_all_flags=1`・exit=0・
round-trip EXACT 維持（props phase 後の MISMATCH も undo 修正で EXACT に復旧）・data.txt 不変）。
そのため Prompt6 は「新規実装」ではなく「既存 codebase（Prompt 2〜Prompt 5 の実装を含む）への統合・最終安定化」の
指示です。**次回予定されている実行は Prompt 6（統合・round-trip 検証・最終安定化 + ゲームモード共存）**です。
本ドキュメントは Prompt 5 実行の結果を踏まえ、Prompt 6 の前提・共通制約・§3 の step 見積を現行 main.cpp の
実済状態に合わせて更新した準備版です。
**Prompt 4 以降のプロンプトには「セッション終了後（selftest の結果確定後）に ./prompts_v2.md の結果節・次プロンプトの
前提・step 見積を更新する」指示を出力形式に挿入済みです**（write は main.cpp のみ、prompts_v2.md の更新は
セッション終了後の追動作、data.txt は書き換えない）。

---

## 1. 現行 main.cpp の実済部分（セッション2 + Prompt 2 + Prompt 3 実行の結果サマリ）

| 区分 | 現行 main.cpp の状態 |
|---|---|
| Strict Document model | 実装済み。`RawLine`(text/terminator/kind/elementIndex), `ElementRecord`(load-time prop を保持・`dirty` は未使用), `Document`(raw 全バイト列), `loadDocument`, `parseElementLine`(quoted string + hex, greedy 末尾锚定点), `serializeDocument`(未編集行は原文・dirty 行のみ再生成), `verifyRoundTrip` / `RoundTripReport` |
| データファイル | **dd.txt は削除済み。現行は `./data.txt`（`../data.txt` フォールバック）**。プロンプト中の "dd.txt" はすべて `data.txt` に読み替える。selftest 実行（`./data.txt` load）は data.txt を変更しない（legacy write-back は `../data.txt` を load した legacy 運用でのみ動作）。Prompt 3 セッション中に外部の legacy write-back で data.txt が変わる観測あり（22:45・93行/4688B → 92行/4687B・blank line 消失・scenes {0:10,1:35,2:31,15:16}・required=11/excluded=0/ghost=0 は不変）。変化後の再実行でも全 phase match=1・EXACT・exit=0（期待値が実行時計算のため変更に追従する）。**数をハードコードしない・期待値は実行時に prop 直接パースで算出**が必須ルール |
| Graph model | 実装済み。`buildGraph` が node を `(scene,id)` key で作り、`EDGE_PARENT / EDGE_CHILD / EDGE_REQUIRED_READ / EDGE_EXCLUDED_READ_SLOT1 / EDGE_EXCLUDED_READ_SLOT2` の `edges`、`parentAdj / childAdj / childLinkAdj / requiredReadAdj / excludedReadAdj`、`roots`(parent==0), `newRootTargets`, `scenes`, `warnings`, `findNode`, `nodesInScene`, `smallestNonZeroScene`(0 は newScene sentinel なので除外) を構築する。dangling は `GraphEdge{from=-1|to=-1, dangling=true}` と `GraphNode.danglingParent/danglingChild` で保持 |
| VIEW_GENERAL | 実装済み。`layoutScene`(column=parent chain depth, row=id 順), `chainDepth`(cycle 打ち切り), `fitViewToScene`, `drawNodeBox`(blueprint 風箱・port・badge・quote/onetime ストライプ・hasRead mark), `drawBezierEdge`, `drawGraphEdges`(parent=sapphire, child=peach), `worldToScreen/screenToWorld`, `pickNode`, `zoomAt`, `switchScene`, pan/zoom。**Prompt 2 で座標分離済み**: `layoutScene` は `wx/wy` のだけを書き、描画・`pickNode`・`fitViewToScene` は `nodePos()` が active layout を読む |
| VIEW_FAMILY | 実装済み（Prompt 2）。`layoutFamily`(multi-root forest・weakly-comp hub・決定性), `drawFamilyView`(階層=sapphire / explicit child=peach / 矛盾=yellow), `drawFamilyTreeEdge`, per-mode 座標 `familyWx/familyWy/famDepth` |
| VIEW_CONSTRAINT | **実装済み（Prompt 3）**。`drawConstraintView` が constraint edges を active layout（既定=general）に**重畳描画**（required=teal / excluded slot1=red / excluded slot2=yellow・中段ラベル `drawGuiLine`・`GraphEdge.to==-1` の ghost を overlay0 破線枠 + id badge で描く）。`ConstraintEdgeCounts` / `countConstraintEdges(scene)` が kind 別・ghost 別 count（HUD/selftest 共通経路）。constraint 専用 layout は**作らず**、既定 active layout は general のまま（要件 2 の「既定」を選択） |
| HUD / keys | 実装済み。`drawHud` が mode 名・scene list・node/edge/root 数・doc stats・round-trip 状態（**baseline diff の added/edited 行を含む**）・font 状態・selected props(`describeElement(nodeIndex, view)` の view 別ソート・general/family/constraint/**EDITOR(props) variant 実済（Prompt 5）**)・export 本文・legend を描く。constraint mode では 3色の意味 + ghost 凡例 + 「family 矛盾 yellow と excluded slot2 yellow は別意味」明記 + `countConstraintEdges` の kind別/ghost別 count + `excluded_edges=0` info を出す。**EDITOR mode では editor add の state（stage / id候補 / parent / child / sync runtime / warning / keys 案内）を出す（Prompt 4）**。general/family の legend 行は "C constraint: implemented / EDIT add: implemented（Prompt4・key 4）" に更新済み。キー: `1-4`=mode 切替（1/2/3 は editor の add flow を取消），`V`=graph toggle, `P`=export panel, `X`=quit export, `E`=dump, `R`=round-trip report, `D`=selected dump, `L`=再 layout+fit, `0`=reset, `+/-`/wheel=zoom, `[ ]`=scene 切替, 矢印=pan, `ESC`=選択解除（**editor の add state 中は取消優先**）, `Q`=quit。既存 runtime キー `TAB/F/O/I/A/U/M/Y/N` はそのまま（挿入モード中は graph 入力を遮断。**EDITOR mode では editorMode ガードで遮断し、GUI 用キー ENTER/BACKSPACE/Y/N/G を使う。**props panel open のとき
TAB/ENTER/SPACE/+/-/Y/N/U/G/ESC は props 経路が優先される（add flow の ENTER/Y/N/G・graph の +/- zoom・ESC 選択解除は
panel open 時だけ遮断・1-4/V/P/X/E/R/D/L/0/[ ]/矢印/Q は共通経路のまま・mode 切替キーは editorLeaveModeIfNeeded が
panel を閉じる・props は idle のときだけ開くので add flow と同時状態にならない）**） |
| EDITOR add | **実装済み（Prompt 4）**。`EditorState`（gAppMode と別 state・stage machine: idle→dialogue→quote→onetime→parent→child→commit）, `nextFreeId(scene)`（nodesInScene の使用 id と衝突しない最小 id・>=1・>0xFF 不可は -1）, `composeEditorProp`（buildProperty と同じ bit 配置 + slot1 child value・child なしは index0/value0 の data.txt 慣習）, `editorValidateAdd`（空 dialogue / 自己 parent / 別 scene の parent / child option の present チェック・invalid は追加禁止）, `editorCommitAdd`（正格式の RawLine + dirty=true の ElementRecord を末尾に追加 → `buildGraph` 再構築 → `applyActiveLayout` → 新ノードを selectedNode、新ノードは (scene,id) で決定的に findNode、新 id は commit 後の nextFreeId で連続追加でも重複しない）, `editorCancelAdd`（ESC・Document 不変）, `editorRestoreToBaseline`（selftest 検証後の baseline 復元・terminator patch 復元）, `drawEditorOverlay` の preview panel（id/parent/child/scene/flags/slot1Index/slot1 value/final prop hex/追加 raw line の予定/baseline 行数→document 行数/candidate ids 列挙・kMC の色: sky=進行 / text=dialogue / subtext=値 / green=commit ready / yellow=warning）, runtime 同期トグル `G`（既定 off・on のとき commit で containerA/B に反映・legacy write-back 経路は触らない）, editor phase selftest（期待値は実行時 prop 直接パース） |
| EDITOR props | **実装済み（Prompt 5・session-213fbdb3 中断→継続セッションで完遂）**。書き込み helper（`setPropBit`/`clearPropBits`/`setPropField`・`setSlotIndex`/`setSlotValue`・合成は read helpers の逆 bit 配置に厳密一致・`editTimerSetterValue`/`editTimerTargetValue`/`editEyelevelToggleValue` は sub-bit だけ編集し vacant 他 bit を保持）、`PropsFieldDef`/`kPropsFields`（22 fields・表示順 = tabs/sections: basic flags → id/parent/scene → slot0..3）、`propsOptionLabel`（value 0 は none・data.txt 慣習）、`PropsEditState gProps`（nodeKey・cursor・baseSlotIndex・undo stack・warningTotal・`kPropsUndoMax=64`）、ノード選択（クリック・右クリック・editor idle のみ・add flow と排他）で panel を開く、キー `TAB`=次項目（循環）・`ENTER`/`SPACE`=トグル（bool 反転・option cycle は最後の次は none=元 index+value 0・enum cycle・uint 最低 bit 反転）・`+/-`=値（clamp）・`Y/N`・`U`=undo・`G`=graph 再構築のみ・`ESC`=panel 閉（選択解除より優先）、`propsCommit`（undo entry 積む → prop 更新 + dirty=true + 正格式 raw line 再生成 → `buildGraph` → `applyActiveLayout` → 選択を編集後の (scene,id) で復元 → fit → `gRoundTrip` 更新・runtime 同期なし）、`propsValidateEdit`（範囲外 clamp + warning・現 scene の duplicate id は reject・id 系 target 不在は warning + constraint の ghost で確認・option 非立ての timer/eyelevel sub field 編集は inert warning）、`describeElement` の EDITOR variant（props 表示・他 view の出力順不変）、HUD の props 行・凡例、`propsUndo`（現在の nodeKey で対象 record を解決 → prop・line text・dirty を完全復元 → 再構築 → nodeKey/選択を prevProp の key へ）、props phase selftest（期待値はすべて実行時計算） |
| **未実装（後フェーズ hook）** | timer/unlock/eyelevel の制約 edge 可視化は未実装（Prompt 3 の default 範囲・必要なら Prompt 6 以降で drawConstraintView に kMC の別意味色で追加）。dialogue の編集は未実装（props panel は dialogue を編集しない・別 panel）。runtime の id 採番 `containerB.size()+1` はそのまま（editor add は `nextFreeId` を使う） |
| 日本語描画 | 実装済み。FreeType を `dlopen`、フォント探索(`DDM_FONT_PATH` 上書き可)、`guiGlyph` の texture cache、`drawGuiText`/`drawGuiLine` が唯一のテキスト描画経路、freetype/日本語フォントが無い環境のみ ASCII `?` fallback(`asciiPreview`) |
| 配色 | **Catppuccin Mocha 準拠**。`kMCBase/kMCMantle/kMCText/kMCSubtext1/kMCSubtext0/kMCOverlay2/1/0/kMCSurface0/1/kMCRed/Green/Yellow/Peach/Mauve/Teal/Sky/Sapphire/Blue/Lavender` + `mcR/mcG/mcB` + `setSolid(r, uint32_t hex)` + tint fill(`kMCFillTemplate/kMCFillEmpty`)。数値 RGB リテラルは全排除済み。Prompt 3 で constraint 色は kMCTeal/kMCRed/kMCYellow の定数使用（意味体系: required/excluded slot1/excluded slot2）。コントラスト実測値: dialogue `#cdd6f4` on surface0 = 8.69:1, HUD 見出し 12.14:1, badge 8.08:1, flags 4.45:1 |
| 検証基盤 | 実装済み。`DDM_SELFTEST=1` + `SDL_VIDEO_DRIVER=dummy` で round-trip / export / selection+zoom/pan 往復 / glyph rasterize / 実描画→`SDL_RenderReadPixels`→`build/viewer_frame.bmp` / bright pixel ヒストグラム / `switchScene(+1)` を stdout に出力。**Prompt 2 で family phase・Prompt 3 で constraint phase・Prompt 4 で editor phase 追加済み**: editor は各 scene の nextFreeId 一致（使用 id 集合との衝突チェック）・空 dialogue 拒否・ESC 取消で Document 不変・追加行の正格式/再パース/child なしは getPropChild()==0・graph nodes/edges/roots の差分・family/constraint への出現（追加行の合成 prop を含む期待値で照合）・RoundTripReport の added/edited・runtime 同期 off・検証後の baseline 復元（EXACT）をすべて実行時計算の期待値で照合する。**Prompt 5 で props phase 追加済み**: helpers 往復（全 fields・各 option 1回）・timer/eyelevel の合成・分解一致・
panel open（click/right-click）・describeElement props variant・キー経路（TAB/ENTER/Y/N/U・循環）・programmatic edits
O1..O16（id clamp/duplicate reject・parent・scene 移動・hasRead・required/excluded の present/ghost・slot 排他・
timer/eyelevel 部分編集・inert warning）・validation warning 総数の実行時期待値照合・constraint counts の全 scene
match（ghost 含む）・G 再構築で runtime 不変・props frame の実色・全 undo で baseline 完全復元（EXACT）をすべて
実行時計算の期待値で照合する。現行結果: `RESULT: EXACT MATCH`, font READY, 全 phase pass（`editor_phase_all_flags=1`・`props_phase_all_flags=1`）, exit=0（props phase 後も round-trip EXACT・全 undo 後 EXACT・data.txt 不変） |

セッション2で判明した運用上の制約（以後のプロンプトに織り込みました）:
- **ccache は `~/.cache` に書けず sandbox で失敗する** → ビルドは `CCACHE_DIR=$PWD/build/.ccache cmake --build build` を使う。
- **glyph texture の byte order**：little-endian では `SDL_PIXELFORMAT_RGBA8888` は packed `0xRRGGBBAA`（メモリ列 A,B,G,R）を指すため、R,G,B,A に書いた buffer が ABGR として読まれ色 mod が壊れた。正しくは **`SDL_PIXELFORMAT_RGBA32`**。pixel readback は driver 依存なので **`SDL_ConvertSurface(..., SDL_PIXELFORMAT_RGB24)`** から読む。
- write ポリシー: **main.cpp のみ書ける。data.txt は書き換えない。**（旧 write-back は `../data.txt` を load した legacy 運用でのみ動作中。quit 時の `backupData()` は `data.txt.bak` を上書きする）

### Prompt 2 実行（session-42e6def2・2026-11-17）の結果 — 現行 main.cpp に追加済み

条件どおりに完遂（write は main.cpp のみ・data.txt 書かない・kMC 定数・drawGuiLine 経由・新ファイルなし）。
現行 main.cpp の該当位置（**Prompt 3 の編集でさらにずれている。identifier で探すこと**。下記 Prompt 3 サマリに
現行 snapshot 位置あり）: GraphNode family fields / `describeElement` / `layoutFamily` / `nodePos` /
`applyActiveLayout` / `drawFamilyView` / legend / `drawGraphOverlay` / selftest family phase。

実装内容:
- **per-mode layout cache**: `GraphNode` に `familyWx/familyWy/famDepth` を追加（famDepth=-1 は未配置）。
  `layoutScene` は `wx/wy` のだけ、`layoutFamily` は family 座標のだけ書く。描画・`pickNode`・
  `fitViewToScene` は `nodePos()` で active layout を読む。`switchScene`/`L`/キー1-4 は `applyActiveLayout()` を呼ぶ。
  現行 VIEW_CONSTRAINT / EDITOR の既定 active layout は general のまま（nodePos は wx/wy を返す）。
- **`layoutFamily(scene)`**: roots(parent==0)+danglingParent を root 扱い（id 昇順）で multi-root forest を
  横に並べる top-down 樹。`childAdj` で subtree 展開（親上部・child 下部・同 depth 横）、`childLinkAdj` はツリー外。
  cycle は path/visited で打ち切り、どの root からも届かない残り component は weakly-connected に group し、
  child 数最大・同数 id 最小の node を hub root にして 1 tree 化（scene 0xf の self-parent hub 0xff を検出）。
  depth 上限 64・id 昇順ソートで決定性・全 node を配置する。
- **`drawFamilyView(r)`**: 階層 edge = sapphire（`drawFamilyTreeEdge`: vertical bezier + chevron）、
  explicit child(slot1) = peach（`drawBezierEdge` の別経路）、矛盾 edge（child target の parent field が別ノード）=
  yellow overlay、構造ノードは既存 tint/overlay0 枠、scene filter 維持、newRoot は mauve "newRoot→" badge。
- **`describeElement(nodeIndex, view)`**: VIEW_FAMILY variant 実済（id/parent/child/scene/flags/newRoot/
  newScene/family_depth/adj/dangling を先頭、required/excluded は 1 行最小表示）。HUD は
  `describeElement(gSelectedNode, gViewMode)` を呼ぶ。
- **HUD legend**: family 凡例 + forest/placed 実測行 + "R general: active. F family viewer: implemented"。
- **selftest family phase**: general 座標 intact・`layoutFamily` 決定性・family 座標での pickNode・
  forest 数の prop 直接パース照合（実行時計算 DSU 規則）・family frame→RGB24 実色ヒストグラム。全 pass。

session-42e6def2 で判明した追加の運用上の制約（Prompt3〜6 に織り込み済み）:
- **data.txt はゲーム側の legacy write-back で実行のたびに変わる**（過去に 76→93 行・内容変化を観測。
  `./data.txt` を load する viewer 運用では write-back は走らないが、いずれにせよ **検証の期待値は必ず
  実行時に現行 gDocument の prop を直接パースして算出する（family/constraint phase の pattern を踏襲）。
  数をハードコードしない。**）
- active layout へのアクセスは `nodePos()` / `applyActiveLayout()` に一本化された。新しい per-mode layout は
  GraphNode にその mode の座標 field を追加し、layout 関数はその座標のだけ書き、これら 2 関数に登録する
  （他 mode の座標を壊さない）。
- fit の下限 zoom = 0.2: wide forest では badge 文字はアンチエイリアス混色になる。実色判定はエッジ線・枠・
  mark 側の kMC 実色で行う（ヒストグラムの PASS 条件を glyph 文字色にしない）。
- data.txt の実測スナップショット（2026-11-17 12:22・93 行 / 4688B）: scenes {0:10, 1:35, 2:31, 15:16},
  required(slot0 val!=0)=11・**excluded(slot1/slot2 val!=0)=0**・child 矛盾 edge = line 12/33。
  Prompt 3 実行時の再実測でも required=11 / excluded=0 / ghost=0 で一致（データは同一・上記の通り実行時計算で照合）。
- session-42e6def2 の 2 番目のターン（§3 の更新依頼）は応答なく終了し、プロンプトファイルの更新は
  行われなかった。この Prompt 3 セッションで本更新を実施し、**以後は各実装セッション終了後に
  プロンプトファイル更新をプロンプト本体の指示として組み込む**（Prompt 4〜6 の出力形式に挿入済み）。

### Prompt 3 実行（本セッション・2026-11-17）の結果 — 現行 main.cpp に追加済み

条件どおりに完遂（write は main.cpp のみ・data.txt 書かない〔selftest 実行は write-back を走らせない。
2回実行で 4688B/md5 不変。22:45 に外部 legacy write-back で 92行/4687B へ変更された〕・kMC 定数のみ・
drawGuiLine 経由・新ファイルなし・既存 runtime 入力不変）。現行 main.cpp の該当位置（この snapshot・
Prompt 4 の編集で再びずれる。**identifier で探すこと**）:
- `describeElement` は L1998 付近、**VIEW_CONSTRAINT variant は family variant の直後（L2031 付近）**。
- `ConstraintEdgeCounts` L2627 付近 / `countConstraintEdges` L2633 付近 / `drawDashedRect` L2653 付近 /
  `drawConstraintView` L2667 付近。`drawEditorOverlay` の空 stub はその直後（L2767 付近）。
- `drawHud` の constraint legend/count は L2864–2880 付近。general/family legend の C 状態行も更新済み。
- selftest constraint phase は family phase の直後（L3540〜L3811、font phase の直前）。

実装内容:
- **constraint 専用 layout は作らない（Prompt 3 要件 2 の「既定」を選択）**: CONSTRAINT の既定 active layout は
  general のまま、`drawConstraintView` を active layout の座標に重畳描画する。`nodePos()/outputPort/inputPort` を
  通るため pick・fit は壊れない（往復検証で実測確認）。optional の `layoutConstraint` は未実装（必要なら次フェーズ）。
- **制約 edge 描画**: required=`kMCTeal` / excluded slot1=`kMCRed` / excluded slot2=`kMCYellow` の bezier
  （既存 `drawBezierEdge` を各色で再利用・統合せず複数描く・self constraint/cycle も描く・scene filter 維持）。
  edge 中段に `requiredReadId:0x…` / `excluded(slot1):0x…` / `excluded(slot2):0x…` を `drawGuiLine` で各色描く。
  ラベル・ghost badge のテキストは `kNodeW*zoom>=44` / `kGhostW*zoom>=44` のゲートで描画（drawNodeBox の badge
  テキスト省略と同じ方針。low zoom では線だけ描く）。
- **ghost 描画**: `GraphEdge.to==-1` の target を「scene内に存在しない id」として、layout 右端の dedicated lane
  （`laneX = max(nodePos().x) + kNodeW + kGapX`・実ノードと重ならない・決定性）に **`kMCOverlay0` 破線枠**
  （新 helper `drawDashedRect`）+ id badge「（scene内に存在しないid）」で描く。同一 id を指す複数 edge（異 kind・
  duplicate）は 1 box に収束、id 昇順で並べる。badge 色は指している edge の最小 kind（required<slot1<slot2）。
  ghost box は fit の outside（fit は node の座標だけ読む）に置かれることがある → edge は source ノードから
  視域内に伸びて PASS 判定（edge 実色 + count）に支障なし。ghost box のピクセル検証は行わない。
- **HUD**: constraint mode で kind 別・ghost 別 count（`countConstraintEdges` は edges の `to==-1` を kind 別に数える。
  ノード側 `requiredReadNodes/excludedReadNodes` の -1 は present count から除外）+ 3色の意味 + ghost 凡例 +
  「excluded slot2 の yellow と family 矛盾 yellow は意味が違う」明記 + `excluded_edges=0 (none in data.txt)` info。
  旧 "C / EDIT: phase hook" 行は "C constraint: implemented / EDIT: hook (Prompt4)" に更新。
- **`describeElement` VIEW_CONSTRAINT variant**: node/prop(raw hex)→ required/excluded の slot index・raw value・
  target が present（同 scene 実在）か ghost かを先頭、値 0 は `-> none`、id/parent/child/scene/flags/newRoot/newScene は
  最小表示、adj は present/ghost を分離して count。他 view（general/family）の出力順は不動。
- **selftest constraint phase**（family pattern 踏襲・数値ハードコードなし）: 各 scene の kind/ghost 数を
  prop 直接パース（duplicate は first-wins・buildGraph と同じ扱い）の期待値と照合 → 全 scene match=1・
  present の node idx 解決確認（`present_resolve_ok=1`）; totals（required=11 / excluded=0 →
  `excluded_edges=0 (none in data.txt)` info）; mode 1→2→3→1 往復で general 座標・選択・fit が intact
  （`constraint_fit_matches_general=1`・同 scene の fit は bitwise に同一）; constraint（general layout のまま）での
  pickNode match=1; capture scene は現行 scene に required が 0 件なら required>0 の最小 scene を選ぶ
  （現行は default scene 0x1 に 7 件あるのでそのまま）; 実描画→`build/viewer_frame_constraint.bmp`→RGB24 histで
  **teal PASS（required≥1）・text/subtext1 出現確認**、red/yellow は expected edges と比較する info（PASS 条件外）;
  capture 時に present-required ノードを决定的に選択して HUD に constraint variant の実描画を確認
  （subtext1 行数増で検証）; 往復後 round-trip EXACT・capture 後に scene/mode を復元（以降の font phase・
  switchScene は不変）。

実測結果（本セッション・2026-11-17）: selftest 実行では data.txt は不変（4688B・`./data.txt` 運用では
legacy write-back が走らないことを確認）。22:45 に外部 legacy write-back で 93行/4688B → 92行/4687B の
変更を観測（blank line 消失・scene/node/constraint の数は不変）。変化後の再実行で **全 phase match=1・
EXACT・exit=0**（実行時計算の期待値が変更に追従。ハードコード禁止が必須であることを実証）。scenes {0:10,1:35,2:31,15:16}。
required edges: 0x0=4 / 0x1=7 / 0x2=0 / 0xf=0（total 11）、excluded=0、ghost=0 → 全 scene `match=1`・
`present_resolve_ok=1`。往復 flag 全て 1。constraint frame: teal=2579（graph 領域の独立ピクセル probe でも
teal 2523 を実線として確認）、red=1741（**excluded edge 0 件なので red の出現は quote ストライプ由来** —
excluded 由来と誤解釈しないこと）、yellow=0（expected 0 と一致）、text/subtext1 出現。全 phase pass・exit=0・
build warning 0・変更は main.cpp のみ。

本セッションで判明した追加の運用上の制約（Prompt 4 以降に織り込み済み）:
- **制約表示は graph 走査一本**: `drawConstraintView`・HUD count・selftest はすべて `gGraph.edges` を読むため、
  `buildGraph` を再構築すれば制約 edge・ghost・HUD count・constraint variant の present/ghost 表示は**即時更新**される。
  Prompt 4/5 で graph を再構築しても constraint/family 側に追加の配線は不要（新ノード・新 edge・編集された target は
  自動で現れる）。Prompt 4 の検証は `countConstraintEdges` + prop 直接パースの期待値（追加行の合成 prop を含める）で照合する。
- `countConstraintEdges(scene)` は kind 別・ghost（to==-1）別 count の唯一の shared helper。HUD/selftest/将来の
  editor validation からこれを使う（ノード側 list の -1 を present count に混ぜない）。
- `describeElement` の variant は general/family/constraint が実済。新しい props 表示（Prompt 5 の menu 表示順序など）は
  同関数に variant として追加する（別機能の新設・他 view の出力順変更は禁止）。
- **BMP の生バイト列は BGR・行は bottom-up**（`SDL_SaveBMP` の保存形式）。selftest のヒストグラムは
  `SDL_ConvertSurface(RGB24)` の surface から読むので RGB で正しい。外部ツールで BMP を直接読む場合は
  BGR/bottom-up で読むこと（本セッションの graph 領域 probe は BGR で一致した）。
- selection は mode 切替では壊れない（key 3/4 への切替は `gSelectedNode` を触らない）。ただし graph 再構築で
  index が変わるため、Prompt 4/5 の再構築後は (scene,id) key で復元する（Prompt 4 要件 5 に明記済み）。
- selftest の capture は frame に lavender 選択を出す（family phase の capture pick と同様の意図的動作）。
  「選択なしの frame」が必要な検証は明示的に gSelectedNode=-1 で capture する。

### Prompt 4 実行（本セッション・2026-11-18）の結果 — 現行 main.cpp に追加済み

条件どおりに完遂（write は main.cpp のみ・data.txt 書かない〔selftest 2回実行で 4687B・md5 不変〕・kMC 定数のみ・
drawGuiLine 経由・新ファイルなし・既存 runtime 入力不変・runtime 同期は既定 off）。現行 main.cpp の該当位置
（この snapshot・main.cpp 4901 行・Prompt 5 の編集でずれる。**identifier で探すこと**）:
- `RoundTripReport` の baseline diff fields は L363 付近（`addedLines/editedLines/baselineLines/diffIsAddedOrDirtyOnly`）。
  `verifyRoundTrip` の baseline 行単位照合は L383 付近（`splitKeepText` を追加。firstDiffLine の意味は不変）。
- `enum EditorStage` / `struct EditorState` / `gEditor` / `gEditorTextInputActive` / `gDocTerminatorPatched` は
  `int gSelectedNode = -1;` の直後（L777〜L800 付近、namespace の GUI state 節）。
- editor のヘルパー群（`composeEditorProp` L2875 / `nextFreeId` L2890 / `editorStageName` / `editorStartText`/
  `editorStopText` / `editorValidateAdd` L2928 / `editorBeginAdd` / `editorCancelAdd` / `editorLeaveModeIfNeeded` /
  `editorBackspaceDialogue` / `editorEnterKey` L2999 / `editorYN` / `editorCommitAdd` L3065 /
  `editorRestoreToBaseline` L3126）は `drawConstraintView` の直後・`drawEditorOverlay` の直前。
  `drawEditorOverlay`（preview panel・L3145）は旧空 stub の位置。`drawHud` の round-trip added/edited 行と
  EDITOR 凡例は L3350〜L3375 付近（constraint/general の legend の EDIT 状態行も更新済み）。
- `handlePointerEvent` の parent/child 候補選択（L3420 付近）と、`SDL_AppEvent` の editor ESC 優先・
  editorMode ガード・editor キー（L3679 / L3707 / runtime 遮断 L3730 付近）、TEXT_INPUT の editor 受け（MODE_NORMAL 節の直前）。
- selftest editor phase は constraint phase の直後・font phase の直前（L4392〜L4755、font phase の直前）。

実装内容:
- **EditorState（gAppMode は使わない・別 state）**: stage machine `idle → dialogue → quote(y/n) → onetime(y/n)
  → parent → child → commit`。dialogue は SDL TEXT_INPUT をそのまま+=（UTF-8・空白保持）、BACKSPACE は
  UTF-8 の codepoint を消す（pop_back のバイト削除で multibyte を壊さない）。空 dialogue は Enter 不可（拒否）。
  Enter 確定・ESC 取消（add state 中は取消優先、それ以外は通常の選択解除）。ENTER は quote/onetime では no として
  扱う（safest default）。child の ENTER は「現在値で commit」、N で child none に戻す（slot1 option の排他選択）。
  Start/Stop は Start/Stop の対称ペア（`gEditorTextInputActive` で二重 Start/二重 Stop を防ぐ）。mode 切替（key 1/2/3）
  は add flow を取消する。gViewMode が EDITOR でないとき GUI 入力（TEXT_INPUT・editor キー）は遮断する。
- **id 採番 `nextFreeId(scene)`**: nodesInScene の使用 id 集合から未使用の最小 id（>=1・id 0 は sentinel 慣習で除外、
  >0xFF は -1 を返し追加不可）。runtime の `containerB.size()+1` は流用しない（scene 無関係・重複する）。
  ユーザー入力 id の override は optional なので未実装（auto のみ。衝突は nextFreeId 側で起きない）。
- **parent / child 選択 UI**: ノードクリックで候補を立てる（parent stage: 自己 id 禁止・id 0 は root/no parent として
  許す・別 scene の id は validation で追加禁止。child stage: id 0 は none 扱い）。HUD と preview panel に
  selected scene の id を列挙し、現在の候補に *P / *C マーカーを出す。child ありは slot1Index=0 のまま value に
  child id、child なしは **既存 data.txt の慣習どおり index0/value0**（getPropChild()==0・buildGraph は edge を作らない）。
- **Document 追加**: 正格式 `'dialogue', '0x lowercase16,',` の RawLine(text/terminator "\n"/LINE_ELEMENT/elementIndex)
  を lines 末尾に追加 + ElementRecord{element, lineIndex, dirty=true} を elements に追加。raw が改行で終わらない
  場合は最終行の terminator を "\n" に補う（`gDocTerminatorPatched`。baseline raw は書き換えない・復元で戻す）。
  `buildGraph` 再構築 → `applyActiveLayout()` → 新ノードを selectedNode（(scene,id) で決定的に findNode）→ fit。
  constraint/family 側には追加配線不要（edges 走査で即時出現・selftest で実測確認）。
- **runtime 同期なし（既定）**: containerA/B/displayList は自動同期しない。`G` トグル（editor のみ）が on のとき
  commit で containerA（scene が gScene と一致なら containerB も）に反映し rebuildC/Display/Objective を呼ぶ。
  既定 off・legacy ../data.txt write-back 経路は触らない。
- **preview panel（`drawEditorOverlay`）**: graph 領域右側（HUD の外側）に mantle 面 + overlay0 枠の panel を
  active layout の描画結果の上に overlay で描く。id/parent/child/scene/flags/slot1Index/slot1 value/final prop hex
  （lowercase16）/追加 raw line の予定/baseline 行数→document 行数/current 段階の案内/candidate ids/runtime 同期
  状態を描く。色は sky=進行段階・text=dialogue・subtext=値・green=commit ready・yellow=warning（constraint の
  teal/red/yellow の意味体系とは別 UI、HUD 凡例に明記）。窓が狭い（panel 幅 < 160px）ときは描かず HUD の editor 行で出す。
- **round-trip baseline diff**: `verifyRoundTrip` は現行の `exact`/`firstDiffLine` を維持したまま、serialize 出力を
  baseline raw と行単位で照合して `addedLines`/`editedLines` を算出する（CRLF の CR は本文から除く・終端改行は行に
  含めない）。追加行はすべて「正格式の dirty Element 行」で、編集行は dirty Element 行のみでなければならない
  （`diffIsAddedOrDirtyOnly`）。HUD と printRoundTripReport に added/edited を報告する。

実測結果（本セッション・2026-11-18・data.txt 4687B/md5 e2074fdc… 不変・2回実行で同一）: selftest 全 phase pass・
**exit=0・build warning 0**。nextFreeId は全 scene で prop 直接パースの期待値と一致（scene 0x0→0x07 / 0x1→0x1e /
0x2→0x20 / 0xf→0x01・no_collision=1・match=1）。空 dialogue 拒否=1・ESC 取消で Document 不変=1・text input の
Start/Stop 対称（quote stage で Stop 確認・mode 復帰で active=0）。追加1: id=0x1e parent=0 child なし → 正格式一致・
再パース一致・getPropChild()==0・nodes +1 / edges +0 / roots +1・family に現れる（placed 36/36）・constraint count は
追加 prop を含む期待値と全 scene match=1・新ノード選択=1・baseline diff added=1 edited=0 added_or_dirty_only=1。
追加2: クリックで parent=0x01 / child=0x02 → prop 再パース一致・child edge + childLinkAdj=1・lines+2・roots 増 0・
added=2 added_or_dirty_only=1・constraint 全 scene match=1・runtime 同期 off で container 不変。
editor frame: build/viewer_frame_editor.bmp（1280x760）→ RGB24 histで sky=109 / text=120 / subtext1=1065 出現（PASS）、
green/yellow/lavender は info。baseline 復元後 EXACT・graph/lines/elements/bytes すべて元に戻り（match=1）、
mode 復帰で editor state/idle・text input 対称（=1）。以降の font phase（READY・glyph 描画・viewer_frame.bmp）・
switchScene(+1) は不変。

本セッションで判明した追加の運用上の制約（Prompt 5 以降に織り込み済み）:
- **editor の追加は「末尾追加のみ」**（削除・挿入は作らない）。baseline 照合は index 対応が揃うため行単位 diff で
  added/edited が正確に算出できる。削除を将来作るなら baseline 照合规则の拡張が必要（Prompt 6 で明示）。
- **`splitKeepText` の行数規則**: 終端が改行の行は「改行前の本文」を 1 行として数える（末尾改行後の空要素は作らない・
  改行で終わらない最終行も 1 行）。初版では末尾改行の実行を pop して行数が1ずれて added_or_dirty_only が誤って 0 に
  なった（修正済み）。data.txt は改行で終わる（wc -l 92・92行）ので現行规则は loadDocument の行数対応と一致する。
- **child の ENTER commit**: ENTER は「現在値で commit」（選択なし = none のまま）。強制的に none に_clear_する実装は
  child あり設定を作れなかった（修正済み）。none に戻す操作は N key（slot1 option の排他選択）。
- **editor の追加直後の選択は新ノード**（要件 5）。既存選択の (scene,id) 復元は props 編集側（Prompt 5）で使う
  ヘルパーとして `gGraph.findNode(scene,id)` を経由する（editorCommitAdd では新ノードを select する）。
- **selected element の stdout dump は追加直後 new ノードの raw line を示す**（click/print と same path）。
  editor phase の検証では意図的に新ノード選択・click を使う（frame の lavender は info）。
- **G トグルは editor のみ**で、commit 時のみ container に触れる。トグル往復だけでは runtime state は不変
  （selftest で実測）。legacy の ../data.txt write-back は `./data.txt` 運用では走らない（Prompt 3 の確認と同じ）。

### Prompt 5 実行（session-213fbdb3・2026-11-18・turn が max-tokens で中断 → 継続セッションで完遂）の結果 — 現行 main.cpp に追加済み

実装完遂（write は main.cpp のみ・data.txt 書かない〔selftest 2回実行で 4687B・md5 e2074fdc… 不変〕・kMC 定数のみ・
drawGuiLine 経由・新ファイルなし・既存 runtime 入力不変・runtime 同期は既定なし）。現行 main.cpp の該当位置
（この snapshot・main.cpp 6341 行・Prompt 6 の編集でずれる。**identifier で探すこと**）:
- 書き込み helper（`setPropBit`/`clearPropBits`/`setPropField`・`setSlotIndex`/`setSlotValue`・
  `editTimerSetterValue`/`editTimerTargetValue`/`editEyelevelToggleValue`）は read helpers の直後（L204〜L236 付近・
  現行 read の逆 bit 配置に厳密一致・slot value の sub-bit だけ編集し vacant 等の他 bit を保持）。
- `struct PropsFieldDef` / `kSlotOptionNames0..3` / `kTimerActionNames` / `kTimerOpNames` / `propsOptionLabel` /
  `kPropsFields`（22 fields・`kPropsFieldCount`）/ `kPropsUndoMax=64` / `PropsUndoEntry` / `PropsEditState` / `gProps` /
  `hexDec` / `binaryByte` は L854〜L958 付近（editor state 節・GUI state の namespace 内）。
- `describeElement` の EDITOR variant（`section=basic flags`…`section=slot0..3` の props 表示順・flags hex+binary・
  id/parent/scene と slot value は hex+decimal・timer/eyelevel は分解値・target は present/ghost 表記・
  undo stack 行）は L2342〜L2443 付近（variant 内に新設・general/family/constraint の出力順は不変）。
- props panel の関数群（`propsResolveCurrentNode` / `propsCurrentProp` / `propsOpen`（L3445・idle のみ・baseSlotIndex
  を記録）/ `propsClose` / `propsCommit`（L3470・正格式再生成 + dirty=true + buildGraph + applyActiveLayout +
  (scene,id) 選択復元・`props: edit …` の diff 行を stdout）/ `propsValidateEdit`（L3523）/
  `propsOptionSwitchWarnings`（ROLE_TARGET の probe を合成して再利用）/ `propsApplyFieldValue`（L3596・absolute/delta・
  clamp warning・id duplicate は reject）/ `propsSetBoolField` / `propsToggleField` / `propsSetOption`（absolute position・
  selftest の決定的編集用）/ `propsSetBoolValueCurrent` / `propsNextField` / `propsUndo`（L3737・後述の順序修正済み）/
  `propsRebuildGraph`）は `editorRestoreToBaseline` の直後・`drawEditorOverlay` の直前。menu 行の描画は
  `drawEditorOverlay`（panel open 時・fields を current prop からその場で分解・cursor ハイライト・warning・undo count）、
  `drawHud` に props メニュー行と凡例（general/constraint の legend は props: implemented に更新済み）。
- forward decl（`propsOpen`/`propsClose`/`propsRebuildGraph`・L1378 付近）は namespace 外（global）に置いてある
  （anonymous namespace の閉位置より前に namespace 内 decl があると別物扱いになる・中断セッションで判明・修正済み）。
- `handlePointerEvent` のノードクリック/右クリックで panel を開く（L4187/L4229 付近・parent/child stage では no-op）。
  `SDL_AppEvent` の props キー優先 routing（L4429〜L4444 付近・panel open のとき TAB/RETURN/SPACE/+/-/Y/N/U/G/ESC を
  props に先に流す・mode 切替キーは editorLeaveModeIfNeeded が panel を閉じる）。
- selftest props phase は editor phase の直後・font phase の直前（L5525〜L6193・期待値はすべて現行 gDocument の
  prop 直接パースから実行時に算出・数をハードコードしない）。

実装内容:
- **キー**: `TAB`=次項目（22 fields を循環・決定性）・`ENTER`/`SPACE`=トグル（PF_BOOL 反転・PF_OPTION は option cycle
  for none: 最後の option の次は baseSlotIndex に戻し value 0・PF_ENUM は 0..max cycle・PF_UINT は lowest bit 反転）・
  `+/-`=値 ±1（max 以上/0 未満は clamp + warning）・`Y/N`=現在 field（bool→true/false・uint/enum→max/0）・
  `U`=undo・`G`=graph 再構築のみ（runtime 触らない）・`ESC`=panel 閉（選択解除・add 取消より優先・selection は維持）。
  add flow の ENTER/Y/N/G とは排他（props は idle のときのみ開く・panel open 時は add flow キーを遮断）。
- **fields**: basic flags bit16-19（hasRead/isQuote/isOnetime/vacant・checkbox・load-time prop は保持・runtime の
  hasRead mutation は作らない）、id bits0-7 / parent bits8-15（hex 入力優先・0x00-0xFF）、scene bits20-23（0x0-0xF）、
  slot0..3 option（none/…・index は 2bit で構造上 1 つ＝排他・切り替えは value を保持する）、slot value
  bits32+8*slot（0x00-0xFF・option の意味通り編集・timer は separate controls・flags は hex+binary で bit 相当編集・
  id 系は present/ghost 表記）、timerSetter timerId(value bits0-1)/action(bits4-5・vacant bits2-3 保持)、
  timerTarget operator(bits0-1・0 は「timerTarget なし」)/timerId(bits2-3)/count(bits4-7)、
  eyelevel(bits0-3)/toggleEyelevel(bits4-7・0 は無条件/toggle なし)。「none」は既存慣習（元 index + value 0）に従う。
- **validation**: 範囲外は clamp + warning（HUD に yellow kMCYellow）・現 scene で id の duplicate は reject
  （編集適用なし・warning のみ）・id 系 target（parent/child/excluded/requiredReadId/newRoot）が現 scene に無い場合は
  warning（編集は許す・constraint viewer の ghost と countConstraintEdges で照合できる・配線追加不要）・
  option 非立ての timer/eyelevel sub field 編集は inert warning（value は実効 inert・編集は許す・期待値は
  baseSlotIndex から実行時に算出）。
- **Document update**: `propsCommit` は undo entry（prevProp・prevLineText・prevDirty）を積んでから prop 更新・
  dirty=true・正格式再生成（`'dialogue', '0x lowercase16,',`・追加行と同じ规则）・buildGraph → applyActiveLayout →
  選択を編集後の (scene,id) で復元 → fitViewToScene → gRoundTrip 更新。runtime state（containerA/B・displayList・
  objectiveList・timer）は触らない。`G` は再構築のみ（nodes/edges 差分 0・runtime 不変を selftest で確認）。
- **undo**: stack は node key + previous prop + 元の raw line text + dirty（上限 64・超過で最古を discard）。
  `propsUndo` は**現在の nodeKey（直近の commit/undo の結果の key）で対象 record を解決 → 復元適用 → 再構築 →
  nodeKey・選択を entry の prevProp の key へ更新**する（全 undo で baseline に戻る・round-trip EXACT）。

実測結果（継続セッション・2026-11-18・data.txt 4687B/md5 e2074fdc… 不変・2回実行で同一・build warning 0・selftest exit=0）:
props phase 全 flag pass。`props_helper_roundtrip_all_fields=1`・`props timerSetter/timerTarget/eyelevel compose-decode=1`
（全 fields 往復・各 option 1回ずつ・vacant bits 保持・read の分解と厳密一致・合成 final prop `af2d1277f9362b5a`）。
対象ノード scene=0x1 id=0x1d・panel open click=1 / right=1 / mode_active=1・`variant_ok=1`（props variant 11 行・
general variant 13 行・他 view の出力順不変）。キー経路 tab/enter_toggle/reparse/undo_restore/y/n/tab_wrap すべて=1・
yn_undo_restore=1・キー編集の warning 0=1。programmatic edits O1..O16 すべて ok=1（id→+1 適用・baseline diff edited=1・
id 0x100→0xFF clamp warning・dup reject 経路・parent present/absent（absent は warning 1）・scene 移動 0x1→0x0・
hasRead toggle・required present/ghost/none・slot1 child edge 出現/excluded 排他/absent ghost/none・slot2 timerSetter
部分編集 decode/unlocks/requiredUnlockFlags/excluded・slot3 timerTarget op=2 id=1 count=3/eyelevel 3+5/inert・
options each-set-once=1）。constraint counts は編集後の全 scene で prop 直接パース期待値と match=1（ghost 含む・
none 復元で baseline 期待値に戻る）。`props validation warnings got=5 expected=5 match=1`・`props G graph rebuild ok=1
runtime untouched=1`・`props ESC close panel ok=1 selection kept=1`。props frame: build/viewer_frame_props.bmp
（1280x760）→ RGB24 hist bright=64850・text=103/subtext1=938/sky=151 present=1・yellow=40・lavender=700・teal=56
（info・constraint の意味色との混同は frame の位置で区別できる）。全 undo: `props undo undo_count=55
restore baseline exact=1 match=1`（最終 entry は scene=0x1 id=0x1d prop=0x000000000010021d dirty=0・serialize==raw・
RoundTripReport exact・graph nodes/edges/roots・bytes・lines・elements・runtime container・元 prop/dirty/text すべて一致）、
key 1 で general 復帰・panel 閉・state cleared=1・`props_phase_all_flags=1`・`round_trip_after_props_phase=EXACT OK`。
以降の font phase（READY・glyph 描画）・switchScene(+1) は不変。

観測差异・修正事項（session-213fbdb3 で残った fail を継続セッションで修正・そのまま記載）:
- **value byte と prop bits は別領域**（important）: `getPropTimerTargetOp/Id/Count` 等は prop bits 56-63（slot value の
  位置）を読む helper なので、合成した value byte 単体（例 0xAE）に適用すると 0 が返った。純値の検証は raw bit
  （value byte は prop の slot value bits と同じ配置）で行い、prop に立てた後の分解は setSlotIndex/setSlotValue の
  往復で検証する。compose-decode は「純値の raw bit 照合」と「prop に立てた後の helpers 分解照合」の 2 段で確認する。
- **compose-decode flag は rtOk の再利用だった**（Prompt 5 の検証項目は helpers 往復と timer/eyelevel 合成・分解の
  2 つの別 flag）→ `cdOk` を分離し `props_phase_all_flags` に含めた。
- **general variant の dump 行 index**: general 既定順は rows 0..3 = node/prop/id/hasRead、rows 4..7 = slot0..3 の
  index 行。`slot0 index=` は [4]（[6] は slot2 の行）・想定インデックス [6] が誤りで variant_ok=0 になっていた。
- **O6 の「他 bit を壊さない」照合の mask が逆**：`clearPropBits(p, ~(1ULL<<16))` は bit16 だけを残す値になるため
  before/after が常に不一致になった。bit16 を mask して除く `clearPropBits(p, 1ULL<<16)` で両側を照合する。
- **propsUndo の解決順序 bug（important）**: prevProp の key を**先に**立てて現 graph から対象 node を解決していたため、
  id/scene 変更の直前状態は現 graph に存在せず解決失敗 → その entry は silent に消費され document は復元されず、
  全 undo でも baseline に戻らない（props phase 後の round-trip MISMATCH・mode back の serialize 不一致も同じ原因）。
  「現在の nodeKey で対象 record を解決 → 復元適用 → 再構築 → key を prevProp から更新」の順序に修正し、全 55 entry
  が復元・exact=1 になった。undo は同じ record の状態を順に復元する chain なので、対象は常に**現在の状態の key**で解決する。
- **undo stack 上限 `kPropsUndoMax=64`**: 超過で最古を discard するため、stack が上限まで積むと「全 undo で baseline に
  戻る」が成立しない（本セッションは 55 entry で上限未到達）。Prompt 6 の長時間編集シナリオでは上限を引き上げるか、
  満杯にならない範囲で明示する。
- **anonymous namespace と forward decl**: props の forward decl を namespace 内・定義を global に置くと別物扱いで
  undefined symbol（中断セッションで判明・decl を namespace 外へ移動して解決）。props 関数群は global 側にある。

---

## 2. 全プロンプト共通の制約（v2.3・Prompt6 で共通。Prompt2〜Prompt5 は完了済み）

```text
共通制約（Prompt6 で共通。Prompt2 は session-42e6def2、Prompt3 は 2026-11-17、Prompt4 は 2026-11-18、
Prompt5 は session-213fbdb3・中断→継続セッションで 2026-11-18 に完遂済み）:
- ./main.cpp と ./data.txt のみ read してよい。write は ./main.cpp のみ許可する。
  ただし Prompt6 は、selftest の結果確定後（セッション終了後に） ./prompts_v2.md の
  「実行結果」節（最終状態）・§3 の step 見積の更新を許可する（プロンプトの出力形式に明記）。
  （Prompt4・Prompt5 は完了済み・本ドキュメントの §1「Prompt 4 実行の結果」節・「Prompt 5 実行の結果」節と
   Prompt 6 の前提・§3 は更新済み。）
  失敗があった場合も観測事実を記載する。data.txt は書き換えない（serialize は stdout / HUD export で提示する）。
- data.txt は書き換えない。ゲーム側の legacy write-back で実行のたび内容が変わる環境があるため、
  検証の期待値は実行時に現行 gDocument の prop を直接パースして算出し、数値をハードコードしない
  （family / constraint phase の expected 算出 pattern が手本。duplicate node は first-wins で buildGraph と揃える）。
- 仕様確認の質問をしない。不足は想定して safest な default で実装する。
- 現行 main.cpp の Section 名・identifier を再利用する。Strict Document model / Graph model /
  VIEW_GENERAL 描画 / layoutFamily・nodePos()・applyActiveLayout()（active layout の一本化）/
  drawFamilyView・drawFamilyTreeEdge / drawConstraintView・ConstraintEdgeCounts・countConstraintEdges・
  drawDashedRect / describeElement の view 別ソート（general/family/constraint/EDITOR(props) variant 実済）/
  EditorState・editorStageName・composeEditorProp・nextFreeId・editorValidateAdd・editorCommitAdd・
  editorCancelAdd・editorEnterKey・editorYN・editorRestoreToBaseline・drawEditorOverlay の preview（Prompt 4 実済）/
  props の setPropBit・clearPropBits・setPropField・setSlotIndex・setSlotValue・editTimerSetterValue・
  editTimerTargetValue・editEyelevelToggleValue・propsOptionLabel・kPropsFields・PropsFieldDef・gProps・
  propsOpen・propsClose・propsCommit・propsValidateEdit・propsApplyFieldValue・propsSetBoolField・
  propsToggleField・propsSetOption・propsSetBoolValueCurrent・propsNextField・propsUndo・propsRebuildGraph・
  selftest props phase・props frame capture の pattern（Prompt 5 実済・§1 の Prompt 5 サマリ参照） /
  RoundTripReport の baseline added/edited（verifyRoundTrip の行単位 baseline 照合・splitKeepText の行数规则）/
  HUD / drawGuiLine / kMC パレット / selftest の各 phase を再実装しない。
  （props 編集は editor の stage machine とは別の panel・selectedNode の編集 flow として実済み。
  describeElement の props 表示は同関数の variant に追加済み・別機能は新設しない・他 view の出力順は壊さない。）
- describeElement(nodeIndex, view) の view 別ソートは実済。新しい view の優先表示・フィルタはこの関数に
  variant として追加する（別機能を新設しない。他 view の出力順は壊さない）。
- 新色は kMC の 0xRRGGBB 定数から選ぶ。数値 RGB リテラルを書かない。
  WCAG: 面に対して text/subtext1 は 4.5:1 以上（mocha の公式組合せに揃える）。
  constraint の意味体系（required=teal / excluded slot1=red / excluded slot2=yellow）を他用途と衝突させない。
- テキストはすべて drawGuiLine() 経由で描く。SDL_RenderDebugText は freetype/jp font が
  無い環境の fallback にのみ残す。
- 新しい texture を作る場合は SDL_PIXELFORMAT_RGBA32 を使い、コメントで byte order を明記する。
- pixel readback は SDL_ConvertSurface(SDL_PIXELFORMAT_RGB24) 後に行う。
  （SDL_SaveBMP の BMP 生バイト列は BGR・行は bottom-up。外部で直接読む場合は BGR で読む。）
- 既存の runtime / console 入力（containerA/B/C, displayList, objectiveList, move mode,
  timer, TAB/F/O/I/A/U/M/Y/N, sendProperty, renderConsole）を壊さない。
- 編集・追加は Document と Graph にだけ反映し、runtime state には自動同期しない
  （Prompt6 で同期方針を明示する）。graph 再構築後は active layout を再適用し、選択は (scene,id) key で復元する。
  constraint/family の表示・HUD の countConstraintEdges は edges 走査なので再構築だけで即時更新される。
  （Prompt4 の `editorCommitAdd` と Prompt5 の `propsCommit` がこの pattern の実済み例:
  buildGraph → applyActiveLayout → `gGraph.findNode(scene,id)` で選択を (scene,id) key に復元
  （props 側も selftest props phase で全 match=1 確認済み）。runtime 同期は Prompt4 の G トグル
  （既定 off・commit 時のみ containerA/B に反映）が実済で、props 編集は同期しない・panel open 時の
  `G` は graph 再構築のみで runtime 不変（selftest で実測）。Prompt6 で同期方針を明示する。）
- 新しい per-mode layout を作る場合は GraphNode にその mode の座標 field を追加し、layout 関数は
  その座標のだけ書き、nodePos()/applyActiveLayout() に登録する。mode 切替の往復で他 mode の
  座標・選択・fit が壊れないことを selftest で確認する（family / constraint phase の規則を踏襲）。
- ビルド: CCACHE_DIR=$PWD/build/.ccache cmake --build build
- 検証: SDL_VIDEO_DRIVER=dummy DDM_SELFTEST=1 ./build/DDMEditor
```

---

## Prompt 2 — family viewer: parent/child の階層可視化（実 hook を埋める） ✅ 完了

session-42e6def2-df8e-4e22-8c06-a955990ee054（2026-11-17）で実装・実機検証済み。結果は §1 の
「Prompt 2 実行の結果」節参照。以下は履歴として保持（再実行しない）。

```text
あなたは DDMEditor の GUI 拡張実装者です。

前提（現行 main.cpp に実済、再実装しない）:
- Graph は buildGraph() で parent field の reverse link（parentAdj/childAdj）と
  slot1 child の explicit link（childLinkAdj）をすでに分離して保持している。
  roots（parent==0）、newRootTargets、nodesInScene(scene)、smallestNonZeroScene() も実済。
- VIEW_FAMILY の mode 切替はキー 2 で動作しており、drawGraphOverlay() は
  VIEW_FAMILY のとき drawFamilyView(r) を呼ぶ。drawFamilyView は空 stub。
- ノード描画は drawNodeBox()（content=surface0/blue、dummy=surface1/overlay0 枠、
  template=peach tint、empty=green tint、選択=lavender）、線は drawBezierEdge()
  （parent=sapphire #74c7ec、child=peach #fab387）。HUD は drawHud()/hudLine()。
- layoutScene(scene) は VIEW_GENERAL の column=depth / row=id 順 layout で、
  GraphNode.wx/wy を in-place で書き換える。

このセッションの目標:
drawFamilyView() を実装し、selected scene の階層ツリー（forest）を VIEW_FAMILY で見る。

実装要件:
1. レイアウトを mode 別にする。
   - layoutScene() が共有の wx/wy を壊すため、mode ごとの layout cache を作る。
     GraphNode に per-mode 座標（familyWx/familyWy など）か、mode を key にする
     layout cache を新設し、VIEW_GENERAL の layout を保持したまま VIEW_FAMILY を切り替えられるようにする。
   - pickNode()/worldToScreen()/fitViewToScene() は「現在 active な layout」を読むようにする。
     switchScene() / L キーは active mode の layout 関数を呼ぶようにする。
   - layout は決定性とする（同じ data.txt → 同じ座標）。
2. layoutFamily(scene) を作る。
   - roots を multi-root forest として横に並べる（root ごとの column offset）。
   - childAdj（parent field から作る階層 child）で subtree を展開し、親を上部・child を下部、
     同 depth は横に並べる。childLinkAdj（slot1 の explicit child）はツリーに含めず、
     peach の overlay edge として別経路で描く（階層 edge と区別）。
   - cycle / dangling は chainDepth() と同様に visited で安全に打ち切る。
     danglingParent のノードは root 扱いとしてレイアウトに含める（ badge の par! はそのまま）。
   - newRoot を持つノードは別 badge（例: "newRoot→" + sky/mauve）を表示する。
3. drawFamilyView(r) の描画。
   - 階層 edge: sapphire。explicit child edge: peach。既存色をそのまま使い、legend を更新する。
   - 矛盾 edge（parent field と slot1 child が別ノードを指す）は yellow #f9e2af で overlay する。
   - 構造ノード（dummy/template/empty）は既存 tint / overlay0 枠をそのまま使い、目立たせない。
   - 表示は scene filter（nodesInScene(gViewScene)）を維持する。
4. ノード詳細を family 向けに優先表示する。
   - describeElement() を再利用し、family 関連項目（id/parent/child/scene/hasRead/isQuote/
      isOnetime/newRoot/newScene/adj counts/dangling）を先頭に並べる view 別ソートを加える。
   - required/excluded は最小表示でよい（constraint viewer が扱う）。
   - dialogue は preview のみ（展開プレースホルダは既存の D 出力が扱っている）。
5. round-trip を維持する。
   - family viewer は Document を mutate しない。gRoundTrip は EXACT のまま。
   - layout は runtime state に分離し、raw line / elements に影響しない。
6. HUD legend を更新する。
   - "R/F/C/EDIT: phase hook, not implemented" の行を実装済み状態に書き換える。
   - family の凡例（階層=sapphire / explicit child=peach / 矛盾=yellow）を加える。

検証（selftest に phase を追加する）:
- mode 切替（1↔2）の往復で VIEW_GENERAL の座標が壊れていないこと。
- layoutFamily が決定性であること（2回 layout → 同一座標）。
- family layout 適用後に pickNode が active layout で正しい node を選ぶこと。
- roots/forest 数が data.txt の実測と一致すること（scene 0/1/2/15 で確認）。
- round-trip は EXACT MATCH のまま、bright pixel / 実色（kMC 値）がヒストグラムに出ること。

出力形式:
- main.cpp を編集して適用する（新ファイルは作らない）。write は main.cpp のみ。
```

---

## Prompt 3 — constraint viewer: requiredReadId / excludedReadId の可視化 ✅ 完了

本セッション（2026-11-17・DDMEditor repo）で実装・実機検証済み。結果は §1 の「Prompt 3 実行の結果」節参照
（実測: 全 scene match=1・present_resolve_ok=1・往復 intact・teal PASS・round-trip EXACT・exit=0）。
以下は履歴として保持（再実行しない）。本セッションでは「既定の重畳描画」を選択し、optional の
constraint 専用 layout は作らなかった（座標・pick・fit は nodePos()/applyActiveLayout() の一本化で保護済み）。

```text
あなたは DDMEditor の GUI 拡張実装者です。

前提（現行 main.cpp に実済、再実装しない。Prompt 2 が session-42e6def2 で完了した後の状態）:
- buildGraph() が requiredReadId(slot0Index==0)/excludedReadId(slot1Index==1)/
  excludedReadId(slot2Index==3) の edges をすでに生成している
  （EDGE_REQUIRED_READ / EDGE_EXCLUDED_READ_SLOT1 / EDGE_EXCLUDED_READ_SLOT2）。
  target が同 scene に無い場合は GraphEdge{to=-1, dangling=true} で保持する。
  注意: requiredReadNodes / excludedReadNodes は -1 を含むので、描画・count は -1 を除外する。
  constraint の ghost/dangling は edges の to==-1 で判定する（GraphNode.danglingParent /
  danglingChild は parent/child 専用。node 側に新 field を足すか edges から数える）。
- getPropRequiredReadId / getPropSlot1ExcludedReadId / getPropSlot2ExcludedReadId /
  getSlotIndex / getSlotValue / formatPropHex16 は実済。
- per-mode layout cache が実済（Prompt 2）: GraphNode に familyWx/familyWy/famDepth。
  layoutScene は wx/wy のだけ、layoutFamily は family 座標のだけを書く。描画・pickNode・
  fitViewToScene は nodePos() が active layout を読む。switchScene / L / キー 1-4 は
  applyActiveLayout() を呼ぶ。現行 VIEW_CONSTRAINT の既定 active layout は general（nodePos() は
  wx/wy を返す）。constraint の座標を変える場合は nodePos()/applyActiveLayout() の登録で対応する。
- VIEW_CONSTRAINT の mode 切替はキー 3 で動作し、drawGraphOverlay() は CONSTRAINT モードで
  drawGraphEdges + drawNodeBox（general active layout）を描いた後に drawConstraintView(r) を呼ぶ。
  drawConstraintView は空 stub → ここに overlay を実装する（active layout の座標に重畳描画できる）。
- describeElement(nodeIndex, view) の view 別ソートが実済（VIEW_FAMILY variant あり。HUD は
  describeElement(gSelectedNode, gViewMode) を呼ぶ）。VIEW_CONSTRAINT は現状 general variant に
  falls through → このセッションで VIEW_CONSTRAINT variant を追加する。
- HUD legend は family 凡例 + "R general: active. F family viewer: implemented" +
  "C / EDIT: phase hook, not implemented yet (Prompt3 / Prompt4)" を出す。実装後の legend 更新先はここ。
- selftest の family phase は「期待値を prop 直接パースから実行時計算する」pattern の手本。
  constraint phase も同じ pattern を使う。
- data.txt はゲーム側の legacy write-back で実行のたびに変わる。期待値・実測数をハードコードしない
  （2026-11-17 スナップショットでは scenes {0:10,1:35,2:31,15:16}・required(val!=0)=11・
  excluded(slot1/slot2 val!=0)=0・child 矛盾 edge line 12/33 で、required/excluded の数は実行ごとに変わる）。

このセッションの目標:
drawConstraintView() を実装し、制約 edge と ghost target を可視化する。

実装要件:
1. 色は mocha の意味体系に合わせる（ノードストライプ・runtime 色と衝突させない）。
   - required edge: teal #94e2d5（hasRead=green、EXACT OK=green と区別）。
   - excluded slot1 edge: red #f38ba8。
   - excluded slot2 edge: yellow #f9e2af（slot1 と slot2 を区別）。
   - ラベルは drawGuiLine() で "requiredReadId:0x…" / "excluded(slot1):0x…" /
     "excluded(slot2):0x…" を edge 中段に描く（kMCSubtext1 / 各色）。
   - legend に 3色の意味を加える。既存の "C / EDIT: phase hook, not implemented yet" 行を
     「C constraint: implemented / EDIT: hook」に更新し、family の矛盾 yellow と意味が
     違うこと（excluded slot2 = yellow）を凡例に明記する。
2. layout は現行の active layout に重畳描画する（既定）。描画・pick・fit は nodePos()/
   applyActiveLayout() を通るため、CONSTRAINT の既定座標は general のまま安全に overlay できる。
   制約 edge を主視にするため、必要なら constraint 専用 layout（制約 chain を column にする）か
   family layout の reuse を optional で作る。その場合は GraphNode に constraintWx/constraintWy を
   追加し layoutConstraint(scene) を新設して nodePos()/applyActiveLayout() に登録する
   （layout 関数はその mode の座標のだけ書き、他 mode の座標を壊さない。決定性を保つ）。
3. ghost node を描く。
   - GraphEdge.to == -1 の target は "ghost" として id badge を overlay0 の dashed 枠で描く
     （新しい helper を作ってよい。kMCOverlay0 の枠・id hex を drawGuiLine で示す）。
   - "scene内に存在しない id" を明示する（既存の par!/chi! badge とは別に、制約 ghost を示す）。
   - HUD に dangling constraint のカウントを出す（edges の to==-1 を kind 別に数える。
      requiredReadNodes/excludedReadNodes の -1 はノード側 count から除外する）。
4. ノード詳細を constraint 向けに強化する。
   - describeElement() に VIEW_CONSTRAINT variant を追加し（family variant と同じ形式）、
      required/excluded（slot index・raw value・target の scene/id が実在か ghost か）を先頭に並べる。
   - 値が 0 の項目は "なし" にするか非表示にする（view 別フィルタは variant 内に新設。
      他 view の出力順は壊さない）。
   - raw prop hex（formatPropHex16）はそのまま表示する。
5. 安全な処理。
   - duplicate constraint edges は統合せず複数描く（self constraint、cycle も描ける）。
   - Document を mutate しない。round-trip を壊さない。
6. デフォルト仕様。
   - このセッションでは timer / unlock flags / eyelevel の制約 edge は描かない
     （Prompt5 の props menu で編集可能にし、必要なら次フェーズで可視化する）。
   - 現行 data.txt に excluded edge が 0 件なら red/yellow は描画に出ない。その場合は
     "excluded_edges=0 (none in data.txt)" を info として出力し、色出現を PASS 条件にしない。
     required edge が 1件以上あれば teal の出現を PASS 条件とする。

検証（selftest に constraint phase を追加する。期待値は実行時に gDocument の prop を直接
パースして算出し、数をハードコードしない。family phase の pattern を踏襲する）:
- 各 scene で required/excluded(edge kind) の数が prop 直接パースの期待値と一致すること
  （kind 別・dangling(to==-1) 別で counts を照合し match=1。present の node idx 解決も確認する）。
- ghost（to==-1）の数と表示が壊れていないこと。
- mode 1↔3 の往復（1↔2 を含む）で他 mode の座標・選択・fit が壊れないこと。
  constraint 専用 layout を作った場合はその決定性も確認する。
- constraint mode の実描画を build/viewer_frame_constraint.bmp に保存し、RGB24 ヒストグラムで
  teal（required edge が 1件以上あれば必須）と kMC text/subtext1 を確認する。
  red/yellow は excluded edge の分だけ出現することを info として報告する。
- round-trip は往復後も EXACT MATCH のまま（Document を mutate しない）。

出力形式:
- main.cpp を編集して適用する（新ファイルは作らない）。write は main.cpp のみ。
```

---

## Prompt 4 — editor: dialogue 入力・parent/child 選択で Element を追加する ✅ 完了

2026-11-18 のセッション（DDMEditor repo・本ドキュメント更新の実行）で実装・実機検証済み。結果は §1 の
「Prompt 4 実行の結果」節参照（実測: nextFreeId 全 scene match=1・空 dialogue 拒否・ESC 取消で Document 不変・
追加行の正格式/再パース・child なし getPropChild()==0・child ありは child edge + childLinkAdj・family/constraint の
出現 match=1・baseline diff added のみ・runtime 同期 off・baseline 復元 EXACT・editor_phase_all_flags=1・exit=0）。
以下は履歴として保持（再実行しない）。本セッションでは ENTER の child commit は「現在値で commit」（選択なし=none のまま、
N で none に戻す）とし、ユーザー入力 id の override（optional）は未実装（auto の nextFreeId のみ）を選択した。
constraint/family は再構築だけで即時更新されるため配線追加不要（実測で確認）。

```text
あなたは DDMEditor の GUI 拡張実装者です。

前提（現行 main.cpp に実済、再実装しない。Prompt 2・Prompt 3 が完了した後の状態・2026-11-17）:
- EDITOR の mode 切替はキー 4 で動作し、drawEditorOverlay(r) の空 stub が呼ばれる。
  （唯一の未実装 hook。drawFamilyView / drawConstraintView は実済み）
- ElementRecord.dirty は宣言済みで未使用。serializeDocument() は dirty 行のみ
  `'dialogue', '0xHEX',`（formatPropHex16, lowercase 16桁, trailing comma）を再生成し、
  未編集行は原文（RawLine.text + terminator）を復元する。
- Prompt 3 の constraint viewer は実済: drawConstraintView / countConstraintEdges(scene) は
  gGraph.edges を直接スキャンするだけなので、buildGraph を再構築すれば制約 edge・ghost・
  HUD の kind別/ghost別 count・constraint variant の present/ghost 表示は即時更新される。
  graph 再構築で constraint/family 側に追加の配線は不要（新ノード・新 edge は自動で出現する）。
  照合には countConstraintEdges + prop 直接パースの期待値（追加行の合成 prop を含めて実行時計算）を使う。
- describeElement(nodeIndex, view) の view 別ソートは general/family/constraint variant 実済
  （HUD は describeElement(gSelectedNode, gViewMode) を呼ぶ。constraint は値 0 を none・present/ghost を
  分離表示）。新しい props 表示は同関数に variant として追加し、別機能を新設しない・他 view の出力順を壊さない。
- per-mode layout cache・nodePos()/applyActiveLayout() の一本化は実済。CONSTRAINT / EDITOR の既定
  active layout は general のまま。Prompt 4 で新 layout は作らない（preview は active layout 上に描く）。
  mode 切替は gSelectedNode を触らないが、graph 再構築で node index は変わる → 選択は (scene,id) key で復元する。
- runtime の console 挿入フロー（TAB → MODE_INSERT_TEXT → MODE_INSERT_QUOTE →
  MODE_INSERT_ONETIME → buildProperty(id = containerB.size()+1, gInsertParent, gScene)）は
  そのまま残す。GUI editor は別 state として新設し、この flow を流用しない。
- buildProperty() は id/parent/scene/isQuote/isOnetime のみ合成する。child/slot は扱わない。
- キー衝突に注意: F(sendProperty) / O / I / A / U / M / TAB / Y / N は runtime のもの。
  EDITOR mode のとき GUI 用のキーを使い、runtime キーは editor では遮断する
  （ただし gAppMode が挿入モードのときは既存の遮断規則を優先する）。
- selftest の round-trip / selection / family / constraint phase は「期待値を現行 gDocument の
  prop 直接パースから実行時計算する」pattern で全 pass・exit=0（2026-11-17）。editor phase も同じ
  pattern を使う。data.txt はゲーム側の legacy write-back で実行のたびに変わる環境があるため
  数をハードコードしない。

このセッションの目標:
EDITOR mode で Element を追加する GUI を作る。
テキストウィンドウに dialogue を入力し、GUI 操作で parent を選び、同様に child を選べる。

実装要件:
1. EditorState を新設する（gAppMode は使わない）。
   - add state: dialogue buffer, parent candidate, child candidate, id candidate, preview prop。
   - EDITOR mode でのみ入力を受け、gViewMode が EDITOR でないときは GUI 入力を遮断する。
   - SDL_StartTextInput/StopTextInput は既存の挿入フローと対称に使う（二重 Start を避ける）。
   - UTF-8・空白を含む入力をそのまま保持する。空 dialogue は不可。Enter 確定、ESC 取消
     （ESC は通常は選択解除なので、editor の add state 中は取消を優先する）。
2. id 採番 nextFreeId(scene) を作る。
   - containerB.size()+1 は scene と無関係で重複する（data.txt では scene 0 と scene 1 が
     同じ id 0x01.. を持つ）。nodesInScene(scene) から未使用の最小 id を選ぶ。
   - id > 0xFF は追加不可。ユーザー入力 id も optional で許す（衝突検出）。
3. parent 選択 UI。
   - ノードクリックで候補ノードを選び、HUD/dropdown に列挙も出す（selected scene の id、
     0 は root/no parent として許す、自己 id は禁止）。
   - 選択すると prop bits 8-15 に value を立てる。invalid（別 scene の id など）は追加禁止。
4. child 選択 UI（helper と data.txt の既存慣習に合わせる）。
   - child あり: slot1Index bits 26-27 = 0、slot1 value bits 40-47 = child id（!= 0）。
   - child なし: **既存 data.txt の慣習どおり slot1Index = 0 / slot1 value = 0 のまま**にする。
     getPropChild() は value が 0 であれば 0 を返し、buildGraph() は childId != 0 のときだけ
     edge を作るため、この形は「child なし」として正しく振る舞う。既存行を index 変更で書き換えない。
   - newRoot（slot1Index==2）/ excludedReadId（slot1Index==1）とは排他。
     選択 UI では slot1 option を排他選択にし、none を選んだら value を 0 にする。
5. Document に追加する。
   - RawLine{text="'dialogue', '0x…',", terminator="\n", kind=LINE_ELEMENT, elementIndex} を
     lines に追加し、ElementRecord{element, lineIndex, dirty=true} を elements に追加する。
   - 追加行は正格式で生成する（hex lowercase 16桁、trailing comma、dialogue は原文のまま）。
   - buildGraph(gDocument, gGraph) を再構築し、active mode の layout を再適用する。
     （再構築だけで constraint/family の edge・ghost・HUD の countConstraintEdges は即時更新される。
      layout の再適用だけが必要。constraint 側に追加の配線はしない。）
   - 選択は index でなく (scene,id) key で復元する（再構築で index が変わる）。
   - 追加直後、新ノードを selectedNode にする。
6. runtime との同期は行わない（デフォルト）。
   - containerA/containerB/displayList は自動的に同期しない。必要なら HUD に
     "runtime に反映" の明示トグル（key G など）を作り、既定では off にする。
   - legacy の ../data.txt write-back 経路には触らない。
7. preview panel を描く。
   - id / parent / child / scene / flags / slot1Index / slot1 value / final prop hex（16桁 lowercase）。
   - 追加前後の diff（raw line の予定）を出す。色は kMC を使う。
8. round-trip を壊さない。
   - 追加前の Document は EXACT MATCH でなければならない（追加前に R で確認する）。
   - 追加後は「追加行のみが baseline と diff」であることを検証する。
     verifyRoundTrip に baseline を持たせる、または baselineRaw を保存して added/edited 行数を
     RoundTripReport に加える（既存の firstDiffLine の意味は壊さない）。

検証（selftest に phase を追加する。期待値は実行時に算出し、数をハードコードしない）:
- nextFreeId が各 scene で重複を作らないこと（使用 id 集合との衝突チェック）。
- 追加した prop を parseElementLine が再パースでき、getPropParent/getPropChild が
  意図どおりの値を返すこと。child なし設定が getPropChild()==0 になること。
- serializeDocument の追加行が正格式（lowercase 16桁 + trailing comma）であること。
- 追加後に graph の nodes/edges が更新され、新ノードが family/constraint に現れること。
  constraint への出現は countConstraintEdges と prop 直接パースの期待値（追加行の合成 prop を
  含めて実行時計算）で照合する（constraint phase の pattern を踏襲・match=1）。
- baseline との diff が追加行のみであること（RoundTripReport の added/edited 報告）。
- 追加→round-trip の baseline diff、取消/ESC で Document が元に戻ることを確認する。
  （selftest で 1件の追加を検証し、検証後は baseline に戻す。data.txt 自体は書き換えない。）

出力形式:
- main.cpp を編集して適用する（新ファイルは作らない）。write は main.cpp のみ。
- セッション終了後（selftest の結果確定後）に ./prompts_v2.md を更新する: §1 に
  「Prompt 4 実行の結果」節を追加し、Prompt 5 の前提・共通制約・§3 の step 見積を現行 main.cpp の
  実済状態に合わせて更新する。失敗・観測差异があればそのまま記載する（data.txt は書き換えない）。
```

---

## Prompt 5 — slot options menu: 全 prop fields の編集 ✅ 完了

2026-11-18 の session-213fbdb3 で実行（実装・build warning 0 まで完了したが turn が max-tokens で中断）、
継続セッションで props phase の残り flag を修正して完遂（全 flag pass・exit=0・
round_trip_after_props_phase=EXACT OK・data.txt 不変）。結果は §1 の「Prompt 5 実行（session-213fbdb3・
中断→継続セッションで完遂）の結果」節参照（観測差异・修正事項も同節に記載）。以下は履歴として保持（再実行しない）。

```text
あなたは DDMEditor の GUI 拡張実装者です。

前提（現行 main.cpp に実済、再実装しない）:
- prop bit assignment と read helpers（getPropID/Parent/HasRead/IsQuote/IsOnetime/Scene,
  getPropRequiredReadId, getPropSlot0Unlocks, getPropChild, getPropSlot1ExcludedReadId,
  getPropNewRoot, getPropTimerSetter(+Id/Action, hasPropTimerSetter), getPropSlot2Unlocks,
  getPropRequiredUnlockFlags, getPropSlot2ExcludedReadId, hasPropTimerTarget,
  getPropTimerTarget(+Op/Id/Count), getPropNewScene, getPropExcludedUnlockFlags,
  getPropEyelevel, getPropToggleEyelevel, getSlotIndex, getSlotValue）は実済。
- describeElement() は全 fields を stdout/HUD に表示済み（general/family/constraint variant 実済・
  Prompt 3 で constraint は present/ghost 分離・値 0 は none 表示）。formatPropHex16 は実済。
- Prompt 3 の constraint viewer・countConstraintEdges は graph 走査なので、props 編集後の
  buildGraph 再構築で edge・ghost・HUD count・constraint variant 表示は即時更新される
  （cross-scene target の warning は constraint の ghost 表示で確認できる。配線追加不要）。
- Prompt 4（2026-11-18）が実済なので、その identifier を再利用して進む（再実装しない・§1 の Prompt 4 サマリ参照）:
  `EditorState`（gAppMode と別 state・stage machine・gViewMode==EDITOR でのみ入力受理・runtime キー TAB/F/O/I/A/U/M/Y/N は
  editorMode ガードで遮断済み・GUI キー ENTER/BACKSPACE/Y/N/G）・`editorEnterKey`/`editorYN`/`editorBeginAdd`/
  `editorCancelAdd`（ESC は add state で取消優先・mode 切替 1/2/3 は add flow 取消）・`editorStartText`/`editorStopText`
  （Start/Stop 対称）・`composeEditorProp`（bit 配置・slot1 child value）・`nextFreeId`・`editorValidateAdd`・
  `editorCommitAdd`（正格式追加 + dirty=true + buildGraph 再構築 + applyActiveLayout + findNode で新ノード選択）/
  `editorRestoreToBaseline`・`drawEditorOverlay` の preview panel・HUD の editor 凡例・`G` の runtime 同期トグル（既定 off）。
  props 編集の panel は **add flow の stage machine とは別**に新設してよいが、入力は EDITOR mode のキー中心で、
  既存の editor キー（ENTER=add flow 進行 / BACKSPACE=dialogue / Y/N=flags・child none / G=runtime 同期）と
  衝突しないキー・操作を選ぶ（Tab=次項目など新キーは 1-4/V/P/X/E/R/D/L/0/+/-/[ ]/矢印/ESC/Q と衝突させない）。
  props 編集後の再構築は `editorCommitAdd` と同じ経路（buildGraph → applyActiveLayout → 選択は編集前の
  (scene,id) key を `gGraph.findNode` で復元）を使う。dirty=true の行再生成は serializeDocument が正格式で行う
  （追加行の format と同じ規則・lowercase 16桁・trailing comma）。
- `RoundTripReport` に baseline の added/edited/added_or_dirty_only が実済（verifyRoundTrip は baseline raw を行単位で
  照合する。編集行は dirty Element 行のみ・`diffIsAddedOrDirtyOnly` で検証できる。firstDiffLine の意味は不変）。
  undo 後 baseline に戻る検証はこの flag と EXACT で行う。

このセッションの目標:
selectedNode の props editor を作り、parent/child 以外の全 prop fields を編集する。

実装要件:
1. 書き込みヘルパーを新設する（read helpers と対になる setProp / setSlot）。
   - setPropBit(prop, bit, bool) / clearPropBits(prop, mask) / setPropField(prop, shift, width, v)
   - setSlotIndex(prop, slot, idx) / setSlotValue(prop, slot, v)
   - slot value の合成は現行 read と逆の bit 配置に厳密に一致させる:
     timerSetter value = timerId(bits0-1) | action(bits4-5<<4)（vacant bits2-3 は保持）
     timerTarget  value = operator(bits0-1) | timerId(bits2-3<<2) | count(bits4-7<<4)
     eyelevelToggle value = eyelevel(bits0-3) | toggleEyelevel(bits4-7<<4)
     flags/unlocks/required/excluded = bits0-7 の mask
     newScene/child/excludedReadId/requiredReadId = 素の 8bit value
2. props menu を作る。
   - ノード選択（クリック、または right-click 相当の選択操作）で editor panel を開く。
   - HUD panel は可変幅（hudW = min(kHudW, winW)）なので、メニュー行も同幅予算で描く
     （drawGuiLine の budgetPx を使う）。
   - tabs/sections: basic flags → id/parent/scene → slot0 → slot1 → slot2 → slot3。
   - 操作はキー中心（editor mode のキーのみ）: Tab=次項目、Space/Enter=トグル、+/-=値、
     Y/N=bool、U=undo、G=graph 再構築、ESC=閉じる。マウスクリック対応は optional。
3. fields の編集仕様。
   - id bits0-7 (0x00-0xFF), parent bits8-15 (0x00-0xFF), scene bits20-23 (0x0-0xF)。
     表示は hex と decimal の両方。入力は hex を優先。
   - hasRead bit16 / isQuote bit17 / isOnetime bit18 は checkbox。vacant bit19 は optional。
   - slot0Index: none / requiredReadId / unlocks。
   - slot1Index: none / child / excludedReadId / newRoot（排他）。
   - slot2Index: none / timerSetter / unlocks / requiredUnlockFlags / excludedReadId。
   - slot3Index: none / timerTarget / newScene / excludedUnlockFlags / eyelevelToggle。
   - slot value bits 32+8*slot は 0x00-0xFF。option に応じて意味通り編集する
     （timer は separate controls、flags は bit selector、id 系は scene 内候補リスト）。
   - "none" の表現は read helpers と data.txt の既存慣習に合わせる。
     helpers は「index が該当 option であれば value を返す」ので、index を立てず value を 0 にする
     （既存行は index=0/value=0 の形で none を表している。既存行を index 変更で書き換えない）。
     slot option を none に戻す操作は「その slot の option index を元に戻し value を 0 にする」
     （元が index=0 の行は 0 のまま、value だけ 0 にする）とする。
     newScene の value 0 は「変化なし」sentinel なので 0 を選択可（smallestNonZeroScene の注記参照）。
   - timerTarget の operator 0 は「timerTarget なし」として扱う（hasPropTimerTarget は op!=0 を前提）。
     1=less, 2=more, 3=equal。timerSetter の action 0=none,1=start,2=stop,3=reset。
   - flags は hex + binary で表示し bit をトグルする（unlocks は slot0 と slot2 の併存、
     requiredUnlockFlags は slot2Index==2、excludedUnlockFlags は slot3Index==2）。
   - eyelevel bits0-3 / toggleEyelevel bits4-7（0 は無条件 / toggle なし）。
   - timer / unlock flags / eyelevel の制約 edge 可視化は未実装のまま（Prompt 3 の default 範囲）。
     必要なら次フェーズで drawConstraintView に kMC の別意味色で追加する（現色と衝突させない）。
4. validation を作る。
   - 範囲外は clamp + warning（HUD に yellow #f9e2af で出す）。
   - target id が現 scene に無い場合は warning を出し、constraint viewer の ghost で表示する
     （Prompt 3 の ghost 描画・countConstraintEdges が実済。そのまま確認できる）。
   - 同一 slot に複数 option を立てられない（index は 1つ）。slot1 の child / excluded / newRoot は排他。
     requiredReadId(slot0) と child(slot1)、excluded(slot1) と excluded(slot2) は併存可。
   - 編集後の prop hex を表示し、旧 prop との diff を出す。色は kMC を使う。
5. Document update を作る。
   - ElementRecord.dirty = true、RawLine.text を正格式で再生成、element.prop を更新する。
   - buildGraph を再構築し、active layout を再適用、選択を (scene,id) で復元する。
   - family/constraint viewer の edge が即時更新される（edges 走査なので配線追加不要）。
   - undo stack（node key + previous prop）を 1段以上保持し U で復元。
   - dialogue は編集しない（dialogue edit は optional で別 panel）。
6. round-trip を壊さない。
   - 未編集行は原文のまま。dirty 行のみ再生成。comment/blank line は保持。
   - hasRead を勝手にクリアしない（load-time prop を保持する。runtime の hasRead mutation は
     この editor では作らない）。
   - baseline との diff は編集行のみであることを RoundTripReport で報告する（Prompt4 で
     baseline が RoundTripReport に追加されている前提）。
   - undo 後 baseline に戻る（round-trip EXACT）。

検証（selftest に phase を追加する。期待値は実行時に算出し、数をハードコードしない）:
- setProp/setSlot → getProp の往復が全 fields で一致すること（各 option を 1回ずつ立てて読む）。
- 編集した RawLine が parseElementLine で再パースでき、prop が同一であること。
- timerSetter/timerTarget/eyelevel の合成・分解が現行 helpers と一致すること。
- validation の warning 数が期待値と一致すること（範囲外、cross-scene target、slot 排他）。
  cross-scene target の ghost は countConstraintEdges の ghost count と prop 直接パースの
  期待値で照合する（match=1）。
- undo が旧 prop を完全復元し、round-trip が baseline に戻る（undo 後 EXACT）。
- 編集後の family/constraint edge が更新されていること（再構築→edges 走査で即時。確認だけ行う）。

出力形式:
- main.cpp を編集して適用する（新ファイルは作らない）。write は main.cpp のみ。
- セッション終了後（selftest の結果確定後）に ./prompts_v2.md を更新する: §1 に
  「Prompt 5 実行の結果」節を追加し、Prompt 6 の前提・共通制約・§3 の step 見積を現行 main.cpp の
  実済状態に合わせて更新する。失敗・観測差异があればそのまま記載する（data.txt は書き換えない）。
```

---

## Prompt 6 — 統合・round-trip 検証・最終安定化（+ ゲームモード共存）

```text
あなたは DDMEditor の GUI 拡張実装者です。

前提（現行 main.cpp に実済）:
- Document/Graph/VIEW_GENERAL/HUD/keys/FreeType 日本語/Mocha パレット/DDM_SELFTEST が動作する。
- Prompt2 で family viewer、Prompt3 で constraint viewer、Prompt4 で editor add、Prompt5 で props menu・
  undo が実済み。selftest は round-trip / selection / family / constraint / editor / props phase が
  「期待値実行時計算」の pattern で全 pass・exit=0・round-trip EXACT（props phase 後も・全 undo 後 EXACT・
  data.txt 不変）（失敗は検知しない）。Prompt5 実済の identifier を再利用する（再実装しない・§1 の
  Prompt 5 サマリ参照）: setProp*/setSlot*/editTimerSetterValue/editTimerTargetValue/editEyelevelToggleValue,
  PropsFieldDef/kPropsFields, PropsEditState gProps（kPropsUndoMax=64）, propsOpen/propsClose/propsCommit,
  propsValidateEdit/propsApplyFieldValue/propsSetOption/propsUndo/propsRebuildGraph,
  describeElement の EDITOR variant, selftest props phase, props frame capture の pattern。
- 現行の自己検証は常に SDL_APP_SUCCESS で終わる（失敗を検知しない）。最終化では失敗を返す。

このセッションの目標:
各フェーズを統合し、編集なしの data.txt 出力が完全一致であることを検証し、
GUI 安定性・色・検証の最終安定化パッチを作る。

実装要件:
1. アーキテクチャ整理（現行 identifier に map する）。
   - Document: gDocument + RawLine/ElementRecord（raw 全バイト列、comment/blank 保持、dirty）。
   - GraphIndex: gGraph（adjacency, edges, roots, scenes, warnings）+ per-mode layout cache +
     active layout + selectedNode を (scene,id) で保持。
   - ViewModes: gViewMode（VIEW_GENERAL/FAMILY/CONSTRAINT/EDITOR）+ view 別 props 表示順序・フィルタ。
   - EditorState: add/edit state、validation warnings、undo stack。
   - Serialization: serializeDocument（未編集行=原文、dirty 行=正格式）+ baseline diff。
   - ファイル write はしない（data.txt に書き戻さない）。
2. round-trip 検証を強化する。
   - load 直後の baseline を保存し、編集/追加後に「diff は dirty/added 行のみ」を検証する。
   - RawLine.terminator（"\n" / "\r\n" / ""）を保持し、CRLF・改行なし最終行を壊さない。
   - quote 形式、trailing comma, `0x` prefix, lowercase 16桁 を検証する。
   - hasRead をクリアしない。dummy 行の prop も原値を保持する。
   - legacy の ../data.txt write-back（hasRead clear, containerA から全行再生成）は、
     viewer 運用では無効化するか、明示的なトグルで分離する。
     （Prompt 3 セッションで `./data.txt` 読み込み運用では write-back が走らないことを確認済み。
      ../data.txt 旧運用との差異をトグルで明示する。）
   - quit の backupData() は data.txt.bak を上書きする。write policy に合わせ
     デフォルト off（トグル B）にするか、明示的に残す旨をコメントで明記する。
3. editor integration test。
   - add: nextFreeId が重複なし、parent bits、child slot（あり/なし）、正格式 line、graph 更新。
   - edit: props menu が全 fields を更新、raw line 更新、family/constraint edge 更新。
     （Prompt5 の props phase で実済・このプロンプトでは flag 出力を FAIL 検知に格上げする。）
   - invalid: 範囲外・slot 排他・cross-scene target が reject/warning される。
   - undo: 全変更が baseline に戻る。undo は上限に注意: kPropsUndoMax=64 で stack が満杯になると
     全 undo で baseline に戻れない（Prompt5 の selftest は 55 entry で上限未到達・実測 exact=1。
     上限を引き上げるか満杯にしない運用を明示する）。
4. family / constraint integration test。
   - active mode の layout で pickNode・pan/zoom・fitViewToScene が正しいこと。
   - parent field edge と slot1 child edge の区別が壊れていないこと。
   - newRoot/dummy/template/empty の表示が graph を壊さないこと。
   - cycle / multi-root forest / ghost(dangling) 表示が壊れていないこと
     （constraint の kind別/ghost別 count は countConstraintEdges + prop 直接パースの期待値で照合）。
5. GUI 操作の安定性。
   - mode 切替・scene 切替で座標・選択・HUD・props panel が壊れない。
   - テキスト入力の Start/Stop が対称で、editor と console 挿入の衝突が無い。
   - runtime キー（TAB/F/O/I/A/U/M/Y/N）は通常運用でそのまま動作し、editor/GUI 専用キーと
     両立する。
   - 描画は drawGraphOverlay → drawHud の順序を維持する。
6. 色と視認性の監査。
   - すべての色が kMC 定数由来であることを grep で確認する（数値 RGB リテラル zero）。
   - WCAG コントラストを pixel readback から実測し、面+テキストの組合せが 4.5:1 以上。
   - glyph texture は SDL_PIXELFORMAT_RGBA32、readback は RGB24 変換後（セッション2の教訓。
     BMP の生バイト列は BGR・bottom-up なので external probe は BGR で読む）。
   - HUD legend を実装状態に合わせて更新する（family/constraint の凡例・意味体系の区別を含む）。
7. ゲームモード共存（todo.md の「通常のゲームモードも実行できるように」対応）。
   - 起動時のモード選択を environment（DDM_MODE=viewer|game）かキートグルで選ぶ。
     デフォルトは現行動作（viewer overlay + console）を維持する。
   - game モードでは graph/HUD を描かず、renderConsole と入力系統だけを動かす。
     DDM_SELFTEST は viewer 検証に集中する。
8. 最終検証手順（この手順を main.cpp の selftest に実装し、失敗時に非成功で終了する）。
   - CCACHE_DIR=$PWD/build/.ccache cmake --build build
   - SDL_VIDEO_DRIVER=dummy DDM_SELFTEST=1 ./build/DDMEditor
   - 各 phase（round-trip baseline / selection / font / family layout / constraint edges /
     editor add / props edit / undo / palette audit / game-mode switch）を順に検証し、
     失敗は stdout に "FAIL <phase>" を出し SDL_APP_FAILURE で終わる。
     （round-trip / selection / family / constraint の既存 phase は flag 出力を FAIL 検知に格上げする。
      新規 phase は既存の「期待値実行時計算」pattern を踏襲し、数をハードコードしない。）
   - mode 別 frame を build/viewer_frame_<mode>.bmp に保存し、bright pixel と実色を報告する。
     （constraint は required≥1 のとき teal PASS・excluded は info、family は sapphire/yellow、
      red の出現は quote ストライプ由来と混同しない。）
   - data.txt は変更されない（bytes が load 時と同一であること）ことを最後に確認する。
9. 最終出力。
   - main.cpp を編集して適用する。新ファイルは作らない。data.txt に write しない。
   - 不足機能は追フィードバック前提で safest な default に留める。
   - セッション終了後（全 phase の結果確定後。失敗があっても）に ./prompts_v2.md を更新する:
     最終状態（各 phase の pass/FAIL・exit・実測色・WCAG・game-mode の動作）を §1 に反映し、
     §3 の step 見積を完了・最終状態にする（write は main.cpp と prompts_v2.md のみ）。
```

---

## 3. ステップ見積もり（v2.3・Prompt1〜5 は完了、Prompt6 が次回予定）

| Session | 目的 | 現行コード上の作業 |
|---|---|---|
| 1 (Prompt1) | 厳密 loader / グラフ基盤 / VIEW_GENERAL / round-trip | **完了**（セッション2で実機検証済み、日本語描画と Mocha 配色も追加） |
| 2 (Prompt2) | family viewer | **完了**（session-42e6def2・2026-11-17: `drawFamilyView` + `layoutFamily` + `nodePos`/`applyActiveLayout` + describeElement family variant + selftest family phase 全 pass・exit=0。§1 の Prompt 2 サマリ参照） |
| 3 (Prompt3) | constraint viewer | **完了**（本セッション・2026-11-17: `drawConstraintView`（general への重畳・teal/red/yellow・中段ラベル・ghost 破線枠）+ `ConstraintEdgeCounts`/`countConstraintEdges` + `drawDashedRect` + describeElement constraint variant + HUD legend/counts + selftest constraint phase 全 pass・exit=0・round-trip EXACT 維持。§1 の Prompt 3 サマリ参照。本ドキュメントはこの更新で Prompt 4 準備版になった） |
| 4 (Prompt4) | editor add | **完了**（2026-11-18: `EditorState`（別 state・stage machine）・`nextFreeId`・`composeEditorProp`・正格式追加 + dirty=true・`buildGraph` 再構築 + `applyActiveLayout` + 新ノード選択（(scene,id) findNode）・`drawEditorOverlay` preview・HUD editor 凡例・`G` runtime 同期トグル（既定 off）・`RoundTripReport` の baseline added/edited（verifyRoundTrip の行単位照合）・editor phase（期待値実行時計算）全 pass・`editor_phase_all_flags=1`・exit=0・data.txt 不変・本ドキュメント更新済み。§1 の Prompt 4 サマリ参照） |
| 5 (Prompt5) | props menu | **完了**（session-213fbdb3・2026-11-18・turn が max-tokens で中断 → 継続セッションで完遂: 書き込みヘルパー `setPropBit`/`clearPropBits`/`setPropField`/`setSlotIndex`/`setSlotValue`（read helpers と逆の bit 配置に厳密一致・timer/eyelevel は sub-bit だけ編集・vacant 保持）・`PropsFieldDef`/`kPropsFields`（22 fields・tabs/sections 順）・`propsOptionLabel`（value 0 = none 慣習）・`PropsEditState gProps`（undo stack・上限 64）・ノード選択で panel を開く（add flow と排他・idle のみ）・キー TAB/ENTER・SPACE/+/−/Y/N/U/G/ESC（props 優先 routing）・`propsValidateEdit`（clamp + yellow warning・id duplicate reject・cross-scene target は warning + constraint の ghost で確認・option 非立て sub field は inert）・`propsCommit`（正格式再生成 + dirty=true + `buildGraph`/`applyActiveLayout` + (scene,id) 選択復元・runtime 同期なし）・`propsUndo`（現在の nodeKey で解決 → 復元 → key 更新・順序修正済み）・`describeElement` EDITOR variant・HUD props 行/凡例・selftest props phase（期待値実行時計算）全 flag pass・`props_phase_all_flags=1`・exit=0・`round_trip_after_props_phase=EXACT OK`・全 undo（55 entry）で baseline 完全復元 exact=1・data.txt 不変・本ドキュメント更新済み。§1 の Prompt 5 サマリ参照。観測差异: value byte と prop bits は別領域・compose-decode は別 flag・general dump の行 index・O6 の mask・undo の解決順序・undo stack 上限） |
| 6 (Prompt6) | 統合・最終検証・ゲームモード共存 | **次回予定**。selftest の既存 phase（round-trip/selection/family/constraint/editor/props・flags は `editor_phase_all_flags`/`props_phase_all_flags` 含む）を FAIL 検知に格上げ（"FAIL <phase>" + SDL_APP_FAILURE）、palette/game-mode の新規 phase 追加（既存の「期待値実行時計算」pattern 踏襲・数をハードコードしない）、write policy・backupData トグル、palette 監査（kMC 由来・数値リテラル zero）、mode 共存 / セッション終了後に prompts_v2.md 最終更新（§1 に最終状態を反映・write は main.cpp と prompts_v2.md のみ） |
