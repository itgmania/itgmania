/* RageTypes - vector and matrix types. */

#ifndef RAGETYPES_H
#define RAGETYPES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "EnumHelper.h"

enum BlendMode {
  BLEND_NORMAL,
  BLEND_ADD,
  BLEND_SUBTRACT,
  BLEND_MODULATE,
  BLEND_COPY_SRC,
  BLEND_ALPHA_MASK,
  BLEND_ALPHA_KNOCK_OUT,
  BLEND_ALPHA_MULTIPLY,
  BLEND_WEIGHTED_MULTIPLY,
  BLEND_INVERT_DEST,
  BLEND_NO_EFFECT,
  NUM_BlendMode,
  BlendMode_Invalid
};
LuaDeclareType(BlendMode);

enum TextureMode {
  // Affects one texture stage. Texture is modulated with the diffuse color.
  TextureMode_Modulate,

  /* Affects one texture stage. Color is replaced with white, leaving alpha.
   * Used with BLEND_ADD to add glow. */
  TextureMode_Glow,

  // Affects one texture stage. Color is added to the previous texture stage.
  TextureMode_Add,

  NUM_TextureMode,
  TextureMode_Invalid
};
LuaDeclareType(TextureMode);

enum EffectMode {
  EffectMode_Normal,
  EffectMode_Unpremultiply,
  EffectMode_ColorBurn,
  EffectMode_ColorDodge,
  EffectMode_VividLight,
  EffectMode_HardMix,
  EffectMode_Overlay,
  EffectMode_Screen,
  EffectMode_YUYV422,
  EffectMode_DistanceField,
  NUM_EffectMode,
  EffectMode_Invalid
};
LuaDeclareType(EffectMode);

enum CullMode {
  CULL_BACK,
  CULL_FRONT,
  CULL_NONE,
  NUM_CullMode,
  CullMode_Invalid
};
LuaDeclareType(CullMode);

enum ZTestMode {
  ZTEST_OFF,
  ZTEST_WRITE_ON_PASS,
  ZTEST_WRITE_ON_FAIL,
  NUM_ZTestMode,
  ZTestMode_Invalid
};
LuaDeclareType(ZTestMode);

enum TextGlowMode {
  TextGlowMode_Inner,
  TextGlowMode_Stroke,
  TextGlowMode_Both,
  NUM_TextGlowMode,
  TextGlowMode_Invalid
};
LuaDeclareType(TextGlowMode);

struct lua_State;

struct RageVector2 {
 public:
  RageVector2() : x_(0), y_(0) {}
  RageVector2(const float* f) : x_(f[0]), y_(f[1]) {}
  RageVector2(float x1, float y1) : x_(x1), y_(y1) {}

  // casting
  operator float*() { return &x_; };
  operator const float*() const { return &x_; };

  // assignment operators
  RageVector2& operator+=(const RageVector2& other) {
    x_ += other.x_;
    y_ += other.y_;
    return *this;
  }
  RageVector2& operator-=(const RageVector2& other) {
    x_ -= other.x_;
    y_ -= other.y_;
    return *this;
  }
  RageVector2& operator*=(float f) {
    x_ *= f;
    y_ *= f;
    return *this;
  }
  RageVector2& operator/=(float f) {
    x_ /= f;
    y_ /= f;
    return *this;
  }

  // binary operators
  RageVector2 operator+(const RageVector2& other) const {
    return RageVector2(x_ + other.x_, y_ + other.y_);
  }
  RageVector2 operator-(const RageVector2& other) const {
    return RageVector2(x_ - other.x_, y_ - other.y_);
  }
  RageVector2 operator*(float f) const { return RageVector2(x_ * f, y_ * f); }
  RageVector2 operator/(float f) const { return RageVector2(x_ / f, y_ / f); }

  friend RageVector2 operator*(float f, const RageVector2& other) {
    return other * f;
  }

  float x_, y_;
};

struct RageVector3 {
 public:
  RageVector3() : x_(0), y_(0), z_(0) {}
  RageVector3(const float* f) : x_(f[0]), y_(f[1]), z_(f[2]) {}
  RageVector3(float x1, float y1, float z1) : x_(x1), y_(y1), z_(z1) {}

