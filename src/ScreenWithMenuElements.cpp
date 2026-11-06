#include "ScreenWithMenuElements.h"

#include <algorithm>
#include <string>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "AutoActor.h"
#include "GameConstantsAndTypes.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "LuaManager.h"
#include "MemoryCardDisplay.h"
#include "MenuTimer.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ThemeManager.h"
#include "global.h"

#define TIMER_STEALTH THEME->GetMetricB(name_, "TimerStealth")
#define SHOW_STAGE_DISPLAY THEME->GetMetricB(name_, "ShowStageDisplay")
#define MEMORY_CARD_ICONS THEME->GetMetricB(name_, "MemoryCardIcons")
#define FORCE_TIMER THEME->GetMetricB(name_, "ForceTimer")
#define STOP_MUSIC_ON_BACK THEME->GetMetricB(name_, "StopMusicOnBack")
#define WAIT_FOR_CHILDREN_BEFORE_TWEENING_OUT \
  THEME->GetMetricB(name_, "WaitForChildrenBeforeTweeningOut")

REGISTER_SCREEN_CLASS(ScreenWithMenuElements);
ScreenWithMenuElements::ScreenWithMenuElements() {
  menu_timer_ = nullptr;
  FOREACH_PlayerNumber(p) memory_card_display_[p] = nullptr;
  menu_timer_ = nullptr;
  should_allow_late_join_ = false;
}

void ScreenWithMenuElements::Init() {
  PLAY_MUSIC.Load(name_, "PlayMusic");
  MUSIC_ALIGN_BEAT.Load(name_, "MusicAlignBeat");
  DELAY_MUSIC_SECONDS.Load(name_, "DelayMusicSeconds");
  CANCEL_TRANSITIONS_OUT.Load(name_, "CancelTransitionsOut");
  TIMER_SECONDS.Load(name_, "TimerSeconds");
  TIMER_METRICS_GROUP.Load(name_, "TimerMetricsGroup");
  Screen::Init();

  ASSERT(this->sub_actors_.empty());  // don't call Init twice!

  if (MEMORY_CARD_ICONS) {
    FOREACH_PlayerNumber(p) {
      ASSERT(memory_card_display_[p] == nullptr);
      memory_card_display_[p] = new MemoryCardDisplay;
      memory_card_display_[p]->Load(p);
      memory_card_display_[p]->SetName(ssprintf("MemoryCardDisplayP%d", p + 1));
      LOAD_ALL_COMMANDS_AND_SET_XY(memory_card_display_[p]);
      this->AddChild(memory_card_display_[p]);
    }
  }

  if (TIMER_SECONDS != -1.0f) {
    ASSERT(menu_timer_ == nullptr);  // don't load twice
    menu_timer_ = new MenuTimer;
    menu_timer_->Load(TIMER_METRICS_GROUP.GetValue());
    menu_timer_->SetName("Timer");
    if (TIMER_STEALTH) {
      menu_timer_->EnableStealth(true);
    }
    LOAD_ALL_COMMANDS_AND_SET_XY(menu_timer_);
    ResetTimer();
    this->AddChild(menu_timer_);
  }

  spr_underlay_.Load(THEME->GetPathB(name_, "underlay"));
  spr_underlay_->SetName("Underlay");
  spr_underlay_->SetDrawOrder(DRAW_ORDER_UNDERLAY);
  this->AddChild(spr_underlay_);
  LOAD_ALL_COMMANDS(spr_underlay_);

  spr_overlay_.Load(THEME->GetPathB(name_, "overlay"));
  spr_overlay_->SetName("Overlay");
  spr_overlay_->SetDrawOrder(DRAW_ORDER_OVERLAY);
  this->AddChild(spr_overlay_);
  LOAD_ALL_COMMANDS(spr_overlay_);

  // decorations
  {
    AutoActor decorations;
    decorations.LoadB(name_, "decorations");
    decorations->SetName("Decorations");
    decorations->SetDrawOrder(DRAW_ORDER_DECORATIONS);
    ActorFrame* pFrame =
        dynamic_cast<ActorFrame*>(static_cast<Actor*>(decorations));
    if (pFrame) {
      decorations_ = pFrame->GetChildren();
      for (Actor* child : decorations_) {
        this->AddChild(child);
      }
      pFrame->RemoveAllChildren();
    }
  }

  in_.SetName("In");
  in_.Load(THEME->GetPathB(name_, "in"));
  in_.SetDrawOrder(DRAW_ORDER_TRANSITIONS);
  this->AddChild(&in_);
  LOAD_ALL_COMMANDS(in_);

  out_.SetName("Out");
  out_.Load(THEME->GetPathB(name_, "out"));
  out_.SetDrawOrder(DRAW_ORDER_TRANSITIONS);
  this->AddChild(&out_);
  LOAD_ALL_COMMANDS(out_);

  cancel_.SetName("Cancel");
  cancel_.Load(THEME->GetPathB(name_, "cancel"));
  cancel_.SetDrawOrder(DRAW_ORDER_TRANSITIONS);
  this->AddChild(&cancel_);
  LOAD_ALL_COMMANDS(cancel_);

  // Grab the music path here; don't GetPath during BeginScreen.
  if (PLAY_MUSIC) {
    path_to_music_ = THEME->GetPathS(name_, "music");
  }
}

