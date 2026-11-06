#include "ScreenNameEntryTraditional.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "DateTime.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Grade.h"
#include "InputEventPlus.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "StatsManager.h"
#include "Style.h"
#include "global.h"

REGISTER_SCREEN_CLASS(ScreenNameEntryTraditional);

void ScreenNameEntryTraditional::Init() {
  if (PREFSMAN->test_initial_screen_.Get() == name_) {
    GAMESTATE->side_is_joined_[PLAYER_1] = true;
    GAMESTATE->side_is_joined_[PLAYER_2] = true;
    GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
    GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
    GAMESTATE->SetCurrentStyle(
        GAMEMAN->GameAndStringToStyle(GAMEMAN->GetDefaultGame(), "versus"),
        GAMESTATE->GetMasterPlayerNumber());
    for (int z = 0; z < 3; ++z) {
      StageStats ss;
      const std::vector<Song*>& apSongs = SONGMAN->GetAllSongs();
      ss.played_songs_.push_back(apSongs[rand() % apSongs.size()]);
      ss.possible_songs_ = ss.played_songs_;
      ss.play_mode_ = GAMESTATE->play_mode_;
      ASSERT(ss.played_songs_[0]->GetAllSteps().size());

      FOREACH_PlayerNumber(p) {
        ss.player_[p].style_ = GAMESTATE->GetCurrentStyle(p);
        StepsType st = GAMESTATE->GetCurrentStyle(p)->steps_type_;
        Steps* pSteps = ss.played_songs_[0]->GetAllSteps()[0];
        ss.player_[p].steps_played_ = 1;
        GAMESTATE->cur_steps_[p].Set(pSteps);
        ss.player_[p].possible_dance_points_ = 100;
        ss.player_[p].actual_dance_points_ = 100;
        ss.player_[p].score_ = 100;
        ss.player_[p].possible_dance_points_ = 1000;
        ss.player_[p].actual_dance_points_ = 985;
        ss.player_[p].possible_steps_.push_back(pSteps);

        HighScore hs;
        hs.SetGrade(Grade_Tier03);
        hs.SetPercentDP(ss.player_[p].GetPercentDancePoints());
        hs.SetScore(ss.player_[p].score_);
        hs.SetDateTime(DateTime::GetNowDateTime());
        int a, b;
        PROFILEMAN->AddStepsScore(ss.played_songs_[0], pSteps, p, hs, a, b);
        PROFILEMAN->AddStepsScore(ss.played_songs_[0], pSteps, p, hs, a, b);
        PROFILEMAN->AddStepsScore(ss.played_songs_[0], pSteps, p, hs, a, b);
        PROFILEMAN->AddStepsScore(ss.played_songs_[0], pSteps, p, hs, a, b);
        PROFILEMAN->AddStepsScore(ss.played_songs_[0], pSteps, p, hs, a, b);
        PROFILEMAN->AddCategoryScore(st, RANKING_A, p, hs, a, b);
      }

      STATSMAN->played_stage_stats_.push_back(ss);
    }
  }

  MAX_RANKING_NAME_LENGTH.Load(name_, "MaxRankingNameLength");

  ScreenWithMenuElements::Init();
}

void ScreenNameEntryTraditional::BeginScreen() {
  /* Find out if players are entering their name. */
  FOREACH_PlayerNumber(pn) {
    std::vector<GameState::RankingFeat> aFeats;
    GAMESTATE->GetRankingFeats(pn, aFeats);

    bool bNoStagesLeft = GAMESTATE->player_stage_tokens_[pn] <= 0;
    entering_name_[pn] =
        (aFeats.size() > 0 || PROFILEMAN->ProfileFromMemoryCardIsNew(pn)) &&
        bNoStagesLeft;
    finalized_[pn] = !entering_name_[pn];
  }

  FOREACH_HumanPlayer(pn) {
    if (!entering_name_[pn]) {
      continue;  // skip
    }

    selection_[pn] = L"";

    // load last used ranking name if any
    const Profile* pProfile = PROFILEMAN->GetProfile(pn);
    if (pProfile && !pProfile->last_used_high_score_name_.empty()) {
      selection_[pn] = RStringToWstring(pProfile->last_used_high_score_name_);
      if ((int)selection_[pn].size() > MAX_RANKING_NAME_LENGTH) {
        selection_[pn].erase(MAX_RANKING_NAME_LENGTH);
      }
    }

    UpdateSelectionText(pn);
  }

  ScreenWithMenuElements::BeginScreen();
}

void ScreenNameEntryTraditional::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_MenuTimer) {
    if (!out_.IsTransitioning()) {
      Message msg("MenuTimerExpired");
      this->HandleMessage(msg);
    }
    return;
  }

  ScreenWithMenuElements::HandleScreenMessage(SM);
}

