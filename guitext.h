#pragma once
// FreeType dlopen GUI テキストエンジン。定義は guitext.cpp（gGuiText の fontPath/lastError/ready は selftest font phase から参照される）。
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

// ============================================================================
// --- 日本語テキスト描画: FreeType を dlopen で使う ---
// ============================================================================
// SDL_RenderDebugText は ASCII の bitmap font のため、dialogue の日本語は '?' に
// 置き換えて表示していた。FreeType を動的ロードし、システムの日本語フォントから
// glyph を rasterize して SDL_Texture に cache し、UTF-8 をそのまま描く。
// freetype / 日本語フォントが無い環境では従来の ASCII 代替表示に戻る。
// freetype ヘッダは使わず、dlsym で関数を取り、安定した ABI の先頭フィールドだけを
// 手で宣言して参照する（初期化時の sanity check でレイアウト検証する）。

constexpr int kGuiTextPx = 14;      // 基準 px。world 側は zoom で拡縮して描く
constexpr size_t kGlyphCacheMax = 512;

struct DFTFaceRec {  // FT_FaceRec の先頭 ~ glyph フィールドまで
  long num_faces;
  long face_index;
  long face_flags;
  long style_flags;
  long num_glyphs;
  char *family_name;
  char *style_name;
  int num_fixed_sizes;
  void *available_sizes;
  int num_charmaps;
  void *charmaps;
  void *generic_data;
  void (*generic_finalizer)(void *);
  long bbox_xmin;
  long bbox_ymin;
  long bbox_xmax;
  long bbox_ymax;
  unsigned short units_per_em;
  short ascender;
  short descender;
  short height;
  short max_advance_width;
  short max_advance_height;
  short underline_position;
  short underline_thickness;
  void *glyph;  // FT_GlyphSlot
};

struct DFTGlyphSlotRec {  // FT_GlyphSlotRec の先頭 ~ bitmap_top まで
  void *library;
  void *face;
  void *next;
  unsigned glyph_index;
  void *generic_data;
  void (*generic_finalizer)(void *);
  long metrics_width;
  long metrics_height;
  long metrics_hori_bearing_x;
  long metrics_hori_bearing_y;
  long metrics_hori_advance;
  long metrics_vert_bearing_x;
  long metrics_vert_bearing_y;
  long metrics_vert_advance;
  long linear_hori_advance;
  long linear_vert_advance;
  long advance_x;
  long advance_y;
  // FT_Glyph_Format の enum は FOUR_CHAR_CODE の値を取るため、この ABI では
  // 8 バイト（long）になる。int で宣言すると bitmap の offset が 4 字节ずれる。
  long format;
  unsigned bitmap_rows;
  unsigned bitmap_width;
  int bitmap_pitch;
  unsigned char *bitmap_buffer;
  unsigned short bitmap_num_grays;
  unsigned char bitmap_pixel_mode;
  unsigned char bitmap_palette_mode;
  void *bitmap_palette;
  int bitmap_left;
  int bitmap_top;
};

typedef int (*FnFTInit)(void **);
typedef void (*FnFTDone)(void *);
typedef int (*FnFTNewFace)(void *, const char *, long, void **);
typedef void (*FnFTDoneFace)(void *);
typedef int (*FnFTSetPixelSizes)(void *, unsigned, unsigned);
typedef unsigned (*FnFTCharIndex)(void *, unsigned long);
typedef int (*FnFTLoadGlyph)(void *, unsigned, int);
typedef int (*FnFTRenderGlyph)(void *, int);

constexpr int kFTLoadNoHinting = 1 << 1;  // FT_LOAD_NO_HINTING
constexpr int kFTLoadNoAutoHint = 1 << 15;  // FT_LOAD_NO_AUTOHINT
constexpr int kFTRenderNormal = 0;         // FT_RENDER_MODE_NORMAL

struct GlyphRec {
  SDL_Texture *tex = nullptr;
  int w = 0;
  int h = 0;
  int left = 0;
  int top = 0;
  float advance = 0.f;
};

struct GuiTextEngine {
  void *dl = nullptr;
  void *ft = nullptr;
  void *face = nullptr;
  FnFTInit fnInit = nullptr;
  FnFTDone fnDone = nullptr;
  FnFTNewFace fnNewFace = nullptr;
  FnFTDoneFace fnDoneFace = nullptr;
  FnFTSetPixelSizes fnSetSizes = nullptr;
  FnFTCharIndex fnCharIndex = nullptr;
  FnFTLoadGlyph fnLoadGlyph = nullptr;
  FnFTRenderGlyph fnRenderGlyph = nullptr;
  float ascentPx = 0.f;
  float descentPx = 0.f;
  bool ready = false;
  std::string fontPath;
  std::string lastError;
  std::unordered_map<uint32_t, GlyphRec> glyphs;
};


extern GuiTextEngine gGuiText;

inline bool decodeUtf8(const std::string &s, size_t &i, uint32_t &cp) {
  if (i >= s.size()) return false;
  const unsigned char b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    cp = b0;
    ++i;
    return true;
  }
  size_t need;
  uint32_t acc;
  if ((b0 & 0xE0) == 0xC0) { need = 1; acc = b0 & 0x1F; }
  else if ((b0 & 0xF0) == 0xE0) { need = 2; acc = b0 & 0x0F; }
  else if ((b0 & 0xF8) == 0xF0) { need = 3; acc = b0 & 0x07; }
  else return false;
  if (i + need >= s.size()) return false;
  for (size_t k = 1; k <= need; ++k) {
    const unsigned char b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xC0) != 0x80) return false;
    acc = (acc << 6) | (b & 0x3F);
  }
  i += need + 1;
  cp = acc;
  return true;
}

bool loadGuiTextEngine();
std::vector<std::string> fontCandidates();
bool tryGuiFont(const std::string &path);
bool initGuiText();
void shutdownGuiText();
const GlyphRec *guiGlyph(SDL_Renderer *r, uint32_t cp);
float drawGuiText(SDL_Renderer *r, float x, float y, const std::string &text, float scale,
                  float budgetPx, uint8_t cr, uint8_t cg, uint8_t cb);
