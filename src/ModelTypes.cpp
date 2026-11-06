#include "ModelTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>

#include "IniFile.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageMath.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "StdString.h"
#include "XmlFile.h"
#include "global.h"

#define MS_MAX_NAME 32

AnimatedTexture::AnimatedTexture() {
  cur_state_ = 0;
  secs_into_frame_ = 0;
  sphere_mapped_ = false;
  tex_offset_ = RageVector2(0, 0);
  tex_velocity_ = RageVector2(0, 0);
  blend_mode_ = BLEND_NORMAL;
}

AnimatedTexture::~AnimatedTexture() { Unload(); }

RageVector3 RadianToDegree(RageVector3 radian) {
  constexpr float RAD_TO_DEG = 180.0f / PI;
  return radian * RAD_TO_DEG;
}

void AnimatedTexture::LoadBlank() {
  AnimatedTextureState state(nullptr, 1, RageVector2(0, 0));
  frames_.push_back(state);
}

void AnimatedTexture::Load(const std::string& sTexOrIniPath) {
  ASSERT(frames_.empty());  // don't load more than once

  sphere_mapped_ = sTexOrIniPath.find("sphere") != std::string::npos;
  if (sTexOrIniPath.find("add") != std::string::npos) {
    blend_mode_ = BLEND_ADD;
  } else {
    blend_mode_ = BLEND_NORMAL;
  }

  if (CompareNoCase(GetExtension(sTexOrIniPath), "ini") == 0) {
    IniFile ini;
    if (!ini.ReadFile(sTexOrIniPath)) {
      RageException::Throw(
          "Error reading \"%s\": %s", sTexOrIniPath.c_str(),
          ini.GetError().c_str());
    }

    const XNode* pAnimatedTexture = ini.GetChild("AnimatedTexture");
    if (pAnimatedTexture == nullptr) {
      RageException::Throw(
          "The animated texture file \"%s\" doesn't contain a section called "
          "\"AnimatedTexture\".",
          sTexOrIniPath.c_str());
    }

    pAnimatedTexture->GetAttrValue("TexVelocityX", tex_velocity_.x_);
    pAnimatedTexture->GetAttrValue("TexVelocityY", tex_velocity_.y_);
    pAnimatedTexture->GetAttrValue("TexOffsetX", tex_offset_.x_);
    pAnimatedTexture->GetAttrValue("TexOffsetY", tex_offset_.y_);

    for (int i = 0; i < 1000; i++) {
      std::string sFileKey = ssprintf("Frame%04d", i);
      std::string sDelayKey = ssprintf("Delay%04d", i);

      std::string sFileName;
      float fDelay = 0;
      if (pAnimatedTexture->GetAttrValue(sFileKey, sFileName) &&
          pAnimatedTexture->GetAttrValue(sDelayKey, fDelay)) {
        std::string sTranslateXKey = ssprintf("TranslateX%04d", i);
        std::string sTranslateYKey = ssprintf("TranslateY%04d", i);

        RageVector2 vOffset(0, 0);
        pAnimatedTexture->GetAttrValue(sTranslateXKey, vOffset.x_);
        pAnimatedTexture->GetAttrValue(sTranslateYKey, vOffset.y_);

        RageTextureID ID;
        ID.filename_ = Dirname(sTexOrIniPath) + sFileName;
        ID.stretch_ = true;
        ID.hot_pink_color_key_ = true;
        ID.mip_maps_ = true;  // use mipmaps in Models
        AnimatedTextureState state(
            TEXTUREMAN->LoadTexture(ID), fDelay, vOffset);
        frames_.push_back(state);
      } else {
        break;
      }
    }
  } else {
    RageTextureID ID;
    ID.filename_ = sTexOrIniPath;
    ID.hot_pink_color_key_ = true;
    ID.stretch_ = true;
    ID.mip_maps_ = true;  // use mipmaps in Models
    AnimatedTextureState state(
        TEXTUREMAN->LoadTexture(ID), 1, RageVector2(0, 0));
    frames_.push_back(state);
  }
}

void AnimatedTexture::Update(float fDelta) {
  if (frames_.empty()) {
    return;
  }
  ASSERT(cur_state_ < (int)frames_.size());
  secs_into_frame_ += fDelta;
  if (secs_into_frame_ > frames_[cur_state_].delay_secs_) {
    secs_into_frame_ -= frames_[cur_state_].delay_secs_;
    cur_state_ = (cur_state_ + 1) % frames_.size();
  }
}

RageTexture* AnimatedTexture::GetCurrentTexture() {
  if (frames_.empty()) {
    return nullptr;
  }
  ASSERT(cur_state_ < (int)frames_.size());
  return frames_[cur_state_].texture_;
}

int AnimatedTexture::GetNumStates() const { return frames_.size(); }

void AnimatedTexture::SetState(int iState) {
  rage_clamp(iState, 0, GetNumStates() - 1);
  cur_state_ = iState;
}

float AnimatedTexture::GetAnimationLengthSeconds() const {
  return std::accumulate(
      frames_.begin(), frames_.end(), 0.f,
      [](float total, const AnimatedTextureState& state) {
        return total + state.delay_secs_;
      });
}

void AnimatedTexture::SetSecondsIntoAnimation(float fSeconds) {
  fSeconds = std::fmod(fSeconds, GetAnimationLengthSeconds());

  cur_state_ = 0;
  for (unsigned i = 0; i < frames_.size(); i++) {
    AnimatedTextureState& ats = frames_[i];
    if (fSeconds >= ats.delay_secs_) {
      fSeconds -= ats.delay_secs_;
      cur_state_ = i + 1;
    } else {
      break;
    }
  }
  secs_into_frame_ = fSeconds;  // remainder
}

