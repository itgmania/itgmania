#include "NotesLoaderDWI.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "MsdFile.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "NotesLoader.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_CharConversions.h"
#include "Song.h"
#include "StdString.h"
#include "Steps.h"
#include "TimingSegments.h"
#include "global.h"

Difficulty DwiCompatibleStringToDifficulty(const std::string& dc);

/** @brief The different types of core DWI arrows and pads. */
enum DanceNotes {
  DANCE_NOTE_NONE = 0,
  DANCE_NOTE_PAD1_LEFT,
  DANCE_NOTE_PAD1_UPLEFT,
  DANCE_NOTE_PAD1_DOWN,
  DANCE_NOTE_PAD1_UP,
  DANCE_NOTE_PAD1_UPRIGHT,
  DANCE_NOTE_PAD1_RIGHT,
  DANCE_NOTE_PAD2_LEFT,
  DANCE_NOTE_PAD2_UPLEFT,
  DANCE_NOTE_PAD2_DOWN,
  DANCE_NOTE_PAD2_UP,
  DANCE_NOTE_PAD2_UPRIGHT,
  DANCE_NOTE_PAD2_RIGHT
};

/**
 * @brief Turn the individual character to the proper note.
 * @param c The character in question.
 * @param i The player.
 * @param note1Out The first result based on the character.
 * @param note2Out The second result based on the character.
 * @param sPath the path to the file.
 */
