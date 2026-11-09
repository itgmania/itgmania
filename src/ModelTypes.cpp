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
  constexpr float kRadToDeg = 180.0f / kPi;
  return radian * kRadToDeg;
}

void AnimatedTexture::LoadBlank() {
  AnimatedTextureState state(nullptr, 1, RageVector2(0, 0));
  frames_.push_back(state);
}

void AnimatedTexture::Load(const std::string& tex_or_ini_path) {
  ASSERT(frames_.empty());  // don't load more than once

  sphere_mapped_ = tex_or_ini_path.find("sphere") != std::string::npos;
  if (tex_or_ini_path.find("add") != std::string::npos) {
    blend_mode_ = BLEND_ADD;
  } else {
    blend_mode_ = BLEND_NORMAL;
  }

  if (CompareNoCase(GetExtension(tex_or_ini_path), "ini") == 0) {
    IniFile ini;
    if (!ini.ReadFile(tex_or_ini_path)) {
      RageException::Throw(
          "Error reading \"%s\": %s", tex_or_ini_path.c_str(),
          ini.GetError().c_str());
    }

    const XNode* animated_texture = ini.GetChild("AnimatedTexture");
    if (animated_texture == nullptr) {
      RageException::Throw(
          "The animated texture file \"%s\" doesn't contain a section called "
          "\"AnimatedTexture\".",
          tex_or_ini_path.c_str());
    }

    animated_texture->GetAttrValue("TexVelocityX", tex_velocity_.x_);
    animated_texture->GetAttrValue("TexVelocityY", tex_velocity_.y_);
    animated_texture->GetAttrValue("TexOffsetX", tex_offset_.x_);
    animated_texture->GetAttrValue("TexOffsetY", tex_offset_.y_);

    for (int i = 0; i < 1000; i++) {
      std::string file_key = ssprintf("Frame%04d", i);
      std::string delay_key = ssprintf("Delay%04d", i);

      std::string file_name;
      float delay = 0;
      if (animated_texture->GetAttrValue(file_key, file_name) &&
          animated_texture->GetAttrValue(delay_key, delay)) {
        std::string translate_x_key = ssprintf("TranslateX%04d", i);
        std::string translate_y_key = ssprintf("TranslateY%04d", i);

        RageVector2 offset(0, 0);
        animated_texture->GetAttrValue(translate_x_key, offset.x_);
        animated_texture->GetAttrValue(translate_y_key, offset.y_);

        RageTextureID id;
        id.filename_ = Dirname(tex_or_ini_path) + file_name;
        id.stretch_ = true;
        id.hot_pink_color_key_ = true;
        id.mip_maps_ = true;  // use mipmaps in Models
        AnimatedTextureState state(TEXTUREMAN->LoadTexture(id), delay, offset);
        frames_.push_back(state);
      } else {
        break;
      }
    }
  } else {
    RageTextureID id;
    id.filename_ = tex_or_ini_path;
    id.hot_pink_color_key_ = true;
    id.stretch_ = true;
    id.mip_maps_ = true;  // use mipmaps in Models
    AnimatedTextureState state(
        TEXTUREMAN->LoadTexture(id), 1, RageVector2(0, 0));
    frames_.push_back(state);
  }
}

