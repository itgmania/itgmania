#include "StageStats.h"

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "DateTime.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "HighScore.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"
#include "global.h"

/* Arcade:	for the current stage (one song).
 * Nonstop/Oni/Endless:	 for current course (which usually contains multiple
 * songs)
 */

StageStats::StageStats() {
  play_mode_ = PlayMode_Invalid;
  stage_ = Stage_Invalid;
  stage_index_ = -1;
  played_songs_.clear();
  possible_songs_.clear();
  earned_extra_stage_ = EarnedExtraStage_No;
  gave_up_ = false;
  used_autoplay_ = false;
  gameplay_seconds_ = 0;
  steps_seconds_ = 0;
  music_rate_ = 1;
  FOREACH_PlayerNumber(pn) { player_[pn].Init(pn); }
  routine_player_.Init(GAMESTATE->GetMasterPlayerNumber());
}

void StageStats::Init() { *this = StageStats(); }

void StageStats::AssertValid(PlayerNumber pn) const {
  ASSERT(played_songs_.size() != 0);
  ASSERT(possible_songs_.size() != 0);
  if (played_songs_[0]) {
    CHECKPOINT_M(played_songs_[0]->GetTranslitFullTitle());
  }
  ASSERT(player_[pn].steps_played_ > 0);
  ASSERT(player_[pn].possible_steps_.size() != 0);
  ASSERT(player_[pn].possible_steps_[0] != nullptr);
  ASSERT_M(play_mode_ < NUM_PlayMode, ssprintf("playmode %i", play_mode_));
  ASSERT_M(
      player_[pn].possible_steps_[0]->GetDifficulty() < NUM_Difficulty,
      ssprintf(
          "Invalid Difficulty %i",
          player_[pn].possible_steps_[0]->GetDifficulty()));
  ASSERT_M(
      (int)played_songs_.size() == player_[pn].steps_played_,
      ssprintf(
          "%i Songs Played != %i Steps Played for player %i",
          (int)played_songs_.size(), (int)player_[pn].steps_played_, pn));
  ASSERT_M(
      possible_songs_.size() == player_[pn].possible_steps_.size(),
      ssprintf(
          "%i Possible Songs != %i Possible Steps for player %i",
          (int)possible_songs_.size(), (int)player_[pn].possible_steps_.size(),
          pn));
}

int StageStats::GetAverageMeter(PlayerNumber pn) const {
  AssertValid(pn);

  // TODO: This isn't correct for courses.
  int iTotalMeter = 0;

  for (unsigned i = 0; i < played_songs_.size(); i++) {
    const Steps* pSteps = player_[pn].possible_steps_[i];
    iTotalMeter += pSteps->GetMeter();
  }
  return iTotalMeter / played_songs_.size();  // round down
}

void StageStats::AddStats(const StageStats& other) {
  ASSERT(!other.played_songs_.empty());
  for (Song* s : other.played_songs_) {
    played_songs_.push_back(s);
  }
  for (Song* s : other.possible_songs_) {
    possible_songs_.push_back(s);
  }
  stage_ = Stage_Invalid;  // meaningless
  stage_index_ = -1;       // meaningless

  gave_up_ |= other.gave_up_;
  used_autoplay_ |= other.used_autoplay_;

  gameplay_seconds_ += other.gameplay_seconds_;
  steps_seconds_ += other.steps_seconds_;

  FOREACH_EnabledPlayer(p) player_[p].AddStats(other.player_[p]);
}

bool StageStats::OnePassed() const {
  FOREACH_EnabledPlayer(p) if (!player_[p].failed_) return true;
  return false;
}

bool StageStats::AllFailed() const {
  FOREACH_EnabledPlayer(p) if (!player_[p].failed_) return false;
  return true;
}

float StageStats::GetTotalPossibleStepsSeconds() const {
  float fSecs = 0;
  for (const Song* s : played_songs_) {
    fSecs += s->GetStepsSeconds();
  }
  return fSecs / music_rate_;
}

