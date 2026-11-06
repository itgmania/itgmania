#include "DancingCharacters.h"

#include <string>

#include "Character.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "Model.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Song.h"
#include "StatsManager.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

int Neg1OrPos1();

#define CAMERA_REST_DISTANCE THEME->GetMetricF("DancingCamera", "RestDistance")
#define CAMERA_REST_LOOK_AT_HEIGHT \
  THEME->GetMetricF("DancingCamera", "RestHeight")
#define CAMERA_SWEEP_DISTANCE \
  THEME->GetMetricF("DancingCamera", "SweepDistance")
#define CAMERA_SWEEP_DISTANCE_VARIANCE \
  THEME->GetMetricF("DancingCamera", "SweepDistanceVariable")
#define CAMERA_SWEEP_HEIGHT_VARIANCE \
  THEME->GetMetricF("DancingCamera", "SweepHeightVariable")
#define CAMERA_SWEEP_PAN_Y_RANGE_DEGREES \
  THEME->GetMetricF("DancingCamera", "SweepPanYRangeDegrees")
#define CAMERA_SWEEP_PAN_Y_VARIANCE_DEGREES \
  THEME->GetMetricF("DancingCamera", "SweepPanYVarianceDegrees")
#define CAMERA_SWEEP_LOOK_AT_HEIGHT \
  THEME->GetMetricF("DancingCamera", "SweepHeight")
#define CAMERA_STILL_DISTANCE \
  THEME->GetMetricF("DancingCamera", "StillDistance")
#define CAMERA_STILL_DISTANCE_VARIANCE \
  THEME->GetMetricF("DancingCamera", "StillDistanceVariance")
#define CAMERA_STILL_PAN_Y_RANGE_DEGREES \
  THEME->GetMetricF("DancingCamera", "StillPanYRangeDegrees")
#define CAMERA_STILL_HEIGHT_VARIANCE \
  THEME->GetMetricF("DancingCamera", "StillHeightVariance")
#define CAMERA_STILL_LOOK_AT_HEIGHT \
  THEME->GetMetricF("DancingCamera", "StillHeight")

// This is to setup the lighting color.
const ThemeMetric<RageColor> LIGHT_AMBIENT_COLOR(
    "DancingCamera", "AmbientColor");
const ThemeMetric<RageColor> LIGHT_DIFFUSE_COLOR(
    "DancingCamera", "DiffuseColor");

// This is for Danger and Failed colors.
const ThemeMetric<RageColor> LIGHT_ADANGER_COLOR(
    "DancingCamera", "AmbientDangerColor");
const ThemeMetric<RageColor> LIGHT_DDANGER_COLOR(
    "DancingCamera", "DiffuseDangerColor");

// This is to make positioning of said light.
#define LIGHTING_X THEME->GetMetricF("DancingCamera", "LightX")
#define LIGHTING_Y THEME->GetMetricF("DancingCamera", "LightY")
#define LIGHTING_Z THEME->GetMetricF("DancingCamera", "LightZ")

// Position of the model in X (one player)
#define MODEL_X_ONE_PLAYER \
  THEME->GetMetricF("DancingCharacters", "OnePlayerModelX")

// As the name implies, this sets the ammount of beats that the camera will
// stay in its current state until transitioning to the next camera tween.
#define AMM_BEATS_TIL_NEXTCAMERA \
  THEME->GetMetricF("DancingCamera", "BeatsUntilNextCamPos")
#define CAM_FOV THEME->GetMetricF("DancingCamera", "CameraFOV")

const float MODEL_X_TWO_PLAYERS[NUM_PLAYERS] = {+8, -8};
const float MODEL_ROTATIONY_TWO_PLAYERS[NUM_PLAYERS] = {-90, 90};

