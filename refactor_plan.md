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
2. data.txt 正格式 `'dialogue', '0xHHHH-HHHH-HHHHHHHH',`（2-2-4 の '-' 区切り・lowercase16・trailing comma）。読み込みは prop 内の '-' をすべて無視するため区切り無旧書式も許す。（2026-11-26 ユーザー決定: 区切り文字は '|' ではなく '-' を採用。selftest の canonical_format 期待値も '-' に統一し、その後に baseline を再取得した。identifier `formatPropHex16Piped` と stdout ラベル `canonical_format pipe_split_2_2_4_0x_comma` は原名・原文のまま保持。）
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

### Session 1 — 2026-11-26
- 実施内容: baseline 取得（リファクタリング前 HEAD 6f56899 でビルド＋selftest 2回実行・出力完全一致を確認）。prop.h/prop.cpp に Element・prop bit レイアウト文書コメント・getProp*/hasProp*/getSlot*/setProp*/setSlot*/edit*Value（inline のまま原文のまま）と buildProperty（定義のみ prop.cpp、宣言は prop.h）を移動。document.h/document.cpp に Strict Document モデル（LineKind/RawLine/ElementRecord/Document/RoundTripReport はヘッダ、loadDocument/serializeDocument/verifyRoundTrip/printDocumentToStdout/backupData は定義を document.cpp、inline ヘルパー hexByte/hexNibble/isCommentLine/parseElementLine/formatPropHex16/formatPropHex16Piped はヘッダ）を移動。main.cpp は prop.h/document.h を include のみ。ロジック・identifier・出力文言は全て原文のまま（移動は行単位の転写のみ、transcribe 検証済み）。
- 作成/変更ファイル: prop.h(236行), prop.cpp, document.h(147行), document.cpp, main.cpp(7148→6599行), CMakeLists.txt(LIB_SOURCE に prop.cpp/document.cpp 追加。Gimmick.cpp は元から build 外 → 変更なし)
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=1 は baseline と一致）/ data.txt unchanged（git status に現れない）/ 通常起動スモーク: dummy driver で 3 秒以上生存・起動ログ正常
- baseline 保存先: `.cache/refactor_baseline/`（gitignore 対象）に baseline_selftest.txt(702行)・baseline_exit.txt(=1)・normalize.sed（ルールなし=恒等。2回実行で出力が完全一致し非決定行なし）。以後の照合は `SDL_VIDEO_DRIVER=dummy DDM_SELFTEST=1 ./build/DDMEditor` をリポジトリルートで実行し、`sed -f .cache/refactor_baseline/normalize.sed` → baseline と diff（空が必須）、exit code は baseline_exit.txt と一致させる。
- 未解決事項・次のセッションへの指示:
  - **HEAD の時点から selftest の `round-trip baseline` phase が FAIL（exit=1, phases=1）**。原因はコード内の既存の不整合: `formatPropHex16Piped` と `parseElementLine` は区切り文字 `-` を使用（data.txt の実書式も `-`）だが、selftest の canonical_format 検査は `'0010|0000|0010021d'`（`|`）を期待している（main.cpp 旧 L4937-4938）。リファクタリングは動作を変えてはいけないので **修正していない**。照合基準は exit=1・`FAIL round-trip baseline`・他 9 phase PASS の出力そのもの。`ALL PHASES PASS` にするには動作変更（selftest 期待値 or 区切り文字の統一）が必要で、リファクタリング範囲外 — ユーザーの判断事項として残す。§2/§4 の「パイプ区切り」記述は実コード・data.txt は `-` である点に注意（コメント原文のまま移動済み）。
  - Session 2+: `state.h` 経由の extern 化に進む。main.cpp の残りの行番号は大幅にずれている（§2 の行番号は参考、セクションコメント境界で探す）。
  - backupData の既定値は document.h の宣言側のみ定義（定義側で重複指定するとコンパイルエラー）。挙動は同一（無指定呼び backupData() も既定値が適用される）。
- 注意・仕様変更なし確認の要点: prop bit レイアウト文書・正格式コメント・write policy・keymap・selftest 出力はすべて未変更。build 対象は main+prop+document の 3 TU のみ（Gimmick 除外は現状維持）。実行はリポジトリルートから（./data.txt 解決）。環境メモ: ccache はワークスペース外に書くため、サンドボックス環境ではビルドにワークスペース外の書き込み許可が必要（通常環境では無関係）。

