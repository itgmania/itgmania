#include "ScreenNameEntry.h"

#include "AnnouncerManager.h"
#include "Course.h"
#include "DateTime.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "Grade.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "ModsGroup.h"
#include "NoteSkinManager.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "StageStats.h"
#include "Style.h"
#include "ThemeManager.h"
#include "global.h"

// used in TestScreen section
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "Song.h"
#include "SongManager.h"
#include "StatsManager.h"

// Defines specific to ScreenNameEntry
#define CATEGORY_Y THEME->GetMetricF(name_, "CategoryY")
#define CATEGORY_ZOOM THEME->GetMetricF(name_, "CategoryZoom")
#define CHARS_ZOOM_SMALL THEME->GetMetricF(name_, "CharsZoomSmall")
#define CHARS_ZOOM_LARGE THEME->GetMetricF(name_, "CharsZoomLarge")
#define CHARS_SPACING_Y THEME->GetMetricF(name_, "CharsSpacingY")
#define CHARS_CHOICES THEME->GetMetric(name_, "CharsChoices")
#define SCROLLING_CHARS_COMMAND \
  THEME->GetMetricA(name_, "ScrollingCharsCommand")
#define SELECTED_CHARS_COMMAND THEME->GetMetricA(name_, "SelectedCharsCommand")
#define GRAY_ARROWS_Y THEME->GetMetricF(name_, "ReceptorArrowsY")
#define NUM_CHARS_TO_DRAW_BEHIND \
  THEME->GetMetricI(name_, "NumCharsToDrawBehind")
#define NUM_CHARS_TO_DRAW_TOTAL THEME->GetMetricI(name_, "NumCharsToDrawTotal")
#define FAKE_BEATS_PER_SEC THEME->GetMetricF(name_, "FakeBeatsPerSec")
#define MAX_RANKING_NAME_LENGTH THEME->GetMetricI(name_, "MaxRankingNameLength")
#define PLAYER_X(p, styleType) \
  THEME->GetMetricF(           \
      name_,                   \
      ssprintf("PlayerP%d%sX", p + 1, StyleTypeToString(styleType).c_str()))

// cache for frequently used metrics
static float g_fCharsZoomSmall;
static float g_fCharsZoomLarge;
static float g_fCharsSpacingY;
static int g_iNumCharsToDrawBehind;
static int g_iNumCharsToDrawTotal;

void ScreenNameEntry::ScrollingText::Init(
    const std::string& sName, const std::vector<float>& xs) {
  SetName(sName);
  xs_ = xs;
  done_ = false;
  stamp_.LoadFromFont(THEME->GetPathF(sName, "letters"));
  stamp_.RunCommands(THEME->GetMetricA(sName, "ScrollingCharsCommand"));
}

void ScreenNameEntry::ScrollingText::DrawPrimitives() {
  const float fFakeBeat = GAMESTATE->position_.song_beat_;
  const size_t iClosestIndex = std::lrint(fFakeBeat) % CHARS_CHOICES.size();
  const float fClosestYOffset = GetClosestCharYOffset(fFakeBeat);

  size_t iCharIndex =
      (iClosestIndex - NUM_CHARS_TO_DRAW_BEHIND + CHARS_CHOICES.size()) %
      CHARS_CHOICES.size();
  float fY = GRAY_ARROWS_Y +
             (fClosestYOffset - g_iNumCharsToDrawBehind) * g_fCharsSpacingY;

  for (int i = 0; i < NUM_CHARS_TO_DRAW_TOTAL; ++i) {
    const std::string c = CHARS_CHOICES.substr(iCharIndex, 1);
    float fZoom = g_fCharsZoomSmall;
    float fAlpha = 1.f;

    if (iCharIndex == iClosestIndex) {
      fZoom = SCALE(
          std::abs(fClosestYOffset), 0, 0.5f, g_fCharsZoomLarge,
          g_fCharsZoomSmall);
    }
    if (i == 0) {
      fAlpha *= SCALE(fClosestYOffset, -0.5f, 0.f, 0.f, 1.f);
    }
    if (i == g_iNumCharsToDrawTotal - 1) {
      fAlpha *= SCALE(fClosestYOffset, 0.f, 0.5f, 1.f, 0.f);
    }

    stamp_.SetZoom(fZoom);
    stamp_.SetDiffuseAlpha(fAlpha);
    stamp_.SetText(c);
    stamp_.SetY(fY);
    for (const float& x : xs_) {
      stamp_.SetX(x);
      stamp_.Draw();
    }
    fY += g_fCharsSpacingY;
    iCharIndex = (iCharIndex + 1) % CHARS_CHOICES.size();
  }
}

