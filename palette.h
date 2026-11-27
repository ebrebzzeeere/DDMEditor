#pragma once
// Catppuccin Mocha パレット定数（公式 hex）と WCAG 輝度/コントラスト helper。hex 値は変更禁止。
#include <algorithm>
#include <cmath>
#include <cstdint>


// --- Catppuccin Mocha パレット（公式 hex をそのまま 0xRRGGBB で保持） ---
// https://catppuccin.com/palette の mocha を準拠色とする。視認性は
// 「dark な surface/mantle に text #cdd6f4 を載せる」公式の組合せに揃える。
constexpr uint32_t kMCBase = 0x1e1e2e;       // base
constexpr uint32_t kMCMantle = 0x181825;     // mantle（HUD panel）
constexpr uint32_t kMCText = 0xcdd6f4;       // text（dialogue・見出し：base/surface0 に ~9:1）
constexpr uint32_t kMCSubtext1 = 0xbac2de;   // subtext1（HUD の情報行）
constexpr uint32_t kMCSubtext0 = 0xa6adc8;   // subtext0（export本文・ポート）
constexpr uint32_t kMCOverlay2 = 0x9399b2;   // overlay2（flags・控えな案内）
constexpr uint32_t kMCOverlay1 = 0x7f849c;   // overlay1（操作ヘルプ）
constexpr uint32_t kMCOverlay0 = 0x6c7086;   // overlay0（区切り線・構造ノード枠）
constexpr uint32_t kMCSurface0 = 0x313244;   // surface0（contentノードの塗り）
constexpr uint32_t kMCSurface1 = 0x45475a;   // surface1（dummyノードの塗り）
constexpr uint32_t kMCRed = 0xf38ba8;        // red（quoteストライプ・失敗表示）
constexpr uint32_t kMCGreen = 0xa6e3a1;      // green（hasRead・EXACT OK・成功表示）
constexpr uint32_t kMCYellow = 0xf9e2af;     // yellow（MISMATCH警告）
constexpr uint32_t kMCPeach = 0xfab387;      // peach（child edge）
constexpr uint32_t kMCMauve = 0xcba6f7;      // mauve（onetimeストライプ）
constexpr uint32_t kMCTeal = 0x94e2d5;       // teal（font path: ok）
constexpr uint32_t kMCSky = 0x89dceb;        // sky（id/parent/scene badge）
constexpr uint32_t kMCSapphire = 0x74c7ec;   // sapphire（parent edge: ノード枠 blue と区別）
constexpr uint32_t kMCBlue = 0x89b4fa;       // blue（contentノード枠）
constexpr uint32_t kMCLavender = 0xb4befe;   // lavender（選択ノード枠）

// 0xRRGGBB からR,G,Bを取り出す（定数展開用）。
constexpr uint8_t mcR(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFFu); }
constexpr uint8_t mcG(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFFu); }
constexpr uint8_t mcB(uint32_t c) { return static_cast<uint8_t>(c & 0xFFu); }


// 0x383039 / 0x2e363c と同じ値になる（selftest palette audit で実色出現を確認する）。
constexpr uint32_t mcBlendTint(uint32_t baseHex, uint32_t tintHex, unsigned pct) {
  const uint32_t r = (mcR(baseHex) * (100u - pct) + mcR(tintHex) * pct + 50u) / 100u;
  const uint32_t g = (mcG(baseHex) * (100u - pct) + mcG(tintHex) * pct + 50u) / 100u;
  const uint32_t b = (mcB(baseHex) * (100u - pct) + mcB(tintHex) * pct + 50u) / 100u;
  return (r << 16) | (g << 8) | b;
}
constexpr uint32_t kMCFillTemplate = mcBlendTint(kMCBase, kMCPeach, 12u); // template tint（peach 12%）
constexpr uint32_t kMCFillEmpty = mcBlendTint(kMCBase, kMCGreen, 12u);    // empty tint（green 12%）


// WCAG 2.1 relative luminance / contrast ratio（sRGB linearize。面+テキストの
// 4.5:1 判定を pixel readback の実色から測るために使う — Prompt 6 要件 6）。
inline double wcagLuminance(uint32_t hexColor) {
  const auto lin = [](uint32_t c8) -> double {
    const double c = static_cast<double>(c8) / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(mcR(hexColor)) + 0.7152 * lin(mcG(hexColor)) + 0.0722 * lin(mcB(hexColor));
}
inline double wcagContrast(uint32_t fgHex, uint32_t bgHex) {
  const double l1 = wcagLuminance(fgHex);
  const double l2 = wcagLuminance(bgHex);
  const double hi = std::max(l1, l2);
  const double lo = std::min(l1, l2);
  return (hi + 0.05) / (lo + 0.05);
}
