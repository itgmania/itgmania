#include "SongUtil.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "CommonMetrics.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Grade.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "UnlockManager.h"
#include "XmlFile.h"
#include "global.h"

ThemeMetric<int> SORT_BPM_DIVISION("MusicWheel", "SortBPMDivision");
ThemeMetric<int> SORT_LENGTH_DIVISION("MusicWheel", "SortLengthDivision");
ThemeMetric<bool> SHOW_SECTIONS_IN_BPM_SORT(
    "MusicWheel", "ShowSectionsInBPMSort");
ThemeMetric<bool> SHOW_SECTIONS_IN_LENGTH_SORT(
    "MusicWheel", "ShowSectionsInLengthSort");

bool SongCriteria::Matches(const Song* song) const {
  if (group_names_.size() > 0 &&
      std::find(group_names_.begin(), group_names_.end(), song->group_name_) ==
          group_names_.end()) {
    return false;
  }

  if (song_names_.size() > 0 &&
      std::find(song_names_.begin(), song_names_.end(), song->song_name_) ==
          song_names_.end() &&
      std::find(song_names_.begin(), song_names_.end(), song->main_title_) ==
          song_names_.end() &&
      std::find(
          song_names_.begin(), song_names_.end(), song->main_title_translit_) ==
          song_names_.end()) {
    return false;
  }

  if (artist_names_.size() > 0 &&
      std::find(artist_names_.begin(), artist_names_.end(), song->artist_) ==
          artist_names_.end() &&
      std::find(
          artist_names_.begin(), artist_names_.end(), song->artist_translit_) ==
          artist_names_.end()) {
    return false;
  }

  if (UNLOCKMAN->SongIsLocked(song) & LOCKED_DISABLED) {
    return false;
  }

  if (use_song_genre_allowed_list_) {
    if (find(
            song_genre_allowed_list_.begin(), song_genre_allowed_list_.end(),
            song->genre_) == song_genre_allowed_list_.end()) {
      return false;
    }
  }

  switch (selectable_) {
    DEFAULT_FAIL(selectable_);
    case Selectable_Yes:
      if (UNLOCKMAN && UNLOCKMAN->SongIsLocked(song) & LOCKED_SELECTABLE) {
        return false;
      }
      break;
    case Selectable_No:
      if (UNLOCKMAN && !(UNLOCKMAN->SongIsLocked(song) & LOCKED_SELECTABLE)) {
        return false;
      }
      break;
    case Selectable_DontCare:
      break;
  }

  if (use_song_allowed_list_) {
    if (find(song_allowed_list_.begin(), song_allowed_list_.end(), song) ==
        song_allowed_list_.end()) {
      return false;
    }
  }

  if (max_stages_for_song_ != -1 &&
      GAMESTATE->GetNumStagesMultiplierForSong(song) > max_stages_for_song_) {
    return false;
  }

  if (max_bpm_ != -1 && min_bpm_ != -1) {
    DisplayBpms bpms;
    song->GetDisplayBpms(bpms);
    if (bpms.GetMax() > max_bpm_) {
      return false;
    }
    if (bpms.GetMin() < min_bpm_) {
      return false;
    }
  }

  if (min_duration_seconds_ != -1 &&
      song->music_length_seconds_ < min_duration_seconds_) {
    return false;
  }

  if (max_duration_seconds_ != -1 &&
      song->music_length_seconds_ > max_duration_seconds_) {
    return false;
  }

  switch (tutorial_) {
    DEFAULT_FAIL(tutorial_);
    case Tutorial_Yes:
      if (!song->IsTutorial()) {
        return false;
      }
      break;
    case Tutorial_No:
      if (song->IsTutorial()) {
        return false;
      }
      break;
    case Tutorial_DontCare:
      break;
  }

  switch (locked_) {
    DEFAULT_FAIL(locked_);
    case Locked_Locked:
      if (UNLOCKMAN && !(UNLOCKMAN->SongIsLocked(song) & LOCKED_LOCK)) {
        return false;
      }
      break;
    case Locked_Unlocked:
      if (UNLOCKMAN && UNLOCKMAN->SongIsLocked(song) & LOCKED_LOCK) {
        return false;
      }
      break;
    case Locked_DontCare:
      break;
  }

  return true;
}

void SongUtil::GetSteps(
    const Song* song, std::vector<Steps*>& array_add_to, StepsType st,
    Difficulty dc, int meter_low, int meter_high,
    const std::string& description, const std::string& credit,
    bool include_auto_gen, unsigned hash, int max_to_get) {
  if (!max_to_get) {
    return;
  }

  const std::vector<Steps*>& vp_steps = st == StepsType_Invalid
                                            ? song->GetAllSteps()
                                            : song->GetStepsByStepsType(st);
  for (unsigned i = 0; i < vp_steps.size();
       i++)  // for each of the Song's Steps
  {
    Steps* p_steps = vp_steps[i];

    if (dc != Difficulty_Invalid && dc != p_steps->GetDifficulty()) {
      continue;
    }
    if (meter_low != -1 && meter_low > p_steps->GetMeter()) {
      continue;
    }
    if (meter_high != -1 && meter_high < p_steps->GetMeter()) {
      continue;
    }
    if (description.size() && description != p_steps->GetDescription()) {
      continue;
    }
    if (credit.size() && credit != p_steps->GetCredit()) {
      continue;
    }
    if (hash != 0 && hash != p_steps->GetHash()) {
      continue;
    }
    if (!include_auto_gen && p_steps->IsAutogen()) {
      continue;
    }

    array_add_to.push_back(p_steps);

    if (max_to_get != -1) {
      --max_to_get;
      if (!max_to_get) {
        break;
      }
    }
  }
}

Steps* SongUtil::GetOneSteps(
    const Song* song, StepsType st, Difficulty dc, int meter_low,
    int meter_high, const std::string& description, const std::string& credit,
    unsigned hash, bool include_auto_gen) {
  std::vector<Steps*> steps;
  GetSteps(
      song, steps, st, dc, meter_low, meter_high, description, credit,
      include_auto_gen, hash, 1);  // get max 1
  if (steps.empty()) {
    return nullptr;
  } else {
    return steps[0];
  }
}