### Session 2 — 2026-11-26
- 実施内容: (a) 区切り文字 '-' の統一（ユーザー決定 2026-11-26）: selftest の canonical_format 期待値 `"0010|0000|0010021d"` → `"0010-0000-0010021d"`（main.cpp）、正格式を '|' で記述していたコメントを main.cpp/prop.h/document.h/document.cpp で '-' に更新。identifier `formatPropHex16Piped`・stdout ラベル `canonical_format pipe_split_2_2_4_0x_comma` は原名・原文のまま。(b) baseline 再取得: 旧 baseline との diff は canonical_format の match 行（0→1）と `FAIL round-trip baseline`→`ALL PHASES PASS`・exit 1→0 のみ。新 baseline を `.cache/refactor_baseline/baseline_selftest.txt`（701行・exit=0・全 phase PASS）として採用、旧 baseline は `baseline_pre_delimiter.txt` に保存。(c) graph.h/graph.cpp 抽出: EdgeKind/GraphEdge/NodeKind/classifyNodeKind/nodeKey/GraphNode/Graph を graph.h（inline 含む原文転写）、buildGraph 定義を graph.cpp（Document を受け取る純関数のまま）。main.cpp は graph.h を include のみ。§4 項目2 を '-' 区切りの記述に更新。
- 作成/変更ファイル: graph.h(115行)・graph.cpp(117行) 新規 / main.cpp(6599→6382行) / CMakeLists.txt(LIB_SOURCE に graph.cpp 追加) / prop.h・document.h・document.cpp（区切りコメント文のみ）/ refactor_plan.md（§4 項目2 更新・本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0 は baseline と一致）/ data.txt unchanged（git status に現れない）
- 未解決事項・次のセッションへの指示:
  - 照合基準は新 baseline（exit=0・`SELFTEST: ALL PHASES PASS`）。`.cache/refactor_baseline/baseline_selftest.txt` と `baseline_exit.txt`(=0) に照合する。`normalize.sed` は恒等のまま。
  - Session 3: 匿名 namespace のグローバルを state.h/state.cpp へ。注意: stFail/stPass・hexDec/binaryByte は他 TU から呼ばれるため state.h に宣言が必要（計画の「state.h は関数を置かない」は宣言のみ許す解釈に修正。定義は state.cpp）。ノード寸法定数・kPropsFields が palette 定数（後方の main.cpp）を参照するなら、state.cpp から見える形に調整が必要（Session 3 で実コードを確認して判断）。
  - グローバルは internal linkage → external linkage になる（動作同じ）。data.txt/todo.md は編集しない。git commit/push は行わない（ユーザーが後で実施）。
- 注意・仕様変更なし確認の要点: graph ロジック・identifier は行単位転写のまま（sed 転写・行数確認済み）。prop bit レイアウト・正格式（実挙動は '-'）・write policy・キー割り当て・selftest 出力形式は不変。canonical_format match=1 は '-' 正格式の仕様どおり。

### Session 3 — 2026-11-26
- 実施内容: 匿名 namespace の全局状態を state.h（宣言・型・enum・定数）/ state.cpp（定義）へ移動。state.h=257行: Element/Document/Graph の include、kFlavorParent・ノード寸法定数・kPropsUndoMax（constexpr 原文）、enum TimerAction/ViewMode/EditorStage/PropsFieldKind/PropsFieldRole/UiMode、struct EditorState/PropsFieldDef/PropsUndoEntry/PropsEditState（原文転写・設計文コメントも一緒に移動）、全 mutable global の extern 宣言、slot option 名配列の extern 宣言、inline slotOptionNames/slotOptionCount（原文）、inline hexDec/binaryByte（原文）、stFail/stPass/propsOptionLabel の宣言。state.cpp=152行: 全 global の定義・option 名配列/kPropsFields の定義・propsOptionLabel/stFail/stPass の定義（コメント原文のまま、行単位転写）。main.cpp は state.h を include、FreeType 以降の匿名 namespace は `namespace {` を再開口してそのまま内部に保持（Session 4 で extern 化）。
- 作成/変更ファイル: state.h(257行)・state.cpp(152行) 新規 / main.cpp(6382→6090行) / CMakeLists.txt(LIB_SOURCE に state.cpp 追加) / refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0 一致）/ data.txt unchanged
- 未解決事項・次のセッションへの指示:
  - 計画の「state.h は関数を置かない」は実現行コードに inline（slotOptionNames/slotOptionCount/hexDec/binaryByte）があったため「inline 原文＋宣言のみ置く」解釈で対応した（ロジック・identifier 不変）。kPropsFields が配列参照のため kPropsFieldCount は constexpr → extern const int（state.cpp で sizeof から計算、値同一）。static 配列は static 除去で extern 化（内部 linkage→外部、動作同じ）。
  - Session 4: main.cpp の残り匿名 namespace（L29-436 付近、FreeType 一式）を guitext.h/guitext.cpp へ。gGuiText は guitext.cpp 定義 + guitext.h extern。FreeType の typedef/定数/構造もともに移動。main.cpp の `namespace { … } // namespace`（436行）は Session 4 で解消。
  - Session 5+: state.h を依存として include する。循環依存を作らない。
