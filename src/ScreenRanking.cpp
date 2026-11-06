#include "ScreenRanking.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ActorUtil.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "HighScore.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "SongManager.h"
#include "ThemeManager.h"
#include "global.h"

static const char* RankingTypeNames[] = {
    "Category",
    "Course",
};
XToString(RankingType);
LuaXType(RankingType);

AutoScreenMessage(SM_ShowNextPage);
AutoScreenMessage(SM_HidePage);

REGISTER_SCREEN_CLASS(ScreenRanking);

#define BULLET_X(row) (BULLET_START_X + ROW_SPACING_X * row)
#define BULLET_Y(row) (BULLET_START_Y + ROW_SPACING_Y * row)
#define NAME_X(row) (NAME_START_X + ROW_SPACING_X * row)
#define NAME_Y(row) (NAME_START_Y + ROW_SPACING_Y * row)
#define SCORE_X(row) (SCORE_START_X + ROW_SPACING_X * row)
#define SCORE_Y(row) (SCORE_START_Y + ROW_SPACING_Y * row)
#define POINTS_X(row) (POINTS_START_X + ROW_SPACING_X * row)
#define POINTS_Y(row) (POINTS_START_Y + ROW_SPACING_Y * row)
#define TIME_X(row) (TIME_START_X + ROW_SPACING_X * row)
#define TIME_Y(row) (TIME_START_Y + ROW_SPACING_Y * row)

static std::string STEPS_TYPE_COLOR_NAME(size_t i) {
  return ssprintf("StepsTypeColor%d", int(i + 1));
}

void ScreenRanking::Init() {
  STEPS_TYPES_TO_SHOW.Load(
      name_,
      "StepsTypesToHide");  // tricky: The metric name is "hide" because
                            // ThemeMetricStepsTypesToShow takes a list of what
                            // to hide and returns what StepsTypes to show
  PAGE_FADE_SECONDS.Load(name_, "PageFadeSeconds");

  ScreenAttract::Init();

  text_steps_type_.SetName("StepsType");
  text_steps_type_.LoadFromFont(THEME->GetPathF(name_, "StepsType"));
  text_steps_type_.SetShadowLength(0);
  this->AddChild(&text_steps_type_);
  LOAD_ALL_COMMANDS(text_steps_type_);

  RANKING_TYPE.Load(name_, "RankingType");
  COURSES_TO_SHOW.Load(name_, "CoursesToShow");
  SECONDS_PER_PAGE.Load(name_, "SecondsPerPage");

  NO_SCORE_NAME.Load(name_, "NoScoreName");
  ROW_SPACING_X.Load(name_, "RowSpacingX");
  ROW_SPACING_Y.Load(name_, "RowSpacingY");
  BULLET_START_X.Load(name_, "BulletStartX");
  BULLET_START_Y.Load(name_, "BulletStartY");
  NAME_START_X.Load(name_, "NameStartX");
  NAME_START_Y.Load(name_, "NameStartY");
  SCORE_START_X.Load(name_, "ScoreStartX");
  SCORE_START_Y.Load(name_, "ScoreStartY");
  POINTS_START_X.Load(name_, "PointsStartX");
  POINTS_START_Y.Load(name_, "PointsStartY");
  TIME_START_X.Load(name_, "TimeStartX");
  TIME_START_Y.Load(name_, "TimeStartY");
  STEPS_TYPE_COLOR.Load(name_, STEPS_TYPE_COLOR_NAME, NUM_RANKING_LINES);

  if (RANKING_TYPE == RankingType_Category) {
    text_category_.SetName("Category");
    text_category_.LoadFromFont(THEME->GetPathF(name_, "category"));
    // m_textCategory.SetShadowLength( 0 );
    this->AddChild(&text_category_);
    LOAD_ALL_COMMANDS(text_category_);

    for (unsigned i = 0; i < STEPS_TYPES_TO_SHOW.GetValue().size(); i++) {
      for (int c = 0; c < NUM_RankingCategory; c++) {
        PageToShow pts;
        pts.color_index_ = i;
        pts.category_ = (RankingCategory)c;
        StepsType st = STEPS_TYPES_TO_SHOW.GetValue()[i];
        pts.types_.push_back(std::make_pair(Difficulty_Invalid, st));
        pages_to_show_.push_back(pts);
      }
    }
  }

  if (RANKING_TYPE == RankingType_SpecificTrail) {
    banner_.SetName("Banner");
    this->AddChild(&banner_);
    LOAD_ALL_COMMANDS(banner_);

    text_course_title_.SetName("CourseTitle");
    text_course_title_.LoadFromFont(THEME->GetPathF(name_, "course title"));
    // m_textCourseTitle.SetShadowLength( 0 );
    this->AddChild(&text_course_title_);
    LOAD_ALL_COMMANDS(text_course_title_);

    std::vector<std::string> asCoursePaths;
    split(COURSES_TO_SHOW, ",", asCoursePaths, true);
    for (unsigned i = 0; i < STEPS_TYPES_TO_SHOW.GetValue().size(); i++) {
      for (unsigned c = 0; c < asCoursePaths.size(); c++) {
        PageToShow pts;
        pts.color_index_ = i;
        StepsType st = STEPS_TYPES_TO_SHOW.GetValue()[i];
        pts.types_.push_back(std::make_pair(Difficulty_Invalid, st));
        pts.course_ = SONGMAN->GetCourseFromPath(asCoursePaths[c]);
        if (pts.course_ == nullptr) {
          continue;
        }

        pts.trail_ = pts.course_->GetTrail(st);
        if (pts.trail_ == nullptr) {
          continue;
        }

        pages_to_show_.push_back(pts);
      }
    }
  }

  for (int l = 0; l < NUM_RANKING_LINES; l++) {
    spr_bullets_[l].Load(
        THEME->GetPathG(name_, ssprintf("bullets 1x%d", NUM_RANKING_LINES)));
    spr_bullets_[l]->SetName(ssprintf("Bullet%d", l + 1));
    spr_bullets_[l]->StopAnimating();
    spr_bullets_[l]->SetState(l);
    spr_bullets_[l]->SetXY(BULLET_X(l), BULLET_Y(l));
    ActorUtil::LoadAllCommands(*spr_bullets_[l], name_);
    this->AddChild(spr_bullets_[l]);

    text_names_[l].SetName(ssprintf("Name%d", l + 1));
    text_names_[l].LoadFromFont(THEME->GetPathF(name_, "name"));
    text_names_[l].SetXY(NAME_X(l), NAME_Y(l));
    ActorUtil::LoadAllCommands(text_names_[l], name_);
    this->AddChild(&text_names_[l]);

    text_scores_[l].SetName(ssprintf("Score%d", l + 1));
    text_scores_[l].LoadFromFont(THEME->GetPathF(name_, "score"));
    text_scores_[l].SetXY(SCORE_X(l), SCORE_Y(l));
    ActorUtil::LoadAllCommands(text_scores_[l], name_);
    this->AddChild(&text_scores_[l]);

    text_points_[l].SetName(ssprintf("Points%d", l + 1));
    text_points_[l].LoadFromFont(THEME->GetPathF(name_, "points"));
    text_points_[l].SetVisible(false);
    text_points_[l].SetXY(POINTS_X(l), POINTS_Y(l));
    ActorUtil::LoadAllCommands(text_points_[l], name_);
    this->AddChild(&text_points_[l]);

    text_time_[l].SetName(ssprintf("Time%d", l + 1));
    text_time_[l].LoadFromFont(THEME->GetPathF(name_, "time"));
    text_time_[l].SetVisible(false);
    text_time_[l].SetXY(TIME_X(l), TIME_Y(l));
    ActorUtil::LoadAllCommands(text_time_[l], name_);
    this->AddChild(&text_time_[l]);
  }
}

