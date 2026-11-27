#include "placeholders.h"
#include <regex>
#include <string>
#include "document.h"
#include "guitext.h"




std::string expandTextPlaceholders(const std::string &dialogue) {
  static const std::regex placeholder("%\\(([A-Za-z0-9]+)\\)");
  std::string result;
  std::string::size_type cursor = 0;
  std::smatch match;
  while (std::regex_search(dialogue.cbegin() + cursor, dialogue.cend(), match,
                           placeholder)) {
    // match[0] / match[1] は string として取る（内容と長さは信頼できる）。
    // 一致開始位置は、cursor 以降の dialogue から一致文字列と同じバイト列を
    // find で探すことで確定する。正規表現は leftmost の一致を返すため、
    // 同じバイト列の最初の出現位置は一致位置と一致する。
    const std::string matched = match[0].str();
    const std::string name = match[1].str();
    const std::string::size_type found = dialogue.find(matched, cursor);
    if (found == std::string::npos) {
      // 念のための防御分岐。一致位置を確定できない場合は残りをそのまま残す。
      break;
    }
    result.append(dialogue, cursor, found - cursor);
    const unsigned *value = resolvePlaceholderVar(name);
    if (value != nullptr) {
      result += std::to_string(*value);
    } else {
      result += matched;
    }
    cursor = found + matched.size();
  }
  result.append(dialogue, cursor, dialogue.size() - cursor);
  return result;
}

// containerB contains only elements in gScene.  ID 0 means that the
