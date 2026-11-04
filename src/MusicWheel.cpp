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

void MusicWheel::Load(std::string sType) {
  ROULETTE_SWITCH_SECONDS.Load(sType, "RouletteSwitchSeconds");
  ROULETTE_SLOW_DOWN_SWITCHES.Load(sType, "RouletteSlowDownSwitches");
  NUM_SECTION_COLORS.Load(sType, "NumSectionColors");
  SONG_REAL_EXTRA_COLOR.Load(sType, "SongRealExtraColor");
  SORT_MENU_COLOR.Load(sType, "SortMenuColor");
  SHOW_ROULETTE.Load(sType, "ShowRoulette");
  SHOW_RANDOM.Load(sType, "ShowRandom");
  SHOW_PORTAL.Load(sType, "ShowPortal");
  RANDOM_PICKS_LOCKED_SONGS.Load(sType, "RandomPicksLockedSongs");
  MOST_PLAYED_SONGS_TO_SHOW.Load(sType, "MostPlayedSongsToShow");
  RECENT_SONGS_TO_SHOW.Load(sType, "RecentSongsToShow");
  MODE_MENU_CHOICE_NAMES.Load(sType, "ModeMenuChoiceNames");
  SORT_ORDERS.Load(sType, "SortOrders");
  SHOW_EASY_FLAG.Load(sType, "UseEasyMarkerFlag");
  USE_SECTIONS_WITH_PREFERRED_GROUP.Load(
      sType, "UseSectionsWithPreferredGroup");
  HIDE_INACTIVE_SECTIONS.Load(sType, "OnlyShowActiveSection");
  HIDE_INACTIVE_PARENT_SECTIONS.Load(sType, "OnlyShowActiveParentSection");
  HIDE_ACTIVE_SECTION_TITLE.Load(sType, "HideActiveSectionTitle");
  REMIND_WHEEL_POSITIONS.Load(sType, "RemindWheelPositions");
  std::vector<std::string> vsModeChoiceNames;
  split(MODE_MENU_CHOICE_NAMES, ",", vsModeChoiceNames);
  CHOICE.Load(sType, CHOICE_NAME, vsModeChoiceNames);
  SECTION_COLORS.Load(sType, SECTION_COLORS_NAME, NUM_SECTION_COLORS);

  CUSTOM_WHEEL_ITEM_NAMES.Load(sType, "CustomWheelItemNames");
  std::vector<std::string> vsCustomItemNames;
  split(CUSTOM_WHEEL_ITEM_NAMES, ",", vsCustomItemNames);
  CUSTOM_CHOICES.Load(sType, CUSTOM_WHEEL_ITEM_NAME, vsCustomItemNames);
  CUSTOM_CHOICE_COLORS.Load(sType, CUSTOM_WHEEL_ITEM_COLOR, vsCustomItemNames);

  ROULETTE_COLOR.Load(sType, "RouletteColor");
  RANDOM_COLOR.Load(sType, "RandomColor");
  PORTAL_COLOR.Load(sType, "PortalColor");
  EMPTY_COLOR.Load(sType, "EmptyColor");

  WheelBase::Load(sType);

  SONGMAN->UpdateRankingCourses();

  sound_change_sort_.Load(THEME->GetPathS(sType, "sort"));
  sound_expand_.Load(THEME->GetPathS(sType, "expand"), true);
  sound_collapse_.Load(THEME->GetPathS(sType, "collapse"), true);

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
      std::vector<Song*> vTemp =
          SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);
      ASSERT(vTemp.size() > 0);
      GAMESTATE->cur_song_.Set(vTemp[0]);
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
      std::vector<Steps*> vpPossibleSteps;
      if (GAMESTATE->cur_song_ != nullptr) {
        SongUtil::GetPlayableSteps(GAMESTATE->cur_song_, vpPossibleSteps);
      }
      bool bStepsIsPossible =
          find(
              vpPossibleSteps.begin(), vpPossibleSteps.end(),
              GAMESTATE->cur_steps_[p]) != vpPossibleSteps.end();
      if (!bStepsIsPossible) {
        GAMESTATE->cur_steps_[p].Set(nullptr);
      }
    }
  }
}

MusicWheel::~MusicWheel() {
  FOREACH_ENUM(SortOrder, so) {
    std::vector<MusicWheelItemData*>::iterator i =
        un_filtered_wheel_item_datas_[so].begin();
    std::vector<MusicWheelItemData*>::iterator iEnd =
        un_filtered_wheel_item_datas_[so].end();
    for (; i != iEnd; ++i) {
      delete *i;
    }
  }
}

void MusicWheel::ReloadSongList() {
  int songIdxToPreserve = selection_;
  // Remove the song from any sorting caches:
  FOREACH_ENUM(SortOrder, so) { wheel_item_datas_status_[so] = INVALID; }
  // rebuild the info associated with this sort order
  readyWheelItemsData(GAMESTATE->sort_order_);
  // re-open the section to refresh song counts, etc.
  SetOpenSections(expanded_parent_section_name_, expanded_section_name_);
  // navigate to the song nearest to what was previously selected
  selection_ = songIdxToPreserve;
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
  std::vector<MusicWheelItemData*>& wiWheelItems =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (unsigned i = 0; i < wiWheelItems.size(); i++) {
    if (wiWheelItems[i]->song_) {
      return SelectSong(wiWheelItems[i]->song_);
    } else if (wiWheelItems[i]->course_) {
      return SelectCourse(wiWheelItems[i]->course_);
    }
  }

  LOG->Trace(
      "MusicWheel::MusicWheel() - No selectable songs or courses found in "
      "WheelData");
  return false;
}

bool MusicWheel::SelectSection(const std::string& SectionName) {
  for (unsigned int i = 0; i < cur_wheel_item_data_.size(); ++i) {
    if (cur_wheel_item_data_[i]->text_ == SectionName) {
      selection_ = i;  // select it
      return true;
    }
  }

  return false;
}

