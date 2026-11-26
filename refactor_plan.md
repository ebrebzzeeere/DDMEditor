# DDMEditor リファクタリング計画（main.cpp 分割プロジェクト）

作成セッション: 2026-11-26（本セッションは計画策定のみ。main.cpp の読取と本ファイルの作成のみを実施した）

---

## 1. 目的と大原則

- **目的**: `main.cpp`（現状 7148 行・約 357 KB）を機能単位で `.h` / `.cpp` に分割し、可読性・保守性を上げる。
- **大原則（违反禁止）**: **動作は整理の前後で一切変化しない。**
  - stdout / HUD の出力文言、キー割り当て、prop の bit レイアウト、書き込みポリシーをそのまま保持する。
  - `DDM_SELFTEST=1` の自己検証は全 phase が PASS を維持する（出力は構造が同一、ランダム値を含む行は正規化して比較）。
  - `data.txt` はリファクタリング前後で内容が変化しない（selftest の data unchanged phase が守る）。
- **方針**: 「翻訳単位の移動」だけを行う。**ロジックの書き換え・リネーム・最適化は行わない**（identifier は可能な限り原名のまま）。グローバル状態は `extern` 宣言 + 1つの `state.cpp` に集約し、呼び出し側の変更はヘッダの include 追加だけで済ませる。
- 分割は **依存の低い順**（prop → document → graph → state → 上層）で行う。各セッション末尾でビルド＋selftest が PASS して初めて commit する。

## 2. 現在の構造マップ（main.cpp、2026-11-26 時点の行番号）

行番号は分割の進行とともにずれる。**セッションではセクションコメント（`// --- ...` マーカー）を境界として使うこと。**