void ScreenRanking::BeginScreen() {
  next_page_to_show_ = 0;

  ScreenAttract::BeginScreen();

  this->HandleScreenMessage(SM_ShowNextPage);
}

void ScreenRanking::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_ShowNextPage) {
    if (next_page_to_show_ < pages_to_show_.size()) {
      float fSecsToShow = SetPage(pages_to_show_[next_page_to_show_]);
      ++next_page_to_show_;
      this->SortByDrawOrder();

      this->PlayCommand("NextPage");
      this->PostScreenMessage(SM_HidePage, fSecsToShow - PAGE_FADE_SECONDS);
    } else {
      StartTransitioningScreen(SM_GoToNextScreen);
    }
  } else if (SM == SM_HidePage) {
    this->PlayCommand("SwitchedPage");
    this->PostScreenMessage(SM_ShowNextPage, PAGE_FADE_SECONDS);
  }

  ScreenAttract::HandleScreenMessage(SM);
}

float ScreenRanking::SetPage(const PageToShow& pts) {
  // This is going to take a while to load.  Possibly longer than one frame.
  // So, zero the next update so we don't skip.
  SCREENMAN->ZeroNextUpdate();

  // init page
  StepsType st = pts.types_.front().second;
  text_steps_type_.SetText(GAMEMAN->GetStepsTypeInfo(st).GetLocalizedString());

  bool bShowScores = false;
  bool bShowPoints = false;
  bool bShowTime = false;
  switch (RANKING_TYPE) {
    case RankingType_Category:
      bShowScores = true;
      break;
    case RankingType_SpecificTrail:
      bShowScores = !pts.course_->IsOni();
      bShowPoints = pts.course_->IsOni();
      bShowTime = pts.course_->IsOni();
      break;
    default:
      break;
  }

  for (int l = 0; l < NUM_RANKING_LINES; l++) {
    text_names_[l].SetDiffuseColor(STEPS_TYPE_COLOR.GetValue(pts.color_index_));

    text_scores_[l].SetVisible(bShowScores);
    text_scores_[l].SetDiffuseColor(
        STEPS_TYPE_COLOR.GetValue(pts.color_index_));

    text_points_[l].SetVisible(bShowPoints);
    text_points_[l].SetDiffuseColor(
        STEPS_TYPE_COLOR.GetValue(pts.color_index_));

    text_time_[l].SetVisible(bShowTime);
    text_time_[l].SetDiffuseColor(STEPS_TYPE_COLOR.GetValue(pts.color_index_));
  }

  RankingType rtype = RANKING_TYPE;
  switch (rtype) {
    case RankingType_Category: {
      text_category_.SetText(ssprintf("Type %c", 'A' + pts.category_));

      for (int l = 0; l < NUM_RANKING_LINES; l++) {
        // No need to shadow at this point.
        st = pts.types_.front().second;
        HighScoreList& hsl =
            PROFILEMAN->GetMachineProfile()->GetCategoryHighScoreList(
                st, pts.category_);
        HighScore hs;
        bool bRecentHighScore = false;
        if (l < (int)hsl.high_scores_.size()) {
          hs = hsl.high_scores_[l];
          std::string* psName = hsl.high_scores_[l].GetNameMutable();
          bRecentHighScore =
              find(
                  GAMESTATE->names_that_were_filled_.begin(),
                  GAMESTATE->names_that_were_filled_.end(),
                  psName) != GAMESTATE->names_that_were_filled_.end();
        } else {
          hs.SetName(NO_SCORE_NAME);
        }

        text_names_[l].SetText(hs.GetDisplayName());
        text_scores_[l].SetText(ssprintf("%09i", hs.GetScore()));
        text_names_[l].SetDiffuseColor(
            STEPS_TYPE_COLOR.GetValue(pts.color_index_));
        text_scores_[l].SetDiffuseColor(
            STEPS_TYPE_COLOR.GetValue(pts.color_index_));

        if (bRecentHighScore) {
          text_names_[l].SetEffectGlowBlink(
              0.1f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
          text_scores_[l].SetEffectGlowBlink(
              0.1f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
        } else {
          text_names_[l].StopEffect();
          text_scores_[l].StopEffect();
        }
      }
    }
      return SECONDS_PER_PAGE;
    case RankingType_SpecificTrail: {
      text_course_title_.SetText(pts.course_->GetDisplayFullTitle());

      banner_.LoadFromCourse(pts.course_);

      const HighScoreList& hsl =
          PROFILEMAN->GetMachineProfile()->GetCourseHighScoreList(
              pts.course_, pts.trail_);
      for (int l = 0; l < NUM_RANKING_LINES; l++) {
        HighScore hs;
        bool bRecentHighScore = false;
        if (l < (int)hsl.high_scores_.size()) {
          hs = hsl.high_scores_[l];
          const std::string* psName = hsl.high_scores_[l].GetNameMutable();
          bRecentHighScore =
              find(
                  GAMESTATE->names_that_were_filled_.begin(),
                  GAMESTATE->names_that_were_filled_.end(),
                  psName) != GAMESTATE->names_that_were_filled_.end();
        } else {
          hs.SetName(NO_SCORE_NAME);
        }

        text_names_[l].SetText(hs.GetDisplayName());
        if (pts.course_->IsOni()) {
          text_points_[l].SetText(ssprintf("%04d", hs.GetScore()));
          text_time_[l].SetText(SecondsToMMSSMsMs(hs.GetSurviveSeconds()));
          text_scores_[l].SetText("");
        } else {
          text_points_[l].SetText("");
          text_time_[l].SetText("");
          text_scores_[l].SetText(ssprintf("%09d", hs.GetScore()));
        }
        text_names_[l].SetDiffuseColor(
            STEPS_TYPE_COLOR.GetValue(pts.color_index_));
        text_points_[l].SetDiffuseColor(
            STEPS_TYPE_COLOR.GetValue(pts.color_index_));
        text_time_[l].SetDiffuseColor(
            STEPS_TYPE_COLOR.GetValue(pts.color_index_));
        text_scores_[l].SetDiffuseColor(
            STEPS_TYPE_COLOR.GetValue(pts.color_index_));

        if (bRecentHighScore) {
          text_names_[l].SetEffectGlowBlink(
              0.1f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
          text_scores_[l].SetEffectGlowBlink(
              0.1f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
        } else {
          text_names_[l].StopEffect();
          text_scores_[l].StopEffect();
        }
      }
    }
      return SECONDS_PER_PAGE;
    default:
      FAIL_M(ssprintf("Invalid RankingType: %i", rtype));
  }
}

/*
 * (c) 2001-2007 Chris Danford, Glenn Maynard
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
