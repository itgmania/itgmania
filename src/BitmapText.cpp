#include "BitmapText.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "Font.h"
#include "FontManager.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "Tween.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(BitmapText);

/* XXX:
 * We need some kind of font modifier string for metrics.  For example,
 * "valign=top;spacing = x+5,y+2"
 *
 * Better, we could go all the way, drop all of the actor-specific font aliases,
 * and do "font=header2;valign=top;...". */

/* XXX: Changing a whole array of diffuse colors every frame (several times) is
 * a waste, when we're usually setting them all to the same value. Rainbow and
 * fading are annoying to optimize, but rarely used. Iterating over every
 * character in Draw() is dumb. */

static std::vector<RageColor> rainbow_colors;

BitmapText::BitmapText() {
  // Loading these theme metrics is slow, so only do it every 20th time.
  // todo: why not check to see if you need to bother updating this at all? -aj
  static int reload_counter = 0;
  if (reload_counter % 20 == 0) {
    rainbow_colors.resize(THEME->GetMetricI("BitmapText", "NumRainbowColors"));
    for (unsigned i = 0; i < rainbow_colors.size(); ++i) {
      rainbow_colors[i] =
          THEME->GetMetricC("BitmapText", ssprintf("RainbowColor%i", i + 1));
    }
  }
  reload_counter++;

  font_ = nullptr;
  uppercase_ = false;

  rainbow_scroll_ = false;
  jitter_ = false;
  distortion_ = 0.0f;
  using_distortion_ = false;
  mult_attrs_with_diffuse_ = false;

  wrap_width_pixels_ = -1;
  max_width_ = 0;
  max_height_ = 0;
  vert_spacing_ = 0;
  max_dimension_uses_zoom_ = false;
  has_glow_attribute_ = false;
  // Never, this way we dont have awkward settings between themes. -Midiman
  SetShadowLength(0);
  // SM4SVN r28328, "draw glow using stroke texture" forces the BitmapText to
  // glow both the inner and stroke elements. This makes BitmapText elements
  // with an invisible stroke have a glowing stroke instead. Not good. -aj
  text_glow_mode_ = TextGlowMode_Both;  // Both used for compatibility with SM4
}

BitmapText::~BitmapText() {
  if (font_) {
    FONT->UnloadFont(font_);
  }
}

BitmapText& BitmapText::operator=(const BitmapText& cpy) {
  Actor::operator=(cpy);

  uppercase_ = cpy.uppercase_;
  text_ = cpy.text_;
  text_lines_ = cpy.text_lines_;
  line_widths_ = cpy.line_widths_;
  wrap_width_pixels_ = cpy.wrap_width_pixels_;
  max_width_ = cpy.max_width_;
  max_height_ = cpy.max_height_;
  rainbow_scroll_ = cpy.rainbow_scroll_;
  jitter_ = cpy.jitter_;
  distortion_ = cpy.distortion_;
  using_distortion_ = cpy.using_distortion_;
  mult_attrs_with_diffuse_ = cpy.mult_attrs_with_diffuse_;
  vert_spacing_ = cpy.vert_spacing_;
  max_dimension_uses_zoom_ = cpy.max_dimension_uses_zoom_;
  vertices_ = cpy.vertices_;
  font_page_textures_ = cpy.font_page_textures_;
  attributes_ = cpy.attributes_;
  has_glow_attribute_ = cpy.has_glow_attribute_;
  bmt_tweens_ = cpy.bmt_tweens_;
  bmt_current_ = cpy.bmt_current_;
  bmt_start_ = cpy.bmt_start_;

  if (font_) {
    FONT->UnloadFont(font_);
  }

  if (cpy.font_ != nullptr) {
    font_ = FONT->CopyFont(cpy.font_);
  } else {
    font_ = nullptr;
  }

  return *this;
}

BitmapText::BitmapText(const BitmapText& cpy) : Actor(cpy) {
  font_ = nullptr;

  *this = cpy;
}

void BitmapText::SetCurrentTweenStart() { bmt_start_ = bmt_current_; }

void BitmapText::EraseHeadTween() {
  if (bmt_tweens_.empty()) {
    return;
  }

  bmt_current_ = bmt_tweens_[0];
  bmt_tweens_.erase(bmt_tweens_.begin());
}

void BitmapText::UpdatePercentThroughTween(float between) {
  if (bmt_tweens_.empty()) {
    return;
  }

  BMT_TweenState::MakeWeightedAverage(
      bmt_current_, bmt_start_, bmt_tweens_[0], between);
}

void BitmapText::BeginTweening(float time, ITween* interp) {
  Actor::BeginTweening(time, interp);
  if (!bmt_tweens_.empty()) {
    bmt_tweens_.push_back(bmt_tweens_.back());
  } else {
    bmt_tweens_.push_back(bmt_current_);
  }
}

void BitmapText::StopTweening() {
  bmt_tweens_.clear();
  Actor::StopTweening();
}