- 注意・仕様変更なし確認の要点: 全 global の型・初期値・identifier 原文のまま（行転写のみ）。enum 値・定数値・kPropsFields の entries（bit shift/width/option 名）は 1 文字も変更なし。出力・挙動不変（全 phase PASS）。

### Session 4 — 2026-11-26
- 実施内容: guitext.h(164行)/guitext.cpp(266行) 抽出: FreeType dlopen エンジン一式（section コメント・kGuiTextPx/kGlyphCacheMax・DFTFaceRec/DFTGlyphSlotRec/FnFT typedef/kFT 定数/GlyphRec/GuiTextEngine はヘッダ原文転写、gGuiText 定義・loadGuiTextEngine/fontCandidates/tryGuiFont/initGuiText/shutdownGuiText/guiGlyph/drawGuiText 定義は cpp、inline decodeUtf8 は原文のままヘッダ、関数宣言は guitext.h）。dlopen/dlclose 対称性は定義転写のまま壊れていない（font phase・palette audit PASS）。main.cpp の残り匿名 namespace（FreeType 一式）を削除し include 化。
- 作成/変更ファイル: guitext.h・guitext.cpp 新規（guitext.cpp に <algorithm>/<filesystem>/<system_error> include 追加—旧 main.cpp の include 依存の再配置、ロジック不変）/ main.cpp(6090→5684行、5692) / CMakeLists.txt(LIB_SOURCE に guitext.cpp 追加) / refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0 一致・font phase 含む）/ data.txt unchanged
- 未解決事項・次のセッションへの指示:
  - **重要**: palette audit phase のソーススキャン（text/api guards）は元々 main.cpp のみ走査。Session 4 で RGBA32 が guitext.cpp へ移動したため走査対象を `{main.cpp, guitext.cpp}`（./, ../ 探索）に拡張した（出力形式・期待値の意味は同一: debugtext_code_uses=1・rgba32>0・rgba8888=0・rgb24>0・kMC 定義>0・6桁リテラル zero）。**Session 6**: `constexpr uint32_t kMC` が palette.h へ移動するので走査名 list に palette.h を追加すること（ヘッダも scan 対象に）。**Session 9**: selftest が selftest.cpp へ移動する（gGuiText.ready 参照・RGB24 readback の needle がそちらへ出る）ので走査名 list に selftest.cpp を追加すること。list にまだ存在しないファイルは probe が開かないので無害。
  - Session 5: placeholders/game/console 抽出。循環依存禁止（game.h は state.h/document.h/graph.h/prop.h のみ、console.h は game.h+state.h 等）。forward decl（props 関数・rebuild 群）は Session 5/7 でヘッダへ統合。
- 注意・仕様変更なし確認の要点: font パス候補・sanity check・negative cache・'~' truncation・RGBA32 バイト列コメントは原文のまま。gGuiText.ready の fallback guard（drawGuiLine）は main.cpp に残り、palette audit の debugtext 1 箇所・fallback のみは維持。出力・挙動不変。

