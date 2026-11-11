/* RageMath - vector/matrix math utilities. */

#ifndef RAGE_MATH_H
#define RAGE_MATH_H

#include <vector>

#include "Compiler.h"
#include "Platform.h"

constexpr float kPi = 3.1415926536f;

struct lua_State;
struct RageVector2;
struct RageVector3;
struct RageVector4;
struct RageMatrix;

void RageVec3ClearBounds(RageVector3& mins, RageVector3& maxs);
void RageVec3AddToBounds(
    const RageVector3& p, RageVector3& mins, RageVector3& maxs);

void RageVec3Normalize(RageVector3* out, const RageVector3* v);
void RageVec3Cross(
    RageVector3* ret, const RageVector3* a, const RageVector3* b);
void RageVec3TransformCoord(
    RageVector3* out, const RageVector3* v, const RageMatrix* m);
void RageVec3TransformNormal(
    RageVector3* out, const RageVector3* v, const RageMatrix* m);
void RageVec4TransformCoord(
    RageVector4* out, const RageVector4* v, const RageMatrix* m);
void RageMatrixIdentity(RageMatrix* out);
// pOut = pB * pA
void RageMatrixMultiply(
    RageMatrix* out, const RageMatrix* a, const RageMatrix* b);
void RageMatrixTranslation(RageMatrix* out, float x, float y, float z);
void RageMatrixScaling(RageMatrix* out, float x, float y, float z);
void RageMatrixSkewX(RageMatrix* out, float amount);
void RageMatrixSkewY(RageMatrix* out, float amount);
void RageMatrixTranslate(
    RageMatrix* out, float trans_x, float trans_y, float trans_z);
void RageMatrixScale(
    RageMatrix* out, float scale_x, float scale_y, float scale_z);
void RageMatrixRotationY(RageMatrix* out, float theta);
void RageMatrixRotationXYZ(RageMatrix* out, float r_x, float r_y, float r_z);
void RageAARotate(RageVector3* inret, const RageVector3* axis, float angle);
void RageQuatFromHPR(RageVector4* out, RageVector3 hpr);
void RageMatrixFromQuat(RageMatrix* out, const RageVector4 q);
void RageQuatSlerp(
    RageVector4* out, const RageVector4& from, const RageVector4& to, float t);
RageVector4 RageQuatFromH(float theta);
RageVector4 RageQuatFromP(float theta);
RageVector4 RageQuatFromR(float theta);
void RageQuatMultiply(
    RageVector4* out, const RageVector4& a, const RageVector4& b);
RageMatrix RageLookAt(
    float eyex, float eyey, float eyez, float centerx, float centery,
    float centerz, float upx, float upy, float upz);
void RageMatrixAngles(RageMatrix* out, const RageVector3& angles);
void RageMatrixTranspose(RageMatrix* out, const RageMatrix* in);

float RageSquare(float x) CONST_FUNCTION;
float RageTriangle(float x) CONST_FUNCTION;

class RageQuadratic {
 public:
  void SetFromBezier(float c1, float c2, float c3, float c4);
  void GetBezier(float& c1, float& c2, float& c3, float& c4) const;

  void SetFromCubic(float x1, float x2, float x3, float x4);

  float Evaluate(float t) const;
  float GetSlope(float t) const;

  /* Equivalent to Evaluate(0.0f) and Evaluate(1.0f), but faster: */
  float GetBezierStart() const { return d_; }
  float GetBezierEnd() const { return a_ + b_ + c_ + d_; }

  void PushSelf(lua_State* L);

 private:
  float a_, b_, c_, d_;
};

class RageBezier2D {
 public:
  void SetFromBezier(
      float c1_x, float c2_x, float c3_x, float c4_x, float c1_y, float c2_y,
      float c3_y, float c4_y);

  void Evaluate(float t, float* x, float* y) const;
  float EvaluateYFromX(float x) const;

  RageQuadratic& get_x() { return x_; }
  RageQuadratic& get_y() { return y_; }
  void PushSelf(lua_State* L);

 private:
  RageQuadratic x_;
  RageQuadratic y_;
};

#endif

/*
 * Copyright (c) 2001-2006 Chris Danford, Glenn Maynard
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