bool ScreenNameEntryTraditional::AnyStillEntering() const {
  FOREACH_PlayerNumber(pn) if (!finalized_[pn]) return true;
  return false;
}

bool ScreenNameEntryTraditional::AnyEntering() const {
  FOREACH_PlayerNumber(pn) if (entering_name_[pn]) return true;
  return false;
}

bool ScreenNameEntryTraditional::Input(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  return ScreenWithMenuElements::Input(input);
}

bool ScreenNameEntryTraditional::Finish(PlayerNumber pn) {
  if (!AnyStillEntering()) {
    StartTransitioningScreen(SM_GoToNextScreen);
    return true;
  }

  if (finalized_[pn]) {
    return false;
  }
  finalized_[pn] = true;

  UpdateSelectionText(pn); /* hide NAME_ cursor */
  std::string sSelection = WStringToRString(selection_[pn]);

  // save last used ranking name
  Profile* pProfile = PROFILEMAN->GetProfile(pn);
  pProfile->last_used_high_score_name_ = sSelection;

  Trim(sSelection, " ");

  GAMESTATE->StoreRankingName(pn, sSelection);

  {
    Message msg("PlayerFinished");
    msg.SetParam("PlayerNumber", pn);
    this->HandleMessage(msg);
  }

  if (!AnyStillEntering()) {
    StartTransitioningScreen(SM_GoToNextScreen);
  }

  return true;
}

void ScreenNameEntryTraditional::UpdateSelectionText(PlayerNumber pn) {
  std::wstring sText = selection_[pn];
  if (!finalized_[pn] && (int)sText.size() < MAX_RANKING_NAME_LENGTH) {
    sText += L"_";
  }

  Message msg("EntryChanged");
  msg.SetParam("PlayerNumber", pn);
  msg.SetParam("Text", WStringToRString(sText));
  this->HandleMessage(msg);
}

bool ScreenNameEntryTraditional::EnterKey(PlayerNumber pn, wchar_t sLetter) {
  if (finalized_[pn]) {
    return false;  // ignore
  }

  /* If we have room, add a new character. */
  if ((int)selection_[pn].size() == MAX_RANKING_NAME_LENGTH) {
    SelectChar(pn, "BACK");
    return false;
  }

  selection_[pn] += sLetter;
  UpdateSelectionText(pn);

  /* If that filled the string, set the cursor on OK. */
  if ((int)selection_[pn].size() == MAX_RANKING_NAME_LENGTH) {
    SelectChar(pn, "ENTER");
  }
  return true;
}

void ScreenNameEntryTraditional::SelectChar(
    PlayerNumber pn, const std::string& sKey) {
  Message msg("SelectKey");
  msg.SetParam("PlayerNumber", pn);
  msg.SetParam("Key", sKey);
  this->HandleMessage(msg);
}

bool ScreenNameEntryTraditional::Backspace(PlayerNumber pn) {
  if (finalized_[pn]) {
    return false;  // ignore
  }

  if (!selection_[pn].size()) {
    return false;
  }

  selection_[pn].erase(selection_[pn].size() - 1, 1);
  UpdateSelectionText(pn);
  return true;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenNameEntryTraditional. */
class LunaScreenNameEntryTraditional : public Luna<ScreenNameEntryTraditional> {
 public:
  static int EnterKey(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    std::string sKey = SArg(2);
    bool bRet = p->EnterKey(pn, utf8_get_char(sKey));
    LuaHelpers::Push(L, bRet);
    return 1;
  }

  static int Finish(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    bool bRet = p->Finish(pn);
    LuaHelpers::Push(L, bRet);
    return 1;
  }

  static int Backspace(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    bool bRet = p->Backspace(pn);
    LuaHelpers::Push(L, bRet);
    return 1;
  }

  static int GetEnteringName(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    LuaHelpers::Push(L, p->entering_name_[pn]);
    return 1;
  }

  static int GetFinalized(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    LuaHelpers::Push(L, p->finalized_[pn]);
    return 1;
  }

  static int GetAnyEntering(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->AnyEntering());
    return 1;
  }

  static int GetAnyStillEntering(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->AnyStillEntering());
    return 1;
  }

  static int GetSelection(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    LuaHelpers::Push(L, WStringToRString(p->selection_[pn]));
    return 1;
  }

  LunaScreenNameEntryTraditional() {
    ADD_METHOD(EnterKey);
    ADD_METHOD(Finish);
    ADD_METHOD(Backspace);
    ADD_METHOD(GetEnteringName);
    ADD_METHOD(GetAnyEntering);
    ADD_METHOD(GetFinalized);
    ADD_METHOD(GetAnyStillEntering);
    ADD_METHOD(GetSelection);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenNameEntryTraditional, ScreenWithMenuElements)
// lua end

/*
 * (c) 2001-2007 Glenn Maynard, Chris Danford
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
