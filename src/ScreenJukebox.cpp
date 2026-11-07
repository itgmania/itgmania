#include "ScreenJukebox.h"

#include <string>
#include <vector>

#include "AdjustSync.h"
#include "Attack.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "ModsGroup.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenGameplay.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "SongManager.h"
#include "SongOptions.h"
#include "SongUtil.h"
#include "StatsManager.h"
#include "StdString.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "UnlockManager.h"
#include "global.h"

#define SHOW_COURSE_MODIFIERS_PROBABILITY \
  THEME->GetMetricF(name_, "ShowCourseModifiersProbability")

REGISTER_SCREEN_CLASS(ScreenJukebox);
void ScreenJukebox::SetSong() {
  ThemeMetric<bool> ALLOW_ADVANCED_MODIFIERS(name_, "AllowAdvancedModifiers");

  std::vector<Song*> songs;

  /* Check to see if there is a theme course. If there is a course that has
   * the exact same name as the theme, then we pick a song from this course. */
  Course* p_course = SONGMAN->GetCourseFromName(THEME->GetCurThemeName());
  if (p_course != nullptr) {
    for (unsigned i = 0; i < p_course->entries_.size(); i++) {
      if (p_course->entries_[i].IsFixedSong()) {
        songs.push_back(p_course->entries_[i].song_id_.ToSong());
      }
    }
  }

  if (songs.size() == 0) {
    songs = SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);
  }
  // Still nothing?
  if (songs.size() == 0) {
    return;
  }

  // Calculate what difficulties to show
  std::vector<Difficulty> difficulties_to_show;
  if (demonstration_) {
    // HACK: This belongs in ScreenDemonstration.
    ThemeMetricDifficultiesToShow DIFFICULTIES_TO_SHOW_HERE(
        name_, "DifficultiesToShow");
    difficulties_to_show = DIFFICULTIES_TO_SHOW_HERE.GetValue();
  } else {
    if (GAMESTATE->preferred_difficulty_[PLAYER_1] != Difficulty_Invalid) {
      difficulties_to_show.push_back(
          GAMESTATE->preferred_difficulty_[PLAYER_1]);
    } else {
      FOREACH_ENUM(Difficulty, dc)
      difficulties_to_show.push_back(dc);
    }
  }

  ASSERT(!difficulties_to_show.empty());

  // Search for a Song and Steps to play during the demo.
  for (int i = 0; i < 1000; i++) {
    Song* song = songs[RandomInt(songs.size())];

    ASSERT(song != nullptr);
    if (!song->HasMusic()) {
      continue;  // skip
    }
    if (!song->NormallyDisplayed()) {
      continue;
    }
    if (!song->ShowInDemonstrationAndRanking()) {
      continue;  // skip
    }

    Difficulty dc =
        difficulties_to_show[RandomInt(difficulties_to_show.size())];
    Steps* steps = SongUtil::GetStepsByDifficulty(
        song, GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_, dc);

    if (steps == nullptr) {
      continue;  // skip
    }

    if (!PREFSMAN->autogen_steps_ && steps->IsAutogen()) {
      continue;  // skip
    }

    // Found something we can use!
    GAMESTATE->cur_song_.Set(song);
    // We just changed the song. Reset the original sync data.
    AdjustSync::ResetOriginalSyncData();
    FOREACH_PlayerNumber(p) GAMESTATE->cur_steps_[p].Set(steps);

    bool show_modifiers = randomf(0, 1) <= SHOW_COURSE_MODIFIERS_PROBABILITY;
    if (show_modifiers) {
      /* If we have a modifier course containing this song, apply its
       * modifiers. Only check fixed course entries. */
      std::vector<Course*> courses;
      SONGMAN->GetAllCourses(courses, false);
      std::vector<const CourseEntry*> options;
      std::vector<Course*> possible_courses;
      for (unsigned j = 0; j < courses.size(); ++j) {
        Course* l_course = courses[j];
        const CourseEntry* entry = l_course->FindFixedSong(song);
        if (entry == nullptr || entry->attacks_.size() == 0) {
          continue;
        }

        if (!ALLOW_ADVANCED_MODIFIERS) {
          // There are some confusing mods that we don't want to show in
          // demonstration.
          bool mods_are_ok_to_show = true;
          AttackArray attacks = entry->attacks_;
          if (!entry->modifiers_.empty()) {
            attacks.push_back(
                Attack::FromGlobalCourseModifier(entry->modifiers_));
          }
          for (const Attack& a : attacks) {
            std::string s = a.modifiers_;
            MakeLower(s);
            // todo: allow themers to modify this list? -aj
            if (s.find("dark") != std::string::npos ||
                s.find("stealth") != std::string::npos) {
              mods_are_ok_to_show = false;
              break;
            }
          }
          if (!mods_are_ok_to_show) {
            continue;  // skip
          }
        }

        options.push_back(entry);
        possible_courses.push_back(p_course);
      }

      if (!options.empty()) {
        int index = RandomInt(options.size());
        Course* l_course = possible_courses[index];

        PlayMode pm = CourseTypeToPlayMode(l_course->GetCourseType());
        GAMESTATE->play_mode_.Set(pm);
        GAMESTATE->cur_course_.Set(l_course);
        FOREACH_PlayerNumber(p) {
          GAMESTATE->cur_trail_[p].Set(l_course->GetTrail(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_));
          ASSERT(GAMESTATE->cur_trail_[p] != nullptr);
        }
      }
    }

    return;  // done looking
  }

  return;  // didn't find a song
}

