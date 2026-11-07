/*
 * Most of these prototypes match up with the D3DX math functions.  Take a
 * function name, replace "Rage" with "D3DX" and look it up in the D3D SDK
 * docs for details.
 */

#include "RageMath.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <vector>

#include "LuaManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "global.h"

void RageVec3ClearBounds(RageVector3& mins, RageVector3& maxs) {
  mins = RageVector3(FLT_MAX, FLT_MAX, FLT_MAX);
  maxs = mins * -1;
}

void RageVec3AddToBounds(
    const RageVector3& p, RageVector3& mins, RageVector3& maxs) {
  mins.x_ = std::min(mins.x_, p.x_);
  mins.y_ = std::min(mins.y_, p.y_);
  mins.z_ = std::min(mins.z_, p.z_);
  maxs.x_ = std::max(maxs.x_, p.x_);
  maxs.y_ = std::max(maxs.y_, p.y_);
  maxs.z_ = std::max(maxs.z_, p.z_);
}

void RageVec3Normalize(RageVector3* out, const RageVector3* v) {
  float scale = 1.0f / std::sqrt(v->x_ * v->x_ + v->y_ * v->y_ + v->z_ * v->z_);
  out->x_ = v->x_ * scale;
  out->y_ = v->y_ * scale;
  out->z_ = v->z_ * scale;
}

void RageVec3Cross(
    RageVector3* ret, const RageVector3* a, const RageVector3* b) {
  ret->x_ = (a->y_ * b->z_) - (a->z_ * b->y_);
  ret->y_ = ((a->x_ * b->z_) - (a->z_ * b->x_));
  ret->z_ = (a->x_ * b->y_) - (a->y_ * b->x_);
}

void RageVec3TransformCoord(
    RageVector3* out, const RageVector3* v, const RageMatrix* m) {
  RageVector4 temp(v->x_, v->y_, v->z_, 1.0f);  // translate
  RageVec4TransformCoord(&temp, &temp, m);
  *out = RageVector3(temp.x_ / temp.w_, temp.y_ / temp.w_, temp.z_ / temp.w_);
}

void RageVec3TransformNormal(
    RageVector3* out, const RageVector3* v, const RageMatrix* m) {
  RageVector4 temp(v->x_, v->y_, v->z_, 0.0f);  // don't translate
  RageVec4TransformCoord(&temp, &temp, m);
  *out = RageVector3(temp.x_, temp.y_, temp.z_);
}

#define m00 m_[0][0]
#define m01 m_[0][1]
#define m02 m_[0][2]
#define m03 m_[0][3]
#define m10 m_[1][0]
#define m11 m_[1][1]
#define m12 m_[1][2]
#define m13 m_[1][3]
#define m20 m_[2][0]
#define m21 m_[2][1]
#define m22 m_[2][2]
#define m23 m_[2][3]
#define m30 m_[3][0]
#define m31 m_[3][1]
#define m32 m_[3][2]
#define m33 m_[3][3]

void RageVec4TransformCoord(
    RageVector4* out, const RageVector4* p_v, const RageMatrix* m) {
  const RageMatrix& a = *m;
  const RageVector4& v = *p_v;
  *out = RageVector4(
      a.m00 * v.x_ + a.m10 * v.y_ + a.m20 * v.z_ + a.m30 * v.w_,
      a.m01 * v.x_ + a.m11 * v.y_ + a.m21 * v.z_ + a.m31 * v.w_,
      a.m02 * v.x_ + a.m12 * v.y_ + a.m22 * v.z_ + a.m32 * v.w_,
      a.m03 * v.x_ + a.m13 * v.y_ + a.m23 * v.z_ + a.m33 * v.w_);
}

