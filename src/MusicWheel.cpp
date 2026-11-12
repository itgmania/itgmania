#include "MusicWheel.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Command.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "CourseUtil.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameCommand.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Group.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "MusicWheelItem.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "RageUtil_AutoPtr.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "WheelBase.h"
#include "WheelItemBase.h"
#include "global.h"

static Preference<bool> g_bMoveRandomToEnd("MoveRandomToEnd", false);
static Preference<bool> g_bPrecacheAllSorts("PreCacheAllWheelSorts", false);

#define NUM_WHEEL_ITEMS ((int)std::ceil(NUM_WHEEL_ITEMS_TO_DRAW + 2))
#define WHEEL_TEXT(s) \
  THEME->GetString("MusicWheel", ssprintf("%sText", s.c_str()));
#define CUSTOM_ITEM_WHEEL_TEXT(s) \
  THEME->GetString("MusicWheel", ssprintf("CustomItem%sText", s.c_str()));

static std::string SECTION_COLORS_NAME(size_t i) {
  return ssprintf("SectionColor%d", int(i + 1));
}
static std::string CHOICE_NAME(std::string s) {
  return ssprintf("Choice%s", s.c_str());
}
static std::string CUSTOM_WHEEL_ITEM_NAME(std::string s) {
  return ssprintf("CustomWheelItem%s", s.c_str());
}
static std::string CUSTOM_WHEEL_ITEM_COLOR(std::string s) {
  return ssprintf("%sColor", s.c_str());
}

static LocalizedString EMPTY_STRING("MusicWheel", "Empty");

AutoScreenMessage(
    SM_SongChanged);  // TODO: Replace this with a Message and MESSAGEMAN
AutoScreenMessage(SM_SortOrderChanging);
AutoScreenMessage(SM_SortOrderChanged);

static SortOrder ForceAppropriateSort(PlayMode pm, SortOrder so) {
  switch (pm) {
    // in course modes, force a particular sort
    case PLAY_MODE_ONI:
      return SORT_ONI_COURSES;
    case PLAY_MODE_NONSTOP:
      return SORT_NONSTOP_COURSES;
    case PLAY_MODE_ENDLESS:
      return SORT_ENDLESS_COURSES;
    default:
      break;
  }

  // If we're not in a course mode, don't start in a course sort.
  switch (so) {
    case SORT_ONI_COURSES:
    case SORT_NONSTOP_COURSES:
    case SORT_ENDLESS_COURSES:
      so = SortOrder_Invalid;
      break;
    default:
      break;
  }

  return so;
}

MusicWheelItem* MusicWheel::MakeItem() { return new MusicWheelItem; }

void MusicWheel::Load(std::string type) {
  ROULETTE_SWITCH_SECONDS.Load(type, "RouletteSwitchSeconds");
  ROULETTE_SLOW_DOWN_SWITCHES.Load(type, "RouletteSlowDownSwitches");
  NUM_SECTION_COLORS.Load(type, "NumSectionColors");
  SONG_REAL_EXTRA_COLOR.Load(type, "SongRealExtraColor");
  SORT_MENU_COLOR.Load(type, "SortMenuColor");
  SHOW_ROULETTE.Load(type, "ShowRoulette");
  SHOW_RANDOM.Load(type, "ShowRandom");
  SHOW_PORTAL.Load(type, "ShowPortal");
  RANDOM_PICKS_LOCKED_SONGS.Load(type, "RandomPicksLockedSongs");
  MOST_PLAYED_SONGS_TO_SHOW.Load(type, "MostPlayedSongsToShow");
  RECENT_SONGS_TO_SHOW.Load(type, "RecentSongsToShow");
  MODE_MENU_CHOICE_NAMES.Load(type, "ModeMenuChoiceNames");
  SORT_ORDERS.Load(type, "SortOrders");
  SHOW_EASY_FLAG.Load(type, "UseEasyMarkerFlag");
  USE_SECTIONS_WITH_PREFERRED_GROUP.Load(type, "UseSectionsWithPreferredGroup");
  HIDE_INACTIVE_SECTIONS.Load(type, "OnlyShowActiveSection");
  HIDE_INACTIVE_PARENT_SECTIONS.Load(type, "OnlyShowActiveParentSection");
  HIDE_ACTIVE_SECTION_TITLE.Load(type, "HideActiveSectionTitle");
  REMIND_WHEEL_POSITIONS.Load(type, "RemindWheelPositions");
  std::vector<std::string> mode_choice_names;
  split(MODE_MENU_CHOICE_NAMES, ",", mode_choice_names);
  CHOICE.Load(type, CHOICE_NAME, mode_choice_names);
  SECTION_COLORS.Load(type, SECTION_COLORS_NAME, NUM_SECTION_COLORS);

  CUSTOM_WHEEL_ITEM_NAMES.Load(type, "CustomWheelItemNames");
  std::vector<std::string> custom_item_names;
  split(CUSTOM_WHEEL_ITEM_NAMES, ",", custom_item_names);
  CUSTOM_CHOICES.Load(type, CUSTOM_WHEEL_ITEM_NAME, custom_item_names);
  CUSTOM_CHOICE_COLORS.Load(type, CUSTOM_WHEEL_ITEM_COLOR, custom_item_names);

  ROULETTE_COLOR.Load(type, "RouletteColor");
  RANDOM_COLOR.Load(type, "RandomColor");
  PORTAL_COLOR.Load(type, "PortalColor");
  EMPTY_COLOR.Load(type, "EmptyColor");

  WheelBase::Load(type);

  SONGMAN->UpdateRankingCourses();

  sound_change_sort_.Load(THEME->GetPathS(type, "sort"));
  sound_expand_.Load(THEME->GetPathS(type, "expand"), true);
  sound_collapse_.Load(THEME->GetPathS(type, "collapse"), true);

  // Update for SORT_MOST_PLAYED.
  SONGMAN->UpdatePopular();

  /* Sort SONGMAN's songs by CompareSongPointersByTitle, so we can do other
   * sorts (with stable_sort) from its output, and title will be the secondary
   * sort, without having to re-sort by title each time. */
  SONGMAN->SortSongs();

  FOREACH_ENUM(SortOrder, so) { wheel_item_datas_status_[so] = INVALID; }
}

void MusicWheel::BeginScreen() {
  RageTimer timer;
  std::string times;
  FOREACH_ENUM(SortOrder, so) {
    if (wheel_item_datas_status_[so] != INVALID) {
      wheel_item_datas_status_[so] = NEEDREFILTER;
    }

    if (g_bPrecacheAllSorts) {
      readyWheelItemsData(so);
      times += ssprintf("%i:%.3f ", so, timer.GetDeltaTime());
    }
  }
  if (g_bPrecacheAllSorts) {
    LOG->Trace("MusicWheel sorting took: %s", times.c_str());
  }

  // Set m_LastModeMenuItem to the first item that matches the current mode. (Do
  // this after building wheel item data.)
  {
    const std::vector<MusicWheelItemData*>& from =
        getWheelItemsData(SORT_MODE_MENU);
    for (unsigned i = 0; i < from.size(); i++) {
      ASSERT(!from[i]->action_.isNull());
      if (from[i]->action_->DescribesCurrentModeForAllPlayers()) {
        last_mode_menu_item_ = from[i]->action_->name_;
        break;
      }
    }
  }

  WheelBase::BeginScreen();

  if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
    wheel_state_ = STATE_LOCKED;
    SCREENMAN->PlayStartSound();
    locked_wheel_velocity_ = 0;
  }

  GAMESTATE->sort_order_.Set(GAMESTATE->preferred_sort_order_);

  // Never start in the mode menu; some elements may not initialize correctly.
  if (GAMESTATE->sort_order_ == SORT_MODE_MENU) {
    GAMESTATE->sort_order_.Set(SortOrder_Invalid);
  }

  GAMESTATE->sort_order_.Set(
      ForceAppropriateSort(GAMESTATE->play_mode_, GAMESTATE->sort_order_));

  /* Only save the sort order if the player didn't already have one.
   * If he did, don't overwrite it. */
  if (GAMESTATE->preferred_sort_order_ == SortOrder_Invalid) {
    GAMESTATE->preferred_sort_order_ = GAMESTATE->sort_order_;
  }

  if (GAMESTATE->preferred_song_group_ != GROUP_ALL) {
    // If a preferred song group is set, open the group and select the
    // first song in the group. -aj
    if (!GAMESTATE->IsCourseMode()) {
      std::vector<Song*> temp =
          SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);
      ASSERT(temp.size() > 0);
      GAMESTATE->cur_song_.Set(temp[0]);
    };
    SetOpenSection(GAMESTATE->preferred_song_group_);
    SelectSongOrCourse();
  } else if (!SelectSongOrCourse()) {
    // Select the the previously selected song (if any)
    SetOpenSection("");
  }

  if (REMIND_WHEEL_POSITIONS && HIDE_INACTIVE_SECTIONS) {
    // store the group song index, run this also here because it forgets the
    // current position when not changing the song if you came back from
    // gameplay or your last round song (profiles) is not the first one in the
    // group.
    for (unsigned idx = 0; idx < wheel_positions_.size(); idx++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(idx)) {
        wheel_positions_[idx] = selection_;
      }
    }
  }

  // rebuild the WheelItems that appear on screen
  RebuildWheelItems();

  /* Invalidate current Song if it can't be played
   * because there are not enough stages remaining. */
  if (GAMESTATE->cur_song_ != nullptr &&
      GameState::GetNumStagesMultiplierForSong(GAMESTATE->cur_song_) >
          GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer()) {
    GAMESTATE->cur_song_.Set(nullptr);
  }

  /* Invalidate current Steps if it can't be played
   * because there are not enough stages remaining. */
  FOREACH_ENUM(PlayerNumber, p) {
    if (GAMESTATE->cur_steps_[p] != nullptr) {
      std::vector<Steps*> possible_steps;
      if (GAMESTATE->cur_song_ != nullptr) {
        SongUtil::GetPlayableSteps(GAMESTATE->cur_song_, possible_steps);
      }
      bool steps_is_possible =
          find(
              possible_steps.begin(), possible_steps.end(),
              GAMESTATE->cur_steps_[p]) != possible_steps.end();
      if (!steps_is_possible) {
        GAMESTATE->cur_steps_[p].Set(nullptr);
      }
    }
  }
}