  // casting
  operator float*() { return &x_; };
  operator const float*() const { return &x_; };

  // assignment operators
  RageVector3& operator+=(const RageVector3& other) {
    x_ += other.x_;
    y_ += other.y_;
    z_ += other.z_;
    return *this;
  }
  RageVector3& operator-=(const RageVector3& other) {
    x_ -= other.x_;
    y_ -= other.y_;
    z_ -= other.z_;
    return *this;
  }
  RageVector3& operator*=(float f) {
    x_ *= f;
    y_ *= f;
    z_ *= f;
    return *this;
  }
  RageVector3& operator/=(float f) {
    x_ /= f;
    y_ /= f;
    z_ /= f;
    return *this;
  }

  // binary operators
  RageVector3 operator+(const RageVector3& other) const {
    return RageVector3(x_ + other.x_, y_ + other.y_, z_ + other.z_);
  }
  RageVector3 operator-(const RageVector3& other) const {
    return RageVector3(x_ - other.x_, y_ - other.y_, z_ - other.z_);
  }
  RageVector3 operator*(float f) const {
    return RageVector3(x_ * f, y_ * f, z_ * f);
  }
  RageVector3 operator/(float f) const {
    return RageVector3(x_ / f, y_ / f, z_ / f);
  }

  friend RageVector3 operator*(float f, const RageVector3& other) {
    return other * f;
  }

  float x_, y_, z_;
};

struct RageVector4 {
 public:
  RageVector4() : x_(0), y_(0), z_(0), w_(0) {}
  RageVector4(const float* f) : x_(f[0]), y_(f[1]), z_(f[2]), w_(f[3]) {}
  RageVector4(float x1, float y1, float z1, float w1)
      : x_(x1), y_(y1), z_(z1), w_(w1) {}

  // casting
  operator float*() { return &x_; };
  operator const float*() const { return &x_; };

  // assignment operators
  RageVector4& operator+=(const RageVector4& other) {
    x_ += other.x_;
    y_ += other.y_;
    z_ += other.z_;
    w_ += other.w_;
    return *this;
  }
  RageVector4& operator-=(const RageVector4& other) {
    x_ -= other.x_;
    y_ -= other.y_;
    z_ -= other.z_;
    w_ -= other.w_;
    return *this;
  }
  RageVector4& operator*=(float f) {
    x_ *= f;
    y_ *= f;
    z_ *= f;
    w_ *= f;
    return *this;
  }
  RageVector4& operator/=(float f) {
    x_ /= f;
    y_ /= f;
    z_ /= f;
    w_ /= f;
    return *this;
  }

  // binary operators
  RageVector4 operator+(const RageVector4& other) const {
    return RageVector4(
        x_ + other.x_, y_ + other.y_, z_ + other.z_, w_ + other.w_);
  }
  RageVector4 operator-(const RageVector4& other) const {
    return RageVector4(
        x_ - other.x_, y_ - other.y_, z_ - other.z_, w_ - other.w_);
  }
  RageVector4 operator*(float f) const {
    return RageVector4(x_ * f, y_ * f, z_ * f, w_ * f);
  }
  RageVector4 operator/(float f) const {
    return RageVector4(x_ / f, y_ / f, z_ / f, w_ / f);
  }

  friend RageVector4 operator*(float f, const RageVector4& other) {
    return other * f;
  }

  float x_, y_, z_, w_;
};

struct RageColor {
 public:
  RageColor() : r_(0), g_(0), b_(0), a_(0) {}
  explicit RageColor(const float* f) : r_(f[0]), g_(f[1]), b_(f[2]), a_(f[3]) {}
  RageColor(float r1, float g1, float b1, float a1)
      : r_(r1), g_(g1), b_(b1), a_(a1) {}

  RageColor& operator=(const RageVector4& rv4) {
    r_ = rv4.x_;
    g_ = rv4.y_;
    b_ = rv4.z_;
    a_ = rv4.w_;
    return *this;
  }

