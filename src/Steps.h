#ifndef STEPS_H
#define STEPS_H

#include <string>
#include <vector>

#include "Attack.h"
#include "ColumnCues.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "NoteAnnotation.h"
#include "PlayerNumber.h"
#include "RadarValues.h"
#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "StreamBreakdown.h"
#include "TechCounts.h"
#include "TimingData.h"

class Profile;
class NoteData;
struct lua_State;

/**
 * @brief Enforce a limit on the number of chars for the description.
 *
 * In In The Groove, this limit was 12: we do not need such a limit now.
 */
const int MAX_STEPS_DESCRIPTION_LENGTH = 255;

/**
 * @brief Current version of GrooveStats hash.
 * Increment this to invalidate previously cached values.
 * If this is incremented, FILE_CACHE_VERSION must also be incremented so that
 * existing cache files are regenerated.
 */
const int CURRENT_GROOVE_STATS_HASH_VERSION = 4;

/** @brief The different ways of displaying the BPM. */
enum DisplayBPM {
  DISPLAY_BPM_ACTUAL,    /**< Display the song's actual BPM. */
  DISPLAY_BPM_SPECIFIED, /**< Display a specified value or values. */
  DISPLAY_BPM_RANDOM,    /**< Display a random selection of BPMs. */
  NUM_DisplayBPM,
  DisplayBPM_Invalid
};
const std::string& DisplayBPMToString(DisplayBPM dbpm);
LuaDeclareType(DisplayBPM);

/**
 * @brief Holds note information for a Song.
 *
 * A Song may have one or more Notes. */
class Steps {
 public:
  /** @brief Set up the Steps with initial values. */
  Steps(Song* song);
  /** @brief Destroy the Steps that are no longer needed. */
  ~Steps();

  // initializers
  void AutogenFrom(const Steps* parent, StepsType to);
  void CopyFrom(Steps* source, StepsType to, float music_length_seconds);
  void CreateBlank(StepsType to);

  void Compress() const;
  void Decompress() const;
  void Decompress();
  /**
   * @brief Determine if these steps were created by the autogenerator.
   * @return true if they were, false otherwise.
   */
  bool IsAutogen() const { return parent_ != nullptr; }

  /**
   * @brief Determine if this set of Steps is an edit.
   *
   * Edits have a special value of difficulty to make it easy to determine.
   * @return true if this is an edit, false otherwise.
   */
  bool IsAnEdit() const { return difficulty_ == Difficulty_Edit; }
  /**
   * @brief Determine if this set of Steps is a player edit.
   *
   * Player edits also have to be loaded from a player's profile slot, not the
   * machine.
   * @return true if this is a player edit, false otherwise. */
  bool IsAPlayerEdit() const {
    return IsAnEdit() && GetLoadedFromProfileSlot() < ProfileSlot_Machine;
  }
  /**
   * @brief Determine if these steps were loaded from a player's profile.
   * @return true if they were from a player profile, false otherwise.
   */
  bool WasLoadedFromProfile() const {
    return loaded_from_profile_ != ProfileSlot_Invalid;
  }
  ProfileSlot GetLoadedFromProfileSlot() const { return loaded_from_profile_; }
  /**
   * @brief Retrieve the description used for this edit.
   * @return the description used for this edit.
   */
  std::string GetDescription() const { return Real()->description_; }
  /**
   * @brief Retrieve the ChartStyle used for this chart.
   * @return the description used for this chart.
   */
  std::string GetChartStyle() const { return Real()->chart_style_; }
  /**
   * @brief Retrieve the difficulty used for this edit.
   * @return the difficulty used for this edit.
   */
  Difficulty GetDifficulty() const { return Real()->difficulty_; }
  /**
   * @brief Retrieve the meter used for this edit.
   * @return the meter used for this edit.
   */
  int GetMeter() const { return Real()->meter_; }
  const RadarValues& GetRadarValues(PlayerNumber pn) const {
    return Real()->radar_values_[pn];
  }
  /**
   * @brief Retrieve the author credit used for this edit.
   * @return the author credit used for this edit.
   */
  std::string GetCredit() const { return Real()->credit_; }

  std::string GetChartName() const {
    return parent_ ? Real()->GetChartName() : this->chart_name_;
  }
  void SetChartName(const std::string name) { this->chart_name_ = name; }
  void SetFilename(std::string fn) { filename_ = fn; }
  std::string GetFilename() const { return filename_; }
  void SetSavedToDisk(bool b) {
    DeAutogen();
    saved_to_disk_ = b;
  }
  bool GetSavedToDisk() const { return Real()->saved_to_disk_; }
  void SetDifficulty(Difficulty dc) {
    SetDifficultyAndDescription(dc, GetDescription());
  }
  void SetDescription(std::string description) {
    SetDifficultyAndDescription(this->GetDifficulty(), description);
  }
  void SetDifficultyAndDescription(Difficulty dc, std::string description);
  void SetCredit(std::string credit);
  void SetChartStyle(std::string chart_style);
  static bool MakeValidEditDescription(
      std::string& preferred_description);  // return true if was modified

