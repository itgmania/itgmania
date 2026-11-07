#include "ScreenUnlockStatus.h"

#include <string>

#include "Actor.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "Course.h"
#include "GameState.h"
#include "PrefsManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "SongManager.h"
#include "Sprite.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "global.h"

#define UNLOCK_TEXT_SCROLL_X \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollX");
#define UNLOCK_TEXT_SCROLL_START_Y \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollStartY")
#define UNLOCK_TEXT_SCROLL_END_Y \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollEndY")
#define UNLOCK_TEXT_SCROLL_ZOOM \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollZoom")
#define UNLOCK_TEXT_SCROLL_ROWS \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollRows")
#define UNLOCK_TEXT_SCROLL_MAX_WIDTH \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollMaxWidth")
#define UNLOCK_TEXT_SCROLL_ICON_X \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollIconX")
#define UNLOCK_TEXT_SCROLL_ICON_SIZE \
  THEME->GetMetricF("ScreenUnlockStatus", "UnlockTextScrollIconSize")
#define UNLOCK_TEXT_SCROLL \
  THEME->GetMetricI("ScreenUnlockStatus", "UnlockTextScroll")
#define TYPE_TO_DISPLAY \
  THEME->GetMetric("ScreenUnlockStatus", "TypeOfPointsToDisplay")
#define ICON_COMMAND \
  THEME->GetMetricA("ScreenUnlockStatus", "UnlockIconCommand")
#define TIME_TO_DISPLAY THEME->GetMetricF("ScreenUnlockStatus", "TimeToDisplay")
#define POINTS_ZOOM THEME->GetMetricF("ScreenUnlockStatus", "PointsZoom")

REGISTER_SCREEN_CLASS(ScreenUnlockStatus);