char ScreenNameEntry::ScrollingText::GetClosestChar(float fFakeBeat) const {
  ASSERT(fFakeBeat >= 0.f);
  return CHARS_CHOICES[std::lrint(fFakeBeat) % CHARS_CHOICES.size()];
}

// return value is relative to gray arrows
float ScreenNameEntry::ScrollingText::GetClosestCharYOffset(
    float fFakeBeat) const {
  float f = std::fmod(fFakeBeat, 1.0f);
  if (f > 0.5f) {
    f -= 1;
  }
  ASSERT(f > -0.5f && f <= 0.5f);
  return -f;
}

REGISTER_SCREEN_CLASS(ScreenNameEntry);
ScreenNameEntry::ScreenNameEntry() {
  if (PREFSMAN->test_initial_screen_.Get() != "") {
    GAMESTATE->side_is_joined_[PLAYER_1] = true;
    GAMESTATE->side_is_joined_[PLAYER_2] = true;
    GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
    GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
    GAMESTATE->SetCurrentStyle(
        GAMEMAN->GameAndStringToStyle(GAMEMAN->GetDefaultGame(), "versus"),
        PLAYER_INVALID);
    StageStats ss;
    for (int z = 0; z < 3; ++z) {
      ss.played_songs_.push_back(SONGMAN->GetRandomSong());
      ss.possible_songs_ = ss.played_songs_;
      ss.player_[PLAYER_1].style_ = GAMESTATE->GetCurrentStyle(PLAYER_1);
      ss.play_mode_ = GAMESTATE->play_mode_;
      ASSERT(ss.played_songs_[0]->GetAllSteps().size() != 0);
      StepsType st = GAMESTATE->GetCurrentStyle(PLAYER_1)->steps_type_;

      FOREACH_PlayerNumber(p) {
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
}

void ScreenNameEntry::Init() {
  ScreenWithMenuElements::Init();

  // update cache
  g_fCharsZoomSmall = CHARS_ZOOM_SMALL;
  g_fCharsZoomLarge = CHARS_ZOOM_LARGE;
  g_fCharsSpacingY = CHARS_SPACING_Y;
  g_iNumCharsToDrawBehind = NUM_CHARS_TO_DRAW_BEHIND;
  g_iNumCharsToDrawTotal = NUM_CHARS_TO_DRAW_TOTAL;

  // reset Player and Song Options
  {
    FOREACH_PlayerNumber(p) PO_GROUP_CALL(
        GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage, Init);
    SO_GROUP_CALL(GAMESTATE->song_options_, ModsLevel_Stage, Init);
  }

  std::vector<GameState::RankingFeat> aFeats[NUM_PLAYERS];

  // Find out if players deserve to enter their name
  FOREACH_PlayerNumber(p) {
    GAMESTATE->GetRankingFeats(p, aFeats[p]);
    GAMESTATE->JoinPlayer(p);
    still_entering_name_[p] = aFeats[p].size() > 0;
#if 0  // Debugging.
		still_entering_name_[p] = p == PLAYER_1;
#endif
  }

  if (!AnyStillEntering()) {
    // Nobody got a high score.
    PostScreenMessage(SM_GoToNextScreen, 0);
    return;
  }

  bool IsOnRanking =
      ((GAMESTATE->play_mode_ == PLAY_MODE_NONSTOP ||
        GAMESTATE->play_mode_ == PLAY_MODE_ONI) &&
       !(GAMESTATE->cur_course_->IsRanking()));

  if (PREFSMAN->get_ranking_name_ == RANKING_OFF ||
      (PREFSMAN->get_ranking_name_ == RANKING_LIST && !IsOnRanking)) {
    // don't collect score due to ranking setting
    PostScreenMessage(SM_GoToNextScreen, 0);
    return;
  }

  GAMESTATE->gameplay_lead_in_.Set(false);  // enable the gray arrows

  FOREACH_PlayerNumber(p) {
    // load last used ranking name if any
    Profile* pProfile = PROFILEMAN->GetProfile(p);
    if (pProfile && !pProfile->last_used_high_score_name_.empty()) {
      selected_name_[p] = pProfile->last_used_high_score_name_;
    }

    // resize string to MAX_RANKING_NAME_LENGTH
    selected_name_[p] = ssprintf(
        "%*.*s", MAX_RANKING_NAME_LENGTH, MAX_RANKING_NAME_LENGTH,
        selected_name_[p].c_str());
    ASSERT((int)selected_name_[p].length() == MAX_RANKING_NAME_LENGTH);

    // don't load player if they aren't going to enter their name
    if (!still_entering_name_[p]) {
      continue;  // skip
    }

    // remove modifiers that may have been on the last song
    PlayerOptions po;
    GAMESTATE->GetDefaultPlayerOptions(po);
    GAMESTATE->player_state_[p]->player_options_.Assign(ModsLevel_Stage, po);

    ASSERT(GAMESTATE->IsHumanPlayer(
        p));  // they better be enabled if they made a high score!

    const float fPlayerX =
        PLAYER_X(p, GAMESTATE->GetCurrentStyle(p)->style_type_);

    {
      LockNoteSkin l(
          GAMESTATE->player_state_[p]->player_options_.GetCurrent().note_skin_);

      receptor_arrow_row_[p].Load(GAMESTATE->player_state_[p], 0);
      receptor_arrow_row_[p].SetX(fPlayerX);
      receptor_arrow_row_[p].SetY(GRAY_ARROWS_Y);
      this->AddChild(&receptor_arrow_row_[p]);
    }

    const Style* pStyle = GAMESTATE->GetCurrentStyle(p);
    const int iMaxCols =
        std::min(int(ABS_MAX_RANKING_NAME_LENGTH), pStyle->cols_per_player_);
    col_to_string_index_[p].insert(
        col_to_string_index_[p].begin(), pStyle->cols_per_player_, -1);
    int CurrentStringIndex = 0;
    std::vector<float> xs;

    for (int iCol = 0; iCol < iMaxCols; ++iCol) {
      if (CurrentStringIndex == MAX_RANKING_NAME_LENGTH) {
        break;  // We have enough columns.
      }

      // Find out if this column is associated with the START menu button.
      std::vector<GameInput> gi;
      GAMESTATE->GetCurrentStyle(p)->StyleInputToGameInput(iCol, p, gi);
      bool gi_is_start = false;
      for (size_t i = 0; i < gi.size(); ++i) {
        gi_is_start |=
            (INPUTMAPPER->GameButtonToMenuButton(gi[i].button_) ==
             GAME_BUTTON_START);
      }
      if (gi_is_start) {
        continue;
      }
      col_to_string_index_[p][iCol] = CurrentStringIndex++;

      float ColX = fPlayerX + pStyle->column_info_[p][iCol].x_offset_;

      text_selected_chars_[p][iCol].LoadFromFont(
          THEME->GetPathF(name_, "letters"));
      text_selected_chars_[p][iCol].SetX(ColX);
      text_selected_chars_[p][iCol].SetY(GRAY_ARROWS_Y);
      text_selected_chars_[p][iCol].RunCommands(SELECTED_CHARS_COMMAND);
      text_selected_chars_[p][iCol].SetZoom(CHARS_ZOOM_LARGE);
      if (iCol < (int)selected_name_[p].length()) {
        text_selected_chars_[p][iCol].SetText(
            selected_name_[p].substr(iCol, 1));
      }
      this->AddChild(&text_selected_chars_[p][iCol]);
      xs.push_back(ColX);
    }
    text_[p].Init(name_, xs);
    this->AddChild(&text_[p]);

    text_category_[p].LoadFromFont(THEME->GetPathF(name_, "category"));
    text_category_[p].SetX(fPlayerX);
    text_category_[p].SetY(CATEGORY_Y);
    text_category_[p].SetZoom(CATEGORY_ZOOM);
    std::string joined;
    for (unsigned j = 0; j < aFeats[p].size(); ++j) {
      if (j) {
        joined += "\n";
      }
      joined += aFeats[p][j].feat_;
    }

    text_category_[p].SetText(joined);
    this->AddChild(&text_category_[p]);
  }

  sound_step_.Load(THEME->GetPathS(name_, "step"));
  fake_beat_ = 0;
}

bool ScreenNameEntry::AnyStillEntering() const {
  FOREACH_PlayerNumber(p) if (still_entering_name_[p]) return true;
  return false;
}

void ScreenNameEntry::Update(float fDelta) {
  if (first_update_) {
    SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("name entry"));
  }

  fake_beat_ += fDelta * FAKE_BEATS_PER_SEC;
  GAMESTATE->position_.song_beat_ = fake_beat_;

  ScreenWithMenuElements::Update(fDelta);
}

bool ScreenNameEntry::Input(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  if (input.type_ != IET_FIRST_PRESS || !input.game_i_.IsValid()) {
    return false;  // ignore
  }

  const int iCol =
      GAMESTATE->GetCurrentStyle(input.pn_)->GameInputToColumn(input.game_i_);
  bool bHandled = false;
  if (iCol != Column_Invalid && still_entering_name_[input.pn_]) {
    int iStringIndex = col_to_string_index_[input.pn_][iCol];
    if (iStringIndex != -1) {
      receptor_arrow_row_[input.pn_].Step(iCol, TNS_W1);
      sound_step_.Play(true);
      char c = text_[input.pn_].GetClosestChar(fake_beat_);
      text_selected_chars_[input.pn_][iCol].SetText(std::string(1, c));
      selected_name_[input.pn_][iStringIndex] = c;
    }
    bHandled = true;
  }

  return ScreenWithMenuElements::Input(input) || bHandled;
}

void ScreenNameEntry::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_MenuTimer) {
    if (!IsTransitioning()) {
      InputEventPlus iep;
      FOREACH_PlayerNumber(p) {
        iep.pn_ = p;
        this->MenuStart(iep);
      }
    }
  }

  ScreenWithMenuElements::HandleScreenMessage(SM);
}

bool ScreenNameEntry::MenuStart(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;

  if (!still_entering_name_[pn]) {
    return false;
  }
  still_entering_name_[pn] = false;
  text_[pn].SetDone();

  sound_step_.Play(true);

  // save last used ranking name
  Profile* pProfile = PROFILEMAN->GetProfile(pn);
  pProfile->last_used_high_score_name_ = selected_name_[pn];

  Trim(selected_name_[pn], " ");

  GAMESTATE->StoreRankingName(pn, selected_name_[pn]);

  if (!AnyStillEntering() && !out_.IsTransitioning()) {
    StartTransitioningScreen(SM_GoToNextScreen);
  }
  return true;
}

/*
 * (c) 2001-2006 Chris Danford, Steve Checkoway
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