void ScreenWithMenuElements::BeginScreen() {
  Screen::BeginScreen();

  in_.Reset();
  out_.Reset();
  cancel_.Reset();

  TweenOnScreen();

  this->SortByDrawOrder();
  in_.StartTransitioning(SM_DoneFadingIn);

  SOUND->PlayOnceFromAnnouncer(name_ + " intro");
  StartPlayingMusic();

  /* Evaluate FirstUpdateCommand. */
  this->PlayCommand("FirstUpdate");

  /* If AutoJoin and a player is already joined, then try to join a player.  (If
   * no players are joined, they'll join on the first JoinInput.) */
  if (GAMESTATE->GetCoinMode() == CoinMode_Pay && GAMESTATE->auto_join_.Get()) {
    if (GAMESTATE->GetNumSidesJoined() > 0 && GAMESTATE->JoinPlayers()) {
      SCREENMAN->PlayStartSound();
    }
  }
}

void ScreenWithMenuElements::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_MenuTimer) {
    FOREACH_HumanPlayer(p) {
      InputEventPlus iep;
      iep.pn_ = p;
      MenuStart(iep);
    }
  }

  Screen::HandleScreenMessage(SM);
}

ScreenWithMenuElements::~ScreenWithMenuElements() {
  RageUtil::SafeDelete(menu_timer_);
  FOREACH_PlayerNumber(p) {
    if (memory_card_display_[p] != nullptr) {
      RageUtil::SafeDelete(memory_card_display_[p]);
    }
  }
  for (Actor* actor : decorations_) {
    delete actor;
  }
}

std::string ScreenWithMenuElements::HandleLuaMusicFile(
    const std::string& path) {
  FileType ft = ActorUtil::GetFileType(path);
  std::string ret = path;
  if (ft == FT_Lua) {
    std::string script;
    std::string error = "Lua runtime error: ";
    if (GetFileContents(path, script)) {
      Lua* L = LUA->Get();
      if (!LuaHelpers::RunScript(L, script, "@" + path, error, 0, 1, true)) {
        ret = "";
      } else {
        // there are two possible ways to load a music file via Lua.
        // 1) return the path to the sound
        // (themer has to use THEME:GetPathS())
        std::string music_path_from_lua;
        LuaHelpers::Pop(L, music_path_from_lua);
        if (!music_path_from_lua.empty()) {
          ret = music_path_from_lua;
        } else {
          // 2) perhaps it's a table with some params? unsure if I want to
          // support this just yet. -aj
          LOG->Trace("Lua music script did not return a path to a sound.");
          ret = "";
        }
      }
      LUA->Release(L);
    } else {
      LOG->Trace("run script failed hardcore, lol");
      ret = "";
    }
  } else if (ft != FT_Sound) {
    LuaHelpers::ReportScriptErrorFmt(
        "Music file %s is not a sound file.", path.c_str());
    ret = "";
  }
  return ret;
}

void ScreenWithMenuElements::StartPlayingMusic() {
  /* Some screens should leave the music alone (eg. ScreenPlayerOptions music
   * sample left over from ScreenSelectMusic). */
  if (PLAY_MUSIC) {
    GameSoundManager::PlayMusicParams pmp;
    pmp.file_ = HandleLuaMusicFile(path_to_music_);
    if (!pmp.file_.empty()) {
      pmp.align_beat_ = MUSIC_ALIGN_BEAT;
      if (DELAY_MUSIC_SECONDS > 0.0f) {
        pmp.start_second_ = -DELAY_MUSIC_SECONDS;
      }
      SOUND->PlayMusic(pmp);
    }
  }
}

void ScreenWithMenuElements::Update(float fDeltaTime) {
  Screen::Update(fDeltaTime);
}