MusicWheel::~MusicWheel() {
  FOREACH_ENUM(SortOrder, so) {
    std::vector<MusicWheelItemData*>::iterator i =
        un_filtered_wheel_item_datas_[so].begin();
    std::vector<MusicWheelItemData*>::iterator end =
        un_filtered_wheel_item_datas_[so].end();
    for (; i != end; ++i) {
      delete *i;
    }
  }
}

void MusicWheel::ReloadSongList() {
  int song_idx_to_preserve = selection_;
  // Remove the song from any sorting caches:
  FOREACH_ENUM(SortOrder, so) { wheel_item_datas_status_[so] = INVALID; }
  // rebuild the info associated with this sort order
  readyWheelItemsData(GAMESTATE->sort_order_);
  // re-open the section to refresh song counts, etc.
  SetOpenSections(expanded_parent_section_name_, expanded_section_name_);
  // navigate to the song nearest to what was previously selected
  selection_ = song_idx_to_preserve;
  RebuildWheelItems();
  // refresh the song preview
  SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);
}

/* If a song or course is set in GAMESTATE and available, select it.  Otherwise,
 * choose the first available song or course.  Return true if an item was set,
 * false if no items are available. */
bool MusicWheel::SelectSongOrCourse() {
  if (GAMESTATE->preferred_song_ && SelectSong(GAMESTATE->preferred_song_)) {
    return true;
  }
  if (GAMESTATE->cur_song_ && SelectSong(GAMESTATE->cur_song_)) {
    return true;
  }
  if (GAMESTATE->preferred_course_ &&
      SelectCourse(GAMESTATE->preferred_course_)) {
    return true;
  }
  if (GAMESTATE->cur_course_ && SelectCourse(GAMESTATE->cur_course_)) {
    return true;
  }

  // Select the first selectable song based on the sort order...
  std::vector<MusicWheelItemData*>& wheel_items =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (unsigned i = 0; i < wheel_items.size(); i++) {
    if (wheel_items[i]->song_) {
      return SelectSong(wheel_items[i]->song_);
    } else if (wheel_items[i]->course_) {
      return SelectCourse(wheel_items[i]->course_);
    }
  }

  LOG->Trace(
      "MusicWheel::MusicWheel() - No selectable songs or courses found in "
      "WheelData");
  return false;
}

bool MusicWheel::SelectSection(const std::string& section_name) {
  for (unsigned int i = 0; i < cur_wheel_item_data_.size(); ++i) {
    if (cur_wheel_item_data_[i]->text_ == section_name) {
      selection_ = i;  // select it
      return true;
    }
  }

  return false;
}

bool MusicWheel::HasSongs() {
  std::vector<MusicWheelItemData*>& wheel_items =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (MusicWheelItemData* item : wheel_items) {
    if (item->type_ == WheelItemDataType_Song) {
      return true;
    }
  }
  return false;
}

bool MusicWheel::SelectSong(const Song* p) {
  if (p == nullptr) {
    return false;
  }

  unsigned i;
  std::vector<MusicWheelItemData*>& from =
      getWheelItemsData(GAMESTATE->sort_order_);
  if (GAMESTATE->last_open_section_ != "" &&
      (GAMESTATE->sort_order_ == SORT_PREFERRED ||
       GAMESTATE->sort_order_ == SORT_METER)) {
    // Return to the last open section if it is defined and exists in the
    // current sort
    for (i = 0; i < from.size(); i++) {
      if (from[i]->text_ == GAMESTATE->last_open_section_) {
        // make its group the currently expanded group
        SetOpenSection(from[i]->text_);
        break;
      }
    }
  } else {
    for (i = 0; i < from.size(); i++) {
      if (from[i]->song_ == p) {
        if (!from[i]->parent_section_.empty()) {
          SetOpenSections(from[i]->parent_section_, from[i]->text_);
        } else {
          // make its group the currently expanded group
          SetOpenSection(from[i]->text_);
        }
        break;
      }
    }
  }
  if (i == from.size()) {
    return false;
  }

  for (i = 0; i < cur_wheel_item_data_.size(); i++) {
    const MusicWheelItemData* wid = GetCurWheelItemData(i);
    if (wid->song_ != p) {
      continue;
    }
    selection_ = i;  // select it (fallback if no exact pin match is found)

    // We check for matching pinned steps for the scenario where the same
    // song is listed multiple times in the wheel, each pinned to different
    // difficulties - we want to select the entry that matches our current
    // pinned steps
    bool pin_matches = false;
    if (GAMESTATE->HasPinnedSteps()) {
      std::vector<Steps*> wid_pinned;
      wid->GetPinnedStepsList(wid_pinned);
      pin_matches = (wid_pinned == GAMESTATE->pinned_steps_);
    } else {
      pin_matches = !wid->IsPinned();
    }
    if (pin_matches) {
      break;
    }
  }
  return true;
}

bool MusicWheel::SelectCourse(const Course* p) {
  if (p == nullptr) {
    return false;
  }

  unsigned i;
  std::vector<MusicWheelItemData*>& from =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (i = 0; i < from.size(); i++) {
    if (from[i]->course_ == p) {
      // make its group the currently expanded group
      if (!from[i]->parent_section_.empty()) {
        SetOpenSections(from[i]->parent_section_, from[i]->text_);
      } else {
        SetOpenSection(from[i]->text_);
      }
      break;
    }
  }

  if (i == from.size()) {
    return false;
  }

  for (i = 0; i < cur_wheel_item_data_.size(); i++) {
    if (GetCurWheelItemData(i)->course_ == p) {
      selection_ = i;  // select it
    }
  }

  return true;
}

bool MusicWheel::SelectModeMenuItem() {
  // Select the last-chosen option.
  ASSERT(GAMESTATE->sort_order_ == SORT_MODE_MENU);
  const std::vector<MusicWheelItemData*>& from =
      getWheelItemsData(GAMESTATE->sort_order_);
  unsigned i;
  for (i = 0; i < from.size(); i++) {
    const GameCommand& gc = *from[i]->action_;
    if (gc.name_ == last_mode_menu_item_) {
      break;
    }
  }
  if (i == from.size()) {
    return false;
  }

  // make its group the currently expanded group
  SetOpenSection(from[i]->text_);

  for (i = 0; i < cur_wheel_item_data_.size(); i++) {
    if (GetCurWheelItemData(i)->action_->name_ != last_mode_menu_item_) {
      continue;
    }
    selection_ = i;  // select it
    break;
  }

  return true;
}

// bool MusicWheel::SelectCustomItem()

void MusicWheel::GetSongList(std::vector<Song*>& array_songs, SortOrder so) {
  std::vector<Song*> all_songs;
  switch (so) {
    case SORT_PREFERRED:
      SONGMAN->GetPreferredSortSongs(all_songs);
      break;
    case SORT_POPULARITY:
      all_songs = SONGMAN->GetPopularSongs();
      break;
    case SORT_SERIES:
    case SORT_GROUP:
      // if we're not using sections with a preferred song group, and there
      // is a group to load, only load those songs. -aj
      if (GAMESTATE->preferred_song_group_ != GROUP_ALL &&
          !USE_SECTIONS_WITH_PREFERRED_GROUP) {
        all_songs = SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);
        break;
      }
      [[fallthrough]];
    default:
      all_songs = SONGMAN->GetAllSongs();
      break;
  }

  FOREACH_PlayerNumber(pn) {
    if (GAMESTATE->IsPlayerEnabled(pn)) {
      Profile* prof = PROFILEMAN->GetProfile(pn);
      for (size_t i = 0; i < prof->songs_.size(); ++i) {
        all_songs.push_back(prof->songs_[i]);
      }
    }
  }

  // filter songs that we don't have enough stages to play
  {
    std::vector<Song*> temp_songs;
    SongCriteria sc;
    sc.max_stages_for_song_ =
        GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer();
    SongUtil::FilterSongs(sc, all_songs, temp_songs);
    all_songs = temp_songs;
  }

  // copy only songs that have at least one Steps for the current GameMode
  for (unsigned i = 0; i < all_songs.size(); i++) {
    Song* song = all_songs[i];

    int locked = UNLOCKMAN->SongIsLocked(song);
    if (locked & LOCKED_DISABLED) {
      continue;
    }

    // If we're on an extra stage, and this song is selected, ignore
    // #SELECTABLE.
    if (song != GAMESTATE->cur_song_ || !GAMESTATE->IsAnExtraStage()) {
      // Hide songs that asked to be hidden via #SELECTABLE.
      if (locked & LOCKED_SELECTABLE) {
        continue;
      }
      if (so != SORT_ROULETTE && locked & LOCKED_ROULETTE) {
        continue;
      }
    }

    /* Hide locked songs. If RANDOM_PICKS_LOCKED_SONGS, hide in Roulette
     * and Random, too. */
    if ((so != SORT_ROULETTE || !RANDOM_PICKS_LOCKED_SONGS) && locked) {
      continue;
    }

    if (PREFSMAN->only_preferred_difficulties_) {
      // if the song has steps that fit the preferred difficulty of the default
      // player
      if (song->HasStepsTypeAndDifficulty(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_,
              GAMESTATE
                  ->preferred_difficulty_[GAMESTATE->GetFirstHumanPlayer()])) {
        array_songs.push_back(song);
      }
    } else if (
        CommonMetrics::AUTO_SET_STYLE ||
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID) == nullptr) {
      if (SongUtil::IsSongPlayable(song)) {
        array_songs.push_back(song);
      }

    } else {
      // If the song has at least one steps, add it.
      if (song->HasStepsType(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
        array_songs.push_back(song);
      }
    }
  }

  /* Hack: Add extra stage item if it was eliminated for any reason
   * (eg. it's a long song). */
  if (GAMESTATE->IsAnExtraStage()) {
    Song* song;
    Steps* steps;
    SONGMAN->GetExtraStageInfo(
        GAMESTATE->IsExtraStage2(), GAMESTATE->GetCurrentStyle(PLAYER_INVALID),
        song, steps);

    if (find(array_songs.begin(), array_songs.end(), song) ==
        array_songs.end()) {
      array_songs.push_back(song);
    }
  }
}