Steps* SongUtil::GetStepsByDifficulty(
    const Song* song, StepsType st, Difficulty dc, bool include_auto_gen) {
  const std::vector<Steps*>& vp_steps = (st == StepsType_Invalid)
                                            ? song->GetAllSteps()
                                            : song->GetStepsByStepsType(st);
  for (unsigned i = 0; i < vp_steps.size();
       i++)  // for each of the Song's Steps
  {
    Steps* p_steps = vp_steps[i];

    if (dc != Difficulty_Invalid && dc != p_steps->GetDifficulty()) {
      continue;
    }
    if (!include_auto_gen && p_steps->IsAutogen()) {
      continue;
    }

    return p_steps;
  }

  return nullptr;
}

Steps* SongUtil::GetStepsByDescription(
    const Song* song, StepsType st, std::string description) {
  std::vector<Steps*> notes;
  GetSteps(song, notes, st, Difficulty_Invalid, -1, -1, description, "");
  if (notes.size() == 0) {
    return nullptr;
  } else {
    return notes[0];
  }
}

Steps* SongUtil::GetClosestNotes(
    const Song* song, StepsType st, Difficulty dc, bool ignore_locked) {
  ASSERT(dc != Difficulty_Invalid);

  const std::vector<Steps*>& vp_steps = (st == StepsType_Invalid)
                                            ? song->GetAllSteps()
                                            : song->GetStepsByStepsType(st);
  Steps* closest = nullptr;
  int closest_distance = 999;
  for (unsigned i = 0; i < vp_steps.size();
       i++)  // for each of the Song's Steps
  {
    Steps* p_steps = vp_steps[i];

    if (p_steps->GetDifficulty() == Difficulty_Edit && dc != Difficulty_Edit) {
      continue;
    }
    if (ignore_locked && UNLOCKMAN->StepsIsLocked(song, p_steps)) {
      continue;
    }

    int distance = std::abs(dc - p_steps->GetDifficulty());
    if (distance < closest_distance) {
      closest = p_steps;
      closest_distance = distance;
    }
  }

  return closest;
}

/* Make any duplicate difficulties edits.  (Note that BMS files do a first pass
 * on this; see BMSLoader::SlideDuplicateDifficulties.) */
void SongUtil::AdjustDuplicateSteps(Song* song) {
  FOREACH_ENUM(StepsType, st) {
    FOREACH_ENUM(Difficulty, dc) {
      if (dc == Difficulty_Edit) {
        continue;
      }

      std::vector<Steps*> steps;
      SongUtil::GetSteps(song, steps, st, dc);

      /* Delete steps that are completely identical.  This happened due to a
       * bug in an earlier version. */
      DeleteDuplicateSteps(song, steps);

      const std::string& song_title = song->GetDisplayFullTitle();
      CHECKPOINT_M(
          ssprintf("Duplicate steps from %s removed.", song_title.c_str()));
      StepsUtil::SortNotesArrayByDifficulty(steps);
      CHECKPOINT_M(ssprintf("Charts from %s sorted.", song_title.c_str()));
      for (unsigned k = 1; k < steps.size(); k++) {
        steps[k]->SetDifficulty(Difficulty_Edit);
        if (steps[k]->GetDescription() == "") {
          /* "Hard Edit" */
          std::string edit_name = Capitalize(DifficultyToString(dc)) + " Edit";
          steps[k]->SetDescription(edit_name);
        }
      }
    }

    /* XXX: Don't allow edits to have descriptions that look like regular
     * difficulties. These are confusing, and they're ambiguous when passed to
     * GetStepsByID. */
  }
}
/**
 * @brief Remove the initial whitespace characters.
 * @param s the string to left trim.
 * @return the trimmed string.
 */
static std::string RemoveInitialWhitespace(std::string s) {
  size_t i = s.find_first_not_of(" \t\r\n");
  if (i != s.npos) {
    s.erase(0, i);
  }
  return s;
}

/* This is called within TidyUpData, before autogen notes are added. */
void SongUtil::DeleteDuplicateSteps(Song* song, std::vector<Steps*>& steps) {
  /* vSteps have the same StepsType and Difficulty.  Delete them if they have
   * the same m_sDescription, m_sCredit, m_iMeter and SMNoteData. */
  for (unsigned i = 0; i < steps.size(); i++) {
    const Steps* s1 = steps[i];

    for (unsigned j = i + 1; j < steps.size(); j++) {
      const Steps* s2 = steps[j];

      if (s1->GetDescription() != s2->GetDescription()) {
        continue;
      }
      if (s1->GetCredit() != s2->GetCredit()) {
        continue;
      }
      if (s1->GetMeter() != s2->GetMeter()) {
        continue;
      }
      /* Compare, ignoring whitespace. */
      std::string sm_note_data1;
      s1->GetSMNoteData(sm_note_data1);
      std::string sm_note_data2;
      s2->GetSMNoteData(sm_note_data2);
      if (RemoveInitialWhitespace(sm_note_data1) !=
          RemoveInitialWhitespace(sm_note_data2)) {
        continue;
      }

      LOG->Trace(
          "Removed %p duplicate steps in song \"%s\" with description \"%s\", "
          "step author \"%s\", and meter \"%i\"",
          static_cast<const void*>(s2), song->GetSongDir().c_str(),
          s1->GetDescription().c_str(), s1->GetCredit().c_str(),
          s1->GetMeter());

      song->DeleteSteps(s2, false);

      steps.erase(steps.begin() + j);
      --j;
    }
  }
}

/////////////////////////////////////
// Sorting
/////////////////////////////////////

static LocalizedString SORT_NOT_AVAILABLE("Sort", "NotAvailable");
static LocalizedString SORT_OTHER("Sort", "Other");

