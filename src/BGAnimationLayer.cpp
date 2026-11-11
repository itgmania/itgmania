#include "BGAnimationLayer.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "AutoActor.h"
#include "GameState.h"
#include "LuaManager.h"
#include "RageMath.h"
#include "RageTextureID.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "ScreenDimensions.h"
#include "Song.h"
#include "Sprite.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

const float kParticleSpeed = 300;

#define MAX_TILES_WIDE int(SCREEN_WIDTH / 32 + 2)
#define MAX_TILES_HIGH int(SCREEN_HEIGHT / 32 + 2)
#define MAX_SPRITES (MAX_TILES_WIDE * MAX_TILES_HIGH)

inline float GetOffScreenLeft(Actor* actor) {
  return SCREEN_LEFT - actor->GetZoomedWidth() / 2;
}
inline float GetOffScreenRight(Actor* actor) {
  return SCREEN_RIGHT + actor->GetZoomedWidth() / 2;
}
inline float GetOffScreenTop(Actor* actor) {
  return SCREEN_TOP - actor->GetZoomedHeight() / 2;
}
inline float GetOffScreenBottom(Actor* actor) {
  return SCREEN_BOTTOM + actor->GetZoomedHeight() / 2;
}

inline bool IsOffScreenLeft(Actor* actor) {
  return actor->GetX() < GetOffScreenLeft(actor);
}
inline bool IsOffScreenRight(Actor* actor) {
  return actor->GetX() > GetOffScreenRight(actor);
}
inline bool IsOffScreenTop(Actor* actor) {
  return actor->GetY() < GetOffScreenTop(actor);
}
inline bool IsOffScreenBottom(Actor* actor) {
  return actor->GetY() > GetOffScreenBottom(actor);
}

// guard rail is the area that keeps particles from going off screen
inline float GetGuardRailLeft(Actor* actor) {
  return SCREEN_LEFT + actor->GetZoomedWidth() / 2;
}
inline float GetGuardRailRight(Actor* actor) {
  return SCREEN_RIGHT - actor->GetZoomedWidth() / 2;
}
inline float GetGuardRailTop(Actor* actor) {
  return SCREEN_TOP + actor->GetZoomedHeight() / 2;
}
inline float GetGuardRailBottom(Actor* actor) {
  return SCREEN_BOTTOM - actor->GetZoomedHeight() / 2;
}

inline bool HitGuardRailLeft(Actor* actor) {
  return actor->GetX() < GetGuardRailLeft(actor);
}
inline bool HitGuardRailRight(Actor* actor) {
  return actor->GetX() > GetGuardRailRight(actor);
}
inline bool HitGuardRailTop(Actor* actor) {
  return actor->GetY() < GetGuardRailTop(actor);
}
inline bool HitGuardRailBottom(Actor* actor) {
  return actor->GetY() > GetGuardRailBottom(actor);
}

BGAnimationLayer::BGAnimationLayer() {
  particle_velocity_.clear();

  type_ = TYPE_SPRITE;

  tex_coord_velocity_x_ = 0;
  tex_coord_velocity_y_ = 0;
  particles_bounce_ = false;
  num_tiles_wide_ = -1;
  num_tiles_high_ = -1;
  tiles_start_x_ = 0;
  tiles_start_y_ = 0;
  tiles_spacing_x_ = -1;
  tiles_spacing_y_ = -1;
  tile_velocity_x_ = 0;
  tile_velocity_y_ = 0;
}

BGAnimationLayer::~BGAnimationLayer() { ActorFrame::DeleteAllChildren(); }