DancingCharacters::DancingCharacters()
    : draw_danger_light_(false),
      camera_distance_(0),
      camera_pan_y_start_(0),
      camera_pan_y_end_(0),
      look_at_height_(0),
      camera_height_start_(0),
      camera_height_end_(0),
      this_camera_start_beat_(0),
      this_camera_end_beat_(0) {
  FOREACH_PlayerNumber(p) {
    character_[p] = new Model;
    if (!GAMESTATE->IsPlayerEnabled(p)) {
      continue;
    }

    Character* pChar = GAMESTATE->cur_characters_[p];
    if (!pChar) {
      continue;
    }

    if (pChar->GetModelPath().empty()) {
      continue;
    }

    if (GAMESTATE->GetNumPlayersEnabled() == 2) {
      character_[p]->SetX(MODEL_X_TWO_PLAYERS[p]);
    } else {
      character_[p]->SetX(MODEL_X_ONE_PLAYER);
    }

    switch (GAMESTATE->play_mode_) {
      case PLAY_MODE_BATTLE:
      case PLAY_MODE_RAVE:
        character_[p]->SetRotationY(MODEL_ROTATIONY_TWO_PLAYERS[p]);
      default:
        break;
    }

    character_[p]->LoadMilkshapeAscii(pChar->GetModelPath());
    character_[p]->LoadMilkshapeAsciiBones(
        "rest", pChar->GetRestAnimationPath());
    character_[p]->LoadMilkshapeAsciiBones(
        "warmup", pChar->GetWarmUpAnimationPath());
    character_[p]->LoadMilkshapeAsciiBones(
        "dance", pChar->GetDanceAnimationPath());
    character_[p]->SetCullMode(
        CULL_NONE);  // many of the models floating around have the vertex order
                     // flipped

    character_[p]->RunCommands(pChar->init_);
  }
}

DancingCharacters::~DancingCharacters() {
  FOREACH_PlayerNumber(p) delete character_[p];
}

void DancingCharacters::LoadNextSong() {
  // initial camera sweep is still
  camera_distance_ = CAMERA_REST_DISTANCE;
  camera_pan_y_start_ = 0;
  camera_pan_y_end_ = 0;
  camera_height_start_ = CAMERA_REST_LOOK_AT_HEIGHT;
  camera_height_end_ = CAMERA_REST_LOOK_AT_HEIGHT;
  look_at_height_ = CAMERA_REST_LOOK_AT_HEIGHT;
  this_camera_start_beat_ = 0;
  this_camera_end_beat_ = 0;

  ASSERT(GAMESTATE->cur_song_ != nullptr);
  this_camera_end_beat_ = GAMESTATE->cur_song_->GetFirstBeat();

  FOREACH_PlayerNumber(p) if (GAMESTATE->IsPlayerEnabled(p)) character_[p]
      ->PlayAnimation("rest");
}

int Neg1OrPos1() { return RandomInt(2) ? -1 : +1; }

void DancingCharacters::Update(float fDelta) {
  if (GAMESTATE->position_.freeze_ || GAMESTATE->position_.delay_) {
    // spin the camera Matrix-style
    camera_pan_y_start_ += fDelta * 40;
    camera_pan_y_end_ += fDelta * 40;
  } else {
    // make the characters move
    float fBPM = GAMESTATE->position_.cur_bps_ * 60;
    float fUpdateScale = SCALE(fBPM, 60.f, 300.f, 0.75f, 1.5f);
    rage_clamp(fUpdateScale, 0.75f, 1.5f);

    /* It's OK for the animation to go slower than natural when we're
     * at a very low music rate. */
    fUpdateScale *= GAMESTATE->song_options_.GetCurrent().music_rate_;

    FOREACH_PlayerNumber(p) {
      if (GAMESTATE->IsPlayerEnabled(p)) {
        character_[p]->Update(fDelta * fUpdateScale);
      }
    }
  }

  static bool bWasGameplayStarting = false;
  bool bGameplayStarting = GAMESTATE->gameplay_lead_in_;
  if (!bWasGameplayStarting && bGameplayStarting) {
    FOREACH_PlayerNumber(p) if (GAMESTATE->IsPlayerEnabled(p)) character_[p]
        ->PlayAnimation("warmup");
  }
  bWasGameplayStarting = bGameplayStarting;

  static float fLastBeat = GAMESTATE->position_.song_beat_;
  float firstBeat = GAMESTATE->cur_song_->GetFirstBeat();
  float fThisBeat = GAMESTATE->position_.song_beat_;
  if (fLastBeat < firstBeat && fThisBeat >= firstBeat) {
    FOREACH_PlayerNumber(p) character_[p]->PlayAnimation("dance");
  }
  fLastBeat = fThisBeat;

  // time for a new sweep?
  if (GAMESTATE->position_.song_beat_ > this_camera_end_beat_) {
    if (RandomInt(6) >= 4) {
      // sweeping camera
      camera_distance_ = CAMERA_SWEEP_DISTANCE +
                         RandomInt(-1, 1) * CAMERA_SWEEP_DISTANCE_VARIANCE;
      camera_pan_y_start_ = camera_pan_y_end_ =
          RandomInt(-1, 1) * CAMERA_SWEEP_PAN_Y_RANGE_DEGREES;
      camera_height_start_ = camera_height_end_ = CAMERA_STILL_LOOK_AT_HEIGHT;

      camera_pan_y_end_ +=
          RandomInt(-1, 1) * CAMERA_SWEEP_PAN_Y_VARIANCE_DEGREES;
      camera_height_start_ = camera_height_end_ =
          camera_height_start_ +
          RandomInt(-1, 1) * CAMERA_SWEEP_HEIGHT_VARIANCE;

      float fCameraHeightVariance =
          RandomInt(-1, 1) * CAMERA_SWEEP_HEIGHT_VARIANCE;
      camera_height_start_ -= fCameraHeightVariance;
      camera_height_end_ += fCameraHeightVariance;

      look_at_height_ = CAMERA_SWEEP_LOOK_AT_HEIGHT;
    } else {
      // still camera
      camera_distance_ = CAMERA_STILL_DISTANCE +
                         RandomInt(-1, 1) * CAMERA_STILL_DISTANCE_VARIANCE;
      camera_pan_y_start_ = camera_pan_y_end_ =
          Neg1OrPos1() * CAMERA_STILL_PAN_Y_RANGE_DEGREES;
      camera_height_start_ = camera_height_end_ =
          CAMERA_SWEEP_LOOK_AT_HEIGHT +
          Neg1OrPos1() * CAMERA_STILL_HEIGHT_VARIANCE;

      look_at_height_ = CAMERA_STILL_LOOK_AT_HEIGHT;
    }

    int iCurBeat = (int)GAMESTATE->position_.song_beat_;
    // Need to convert it to a int, otherwise it complains.
    int NewBeatToSet = (int)AMM_BEATS_TIL_NEXTCAMERA;
    if (NewBeatToSet != 0) {
      iCurBeat -= iCurBeat % NewBeatToSet;
    }

    this_camera_start_beat_ = (float)iCurBeat;
    this_camera_end_beat_ = float(iCurBeat + AMM_BEATS_TIL_NEXTCAMERA);
  }
}

