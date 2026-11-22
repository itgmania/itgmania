#include "NotesLoaderSM.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Attack.h"
#include "BackgroundUtil.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "MsdFile.h"
#include "NoteTypes.h"
#include "PrefsManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/ConvertValue.h"
#include "Song.h"
#include "SongManager.h"
#include "StdString.h"
#include "Steps.h"
#include "TimingSegments.h"
#include "global.h"

// Everything from this line to the creation of sm_parser_helper exists to
// speed up parsing by allowing the use of std::map.  All these functions
// are put into a map of function pointers which is used when loading.
// -Kyz
/****************************************************************/
struct SMSongTagInfo {
  SMLoader* loader_;
  Song* song_;
  const MsdFile::value_t* params_;
  const std::string& path_;
  std::vector<std::pair<float, float>> bpm_changes_, stops_;
  SMSongTagInfo(SMLoader* l, Song* s, const std::string& p)
      : loader_(l), song_(s), path_(p) {}
};

typedef void (*song_tag_func_t)(SMSongTagInfo& info);

// Functions for song tags go below this line. -Kyz
/****************************************************************/
void SMSetTitle(SMSongTagInfo& info) {
  info.song_->main_title_ = (*info.params_)[1];
  info.loader_->SetSongTitle((*info.params_)[1]);
}
void SMSetSubtitle(SMSongTagInfo& info) {
  info.song_->sub_title_ = (*info.params_)[1];
}
void SMSetArtist(SMSongTagInfo& info) {
  info.song_->artist_ = (*info.params_)[1];
}
void SMSetTitleTranslit(SMSongTagInfo& info) {
  info.song_->main_title_translit_ = (*info.params_)[1];
}
void SMSetSubtitleTranslit(SMSongTagInfo& info) {
  info.song_->sub_title_translit_ = (*info.params_)[1];
}
void SMSetArtistTranslit(SMSongTagInfo& info) {
  info.song_->artist_translit_ = (*info.params_)[1];
}
void SMSetGenre(SMSongTagInfo& info) {
  info.song_->genre_ = (*info.params_)[1];
}
void SMSetCredit(SMSongTagInfo& info) {
  info.song_->credit_ = (*info.params_)[1];
}
void SMSetBanner(SMSongTagInfo& info) {
  info.song_->banner_file_ = (*info.params_)[1];
}
void SMSetBackground(SMSongTagInfo& info) {
  info.song_->background_file_ = (*info.params_)[1];
}
void SMSetLyricsPath(SMSongTagInfo& info) {
  info.song_->lyrics_file_ = (*info.params_)[1];
}
void SMSetCDTitle(SMSongTagInfo& info) {
  info.song_->cd_title_file_ = (*info.params_)[1];
}
void SMSetMusic(SMSongTagInfo& info) {
  info.song_->music_file_ = (*info.params_)[1];
}
void SMSetOffset(SMSongTagInfo& info) {
  info.song_->song_timing_.beat0_offset_in_seconds_ =
      StringToFloat((*info.params_)[1]);
}
void SMSetBPMs(SMSongTagInfo& info) {
  info.bpm_changes_.clear();
  info.loader_->ParseBPMs(info.bpm_changes_, (*info.params_)[1]);
}
void SMSetStops(SMSongTagInfo& info) {
  info.stops_.clear();
  info.loader_->ParseStops(info.stops_, (*info.params_)[1]);
}
void SMSetDelays(SMSongTagInfo& info) {
  info.loader_->ProcessDelays(info.song_->song_timing_, (*info.params_)[1]);
}
void SMSetTimeSignatures(SMSongTagInfo& info) {
  info.loader_->ProcessTimeSignatures(
      info.song_->song_timing_, (*info.params_)[1]);
}
void SMSetTickCounts(SMSongTagInfo& info) {
  info.loader_->ProcessTickcounts(info.song_->song_timing_, (*info.params_)[1]);
}
void SMSetSampleStart(SMSongTagInfo& info) {
  info.song_->music_sample_start_seconds_ = HHMMSSToSeconds((*info.params_)[1]);
}
void SMSetSampleLength(SMSongTagInfo& info) {
  info.song_->music_sample_length_seconds_ =
      HHMMSSToSeconds((*info.params_)[1]);
}
void SMSetDisplayBPM(SMSongTagInfo& info) {
  // #DISPLAYBPM:[xxx][xxx:xxx]|[*];
  if ((*info.params_)[1] == "*") {
    info.song_->display_bpm_type_ = DISPLAY_BPM_RANDOM;
  } else {
    info.song_->display_bpm_type_ = DISPLAY_BPM_SPECIFIED;
    info.song_->specified_bpm_min_ = StringToFloat((*info.params_)[1]);
    if ((*info.params_)[2].empty()) {
      info.song_->specified_bpm_max_ = info.song_->specified_bpm_min_;
    } else {
      info.song_->specified_bpm_max_ = StringToFloat((*info.params_)[2]);
    }
  }
}
void SMSetSelectable(SMSongTagInfo& info) {
  if (EqualsNoCase((*info.params_)[1], "YES")) {
    info.song_->selection_display_ = info.song_->SHOW_ALWAYS;
  } else if (EqualsNoCase((*info.params_)[1], "NO")) {
    info.song_->selection_display_ = info.song_->SHOW_NEVER;
  }
  // ROULETTE from 3.9. It was removed since UnlockManager can serve
  // the same purpose somehow. This, of course, assumes you're using
  // unlocks. -aj
  else if (EqualsNoCase((*info.params_)[1], "ROULETTE")) {
    info.song_->selection_display_ = info.song_->SHOW_ALWAYS;
  }
  /* The following two cases are just fixes to make sure simfiles that
   * used 3.9+ features are not excluded here */
  else if (
      EqualsNoCase((*info.params_)[1], "ES") ||
      EqualsNoCase((*info.params_)[1], "OMES")) {
    info.song_->selection_display_ = info.song_->SHOW_ALWAYS;
  } else if (StringToInt((*info.params_)[1]) > 0) {
    info.song_->selection_display_ = info.song_->SHOW_ALWAYS;
  } else {
    LOG->UserLog(
        "Song file", info.path_,
        "has an unknown #SELECTABLE value, \"%s\"; ignored.",
        (*info.params_)[1].c_str());
  }
}
void SMSetBGChanges(SMSongTagInfo& info) {
  info.loader_->ProcessBGChanges(
      *info.song_, (*info.params_)[0], info.path_, (*info.params_)[1]);
}
void SMSetFGChanges(SMSongTagInfo& info) {
  std::vector<std::vector<std::string>> fg_changes;
  info.loader_->ParseBGChangesString(
      (*info.params_)[1], fg_changes, info.song_->GetSongDir());

  for (const auto& b : fg_changes) {
    BackgroundChange change;
    if (info.loader_->LoadFromBGChangesVector(change, b)) {
      info.song_->AddForegroundChange(change);
    }
  }
}
void SMSetKeysounds(SMSongTagInfo& info) {
  split((*info.params_)[1], ",", info.song_->keysound_file_);
}
void SMSetAttacks(SMSongTagInfo& info) {
  info.loader_->ProcessAttackString(
      info.song_->attack_string_, (*info.params_));
  info.loader_->ProcessAttacks(info.song_->attacks_, (*info.params_));
}