static void DWIcharToNote(
    char c, GameController i, int& note1_out, int& note2_out,
    const std::string& path) {
  switch (c) {
    case '0':
      note1_out = DANCE_NOTE_NONE;
      note2_out = DANCE_NOTE_NONE;
      break;
    case '1':
      note1_out = DANCE_NOTE_PAD1_DOWN;
      note2_out = DANCE_NOTE_PAD1_LEFT;
      break;
    case '2':
      note1_out = DANCE_NOTE_PAD1_DOWN;
      note2_out = DANCE_NOTE_NONE;
      break;
    case '3':
      note1_out = DANCE_NOTE_PAD1_DOWN;
      note2_out = DANCE_NOTE_PAD1_RIGHT;
      break;
    case '4':
      note1_out = DANCE_NOTE_PAD1_LEFT;
      note2_out = DANCE_NOTE_NONE;
      break;
    case '5':
      note1_out = DANCE_NOTE_NONE;
      note2_out = DANCE_NOTE_NONE;
      break;
    case '6':
      note1_out = DANCE_NOTE_PAD1_RIGHT;
      note2_out = DANCE_NOTE_NONE;
      break;
    case '7':
      note1_out = DANCE_NOTE_PAD1_UP;
      note2_out = DANCE_NOTE_PAD1_LEFT;
      break;
    case '8':
      note1_out = DANCE_NOTE_PAD1_UP;
      note2_out = DANCE_NOTE_NONE;
      break;
    case '9':
      note1_out = DANCE_NOTE_PAD1_UP;
      note2_out = DANCE_NOTE_PAD1_RIGHT;
      break;
    case 'A':
      note1_out = DANCE_NOTE_PAD1_UP;
      note2_out = DANCE_NOTE_PAD1_DOWN;
      break;
    case 'B':
      note1_out = DANCE_NOTE_PAD1_LEFT;
      note2_out = DANCE_NOTE_PAD1_RIGHT;
      break;
    case 'C':
      note1_out = DANCE_NOTE_PAD1_UPLEFT;
      note2_out = DANCE_NOTE_NONE;
      break;
    case 'D':
      note1_out = DANCE_NOTE_PAD1_UPRIGHT;
      note2_out = DANCE_NOTE_NONE;
      break;
    case 'E':
      note1_out = DANCE_NOTE_PAD1_LEFT;
      note2_out = DANCE_NOTE_PAD1_UPLEFT;
      break;
    case 'F':
      note1_out = DANCE_NOTE_PAD1_UPLEFT;
      note2_out = DANCE_NOTE_PAD1_DOWN;
      break;
    case 'G':
      note1_out = DANCE_NOTE_PAD1_UPLEFT;
      note2_out = DANCE_NOTE_PAD1_UP;
      break;
    case 'H':
      note1_out = DANCE_NOTE_PAD1_UPLEFT;
      note2_out = DANCE_NOTE_PAD1_RIGHT;
      break;
    case 'I':
      note1_out = DANCE_NOTE_PAD1_LEFT;
      note2_out = DANCE_NOTE_PAD1_UPRIGHT;
      break;
    case 'J':
      note1_out = DANCE_NOTE_PAD1_DOWN;
      note2_out = DANCE_NOTE_PAD1_UPRIGHT;
      break;
    case 'K':
      note1_out = DANCE_NOTE_PAD1_UP;
      note2_out = DANCE_NOTE_PAD1_UPRIGHT;
      break;
    case 'L':
      note1_out = DANCE_NOTE_PAD1_UPRIGHT;
      note2_out = DANCE_NOTE_PAD1_RIGHT;
      break;
    case 'M':
      note1_out = DANCE_NOTE_PAD1_UPLEFT;
      note2_out = DANCE_NOTE_PAD1_UPRIGHT;
      break;
    default:
      LOG->UserLog(
          "Song file", path, "has an invalid DWI note character '%c'.", c);
      note1_out = DANCE_NOTE_NONE;
      note2_out = DANCE_NOTE_NONE;
      break;
  }

  switch (i) {
    case GameController_1:
      break;
    case GameController_2:
      if (note1_out != DANCE_NOTE_NONE) {
        note1_out += 6;
      }
      if (note2_out != DANCE_NOTE_NONE) {
        note2_out += 6;
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid GameController: %i", i));
  }
}

/**
 * @brief Determine the note column[s] to place notes.
 * @param c The character in question.
 * @param i The player.
 * @param col1Out The first result based on the character.
 * @param col2Out The second result based on the character.
 * @param sPath the path to the file.
 */
static void DWIcharToNoteCol(
    char c, GameController i, int& col1_out, int& col2_out,
    const std::string& path,
    const std::map<int, int>& map_dance_note_to_note_data_column) {
  int note1, note2;
  DWIcharToNote(c, i, note1, note2, path);

  if (note1 != DANCE_NOTE_NONE) {
    col1_out = map_dance_note_to_note_data_column.at(note1);
  } else {
    col1_out = -1;
  }

  if (note2 != DANCE_NOTE_NONE) {
    col2_out = map_dance_note_to_note_data_column.at(note2);
  } else {
    col2_out = -1;
  }
}

/**
 * @brief Determine if the note in question is a 192nd note.
 *
 * DWI used to use <...> to indicate 1/192nd notes; at some
 * point, <...> was changed to indicate jumps, and `' was used for
 * 1/192nds.  So, we have to do a check to figure out what it really
 * means.  If it contains 0s, it's most likely 192nds; otherwise,
 * it's most likely a jump.  Search for a 0 before the next >:
 * @param sStepData the step data.
 * @param pos the position of the step data.
 * @return true if it's a 192nd note, false otherwise.
 */
static bool Is192(const std::string& step_data, size_t pos) {
  while (pos < step_data.size()) {
    if (step_data[pos] == '>') {
      return false;
    }
    if (step_data[pos] == '0') {
      return true;
    }
    ++pos;
  }

  return false;
}
/** @brief All DWI files use 4 beats per measure. */
const int kBeatsPerMeasure = 4;

/* We prefer the normal names; recognize a number of others, too. (They'll get
 * normalized when written to SMs, etc.) */
Difficulty DwiCompatibleStringToDifficulty(const std::string& dc) {
  std::string s2 = dc;
  MakeLower(s2);
  if (s2 == "beginner") {
    return Difficulty_Beginner;
  } else if (s2 == "easy") {
    return Difficulty_Easy;
  } else if (s2 == "basic") {
    return Difficulty_Easy;
  } else if (s2 == "light") {
    return Difficulty_Easy;
  } else if (s2 == "medium") {
    return Difficulty_Medium;
  } else if (s2 == "another") {
    return Difficulty_Medium;
  } else if (s2 == "trick") {
    return Difficulty_Medium;
  } else if (s2 == "standard") {
    return Difficulty_Medium;
  } else if (s2 == "difficult") {
    return Difficulty_Medium;
  } else if (s2 == "hard") {
    return Difficulty_Hard;
  } else if (s2 == "ssr") {
    return Difficulty_Hard;
  } else if (s2 == "maniac") {
    return Difficulty_Hard;
  } else if (s2 == "heavy") {
    return Difficulty_Hard;
  } else if (s2 == "smaniac") {
    return Difficulty_Challenge;
  } else if (s2 == "challenge") {
    return Difficulty_Challenge;
  } else if (s2 == "expert") {
    return Difficulty_Challenge;
  } else if (s2 == "oni") {
    return Difficulty_Challenge;
  } else if (s2 == "edit") {
    return Difficulty_Edit;
  } else {
    return Difficulty_Invalid;
  }
}

static StepsType GetTypeFromMode(const std::string& mode) {
  if (mode == "SINGLE") {
    return StepsType_dance_single;
  } else if (mode == "DOUBLE") {
    return StepsType_dance_double;
  } else if (mode == "COUPLE") {
    return StepsType_dance_couple;
  } else if (mode == "SOLO") {
    return StepsType_dance_solo;
  }
  ASSERT_M(0, "Unrecognized DWI notes format " + mode + "!");
  return StepsType_Invalid;  // just in case.
}

static NoteData ParseNoteData(
    std::string& step1, std::string& step2, Steps& out,
    const std::string& path) {
  if (step1.size() < 2 && step2.size() < 2) {
    LOG->Warn("Didn't get enough data when attempting to load a DWI file");
    // Handle the error by returning an empty NoteData object
    return NoteData();
  }

  std::map<int, int> map_dance_note_to_note_data_column;
  switch (out.steps_type_) {
    case StepsType_dance_single:
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_LEFT] = 0;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_DOWN] = 1;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_UP] = 2;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_RIGHT] = 3;
      break;
    case StepsType_dance_double:
    case StepsType_dance_couple:
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_LEFT] = 0;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_DOWN] = 1;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_UP] = 2;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_RIGHT] = 3;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD2_LEFT] = 4;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD2_DOWN] = 5;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD2_UP] = 6;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD2_RIGHT] = 7;
      break;
    case StepsType_dance_solo:
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_LEFT] = 0;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_UPLEFT] = 1;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_DOWN] = 2;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_UP] = 3;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_UPRIGHT] = 4;
      map_dance_note_to_note_data_column[DANCE_NOTE_PAD1_RIGHT] = 5;
      break;
      DEFAULT_FAIL(out.steps_type_);
  }

  NoteData new_note_data;
  new_note_data.SetNumTracks(map_dance_note_to_note_data_column.size());

  for (int pad = 0; pad < 2; pad++)  // foreach pad
  {
    std::string step_data;
    switch (pad) {
      case 0:
        step_data = step1;
        break;
      case 1:
        if (step2 == "") {  // no data
          continue;         // skip
        }
        step_data = step2;
        break;
        DEFAULT_FAIL(pad);
    }

    Replace(step_data, "\n", "");
    Replace(step_data, "\r", "");
    Replace(step_data, "\t", "");
    Replace(step_data, " ", "");

    double current_beat = 0;
    double current_incrementer = 1.0 / 8 * kBeatsPerMeasure;

    for (size_t i = 0; i < step_data.size();) {
      char c = step_data[i++];
      switch (c) {
          // begins a series
        case '(':
          current_incrementer = 1.0 / 16 * kBeatsPerMeasure;
          break;
        case '[':
          current_incrementer = 1.0 / 24 * kBeatsPerMeasure;
          break;
        case '{':
          current_incrementer = 1.0 / 64 * kBeatsPerMeasure;
          break;
        case '`':
          current_incrementer = 1.0 / 192 * kBeatsPerMeasure;
          break;

          // ends a series
        case ')':
        case ']':
        case '}':
        case '\'':
        case '>':
          current_incrementer = 1.0 / 8 * kBeatsPerMeasure;
          break;

        default:  // this is a note character
        {
          if (c == '!') {
            LOG->UserLog(
                "Song file", path, "has an unexpected character: '!'.");
            continue;
          }

          bool jump = false;
          if (c == '<') {
            /* Arr.  Is this a jump or a 1/192 marker? */
            if (Is192(step_data, i)) {
              current_incrementer = 1.0 / 192 * kBeatsPerMeasure;
              break;
            }

            /* It's a jump.
             * We need to keep reading notes until we hit a >. */
            jump = true;
            c = step_data[i++];
          }

          const int index = BeatToNoteRow((float)current_beat);
          i--;
          do {
            c = step_data[i++];

            if (jump && c == '>') {
              break;
            }

            int col1, col2;
            DWIcharToNoteCol(
                c, (GameController)pad, col1, col2, path,
                map_dance_note_to_note_data_column);

            if (col1 != -1) {
              new_note_data.SetTapNote(col1, index, tap_original_tap);
            }
            if (col2 != -1) {
              new_note_data.SetTapNote(col2, index, tap_original_tap);
            }

            if (i >= step_data.length()) {
              break;
              // we ran out of data
              // while looking for the ending > mark
            }

            if (step_data[i] == '!') {
              i++;
              const char hold_char = step_data[i++];

              DWIcharToNoteCol(
                  hold_char, (GameController)pad, col1, col2, path,
                  map_dance_note_to_note_data_column);

              if (col1 != -1) {
                new_note_data.SetTapNote(col1, index, tap_original_hold_head);
              }
              if (col2 != -1) {
                new_note_data.SetTapNote(col2, index, tap_original_hold_head);
              }
            }

            if (jump && i < step_data.size()) {
              c = step_data[i++];
            }
          } while (jump);
          current_beat += current_incrementer;
        } break;
      }
    }
  }

  /* Fill in iDuration. */
  for (int t = 0; t < new_note_data.GetNumTracks(); ++t) {
    FOREACH_NONEMPTY_ROW_IN_TRACK(new_note_data, t, head_row) {
      TapNote tn = new_note_data.GetTapNote(t, head_row);
      if (tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      int tail_row = head_row;
      bool found = false;
      while (!found && new_note_data.GetNextTapNoteRowForTrack(t, tail_row)) {
        const TapNote& tail_tap = new_note_data.GetTapNote(t, tail_row);
        if (tail_tap.type_ == TapNoteType_Empty) {
          continue;
        }

        new_note_data.SetTapNote(t, tail_row, tap_empty);
        tn.duration_ = tail_row - head_row;
        new_note_data.SetTapNote(t, head_row, tn);
        found = true;
      }

      if (!found) {
        /* The hold was never closed.  */
        LOG->UserLog(
            "Song file", path,
            "failed to close a hold note in \"%s\" on track %i",
            DifficultyToString(out.GetDifficulty()).c_str(), t);

        new_note_data.SetTapNote(t, head_row, tap_empty);
      }
    }
  }

  ASSERT(new_note_data.GetNumTracks() > 0);
  return new_note_data;
}

