#ifndef HIGH_SCORE_H
#define HIGH_SCORE_H

#include <vector>

#include "DateTime.h"
#include "GameConstantsAndTypes.h"
#include "Grade.h"
#include "RadarValues.h"
#include "RageUtil_AutoPtr.h"
#include "XmlFile.h"

struct lua_State;

// TODO(teejusb): This is defined in the cpp file.
struct HighScoreImpl;

// The high score that is earned by a player.
// This is scoring data that is persisted between sessions.
struct HighScore {
  HighScore();

  // Retrieve the name of the player that set the high score.
  RString GetName() const;
  // Retrieve the grade earned from this score.
  Grade GetGrade() const;
  // Retrieve the score earned.
  unsigned int GetScore() const;
  // Determine if any judgments were tallied during this run.
  bool IsEmpty() const;
  float GetPercentDP() const;
  // Determine how many seconds the player had left in Survival mode.
  float GetSurviveSeconds() const;
  float GetSurvivalSeconds() const;
  unsigned int GetMaxCombo() const;
  StageAward GetStageAward() const;
  PeakComboAward GetPeakComboAward() const;
  // Get the modifiers used for this run.
  RString GetModifiers() const;
  DateTime GetDateTime() const;
  RString GetPlayerGuid() const;
  RString GetMachineGuid() const;
  int GetProductID() const;
  int GetTapNoteScore(TapNoteScore tns) const;
  int GetHoldNoteScore(HoldNoteScore tns) const;
  const RadarValues& GetRadarValues() const;
  float GetLifeRemainingSeconds() const;
  // Determine if this score was from a situation that would cause
	// disqualification.
  bool GetDisqualified() const;

  // Set the name of the Player that earned the score.
  void SetName(const RString& name);
  void SetGrade(Grade grade);
  void SetScore(unsigned int score);
  void SetPercentDP(float percent_dp);
  void SetAliveSeconds(float alive_seconds);
  void SetMaxCombo(unsigned int max_combo);
  void SetStageAward(StageAward stage_award);
  void SetPeakComboAward(PeakComboAward peak_combo_award);
  void SetModifiers(RString modifiers);
  void SetDateTime(DateTime date_time);
  void SetPlayerGuid(RString player_guid);
  void SetMachineGuid(RString maching_guid);
  void SetProductID(int product_id);
  void SetTapNoteScore(TapNoteScore tns, int i);
  void SetHoldNoteScore(HoldNoteScore tns, int i);
  void SetRadarValues(const RadarValues& radar_values);
  void SetLifeRemainingSeconds(float life_remaining_seconds);
  void SetDisqualified(bool disqualified);

  RString* GetNameMutable();
  const RString* GetNameMutable() const {
    return const_cast<RString*>(const_cast<HighScore*>(this)->GetNameMutable());
  }

  void Unset();

  bool operator<(const HighScore& other) const;
  bool operator>(const HighScore& other) const;
  bool operator<=(const HighScore& other) const;
  bool operator>=(const HighScore& other) const;
  bool operator==(const HighScore& other) const;
  bool operator!=(const HighScore& other) const;

  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);

  RString GetDisplayName() const;

  // Lua
  void PushSelf(lua_State* L);

 private:
  HiddenPtr<HighScoreImpl> high_score_impl_;
};

// The list of high scores
struct HighScoreList {
 public:
  // Set up the HighScore List with default values.
  // This used to call Init(), but it's better to be explicit here.
  HighScoreList()
      : high_scores_(),
        high_grade_(Grade_NoData),
        num_times_played_(0),
        last_played_() {}

  void Init();

  int GetNumTimesPlayed() const { return num_times_played_; }
  DateTime GetLastPlayed() const {
		// Don't call this unless the song has been played.
    ASSERT(num_times_played_ > 0);
    return last_played_;
  }
  const HighScore& GetTopScore() const;

  void AddHighScore(HighScore high_score, int& index_out, bool is_machine);
  void IncrementPlayCount(DateTime last_played_);
  void RemoveAllButOneOfEachName();
  void ClampSize(bool is_machine);

  void MergeFromOtherHSL(HighScoreList& other, bool is_machine);

  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);

  std::vector<HighScore> high_scores_;
  Grade high_grade_;

  // Lua
  void PushSelf(lua_State* L);

 private:
  int num_times_played_;
	// Meaningless if num_times_played_ == 0.
  DateTime last_played_;
};

// the picture taken of the high score.
struct Screenshot {
  // the filename of the screen shot. There is no directory part.
  RString file_name;
  // The MD5 hash of the screen shot file above.
  RString md5;
  // The actual high score in question.
  HighScore high_score;

  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);
  bool operator<(const Screenshot& rhs) const {
    return high_score.GetDateTime() < rhs.high_score.GetDateTime();
  }

  bool operator==(const Screenshot& rhs) const {
    return file_name == rhs.file_name;
  }
};

#endif  // HIGH_SCORE_H

/**
 * @file
 * @author Chris Danford (c) 2004
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