void MusicWheel::BuildWheelItemDatas(
    std::vector<MusicWheelItemData*>& array_wheel_item_datas, SortOrder so) {
  switch (so) {
    case SORT_MODE_MENU: {
      array_wheel_item_datas.clear();  // clear out the previous wheel items
      std::vector<std::string> names;
      split(MODE_MENU_CHOICE_NAMES, ",", names);
      for (unsigned i = 0; i < names.size(); ++i) {
        MusicWheelItemData wid(
            WheelItemDataType_Sort, nullptr, "", nullptr, nullptr,
            SORT_MENU_COLOR, 0);
        wid.action_ = HiddenPtr<GameCommand>(new GameCommand);
        wid.action_->name_ = names[i];
        wid.action_->Load(i, ParseCommands(CHOICE.GetValue(names[i])));
        wid.label_ = WHEEL_TEXT(names[i]);

        if (!wid.action_->IsPlayable()) {
          continue;
        }

        array_wheel_item_datas.push_back(new MusicWheelItemData(wid));
      }
      break;
    }
    case SORT_METER:
    case SORT_PREFERRED:
    case SORT_ROULETTE:
    case SORT_SERIES:
    case SORT_GROUP:
    case SORT_TITLE:
    case SORT_BPM:
    case SORT_POPULARITY:
    case SORT_POPULARITY_P1:
    case SORT_POPULARITY_P2:
    case SORT_TOP_GRADES:
    case SORT_TOP_GRADES_P1:
    case SORT_TOP_GRADES_P2:
    case SORT_ARTIST:
    case SORT_GENRE:
    case SORT_BEGINNER_METER:
    case SORT_EASY_METER:
    case SORT_MEDIUM_METER:
    case SORT_HARD_METER:
    case SORT_CHALLENGE_METER:
    case SORT_DOUBLE_EASY_METER:
    case SORT_DOUBLE_MEDIUM_METER:
    case SORT_DOUBLE_HARD_METER:
    case SORT_DOUBLE_CHALLENGE_METER:
    case SORT_LENGTH:
    case SORT_RECENT:
    case SORT_RECENT_P1:
    case SORT_RECENT_P2: {
      // Make an array of Song*, then sort them
      std::vector<Song*> array_songs;
      GetSongList(array_songs, so);

      bool use_sections = true;

      // sort the songs
      switch (so) {
        case SORT_METER:
          SONGMAN->UpdateMeterSort(array_songs);
          break;
        case SORT_PREFERRED:
          // obey order specified by the preferred sort list
          break;
        case SORT_ROULETTE: {
          StepsType st;
          Difficulty dc;
          SongUtil::GetStepsTypeAndDifficultyFromSortOrder(
              SORT_EASY_METER, st, dc);
          SongUtil::SortSongPointerArrayByStepsTypeAndMeter(
              array_songs, st, dc);
          if ((bool)PREFSMAN->preferred_sort_uses_groups_) {
            stable_sort(
                array_songs.begin(), array_songs.end(),
                SongUtil::CompareSongPointersByGroup);
          }
          use_sections = false;
          break;
        }
        case SORT_SERIES:
        case SORT_GROUP:
          SongUtil::SortSongPointerArrayByGroupAndTitle(array_songs);
          if (USE_SECTIONS_WITH_PREFERRED_GROUP) {
            use_sections = true;
          } else {
            use_sections = GAMESTATE->preferred_song_group_ == GROUP_ALL;
          }
          break;
        case SORT_TITLE:
          SongUtil::SortSongPointerArrayByTitle(array_songs);
          break;
        case SORT_BPM:
          SongUtil::SortSongPointerArrayByBPM(array_songs);
          break;
        case SORT_POPULARITY:
          if ((int)array_songs.size() > MOST_PLAYED_SONGS_TO_SHOW) {
            array_songs.erase(
                array_songs.begin() + MOST_PLAYED_SONGS_TO_SHOW,
                array_songs.end());
          }
          use_sections = true;
          break;
        case SORT_POPULARITY_P1:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_1)) {
            SongUtil::SortSongPointerArrayByNumPlays(
                array_songs, ProfileSlot_Player1, true);
          }
          if ((int)array_songs.size() > MOST_PLAYED_SONGS_TO_SHOW) {
            array_songs.erase(
                array_songs.begin() + MOST_PLAYED_SONGS_TO_SHOW,
                array_songs.end());
          }
          use_sections = true;
          break;
        case SORT_POPULARITY_P2:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_2)) {
            SongUtil::SortSongPointerArrayByNumPlays(
                array_songs, ProfileSlot_Player2, true);
          }
          if ((int)array_songs.size() > MOST_PLAYED_SONGS_TO_SHOW) {
            array_songs.erase(
                array_songs.begin() + MOST_PLAYED_SONGS_TO_SHOW,
                array_songs.end());
          }
          use_sections = true;
          break;
        case SORT_TOP_GRADES:
          SongUtil::SortSongPointerArrayByGrades(array_songs, true);
          break;
        case SORT_TOP_GRADES_P1:
          // Check if player profile is persistent
          if (PROFILEMAN->IsPersistentProfile(PLAYER_1)) {
            SongUtil::SortSongPointerArrayByProfileGrades(
                array_songs, true, PLAYER_1);
          }
          break;
        case SORT_TOP_GRADES_P2:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_2)) {
            SongUtil::SortSongPointerArrayByProfileGrades(
                array_songs, true, PLAYER_2);
          }
          break;
        case SORT_ARTIST:
          SongUtil::SortSongPointerArrayByArtist(array_songs);
          break;
        case SORT_GENRE:
          SongUtil::SortSongPointerArrayByGenre(array_songs);
          break;
        case SORT_LENGTH:
          SongUtil::SortSongPointerArrayByLength(array_songs);
          break;
        case SORT_RECENT:
          SongUtil::SortByMostRecentlyPlayedForMachine(array_songs);
          if ((int)array_songs.size() > RECENT_SONGS_TO_SHOW) {
            array_songs.erase(
                array_songs.begin() + RECENT_SONGS_TO_SHOW, array_songs.end());
          }
          use_sections = true;
          break;
        case SORT_RECENT_P1:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_1)) {
            SongUtil::SortByMostRecentlyPlayedForProfile(array_songs, PLAYER_1);
          }
          if ((int)array_songs.size() > RECENT_SONGS_TO_SHOW) {
            array_songs.erase(
                array_songs.begin() + RECENT_SONGS_TO_SHOW, array_songs.end());
          }
          use_sections = true;
          break;
        case SORT_RECENT_P2:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_2)) {
            SongUtil::SortByMostRecentlyPlayedForProfile(array_songs, PLAYER_2);
          }
          if ((int)array_songs.size() > RECENT_SONGS_TO_SHOW) {
            array_songs.erase(
                array_songs.begin() + RECENT_SONGS_TO_SHOW, array_songs.end());
          }
          use_sections = true;
          break;
        case SORT_BEGINNER_METER:
        case SORT_EASY_METER:
        case SORT_MEDIUM_METER:
        case SORT_HARD_METER:
        case SORT_CHALLENGE_METER:
        case SORT_DOUBLE_EASY_METER:
        case SORT_DOUBLE_MEDIUM_METER:
        case SORT_DOUBLE_HARD_METER:
        case SORT_DOUBLE_CHALLENGE_METER:
          StepsType st;
          Difficulty dc;
          SongUtil::GetStepsTypeAndDifficultyFromSortOrder(so, st, dc);
          SongUtil::SortSongPointerArrayByStepsTypeAndMeter(
              array_songs, st, dc);
          break;
        default:
          FAIL_M("Unhandled sort order! Aborting...");
      }

      // Build an array of WheelItemDatas from the sorted list of Song*'s
      array_wheel_item_datas.clear();  // clear out the previous wheel items
      array_wheel_item_datas.reserve(array_songs.size());

      switch (PREFSMAN->music_wheel_uses_sections_) {
        case MusicWheelUsesSections_NEVER:
          use_sections = false;
          break;
        case MusicWheelUsesSections_ABC_ONLY:
          if (so != SORT_TITLE && so != SORT_GROUP) {
            use_sections = false;
          }
          break;
        default:
          break;
      }

      if (use_sections) {
        // Sorting twice isn't necessary. Instead, modify the compatator
        // functions in Song.cpp to have the desired effect. -Chris
        /* Keeping groups together with the sorts is tricky and brittle; we
         * keep getting OTHER split up without this. However, it puts the
         * Grade and BPM sorts in the wrong order, and they're already correct,
         * so don't re-sort for them. */
        /* We're using sections, so use the section name as the top-level sort.
         */
        switch (so) {
          case SORT_SERIES:
            SongUtil::SortSongPointerArrayByParentSectionName(array_songs);
            break;
          case SORT_GROUP:
            SongUtil::SortSongPointerArrayByGroup(array_songs);
            break;
          case SORT_METER:
          case SORT_PREFERRED:
          case SORT_TOP_GRADES:
          case SORT_TOP_GRADES_P1:
          case SORT_TOP_GRADES_P2:
          case SORT_BPM:
          case SORT_LENGTH:
            break;  // don't sort by section
          default:
            SongUtil::SortSongPointerArrayBySectionName(array_songs, so);
            break;
        }
      }
      // make WheelItemDatas with sections
      std::string last_section = "";
      int section_color_index = 0;
      switch (so) {
        case SORT_PREFERRED:
          // If the sort order is Preferred handle it differently because we
          // already know the sections
          if (use_sections) {
            for (const auto& [section_name, entries] :
                 SONGMAN->GetPreferredSortEntriesMap()) {
              // todo: preferred sort section color handling? -aj
              RageColor color_section =
                  SECTION_COLORS.GetValue(section_color_index);
              section_color_index =
                  (section_color_index + 1) % NUM_SECTION_COLORS;
              // Add the section item
              array_wheel_item_datas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Section, nullptr, section_name, nullptr,
                  nullptr, color_section, entries.size()));
              // Add all the songs in this section
              for (const auto& entry : entries) {
                MusicWheelItemData* wid = new MusicWheelItemData(
                    WheelItemDataType_Song, entry.song_, section_name, nullptr,
                    nullptr, SONGMAN->GetSongColor(entry.song_), 0);
                wid->pinned_difficulty_ = entry.pinned_difficulty_;
                wid->pinned_edit_description_ = entry.pinned_edit_description_;
                array_wheel_item_datas.push_back(wid);
              }
            }
          }
          break;
        case SORT_METER:
          if (use_sections) {
            int section_count = 0;
            for (const auto& [section_name, songs] :
                 SONGMAN->GetMeterToSongsMap()) {
              RageColor color_section =
                  SECTION_COLORS.GetValue(section_color_index);
              section_color_index =
                  (section_color_index + 1) % NUM_SECTION_COLORS;
              // Add the section item
              array_wheel_item_datas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Section, nullptr,
                  ssprintf("%d", section_name), nullptr, nullptr, color_section,
                  songs.size()));
              // Add all the songs in this section
              for (const auto& song : songs) {
                MusicWheelItemData* wid = new MusicWheelItemData(
                    WheelItemDataType_Song, song, ssprintf("%d", section_name),
                    nullptr, nullptr, SONGMAN->GetSongColor(song), 0);
                wid->pinned_meter_ = section_name;
                array_wheel_item_datas.push_back(wid);
              }
            }
          }
          break;
        case SORT_GROUP:
          for (unsigned i = 0; i < array_songs.size(); i++) {
            Song* p_song = array_songs[i];
            Group* group = SONGMAN->GetGroup(p_song);
            if (use_sections) {
              std::string this_section = group->GetGroupName();

              if (this_section != last_section) {
                int section_count = 0;
                // Count songs in this section
                unsigned j;
                for (j = i; j < array_songs.size(); j++) {
                  if (SONGMAN->GetGroup(array_songs[j]) == nullptr) {
                    LOG->Warn(
                        "Song %s has no group!",
                        array_songs[j]->GetSongDir().c_str());
                    continue;
                  } else if (
                      SONGMAN->GetGroup(array_songs[j])->GetGroupName() !=
                      this_section) {
                    break;
                  }
                }
                section_count = j - i;

                // new section, make a section item
                // todo: preferred sort section color handling? -aj
                RageColor color_section =
                    (so == SORT_GROUP)
                        ? SONGMAN->GetSongGroupColor(this_section)
                        : SECTION_COLORS.GetValue(section_color_index);
                section_color_index =
                    (section_color_index + 1) % NUM_SECTION_COLORS;
                array_wheel_item_datas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Section, nullptr, this_section, nullptr,
                    group, color_section, section_count));
                last_section = this_section;
              }
            }
            array_wheel_item_datas.push_back(new MusicWheelItemData(
                WheelItemDataType_Song, p_song, last_section, nullptr, group,
                SONGMAN->GetSongColor(p_song), 0));
          }
          break;

        case SORT_SERIES:
          // this is currently the only sort that creates
          // WheelItemDataType_ParentSection items.
          if (use_sections) {
            std::string last_series;
            std::string last_group;
            bool have_last = false;
            for (unsigned i = 0; i < array_songs.size(); ++i) {
              Song* p_song = array_songs[i];
              Group* group = SONGMAN->GetGroup(p_song);
              if (group == nullptr) {
                LOG->Warn(
                    "Song %s has no group!", p_song->GetSongDir().c_str());
                continue;
              }
              const std::string group_name = group->GetGroupName();
              int num_groups_in_series =
                  SONGMAN->GetGroupsInSeries(group->GetSeries()).size();
              std::string series_name;
              if (num_groups_in_series > 1) {
                series_name = group->GetSeries();
              } else {
                series_name = "";
              }
              // New ParentSection for each new series.
              if (!series_name.empty() &&
                  (!have_last || series_name != last_series)) {
                int series_song_count = 0;
                for (unsigned j = i; j < array_songs.size(); ++j) {
                  Group* g = SONGMAN->GetGroup(array_songs[j]);
                  if (g != nullptr && g->GetSeries() == series_name) {
                    ++series_song_count;
                  } else {
                    break;
                  }
                }
                RageColor series_color =
                    SECTION_COLORS.GetValue(section_color_index);
                section_color_index =
                    (section_color_index + 1) % NUM_SECTION_COLORS;
                MusicWheelItemData* series_item = new MusicWheelItemData(
                    WheelItemDataType_ParentSection, nullptr, series_name,
                    nullptr, nullptr, series_color, series_song_count);
                series_item->label_ = series_name;
                array_wheel_item_datas.push_back(series_item);
              }

              if (!have_last || group_name != last_group) {
                int group_song_count = 0;
                for (unsigned j = i; j < array_songs.size(); ++j) {
                  Group* g = SONGMAN->GetGroup(array_songs[j]);
                  if (g == group) {
                    ++group_song_count;
                  } else {
                    break;
                  }
                }
                array_wheel_item_datas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Section, nullptr, group_name, nullptr,
                    group, SONGMAN->GetSongGroupColor(group_name),
                    group_song_count, series_name));
              }

              array_wheel_item_datas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Song, p_song, group_name, nullptr, group,
                  SONGMAN->GetSongColor(p_song), 0, series_name));

              last_series = series_name;
              last_group = group_name;
              have_last = true;
            }
          } else {
            for (unsigned i = 0; i < array_songs.size(); i++) {
              Song* p_song = array_songs[i];
              Group* group = SONGMAN->GetGroup(p_song);
              array_wheel_item_datas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Song, p_song, last_section, nullptr, group,
                  SONGMAN->GetSongColor(p_song), 0));
            }
          }
          break;

        default:
          for (unsigned i = 0; i < array_songs.size(); i++) {
            Song* p_song = array_songs[i];
            if (use_sections) {
              std::string this_section =
                  SongUtil::GetSectionNameFromSongAndSort(p_song, so);

              if (this_section != last_section) {
                int section_count = 0;
                // Count songs in this section
                unsigned j;
                for (j = i; j < array_songs.size(); j++) {
                  if (SongUtil::GetSectionNameFromSongAndSort(
                          array_songs[j], so) != this_section) {
                    break;
                  }
                }
                section_count = j - i;

                // new section, make a section item
                // todo: preferred sort section color handling? -aj
                RageColor color_section =
                    (so == SORT_GROUP)
                        ? SONGMAN->GetSongGroupColor(this_section)
                        : SECTION_COLORS.GetValue(section_color_index);
                section_color_index =
                    (section_color_index + 1) % NUM_SECTION_COLORS;
                array_wheel_item_datas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Section, nullptr, this_section, nullptr,
                    nullptr, color_section, section_count));
                last_section = this_section;
              }
            }
            array_wheel_item_datas.push_back(new MusicWheelItemData(
                WheelItemDataType_Song, p_song, last_section, nullptr, nullptr,
                SONGMAN->GetSongColor(p_song), 0));
          }
          break;
      }

      if (so != SORT_ROULETTE) {
        // todo: allow themers to change the order of the items. -aj
        if (SHOW_ROULETTE) {
          array_wheel_item_datas.push_back(new MusicWheelItemData(
              WheelItemDataType_Roulette, nullptr, "", nullptr, nullptr,
              ROULETTE_COLOR, 0));
        }

        // Only add WheelItemDataType_Random and WheelItemDataType_Portal if
        // there's at least one song on the list.
        bool found_any_song = false;
        for (unsigned i = 0;
             !found_any_song && i < array_wheel_item_datas.size(); i++) {
          if (array_wheel_item_datas[i]->type_ == WheelItemDataType_Song) {
            found_any_song = true;
          }
        }

        if (SHOW_RANDOM && found_any_song) {
          array_wheel_item_datas.push_back(new MusicWheelItemData(
              WheelItemDataType_Random, nullptr, "", nullptr, nullptr,
              RANDOM_COLOR, 0));
        }

        if (SHOW_PORTAL && found_any_song) {
          array_wheel_item_datas.push_back(new MusicWheelItemData(
              WheelItemDataType_Portal, nullptr, "", nullptr, nullptr,
              PORTAL_COLOR, 0));
        }

        // add custom wheel items
        std::vector<std::string> names;
        split(CUSTOM_WHEEL_ITEM_NAMES, ",", names);
        for (unsigned i = 0; i < names.size(); ++i) {
          MusicWheelItemData wid(
              WheelItemDataType_Custom, nullptr, "", nullptr, nullptr,
              CUSTOM_CHOICE_COLORS.GetValue(names[i]), 0);
          wid.action_ = HiddenPtr<GameCommand>(new GameCommand);
          wid.action_->name_ = names[i];
          wid.action_->Load(
              i, ParseCommands(CUSTOM_CHOICES.GetValue(names[i])));
          wid.label_ = CUSTOM_ITEM_WHEEL_TEXT(names[i]);

          if (!wid.action_->IsPlayable()) {
            continue;
          }

          array_wheel_item_datas.push_back(new MusicWheelItemData(wid));
        }
      }

      if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
        Song* p_song;
        Steps* steps;
        SONGMAN->GetExtraStageInfo(
            GAMESTATE->IsExtraStage2(),
            GAMESTATE->GetCurrentStyle(PLAYER_INVALID), p_song, steps);

        for (unsigned i = 0; i < array_wheel_item_datas.size(); i++) {
          if (array_wheel_item_datas[i]->song_ == p_song) {
            // Change the song color.
            array_wheel_item_datas[i]->color_ = SONG_REAL_EXTRA_COLOR;
            break;
          }
        }
      }
      break;
    }
    case SORT_ALL_COURSES:
    case SORT_NONSTOP_COURSES:
    case SORT_ONI_COURSES:
    case SORT_ENDLESS_COURSES: {
      bool only_preferred =
          PREFSMAN->course_sort_order_ == COURSE_SORT_PREFERRED;

      std::vector<CourseType> vct;
      switch (so) {
        case SORT_NONSTOP_COURSES:
          vct.push_back(COURSE_TYPE_NONSTOP);
          break;
        case SORT_ONI_COURSES:
          vct.push_back(COURSE_TYPE_ONI);
          vct.push_back(COURSE_TYPE_SURVIVAL);
          break;
        case SORT_ENDLESS_COURSES:
          vct.push_back(COURSE_TYPE_ENDLESS);
          break;
        case SORT_ALL_COURSES:
          FOREACH_ENUM(CourseType, i)
          vct.push_back(i);
          break;
        default:
          FAIL_M(ssprintf("Wrong sort order: %i", so));
      }

      std::vector<Course*> courses;
      for (const CourseType& ct : vct) {
        if (only_preferred) {
          SONGMAN->GetPreferredSortCourses(
              ct, courses, PREFSMAN->autogen_group_courses_);
        } else {
          SONGMAN->GetCourses(ct, courses, PREFSMAN->autogen_group_courses_);
        }
      }

      switch (PREFSMAN->course_sort_order_) {
        case COURSE_SORT_SONGS:
          CourseUtil::SortCoursePointerArrayByDifficulty(courses);
          break;
        case COURSE_SORT_PREFERRED:
          break;
        case COURSE_SORT_METER:
          CourseUtil::SortCoursePointerArrayByAvgDifficulty(courses);
          break;
        case COURSE_SORT_METER_SUM:
          CourseUtil::SortCoursePointerArrayByTotalDifficulty(courses);
          break;
        case COURSE_SORT_RANK:
          CourseUtil::SortCoursePointerArrayByRanking(courses);
          break;
        default:
          FAIL_M("Impossible to sort the courses! Aborting...");
      }

      // since we can't agree, make it an option
      if (PREFSMAN->course_sort_order_ != COURSE_SORT_SONGS &&
          g_bMoveRandomToEnd) {
        CourseUtil::MoveRandomToEnd(courses);
      }

      if (so == SORT_ALL_COURSES) {
        CourseUtil::SortCoursePointerArrayByType(courses);
      }

      array_wheel_item_datas.clear();  // clear out the previous wheel items

      std::string last_section = "";
      int section_color_index = 0;
      for (unsigned i = 0; i < courses.size(); i++)  // foreach course
      {
        Course* course = courses[i];

        // if unlocks are on, make sure it is unlocked
        if (UNLOCKMAN->CourseIsLocked(course)) {
          continue;
        }

        std::string this_section = "";
        if (so == SORT_ALL_COURSES) {
          switch (course->GetPlayMode()) {
            case PLAY_MODE_ONI:
              this_section = "Oni";
              break;
            case PLAY_MODE_NONSTOP:
              this_section = "Nonstop";
              break;
            case PLAY_MODE_ENDLESS:
              this_section = "Endless";
              break;
            default:
              break;
          }
        }

        // check that this course has at least one song playable in the current
        // style
        if (!course->IsPlayableIn(
                GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
          continue;
        }

        if (this_section != last_section)  // new section, make a section item
        {
          RageColor c = SECTION_COLORS.GetValue(section_color_index);
          section_color_index = (section_color_index + 1) % NUM_SECTION_COLORS;
          array_wheel_item_datas.push_back(new MusicWheelItemData(
              WheelItemDataType_Section, nullptr, this_section, nullptr,
              nullptr, c, 0));
          last_section = this_section;
        }

        RageColor c = (course->group_name_.size() == 0)
                          ? course->GetColor()
                          : SONGMAN->GetCourseColor(course);
        array_wheel_item_datas.push_back(new MusicWheelItemData(
            WheelItemDataType_Course, nullptr, this_section, course, nullptr, c,
            0));
      }
      break;
    }
    default:
      break;
  }

  // init music status icons
  for (MusicWheelItemData* WID : array_wheel_item_datas) {
    if (WID->song_ != nullptr) {
      WID->flags_.has_beginner_or1_meter_ =
          WID->song_->IsEasy(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_) &&
          SHOW_EASY_FLAG;
      WID->flags_.edits_ = false;
      std::set<StepsType> steps_type;
      SongUtil::GetPlayableStepsTypes(WID->song_, steps_type);
      for (const StepsType& type : steps_type) {
        WID->flags_.edits_ |= WID->song_->HasEdits(type);
      }
      WID->flags_.stages_for_song_ =
          GameState::GetNumStagesMultiplierForSong(WID->song_);
    } else if (WID->course_ != nullptr) {
      WID->flags_.has_beginner_or1_meter_ = false;
      WID->flags_.edits_ = WID->course_->IsAnEdit();
      WID->flags_.stages_for_song_ = 1;
    }
  }
}

