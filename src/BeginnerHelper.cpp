#include "global.h"

#include "BeginnerHelper.h"

#include "ActorUtil.h"
#include "GameState.h"
#include "Model.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "ScreenDimensions.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"

// "PLAYER_X" offsets are relative to the pad.
// ex: Setting this to 10, and the HELPER to 300, will put the dancer at 310.
#define PLAYER_X(px) \
  THEME->GetMetricF("BeginnerHelper", ssprintf("Player%dX", px + 1))

// "HELPER" offsets effect the pad/dancer as a whole.
// Their relative Y cooridinates are hard-coded for each other.
#define HELPER_X THEME->GetMetricF("BeginnerHelper", "HelperX")
#define HELPER_Y THEME->GetMetricF("BeginnerHelper", "HelperY")

#define ST_LEFT 0x01
#define ST_DOWN 0x02
#define ST_UP 0x04
#define ST_RIGHT 0x08
#define ST_JUMPLR (ST_LEFT | ST_RIGHT)
#define ST_JUMPUD (ST_UP | ST_DOWN)

enum Animation {
  ANIM_DANCE_PAD,
  ANIM_DANCE_PADS,
  ANIM_UP,
  ANIM_DOWN,
  ANIM_LEFT,
  ANIM_RIGHT,
  ANIM_JUMPLR,
  NUM_ANIMATIONS
};

static const char* anims[NUM_ANIMATIONS] = {
    "DancePad.txt",
    "DancePads.txt",
    "BeginnerHelper_step-up.bones.txt",
    "BeginnerHelper_step-down.bones.txt",
    "BeginnerHelper_step-left.bones.txt",
    "BeginnerHelper_step-right.bones.txt",
    "BeginnerHelper_step-jumplr.bones.txt"};

static RString GetAnimPath(Animation a) {
  return RString("Characters/") + anims[a];
}

BeginnerHelper::BeginnerHelper() {
  m_bShowBackground = true;
  initialized_ = false;
  last_row_checked_ = 0;
  last_row_flashed_ = 0;
  FOREACH_PlayerNumber(pn) {
    players_enabled_[pn] = false;
  }
  FOREACH_PlayerNumber(pn) {
    dancers_[pn] = new Model;
  }
  dance_pad_ = new Model;
}

BeginnerHelper::~BeginnerHelper() {
  FOREACH_PlayerNumber(pn) { delete dancers_[pn]; }
  delete dance_pad_;
}