void BGAnimationLayer::LoadFromAniLayerFile(const std::string& s_path) {
  /* Generic BGAs are new.  Animation directories with no INI are old and
   * obsolete. Don't combine them. */
  std::string lc_path = s_path;
  MakeLower(lc_path);

  if (lc_path.find("usesongbg") != std::string::npos) {
    const Song* song = GAMESTATE->cur_song_;
    std::string song_bg_path;
    if (song && song->HasBackground()) {
      song_bg_path = song->GetBackgroundPath();
    } else {
      song_bg_path = THEME->GetPathG("Common", "fallback background");
    }

    Sprite* sprite = new Sprite;
    sprite->Load(Sprite::SongBGTexture(song_bg_path));
    sprite->StretchTo(FullScreenRectF);
    this->AddChild(sprite);

    return;  // this will ignore other effects in the file name
  }

  enum Effect {
    EFFECT_CENTER,
    EFFECT_STRETCH_STILL,
    EFFECT_STRETCH_SCROLL_LEFT,
    EFFECT_STRETCH_SCROLL_RIGHT,
    EFFECT_STRETCH_SCROLL_UP,
    EFFECT_STRETCH_SCROLL_DOWN,
    EFFECT_STRETCH_WATER,
    EFFECT_STRETCH_BUBBLE,
    EFFECT_STRETCH_TWIST,
    EFFECT_STRETCH_SPIN,
    EFFECT_PARTICLES_SPIRAL_OUT,
    EFFECT_PARTICLES_SPIRAL_IN,
    EFFECT_PARTICLES_FLOAT_UP,
    EFFECT_PARTICLES_FLOAT_DOWN,
    EFFECT_PARTICLES_FLOAT_LEFT,
    EFFECT_PARTICLES_FLOAT_RIGHT,
    EFFECT_PARTICLES_BOUNCE,
    EFFECT_TILE_STILL,
    EFFECT_TILE_SCROLL_LEFT,
    EFFECT_TILE_SCROLL_RIGHT,
    EFFECT_TILE_SCROLL_UP,
    EFFECT_TILE_SCROLL_DOWN,
    EFFECT_TILE_FLIP_X,
    EFFECT_TILE_FLIP_Y,
    EFFECT_TILE_PULSE,
    NUM_EFFECTS,  // leave this at the end
    EFFECT_INVALID
  };

  const std::string kEffectString[NUM_EFFECTS] = {
      "center",
      "stretchstill",
      "stretchscrollleft",
      "stretchscrollright",
      "stretchscrollup",
      "stretchscrolldown",
      "stretchwater",
      "stretchbubble",
      "stretchtwist",
      "stretchspin",
      "particlesspiralout",
      "particlesspiralin",
      "particlesfloatup",
      "particlesfloatdown",
      "particlesfloatleft",
      "particlesfloatright",
      "particlesbounce",
      "tilestill",
      "tilescrollleft",
      "tilescrollright",
      "tilescrollup",
      "tilescrolldown",
      "tileflipx",
      "tileflipy",
      "tilepulse",
  };

  Effect effect = EFFECT_CENTER;

  for (int i = 0; i < NUM_EFFECTS; i++) {
    if (lc_path.find(kEffectString[i]) != std::string::npos) {
      effect = (Effect)i;
    }
  }

  switch (effect) {
    case EFFECT_CENTER: {
      type_ = TYPE_SPRITE;
      Sprite* sprite = new Sprite;
      this->AddChild(sprite);
      sprite->Load(s_path);
      sprite->SetXY(SCREEN_CENTER_X, SCREEN_CENTER_Y);
    } break;
    case EFFECT_STRETCH_STILL:
    case EFFECT_STRETCH_SCROLL_LEFT:
    case EFFECT_STRETCH_SCROLL_RIGHT:
    case EFFECT_STRETCH_SCROLL_UP:
    case EFFECT_STRETCH_SCROLL_DOWN:
    case EFFECT_STRETCH_WATER:
    case EFFECT_STRETCH_BUBBLE:
    case EFFECT_STRETCH_TWIST: {
      type_ = TYPE_SPRITE;
      Sprite* sprite = new Sprite;
      this->AddChild(sprite);
      RageTextureID id(s_path);
      id.stretch_ = true;
      sprite->Load(Sprite::SongBGTexture(id));
      sprite->StretchTo(FullScreenRectF);
      sprite->SetCustomTextureRect(RectF(0, 0, 1, 1));

      switch (effect) {
        case EFFECT_STRETCH_SCROLL_LEFT:
          tex_coord_velocity_x_ = +0.5f;
          tex_coord_velocity_y_ = 0;
          break;
        case EFFECT_STRETCH_SCROLL_RIGHT:
          tex_coord_velocity_x_ = -0.5f;
          tex_coord_velocity_y_ = 0;
          break;
        case EFFECT_STRETCH_SCROLL_UP:
          tex_coord_velocity_x_ = 0;
          tex_coord_velocity_y_ = +0.5f;
          break;
        case EFFECT_STRETCH_SCROLL_DOWN:
          tex_coord_velocity_x_ = 0;
          tex_coord_velocity_y_ = -0.5f;
          break;
        default:
          break;
      }
    } break;
    case EFFECT_STRETCH_SPIN: {
      type_ = TYPE_SPRITE;
      Sprite* sprite = new Sprite;
      this->AddChild(sprite);
      sprite->Load(Sprite::SongBGTexture(s_path));
      const RectF stretched_full_screen_rect_f(
          FullScreenRectF.left_ - 200, FullScreenRectF.top_ - 200,
          FullScreenRectF.right_ + 200, FullScreenRectF.bottom_ + 200);

      sprite->ScaleToCover(stretched_full_screen_rect_f);
      sprite->SetEffectSpin(RageVector3(0, 0, 60));
    } break;
    case EFFECT_PARTICLES_SPIRAL_OUT:
    case EFFECT_PARTICLES_SPIRAL_IN:
    case EFFECT_PARTICLES_FLOAT_UP:
    case EFFECT_PARTICLES_FLOAT_DOWN:
    case EFFECT_PARTICLES_FLOAT_LEFT:
    case EFFECT_PARTICLES_FLOAT_RIGHT:
    case EFFECT_PARTICLES_BOUNCE: {
      type_ = TYPE_PARTICLES;
      Sprite s;
      s.Load(s_path);
      int sprite_area = int(s.GetUnzoomedWidth() * s.GetUnzoomedHeight());
      const int max_area = int(SCREEN_WIDTH * SCREEN_HEIGHT);
      int num_particles = max_area / sprite_area;
      num_particles = std::min(num_particles, MAX_SPRITES);

      for (int i = 0; i < num_particles; i++) {
        Sprite* sprite = new Sprite;
        this->AddChild(sprite);
        sprite->Load(s_path);
        sprite->SetZoom(0.7f + 0.6f * i / (float)num_particles);
        switch (effect) {
          case EFFECT_PARTICLES_BOUNCE:
            sprite->SetX(
                randomf(GetGuardRailLeft(sprite), GetGuardRailRight(sprite)));
            sprite->SetY(
                randomf(GetGuardRailTop(sprite), GetGuardRailBottom(sprite)));
            break;
          default:
            sprite->SetX(
                randomf(GetOffScreenLeft(sprite), GetOffScreenRight(sprite)));
            sprite->SetY(
                randomf(GetOffScreenTop(sprite), GetOffScreenBottom(sprite)));
            break;
        }

        switch (effect) {
          case EFFECT_PARTICLES_FLOAT_UP:
          case EFFECT_PARTICLES_SPIRAL_OUT:
            particle_velocity_.push_back(
                RageVector3(0, -kParticleSpeed * sprite->GetZoom(), 0));
            break;
          case EFFECT_PARTICLES_FLOAT_DOWN:
          case EFFECT_PARTICLES_SPIRAL_IN:
            particle_velocity_.push_back(
                RageVector3(0, kParticleSpeed * sprite->GetZoom(), 0));
            break;
          case EFFECT_PARTICLES_FLOAT_LEFT:
            particle_velocity_.push_back(
                RageVector3(-kParticleSpeed * sprite->GetZoom(), 0, 0));
            break;
          case EFFECT_PARTICLES_FLOAT_RIGHT:
            particle_velocity_.push_back(
                RageVector3(+kParticleSpeed * sprite->GetZoom(), 0, 0));
            break;
          case EFFECT_PARTICLES_BOUNCE:
            particles_bounce_ = true;
            sprite->SetZoom(1);
            particle_velocity_.push_back(RageVector3(randomf(), randomf(), 0));
            RageVec3Normalize(&particle_velocity_[i], &particle_velocity_[i]);
            particle_velocity_[i].x_ *= kParticleSpeed;
            particle_velocity_[i].y_ *= kParticleSpeed;
            break;
          default:
            FAIL_M(ssprintf("Unrecognized layer effect: %i", effect));
        }
      }
    } break;
    case EFFECT_TILE_STILL:
    case EFFECT_TILE_SCROLL_LEFT:
    case EFFECT_TILE_SCROLL_RIGHT:
    case EFFECT_TILE_SCROLL_UP:
    case EFFECT_TILE_SCROLL_DOWN:
    case EFFECT_TILE_FLIP_X:
    case EFFECT_TILE_FLIP_Y:
    case EFFECT_TILE_PULSE: {
      type_ = TYPE_TILES;
      RageTextureID id(s_path);
      id.stretch_ = true;
      Sprite s;
      s.Load(id);
      num_tiles_wide_ = 2 + int(SCREEN_WIDTH / s.GetUnzoomedWidth());
      num_tiles_wide_ = std::min(num_tiles_wide_, MAX_TILES_WIDE);
      num_tiles_high_ = 2 + int(SCREEN_HEIGHT / s.GetUnzoomedHeight());
      num_tiles_high_ = std::min(num_tiles_high_, MAX_TILES_HIGH);
      tiles_start_x_ = s.GetUnzoomedWidth() / 2;
      tiles_start_y_ = s.GetUnzoomedHeight() / 2;
      tiles_spacing_x_ = s.GetUnzoomedWidth();
      tiles_spacing_y_ = s.GetUnzoomedHeight();
      for (int x = 0; x < num_tiles_wide_; x++) {
        for (int y = 0; y < num_tiles_high_; y++) {
          Sprite* sprite = new Sprite;
          this->AddChild(sprite);
          sprite->Load(id);
          sprite->SetTextureWrapping(true);  // gets rid of some "cracks"

          switch (effect) {
            case EFFECT_TILE_STILL:
              break;
            case EFFECT_TILE_SCROLL_LEFT:
              tile_velocity_x_ = -kParticleSpeed;
              break;
            case EFFECT_TILE_SCROLL_RIGHT:
              tile_velocity_x_ = +kParticleSpeed;
              break;
            case EFFECT_TILE_SCROLL_UP:
              tile_velocity_y_ = -kParticleSpeed;
              break;
            case EFFECT_TILE_SCROLL_DOWN:
              tile_velocity_y_ = +kParticleSpeed;
              break;
            case EFFECT_TILE_FLIP_X:
              sprite->SetEffectSpin(RageVector3(180, 0, 0));
              break;
            case EFFECT_TILE_FLIP_Y:
              sprite->SetEffectSpin(RageVector3(0, 180, 0));
              break;
            case EFFECT_TILE_PULSE:
              sprite->SetEffectPulse(1, 0.3f, 1.f);
              break;
            default:
              FAIL_M(ssprintf("Not a tile effect: %i", effect));
          }
        }
      }
    } break;
    default:
      FAIL_M(ssprintf("Unrecognized layer effect: %i", effect));
  }

  std::string hint = s_path;
  MakeLower(hint);

  if (hint.find("cyclecolor") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetEffectRainbow(5);
    }
  }

  if (hint.find("cyclealpha") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetEffectDiffuseShift(
          2, RageColor(1, 1, 1, 1), RageColor(1, 1, 1, 0));
    }
  }

  if (hint.find("startonrandomframe") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetState(RandomInt(sub_actors_[i]->GetNumStates()));
    }
  }

  if (hint.find("dontanimate") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->StopAnimating();
    }
  }

  if (hint.find("add") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetBlendMode(BLEND_ADD);
    }
  }
}