| 現行範囲 | 内容 | 移行先 |
|---|---|---|
| L1-24 | include群 | 各モジュールへ分散 |
| L25-92 | `struct Element` + prop bit レイアウト文書 | `prop.h` |
| L96-253 | prop read/write ヘルパー（getProp* / setProp* / setSlot* / edit*Value） | `prop.cpp` |
| L255-547 | Strict Document モデル（`RawLine`/`ElementRecord`/`Document`/`loadDocument`/`parseElementLine`/`serializeDocument`/`formatPropHex16*`/`verifyRoundTrip`/`RoundTripReport`/`hexByte`/`hexNibble`/`isCommentLine`） | `document.cpp` |
| L548-765 | Graph モデル（`EdgeKind`/`GraphEdge`/`NodeKind`/`classifyNodeKind`/`nodeKey`/`GraphNode`/`Graph`/`buildGraph`） | `graph.cpp` |
| L767-1061 | 匿名namespace のグローバル状態（containerA/B/C, displayList, objectList, flavorContainer, 選択状態, move mode, SDL window/renderer, scene/root/unlocks/eyelevel/tick/timers, ViewMode, gDocument/gGraph/gRoundTrip/gLoadedPath, viewport, selectedNode, EditorState, PropsFieldDef/kPropsFields/PropsEditState, UiMode, write policy, selftest失敗集約, `stFail`/`stPass`, hexDec/binaryByte, ノード寸法定数） | `state.h`（宣言）+ `state.cpp`（定義） |
| L1063-1465 | FreeType dlopen GUI テキストエンジン（`DFTFaceRec`/`DFTGlyphSlotRec`/`GlyphRec`/`GuiTextEngine gGuiText`/`decodeUtf8`/`loadGuiTextEngine`/`fontCandidates`/`tryGuiFont`/`initGuiText`/`shutdownGuiText`/`drawGuiText`） | `guitext.cpp` |
| L1467-1473 | props 関数 forward decl | `props.h` に吸収 |
| L1474-1550 | `%(変数名)` プレースホルダ展開（`resolvePlaceholderVar`/`expandTextPlaceholders`） | `placeholders.cpp` |
| L1551-1627 | 条件判定（`isReadInCurrentScene`/`satisfiesRequiredReadId`/`satisfiesExcludedReadId`/`evalCond5`） | `game.cpp` |
| L1628-1666 | タイマー（`resetTimer`/`advanceTimers`/`gTickTimer`）+ rebuild群 forward decl | `game.cpp` / `game.h` |
| L1668-1734 | `gimmick` / `hasValidObjective` | `game.cpp` |
| L1735-1843 | `renderConsole`（stdout の State/Containers/Selection、Verb/Objective 段表示） | `console.cpp` |
| L1844-2110 | コンテナ再構築（`rebuildContainerB/C`/`rebuildFlavorContainer`/`rebuildDisplayList`/`rebuildObjectiveList`/`rebuildMoveDestinationList`/`enterMoveMode`/`exitMoveMode`） | `game.cpp` |
| L2111-2310 | `processContainerLifecycle` / `sendProperty` | `game.cpp` |
| L2311-2374 | viewport変換（`worldToScreen`/`screenToWorld`）+ `setSolid` + Catppuccin Mocha パレット定数 | `view.cpp`（パレットは `palette.h`） |
| L2375-2406 | WCAG 輝度/コントラスト, `viewModeName` | `view.cpp` |
| L2407-2680 | `asciiPreview`/`drawGuiLine`/`describeElement`（view別 props 表示） | `view.cpp` |
| L2681-2724 | `printDocumentToStdout`/`printRoundTripReport`/`printSelectedElementDetails` | `document.cpp` / `hud.cpp` |
| L2725-2904 | レイアウト（`chainDepth`/`layoutScene`/family forest/`nodePos`/`applyActiveLayout`/`windowSize`/`fitViewToScene`） | `layout.cpp` |
| L2905-3320 | graph/family/constraint の描画（ports, bezier, node box, edges, dashed rect, constraint counts） | `view.cpp` |
| L3321-3603 | EDITOR add flow（`composeEditorProp`/`nextFreeId`/`editorStartText`...`editorCommitAdd`/`editorRestoreToBaseline`） | `editor.cpp` |
| L3604-4051 | props menu 編集（`propsOpen/Close/Commit/ValidateEdit/ApplyFieldValue/SetBool/Toggle/SetOption/NextField/Undo/RebuildGraph/MenuRows`） | `props.cpp` |
| L4052-4453 | `drawEditorOverlay`/`drawHud`/`drawGraphOverlay`/`pickNode`/`zoomAt`/`switchScene`/`handlePointerEvent` | `hud.cpp` + `view.cpp`（pointer系は `view.cpp` に残す） |
| L4454-4494 | `AppMode`/挿入入力状態/`buildProperty`/`cancelInsertMode` | `prop.cpp`（buildProperty）/ `state.h`（AppMode・gAppMode等）/ `console.cpp`（cancelInsertMode・挿入フロー） |
| L4497-4591 | `SDL_AppInit` | `main.cpp` に残す（薄層） |
| L4597-4607 | `setUiMode` | `app.cpp`（または main.cpp 薄層） |
| L4610-4880 | `SDL_AppEvent`（キー・マウス・テキスト入力ディスパッチ） | キー処理を `input.cpp` に抽出、`main.cpp` は薄いディスパッチ |
| L4881-7054 | `SDL_AppIterate` の selftest 分岐（10 phase: round-trip baseline / selection / family / constraint / editor / props / font / palette audit / game-mode / data unchanged） | `selftest.cpp`（phase ごとに関数分割） |
| L7057-7092 | 通常フレームループ（renderConsole、game mode 分岐、graph overlay 描画、fps調整） | `main.cpp` 薄層（`app_frame.cpp` に抽出可） |
| L7095-7107 | `backupData` | `document.cpp`（ファイルI/O） |
| L7110-7148 | `SDL_AppQuit`（backup policy、legacy ../data.txt write-back） | `main.cpp` 薄層 |

## 3. 目標アーキテクチャ

新規ファイルはリポジトリ直下に置く（`src/`  subdir は作らない。CMakeLists の source list 変更のみで対応するため）。

