#include "ScreenHighScores.h"

#include <climits>
#include <cstddef>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "CourseUtil.h"
#include "Difficulty.h"
#include "DynamicActorScroller.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "ThemeManager.h"
#include "Trail.h"
#include "UnlockManager.h"
#include "global.h"

std::string COLUMN_DIFFICULTY_NAME(size_t i);
std::string COLUMN_STEPS_TYPE_NAME(size_t i);

static const char* HighScoresTypeNames[] = {
    "AllSteps", "NonstopCourses", "OniCourses", "SurvivalCourses", "AllCourses",
};
XToString(HighScoresType);
LuaXType(HighScoresType);

REGISTER_SCREEN_CLASS(ScreenHighScores);

static void GetAllSongsToShow(
    std::vector<Song*>& out, int num_most_recent_scores_to_show) {
  out.clear();
  for (Song* s : SONGMAN->GetAllSongs()) {
    if (!s->NormallyDisplayed()) {
      continue;  // skip
    }
    if (!s->ShowInDemonstrationAndRanking()) {
      continue;  // skip
    }
    out.push_back(s);
  }

  if ((int)out.size() > num_most_recent_scores_to_show) {
    SongUtil::SortSongPointerArrayByTitle(out);
    SongUtil::SortByMostRecentlyPlayedForMachine(out);
    if ((int)out.size() > num_most_recent_scores_to_show) {
      out.erase(out.begin() + num_most_recent_scores_to_show, out.end());
    }
  }
}

static void GetAllCoursesToShow(
    std::vector<Course*>& out, CourseType ct,
    int num_most_recent_scores_to_show) {
  out.clear();
  std::vector<Course*> courses;
  if (ct == CourseType_Invalid) {
    SONGMAN->GetAllCourses(courses, false);
  } else {
    SONGMAN->GetCourses(ct, courses, false);
  }

  for (Course* c : courses) {
    if (UNLOCKMAN->CourseIsLocked(c)) {
      continue;  // skip
    }
    if (!c->ShowInDemonstrationAndRanking()) {
      continue;  // skip
    }
    out.push_back(c);
  }
  if ((int)out.size() > num_most_recent_scores_to_show) {
    CourseUtil::SortCoursePointerArrayByTitle(out);
    CourseUtil::SortByMostRecentlyPlayedForMachine(out);
    if ((int)out.size() > num_most_recent_scores_to_show) {
      out.erase(out.begin() + num_most_recent_scores_to_show, out.end());
    }
  }
}

/////////////////////////////////////////////

ScoreScroller::ScoreScroller() { this->DeleteChildrenWhenDone(); }

void ScoreScroller::SetDisplay(
    const std::vector<DifficultyAndStepsType>& difficulties_to_show) {
  difficulties_to_show_ = difficulties_to_show;
  ShiftSubActors(INT_MAX);
}

bool ScoreScroller::Scroll(int dir) {
  if ((int)score_row_item_data_.size() <= SCROLLER_ITEMS_TO_DRAW) {
    return false;
  }

  float dest = GetDestinationItem();
  float old_dest = dest;
  dest += dir;
  float low_clamp = (SCROLLER_ITEMS_TO_DRAW - 1) / 2.0f;
  float high_clamp =
      score_row_item_data_.size() - (SCROLLER_ITEMS_TO_DRAW - 1) / 2.0f - 1;
  rage_clamp(dest, low_clamp, high_clamp);
  if (old_dest != dest) {
    SetDestinationItem(dest);
    return true;
  } else {
    return false;
  }
}

void ScoreScroller::ScrollTop() {
  SetCurrentAndDestinationItem((SCROLLER_ITEMS_TO_DRAW - 1) / 2.0f);
}