std::vector<MusicWheelItemData*>& MusicWheel::getWheelItemsData(SortOrder so) {
  // Update the popularity and init icons.
  readyWheelItemsData(so);
  return wheel_item_datas_[so];
}

void MusicWheel::readyWheelItemsData(SortOrder so) {
  if (wheel_item_datas_status_[so] == VALID && so != SORT_PREFERRED) {
    return;
  }

  std::vector<MusicWheelItemData*>& un_filtered_datas =
      un_filtered_wheel_item_datas_[so];

  if (wheel_item_datas_status_[so] == INVALID) {
    BuildWheelItemDatas(un_filtered_datas, so);
  }
  FilterWheelItemDatas(un_filtered_datas, wheel_item_datas_[so], so);
  // The preferred sort's songs are subject to change during a session
  // (particularly if two players have different preferred songs) thus it's
  // status should remain invalid so the wheel items are rebuilt each time in
  // case of change.
  if (so != SORT_PREFERRED) {
    wheel_item_datas_status_[so] = VALID;
  }
}

void MusicWheel::FilterWheelItemDatas(
    std::vector<MusicWheelItemData*>& un_filtered_datas,
    std::vector<MusicWheelItemData*>& filtered_data, SortOrder so) {
  filtered_data.clear();

  unsigned unfiltered_size = un_filtered_datas.size();

  /* Only add WheelItemDataType_Portal if there's at least one song on the list.
   */
  bool found_any_song = false;
  for (unsigned i = 0; i < unfiltered_size; i++) {
    if (un_filtered_datas[i]->type_ == WheelItemDataType_Song) {
      found_any_song = true;
      break;
    }
  }

  std::vector<bool> remove;
  remove.insert(remove.begin(), unfiltered_size, false);

  const int max_stages_for_song =
      GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer();

  Song* extra_stage_song = nullptr;
  if (GAMESTATE->IsAnExtraStage()) {
    Steps* steps;
    SONGMAN->GetExtraStageInfo(
        GAMESTATE->IsExtraStage2(), GAMESTATE->GetCurrentStyle(PLAYER_INVALID),
        extra_stage_song, steps);
  }

  /* Mark any songs that aren't playable in aiRemove. */

  for (unsigned i = 0; i < unfiltered_size; i++) {
    MusicWheelItemData& wid = *un_filtered_datas[i];

    /* If we have no songs, remove Random and Portal. */
    if (wid.type_ == WheelItemDataType_Random ||
        wid.type_ == WheelItemDataType_Portal ||
        wid.type_ == WheelItemDataType_Roulette) {
      if (!found_any_song) {
        remove[i] = true;
      }
      if (STATE_ROULETTE_SPINNING == wheel_state_) {
        remove[i] = true;
      }
      continue;
    }

    if ((wid.type_ == WheelItemDataType_Section ||
         wid.type_ == WheelItemDataType_ParentSection) &&
        STATE_ROULETTE_SPINNING == wheel_state_) {
      remove[i] = true;
    }

    /* Filter songs that we don't have enough stages to play. */
    if (wid.type_ == WheelItemDataType_Song) {
      Song* song = wid.song_;

      /* Never remove the extra stage song. */
      if (extra_stage_song && wid.song_ == extra_stage_song) {
        continue;
      }

      /* Check that we have enough stages to play this song. */
      if (GAMESTATE->GetNumStagesMultiplierForSong(wid.song_) >
          max_stages_for_song) {
        remove[i] = true;
        continue;
      }

      int locked = UNLOCKMAN->SongIsLocked(song);
      if (locked & LOCKED_DISABLED) {
        remove[i] = true;
        continue;
      }

      /* If we're on an extra stage, and this song is selected, ignore
       * #SELECTABLE. */
      if (song != GAMESTATE->cur_song_ || !GAMESTATE->IsAnExtraStage()) {
        /* Hide songs that asked to be hidden via #SELECTABLE. */
        if (locked & LOCKED_SELECTABLE) {
          remove[i] = true;
          continue;
        }
        if (so != SORT_ROULETTE && locked & LOCKED_ROULETTE) {
          remove[i] = true;
          continue;
        }
      }

      /* Hide locked songs.  If RANDOM_PICKS_LOCKED_SONGS, hide in Roulette and
       * Random, too. */
      if ((so != SORT_ROULETTE || !RANDOM_PICKS_LOCKED_SONGS) && locked) {
        remove[i] = true;
        continue;
      }

      /* If the song has no steps for the current style, remove it. */
      if (!CommonMetrics::AUTO_SET_STYLE &&
          !song->HasStepsType(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
        remove[i] = true;
        continue;
      }

      // if AutoSetStyle, make sure the song is playable in the end.
      if (!SongUtil::IsSongPlayable(song)) {
        remove[i] = true;
        continue;
      }

      if (!GAMESTATE->IsSongAllowedByPremiumFree(song)) {
        remove[i] = true;
        continue;
      }

      if (wid.IsPinned()) {
        std::vector<Steps*> pinned_steps;
        wid.GetPinnedStepsList(pinned_steps);
        if (pinned_steps.empty()) {
          remove[i] = true;
          continue;
        }
      }
    }

    if (wid.type_ == WheelItemDataType_Course) {
      if (!wid.course_->IsPlayableIn(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
        remove[i] = true;
      }
    }
  }

  /* Filter out the songs we're removing. */

  filtered_data.reserve(unfiltered_size);
  for (unsigned i = 0; i < unfiltered_size; i++) {
    if (remove[i]) {
      continue;
    }
    filtered_data.push_back(un_filtered_datas[i]);
  }

  // Update the song count in each section header.
  unsigned filtered_size = filtered_data.size();
  MusicWheelItemData* cur_section = nullptr;
  MusicWheelItemData* cur_parent = nullptr;
  for (unsigned i = 0; i < filtered_size; ++i) {
    MusicWheelItemData& wid = *filtered_data[i];
    if (wid.type_ == WheelItemDataType_ParentSection) {
      cur_parent = &wid;
      cur_parent->section_count_ = 0;
      cur_section = nullptr;
    } else if (wid.type_ == WheelItemDataType_Section) {
      cur_section = &wid;
      cur_section->section_count_ = 0;
      if (wid.parent_section_.empty()) {
        cur_parent = nullptr;
      }
    } else if (wid.type_ == WheelItemDataType_Song) {
      if (cur_section != nullptr) {
        ++cur_section->section_count_;
      }
      if (cur_parent != nullptr && !wid.parent_section_.empty()) {
        ++cur_parent->section_count_;
      }
    }
  }

  // If we have any section headers with no songs, then we filtered all of the
  // songs in that group, so remove it.  This isn't optimized like the above
  // since this is a rare case.
  for (unsigned i = 0; i < filtered_size; ++i) {
    MusicWheelItemData& wid = *filtered_data[i];
    if (wid.type_ != WheelItemDataType_Section &&
        wid.type_ != WheelItemDataType_ParentSection) {
      continue;
    }
    if (wid.section_count_ > 0) {
      continue;
    }
    filtered_data.erase(
        filtered_data.begin() + i, filtered_data.begin() + i + 1);
    --i;
    --filtered_size;
  }

  /* Update the popularity.  This is affected by filtering. */
  if (so == SORT_POPULARITY || so == SORT_POPULARITY_P1 ||
      so == SORT_POPULARITY_P2)

  {
    for (unsigned i = 0; i < std::min<unsigned int>(3u, filtered_data.size());
         i++) {
      MusicWheelItemData& wid = *filtered_data[i];
      wid.flags_.players_best_number_ = i + 1;
    }
  }

  // If we've filtered all items, insert a dummy.
  if (filtered_data.empty()) {
    filtered_data.push_back(new MusicWheelItemData(
        WheelItemDataType_Section, nullptr, EMPTY_STRING, nullptr, nullptr,
        EMPTY_COLOR, 0));
  }
}

void MusicWheel::UpdateSwitch() {
  switch (wheel_state_) {
    case STATE_FLYING_OFF_BEFORE_NEXT_SORT: {
      const Song* prev_selected_song = GetCurWheelItemData(selection_)->song_;

      SCREENMAN->PostMessageToTopScreen(SM_SortOrderChanged, 0);

      SetOpenSection(
          SongUtil::GetSectionNameFromSongAndSort(
              prev_selected_song, GAMESTATE->sort_order_));

      selection_ = 0;

      // Select the previously selected item
      switch (GAMESTATE->sort_order_) {
        default:
          // Look for the last selected song or course
          SelectSongOrCourse();
          break;
        case SORT_MODE_MENU:
          SelectModeMenuItem();
          break;
      }

      // Change difficulty for sorts by meter
      // XXX: do this with GameCommand?
      StepsType st;
      Difficulty dc;
      if (SongUtil::GetStepsTypeAndDifficultyFromSortOrder(
              GAMESTATE->sort_order_, st, dc)) {
        ASSERT(dc != Difficulty_Invalid);
        FOREACH_PlayerNumber(p) if (GAMESTATE->IsPlayerEnabled(p))
            GAMESTATE->preferred_difficulty_[p]
                .Set(dc);
      }

      SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);
      RebuildWheelItems();
      TweenOnScreenForSort();
    } break;

    case STATE_FLYING_ON_AFTER_NEXT_SORT:
      wheel_state_ = STATE_SELECTING;  // now, wait for input
      break;

    case STATE_SELECTING:
      time_left_in_state_ = 0;
      break;
    case STATE_ROULETTE_SPINNING:
      break;
    case STATE_LOCKED:
      break;
    case STATE_ROULETTE_SLOWING_DOWN:
      if (switches_left_in_spin_down_ == 0) {
        wheel_state_ = STATE_LOCKED;
        time_left_in_state_ = 0;
        SCREENMAN->PlayStartSound();
        locked_wheel_velocity_ = 0;

        // Send this again so the screen starts sample music.
        SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);
        MESSAGEMAN->Broadcast("RouletteStopped");
      } else {
        --switches_left_in_spin_down_;
        const float switch_times[] = {0.5f, 1.3f, 0.8f, 0.4f, 0.2f};
        ASSERT(
            switches_left_in_spin_down_ >= 0 &&
            switches_left_in_spin_down_ <= 4);
        time_left_in_state_ = switch_times[switches_left_in_spin_down_];
        moving_ = 0;

        LOG->Trace(
            "switches_left_in_spin_down_ id %d, time_left_in_state_ is %f",
            switches_left_in_spin_down_, time_left_in_state_);

        if (switches_left_in_spin_down_ == 0) {
          ChangeMusic(randomf(0, 1) >= 0.5f ? 1 : -1);
        } else {
          ChangeMusic(1);
        }
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid wheel state: %i", wheel_state_));
  }
}

void MusicWheel::ChangeMusic(int dist) {
  selection_ += dist;
  wrap(selection_, cur_wheel_item_data_.size());

  if (REMIND_WHEEL_POSITIONS && HIDE_INACTIVE_SECTIONS) {
    // store the group song index
    for (unsigned idx = 0; idx < wheel_positions_.size(); idx++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(idx)) {
        wheel_positions_[idx] = selection_;
      }
    }
  }

  RebuildWheelItems(dist);

  position_offset_from_selection_ += dist;

  SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);

  // If we're moving automatically, don't play this; it'll be called in Update.
  if (!IsMoving()) {
    sound_change_music_.Play(true);
  }
}