  // casting
  operator float*() { return &r_; };
  operator const float*() const { return &r_; };

  // assignment operators
  RageColor& operator+=(const RageColor& other) {
    r_ += other.r_;
    g_ += other.g_;
    b_ += other.b_;
    a_ += other.a_;
    return *this;
  }
  RageColor& operator-=(const RageColor& other) {
    r_ -= other.r_;
    g_ -= other.g_;
    b_ -= other.b_;
    a_ -= other.a_;
    return *this;
  }
  RageColor& operator*=(const RageColor& other) {
    r_ *= other.r_;
    g_ *= other.g_;
    b_ *= other.b_;
    a_ *= other.a_;
    return *this;
  }
  RageColor& operator*=(float f) {
    r_ *= f;
    g_ *= f;
    b_ *= f;
    a_ *= f;
    return *this;
  }
  /* Divide is rarely useful: you can always use multiplication, and you don't
   * have to worry about div/0. */
  //    RageColor& operator /= ( float f )		{ r/=f; g/=f; b/=f;
  //    a/=f; return *this; }

  // binary operators
  RageColor operator+(const RageColor& other) const {
    return RageColor(
        r_ + other.r_, g_ + other.g_, b_ + other.b_, a_ + other.a_);
  }
  RageColor operator-(const RageColor& other) const {
    return RageColor(
        r_ - other.r_, g_ - other.g_, b_ - other.b_, a_ - other.a_);
  }
  RageColor operator*(const RageColor& other) const {
    return RageColor(
        r_ * other.r_, g_ * other.g_, b_ * other.b_, a_ * other.a_);
  }
  RageColor operator*(float f) const {
    return RageColor(r_ * f, g_ * f, b_ * f, a_ * f);
  }
  // Divide is useful for using with the SCALE macro
  RageColor operator/(float f) const {
    return RageColor(r_ / f, g_ / f, b_ / f, a_ / f);
  }

  friend RageColor operator*(float f, const RageColor& other) {
    return other * f;
  }  // What is this for?  Did I add this?  -Chris

  bool operator==(const RageColor& other) const {
    return r_ == other.r_ && g_ == other.g_ && b_ == other.b_ && a_ == other.a_;
  }
  bool operator!=(const RageColor& other) const { return !operator==(other); }

  bool FromString(const std::string& str) {
    int result = sscanf(str.c_str(), "%f,%f,%f,%f", &r_, &g_, &b_, &a_);
    if (result == 3) {
      a_ = 1;
      return true;
    }
    if (result == 4) {
      return true;
    }

    unsigned int ir = 255, ib = 255, ig = 255, ia = 255;
    result = sscanf(str.c_str(), "#%2x%2x%2x%2x", &ir, &ig, &ib, &ia);
    if (result >= 3) {
      r_ = ir / 255.0f;
      g_ = ig / 255.0f;
      b_ = ib / 255.0f;
      if (result == 4) {
        a_ = ia / 255.0f;
      } else {
        a_ = 1;
      }
      return true;
    }

    r_ = 1;
    b_ = 1;
    g_ = 1;
    a_ = 1;
    return false;
  }

  std::string ToString() const;
  static std::string NormalizeColorString(std::string color);

  void PushTable(lua_State* L) const;
  void FromStack(lua_State* L, int pos);
  void FromStackCompat(lua_State* L, int pos);

  float r_, g_, b_, a_;
};

/* Convert floating-point 0..1 value to integer 0..255 value. *
 *
 * As a test case,
 *
 * int cnts[1000]; memset(cnts, 0, sizeof(cnts));
 * for( float n = 0; n <= 1.0; n += 0.0001 ) cnts[FTOC(n)]++;
 * for( int i = 0; i < 256; ++i ) printf("%i ", cnts[i]);
 *
 * should output the same value (+-1) 256 times.  If this function is
 * incorrect, the first and/or last values may be biased. */
inline unsigned char FTOC(float a) {
  int value = static_cast<int>(a * 256.0f);
  return static_cast<unsigned char>(std::clamp(value, 0, 255));
}