void ScreenWithMenuElements::ResetTimer() {
  if (menu_timer_ == nullptr) {
    return;
  }

  if (TIMER_SECONDS > 0.0f && (PREFSMAN->menu_timer_ || FORCE_TIMER)) {
    menu_timer_->SetSeconds(TIMER_SECONDS);
    menu_timer_->Start();
  } else {
    menu_timer_->Disable();
  }
}

void ScreenWithMenuElements::StartTransitioningScreen(
    ScreenMessage smSendWhenDone) {
  TweenOffScreen();

  out_.StartTransitioning(smSendWhenDone);
  if (WAIT_FOR_CHILDREN_BEFORE_TWEENING_OUT) {
    // Time the transition so that it finishes exactly when all actors have
    // finished tweening.
    float fSecondsUntilFinished = GetTweenTimeLeft();
    float fSecondsUntilBeginOff =
        std::max(fSecondsUntilFinished - out_.GetTweenTimeLeft(), 0.0f);
    out_.SetHibernate(fSecondsUntilBeginOff);
  }
}

void ScreenWithMenuElements::TweenOnScreen() { this->PlayCommand("On"); }

void ScreenWithMenuElements::TweenOffScreen() {
  if (menu_timer_) {
    menu_timer_->SetSeconds(0);
    menu_timer_->Stop();
  }

  this->PlayCommand("Off");

  // If we're a stacked screen, then there's someone else between us and the
  // background, so don't tween it off.
  if (!SCREENMAN->IsStackedScreen(this)) {
    SCREENMAN->PlaySharedBackgroundOffCommand();
  }
}

void ScreenWithMenuElements::Cancel(ScreenMessage smSendWhenDone) {
  next_screen_ = GetPrevScreen();

  if (CANCEL_TRANSITIONS_OUT) {
    StartTransitioningScreen(smSendWhenDone);
    COMMAND(out_, "Cancel");
    return;
  }

  this->PlayCommand("Cancel");

  if (cancel_.IsTransitioning()) {
    return;  // ignore
  }

  if (STOP_MUSIC_ON_BACK) {
    SOUND->StopMusic();
  }

  if (menu_timer_) {
    menu_timer_->Stop();
  }
  cancel_.StartTransitioning(smSendWhenDone);
  COMMAND(cancel_, "Cancel");
}

bool ScreenWithMenuElements::IsTransitioning() {
  return in_.IsTransitioning() || out_.IsTransitioning() ||
         cancel_.IsTransitioning();
}

void ScreenWithMenuElements::StopTimer() {
  if (menu_timer_) {
    menu_timer_->Stop();
  }
}

REGISTER_SCREEN_CLASS(ScreenWithMenuElementsSimple);

bool ScreenWithMenuElementsSimple::MenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }
  if (lock_input_secs_ > 0) {
    return false;
  }

  StartTransitioningScreen(SM_GoToNextScreen);
  return true;
}

bool ScreenWithMenuElementsSimple::MenuBack(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }
  if (lock_input_secs_ > 0) {
    return false;
  }

  Cancel(SM_GoToPrevScreen);
  return true;
}

// lua start
#include "LuaBinding.h"
/** @brief Allow Lua to have access to the ScreenWithMenuElements. */
class LunaScreenWithMenuElements : public Luna<ScreenWithMenuElements> {
 public:
  static int Cancel(T* p, lua_State* L) {
    p->Cancel(SM_GoToPrevScreen);
    COMMON_RETURN_SELF;
  }
  static int IsTransitioning(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsTransitioning());
    return 1;
  }
  static int SetAllowLateJoin(T* p, lua_State* L) {
    p->should_allow_late_join_ = BArg(1);
    COMMON_RETURN_SELF;
  }

  static int StartTransitioningScreen(T* p, lua_State* L) {
    std::string sMessage = SArg(1);
    ScreenMessage SM = ScreenMessageHelpers::ToScreenMessage(sMessage);
    p->StartTransitioningScreen(SM);
    COMMON_RETURN_SELF;
  }

  LunaScreenWithMenuElements() {
    ADD_METHOD(Cancel);
    ADD_METHOD(IsTransitioning);
    ADD_METHOD(SetAllowLateJoin);
    ADD_METHOD(StartTransitioningScreen);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenWithMenuElements, Screen)

/** @brief Allow Lua to have access to the ScreenWithMenuElementsSimple. */
class LunaScreenWithMenuElementsSimple
    : public Luna<ScreenWithMenuElementsSimple> {
 public:
  LunaScreenWithMenuElementsSimple() {}
};

LUA_REGISTER_DERIVED_CLASS(ScreenWithMenuElementsSimple, ScreenWithMenuElements)

// lua end

/*
 * (c) 2004 Chris Danford
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