void BitmapText::FinishTweening() {
  if (!bmt_tweens_.empty()) {
    bmt_current_ = BMT_DestTweenState();
  }
  Actor::FinishTweening();
}

void BitmapText::BMT_TweenState::MakeWeightedAverage(
    BMT_TweenState& out, const BMT_TweenState& from, const BMT_TweenState& to,
    float between) {
  out.stroke_color_.b_ =
      lerp(between, from.stroke_color_.b_, to.stroke_color_.b_);
  out.stroke_color_.g_ =
      lerp(between, from.stroke_color_.g_, to.stroke_color_.g_);
  out.stroke_color_.r_ =
      lerp(between, from.stroke_color_.r_, to.stroke_color_.r_);
  out.stroke_color_.a_ =
      lerp(between, from.stroke_color_.a_, to.stroke_color_.a_);
}

void BitmapText::LoadFromNode(const XNode* node) {
  std::string text;
  node->GetAttrValue("Text", text);
  std::string alt_text;
  node->GetAttrValue("AltText", alt_text);

  ThemeManager::EvaluateString(text);
  ThemeManager::EvaluateString(alt_text);

  std::string font;
  // Pass optional= true so that an error will not be reported if the path
  // doesn't resolve to a file.  This way, a font can be either a path or the
  // name of a font to look up in Fonts/.  -Kyz
  if (!ActorUtil::GetAttrPath(node, "Font", font, true) &&
      !ActorUtil::GetAttrPath(node, "File", font, true)) {
    if (!node->GetAttrValue("Font", font) &&
        !node->GetAttrValue(
            "File", font))  // accept "File" for backward compatibility
    {
      LuaHelpers::ReportScriptErrorFmt(
          "%s: BitmapText: Font or File attribute"
          " not found",
          ActorUtil::GetWhere(node).c_str());
      font = "Common Normal";
    }
    font = THEME->GetPathF("", font);
  }

  LoadFromFont(font);

  SetText(text, alt_text);
  Actor::LoadFromNode(node);
}

bool BitmapText::LoadFromFont(const std::string& font_file_path) {
  CHECKPOINT_M(
      ssprintf("BitmapText::LoadFromFont(%s)", font_file_path.c_str()));

  if (font_) {
    FONT->UnloadFont(font_);
    font_ = nullptr;
  }

  font_ = FONT->LoadFont(font_file_path);

  this->SetStrokeColor(font_->GetDefaultStrokeColor());

  BuildChars();

  return true;
}

bool BitmapText::LoadFromTextureAndChars(
    const std::string& texture_path, const std::string& chars) {
  CHECKPOINT_M(ssprintf(
      "BitmapText::LoadFromTextureAndChars(\"%s\",\"%s\")",
      texture_path.c_str(), chars.c_str()));

  if (font_) {
    FONT->UnloadFont(font_);
    font_ = nullptr;
  }

  font_ = FONT->LoadFont(texture_path, chars);

  BuildChars();

  return true;
}