/* Just calculating GetNumTimesPlayed within the sort is pretty slow, so let's
 * precompute it.  (This could be generalized with a template.) */
static std::map<const Song*, std::string> map_song_sort_val;

static bool CompareSongPointersBySortValueAscending(
    const Song* song1, const Song* song2) {
  return map_song_sort_val[song1] < map_song_sort_val[song2];
}

static bool CompareSongPointersBySortValueDescending(
    const Song* song1, const Song* song2) {
  return map_song_sort_val[song1] > map_song_sort_val[song2];
}

std::string SongUtil::MakeSortString(std::string s) {
  MakeUpper(s);

  // Make sure that non-alphanumeric strings are placed at the very end.
  if (s.size() > 0) {
    if (s[0] == '.') {  // like the song ".59"
      s.erase(s.begin());
    }
    if ((s[0] < 'A' || s[0] > 'Z') && (s[0] < '0' || s[0] > '9')) {
      s = char(126) + s;
    }
  }

  return s;
}

static bool CompareSongPointersByTitle(const Song* song1, const Song* song2) {
  // Prefer transliterations to full titles
  std::string s1 = song1->GetTranslitMainTitle();
  std::string s2 = song2->GetTranslitMainTitle();
  if (s1 == s2) {
    s1 = song1->GetTranslitSubTitle();
    s2 = song2->GetTranslitSubTitle();
  }

  s1 = SongUtil::MakeSortString(s1);
  s2 = SongUtil::MakeSortString(s2);

  int ret = strcmp(s1.c_str(), s2.c_str());
  if (ret < 0) {
    return true;
  }
  if (ret > 0) {
    return false;
  }

  /* The titles are the same.  Ensure we get a consistent ordering
   * by comparing the unique SongFilePaths. */
  return CompareNoCase(song1->GetSongFilePath(), song2->GetSongFilePath()) < 0;
}

void SongUtil::SortSongPointerArrayByTitle(std::vector<Song*>& songs_in_out) {
  sort(songs_in_out.begin(), songs_in_out.end(), CompareSongPointersByTitle);
}

static bool CompareSongPointersByBPM(const Song* song1, const Song* song2) {
  DisplayBpms bpms1, bpms2;
  song1->GetDisplayBpms(bpms1);
  song2->GetDisplayBpms(bpms2);

  if (bpms1.GetMax() < bpms2.GetMax()) {
    return true;
  }
  if (bpms1.GetMax() > bpms2.GetMax()) {
    return false;
  }

  return CompareRStringsAsc(song1->GetSongFilePath(), song2->GetSongFilePath());
}

void SongUtil::SortSongPointerArrayByBPM(std::vector<Song*>& songs_in_out) {
  sort(songs_in_out.begin(), songs_in_out.end(), CompareSongPointersByBPM);
}

static bool CompareSongPointersByLength(const Song* song1, const Song* song2) {
  float length1, length2;
  length1 = song1->music_length_seconds_;
  length2 = song2->music_length_seconds_;

  if (length1 < length2) {
    return true;
  }
  if (length1 > length2) {
    return false;
  }

  return CompareRStringsAsc(song1->GetSongFilePath(), song2->GetSongFilePath());
}

void SongUtil::SortSongPointerArrayByLength(std::vector<Song*>& songs_in_out) {
  sort(songs_in_out.begin(), songs_in_out.end(), CompareSongPointersByLength);
}

void AppendOctal(int n, int digits, std::string& out) {
  for (int p = digits - 1; p >= 0; --p) {
    const int shift = p * 3;
    int n2 = (n >> shift) & 0x7;
    out.insert(out.end(), (char)(n2 + '0'));
  }
}

static bool CompDescending(
    const std::pair<Song*, std::string>& a,
    const std::pair<Song*, std::string>& b) {
  return a.second > b.second;
}
static bool CompAscending(
    const std::pair<Song*, std::string>& a,
    const std::pair<Song*, std::string>& b) {
  return a.second < b.second;
}

void SongUtil::SortSongPointerArrayByGrades(
    std::vector<Song*>& songs_in_out, bool descending) {
  /* Optimize by pre-writing a string to compare, since doing
   * GetNumNotesWithGrade inside the sort is too slow. */
  typedef std::pair<Song*, std::string> val;
  std::vector<val> vals;
  vals.reserve(songs_in_out.size());

  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    Song* song = songs_in_out[i];

    int counts[NUM_Grade];
    const Profile* profile = PROFILEMAN->GetMachineProfile();
    ASSERT(profile != nullptr);
    profile->GetGrades(
        song,
        GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
            ->steps_type_,
        counts);

    std::string foo;
    foo.reserve(256);
    for (int g = Grade_Tier01; g < NUM_Grade; ++g) {
      AppendOctal(counts[g], 3, foo);
    }
    vals.push_back(val(song, foo));
  }

  sort(vals.begin(), vals.end(), descending ? CompDescending : CompAscending);

  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    songs_in_out[i] = vals[i].first;
  }
}

void SongUtil::SortSongPointerArrayByProfileGrades(
    std::vector<Song*>& songs_in_out, bool descending, PlayerNumber pn) {
  /* Optimize by pre-writing a string to compare, since doing
   * GetNumNotesWithGrade inside the sort is too slow. */
  typedef std::pair<Song*, std::string> val;
  std::vector<val> vals;
  vals.reserve(songs_in_out.size());

  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    Song* song = songs_in_out[i];

    int counts[NUM_Grade];
    const Profile* profile = PROFILEMAN->GetProfile(pn);
    ASSERT(profile != nullptr);
    profile->GetGrades(
        song, GAMESTATE->GetCurrentStyle(pn)->steps_type_, counts);

    std::string foo;
    foo.reserve(256);
    for (int g = Grade_Tier01; g < NUM_Grade; ++g) {
      AppendOctal(counts[g], 3, foo);
    }
    vals.push_back(val(song, foo));
  }

  sort(vals.begin(), vals.end(), descending ? CompDescending : CompAscending);

  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    songs_in_out[i] = vals[i].first;
  }
}