ScreenJukebox::ScreenJukebox() { demonstration_ = false; }

static LocalizedString NO_MATCHING_STEPS("ScreenJukebox", "NoMatchingSteps");
void ScreenJukebox::Init() {
  // ScreenJukeboxMenu must set this
  ASSERT(GAMESTATE->GetCurrentStyle(PLAYER_INVALID) != nullptr);
  GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);

  SetSong();

  //	ASSERT( GAMESTATE->m_pCurSong );

  GAMESTATE->SetMasterPlayerNumber(PLAYER_1);

  // choose some cool options
  int benchmark = 0;
  if (benchmark) {
    /* Note that you also need to make sure you benchmark with the same notes.
     * I (who? -aj) use a copy of Maxx Unlimited with only heavy notes
     * included.*/
    FOREACH_EnabledPlayer(p) {
      /* Lots and lots of arrows. This might even bias to arrows a little
       * too much. */
      PO_GROUP_CALL(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage, Init);
      PO_GROUP_ASSIGN(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
          scroll_speed_, .25f);
      PO_GROUP_ASSIGN(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
          perspective_tilt_, -1.0f);
      PO_GROUP_ASSIGN_N(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
          effects_, PlayerOptions::EFFECT_TINY, 1.0f);
      PO_GROUP_ASSIGN(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
          life_type_, LifeType_Battery);
    }
  }

  FOREACH_EnabledPlayer(p) {
    // Reset score between songs.
    STATSMAN->cur_stage_stats_.player_[p].ResetScoreForLesson();

    // This eats any noteskins that may have been set in ScreenJukeboxMenu. -aj
    if (GAMESTATE->jukebox_uses_modifiers_) {
      PlayerOptions po;
      GAMESTATE->GetDefaultPlayerOptions(po);
      po.ChooseRandomModifiers();
      GAMESTATE->player_state_[p]->player_options_.Assign(ModsLevel_Stage, po);
    }
  }

  SongOptions so;
  GAMESTATE->GetDefaultSongOptions(so);
  GAMESTATE->song_options_.Assign(ModsLevel_Stage, so);

  FOREACH_EnabledPlayer(p) PO_GROUP_ASSIGN(
      GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage, fail_type_,
      FailType_Off);

  GAMESTATE->demonstration_or_jukebox_ = true;

  // Now that we've set up, init the base class.
  ScreenGameplay::Init();

  if (GAMESTATE->cur_song_ == nullptr)  // we didn't find a song.
  {
    SCREENMAN->SystemMessage(NO_MATCHING_STEPS);
    this->PostScreenMessage(SM_GoToPrevScreen, 0);  // Abort demonstration.
    return;
  }

  ClearMessageQueue();  // remove all of the messages set in ScreenGameplay that
                        // animate "ready", "here we go", etc.

  GAMESTATE->gameplay_lead_in_.Set(false);

  dancing_state_ = STATE_DANCING;
}

bool ScreenJukebox::Input(const InputEventPlus& input) {
  // LOG->Trace( "ScreenJukebox::Input()" );

  if (input.type_ != IET_FIRST_PRESS) {
    return false;  // ignore
  }

  switch (input.menu_i_) {
    case GAME_BUTTON_LEFT:
    case GAME_BUTTON_RIGHT:
      SCREENMAN->PostMessageToTopScreen(SM_NotesEnded, 0);
      return true;
    default:
      break;
  }

  return ScreenAttract::AttractInput(input, this);
}

void ScreenJukebox::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_NotesEnded) {
    if (out_.IsTransitioning() || out_.IsFinished()) {
      return;  // ignore - we're already fading or faded
    }
    out_.StartTransitioning(SM_GoToNextScreen);
    return;
  } else if (sm == SM_GoToNextScreen) {
    if (sound_music_) {
      sound_music_->Stop();
    }
  } else if (sm == SM_GoToStartScreen) {
    ScreenAttract::GoToStartScreen(name_);
  }

  ScreenGameplay::HandleScreenMessage(sm);
}

void ScreenJukebox::InitSongQueues() {
  ScreenGameplay::InitSongQueues();

  // Pare down to just the song in the course that we want.

  int index_to_keep = -1;
  for (unsigned i = 0; i < songs_queue_.size(); i++) {
    if (songs_queue_[i] == GAMESTATE->cur_song_) {
      index_to_keep = i;
      break;
    }
  }

  ASSERT(index_to_keep != -1);

  for (int i = (songs_queue_.size()) - 1; i >= 0; i--) {
    if (i != index_to_keep) {
      songs_queue_.erase(songs_queue_.begin() + i);
      FOREACH_EnabledPlayerInfo(player_info_, pi) {
        pi->steps_queue_.erase(pi->steps_queue_.begin() + i);
        pi->modifiers_queue_.erase(pi->modifiers_queue_.begin() + i);
      }
    }
  }

  ASSERT_M(songs_queue_.size() == 1, ssprintf("%i", (int)songs_queue_.size()));
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    ASSERT_M(
        pi->steps_queue_.size() == 1,
        ssprintf("%i", (int)pi->steps_queue_.size()));
    ASSERT_M(
        pi->modifiers_queue_.size() == 1,
        ssprintf("%i", (int)pi->modifiers_queue_.size()));
  }
}

/*
 * (c) 2003-2004 Chris Danford
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
