# DDMEditor 機能マニュアル

対象: `./main.cpp`（2026-11-18 時点の現行コード、7001 行）に基づく機能マニュアルです。

---

## 1. 概要

DDMEditor は **SDL3** を使った対話型アプリで、次の 3 つの機能を 1 つのバイナリに統合しています。

1. **データビューア（graph viewer）** — `data.txt` に保存された「要素（Element）」をノード/エッジのグラフとして描画する GUI（4 種類のビューモード、HUD、日本語描画）。
2. **フォーカスゲーム（console runtime）** — 端末 stdout に 2 段の選択コンソールを表示し、キー入力で会話グラフを「送信」しながら進行するシミュレーション（Verb / Objective の選択、条件判定、タイマー、シーン遷移）。
3. **エディタ（EDITOR モード）** — 新しい要素行の追加（stage 入力フロー）と、既存要素の prop（64bit プロパティ）の全フィールド編集（props パネル・undo stack）。

- 言語: C++20、単一ファイル `main.cpp`。SDL3 の callbacks API（`SDL_MAIN_USE_CALLBACKS`）で `SDL_AppInit / SDL_AppEvent / SDL_AppIterate / SDL_AppQuit` を実装。
- 描画色は **Catppuccin Mocha** の定数パレットに統一（数値 RGB リテラルは定数定義のみ）。
- 日本語テキストは **FreeType を dlopen** で動的ロードして描画。freetype / 日本語フォントが無い環境では ASCII（`?` 代替）にフォールバック。
- フレーム目標は 33ms（約 30fps）+ VSync。

**書込みポリシー**: 原則として `data.txt` に一切書き戻しません（write は stdout / HUD の export のみ）。例外は後述の「レガシー write-back」と「quit backup」のみ。

---

## 2. ビルドと実行

### ビルド（CMakeLists.txt）

- `cmake_minimum_required(4.4.2)`, C++20 必須。ccache があればビルドに使用。
- 必須リンク: `SDL3::SDL3`、および `FinalSTATIC`（`/home/herb1s/Projects/NEELA/Final/build/Final.a` + include ディレクトリ）。
- ビルド対象は `main.cpp` のみ。リポジトリ内の `Gimmick.cpp` / `Gimmick.h` は **main.cpp から参照されておらずビルドにも含まれません**（main.cpp 内の `gimmick()` 関数は独立実装）。

```sh
cmake -S . -B build
cmake --build build
```

### 実行と起動シーケンス

```sh
./build/DDMEditor            # リポジトリルートから実行
cd build && ./DDMEditor      # build/ 直下から実行
```

- データファイルは `./data.txt` → 無ければ `../data.txt` の順でロード。どちらも開けなければ起動失敗（終了）。
- 初期状態: コンソール runtime は `gScene=1, gRoot=0, gParent=0, gUnlocks=0, gEyelevel=1, gCallingNumber=1031`、タイマー 0〜3 は停止/0。ビューの初期 scene は「0 を除いた最小 scene」（data.txt では `0x1`）。初期 view は `VIEW_GENERAL`。
- ウィンドウは 1280×760（リサイズ可）、タイトル「DDMEditor Focus Window」。
- 環境変数:
  - `DDM_MODE=game` … 起動から **game UI**（コンソール + 入力系統のみ、graph/HUD を描かない）。既定は viewer。実行中は `H` キーで往復切替可能。
  - `DDM_SELFTEST=<非空>` … 自己検証モード（→ 第 8 節）。対話実行の通常キー操作は走りません。

---

## 3. データフォーマット（data.txt）

1 行 = 1 要素。正規形式:

```
'dialogue', '0xHHHHHHHHHHHHHHHH',
```

- `dialogue`: 表示文字列（日本語・空白・`'` を含めることができる。greedy 解析で「最も後ろの valid な区切り」を prop とみなす）。trailing comma は省略可、hex は 1〜16 桁（`0x` なしも許容）。
- コメント行（`//` `/*` `*` で始まる行）と空行は graph に含めないが、**原文のまま保持**される。
- 解析できなかった行は warning を出力し、原文のまま保存（消したり直したりしない）。

