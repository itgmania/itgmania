#include "ScreenOptionsManageCourses.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "CommonMetrics.h"
#include "CourseUtil.h"
#include "CourseWriterCRS.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"
#include "ScreenPrompt.h"
#include "SongManager.h"
#include "ThemeManager.h"
#include "global.h"

REGISTER_SCREEN_CLASS(ScreenOptionsManageCourses);

struct StepsTypeAndDifficulty {
  StepsType st_;
  Difficulty cd_;

  StepsTypeAndDifficulty(const StepsType& s, const Difficulty& d)
      : st_(s), cd_(d) {}
};

inline bool operator==(
    const StepsTypeAndDifficulty& lhs, const StepsTypeAndDifficulty& rhs) {
  return lhs.st_ == rhs.st_ && lhs.cd_ == rhs.cd_;
}
inline bool operator!=(
    const StepsTypeAndDifficulty& lhs, const StepsTypeAndDifficulty& rhs) {
  return !operator==(lhs, rhs);
}

static void SetNextCombination() {
  std::vector<StepsTypeAndDifficulty> v;
  {
    for (const StepsType& st : CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue()) {
      for (const CourseDifficulty& cd :
           CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue()) {
        v.push_back(StepsTypeAndDifficulty(st, cd));
      }
    }
  }

  StepsTypeAndDifficulty curVal(GAMESTATE->st_edit_, GAMESTATE->cd_edit_);
  std::vector<StepsTypeAndDifficulty>::const_iterator iter =
      find(v.begin(), v.end(), curVal);
  if (iter == v.end() || ++iter == v.end()) {
    iter = v.begin();
  }

  curVal = *iter;

  GAMESTATE->st_edit_.Set(curVal.st_);
  GAMESTATE->cd_edit_.Set(curVal.cd_);

  EditCourseUtil::UpdateAndSetTrail();
}

void ScreenOptionsManageCourses::Init() {
  if (PREFSMAN->arcade_options_navigation_) {
    SetNavigation(NAV_THREE_KEY_MENU);
  }

  ScreenOptions::Init();

  sound_difficulty_changed_.Load(
      THEME->GetPathS("ScreenEditCourseSubmenu", "difficulty changed"));
  EDIT_MODE.Load(name_, "EditMode");
  CREATE_NEW_SCREEN.Load(name_, "CreateNewScreen");
}

void ScreenOptionsManageCourses::BeginScreen() {
  std::vector<const Style*> vpStyles;
  GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, vpStyles);
  const Style* pStyle = vpStyles[0];
  GAMESTATE->SetCurrentStyle(pStyle, PLAYER_INVALID);

  if (GAMESTATE->st_edit_ == StepsType_Invalid ||
      GAMESTATE->cd_edit_ == Difficulty_Invalid) {
    SetNextCombination();
  }

  // Remember the current course. All Course pointers will be invalidated when
  // we load the machine profile below.
  CourseID cidLast;
  cidLast.FromCourse(GAMESTATE->cur_course_);

  std::vector<OptionRowHandler*> vHands;

  int iIndex = 0;

  {
    vHands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = vHands.back()->def_;
    def.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def.one_choice_for_all_players_ = true;
    def.name_ = "Create New Course";
    def.explanation_name_ = "Create New Course";
    def.choices_.clear();
    def.choices_.push_back("");
    iIndex++;
  }

  courses_.clear();
  // XXX: Why are we flushing here?
  FILEMAN->FlushDirCache();
  PROFILEMAN->LoadMachineProfileEdits();

  switch (EDIT_MODE.GetValue()) {
    DEFAULT_FAIL(EDIT_MODE.GetValue());
    case EditMode_Home:
      EditCourseUtil::GetAllEditCourses(courses_);
      break;
    case EditMode_Practice:
    case EditMode_Full:
      SONGMAN->GetAllCourses(courses_, false);
      break;
  }

  for (const Course* p : courses_) {
    vHands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = vHands.back()->def_;

    def.name_ = p->GetDisplayFullTitle();
    def.allow_theme_title_ = false;  // not themable
    def.explanation_name_ = "Select Course";
    def.choices_.clear();
    def.choices_.push_back("");
    def.allow_theme_items_ = false;  // already themed
    iIndex++;
  }

  ScreenOptions::InitMenu(vHands);

  ScreenOptions::BeginScreen();

  // select the last chosen course
  GAMESTATE->cur_course_.Set(cidLast.ToCourse());
  if (GAMESTATE->cur_course_) {
    EditCourseUtil::UpdateAndSetTrail();
    std::vector<Course*>::const_iterator iter =
        find(courses_.begin(), courses_.end(), GAMESTATE->cur_course_);
    if (iter != courses_.end()) {
      iIndex = iter - courses_.begin();
      this->MoveRowAbsolute(GAMESTATE->GetMasterPlayerNumber(), 1 + iIndex);
    }
  }

  AfterChangeRow(GAMESTATE->GetMasterPlayerNumber());
}