RageMatrix::RageMatrix(
    float v00, float v01, float v02, float v03, float v10, float v11, float v12,
    float v13, float v20, float v21, float v22, float v23, float v30, float v31,
    float v32, float v33) {
  m00 = v00;
  m01 = v01;
  m02 = v02;
  m03 = v03;
  m10 = v10;
  m11 = v11;
  m12 = v12;
  m13 = v13;
  m20 = v20;
  m21 = v21;
  m22 = v22;
  m23 = v23;
  m30 = v30;
  m31 = v31;
  m32 = v32;
  m33 = v33;
}

void RageMatrixIdentity(RageMatrix* out) {
  static float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  memcpy(&out->m00, identity, sizeof(identity));
  /*	*pOut = RageMatrix(
                  1,0,0,0,
                  0,1,0,0,
                  0,0,1,0,
                  0,0,0,1 );
  */
}

void RageMatrixMultiply(
    RageMatrix* out, const RageMatrix* p_a, const RageMatrix* p_b) {
  // #if defined(_WIN32)
  //	// <30 cycles for theirs versus >100 for ours.
  //	D3DXMatrixMultiply( (D3DMATRIX*)pOut, (D3DMATRIX*)pA, (D3DMATRIX*)pB );
  // #else
  const RageMatrix& a = *p_a;
  const RageMatrix& b = *p_b;

  *out = RageMatrix(
      b.m00 * a.m00 + b.m01 * a.m10 + b.m02 * a.m20 + b.m03 * a.m30,
      b.m00 * a.m01 + b.m01 * a.m11 + b.m02 * a.m21 + b.m03 * a.m31,
      b.m00 * a.m02 + b.m01 * a.m12 + b.m02 * a.m22 + b.m03 * a.m32,
      b.m00 * a.m03 + b.m01 * a.m13 + b.m02 * a.m23 + b.m03 * a.m33,
      b.m10 * a.m00 + b.m11 * a.m10 + b.m12 * a.m20 + b.m13 * a.m30,
      b.m10 * a.m01 + b.m11 * a.m11 + b.m12 * a.m21 + b.m13 * a.m31,
      b.m10 * a.m02 + b.m11 * a.m12 + b.m12 * a.m22 + b.m13 * a.m32,
      b.m10 * a.m03 + b.m11 * a.m13 + b.m12 * a.m23 + b.m13 * a.m33,
      b.m20 * a.m00 + b.m21 * a.m10 + b.m22 * a.m20 + b.m23 * a.m30,
      b.m20 * a.m01 + b.m21 * a.m11 + b.m22 * a.m21 + b.m23 * a.m31,
      b.m20 * a.m02 + b.m21 * a.m12 + b.m22 * a.m22 + b.m23 * a.m32,
      b.m20 * a.m03 + b.m21 * a.m13 + b.m22 * a.m23 + b.m23 * a.m33,
      b.m30 * a.m00 + b.m31 * a.m10 + b.m32 * a.m20 + b.m33 * a.m30,
      b.m30 * a.m01 + b.m31 * a.m11 + b.m32 * a.m21 + b.m33 * a.m31,
      b.m30 * a.m02 + b.m31 * a.m12 + b.m32 * a.m22 + b.m33 * a.m32,
      b.m30 * a.m03 + b.m31 * a.m13 + b.m32 * a.m23 + b.m33 * a.m33);
  // phew!
  // #endif
}

void RageMatrixTranslation(RageMatrix* out, float x, float y, float z) {
  RageMatrixIdentity(out);
  out->m_[3][0] = x;
  out->m_[3][1] = y;
  out->m_[3][2] = z;
}

void RageMatrixScaling(RageMatrix* out, float x, float y, float z) {
  RageMatrixIdentity(out);
  out->m_[0][0] = x;
  out->m_[1][1] = y;
  out->m_[2][2] = z;
}

void RageMatrixSkewX(RageMatrix* out, float amount) {
  RageMatrixIdentity(out);
  out->m_[1][0] = amount;
}

void RageMatrixSkewY(RageMatrix* out, float amount) {
  RageMatrixIdentity(out);
  out->m_[0][1] = amount;
}