static HighScore FillInHighScore(
    const PlayerStageStats& pss, const PlayerState& ps,
    std::string sRankingToFillInMarker, std::string sPlayerGuid) {
  HighScore hs;
  hs.SetName(sRankingToFillInMarker);
  hs.SetGrade(pss.GetGrade());
  hs.SetScore(pss.score_);
  hs.SetPercentDP(pss.GetPercentDancePoints());
  hs.SetAliveSeconds(pss.alive_seconds_);
  hs.SetMaxCombo(pss.GetMaxCombo().cnt_);
  hs.SetStageAward(pss.stage_award_);
  hs.SetPeakComboAward(pss.peak_combo_award_);

  std::vector<std::string> asModifiers;
  {
    std::string sPlayerOptions = ps.player_options_.GetStage().GetString();
    if (!sPlayerOptions.empty()) {
      asModifiers.push_back(sPlayerOptions);
    }
    std::string sSongOptions = GAMESTATE->song_options_.GetStage().GetString();
    if (!sSongOptions.empty()) {
      asModifiers.push_back(sSongOptions);
    }
  }
  hs.SetModifiers(join(", ", asModifiers));

  hs.SetDateTime(DateTime::GetNowDateTime());
  hs.SetPlayerGuid(sPlayerGuid);
  hs.SetMachineGuid(PROFILEMAN->GetMachineProfile()->guid_);
  hs.SetProductID(PREFSMAN->product_id_);
  FOREACH_ENUM(TapNoteScore, tns)
  hs.SetTapNoteScore(tns, pss.tap_note_scores_[tns]);
  FOREACH_ENUM(HoldNoteScore, hns)
  hs.SetHoldNoteScore(hns, pss.hold_note_scores_[hns]);
  hs.SetRadarValues(pss.radar_actual_);
  hs.SetLifeRemainingSeconds(pss.life_remaining_seconds_);
  hs.SetDisqualified(pss.IsDisqualified());

  return hs;
}

static HighScore FillInRoutineHighScore(
    const PlayerStageStats& pss, const PlayerState& ps,
    std::string sRankingToFillInMarker, std::vector<PlayerStageStats>& ppss,
    std::vector<std::string>& sPlayerGuids) {
  HighScore hs;
  hs.SetName(sRankingToFillInMarker);
  hs.SetGrade(pss.GetGrade());
  hs.SetScore(pss.score_);
  hs.SetPercentDP(pss.GetPercentDancePoints());
  hs.SetAliveSeconds(pss.alive_seconds_);
  hs.SetMaxCombo(pss.GetMaxCombo().cnt_);
  hs.SetStageAward(pss.stage_award_);
  hs.SetPeakComboAward(pss.peak_combo_award_);

  std::vector<std::string> asModifiers;
  {
    std::string sPlayerOptions = ps.player_options_.GetStage().GetString();
    if (!sPlayerOptions.empty()) {
      asModifiers.push_back(sPlayerOptions);
    }
    std::string sSongOptions = GAMESTATE->song_options_.GetStage().GetString();
    if (!sSongOptions.empty()) {
      asModifiers.push_back(sSongOptions);
    }
  }
  hs.SetModifiers(join(", ", asModifiers));

  hs.SetDateTime(DateTime::GetNowDateTime());
  hs.SetPlayerGuid(sPlayerGuids[0]);  // just pick the first player's guid to
                                      // represent the routine
  hs.SetMachineGuid(PROFILEMAN->GetMachineProfile()->guid_);
  hs.SetProductID(PREFSMAN->product_id_);
  FOREACH_ENUM(TapNoteScore, tns)
  hs.SetTapNoteScore(tns, pss.tap_note_scores_[tns]);
  FOREACH_ENUM(HoldNoteScore, hns)
  hs.SetHoldNoteScore(hns, pss.hold_note_scores_[hns]);
  hs.SetRadarValues(pss.radar_actual_);
  hs.SetLifeRemainingSeconds(pss.life_remaining_seconds_);
  hs.SetDisqualified(pss.IsDisqualified());
  hs.SetRoutine(true);
  FOREACH_HumanPlayer(pn) {
    hs.SetPlayerGrade(pn, ppss[pn].GetGrade());
    hs.SetPlayerScore(pn, ppss[pn].score_);
    hs.SetPlayerPercentDP(pn, ppss[pn].GetPercentDancePoints());
    hs.SetPlayerMaxCombo(pn, ppss[pn].GetMaxCombo().cnt_);
    hs.SetPlayerGuid(pn, sPlayerGuids[pn]);
    std::string tnsStr = "";
    FOREACH_ENUM(TapNoteScore, tns) {
      hs.SetPlayerTapNoteScore(pn, tns, ppss[pn].tap_note_scores_[tns]);
      tnsStr += TapNoteScoreToString(tns) + ": " +
                ssprintf("%i", ppss[pn].tap_note_scores_[tns]) + ", ";
    }
    FOREACH_ENUM(HoldNoteScore, hns)
    hs.SetPlayerHoldNoteScore(pn, hns, ppss[pn].hold_note_scores_[hns]);
  }

  return hs;
}

