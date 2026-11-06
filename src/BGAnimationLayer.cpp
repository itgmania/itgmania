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

const float PARTICLE_SPEED = 300;

#define MAX_TILES_WIDE int(SCREEN_WIDTH / 32 + 2)
#define MAX_TILES_HIGH int(SCREEN_HEIGHT / 32 + 2)
#define MAX_SPRITES (MAX_TILES_WIDE * MAX_TILES_HIGH)

inline float GetOffScreenLeft(Actor* pActor) {
  return SCREEN_LEFT - pActor->GetZoomedWidth() / 2;
}
inline float GetOffScreenRight(Actor* pActor) {
  return SCREEN_RIGHT + pActor->GetZoomedWidth() / 2;
}
inline float GetOffScreenTop(Actor* pActor) {
  return SCREEN_TOP - pActor->GetZoomedHeight() / 2;
}
inline float GetOffScreenBottom(Actor* pActor) {
  return SCREEN_BOTTOM + pActor->GetZoomedHeight() / 2;
}

inline bool IsOffScreenLeft(Actor* pActor) {
  return pActor->GetX() < GetOffScreenLeft(pActor);
}
inline bool IsOffScreenRight(Actor* pActor) {
  return pActor->GetX() > GetOffScreenRight(pActor);
}
inline bool IsOffScreenTop(Actor* pActor) {
  return pActor->GetY() < GetOffScreenTop(pActor);
}
inline bool IsOffScreenBottom(Actor* pActor) {
  return pActor->GetY() > GetOffScreenBottom(pActor);
}

// guard rail is the area that keeps particles from going off screen
inline float GetGuardRailLeft(Actor* pActor) {
  return SCREEN_LEFT + pActor->GetZoomedWidth() / 2;
}
inline float GetGuardRailRight(Actor* pActor) {
  return SCREEN_RIGHT - pActor->GetZoomedWidth() / 2;
}
inline float GetGuardRailTop(Actor* pActor) {
  return SCREEN_TOP + pActor->GetZoomedHeight() / 2;
}
inline float GetGuardRailBottom(Actor* pActor) {
  return SCREEN_BOTTOM - pActor->GetZoomedHeight() / 2;
}