/**
 * @brief Look through the notes tag to extract the data.
 * @param sMode the steps type.
 * @param sDescription the difficulty.
 * @param sNumFeet the meter.
 * @param sStepData1 the guaranteed step data.
 * @param sStepData2 used if sMode is double or couple.
 * @param out the step data.
 * @param sPath the path to the file.
 * @return the success or failure of the operation.
 */
static bool LoadFromDWITokens(
    std::string mode, std::string description, std::string num_feet,
    std::string step_data1, std::string step_data2, Steps& out,
    const std::string& path) {
  CHECKPOINT_M("DWILoader::LoadFromDWITokens()");

  // If there's no step data to load for this chart, bail out here, instead of
  // letting SetNoteData() throw an assertion. If the step data isn't at least 2
  // characters long, ParseNoteData() runs the risk of trying to read past the
  // end of the string.
  if (step_data1.size() < 2 && step_data2.size() < 2) {
    LOG->Warn(
        "Song file \"%s\" has malformed chart data for chart %s:%s:%s. "
        "Skipping chart.",
        path.c_str(), mode.c_str(), description.c_str(), num_feet.c_str());
    return false;
  }
  out.steps_type_ = GetTypeFromMode(mode);

  // if the meter is empty, force it to 1.
  if (num_feet.empty()) {
    num_feet = "1";
  }

  out.SetMeter(StringToInt(num_feet));

  out.SetDifficulty(DwiCompatibleStringToDifficulty(description));

  out.SetNoteData(ParseNoteData(step_data1, step_data2, out, path));

  out.TidyUpData();

  out.SetSavedToDisk(true);  // we're loading from disk, so this is by
                             // definintion already saved
  return true;
}

