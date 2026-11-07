/** @brief SongUtil - Utility functions that deal with Song. */

#ifndef SONG_UTIL_H
#define SONG_UTIL_H
#include <set>
#include <string>
#include <vector>

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"

class Song;
class Steps;
class Profile;
class XNode;

void AppendOctal(int n, int digits, std::string& out);

/** @brief The criteria for dealing with songs. */
class SongCriteria {
 public:
  /**
   * @brief What group name are we searching for for Songs?
   *
   * If an empty string, don't bother using this for searching. */
  std::vector<std::string> group_names_;
  std::vector<std::string> song_names_;
  std::vector<std::string> artist_names_;

  bool use_song_genre_allowed_list_;
  std::vector<std::string> song_genre_allowed_list_;
  enum Selectable {
    Selectable_Yes,
    Selectable_No,
    Selectable_DontCare
  } selectable_;
  bool use_song_allowed_list_;
  std::vector<Song*> song_allowed_list_;
  /** @brief How many songs does this take max? Don't use this if it's -1. */
  int max_stages_for_song_;     // don't filter if -1
  float min_bpm_;               // don't filter if -1
  float max_bpm_;               // don't filter if -1
  float min_duration_seconds_;  // don't filter if -1
  float max_duration_seconds_;  // don't filter if -1

  /** @brief Is this song used for tutorial purposes? */
  enum Tutorial {
    Tutorial_Yes,     /**< This song is used for tutorial purposes. */
    Tutorial_No,      /**< This song is not used for tutorial purposes. */
    Tutorial_DontCare /**< This song can or cannot be used for tutorial
                         purposes. */
  } tutorial_;
  /** @brief Is this song used for locking/unlocking purposes? */
  enum Locked {
    Locked_Locked,   /**< This song is a locked song. */
    Locked_Unlocked, /**< This song is an unlocked song. */
    Locked_DontCare  /**< This song can or cannot be locked or unlocked. */
  } locked_;

  /** @brief Set up some initial song criteria. */
  SongCriteria()
      : group_names_(),
        song_names_(),
        artist_names_(),
        use_song_genre_allowed_list_(false),
        song_genre_allowed_list_(),
        selectable_(Selectable_DontCare),
        use_song_allowed_list_(false),
        song_allowed_list_(),
        max_stages_for_song_(-1),
        min_bpm_(-1),
        max_bpm_(-1),
        min_duration_seconds_(-1),
        max_duration_seconds_(-1),
        tutorial_(Tutorial_DontCare),
        locked_(Locked_DontCare) {
    // m_fMinBPM = -1;
    // m_fMaxBPM = -1;
  }

  /**
   * @brief Determine if the song matches the current criteria.
   * @param p the song to compare against the criteria.
   * @return true of the song matches the criteria, false otherwise.
   */
  bool Matches(const Song* p) const;
  /**
   * @brief Determine if two sets of criteria are equivalent.
   * @param other the other criteria.
   * @return true if the two sets of criteria are equal, false otherwise.
   */
  bool operator==(const SongCriteria& other) const {
/** @brief A quick way to match every part of the song criterium. */
#define X(x) (x == other.x)
    return X(group_names_) && X(song_names_) && X(artist_names_) &&
           X(use_song_genre_allowed_list_) && X(song_genre_allowed_list_) &&
           X(selectable_) && X(use_song_allowed_list_) &&
           X(song_allowed_list_) && X(max_stages_for_song_) && X(min_bpm_) &&
           X(max_bpm_) && X(min_duration_seconds_) &&
           X(max_duration_seconds_) && X(tutorial_) && X(locked_);
#undef X
  }
  /**
   * @brief Determine if two sets of criteria are not equivalent.
   * @param other the other criteria.
   * @return true if the two sets of criteria are not equal, false otherwise.
   */
  bool operator!=(const SongCriteria& other) const {
    return !operator==(other);
  }
};