/*
 * Return:
 *
 * RageMatrix translate;
 * RageMatrixTranslation( &translate, fTransX, fTransY, fTransZ );
 * RageMatrix scale;
 * RageMatrixScaling( &scale, fScaleX, float fScaleY, float fScaleZ );
 * RageMatrixMultiply( pOut, &translate, &scale );
 */
void RageMatrixTranslate(
    RageMatrix* out, float trans_x, float trans_y, float trans_z) {
  out->m00 = 1;
  out->m01 = 0;
  out->m02 = 0;
  out->m03 = 0;

  out->m10 = 0;
  out->m11 = 1;
  out->m12 = 0;
  out->m13 = 0;

  out->m20 = 0;
  out->m21 = 0;
  out->m22 = 1;
  out->m23 = 0;

  out->m30 = trans_x;
  out->m31 = trans_y;
  out->m32 = trans_z;
  out->m33 = 1;
}

void RageMatrixScale(
    RageMatrix* out, float scale_x, float scale_y, float scale_z) {
  out->m00 = scale_x;
  out->m01 = 0;
  out->m02 = 0;
  out->m03 = 0;

  out->m10 = 0;
  out->m11 = scale_y;
  out->m12 = 0;
  out->m13 = 0;

  out->m20 = 0;
  out->m21 = 0;
  out->m22 = scale_z;
  out->m23 = 0;

  out->m30 = 0;
  out->m31 = 0;
  out->m32 = 0;
  out->m33 = 1;
}

void RageMatrixRotationY(RageMatrix* out, float theta) {
  theta *= PI / 180;

  RageMatrixIdentity(out);
  out->m_[0][0] = std::cos(theta);
  out->m_[2][2] = out->m_[0][0];

  out->m_[0][2] = std::sin(theta);
  out->m_[2][0] = -out->m_[0][2];
}

/* Return RageMatrixRotationX(rX) * RageMatrixRotationY(rY) *
 * RageMatrixRotationZ(rZ) quickly (without actually doing two complete matrix
 * multiplies), by removing the parts of the matrix multiplies that we know will
 * be 0. */
void RageMatrixRotationXYZ(RageMatrix* out, float r_x, float r_y, float r_z) {
  r_x *= PI / 180;
  r_y *= PI / 180;
  r_z *= PI / 180;

  const float c_x = std::cos(r_x);
  const float s_x = std::sin(r_x);
  const float c_y = std::cos(r_y);
  const float s_y = std::sin(r_y);
  const float c_z = std::cos(r_z);
  const float s_z = std::sin(r_z);

  /*
   * X*Y:
   * RageMatrix(
   *	cY,  sY*sX, sY*cX, 0,
   *	0,   cX,    -sX,   0,
   *	-sY, cY*sX, cY*cX, 0,
   *	0, 0, 0, 1
   * );
   *
   * X*Y*Z:
   *
   * RageMatrix(
   *	cZ*cY, cZ*sY*sX+sZ*cX, cZ*sY*cX+sZ*(-sX), 0,
   *	(-sZ)*cY, (-sZ)*sY*sX+cZ*cX, (-sZ)*sY*cX+cZ*(-sX), 0,
   *	-sY, cY*sX, cY*cX, 0,
   *	0, 0, 0, 1
   * );
   */

  out->m00 = c_z * c_y;
  out->m01 = c_z * s_y * s_x + s_z * c_x;
  out->m02 = c_z * s_y * c_x + s_z * (-s_x);
  out->m03 = 0;
  out->m10 = (-s_z) * c_y;
  out->m11 = (-s_z) * s_y * s_x + c_z * c_x;
  out->m12 = (-s_z) * s_y * c_x + c_z * (-s_x);
  out->m13 = 0;
  out->m20 = -s_y;
  out->m21 = c_y * s_x;
  out->m22 = c_y * c_x;
  out->m23 = 0;
  out->m30 = 0;
  out->m31 = 0;
  out->m32 = 0;
  out->m33 = 1;
}