/**
 * @brief Turn the DWI style timestamp into a compatible time for our system.
 *
 * This value can be in either "HH:MM:SS.sssss", "MM:SS.sssss", "SSS.sssss"
 * or milliseconds.
 * @param arg1 Either hours, minutes, or seconds, depending on other args.
 * @param arg2 Either minutes or seconds, depending on other args.
 * @param arg3 Seconds if not empty.
 * @return the proper timestamp.
 */
static float ParseBrokenDWITimestamp(
    const std::string& arg1, const std::string& arg2, const std::string& arg3) {
  if (arg1.empty()) {
    return 0;
  }

  /* 1+ args */
  if (arg2.empty()) {
    /* If the value contains a period, treat it as seconds; otherwise ms. */
    if (arg1.find_first_of(".") != arg1.npos) {
      return StringToFloat(arg1);
    } else {
      return StringToFloat(arg1) / 1000.f;
    }
  }

  /* 2+ args */
  if (arg3.empty()) {
    return HHMMSSToSeconds(arg1 + ":" + arg2);
  }

  /* 3+ args */
  return HHMMSSToSeconds(arg1 + ":" + arg2 + ":" + arg3);
}

void DWILoader::GetApplicableFiles(
    const std::string& path, std::vector<std::string>& out) {
  GetDirListing(path + std::string("*.dwi"), out);
}