void StageStats::FinalizeScores(bool bSummary) {
  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      return;  // don't save scores in battle
    default:
      break;
  }

  if (PREFSMAN->test_initial_screen_.Get() != "") {
    FOREACH_PlayerNumber(pn) {
      player_[pn].personal_high_score_index_ = 0;
      player_[pn].machine_high_score_index_ = 0;
    }
  }

  // don't save scores if the player chose not to
  if (!GAMESTATE->song_options_.GetCurrent().save_score_) {
    return;
  }

  LOG->Trace("saving stats and high scores");

  // generate a HighScore for each player

  // whether or not to save scores when the stage was failed depends on if this
  // is a course or not... it's handled below in the switch.
  StyleType styleType =
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->style_type_;
  // If the stepstype is routine, fill high scores with each player
  if (styleType == StyleType_TwoPlayersSharedSides) {
    PlayerNumber p = GAMESTATE->GetMasterPlayerNumber();
    std::vector<PlayerStageStats> ppss(NUM_PLAYERS);
    std::vector<std::string> sPlayerGuids(NUM_PLAYERS);
    FOREACH_HumanPlayer(pn) {
      ppss[pn] = player_[pn];
      sPlayerGuids[pn] = PROFILEMAN->IsPersistentProfile(pn)
                             ? PROFILEMAN->GetProfile(pn)->guid_
                             : std::string("");
    }
    player_[p].high_score_ = FillInRoutineHighScore(
        routine_player_, *GAMESTATE->player_state_[p],
        RANKING_TO_FILL_IN_MARKER[p], ppss, sPlayerGuids);
  } else {
    FOREACH_HumanPlayer(p) {
      std::string sPlayerGuid = PROFILEMAN->IsPersistentProfile(p)
                                    ? PROFILEMAN->GetProfile(p)->guid_
                                    : std::string("");
      player_[p].high_score_ = FillInHighScore(
          player_[p], *GAMESTATE->player_state_[p],
          RANKING_TO_FILL_IN_MARKER[p], sPlayerGuid);
    }
  }

  FOREACH_HumanPlayer(p) {
    const HighScore& hs = player_[p].high_score_;
    StepsType stepsType = GAMESTATE->GetCurrentStyle(p)->steps_type_;

    const Song* pSong = GAMESTATE->cur_song_;
    const Steps* pSteps = GAMESTATE->cur_steps_[p];

    // Don't save DQ'd scores
    if (hs.GetDisqualified()) {
      continue;
    }

    // Don't save autoplay scores
    if (used_autoplay_) {
      continue;
    }

    if (bSummary) {
      // don't save scores if any stage was failed
      if (player_[p].failed_) {
        continue;
      }

      int iAverageMeter = GetAverageMeter(p);
      player_[p].rc_ = AverageMeterToRankingCategory(iAverageMeter);

      PROFILEMAN->AddCategoryScore(
          stepsType, player_[p].rc_, p, hs,
          player_[p].personal_high_score_index_,
          player_[p].machine_high_score_index_);

      // TRICKY: Increment play count here, and not on ScreenGameplay like the
      // others.
      PROFILEMAN->IncrementCategoryPlayCount(stepsType, player_[p].rc_, p);
    } else if (GAMESTATE->IsCourseMode()) {
      // Save this stage to recent scores
      Course* pCourse = GAMESTATE->cur_course_;
      ASSERT(pCourse != nullptr);
      Trail* pTrail = GAMESTATE->cur_trail_[p];

      PROFILEMAN->AddCourseScore(
          pCourse, pTrail, p, hs, player_[p].personal_high_score_index_,
          player_[p].machine_high_score_index_);
    } else {
      ASSERT(pSteps != nullptr);

      PROFILEMAN->AddStepsScore(
          pSong, pSteps, p, hs, player_[p].personal_high_score_index_,
          player_[p].machine_high_score_index_);
    }
  }

  // If both players get a machine high score in the same HighScoreList,
  // then one player's score may have bumped the other player. Look in
  // the HighScoreList and re-get the high score index.
  FOREACH_HumanPlayer(p) {
    if (player_[p].machine_high_score_index_ == -1) {  // no record
      continue;                                        // skip
    }

    HighScore& hs = player_[p].high_score_;
    Profile* pProfile = PROFILEMAN->GetMachineProfile();
    StepsType stepsType = GAMESTATE->GetCurrentStyle(p)->steps_type_;

    const HighScoreList* pHSL = nullptr;
    if (bSummary) {
      pHSL = &pProfile->GetCategoryHighScoreList(stepsType, player_[p].rc_);
    } else if (GAMESTATE->IsCourseMode()) {
      Course* pCourse = GAMESTATE->cur_course_;
      ASSERT(pCourse != nullptr);
      Trail* pTrail = GAMESTATE->cur_trail_[p];
      ASSERT(pTrail != nullptr);
      pHSL = &pProfile->GetCourseHighScoreList(pCourse, pTrail);
    } else {
      Song* pSong = GAMESTATE->cur_song_;
      Steps* pSteps = GAMESTATE->cur_steps_[p];
      pHSL = &pProfile->GetStepsHighScoreList(pSong, pSteps);
    }

    std::vector<HighScore>::const_iterator iter =
        find(pHSL->high_scores_.begin(), pHSL->high_scores_.end(), hs);
    if (iter == pHSL->high_scores_.end()) {
      player_[p].machine_high_score_index_ = -1;
    } else {
      player_[p].machine_high_score_index_ = iter - pHSL->high_scores_.begin();
    }
  }

  LOG->Trace("done saving stats and high scores");
}