void BitmapText::BuildChars() {
  // If we don't have a font yet, we'll do this when it loads.
  if (font_ == nullptr) {
    return;
  }

  // calculate line lengths and widths
  size_.x_ = 0;

  line_widths_.clear();
  for (unsigned l = 0; l < text_lines_.size(); l++)  // for each line
  {
    line_widths_.push_back(font_->GetLineWidthInSourcePixels(text_lines_[l]));
    size_.x_ = std::max(size_.x_, (float)line_widths_.back());
  }

  /* Ensure that the width is always even. This maintains pixel alignment;
   * fX below will always be an integer. */
  size_.x_ = QuantizeUp(size_.x_, 2.0f);

  vertices_.clear();
  font_page_textures_.clear();

  if (text_lines_.empty()) {
    return;
  }

  size_.y_ = float(font_->GetHeight() * text_lines_.size());

  // The height (from the origin to the baseline):
  int padding = font_->GetLineSpacing() - font_->GetHeight();
  padding += vert_spacing_;

  // There's padding between every line:
  size_.y_ += padding * int(text_lines_.size() - 1);

  // the top position of the first row of characters
  int y = std::lrint(-size_.y_ / 2.0f);

  for (unsigned i = 0; i < text_lines_.size(); i++)  // foreach line
  {
    y += font_->GetHeight();

    std::wstring line = text_lines_[i];
    if (font_->IsRightToLeft()) {
      reverse(line.begin(), line.end());
    }
    const int line_width = line_widths_[i];

    float f_x = SCALE(
        horiz_align_, 0.0f, 1.0f, -size_.x_ / 2.0f,
        +size_.x_ / 2.0f - line_width);
    int i_x = std::lrint(f_x);

    for (unsigned j = 0; j < line.size(); ++j) {
      RageSpriteVertex v[4];
      const glyph& g = font_->GetGlyph(line[j]);

      // Advance the cursor early for RTL(?)
      if (font_->IsRightToLeft()) {
        i_x -= g.hadvance_;
      }

      // set vertex positions
      v[0].p_ =
          RageVector3(i_x + g.hshift_, y + g.page_->vshift_, 0);  // top left
      v[1].p_ = RageVector3(
          i_x + g.hshift_, y + g.page_->vshift_ + g.height_,
          0);  // bottom left
      v[2].p_ = RageVector3(
          i_x + g.hshift_ + g.width_, y + g.page_->vshift_ + g.height_,
          0);  // bottom right
      v[3].p_ = RageVector3(
          i_x + g.hshift_ + g.width_, y + g.page_->vshift_,
          0);  // top right

      // Advance the cursor.
      if (!font_->IsRightToLeft()) {
        i_x += g.hadvance_;
      }

      // set texture coordinates
      v[0].t_ = RageVector2(g.tex_rect_.left_, g.tex_rect_.top_);
      v[1].t_ = RageVector2(g.tex_rect_.left_, g.tex_rect_.bottom_);
      v[2].t_ = RageVector2(g.tex_rect_.right_, g.tex_rect_.bottom_);
      v[3].t_ = RageVector2(g.tex_rect_.right_, g.tex_rect_.top_);

      vertices_.insert(vertices_.end(), &v[0], &v[4]);
      font_page_textures_.push_back(g.GetFontPageTextures());
    }

    // The amount of padding a line needs:
    y += padding;
  }

  if (using_distortion_) {
    int seed = RageTimer::GetTimeSinceStartSeconds() * 500000;
    RandomGen rnd(seed);
    for (unsigned int i = 0; i < vertices_.size(); i += 4) {
      float w = vertices_[i + 2].p_.x_ - vertices_[i].p_.x_;
      float h = vertices_[i + 2].p_.y_ - vertices_[i].p_.y_;
      for (unsigned int ioff = 0; ioff < 4; ++ioff) {
        vertices_[i + ioff].p_.x_ +=
            ((rnd() % 9) / 8.0f - .5f) * distortion_ * w;
        vertices_[i + ioff].p_.y_ +=
            ((rnd() % 9) / 8.0f - .5f) * distortion_ * h;
      }
    }
  }
}

void BitmapText::DrawChars(bool use_stroke_texture) {
  // bail if cropped all the way
  if (temp_state_->crop_.left_ + temp_state_->crop_.right_ >= 1 ||
      temp_state_->crop_.top_ + temp_state_->crop_.bottom_ >= 1) {
    return;
  }

  const int num_glyphs = font_page_textures_.size();
  int start_glyph = std::lrint(
      SCALE(temp_state_->crop_.left_, 0.f, 1.f, 0, (float)num_glyphs));
  int end_glyph = std::lrint(
      SCALE(temp_state_->crop_.right_, 0.f, 1.f, (float)num_glyphs, 0));
  start_glyph = std::clamp(start_glyph, 0, num_glyphs);
  end_glyph = std::clamp(end_glyph, 0, num_glyphs);

  if (temp_state_->fade_.top_ > 0 || temp_state_->fade_.bottom_ > 0 ||
      temp_state_->fade_.left_ > 0 || temp_state_->fade_.right_ > 0) {
    // Handle fading by tweaking the alpha values of the vertices.

    // Actual size of the fade on each side:
    const RectF& fade_dist = temp_state_->fade_;
    RectF fade_size = fade_dist;

    // If the cropped size is less than the fade distance, clamp.
    const float horiz_remaining =
        1.0f - (temp_state_->crop_.left_ + temp_state_->crop_.right_);
    if (fade_dist.left_ + fade_dist.right_ > 0 &&
        horiz_remaining < fade_dist.left_ + fade_dist.right_) {
      const float left_percent =
          fade_dist.left_ / (fade_dist.left_ + fade_dist.right_);
      fade_size.left_ = left_percent * horiz_remaining;
      fade_size.right_ = (1.0f - left_percent) * horiz_remaining;
    }

    /* We fade from 0 to LeftColor, then from RightColor to 0. (We won't fade
     * all the way to 0 if the crop is beyond the outer edge.) */
    const float right_alpha =
        SCALE(fade_size.right_, fade_dist.right_, 0, 1, 0);
    const float left_alpha = SCALE(fade_size.left_, fade_dist.left_, 0, 1, 0);

    const float start_fade_left_percent = temp_state_->crop_.left_;
    const float stop_fade_left_percent =
        temp_state_->crop_.left_ + fade_size.left_;
    const float left_fade_start_glyph =
        SCALE(start_fade_left_percent, 0.f, 1.f, 0, (float)num_glyphs);
    const float left_fade_stop_glyph =
        SCALE(stop_fade_left_percent, 0.f, 1.f, 0, (float)num_glyphs);

    const float start_fade_right_percent =
        1 - (temp_state_->crop_.right_ + fade_size.right_);
    const float stop_fade_right_percent = 1 - (temp_state_->crop_.right_);
    const float right_fade_start_glyph =
        SCALE(start_fade_right_percent, 0.f, 1.f, 0, (float)num_glyphs);
    const float right_fade_stop_glyph =
        SCALE(stop_fade_right_percent, 0.f, 1.f, 0, (float)num_glyphs);

    for (int start = start_glyph; start < end_glyph; ++start) {
      int i = start * 4;

      float alpha = 1.0f;
      if (fade_size.left_ > 0.001f) {
        // Add .5, so we fade wrt. the center of the vert, not the left side.
        float percent = SCALE(
            start + 0.5f, left_fade_start_glyph, left_fade_stop_glyph, 0.0f,
            1.0f);
        percent = std::clamp(percent, 0.0f, 1.0f);
        alpha *= percent * left_alpha;
      }

      if (fade_size.right_ > 0.001f) {
        float percent = SCALE(
            start + 0.5f, right_fade_start_glyph, right_fade_stop_glyph, 1.0f,
            0.0f);
        percent = std::clamp(percent, 0.0f, 1.0f);
        alpha *= percent * right_alpha;
      }

      for (int j = 0; j < 4; ++j) {
        vertices_[i + j].c_.a_ =
            (unsigned char)(vertices_[i + j].c_.a_ * alpha);
      }
    }
  }

  bool distance_field = font_->IsDistanceField();
  if (distance_field) {
    DISPLAY->SetEffectMode(EffectMode_DistanceField);
  }

  for (int start = start_glyph; start < end_glyph;) {
    int end = start;
    while (end < end_glyph &&
           *font_page_textures_[end] == *font_page_textures_[start]) {
      end++;
    }

    bool have_a_texture =
        !use_stroke_texture ||
        (use_stroke_texture && font_page_textures_[start]->texture_stroke_);
    if (have_a_texture) {
      DISPLAY->ClearAllTextures();
      if (use_stroke_texture) {
        DISPLAY->SetTexture(
            TextureUnit_1,
            font_page_textures_[start]->texture_stroke_->GetTexHandle());
      } else {
        DISPLAY->SetTexture(
            TextureUnit_1,
            font_page_textures_[start]->texture_main_->GetTexHandle());
      }

      // Don't bother setting texture render states for text. We never go
      // outside of 0..1.
      /* We should call SetTextureRenderStates because it does more than just
       * setting the texture wrapping state. If setting the wrapping state is
       * found to be slow, there should probably be a "don't care" texture
       * wrapping mode set in Actor. -Chris */

      // This is SLOW. We need to do something else about this. -Colby
      // Actor::SetTextureRenderStates();

      DISPLAY->DrawQuads(&vertices_[start * 4], (end - start) * 4);
    }

    start = end;
  }
  if (distance_field) {
    DISPLAY->SetEffectMode(EffectMode_Normal);
  }
}

