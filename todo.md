/*
prop:
  bits 0-7   id
  bits 8-15  parent
  bit 16     hasRead
  bit 17     isQuote
  bit 18     isOnetime
  bit 19     vacant
  bits 20-23 scene
  bits 24-25 slot0Index
  bits 26-27 slot1Index
  bits 28-29 slot2Index
  bits 30-31 slot3Index
  bits 32-39 slot0 value
  bits 40-47 slot1 value
  bits 48-55 slot2 value
  bits 56-63 slot3 value

slot0Index:
  0: requiredReadId
  1: unlocks

slot1Index:
  0: child
  1: excludedReadId
  2: newRoot

slot2Index:
  0: timerSetter
  1: unlocks
  2: requiredUnlockFlags
  3: excludedReadId

slot3Index:
  0: timerTarget
  1: newScene
  2: excludedUnlockFlags
  3: eyelevelToggle

timerTarget slot value:
  bits 0-1 operator
  bits 2-3 timerId
  bits 4-7 count

timerSetter slot value:
  bits 0-1 timerId
  bits 2-3 vacant
  bits 4-5 action

eyelevelToggle slot value:
  bits 0-3 eyelevel
  bits 4-7 toggleEyelevel
*/

いろんな条件
timerが鳴る。
unlocksが開く。
reqReadId
parent
->
gimmickとddmがcommunicateするには、、、
timerを鳴らす。
unlocksを開く。
hasReadを立てる。
gParentを変える。
->
素直に、propでフラグを待ち、単純にgimmick()でフラグをたてればいい。
->
ランタイムの数値によって、フラグを操作する。


---

## 最初に各種用語を以下に述べる。
- onetime quote: isOnetime, isQuoteがともにtrueのElement.
- verb: 主にidが0x01, 02, 03の見る、話す、行くのElement.
- object: parentがverbである、verbに対する目的語に相当するElement.
- option: isQuote == falseのElement.
- dialogue: verb, objectと選択した結果、読めるセリフ。e.g. 見る(verb)->こども(object)->にこにこ笑っている。(dialogue)
- root: gParentが0x0のverbが表示されているホーム状態。
- tick: rootから始まって、末端のdialogueにたどり着いた後、"no content"で再びrootに戻るまでの1プロセス。parent == 0x00のonetime quoteを消費しても、verbとobjectを経由してないので、tickを消費するとはみなさない。

# flavorContainerを実装してほしい。
- rootのとき、display listにonetime quoteは優先的に配信されるようになっている。
- これをあらため、現在rootで配信されるonetime quoteはいずれもフレーバーテキストの役割を果たすものだから、flavorContainerを新しく実装してほしい。ただし、parentがffのものを抽出してgParentに関係なく、sendProperty()内で構成するものとする。
- 構成したflavorContainerに要素があれば、isOnetime, quote/optionに関係なく、一つだけランダムに選んで表示する。
- flavorContainerが空なら通常通り、containerCからdisplayListを構成する。
- 上記の作業終了後に、rootにおいてonetime quoteを優先する処理を削除する。

# data.txtの以下のdialogueのpropを編集し、以下の指示の挙動を示すようにしてほしい。
- flavorContainerに含める。2-3tickごとにgUnlockの1bit目を立てて配信する。(gUnlock |= 0x1;)その次のtickでは下げるので、不規則に発振する状態となる。(gUnlocks &= ~1ULL;)
- '%(gCallingNumber)！', '0x0001000020160022',
- '%(gCallingNumber)番のひとー', '0x0001000020160023',
- '%(gCallingNumber)番どうぞー', '0x0001000020160024',
- 'はい、%(gCallingNumber)番ー。', '0x0001000020160025',

# 以下の指示にあるdialogueに対して、適切にpropを与え、指示の通りの挙動を実現してほしい。
- '札をとる'送信で、札を取得する。札の番号は1032 + gTickとする。
- 札の番号は保持する。gCallingNumberと札の番号が等しい時、gUnlocksの2bit目を立てる。
- gUnlocksの2bit目が立っていない時、'まだ順番ではありませんよ。'を配信する。
- gUnlocksの2bit目が立っていて、なおかつ札番号が偶数なら'おはようございます(黄色)'を配信する。
- 札の番号が奇数の場合は、'本日は。(緑)'を配信する。
- この偶奇の判定にもgUnlocksの3bit目を使い、選択肢はexcludedUnlockFlagsでトグルするようにしてほしい。
- なお、このgUnlocksの3bit目は札取得時に、偶奇の判定を行い、その場で立てるとよいだろう。

---

以上のようなcondNの判定とは性質の異なるElementドリブンでない、ランタイムドリブンのgUnlocksを通じたコミュニケーションは理想的には、
gimmick()のような一つの関数を呼び出して行い、内部でswitch(gScene)のように分岐し、sceneごとのギミックを動作させる設計にしたい。
すなわち、このような特殊prop処理は今後も多数実装することになるものだから、なるべく汎用的な機能を用意したいという意図がある。

---

# 見回りギミックのプロンプト化
- 単純にgUnlocksをチカチカさせればよい。
- 2-3tick事に、持ち場を離れ、戻ってくる。
- 持ち場をはなれている間なら、階段を登ることが出来る。
- 初回は必ず失敗する。

# 合意形成ゲーム
- このフロアでは、見ることで話し声が聞こえる。
- 連続で見ると声をひそめられる。
- 3回までなら、聞ける。あとはよく聞こえない。
- 立食パーティ風にお茶を渡して、交渉、ナンパ。
- 見るコマンドで事情を推察し、実際に話すコマンドで話す、お茶を渡す。
- 交渉を成立させていく。
- 3回交渉成立で、ショーメイ成功。拍手しながら階段を降りてくる。
- tickで視界に入る人が入れ替わる。eyelevel, 行くを組合せてもいいかもしれへん。
- 神経衰弱の感覚で。

# ./main.cppは様々な機能がすべて書かれてあって、現在著しく可読性が落ちてるので必要に応じて、新規に.h, .cppを作成し、なるべく可読性が高まるようにプロジェクトを整理してほしい。
すでにファイルは巨大になっており、ステップも多岐にわたると思われるので、複数回のセッションを通じてプロジェクトを完遂する。
本セッションでは方針と計画を策定してほしい。
その複数回のセッションで実行するプロンプトを構成し、./prompts.mdを新規作成し、出力してほしい。
無論のこと、動作は整理の前後で変化しないように作業を完了すること。
## 共通の制約
- 毎セッションが完了する事に、次回のセッションに向けて引き継ぎを./prompts.mdに記入すること。
- ワーキングディレクトリ内には自由にフォルダとファイルを作成して良い。
- ディレクトリ内のファイルはtodo.mdを除いて自由に編集して良い。
- 作業完了後は、git add ., git commit -m "適切なメッセージを生成する。", git pushを実行すること。

# DDME GUIプロジェクト
## data.txt編集インターフェース
## DDMEditorランタイムシミュレータ
## 起動設定でビューワ実装前の通常のゲームモードも実行できるようにしてほしい。

# 現在のmain.cppの機能についてマニュアルを書いてほしい。./README.mdを新規作成し、そこに出力してほしい。

---

# アイデア
- 見回りギミックをクリアして、2階へ。
- 2階でのギミックは？
- コマンド成立前に、たくさんひっかけする。
- 同意形成ゲームで、交渉以外にナンパのギミックも考える。