bool MusicWheel::ChangeSort(
    SortOrder new_so, bool allow_same_sort)  // return true if change successful
{
  ASSERT(new_so < NUM_SortOrder);
  // Reset LastOpenSection as sections differ between sorts
  GAMESTATE->last_open_section_ = "";
  // NOTE(crashcringle): Ignore allowSameSort if we're using SORT_PREFERRED.
  // Each player has their own preferred songs which sorts the songs differently
  if (GAMESTATE->sort_order_ == new_so &&
      (!allow_same_sort && new_so != SORT_PREFERRED)) {
    return false;
  }

  // Don't change to SORT_MODE_MENU if it doesn't have at least two choices.
  if (new_so == SORT_MODE_MENU && getWheelItemsData(new_so).size() < 2) {
    return false;
  }

  switch (wheel_state_) {
    case STATE_SELECTING:
    case STATE_FLYING_ON_AFTER_NEXT_SORT:
      break;  // fall through
    default:
      return false;  // don't continue
  }

  SCREENMAN->PostMessageToTopScreen(SM_SortOrderChanging, 0);

  sound_change_sort_.Play(true);

  TweenOffScreenForSort();

  // Save the new preference.
  if (IsSongSort(new_so)) {
    GAMESTATE->preferred_sort_order_ = new_so;
  }
  if (GAMESTATE->sort_order_ != new_so) {
    GAMESTATE->pinned_steps_.clear();
  }
  GAMESTATE->sort_order_.Set(new_so);

  return true;
}