void SongUtil::SortSongPointerArrayByArtist(std::vector<Song*>& songs_in_out) {
  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    map_song_sort_val[songs_in_out[i]] =
        MakeSortString(songs_in_out[i]->GetTranslitArtist());
  }
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueAscending);
}

/* This is for internal use, not display; sorting by Unicode codepoints isn't
 * very interesting for display. */
void SongUtil::SortSongPointerArrayByDisplayArtist(
    std::vector<Song*>& songs_in_out) {
  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    map_song_sort_val[songs_in_out[i]] =
        MakeSortString(songs_in_out[i]->GetDisplayArtist());
  }
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueAscending);
}

static int CompareSongPointersByGenre(const Song* song1, const Song* song2) {
  return song1->genre_ < song2->genre_;
}

void SongUtil::SortSongPointerArrayByGenre(std::vector<Song*>& songs_in_out) {
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(), CompareSongPointersByGenre);
}

void SongUtil::SortSongPointerArrayByGroup(std::vector<Song*>& songs_in_out) {
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(), CompareSongPointersByGroup);
}

int SongUtil::CompareSongPointersByGroup(const Song* song1, const Song* song2) {
  std::string s1 = "";
  std::string s2 = "";

  if (SONGMAN->GetGroup(song1) != nullptr) {
    s1 = SONGMAN->GetGroup(song1)->GetSortTitle();
  } else {
    LOG->Warn(
        "SongUtil::CompareSongPointersByGroup: %s has no group",
        song1->GetSongDir().c_str());
  }

  if (SONGMAN->GetGroup(song2) != nullptr) {
    s2 = SONGMAN->GetGroup(song2)->GetSortTitle();
  } else {
    LOG->Warn(
        "SongUtil::CompareSongPointersByGroup: %s has no group",
        song2->GetSongDir().c_str());
  }

  // Sort Titles are the same, fall back on group folder name
  if (s1 == s2) {
    return song1->group_name_ < song2->group_name_;
  }

  s1 = SongUtil::MakeSortString(s1);
  s2 = SongUtil::MakeSortString(s2);

  int ret = strcmp(s1.c_str(), s2.c_str());
  return ret < 0;
}

static int CompareSongPointersByGroupAndTitle(
    const Song* song1, const Song* song2) {
  std::string s1 = "";
  std::string s2 = "";

  if (SONGMAN->GetGroup(song1) != nullptr) {
    s1 = SONGMAN->GetGroup(song1)->GetSortTitle();
  } else {
    LOG->Warn(
        "SongUtil::CompareSongPointersByGroup: %s has no group",
        song1->GetSongDir().c_str());
  }

  if (SONGMAN->GetGroup(song2) != nullptr) {
    s2 = SONGMAN->GetGroup(song2)->GetSortTitle();
  } else {
    LOG->Warn(
        "SongUtil::CompareSongPointersByGroup: %s has no group",
        song2->GetSongDir().c_str());
  }

  // Sort Titles are the same, fall back on group folder name
  if (s1 == s2) {
    /* Same group; compare by name. */
    if (song1->group_name_ == song2->group_name_) {
      return CompareSongPointersByTitle(song1, song2);
    } else {
      return song1->group_name_ < song2->group_name_;
    }
  }

  s1 = SongUtil::MakeSortString(s1);
  s2 = SongUtil::MakeSortString(s2);

  int ret = strcmp(s1.c_str(), s2.c_str());
  return ret < 0;
}

void SongUtil::SortSongPointerArrayByGroupAndTitle(
    std::vector<Song*>& songs_in_out) {
  sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersByGroupAndTitle);
}

void SongUtil::SortSongPointerArrayByNumPlays(
    std::vector<Song*>& songs_in_out, ProfileSlot slot, bool descending) {
  if (!PROFILEMAN->IsPersistentProfile(slot)) {
    return;  // nothing to do since we don't have data
  }
  Profile* profile = PROFILEMAN->GetProfile(slot);
  SortSongPointerArrayByNumPlays(songs_in_out, profile, descending);
}

void SongUtil::SortSongPointerArrayByNumPlays(
    std::vector<Song*>& songs_in_out, const Profile* profile, bool descending) {
  ASSERT(profile != nullptr);
  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    map_song_sort_val[songs_in_out[i]] =
        ssprintf("%9i", profile->GetSongNumTimesPlayed(songs_in_out[i]));
  }
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      descending ? CompareSongPointersBySortValueDescending
                 : CompareSongPointersBySortValueAscending);
  map_song_sort_val.clear();
}

