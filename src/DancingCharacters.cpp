#include "global.h"

#include "DancingCharacters.h"

#include "Character.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "Model.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageMath.h"
#include "RageUtil.h"
#include "Song.h"
#include "StatsManager.h"

int Neg1OrPos1();

#define DC_X(choice) \
  THEME->GetMetricF( \
      "DancingCharacters", ssprintf("2DCharacterXP%d", choice + 1))
#define DC_Y(choice) \
  THEME->GetMetricF( \
      "DancingCharacters", ssprintf("2DCharacterYP%d", choice + 1))

// TODO(Colby):
// - Metrics/Lua for lighting and camera sweeping.
// - Ability to load secondary elements i.e. stages.
// - Remove support for 2D characters (Lua can do it).
// - Cleanup!
#define CAMERA_REST_DISTANCE \
	THEME->GetMetricF("DancingCamera", "RestDistance")
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
const ThemeMetric<RageColor> LIGHT_AFAILED_COLOR(
    "DancingCamera", "AmbientFailedColor");
const ThemeMetric<RageColor> LIGHT_DDANGER_COLOR(
    "DancingCamera", "DiffuseDangerColor");
const ThemeMetric<RageColor> LIGHT_DFAILED_COLOR(
    "DancingCamera", "DiffuseFailedColor");

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
    : m_bDrawDangerLight(false),
      m_CameraDistance(0),
      m_CameraPanYStart(0),
      m_CameraPanYEnd(0),
      m_fLookAtHeight(0),
      m_fCameraHeightStart(0),
      m_fCameraHeightEnd(0),
      m_fThisCameraStartBeat(0),
      m_fThisCameraEndBeat(0) {
  FOREACH_PlayerNumber(p) {
    m_pCharacter[p] = new Model;
    m_2DIdleTimer[p].SetZero();
		// Start on idle state.
    m_i2DAnimState[p] = AS2D_IDLE;
    if (!GAMESTATE->IsPlayerEnabled(p)) {
      continue;
    }

    Character* pChar = GAMESTATE->cur_characters_[p];
    if (!pChar) {
      continue;
    }

    // Load in any potential 2D stuff.
    RString sCharacterDirectory = pChar->character_dir_;
    RString sCurrentAnim;
    sCurrentAnim = sCharacterDirectory + "2DIdle";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgIdle[p].Load(sCurrentAnim);
      m_bgIdle[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DMiss";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgMiss[p].Load(sCurrentAnim);
      m_bgMiss[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DGood";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgGood[p].Load(sCurrentAnim);
      m_bgGood[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DGreat";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgGreat[p].Load(sCurrentAnim);
      m_bgGreat[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DFever";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgFever[p].Load(sCurrentAnim);
      m_bgFever[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DFail";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgFail[p].Load(sCurrentAnim);
      m_bgFail[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DWin";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgWin[p].Load(sCurrentAnim);
      m_bgWin[p]->SetXY(DC_X(p), DC_Y(p));
    }

    sCurrentAnim = sCharacterDirectory + "2DWinFever";
    // Check 2D Idle BGAnim exists.
    if (DoesFileExist(sCurrentAnim + "/BGAnimation.ini")) {
      m_bgWinFever[p].Load(sCurrentAnim);
      m_bgWinFever[p]->SetXY(DC_X(p), DC_Y(p));
    }

    if (pChar->GetModelPath().empty()) {
      continue;
    }

    if (GAMESTATE->GetNumPlayersEnabled() == 2) {
      m_pCharacter[p]->SetX(MODEL_X_TWO_PLAYERS[p]);
    } else {
      m_pCharacter[p]->SetX(MODEL_X_ONE_PLAYER);
    }

    switch (GAMESTATE->play_mode_) {
      case PLAY_MODE_BATTLE:
      case PLAY_MODE_RAVE:
        m_pCharacter[p]->SetRotationY(MODEL_ROTATIONY_TWO_PLAYERS[p]);
      default:
        break;
    }

    m_pCharacter[p]->LoadMilkshapeAscii(pChar->GetModelPath());
    m_pCharacter[p]->LoadMilkshapeAsciiBones(
        "rest", pChar->GetRestAnimationPath());
    m_pCharacter[p]->LoadMilkshapeAsciiBones(
        "warmup", pChar->GetWarmUpAnimationPath());
    m_pCharacter[p]->LoadMilkshapeAsciiBones(
        "dance", pChar->GetDanceAnimationPath());
    // Many of the models floating around have the vertex order flipped.
    m_pCharacter[p]->SetCullMode(CULL_NONE);

    m_pCharacter[p]->RunCommands(pChar->m_cmdInit);
  }
}

DancingCharacters::~DancingCharacters() {
  FOREACH_PlayerNumber(p) delete m_pCharacter[p];
}

void DancingCharacters::LoadNextSong() {
  // Initial camera sweep is still.
  m_CameraDistance = CAMERA_REST_DISTANCE;
  m_CameraPanYStart = 0;
  m_CameraPanYEnd = 0;
  m_fCameraHeightStart = CAMERA_REST_LOOK_AT_HEIGHT;
  m_fCameraHeightEnd = CAMERA_REST_LOOK_AT_HEIGHT;
  m_fLookAtHeight = CAMERA_REST_LOOK_AT_HEIGHT;
  m_fThisCameraStartBeat = 0;
  m_fThisCameraEndBeat = 0;

  ASSERT(GAMESTATE->cur_song_ != nullptr);
  m_fThisCameraEndBeat = GAMESTATE->cur_song_->GetFirstBeat();

  FOREACH_PlayerNumber(p) if (GAMESTATE->IsPlayerEnabled(p)) m_pCharacter[p]
      ->PlayAnimation("rest");
}

int Neg1OrPos1() { return RandomInt(2) ? -1 : +1; }

void DancingCharacters::Update(float fDelta) {
  if (GAMESTATE->position_.m_bFreeze || GAMESTATE->position_.m_bDelay) {
    // Spin the camera Matrix-style.
    m_CameraPanYStart += fDelta * 40;
    m_CameraPanYEnd += fDelta * 40;
  } else {
    // Make the characters move.
    float fBPM = GAMESTATE->position_.m_fCurBPS * 60;
    float fUpdateScale = SCALE(fBPM, 60.f, 300.f, 0.75f, 1.5f);
    CLAMP(fUpdateScale, 0.75f, 1.5f);

    // It's OK for the animation to go slower than natural when we're
    // at a very low music rate.
    fUpdateScale *= GAMESTATE->song_options_.GetCurrent().m_fMusicRate;

    FOREACH_PlayerNumber(p) {
      if (GAMESTATE->IsPlayerEnabled(p)) {
        m_pCharacter[p]->Update(fDelta * fUpdateScale);
      }
    }
  }

  static bool bWasGameplayStarting = false;
  bool bGameplayStarting = GAMESTATE->gameplay_lead_in_;
  if (!bWasGameplayStarting && bGameplayStarting) {
    FOREACH_PlayerNumber(p) if (GAMESTATE->IsPlayerEnabled(p)) m_pCharacter[p]
        ->PlayAnimation("warmup");
  }
  bWasGameplayStarting = bGameplayStarting;

  static float fLastBeat = GAMESTATE->position_.m_fSongBeat;
  float firstBeat = GAMESTATE->cur_song_->GetFirstBeat();
  float fThisBeat = GAMESTATE->position_.m_fSongBeat;
  if (fLastBeat < firstBeat && fThisBeat >= firstBeat) {
    FOREACH_PlayerNumber(p) m_pCharacter[p]->PlayAnimation("dance");
  }
  fLastBeat = fThisBeat;

  // Time for a new sweep?
  if (GAMESTATE->position_.m_fSongBeat > m_fThisCameraEndBeat) {
    if (RandomInt(6) >= 4) {
      // Sweeping camera.
      m_CameraDistance = CAMERA_SWEEP_DISTANCE +
                         RandomInt(-1, 1) * CAMERA_SWEEP_DISTANCE_VARIANCE;
      m_CameraPanYStart = m_CameraPanYEnd =
          RandomInt(-1, 1) * CAMERA_SWEEP_PAN_Y_RANGE_DEGREES;
      m_fCameraHeightStart = m_fCameraHeightEnd = CAMERA_STILL_LOOK_AT_HEIGHT;

      m_CameraPanYEnd += RandomInt(-1, 1) * CAMERA_SWEEP_PAN_Y_VARIANCE_DEGREES;
      m_fCameraHeightStart = m_fCameraHeightEnd =
          m_fCameraHeightStart +
          RandomInt(-1, 1) * CAMERA_SWEEP_HEIGHT_VARIANCE;

      float fCameraHeightVariance =
          RandomInt(-1, 1) * CAMERA_SWEEP_HEIGHT_VARIANCE;
      m_fCameraHeightStart -= fCameraHeightVariance;
      m_fCameraHeightEnd += fCameraHeightVariance;

      m_fLookAtHeight = CAMERA_SWEEP_LOOK_AT_HEIGHT;
    } else {
      // Still camera.
      m_CameraDistance = CAMERA_STILL_DISTANCE +
                         RandomInt(-1, 1) * CAMERA_STILL_DISTANCE_VARIANCE;
      m_CameraPanYStart = m_CameraPanYEnd =
          Neg1OrPos1() * CAMERA_STILL_PAN_Y_RANGE_DEGREES;
      m_fCameraHeightStart = m_fCameraHeightEnd =
          CAMERA_SWEEP_LOOK_AT_HEIGHT +
          Neg1OrPos1() * CAMERA_STILL_HEIGHT_VARIANCE;

      m_fLookAtHeight = CAMERA_STILL_LOOK_AT_HEIGHT;
    }

    int iCurBeat = (int)GAMESTATE->position_.m_fSongBeat;
    // Need to convert it to a int, otherwise it complains.
    int NewBeatToSet = (int)AMM_BEATS_TIL_NEXTCAMERA;
    iCurBeat -= iCurBeat % NewBeatToSet;

    m_fThisCameraStartBeat = (float)iCurBeat;
    m_fThisCameraEndBeat = float(iCurBeat + AMM_BEATS_TIL_NEXTCAMERA);
  }
}

void DancingCharacters::Change2DAnimState(PlayerNumber pn, int iState) {
  ASSERT(pn < NUM_PLAYERS);
  ASSERT(iState < AS2D_MAXSTATES);

  m_i2DAnimState[pn] = iState;
}

void DancingCharacters::DrawPrimitives() {
  DISPLAY->CameraPushMatrix();

  float fPercentIntoSweep;
  if (m_fThisCameraStartBeat == m_fThisCameraEndBeat) {
    fPercentIntoSweep = 0;
  } else {
    fPercentIntoSweep = SCALE(
        GAMESTATE->position_.m_fSongBeat, m_fThisCameraStartBeat,
        m_fThisCameraEndBeat, 0.f, 1.f);
  }
  float fCameraPanY =
      SCALE(fPercentIntoSweep, 0.f, 1.f, m_CameraPanYStart, m_CameraPanYEnd);
  float fCameraHeight = SCALE(
      fPercentIntoSweep, 0.f, 1.f, m_fCameraHeightStart, m_fCameraHeightEnd);

  RageVector3 m_CameraPoint(0, fCameraHeight, -m_CameraDistance);
  RageMatrix CameraRot;
  RageMatrixRotationY(&CameraRot, fCameraPanY);
  RageVec3TransformCoord(&m_CameraPoint, &m_CameraPoint, &CameraRot);

  RageVector3 m_LookAt(0, m_fLookAtHeight, 0);

  DISPLAY->LoadLookAt(CAM_FOV, m_CameraPoint, m_LookAt, RageVector3(0, 1, 0));

  FOREACH_EnabledPlayer(p) {
    bool bFailed = STATSMAN->m_CurStageStats.m_player[p].m_bFailed;
    bool bDanger = m_bDrawDangerLight;

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

    if (PREFSMAN->m_bCelShadeModels) {
      m_pCharacter[p]->DrawCelShaded();

      DISPLAY->SetLightOff(0);
      DISPLAY->SetLighting(false);
      continue;
    }

    m_pCharacter[p]->Draw();

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
