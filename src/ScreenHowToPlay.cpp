#include "ScreenHowToPlay.h"

#include <string>
#include <vector>

#include "ActorUtil.h"
#include "Character.h"
#include "CharacterManager.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "LifeMeterBar.h"
#include "LuaManager.h"
#include "Model.h"
#include "ModsGroup.h"
#include "NoteTypes.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenMessage.h"
#include "SongUtil.h"
#include "StatsManager.h"
#include "StdString.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "Tween.h"
#include "global.h"

static const ThemeMetric<int> NUM_W2S("ScreenHowToPlay", "NumW2s");
static const ThemeMetric<int> NUM_MISSES("ScreenHowToPlay", "NumMisses");
static const ThemeMetric<bool> USE_CHARACTER("ScreenHowToPlay", "UseCharacter");
static const ThemeMetric<bool> USE_PAD("ScreenHowToPlay", "UsePad");
static const ThemeMetric<bool> USE_PLAYER("ScreenHowToPlay", "UsePlayer");
static const ThemeMetric<std::string> CHARACTER_NAME(
    "ScreenHowToPlay", "CharacterName");

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

static const std::string anims[NUM_ANIMATIONS] = {
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

static bool HaveAllCharAnimations() {
  for (int i = ANIM_UP; i < NUM_ANIMATIONS; ++i) {
    if (!DoesFileExist(GetAnimPath((Animation)i))) {
      return false;
    }
  }
  return true;
}

REGISTER_SCREEN_CLASS(ScreenHowToPlay);
ScreenHowToPlay::ScreenHowToPlay() {
  w2s_ = 0;
  num_w2s_ = NUM_W2S;

  // initialize these because they might not be used.
  life_meter_bar_ = nullptr;
  character_ = nullptr;
  dance_pad_ = nullptr;
}

void ScreenHowToPlay::Init() {
  ScreenAttract::Init();

  if ((bool)USE_PAD && DoesFileExist(GetAnimPath(ANIM_DANCE_PAD))) {
    dance_pad_ = new Model;
    dance_pad_->SetName("Pad");
    dance_pad_->LoadMilkshapeAscii(GetAnimPath(ANIM_DANCE_PAD));
    // xxx: hardcoded rotation. can be undone, but still. -freem
    dance_pad_->SetRotationX(35);
    ActorUtil::LoadAllCommandsAndSetXY(dance_pad_, name_);
  }

  // Display a character
  std::vector<Character*> vpCharacters;
  CHARMAN->GetCharacters(vpCharacters);
  if ((bool)USE_CHARACTER && vpCharacters.size() && HaveAllCharAnimations()) {
    Character* displayChar;
    if (!CHARACTER_NAME.GetValue().empty() &&
        CHARMAN->GetCharacterFromID(CHARACTER_NAME.GetValue())) {
      displayChar = CHARMAN->GetCharacterFromID(CHARACTER_NAME.GetValue());
    } else {
      displayChar = CHARMAN->GetRandomCharacter();
    }

    std::string sModelPath = displayChar->GetModelPath();
    if (sModelPath != "") {
      character_ = new Model;
      character_->SetName("Character");
      character_->LoadMilkshapeAscii(displayChar->GetModelPath());
      character_->LoadMilkshapeAsciiBones("Step-LEFT", GetAnimPath(ANIM_LEFT));
      character_->LoadMilkshapeAsciiBones("Step-DOWN", GetAnimPath(ANIM_DOWN));
      character_->LoadMilkshapeAsciiBones("Step-UP", GetAnimPath(ANIM_UP));
      character_->LoadMilkshapeAsciiBones(
          "Step-RIGHT", GetAnimPath(ANIM_RIGHT));
      character_->LoadMilkshapeAsciiBones(
          "Step-JUMPLR", GetAnimPath(ANIM_JUMPLR));
      std::string sRestFile = displayChar->GetRestAnimationPath();
      ASSERT(!sRestFile.empty());
      character_->LoadMilkshapeAsciiBones(
          "rest", displayChar->GetRestAnimationPath());
      character_->SetDefaultAnimation("rest");
      character_->PlayAnimation(
          "rest");  // Stay bouncing after a step has finished animating.

      // xxx: hardcoded rotation. can be undone, but still. -freem
      character_->SetRotationX(40);
      character_->SetCullMode(CULL_NONE);  // many of the models floating around
                                           // have the vertex order flipped
      character_->RunCommands(displayChar->init_);  // run InitCommand from file
      ActorUtil::LoadAllCommandsAndSetXY(character_, name_);
    }
  }

  GAMESTATE->SetCurrentStyle(
      GAMEMAN->GetHowToPlayStyleForGame(GAMESTATE->cur_game_), PLAYER_INVALID);

  if (USE_PLAYER) {
    GAMESTATE->SetMasterPlayerNumber(PLAYER_1);

    life_meter_bar_ = new LifeMeterBar;
    life_meter_bar_->Load(
        GAMESTATE->player_state_[PLAYER_1],
        &STATSMAN->cur_stage_stats_.player_[PLAYER_1]);
    life_meter_bar_->SetName("LifeMeterBar");
    ActorUtil::LoadAllCommandsAndSetXY(life_meter_bar_, name_);
    life_meter_bar_->FillForHowToPlay(NUM_W2S, NUM_MISSES);

    // Allow themers to use either a .ssc or .sm file for this. -aj
    std::string sStepsPath = THEME->GetPathO(name_, "steps");
    SSCLoader loaderSSC;
    SMLoader loaderSM;
    if (Right(sStepsPath, 4) == ".ssc") {
      loaderSSC.LoadFromSimfile(sStepsPath, song_);
    } else {
      loaderSM.LoadFromSimfile(sStepsPath, song_);
    }
    song_.AddAutoGenNotes();

    const Style* pStyle = GAMESTATE->GetCurrentStyle(PLAYER_INVALID);

    Steps* pSteps = SongUtil::GetClosestNotes(
        &song_, pStyle->steps_type_, Difficulty_Beginner);
    if (pSteps == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "No playable steps of StepsType '%s' for ScreenHowToPlay in file %s",
          StringConversion::ToString(pStyle->steps_type_).c_str(),
          sStepsPath.c_str());
    } else {
      song_.song_timing_.TidyUpData(false);
      pSteps->timing_.TidyUpData(true);
      NoteData tempNoteData;
      pSteps->GetNoteData(tempNoteData);
      pStyle->GetTransformedNoteDataForStyle(
          PLAYER_1, tempNoteData, note_data_);

      GAMESTATE->cur_song_.Set(&song_);
      GAMESTATE->cur_steps_[PLAYER_1].Set(pSteps);
      GAMESTATE->gameplay_lead_in_.Set(false);
      GAMESTATE->player_state_[PLAYER_1]->player_controller_ = PC_AUTOPLAY;

      player_->Init(
          "Player", GAMESTATE->player_state_[PLAYER_1], nullptr,
          life_meter_bar_, nullptr, nullptr, nullptr, nullptr, nullptr);
      player_.Load(note_data_);
      player_->SetName("Player");
      this->AddChild(player_);
      ActorUtil::LoadAllCommandsAndSetXY(player_, name_);

      // Don't show judgment
      PO_GROUP_ASSIGN(
          GAMESTATE->player_state_[PLAYER_1]->player_options_, ModsLevel_Stage,
          blind_, 1.0f);
      GAMESTATE->demonstration_or_jukebox_ = true;
    }
  }

  // deferred until after the player, so the notes go under it
  if (life_meter_bar_) {
    this->AddChild(life_meter_bar_);
  }
  if (dance_pad_) {
    this->AddChild(dance_pad_);
  }
  if (character_) {
    this->AddChild(character_);
  }

  fake_seconds_into_song_ = 0;

  this->MoveToTail(&in_);
  this->MoveToTail(&out_);
}