inline bool HitGuardRailLeft(Actor* pActor) {
  return pActor->GetX() < GetGuardRailLeft(pActor);
}
inline bool HitGuardRailRight(Actor* pActor) {
  return pActor->GetX() > GetGuardRailRight(pActor);
}
inline bool HitGuardRailTop(Actor* pActor) {
  return pActor->GetY() < GetGuardRailTop(pActor);
}
inline bool HitGuardRailBottom(Actor* pActor) {
  return pActor->GetY() > GetGuardRailBottom(pActor);
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

void BGAnimationLayer::LoadFromAniLayerFile(const std::string& sPath) {
  /* Generic BGAs are new.  Animation directories with no INI are old and
   * obsolete. Don't combine them. */
  std::string lcPath = sPath;
  MakeLower(lcPath);

  if (lcPath.find("usesongbg") != std::string::npos) {
    const Song* pSong = GAMESTATE->cur_song_;
    std::string sSongBGPath;
    if (pSong && pSong->HasBackground()) {
      sSongBGPath = pSong->GetBackgroundPath();
    } else {
      sSongBGPath = THEME->GetPathG("Common", "fallback background");
    }

    Sprite* pSprite = new Sprite;
    pSprite->Load(Sprite::SongBGTexture(sSongBGPath));
    pSprite->StretchTo(FullScreenRectF);
    this->AddChild(pSprite);

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

  const std::string EFFECT_STRING[NUM_EFFECTS] = {
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
    if (lcPath.find(EFFECT_STRING[i]) != std::string::npos) {
      effect = (Effect)i;
    }
  }

  switch (effect) {
    case EFFECT_CENTER: {
      type_ = TYPE_SPRITE;
      Sprite* pSprite = new Sprite;
      this->AddChild(pSprite);
      pSprite->Load(sPath);
      pSprite->SetXY(SCREEN_CENTER_X, SCREEN_CENTER_Y);
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
      Sprite* pSprite = new Sprite;
      this->AddChild(pSprite);
      RageTextureID ID(sPath);
      ID.stretch_ = true;
      pSprite->Load(Sprite::SongBGTexture(ID));
      pSprite->StretchTo(FullScreenRectF);
      pSprite->SetCustomTextureRect(RectF(0, 0, 1, 1));

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
      Sprite* pSprite = new Sprite;
      this->AddChild(pSprite);
      pSprite->Load(Sprite::SongBGTexture(sPath));
      const RectF StretchedFullScreenRectF(
          FullScreenRectF.left_ - 200, FullScreenRectF.top_ - 200,
          FullScreenRectF.right_ + 200, FullScreenRectF.bottom_ + 200);

      pSprite->ScaleToCover(StretchedFullScreenRectF);
      pSprite->SetEffectSpin(RageVector3(0, 0, 60));
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
      s.Load(sPath);
      int iSpriteArea = int(s.GetUnzoomedWidth() * s.GetUnzoomedHeight());
      const int iMaxArea = int(SCREEN_WIDTH * SCREEN_HEIGHT);
      int iNumParticles = iMaxArea / iSpriteArea;
      iNumParticles = std::min(iNumParticles, MAX_SPRITES);

      for (int i = 0; i < iNumParticles; i++) {
        Sprite* pSprite = new Sprite;
        this->AddChild(pSprite);
        pSprite->Load(sPath);
        pSprite->SetZoom(0.7f + 0.6f * i / (float)iNumParticles);
        switch (effect) {
          case EFFECT_PARTICLES_BOUNCE:
            pSprite->SetX(
                randomf(GetGuardRailLeft(pSprite), GetGuardRailRight(pSprite)));
            pSprite->SetY(
                randomf(GetGuardRailTop(pSprite), GetGuardRailBottom(pSprite)));
            break;
          default:
            pSprite->SetX(
                randomf(GetOffScreenLeft(pSprite), GetOffScreenRight(pSprite)));
            pSprite->SetY(
                randomf(GetOffScreenTop(pSprite), GetOffScreenBottom(pSprite)));
            break;
        }

        switch (effect) {
          case EFFECT_PARTICLES_FLOAT_UP:
          case EFFECT_PARTICLES_SPIRAL_OUT:
            particle_velocity_.push_back(
                RageVector3(0, -PARTICLE_SPEED * pSprite->GetZoom(), 0));
            break;
          case EFFECT_PARTICLES_FLOAT_DOWN:
          case EFFECT_PARTICLES_SPIRAL_IN:
            particle_velocity_.push_back(
                RageVector3(0, PARTICLE_SPEED * pSprite->GetZoom(), 0));
            break;
          case EFFECT_PARTICLES_FLOAT_LEFT:
            particle_velocity_.push_back(
                RageVector3(-PARTICLE_SPEED * pSprite->GetZoom(), 0, 0));
            break;
          case EFFECT_PARTICLES_FLOAT_RIGHT:
            particle_velocity_.push_back(
                RageVector3(+PARTICLE_SPEED * pSprite->GetZoom(), 0, 0));
            break;
          case EFFECT_PARTICLES_BOUNCE:
            particles_bounce_ = true;
            pSprite->SetZoom(1);
            particle_velocity_.push_back(RageVector3(randomf(), randomf(), 0));
            RageVec3Normalize(&particle_velocity_[i], &particle_velocity_[i]);
            particle_velocity_[i].x_ *= PARTICLE_SPEED;
            particle_velocity_[i].y_ *= PARTICLE_SPEED;
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
      RageTextureID ID(sPath);
      ID.stretch_ = true;
      Sprite s;
      s.Load(ID);
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
          Sprite* pSprite = new Sprite;
          this->AddChild(pSprite);
          pSprite->Load(ID);
          pSprite->SetTextureWrapping(true);  // gets rid of some "cracks"

          switch (effect) {
            case EFFECT_TILE_STILL:
              break;
            case EFFECT_TILE_SCROLL_LEFT:
              tile_velocity_x_ = -PARTICLE_SPEED;
              break;
            case EFFECT_TILE_SCROLL_RIGHT:
              tile_velocity_x_ = +PARTICLE_SPEED;
              break;
            case EFFECT_TILE_SCROLL_UP:
              tile_velocity_y_ = -PARTICLE_SPEED;
              break;
            case EFFECT_TILE_SCROLL_DOWN:
              tile_velocity_y_ = +PARTICLE_SPEED;
              break;
            case EFFECT_TILE_FLIP_X:
              pSprite->SetEffectSpin(RageVector3(180, 0, 0));
              break;
            case EFFECT_TILE_FLIP_Y:
              pSprite->SetEffectSpin(RageVector3(0, 180, 0));
              break;
            case EFFECT_TILE_PULSE:
              pSprite->SetEffectPulse(1, 0.3f, 1.f);
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

  std::string sHint = sPath;
  MakeLower(sHint);

  if (sHint.find("cyclecolor") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetEffectRainbow(5);
    }
  }

  if (sHint.find("cyclealpha") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetEffectDiffuseShift(
          2, RageColor(1, 1, 1, 1), RageColor(1, 1, 1, 0));
    }
  }

  if (sHint.find("startonrandomframe") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetState(RandomInt(sub_actors_[i]->GetNumStates()));
    }
  }

  if (sHint.find("dontanimate") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->StopAnimating();
    }
  }

  if (sHint.find("add") != std::string::npos) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetBlendMode(BLEND_ADD);
    }
  }
}

void BGAnimationLayer::LoadFromNode(const XNode* pNode) {
  {
    bool bCond;
    if (pNode->GetAttrValue("Condition", bCond) && !bCond) {
      return;
    }
  }

  bool bStretch = false;
  {
    std::string type = "sprite";
    pNode->GetAttrValue("Type", type);
    MakeLower(type);

    /* The preferred way of stretching a sprite to fit the screen is
     * "Type=sprite" and "stretch=1".  "type=1" is for backwards-compatibility.
     */
    pNode->GetAttrValue("Stretch", bStretch);

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
      bStretch = true;
    } else if (StringToInt(type) == 2) {
      type_ = TYPE_PARTICLES;
    } else if (StringToInt(type) == 3) {
      type_ = TYPE_TILES;
    } else {
      type_ = TYPE_SPRITE;
    }
  }

  pNode->GetAttrValue("FOV", fov_);
  pNode->GetAttrValue("Lighting", lighting_);

  pNode->GetAttrValue("TexCoordVelocityX", tex_coord_velocity_x_);
  pNode->GetAttrValue("TexCoordVelocityY", tex_coord_velocity_y_);

  // compat:
  pNode->GetAttrValue("StretchTexCoordVelocityX", tex_coord_velocity_x_);
  pNode->GetAttrValue("StretchTexCoordVelocityY", tex_coord_velocity_y_);

  // particle and tile stuff
  float fZoomMin = 1;
  float fZoomMax = 1;
  pNode->GetAttrValue("ZoomMin", fZoomMin);
  pNode->GetAttrValue("ZoomMax", fZoomMax);

  float fVelocityXMin = 10, fVelocityXMax = 10;
  float fVelocityYMin = 0, fVelocityYMax = 0;
  float fVelocityZMin = 0, fVelocityZMax = 0;
  float fOverrideSpeed = 0;  // 0 means don't override speed
  pNode->GetAttrValue("VelocityXMin", fVelocityXMin);
  pNode->GetAttrValue("VelocityXMax", fVelocityXMax);
  pNode->GetAttrValue("VelocityYMin", fVelocityYMin);
  pNode->GetAttrValue("VelocityYMax", fVelocityYMax);
  pNode->GetAttrValue("VelocityZMin", fVelocityZMin);
  pNode->GetAttrValue("VelocityZMax", fVelocityZMax);
  pNode->GetAttrValue("OverrideSpeed", fOverrideSpeed);

  int iNumParticles = 10;
  pNode->GetAttrValue("NumParticles", iNumParticles);

  pNode->GetAttrValue("ParticlesBounce", particles_bounce_);
  pNode->GetAttrValue("TilesStartX", tiles_start_x_);
  pNode->GetAttrValue("TilesStartY", tiles_start_y_);
  pNode->GetAttrValue("TilesSpacingX", tiles_spacing_x_);
  pNode->GetAttrValue("TilesSpacingY", tiles_spacing_y_);
  pNode->GetAttrValue("TileVelocityX", tile_velocity_x_);
  pNode->GetAttrValue("TileVelocityY", tile_velocity_y_);

  switch (type_) {
    case TYPE_SPRITE: {
      Actor* pActor = ActorUtil::LoadFromNode(pNode, this);
      this->AddChild(pActor);
      if (bStretch) {
        pActor->StretchTo(FullScreenRectF);
      }
    } break;
    case TYPE_PARTICLES: {
      std::string sFile;
      ActorUtil::GetAttrPath(pNode, "File", sFile);
      FixSlashesInPlace(sFile);

      CollapsePath(sFile);

      for (int i = 0; i < iNumParticles; i++) {
        Actor* pActor = ActorUtil::MakeActor(sFile, this);
        if (pActor == nullptr) {
          continue;
        }
        this->AddChild(pActor);
        pActor->SetXY(
            randomf(
                float(FullScreenRectF.left_), float(FullScreenRectF.right_)),
            randomf(
                float(FullScreenRectF.top_), float(FullScreenRectF.bottom_)));
        pActor->SetZoom(randomf(fZoomMin, fZoomMax));
        particle_velocity_.push_back(RageVector3(
            randomf(fVelocityXMin, fVelocityXMax),
            randomf(fVelocityYMin, fVelocityYMax),
            randomf(fVelocityZMin, fVelocityZMax)));
        if (fOverrideSpeed != 0) {
          RageVec3Normalize(&particle_velocity_[i], &particle_velocity_[i]);
          particle_velocity_[i] *= fOverrideSpeed;
        }
      }
    } break;
    case TYPE_TILES: {
      std::string sFile;
      ActorUtil::GetAttrPath(pNode, "File", sFile);
      FixSlashesInPlace(sFile);

      CollapsePath(sFile);

      AutoActor s;
      s.Load(sFile);
      if (tiles_spacing_x_ == -1) {
        tiles_spacing_x_ = s->GetUnzoomedWidth();
      }
      if (tiles_spacing_y_ == -1) {
        tiles_spacing_y_ = s->GetUnzoomedHeight();
      }
      num_tiles_wide_ = 2 + (int)(SCREEN_WIDTH / tiles_spacing_x_);
      num_tiles_high_ = 2 + (int)(SCREEN_HEIGHT / tiles_spacing_y_);
      unsigned NumSprites = num_tiles_wide_ * num_tiles_high_;
      for (unsigned i = 0; i < NumSprites; i++) {
        Actor* pSprite = ActorUtil::MakeActor(sFile, this);
        if (pSprite == nullptr) {
          continue;
        }
        this->AddChild(pSprite);
        pSprite->SetTextureWrapping(true);  // gets rid of some "cracks"
        pSprite->SetZoom(randomf(fZoomMin, fZoomMax));
      }
    } break;
    default:
      FAIL_M(ssprintf("Unrecognized layer type: %i", type_));
  }

  bool bStartOnRandomFrame = false;
  pNode->GetAttrValue("StartOnRandomFrame", bStartOnRandomFrame);
  if (bStartOnRandomFrame) {
    for (unsigned i = 0; i < sub_actors_.size(); i++) {
      sub_actors_[i]->SetState(RandomInt(sub_actors_[i]->GetNumStates()));
    }
  }
}