```
prop.h / prop.cpp         Element + bit layout + get/set helpers      （依存: std のみ）
document.h / document.cpp Document モデル・load/serialize/round-trip・backupData
                          （依存: prop）
graph.h / graph.cpp       GraphIndex・buildGraph                       （依存: prop, document）
state.h / state.cpp       全グローバル状態・enum・定数・stFail/stPass   （依存: document, graph）
                          ※ state.h は extern 宣言のみ。定義は state.cpp。
guitext.h / guitext.cpp   FreeType dlopen・GUIテキスト描画             （依存: SDL, std）
placeholders.h / placeholders.cpp %(変数)展開                           （依存: state）
game.h / game.cpp         条件判定・タイマー・gimmick・コンテナ再構築・
                          move mode・processContainerLifecycle・sendProperty
                          （依存: prop, document, graph, state, placeholders）
console.h / console.cpp   renderConsole・挿入モード(AppMode flow)・stdout表示
                          （依存: game, state, document）
palette.h                 Catppuccin Mocha パレット定数・wcag helpers  （依存: std）
layout.h / layout.cpp     layoutScene/layoutFamily/applyActiveLayout/fitViewToScene
                          （依存: graph, state）
view.h / view.cpp         viewport変換・describeElement・ノード/辺/family/constraint描画・
                          pickNode/zoomAt/switchScene/handlePointerEvent
                          （依存: layout, palette, guitext, state）
editor.h / editor.cpp     EDITOR add flow                              （依存: prop, document, graph, state）
props.h / props.cpp       props menu 編集                              （依存: prop, document, graph, state）
hud.h / hud.cpp           drawEditorOverlay/drawHud/drawGraphOverlay/print系
                          （依存: view, editor, props, state）
selftest.h / selftest.cpp runSelftest() + phase 関数群                 （依存: 全部）
input.h / input.cpp       SDL_AppEvent のキー/テキスト/マウスハンドラ    （依存: hud, game, editor, props, view, state）
app.h / app.cpp           setUiMode・フレームループ・write policy・quit 処理（main.cpp の薄層実装）
                          （依存: game, document, state）
main.cpp                  SDL_AppInit / SDL_AppEvent(薄いディスパッチ) /
                          SDL_AppIterate(薄いフレームループ) / SDL_AppQuit
                          （依存: 全部）
```

**依存規則**
- 上位モジュールは下位モジュールのみを含む。循環依存禁止（必要なら state.h の extern 経由で共有する）。
- `state.h` は状態の宣言に限定し、関数は置かない（`stFail/stPass` のみ state.cpp）。
- ヘッダは自己完結（include guard / `#pragma once`、使う型を自分でinclude）。
- `Gimmick.h` / `Gimmick.cpp` は main.cpp から参照されていない。現状のビルド構成を壊さない（Session 1 で CMakeLists を確認し、Gimmick が build に含まれるならそのまま維持）。

## 4. 保持すべき不変条件（各セッションで再確認）

1. prop bit レイアウト（id/parent/hasRead/isQuote/isOnetime/vacant/scene/slot index・value）とそのコメント文。
2. data.txt 正格式 `'dialogue', '0xHHHH|HHHH|HHHHHHHH',`（2|2|4 パイプ区切り・lowercase16・trailing comma）。読み込みはパイプ無旧書式も許す。
3. write policy: data.txt に書き戻さない。legacy の `../data.txt` write-back は load元が `../` のときのみ。quit の backupData は既定 off（Bトグル）。
4. キー割り当て（F/O/I/A/U/M/Y/N/TAB/Q/ESC, viewer: 1-4/V/P/X/E/R/D/L/0/+/-/[ ]/矢印, editor GUI: ENTER/BACKSPACE/Y/N/G, props: TAB/ENTER/SPACE/+/-/Y/N/U/G/ESC, H/B）。
5. DDM_MODE / DDM_SELFTEST 環境変数の挙動。
6. stdout のヘッダー（`=== [ ... ] ===`）と各 selftest phase の出力形式・`SELFTEST: ALL PHASES PASS` / `FAIL <phase>` の契約。
7. props undo stack 上限 256、round-trip baseline diff の意味（added/edited の規則）。
8. パレット hex（Catppuccin Mocha 公式値）と WCAG 監査の閾値。