typedef std::map<std::string, song_tag_func_t> song_handler_map_t;

struct sm_parser_helper_t {
  song_handler_map_t song_tag_handlers_;
  // Unless signed, the comments in this tag list are not by me.  They were
  // moved here when converting from the else if chain. -Kyz
  sm_parser_helper_t() {
    song_tag_handlers_["TITLE"] = &SMSetTitle;
    song_tag_handlers_["SUBTITLE"] = &SMSetSubtitle;
    song_tag_handlers_["ARTIST"] = &SMSetArtist;
    song_tag_handlers_["TITLETRANSLIT"] = &SMSetTitleTranslit;
    song_tag_handlers_["SUBTITLETRANSLIT"] = &SMSetSubtitleTranslit;
    song_tag_handlers_["ARTISTTRANSLIT"] = &SMSetArtistTranslit;
    song_tag_handlers_["GENRE"] = &SMSetGenre;
    song_tag_handlers_["CREDIT"] = &SMSetCredit;
    song_tag_handlers_["BANNER"] = &SMSetBanner;
    song_tag_handlers_["BACKGROUND"] = &SMSetBackground;
    // Save "#LYRICS" for later, so we can add an internal lyrics tag.
    song_tag_handlers_["LYRICSPATH"] = &SMSetLyricsPath;
    song_tag_handlers_["CDTITLE"] = &SMSetCDTitle;
    song_tag_handlers_["MUSIC"] = &SMSetMusic;
    song_tag_handlers_["OFFSET"] = &SMSetOffset;
    song_tag_handlers_["BPMS"] = &SMSetBPMs;
    song_tag_handlers_["STOPS"] = &SMSetStops;
    song_tag_handlers_["FREEZES"] = &SMSetStops;
    song_tag_handlers_["DELAYS"] = &SMSetDelays;
    song_tag_handlers_["TIMESIGNATURES"] = &SMSetTimeSignatures;
    song_tag_handlers_["TICKCOUNTS"] = &SMSetTickCounts;
    song_tag_handlers_["SAMPLESTART"] = &SMSetSampleStart;
    song_tag_handlers_["SAMPLELENGTH"] = &SMSetSampleLength;
    song_tag_handlers_["DISPLAYBPM"] = &SMSetDisplayBPM;
    song_tag_handlers_["SELECTABLE"] = &SMSetSelectable;
    // It's a bit odd to have the tag that exists for backwards compatibility
    // in this list and not the replacement, but the BGCHANGES tag has a
    // number on the end, allowing up to NUM_BackgroundLayer tags, so it
    // can't fit in the map. -Kyz
    song_tag_handlers_["ANIMATIONS"] = &SMSetBGChanges;
    song_tag_handlers_["FGCHANGES"] = &SMSetFGChanges;
    song_tag_handlers_["KEYSOUNDS"] = &SMSetKeysounds;
    // Attacks loaded from file
    song_tag_handlers_["ATTACKS"] = &SMSetAttacks;
    /* Tags that no longer exist, listed for posterity.  May their names
     * never be forgotten for their service to Stepmania. -Kyz
     * LASTBEATHINT: // unable to identify at this point: ignore
     * MUSICBYTES: // ignore
     * FIRSTBEAT: // cache tags from older SM files: ignore.
     * LASTBEAT: // cache tags from older SM files: ignore.
     * SONGFILENAME: // cache tags from older SM files: ignore.
     * HASMUSIC: // cache tags from older SM files: ignore.
     * HASBANNER: // cache tags from older SM files: ignore.
     * SAMPLEPATH: // SamplePath was used when the song has a separate preview
     * clip. -aj LEADTRACK: // XXX: Does anyone know what LEADTRACK is for?
     * -Wolfman2000 MUSICLENGTH: // Loaded from the cache now. -Kyz
     */
  }
};
sm_parser_helper_t sm_parser_helper;
// End sm_parser_helper related functions. -Kyz
/****************************************************************/

void SMLoader::SetSongTitle(const std::string& title) {
  this->song_title_ = title;
}

std::string SMLoader::GetSongTitle() const { return this->song_title_; }

bool SMLoader::LoadFromDir(
    const std::string& path, Song& out, bool load_autosave) {
  std::vector<std::string> file_names;
  GetApplicableFiles(path, file_names, load_autosave);
  return LoadFromSimfile(path + file_names[0], out);
}