ノードの単位は `(scene, id)`。**同じ id でも別 scene は別ノード**として扱います。同一 `(scene, id)` の重複行は「最初の行のみ」採用し、warning を出します。

### prop（64bit）のビット配置

| bits | 内容 |
|---|---|
| 0–7 | id |
| 8–15 | parent（`0` = root / 親なし） |
| 16 | hasRead（送信済み印） |
| 17 | isQuote |
| 18 | isOnetime |
| 19 | vacant（reader は使わない・オプション） |
| 20–23 | scene（0x0–0xF） |
| 24–25 / 32–39 | slot0 の option index / value |
| 26–27 / 40–47 | slot1 の option index / value |
| 28–29 / 48–55 | slot2 の option index / value |
| 30–31 / 56–63 | slot3 の option index / value |

各 slot は「index が該当 option に一致するときだけ value を意味として読む」方式です。**value 0 はどの option でも none**（data.txt の既存慣習）。slot の option は index が 1 つなので**排他**（同一 slot に複数の option を立てられない）。

| slot | option index | 意味 | value の解釈 |
|---|---|---|---|
| 0 | 0 | requiredReadId | 現 scene で「読済み」必須の要素 id |
| 0 | 1 | unlocks | 送信時に `gUnlocks` に OR される 8bit フラグ |
| 1 | 0 | child | 送信時に子として descend する id |
| 1 | 1 | excludedReadId | 「未読」必須の id |
| 1 | 2 | newRoot | 送信時に `gRoot` へ立てる id（unlocks/eyelevel/tick をリセット） |
| 1 | 3 | downUnlocks | 8bit mask。送信時に `gUnlocks &= ~mask` で指定bitのフラグだけを下げる（id ターゲットではない・target 不在の warning なし） |
| 2 | 0 | timerSetter | value bits0–1: timerId(0–3), bits2–3: vacant(保持), bits4–5: action(0=none,1=start,2=stop,3=reset) |
| 2 | 1 | unlocks | 送信時に OR（slot0 unlocks と OR 結合） |
| 2 | 2 | requiredUnlockFlags | `gUnlocks` に含まれていなければ表示不可 |
| 2 | 3 | excludedReadId | 「未読」必須の id（slot1 とは別枠） |
| 3 | 0 | timerTarget | value bits0–1: 演算子(0=none,1=`<`,2=`>`,3=`==`), bits2–3: timerId, bits4–7: count |
| 3 | 1 | newScene | 送信時にシーンを変更する値（`0` は「変化なし」sentinel） |
| 3 | 2 | excludedUnlockFlags | `gUnlocks` に完全に含まれていれば表示不可 |
| 3 | 3 | eyelevelToggle | value bits0–3: eyelevel(0=無条件), bits4–7: toggleEyelevel(0=toggle なし) |

特殊ノード: `dialogue` が `dummy` / `template` / `empty` のものは「構造ノード」として別色・別扱いで描画・処理されます（dummy は親 descend の中継点として runtime で優先処理）。

### % による値展開（表示時のみ）

表示直前に `%(変数名)` をその時点の値（10 進）に置換します。格納された dialogue は変更しません。対象変数:

`gTick, gScene, gRoot, gParent, gUnlocks, gEyelevel, gTimer0..gTimer3, gTicketNumber, gCallingNumber`

対象外の `%(name)` はリテラルのまま出力します。展開されるのはコンソール表示と `D` ダンプの「expand」行のみです。

### ラウンドトリップ（壊さない約束）

- `serializeDocument()` は **dirty（編集された）要素行だけを正格式で再生成**し、未編集行・コメント・空行・改行（`\n` / `\r\n` / 改行なし最終行）は原文をそのまま復元します。未編集文書では `serialize == 元のバイト列`（EXACT）が成立します。
- `verifyRoundTrip()` は baseline（load 時の原文）との行単位 diff を報告します（added 行数 / edited 行数 / diff が「追加行＋dirty 行のみ」か）。編集・追加後もこの性質を保つことが自己検証で確認されます。