std::string SongUtil::GetSectionNameFromSongAndSort(
    const Song* song, SortOrder so) {
  if (song == nullptr) {
    return std::string();
  }

  switch (so) {
    case SORT_PREFERRED:
      return SONGMAN->SongToPreferredSortSectionName(song);
    case SORT_SERIES:
    case SORT_GROUP:
      if (SONGMAN->GetGroup(song) == nullptr) {
        LOG->Warn(
            "SongUtil::GetSectionNameFromSongAndSort: %s has no group",
            song->GetSongDir().c_str());
        return std::string();
      } else {
        return SONGMAN->GetGroup(song)->GetGroupName();
      }
    case SORT_TITLE:
    case SORT_ARTIST: {
      std::string s;
      switch (so) {
        case SORT_TITLE:
          s = song->GetTranslitMainTitle();
          break;
        case SORT_ARTIST:
          s = song->GetTranslitArtist();
          break;
        default:
          FAIL_M(ssprintf("Unexpected SortOrder: %i", so));
      }
      s = MakeSortString(s);  // resulting string will be uppercase

      if (s.empty()) {
        return std::string();
      } else if (s[0] >= '0' && s[0] <= '9') {
        return "0-9";
      } else if (s[0] < 'A' || s[0] > 'Z') {
        return SORT_OTHER.GetValue();
      } else {
        return Left(s, 1);
      }
    }
    case SORT_GENRE:
      if (!song->genre_.empty()) {
        return song->genre_;
      }
      return SORT_NOT_AVAILABLE.GetValue();
    case SORT_BPM: {
      if (SHOW_SECTIONS_IN_BPM_SORT) {
        const int bpm_group_size = SORT_BPM_DIVISION;
        DisplayBpms bpms;
        song->GetDisplayBpms(bpms);
        int max_bpm = (int)bpms.GetMax();
        max_bpm += bpm_group_size - (max_bpm % bpm_group_size) - 1;
        return ssprintf("%03d-%03d", max_bpm - (bpm_group_size - 1), max_bpm);
      } else {
        return std::string();
      }
    }
    case SORT_LENGTH: {
      if (SHOW_SECTIONS_IN_LENGTH_SORT) {
        const int sort_length_size = SORT_LENGTH_DIVISION;
        int max_length = (int)song->music_length_seconds_;
        max_length += (sort_length_size - (max_length % sort_length_size) - 1);
        int min_length = max_length - (sort_length_size - 1);
        return ssprintf(
            "%s-%s", SecondsToMMSS(min_length).c_str(),
            SecondsToMMSS(max_length).c_str());
      } else {
        return std::string();
      }
    }
    case SORT_POPULARITY:
    case SORT_POPULARITY_P1:
    case SORT_POPULARITY_P2:
    case SORT_RECENT:
    case SORT_RECENT_P1:
    case SORT_RECENT_P2:
      return THEME->GetString(
          "MusicWheel",
          ssprintf("%s%s", SortOrderToString(so).c_str(), "Text"));
    case SORT_TOP_GRADES_P1: {
      int counts[NUM_Grade];
      PROFILEMAN->GetProfile(PLAYER_1)->GetGrades(
          song, GAMESTATE->GetCurrentStyle(PLAYER_1)->steps_type_, counts);

      for (int i = Grade_Tier01; i < NUM_Grade; ++i) {
        Grade g = (Grade)i;
        if (counts[i] > 0) {
          return ssprintf(
              "%4s x %d", GradeToLocalizedString(g).c_str(), counts[i]);
        }
      }
      return GradeToLocalizedString(Grade_NoData);
    }
    case SORT_TOP_GRADES_P2: {
      int counts[NUM_Grade];
      PROFILEMAN->GetProfile(PLAYER_2)->GetGrades(
          song, GAMESTATE->GetCurrentStyle(PLAYER_2)->steps_type_, counts);

      for (int i = Grade_Tier01; i < NUM_Grade; ++i) {
        Grade g = (Grade)i;
        if (counts[i] > 0) {
          return ssprintf(
              "%4s x %d", GradeToLocalizedString(g).c_str(), counts[i]);
        }
      }
      return GradeToLocalizedString(Grade_NoData);
    }
    case SORT_TOP_GRADES: {
      int counts[NUM_Grade];
      PROFILEMAN->GetMachineProfile()->GetGrades(
          song,
          GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->steps_type_,
          counts);

      for (int i = Grade_Tier01; i < NUM_Grade; ++i) {
        Grade g = (Grade)i;
        if (counts[i] > 0) {
          return ssprintf(
              "%4s x %d", GradeToLocalizedString(g).c_str(), counts[i]);
        }
      }
      return GradeToLocalizedString(Grade_NoData);
    }
    case SORT_BEGINNER_METER:
    case SORT_EASY_METER:
    case SORT_MEDIUM_METER:
    case SORT_HARD_METER:
    case SORT_CHALLENGE_METER:
    case SORT_DOUBLE_EASY_METER:
    case SORT_DOUBLE_MEDIUM_METER:
    case SORT_DOUBLE_HARD_METER:
    case SORT_DOUBLE_CHALLENGE_METER: {
      StepsType st;
      Difficulty dc;
      SongUtil::GetStepsTypeAndDifficultyFromSortOrder(so, st, dc);

      Steps* steps = GetStepsByDifficulty(song, st, dc);
      if (steps && !UNLOCKMAN->StepsIsLocked(song, steps)) {
        return ssprintf("%02d", steps->GetMeter());
      }
      return SORT_NOT_AVAILABLE.GetValue();
    }
    case SORT_METER:
      return std::string();
    case SORT_MODE_MENU:
      return std::string();
    case SORT_ALL_COURSES:
    case SORT_NONSTOP_COURSES:
    case SORT_ONI_COURSES:
    case SORT_ENDLESS_COURSES:
    default:
      FAIL_M(ssprintf("Invalid SortOrder: %i", so));
  }
}

void SongUtil::SortSongPointerArrayBySectionName(
    std::vector<Song*>& songs_in_out, SortOrder so) {
  std::string other = SORT_OTHER.GetValue();
  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    std::string val = GetSectionNameFromSongAndSort(songs_in_out[i], so);

    // Make sure 0-9 comes first and OTHER comes last.
    if (val == "0-9") {
      val = "0";
    } else if (val == other) {
      val = "2";
    } else {
      val = "1" + MakeSortString(val);
    }

    map_song_sort_val[songs_in_out[i]] = val;
  }

  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueAscending);
  map_song_sort_val.clear();
}

void SongUtil::SortSongPointerArrayByParentSectionName(
    std::vector<Song*>& songs_in_out) {
  // Sort songs by (series, group). Songs whose group has no series sort
  // alongside series
  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    std::string val;
    Group* group = SONGMAN->GetGroup(songs_in_out[i]);
    if (group == nullptr) {
      LOG->Warn(
          "SongUtil::SortSongPointerArrayByParentSectionName: %s has no group",
          songs_in_out[i]->GetSongDir().c_str());
      val = "2";
    } else {
      const std::string& series_name = group->GetSeries();
      const std::string group_key = MakeSortString(group->GetSortTitle());
      if (series_name.empty()) {
        val = "1" + group_key;
      } else {
        val = "1" + MakeSortString(series_name) + '\x01' + group_key;
      }
    }
    map_song_sort_val[songs_in_out[i]] = val;
  }

  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueAscending);
  map_song_sort_val.clear();
}