## 5. 検証手順（共通）

セッション 1 で baseline を固定し、以後すべてのセッションでこれと照合する。

1. `CMakeLists.txt`・`.gitignore` を読み、ビルドコマンドと生成バイナリ名を確定する。
2. baseline 取得（リファクタリング前の HEAD で）:
   - `cmake -S . -B build && cmake --build build`
   - `SDL_VIDEO_DRIVER=dummy DDM_SELFTEST=1 <binary> > baseline_selftest.txt 2>&1; echo exit=$?` → exit 0・`ALL PHASES PASS` を保存。
   - baseline は git に載らない場所（`.gitignore` で覆われる `build/` または `.cache/` のサブディレクトリ。Session 1 で `.gitignore` を見て判断）に保存する。
   - 非決定行（乱数 tick duration 等）の正規化ルールを `normalize.sed` に記録し、以後の比較は `sed -f normalize.sed` 後に diff を取る。
3. 各セッション末尾: 同上で再ビルド → 正規化後 diff が空 → exit 0 → `data.txt` が `git status` で未変更。
4. 追加の簡易スモーク（可能なら）: 通常起動（dummy driver）で数フレーム走らせ異常終了しないこと。
5. 検証失敗時は **そのセッションの分割変更を `git restore` / `git checkout -- .` で戻し**、原因を引き継ぎに書いて再試行する。検証未達では commit しない。

## 6. セッションプロンプト（そのまま複製して使用する）

セッションプロンプト冒頭に貼る共通前置テキスト:

```
まず ./refactor_plan.md を読み、「共通制約」「§5 検証手順」「§7 引き継ぎ」の最新状態を把握して続行してください。
引き継ぎに未解決事項があれば先に処理してください。
```

各セッション末尾の共通終了手順（プロンプトに明記していない場合も必ず適用）:
1. §5 の検証を完遂（PASS 必須）
2. `./refactor_plan.md` の「§7 引き継ぎ」に本セッションの記録を追記
3. `git add . && git commit -m "<内容に即したメッセージ>" && git push`
4. todo.md・data.txt は編集しない。README.md は最終セッションのみ更新。

---

### Session 1 — 基盤整備 + prop / document 抽出

```
DDMEditor のリファクタリング Session 1 です。
1. ./refactor_plan.md（計画・検証手順・引き継ぎ）と CMakeLists.txt、.gitignore、todo.md、README.md を読んで、ビルド方法とバイナリ名、baseline保存先（git対象外）を確定してください。
2. リファクタリング前の HEAD で baseline を取得し、selftest 出力・exit code・正規化ルールを保存してください（refactor_plan.md §5）。以後すべてのセッションの照合基準になります。
3. src を作らずリポジトリ直下に prop.h/prop.cpp を作成し、main.cpp の Element 定義・prop bit レイアウト文書コメント・getProp*/setProp*/setSlot*/edit*Value・buildProperty を移動してください（ロジック・identifierは変更しない）。main.cpp は prop.h を include して同名関数を使う形にしてください。
4. 同様に document.h/document.cpp を作成し、Strict Document モデル（LineKind/RawLine/ElementRecord/Document/hexByte/hexNibble/isCommentLine/parseElementLine/loadDocument/formatPropHex16/formatPropHex16Piped/serializeDocument/RoundTripReport/verifyRoundTrip/printDocumentToStdout/backupData）を移動してください。グローバル状態（gDocument等）は移動せず main.cpp に残してください。
5. CMakeLists.txt に新 .cpp を追加し、ビルド・selftest が baseline と一致することを確認してください。
6. 動作変化がないことを確認したら、refactor_plan.md §7 に引き継ぎを記入し、git add . / commit / push してください。
```

### Session 2 — graph モデル抽出

