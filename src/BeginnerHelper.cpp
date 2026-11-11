#include "BeginnerHelper.h"

#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "Character.h"
#include "GameState.h"
#include "Model.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "Tween.h"
#include "global.h"

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

static std::string GetAnimPath(Animation a) {
  return std::string("Characters/") + anims[a];
}

BeginnerHelper::BeginnerHelper() {
  show_background_ = true;
  initialized_ = false;
  last_row_checked_ = 0;
  FOREACH_PlayerNumber(pn) player_enabled_[pn] = false;
  FOREACH_PlayerNumber(pn) dancer_[pn] = new Model;
  dance_pad_ = new Model;
}

BeginnerHelper::~BeginnerHelper() {
  FOREACH_PlayerNumber(pn) delete dancer_[pn];
  delete dance_pad_;
}

bool BeginnerHelper::Init(int dance_pad_type) {
  ASSERT(!initialized_);
  if (!CanUse(kPlayerInvalid)) {
    return false;
  }

  // If no players were successfully added, bail.
  {
    bool any_loaded = false;
    for (int pn = 0; pn < kNumPlayers; pn++) {
      if (player_enabled_[pn]) {
        any_loaded = true;
      }
    }

    if (!any_loaded) {
      return false;
    }
  }

  // Load the Background and flash. Flash only shows if the BG does.
  if (show_background_) {
    background_.Load(THEME->GetPathG("BeginnerHelper", "background"));
    this->AddChild(background_);
    // m_sBackground.SetXY( 1, 1 );

    flash_.Load(THEME->GetPathG("BeginnerHelper", "flash"));
    flash_.SetXY(0, 0);
    flash_.SetDiffuseAlpha(0);
  }

  // Load StepCircle graphics
  for (int lsc = 0; lsc < kNumPlayers; lsc++) {
    for (int lsce = 0; lsce < 4; lsce++) {
      step_circle_[lsc][lsce].Load(
          THEME->GetPathG("BeginnerHelper", "stepcircle"));
      step_circle_[lsc][lsce].SetZoom(0);  // Hide until needed.
      this->AddChild(&step_circle_[lsc][lsce]);

      // Set StepCircle coordinates
      switch (lsce) {
        case 0:
          step_circle_[lsc][lsce].SetXY(
              (HELPER_X + PLAYER_X(lsc) - 80), HELPER_Y);
          break;  // Left
        case 1:
          step_circle_[lsc][lsce].SetXY(
              (HELPER_X + PLAYER_X(lsc) + 80), HELPER_Y);
          break;  // Right
        case 2:
          step_circle_[lsc][lsce].SetXY(
              (HELPER_X + PLAYER_X(lsc)), (HELPER_Y - 60));
          break;  // Up
        case 3:
          step_circle_[lsc][lsce].SetXY(
              (HELPER_X + PLAYER_X(lsc)), (HELPER_Y + 60));
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

  for (int pl = 0; pl < kNumPlayers; pl++)  // Load players
  {
    // Skip if not enabled
    if (!player_enabled_[pl]) {
      continue;
    }

    // Load character data
    const Character* character = GAMESTATE->cur_characters_[pl];
    ASSERT(character != nullptr);

    dancer_[pl]->SetName(ssprintf("PlayerP%d", pl + 1));

    // Load textures
    dancer_[pl]->LoadMilkshapeAscii(character->GetModelPath());

    // Load needed animations
    dancer_[pl]->LoadMilkshapeAsciiBones("Step-LEFT", GetAnimPath(ANIM_LEFT));
    dancer_[pl]->LoadMilkshapeAsciiBones("Step-DOWN", GetAnimPath(ANIM_DOWN));
    dancer_[pl]->LoadMilkshapeAsciiBones("Step-UP", GetAnimPath(ANIM_UP));
    dancer_[pl]->LoadMilkshapeAsciiBones("Step-RIGHT", GetAnimPath(ANIM_RIGHT));
    dancer_[pl]->LoadMilkshapeAsciiBones(
        "Step-JUMPLR", GetAnimPath(ANIM_JUMPLR));
    dancer_[pl]->LoadMilkshapeAsciiBones(
        "rest", character->GetRestAnimationPath());
    dancer_[pl]->SetDefaultAnimation(
        "rest");  // Stay bouncing after a step has finished animating
    dancer_[pl]->PlayAnimation("rest");
    dancer_[pl]->SetX(HELPER_X + PLAYER_X(pl));
    dancer_[pl]->SetY(HELPER_Y + 10);
    ActorUtil::LoadAllCommandsAndOnCommand(dancer_[pl], "BeginnerHelper");
    // many of the models floating around have the vertex order flipped, so
    // force this.
    dancer_[pl]->SetCullMode(CULL_NONE);
  }

  initialized_ = true;
  return true;
}

void BeginnerHelper::ShowStepCircle(PlayerNumber pn, int cstep) {
  int isc = 0;  // Save OR issues within array boundries.. it's worth the extra
                // few bytes of memory.
  switch (cstep) {
    case ST_LEFT:
      isc = 0;
      break;
    case ST_RIGHT:
      isc = 1;
      break;
    case ST_UP:
      isc = 2;
      break;
    case ST_DOWN:
      isc = 3;
      break;
  }

  step_circle_[pn][isc].StopEffect();
  step_circle_[pn][isc].SetZoom(2);
  step_circle_[pn][isc].StopTweening();
  step_circle_[pn][isc].BeginTweening(
      GAMESTATE->position_.cur_bps_ / 3, TWEEN_LINEAR);
  step_circle_[pn][isc].SetZoom(0);
}

void BeginnerHelper::AddPlayer(PlayerNumber pn, const NoteData& ns) {
  ASSERT(!initialized_);
  ASSERT(pn >= 0 && pn < kNumPlayers);
  ASSERT(GAMESTATE->IsHumanPlayer(pn));

  if (!CanUse(pn)) {
    return;
  }

  const Character* character = GAMESTATE->cur_characters_[pn];
  ASSERT(character != nullptr);
  if (!DoesFileExist(character->GetModelPath())) {
    return;
  }

  note_data_[pn].CopyAll(ns);
  player_enabled_[pn] = true;
}

bool BeginnerHelper::CanUse(PlayerNumber pn) {
  for (int i = 0; i < NUM_ANIMATIONS; ++i) {
    if (!DoesFileExist(GetAnimPath((Animation)i))) {
      return false;
    }
  }

  // This does not pass PLAYER_INVALID to GetCurrentStyle because that would
  // only check the first non-nullptr style.  Both styles need to be checked.
  // -Kyz
  if (pn == kPlayerInvalid) {
    return GAMESTATE->GetCurrentStyle(PLAYER_1)->can_use_beginner_helper_ ||
           GAMESTATE->GetCurrentStyle(PLAYER_2)->can_use_beginner_helper_;
  }
  return GAMESTATE->GetCurrentStyle(pn)->can_use_beginner_helper_;
}

void BeginnerHelper::DrawPrimitives() {
  // If not initialized, don't bother with this
  if (!initialized_) {
    return;
  }

  ActorFrame::DrawPrimitives();
  flash_.Draw();

  bool draw_cel_shaded = PREFSMAN->cel_shade_models_;
  // Draw Pad
  if (SHOW_DANCE_PAD) {
    if (draw_cel_shaded) {
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
  for (int scd = 0; scd < kNumPlayers; scd++) {
    for (int scde = 0; scde < 4; scde++) {
      step_circle_[scd][scde].Draw();
    }
  }

  // Draw Dancers
  if (draw_cel_shaded) {
    FOREACH_PlayerNumber(pn) if (GAMESTATE->IsHumanPlayer(pn)) dancer_[pn]
        ->DrawCelShaded();
  } else {
    DISPLAY->SetLighting(true);
    DISPLAY->SetLightDirectional(
        0, RageColor(0.5f, 0.5f, 0.5f, 1), RageColor(1, 1, 1, 1),
        RageColor(0, 0, 0, 1), RageVector3(0, 0, 1));

    FOREACH_PlayerNumber(pn) if (GAMESTATE->IsHumanPlayer(pn)) dancer_[pn]
        ->Draw();

    DISPLAY->SetLightOff(0);
    DISPLAY->SetLighting(false);
  }
}

void BeginnerHelper::Step(PlayerNumber pn, int cstep) {
  dancer_[pn]->StopTweening();
  dancer_[pn]->SetRotationY(
      0);  // Make sure we're not still inside of a JUMPUD tween.

  switch (cstep) {
    case ST_LEFT:
      ShowStepCircle(pn, ST_LEFT);
      dancer_[pn]->PlayAnimation("Step-LEFT", 1.5f);
      break;
    case ST_RIGHT:
      ShowStepCircle(pn, ST_RIGHT);
      dancer_[pn]->PlayAnimation("Step-RIGHT", 1.5f);
      break;
    case ST_UP:
      ShowStepCircle(pn, ST_UP);
      dancer_[pn]->PlayAnimation("Step-UP", 1.5f);
      break;
    case ST_DOWN:
      ShowStepCircle(pn, ST_DOWN);
      dancer_[pn]->PlayAnimation("Step-DOWN", 1.5f);
      break;
    case ST_JUMPLR:
      ShowStepCircle(pn, ST_LEFT);
      ShowStepCircle(pn, ST_RIGHT);
      dancer_[pn]->PlayAnimation("Step-JUMPLR", 1.5f);
      break;
    case ST_JUMPUD:
      ShowStepCircle(pn, ST_UP);
      ShowStepCircle(pn, ST_DOWN);
      dancer_[pn]->StopTweening();
      dancer_[pn]->PlayAnimation("Step-JUMPLR", 1.5f);
      dancer_[pn]->BeginTweening(
          GAMESTATE->position_.cur_bps_ / 8, TWEEN_LINEAR);
      dancer_[pn]->SetRotationY(90);
      dancer_[pn]->BeginTweening(
          1 /
          (GAMESTATE->position_.cur_bps_ * 2));  // sleep between jump-frames
      dancer_[pn]->BeginTweening(
          GAMESTATE->position_.cur_bps_ / 6, TWEEN_LINEAR);
      dancer_[pn]->SetRotationY(0);
      break;
  }

  flash_.StopEffect();
  flash_.StopTweening();
  flash_.Sleep(GAMESTATE->position_.cur_bps_ / 16);
  flash_.SetDiffuseAlpha(1);
  flash_.BeginTweening(1 / GAMESTATE->position_.cur_bps_ * 0.5f);
  flash_.SetDiffuseAlpha(0);
}

void BeginnerHelper::Update(float delta_time) {
  if (!initialized_) {
    return;
  }

  // the row we want to check on this update
  int cur_row = BeatToNoteRowNotRounded(GAMESTATE->position_.song_beat_ + 0.4f);
  FOREACH_EnabledPlayer(pn) {
    for (int row = last_row_checked_; row < cur_row; row++) {
      // Check if there are any notes at all on this row.. If not, save
      // scanning.
      if (!note_data_[pn].IsThereATapAtRow(row)) {
        continue;
      }

      // Find all steps on this row, in order to show the correct animations
      int step = 0;
      const int num_tracks = note_data_[pn].GetNumTracks();
      for (int t = 0; t < num_tracks; t++) {
        if (note_data_[pn].GetTapNote(t, row).type_ == TapNoteType_Tap) {
          step |= 1 << t;
        }
      }

      // Assign new data
      this->Step(pn, step);
    }
  }

  // Make sure we don't accidentally scan a row 2x
  last_row_checked_ = cur_row;

  // Update animations
  ActorFrame::Update(delta_time);
  dance_pad_->Update(delta_time);
  flash_.Update(delta_time);

  float beat = delta_time * GAMESTATE->position_.cur_bps_;
  // If this is not a human player, the dancer is not shown
  FOREACH_HumanPlayer(pu) {
    // Update dancer's animation and StepCircles
    dancer_[pu]->Update(beat);
    for (int scu = 0; scu < kNumPlayers; scu++) {
      for (int scue = 0; scue < 4; scue++) {
        step_circle_[scu][scue].Update(beat);
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
