#include "guitext.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include "document.h"
#include "state.h"
#include "dlfcn.h"




GuiTextEngine gGuiText;

bool loadGuiTextEngine() {
  gGuiText.dl = dlopen("libfreetype.so.6", RTLD_NOW | RTLD_GLOBAL);
  if (!gGuiText.dl) gGuiText.dl = dlopen("libfreetype.so", RTLD_NOW | RTLD_GLOBAL);
  if (!gGuiText.dl) return false;
  gGuiText.fnInit = reinterpret_cast<FnFTInit>(dlsym(gGuiText.dl, "FT_Init_FreeType"));
  gGuiText.fnDone = reinterpret_cast<FnFTDone>(dlsym(gGuiText.dl, "FT_Done_FreeType"));
  gGuiText.fnNewFace = reinterpret_cast<FnFTNewFace>(dlsym(gGuiText.dl, "FT_New_Face"));
  gGuiText.fnDoneFace = reinterpret_cast<FnFTDoneFace>(dlsym(gGuiText.dl, "FT_Done_Face"));
  gGuiText.fnSetSizes = reinterpret_cast<FnFTSetPixelSizes>(dlsym(gGuiText.dl, "FT_Set_Pixel_Sizes"));
  gGuiText.fnCharIndex = reinterpret_cast<FnFTCharIndex>(dlsym(gGuiText.dl, "FT_Get_Char_Index"));
  gGuiText.fnLoadGlyph = reinterpret_cast<FnFTLoadGlyph>(dlsym(gGuiText.dl, "FT_Load_Glyph"));
  gGuiText.fnRenderGlyph = reinterpret_cast<FnFTRenderGlyph>(dlsym(gGuiText.dl, "FT_Render_Glyph"));
  if (!gGuiText.fnInit || !gGuiText.fnDone || !gGuiText.fnNewFace || !gGuiText.fnDoneFace ||
      !gGuiText.fnSetSizes || !gGuiText.fnCharIndex || !gGuiText.fnLoadGlyph ||
      !gGuiText.fnRenderGlyph) {
    return false;
  }
  return gGuiText.fnInit(&gGuiText.ft) == 0 && gGuiText.ft != nullptr;
}

// 日本語を含むフォントを探す。DDM_FONT_PATH で明示指定もできる。
std::vector<std::string> fontCandidates() {
  std::vector<std::string> out;
  const char *env = std::getenv("DDM_FONT_PATH");
  if (env && env[0]) out.push_back(env);

  struct Scored { int score; std::string path; };
  std::vector<Scored> found;
  for (const char *dir : {"/usr/share/fonts", "/usr/local/share/fonts"}) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) continue;
    int examined = 0;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
         it != end && examined < 300; ++it, ++examined) {
      std::error_code fec;
      if (!it->is_regular_file(fec)) continue;
      std::string path = it->path().string();
      std::string lower = path;
      for (auto &c : lower) c = static_cast<char>(std::tolower(c));
      const size_t n = lower.size();
      if (n < 4 || (lower.compare(n - 4, 4, ".ttf") != 0 && lower.compare(n - 4, 4, ".otf") != 0 &&
                    lower.compare(n - 3, 3, ".ttc") != 0)) {
        continue;
      }
      int score = 0;
      if (lower.find("jp") != std::string::npos || lower.find("cjk") != std::string::npos) score += 100;
      if (lower.find("ipa") != std::string::npos || lower.find("takao") != std::string::npos ||
          lower.find("arphic") != std::string::npos || lower.find("freesans") != std::string::npos) {
        score += 40;
      }
      if (lower.find("mono") != std::string::npos) score -= 20;
      // Regular を優先し、thin/light/medium/bold/italic/oblique は減点する。
      if (lower.find("regular") != std::string::npos) score += 30;
      for (const char *weight : {"thin", "extralight", "extra-light", "light", "normal",
                                 "medium", "semibold", "bold", "heavy", "black", "italic",
                                 "oblique"}) {
        if (lower.find(weight) != std::string::npos) score -= 25;
      }
      found.push_back(Scored{score, std::move(path)});
    }
  }
  std::sort(found.begin(), found.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
  for (const Scored &f : found) out.push_back(f.path);
  return out;
}