/* sText is UTF-8. If not all of the characters in sText are available in the
 * font, sAlternateText will be used instead. If there are unavailable
 * characters in sAlternateText, too, just use sText. */
void BitmapText::SetText(
    const std::string& text, const std::string& alternate_text,
    int wrap_width_pixels) {
  ASSERT(font_ != nullptr);

  std::string new_text =
      StringWillUseAlternate(text, alternate_text) ? alternate_text : text;

  if (uppercase_) {
    MakeUpper(new_text);
  }

  if (wrap_width_pixels == -1) {  // wrap not specified
    wrap_width_pixels = wrap_width_pixels_;
  }

  if (text_ == new_text && wrap_width_pixels == wrap_width_pixels_) {
    return;
  }

  text_ = new_text;
  wrap_width_pixels_ = wrap_width_pixels;
  ClearAttributes();
  SetTextInternal();
}

void BitmapText::SetTextInternal() {
  // Break the string into lines.

  text_lines_.clear();

  if (wrap_width_pixels_ == -1) {
    split(RStringToWstring(text_), L"\n", text_lines_, false);
  } else {
    // Break sText into lines that don't exceed iWrapWidthPixels. (if only
    // one word fits on the line, it may be larger than iWrapWidthPixels).

    // This does not work in all languages:
    /* "...I can add Japanese wrapping, at least. We could handle hyphens
     * and soft hyphens and pretty easily, too." -glenn */
    // TODO: Move this wrapping logic into Font.
    std::vector<std::string> lines;
    split(text_, "\n", lines, false);

    for (unsigned line = 0; line < lines.size(); ++line) {
      std::vector<std::string> words;
      split(lines[line], " ", words);

      std::string cur_line;
      int cur_line_width = 0;

      for (unsigned i = 0; i < words.size(); i++) {
        const std::string& word = words[i];
        int width_word =
            font_->GetLineWidthInSourcePixels(RStringToWstring(word));

        if (cur_line.empty()) {
          cur_line = word;
          cur_line_width = width_word;
          continue;
        }

        std::string to_add = " " + word;
        int width_to_add = font_->GetLineWidthInSourcePixels(L" ") + width_word;
        if (cur_line_width + width_to_add <=
            wrap_width_pixels_)  // will fit on current line
        {
          cur_line += to_add;
          cur_line_width += width_to_add;
        } else {
          text_lines_.push_back(RStringToWstring(cur_line));
          cur_line = word;
          cur_line_width = width_word;
        }
      }
      text_lines_.push_back(RStringToWstring(cur_line));
    }
  }

  BuildChars();
  UpdateBaseZoom();
}