float AnimatedTexture::GetSecondsIntoAnimation() const {
  float fSeconds = 0;

  for (unsigned i = 0; i < frames_.size(); i++) {
    const AnimatedTextureState& ats = frames_[i];
    if (int(i) >= cur_state_) {
      break;
    }

    fSeconds += ats.delay_secs_;
  }
  fSeconds += secs_into_frame_;
  return fSeconds;
}

void AnimatedTexture::Unload() {
  for (unsigned i = 0; i < frames_.size(); ++i) {
    TEXTUREMAN->UnloadTexture(frames_[i].texture_);
  }
  frames_.clear();
  cur_state_ = 0;
  secs_into_frame_ = 0;
}

RageVector2 AnimatedTexture::GetTextureTranslate() {
  float fPercentIntoAnimation =
      GetSecondsIntoAnimation() / GetAnimationLengthSeconds();
  RageVector2 v = tex_velocity_ * fPercentIntoAnimation + tex_offset_;

  if (frames_.empty()) {
    return v;
  }

  ASSERT(cur_state_ < (int)frames_.size());
  v += frames_[cur_state_].translate_;

  return v;
}

#define THROW                                                               \
  RageException::Throw(                                                     \
      "Parse error in \"%s\" at line %d: \"%s\".", sPath.c_str(), iLineNum, \
      sLine.c_str())

bool msAnimation::LoadMilkshapeAsciiBones(
    std::string sAniName, std::string sPath) {
  FixSlashesInPlace(sPath);
  const std::string sDir = Dirname(sPath);

  RageFile f;
  if (!f.Open(sPath)) {
    RageException::Throw(
        "Model:: Could not open \"%s\": %s", sPath.c_str(),
        f.GetError().c_str());
  }

  std::string sLine;
  int iLineNum = 0;

  msAnimation& Animation = *this;

  bool bLoaded = false;
  while (f.GetLine(sLine) > 0) {
    iLineNum++;

    if (!strncmp(sLine.c_str(), "//", 2)) {
      continue;
    }

    // bones
    int nNumBones = 0;
    if (sscanf(sLine.c_str(), "Bones: %d", &nNumBones) != 1) {
      continue;
    }

    char szName[MS_MAX_NAME];

    Animation.bones_.resize(nNumBones);

    for (int i = 0; i < nNumBones; i++) {
      msBone& Bone = Animation.bones_[i];

      // name
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      if (sscanf(sLine.c_str(), "\"%31[^\"]\"", szName) != 1) {
        THROW;
      }
      Bone.name_ = szName;

      // parent
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      strcpy(szName, "");
      sscanf(sLine.c_str(), "\"%31[^\"]\"", szName);

      Bone.parent_name_ = szName;

      // flags, position, rotation
      RageVector3 Position, Rotation;
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }

      int nFlags;
      if (sscanf(
              sLine.c_str(), "%d %f %f %f %f %f %f", &nFlags, &Position[0],
              &Position[1], &Position[2], &Rotation[0], &Rotation[1],
              &Rotation[2]) != 7) {
        THROW;
      }
      Rotation = RadianToDegree(Rotation);

      Bone.flags_ = nFlags;
      memcpy(&Bone.position_, &Position, sizeof(Bone.position_));
      memcpy(&Bone.rotation_, &Rotation, sizeof(Bone.rotation_));

      // position key count
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      int nNumPositionKeys = 0;
      if (sscanf(sLine.c_str(), "%d", &nNumPositionKeys) != 1) {
        THROW;
      }

      Bone.position_keys_.resize(nNumPositionKeys);

      for (int j = 0; j < nNumPositionKeys; ++j) {
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        float fTime;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &fTime, &Position[0],
                &Position[1], &Position[2]) != 4) {
          THROW;
        }

        msPositionKey key = {};
        key.time_ = fTime;
        key.position_ = RageVector3(Position[0], Position[1], Position[2]);
        Bone.position_keys_[j] = key;
      }

      // rotation key count
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      int nNumRotationKeys = 0;
      if (sscanf(sLine.c_str(), "%d", &nNumRotationKeys) != 1) {
        THROW;
      }

      Bone.rotation_keys_.resize(nNumRotationKeys);

      for (int j = 0; j < nNumRotationKeys; ++j) {
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        float fTime;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &fTime, &Rotation[0],
                &Rotation[1], &Rotation[2]) != 4) {
          THROW;
        }
        Rotation = RadianToDegree(Rotation);

        msRotationKey key = {};
        key.time_ = fTime;
        Rotation = RageVector3(Rotation[0], Rotation[1], Rotation[2]);
        RageQuatFromHPR(&key.rotation_, Rotation);
        Bone.rotation_keys_[j] = key;
      }
    }

    // Ignore "Frames:" in file.  Calculate it ourself
    Animation.total_frames_ = 0;
    for (int i = 0; i < (int)Animation.bones_.size(); i++) {
      msBone& Bone = Animation.bones_[i];
      for (unsigned j = 0; j < Bone.position_keys_.size(); ++j) {
        Animation.total_frames_ = std::max(
            Animation.total_frames_, (int)Bone.position_keys_[j].time_);
      }
      for (unsigned j = 0; j < Bone.rotation_keys_.size(); ++j) {
        Animation.total_frames_ = std::max(
            Animation.total_frames_, (int)Bone.rotation_keys_[j].time_);
      }
    }
  }

  return bLoaded;
}

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
