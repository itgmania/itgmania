#ifndef NotesLoaderSM_H
#define NotesLoaderSM_H

#include <string>
#include <utility>
#include <vector>

#include "Attack.h"
#include "BackgroundUtil.h"
#include "GameConstantsAndTypes.h"
#include "MsdFile.h"  // we require the struct from here.

class Song;
class Steps;
class TimingData;

/**
 * @brief The highest allowable speed before Warps come in.
 *
 * This was brought in from StepMania 4's recent betas. */
const float FAST_BPM_WARP = 9999999.f;

/** @brief The maximum file size for edits. */
const int MAX_EDIT_STEPS_SIZE_BYTES = 60 * 1024;  // 60KB

/** @brief Reads a Song from an .SM file. */
struct SMLoader {
  SMLoader() : file_ext_(".sm"), song_title_() {}

  SMLoader(std::string ext) : file_ext_(ext), song_title_() {}

  virtual ~SMLoader() {}

  /**
   * @brief Attempt to load a song from a specified path.
   * @param sPath a const reference to the path on the hard drive to check.
   * @param out a reference to the Song that will retrieve the song information.
   * @return its success or failure.
   */
  virtual bool LoadFromDir(
      const std::string& path, Song& out, bool load_autosave = false);

  /**
   * @brief Retrieve the relevant notedata from the simfile.
   * @param path the path where the simfile lives.
   * @param out the Steps we are loading the data into. */
  virtual bool LoadNoteDataFromSimfile(const std::string& path, Steps& out);

  /**
   * @brief Attempt to load the specified sm file.
   * @param sPath a const reference to the path on the hard drive to check.
   * @param out a reference to the Song that will retrieve the song information.
   * @return its success or failure.
   */
  virtual bool LoadFromSimfile(const std::string& path, Song& out);
  /**
   * @brief Retrieve the list of .sm files.
   * @param sPath a const reference to the path on the hard drive to check.
   * @param out a vector of files found in the path.
   */
  virtual void GetApplicableFiles(
      const std::string& path, std::vector<std::string>& out,
      bool load_autosave = false);
  virtual bool LoadEditFromFile(
      std::string edit_file_path, ProfileSlot slot, bool add_steps_to_song,
      Song* given_song = nullptr);
  virtual bool LoadEditFromMsd(
      const MsdFile& msd, const std::string& edit_file_path, ProfileSlot slot,
      bool add_steps_to_song, Song* given_song = nullptr);
  virtual bool LoadFromBGChangesVector(
      BackgroundChange& change, std::vector<std::string> bg_change_values);

  /**
   * @brief Parse BPM Changes data from a string.
   * @param out the vector to put the data in.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  void ParseBPMs(
      std::vector<std::pair<float, float>>& out, const std::string line,
      const int rows_per_beat = -1);
  /**
   * @brief Process the BPM Segments from the string.
   * @param out the TimingData being modified.
   * @param vBPMChanges the vector of BPM Changes data. */
  void ProcessBPMs(
      TimingData& out, const std::vector<std::pair<float, float>>& bpm_changes);
  /**
   * @brief Parse Stops data from a string.
   * @param out the vector to put the data in.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  void ParseStops(
      std::vector<std::pair<float, float>>& out, const std::string line,
      const int rows_per_beat = -1);
  /**
   * @brief Process the Stop Segments from the data.
   * @param out the TimingData being modified.
   * @param vStops the vector of Stops data. */
  void ProcessStops(
      TimingData& out, const std::vector<std::pair<float, float>>& stops);
  /**
   * @brief Process BPM and stop segments from the data.
   * @param out the TimingData being modified.
   * @param vBPMs the vector of BPM changes.
   * @param vStops the vector of stops. */
  void ProcessBPMsAndStops(
      TimingData& out, std::vector<std::pair<float, float>>& bp_ms,
      std::vector<std::pair<float, float>>& stops);
  /**
   * @brief Process the Delay Segments from the string.
   * @param out the TimingData being modified.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  void ProcessDelays(
      TimingData& out, const std::string line, const int rows_per_beat = -1);
  /**
   * @brief Process the Time Signature Segments from the string.
   * @param out the TimingData being modified.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  void ProcessTimeSignatures(
      TimingData& out, const std::string line, const int rows_per_beat = -1);
  /**
   * @brief Process the Tickcount Segments from the string.
   * @param out the TimingData being modified.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  void ProcessTickcounts(
      TimingData& out, const std::string line, const int rows_per_beat = -1);

  /**
   * @brief Process the Speed Segments from the string.
   * @param out the TimingData being modified.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  virtual void ProcessSpeeds(
      TimingData& out, const std::string line, const int rows_per_beat = -1);

  virtual void ProcessCombos(
      TimingData& /* out */, const std::string line,
      const int /* rowsPerBeat */ = -1) {}

  /**
   * @brief Process the Fake Segments from the string.
   * @param out the TimingData being modified.
   * @param line the string in question.
   * @param rowsPerBeat the number of rows per beat for this purpose. */
  virtual void ProcessFakes(
      TimingData& out, const std::string line, const int rows_per_beat = -1);

  virtual void ProcessBGChanges(
      Song& out, const std::string& value_name, const std::string& path,
      const std::string& param);

  virtual void ParseBGChangesString(
      const std::string& changes, std::vector<std::vector<std::string>>& add_to,
      const std::string& song_dir);

  /**
   * @brief Put the attacks in the attacks string.
   * @param attacks the attack string.
   * @param params the params from the simfile. */
  virtual void ProcessAttackString(
      std::vector<std::string>& attacks, MsdFile::value_t params);

  /**
   * @brief Put the attacks in the attacks array.
   * @param attacks the attacks array.
   * @param params the params from the simfile. */
  void ProcessAttacks(AttackArray& attacks, MsdFile::value_t params);

  /**
   * @brief Convert a row value to the proper beat value.
   *
   * This is primarily used for assistance with converting SMA files.
   * @param line The line that contains the value.
   * @param rowsPerBeat the number of rows per beat according to the original
   * file.
   * @return the converted beat value. */
  float RowToBeat(std::string line, const int rows_per_beat);

 protected:
  /**
   * @brief Process the different tokens we have available to get NoteData.
   * @param stepsType The current StepsType.
   * @param description The description of the chart.
   * @param difficulty The difficulty (in words) of the chart.
   * @param meter the difficulty (in numbers) of the chart.
   * @param radarValues the calculated radar values.
   * @param noteData the note data itself.
   * @param out the Steps getting the data. */
  virtual void LoadFromTokens(
      std::string steps_type, std::string description, std::string difficulty,
      std::string meter, std::string radar_values, std::string note_data,
      Steps& out);

  /**
   * @brief Retrieve the file extension associated with this loader.
   * @return the file extension. */
  std::string GetFileExtension() const { return file_ext_; }

  std::vector<std::string> GetSongDirFiles(const std::string& song_dir);

 public:
  // SetSongTitle and GetSongTitle changed to public to allow the functions
  // used by the parser helper to access them. -Kyz
  /**
   * @brief Set the song title.
   * @param t the song title. */
  virtual void SetSongTitle(const std::string& title);

  /**
   * @brief Get the song title.
   * @return the song title. */
  virtual std::string GetSongTitle() const;

 private:
  /** @brief The file extension in use. */
  const std::string file_ext_;
  /** @brief The song title that is being processed. */
  std::string song_title_;

  std::vector<std::string> song_dir_files_;
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