void RageAARotate(RageVector3* inret, const RageVector3* axis, float angle) {
  float ha = angle / 2.0f;
  float ca2 = std::cos(ha);
  float sa2 = std::sin(ha);
  RageVector4 quat(axis->x_ * sa2, axis->y_ * sa2, axis->z_ * sa2, ca2);
  RageVector4 quatc(-quat.x_, -quat.y_, -quat.z_, ca2);
  RageVector4 point(inret->x_, inret->y_, inret->z_, 0.0f);
  RageQuatMultiply(&point, quat, point);
  RageQuatMultiply(&point, point, quatc);
  inret->x_ = point.x_;
  inret->y_ = point.y_;
  inret->z_ = point.z_;
}

void RageQuatMultiply(
    RageVector4* p_out, const RageVector4& a, const RageVector4& b) {
  RageVector4 out;
  out.x_ = a.w_ * b.x_ + a.x_ * b.w_ + a.y_ * b.z_ - a.z_ * b.y_;
  out.y_ = a.w_ * b.y_ + a.y_ * b.w_ + a.z_ * b.x_ - a.x_ * b.z_;
  out.z_ = a.w_ * b.z_ + a.z_ * b.w_ + a.x_ * b.y_ - a.y_ * b.x_;
  out.w_ = a.w_ * b.w_ - a.x_ * b.x_ - a.y_ * b.y_ - a.z_ * b.z_;

  float dist, square;

  square =
      out.x_ * out.x_ + out.y_ * out.y_ + out.z_ * out.z_ + out.w_ * out.w_;

  if (square > 0.0) {
    dist = 1.0f / std::sqrt(square);
  } else {
    dist = 1;
  }

  out.x_ *= dist;
  out.y_ *= dist;
  out.z_ *= dist;
  out.w_ *= dist;

  *p_out = out;
}

RageVector4 RageQuatFromH(float theta) {
  theta *= PI / 180.0f;
  theta /= 2.0f;
  theta *= -1;
  const float c = std::cos(theta);
  const float s = std::sin(theta);

  return RageVector4(0, s, 0, c);
}

RageVector4 RageQuatFromP(float theta) {
  theta *= PI / 180.0f;
  theta /= 2.0f;
  theta *= -1;
  const float c = std::cos(theta);
  const float s = std::sin(theta);

  return RageVector4(s, 0, 0, c);
}

RageVector4 RageQuatFromR(float theta) {
  theta *= PI / 180.0f;
  theta /= 2.0f;
  theta *= -1;
  const float c = std::cos(theta);
  const float s = std::sin(theta);

  return RageVector4(0, 0, s, c);
}

/* Math from http://www.gamasutra.com/features/19980703/quaternions_01.htm . */

/* prh.xyz -> heading, pitch, roll */
void RageQuatFromHPR(RageVector4* out, RageVector3 hpr) {
  hpr *= PI;
  hpr /= 180.0f;
  hpr /= 2.0f;

  const float s_x = std::sin(hpr.x_);
  const float c_x = std::cos(hpr.x_);
  const float s_y = std::sin(hpr.y_);
  const float c_y = std::cos(hpr.y_);
  const float s_z = std::sin(hpr.z_);
  const float c_z = std::cos(hpr.z_);

  out->w_ = c_x * c_y * c_z + s_x * s_y * s_z;
  out->x_ = s_x * c_y * c_z - c_x * s_y * s_z;
  out->y_ = c_x * s_y * c_z + s_x * c_y * s_z;
  out->z_ = c_x * c_y * s_z - s_x * s_y * c_z;
}

/*
 * Screen orientatoin:  the "floor" is the XZ plane, and Y is height; in other
 * words, the screen is the XY plane and negative Z goes into it.
 */