// face が sanity check を通るか。ABI のレイアウト違いや日本語Coverageはここで弾く。
bool tryGuiFont(const std::string &path) {
  void *face = nullptr;
  if (gGuiText.fnNewFace(gGuiText.ft, path.c_str(), 0, &face) != 0 || !face) {
    gGuiText.lastError = "FT_New_Face failed: " + path;
    return false;
  }
  const DFTFaceRec *rec = reinterpret_cast<const DFTFaceRec *>(face);
  if (rec->num_glyphs <= 0 || rec->num_glyphs > 300000 || rec->units_per_em < 256 ||
      rec->units_per_em > 100000 || rec->ascender <= 0 || rec->descender >= 0 || !rec->glyph) {
    gGuiText.lastError = "face sanity failed " + path + " num_glyphs=" +
                         std::to_string(rec->num_glyphs) + " upem=" +
                         std::to_string(rec->units_per_em) + " ascender=" +
                         std::to_string(rec->ascender) + " descender=" +
                         std::to_string(rec->descender);
    gGuiText.fnDoneFace(face);
    return false;
  }
  if (gGuiText.fnSetSizes(face, 0, kGuiTextPx) != 0) {
    gGuiText.lastError = "FT_Set_Pixel_Sizes failed: " + path;
    gGuiText.fnDoneFace(face);
    return false;
  }
  for (uint32_t probe : {uint32_t('A'), uint32_t('m'), uint32_t(0x8983u) /* 見 */, uint32_t(0x8A71u) /* 話 */}) {
    const unsigned idx = gGuiText.fnCharIndex(face, probe);
    if (idx == 0) {
      gGuiText.lastError = "no glyph for cp=" + std::to_string(probe) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
    if (gGuiText.fnLoadGlyph(face, idx, kFTLoadNoHinting | kFTLoadNoAutoHint) != 0) {
      gGuiText.lastError = "FT_Load_Glyph failed cp=" + std::to_string(probe) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
    if (gGuiText.fnRenderGlyph(rec->glyph, kFTRenderNormal) != 0) {
      gGuiText.lastError = "FT_Render_Glyph failed cp=" + std::to_string(probe) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
    const DFTGlyphSlotRec *slot = reinterpret_cast<const DFTGlyphSlotRec *>(rec->glyph);
    if (slot->bitmap_rows == 0 || slot->bitmap_rows > 64 || slot->bitmap_width == 0 ||
        slot->bitmap_width > 64 || slot->bitmap_pitch <= 0 || !slot->bitmap_buffer ||
        slot->bitmap_num_grays < 2 || slot->advance_x < 64 || slot->advance_x > 64 * 64) {
      gGuiText.lastError = "bitmap sanity failed cp=" + std::to_string(probe) + " rows=" +
                           std::to_string(slot->bitmap_rows) + " width=" +
                           std::to_string(slot->bitmap_width) + " pitch=" +
                           std::to_string(slot->bitmap_pitch) + " grays=" +
                           std::to_string(slot->bitmap_num_grays) + " pixel_mode=" +
                           std::to_string(slot->bitmap_pixel_mode) + " adv=" +
                           std::to_string(slot->advance_x) + " in " + path;
      gGuiText.fnDoneFace(face);
      return false;
    }
  }
  gGuiText.face = face;
  gGuiText.fontPath = path;
  gGuiText.ascentPx = static_cast<float>(rec->ascender) * static_cast<float>(kGuiTextPx) /
                      static_cast<float>(rec->units_per_em);
  gGuiText.descentPx = static_cast<float>(-rec->descender) * static_cast<float>(kGuiTextPx) /
                       static_cast<float>(rec->units_per_em);
  gGuiText.ready = true;
  return true;
}

bool initGuiText() {
  if (!loadGuiTextEngine()) {
    gGuiText.lastError = "dlopen libfreetype.so.6 failed: " + std::string(dlerror() ? dlerror() : "?");
    return false;
  }
  const std::vector<std::string> candidates = fontCandidates();
  if (candidates.empty()) {
    gGuiText.lastError = "no font file found under /usr/share/fonts";
    return false;
  }
  for (const std::string &cand : candidates) {
    if (tryGuiFont(cand)) return true;
  }
  gGuiText.lastError += " (candidates=" + std::to_string(candidates.size()) + ", first=" +
                        candidates[0] + ")";
  return false;
}

void shutdownGuiText() {
  for (auto &kv : gGuiText.glyphs) {
    if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
  }
  gGuiText.glyphs.clear();
  if (gGuiText.face && gGuiText.fnDoneFace) gGuiText.fnDoneFace(gGuiText.face);
  gGuiText.face = nullptr;
  if (gGuiText.ft && gGuiText.fnDone) gGuiText.fnDone(gGuiText.ft);
  gGuiText.ft = nullptr;
  if (gGuiText.dl) {
    dlclose(gGuiText.dl);
    gGuiText.dl = nullptr;
  }
  gGuiText.ready = false;
}

// glyph を rasterize して texture に cache する。失敗は negative cache する。
const GlyphRec *guiGlyph(SDL_Renderer *r, uint32_t cp) {
  const auto it = gGuiText.glyphs.find(cp);
  if (it != gGuiText.glyphs.end()) return it->second.tex ? &it->second : nullptr;
  if (gGuiText.glyphs.size() >= kGlyphCacheMax) return nullptr;

  const DFTFaceRec *rec = reinterpret_cast<const DFTFaceRec *>(gGuiText.face);
  GlyphRec g;
  const unsigned idx = gGuiText.fnCharIndex(gGuiText.face, cp);
  if (idx == 0 ||
      gGuiText.fnLoadGlyph(gGuiText.face, idx, kFTLoadNoHinting | kFTLoadNoAutoHint) != 0 ||
      gGuiText.fnRenderGlyph(rec->glyph, kFTRenderNormal) != 0) {
    gGuiText.glyphs.emplace(cp, g);
    return nullptr;
  }
  const DFTGlyphSlotRec *slot = reinterpret_cast<const DFTGlyphSlotRec *>(rec->glyph);
  g.w = static_cast<int>(slot->bitmap_width);
  g.h = static_cast<int>(slot->bitmap_rows);
  g.left = slot->bitmap_left;
  g.top = slot->bitmap_top;
  g.advance = static_cast<float>(slot->advance_x) / 64.f;  // 26.6 fixed → px
  if (g.w <= 0 || g.h <= 0) {
    gGuiText.glyphs.emplace(cp, g);
    return nullptr;
  }

  std::vector<uint8_t> rgba(static_cast<size_t>(g.w) * static_cast<size_t>(g.h) * 4u);
  const unsigned grays = slot->bitmap_num_grays > 1 ? slot->bitmap_num_grays : 2;
  for (int y = 0; y < g.h; ++y) {
    const unsigned char *src = slot->bitmap_buffer + static_cast<size_t>(y) * slot->bitmap_pitch;
    uint8_t *dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(g.w) * 4u;
    for (int x = 0; x < g.w; ++x) {
      dst[x * 4 + 0] = 255;
      dst[x * 4 + 1] = 255;
      dst[x * 4 + 2] = 255;
      dst[x * 4 + 3] = static_cast<uint8_t>(static_cast<unsigned>(src[x]) * 255u / (grays - 1u));
    }
  }
  // buffer のバイト列は R,G,B,A のまま。SDL_PIXELFORMAT_RGBA8888 は packed の
  // uint32 が 0xRRGGBBAA（little-endian ではバイト列 A,B,G,R）を指すため、この
  // buffer をそのまま渡すと RとAが入れ替わり alpha が色として化ける。
  // little-endian では RGBA32 == ABGR8888 == バイト列 R,G,B,A なので一致する。
  g.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, g.w, g.h);
  if (!g.tex) {
    gGuiText.glyphs.emplace(cp, g);
    return nullptr;
  }
  SDL_UpdateTexture(g.tex, nullptr, rgba.data(), g.w * 4);
  SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND);
  gGuiText.glyphs.emplace(cp, g);
  const auto &stored = gGuiText.glyphs.at(cp);
  return stored.tex ? &stored : nullptr;
}

// UTF-8 テキストを描く。scale は world→screen の拡縮率、budgetPx は screen px の上限。
// 上限を超えた部分は '~' で打ち切る。返り値は実際に使った幅（screen px）。
float drawGuiText(SDL_Renderer *r, float x, float y, const std::string &text, float scale,
                  float budgetPx, uint8_t cr, uint8_t cg, uint8_t cb) {
  if (!gGuiText.ready || scale <= 0.f) return 0.f;
  const float baseline = y + gGuiText.ascentPx * scale;
  float pen = 0.f;
  size_t i = 0;
  bool truncated = false;
  while (i < text.size()) {
    uint32_t cp = 0;
    if (!decodeUtf8(text, i, cp)) { ++i; continue; }
    const GlyphRec *g = guiGlyph(r, cp);
    if (!g) continue;
    if (pen + g->advance * scale > budgetPx) {
      truncated = true;
      break;
    }
    const SDL_FRect dst{x + pen + static_cast<float>(g->left) * scale,
                        baseline - static_cast<float>(g->top) * scale,
                        static_cast<float>(g->w) * scale, static_cast<float>(g->h) * scale};
    SDL_SetTextureColorMod(g->tex, cr, cg, cb);
    SDL_RenderTexture(r, g->tex, nullptr, &dst);
    pen += g->advance * scale;
  }
  if (truncated) {
    const GlyphRec *tilde = guiGlyph(r, '~');
    if (tilde) {
      const SDL_FRect dst{x + pen + static_cast<float>(tilde->left) * scale,
                          baseline - static_cast<float>(tilde->top) * scale,
                          static_cast<float>(tilde->w) * scale, static_cast<float>(tilde->h) * scale};
      SDL_SetTextureColorMod(tilde->tex, cr, cg, cb);
      SDL_RenderTexture(r, tilde->tex, nullptr, &dst);
      pen += tilde->advance * scale;
    }
  }
  return pen;
}