float SMLoader::RowToBeat(std::string line, const int rows_per_beat) {
  std::string backup = line;
  Trim(line, "r");
  Trim(line, "R");
  if (backup != line) {
    return StringToFloat(line) / rows_per_beat;
  } else {
    return StringToFloat(line);
  }
}

void SMLoader::LoadFromTokens(
    std::string steps_type, std::string description, std::string difficulty,
    std::string meter, std::string radar_values, std::string note_data,
    Steps& out) {
  // we're loading from disk, so this is by definition already saved:
  out.SetSavedToDisk(true);

  Trim(steps_type);
  Trim(description);
  Trim(difficulty);
  Trim(note_data);

  // LOG->Trace( "Steps::LoadFromTokens(), %s", sStepsType.c_str() );

  // backwards compatibility hacks:
  // HACK: We eliminated "ez2-single-hard", but we should still handle it.
  if (steps_type == "ez2-single-hard") {
    steps_type = "ez2-single";
  }

  // HACK: "para-single" used to be called just "para"
  if (steps_type == "para") {
    steps_type = "para-single";
  }

  out.steps_type_ = GAMEMAN->StringToStepsType(steps_type);
  out.steps_type_str_ = steps_type;
  out.SetDescription(description);
  out.SetCredit(description);     // this is often used for both.
  out.SetChartName(description);  // yeah, one more for good measure.
  out.SetDifficulty(OldStyleStringToDifficulty(difficulty));

  // Handle hacks that originated back when StepMania didn't have
  // Difficulty_Challenge. (At least v1.64, possibly v3.0 final...)
  if (out.GetDifficulty() == Difficulty_Hard) {
    // HACK: SMANIAC used to be Difficulty_Hard with a special description.
    if (CompareNoCase(description, "smaniac") == 0) {
      out.SetDifficulty(Difficulty_Challenge);
    }

    // HACK: CHALLENGE used to be Difficulty_Hard with a special description.
    if (CompareNoCase(description, "challenge") == 0) {
      out.SetDifficulty(Difficulty_Challenge);
    }
  }

  if (meter.empty()) {
    // some simfiles (e.g. X-SPECIALs from Zenius-I-Vanisher) don't
    // have a meter on certain steps. Make the meter 1 in these instances.
    meter = "1";
  }
  out.SetMeter(StringToInt(meter));

  out.SetSMNoteData(note_data);

  out.TidyUpData();
}

void SMLoader::ProcessBGChanges(
    Song& out, const std::string& value_name, const std::string& path,
    const std::string& param) {
  BackgroundLayer layer = BACKGROUND_LAYER_1;
  if (sscanf(value_name.c_str(), "BGCHANGES%d", &*ConvertValue<int>(&layer)) ==
      1) {
    enum_add(layer, -1);  // #BGCHANGES2 = BACKGROUND_LAYER_2
  }

  bool valid = layer >= 0 && layer < NUM_BackgroundLayer;
  if (!valid) {
    LOG->UserLog(
        "Song file", path, "has a #BGCHANGES tag \"%s\" that is out of range.",
        value_name.c_str());
  } else {
    std::vector<std::vector<std::string>> bg_changes;
    ParseBGChangesString(param, bg_changes, out.GetSongDir());

    for (const auto& b : bg_changes) {
      BackgroundChange change;
      if (LoadFromBGChangesVector(change, b)) {
        out.AddBackgroundChange(layer, change);
      }
    }
  }
}

void SMLoader::ProcessAttackString(
    std::vector<std::string>& attacks, MsdFile::value_t params) {
  for (unsigned s = 1; s < params.params_.size(); ++s) {
    std::string tmp = params[s];
    Trim(tmp);
    if (tmp.size() > 0) {
      attacks.push_back(tmp);
    }
  }
}

void SMLoader::ProcessAttacks(AttackArray& attacks, MsdFile::value_t params) {
  Attack attack;
  float end = -9999;

  for (unsigned j = 1; j < params.params_.size(); ++j) {
    std::vector<std::string> bits;
    split(params[j], "=", bits, false);

    // Need an identifer and a value for this to work
    if (bits.size() < 2) {
      continue;
    }

    Trim(bits[0]);

    if (!CompareNoCase(bits[0], "TIME")) {
      attack.start_second_ = strtof(bits[1].c_str(), nullptr);
    } else if (!CompareNoCase(bits[0], "LEN")) {
      attack.secs_remaining_ = strtof(bits[1].c_str(), nullptr);
    } else if (!CompareNoCase(bits[0], "END")) {
      end = strtof(bits[1].c_str(), nullptr);
    } else if (!CompareNoCase(bits[0], "MODS")) {
      Trim(bits[1]);
      attack.modifiers_ = bits[1];

      if (end != -9999) {
        attack.secs_remaining_ = end - attack.start_second_;
        end = -9999;
      }

      if (attack.secs_remaining_ < 0.0f) {
        attack.secs_remaining_ = 0.0f;
      }

      attacks.push_back(attack);
    }
  }
}

void SMLoader::ParseBPMs(
    std::vector<std::pair<float, float>>& out, const std::string line,
    const int rows_per_beat) {
  std::vector<std::string> array_bpm_change_expressions;
  split(line, ",", array_bpm_change_expressions);

  for (unsigned b = 0; b < array_bpm_change_expressions.size(); b++) {
    std::vector<std::string> array_bpm_change_values;
    Trim(array_bpm_change_expressions[b]);
    if (array_bpm_change_expressions[b].empty()) {
      continue;
    }
    split(array_bpm_change_expressions[b], "=", array_bpm_change_values);
    if (array_bpm_change_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #BPMs value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_bpm_change_expressions[b].c_str());
      continue;
    }

    const float beat = RowToBeat(array_bpm_change_values[0], rows_per_beat);
    const float new_bpm = StringToFloat(array_bpm_change_values[1]);
    if (new_bpm == 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(), "has a zero BPM; ignored.");
      continue;
    }

    out.push_back(std::make_pair(beat, new_bpm));
  }
}