void BitmapText::SetVertSpacing(int spacing) {
  vert_spacing_ = spacing;
  BuildChars();
}

void BitmapText::SetMaxWidth(float max_width) {
  max_width_ = max_width;
  UpdateBaseZoom();
}

void BitmapText::SetMaxHeight(float max_height) {
  max_height_ = max_height;
  UpdateBaseZoom();
}

void BitmapText::SetMaxDimUseZoom(bool use) { max_dimension_uses_zoom_ = use; }

void BitmapText::SetUppercase(bool b) {
  uppercase_ = b;
  BuildChars();
}

void BitmapText::SetDistortion(float f) {
  distortion_ = f;
  using_distortion_ = true;
  BuildChars();
}

void BitmapText::UnSetDistortion() {
  using_distortion_ = false;
  BuildChars();
}

void BitmapText::set_mult_attrs_with_diffuse(bool m) {
  mult_attrs_with_diffuse_ = m;
  BuildChars();
}

bool BitmapText::get_mult_attrs_with_diffuse() {
  return mult_attrs_with_diffuse_;
}

void BitmapText::UpdateBaseZoom() {
  // don't divide by 0
  // Never apply a zoom greater than 1.
  // Factor in the non-base zoom so that maxwidth will be in terms of theme
  // pixels when zoom is used.

  constexpr float kMaxZoom = 1.0f;

  if (max_width_ == 0) {
    SetBaseZoomX(1);
  } else {
    float width = GetUnzoomedWidth();
    if (max_dimension_uses_zoom_) {
      width /= GetZoomX();
    }
    if (width != 0) {
      const float zoom = std::fmin(kMaxZoom, max_width_ / width);
      SetBaseZoomX(zoom);
    }
  }

  if (max_height_ == 0) {
    SetBaseZoomY(1);
  } else {
    float height = GetUnzoomedHeight();
    if (max_dimension_uses_zoom_) {
      height /= GetZoomY();
    }
    if (height != 0) {
      const float zoom = std::fmin(kMaxZoom, max_height_ / height);
      SetBaseZoomY(zoom);
    }
  }
}

bool BitmapText::StringWillUseAlternate(
    const std::string& text, const std::string& alternate_text) const {
  ASSERT(font_ != nullptr);

  // Can't use the alternate if there isn't one.
  if (!alternate_text.size()) {
    return false;
  }

  // False if the alternate isn't needed.
  if (font_->FontCompleteForString(RStringToWstring(text))) {
    return false;
  }

  // False if the alternate is also incomplete.
  if (!font_->FontCompleteForString(RStringToWstring(alternate_text))) {
    return false;
  }

  return true;
}

void BitmapText::CropLineToWidth(size_t l, int width) {
  if (l < text_lines_.size()) {
    int used_width = width;
    std::wstring& line = text_lines_[l];
    const size_t fit = font_->GetGlyphsThatFit(line, &used_width);
    if (fit < line.size()) {
      line.erase(line.begin() + fit, line.end());
    }
    line_widths_[l] = used_width;
  }
}

void BitmapText::CropToWidth(int width) {
  for (size_t l = 0; l < text_lines_.size(); ++l) {
    CropLineToWidth(l, width);
  }
  BuildChars();
}

bool BitmapText::EarlyAbortDraw() const { return text_lines_.empty(); }