---

## 4. キー操作一覧

### 共通（どちらの UI でも）

| Key | 動作 |
|---|---|
| `Q` | 終了（ウィンドウ閉じも終了） |
| `H` | viewer / game の UI モード切替 |
| `B` | quit 時の backup（`../data.txt → ../data.txt.bak`）トグル（既定 off） |
| `ESC` | 挿入モードの取消（runtime） |

### runtime コンソールキー（game / editor 以外の画面で有効）

| Key | 動作 |
|---|---|
| `i` / `o` | 選択の左 / 右移動（Verb 段では Verb、Objective 段では Objective を循環選択。Verb 選択中の変更は Objective 再構築） |
| `a` | Verb 段 → Objective 段へ移動（`gParent == gRoot` のときのみ） |
| `u` | Objective 段 → Verb 段へ戻る（移動モード中は解除しない） |
| `f` | 選択中要素の送信（第 6 節「送信」参照） |
| `TAB` | 要素の挿入フロー開始（テキスト入力 → `isQuote? <y/n>` → `isOnetime? <y/n>` → 追加し続けて INSERT 状態へ復帰） |
| `M` | 「移動する」コンテナ表示のトグル（現 scene に id==0x04 の要素があるときのみ。選択状態は退避→復元） |

EDITOR モードではこれらの runtime キー（`TAB F O I A U M Y N`）は遮断されます（Y/N はエディタ側で使用）。

### viewer GUI キー（viewer UI のときのみ）

| Key | 動作 |
|---|---|
| `1` | VIEW_GENERAL（parent/child の bezier グラフ） |
| `2` | VIEW_FAMILY（階層フォレスト、top-down） |
| `3` | VIEW_CONSTRAINT（制約エッジの overlay） |
| `4` | EDITOR（追加フロー + props パネル） |
| `V` | graph overlay の描画トグル（HUD は残る） |
| `P` | HUD の export（serializeDocument の全文表示）パネルトグル |
| `X` | 終了時に全文を stdout へ export するトグル |
| `E` | 現在 Document の serialize を stdout に dump |
| `R` | ラウンドトリップレポートを stdout に出力 |
| `D` | 選択中ノードの詳細（raw line / dialogue / prop 分解）を stdout に dump |
| `L` | active layout 再適用 + scene に view を fit |
| `0` | view を scene に reset（fit） |
| `[` / `]` | 前 / 次 scene へ切替（applyActiveLayout + fit） |
| `+` / `-` | ズーム（ホイールでもズーム、カーソル位置基準） |
| 矢印 | パン（マウスドラッグでもパン） |
| 左クリック | ノード選択（詳細を stdout に出力）。EDITOR の parent/child stage では候補選択、idle では props パネル open。空白領域ドラッグ = パン |
| 右クリック | EDITOR の idle でノードを選択し props パネル open |
| `ESC` | 優先度順に: props パネルを閉じる → add flow 取消 → ノード選択解除 |

### EDITOR 追加フローキー（mode 4・panel 非 open）

| Key | 動作 |
|---|---|
| `ENTER` | stage 進行: idle → dialogue → quote → onetime → parent → child → commit（quote/onetime の ENTER は no として扱う。child の ENTER は現在の値で commit） |
| テキスト入力 | dialogue 入力（UTF-8・空白をそのまま保持。空 dialogue は不可） |
| `BACKSPACE` | dialogue の末尾 1 codepoint 削除（multibyte を壊さない） |
| `Y` / `N` | quote / onetime の yes/no 回答。child stage の N = child を none に戻す |
| 左クリック | parent / child stage でノードを候補として選択（id 0 は parent では root、child では none に） |
| `G` | runtime 同期トグル（既定 off。on のとき commit で containerA/B に反映） |
| `ESC` | 追加取消（Document は変わらない） |