void SMLoader::ParseStops(
    std::vector<std::pair<float, float>>& out, const std::string line,
    const int rows_per_beat) {
  std::vector<std::string> array_freeze_expressions;
  split(line, ",", array_freeze_expressions);

  for (unsigned f = 0; f < array_freeze_expressions.size(); f++) {
    std::vector<std::string> array_freeze_values;
    Trim(array_freeze_expressions[f]);
    if (array_freeze_expressions[f].empty()) {
      continue;
    }
    split(array_freeze_expressions[f], "=", array_freeze_values);
    if (array_freeze_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #STOPS value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_freeze_expressions[f].c_str());
      continue;
    }

    const float freeze_beat = RowToBeat(array_freeze_values[0], rows_per_beat);
    const float freeze_seconds = StringToFloat(array_freeze_values[1]);
    if (freeze_seconds == 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has a zero-length stop; ignored.");
      continue;
    }

    out.push_back(std::make_pair(freeze_beat, freeze_seconds));
  }
}

// Utility function for sorting timing change data
namespace {
bool compare_first(std::pair<float, float> a, std::pair<float, float> b) {
  return a.first < b.first;
}
}  // namespace

// Precondition: no BPM change or stop has 0 for its value (change.second).
//     (The ParseBPMs and ParseStops functions make sure of this.)
// Postcondition: all BPM changes, stops, and warps are added to the out
//     parameter, already sorted by beat.
void SMLoader::ProcessBPMsAndStops(
    TimingData& out, std::vector<std::pair<float, float>>& bp_ms,
    std::vector<std::pair<float, float>>& stops) {
  std::vector<std::pair<float, float>>::const_iterator ibpm, ibpmend;
  std::vector<std::pair<float, float>>::const_iterator istop, istopend;

  // Current BPM (positive or negative)
  float bpm = 0;
  // Beat at which the previous timing change occurred
  float prevbeat = 0;
  // Start/end of current warp (-1 if not currently warping)
  float warpstart = -1;
  float warpend = -1;
  // BPM prior to current warp, to detect if it has changed
  float prewarpbpm = 0;
  // How far off we have gotten due to negative changes
  float timeofs = 0;

  // Sort BPM changes and stops by beat.  Order matters.
  // TODO: Make sorted lists a precondition rather than sorting them here.
  // The caller may know that the lists are sorted already (e.g. if
  // loaded from cache).
  stable_sort(bp_ms.begin(), bp_ms.end(), compare_first);
  stable_sort(stops.begin(), stops.end(), compare_first);

  // Convert stops that come before beat 0.  All these really do is affect
  // where the arrows are with respect to the music, i.e. the song offset.
  // Positive stops subtract from the offset, and negative add to it.
  istop = stops.begin();
  istopend = stops.end();
  for (/* istop */; istop != istopend && istop->first < 0; istop++) {
    out.beat0_offset_in_seconds_ -= istop->second;
  }

  // Get rid of BPM changes that come before beat 0.  Positive BPMs before
  // the chart don't really do anything, so we just ignore them.  Negative
  // BPMs cause unpredictable behavior, so ignore them as well and issue a
  // warning.
  ibpm = bp_ms.begin();
  ibpmend = bp_ms.end();
  for (/* ibpm */; ibpm != ibpmend && ibpm->first <= 0; ibpm++) {
    bpm = ibpm->second;
    if (bpm < 0 && ibpm->first < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has a negative BPM prior to beat 0.  "
          "These cause problems; ignoring.");
    }
  }

  // It's beat 0.  Do you know where your BPMs are?
  if (bpm == 0) {
    // Nope.  Can we just use the next BPM value?
    if (ibpm == ibpmend) {
      // Nope.
      bpm = 60;
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has no valid BPMs.  Defaulting to 60.");
    } else {
      // Yep.  Get the next BPM.
      ibpm++;
      bpm = ibpm->second;
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "does not establish a BPM before beat 0.  "
          "Using the value from the next BPM change.");
    }
  }
  // We always want to have an initial BPM.  If we start out warping, this
  // BPM will be added later.  If we start with a regular BPM, add it now.
  if (bpm > 0 && bpm <= FAST_BPM_WARP) {
    out.AddSegment(BPMSegment(BeatToNoteRow(0), bpm));
  }

  // Iterate over all BPMs and stops in tandem
  while (ibpm != ibpmend || istop != istopend) {
    // Get the next change in order, with BPMs taking precedence
    // when they fall on the same beat.
    bool change_is_bpm =
        istop == istopend || (ibpm != ibpmend && ibpm->first <= istop->first);
    const std::pair<float, float>& change = change_is_bpm ? *ibpm : *istop;

    // Calculate the effects of time at the current BPM.  "Infinite"
    // BPMs (SM4 warps) imply that zero time passes, so skip this
    // step in that case.
    if (bpm <= FAST_BPM_WARP) {
      timeofs += (change.first - prevbeat) * 60 / bpm;

      // If we were in a warp and it finished during this
      // timeframe, create the warp segment.
      if (warpstart >= 0 && bpm > 0 && timeofs > 0) {
        // timeofs represents how far past the end we are
        warpend = change.first - (timeofs * bpm / 60);
        out.AddSegment(
            WarpSegment(BeatToNoteRow(warpstart), warpend - warpstart));

        // If the BPM changed during the warp, put that
        // change at the beginning of the warp.
        if (bpm != prewarpbpm) {
          out.AddSegment(BPMSegment(BeatToNoteRow(warpstart), bpm));
        }
        // No longer warping
        warpstart = -1;
      }
    }

    // Save the current beat for the next round of calculations
    prevbeat = change.first;

    // Now handle the timing changes themselves
    if (change_is_bpm) {
      // Does this BPM change start a new warp?
      if (warpstart < 0 &&
          (change.second < 0 || change.second > FAST_BPM_WARP)) {
        // Yes.
        warpstart = change.first;
        prewarpbpm = bpm;
        timeofs = 0;
      } else if (warpstart < 0) {
        // No, and we aren't currently warping either.
        // Just a normal BPM change.
        out.AddSegment(BPMSegment(BeatToNoteRow(change.first), change.second));
      }
      bpm = change.second;
      ibpm++;
    } else {
      // Does this stop start a new warp?
      if (warpstart < 0 && change.second < 0) {
        // Yes.
        warpstart = change.first;
        prewarpbpm = bpm;
        timeofs = change.second;
      } else if (warpstart < 0) {
        // No, and we aren't currently warping either.
        // Just a normal stop.
        out.AddSegment(StopSegment(BeatToNoteRow(change.first), change.second));
      } else {
        // We're warping already.  Stops affect the time
        // offset directly.
        timeofs += change.second;

        // If a stop overcompensates for the time
        // deficit, the warp ends and we stop for the
        // amount it goes over.
        if (change.second > 0 && timeofs > 0) {
          warpend = change.first;
          out.AddSegment(
              WarpSegment(BeatToNoteRow(warpstart), warpend - warpstart));
          out.AddSegment(StopSegment(BeatToNoteRow(change.first), timeofs));

          // Now, are we still warping because of
          // the BPM value?
          if (bpm < 0 || bpm > FAST_BPM_WARP) {
            // Yep.
            warpstart = change.first;
            // prewarpbpm remains the same
            timeofs = 0;
          } else {
            // Nope, warp is done.  Add any
            // BPM change that happened in
            // the meantime.
            if (bpm != prewarpbpm) {
              out.AddSegment(BPMSegment(BeatToNoteRow(warpstart), bpm));
            }
            warpstart = -1;
          }
        }
      }
      istop++;
    }
  }

  // If we are still warping, we now have to consider the time remaining
  // after the last timing change.
  if (warpstart >= 0) {
    // Will this warp ever end?
    if (bpm < 0 || bpm > FAST_BPM_WARP) {
      // No, so it ends the entire chart immediately.
      // XXX There must be a less hacky and more accurate way
      // to do this.
      warpend = 99999999.0f;
    } else {
      // Yes.  Figure out when it will end.
      warpend = prevbeat - (timeofs * bpm / 60);
    }
    out.AddSegment(WarpSegment(BeatToNoteRow(warpstart), warpend - warpstart));

    // As usual, record any BPM change that happened during the warp
    if (bpm != prewarpbpm) {
      out.AddSegment(BPMSegment(BeatToNoteRow(warpstart), bpm));
    }
  }
}