bool MusicWheel::NextSort()  // return true if change successful
{
  // don't allow NextSort when on the mode menu
  if (GAMESTATE->sort_order_ == SORT_MODE_MENU) {
    return false;
  }

  std::vector<SortOrder> sort_orders;
  {
    Lua* L = LUA->Get();
    SORT_ORDERS.PushSelf(L);
    FOREACH_LUATABLE(L, -1) {
      SortOrder so = Enum::Check<SortOrder>(L, -1, true);
      sort_orders.push_back(so);
    }
    lua_pop(L, 1);
    LUA->Release(L);
  }

  // find the index of the current sort
  int cur = 0;
  while (cur < int(sort_orders.size()) &&
         sort_orders[cur] != GAMESTATE->sort_order_) {
    ++cur;
  }

  // move to the next sort with wrapping
  ++cur;
  wrap(cur, sort_orders.size());

  // apply new sort
  SortOrder so_new = sort_orders[cur];
  return ChangeSort(so_new);
}

bool MusicWheel::Select()  // return true if this selection ends the screen
{
  LOG->Trace("MusicWheel::Select()");

  switch (wheel_state_) {
    case STATE_FLYING_OFF_BEFORE_NEXT_SORT:
    case STATE_ROULETTE_SLOWING_DOWN:
      return false;
    case STATE_ROULETTE_SPINNING:
      wheel_state_ = STATE_ROULETTE_SLOWING_DOWN;
      switches_left_in_spin_down_ = ROULETTE_SLOW_DOWN_SWITCHES / 2 + 1 +
                                    RandomInt(ROULETTE_SLOW_DOWN_SWITCHES / 2);
      time_left_in_state_ = 0.1f;
      return false;
    default:
      break;
  }

  const MusicWheelItemData* selection = GetCurWheelItemData(selection_);
  if (selection->type_ == WheelItemDataType_Section ||
      selection->type_ == WheelItemDataType_ParentSection) {
    GAMESTATE->last_open_section_ = selection->text_;
    if (selection->type_ == WheelItemDataType_ParentSection) {
      if (expanded_parent_section_name_ == selection->text_ &&
          expanded_section_name_.empty()) {
        SetOpenSections("", "");
        sound_collapse_.Play(true);
      } else {
        SetOpenSections(selection->text_, "");
        sound_expand_.Play(true);
      }
      return false;
    }

    if (!selection->parent_section_.empty()) {
      if (expanded_parent_section_name_ == selection->parent_section_ &&
          expanded_section_name_ == selection->text_) {
        SetOpenSections(selection->parent_section_, "");
        sound_collapse_.Play(true);
      } else {
        SetOpenSections(selection->parent_section_, selection->text_);
        sound_expand_.Play(true);
      }
      return false;
    }

    if (expanded_parent_section_name_.empty() &&
        expanded_section_name_ == selection->text_) {
      SetOpenSections("", "");
      sound_collapse_.Play(true);
    } else {
      SetOpenSections("", selection->text_);
      sound_expand_.Play(true);
    }
    return false;
  }

  if (!WheelBase::Select()) {
    return false;
  }

  switch (cur_wheel_item_data_[selection_]->type_) {
    case WheelItemDataType_Roulette:
      StartRoulette();
      return false;
    case WheelItemDataType_Random:
      StartRandom();
      return false;
    case WheelItemDataType_Sort:
      GetCurWheelItemData(selection_)->action_->ApplyToAllPlayers();
      ChangeSort(GAMESTATE->preferred_sort_order_);
      last_mode_menu_item_ = GetCurWheelItemData(selection_)->action_->name_;
      return false;
    case WheelItemDataType_Custom:
      GetCurWheelItemData(selection_)->action_->ApplyToAllPlayers();
      if (GetCurWheelItemData(selection_)->action_->screen_ != "") {
        return true;
      } else {
        return false;
      }
    default:
      return true;
  }
}