  /** @brief Produces a chart that's reduced to it's smallest unique
   * representable form. */
  std::string MinimizedChartString(const NoteData& note_data);

  /** @brief Generates a hash used for GrooveStats integration. */
  void CalculateGrooveStatsHash(const NoteData& note_data);
  const std::string GetGrooveStatsHash() const;
  int GetGrooveStatsHashVersion() const;

  void ChangeFilenamesForCustomSong();

  void SetLoadedFromProfile(ProfileSlot slot) { loaded_from_profile_ = slot; }
  void SetMeter(int meter);
  void SetRadarValues(const RadarValues v[NUM_PLAYERS]);
  void SetTechCounts(const TechCounts ts[NUM_PLAYERS]);
  void SetNpsPerMeasure(std::vector<std::vector<float>>& nps_per_measure);
  void SetNotesPerMeasure(std::vector<std::vector<int>>& notes_per_measure);
  void SetStreamBreakdown(std::vector<StreamBreakdown>& stream_breakdown);
  void SetNoteAnnotations(std::vector<NoteAnnotationCache>& note_annotations);
  void SetPeakNps(std::vector<float>& peak_nps);
  void SetGrooveStatsHash(const std::string& key);
  void SetGrooveStatsHashVersion(int version);
  float PredictMeter() const;

  unsigned GetHash() const;
  void GetNoteData(NoteData& note_data_out) const;
  NoteData GetNoteData() const;
  void SetNoteData(const NoteData& note_data_new);
  void SetSMNoteData(const std::string& notes_comp);
  void GetSMNoteData(std::string& notes_comp_out) const;

  /**
   * @brief Retrieve the NoteData from the original source.
   * @return true if successful, false for failure. */
  bool GetNoteDataFromSimfile();

  /**
   * @brief Determine if we are missing any note data.
   *
   * This takes advantage of the fact that we usually compress our data.
   * @return true if our notedata is empty, false otherwise. */
  bool IsNoteDataEmpty() const;

  void TidyUpData();

  /** @brief Convenience function to calculate Radar Values, Tech Stats, Measure
   * Stats, and GrooveStats key.*/
  void CalculateStepStats(float music_length_seconds);

  void CalculateRadarValues(
      float music_length_seconds, const NoteData& note_data);

  void CalculateTechCounts(const NoteData& note_data);
  const TechCounts& GetTechCounts(PlayerNumber pn) const {
    return Real()->tech_counts_[pn];
  }

  void CalculateMeasureInfo(const NoteData& note_data);
  const std::vector<NoteAnnotation>& GetNoteAnnotations(PlayerNumber pn) const;
  const std::vector<NoteAnnotationCache>& GetNoteAnnotationCaches() const;

  const std::vector<std::vector<float>>& GetAllNpsPerMeasures() const {
    return Real()->nps_per_measure_;
  }
  const std::vector<float>& GetNpsPerMeasure(PlayerNumber pn) const;
  const std::vector<std::vector<int>>& GetAllNotesPerMeasures() const {
    return Real()->notes_per_measure_;
  };
  const std::vector<int>& GetNotesPerMeasure(PlayerNumber pn) const;
  const std::vector<StreamBreakdown>& GetAllStreamBreakdowns() const {
    return Real()->stream_breakdown_;
  }
  const StreamBreakdown& GetStreamBreakdown(PlayerNumber pn) const;

  float GetPeakNps(PlayerNumber pn) const;
  const std::vector<float>& GetAllPeakNps() const { return Real()->peak_nps_; }
  /**
   * @brief The TimingData used by the Steps.
   *
   * This is required to allow Split Timing. */
  TimingData timing_;

  /**
   * @brief Retrieves the appropriate timing data for the Steps.  Falls
   * back on the Song if needed. */
  const TimingData* GetTimingData() const;
  TimingData* GetTimingData() {
    return const_cast<TimingData*>(
        static_cast<const Steps*>(this)->GetTimingData());
  };

  /**
   * @brief Determine if the Steps have any major timing changes during
   * gameplay.
   * @return true if it does, or false otherwise. */
  bool HasSignificantTimingChanges() const;