```
DDMEditor のリファクタリング Session 2 です（refactor_plan.md §7 の引き継ぎを確認して継続）。
graph.h/graph.cpp を作成し、EdgeKind/GraphEdge/NodeKind/classifyNodeKind/nodeKey/GraphNode/Graph/buildGraph を main.cpp から移動してください。buildGraph は Document を受け取る純関数のままです。呼び出し側は graph.h を include するだけにして、挙動・出力を変えないでください。
CMakeLists.txt を更新し、refactor_plan.md §5 の検証（正規化後 selftest diff 空・exit 0・data.txt 未変更）を完遂してください。失敗したら変更を git restore で戻して原因を引き継ぎに書いて再試行してください。
完了したら §7 に引き継ぎを記入し、git add . / commit -m "適切なメッセージ" / git push してください。
```

### Session 3 — 状態定義（state.h / state.cpp）

```
DDMEditor のリファクタリング Session 3 です（refactor_plan.md を読み継続）。
main.cpp の匿名 namespace の全グローバル・enum・定数を state.h（extern 宣言・型・enum・定数）と state.cpp（定義）へ移動してください: containerA/B/C, displayList, objectList, flavorContainer+kFlavorParent, 選択状態, needRedraw, noContentState, move mode 一式, gWindow/gRenderer, gScene/gRoot/gParent/gUnlocks/gEyelevel/gTick/gTicketNumber/gCallingNumber, gTimers/gTimerRunning/gPendingTimerActions/TimerAction, gGTick*, ViewMode, gDocument/gGraph/gRoundTrip/gLoadedPath/gLoadWarnings, gViewMode/gViewScene/gGraphEnabled/gExportPanel/gExportOnQuit, viewport 一式, gSelectedNode, EditorStage/EditorState/gEditor/gEditorTextInputActive/gDocTerminatorPatched, PropsFieldKind/PropsFieldRole/PropsFieldDef/slot option names/kPropsFields/kPropsFieldCount/PropsUndoEntry/PropsEditState/gProps/kPropsUndoMax, UiMode/gUiMode/gBackupOnQuit/gLoadedBytes/gSelftestFailures/stFail/stPass, hexDec/binaryByte, ノード寸法定数, AppMode/gAppMode/gInputText/gInputIsQuote/gInputIsOnetime/gInsertParent。
内部 linkage は extern に変わりますが動作は同じです。identifier・値・出力は一切変えないでください。main.cpp は state.h を include します。
CMakeLists.txt は必要なら更新。refactor_plan.md §5 の検証を完遂し、引き継ぎを §7 に記入、git add . / commit / push。
```

### Session 4 — GUI テキストエンジン抽出

```
DDMEditor のリファクタリング Session 4 です（refactor_plan.md を読み継続）。
guitext.h/guitext.cpp を作成し、FreeType dlopen エンジン一式（DFTFaceRec/DFTGlyphSlotRec/GlyphRec/GuiTextEngine/gGuiText/decodeUtf8/loadGuiTextEngine/fontCandidates/tryGuiFont/initGuiText/shutdownGuiText/drawGuiText）を移動してください。gGuiText は guitext.cpp の定義 + guitext.h の extern 宣言にします（selftest の font phase が参照するため fontPath/lastError/ready は参照可能に）。
初期化・シャットダウンの呼び出し順序と dlopen/dlclose の対称性を壊さないでください。
CMakeLists.txt を更新し、refactor_plan.md §5 の検証（font phase 含む）を完遂、§7 に引き継ぎ、git add . / commit / push。
```

### Session 5 — ゲームロジック抽出（game / console / placeholders）