void SongUtil::SortSongPointerArrayByStepsTypeAndMeter(
    std::vector<Song*>& songs_in_out, StepsType st, Difficulty dc) {
  map_song_sort_val.clear();
  for (unsigned i = 0; i < songs_in_out.size(); ++i) {
    // Ignore locked steps.
    const Steps* steps = GetClosestNotes(songs_in_out[i], st, dc, true);
    std::string& s = map_song_sort_val[songs_in_out[i]];
    s = ssprintf("%03d", steps ? steps->GetMeter() : 0);

    /* pSteps may not be exactly the difficulty we want; for example, we
     * may be sorting by Hard difficulty and a song may have no Hard steps.
     * In this case, we can end up with unintuitive ties; for example, pSteps
     * may be Medium with a meter of 5, which will sort it among the 5-meter
     * Hard songs.  Break the tie, by adding the difficulty to the sort as
     * well. That way, we'll always put Medium 5s before Hard 5s. If all
     * songs are using the preferred difficulty (dc), this will be a no-op. */
    s += ssprintf("%c", (steps ? steps->GetDifficulty() : 0) + '0');

    if (PREFSMAN->sub_sort_by_num_steps_) {
      s += ssprintf(
          "%06.0f",
          steps ? steps->GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds]
                : 0);
    }
  }
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueAscending);
}

void SongUtil::SortByMostRecentlyPlayedForMachine(
    std::vector<Song*>& songs_in_out) {
  Profile* profile = PROFILEMAN->GetMachineProfile();

  for (const Song* s : songs_in_out) {
    int num_times_played = profile->GetSongNumTimesPlayed(s);
    std::string val = num_times_played
                          ? profile->GetSongLastPlayedDateTime(s).GetString()
                          : std::string("0");
    map_song_sort_val[s] = val;
  }

  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueDescending);
  map_song_sort_val.clear();
}

void SongUtil::SortByMostRecentlyPlayedForProfile(
    std::vector<Song*>& songs_in_out, PlayerNumber pn) {
  Profile* profile = PROFILEMAN->GetProfile(pn);
  for (const Song* s : songs_in_out) {
    int num_times_played = profile->GetSongNumTimesPlayed(s);
    std::string val = num_times_played
                          ? profile->GetSongLastPlayedDateTime(s).GetString()
                          : std::string("0");
    map_song_sort_val[s] = val;
  }
  stable_sort(
      songs_in_out.begin(), songs_in_out.end(),
      CompareSongPointersBySortValueDescending);
  map_song_sort_val.clear();
}

bool SongUtil::IsEditDescriptionUnique(
    const Song* song, StepsType st, const std::string& preferred_description,
    const Steps* exclude) {
  for (const Steps* steps : song->GetAllSteps()) {
    if (steps->GetDifficulty() != Difficulty_Edit) {
      continue;
    }
    if (steps->steps_type_ != st) {
      continue;
    }
    if (steps == exclude) {
      continue;
    }
    if (steps->GetDescription() == preferred_description) {
      return false;
    }
  }
  return true;
}

bool SongUtil::IsChartNameUnique(
    const Song* song, StepsType st, const std::string& name,
    const Steps* exclude) {
  for (const Steps* steps : song->GetAllSteps()) {
    if (steps->steps_type_ != st) {
      continue;
    }
    if (steps == exclude) {
      continue;
    }
    if (steps->GetChartName() == name) {
      return false;
    }
  }
  return true;
}

std::string SongUtil::MakeUniqueEditDescription(
    const Song* song, StepsType st, const std::string& preferred_description) {
  if (IsEditDescriptionUnique(song, st, preferred_description, nullptr)) {
    return preferred_description;
  }

  std::string temp;

  for (int i = 0; i < 1000; i++) {
    // make name "My Edit" -> "My Edit2"
    std::string num = ssprintf("%d", i + 1);
    temp =
        Left(preferred_description, kMaxStepsDescriptionLength - num.size()) +
        num;

    if (IsEditDescriptionUnique(song, st, temp, nullptr)) {
      return temp;
    }
  }

  // Edit limit guards should prevent this
  FAIL_M("Exceeded limit of 1000 edits per song");
}

static LocalizedString YOU_MUST_SUPPLY_NAME(
    "SongUtil", "You must supply a name for your new edit.");
static LocalizedString CHART_NAME_CONFLICTS(
    "SongUtil",
    "The name you chose conflicts with another chart. Please use a different "
    "name.");
static LocalizedString EDIT_NAME_CONFLICTS(
    "SongUtil",
    "The name you chose conflicts with another edit. Please use a different "
    "name.");
static LocalizedString CHART_NAME_CANNOT_CONTAIN(
    "SongUtil",
    "The chart name cannot contain any of the following characters: %s");
static LocalizedString EDIT_NAME_CANNOT_CONTAIN(
    "SongUtil",
    "The edit name cannot contain any of the following characters: %s");

bool SongUtil::ValidateCurrentEditStepsDescription(
    const std::string& answer, std::string& error_out) {
  Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];
  Song* song = steps->song_;

  ASSERT(steps->IsAnEdit());

  if (answer.empty()) {
    error_out = YOU_MUST_SUPPLY_NAME;
    return false;
  }

  static const std::string kInvalidChars = "\\/:*?\"<>|";
  if (strpbrk(answer.c_str(), kInvalidChars.c_str()) != nullptr) {
    error_out = ssprintf(
        EDIT_NAME_CANNOT_CONTAIN.GetValue().c_str(), kInvalidChars.c_str());
    return false;
  }

  // Steps name must be unique for this song.
  std::vector<Steps*> v;
  GetSteps(song, v, StepsType_Invalid, Difficulty_Edit);
  for (const Steps* s : v) {
    if (steps == s) {
      continue;  // don't compare name against ourself
    }

    if (s->GetDescription() == answer) {
      error_out = EDIT_NAME_CONFLICTS;
      return false;
    }
  }

  return true;
}