void SMLoader::ProcessDelays(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> array_delay_expressions;
  split(line, ",", array_delay_expressions);

  for (unsigned f = 0; f < array_delay_expressions.size(); f++) {
    std::vector<std::string> array_delay_values;
    Trim(array_delay_expressions[f]);
    if (array_delay_expressions[f].empty()) {
      continue;
    }
    split(array_delay_expressions[f], "=", array_delay_values);
    if (array_delay_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #DELAYS value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_delay_expressions[f].c_str());
      continue;
    }
    const float freeze_beat = RowToBeat(array_delay_values[0], rows_per_beat);
    const float freeze_seconds = StringToFloat(array_delay_values[1]);
    // LOG->Trace( "Adding a delay segment: beat: %f, seconds = %f",
    // new_seg.m_fStartBeat, new_seg.m_fStopSeconds );

    if (freeze_seconds > 0.0f) {
      out.AddSegment(DelaySegment(BeatToNoteRow(freeze_beat), freeze_seconds));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid delay at beat %f, length %f.", freeze_beat,
          freeze_seconds);
    }
  }
}

void SMLoader::ProcessTimeSignatures(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> vs1;
  std::vector<TimeSignatureSegment> segments;
  split(line, ",", vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    split(s1, "=", vs2);

    if (vs2.size() < 3) {
      LOG->UserLog(
          "Song file", GetSongTitle(),
          "has an invalid time signature change with %i values.",
          static_cast<int>(vs2.size()));
      continue;
    }

    const float beat = RowToBeat(vs2[0], rows_per_beat);
    const int numerator = StringToInt(vs2[1]);
    const int denominator = StringToInt(vs2[2]);

    if (beat < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid time signature change with beat %f.", beat);
      continue;
    }

    if (numerator < 1) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid time signature change with beat %f, iNumerator %i.",
          beat, numerator);
      continue;
    }

    if (denominator < 1) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid time signature change with beat %f, iDenominator %i.",
          beat, denominator);
      continue;
    }
    segments.push_back(
        TimeSignatureSegment(BeatToNoteRow(beat), numerator, denominator));
  }

  // If there are any time signatures defined, but there isn't one
  // for the very first beat of the song, then add one.
  // Without it, calls to functions like TimingData::NoteRowToMeasureAndBeat
  // can fail for charts that are otherwise valid.
  if (segments.size() > 0 && segments[0].GetRow() > 0) {
    out.AddSegment(TimeSignatureSegment(0, 4, 4));
  }

  for (TimeSignatureSegment segment : segments) {
    out.AddSegment(segment);
  }
}

void SMLoader::ProcessTickcounts(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> array_tickcount_expressions;
  split(line, ",", array_tickcount_expressions);

  for (unsigned f = 0; f < array_tickcount_expressions.size(); f++) {
    std::vector<std::string> array_tickcount_values;
    Trim(array_tickcount_expressions[f]);
    if (array_tickcount_expressions[f].empty()) {
      continue;
    }
    split(array_tickcount_expressions[f], "=", array_tickcount_values);
    if (array_tickcount_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #TICKCOUNTS value \"%s\" (must have exactly one "
          "'='), ignored.",
          array_tickcount_expressions[f].c_str());
      continue;
    }

    const float tickcount_beat =
        RowToBeat(array_tickcount_values[0], rows_per_beat);
    int ticks =
        std::clamp(atoi(array_tickcount_values[1].c_str()), 0, ROWS_PER_BEAT);

    out.AddSegment(TickcountSegment(BeatToNoteRow(tickcount_beat), ticks));
  }
}