追加の validation（commit 拒否理由として HUD に表示）: 空 dialogue / scene の id が 0xFF まで使用済み（追加不可）/ 自己 parent / parent id が現 scene に存在しない / child 指定が現 scene に存在しない id。

追加は **Document の末尾に正格式の 1 行を追加**し、graph 再構築・layout 再適用・新ノードの選択・fit を行います。diff は「追加行のみ」で baseline は壊れません。

### props パネルキー（mode 4・idle でノード選択/right-click で open）

| Key | 動作 |
|---|---|
| `TAB` | 次の項目へ（循環） |
| `ENTER` / `SPACE` | 現在の項目をトグル（option cycle / bool 反転 / enum cycle / uint は最下位 bit 反転） |
| `+` / `-` | 値 ±1（範囲外は clamp + warning） |
| `Y` / `N` | bool 項目は true / false、uint・enum は max / 0 |
| `U` | undo（1 段復元・元書式の line text / dirty も戻る） |
| `G` | graph 再構築のみ（runtime 同期なし） |
| `ESC` | パネルを閉じる（選択・undo stack は保持） |

panel open 中は `+/-`（ズーム）・`ENTER`（add flow）・`ESC`（選択解除）に優先します。`1-4 V P X E R D L 0 [ ] 矢印 Q` は共通経路のまま通過します。

### props 編集対象フィールド（表示順 = セクション順）

1. **basic flags**: hasRead(bit16) / isQuote(bit17) / isOnetime(bit18) / vacant(bit19)
2. **id/parent/scene**: id(0x00–0xFF) / parent(0x00–0xFF, 0=root) / scene(0x0–0xF)
3. **slot0**: option(none/requiredReadId/unlocks) / value
4. **slot1**: option(none/child/excludedReadId/newRoot/downUnlocks・排他) / value
5. **slot2**: option(none/timerSetter/unlocks/requiredUnlockFlags/excludedReadId) / value + timerSetter の timerId・action（部分フィールド）
6. **slot3**: option(none/timerTarget/newScene/excludedUnlockFlags/eyelevelToggle) / value + timerTarget の operator・timerId・count、eyelevel・toggleEyelevel（部分フィールド）

validation の挙動:

- 範囲外入力 → **clamp + warning**（HUD に yellow）
- **id の duplicate**（現 scene で他ノードが持つ id への変更）→ 編集を適用せず warning のみ（reject）
- id 系ターゲット（parent/child/excluded/requiredReadId/newRoot）が現 scene に無い id → **編集は許す + warning**（constraint viewer の ghost 枠や `par!`/`chi!` badge で確認できる）
- timerSetter/timerTarget/eyelevel の部分フィールドを option 非立てで編集 → 値は実効 inert（編集は許す + warning）
- option 切り替えは value を保持する（意味が変わる）。none に戻す場合は元の index に復元し value=0 にする
- **dialogue は編集しない**（別 panel・原文のまま）
- undo stack は最大 256 段。全 undo で baseline（load 時の原文）に完全復元（round-trip EXACT）できることが自己検証で確認されます

---

## 5. ビューア（graph viewer）機能

### VIEW_GENERAL（key 1）

- scene 単位でノードを「parent chain の depth = 列、id 昇順 = 行」に配置し、parent→id を sapphire、id→child（slot1）を peach の bezier + 矢印で描く。dangling（同 scene に無い id）は線を引かずノード badge に `par!` / `chi!` を表示。
- ノード箱: content = surface0 + blue 枠、dummy = surface1、template = peach tint、empty = green tint。左ストライプ: quote = red、onetime = mauve。右上の green 四角 = hasRead。入出力ポート、id/par/sc badge（sky）、newRoot badge（mauve）、dialogue 本文（text）、flags 行。
- 選択ノードは lavender 枠。HUD に選択ノードの prop 詳細（general 表示順）を出す。

### VIEW_FAMILY（key 2）