```
DDMEditor のリファクタリング Session 5 です（refactor_plan.md を読み継続）。
placeholders.h/cpp: resolvePlaceholderVar/expandTextPlaceholders を移動。
game.h/cpp: isReadInCurrentScene/satisfiesRequiredReadId/satisfiesExcludedReadId/evalCond5/resetTimer/advanceTimers/gTickTimer/gimmick/hasValidObjective/rebuildContainerB/rebuildContainerC/rebuildFlavorContainer/rebuildDisplayList/rebuildObjectiveList/rebuildMoveDestinationList/enterMoveMode/exitMoveMode/processContainerLifecycle/sendProperty を移動。
console.h/cpp: renderConsole と挿入モードフロー（cancelInsertMode、挿入のテキスト入力処理本体）を移動。stdout の "=== [ ... ] ===" 出力文言は1文字たりとも変えないでください。
forward decl は該当ヘッダに統合し、循環依存を作らないでください（game.h は state.h/document.h/graph.h/prop.h のみ依存、console.h は game.h 依存）。
CMakeLists.txt を更新し、refactor_plan.md §5 の検証を完遂（round-trip baseline / game-mode phase を特に確認）、§7 に引き継ぎ、git add . / commit / push。
```

### Session 6 — レイアウト / 描画抽出（layout / palette / view）

```
DDMEditor のリファクタリング Session 6 です（refactor_plan.md を読み継続）。
palette.h: Catppuccin Mocha パレット定数と wcagLuminance/wcagContrast を移動（hex 値は変更禁止）。
layout.h/cpp: chainDepth/layoutScene/familyChildren/familySubWidth/familyPlace/layoutFamily/nodePos/applyActiveLayout/windowSize/fitViewToScene を移動。
view.h/cpp: worldToScreen/screenToWorld/setSolid/viewModeName/asciiPreview/drawGuiLine/describeElement/outputPort/inputPort/familyBottomPort/familyTopPort/drawBezierEdge/drawNodeBox/drawGraphEdges/drawFamilyTreeEdge/drawFamilyView/ConstraintEdgeCounts/drawDashedRect/drawConstraintView/pickNode/zoomAt/switchScene/handlePointerEvent を移動。
describeElement の view別 props 表示順序・フィルタは仕様です。変更しないでください。
CMakeLists.txt を更新し、refactor_plan.md §5 の検証（selection / family / constraint / palette audit phase を確認）を完遂、§7 に引き継ぎ、git add . / commit / push。
```

### Session 7 — EDITOR add flow / props menu 抽出

```
DDMEditor のリファクタリング Session 7 です（refactor_plan.md を読み継続）。
editor.h/cpp: composeEditorProp/nextFreeId/editorStartText/editorStopText/editorValidateAdd/editorBeginAdd/editorCancelAdd/editorLeaveModeIfNeeded/editorBackspaceDialogue/editorEnterKey/editorYN/editorCommitAdd/editorRestoreToBaseline を移動。
props.h/cpp: propsNodeKeyOf/propsResolveCurrentNode/propsCurrentProp/propsOpen/propsClose/propsCommit/propsValidateEdit/propsOptionSwitchWarnings/propsApplyFieldValue/propsSetBoolField/propsToggleField/propsSetOption/propsSetBoolValueCurrent/propsNextField/propsUndo/propsRebuildGraph/propsMenuRows を移動（既存の forward decl は props.h に置き換える）。
warning 文言、undo の挙動（baseline完全復元・元書式の復元）、Document dirty 扱いをそのまま保持してください。
CMakeLists.txt を更新し、refactor_plan.md §5 の検証（editor / props phase を確認）を完遂、§7 に引き継ぎ、git add . / commit / push。
```

### Session 8 — HUD / 入力ディスパッチ抽出

```
DDMEditor のリファクタリング Session 8 です（refactor_plan.md を読み継続）。
hud.h/cpp: drawEditorOverlay/drawHud/drawGraphOverlay/printSelectedElementDetails を移動。HUD の表示項目・色・文言は仕様です。
input.h/cpp: SDL_AppEvent の中身（props優先処理、H/Bトグル、viewer graph keys, editor GUI keys, Q, runtime keys TAB/F/O/I/A/U/M/Y/N, pointer event 委譲, テキスト入力, 挿入モード分岐）をハンドラ関数へ分割して移動。main.cpp の SDL_AppEvent は薄いディスパッチ（イベント種別別に分岐しハンドラを呼ぶ）にします。キーの優先順位・遮断条件（editorMode, viewerUi, props active, gMoveMode）は仕様です。そのまま保持してください。
setUiMode は app.h/cpp（または main.cpp の薄層）に整理します。
CMakeLists.txt を更新し、refactor_plan.md §5 の検証を完遂、§7 に引き継ぎ、git add . / commit / push。
```