void BGAnimationLayer::LoadFromNode(const XNode* node) {
  {
    bool cond;
    if (node->GetAttrValue("Condition", cond) && !cond) {
      return;
    }
  }

  bool stretch = false;
  {
    std::string type = "sprite";
    node->GetAttrValue("Type", type);
    MakeLower(type);

    /* The preferred way of stretching a sprite to fit the screen is
     * "Type=sprite" and "stretch=1".  "type=1" is for backwards-compatibility.
     */
    node->GetAttrValue("Stretch", stretch);

    // Check for string match first, then do integer match.
    // "if(StringType(type)==0)" was matching against all string matches.
    // -Chris
    if (EqualsNoCase(type, "sprite")) {
      type_ = TYPE_SPRITE;
    } else if (EqualsNoCase(type, "particles")) {
      type_ = TYPE_PARTICLES;
    } else if (EqualsNoCase(type, "tiles")) {
      type_ = TYPE_TILES;
    } else if (StringToInt(type) == 1) {
      type_ = TYPE_SPRITE;
      stretch = true;
    } else if (StringToInt(type) == 2) {
      type_ = TYPE_PARTICLES;
    } else if (StringToInt(type) == 3) {
      type_ = TYPE_TILES;
    } else {
      type_ = TYPE_SPRITE;
    }
  }

  node->GetAttrValue("FOV", fov_);
  node->GetAttrValue("Lighting", lighting_);

  node->GetAttrValue("TexCoordVelocityX", tex_coord_velocity_x_);
  node->GetAttrValue("TexCoordVelocityY", tex_coord_velocity_y_);

  // compat:
  node->GetAttrValue("StretchTexCoordVelocityX", tex_coord_velocity_x_);
  node->GetAttrValue("StretchTexCoordVelocityY", tex_coord_velocity_y_);

  // particle and tile stuff
  float zoom_min = 1;
  float zoom_max = 1;
  node->GetAttrValue("ZoomMin", zoom_min);
  node->GetAttrValue("ZoomMax", zoom_max);

  float velocity_x_min = 10, velocity_x_max = 10;
  float velocity_y_min = 0, velocity_y_max = 0;
  float velocity_z_min = 0, velocity_z_max = 0;
  float override_speed = 0;  // 0 means don't override speed
  node->GetAttrValue("VelocityXMin", velocity_x_min);
  node->GetAttrValue("VelocityXMax", velocity_x_max);
  node->GetAttrValue("VelocityYMin", velocity_y_min);
  node->GetAttrValue("VelocityYMax", velocity_y_max);
  node->GetAttrValue("VelocityZMin", velocity_z_min);
  node->GetAttrValue("VelocityZMax", velocity_z_max);
  node->GetAttrValue("OverrideSpeed", override_speed);

  int num_particles = 10;
  node->GetAttrValue("NumParticles", num_particles);

  node->GetAttrValue("ParticlesBounce", particles_bounce_);
  node->GetAttrValue("TilesStartX", tiles_start_x_);
  node->GetAttrValue("TilesStartY", tiles_start_y_);
  node->GetAttrValue("TilesSpacingX", tiles_spacing_x_);
  node->GetAttrValue("TilesSpacingY", tiles_spacing_y_);
  node->GetAttrValue("TileVelocityX", tile_velocity_x_);
  node->GetAttrValue("TileVelocityY", tile_velocity_y_);

  switch (type_) {
    case TYPE_SPRITE: {
      Actor* actor = ActorUtil::LoadFromNode(node, this);
      this->AddChild(actor);
      if (stretch) {
        actor->StretchTo(FullScreenRectF);
      }
    } break;
    case TYPE_PARTICLES: {
      std::string file;
      ActorUtil::GetAttrPath(node, "File", file);
      FixSlashesInPlace(file);

      CollapsePath(file);

      for (int i = 0; i < num_particles; i++) {
        Actor* actor = ActorUtil::MakeActor(file, this);
        if (actor == nullptr) {
          continue;
        }
        this->AddChild(actor);
        actor->SetXY(
            randomf(
                float(FullScreenRectF.left_), float(FullScreenRectF.right_)),
            randomf(
                float(FullScreenRectF.top_), float(FullScreenRectF.bottom_)));
        actor->SetZoom(randomf(zoom_min, zoom_max));
        particle_velocity_.push_back(RageVector3(
            randomf(velocity_x_min, velocity_x_max),
            randomf(velocity_y_min, velocity_y_max),
            randomf(velocity_z_min, velocity_z_max)));
        if (override_speed != 0) {
          RageVec3Normalize(&particle_velocity_[i], &particle_velocity_[i]);
          particle_velocity_[i] *= override_speed;
        }
      }
    } break;
    case TYPE_TILES: {
      std::string file;
      ActorUtil::GetAttrPath(node, "File", file);
      FixSlashesInPlace(file);

      CollapsePath(file);

      AutoActor s;
      s.Load(file);
      if (tiles_spacing_x_ == -1) {
        tiles_spacing_x_ = s->GetUnzoomedWidth();
      }
      if (tiles_spacing_y_ == -1) {
        tiles_spacing_y_ = s->GetUnzoomedHeight();
      }
      num_tiles_wide_ = 2 + (int)(SCREEN_WIDTH / tiles_spacing_x_);
      num_tiles_high_ = 2 + (int)(SCREEN_HEIGHT / tiles_spacing_y_);
      unsigned num_sprites = num_tiles_wide_ * num_tiles_high_;
      for (unsigned i = 0; i < num_sprites; i++) {
        Actor* sprite = ActorUtil::MakeActor(file, this);
        if (sprite == nullptr) {
          continue;
        }
        this->AddChild(sprite);
        sprite->SetTextureWrapping(true);  // gets rid of some "cracks"
        sprite->SetZoom(randomf(zoom_min, zoom_max));
      }
    } break;
    default:
      FAIL_M(ssprintf("Unrecognized layer type: %i", type_));
  }

  bool start_on_random_frame = false;
  node->GetAttrValue("StartOnRandomFrame", start_on_random_frame);
  if (start_on_random_frame) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetState(RandomInt(sub_actors_[i]->GetNumStates()));
    }
  }
}