- parent field の階層関係を「親が上・子が下」の top-down フォレストに**決定性**レイアウト（同じ data.txt → 同じ座標）。root 扱い = `parent==0` または dangling parent。cycle 等 root が無い component も weakly-connected に 1 tree として配置（全ノードを配置する）。
- 階層 edge = sapphire（縦 bezier）。slot1 の explicit child はツリーに含めず横 bezier で別経路描画（peach）。parent field と slot1 child が別ノードを指す**矛盾 edge は yellow**。
- mode 切替で VIEW_GENERAL の座標は壊れない（family 座標は別領域に保持）。

### VIEW_CONSTRAINT（key 3）

- active layout（general の座標）に制約エッジを重畳描画する。Document は変更しない。
- requiredReadId edge = teal、excluded(slot1) = red、excluded(slot2) = yellow（family の矛盾 yellow とは別意味）。エッジ中段に `requiredReadId:0x..` 等のラベル。
- **ghost**（現 scene に存在しないターゲット id）は実ノードの右に dedicated lane を作り、overlay0 の破線枠 + id badge の box に結ぶ。HUD に kind 別 / ghost 別のカウントを出す。
- timer / unlock flags / eyelevel の制約 edge は未描画（現フェーズの範囲）。

### HUD（左パネル）

mode 名、操作キー案内、view scene・scene 一覧（ノード数付き）、nodes/edges/roots 数、load パス・bytes・行数・要素/コメント/空行数、round-trip の EXACT/MISMATCH + baseline diff（added/edited/added_or_dirty_only）、write policy の状態（legacy write-back の on/off）、backup on quit の状態、使用フォント（freetype のパス or ASCII fallback）、選択ノードの props 詳細（**view 別の表示順序** — family/constraint/EDITOR は優先表示 variant）、export パネル（P でトグル）、各 view の凡例。

### 日本語描画

FreeType を dlopen（シンボルは dlsym、ABI の先頭フィールドのみ手宣言）し、システムから日本語フォントの候補を探して rasterize、SDL_Texture に cache して UTF-8 をそのまま描きます。フォントが無い環境は ASCII debug text の `?` 代替にフォールバックします（全文は stdout の dump で確認できます）。

---

## 6. ゲーム runtime（コンソール）の機能

stdout に ANSI（画面クリア・色付き）で 2 段のコンソールを描きます。

```
=== [ State ] ===  gScene/gRoot/gParent/gUnlocks/gEyelevel/gTimers
=== [ Containers Status ] ===  A/B/C/displayList/objectList の件数
=== [ Selection ] ===
1段目: Verb（displayList）… 有効な Objective が無い Verb は [で囲む]
2段目: Objective（objectList, gParent==gRoot のときのみ表示）
(Focus Window -> 'i': Left, 'o': Right, 'a': Select Objective, 'u': Select Verb, 'f': Send, 'q': Quit)
```

### コンテナと条件判定

- **containerA**: ロードした全要素（load 時の prop をそのまま保持。hasRead をクリアしない）。
- **containerB**: containerA のうち runtime の現在シーン `gScene` の要素。
- **containerC**: containerB のうち「表示条件を満たし parent == gParent の要素」= 1 段目の候補集合。
- **displayList**: containerC から作る 1 段目（Verb）一覧。
  - isOnetime かつ isQuote の候補があれば **1 件をランダム選択**
  - それ以外で containerC が 5 件以上ならシャッフルして **先頭 4 件**
  - 4 件未満なら全件
- **objectList**: 選択中 Verb の子集合。Verb に child（slot1）があればその要素を参照し、`dummy` なら dummy を新しい親として**再帰的に descend**（dummy が timerTarget を持つならそのタイマーをリセット）。child が dummy でなければ単独の Objective として即表示。Verb が isQuote のとき Objective 段は空。

表示条件（containerC と Objective で共通の 8 条件、すべて AND）:

| 条件 | 意味 |
|---|---|
| cond1 | parent == gParent（Verb）/ parent == verbId（Objective） |
| cond2 | requiredReadId（0 は条件なし）が現 scene で hasRead |
| cond3 | 未読 **または** isOnetime でない（hasRead でも isOnetime でなければ通過） |
| cond4 | excludedReadId（slot1・slot2、0 は条件なし）が未読 |
| cond5 | timerTarget 未設定なら通過。設定時は該当タイマーが**稼働中かつ値>0**で演算子（< / > / ==）が成立（停止中タイマーは常に不成立） |
| cond6 | requiredUnlockFlags が gUnlocks に含まれる |
| cond7 | excludedUnlockFlags が gUnlocks に完全含まれない |
| cond8 | eyelevel == 0 または eyelevel == gEyelevel |

dummy が条件を満たすと最優先で処理され（タイマーリセット→親交代→再構築）、Objective 側でも同様に descend します。containerC が空なら `noContentState`（コンソールは `[no content]` + `'f'` で続行案内）。

### 送信（`f` = sendProperty）

1. **移動モード中**: 移動先リスト（parent==0x04 の要素）を再構成するだけで、送信・変数変更は行わない。
2. **noContentState**: fallback — gParent を gRoot にして再構築、それでも空なら root/parent/unlocks/eyelevel を 0 にして再構築し、表示を再作成。
3. **Verb 選択中の送信禁止規則**: isQuote でなく parent == gRoot の Verb は送信できない。
4. **Objective 選択で objectList が空**: 擬似要素 `"no content"`（prop=0）を送信。
5. **gimmick**: prop が `0x0010000000100a07`（「札を取る」）なら `gTicketNumber = gTick + 1032` を立てて送信を完了する（他の効果は行わない）。
6. **child の解決**: child が `dummy` → gParent を dummy の id に交代（descend）。child が content → containerC にその要素 1 件だけ立てて表示を再構築し、**ここで送信処理は終了**（hasRead 立上げ・シーン効果は行わない）。
7. child なし → `gParent = id`。
8. **hasRead 立上げ**: containerA の同 dialogue・同 prop の要素に bit16 を立てる（isOnetime でなくても立上げるが表示は継続される）。
9. **newScene != 0**: `gScene = newScene`、gRoot/gParent/gUnlocks/gEyelevel/gTick をリセット。
10. そうでなければ `gUnlocks |= slot0 unlocks | slot2 unlocks` の後に `gUnlocks &= ~slot1 downUnlocks`（同じbitの up/down 競合では down が勝つ）、toggleEyelevel != 0 なら `gEyelevel = toggleEyelevel`、newRoot != 0 なら `gRoot = newRoot` かつ unlocks/gEyelevel/gTick をリセット。
11. lifecycle 再構築（containerB/C、displayList、objectList）。**noContentState かつ選んだ要素の parent が現在の gRoot ででないとき**（= verb→quote の往輪が root に戻って完了したとき）タイマーを進める（`advanceTimers()`）。

### タイマー（0〜3）

- 稼働中タイマーは `advanceTimers()`（root 往輪完了時）ごとに +1。15 から 0 に wrap して停止。
- timerSetter の action（start/stop/reset）は往輪中に收集され、root 復帰時に適用されます。stop は値を保持したまま停止、reset は 0 + 停止。
- timerTarget の条件（cond5）は稼働中かつ 0 より大きい値でのみ成立。dummy の優先処理・Objective descend では dummy の timerTarget のタイマーをリセットします。

### gTick 秒読みタイマーと gimmick の unlocked

`gimmick()` は毎回 `gTickTimer()` を評価します: 1〜3 の random duration を抽選し、gTick がその差に達した時点で発火 — 発火時に `gUnlocks` bit0 を立て `gCallingNumber++`（非発火時は bit0 を clear）。gTick がシーン/newRoot でリセットされたら秒読みを現在地点からやり直します。

### 「移動する」コンテナ（id == 0x04）のトグル表示（`M`）

現 runtime scene に id==0x04 の要素があるときだけトグル可能。1 段目を「移動する」 marker 1 件に、2 段目を parent==0x04 の要素一覧に差し替えます（常に Objective 段選択状態として扱う）。元の表示・選択状態は suspend 配列に退避し、解除時にそのまま復元します。gScene / gUnlocks 等の変数は一切変更しません。