void SMLoader::ProcessSpeeds(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> vs1;
  split(line, ",", vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    split(s1, "=", vs2);

    if (vs2[0].empty() && vs2.size() == 2)  // First one always seems to have 2.
    {
      vs2.push_back("0");
    }

    if (vs2.size() == 3)  // use beats by default.
    {
      vs2.push_back("0");
    }

    if (vs2.size() < 4) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an speed change with %i values.", static_cast<int>(vs2.size()));
      continue;
    }

    const float beat = RowToBeat(vs2[0], rows_per_beat);
    const float ratio = StringToFloat(vs2[1]);
    const float delay = StringToFloat(vs2[2]);

    // XXX: ugly...
    int i_unit = StringToInt(vs2[3]);
    SpeedSegment::BaseUnit unit =
        (i_unit == 0) ? SpeedSegment::UNIT_BEATS : SpeedSegment::UNIT_SECONDS;

    if (beat < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an speed change with beat %f.", beat);
      continue;
    }

    if (delay < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an speed change with beat %f, length %f.", beat, delay);
      continue;
    }

    out.AddSegment(SpeedSegment(BeatToNoteRow(beat), ratio, delay, unit));
  }
}

void SMLoader::ProcessFakes(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> array_fake_expressions;
  split(line, ",", array_fake_expressions);

  for (unsigned b = 0; b < array_fake_expressions.size(); b++) {
    std::vector<std::string> array_fake_values;
    Trim(array_fake_expressions[b]);
    if (array_fake_expressions[b].empty()) {
      continue;
    }
    split(array_fake_expressions[b], "=", array_fake_values);
    if (array_fake_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #FAKES value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_fake_expressions[b].c_str());
      continue;
    }

    const float beat = RowToBeat(array_fake_values[0], rows_per_beat);
    const float skipped_beats = StringToFloat(array_fake_values[1]);

    if (skipped_beats > 0) {
      out.AddSegment(FakeSegment(BeatToNoteRow(beat), skipped_beats));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Fake at beat %f, beats to skip %f.", beat,
          skipped_beats);
    }
  }
}

bool SMLoader::LoadFromBGChangesVector(
    BackgroundChange& change, std::vector<std::string> bg_change_values) {
  bg_change_values.resize(std::min((int)bg_change_values.size(), 11));

  switch (bg_change_values.size()) {
    case 11:
      change.def_.color2_ = bg_change_values[10];
      Replace(change.def_.color2_, '^', ',');
      change.def_.color2_ =
          RageColor::NormalizeColorString(change.def_.color2_);
      [[fallthrough]];
    case 10:
      change.def_.color1_ = bg_change_values[9];
      Replace(change.def_.color1_, '^', ',');
      change.def_.color1_ =
          RageColor::NormalizeColorString(change.def_.color1_);
      [[fallthrough]];
    case 9:
      change.transition_ = bg_change_values[8];
      [[fallthrough]];
    case 8: {
      std::string tmp = bg_change_values[7];
      MakeLower(tmp);
      if ((tmp.find(".ini") != std::string::npos ||
           tmp.find(".xml") != std::string::npos) &&
          !PREFSMAN->quirks_mode_) {
        return false;
      }
      change.def_.file2_ = bg_change_values[7];
      [[fallthrough]];
    }
    case 7:
      change.def_.effect_ = bg_change_values[6];
      [[fallthrough]];
    case 6:
      // param 7 overrides this.
      // Backward compatibility:
      if (change.def_.effect_.empty()) {
        bool loop = StringToInt(bg_change_values[5]) != 0;
        if (!loop) {
          change.def_.effect_ = SBE_StretchNoLoop;
        }
      }
      [[fallthrough]];
    case 5:
      // param 7 overrides this.
      // Backward compatibility:
      if (change.def_.effect_.empty()) {
        bool rewind_movie = StringToInt(bg_change_values[4]) != 0;
        if (rewind_movie) {
          change.def_.effect_ = SBE_StretchRewind;
        }
      }
      [[fallthrough]];
    case 4:
      // param 9 overrides this.
      // Backward compatibility:
      if (change.transition_.empty()) {
        change.transition_ =
            (StringToInt(bg_change_values[3]) != 0) ? "CrossFade" : "";
      }
      [[fallthrough]];
    case 3:
      change.rate_ = StringToFloat(bg_change_values[2]);
      [[fallthrough]];
    case 2: {
      std::string tmp = bg_change_values[1];
      MakeLower(tmp);
      if ((tmp.find(".ini") != std::string::npos ||
           tmp.find(".xml") != std::string::npos) &&
          !PREFSMAN->quirks_mode_) {
        return false;
      }
      change.def_.file1_ = bg_change_values[1];
      [[fallthrough]];
    }
    case 1:
      change.start_beat_ = StringToFloat(bg_change_values[0]);
  }

  return bg_change_values.size() >= 2;
}