void BGAnimationLayer::UpdateInternal(float fDeltaTime) {
  ActorFrame::UpdateInternal(fDeltaTime);

  fDeltaTime *= update_rate_;

  switch (type_) {
    case TYPE_SPRITE:
      if (tex_coord_velocity_x_ || tex_coord_velocity_y_) {
        for (unsigned i = 0; i < sub_actors_.size(); i++) {
          // XXX: there's no longer any guarantee that this is a Sprite
          Sprite* pSprite = (Sprite*)sub_actors_[i];
          pSprite->StretchTexCoords(
              fDeltaTime * tex_coord_velocity_x_,
              fDeltaTime * tex_coord_velocity_y_);
        }
      }
      break;
    case TYPE_PARTICLES:
      for (unsigned i = 0; i < sub_actors_.size(); i++) {
        Actor* pActor = sub_actors_[i];
        RageVector3& vel = particle_velocity_[i];

        sub_actors_[i]->SetX(pActor->GetX() + fDeltaTime * vel.x_);
        sub_actors_[i]->SetY(pActor->GetY() + fDeltaTime * vel.y_);
        pActor->SetZ(pActor->GetZ() + fDeltaTime * vel.z_);
        if (particles_bounce_) {
          if (HitGuardRailLeft(pActor)) {
            vel.x_ *= -1;
            pActor->SetX(GetGuardRailLeft(pActor));
          }
          if (HitGuardRailRight(pActor)) {
            vel.x_ *= -1;
            pActor->SetX(GetGuardRailRight(pActor));
          }
          if (HitGuardRailTop(pActor)) {
            vel.y_ *= -1;
            pActor->SetY(GetGuardRailTop(pActor));
          }
          if (HitGuardRailBottom(pActor)) {
            vel.y_ *= -1;
            pActor->SetY(GetGuardRailBottom(pActor));
          }
        } else  // !m_bParticlesBounce
        {
          if (vel.x_ < 0 && IsOffScreenLeft(pActor)) {
            pActor->SetX(GetOffScreenRight(pActor));
          }
          if (vel.x_ > 0 && IsOffScreenRight(pActor)) {
            pActor->SetX(GetOffScreenLeft(pActor));
          }
          if (vel.y_ < 0 && IsOffScreenTop(pActor)) {
            pActor->SetY(GetOffScreenBottom(pActor));
          }
          if (vel.y_ > 0 && IsOffScreenBottom(pActor)) {
            pActor->SetY(GetOffScreenTop(pActor));
          }
        }
      }
      break;
    case TYPE_TILES: {
      double fSecs = RageTimer::GetTimeSinceStart();
      double fTotalWidth =
          static_cast<double>(num_tiles_wide_) * tiles_spacing_x_;
      double fTotalHeight =
          static_cast<double>(num_tiles_high_) * tiles_spacing_y_;

      ASSERT(int(sub_actors_.size()) == num_tiles_wide_ * num_tiles_high_);

      for (int x = 0; x < num_tiles_wide_; x++) {
        for (int y = 0; y < num_tiles_high_; y++) {
          int i = y * num_tiles_wide_ + x;

          double fX =
              tiles_start_x_ + tiles_spacing_x_ * x + fSecs * tile_velocity_x_;
          double fY =
              tiles_start_y_ + tiles_spacing_y_ * y + fSecs * tile_velocity_y_;

          fX += tiles_spacing_x_ / 2;
          fY += tiles_spacing_y_ / 2;

          fX = std::fmod(fX, fTotalWidth);
          fY = std::fmod(fY, fTotalHeight);

          if (fX < 0) {
            fX += fTotalWidth;
          }
          if (fY < 0) {
            fY += fTotalHeight;
          }

          fX -= tiles_spacing_x_ / 2;
          fY -= tiles_spacing_y_ / 2;

          sub_actors_[i]->SetX(static_cast<float>(fX));
          sub_actors_[i]->SetY(static_cast<float>(fY));
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