### Session 5 — 2026-11-27
- 実施内容: placeholders.h/cpp（%(変数)展開: inline resolvePlaceholderVar はヘッダ原文、expandTextPlaceifiers 定義は cpp）、game.h/cpp（配信条件 isRead*/satisfies*/evalCond5・resetTimer/advanceTimers はヘッダ inline 原文、gTickTimer/gimmick/hasValidObjective/rebuildContainerB/C/rebuildFlavorContainer/rebuildDisplayList/rebuildObjectiveList/rebuildMoveDestinationList/enterMoveMode/exitMoveMode/processContainerLifecycle/sendProperty 定義は cpp・gTick の mt19937 static も gTickTimer と一緒に cpp へ。rebuild 群 forward decl は game.h に統合）、console.h/cpp（renderConsole・cancelInsertMode の定義+設計コメント）。AppMode enum・gAppMode/gInputText/gInputIsQuote/gInputIsOnetime/gInsertParent は Session 3 の対象だったものの main.cpp の匿名 namespace 外にあったため、このセッションで state.h/state.cpp へ移動（enum 原文転写、定義は state.cpp、extern 宣言は state.h）。main.cpp は該当領域(旧39-874, 3016-3045)を削除し placeholders.h/game.h/console.h を include。props forward decl(32-38)は Session 7 で props.h に置換予定なので現状維持。
- 作成/変更ファイル: placeholders.h(52行)/placeholders.cpp(36行)/game.h(96行)/game.cpp(569行)/console.h(10行)/console.cpp(128行) 新規 / state.h・state.cpp（AppMode 追記）/ main.cpp(4828行) / CMakeLists.txt（3 TU 追加） / refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0・round-trip baseline・game-mode phase 含む）/ data.txt unchanged
- 未解決事項・次のセッションへの指示:
  - 元 main.cpp は関数間に空行なしの密集書式のため、行番号境界抽出では closing `}` が次のチャンクに混入/欠落する事故 occurred（advanceTimers・hasValidObjective・renderConsole で検出・修復済み、意味は同一）。以後のセッションもチャンク端はビルド+diff で必ず確認すること。
  - Session 6: palette.h（kMC 定数・mcR/G/B・wcag helpers。`constexpr uint32_t kMC` の移動により palette audit の走査名 list に palette.h 追加必須）。layout.h/cpp・view.h/cpp の境界はセクションマーカー（VIEW_GENERAL/VIEW_FAMILY/VIEW_CONSTRAINT）で探す。setSolid/viewModeName は view、パレット定数と wcag は palette。
  - 挿入モードのテキスト入力処理本体は SDL_AppEvent に残存（Session 8 の input.cpp へ移動）。