bool SMLoader::LoadNoteDataFromSimfile(const std::string& path, Steps& out) {
  MsdFile msd;
  if (!msd.ReadFile(path, true))  // unescape
  {
    LOG->UserLog(
        "Song file", path, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }
  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);

    // The only tag we care about is the #NOTES tag.
    if (value_name == "NOTES" || value_name == "NOTES2") {
      if (num_params < 7) {
        LOG->UserLog(
            "Song file", path,
            "has %d fields in a #NOTES tag, but should have at least 7.",
            num_params);
        continue;
      }

      std::string steps_type = params[1];
      std::string description = params[2];
      std::string difficulty = params[3];

      // HACK?: If this is a .edit fudge the edit difficulty
      if (CompareNoCase(Right(path, 5), ".edit") == 0) {
        difficulty = "edit";
      }

      Trim(steps_type);
      Trim(description);
      Trim(difficulty);
      // Remember our old versions.
      if (CompareNoCase(difficulty, "smaniac") == 0) {
        difficulty = "Challenge";
      }

      /* Handle hacks that originated back when StepMania didn't have
       * Difficulty_Challenge. TODO: Remove the need for said hacks. */
      if (CompareNoCase(difficulty, "hard") == 0) {
        /* HACK: Both SMANIAC and CHALLENGE used to be Difficulty_Hard.
         * They were differentiated via aspecial description.
         * Account for the rogue charts that do this. */
        // HACK: SMANIAC used to be Difficulty_Hard with a special description.
        if (CompareNoCase(description, "smaniac") == 0 ||
            CompareNoCase(description, "challenge") == 0) {
          difficulty = "Challenge";
        }
      }

      if (!(out.steps_type_ == GAMEMAN->StringToStepsType(steps_type) &&
            out.GetDescription() == description &&
            (out.GetDifficulty() == StringToDifficulty(difficulty) ||
             out.GetDifficulty() == OldStyleStringToDifficulty(difficulty)))) {
        continue;
      }

      std::string note_data = params[6];
      Trim(note_data);
      out.SetSMNoteData(note_data);
      out.TidyUpData();
      return true;
    }
  }
  return false;
}

bool SMLoader::LoadFromSimfile(const std::string& path, Song& out) {
  // LOG->Trace( "Song::LoadFromSMFile(%s)", sPath.c_str() );

  MsdFile msd;
  if (!msd.ReadFile(path, true))  // unescape
  {
    LOG->UserLog(
        "Song file", path, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }

  out.song_timing_.file_ = path;
  out.song_file_name_ = path;

  SMSongTagInfo reused_song_info(&*this, &out, path);

  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);

    reused_song_info.params_ = &params;
    song_handler_map_t::iterator handler =
        sm_parser_helper.song_tag_handlers_.find(value_name);
    if (handler != sm_parser_helper.song_tag_handlers_.end()) {
      /* Don't use GetMainAndSubTitlesFromFullTitle; that's only for
       * heuristically splitting other formats that *don't* natively support
       * #SUBTITLE. */
      handler->second(reused_song_info);
    } else if (Left(value_name, strlen("BGCHANGES")) == "BGCHANGES") {
      SMSetBGChanges(reused_song_info);
    } else if (value_name == "NOTES" || value_name == "NOTES2") {
      if (num_params < 7) {
        LOG->UserLog(
            "Song file", path,
            "has %d fields in a #NOTES tag, but should have at least 7.",
            num_params);
        continue;
      }

      std::unique_ptr<Steps> new_notes = out.CreateSteps();
      LoadFromTokens(
          params[1], params[2], params[3], params[4], params[5], params[6],
          *new_notes);

      new_notes->SetFilename(path);
      out.AddSteps(std::move(new_notes));
    } else {
      LOG->UserLog(
          "Song file", path, "has an unexpected value named \"%s\".",
          value_name.c_str());
    }
  }

  // Turn negative time changes into warps
  ProcessBPMsAndStops(
      out.song_timing_, reused_song_info.bpm_changes_, reused_song_info.stops_);

  out.FixupBackgroundChanges();
  return true;
}

bool SMLoader::LoadEditFromFile(
    std::string edit_file_path, ProfileSlot slot, bool add_steps_to_song,
    Song* given_song /* =nullptr */) {
  LOG->Trace("SMLoader::LoadEditFromFile(%s)", edit_file_path.c_str());

  int bytes = FILEMAN->GetFileSizeInBytes(edit_file_path);
  if (bytes > MAX_EDIT_STEPS_SIZE_BYTES) {
    LOG->UserLog(
        "Edit file", edit_file_path,
        "is unreasonably large. It won't be loaded.");
    return false;
  }

  MsdFile msd;
  if (!msd.ReadFile(edit_file_path, true))  // unescape
  {
    LOG->UserLog(
        "Edit file", edit_file_path, "couldn't be opened: %s",
        msd.GetError().c_str());
    return false;
  }

  return LoadEditFromMsd(
      msd, edit_file_path, slot, add_steps_to_song, given_song);
}

bool SMLoader::LoadEditFromMsd(
    const MsdFile& msd, const std::string& edit_file_path, ProfileSlot slot,
    bool add_steps_to_song, Song* given_song /* = nullptr */) {
  Song* song = given_song;

  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);

    // handle the data
    if (value_name == "SONG") {
      if (song) {
        /* LOG->UserLog( "Edit file", sEditFilePath, "has more than one #SONG
        tag." ); return false; */
        // May have been given the song from outside the file. Not worth
        // checking for.
        continue;
      }

      std::string song_full_title = params[1];
      this->SetSongTitle(params[1]);
      Replace(song_full_title, '\\', '/');

      song = SONGMAN->FindSong(song_full_title);
      if (song == nullptr) {
        LOG->UserLog(
            "Edit file", edit_file_path,
            "requires a song \"%s\" that isn't present.",
            song_full_title.c_str());
        return false;
      }

      if (song->GetNumStepsLoadedFromProfile(slot) >=
          MAX_EDITS_PER_SONG_PER_PROFILE) {
        LOG->UserLog(
            "Song file", song_full_title,
            "already has the maximum number of edits allowed for "
            "ProfileSlotP%d.",
            slot + 1);
        return false;
      }
    }

    else if (value_name == "NOTES") {
      if (song == nullptr) {
        LOG->UserLog(
            "Edit file", edit_file_path,
            "doesn't have a #SONG tag preceeding the first #NOTES tag, and is "
            "not in a valid song-specific folder.");
        return false;
      }

      if (num_params < 7) {
        LOG->UserLog(
            "Edit file", edit_file_path,
            "has %d fields in a #NOTES tag, but should have at least 7.",
            num_params);
        continue;
      }

      if (!add_steps_to_song) {
        return true;
      }

      std::unique_ptr<Steps> new_notes = song->CreateSteps();
      LoadFromTokens(
          params[1], params[2], params[3], params[4], params[5], params[6],
          *new_notes);

      new_notes->SetLoadedFromProfile(slot);
      new_notes->SetDifficulty(Difficulty_Edit);
      new_notes->SetFilename(edit_file_path);

      if (song->IsEditAlreadyLoaded(*new_notes)) {
        LOG->UserLog(
            "Edit file", edit_file_path,
            "is a duplicate of another edit that was already loaded.");
        return false;
      }

      song->AddSteps(std::move(new_notes));
      return true;  // Only allow one Steps per edit file!
    } else {
      LOG->UserLog(
          "Edit file", edit_file_path, "has an unexpected value \"%s\".",
          value_name.c_str());
    }
  }

  // Edit had no valid #NOTES sections
  return false;
}