ScreenHowToPlay::~ScreenHowToPlay() {
  delete life_meter_bar_;
  delete character_;
  delete dance_pad_;
}

void ScreenHowToPlay::Step() {
// xxx: assumes dance. -freem
#define ST_LEFT 0x01
#define ST_DOWN 0x02
#define ST_UP 0x04
#define ST_RIGHT 0x08
#define ST_JUMPLR (ST_LEFT | ST_RIGHT)
#define ST_JUMPUD (ST_UP | ST_DOWN)

  int iStep = 0;
  const int iNoteRow =
      BeatToNoteRowNotRounded(GAMESTATE->position_.song_beat_ + 0.6f);
  // if we want to miss from here on out, don't process steps.
  if (w2s_ < num_w2s_ && note_data_.IsThereATapAtRow(iNoteRow)) {
    const int iNumTracks = note_data_.GetNumTracks();
    for (int k = 0; k < iNumTracks; k++) {
      if (note_data_.GetTapNote(k, iNoteRow).type_ == TapNoteType_Tap) {
        iStep |= 1 << k;
      }
    }

    switch (iStep) {
      case ST_LEFT:
        character_->PlayAnimation("Step-LEFT", 1.8f);
        break;
      case ST_RIGHT:
        character_->PlayAnimation("Step-RIGHT", 1.8f);
        break;
      case ST_UP:
        character_->PlayAnimation("Step-UP", 1.8f);
        break;
      case ST_DOWN:
        character_->PlayAnimation("Step-DOWN", 1.8f);
        break;
      case ST_JUMPLR:
        character_->PlayAnimation("Step-JUMPLR", 1.8f);
        break;
      case ST_JUMPUD:
        // Until I can get an UP+DOWN jump animation, this will have to do.
        character_->PlayAnimation("Step-JUMPLR", 1.8f);

        character_->StopTweening();
        character_->BeginTweening(
            GAMESTATE->position_.cur_bps_ / 8, TWEEN_LINEAR);
        character_->SetRotationY(90);
        character_->BeginTweening(
            (1 / (GAMESTATE->position_.cur_bps_ *
                  2)));  // sleep between jump-frames
        character_->BeginTweening(
            GAMESTATE->position_.cur_bps_ / 6, TWEEN_LINEAR);
        character_->SetRotationY(0);
        break;
    }
  }
}