void ScreenOptionsManageCourses::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_GoToNextScreen) {
    int iCurRow = current_row_[GAMESTATE->GetMasterPlayerNumber()];

    if (iCurRow == 0)  // "create new"
    {
      /* Allocate the Course now, but don't save the file until the user
       * explicitly chooses Save */
      Course* pCourse = new Course;
      EditCourseUtil::LoadDefaults(*pCourse);
      pCourse->loaded_from_profile_ = ProfileSlot_Machine;
      SONGMAN->AddCourse(pCourse);
      GAMESTATE->cur_course_.Set(pCourse);
      EditCourseUtil::s_bNewCourseNeedsName = true;
      EditCourseUtil::UpdateAndSetTrail();

      SCREENMAN->SetNewScreen(CREATE_NEW_SCREEN);
      return;  // don't call base
    } else if (rows_[iCurRow]->GetRowType() == OptionRow::RowType_Exit) {
      this->HandleScreenMessage(SM_GoToPrevScreen);
      return;  // don't call base
    } else {
      // do base behavior
    }
  } else if (SM == SM_LoseFocus) {
    this->PlayCommand("ScreenLoseFocus");
  } else if (SM == SM_GainFocus) {
    this->PlayCommand("ScreenGainFocus");
  }

  ScreenOptions::HandleScreenMessage(SM);
}

void ScreenOptionsManageCourses::AfterChangeRow(PlayerNumber pn) {
  Course* pCourse = GetCourseWithFocus();
  Trail* pTrail =
      pCourse ? pCourse->GetTrail(GAMESTATE->st_edit_, GAMESTATE->cd_edit_)
              : nullptr;

  GAMESTATE->cur_course_.Set(pCourse);
  GAMESTATE->cur_trail_[PLAYER_1].Set(pTrail);

  ScreenOptions::AfterChangeRow(pn);
}

bool ScreenOptionsManageCourses::MenuSelect(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  SetNextCombination();
  sound_difficulty_changed_.Play(true);
  return true;
}

static LocalizedString YOU_HAVE_MAX(
    "ScreenOptionsManageCourses", "You have %d, the maximum number allowed.");
static LocalizedString YOU_MUST_DELETE(
    "ScreenOptionsManageCourses",
    "You must delete an existing before creating a new.");
void ScreenOptionsManageCourses::ProcessMenuStart(const InputEventPlus&) {
  if (IsTransitioning()) {
    return;
  }

  int iCurRow = current_row_[GAMESTATE->GetMasterPlayerNumber()];

  if (iCurRow == 0)  // "create new"
  {
    std::vector<Course*> vpCourses;
    EditCourseUtil::GetAllEditCourses(vpCourses);
    if (vpCourses.size() >= (size_t)EditCourseUtil::MAX_PER_PROFILE) {
      std::string s = ssprintf(
          (YOU_HAVE_MAX.GetValue() + "\n\n" + YOU_MUST_DELETE.GetValue())
              .c_str(),
          EditCourseUtil::MAX_PER_PROFILE);
      ScreenPrompt::Prompt(SM_None, s);
      return;
    }
    SCREENMAN->PlayStartSound();
    this->BeginFadingOut();
  } else if (rows_[iCurRow]->GetRowType() == OptionRow::RowType_Exit) {
    SCREENMAN->PlayStartSound();
    this->BeginFadingOut();
  } else  // a course
  {
    GAMESTATE->cur_course_.Set(GetCourseWithFocus());
    EditCourseUtil::UpdateAndSetTrail();
    EditCourseUtil::s_bNewCourseNeedsName = false;
    ScreenOptions::BeginFadingOut();
  }
}

void ScreenOptionsManageCourses::ImportOptions(
    int iRow, const std::vector<PlayerNumber>& vpns) {}

void ScreenOptionsManageCourses::ExportOptions(
    int iRow, const std::vector<PlayerNumber>& vpns) {}

Course* ScreenOptionsManageCourses::GetCourseWithFocus() const {
  int iCurRow = current_row_[GAMESTATE->GetMasterPlayerNumber()];
  if (iCurRow == 0) {
    return nullptr;
  } else if (rows_[iCurRow]->GetRowType() == OptionRow::RowType_Exit) {
    return nullptr;
  }

  // a course
  int index = iCurRow - 1;
  return courses_[index];
}

/*
 * (c) 2002-2004 Chris Danford
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