  /**
   * @brief Determine if the Steps or their Song have any attacks.
   * @return true if they do, false otherwise. */
  bool HasAttacks() const;
  /** @brief The attacks that belong to these Steps, excluding the Song's. */
  const AttackArray& GetStepsAttacks() const { return attacks_; }
  AttackArray& GetStepsAttacks() { return attacks_; }
  /** @brief The stringified attacks that belong to these Steps. */
  const std::vector<std::string>& GetStepsAttackStrings() const {
    return attack_string_;
  }
  std::vector<std::string>& GetStepsAttackStrings() { return attack_string_; }
  /** @brief The Song's attacks followed by these Steps' attacks. */
  AttackArray GetAttacks() const;

  const std::string GetMusicPath() const;  // Returns the path for loading.
  const std::string& GetMusicFile()
      const;  // Returns the filename for the simfile.
  void SetMusicFile(const std::string& file);

  // Lua
  void PushSelf(lua_State* L);

  StepsType steps_type_;
  /** @brief The string form of the StepsType, for dealing with unrecognized
   * styles. */
  std::string steps_type_str_;
  /** @brief The Song these Steps are associated with */
  Song* song_;

  void SetDisplayBPM(const DisplayBPM type) { this->display_bpm_type_ = type; }
  DisplayBPM GetDisplayBPM() const { return this->display_bpm_type_; }
  void SetMinBPM(const float f) { this->specified_bpm_min_ = f; }
  float GetMinBPM() const { return this->specified_bpm_min_; }
  void SetMaxBPM(const float f) { this->specified_bpm_max_ = f; }
  float GetMaxBPM() const { return this->specified_bpm_max_; }
  void GetDisplayBpms(DisplayBpms& add_to) const;

  std::string GetStepsAttackString() const {
    return join(":", this->attack_string_);
  }

 private:
  /** @brief The list of attacks that belong to these Steps, excluding the
   * Song's. */
  AttackArray attacks_;
  /** @brief The stringified list of attacks that belong to these Steps. */
  std::vector<std::string> attack_string_;

 public:
  std::vector<ColumnCue> GetColumnCues(PlayerNumber pn, float min_duration);

 private:
  inline const Steps* Real() const { return parent_ ? parent_ : this; }
  void DeAutogen(bool copy_note_data = true); /* If this Steps is autogenerated,
                                                make it a real Steps. */

  /**
   * @brief Identify this Steps' parent.
   *
   * If this Steps is autogenerated, this will point to the autogen
   * source.  If this is true, m_sNoteDataCompressed will always be empty. */
  const Steps* parent_;

  /* We can have one or both of these; if we have both, they're always
   * identical. Call Compress() to force us to only have m_sNoteDataCompressed;
   * otherwise, creation of these is transparent. */
  mutable HiddenPtr<NoteData> note_data_;
  mutable bool note_data_is_filled_;
  mutable std::string note_data_compressed_;

  /** @brief The name of the file where these steps are stored. */
  std::string filename_;
  /** @brief true if these Steps were loaded from or saved to disk. */
  bool saved_to_disk_;
  /** @brief allows the steps to specify their own music file. */
  std::string music_file_;
  /** @brief What profile was used? This is ProfileSlot_Invalid if not from a
   * profile. */
  ProfileSlot loaded_from_profile_;

  /* These values are pulled from the autogen source first, if there is one. */
  /** @brief The hash of the steps. This is used only for Edit Steps. */
  mutable unsigned hash_;
  /** @brief The name of the edit, or some other useful description.
   This used to also contain the step author's name. */
  std::string description_;
  /** @brief The style of the chart. (e.g. "Pad", "Keyboard") */
  std::string chart_style_;
  /** @brief The difficulty that these steps are assigned to. */
  Difficulty difficulty_;
  /** @brief The numeric difficulty of the Steps, ranging from MIN_METER to
   * MAX_METER. */
  int meter_;
  /** @brief The radar values used for each player. */
  RadarValues radar_values_[NUM_PLAYERS];

  /** @brief The tech stats used for each player */
  TechCounts tech_counts_[NUM_PLAYERS];

  std::vector<std::vector<float>> nps_per_measure_;
  std::vector<std::vector<int>> notes_per_measure_;
  std::vector<StreamBreakdown> stream_breakdown_;

  std::vector<NoteAnnotationCache> note_annotations_;
  std::vector<float> peak_nps_;

  std::string groove_stats_hash_;
  int groove_stats_hash_version_;

  /** @brief The name of the person who created the Steps. */
  std::string credit_;
  /** @brief The name of the chart. */
  std::string chart_name_;
  /** @brief How is the BPM displayed for this chart? */
  DisplayBPM display_bpm_type_;
  /** @brief What is the minimum specified BPM? */
  float specified_bpm_min_;
  /**
   * @brief What is the maximum specified BPM?
   * If this is a range, then min should not be equal to max. */
  float specified_bpm_max_;
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