// draw text at x, y using colorTop blended down to colorBottom, with size
// multiplied by scale
void BitmapText::DrawPrimitives() noexcept {
  Actor::SetGlobalRenderStates();  // set Actor-specified render states
  DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Modulate);

  // Draw if we're not fully transparent or the zbuffer is enabled
  if (temp_state_->diffuse_[0].a_ != 0) {
    // render the shadow
    if (shadow_length_x_ != 0 || shadow_length_y_ != 0) {
      DISPLAY->PushMatrix();
      DISPLAY->TranslateWorld(shadow_length_x_, shadow_length_y_, 0);

      RageColor c = shadow_color_;
      c.a_ *= temp_state_->diffuse_[0].a_;

      for (RageSpriteVertex& vertex : vertices_) {
        vertex.c_ = c;
      }

      DrawChars(false);

      DISPLAY->PopMatrix();
    }

    // render the stroke
    RageColor stroke_color = GetCurrStrokeColor();
    if (stroke_color.a_ > 0) {
      stroke_color.a_ *= temp_state_->diffuse_[0].a_;
      for (RageSpriteVertex& vertex : vertices_) {
        vertex.c_ = stroke_color;
      }
      DrawChars(true);
    }

    // render the diffuse pass
    if (rainbow_scroll_) {
      int color_index =
          int(RageTimer::GetTimeSinceStart() / 0.200) % rainbow_colors.size();
      for (size_t i = 0; i < vertices_.size(); i += 4) {
        const RageColor color = rainbow_colors[color_index];
        for (size_t j = i; j < i + 4; j++) {
          vertices_[j].c_ = color;
        }

        color_index = (color_index + 1) % rainbow_colors.size();
      }
    } else {
      size_t i = 0;
      std::map<size_t, Attribute>::const_iterator iter = attributes_.begin();
      while (i < vertices_.size()) {
        // Set the colors up to the next attribute.
        size_t end =
            iter == attributes_.end() ? vertices_.size() : iter->first * 4;
        end = std::min(end, vertices_.size());  // clamp to vertex size
        for (; i < end; i += 4) {
          vertices_[i + 0].c_ = temp_state_->diffuse_[0];  // top left
          vertices_[i + 1].c_ = temp_state_->diffuse_[2];  // bottom left
          vertices_[i + 2].c_ = temp_state_->diffuse_[3];  // bottom right
          vertices_[i + 3].c_ = temp_state_->diffuse_[1];  // top right
        }
        if (iter == attributes_.end()) {
          break;
        }
        // Set the colors according to this attribute.
        const Attribute& attr = iter->second;
        ++iter;
        if (attr.length_ < 0) {
          end = iter == attributes_.end() ? vertices_.size() : iter->first * 4;
        } else {
          end = i + attr.length_ * 4;
        }
        end = std::min(end, vertices_.size());  // clamp to vertex size
        std::vector<RageColor> temp_attr_diffuse(
            kNumDiffuseColors, internal_diffuse_);
        for (size_t c = 0; c < kNumDiffuseColors; ++c) {
          temp_attr_diffuse[c] *= attr.diffuse_[c];
          if (mult_attrs_with_diffuse_) {
            temp_attr_diffuse[c] *= temp_state_->diffuse_[c];
          }
        }
        for (; i < end; i += 4) {
          vertices_[i + 0].c_ = temp_attr_diffuse[0];  // top left
          vertices_[i + 1].c_ = temp_attr_diffuse[2];  // bottom left
          vertices_[i + 2].c_ = temp_attr_diffuse[3];  // bottom right
          vertices_[i + 3].c_ = temp_attr_diffuse[1];  // top right
        }
      }
    }

    // apply jitter to verts
    std::vector<RageVector3> glyph_jitter;
    if (jitter_) {
      int seed = RageTimer::GetTimeSinceStartSeconds() * 8;
      RandomGen rnd(seed);
      for (size_t i = 0; i < vertices_.size(); i += 4) {
        RageVector3 jitter(rnd() % 2, rnd() % 3, 0);
        glyph_jitter.push_back(jitter);

        vertices_[i + 0].p_ += jitter;  // top left
        vertices_[i + 1].p_ += jitter;  // bottom left
        vertices_[i + 2].p_ += jitter;  // bottom right
        vertices_[i + 3].p_ += jitter;  // top right
      }
    }

    DrawChars(false);

    // undo jitter to verts
    if (jitter_) {
      ASSERT(glyph_jitter.size() == vertices_.size() / 4);
      for (size_t i = 0; i < vertices_.size(); i += 4) {
        const RageVector3& jitter = glyph_jitter[i / 4];
        ;

        vertices_[i + 0].p_ -= jitter;  // top left
        vertices_[i + 1].p_ -= jitter;  // bottom left
        vertices_[i + 2].p_ -= jitter;  // bottom right
        vertices_[i + 3].p_ -= jitter;  // top right
      }
    }
  }

  // render the glow pass
  if (temp_state_->glow_.a_ > 0.0001f || has_glow_attribute_) {
    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Glow);

    size_t i = 0;
    auto iter = attributes_.begin();
    while (i < vertices_.size()) {
      size_t end =
          iter == attributes_.end() ? vertices_.size() : iter->first * 4;
      end = std::min(end, vertices_.size());  // clamp to vertex size
      for (; i < end; ++i) {
        vertices_[i].c_ = temp_state_->glow_;
      }
      if (iter == attributes_.end()) {
        break;
      }
      // Set the glow according to this attribute.
      const Attribute& attr = iter->second;
      ++iter;
      if (attr.length_ < 0) {
        end = iter == attributes_.end() ? vertices_.size() : iter->first * 4;
      } else {
        end = i + attr.length_ * 4;
      }
      end = std::min(end, vertices_.size());  // clamp to vertex size
      for (; i < end; ++i) {
        if (internal_glow_.a_ > 0) {
          vertices_[i].c_ = attr.glow_ * internal_glow_;
        } else {
          vertices_[i].c_ = attr.glow_;
        }
      }
    }
    /* Draw glow using the base texture and the glow texture. Otherwise,
     * glow looks too tame on BitmapText that has a stroke. - Chris Danford */
    /* This doesn't work well if the font is using an invisible stroke, as
     * the invisible stroke will glow as well. Time for TextGlowMode.
     * Only draw the strokes if the glow mode is not inner only. -aj */
    DrawChars(text_glow_mode_ != TextGlowMode_Inner);
  }
}

