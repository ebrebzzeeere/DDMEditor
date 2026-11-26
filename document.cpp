#include "document.h"

#include <filesystem>
#include <iostream>

bool loadDocument(const std::string &path, Document &doc,
                  std::vector<std::string> &warnings) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;

  doc = Document{};
  doc.raw.assign(std::istreambuf_iterator<char>(in),
                 std::istreambuf_iterator<char>());
  in.close();

  size_t pos = 0;
  while (pos < doc.raw.size()) {
    const size_t nl = doc.raw.find('\n', pos);
    RawLine line;
    if (nl == std::string::npos) {
      line.text = doc.raw.substr(pos); // 改行で終わらない最終行も保持する
    } else {
      std::string body = doc.raw.substr(pos, nl - pos);
      if (!body.empty() && body.back() == '\r') {
        line.text = body.substr(0, body.size() - 1);
        line.terminator = "\r\n";
      } else {
        line.text = std::move(body);
        line.terminator = "\n";
      }
    }

    bool whitespaceOnly = true;
    for (char c : line.text) {
      if (c != ' ' && c != '\t') {
        whitespaceOnly = false;
        break;
      }
    }

    if (line.text.empty() || whitespaceOnly) {
      line.kind = LINE_BLANK;
      ++doc.blankLines;
    } else if (isCommentLine(line.text)) {
      line.kind = LINE_COMMENT;
      ++doc.commentLines;
    } else {
      std::string dialogue;
      uint64_t prop = 0;
      if (parseElementLine(line.text, dialogue, prop)) {
        line.kind = LINE_ELEMENT;
        line.elementIndex = static_cast<int>(doc.elements.size());
        doc.elements.push_back(ElementRecord{
            {dialogue, prop}, static_cast<int>(doc.lines.size()), false});
        ++doc.parsedLines;
      } else {
        line.kind = LINE_UNPARSED;
        ++doc.unparsedLines;
        warnings.push_back("line " + std::to_string(doc.lines.size() + 1) +
                           " を解析できませんでした: " + line.text.substr(0, 72));
      }
    }

    doc.lines.push_back(std::move(line));
    pos = (nl == std::string::npos) ? doc.raw.size() : nl + 1;
  }
  return true;
}

// 未編集の行は原文をそのまま、dirty な Element 行のみ `'dialogue', '0xHHHH|HHHH|HHHHHHHH',` に
// 再生成する。comment / blank line は原文のまま。terminator は原文の改行を再現するので、
// 未編集の Document では serializeDocument() == Document::raw が成立する。
std::string serializeDocument(const Document &doc) {
  std::string out;
  out.reserve(doc.raw.size());
  for (const RawLine &line : doc.lines) {
    if (line.kind == LINE_ELEMENT && line.elementIndex >= 0 &&
        doc.elements[line.elementIndex].dirty) {
      const Element &e = doc.elements[line.elementIndex].element;
      out += "'" + e.dialogue + "', '0x" + formatPropHex16Piped(e.prop) + "',";
    } else {
      out += line.text;
    }
    out += line.terminator;
  }
  return out;
}

RoundTripReport verifyRoundTrip(const Document &doc) {
  RoundTripReport rep;
  const std::string out = serializeDocument(doc);
  rep.originalBytes = doc.raw.size();
  rep.serializedBytes = out.size();
  rep.lineCount = doc.lines.size();
  rep.elementLines = doc.parsedLines;
  rep.commentLines = doc.commentLines;
  rep.blankLines = doc.blankLines;
  rep.exact = (out == doc.raw);
  if (!rep.exact) {
    size_t a = 0, b = 0;
    size_t line = 1;
    while (a < doc.raw.size() && b < out.size()) {
      if (doc.raw[a] != out[b]) {
        rep.firstDiffLine = line;
        break;
      }
      if (doc.raw[a] == '\n') ++line;
      ++a; ++b;
    }
    if (rep.firstDiffLine == 0) rep.firstDiffLine = line; // 長さが違う場合
    rep.originalSample = doc.raw.substr(a, 48);
    rep.serializedSample = out.substr(b, 48);
  }

  // Prompt 4 baseline diff: serializeDocument の出力を baseline raw と行単位で比較する。
  // terminator（"\n" / "\r\n" / ""）は比較せず本文テキストだけ照合する（CRLF を壊さない）。
  // 末尾改行による空行は「行」として数えない（raw と serialized で同じ規則）。
  const auto splitKeepText = [](const std::string &s) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < s.size()) {
      const size_t nl = s.find('\n', pos);
      if (nl == std::string::npos) {
        lines.push_back(s.substr(pos));
        break;
      }
      lines.push_back(s.substr(pos, nl - pos));
      pos = nl + 1;
    }
    for (std::string &l : lines) {
      if (!l.empty() && l.back() == '\r') l.pop_back(); // CRLF の CR は本文から除く
    }
    // 終端が改行の行は「改行前の本文」として 1 行として数える（末尾の空要素は作らない）。
    // 改行で終わらない最終行も 1 行として保持する（loadDocument の行数対応と同じ）。
    return lines;
  };
  const std::vector<std::string> baseLines = splitKeepText(doc.raw);
  const std::vector<std::string> outLines = splitKeepText(out);
  rep.baselineLines = baseLines.size();
  rep.diffIsAddedOrDirtyOnly = outLines.size() >= baseLines.size(); // 削除行があれば不正
  if (outLines.size() > baseLines.size()) rep.addedLines = outLines.size() - baseLines.size();

  // baseline の位置まで同じ index で対応する（追加は末尾のみ・削除は作らない）。
  // 異なる行は dirty Element 行でなければならない（それ以外の差分は想定外の編集）。
  for (size_t i = 0; i < baseLines.size(); ++i) {
    if (i >= outLines.size()) {
      rep.diffIsAddedOrDirtyOnly = false;
      break;
    }
    if (outLines[i] == baseLines[i]) continue;
    ++rep.editedLines;
    if (i >= doc.lines.size() || doc.lines[i].kind != LINE_ELEMENT ||
        doc.lines[i].elementIndex < 0 ||
        doc.lines[i].elementIndex >= static_cast<int>(doc.elements.size()) ||
        !doc.elements[doc.lines[i].elementIndex].dirty) {
      rep.diffIsAddedOrDirtyOnly = false;
    }
  }
  // baseline より後の行はすべて「追加された正格式の Element 行」でなければならない。
  for (size_t j = baseLines.size(); j < outLines.size(); ++j) {
    if (j >= doc.lines.size() || doc.lines[j].kind != LINE_ELEMENT ||
        doc.lines[j].elementIndex < 0 ||
        doc.lines[j].elementIndex >= static_cast<int>(doc.elements.size()) ||
        !doc.elements[doc.lines[j].elementIndex].dirty ||
        outLines[j] != doc.lines[j].text) {
      rep.diffIsAddedOrDirtyOnly = false;
    }
  }
  return rep;
}

void printDocumentToStdout(const Document &doc) {
  std::cout << serializeDocument(doc) << std::flush;
}

// バックアップモジュール
// 既定値は document.h の宣言側に定義（定义側で重複指定すると再定義エラーになるため）。
// 呼ばれ方（既定値・引数指定）は元の main.cpp と完全に同一。
void backupData(const std::string &srcPath, const std::string &dstPath) {
  std::error_code ec;
  if (std::filesystem::exists(srcPath, ec)) {
    std::filesystem::copy_file(
        srcPath, dstPath, std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
      std::cerr << "バックアップの作成に失敗しました: " << ec.message()
                << std::endl;
    }
  }
}