void ScreenHowToPlay::Update(float fDelta) {
  if (GAMESTATE->cur_song_ != nullptr) {
    RageTimer tm;
    GAMESTATE->UpdateSongPosition(
        fake_seconds_into_song_, GAMESTATE->cur_song_->song_timing_, tm);
    fake_seconds_into_song_ += fDelta;

    static int iLastNoteRowCounted = 0;
    int iCurNoteRow = BeatToNoteRowNotRounded(GAMESTATE->position_.song_beat_);

    if (iCurNoteRow != iLastNoteRowCounted &&
        note_data_.IsThereATapAtRow(iCurNoteRow)) {
      if (life_meter_bar_ && !player_) {
        if (w2s_ < num_w2s_) {
          life_meter_bar_->ChangeLife(TNS_W2);
        } else {
          life_meter_bar_->ChangeLife(TNS_Miss);
        }
      }
      w2s_++;
      iLastNoteRowCounted = iCurNoteRow;
    }

    // Once we hit the number of perfects we want, we want to fail. Switch
    // the controller to HUMAN. Since we aren't taking input, the steps will
    // always be misses.
    if (w2s_ > num_w2s_) {
      GAMESTATE->player_state_[PLAYER_1]->player_controller_ = PC_HUMAN;
    }

    // Per the above code, we don't always want the character stepping.
    // If they try to make all of the steps in the miss part, they look
    // silly. Have then stand still instead. - freem
    if (character_) {
      if (w2s_ <= num_w2s_) {
        Step();
      }
    }
  }

  ScreenAttract::Update(fDelta);
}

void ScreenHowToPlay::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_GainFocus) {
    // We do this ourself.
    SOUND->HandleSongTimer(false);
  } else if (SM == SM_LoseFocus) {
    SOUND->HandleSongTimer(true);
  } else if (SM == SM_GoToNextScreen) {
    GAMESTATE->Reset();
  }
  ScreenAttract::HandleScreenMessage(SM);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenHowToPlay. */
class LunaScreenHowToPlay : public Luna<ScreenHowToPlay> {
 public:
  static int GetLifeMeter(T* p, lua_State* L) {
    // PlayerNumber pn = Enum::Check<PlayerNumber>( L, 1 );

    p->life_meter_bar_->PushSelf(L);
    return 1;
  }

  LunaScreenHowToPlay() { ADD_METHOD(GetLifeMeter); }
};

LUA_REGISTER_DERIVED_CLASS(ScreenHowToPlay, ScreenAttract)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Tracy Ward
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