void MusicWheel::StartRoulette() {
  MESSAGEMAN->Broadcast("StartRoulette");
  wheel_state_ = STATE_ROULETTE_SPINNING;
  moving_ = 1;
  time_before_moving_begins_ = 0;
  spin_speed_ = 1.0f / ROULETTE_SWITCH_SECONDS;
  if (expanded_section_name_ == "") {
    GAMESTATE->sort_order_.Set(SORT_ROULETTE);
  }

  wheel_item_datas_status_[GAMESTATE->sort_order_] = INVALID;  // force rebuild
  SetOpenSections(expanded_parent_section_name_, expanded_section_name_);
  readyWheelItemsData(GAMESTATE->sort_order_);
}

void MusicWheel::StartRandom() {
  MESSAGEMAN->Broadcast("StartRandom");
  /* If RANDOM_PICKS_LOCKED_SONGS is disabled, pick a song from the active sort
   * and section.  If enabled, picking from the section makes it too easy to
   * trick the game into picking a locked song, so pick from SORT_ROULETTE. */
  if (RANDOM_PICKS_LOCKED_SONGS) {
    // Shuffle and use the roulette wheel.
    auto& roulette_items = getWheelItemsData(SORT_ROULETTE);
    std::shuffle(
        roulette_items.begin(), roulette_items.end(), g_RandomNumberGenerator);
    GAMESTATE->sort_order_.Set(SORT_ROULETTE);
  } else {
    GAMESTATE->sort_order_.Set(GAMESTATE->preferred_sort_order_);
  }

  SelectSong(GetPreferredSelectionForRandomOrPortal());

  RebuildWheelItems();
  SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);
  TweenOnScreenForSort();
  wheel_state_ = STATE_LOCKED;
}

void MusicWheel::SetOpenSection(std::string group) {
  SetOpenSections("", group);
}

bool MusicWheel::CloseOpenSectionOneLevel() {
  // In a section within a parent: collapse back to the parent.
  if (!expanded_parent_section_name_.empty() &&
      !expanded_section_name_.empty()) {
    const std::string parent_section_name = expanded_parent_section_name_;
    const std::string child_section_name = expanded_section_name_;
    SetOpenSections(parent_section_name, "");
    SelectSection(child_section_name);
    RebuildWheelItems();
    return true;
  }

  // Only a parent section is open: collapse it.
  if (!expanded_parent_section_name_.empty()) {
    const std::string parent_section_name = expanded_parent_section_name_;
    SetOpenSections("", "");
    SelectSection(parent_section_name);
    RebuildWheelItems();
    return true;
  }

  if (!expanded_section_name_.empty()) {
    const std::string section_name = expanded_section_name_;
    SetOpenSections("", "");
    SelectSection(section_name);
    RebuildWheelItems();
    return true;
  }

  return false;
}

void MusicWheel::SetOpenSections(
    const std::string& parent_section, const std::string& section) {
  expanded_parent_section_name_ = parent_section;
  expanded_section_name_ = section;
  GAMESTATE->expanded_section_name_ = expanded_section_name_.empty()
                                          ? expanded_parent_section_name_
                                          : expanded_section_name_;
  GAMESTATE->expanded_parent_section_name_ = expanded_parent_section_name_;

  // wheel positions = num song groups
  if (REMIND_WHEEL_POSITIONS && HIDE_INACTIVE_SECTIONS) {
    wheel_positions_.resize(SONGMAN->GetNumSongGroups());
  }

  const WheelItemBaseData* old = nullptr;
  if (!cur_wheel_item_data_.empty()) {
    old = GetCurWheelItemData(selection_);
  }

  std::vector<const Style*> possible_styles;
  if (CommonMetrics::AUTO_SET_STYLE) {
    GAMEMAN->GetCompatibleStyles(
        GAMESTATE->cur_game_, GAMESTATE->GetNumPlayersEnabled(),
        possible_styles);
  }

  cur_wheel_item_data_.clear();
  std::vector<MusicWheelItemData*>& from =
      getWheelItemsData(GAMESTATE->sort_order_);
  cur_wheel_item_data_.reserve(from.size());

  // A parent section is open but no child section has been drilled into
  // yet: show only parent sections and the child sections of the open
  // parent (no songs).
  const bool parent_section_only_view =
      (GAMESTATE->sort_order_ == SORT_SERIES &&
       !expanded_parent_section_name_.empty() &&
       expanded_section_name_.empty());

  if (parent_section_only_view) {
    for (unsigned i = 0; i < from.size(); ++i) {
      MusicWheelItemData& d = *from[i];
      if (d.type_ != WheelItemDataType_Section &&
          d.type_ != WheelItemDataType_ParentSection) {
        continue;
      }

      // Child sections are only visible while their parent is open.
      if (!d.parent_section_.empty() &&
          d.parent_section_ != expanded_parent_section_name_) {
        continue;
      }

      // Show only the active parent and its child sections.
      if (HIDE_INACTIVE_PARENT_SECTIONS) {
        bool is_active_parent = d.type_ == WheelItemDataType_ParentSection &&
                                d.parent_section_.empty() &&
                                d.text_ == expanded_parent_section_name_;
        bool is_child_of_active =
            !d.parent_section_.empty() &&
            d.parent_section_ == expanded_parent_section_name_;
        if (!is_active_parent && !is_child_of_active) {
          continue;
        }
        if (HIDE_ACTIVE_SECTION_TITLE && is_active_parent) {
          continue;
        }
      }

      cur_wheel_item_data_.push_back(&d);
    }
  } else {
    for (unsigned i = 0; i < from.size(); ++i) {
      MusicWheelItemData& d = *from[i];

      if ((d.type_ == WheelItemDataType_Section ||
           d.type_ == WheelItemDataType_ParentSection) &&
          !d.parent_section_.empty() &&
          d.parent_section_ != expanded_parent_section_name_) {
        continue;
      }

      if ((d.type_ == WheelItemDataType_Song ||
           d.type_ == WheelItemDataType_Course) &&
          !d.text_.empty()) {
        if (expanded_section_name_.empty() ||
            d.text_ != expanded_section_name_) {
          continue;
        }
      }

      // In certain situations (e.g. simulating Pump it Up or IIDX),
      // themes may want to hide inactive section headings as well.
      // We extend this to both parent and child sections.
      if (HIDE_INACTIVE_SECTIONS &&
          (d.type_ == WheelItemDataType_Section ||
           d.type_ == WheelItemDataType_ParentSection) &&
          !expanded_section_name_.empty()) {
        bool is_open_parent_section =
            d.type_ == WheelItemDataType_ParentSection &&
            d.parent_section_.empty() &&
            d.text_ == expanded_parent_section_name_;
        bool is_open_child_section =
            !d.parent_section_.empty() &&
            d.parent_section_ == expanded_parent_section_name_ &&
            d.text_ == expanded_section_name_;
        bool is_open_top_section = d.type_ == WheelItemDataType_Section &&
                                   d.parent_section_.empty() &&
                                   expanded_parent_section_name_.empty() &&
                                   d.text_ == expanded_section_name_;
        if (!is_open_parent_section && !is_open_child_section &&
            !is_open_top_section) {
          continue;
        }
        if (HIDE_ACTIVE_SECTION_TITLE &&
            (is_open_child_section || is_open_top_section)) {
          continue;
        }
      }

      // If AUTO_SET_STYLE, hide courses that prefer a style that isn't
      // available.
      if (d.type_ == WheelItemDataType_Course &&
          CommonMetrics::AUTO_SET_STYLE) {
        const Style* style = d.course_->GetCourseStyle(
            GAMESTATE->cur_game_, GAMESTATE->GetNumSidesJoined());
        if (style) {
          if (find(possible_styles.begin(), possible_styles.end(), style) ==
              possible_styles.end()) {
            continue;
          }
        }
      }

      // Only show tutorial songs in arcade
      if (GAMESTATE->play_mode_ != PLAY_MODE_REGULAR && d.song_ &&
          d.song_->IsTutorial()) {
        continue;
      }

      cur_wheel_item_data_.push_back(&d);
    }
  }

  // restore the past group song index
  if (REMIND_WHEEL_POSITIONS && HIDE_INACTIVE_SECTIONS) {
    for (unsigned idx = 0; idx < wheel_positions_.size(); idx++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(idx)) {
        selection_ = wheel_positions_[idx];
      }
    }
  } else {
    // Try to select the item that was selected before changing groups
    selection_ = 0;

    for (unsigned i = 0; i < cur_wheel_item_data_.size(); i++) {
      if (GAMESTATE->sort_order_ == SORT_PREFERRED &&
          !GAMESTATE->last_open_section_.empty()) {
        // old doesn't always have data, use LastOpenSection instead
        if (cur_wheel_item_data_[i]->text_ == GAMESTATE->last_open_section_) {
          selection_ = i;
          break;
        }
      } else if (cur_wheel_item_data_[i] == old) {
        selection_ = i;
        break;
      }
    }
  }

  RebuildWheelItems();
}

void MusicWheel::GetCurrentSections(std::vector<std::string>& sections) {
  std::vector<MusicWheelItemData*>& wheel_items =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (unsigned i = 0; i < wheel_items.size(); i++) {
    if ((wheel_items[i]->type_ == WheelItemDataType_Section ||
         wheel_items[i]->type_ == WheelItemDataType_ParentSection) &&
        wheel_items[i]->parent_section_.empty() &&
        !wheel_items[i]->text_.empty()) {
      if (wheel_items[i]->type_ == WheelItemDataType_ParentSection &&
          !wheel_items[i]->label_.empty()) {
        sections.push_back(wheel_items[i]->label_);
      } else {
        sections.push_back(wheel_items[i]->text_);
      }
    }
  }
}