void RageMatrixFromQuat(RageMatrix* out, const RageVector4 q) {
  float xx = q.x_ * (q.x_ + q.x_);
  float xy = q.x_ * (q.y_ + q.y_);
  float xz = q.x_ * (q.z_ + q.z_);

  float wx = q.w_ * (q.x_ + q.x_);
  float wy = q.w_ * (q.y_ + q.y_);
  float wz = q.w_ * (q.z_ + q.z_);

  float yy = q.y_ * (q.y_ + q.y_);
  float yz = q.y_ * (q.z_ + q.z_);

  float zz = q.z_ * (q.z_ + q.z_);
  // careful.  The param order is row-major, which is the
  // transpose of the order shown in the OpenGL docs.
  *out = RageMatrix(
      1 - (yy + zz), xy + wz, xz - wy, 0, xy - wz, 1 - (xx + zz), yz + wx, 0,
      xz + wy, yz - wx, 1 - (xx + yy), 0, 0, 0, 0, 1);
}

void RageQuatSlerp(
    RageVector4* out, const RageVector4& from, const RageVector4& to, float t) {
  float to1[4];

  // calc cosine
  float cosom =
      from.x_ * to.x_ + from.y_ * to.y_ + from.z_ * to.z_ + from.w_ * to.w_;

  // adjust signs (if necessary)
  if (cosom < 0) {
    cosom = -cosom;
    to1[0] = -to.x_;
    to1[1] = -to.y_;
    to1[2] = -to.z_;
    to1[3] = -to.w_;
  } else {
    to1[0] = to.x_;
    to1[1] = to.y_;
    to1[2] = to.z_;
    to1[3] = to.w_;
  }

  // calculate coefficients
  float scale0, scale1;
  if (cosom < 0.9999f) {
    // standard case (slerp)
    float omega = std::acos(cosom);
    float sinom = std::sin(omega);
    scale0 = std::sin((1.0f - t) * omega) / sinom;
    scale1 = std::sin(t * omega) / sinom;
  } else {
    // "from" and "to" quaternions are very close
    //  ... so we can do a linear interpolation
    scale0 = 1.0f - t;
    scale1 = t;
  }
  // calculate final values
  out->x_ = scale0 * from.x_ + scale1 * to1[0];
  out->y_ = scale0 * from.y_ + scale1 * to1[1];
  out->z_ = scale0 * from.z_ + scale1 * to1[2];
  out->w_ = scale0 * from.w_ + scale1 * to1[3];
}

RageMatrix RageLookAt(
    float eyex, float eyey, float eyez, float centerx, float centery,
    float centerz, float upx, float upy, float upz) {
  RageVector3 z(eyex - centerx, eyey - centery, eyez - centerz);
  RageVec3Normalize(&z, &z);

  RageVector3 y(upx, upy, upz);

  RageVector3 x(
      y[1] * z[2] - y[2] * z[1], -y[0] * z[2] + y[2] * z[0],
      y[0] * z[1] - y[1] * z[0]);

  y = RageVector3(
      z[1] * x[2] - z[2] * x[1], -z[0] * x[2] + z[2] * x[0],
      z[0] * x[1] - z[1] * x[0]);

  RageVec3Normalize(&x, &x);
  RageVec3Normalize(&y, &y);

  RageMatrix mat(
      x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, 0, 0, 0,
      1);

  RageMatrix mat2;
  RageMatrixTranslation(&mat2, -eyex, -eyey, -eyez);

  RageMatrix ret;
  RageMatrixMultiply(&ret, &mat, &mat2);

  return ret;
}