### 挿入フロー（runtime の `TAB`）

親IDを決めて（Objective 選択中なら選択 Verb の id、そうでなければ gParent）テキスト入力 → Y/N で isQuote → Y/N で isOnetime。id は `containerB.size()+1`（runtime 側の旧来方式。EDITOR の nextFreeId とは別）で prop を合成し containerA/B に追加、表示リストを再構築して次の INSERT に復帰します。`ESC` で取消（text input を対称に閉じ、表示を再構築）。

### 終了時の動作（SDL_AppQuit）

- backup on quit（`B` でトグル、既定 off）が on のときだけ `../data.txt → ../data.txt.bak` に上書きコピー。
- export on quit（`X`）が on のとき全文を stdout に出力。
- **レガシー write-back**: load 元が `../data.txt` の旧運用（build/ 直下から実行）でのみ、`../data.txt` に書き戻します（dummy 以外から hasRead bit をクリアし、正格式で全件出力）。`./data.txt` を load した現行運用では書き換えません。
- FreeType の glyph texture を破棄し、renderer/window を破棄。

---

## 7. UI モード（viewer / game）

- **UI_VIEWER（既定）**: graph overlay + HUD + console（stdout）。GUI キー・マウス入力・editor/props は viewer のみ受理。
- **UI_GAME**: `renderConsole` と runtime 入力系統のみ。graph/HUD は描かない（ウィンドウは base 色のみ）。挿入フロー等の runtime 入力はそのまま動作。
- `H` で往復切替。game に離れるとき editor の add flow と props パネルは閉じられます（Document は変更しない）。viewer 復帰時に graph の mode・座標・選択はそのまま復元。
- `DDM_MODE=game` で起動しても復路（H → viewer）は動作します。

---

## 8. 自己検証（DDM_SELFTEST）

`DDM_SELFTEST=1 ./DDMEditor`（描画環境が無い場合 SDL dummy video driver でも大半の phase は動作）で、対話せずに各 phase を stdout に検証して終了します。期待値はすべて**実行時に data.txt の prop 直接パースから算出**（数をハードコードしない）。

phase と検証内容:

| phase | 内容 |
|---|---|
| round-trip baseline | load 直後の serialize==原文、terminator（CRLF・改行なし最終行）保持、正格式规则（quote・'0x'・lowercase16・trailing comma）の合成→再パース往復、load-time prop の保持（hasRead をクリアしない・dummy の prop も原値） |
| selection | ノード中心クリックの pickNode、zoom/pan 適用後の往復での同一ノード選択 |
| family layout | layoutFamily の決定性、active layout の pick、全 scene のフォレスト数/全ノード配置の照合、構造 edge（parent vs slot1 child、矛盾、dangling）の counts 照合、実描画の pixel readback（sapphire/peach/yellow/text/subtext1 の実色）、general 座標の非破壊、round-trip EXACT 維持 |
| constraint edges | kind 別/ghost 別 counts の全 scene 照合、present ターゲットの id 解決、mode 往復（1→2→3→1）での fit・座標・選択の非破壊、実描画で required≥1 なら teal 出現必須、round-trip EXACT 維持 |
| editor add | nextFreeId の重複なし、空 dialogue 拒否、ESC 取消で Document 復元、追加行の正格式と再パース、graph/family/constraint への即時出現、baseline diff が「追加行のみ」、runtime 同期既定 off、検証後の baseline 復元、editor の runtime キー遮断と挿入フローの対称性 |
| props edit | write helpers↔read helpers の全フィールド往復（timer/eyelevel の合成・分解、vacant bit 保持）、panel open（クリック/right-click）、describeElement の EDITOR variant、キーフロー（TAB/ENTER/Y/N/U/循環）、programmatic 編集 O1〜O16 + 各 option 1 回ずつ、validation warning 総数の照合、G 再構築（runtime 不変）、scene 切替で panel/nodeKey が壊れないこと、実描画の readback |
| undo | 全 undo で baseline の prop・line text・dirty・graph・round-trip EXACT が完全復元すること、undo 上限（256）に未到達 |
| font | freetype ready 時の日本語 glyph rasterize、実描画 frame の bright pixel・text/subtext1 実色 |
| palette audit | 数値 RGB リテラルが kMC 定数定義のみであること（grep 相当の源码監査）、SDL_RenderDebugText は fallback のみ、pixel readback の実色で WCAG 4.5:1 を面×text/subtext1 の全組合せで測定 |
| game-mode | H 往復、game では graph/HUD 実色が出ないこと（bright=0）、game でも runtime 挿入フローが動作、viewer 復帰後の描画復元 |
| data unchanged | 最終で磁盘の data.txt が load 時のバイト列と完全一致（書き変わっていないこと）、round-trip 最終 EXACT |