/** @brief A set of song utilities to make working with songs easier. */
namespace SongUtil {
void GetSteps(
    const Song* song, std::vector<Steps*>& array_add_to,
    StepsType st = StepsType_Invalid, Difficulty dc = Difficulty_Invalid,
    int meter_low = -1, int meter_high = -1,
    const std::string& description = "", const std::string& credit = "",
    bool include_auto_gen = true, unsigned hash = 0, int max_to_get = -1);
Steps* GetOneSteps(
    const Song* song, StepsType st = StepsType_Invalid,
    Difficulty dc = Difficulty_Invalid, int meter_low = -1, int meter_high = -1,
    const std::string& description = "", const std::string& credit = "",
    unsigned hash = 0, bool include_auto_gen = true);
Steps* GetStepsByDifficulty(
    const Song* song, StepsType st, Difficulty dc,
    bool include_auto_gen = true);
Steps* GetStepsByDescription(
    const Song* song, StepsType st, std::string description);
Steps* GetClosestNotes(
    const Song* song, StepsType st, Difficulty dc, bool ignore_locked = false);

void AdjustDuplicateSteps(Song* song);  // part of TidyUpData
void DeleteDuplicateSteps(Song* song, std::vector<Steps*>& steps);

std::string MakeSortString(std::string s);
void SortSongPointerArrayByTitle(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByBPM(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByGrades(
    std::vector<Song*>& songs_in_out, bool descending);
void SortSongPointerArrayByProfileGrades(
    std::vector<Song*>& songs_in_out, bool descending, PlayerNumber pn);
void SortSongPointerArrayByArtist(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByDisplayArtist(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByGenre(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByGroupAndTitle(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByNumPlays(
    std::vector<Song*>& songs_in_out, ProfileSlot slot, bool descending);
void SortSongPointerArrayByNumPlays(
    std::vector<Song*>& songs_in_out, const Profile* profile, bool descending);
void SortSongPointerArrayByStepsTypeAndMeter(
    std::vector<Song*>& songs_in_out, StepsType st, Difficulty dc);
std::string GetSectionNameFromSongAndSort(const Song* song, SortOrder so);
void SortSongPointerArrayBySectionName(
    std::vector<Song*>& songs_in_out, SortOrder so);
void SortSongPointerArrayByParentSectionName(std::vector<Song*>& songs_in_out);
void SortByMostRecentlyPlayedForMachine(std::vector<Song*>& songs_in_out);
void SortByMostRecentlyPlayedForProfile(
    std::vector<Song*>& songs_in_out, PlayerNumber pn);
void SortSongPointerArrayByLength(std::vector<Song*>& songs_in_out);
void SortSongPointerArrayByGroup(std::vector<Song*>& songs_in_out);

int CompareSongPointersByGroup(const Song* song1, const Song* song2);

/**
 * @brief Determine if the requested description for an edit is unique.
 * @param pSong the song the edit is for.
 * @param st the steps type for the edit.
 * @param sPreferredDescription the requested description.
 * @param pExclude the steps that want the description.
 * @return true if it is unique, false otherwise.
 */
bool IsEditDescriptionUnique(
    const Song* song, StepsType st, const std::string& preferred_description,
    const Steps* exclude);
bool IsChartNameUnique(
    const Song* song, StepsType st, const std::string& name,
    const Steps* exclude);
std::string MakeUniqueEditDescription(
    const Song* song, StepsType st, const std::string& preferred_description);
bool ValidateCurrentEditStepsDescription(
    const std::string& answer, std::string& error_out);
bool ValidateCurrentStepsDescription(
    const std::string& answer, std::string& error_out);
bool ValidateCurrentStepsCredit(
    const std::string& answer, std::string& error_out);
bool ValidateCurrentStepsChartName(
    const std::string& answer, std::string& error);
bool ValidateCurrentSongPreview(const std::string& answer, std::string& error);
bool ValidateCurrentStepsMusic(const std::string& answer, std::string& error);

/**
 * @brief Filter the selection of songs to only match certain criteria.
 * @param sc the intended song criteria.
 * @param in the starting batch of songs.
 * @param out the resulting batch.
 * @param doCareAboutGame a flag to see if we should only get playable steps. */
void FilterSongs(
    const SongCriteria& sc, const std::vector<Song*>& in,
    std::vector<Song*>& out, bool do_care_about_game = false);

void GetPlayableStepsTypes(const Song* song, std::set<StepsType>& out);
void GetPlayableSteps(const Song* song, std::vector<Steps*>& out);
bool IsStepsTypePlayable(Song* song, StepsType st);
bool IsStepsPlayable(Song* song, Steps* steps);

/**
 * @brief Determine if the song has any playable steps in the present game.
 * @param s the current song.
 * @return true if the song has playable steps, false otherwise. */
bool IsSongPlayable(Song* s);

bool GetStepsTypeAndDifficultyFromSortOrder(
    SortOrder so, StepsType& st, Difficulty& dc);
}  // namespace SongUtil

class SongID {
  std::string dir_;

 public:
  /**
   * @brief Set up the SongID with default values.
   *
   * This used to call Unset() to do the same thing. */
  SongID() : dir_("") {}
  void Unset() { FromSong(nullptr); }
  void FromSong(const Song* p);
  Song* ToSong() const;
  bool operator<(const SongID& other) const { return dir_ < other.dir_; }
  bool operator==(const SongID& other) const { return dir_ == other.dir_; }

  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);
  void FromString(std::string dir) { dir_ = dir; }
  std::string ToString() const;
  bool IsValid() const;
};

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
 * @section LICENSE
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