void BGAnimationLayer::UpdateInternal(float delta_time) {
  ActorFrame::UpdateInternal(delta_time);

  delta_time *= update_rate_;

  switch (type_) {
    case TYPE_SPRITE:
      if (tex_coord_velocity_x_ || tex_coord_velocity_y_) {
        for (unsigned i = 0; i < sub_actors_.size(); i++) {
          // XXX: there's no longer any guarantee that this is a Sprite
          Sprite* sprite = (Sprite*)sub_actors_[i];
          sprite->StretchTexCoords(
              delta_time * tex_coord_velocity_x_,
              delta_time * tex_coord_velocity_y_);
        }
      }
      break;
    case TYPE_PARTICLES:
      for (unsigned i = 0; i < sub_actors_.size(); i++) {
        Actor* actor = sub_actors_[i];
        RageVector3& vel = particle_velocity_[i];

        sub_actors_[i]->SetX(actor->GetX() + delta_time * vel.x_);
        sub_actors_[i]->SetY(actor->GetY() + delta_time * vel.y_);
        actor->SetZ(actor->GetZ() + delta_time * vel.z_);
        if (particles_bounce_) {
          if (HitGuardRailLeft(actor)) {
            vel.x_ *= -1;
            actor->SetX(GetGuardRailLeft(actor));
          }
          if (HitGuardRailRight(actor)) {
            vel.x_ *= -1;
            actor->SetX(GetGuardRailRight(actor));
          }
          if (HitGuardRailTop(actor)) {
            vel.y_ *= -1;
            actor->SetY(GetGuardRailTop(actor));
          }
          if (HitGuardRailBottom(actor)) {
            vel.y_ *= -1;
            actor->SetY(GetGuardRailBottom(actor));
          }
        } else  // !m_bParticlesBounce
        {
          if (vel.x_ < 0 && IsOffScreenLeft(actor)) {
            actor->SetX(GetOffScreenRight(actor));
          }
          if (vel.x_ > 0 && IsOffScreenRight(actor)) {
            actor->SetX(GetOffScreenLeft(actor));
          }
          if (vel.y_ < 0 && IsOffScreenTop(actor)) {
            actor->SetY(GetOffScreenBottom(actor));
          }
          if (vel.y_ > 0 && IsOffScreenBottom(actor)) {
            actor->SetY(GetOffScreenTop(actor));
          }
        }
      }
      break;
    case TYPE_TILES: {
      double secs = RageTimer::GetTimeSinceStart();
      double total_width =
          static_cast<double>(num_tiles_wide_) * tiles_spacing_x_;
      double total_height =
          static_cast<double>(num_tiles_high_) * tiles_spacing_y_;

      ASSERT(int(sub_actors_.size()) == num_tiles_wide_ * num_tiles_high_);

      for (int x = 0; x < num_tiles_wide_; x++) {
        for (int y = 0; y < num_tiles_high_; y++) {
          int i = y * num_tiles_wide_ + x;

          double f_x =
              tiles_start_x_ + tiles_spacing_x_ * x + secs * tile_velocity_x_;
          double f_y =
              tiles_start_y_ + tiles_spacing_y_ * y + secs * tile_velocity_y_;

          f_x += tiles_spacing_x_ / 2;
          f_y += tiles_spacing_y_ / 2;

          f_x = std::fmod(f_x, total_width);
          f_y = std::fmod(f_y, total_height);

          if (f_x < 0) {
            f_x += total_width;
          }
          if (f_y < 0) {
            f_y += total_height;
          }

          f_x -= tiles_spacing_x_ / 2;
          f_y -= tiles_spacing_y_ / 2;

          sub_actors_[i]->SetX(static_cast<float>(f_x));
          sub_actors_[i]->SetY(static_cast<float>(f_y));
        }
      }
    } break;
    default:
      FAIL_M(ssprintf("Unrecognized layer type: %i", type_));
  }
}

/*
 * (c) 2001-2004 Ben Nordstrom, Chris Danford, Glenn Maynard
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