void ScreenUnlockStatus::Init() {
  ScreenAttract::Init();

  unsigned i_num_unlocks = UNLOCKMAN->unlock_entries_.size();

  if (!PREFSMAN->use_unlock_system_ || i_num_unlocks == 0) {
    this->PostScreenMessage(SM_GoToNextScreen, 0);
    return;
  }

  unsigned num_unlocks = UNLOCKMAN->unlock_entries_.size();

  points_until_next_unlock_.LoadFromFont(THEME->GetPathF("Common", "normal"));
  points_until_next_unlock_.SetHorizAlign(align_left);

  apActorCommands icon_command = ICON_COMMAND;
  for (unsigned i = 1; i <= num_unlocks; i++) {
    // get pertaining UnlockEntry
    const UnlockEntry& entry = UNLOCKMAN->unlock_entries_[i - 1];
    const Song* song = entry.song_.ToSong();

    if (song == nullptr) {
      continue;
    }

    Sprite* spr = new Sprite;

    // new unlock graphic
    spr->Load(THEME->GetPathG("ScreenUnlockStatus", ssprintf("%04d icon", i)));

    // set graphic location
    spr->SetName(ssprintf("Unlock%04d", i));
    LOAD_ALL_COMMANDS_AND_SET_XY(spr);

    spr->RunCommands(icon_command);
    unlocks_.push_back(spr);

    if (!entry.IsLocked()) {
      this->AddChild(unlocks_[unlocks_.size() - 1]);
    }
  }

  // scrolling text
  if (UNLOCK_TEXT_SCROLL != 0) {
    float scrolling_text_x = UNLOCK_TEXT_SCROLL_X;
    float scrolling_text_start_y = UNLOCK_TEXT_SCROLL_START_Y;
    float scrolling_text_end_y = UNLOCK_TEXT_SCROLL_END_Y;
    float scrolling_text_zoom = UNLOCK_TEXT_SCROLL_ZOOM;
    float scrolling_text_rows = UNLOCK_TEXT_SCROLL_ROWS;
    float max_width = UNLOCK_TEXT_SCROLL_MAX_WIDTH;

    float seconds_to_scroll = TIME_TO_DISPLAY;

    if (seconds_to_scroll > 2) {
      seconds_to_scroll--;
    }

    float secs_per_cycle = 0;

    if (UNLOCK_TEXT_SCROLL != 3) {
      secs_per_cycle =
          (float)seconds_to_scroll / (scrolling_text_rows + num_unlocks);
    } else {
      secs_per_cycle = (float)seconds_to_scroll /
                       (scrolling_text_rows * 3 + num_unlocks + 4);
    }

    for (unsigned i = 1; i <= num_unlocks; i++) {
      const UnlockEntry& entry = UNLOCKMAN->unlock_entries_[i - 1];

      BitmapText* text = new BitmapText;

      text->LoadFromFont(THEME->GetPathF("ScreenUnlockStatus", "text"));
      text->SetHorizAlign(align_left);
      text->SetZoom(scrolling_text_zoom);

      switch (entry.type_) {
        case UnlockRewardType_Song: {
          const Song* song = entry.song_.ToSong();
          ASSERT(song != nullptr);

          std::string title = song->GetDisplayMainTitle();
          std::string subtitle = song->GetDisplaySubTitle();
          if (subtitle != "") {
            title = title + "\n" + subtitle;
          }
          text->SetMaxWidth(max_width);
          text->SetText(title);
        } break;
        case UnlockRewardType_Course: {
          const Course* course = entry.course_.ToCourse();
          ASSERT(course != nullptr);

          text->SetMaxWidth(max_width);
          text->SetText(course->GetDisplayFullTitle());
          text->SetDiffuse(RageColor(0, 1, 0, 1));
        } break;
        default:
          text->SetText("");
          text->SetDiffuse(RageColor(0.5f, 0, 0, 1));
          break;
      }

      if (entry.IsLocked()) {
        text->SetText("???");
        text->SetZoomX(1);
      } else {
        // unlocked. change color
        const Song* song = entry.song_.ToSong();
        RageColor color = RageColor(1, 1, 1, 1);
        if (song) {
          color = SONGMAN->GetSongGroupColor(song->group_name_);
        }
        text->SetGlobalDiffuseColor(color);
      }

      text->SetXY(scrolling_text_x, scrolling_text_start_y);

      if (UNLOCK_TEXT_SCROLL == 3 &&
          UNLOCK_TEXT_SCROLL_ROWS + i >
              num_unlocks) {  // special command for last unlocks when scrolling
                              // is in effect
        float target_row = -0.5f + i + UNLOCK_TEXT_SCROLL_ROWS - num_unlocks;
        float stop_off_point =
            scrolling_text_end_y -
            target_row / UNLOCK_TEXT_SCROLL_ROWS *
                (scrolling_text_end_y - scrolling_text_start_y);
        float first_cycle_time =
            (UNLOCK_TEXT_SCROLL_ROWS - target_row) * secs_per_cycle;
        float second_cycle_time =
            (6 + target_row) * secs_per_cycle - first_cycle_time;
        // LOG->Trace("Target Row: %f", TargetRow);
        // LOG->Trace("command for icon %d: %s", i,
        // ssprintf("diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;linear,%f;y,%f;linear,0.1;diffusealpha,0",
        // SECS_PER_CYCLE * (i - 1), FirstCycleTime, StopOffPoint,
        // SecondCycleTime * 2, ScrollingTextEndY).c_str() );
        std::string command = ssprintf(
            "diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;linear,%f;y,"
            "%f;linear,0.1;diffusealpha,0",
            secs_per_cycle * (i - 1), first_cycle_time, stop_off_point,
            second_cycle_time, scrolling_text_end_y);
        text->RunCommands(ActorUtil::ParseActorCommands(command));
      } else {
        std::string command = ssprintf(
            "diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;linear,0.1;"
            "diffusealpha,0",
            secs_per_cycle * (i - 1), secs_per_cycle * (scrolling_text_rows),
            scrolling_text_end_y);
        text->RunCommands(ActorUtil::ParseActorCommands(command));
      }

      item_.push_back(text);

      if (UNLOCK_TEXT_SCROLL >= 2) {
        Sprite* icon_count = new Sprite;

        // new unlock graphic
        icon_count->Load(
            THEME->GetPathG("ScreenUnlockStatus", ssprintf("%04d icon", i)));

        // set graphic location
        icon_count->SetXY(UNLOCK_TEXT_SCROLL_ICON_X, scrolling_text_start_y);

        icon_count->SetHeight(UNLOCK_TEXT_SCROLL_ICON_SIZE);
        icon_count->SetWidth(UNLOCK_TEXT_SCROLL_ICON_SIZE);

        if (UNLOCK_TEXT_SCROLL == 3 &&
            UNLOCK_TEXT_SCROLL_ROWS + i > num_unlocks) {
          float target_row = -0.5f + i + UNLOCK_TEXT_SCROLL_ROWS - num_unlocks;
          float stop_off_point =
              scrolling_text_end_y -
              target_row / UNLOCK_TEXT_SCROLL_ROWS *
                  (scrolling_text_end_y - scrolling_text_start_y);
          float first_cycle_time =
              (UNLOCK_TEXT_SCROLL_ROWS - target_row) * secs_per_cycle;
          float second_cycle_time =
              (6 + target_row) * secs_per_cycle - first_cycle_time;
          // LOG->Trace("Target Row: %f", TargetRow);
          // LOG->Trace("command for icon %d: %s", i,
          // ssprintf("diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;linear,%f;y,%f;linear,0.1;diffusealpha,0",
          // SECS_PER_CYCLE * (i - 1), FirstCycleTime, StopOffPoint,
          // SecondCycleTime * 2, ScrollingTextEndY).c_str() );
          std::string command = ssprintf(
              "diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;linear,%f;"
              "y,%f;linear,0.1;diffusealpha,0",
              secs_per_cycle * (i - 1), first_cycle_time, stop_off_point,
              second_cycle_time, scrolling_text_end_y);
          icon_count->RunCommands(ActorUtil::ParseActorCommands(command));
        } else {
          std::string command = ssprintf(
              "diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;linear,0."
              "1;diffusealpha,0",
              secs_per_cycle * (i - 1), secs_per_cycle * (scrolling_text_rows),
              scrolling_text_end_y);
          icon_count->RunCommands(ActorUtil::ParseActorCommands(command));
        }

        item_icons_.push_back(icon_count);

        // LOG->Trace("Added unlock text %d", i);

        if (UNLOCK_TEXT_SCROLL == 3) {
          if (!entry.IsLocked()) {
            last_unlocks_.push_back(i);
          }
        }
      }
    }
  }

  if (UNLOCK_TEXT_SCROLL == 3) {
    float scrolling_text_x = UNLOCK_TEXT_SCROLL_X;
    float scrolling_text_start_y = UNLOCK_TEXT_SCROLL_START_Y;
    float scrolling_text_end_y = UNLOCK_TEXT_SCROLL_END_Y;
    float scrolling_text_rows = UNLOCK_TEXT_SCROLL_ROWS;
    float max_width = UNLOCK_TEXT_SCROLL_MAX_WIDTH;
    float seconds_to_scroll = TIME_TO_DISPLAY - 1;
    float secs_per_cycle =
        (float)seconds_to_scroll / (scrolling_text_rows * 3 + num_unlocks + 4);

    for (unsigned i = 1; i <= UNLOCK_TEXT_SCROLL_ROWS; i++) {
      if (i > last_unlocks_.size()) {
        continue;
      }

      unsigned next_icon = last_unlocks_[last_unlocks_.size() - i];

      const UnlockEntry& entry = UNLOCKMAN->unlock_entries_[next_icon - 1];
      const Song* song = entry.song_.ToSong();
      if (song == nullptr) {
        continue;
      }

      BitmapText* new_text = new BitmapText;

      new_text->LoadFromFont(THEME->GetPathF("ScreenUnlockStatus", "text"));
      new_text->SetHorizAlign(align_left);

      std::string title = song->GetDisplayMainTitle();
      std::string subtitle = song->GetDisplaySubTitle();

      if (subtitle != "") {
        title = title + "\n" + subtitle;
      }
      new_text->SetZoom(UNLOCK_TEXT_SCROLL_ZOOM);
      new_text->SetMaxWidth(max_width);
      new_text->SetText(title);

      RageColor color = SONGMAN->GetSongGroupColor(song->group_name_);
      new_text->SetGlobalDiffuseColor(color);

      new_text->SetXY(scrolling_text_x, scrolling_text_start_y);
      {
        std::string command = ssprintf(
            "diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;",
            secs_per_cycle * (num_unlocks + 2 * i - 2),
            secs_per_cycle * ((scrolling_text_rows - i) * 2 + 1),
            (scrolling_text_start_y +
             (scrolling_text_end_y - scrolling_text_start_y) *
                 (scrolling_text_rows - i + 0.5) / scrolling_text_rows));
        new_text->RunCommands(ActorUtil::ParseActorCommands(command));
      }

      // new unlock graphic
      Sprite* new_icon = new Sprite;
      new_icon->Load(THEME->GetPathG(
          "ScreenUnlockStatus", ssprintf("%04d icon", next_icon)));
      new_icon->SetXY(UNLOCK_TEXT_SCROLL_ICON_X, scrolling_text_start_y);
      new_icon->SetHeight(UNLOCK_TEXT_SCROLL_ICON_SIZE);
      new_icon->SetWidth(UNLOCK_TEXT_SCROLL_ICON_SIZE);
      {
        std::string command = ssprintf(
            "diffusealpha,0;sleep,%f;diffusealpha,1;linear,%f;y,%f;",
            secs_per_cycle * (num_unlocks + 2 * i - 2),
            secs_per_cycle * ((scrolling_text_rows - i) * 2 + 1),
            (scrolling_text_start_y +
             (scrolling_text_end_y - scrolling_text_start_y) *
                 (scrolling_text_rows - i + 0.5) / scrolling_text_rows));
        new_icon->RunCommands(ActorUtil::ParseActorCommands(command));
      }

      item_icons_.push_back(new_icon);
      item_.push_back(new_text);
    }
  }

  // NOTE: the following two loops require the iterator to
  // be ints because if you decrement an unsigned when it
  // equals zero, you get the maximum value of an unsigned,
  // which is still greater than 0.  By typecasting it as
  // an integer, you can achieve -1, which exits the loop.

  for (int i = item_.size() - 1; (int)i >= 0; i--) {
    this->AddChild(item_[i]);
  }

  for (int i = item_icons_.size() - 1; (int)i >= 0; i--) {
    this->AddChild(item_icons_[i]);
  }

  points_until_next_unlock_.SetName("PointsDisplay");

  std::string point_display = TYPE_TO_DISPLAY;
  if (point_display == "DP" || point_display == "Dance") {
    std::string dp = ssprintf(
        "%d",
        (int)UNLOCKMAN->PointsUntilNextUnlock(UnlockRequirement_DancePoints));
    points_until_next_unlock_.SetText(dp);
  } else if (point_display == "AP" || point_display == "Arcade") {
    std::string ap = ssprintf(
        "%d",
        (int)UNLOCKMAN->PointsUntilNextUnlock(UnlockRequirement_ArcadePoints));
    points_until_next_unlock_.SetText(ap);
  } else if (point_display == "SP" || point_display == "Song") {
    std::string sp = ssprintf(
        "%d",
        (int)UNLOCKMAN->PointsUntilNextUnlock(UnlockRequirement_SongPoints));
    points_until_next_unlock_.SetText(sp);
  }

  points_until_next_unlock_.SetZoom(POINTS_ZOOM);
  LOAD_ALL_COMMANDS_AND_SET_XY(points_until_next_unlock_);
  this->AddChild(&points_until_next_unlock_);

  this->ClearMessageQueue(
      SM_BeginFadingOut);  // ignore ScreenAttract's SecsToShow

  this->PostScreenMessage(SM_BeginFadingOut, TIME_TO_DISPLAY);
}

ScreenUnlockStatus::~ScreenUnlockStatus() {
  while (unlocks_.size() > 0) {
    Sprite* entry = unlocks_[unlocks_.size() - 1];
    RageUtil::SafeDelete(entry);
    unlocks_.pop_back();
  }
  while (item_.size() > 0) {
    BitmapText* entry = item_[item_.size() - 1];
    RageUtil::SafeDelete(entry);
    item_.pop_back();
  }
  while (item_icons_.size() > 0) {
    Sprite* entry = item_icons_[item_icons_.size() - 1];
    RageUtil::SafeDelete(entry);
    item_icons_.pop_back();
  }
}

/*
 * (c) 2003 Andrew Wong
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