/* Color type used only in vertex lists.  OpenGL expects colors in
 * r, g, b, a order, independent of endianness, so storing them this
 * way avoids endianness problems.  Don't try to manipulate this; only
 * manip RageColors. */
/* Perhaps the math in RageColor could be moved to RageVColor.  We don't need
 * the precision of a float for our calculations anyway.   -Chris */
class RageVColor {
 public:
  uint8_t b_, g_, r_, a_;  // specific ordering required by Direct3D

  RageVColor() : b_(0), g_(0), r_(0), a_(0) {}
  RageVColor(const RageColor& rc) : b_(0), g_(0), r_(0), a_(0) { *this = rc; }
  RageVColor& operator=(const RageColor& rc) {
    r_ = FTOC(rc.r_);
    g_ = FTOC(rc.g_);
    b_ = FTOC(rc.b_);
    a_ = FTOC(rc.a_);
    return *this;
  }
};

namespace StepMania {
template <class T>
class Rect {
 public:
  Rect() : left_(0), top_(0), right_(0), bottom_(0) {}
  Rect(T l, T t, T r, T b) : left_(l), top_(t), right_(r), bottom_(b) {}

  T GetWidth() const { return right_ - left_; };
  T GetHeight() const { return bottom_ - top_; };

  bool operator==(const Rect& other) const {
    if (left_ != other.left_) {
      return false;
    }
    if (top_ != other.top_) {
      return false;
    }
    if (right_ != other.right_) {
      return false;
    }
    if (bottom_ != other.bottom_) {
      return false;
    }
    return true;
  }

  bool operator!=(const Rect& other) const { return !operator==(other); }

  T left_, top_, right_, bottom_;
};
}  // namespace StepMania
typedef StepMania::Rect<int> RectI;
typedef StepMania::Rect<float> RectF;

/* Structure for our custom vertex type.  Note that these data structes
 * have the same layout that D3D expects. */
struct RageSpriteVertex  // has color
{
  RageSpriteVertex() : p_(), n_(), c_(), t_() {}
  RageVector3 p_;  // position
  RageVector3 n_;  // normal
  RageVColor c_;   // diffuse color
  RageVector2 t_;  // texture coordinates
};

void lerp_rage_color(
    RageColor& out, const RageColor& a, const RageColor& b, float t);
void WeightedAvergeOfRSVs(
    RageSpriteVertex& average_out, const RageSpriteVertex& rsv1,
    const RageSpriteVertex& rsv2, float percent_between);

struct RageModelVertex  // doesn't have color.  Relies on material color
{
  /* Zero out by default. */
  RageModelVertex()
      : p_(0, 0, 0),
        n_(0, 0, 0),
        t_(0, 0),
        bone_(0),
        texture_matrix_scale_(1, 1) {}
  RageVector3 p_;  // position
  RageVector3 n_;  // normal
  RageVector2 t_;  // texture coordinates
  int8_t bone_;
  RageVector2 texture_matrix_scale_;  // usually 1,1
};

// RageMatrix elements are specified in row-major order.  This
// means that the translate terms are located in the fourth row and the
// projection terms in the fourth column.  This is consistent with the way
// MAX, Direct3D, and OpenGL all handle matrices.  Even though the OpenGL
// documentation is in column-major form, the OpenGL code is designed to
// handle matrix operations in row-major form.
struct RageMatrix {
 public:
  RageMatrix() = default;
  RageMatrix(const float* f) {
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        m_[j][i] = f[j * 4 + i];
      }
    }
  }
  RageMatrix(
      float v00, float v01, float v02, float v03, float v10, float v11,
      float v12, float v13, float v20, float v21, float v22, float v23,
      float v30, float v31, float v32, float v33);

  // access grants
  float& operator()(int row, int col) { return m_[col][row]; }
  float operator()(int row, int col) const { return m_[col][row]; }

  // casting operators
  operator float*() { return m_[0].data(); }
  operator const float*() const { return m_[0].data(); }

  std::array<std::array<float, 4>, 4> m_{};
};

#endif

/*
 * Copyright (c) 2001-2002 Chris Danford
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