void RageMatrixAngles(RageMatrix* out, const RageVector3& angles) {
  const RageVector3 angles_radians(angles * 2 * PI / 360);

  const float sy = std::sin(angles_radians[2]);
  const float cy = std::cos(angles_radians[2]);
  const float sp = std::sin(angles_radians[1]);
  const float cp = std::cos(angles_radians[1]);
  const float sr = std::sin(angles_radians[0]);
  const float cr = std::cos(angles_radians[0]);

  RageMatrixIdentity(out);

  // matrix = (Z * Y) * X
  out->m_[0][0] = cp * cy;
  out->m_[0][1] = cp * sy;
  out->m_[0][2] = -sp;
  out->m_[1][0] = sr * sp * cy + cr * -sy;
  out->m_[1][1] = sr * sp * sy + cr * cy;
  out->m_[1][2] = sr * cp;
  out->m_[2][0] = (cr * sp * cy + -sr * -sy);
  out->m_[2][1] = (cr * sp * sy + -sr * cy);
  out->m_[2][2] = cr * cp;
}

void RageMatrixTranspose(RageMatrix* out, const RageMatrix* in) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      out->m_[j][i] = in->m_[i][j];
    }
  }
}

float RageSquare(float angle) {
  float f_angle = std::fmod(angle, (PI * 2));
  // Hack: This ensures the hold notes don't flicker right before they're hit.
  if (f_angle < 0.01f) {
    f_angle += PI * 2;
  }
  return f_angle >= PI ? -1.0 : 1.0;
}

float RageTriangle(float angle) {
  float f_angle = std::fmod(angle, PI * 2.0f);
  if (f_angle < 0.0) {
    f_angle += PI * 2.0;
  }
  double result = f_angle * (1 / PI);
  if (result < .5) {
    return result * 2.0;
  } else if (result < 1.5) {
    return 1.0 - ((result - .5) * 2.0);
  } else {
    return -4.0 + (result * 2.0);
  }
}

float RageQuadratic::Evaluate(float t) const {
  // optimized (m_fA * fT*fT*fT) + (m_fB * fT*fT) + (m_fC * fT) + m_fD;
  return ((a_ * t + b_) * t + c_) * t + d_;
}

void RageQuadratic::SetFromBezier(float x1, float x2, float x3, float x4) {
  d_ = x1;
  c_ = 3.0f * (x2 - x1);
  b_ = 3.0f * (x3 - x2) - c_;
  a_ = x4 - x1 - c_ - b_;
}

void RageQuadratic::GetBezier(
    float& x1, float& x2, float& x3, float& x4) const {
  x1 = d_;
  x2 = d_ + c_ / 3.0f;
  x3 = d_ + 2 * c_ / 3.0f + b_ / 3.0f;
  x4 = d_ + c_ + b_ + a_;
}

/* Cubic polynomial interpolation.  SetFromCubicPoly(-1, 0, 1, 2); Evaluate(p)
 * will interpolate between 0 and 1. */
void RageQuadratic::SetFromCubic(float x1, float x2, float x3, float x4) {
  a_ = -1.0f / 6.0f * x1 + +3.0f / 6.0f * x2 + -3.0f / 6.0f * x3 +
       1.0f / 6.0f * x4;
  b_ = 3.0f / 6.0f * x1 + -6.0f / 6.0f * x2 + 3.0f / 6.0f * x3;
  c_ = -2.0f / 6.0f * x1 + -3.0f / 6.0f * x2 + x3 + -1.0f / 6.0f * x4;
  d_ = x2;
}

float RageQuadratic::GetSlope(float t) const {
  return 3 * a_ * t * t + 2 * b_ * t + c_;
}

void RageBezier2D::Evaluate(float t, float* x, float* y) const {
  *x = x_.Evaluate(t);
  *y = y_.Evaluate(t);
}

float RageBezier2D::EvaluateYFromX(float x) const {
  /* Quickly approximate T using Newton-Raphelson successive optimization (see
   * http://www.tinaja.com/text/bezmath.html).  This usually finds T within an
   * acceptable error margin in a few steps. */
  float t = SCALE(x, x_.GetBezierStart(), x_.GetBezierEnd(), 0, 1);
  // Don't try more than 100 times, the curve might be a bit nonsensical. -Kyz
  for (int i = 0; i < 100; ++i) {
    float guessed_x = x_.Evaluate(t);
    float error = x - guessed_x;

    /* If our guess is good enough, evaluate the result Y and return. */
    if (unlikely(std::abs(error) < 0.0001f)) {
      return y_.Evaluate(t);
    }

    float slope = x_.GetSlope(t);
    t += error / slope;
  }
  return y_.Evaluate(t);
}

