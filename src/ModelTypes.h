/* ModelTypes - Types defined in msLib.h.  C arrays converted to use std::vector
 */

#ifndef MODEL_TYPES_H
#define MODEL_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include "RageTypes.h"

struct msTriangle {
  uint16_t vertex_indices_[3];
};

struct msMesh {
  std::string name_;
  int8_t material_index_;

  std::vector<RageModelVertex> vertices_;

  // OPTIMIZATION: If all verts in a mesh are transformed by the same bone,
  // then send the transform to the graphics card for the whole mesh instead
  // of transforming each vertex on the CPU;
  int8_t bone_index_;  // -1 = no bone

  std::vector<msTriangle> triangles_;
};

class RageTexture;

class AnimatedTexture {
 public:
  AnimatedTexture();
  ~AnimatedTexture();

  RageVector3 RadianToDegree(RageVector3 radian);

  void LoadBlank();
  void Load(const std::string& sTexOrIniFile);
  void Unload();
  void Update(float fDelta);

  RageTexture* GetCurrentTexture();

  int GetNumStates() const;
  void SetState(int iNewState);
  float GetAnimationLengthSeconds() const;
  void SetSecondsIntoAnimation(float fSeconds);
  float GetSecondsIntoAnimation() const;
  RageVector2 GetTextureTranslate();

  bool sphere_mapped_;
  BlendMode blend_mode_;

  bool NeedsNormals() const { return sphere_mapped_; }

 private:
  RageVector2 tex_offset_;
  RageVector2 tex_velocity_;

  int cur_state_;
  float secs_into_frame_;
  struct AnimatedTextureState {
    AnimatedTextureState(
        RageTexture* pTexture_, float fDelaySecs_, RageVector2 vTranslate_)
        : texture_(pTexture_),
          delay_secs_(fDelaySecs_),
          translate_(vTranslate_) {}

    RageTexture* texture_;
    float delay_secs_;
    RageVector2 translate_;
  };
  std::vector<AnimatedTextureState> frames_;
};

struct msMaterial {
  int flags_;
  std::string name_;
  RageColor ambient_;
  RageColor Diffuse;
  RageColor specular_;
  RageColor emissive_;
  float shininess_;
  float transparency_;

  AnimatedTexture diffuse;
  AnimatedTexture alpha_;

  bool NeedsNormals() const {
    return diffuse.NeedsNormals() || alpha_.NeedsNormals();
  }
};

struct msPositionKey {
  float time_;
  RageVector3 position_;

  msPositionKey() : time_(0.0f) {}
};

struct msRotationKey {
  float time_;
  RageVector4 rotation_;

  msRotationKey() : time_(0.0f) {}
};

struct msBone {
  int flags_;
  std::string name_;
  std::string parent_name_;
  RageVector3 position_;
  RageVector3 rotation_;

  std::vector<msPositionKey> position_keys_;
  std::vector<msRotationKey> rotation_keys_;

  msBone() : flags_(0) {}
};

struct msAnimation {
  int FindBoneByName(const std::string& sName) const {
    for (unsigned i = 0; i < bones_.size(); i++) {
      if (bones_[i].name_ == sName) {
        return i;
      }
    }
    return -1;
  }

  bool LoadMilkshapeAsciiBones(std::string sAniName, std::string sPath);

  std::vector<msBone> bones_;
  int total_frames_ = 0;
};

struct myBone_t {
  RageMatrix relative_;
  RageMatrix absolute_;
  RageMatrix final_;
};

#endif

/*
 * (c) 2003-2004 Chris Danford
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