bool StageStats::PlayerHasHighScore(PlayerNumber pn) const {
  const Song* pSong = played_songs_[0];
  const Steps* pSteps = player_[pn].possible_steps_[0];
  const Course* pCourse = GAMESTATE->cur_course_;
  const Trail* pTrail = GAMESTATE->cur_trail_[pn];

  // Don't show high scores for tutorial songs.
  if (pSong->IsTutorial() == Song::SHOW_NEVER) {
    return false;
  }

  const HighScoreList& hsl =
      GAMESTATE->IsCourseMode()
          ? PROFILEMAN->GetMachineProfile()->GetCourseHighScoreList(
                pCourse, pTrail)
          : PROFILEMAN->GetMachineProfile()->GetStepsHighScoreList(
                pSong, pSteps);

  const unsigned int iScore = player_[pn].score_;
  float fPercentDP = player_[pn].GetPercentDancePoints();
  for (int h = 0; h < (int)hsl.high_scores_.size() &&
                  h < PREFSMAN->max_high_scores_per_list_for_machine_;
       ++h) {
    const HighScore& hs = hsl.high_scores_[h];
    if (hs.GetName() == RANKING_TO_FILL_IN_MARKER[pn] &&
        hs.GetPercentDP() == fPercentDP && hs.GetScore() == iScore) {
      return true;
    }
  }

  return false;
}

unsigned int StageStats::GetMinimumMissCombo() const {
  unsigned int iMin = INT_MAX;
  FOREACH_HumanPlayer(p) iMin = std::min(iMin, player_[p].cur_miss_combo_);
  return iMin;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the StageStats. */
class LunaStageStats : public Luna<StageStats> {
 public:
  static int GetPlayerStageStats(T* p, lua_State* L) {
    p->player_[Enum::Check<PlayerNumber>(L, 1)].PushSelf(L);
    return 1;
  }
  static int GetPlayedSongs(T* p, lua_State* L) {
    lua_newtable(L);
    for (int i = 0; i < (int)p->played_songs_.size(); ++i) {
      p->played_songs_[i]->PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }
  static int GetPossibleSongs(T* p, lua_State* L) {
    lua_newtable(L);
    for (int i = 0; i < (int)p->possible_songs_.size(); ++i) {
      p->possible_songs_[i]->PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }
  static int GetGameplaySeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->gameplay_seconds_);
    return 1;
  }
  static int OnePassed(T* p, lua_State* L) {
    lua_pushboolean(L, p->OnePassed());
    return 1;
  }
  static int AllFailed(T* p, lua_State* L) {
    lua_pushboolean(L, p->AllFailed());
    return 1;
  }
  static int GetStage(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->stage_);
    return 1;
  }
  DEFINE_METHOD(GetStageIndex, stage_index_)
  DEFINE_METHOD(GetStepsSeconds, steps_seconds_)
  static int PlayerHasHighScore(T* p, lua_State* L) {
    lua_pushboolean(L, p->PlayerHasHighScore(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  DEFINE_METHOD(GetEarnedExtraStage, earned_extra_stage_)
  static int GaveUp(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->gave_up_);
    return 1;
  }

  static int GetRoutineStageStats(T* p, lua_State* L) {
    p->routine_player_.PushSelf(L);
    return 1;
  }

  LunaStageStats() {
    ADD_METHOD(GetPlayerStageStats);
    ADD_METHOD(GetPlayedSongs);
    ADD_METHOD(GetPossibleSongs);
    ADD_METHOD(GetGameplaySeconds);
    ADD_METHOD(OnePassed);
    ADD_METHOD(AllFailed);
    ADD_METHOD(GetStage);
    ADD_METHOD(GaveUp);
    ADD_METHOD(GetStageIndex);
    ADD_METHOD(GetStepsSeconds);
    ADD_METHOD(PlayerHasHighScore);
    ADD_METHOD(GetEarnedExtraStage);
    ADD_METHOD(GetRoutineStageStats);
  }
};

LUA_REGISTER_CLASS(StageStats)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