bool BeginnerHelper::Init(int dance_pad_type) {
  ASSERT(!initialized_);
  if (!CanUse(PLAYER_INVALID)) {
    return false;
  }

  // If no players were successfully added, bail.
  {
    bool any_loaded = false;
    for (int pn = 0; pn < NUM_PLAYERS; ++pn) {
      if (players_enabled_[pn]) {
        any_loaded = true;
      }
    }

    if (!any_loaded) {
      return false;
    }
  }

  // Load the Background and flash. Flash only shows if the BG does.
  if (m_bShowBackground) {
    background_.Load(THEME->GetPathG("BeginnerHelper", "background"));
    this->AddChild(background_);

    flash_.Load(THEME->GetPathG("BeginnerHelper", "flash"));
    flash_.SetXY(0, 0);
    flash_.SetDiffuseAlpha(0);
  }

  // Load StepCircle graphics
  for (int pn = 0; pn < NUM_PLAYERS; ++pn) {
    for (int step_circle = 0; step_circle < 4; ++step_circle) {
      step_circles_[pn][step_circle].Load(
          THEME->GetPathG("BeginnerHelper", "stepcircle"));
      // Hide until needed.
      step_circles_[pn][step_circle].SetZoom(0);
      this->AddChild(&step_circles_[pn][step_circle]);

      // Set StepCircle coordinates
      switch (step_circle) {
        case 0:
          step_circles_[pn][step_circle].SetXY(
              (HELPER_X + PLAYER_X(pn) - 80), HELPER_Y);
          break;  // Left
        case 1:
          step_circles_[pn][step_circle].SetXY(
              (HELPER_X + PLAYER_X(pn) + 80), HELPER_Y);
          break;  // Right
        case 2:
          step_circles_[pn][step_circle].SetXY(
              (HELPER_X + PLAYER_X(pn)), (HELPER_Y - 60));
          break;  // Up
        case 3:
          step_circles_[pn][step_circle].SetXY(
              (HELPER_X + PLAYER_X(pn)), (HELPER_Y + 60));
          break;  // Down
      }
    }
  }

  SHOW_DANCE_PAD.Load("BeginnerHelper", "ShowDancePad");
  // Load the DancePad
  if (SHOW_DANCE_PAD) {
    switch (dance_pad_type) {
      case 0:
        break;  // No pad
      case 1:
        dance_pad_->LoadMilkshapeAscii(GetAnimPath(ANIM_DANCE_PAD));
        break;
      case 2:
        dance_pad_->LoadMilkshapeAscii(GetAnimPath(ANIM_DANCE_PADS));
        break;
    }

    dance_pad_->SetName("DancePad");
    dance_pad_->SetX(HELPER_X);
    dance_pad_->SetY(HELPER_Y);
    ActorUtil::LoadAllCommands(dance_pad_, "BeginnerHelper");
  }

  // Load players
  for (int pn = 0; pn < NUM_PLAYERS; ++pn) {
    // Skip if not enabled
    if (!players_enabled_[pn]) {
      continue;
    }

    // Load character data
    const Character* Character = GAMESTATE->m_pCurCharacters[pn];
    ASSERT(Character != nullptr);

    dancers_[pn]->SetName(ssprintf("PlayerP%d", pn + 1));

    // Load textures
    dancers_[pn]->LoadMilkshapeAscii(Character->GetModelPath());

    // Load needed animations
    dancers_[pn]->LoadMilkshapeAsciiBones("Step-LEFT", GetAnimPath(ANIM_LEFT));
    dancers_[pn]->LoadMilkshapeAsciiBones("Step-DOWN", GetAnimPath(ANIM_DOWN));
    dancers_[pn]->LoadMilkshapeAsciiBones("Step-UP", GetAnimPath(ANIM_UP));
    dancers_[pn]->LoadMilkshapeAsciiBones(
        "Step-RIGHT", GetAnimPath(ANIM_RIGHT));
    dancers_[pn]->LoadMilkshapeAsciiBones(
        "Step-JUMPLR", GetAnimPath(ANIM_JUMPLR));
    dancers_[pn]->LoadMilkshapeAsciiBones(
        "rest", Character->GetRestAnimationPath());
    dancers_[pn]->SetDefaultAnimation(
        "rest");  // Stay bouncing after a step has finished animating
    dancers_[pn]->PlayAnimation("rest");
    dancers_[pn]->SetX(HELPER_X + PLAYER_X(pn));
    dancers_[pn]->SetY(HELPER_Y + 10);
    ActorUtil::LoadAllCommandsAndOnCommand(dancers_[pn], "BeginnerHelper");
    // many of the models floating around have the vertex order flipped, so
    // force this.
    dancers_[pn]->SetCullMode(CULL_NONE);
  }

  initialized_ = true;
  return true;
}

void BeginnerHelper::ShowStepCircle(PlayerNumber pn, int step) {
  // Save OR issues within array boundries.. it's worth the extra few bytes of
  // memory.
  int step_circle = 0;
  switch (step) {
    case ST_LEFT:
      step_circle = 0;
      break;
    case ST_RIGHT:
      step_circle = 1;
      break;
    case ST_UP:
      step_circle = 2;
      break;
    case ST_DOWN:
      step_circle = 3;
      break;
  }

  step_circles_[pn][step_circle].StopEffect();
  step_circles_[pn][step_circle].SetZoom(2);
  step_circles_[pn][step_circle].StopTweening();
  step_circles_[pn][step_circle].BeginTweening(
      GAMESTATE->m_Position.m_fCurBPS / 3, TWEEN_LINEAR);
  step_circles_[pn][step_circle].SetZoom(0);
}