void SMLoader::GetApplicableFiles(
    const std::string& path, std::vector<std::string>& out,
    bool load_autosave) {
  if (load_autosave) {
    GetDirListing(path + std::string("*.ats"), out);
  } else {
    GetDirListing(path + std::string("*" + this->GetFileExtension()), out);
  }
}

std::vector<std::string> SMLoader::GetSongDirFiles(
    const std::string& song_dir) {
  if (!song_dir_files_.empty()) {
    return song_dir_files_;
  }

  ASSERT(!song_dir.empty());

  std::vector<std::string> dirs;
  dirs.push_back(song_dir);

  while (!dirs.empty()) {
    std::string d = dirs.back();
    dirs.pop_back();

    std::vector<std::string> files;
    GetDirListing(d + "*", files, false, true);

    for (const std::string& f : files) {
      if (IsADirectory(f)) {
        dirs.push_back(f + "/");
      }

      song_dir_files_.push_back(f.substr(song_dir.size()));
    }
  }

  return song_dir_files_;
}

void SMLoader::ParseBGChangesString(
    const std::string& s_changes, std::vector<std::vector<std::string>>& add_to,
    const std::string& song_dir) {
  // short circuit: empty string
  if (s_changes.empty()) {
    return;
  }

  // strip newlines (basically operates as both split and join at the same time)
  std::string sChanges;
  size_t start = 0;
  do {
    size_t pos = s_changes.find_first_of("\r\n", start);
    if (std::string::npos == pos) {
      pos = s_changes.size();
    }

    if (pos != start) {
      if ((start == 0) && (pos == s_changes.size())) {
        sChanges = s_changes;
      } else {
        sChanges += s_changes.substr(start, pos - start);
      }
    }
    start = pos + 1;
  } while (start <= s_changes.size());

  // after removing newlines, do we have anything?
  if (sChanges.empty()) {
    return;
  }

  // get the list of possible files/directories for the file parameters
  std::vector<std::string> files = GetSongDirFiles(song_dir);

  start = 0;
  int pnum = 0;
  do {
    switch (pnum) {
      // parameters 1 and 7 can be files or folder names
      case 1:
      case 7: {
        // see if one of the files in the song directory are listed.
        std::string found;
        for (const auto& f : files) {
          // there aren't enough characters for this to match
          if ((sChanges.size() - start) < f.size()) {
            continue;
          }

          // the string itself matches
          if (EqualsNoCase(f, sChanges.substr(start, f.size()).c_str())) {
            size_t nextpos = start + f.size();

            // is this name followed by end-of-string, equals, or comma?
            if ((nextpos == sChanges.size()) || (sChanges[nextpos] == '=') ||
                (sChanges[nextpos] == ',')) {
              found = f;
              break;
            }
          }
        }
        // yes. use that as this parameter, even if it has commas or equals
        // signs in it
        if (!found.empty()) {
          add_to.back().push_back(found);
          start += found.size();
          // the next character should be a comma or equals. skip it
          if (start < sChanges.size()) {
            if (sChanges[start] == '=') {
              ++pnum;
            } else {
              ASSERT(sChanges[start] == ',');
              pnum = 0;
            }
            start += 1;
          }
          // move to the next parameter
          break;
        }
        // deliberate fall-through if not found. treat it as a normal string
        // like before
        [[fallthrough]];
      }
      // everything else should be safe
      default:
        if (0 == pnum) {
          add_to.push_back(
              std::vector<std::string>());  // first value of this set. create
                                            // our vector
        }

        {
          size_t eqpos = sChanges.find('=', start);
          size_t compos = sChanges.find(',', start);

          if ((eqpos == std::string::npos) && (compos == std::string::npos)) {
            // neither = nor , were found in the remainder of the string.
            // consume the rest of the string.
            add_to.back().push_back(sChanges.substr(start));
            start = sChanges.size() + 1;
          } else if (
              (eqpos != std::string::npos) && (compos != std::string::npos)) {
            // both were found. which came first?
            if (eqpos < compos) {
              // equals. consume value and move to next value
              add_to.back().push_back(sChanges.substr(start, eqpos - start));
              start = eqpos + 1;
              ++pnum;
            } else {
              // comma. consume value and move to next set
              add_to.back().push_back(sChanges.substr(start, compos - start));
              start = compos + 1;
              pnum = 0;
            }
          } else if (eqpos != std::string::npos) {
            // found only equals. consume and move on.
            add_to.back().push_back(sChanges.substr(start, eqpos - start));
            start = eqpos + 1;
            ++pnum;
          } else {
            // only foudn comma. consume and move on.
            add_to.back().push_back(sChanges.substr(start, compos - start));
            start = compos + 1;
            pnum = 0;
          }
          break;
        }
    }
  } while (start <= sChanges.size());
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