描画 frame は `build/viewer_frame.bmp`, `build/viewer_frame_general/family/constraint/editor/props/palette/game.bmp` に保存されます。

結果集約: 全 phase pass → `SELFTEST: ALL PHASES PASS` + 正常終了。失敗 phase は `FAIL <phase>` を出力し `selftest exit: FAIL phases=N` で失敗終了（終了コードは失敗）。selftest の編集・追加はすべて検証後に baseline へ復元されます（data.txt は不変）。

---

## 9. 既知の範囲・制限

- EDITOR は**要素行の追加と prop フィールドの編集**のみ。dialogue の編集・行削除は無い（追加は末尾のみ）。
- constraint viewer は general layout の重畳のみ。timer / unlock flags / eyelevel の制約エッジ可视化は未実装。
- props の編集・追加は既定では runtime（containerA/B 等）へ同期しません（editor 追加の `G` トグルのみ同期経路あり）。
- レガシー write-back は `../data.txt` を load した旧運用のみ。現行運用（`./data.txt`）では data.txt は書き換えられません。
- `Gimmick.cpp` / `Gimmick.h` は現状 main.cpp から参照されずビルド対象にも含まれません（main.cpp 内の `gimmick()` がギミック処理の本体。将来 switch(gScene) 汎用化の TODO がコードに残る）。
- テキスト描画 API は FreeType + texture cache。無い環境は ASCII `?` 代替（全文は stdout で確認可能）。
- タイマーは root 往輪完了時のみ進みます（個別選択では進まない）。
- 表示色・レイアウトは Catppuccin Mocha と WCAG 4.5:1 の監査に準拠しています（selftest の palette audit が源码・実色両面で監査）。

---

## 10. ソース構成（現行）

| 領域 | 内容 |
|---|---|
| `main.cpp` L25–238 | Element / prop bit の read・write helper |
| L240–508 | Strict Document モデル（loadDocument / parseElementLine / serializeDocument / verifyRoundTrip） |
| L510–726 | Graph モデル（(scene,id) node、parent/child/required/excluded edge、adjacency、warnings） |
| L728–1010 | グローバル state（runtime・GUI・editor・props・selftest の集約 state、kMC パレット定数） |
| L1012–1418 | FreeType dlopen による日本語テキストエンジン（font candidate 探索・glyph cache・ASCII fallback） |
| L1424–2188 | runtime のロジック（placeholder 展開、条件判定、コンテナ lifecycle、表示/目的リスト構築、タイマー、gimmick、移動モード、sendProperty） |
| L2191–3196 | 描画（world↔screen、ノード箱、bezier edge、family view、constraint view、stdout dump、レイアウト） |
| L3198–3921 | EDITOR 追加フロー・props 編集（validation・commit・undo・メニュー行・preview panel） |
| L4037–4326 | HUD・graph overlay・pick/zoom/scene 切替・マウスイベント |
| L4328–4752 | AppInit / AppEvent（キー分岐・挿入フロー・UI/View モード切替） |
| L4554–6945 | AppIterate（DDM_SELFTEST の全 phase、通常描画ループ） |
| L6947–7001 | backupData・SDL_AppQuit（write policy・legacy write-back） |