bool SongUtil::ValidateCurrentStepsDescription(
    const std::string& answer, std::string& error_out) {
  if (answer.empty()) {
    return true;
  }

  /* Don't allow duplicate edit names within the same StepsType; edit names
   * uniquely identify the edit. */
  Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];

  // If unchanged:
  if (steps->GetDescription() == answer) {
    return true;
  }

  if (steps->IsAnEdit()) {
    return SongUtil::ValidateCurrentEditStepsDescription(answer, error_out);
  }

  return true;
}

bool SongUtil::ValidateCurrentStepsChartName(
    const std::string& answer, std::string& error) {
  if (answer.empty()) {
    return true;
  }

  static const std::string kInvalidChars = "\\/:*?\"<>|";
  if (strpbrk(answer.c_str(), kInvalidChars.c_str()) != nullptr) {
    error = ssprintf(
        CHART_NAME_CANNOT_CONTAIN.GetValue().c_str(), kInvalidChars.c_str());
    return false;
  }

  /* Don't allow duplicate title names within the same StepsType.
   * We need some way of identifying the unique charts. */
  Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];

  if (steps->GetChartName() == answer) {
    return true;
  }

  // TODO next commit: borrow code from EditStepsDescription.
  bool result = SongUtil::IsChartNameUnique(
      GAMESTATE->cur_song_, steps->steps_type_, answer, steps);
  if (!result) {
    error = CHART_NAME_CONFLICTS;
  }
  return result;
}

static LocalizedString AUTHOR_NAME_CANNOT_CONTAIN(
    "SongUtil",
    "The step author's name cannot contain any of the following characters: "
    "%s");

bool SongUtil::ValidateCurrentStepsCredit(
    const std::string& answer, std::string& error_out) {
  if (answer.empty()) {
    return true;
  }

  Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];
  // If unchanged:
  if (steps->GetCredit() == answer) {
    return true;
  }

  // Borrow from EditDescription testing. Perhaps this should be abstracted?
  // -Wolfman2000
  static const std::string kInvalidChars = "\\/:*?\"<>|";
  if (strpbrk(answer.c_str(), kInvalidChars.c_str()) != nullptr) {
    error_out = ssprintf(
        AUTHOR_NAME_CANNOT_CONTAIN.GetValue().c_str(), kInvalidChars.c_str());
    return false;
  }

  return true;
}

static LocalizedString PREVIEW_DOES_NOT_EXIST(
    "SongUtil", "The preview file '%s' does not exist.");
bool SongUtil::ValidateCurrentSongPreview(
    const std::string& answer, std::string& error) {
  if (answer.empty()) {
    return true;
  }
  Song* song = GAMESTATE->cur_song_;
  std::string real_file = song->preview_file_;
  song->preview_file_ = answer;
  std::string path = song->GetPreviewMusicPath();
  bool valid = DoesFileExist(path);
  song->preview_file_ = real_file;
  if (!valid) {
    error = ssprintf(PREVIEW_DOES_NOT_EXIST.GetValue().c_str(), answer.c_str());
  }
  return valid;
}

static LocalizedString MUSIC_DOES_NOT_EXIST(
    "SongUtil", "The music file '%s' does not exist.");
bool SongUtil::ValidateCurrentStepsMusic(
    const std::string& answer, std::string& error) {
  if (answer.empty()) {
    return true;
  }
  Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];
  std::string real_file = steps->GetMusicFile();
  steps->SetMusicFile(answer);
  std::string path = steps->GetMusicPath();
  bool valid = DoesFileExist(path);
  steps->SetMusicFile(real_file);
  if (!valid) {
    error = ssprintf(MUSIC_DOES_NOT_EXIST.GetValue().c_str(), answer.c_str());
  }
  return valid;
}

void SongUtil::FilterSongs(
    const SongCriteria& sc, const std::vector<Song*>& in,
    std::vector<Song*>& out, bool do_care_about_game) {
  out.reserve(in.size());
  for (Song* s : in) {
    if (sc.Matches(s) && (!do_care_about_game || IsSongPlayable(s))) {
      out.push_back(s);
    }
  }
}

void SongUtil::GetPlayableStepsTypes(
    const Song* song, std::set<StepsType>& out) {
  std::vector<const Style*> possible_styles;
  // If AutoSetStyle, or a Style hasn't been chosen, check StepsTypes for all
  // Styles.
  if (CommonMetrics::AUTO_SET_STYLE ||
      GAMESTATE->GetCurrentStyle(kPlayerInvalid) == nullptr) {
    GAMEMAN->GetCompatibleStyles(
        GAMESTATE->cur_game_, GAMESTATE->GetNumPlayersEnabled(),
        possible_styles);
  } else {
    possible_styles.push_back(GAMESTATE->GetCurrentStyle(kPlayerInvalid));
  }

  std::set<StepsType> steps_types;
  for (const Style* s : possible_styles) {
    steps_types.insert(s->steps_type_);
  }

  /* filter out hidden StepsTypes, and remove steps that we don't have enough
   * stages left to play. */
  // this being const may have caused some problems... -aj
  const std::vector<StepsType>& to_show =
      CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue();
  for (const StepsType& type : steps_types) {
    bool show_this_steps_type =
        find(to_show.begin(), to_show.end(), type) != to_show.end();

    int num_players = GAMESTATE->GetNumPlayersEnabled();
    num_players = std::max(num_players, 1);

    bool enough_stages =
        GAMESTATE->IsAnExtraStage() ||
        GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer() >=
            GAMESTATE->GetNumStagesMultiplierForSong(song);

    if (show_this_steps_type && enough_stages) {
      out.insert(type);
    }
  }
}

void SongUtil::GetPlayableSteps(const Song* song, std::vector<Steps*>& out) {
  std::set<StepsType> steps_type;
  GetPlayableStepsTypes(song, steps_type);

  for (const StepsType& type : steps_type) {
    SongUtil::GetSteps(song, out, type);
  }

  StepsUtil::RemoveLockedSteps(song, out);
  StepsUtil::SortNotesArrayByDifficulty(out);
  StepsUtil::SortStepsByTypeAndDifficulty(out);
}