// sm-ssc additions: jump to group
std::string MusicWheel::JumpToNextGroup() {
  // Thanks to Juanelote for this logic:
  if (HIDE_INACTIVE_SECTIONS) {
    // todo: make it work with other sort types
    unsigned num_groups = SONGMAN->GetNumSongGroups();

    for (unsigned i = 0; i < num_groups; i++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(i)) {
        if (i < num_groups - 1) {
          return SONGMAN->GetSongGroupByIndex(i + 1);
        } else {
          // i = 0;
          return SONGMAN->GetSongGroupByIndex(0);
        }
      }
    }
  } else {
    unsigned int last_selection = selection_;
    for (unsigned int i = selection_; i < cur_wheel_item_data_.size(); ++i) {
      if ((cur_wheel_item_data_[i]->type_ == WheelItemDataType_Section ||
           cur_wheel_item_data_[i]->type_ == WheelItemDataType_ParentSection) &&
          i != (unsigned int)selection_) {
        selection_ = i;
        return cur_wheel_item_data_[i]->text_;
      }
    }
    // it should not get down here, but it might happen... only search up to
    // the previous selection.
    for (unsigned int i = 0; i < last_selection; ++i) {
      if ((cur_wheel_item_data_[i]->type_ == WheelItemDataType_Section ||
           cur_wheel_item_data_[i]->type_ == WheelItemDataType_ParentSection) &&
          i != (unsigned int)selection_) {
        selection_ = i;
        return cur_wheel_item_data_[i]->text_;
      }
    }
  }
  // it shouldn't get here, but just in case...
  return "";
}

std::string MusicWheel::JumpToPrevGroup() {
  if (HIDE_INACTIVE_SECTIONS) {
    unsigned num_groups = SONGMAN->GetNumSongGroups();

    for (unsigned i = 0; i < num_groups; i++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(i)) {
        if (i > 0) {
          return SONGMAN->GetSongGroupByIndex(i - 1);
        } else {
          // i = iNumGroups - 1;
          return SONGMAN->GetSongGroupByIndex(num_groups - 1);
        }
      }
    }
  } else {
    for (unsigned int i = selection_; i > 0; --i) {
      if ((cur_wheel_item_data_[i]->type_ == WheelItemDataType_Section ||
           cur_wheel_item_data_[i]->type_ == WheelItemDataType_ParentSection) &&
          i != (unsigned int)selection_) {
        selection_ = i;
        return cur_wheel_item_data_[i]->text_;
      }
    }
    // in case it wasn't found above:
    for (unsigned int i = cur_wheel_item_data_.size() - 1; i > 0; --i) {
      LOG->Trace("JumpToPrevGroup iteration 2 | i = %u", i);
      if (cur_wheel_item_data_[i]->type_ == WheelItemDataType_Section ||
          cur_wheel_item_data_[i]->type_ == WheelItemDataType_ParentSection) {
        selection_ = i;
        LOG->Trace(
            "finding it in #2 | i = %u | text = %s", i,
            cur_wheel_item_data_[i]->text_.c_str());
        return cur_wheel_item_data_[i]->text_;
      }
    }
  }
  // it shouldn't get here, but just in case...
  return "";
}

// Called on late join. Selectable courses may have changed; reopen the section.
void MusicWheel::PlayerJoined() {
  // If someone joins, there may be songs on the wheel that should not be
  // selectable, or there may be songs that have become selectable.
  // Set the status of all the wheel item data vectors to invalid so that
  // readyWheelItemsData will rebuild all the data next time
  // getWheelItemsData is called for that SortOrder.  SetOpenSection calls
  // readyWheelItemsData to get the items, and RebuildWheelItems when its
  // done, so invalidating and calling SetOpenSection is all we need to do.
  // -Kyz
  // Also removed the weird checks for course mode and autogen because
  // it seems weird that courses wouldn't also be affected by a player
  // joining, and not doing it in autogen causes other weird problems. -Kyz
  FOREACH_ENUM(SortOrder, so) { wheel_item_datas_status_[so] = INVALID; }
  SetOpenSections(expanded_parent_section_name_, expanded_section_name_);
}

bool MusicWheel::IsRouletting() const {
  return wheel_state_ == STATE_ROULETTE_SPINNING ||
         wheel_state_ == STATE_ROULETTE_SLOWING_DOWN;
}

Song* MusicWheel::GetSelectedSong() {
  switch (cur_wheel_item_data_[selection_]->type_) {
    case WheelItemDataType_Portal:
      return GetPreferredSelectionForRandomOrPortal();
    default:
      return GetCurWheelItemData(selection_)->song_;
  }
}

/* Find a random song.  If possible, find one that has the preferred
 * difficulties of each player.  Prefer songs in the active group, if any.
 *
 * Note that if this is called, we *must* find a song.  We will only be called
 * if the active sort has at least one song, but there may be no open group.
 * This means that any filters and preferences applied here must be optional. */
Song* MusicWheel::GetPreferredSelectionForRandomOrPortal() {
  // probe to find a song that has the preferred
  // difficulties of each player
  std::vector<Difficulty> difficulties_to_require;
  FOREACH_HumanPlayer(p) {
    if (GAMESTATE->preferred_difficulty_[p] == Difficulty_Invalid) {
      continue;  // skip
    }

    // TRICKY: Don't require that edits be present if perferred
    // difficulty is Difficulty_Edit.  Otherwise, players could use this
    // to set up a 100% chance of getting a particular locked song by
    // having a single edit for a locked song.
    if (GAMESTATE->preferred_difficulty_[p] == Difficulty_Edit) {
      continue;  // skip
    }

    difficulties_to_require.push_back(GAMESTATE->preferred_difficulty_[p]);
  }

  std::vector<MusicWheelItemData*>& wid =
      getWheelItemsData(GAMESTATE->sort_order_);
  StepsType st = GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                     ->steps_type_;
  std::vector<MusicWheelItemData*> random_songs;

  // If we have an open section, only pick from songs in that section.
  if (!expanded_section_name_.empty()) {
    for (unsigned i = 0; i < cur_wheel_item_data_.size(); i++) {
      MusicWheelItemData* selection =
          (MusicWheelItemData*)cur_wheel_item_data_[i];
      // Filter out non-songs
      if (selection->type_ != WheelItemDataType_Song) {
        continue;
      }
      // Only add songs in the open section
      if (selection->text_ == expanded_section_name_) {
        random_songs.push_back(selection);
      }
    }
  } else {
    // Otherwise, pick from all songs in the current sort.
    random_songs = wid;
  }
  std::shuffle(
      random_songs.begin(), random_songs.end(), g_RandomNumberGenerator);

#define NUM_PROBES 1000
  for (int i = 0; i < NUM_PROBES; i++) {
    bool is_valid = true;
    int i_selection = 0;
    Song* p_song;
    i_selection = RandomInt(random_songs.size());
    MusicWheelItemData* selection =
        (MusicWheelItemData*)random_songs[i_selection];
    p_song = selection->song_;

    if (p_song == nullptr) {
      continue;
    }

    // There's an off possibility that somebody might have only one song with
    // only beginner steps.
    if (i < 900 && p_song->IsTutorial()) {
      continue;
    }

    // We need to check for difficulty size because when you first enter the
    // music wheel You may not have a preferred difficulty set yet and this
    // would result in every song even songs in other game types, being valid.

    if (i == NUM_PROBES / 2 || !PREFSMAN->random_prioritize_difficulty_) {
      difficulties_to_require.clear();
    }
    if (difficulties_to_require.size() != 0) {
      is_valid = std::none_of(
          difficulties_to_require.begin(), difficulties_to_require.end(),
          [&](const Difficulty& d) {
            return !p_song->HasStepsTypeAndDifficulty(st, d);
          });
    } else {
      is_valid = p_song->HasStepsType(st);
    }

    Song* song = p_song;
    if (is_valid && SongUtil::IsSongPlayable(song)) {
      return p_song;
    }
  }
  LuaHelpers::ReportScriptError("Couldn't find any songs");
  return random_songs[0]->song_;
}

void MusicWheel::FinishChangingSorts() {
  FinishTweening();
  wheel_state_ = STATE_SELECTING;
  time_left_in_state_ = 0;
}

// lua start
#include "LuaBinding.h"

class LunaMusicWheel : public Luna<MusicWheel> {
 public:
  static int ChangeSort(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      lua_pushboolean(L, false);
    } else {
      SortOrder so = Enum::Check<SortOrder>(L, 1);
      lua_pushboolean(L, p->ChangeSort(so));
    }
    return 1;
  }
  DEFINE_METHOD(GetSelectedSection, GetSelectedSection());
  static int GetCurrentSections(T* p, lua_State* L) {
    std::vector<std::string> v;
    p->GetCurrentSections(v);
    LuaHelpers::CreateTableFromArray<std::string>(v, L);
    return 1;
  }
  static int IsRouletting(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsRouletting());
    return 1;
  }
  static int IsLocked(T* p, lua_State* L) {
    lua_pushboolean(L, p->WheelIsLocked());
    return 1;
  }
  static int HasSongs(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasSongs());
    return 1;
  }
  static int SelectSong(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      lua_pushboolean(L, false);
    } else {
      Song* s = Luna<Song>::check(L, 1, true);
      lua_pushboolean(L, p->SelectSong(s));
    }
    return 1;
  }
  static int SelectCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      lua_pushboolean(L, false);
    } else {
      Course* c = Luna<Course>::check(L, 1, true);
      lua_pushboolean(L, p->SelectCourse(c));
    }
    return 1;
  }

  static int Move(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->Move(0);
    } else {
      p->Move(IArg(1));
    }
    return 1;
  }

  LunaMusicWheel() {
    ADD_METHOD(ChangeSort);
    ADD_METHOD(GetSelectedSection);
    ADD_METHOD(IsRouletting);
    ADD_METHOD(IsLocked);
    ADD_METHOD(HasSongs);
    ADD_METHOD(SelectSong);
    ADD_METHOD(SelectCourse);
    ADD_METHOD(Move);
    ADD_METHOD(GetCurrentSections);
  }
};

LUA_REGISTER_DERIVED_CLASS(MusicWheel, WheelBase)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez, Glenn Maynard
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