### Session 9 — selftest 抽出・main.cpp 薄層化

```
DDMEditor のリファクタリング Session 9 です（refactor_plan.md を読み継続）。
selftest.h/cpp を作成し、SDL_AppIterate の DDM_SELFTEST 分岐全体を runSelftest() に移動し、10 phase（round-trip baseline / selection / family / constraint / editor / props / font / palette audit / game-mode / data unchanged）を phase 別 static 関数に分割してください。期待値の実行時計算・ハードコード禁止の原則は維持します。
main.cpp は SDL_AppInit / SDL_AppEvent / SDL_AppIterate / SDL_AppQuit の薄層にします（frame loop・write policy・backup は app.h/cpp に抽出可）。SDL callbacks の名前と戻り値契約（SDL_APP_CONTINUE/SUCCESS/FAILURE）はそのまま。
refactor_plan.md §5 の検証を完遂（全 phase PASS・diff 空・data.txt 未変更）、§7 に引き継ぎ、git add . / commit / push。
```

### Session 10 — 最終整理・README 更新・完工

```
DDMEditor リファクタリングの最終セッションです（refactor_plan.md を読み、§7 の未解決事項をすべて解消してください）。
1. 全モジュールの include 依存を整理（不要include削除、循環依存ゼロ、各ヘッダ自己完結、pragma once 統一）。
2. 各ファイル冒頭に「このファイルの役割・見るべき場面」の短いコメントを追加。コメント内容そのものは原コメント（設計仕様）を移動・保持し、削除しない。
3. 最終検証: クリーンビルド、selftest 全 phase PASS・baseline と正規化後 diff 空、data.txt 未変更、通常起動（dummy driver）で異常終了しないこと。
4. ./README.md を更新してください。必須内容:
   - ビルド・実行・selftest・DDM_MODE の手順。
   - **作業の性質別 ファイル指針**（どこを見て、何を見なくてよいか）:
     prop bit レイアウト/値抽出 → prop.h/cpp（他ファイル不要）
     data.txt の読み書き・正格式・round-trip → document.h/cpp
     ノード/辺/scene索引の構築 → graph.h/cpp
     状態・enum・定数・global → state.h/cpp
     日本語フォント・テキスト描画 → guitext.h/cpp
     プレースホルダ展開 → placeholders.h/cpp
     コンテナ再構築・配信条件・タイマー・sendProperty → game.h/cpp
     console 出力・挿入モード → console.h/cpp
     色・WCAG → palette.h
     レイアウト・view座標系 → layout.h/cpp
     graph/family/constraint 描画・describeElement・視点操作 → view.h/cpp
     Element追加フロー → editor.h/cpp
     props メニュー編集・undo → props.h/cpp
     HUD・overlay・selected表示 → hud.h/cpp
     キー/マウスの割り当てと優先順位 → input.h/cpp
     自己検証の各 phase → selftest.cpp
     SDL callbacks・起動/終了・write policy → main.cpp（+ app系）
   - 「触らなくてよい」範囲（例: ゲームロジック変更なら guitext/selftest を読まない等）。
   - リファクタリング後のディレクトリ構成表。
5. §7 に完工記録（全セッションサマリ・残存リスク）を記入し、git add . / commit / push してください。
```

## 7. 引き継ぎ（各セッションがここに追記する）

書式（セッション完了ごとに追加、削除・編集はしない）:

```
### Session N — <日付>
- 実施内容:
- 作成/変更ファイル:
- 検証結果: build <PASS/FAIL> / selftest <PASS/FAIL・diff行数> / data.txt <unchanged>
- 未解決事項・次のセッションへの指示:
- 注意・仕様変更なし確認の要点:
```

- Session 1: *(未実施)*