void ScoreScroller::ConfigureActor(Actor* actor, int i_item) {
  LOG->Trace("ScoreScroller::ConfigureActor");

  Actor& item = *dynamic_cast<Actor*>(actor);
  const ScoreRowItemData& data = score_row_item_data_[i_item];

  Message msg("Set");
  if (data.song_ != nullptr) {
    msg.SetParam("Song", data.song_);
  }
  if (data.course_ != nullptr) {
    msg.SetParam("Course", data.course_);
  }

  Lua* L = LUA->Get();
  lua_newtable(L);
  lua_pushvalue(L, -1);
  msg.SetParamFromStack(L, "Entries");

  int i = 0;
  for (DifficultyAndStepsType& iter : difficulties_to_show_) {
    Difficulty dc = iter.first;
    StepsType st = iter.second;

    if (data.song_ != nullptr) {
      const Song* song = data.song_;
      Steps* steps = SongUtil::GetStepsByDifficulty(song, st, dc, false);
      if (steps && UNLOCKMAN->StepsIsLocked(song, steps)) {
        steps = nullptr;
      }
      LuaHelpers::Push(L, steps);
    } else if (data.course_ != nullptr) {
      const Course* course = data.course_;
      Trail* trail = course->GetTrail(st, dc);
      LuaHelpers::Push(L, trail);
    }
    // Because pSteps or pTrail can be nullptr, what we're creating in Lua is
    // not an array. It must be iterated using pairs(), not ipairs().
    lua_setfield(L, -2, ssprintf("%d", i + 1).c_str());
    i++;
  }
  lua_pop(L, 1);
  LUA->Release(L);

  item.HandleMessage(msg);
  LOG->Trace("end ScoreScroller::ConfigureActor");
}

void ScoreScroller::LoadSongs(int num_recent_scores) {
  std::vector<Song*> songs;
  GetAllSongsToShow(songs, num_recent_scores);
  score_row_item_data_.resize(songs.size());
  for (unsigned i = 0; i < score_row_item_data_.size(); ++i) {
    score_row_item_data_[i].song_ = songs[i];
  }
}

void ScoreScroller::LoadCourses(CourseType ct, int num_recent_scores) {
  std::vector<Course*> courses;
  GetAllCoursesToShow(courses, ct, num_recent_scores);
  score_row_item_data_.resize(courses.size());
  for (unsigned i = 0; i < score_row_item_data_.size(); ++i) {
    score_row_item_data_[i].course_ = courses[i];
  }
}

void ScoreScroller::Load(std::string metrics_group) {
  SCROLLER_ITEMS_TO_DRAW.Load(metrics_group, "ScrollerItemsToDraw");
  SCROLLER_SECONDS_PER_ITEM.Load(metrics_group, "ScrollerSecondsPerItem");

  int num_copies = SCROLLER_ITEMS_TO_DRAW + 1;
  for (int i = 0; i < num_copies; ++i) {
    Actor* actor =
        ActorUtil::MakeActor(THEME->GetPathG(metrics_group, "ScrollerItem"));
    if (actor != nullptr) {
      this->AddChild(actor);
    }
  }

  DynamicActorScroller::SetTransformFromReference(
      THEME->GetMetricR(metrics_group, "ScrollerItemTransformFunction"));
  DynamicActorScroller::SetNumItemsToDraw((float)SCROLLER_ITEMS_TO_DRAW);
  DynamicActorScroller::SetSecondsPerItem(SCROLLER_SECONDS_PER_ITEM);
  DynamicActorScroller::Load2();

  num_items_ = score_row_item_data_.size();
}

/////////////////////////////////////////////

std::string COLUMN_DIFFICULTY_NAME(size_t i) {
  return ssprintf("ColumnDifficulty%d", int(i + 1));
}
std::string COLUMN_STEPS_TYPE_NAME(size_t i) {
  return ssprintf("ColumnStepsType%d", int(i + 1));
}