void DancingCharacters::DrawPrimitives() {
  DISPLAY->CameraPushMatrix();

  float fPercentIntoSweep;
  if (this_camera_start_beat_ == this_camera_end_beat_) {
    fPercentIntoSweep = 0;
  } else {
    fPercentIntoSweep = SCALE(
        GAMESTATE->position_.song_beat_, this_camera_start_beat_,
        this_camera_end_beat_, 0.f, 1.f);
  }
  float fCameraPanY = SCALE(
      fPercentIntoSweep, 0.f, 1.f, camera_pan_y_start_, camera_pan_y_end_);
  float fCameraHeight = SCALE(
      fPercentIntoSweep, 0.f, 1.f, camera_height_start_, camera_height_end_);

  RageVector3 m_CameraPoint(0, fCameraHeight, -camera_distance_);
  RageMatrix CameraRot;
  RageMatrixRotationY(&CameraRot, fCameraPanY);
  RageVec3TransformCoord(&m_CameraPoint, &m_CameraPoint, &CameraRot);

  RageVector3 m_LookAt(0, look_at_height_, 0);

  DISPLAY->LoadLookAt(CAM_FOV, m_CameraPoint, m_LookAt, RageVector3(0, 1, 0));

  FOREACH_EnabledPlayer(p) {
    bool bFailed = STATSMAN->cur_stage_stats_.player_[p].failed_;
    bool bDanger = draw_danger_light_;

    DISPLAY->SetLighting(true);

    RageColor ambient = bFailed ? (RageColor)LIGHT_ADANGER_COLOR
                                : (bDanger ? (RageColor)LIGHT_ADANGER_COLOR
                                           : (RageColor)LIGHT_AMBIENT_COLOR);
    RageColor diffuse = bFailed ? (RageColor)LIGHT_DDANGER_COLOR
                                : (bDanger ? (RageColor)LIGHT_DDANGER_COLOR
                                           : (RageColor)LIGHT_DIFFUSE_COLOR);
    RageColor specular = (RageColor)LIGHT_AMBIENT_COLOR;
    DISPLAY->SetLightDirectional(
        0, ambient, diffuse, specular,
        RageVector3(LIGHTING_X, LIGHTING_Y, LIGHTING_Z));

    if (PREFSMAN->cel_shade_models_) {
      character_[p]->DrawCelShaded();

      DISPLAY->SetLightOff(0);
      DISPLAY->SetLighting(false);
      continue;
    }

    character_[p]->Draw();

    DISPLAY->SetLightOff(0);
    DISPLAY->SetLighting(false);
  }

  DISPLAY->CameraPopMatrix();
}

/*	2018 Jose Varela
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