bool SongUtil::IsStepsTypePlayable(Song* song, StepsType st) {
  std::set<StepsType> steps_type;
  GetPlayableStepsTypes(song, steps_type);
  return steps_type.find(st) != steps_type.end();
}

bool SongUtil::IsStepsPlayable(Song* song, Steps* p_steps) {
  std::vector<Steps*> vp_steps;
  GetPlayableSteps(song, vp_steps);
  return find(vp_steps.begin(), vp_steps.end(), p_steps) != vp_steps.end();
}

bool SongUtil::IsSongPlayable(Song* s) {
  const std::vector<Steps*>& steps = s->GetAllSteps();
  // I'm sure there is a foreach loop, but I don't
  return std::any_of(steps.begin(), steps.end(), [&](Steps* step) {
    return IsStepsPlayable(s, step);
  });
}

bool SongUtil::GetStepsTypeAndDifficultyFromSortOrder(
    SortOrder so, StepsType& st_out, Difficulty& dc_out) {
  switch (so) {
    case SORT_BEGINNER_METER:
      dc_out = Difficulty_Beginner;
      break;
    case SORT_EASY_METER:
      dc_out = Difficulty_Easy;
      break;
    case SORT_MEDIUM_METER:
      dc_out = Difficulty_Medium;
      break;
    case SORT_HARD_METER:
      dc_out = Difficulty_Hard;
      break;
    case SORT_CHALLENGE_METER:
      dc_out = Difficulty_Challenge;
      break;
    case SORT_DOUBLE_EASY_METER:
      dc_out = Difficulty_Easy;
      break;
    case SORT_DOUBLE_MEDIUM_METER:
      dc_out = Difficulty_Medium;
      break;
    case SORT_DOUBLE_HARD_METER:
      dc_out = Difficulty_Hard;
      break;
    case SORT_DOUBLE_CHALLENGE_METER:
      dc_out = Difficulty_Challenge;
      break;
    default:
      return false;
  }

  switch (so) {
    DEFAULT_FAIL(so);
    case SORT_BEGINNER_METER:
    case SORT_EASY_METER:
    case SORT_MEDIUM_METER:
    case SORT_HARD_METER:
    case SORT_CHALLENGE_METER:
      st_out = GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                   ->steps_type_;
      break;
    case SORT_DOUBLE_EASY_METER:
    case SORT_DOUBLE_MEDIUM_METER:
    case SORT_DOUBLE_HARD_METER:
    case SORT_DOUBLE_CHALLENGE_METER:
      st_out = GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                   ->steps_type_;  // in case we don't find any matches below
      std::vector<const Style*> styles;
      GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, styles);
      for (const Style* i : styles) {
        if (i->style_type_ == StyleType_OnePlayerTwoSides) {
          // Ugly hack to ignore pump's half-double.
          bool contains_half =
              ((std::string)i->name_).find("half") != std::string::npos;
          if (contains_half) {
            continue;
          }
          st_out = i->steps_type_;
          break;
        }
      }
  }

  return true;
}

//////////////////////////////////
// SongID
//////////////////////////////////

void SongID::FromSong(const Song* p) {
  if (p) {
    dir_ = p->GetSongDir();
  } else {
    dir_ = "";
  }

  // HACK for backwards compatibility:
  // Strip off leading "/".  2005/05/21 file layer changes added a leading
  // slash.
  if (Left(dir_, 1) == "/") {
    dir_.erase(dir_.begin());
  }
}

Song* SongID::ToSong() const {
  Song* ret = nullptr;
  if (!dir_.empty()) {
    // HACK for backwards compatibility: Re-add the leading "/".
    // 2005/05/21 file layer changes added a leading slash.
    std::string dir2 = dir_;
    if (Left(dir2, 1) != "/") {
      dir2 = "/" + dir2;
    }
    ret = SONGMAN->GetSongFromDir(dir2);
  }
  return ret;
}

XNode* SongID::CreateNode() const {
  XNode* node = new XNode("Song");
  node->AppendAttr("Dir", dir_);
  return node;
}

void SongID::LoadFromNode(const XNode* node) {
  ASSERT(node->GetName() == "Song");
  node->GetAttrValue("Dir", dir_);

  // HACK for backwards compatibility: /AdditionalSongs has been merged into
  // /Songs
  if (Left(dir_, 16) == "AdditionalSongs/") {
    dir_.replace(0, 16, "Songs/");
  }
}

std::string SongID::ToString() const { return dir_; }

bool SongID::IsValid() const { return ToSong() != nullptr; }

// lua start
#include "LuaBinding.h"

namespace {
int GetPlayableSteps(lua_State* L) {
  const Song* song = Luna<Song>::check(L, 1, true);
  std::vector<Steps*> steps;
  SongUtil::GetPlayableSteps(song, steps);
  LuaHelpers::CreateTableFromArray<Steps*>(steps, L);
  return 1;
}
int IsStepsTypePlayable(lua_State* L) {
  Song* song = Luna<Song>::check(L, 1, true);
  StepsType st = Enum::Check<StepsType>(L, 2);
  bool b = SongUtil::IsStepsTypePlayable(song, st);
  LuaHelpers::Push(L, b);
  return 1;
}
int IsStepsPlayable(lua_State* L) {
  Song* song = Luna<Song>::check(L, 1, true);
  Steps* steps = Luna<Steps>::check(L, 2, true);
  bool b = SongUtil::IsStepsPlayable(song, steps);
  LuaHelpers::Push(L, b);
  return 1;
}

const luaL_Reg SongUtilTable[] = {
    LIST_METHOD(GetPlayableSteps),
    LIST_METHOD(IsStepsTypePlayable),
    LIST_METHOD(IsStepsPlayable),
    {nullptr, nullptr}};
}  // namespace

LUA_REGISTER_NAMESPACE(SongUtil)

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