void ScreenHighScores::Init() {
  ScreenAttract::Init();

  MANUAL_SCROLLING.Load(name_, "ManualScrolling");
  HIGH_SCORES_TYPE.Load(name_, "HighScoresType");
  NUM_COLUMNS.Load(name_, "NumColumns");
  COLUMN_DIFFICULTY.Load(name_, COLUMN_DIFFICULTY_NAME, NUM_COLUMNS);
  COLUMN_STEPS_TYPE.Load(name_, COLUMN_STEPS_TYPE_NAME, NUM_COLUMNS);
  MAX_ITEMS_TO_SHOW.Load(name_, "MaxItemsToShow");

  scroller_.SetName("Scroller");
  LOAD_ALL_COMMANDS(scroller_);
  HighScoresType type = HIGH_SCORES_TYPE;
  switch (type) {
    DEFAULT_FAIL(type);
    case HighScoresType_AllSteps:
      scroller_.LoadSongs(MAX_ITEMS_TO_SHOW);
      break;
    case HighScoresType_NonstopCourses:
    case HighScoresType_OniCourses:
    case HighScoresType_SurvivalCourses:
    case HighScoresType_AllCourses: {
      CourseType ct;
      switch (type) {
        default:
          FAIL_M(ssprintf("Invalid HighScoresType: %i", type));
        case HighScoresType_NonstopCourses:
          ct = COURSE_TYPE_NONSTOP;
          break;
        case HighScoresType_OniCourses:
          ct = COURSE_TYPE_ONI;
          break;
        case HighScoresType_SurvivalCourses:
          ct = COURSE_TYPE_SURVIVAL;
          break;
        case HighScoresType_AllCourses:
          ct = CourseType_Invalid;
          break;
      }

      scroller_.LoadCourses(ct, MAX_ITEMS_TO_SHOW);
    } break;
  }

  scroller_.Load(name_);
  this->AddChild(&scroller_);
}

void ScreenHighScores::BeginScreen() {
  ScreenAttract::BeginScreen();

  std::vector<DifficultyAndStepsType> vdast;
  for (int i = 0; i < NUM_COLUMNS; i++) {
    DifficultyAndStepsType dast(
        COLUMN_DIFFICULTY.GetValue(i), COLUMN_STEPS_TYPE.GetValue(i));
    vdast.push_back(dast);
  }

  scroller_.SetDisplay(vdast);

  if ((bool)MANUAL_SCROLLING) {
    scroller_.ScrollTop();
  } else {
    scroller_.ScrollThroughAllItems();
  }

  if (!MANUAL_SCROLLING) {
    float secs = scroller_.GetSecondsForCompleteScrollThrough();
    this->PostScreenMessage(SM_BeginFadingOut, secs);
  }
}

bool ScreenHighScores::Input(const InputEventPlus& input) {
  // LOG->Trace( "ScreenRanking::Input()" );
  if (IsTransitioning()) {
    return false;
  }

  // If manually scrolling, pass the input to Screen::Input so it will call
  // Menu*
  if ((bool)MANUAL_SCROLLING) {
    return Screen::Input(input);
  } else {
    return ScreenAttract::Input(input);
  }
}

void ScreenHighScores::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_BeginFadingOut) /* Screen is starting to tween out. */
  {
    StartTransitioningScreen(SM_GoToNextScreen);
  }

  ScreenAttract::HandleScreenMessage(sm);
}

bool ScreenHighScores::MenuStart(const InputEventPlus& input) {
  if (!MANUAL_SCROLLING) {
    return false;
  }
  if (!IsTransitioning()) {
    StartTransitioningScreen(SM_GoToNextScreen);
  }
  SCREENMAN->PlayStartSound();
  return true;
}

bool ScreenHighScores::MenuBack(const InputEventPlus& input) {
  if (!MANUAL_SCROLLING) {
    return false;
  }
  if (!IsTransitioning()) {
    StartTransitioningScreen(SM_GoToNextScreen);
  }
  SCREENMAN->PlayStartSound();
  return true;
}

void ScreenHighScores::DoScroll(int dir) {
  if (!scroller_.Scroll(dir)) {
    dir = 0;
  }
  Message msg("Scrolled");
  msg.SetParam("Dir", dir);
  this->HandleMessage(msg);
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