void BeginnerHelper::AddPlayer(PlayerNumber pn, const NoteData& note_data) {
  ASSERT(!initialized_);
  ASSERT(pn >= 0 && pn < NUM_PLAYERS);
  ASSERT(GAMESTATE->IsHumanPlayer(pn));

  if (!CanUse(pn)) {
    return;
  }

  const Character* Character = GAMESTATE->m_pCurCharacters[pn];
  ASSERT(Character != nullptr);
  if (!DoesFileExist(Character->GetModelPath())) {
    return;
  }

  note_data_[pn].CopyAll(note_data);
  players_enabled_[pn] = true;
}

bool BeginnerHelper::CanUse(PlayerNumber pn) {
  for (int i = 0; i < NUM_ANIMATIONS; ++i) {
    if (!DoesFileExist(GetAnimPath((Animation)i))) {
      return false;
    }
  }

  // NOTE(Kyz): This does not pass PLAYER_INVALID to GetCurrentStyle because
  // that would only check the first non-nullptr style. Both styles need to be
  // checked.
  if (pn == PLAYER_INVALID) {
    return GAMESTATE->GetCurrentStyle(PLAYER_1)->m_bCanUseBeginnerHelper ||
           GAMESTATE->GetCurrentStyle(PLAYER_2)->m_bCanUseBeginnerHelper;
  }
  return GAMESTATE->GetCurrentStyle(pn)->m_bCanUseBeginnerHelper;
}

void BeginnerHelper::DrawPrimitives() {
  // If not initialized, don't bother with this
  if (!initialized_) {
    return;
  }

  ActorFrame::DrawPrimitives();
  flash_.Draw();

  bool draw_cell_shaded = PREFSMAN->m_bCelShadeModels;
  // Draw Pad
  if (SHOW_DANCE_PAD) {
    if (draw_cell_shaded) {
      dance_pad_->DrawCelShaded();
    } else {
      DISPLAY->SetLighting(true);
      DISPLAY->SetLightDirectional(
          0, RageColor(0.5f, 0.5f, 0.5f, 1), RageColor(1, 1, 1, 1),
          RageColor(0, 0, 0, 1), RageVector3(0, 0, 1));

      dance_pad_->Draw();
      DISPLAY
          ->ClearZBuffer();  // So character doesn't step "into" the dance pad.
      DISPLAY->SetLightOff(0);
      DISPLAY->SetLighting(false);
    }
  }

  // Draw StepCircles
  for (int pn = 0; pn < NUM_PLAYERS; ++pn) {
    for (int step_circle = 0; step_circle < 4; ++step_circle) {
      step_circles_[pn][step_circle].Draw();
    }
  }

  // Draw Dancers
  if (draw_cell_shaded) {
    FOREACH_PlayerNumber(pn) {
      if (GAMESTATE->IsHumanPlayer(pn)) {
        dancers_[pn] ->DrawCelShaded();
      }
    }
  } else {
    DISPLAY->SetLighting(true);
    DISPLAY->SetLightDirectional(
        0, RageColor(0.5f, 0.5f, 0.5f, 1), RageColor(1, 1, 1, 1),
        RageColor(0, 0, 0, 1), RageVector3(0, 0, 1));

    FOREACH_PlayerNumber(pn) {
      if (GAMESTATE->IsHumanPlayer(pn)) {
        dancers_[pn]->Draw();
      }
    }

    DISPLAY->SetLightOff(0);
    DISPLAY->SetLighting(false);
  }
}