void RageBezier2D::SetFromBezier(
    float c1_x, float c1_y, float c2_x, float c2_y, float c3_x, float c3_y,
    float c4_x, float c4_y) {
  x_.SetFromBezier(c1_x, c2_x, c3_x, c4_x);
  y_.SetFromBezier(c1_y, c2_y, c3_y, c4_y);
}

#include "LuaBinding.h"

struct LunaRageQuadratic : Luna<RageQuadratic> {
  static int evaluate(T* p, lua_State* L) {
    lua_pushnumber(L, p->Evaluate(FArg(1)));
    return 1;
  }
  static int get_bezier(T* p, lua_State* L) {
    float a, b, c, d;
    p->GetBezier(a, b, c, d);
    lua_pushnumber(L, a);
    lua_pushnumber(L, b);
    lua_pushnumber(L, c);
    lua_pushnumber(L, d);
    return 4;
  }
  static int get_bezier_end(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBezierEnd());
    return 1;
  }
  static int get_bezier_start(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBezierStart());
    return 1;
  }
  static int get_slope(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSlope(FArg(1)));
    return 1;
  }
  static int set_from_bezier(T* p, lua_State* L) {
    p->SetFromBezier(FArg(1), FArg(2), FArg(3), FArg(4));
    COMMON_RETURN_SELF;
  }
  static int set_from_cubic(T* p, lua_State* L) {
    p->SetFromCubic(FArg(1), FArg(2), FArg(3), FArg(4));
    COMMON_RETURN_SELF;
  }
  LunaRageQuadratic() {
    ADD_METHOD(evaluate);
    ADD_METHOD(get_bezier);
    ADD_METHOD(get_bezier_end);
    ADD_METHOD(get_bezier_start);
    ADD_METHOD(get_slope);
    ADD_METHOD(set_from_bezier);
    ADD_METHOD(set_from_cubic);
  }
};
LUA_REGISTER_CLASS(RageQuadratic);

struct LunaRageBezier2D : Luna<RageBezier2D> {
  static int evaluate(T* p, lua_State* L) {
    float x, y;
    p->Evaluate(FArg(1), &x, &y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
  }
  static int evaluate_y_from_x(T* p, lua_State* L) {
    lua_pushnumber(L, p->EvaluateYFromX(FArg(1)));
    return 1;
  }
  static int get_x(T* p, lua_State* L) {
    p->get_x().PushSelf(L);
    return 1;
  }
  static int get_y(T* p, lua_State* L) {
    p->get_y().PushSelf(L);
    return 1;
  }
  static int set_from_bezier(T* p, lua_State* L) {
    p->SetFromBezier(
        FArg(1), FArg(2), FArg(3), FArg(4), FArg(5), FArg(6), FArg(7), FArg(8));
    COMMON_RETURN_SELF;
  }
  static int destroy(T* p, lua_State* L) {
    RageUtil::SafeDelete(p);
    return 0;
  }
  LunaRageBezier2D() {
    ADD_METHOD(destroy);
    ADD_METHOD(evaluate);
    ADD_METHOD(evaluate_y_from_x);
    ADD_METHOD(get_x);
    ADD_METHOD(get_y);
    ADD_METHOD(set_from_bezier);
  }
};
LUA_REGISTER_CLASS(RageBezier2D);

int LuaFunc_create_bezier(lua_State* L);
int LuaFunc_create_bezier(lua_State* L) {
  RageBezier2D* bezier = new RageBezier2D;
  bezier->PushSelf(L);
  return 1;
}
LUAFUNC_REGISTER_COMMON(create_bezier);

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