bool MusicWheel::HasSongs() {
  std::vector<MusicWheelItemData*>& wheelItems =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (MusicWheelItemData* item : wheelItems) {
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
    if (GetCurWheelItemData(i)->song_ == p) {
      selection_ = i;  // select it
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

void MusicWheel::GetSongList(std::vector<Song*>& arraySongs, SortOrder so) {
  std::vector<Song*> apAllSongs;
  switch (so) {
    case SORT_PREFERRED:
      SONGMAN->GetPreferredSortSongs(apAllSongs);
      break;
    case SORT_POPULARITY:
      apAllSongs = SONGMAN->GetPopularSongs();
      break;
    case SORT_SERIES:
    case SORT_GROUP:
      // if we're not using sections with a preferred song group, and there
      // is a group to load, only load those songs. -aj
      if (GAMESTATE->preferred_song_group_ != GROUP_ALL &&
          !USE_SECTIONS_WITH_PREFERRED_GROUP) {
        apAllSongs = SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);
        break;
      }
      [[fallthrough]];
    default:
      apAllSongs = SONGMAN->GetAllSongs();
      break;
  }

  FOREACH_PlayerNumber(pn) {
    if (GAMESTATE->IsPlayerEnabled(pn)) {
      Profile* prof = PROFILEMAN->GetProfile(pn);
      for (size_t i = 0; i < prof->songs_.size(); ++i) {
        apAllSongs.push_back(prof->songs_[i]);
      }
    }
  }

  // filter songs that we don't have enough stages to play
  {
    std::vector<Song*> vTempSongs;
    SongCriteria sc;
    sc.max_stages_for_song_ =
        GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer();
    SongUtil::FilterSongs(sc, apAllSongs, vTempSongs);
    apAllSongs = vTempSongs;
  }

  // copy only songs that have at least one Steps for the current GameMode
  for (unsigned i = 0; i < apAllSongs.size(); i++) {
    Song* pSong = apAllSongs[i];

    int iLocked = UNLOCKMAN->SongIsLocked(pSong);
    if (iLocked & LOCKED_DISABLED) {
      continue;
    }

    // If we're on an extra stage, and this song is selected, ignore
    // #SELECTABLE.
    if (pSong != GAMESTATE->cur_song_ || !GAMESTATE->IsAnExtraStage()) {
      // Hide songs that asked to be hidden via #SELECTABLE.
      if (iLocked & LOCKED_SELECTABLE) {
        continue;
      }
      if (so != SORT_ROULETTE && iLocked & LOCKED_ROULETTE) {
        continue;
      }
    }

    /* Hide locked songs. If RANDOM_PICKS_LOCKED_SONGS, hide in Roulette
     * and Random, too. */
    if ((so != SORT_ROULETTE || !RANDOM_PICKS_LOCKED_SONGS) && iLocked) {
      continue;
    }

    if (PREFSMAN->only_preferred_difficulties_) {
      // if the song has steps that fit the preferred difficulty of the default
      // player
      if (pSong->HasStepsTypeAndDifficulty(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_,
              GAMESTATE
                  ->preferred_difficulty_[GAMESTATE->GetFirstHumanPlayer()])) {
        arraySongs.push_back(pSong);
      }
    } else if (
        CommonMetrics::AUTO_SET_STYLE ||
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID) == nullptr) {
      if (SongUtil::IsSongPlayable(pSong)) {
        arraySongs.push_back(pSong);
      }

    } else {
      // If the song has at least one steps, add it.
      if (pSong->HasStepsType(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
        arraySongs.push_back(pSong);
      }
    }
  }

  /* Hack: Add extra stage item if it was eliminated for any reason
   * (eg. it's a long song). */
  if (GAMESTATE->IsAnExtraStage()) {
    Song* pSong;
    Steps* pSteps;
    SONGMAN->GetExtraStageInfo(
        GAMESTATE->IsExtraStage2(), GAMESTATE->GetCurrentStyle(PLAYER_INVALID),
        pSong, pSteps);

    if (find(arraySongs.begin(), arraySongs.end(), pSong) == arraySongs.end()) {
      arraySongs.push_back(pSong);
    }
  }
}

void MusicWheel::BuildWheelItemDatas(
    std::vector<MusicWheelItemData*>& arrayWheelItemDatas, SortOrder so) {
  switch (so) {
    case SORT_MODE_MENU: {
      arrayWheelItemDatas.clear();  // clear out the previous wheel items
      std::vector<std::string> vsNames;
      split(MODE_MENU_CHOICE_NAMES, ",", vsNames);
      for (unsigned i = 0; i < vsNames.size(); ++i) {
        MusicWheelItemData wid(
            WheelItemDataType_Sort, nullptr, "", nullptr, nullptr,
            SORT_MENU_COLOR, 0);
        wid.action_ = HiddenPtr<GameCommand>(new GameCommand);
        wid.action_->name_ = vsNames[i];
        wid.action_->Load(i, ParseCommands(CHOICE.GetValue(vsNames[i])));
        wid.label_ = WHEEL_TEXT(vsNames[i]);

        if (!wid.action_->IsPlayable()) {
          continue;
        }

        arrayWheelItemDatas.push_back(new MusicWheelItemData(wid));
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
      std::vector<Song*> arraySongs;
      GetSongList(arraySongs, so);

      bool bUseSections = true;

      // sort the songs
      switch (so) {
        case SORT_METER:
          SONGMAN->UpdateMeterSort(arraySongs);
          break;
        case SORT_PREFERRED:
          // obey order specified by the preferred sort list
          break;
        case SORT_ROULETTE: {
          StepsType st;
          Difficulty dc;
          SongUtil::GetStepsTypeAndDifficultyFromSortOrder(
              SORT_EASY_METER, st, dc);
          SongUtil::SortSongPointerArrayByStepsTypeAndMeter(arraySongs, st, dc);
          if ((bool)PREFSMAN->preferred_sort_uses_groups_) {
            stable_sort(
                arraySongs.begin(), arraySongs.end(),
                SongUtil::CompareSongPointersByGroup);
          }
          bUseSections = false;
          break;
        }
        case SORT_SERIES:
        case SORT_GROUP:
          SongUtil::SortSongPointerArrayByGroupAndTitle(arraySongs);
          if (USE_SECTIONS_WITH_PREFERRED_GROUP) {
            bUseSections = true;
          } else {
            bUseSections = GAMESTATE->preferred_song_group_ == GROUP_ALL;
          }
          break;
        case SORT_TITLE:
          SongUtil::SortSongPointerArrayByTitle(arraySongs);
          break;
        case SORT_BPM:
          SongUtil::SortSongPointerArrayByBPM(arraySongs);
          break;
        case SORT_POPULARITY:
          if ((int)arraySongs.size() > MOST_PLAYED_SONGS_TO_SHOW) {
            arraySongs.erase(
                arraySongs.begin() + MOST_PLAYED_SONGS_TO_SHOW,
                arraySongs.end());
          }
          bUseSections = true;
          break;
        case SORT_POPULARITY_P1:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_1)) {
            SongUtil::SortSongPointerArrayByNumPlays(
                arraySongs, ProfileSlot_Player1, true);
          }
          if ((int)arraySongs.size() > MOST_PLAYED_SONGS_TO_SHOW) {
            arraySongs.erase(
                arraySongs.begin() + MOST_PLAYED_SONGS_TO_SHOW,
                arraySongs.end());
          }
          bUseSections = true;
          break;
        case SORT_POPULARITY_P2:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_2)) {
            SongUtil::SortSongPointerArrayByNumPlays(
                arraySongs, ProfileSlot_Player2, true);
          }
          if ((int)arraySongs.size() > MOST_PLAYED_SONGS_TO_SHOW) {
            arraySongs.erase(
                arraySongs.begin() + MOST_PLAYED_SONGS_TO_SHOW,
                arraySongs.end());
          }
          bUseSections = true;
          break;
        case SORT_TOP_GRADES:
          SongUtil::SortSongPointerArrayByGrades(arraySongs, true);
          break;
        case SORT_TOP_GRADES_P1:
          // Check if player profile is persistent
          if (PROFILEMAN->IsPersistentProfile(PLAYER_1)) {
            SongUtil::SortSongPointerArrayByProfileGrades(
                arraySongs, true, PLAYER_1);
          }
          break;
        case SORT_TOP_GRADES_P2:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_2)) {
            SongUtil::SortSongPointerArrayByProfileGrades(
                arraySongs, true, PLAYER_2);
          }
          break;
        case SORT_ARTIST:
          SongUtil::SortSongPointerArrayByArtist(arraySongs);
          break;
        case SORT_GENRE:
          SongUtil::SortSongPointerArrayByGenre(arraySongs);
          break;
        case SORT_LENGTH:
          SongUtil::SortSongPointerArrayByLength(arraySongs);
          break;
        case SORT_RECENT:
          SongUtil::SortByMostRecentlyPlayedForMachine(arraySongs);
          if ((int)arraySongs.size() > RECENT_SONGS_TO_SHOW) {
            arraySongs.erase(
                arraySongs.begin() + RECENT_SONGS_TO_SHOW, arraySongs.end());
          }
          bUseSections = true;
          break;
        case SORT_RECENT_P1:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_1)) {
            SongUtil::SortByMostRecentlyPlayedForProfile(arraySongs, PLAYER_1);
          }
          if ((int)arraySongs.size() > RECENT_SONGS_TO_SHOW) {
            arraySongs.erase(
                arraySongs.begin() + RECENT_SONGS_TO_SHOW, arraySongs.end());
          }
          bUseSections = true;
          break;
        case SORT_RECENT_P2:
          if (PROFILEMAN->IsPersistentProfile(PLAYER_2)) {
            SongUtil::SortByMostRecentlyPlayedForProfile(arraySongs, PLAYER_2);
          }
          if ((int)arraySongs.size() > RECENT_SONGS_TO_SHOW) {
            arraySongs.erase(
                arraySongs.begin() + RECENT_SONGS_TO_SHOW, arraySongs.end());
          }
          bUseSections = true;
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
          SongUtil::SortSongPointerArrayByStepsTypeAndMeter(arraySongs, st, dc);
          break;
        default:
          FAIL_M("Unhandled sort order! Aborting...");
      }

      // Build an array of WheelItemDatas from the sorted list of Song*'s
      arrayWheelItemDatas.clear();  // clear out the previous wheel items
      arrayWheelItemDatas.reserve(arraySongs.size());

      switch (PREFSMAN->music_wheel_uses_sections_) {
        case MusicWheelUsesSections_NEVER:
          bUseSections = false;
          break;
        case MusicWheelUsesSections_ABC_ONLY:
          if (so != SORT_TITLE && so != SORT_GROUP) {
            bUseSections = false;
          }
          break;
        default:
          break;
      }

      if (bUseSections) {
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
            SongUtil::SortSongPointerArrayByParentSectionName(arraySongs);
            break;
          case SORT_GROUP:
            SongUtil::SortSongPointerArrayByGroup(arraySongs);
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
            SongUtil::SortSongPointerArrayBySectionName(arraySongs, so);
            break;
        }
      }
      // make WheelItemDatas with sections
      std::string sLastSection = "";
      int iSectionColorIndex = 0;
      switch (so) {
        case SORT_PREFERRED:
          // If the sort order is Preferred handle it differently because we
          // already know the sections
          if (bUseSections) {
            for (const auto& [sectionName, songs] :
                 SONGMAN->GetPreferredSortSongsMap()) {
              // todo: preferred sort section color handling? -aj
              RageColor colorSection =
                  SECTION_COLORS.GetValue(iSectionColorIndex);
              iSectionColorIndex =
                  (iSectionColorIndex + 1) % NUM_SECTION_COLORS;
              // Add the section item
              arrayWheelItemDatas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Section, nullptr, sectionName, nullptr,
                  nullptr, colorSection, songs.size()));
              // Add all the songs in this section
              for (const auto& song : songs) {
                arrayWheelItemDatas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Song, song, sectionName, nullptr, nullptr,
                    SONGMAN->GetSongColor(song), 0));
              }
            }
          }
          break;
        case SORT_METER:
          if (bUseSections) {
            int iSectionCount = 0;
            for (const auto& [sectionName, songs] :
                 SONGMAN->GetMeterToSongsMap()) {
              RageColor colorSection =
                  SECTION_COLORS.GetValue(iSectionColorIndex);
              iSectionColorIndex =
                  (iSectionColorIndex + 1) % NUM_SECTION_COLORS;
              // Add the section item
              arrayWheelItemDatas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Section, nullptr,
                  ssprintf("%d", sectionName), nullptr, nullptr, colorSection,
                  songs.size()));
              // Add all the songs in this section
              for (const auto& song : songs) {
                arrayWheelItemDatas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Song, song, ssprintf("%d", sectionName),
                    nullptr, nullptr, SONGMAN->GetSongColor(song), 0));
              }
            }
          }
          break;
        case SORT_GROUP:
          for (unsigned i = 0; i < arraySongs.size(); i++) {
            Song* pSong = arraySongs[i];
            Group* pGroup = SONGMAN->GetGroup(pSong);
            if (bUseSections) {
              std::string sThisSection = pGroup->GetGroupName();

              if (sThisSection != sLastSection) {
                int iSectionCount = 0;
                // Count songs in this section
                unsigned j;
                for (j = i; j < arraySongs.size(); j++) {
                  if (SONGMAN->GetGroup(arraySongs[j]) == nullptr) {
                    LOG->Warn(
                        "Song %s has no group!",
                        arraySongs[j]->GetSongDir().c_str());
                    continue;
                  } else if (
                      SONGMAN->GetGroup(arraySongs[j])->GetGroupName() !=
                      sThisSection) {
                    break;
                  }
                }
                iSectionCount = j - i;

                // new section, make a section item
                // todo: preferred sort section color handling? -aj
                RageColor colorSection =
                    (so == SORT_GROUP)
                        ? SONGMAN->GetSongGroupColor(sThisSection)
                        : SECTION_COLORS.GetValue(iSectionColorIndex);
                iSectionColorIndex =
                    (iSectionColorIndex + 1) % NUM_SECTION_COLORS;
                arrayWheelItemDatas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Section, nullptr, sThisSection, nullptr,
                    pGroup, colorSection, iSectionCount));
                sLastSection = sThisSection;
              }
            }
            arrayWheelItemDatas.push_back(new MusicWheelItemData(
                WheelItemDataType_Song, pSong, sLastSection, nullptr, pGroup,
                SONGMAN->GetSongColor(pSong), 0));
          }
          break;

        case SORT_SERIES:
          // this is currently the only sort that creates
          // WheelItemDataType_ParentSection items.
          if (bUseSections) {
            std::string lastSeries;
            std::string lastGroup;
            bool haveLast = false;
            for (unsigned i = 0; i < arraySongs.size(); ++i) {
              Song* pSong = arraySongs[i];
              Group* pGroup = SONGMAN->GetGroup(pSong);
              if (pGroup == nullptr) {
                LOG->Warn("Song %s has no group!", pSong->GetSongDir().c_str());
                continue;
              }
              const std::string groupName = pGroup->GetGroupName();
              int numGroupsInSeries =
                  SONGMAN->GetGroupsInSeries(pGroup->GetSeries()).size();
              std::string seriesName;
              if (numGroupsInSeries > 1) {
                seriesName = pGroup->GetSeries();
              } else {
                seriesName = "";
              }
              // New ParentSection for each new series.
              if (!seriesName.empty() &&
                  (!haveLast || seriesName != lastSeries)) {
                int seriesSongCount = 0;
                for (unsigned j = i; j < arraySongs.size(); ++j) {
                  Group* pG = SONGMAN->GetGroup(arraySongs[j]);
                  if (pG != nullptr && pG->GetSeries() == seriesName) {
                    ++seriesSongCount;
                  } else {
                    break;
                  }
                }
                RageColor seriesColor =
                    SECTION_COLORS.GetValue(iSectionColorIndex);
                iSectionColorIndex =
                    (iSectionColorIndex + 1) % NUM_SECTION_COLORS;
                MusicWheelItemData* pSeriesItem = new MusicWheelItemData(
                    WheelItemDataType_ParentSection, nullptr, seriesName,
                    nullptr, nullptr, seriesColor, seriesSongCount);
                pSeriesItem->label_ = seriesName;
                arrayWheelItemDatas.push_back(pSeriesItem);
              }

              if (!haveLast || groupName != lastGroup) {
                int groupSongCount = 0;
                for (unsigned j = i; j < arraySongs.size(); ++j) {
                  Group* pG = SONGMAN->GetGroup(arraySongs[j]);
                  if (pG == pGroup) {
                    ++groupSongCount;
                  } else {
                    break;
                  }
                }
                arrayWheelItemDatas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Section, nullptr, groupName, nullptr,
                    pGroup, SONGMAN->GetSongGroupColor(groupName),
                    groupSongCount, seriesName));
              }

              arrayWheelItemDatas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Song, pSong, groupName, nullptr, pGroup,
                  SONGMAN->GetSongColor(pSong), 0, seriesName));

              lastSeries = seriesName;
              lastGroup = groupName;
              haveLast = true;
            }
          } else {
            for (unsigned i = 0; i < arraySongs.size(); i++) {
              Song* pSong = arraySongs[i];
              Group* pGroup = SONGMAN->GetGroup(pSong);
              arrayWheelItemDatas.push_back(new MusicWheelItemData(
                  WheelItemDataType_Song, pSong, sLastSection, nullptr, pGroup,
                  SONGMAN->GetSongColor(pSong), 0));
            }
          }
          break;

        default:
          for (unsigned i = 0; i < arraySongs.size(); i++) {
            Song* pSong = arraySongs[i];
            if (bUseSections) {
              std::string sThisSection =
                  SongUtil::GetSectionNameFromSongAndSort(pSong, so);

              if (sThisSection != sLastSection) {
                int iSectionCount = 0;
                // Count songs in this section
                unsigned j;
                for (j = i; j < arraySongs.size(); j++) {
                  if (SongUtil::GetSectionNameFromSongAndSort(
                          arraySongs[j], so) != sThisSection) {
                    break;
                  }
                }
                iSectionCount = j - i;

                // new section, make a section item
                // todo: preferred sort section color handling? -aj
                RageColor colorSection =
                    (so == SORT_GROUP)
                        ? SONGMAN->GetSongGroupColor(sThisSection)
                        : SECTION_COLORS.GetValue(iSectionColorIndex);
                iSectionColorIndex =
                    (iSectionColorIndex + 1) % NUM_SECTION_COLORS;
                arrayWheelItemDatas.push_back(new MusicWheelItemData(
                    WheelItemDataType_Section, nullptr, sThisSection, nullptr,
                    nullptr, colorSection, iSectionCount));
                sLastSection = sThisSection;
              }
            }
            arrayWheelItemDatas.push_back(new MusicWheelItemData(
                WheelItemDataType_Song, pSong, sLastSection, nullptr, nullptr,
                SONGMAN->GetSongColor(pSong), 0));
          }
          break;
      }

      if (so != SORT_ROULETTE) {
        // todo: allow themers to change the order of the items. -aj
        if (SHOW_ROULETTE) {
          arrayWheelItemDatas.push_back(new MusicWheelItemData(
              WheelItemDataType_Roulette, nullptr, "", nullptr, nullptr,
              ROULETTE_COLOR, 0));
        }

        // Only add WheelItemDataType_Random and WheelItemDataType_Portal if
        // there's at least one song on the list.
        bool bFoundAnySong = false;
        for (unsigned i = 0; !bFoundAnySong && i < arrayWheelItemDatas.size();
             i++) {
          if (arrayWheelItemDatas[i]->type_ == WheelItemDataType_Song) {
            bFoundAnySong = true;
          }
        }

        if (SHOW_RANDOM && bFoundAnySong) {
          arrayWheelItemDatas.push_back(new MusicWheelItemData(
              WheelItemDataType_Random, nullptr, "", nullptr, nullptr,
              RANDOM_COLOR, 0));
        }

        if (SHOW_PORTAL && bFoundAnySong) {
          arrayWheelItemDatas.push_back(new MusicWheelItemData(
              WheelItemDataType_Portal, nullptr, "", nullptr, nullptr,
              PORTAL_COLOR, 0));
        }

        // add custom wheel items
        std::vector<std::string> vsNames;
        split(CUSTOM_WHEEL_ITEM_NAMES, ",", vsNames);
        for (unsigned i = 0; i < vsNames.size(); ++i) {
          MusicWheelItemData wid(
              WheelItemDataType_Custom, nullptr, "", nullptr, nullptr,
              CUSTOM_CHOICE_COLORS.GetValue(vsNames[i]), 0);
          wid.action_ = HiddenPtr<GameCommand>(new GameCommand);
          wid.action_->name_ = vsNames[i];
          wid.action_->Load(
              i, ParseCommands(CUSTOM_CHOICES.GetValue(vsNames[i])));
          wid.label_ = CUSTOM_ITEM_WHEEL_TEXT(vsNames[i]);

          if (!wid.action_->IsPlayable()) {
            continue;
          }

          arrayWheelItemDatas.push_back(new MusicWheelItemData(wid));
        }
      }

      if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
        Song* pSong;
        Steps* pSteps;
        SONGMAN->GetExtraStageInfo(
            GAMESTATE->IsExtraStage2(),
            GAMESTATE->GetCurrentStyle(PLAYER_INVALID), pSong, pSteps);

        for (unsigned i = 0; i < arrayWheelItemDatas.size(); i++) {
          if (arrayWheelItemDatas[i]->song_ == pSong) {
            // Change the song color.
            arrayWheelItemDatas[i]->color_ = SONG_REAL_EXTRA_COLOR;
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
      bool bOnlyPreferred =
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

      std::vector<Course*> apCourses;
      for (const CourseType& ct : vct) {
        if (bOnlyPreferred) {
          SONGMAN->GetPreferredSortCourses(
              ct, apCourses, PREFSMAN->autogen_group_courses_);
        } else {
          SONGMAN->GetCourses(ct, apCourses, PREFSMAN->autogen_group_courses_);
        }
      }

      switch (PREFSMAN->course_sort_order_) {
        case COURSE_SORT_SONGS:
          CourseUtil::SortCoursePointerArrayByDifficulty(apCourses);
          break;
        case COURSE_SORT_PREFERRED:
          break;
        case COURSE_SORT_METER:
          CourseUtil::SortCoursePointerArrayByAvgDifficulty(apCourses);
          break;
        case COURSE_SORT_METER_SUM:
          CourseUtil::SortCoursePointerArrayByTotalDifficulty(apCourses);
          break;
        case COURSE_SORT_RANK:
          CourseUtil::SortCoursePointerArrayByRanking(apCourses);
          break;
        default:
          FAIL_M("Impossible to sort the courses! Aborting...");
      }

      // since we can't agree, make it an option
      if (PREFSMAN->course_sort_order_ != COURSE_SORT_SONGS &&
          g_bMoveRandomToEnd) {
        CourseUtil::MoveRandomToEnd(apCourses);
      }

      if (so == SORT_ALL_COURSES) {
        CourseUtil::SortCoursePointerArrayByType(apCourses);
      }

      arrayWheelItemDatas.clear();  // clear out the previous wheel items

      std::string sLastSection = "";
      int iSectionColorIndex = 0;
      for (unsigned i = 0; i < apCourses.size(); i++)  // foreach course
      {
        Course* pCourse = apCourses[i];

        // if unlocks are on, make sure it is unlocked
        if (UNLOCKMAN->CourseIsLocked(pCourse)) {
          continue;
        }

        std::string sThisSection = "";
        if (so == SORT_ALL_COURSES) {
          switch (pCourse->GetPlayMode()) {
            case PLAY_MODE_ONI:
              sThisSection = "Oni";
              break;
            case PLAY_MODE_NONSTOP:
              sThisSection = "Nonstop";
              break;
            case PLAY_MODE_ENDLESS:
              sThisSection = "Endless";
              break;
            default:
              break;
          }
        }

        // check that this course has at least one song playable in the current
        // style
        if (!pCourse->IsPlayableIn(
                GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
          continue;
        }

        if (sThisSection != sLastSection)  // new section, make a section item
        {
          RageColor c = SECTION_COLORS.GetValue(iSectionColorIndex);
          iSectionColorIndex = (iSectionColorIndex + 1) % NUM_SECTION_COLORS;
          arrayWheelItemDatas.push_back(new MusicWheelItemData(
              WheelItemDataType_Section, nullptr, sThisSection, nullptr,
              nullptr, c, 0));
          sLastSection = sThisSection;
        }

        RageColor c = (pCourse->group_name_.size() == 0)
                          ? pCourse->GetColor()
                          : SONGMAN->GetCourseColor(pCourse);
        arrayWheelItemDatas.push_back(new MusicWheelItemData(
            WheelItemDataType_Course, nullptr, sThisSection, pCourse, nullptr,
            c, 0));
      }
      break;
    }
    default:
      break;
  }

  // init music status icons
  for (MusicWheelItemData* WID : arrayWheelItemDatas) {
    if (WID->song_ != nullptr) {
      WID->flags_.has_beginner_or1_meter_ =
          WID->song_->IsEasy(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_) &&
          SHOW_EASY_FLAG;
      WID->flags_.edits_ = false;
      std::set<StepsType> vStepsType;
      SongUtil::GetPlayableStepsTypes(WID->song_, vStepsType);
      for (const StepsType& type : vStepsType) {
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

  std::vector<MusicWheelItemData*>& aUnFilteredDatas =
      un_filtered_wheel_item_datas_[so];

  if (wheel_item_datas_status_[so] == INVALID) {
    BuildWheelItemDatas(aUnFilteredDatas, so);
  }
  FilterWheelItemDatas(aUnFilteredDatas, wheel_item_datas_[so], so);
  // The preferred sort's songs are subject to change during a session
  // (particularly if two players have different preferred songs) thus it's
  // status should remain invalid so the wheel items are rebuilt each time in
  // case of change.
  if (so != SORT_PREFERRED) {
    wheel_item_datas_status_[so] = VALID;
  }
}

void MusicWheel::FilterWheelItemDatas(
    std::vector<MusicWheelItemData*>& aUnFilteredDatas,
    std::vector<MusicWheelItemData*>& aFilteredData, SortOrder so) {
  aFilteredData.clear();

  unsigned unfilteredSize = aUnFilteredDatas.size();

  /* Only add WheelItemDataType_Portal if there's at least one song on the list.
   */
  bool bFoundAnySong = false;
  for (unsigned i = 0; i < unfilteredSize; i++) {
    if (aUnFilteredDatas[i]->type_ == WheelItemDataType_Song) {
      bFoundAnySong = true;
      break;
    }
  }

  std::vector<bool> aiRemove;
  aiRemove.insert(aiRemove.begin(), unfilteredSize, false);

  const int iMaxStagesForSong =
      GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer();

  Song* pExtraStageSong = nullptr;
  if (GAMESTATE->IsAnExtraStage()) {
    Steps* pSteps;
    SONGMAN->GetExtraStageInfo(
        GAMESTATE->IsExtraStage2(), GAMESTATE->GetCurrentStyle(PLAYER_INVALID),
        pExtraStageSong, pSteps);
  }

  /* Mark any songs that aren't playable in aiRemove. */

  for (unsigned i = 0; i < unfilteredSize; i++) {
    MusicWheelItemData& WID = *aUnFilteredDatas[i];

    /* If we have no songs, remove Random and Portal. */
    if (WID.type_ == WheelItemDataType_Random ||
        WID.type_ == WheelItemDataType_Portal ||
        WID.type_ == WheelItemDataType_Roulette) {
      if (!bFoundAnySong) {
        aiRemove[i] = true;
      }
      if (STATE_ROULETTE_SPINNING == wheel_state_) {
        aiRemove[i] = true;
      }
      continue;
    }

    if ((WID.type_ == WheelItemDataType_Section ||
         WID.type_ == WheelItemDataType_ParentSection) &&
        STATE_ROULETTE_SPINNING == wheel_state_) {
      aiRemove[i] = true;
    }

    /* Filter songs that we don't have enough stages to play. */
    if (WID.type_ == WheelItemDataType_Song) {
      Song* pSong = WID.song_;

      /* Never remove the extra stage song. */
      if (pExtraStageSong && WID.song_ == pExtraStageSong) {
        continue;
      }

      /* Check that we have enough stages to play this song. */
      if (GAMESTATE->GetNumStagesMultiplierForSong(WID.song_) >
          iMaxStagesForSong) {
        aiRemove[i] = true;
        continue;
      }

      int iLocked = UNLOCKMAN->SongIsLocked(pSong);
      if (iLocked & LOCKED_DISABLED) {
        aiRemove[i] = true;
        continue;
      }

      /* If we're on an extra stage, and this song is selected, ignore
       * #SELECTABLE. */
      if (pSong != GAMESTATE->cur_song_ || !GAMESTATE->IsAnExtraStage()) {
        /* Hide songs that asked to be hidden via #SELECTABLE. */
        if (iLocked & LOCKED_SELECTABLE) {
          aiRemove[i] = true;
          continue;
        }
        if (so != SORT_ROULETTE && iLocked & LOCKED_ROULETTE) {
          aiRemove[i] = true;
          continue;
        }
      }

      /* Hide locked songs.  If RANDOM_PICKS_LOCKED_SONGS, hide in Roulette and
       * Random, too. */
      if ((so != SORT_ROULETTE || !RANDOM_PICKS_LOCKED_SONGS) && iLocked) {
        aiRemove[i] = true;
        continue;
      }

      /* If the song has no steps for the current style, remove it. */
      if (!CommonMetrics::AUTO_SET_STYLE &&
          !pSong->HasStepsType(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
        aiRemove[i] = true;
        continue;
      }

      // if AutoSetStyle, make sure the song is playable in the end.
      if (!SongUtil::IsSongPlayable(pSong)) {
        aiRemove[i] = true;
        continue;
      }

      if (!GAMESTATE->IsSongAllowedByPremiumFree(pSong)) {
        aiRemove[i] = true;
        continue;
      }
    }

    if (WID.type_ == WheelItemDataType_Course) {
      if (!WID.course_->IsPlayableIn(
              GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_)) {
        aiRemove[i] = true;
      }
    }
  }

  /* Filter out the songs we're removing. */

  aFilteredData.reserve(unfilteredSize);
  for (unsigned i = 0; i < unfilteredSize; i++) {
    if (aiRemove[i]) {
      continue;
    }
    aFilteredData.push_back(aUnFilteredDatas[i]);
  }

  // Update the song count in each section header.
  unsigned filteredSize = aFilteredData.size();
  MusicWheelItemData* pCurSection = nullptr;
  MusicWheelItemData* pCurParent = nullptr;
  for (unsigned i = 0; i < filteredSize; ++i) {
    MusicWheelItemData& WID = *aFilteredData[i];
    if (WID.type_ == WheelItemDataType_ParentSection) {
      pCurParent = &WID;
      pCurParent->section_count_ = 0;
      pCurSection = nullptr;
    } else if (WID.type_ == WheelItemDataType_Section) {
      pCurSection = &WID;
      pCurSection->section_count_ = 0;
      if (WID.parent_section_.empty()) {
        pCurParent = nullptr;
      }
    } else if (WID.type_ == WheelItemDataType_Song) {
      if (pCurSection != nullptr) {
        ++pCurSection->section_count_;
      }
      if (pCurParent != nullptr && !WID.parent_section_.empty()) {
        ++pCurParent->section_count_;
      }
    }
  }

  // If we have any section headers with no songs, then we filtered all of the
  // songs in that group, so remove it.  This isn't optimized like the above
  // since this is a rare case.
  for (unsigned i = 0; i < filteredSize; ++i) {
    MusicWheelItemData& WID = *aFilteredData[i];
    if (WID.type_ != WheelItemDataType_Section &&
        WID.type_ != WheelItemDataType_ParentSection) {
      continue;
    }
    if (WID.section_count_ > 0) {
      continue;
    }
    aFilteredData.erase(
        aFilteredData.begin() + i, aFilteredData.begin() + i + 1);
    --i;
    --filteredSize;
  }

  /* Update the popularity.  This is affected by filtering. */
  if (so == SORT_POPULARITY || so == SORT_POPULARITY_P1 ||
      so == SORT_POPULARITY_P2)

  {
    for (unsigned i = 0; i < std::min<unsigned int>(3u, aFilteredData.size());
         i++) {
      MusicWheelItemData& WID = *aFilteredData[i];
      WID.flags_.players_best_number_ = i + 1;
    }
  }

  // If we've filtered all items, insert a dummy.
  if (aFilteredData.empty()) {
    aFilteredData.push_back(new MusicWheelItemData(
        WheelItemDataType_Section, nullptr, EMPTY_STRING, nullptr, nullptr,
        EMPTY_COLOR, 0));
  }
}

void MusicWheel::UpdateSwitch() {
  switch (wheel_state_) {
    case STATE_FLYING_OFF_BEFORE_NEXT_SORT: {
      const Song* pPrevSelectedSong = GetCurWheelItemData(selection_)->song_;

      SCREENMAN->PostMessageToTopScreen(SM_SortOrderChanged, 0);

      SetOpenSection(
          SongUtil::GetSectionNameFromSongAndSort(
              pPrevSelectedSong, GAMESTATE->sort_order_));

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
        const float SwitchTimes[] = {0.5f, 1.3f, 0.8f, 0.4f, 0.2f};
        ASSERT(
            switches_left_in_spin_down_ >= 0 &&
            switches_left_in_spin_down_ <= 4);
        time_left_in_state_ = SwitchTimes[switches_left_in_spin_down_];
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

void MusicWheel::ChangeMusic(int iDist) {
  selection_ += iDist;
  wrap(selection_, cur_wheel_item_data_.size());

  if (REMIND_WHEEL_POSITIONS && HIDE_INACTIVE_SECTIONS) {
    // store the group song index
    for (unsigned idx = 0; idx < wheel_positions_.size(); idx++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(idx)) {
        wheel_positions_[idx] = selection_;
      }
    }
  }

  RebuildWheelItems(iDist);

  position_offset_from_selection_ += iDist;

  SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);

  // If we're moving automatically, don't play this; it'll be called in Update.
  if (!IsMoving()) {
    sound_change_music_.Play(true);
  }
}

bool MusicWheel::ChangeSort(
    SortOrder new_so, bool allowSameSort)  // return true if change successful
{
  ASSERT(new_so < NUM_SortOrder);
  // Reset LastOpenSection as sections differ between sorts
  GAMESTATE->last_open_section_ = "";
  // NOTE(crashcringle): Ignore allowSameSort if we're using SORT_PREFERRED.
  // Each player has their own preferred songs which sorts the songs differently
  if (GAMESTATE->sort_order_ == new_so &&
      (!allowSameSort && new_so != SORT_PREFERRED)) {
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
  GAMESTATE->sort_order_.Set(new_so);

  return true;
}

bool MusicWheel::NextSort()  // return true if change successful
{
  // don't allow NextSort when on the mode menu
  if (GAMESTATE->sort_order_ == SORT_MODE_MENU) {
    return false;
  }

  std::vector<SortOrder> aSortOrders;
  {
    Lua* L = LUA->Get();
    SORT_ORDERS.PushSelf(L);
    FOREACH_LUATABLE(L, -1) {
      SortOrder so = Enum::Check<SortOrder>(L, -1, true);
      aSortOrders.push_back(so);
    }
    lua_pop(L, 1);
    LUA->Release(L);
  }

  // find the index of the current sort
  int cur = 0;
  while (cur < int(aSortOrders.size()) &&
         aSortOrders[cur] != GAMESTATE->sort_order_) {
    ++cur;
  }

  // move to the next sort with wrapping
  ++cur;
  wrap(cur, aSortOrders.size());

  // apply new sort
  SortOrder soNew = aSortOrders[cur];
  return ChangeSort(soNew);
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

  const MusicWheelItemData* pSelection = GetCurWheelItemData(selection_);
  if (pSelection->type_ == WheelItemDataType_Section ||
      pSelection->type_ == WheelItemDataType_ParentSection) {
    GAMESTATE->last_open_section_ = pSelection->text_;
    if (pSelection->type_ == WheelItemDataType_ParentSection) {
      if (expanded_parent_section_name_ == pSelection->text_ &&
          expanded_section_name_.empty()) {
        SetOpenSections("", "");
        sound_collapse_.Play(true);
      } else {
        SetOpenSections(pSelection->text_, "");
        sound_expand_.Play(true);
      }
      return false;
    }

    if (!pSelection->parent_section_.empty()) {
      if (expanded_parent_section_name_ == pSelection->parent_section_ &&
          expanded_section_name_ == pSelection->text_) {
        SetOpenSections(pSelection->parent_section_, "");
        sound_collapse_.Play(true);
      } else {
        SetOpenSections(pSelection->parent_section_, pSelection->text_);
        sound_expand_.Play(true);
      }
      return false;
    }

    if (expanded_parent_section_name_.empty() &&
        expanded_section_name_ == pSelection->text_) {
      SetOpenSections("", "");
      sound_collapse_.Play(true);
    } else {
      SetOpenSections("", pSelection->text_);
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
    auto& rouletteItems = getWheelItemsData(SORT_ROULETTE);
    std::shuffle(
        rouletteItems.begin(), rouletteItems.end(), g_RandomNumberGenerator);
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
    const std::string parentSectionName = expanded_parent_section_name_;
    const std::string childSectionName = expanded_section_name_;
    SetOpenSections(parentSectionName, "");
    SelectSection(childSectionName);
    RebuildWheelItems();
    return true;
  }

  // Only a parent section is open: collapse it.
  if (!expanded_parent_section_name_.empty()) {
    const std::string parentSectionName = expanded_parent_section_name_;
    SetOpenSections("", "");
    SelectSection(parentSectionName);
    RebuildWheelItems();
    return true;
  }

  if (!expanded_section_name_.empty()) {
    const std::string sectionName = expanded_section_name_;
    SetOpenSections("", "");
    SelectSection(sectionName);
    RebuildWheelItems();
    return true;
  }

  return false;
}

void MusicWheel::SetOpenSections(
    const std::string& parentSection, const std::string& section) {
  expanded_parent_section_name_ = parentSection;
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

  std::vector<const Style*> vpPossibleStyles;
  if (CommonMetrics::AUTO_SET_STYLE) {
    GAMEMAN->GetCompatibleStyles(
        GAMESTATE->cur_game_, GAMESTATE->GetNumPlayersEnabled(),
        vpPossibleStyles);
  }

  cur_wheel_item_data_.clear();
  std::vector<MusicWheelItemData*>& from =
      getWheelItemsData(GAMESTATE->sort_order_);
  cur_wheel_item_data_.reserve(from.size());

  // A parent section is open but no child section has been drilled into
  // yet: show only parent sections and the child sections of the open
  // parent (no songs).
  const bool parentSectionOnlyView =
      (GAMESTATE->sort_order_ == SORT_SERIES &&
       !expanded_parent_section_name_.empty() &&
       expanded_section_name_.empty());

  if (parentSectionOnlyView) {
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
        bool isActiveParent = d.type_ == WheelItemDataType_ParentSection &&
                              d.parent_section_.empty() &&
                              d.text_ == expanded_parent_section_name_;
        bool isChildOfActive =
            !d.parent_section_.empty() &&
            d.parent_section_ == expanded_parent_section_name_;
        if (!isActiveParent && !isChildOfActive) {
          continue;
        }
        if (HIDE_ACTIVE_SECTION_TITLE && isActiveParent) {
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
        bool isOpenParentSection = d.type_ == WheelItemDataType_ParentSection &&
                                   d.parent_section_.empty() &&
                                   d.text_ == expanded_parent_section_name_;
        bool isOpenChildSection =
            !d.parent_section_.empty() &&
            d.parent_section_ == expanded_parent_section_name_ &&
            d.text_ == expanded_section_name_;
        bool isOpenTopSection = d.type_ == WheelItemDataType_Section &&
                                d.parent_section_.empty() &&
                                expanded_parent_section_name_.empty() &&
                                d.text_ == expanded_section_name_;
        if (!isOpenParentSection && !isOpenChildSection && !isOpenTopSection) {
          continue;
        }
        if (HIDE_ACTIVE_SECTION_TITLE &&
            (isOpenChildSection || isOpenTopSection)) {
          continue;
        }
      }

      // If AUTO_SET_STYLE, hide courses that prefer a style that isn't
      // available.
      if (d.type_ == WheelItemDataType_Course &&
          CommonMetrics::AUTO_SET_STYLE) {
        const Style* pStyle = d.course_->GetCourseStyle(
            GAMESTATE->cur_game_, GAMESTATE->GetNumSidesJoined());
        if (pStyle) {
          if (find(vpPossibleStyles.begin(), vpPossibleStyles.end(), pStyle) ==
              vpPossibleStyles.end()) {
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
  std::vector<MusicWheelItemData*>& wiWheelItems =
      getWheelItemsData(GAMESTATE->sort_order_);
  for (unsigned i = 0; i < wiWheelItems.size(); i++) {
    if ((wiWheelItems[i]->type_ == WheelItemDataType_Section ||
         wiWheelItems[i]->type_ == WheelItemDataType_ParentSection) &&
        wiWheelItems[i]->parent_section_.empty() &&
        !wiWheelItems[i]->text_.empty()) {
      if (wiWheelItems[i]->type_ == WheelItemDataType_ParentSection &&
          !wiWheelItems[i]->label_.empty()) {
        sections.push_back(wiWheelItems[i]->label_);
      } else {
        sections.push_back(wiWheelItems[i]->text_);
      }
    }
  }
}

// sm-ssc additions: jump to group
std::string MusicWheel::JumpToNextGroup() {
  // Thanks to Juanelote for this logic:
  if (HIDE_INACTIVE_SECTIONS) {
    // todo: make it work with other sort types
    unsigned iNumGroups = SONGMAN->GetNumSongGroups();

    for (unsigned i = 0; i < iNumGroups; i++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(i)) {
        if (i < iNumGroups - 1) {
          return SONGMAN->GetSongGroupByIndex(i + 1);
        } else {
          // i = 0;
          return SONGMAN->GetSongGroupByIndex(0);
        }
      }
    }
  } else {
    unsigned int iLastSelection = selection_;
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
    for (unsigned int i = 0; i < iLastSelection; ++i) {
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
    unsigned iNumGroups = SONGMAN->GetNumSongGroups();

    for (unsigned i = 0; i < iNumGroups; i++) {
      if (expanded_section_name_ == SONGMAN->GetSongGroupByIndex(i)) {
        if (i > 0) {
          return SONGMAN->GetSongGroupByIndex(i - 1);
        } else {
          // i = iNumGroups - 1;
          return SONGMAN->GetSongGroupByIndex(iNumGroups - 1);
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
  std::vector<Difficulty> vDifficultiesToRequire;
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

    vDifficultiesToRequire.push_back(GAMESTATE->preferred_difficulty_[p]);
  }

  std::vector<MusicWheelItemData*>& wid =
      getWheelItemsData(GAMESTATE->sort_order_);
  StepsType st = GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                     ->steps_type_;
  std::vector<MusicWheelItemData*> randomSongs;

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
        randomSongs.push_back(selection);
      }
    }
  } else {
    // Otherwise, pick from all songs in the current sort.
    randomSongs = wid;
  }
  std::shuffle(randomSongs.begin(), randomSongs.end(), g_RandomNumberGenerator);

#define NUM_PROBES 1000
  for (int i = 0; i < NUM_PROBES; i++) {
    bool isValid = true;
    int iSelection = 0;
    Song* pSong;
    iSelection = RandomInt(randomSongs.size());
    MusicWheelItemData* selection =
        (MusicWheelItemData*)randomSongs[iSelection];
    pSong = selection->song_;

    if (pSong == nullptr) {
      continue;
    }

    // There's an off possibility that somebody might have only one song with
    // only beginner steps.
    if (i < 900 && pSong->IsTutorial()) {
      continue;
    }

    // We need to check for difficulty size because when you first enter the
    // music wheel You may not have a preferred difficulty set yet and this
    // would result in every song even songs in other game types, being valid.

    if (i == NUM_PROBES / 2 || !PREFSMAN->random_prioritize_difficulty_) {
      vDifficultiesToRequire.clear();
    }
    if (vDifficultiesToRequire.size() != 0) {
      isValid = std::none_of(
          vDifficultiesToRequire.begin(), vDifficultiesToRequire.end(),
          [&](const Difficulty& d) {
            return !pSong->HasStepsTypeAndDifficulty(st, d);
          });
    } else {
      isValid = pSong->HasStepsType(st);
    }

    Song* song = pSong;
    if (isValid && SongUtil::IsSongPlayable(song)) {
      return pSong;
    }
  }
  LuaHelpers::ReportScriptError("Couldn't find any songs");
  return randomSongs[0]->song_;
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
      Song* pS = Luna<Song>::check(L, 1, true);
      lua_pushboolean(L, p->SelectSong(pS));
    }
    return 1;
  }
  static int SelectCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      lua_pushboolean(L, false);
    } else {
      Course* pC = Luna<Course>::check(L, 1, true);
      lua_pushboolean(L, p->SelectCourse(pC));
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