void BeginnerHelper::Step(PlayerNumber pn, int step) {
  dancers_[pn]->StopTweening();
  // Make sure we're not still inside of a JUMPUD tween.
  dancers_[pn]->SetRotationY(0);

  switch (step) {
    case ST_LEFT:
      ShowStepCircle(pn, ST_LEFT);
      dancers_[pn]->PlayAnimation("Step-LEFT", 1.5f);
      break;
    case ST_RIGHT:
      ShowStepCircle(pn, ST_RIGHT);
      dancers_[pn]->PlayAnimation("Step-RIGHT", 1.5f);
      break;
    case ST_UP:
      ShowStepCircle(pn, ST_UP);
      dancers_[pn]->PlayAnimation("Step-UP", 1.5f);
      break;
    case ST_DOWN:
      ShowStepCircle(pn, ST_DOWN);
      dancers_[pn]->PlayAnimation("Step-DOWN", 1.5f);
      break;
    case ST_JUMPLR:
      ShowStepCircle(pn, ST_LEFT);
      ShowStepCircle(pn, ST_RIGHT);
      dancers_[pn]->PlayAnimation("Step-JUMPLR", 1.5f);
      break;
    case ST_JUMPUD:
      ShowStepCircle(pn, ST_UP);
      ShowStepCircle(pn, ST_DOWN);
      dancers_[pn]->StopTweening();
      dancers_[pn]->PlayAnimation("Step-JUMPLR", 1.5f);
      dancers_[pn]->BeginTweening(
          GAMESTATE->m_Position.m_fCurBPS / 8, TWEEN_LINEAR);
      dancers_[pn]->SetRotationY(90);
      // sleep between jump-frames
      dancers_[pn]->BeginTweening(
          1 / (GAMESTATE->m_Position.m_fCurBPS * 2));
      dancers_[pn]->BeginTweening(
          GAMESTATE->m_Position.m_fCurBPS / 6, TWEEN_LINEAR);
      dancers_[pn]->SetRotationY(0);
      break;
  }

  flash_.StopEffect();
  flash_.StopTweening();
  flash_.Sleep(GAMESTATE->m_Position.m_fCurBPS / 16);
  flash_.SetDiffuseAlpha(1);
  flash_.BeginTweening(1 / GAMESTATE->m_Position.m_fCurBPS * 0.5f);
  flash_.SetDiffuseAlpha(0);
}

void BeginnerHelper::Update(float delta) {
  if (!initialized_) {
    return;
  }

  // the row we want to check on this update
  int cur_row =
      BeatToNoteRowNotRounded(GAMESTATE->m_Position.m_fSongBeat + 0.4f);
  FOREACH_EnabledPlayer(pn) {
    for (int row = last_row_checked_; row < cur_row; ++row) {
      // Check if there are any notes at all on this row. If not, save scanning.
      if (!note_data_[pn].IsThereATapAtRow(row)) {
        continue;
      }

      // Find all steps on this row, in order to show the correct animations
      int step = 0;
      const int num_tracks = note_data_[pn].GetNumTracks();
      for (int track = 0; track < num_tracks; ++track) {
        if (note_data_[pn].GetTapNote(track, row).type == TapNoteType_Tap) {
          step |= 1 << track;
        }
      }

      // Assign new data
      this->Step(pn, step);
    }
  }

  // Make sure we don't accidentally scan a row 2x
  last_row_checked_ = cur_row;

  // Update animations
  ActorFrame::Update(delta);
  dance_pad_->Update(delta);
  flash_.Update(delta);

  float beat = delta * GAMESTATE->m_Position.m_fCurBPS;
  // If this is not a human player, the dancer is not shown
  FOREACH_HumanPlayer(pu) {
    // Update dancer's animation and StepCircles
    dancers_[pu]->Update(beat);
    for (int pn = 0; pn < NUM_PLAYERS; pn++) {
      for (int step_circle = 0; step_circle < 4; ++step_circle) {
        step_circles_[pn][step_circle].Update(beat);
      }
    }
  }
}

/*
 * (c) 2003 Kevin Slaughter, Tracy Ward
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