// Rebuild when these change.
void BitmapText::SetHorizAlign(float f) {
  float horiz_align = horiz_align_;
  Actor::SetHorizAlign(f);
  if (horiz_align == horiz_align_) {
    return;
  }
  BuildChars();
}

void BitmapText::SetWrapWidthPixels(int wrap_width_pixels) {
  ASSERT(font_ != nullptr);  // always load a font first
  if (wrap_width_pixels_ == wrap_width_pixels) {
    return;
  }
  wrap_width_pixels_ = wrap_width_pixels;
  SetTextInternal();
}

BitmapText::Attribute BitmapText::GetDefaultAttribute() const {
  Attribute attr;
  for (int i = 0; i < 4; ++i) {
    attr.diffuse_[i] = GetDiffuses(i);
  }
  attr.glow_ = GetGlow();
  return attr;
}

void BitmapText::AddAttribute(size_t pos, const Attribute& attr) {
  // Fixup position for new lines.
  Attribute new_attr = attr;
  auto line_iter = text_lines_.cbegin();

  int lines = 0;
  size_t adjusted_pos = pos;

  for (; line_iter != text_lines_.cend(); ++line_iter) {
    size_t length = line_iter->length() +
                    1;  // +1 to account for implicit newline at the end
    if (length > adjusted_pos) {
      break;
    }
    adjusted_pos -= length;
    ++lines;
  }

  if (new_attr.length_ > 0) {
    // Fixup length for new lines.
    size_t adjusted_end_pos = adjusted_pos + new_attr.length_;
    for (; line_iter != text_lines_.cend(); ++line_iter) {
      size_t length = line_iter->length() +
                      1;  // +1 to account for implicit newline at the end
      if (length > adjusted_end_pos || new_attr.length_ == 0) {
        break;
      }
      adjusted_end_pos -= length;
      new_attr.length_ -= 1;
    }
  }

  if (new_attr.length_ ==
      0) {  // Attribute doesn't cover any printable characters
    return;
  }

  // Check if there are existing attributes overlapping this one. We might need
  // to remove or fix them up.
  const size_t start_pos = pos - lines;
  const size_t end_pos = start_pos + new_attr.length_;

  // First attribute starting at the same position or further than the new
  // attribute
  const auto iter_first_after_start = attributes_.lower_bound(start_pos);
  if (iter_first_after_start != attributes_.begin()) {
    // Last attribute starting at earlier position than the new attribute (if it
    // exists)
    auto iter_last_before_start = iter_first_after_start;
    --iter_last_before_start;

    // Fixup the length so that it ends before the new attribute
    iter_last_before_start->second.length_ = std::min(
        iter_last_before_start->second.length_,
        static_cast<int>(start_pos - iter_last_before_start->first));
  }

  // First attribute starting after the end of the new attribute
  auto iter_last_before_end = attributes_.lower_bound(end_pos);
  if (iter_last_before_end != iter_first_after_start) {
    // Go back one, so that we are at the last overlapping attribute
    --iter_last_before_end;
    const bool last_attr_overlapping_completely =
        iter_last_before_end->first + iter_last_before_end->second.length_ <=
        end_pos;

    auto iter_erase_end = iter_last_before_end;
    // If it's overlapping completely, erase it as well
    if (last_attr_overlapping_completely) {
      ++iter_erase_end;
    }
    attributes_.erase(iter_first_after_start, iter_erase_end);

    // Otherwise it's only overlapping partially so fix it up
    if (!last_attr_overlapping_completely) {
      // Fixup the length accordingly
      Attribute last_attr = iter_last_before_end->second;
      last_attr.length_ -= end_pos - iter_last_before_end->first;

      // Erase it and insert just after the new attribute
      attributes_.erase(iter_last_before_end);
      attributes_[end_pos] = last_attr;
    }
  }

  attributes_[start_pos] = new_attr;
  has_glow_attribute_ = has_glow_attribute_ || attr.glow_.a_ > 0.0001f;
}

void BitmapText::ClearAttributes() {
  attributes_.clear();
  has_glow_attribute_ = false;
}