void AnimatedTexture::Update(float delta) {
  if (frames_.empty()) {
    return;
  }
  ASSERT(cur_state_ < (int)frames_.size());
  secs_into_frame_ += delta;
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

void AnimatedTexture::SetState(int state) {
  rage_clamp(state, 0, GetNumStates() - 1);
  cur_state_ = state;
}

float AnimatedTexture::GetAnimationLengthSeconds() const {
  return std::accumulate(
      frames_.begin(), frames_.end(), 0.f,
      [](float total, const AnimatedTextureState& state) {
        return total + state.delay_secs_;
      });
}

void AnimatedTexture::SetSecondsIntoAnimation(float seconds) {
  seconds = std::fmod(seconds, GetAnimationLengthSeconds());

  cur_state_ = 0;
  for (unsigned i = 0; i < frames_.size(); i++) {
    AnimatedTextureState& ats = frames_[i];
    if (seconds >= ats.delay_secs_) {
      seconds -= ats.delay_secs_;
      cur_state_ = i + 1;
    } else {
      break;
    }
  }
  secs_into_frame_ = seconds;  // remainder
}

float AnimatedTexture::GetSecondsIntoAnimation() const {
  float seconds = 0;

  for (unsigned i = 0; i < frames_.size(); i++) {
    const AnimatedTextureState& ats = frames_[i];
    if (int(i) >= cur_state_) {
      break;
    }

    seconds += ats.delay_secs_;
  }
  seconds += secs_into_frame_;
  return seconds;
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
  float percent_into_animation =
      GetSecondsIntoAnimation() / GetAnimationLengthSeconds();
  RageVector2 v = tex_velocity_ * percent_into_animation + tex_offset_;

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
    std::string ani_name, std::string sPath) {
  FixSlashesInPlace(sPath);
  const std::string dir = Dirname(sPath);

  RageFile f;
  if (!f.Open(sPath)) {
    RageException::Throw(
        "Model:: Could not open \"%s\": %s", sPath.c_str(),
        f.GetError().c_str());
  }

  std::string sLine;
  int iLineNum = 0;

  msAnimation& animation = *this;

  bool loaded = false;
  while (f.GetLine(sLine) > 0) {
    iLineNum++;

    if (!strncmp(sLine.c_str(), "//", 2)) {
      continue;
    }

    // bones
    int num_bones = 0;
    if (sscanf(sLine.c_str(), "Bones: %d", &num_bones) != 1) {
      continue;
    }

    char name[MS_MAX_NAME];

    animation.bones_.resize(num_bones);

    for (int i = 0; i < num_bones; i++) {
      msBone& bone = animation.bones_[i];

      // name
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      if (sscanf(sLine.c_str(), "\"%31[^\"]\"", name) != 1) {
        THROW;
      }
      bone.name_ = name;

      // parent
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      strcpy(name, "");
      sscanf(sLine.c_str(), "\"%31[^\"]\"", name);

      bone.parent_name_ = name;

      // flags, position, rotation
      RageVector3 position, rotation;
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }

      int flags;
      if (sscanf(
              sLine.c_str(), "%d %f %f %f %f %f %f", &flags, &position[0],
              &position[1], &position[2], &rotation[0], &rotation[1],
              &rotation[2]) != 7) {
        THROW;
      }
      rotation = RadianToDegree(rotation);

      bone.flags_ = flags;
      memcpy(&bone.position_, &position, sizeof(bone.position_));
      memcpy(&bone.rotation_, &rotation, sizeof(bone.rotation_));

      // position key count
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      int num_position_keys = 0;
      if (sscanf(sLine.c_str(), "%d", &num_position_keys) != 1) {
        THROW;
      }

      bone.position_keys_.resize(num_position_keys);

      for (int j = 0; j < num_position_keys; ++j) {
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        float time;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &time, &position[0], &position[1],
                &position[2]) != 4) {
          THROW;
        }

        msPositionKey key = {};
        key.time_ = time;
        key.position_ = RageVector3(position[0], position[1], position[2]);
        bone.position_keys_[j] = key;
      }

      // rotation key count
      if (f.GetLine(sLine) <= 0) {
        THROW;
      }
      int num_rotation_keys = 0;
      if (sscanf(sLine.c_str(), "%d", &num_rotation_keys) != 1) {
        THROW;
      }

      bone.rotation_keys_.resize(num_rotation_keys);

      for (int j = 0; j < num_rotation_keys; ++j) {
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        float time;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &time, &rotation[0], &rotation[1],
                &rotation[2]) != 4) {
          THROW;
        }
        rotation = RadianToDegree(rotation);

        msRotationKey key = {};
        key.time_ = time;
        rotation = RageVector3(rotation[0], rotation[1], rotation[2]);
        RageQuatFromHPR(&key.rotation_, rotation);
        bone.rotation_keys_[j] = key;
      }
    }

    // Ignore "Frames:" in file.  Calculate it ourself
    animation.total_frames_ = 0;
    for (int i = 0; i < (int)animation.bones_.size(); i++) {
      msBone& bone = animation.bones_[i];
      for (unsigned j = 0; j < bone.position_keys_.size(); ++j) {
        animation.total_frames_ = std::max(
            animation.total_frames_, (int)bone.position_keys_[j].time_);
      }
      for (unsigned j = 0; j < bone.rotation_keys_.size(); ++j) {
        animation.total_frames_ = std::max(
            animation.total_frames_, (int)bone.rotation_keys_[j].time_);
      }
    }
  }

  return loaded;
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