bool DWILoader::LoadNoteDataFromSimfile(const std::string& path, Steps& out) {
  MsdFile msd;
  if (!msd.ReadFile(path, false))  // don't unescape
  {
    LOG->UserLog(
        "Song file", path, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }

  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];

    if (EqualsNoCase(value_name, "SINGLE") ||
        EqualsNoCase(value_name, "DOUBLE") ||
        EqualsNoCase(value_name, "COUPLE") ||
        EqualsNoCase(value_name, "SOLO")) {
      if (out.steps_type_ != GetTypeFromMode(value_name)) {
        continue;
      }
      if (out.GetDifficulty() != DwiCompatibleStringToDifficulty(params[1])) {
        continue;
      }
      if (out.GetMeter() != StringToInt(params[2])) {
        continue;
      }
      std::string step1 = params[3];
      std::string step2 = (num_params == 5) ? params[4] : std::string("");
      out.SetNoteData(ParseNoteData(step1, step2, out, path));
      return true;
    }
  }
  return false;
}

bool DWILoader::LoadFromDir(
    const std::string& dir, Song& out,
    std::set<std::string>& blacklisted_images) {
  std::vector<std::string> file_names;
  GetApplicableFiles(dir, file_names);

  if (file_names.size() > 1) {
    LOG->UserLog(
        "Song", dir, "has more than one DWI file. There should be only one!");
    return false;
  }

  /* We should have exactly one; if we had none, we shouldn't have been called
   * to begin with. */
  ASSERT(file_names.size() == 1);
  const std::string path = dir + file_names[0];

  LOG->Trace("Song::LoadFromDWIFile(%s)", path.c_str());

  MsdFile msd;
  if (!msd.ReadFile(path, false))  // don't unescape
  {
    LOG->UserLog(
        "Song file", path, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }

  out.song_file_name_ = path;

  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];

    if (num_params < 1) {
      LOG->UserLog(
          "Song file", path, "has tag \"%s\" with no parameters.",
          value_name.c_str());
      continue;
    }

    // handle the data
    if (EqualsNoCase(value_name, "FILE")) {
      out.music_file_ = params[1];
    }

    else if (EqualsNoCase(value_name, "TITLE")) {
      NotesLoader::GetMainAndSubTitlesFromFullTitle(
          params[1], out.main_title_, out.sub_title_);

      /* As far as I know, there's no spec on the encoding of this text. (I
       * didn't look very hard, though.)  I've seen at least one file in
       * ISO-8859-1. */
      ConvertString(out.main_title_, "utf-8,english");
      ConvertString(out.sub_title_, "utf-8,english");
    }

    else if (EqualsNoCase(value_name, "ARTIST")) {
      out.artist_ = params[1];
      ConvertString(out.artist_, "utf-8,english");
    }

    else if (EqualsNoCase(value_name, "GENRE")) {
      out.genre_ = params[1];
      ConvertString(out.genre_, "utf-8,english");
    }

    else if (EqualsNoCase(value_name, "CDTITLE")) {
      out.cd_title_file_ = params[1];
    }

    else if (EqualsNoCase(value_name, "BPM")) {
      const float bpm = StringToFloat(params[1]);

      if (unlikely(bpm <= 0.0f && !PREFSMAN->quirks_mode_)) {
        LOG->UserLog(
            "Song file", path, "has an invalid BPM change at beat %f, BPM %f.",
            0.0f, bpm);
      } else {
        out.song_timing_.AddSegment(BPMSegment(0, bpm));
      }
    } else if (EqualsNoCase(value_name, "DISPLAYBPM")) {
      // #DISPLAYBPM:[xxx..xxx]|[xxx]|[*];
      int min, max;
      /* We can't parse this as a float with sscanf, since '.' is a valid
       * character in a float.  (We could do it with a regex, but it's not
       * worth bothering with since we don't display fractional BPM anyway.) */
      if (sscanf(params[1].c_str(), "%i..%i", &min, &max) == 2) {
        out.display_bpm_type_ = DISPLAY_BPM_SPECIFIED;
        out.specified_bpm_min_ = (float)min;
        out.specified_bpm_max_ = (float)max;
      } else if (sscanf(params[1].c_str(), "%i", &min) == 1) {
        out.display_bpm_type_ = DISPLAY_BPM_SPECIFIED;
        out.specified_bpm_min_ = out.specified_bpm_max_ = (float)min;
      } else {
        out.display_bpm_type_ = DISPLAY_BPM_RANDOM;
      }
    }

    else if (EqualsNoCase(value_name, "GAP")) {
      // the units of GAP is 1/1000 second
      out.song_timing_.beat0_offset_in_seconds_ =
          -StringToInt(params[1]) / 1000.0f;
    }

    else if (EqualsNoCase(value_name, "SAMPLESTART")) {
      out.music_sample_start_seconds_ =
          ParseBrokenDWITimestamp(params[1], params[2], params[3]);
    }

    else if (EqualsNoCase(value_name, "SAMPLELENGTH")) {
      float sample_length =
          ParseBrokenDWITimestamp(params[1], params[2], params[3]);
      if (sample_length > 0 && sample_length < 1) {
        // there were multiple versions of this tag allegedly: ensure a decent
        // length if requested.
        sample_length *= 1000;
      }
      out.music_sample_length_seconds_ = sample_length;

    }

    else if (EqualsNoCase(value_name, "FREEZE")) {
      std::vector<std::string> array_freeze_expressions;
      split(params[1], ",", array_freeze_expressions);

      for (unsigned f = 0; f < array_freeze_expressions.size(); f++) {
        std::vector<std::string> array_freeze_values;
        split(array_freeze_expressions[f], "=", array_freeze_values);
        if (array_freeze_values.size() != 2) {
          LOG->UserLog(
              "Song file", path, "has an invalid FREEZE: '%s'.",
              array_freeze_expressions[f].c_str());
          continue;
        }
        int freeze_row =
            BeatToNoteRow(StringToFloat(array_freeze_values[0]) / 4.0f);
        float freeze_seconds = StringToFloat(array_freeze_values[1]) / 1000.0f;

        out.song_timing_.AddSegment(StopSegment(freeze_row, freeze_seconds));
        //				LOG->Trace( "Adding a freeze segment:
        // beat: %f, seconds = %f", fFreezeBeat, fFreezeSeconds );
      }
    }

    else if (
        EqualsNoCase(value_name, "CHANGEBPM") ||
        EqualsNoCase(value_name, "BPMCHANGE")) {
      std::vector<std::string> array_bpm_change_expressions;
      split(params[1], ",", array_bpm_change_expressions);

      for (unsigned b = 0; b < array_bpm_change_expressions.size(); b++) {
        std::vector<std::string> array_bpm_change_values;
        split(array_bpm_change_expressions[b], "=", array_bpm_change_values);
        if (array_bpm_change_values.size() != 2) {
          LOG->UserLog(
              "Song file", path, "has an invalid CHANGEBPM: '%s'.",
              array_bpm_change_expressions[b].c_str());
          continue;
        }

        int start_index =
            BeatToNoteRow(StringToFloat(array_bpm_change_values[0]) / 4.0f);
        float bpm = StringToFloat(array_bpm_change_values[1]);
        if (bpm > 0.0f) {
          out.song_timing_.AddSegment(BPMSegment(start_index, bpm));
        } else {
          LOG->UserLog(
              "Song file", path,
              "has an invalid BPM change at beat %f, BPM %f.",
              NoteRowToBeat(start_index), bpm);
        }
      }
    }

    else if (
        EqualsNoCase(value_name, "SINGLE") ||
        EqualsNoCase(value_name, "DOUBLE") ||
        EqualsNoCase(value_name, "COUPLE") ||
        EqualsNoCase(value_name, "SOLO")) {
      Steps* new_notes = out.CreateSteps();
      LoadFromDWITokens(
          params[0], params[1], params[2], params[3],
          (num_params == 5) ? params[4] : std::string(""), *new_notes, path);
      if (new_notes->steps_type_ != StepsType_Invalid) {
        new_notes->SetFilename(path);
        out.AddSteps(new_notes);
      } else {
        delete new_notes;
      }
    } else if (
        EqualsNoCase(value_name, "DISPLAYTITLE") ||
        EqualsNoCase(value_name, "DISPLAYARTIST")) {
      /* We don't want to support these tags.  However, we don't want
       * to pick up images used here as song images (eg. banners). */
      std::string param = params[1];
      /* "{foo} ... {foo2}" */
      size_t pos = 0;
      while (pos < std::string::npos) {
        size_t startpos = param.find('{', pos);
        if (startpos == std::string::npos) {
          break;
        }
        size_t endpos = param.find('}', startpos);
        if (endpos == std::string::npos) {
          break;
        }

        std::string sub = param.substr(startpos + 1, endpos - startpos - 1);

        pos = endpos + 1;

        MakeLower(sub);
        blacklisted_images.insert(sub);
      }
    } else {
      // do nothing.  We don't care about this value name
    }
  }

  return true;
}

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