void BitmapText::Attribute::FromStack(lua_State* L, int pos) {
  if (lua_type(L, pos) != LUA_TTABLE) {
    return;
  }

  lua_pushvalue(L, pos);
  const int tab = lua_gettop(L);

  // Get the length.
  lua_getfield(L, tab, "Length");
  length_ = lua_tointeger(L, -1);
  lua_settop(L, tab);

  // Get the diffuse colors.
  lua_getfield(L, tab, "Diffuses");
  if (!lua_isnil(L, -1)) {
    for (int i = 1; i <= kNumDiffuseColors; ++i) {
      lua_rawgeti(L, -i, i);
      diffuse_[i - 1].FromStack(L, -1);
    }
  }
  lua_settop(L, tab);

  // Get a single diffuse color.
  lua_getfield(L, tab, "Diffuse");
  if (!lua_isnil(L, -1)) {
    diffuse_[0].FromStack(L, -1);
    diffuse_[1] = diffuse_[2] = diffuse_[3] = diffuse_[0];
  }
  lua_settop(L, tab);

  // Get the glow color.
  lua_getfield(L, tab, "Glow");
  glow_.FromStack(L, -1);

  lua_settop(L, tab - 1);
}

// lua start
#include "FontCharAliases.h"

/** @brief Allow Lua to have access to the BitmapText. */
class LunaBitmapText : public Luna<BitmapText> {
 public:
  static int wrapwidthpixels(T* p, lua_State* L) {
    p->SetWrapWidthPixels(IArg(1));
    COMMON_RETURN_SELF;
  }
#define MAX_DIMENSION(maxdimension, SetMaxDimension) \
  static int maxdimension(T* p, lua_State* L) {      \
    p->SetMaxDimension(FArg(1));                     \
    COMMON_RETURN_SELF;                              \
  }
  MAX_DIMENSION(maxwidth, SetMaxWidth);
  MAX_DIMENSION(maxheight, SetMaxHeight);
#undef MAX_DIMENSION
  static int max_dimension_use_zoom(T* p, lua_State* L) {
    p->SetMaxDimUseZoom(lua_toboolean(L, 1));
    COMMON_RETURN_SELF;
  }
  static int vertspacing(T* p, lua_State* L) {
    p->SetVertSpacing(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int settext(T* p, lua_State* L) {
    std::string s = SArg(1);
    std::string alt;
    /* XXX: Lua strings should simply use "\n" natively. However, some
     * settext calls may be made from GetMetric() calls to other strings, and
     * it's confusing for :: to work in some strings and not others.
     * Eventually, all strings should be Lua expressions, but until then,
     * continue to support this. */
    Replace(s, "::", "\n");
    FontCharAliases::ReplaceMarkers(s);

    if (lua_gettop(L) > 1) {
      alt = SArg(2);
      Replace(alt, "::", "\n");
      FontCharAliases::ReplaceMarkers(alt);
    }

    p->SetText(s, alt);
    COMMON_RETURN_SELF;
  }
  static int rainbowscroll(T* p, lua_State* L) {
    p->SetRainbowScroll(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int jitter(T* p, lua_State* L) {
    p->SetJitter(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int distort(T* p, lua_State* L) {
    p->SetDistortion(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int undistort(T* p, lua_State* L) {
    p->UnSetDistortion();
    COMMON_RETURN_SELF;
  }
  GETTER_SETTER_BOOL_METHOD(mult_attrs_with_diffuse);
  static int GetText(T* p, lua_State* L) {
    lua_pushstring(L, p->GetText().c_str());
    return 1;
  }
  static int AddAttribute(T* p, lua_State* L) {
    size_t pos = IArg(1);
    BitmapText::Attribute attr = p->GetDefaultAttribute();

    attr.FromStack(L, 2);
    p->AddAttribute(pos, attr);
    COMMON_RETURN_SELF;
  }
  static int ClearAttributes(T* p, lua_State* L) {
    p->ClearAttributes();
    COMMON_RETURN_SELF;
  }
  static int strokecolor(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetStrokeColor(c);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(getstrokecolor, GetStrokeColor());
  static int uppercase(T* p, lua_State* L) {
    p->SetUppercase(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int textglowmode(T* p, lua_State* L) {
    p->SetTextGlowMode(Enum::Check<TextGlowMode>(L, 1));
    COMMON_RETURN_SELF;
  }

  LunaBitmapText() {
    ADD_METHOD(wrapwidthpixels);
    ADD_METHOD(maxwidth);
    ADD_METHOD(maxheight);
    ADD_METHOD(max_dimension_use_zoom);
    ADD_METHOD(vertspacing);
    ADD_METHOD(settext);
    ADD_METHOD(rainbowscroll);
    ADD_METHOD(jitter);
    ADD_METHOD(distort);
    ADD_METHOD(undistort);
    ADD_GET_SET_METHODS(mult_attrs_with_diffuse);
    ADD_METHOD(GetText);
    ADD_METHOD(AddAttribute);
    ADD_METHOD(ClearAttributes);
    ADD_METHOD(strokecolor);
    ADD_METHOD(getstrokecolor);
    ADD_METHOD(uppercase);
    ADD_METHOD(textglowmode);
    // ADD_METHOD( LoadFromFont );
    // ADD_METHOD( LoadFromTextureAndChars );
  }
};

LUA_REGISTER_DERIVED_CLASS(BitmapText, Actor)

// lua end

/*
 * (c) 2003-2007 Chris Danford, Charles Lohr, Steve Checkoway
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