- 注意・仕様変更なし確認の要点: console の "=== [ ... ] ==="・Verb/Objective 段・help 文言は renderConsole 原文転写（画面清席 \033[2J もそのまま）。game の配信条件 cond1-8・タイマー・gTick・move mode の suspend/restore・sendProperty の副作用順は原文のまま。出力・挙動不変（全 phase PASS）。

### Session 6 — 2026-11-27
- 実施内容: palette.h（Catppuccin Mocha 定数・mcR/G/B・mcBlendTint・kMCFill*・wcag helpers を原文転写。wcagLuminance/wcagContrast のみヘッダ単独配置のため inline キーワード追加—ロジック・値不変）。layout.h/cpp（nodePos inline・applyActiveLayout・windowSize・fitViewToScene・chainDepth・layoutScene・familySubWidth・familyPlace。設計コメントも該当定義へ移動）。view.h/cpp（worldToScreen/screenToWorld/setSolid 両オーバーロード・viewModeName・ports inline・asciiPreview・drawGuiLine・describeElement（default arg はヘッダ宣言側のみ、cpp 定義側は削除—呼び出し挙動同一）・drawBezierEdge/drawNodeBox/drawGraphEdges/drawFamilyTreeEdge/drawFamilyView/ConstraintEdgeCounts/countConstraintEdges/drawDashedRect/drawConstraintView/pickNode/zoomAt/switchScene/handlePointerEvent）。main.cpp は該当領域（旧45-412, 454-1046, 2064-2181）削除、include 追加。printRoundTripReport/printSelectedElementDetails は main.cpp 残存（Session 8 で hud.cpp / Session 9 で selftest 化を確認し最終整理で document.h/cpp・hud.cpp へ整理）。
- 作成/変更ファイル: palette.h(64行)/layout.h(25行)/layout.cpp(221行)/view.h(106行)/view.cpp(749行) 新規 / main.cpp(3744行) / CMakeLists.txt(layout.cpp/view.cpp 追加) / main.cpp palette audit スキャン list を {main.cpp, guitext.cpp, palette.h, view.cpp} に拡張（kMC 定義・debugtext fallback・RGBA32・readback の needle が分散したため。出力形式・期待値不変）/ refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0・selection/family/constraint/palette audit phase 確認）/ data.txt unchanged
- 未解決事項・次のセッションへの指示:
  - view.h に propsOpen/printSelectedElementDetails の temporary forward decl あり（handlePointerEvent から呼ばれるため）。Session 7 で props.h に置換（view.cpp は props.h を include）、Session 8 で printSelectedElementDetails は hud.h 宣言へ（view.cpp は hud.h include。ヘッダ間循環は作らない: view.h は hud.h を含まない）。
  - Session 7: editor/props 抽出。props.h に propsOpen/propsClose/propsRebuildGraph 等を宣言し main.cpp の forward decl（旧32-38）を削除。describeElement の EDITOR variant 表示順は view.cpp にあるまま（props.h 経由の kPropsFields は state.h 経由で見える）。
  - チャンク端の `}` 事故は continue（drawConstraintView・nodePos で検出・修復済み）。各抽出後にビルドで必ず検証。
- 注意・仕様変更なし確認の要点: パレット hex は公式値のまま（wcag 閾値 0.03928/2.4 も原文）。describeElement の view 別表示順序・フィルタ、ノード寸法・bezier・dashed rect・constraint 数え（ghost 含む）・pick/zoom/switch の挙動は原文転写。出力不変。

### Session 7 — 2026-11-27
- 実施内容: editor.h/cpp（composeEditorProp・nextFreeId・editorStageName・editorStartText/StopText/ValidateAdd/BeginAdd/CancelAdd/LeaveModeIfNeeded/BackspaceDialogue/EnterKey/YN/CommitAdd/RestoreToBaseline の定義+設計コメント原文転写。editorCommitAdd の runtime 同期（rebuildContainerC/DisplayList/ObjectiveList）は game.h を include で解決）、props.h/cpp（propsNodeKeyOf はヘッダ inline 原文、resolve/current/open/close/commit/validate/optionSwitchWarnings/applyFieldValue/setBool/toggle/setOption/setBoolValueCurrent/nextField/undo/rebuildGraph/menuRows の定義+設計コメント。propsApplyFieldValue の default arg はヘッダ宣言側のみ・cpp 定義側は delta のみ。propsRebuildGraph の buildGraph→applyActiveLayout→fitViewToScene→verifyRoundTrip は layout.h include で解決。kMC 色は palette.h include）。main.cpp の props forward decl 削除・editor.h/props.h include、view.h の propsOpen temporary decl 削除（view.cpp は props.h include）。
- 作成/変更ファイル: editor.h(21行)/editor.cpp(291行)/props.h(28行)/props.cpp(452行) 新規 / main.cpp(3017行) / view.h・view.cpp（forward decl 整理）/ CMakeLists.txt（editor.cpp/props.cpp 追加） / refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0・editor/props phase 確認）/ data.txt unchanged
- 未解決事項・次のセッションへの指示:
  - main.cpp に残る: printRoundTripReport・printSelectedElementDetails・drawEditorOverlay/drawHud/drawGraphOverlay・SDL callbacks。Session 8 で hud.h/cpp（draw系+printSelectedElementDetails）・input.h/cpp（SDL_AppEvent のハンドラ群）・setUiMode は app.h/cpp（このセッションで作成可）へ。view.cpp の printSelectedElementDetails temporary decl は hud.h include に置換。printRoundTripReport は Session 9 で selftest.cpp から呼ばれる（宣言は document.h に置くのが自然 — round-trip 報告なので document.h/cpp へ移動推奨）。
  - Session 9 で palette audit スキャン list に selftest.cpp 追加（gGuiText.ready・RGB24 needle が移動するため）。HUD が main.cpp から view.cpp/…へ分散しても readback の kMC は palette.h にあるまま。
- 注意・仕様変更なし確認の要点: warning 文言（props: edit rejected / undo restore / graph rebuild / editor: added line / cancelled（document unchanged））原文。undo の baseline 完全復元・元書式復元・dirty 扱い、undo stack 上限 256 の挙動不変。add flow の stage machine・child commit の forward decl は原文のまま。

### Session 8 — 2026-11-27
- 実施内容: hud.h/cpp（printSelectedElementDetails・drawEditorOverlay・drawHud・drawGraphOverlay の定義+設計コメント原文転写）、input.h/cpp（SDL_AppEvent の本体を handleAppEvent へ移動し static ハンドラへ分割: handlePropsKeys(bool returned true→CONTINUE 委譲, 文言・優先順不変)・handleViewerKeys・handleEditorGuiKeys(key, editorMode)・handleRuntimeKeys・handleInsertTextEvent/QuoteEvent/OnetimeEvent。ディスパッチ順序=QUIT→ESC(editor add 取消)→ESC(挿入取消)→pointer 委譲→editor dialogue text→NORMAL(props→H/B→viewer→editor GUI→Q→runtime)→挿入3モードは元の順序そのまま。main.cpp の SDL_AppEvent は薄いディスパッチ（handleAppEvent 委・SDL callbacks の戻り値契約不変））、app.h/cpp（setUiMode 移動・設計コメント付随）、printRoundTripReport を document.h/cpp へ移動（round-trip 報告なので document に統合）、props.cpp に props menu セクションマーカー復元、view.h の temporary forward decl 削除（view.cpp は hud.h include）。
- 作成/変更ファイル: input.h(6行)/input.cpp(307行)/app.h(5行)/app.cpp(21行)/hud.h(12行)/hud.cpp(311行) 新規 / main.cpp(2412行・薄い AppInit/AppEvent/AppIterate/AppQuit へ）/ document.h・document.cpp(printRoundTripReport 追加) / props.cpp(マーカー・comment 配置修正) / view.h・view.cpp / CMakeLists.txt（3 TU 追加） / refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0・keymap 相关 phase・game-mode phase 確認）/ data.txt unchanged
- 未解決事項・次のセッションへの指示:
  - Session 9: main.cpp の SDL_AppIterate の DDM_SELFTEST 分岐全体を selftest.h/cpp の runSelftest() へ移動し、10 phase を phase 別 static 関数に分割。実行時期待値計算・ハードコード禁止の原則維持。selftest.cpp は全モジュール include（guitext・document・graph・state・game・console・editor・props・view・layout・hud・app・input が必要になる可能性）。palette audit のスキャン list に selftest.cpp を追加（gGuiText.ready 参照・RGB24 readback の needle が selftest.cpp へ移動するため）。フレームループ・write policy・quit 処理は app.h/cpp に抽出可（SDL callbacks の名前は main.cpp に残す）。
  - main.cpp の selftest 分岐には大量の print と stFail/stPass 使用がある（宣言は state.h にある）。
- 注意・仕様変更なし確認の要点: キー優先順位・遮断条件（editorMode/viewerUi/props active/gMoveMode/挿入モード）はそのまま。H/B は両 mode で動作・Q はどちらの mode でも SUCCESS・挿入 3 モードのフロー（buildProperty・containerA/B push・rebuild 群）原文。HUD/overlay の色・項目・文言不変。出力不変（全 phase PASS）。

### Session 9 — 2026-11-27
- 実施内容: selftest.h/cpp（runSelftest() + 10 phase 別 static 関数: phase_roundtrip_baseline/selection/family/constraint/editor/props/font/palette_audit/game_mode/data_unchanged。期待値の実行時計算・ハードコード禁止の原則は各 phase の原文のまま。runSelftest は gUiMode==UI_GAME のとき viewer 復帰→printRoundTripReport/printDocumentToStdout→phase 群→集約（ALL PHASES PASS→true / FAIL <phase>→false）を原文転写）。main.cpp は SDL_AppInit / SDL_AppEvent（handleAppEvent 委譲）/ SDL_AppIterate（薄い frame loop: DDM_SELFTEST→runSelftest、needRedraw→renderConsole+挿入プロンプト、UI_GAME 分岐、drawGraphOverlay+fps 調整）/ SDL_AppQuit（appQuit 委譲）の薄層（202行）。app.cpp に appQuit（backup policy・legacy ../data.txt write-back・shutdownGuiText・renderer/window 破棄）を追加。palette audit のスキャン list は {"main.cpp","guitext.cpp","palette.h","view.cpp","selftest.cpp"}（selftest.cpp 追加済み・Session 8 指示どおり）。**リンク bug 修正**: selftest.cpp の SDL_AppEvent 手書き宣言が C++ linkage（mangled 名）だったため main.cpp の extern "C" 定義が解決されず、未解決名を Final.a の WindowCreator.cpp.o（C++ SDL callbacks）が引き込んでリンク失敗（中断セッションの stop 原因）。宣言を `extern "C"` に修正（呼び出し・挙動は元と同一: main.cpp の SDL_AppEvent→handleAppEvent と同じ経路）。
- ユーザー判断（リファクタリング範囲外・本セッションで反映）: (a) data.txt の 1 行の会話文変更（`'いまお店にいるお客さんで…'` → `'あのお客さんたち、一緒に仕事したらいいのにね。'`・prop は同一 0x0000-0000-00e62021・9966→9926 bytes）。(b) CMakeLists.txt から FinalSTATIC（../Final/build/Final.a・include dir・link）を削除（「いま使っていない」— リンク不要。SDL3 のみ）。
- 作成/変更ファイル: selftest.h(5行)/selftest.cpp(2241行) 新規（extern "C" 宣言修正含む） / main.cpp(2412→202行・薄層) / app.h・app.cpp（appQuit 追加・setUiMode は Session 8 のまま） / CMakeLists.txt（selftest.cpp 追加・FinalSTATIC 削除はユーザー実施） / .cache/refactor_baseline（baseline 再取得・下記） / refactor_plan.md（本引き継ぎ）
- 検証結果: build PASS（Final.a 削除後のクリーンビルド・全 TU）/ selftest PASS・exit=0・`SELFTEST: ALL PHASES PASS`・2回実行で出力完全一致（決定性確認）/ **照合**: Session 8 出力 after_s8.txt との diff は data.txt のユーザー編集由来の行のみ（bytes 9966→9926、該当会話文、その会話文を描く画面の text/明色ピクセルカウント ±2・match=1/PASS 全維持）。palette audit は after_s8 と同一（kmc_definitions=23・rgb24_readback=8・debugtext=1・match=1）。data.txt 実行前後で 9926 bytes 変化なし（data_unchanged phase match=1・git status の M はユーザー編集のみ）/ 通常起動スモーク: dummy driver で 3 秒生存・起動ログ正常（exit=124 は timeout kill）。
- baseline 再取得: 中断セッションが 12:24 に取得した baseline は「Session 9 途中 + data 編集」の partial 状態（palette audit FAIL・kmc=22・rgb24=0）で不正。`baseline_selftest.txt` を現検証済み出力（701行・全 phase PASS・exit=0）に再取得し、旧 partial は `baseline_invalid_mid_s9.txt` に退避。`baseline_exit.txt` は `exit=0` 形式の 1 行（照合は `echo "exit=$?" > after_exit.txt; diff after_exit.txt baseline_exit.txt`）。`baseline_pre_dataedit.txt`（43455行・data 編集前の S8 出力）と after_s8.txt は保持。以後の照合は `sed -f .cache/refactor_baseline/normalize.sed`（恒等）→ diff 空・exit 一致。以後のセッションで data.txt の内容が変わると baseline と差分が出る（data-driven 行は許容・それ以外が空であること）。
- 未解決事項・次のセッションへの指示:
  - Session 10: 全ヘッダの include 依存整理・循環依存ゼロ・pragma once 統一・ファイル冒頭コメント。main.cpp の include 群には旧 monolith 由来の未使用 include（<algorithm>/<cmath>/<regex>/<random>/<unordered_map> 等）が残っている — 削除可（動作不変、ビルド+diff で確認）。app.cpp は `#include "app.h"` がファイル中間に重複（第 2 ブロックの転写名残）— Session 10 の include 整理で冒頭に統合可。
  - git commit/push は従来どおりユーザーが後で実施（本セッションも未 commit）。
- 注意・仕様変更なし確認の要点: selftest の出力形式・phase 名・期待値実行時計算（data.txt 依存）・stFail/stPass 集約・SDL callbacks の名前と戻り値契約（CONTINUE/SUCCESS/FAILURE）は原文のまま。write policy（data.txt に書かない・legacy ../ のみ・backup 既定 off）・DDM_MODE/DDM_SELFTEST 挙動不変。extern "C" 化はリンクage のみで実行挙動・出力不変。

### Session 10 — 2026-11-27（完工）
- 実施内容: (a) include 依存整理: 全ヘッダは standalone syntax OK（自己完結・pragma once 全統一済み・循環依存ゼロを確認 — graph は prop<document<graph<state<… の一方向）。cpp の include を「実使用（コメント・文字列リテラルを除くトークン）+コンパイル検証」で削減し、使用している std/SDL/プロジェクトヘッダは直接 include に正規化（own header → std → SDL → プロジェクト依存順のブロック、重複・旧 monolith 由来の未使用 include 削除、main.h/main.cpp の own 扱い例外、selftest.cpp に誤追加された Gimmick.h を削除）。app.cpp の中間ブロックの重複 app.h を統合。(b) 各ファイル冒頭に「役割・見るべき場面」の // コメント追加（既存の設計文コメント・原文コメントはそのまま保持。palette 走査対象 5 ファイルには // 形式のみ使用＝走査から除外されるため audit 数値不変）。(c) README.md 更新: 対象記述（モジュール構成・動作不変の明記）、§1 概要の単一ファイル記述→分割記述、§2 ビルド（FinalSTATIC 削除・LIB_SOURCE 対象列挙）、§8 palette audit の走査対象 5 ファイル注記、§10 をリファクタリング後のディレクトリ構成表に置換、§11「作業の性質別 ファイル指針」+「触らなくてよい」追加、§12 旧単一ファイル版の領域対応（refactor_plan §2 への誘導）。(d) ユーザー判断の反映（本セッション前の実施・記録のみ）: CMakeLists.txt から FinalSTATIC 削除（リンク不要）、data.txt の 1 会話文編集（baseline に吸収済み）。
- 作成/変更ファイル: 全 *.cpp の include ブロック正規化 + 冒頭コメント / prop.h・document.h・graph.h に「見るべき場面」追記 / README.md 更新 / refactor_plan.md（本引き継ぎ） / .cache/refactor_baseline（s10_final.txt 等・照合用。git 対象外）
- 検証結果: クリーンビルド（rm -rf build → cmake 再設定 → build）PASS / selftest 照合 PASS（正規化後 diff 0行・exit=0・10 phase・ALL PHASES PASS）/ data.txt 実行前後で変化なし（md5 同一・git の M はユーザー編集のみ）/ 通常起動スモーク: dummy driver で 3 秒生存・異常終了なし
- 完工サマリ（全セッション）: Session 1 基盤+prop/document 抽出・baseline 固定 / Session 2 区切り '-' 統一・graph 抽出・baseline 再取得 / Session 3 state.h/cpp / Session 4 guitext / Session 5 placeholders/game/console / Session 6 palette/layout/view / Session 7 editor/props / Session 8 hud/input/app + printRoundTripReport→document / Session 9 selftest 抽出+main.cpp 薄層（extern "C" リンバグ修正・Final.a 削除反映・baseline 再取得）/ Session 10 include 整理・コメント・README・完工。全セッションで出力・挙動・data.txt の不変性を照合（最終 diff 空・exit 一致）。
- 残存リスク・注意:
  - Gimmick.h/Gimmick.cpp は今も参照ゼロ・ビルド対象外（現状維持）。Final の include ディレクトリ削除により Gimmick.cpp は単独ではコンパイル不可（../Final のヘッダ参照）。将来ビルドに含めるなら Final の include/path を復活させる必要がある。
  - cpp の include は「実使用」基準の最小集合+own header。一部の型はヘッダ側の transitive include に依存している（ヘッダ自己完結が保たれている限り安全）。ヘッダの include を将来削除する場合は cpp 側も確認すること。
  - baseline の正当性: 現 baseline（.cache/refactor_baseline/baseline_selftest.txt・exit=0）は「Session 9 検証済み出力 + ユーザーの data.txt 編集」を反映したもの。以後 data.txt の内容を変更すると data-driven 行（bytes・該当会話文・その描画ピクセルカウント）の差分が出る（それ以外の差分は仕様違反として調査）。
  - git commit/push: Session 2〜9 はユーザー実施のため未 commit。本セッション（最終）で一括 commit/push を実行（ユーザーの data.txt・CMakeLists 変更も含む）。
- 注意・仕様変更なし確認の要点: prop bit レイアウト・正格式（'-'）・write policy・キー割り当て・selftest 出力契約・パレット hex・WCAG 閾値はすべて不変（最終 selftest 全 phase PASS・diff 空で確認）。コメント追加は // のみ（palette audit の源码監査は line comment を除外するため数値同一: kmc=23・rgb24=8・debugtext=1・match=1）。
