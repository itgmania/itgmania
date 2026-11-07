#include "NotesLoaderSSC.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Attack.h"
#include "BackgroundUtil.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "MsdFile.h"  // No JSON here.
#include "NoteAnnotation.h"
#include "NoteTypes.h"
#include "NotesLoaderSM.h"  // For programming shortcuts.
#include "PlayerNumber.h"
#include "RadarValues.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "StdString.h"
#include "Steps.h"
#include "TechCounts.h"
#include "TimingSegments.h"

// Everything from this line to the creation of parser_helper exists to
// speed up parsing by allowing the use of std::map.  All these functions
// are put into a map of function pointers which is used when loading.
// -Kyz
/****************************************************************/
struct StepsTagInfo {
  SSCLoader* loader_;
  Song* song_;
  Steps* steps_;
  TimingData* timing_;
  const MsdFile::value_t* params_;
  const std::string& path_;
  bool has_own_timing_;
  bool ssc_format_;
  bool for_load_edit_;
  StepsTagInfo(SSCLoader* l, Song* s, const std::string& p)
      : loader_(l),
        song_(s),
        path_(p),
        has_own_timing_(false),
        ssc_format_(false),
        for_load_edit_(false) {}
};
struct SongTagInfo {
  SSCLoader* loader_;
  Song* song_;
  const MsdFile::value_t* params_;
  const std::string& path_;
  SongTagInfo(SSCLoader* l, Song* s, const std::string& p)
      : loader_(l), song_(s), path_(p) {}
};
// LoadNoteDataFromSimfile uses LoadNoteDataTagIDs because its parts operate
// on state variables internal to the function.
enum LoadNoteDataTagIDs {
  LNDID_version,
  LNDID_stepstype,
  LNDID_chartname,
  LNDID_description,
  LNDID_difficulty,
  LNDID_meter,
  LNDID_credit,
  LNDID_notes,
  LNDID_notes2,
  LNDID_notedata
};

typedef void (*steps_tag_func_t)(StepsTagInfo& info);
typedef void (*song_tag_func_t)(SongTagInfo& info);

// Functions for song tags go below this line. -Kyz
/****************************************************************/
void SetVersion(SongTagInfo& info) {
  info.song_->version_ = StringToFloat((*info.params_)[1]);
}
void SetTitle(SongTagInfo& info) {
  info.song_->main_title_ = (*info.params_)[1];
  info.loader_->SetSongTitle((*info.params_)[1]);
}
void SetSubtitle(SongTagInfo& info) {
  info.song_->sub_title_ = (*info.params_)[1];
}
void SetArtist(SongTagInfo& info) { info.song_->artist_ = (*info.params_)[1]; }
void SetMainTitleTranslit(SongTagInfo& info) {
  info.song_->main_title_translit_ = (*info.params_)[1];
}
void SetSubtitleTranslit(SongTagInfo& info) {
  info.song_->sub_title_translit_ = (*info.params_)[1];
}
void SetArtistTranslit(SongTagInfo& info) {
  info.song_->artist_translit_ = (*info.params_)[1];
}
void SetGenre(SongTagInfo& info) { info.song_->genre_ = (*info.params_)[1]; }
void SetOrigin(SongTagInfo& info) { info.song_->origin_ = (*info.params_)[1]; }
void SetCredit(SongTagInfo& info) {
  info.song_->credit_ = (*info.params_)[1];
  Trim(info.song_->credit_);
}
void SetBanner(SongTagInfo& info) {
  info.song_->banner_file_ = (*info.params_)[1];
}
void SetBackground(SongTagInfo& info) {
  info.song_->background_file_ = (*info.params_)[1];
}
void SetPreviewVid(SongTagInfo& info) {
  info.song_->preview_vid_file_ = (*info.params_)[1];
}
void SetJacket(SongTagInfo& info) {
  info.song_->jacket_file_ = (*info.params_)[1];
}
void SetCDImage(SongTagInfo& info) {
  info.song_->cd_file_ = (*info.params_)[1];
}
void SetDiscImage(SongTagInfo& info) {
  info.song_->disc_file_ = (*info.params_)[1];
}
void SetLyricsPath(SongTagInfo& info) {
  info.song_->lyrics_file_ = (*info.params_)[1];
}
void SetCDTitle(SongTagInfo& info) {
  info.song_->cd_title_file_ = (*info.params_)[1];
}
void SetMusic(SongTagInfo& info) {
  info.song_->music_file_ = (*info.params_)[1];
}
void SetPreview(SongTagInfo& info) {
  info.song_->preview_file_ = (*info.params_)[1];
}
void SetLastSecondHint(SongTagInfo& info) {
  info.song_->SetSpecifiedLastSecond(StringToFloat((*info.params_)[1]));
}
void SetSampleStart(SongTagInfo& info) {
  info.song_->music_sample_start_seconds_ = HHMMSSToSeconds((*info.params_)[1]);
}
void SetSampleLength(SongTagInfo& info) {
  info.song_->music_sample_length_seconds_ =
      HHMMSSToSeconds((*info.params_)[1]);
}
void SetDisplayBPM(SongTagInfo& info) {
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
void SetSelectable(SongTagInfo& info) {
  if (EqualsNoCase((*info.params_)[1], "YES")) {
    info.song_->selection_display_ = info.song_->SHOW_ALWAYS;
  } else if (EqualsNoCase((*info.params_)[1], "NO")) {
    info.song_->selection_display_ = info.song_->SHOW_NEVER;
  }
  // ROULETTE from 3.9 is no longer in use.
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
void SetBGChanges(SongTagInfo& info) {
  info.loader_->ProcessBGChanges(
      *info.song_, (*info.params_)[0], info.path_, (*info.params_)[1]);
}
void SetFGChanges(SongTagInfo& info) {
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
void SetKeysounds(SongTagInfo& info) {
  std::string keysounds = (*info.params_)[1];
  if (keysounds.length() >= 2 && keysounds.substr(0, 2) == "\\#") {
    keysounds = keysounds.substr(1);
  }
  split(keysounds, ",", info.song_->keysound_file_);
}
void SetAttacks(SongTagInfo& info) {
  info.loader_->ProcessAttackString(
      info.song_->attack_string_, (*info.params_));
  info.loader_->ProcessAttacks(info.song_->attacks_, (*info.params_));
}
void SetOffset(SongTagInfo& info) {
  info.song_->song_timing_.beat0_offset_in_seconds_ =
      StringToFloat((*info.params_)[1]);
}
void SetSongStops(SongTagInfo& info) {
  info.loader_->ProcessStops(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongDelays(SongTagInfo& info) {
  info.loader_->ProcessDelays(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongBPMs(SongTagInfo& info) {
  info.loader_->ProcessBPMs(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongWarps(SongTagInfo& info) {
  info.loader_->ProcessWarps(
      info.song_->song_timing_, (*info.params_)[1], info.song_->version_);
}
void SetSongLabels(SongTagInfo& info) {
  info.loader_->ProcessLabels(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongTimeSignatures(SongTagInfo& info) {
  info.loader_->ProcessTimeSignatures(
      info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongTickCounts(SongTagInfo& info) {
  info.loader_->ProcessTickcounts(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongCombos(SongTagInfo& info) {
  info.loader_->ProcessCombos(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongSpeeds(SongTagInfo& info) {
  info.loader_->ProcessSpeeds(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongScrolls(SongTagInfo& info) {
  info.loader_->ProcessScrolls(info.song_->song_timing_, (*info.params_)[1]);
}
void SetSongFakes(SongTagInfo& info) {
  info.loader_->ProcessFakes(info.song_->song_timing_, (*info.params_)[1]);
}

// Functions for steps tags go below this line. -Kyz
/****************************************************************/
void SetStepsVersion(StepsTagInfo& info) {
  info.song_->version_ = StringToFloat((*info.params_)[1]);
}
void SetChartName(StepsTagInfo& info) {
  std::string name = (*info.params_)[1];
  Trim(name);
  info.steps_->SetChartName(name);
}
void SetStepsType(StepsTagInfo& info) {
  info.steps_->steps_type_ = GAMEMAN->StringToStepsType((*info.params_)[1]);
  info.steps_->steps_type_str_ = (*info.params_)[1];
  info.ssc_format_ = true;
}
void SetChartStyle(StepsTagInfo& info) {
  info.steps_->SetChartStyle((*info.params_)[1]);
  info.ssc_format_ = true;
}
void SetDescription(StepsTagInfo& info) {
  std::string name = (*info.params_)[1];
  Trim(name);
  if (info.song_->version_ < VERSION_CHART_NAME_TAG && !info.for_load_edit_) {
    info.steps_->SetChartName(name);
  } else {
    info.steps_->SetDescription(name);
  }
  info.ssc_format_ = true;
}
void SetDifficulty(StepsTagInfo& info) {
  info.steps_->SetDifficulty(StringToDifficulty((*info.params_)[1]));
  info.ssc_format_ = true;
}
void SetMeter(StepsTagInfo& info) {
  info.steps_->SetMeter(StringToInt((*info.params_)[1]));
  info.ssc_format_ = true;
}
void SetRadarValues(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> values;
    split((*info.params_)[1], ",", values, true);
    // Instead of trying to use the version to figure out how many
    // categories to expect, look at the number of values and split them
    // evenly. -Kyz
    size_t cats_per_player = values.size() / NUM_PlayerNumber;
    RadarValues v[NUM_PLAYERS];
    FOREACH_PlayerNumber(pn) {
      for (size_t i = 0; i < cats_per_player; ++i) {
        v[pn][i] = StringToFloat(values[pn * cats_per_player + i]);
      }
    }
    info.steps_->SetRadarValues(v);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetTechCounts(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> values;
    split((*info.params_)[1], ",", values, true);
    std::size_t cats_per_player = values.size() / NUM_PlayerNumber;
    TechCounts v[NUM_PLAYERS];
    FOREACH_PlayerNumber(pn) {
      for (std::size_t i = 0; i < cats_per_player; ++i) {
        v[pn][i] = StringToFloat(values[pn * cats_per_player + i]);
      }
    }
    info.steps_->SetTechCounts(v);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetNoteAnnotations(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> values_per_player;
    split((*info.params_)[1], "|", values_per_player, true);

    if (values_per_player.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#NOTEANNOTATIONS has more sections (%zu) than possible number of "
          "players (%d)!",
          values_per_player.size(), NUM_PlayerNumber);
    }

    std::vector<NoteAnnotationCache> annotations_per_player;

    for (std::size_t pn = 0;
         pn < values_per_player.size() && pn < NUM_PlayerNumber; pn++) {
      NoteAnnotationCache annotation_cache;
      annotation_cache.compressed_ = values_per_player[pn];
      annotations_per_player.push_back(annotation_cache);
    }
    info.steps_->SetNoteAnnotations(annotations_per_player);
  }
  info.ssc_format_ = true;
}

template <typename T, typename Convert>
static std::vector<T> ParseMeasureValues(
    const std::string& value, Convert convert) {
  std::vector<T> values;
  for (std::size_t start = 0; start < value.size();) {
    std::size_t end = value.find(',', start);
    if (end == std::string::npos) {
      end = value.size();
    }
    if (end > start) {
      values.push_back(convert(value.c_str() + start));
    }
    start = end + 1;
  }
  return values;
}

void SetNpsPerMeasure(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> values_per_player;
    split((*info.params_)[1], "|", values_per_player, true);

    if (values_per_player.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#NPSPERMEASURE has more sections (%zu) than possible number of "
          "players (%d)!",
          values_per_player.size(), NUM_PlayerNumber);
    }

    std::vector<std::vector<float>> nps_per_measures;
    for (std::size_t pn = 0;
         pn < values_per_player.size() && pn < NUM_PlayerNumber; pn++) {
      nps_per_measures.push_back(
          ParseMeasureValues<float>(
              values_per_player[pn], [](const char* p_value) {
                const float f_value = std::strtof(p_value, nullptr);
                return std::isfinite(f_value) ? f_value : 0.0f;
              }));
    }
    info.steps_->SetNpsPerMeasure(nps_per_measures);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetNotesPerMeasure(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> values_per_player;
    split((*info.params_)[1], "|", values_per_player, true);

    if (values_per_player.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#NOTESPERMEASURE has more sections (%zu) than possible number of "
          "players (%d)!",
          values_per_player.size(), NUM_PlayerNumber);
    }

    std::vector<std::vector<int>> notes_per_measures;
    for (std::size_t pn = 0;
         pn < values_per_player.size() && pn < NUM_PlayerNumber; pn++) {
      notes_per_measures.push_back(
          ParseMeasureValues<int>(values_per_player[pn], [](const char* value) {
            return static_cast<int>(std::strtol(value, nullptr, 10));
          }));
    }
    info.steps_->SetNotesPerMeasure(notes_per_measures);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetPeakNps(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> values_per_player;
    split((*info.params_)[1], "|", values_per_player, true);

    if (values_per_player.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#PEAKNPS has more sections (%zu) than possible number of players "
          "(%d)!",
          values_per_player.size(), NUM_PlayerNumber);
    }

    std::vector<float> peak_nps;
    for (std::size_t pn = 0;
         pn < values_per_player.size() && pn < NUM_PlayerNumber; pn++) {
      peak_nps.push_back(StringToFloat(values_per_player[pn]));
    }
    info.steps_->SetPeakNps(peak_nps);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetGrooveStatsHash(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::string value = (*info.params_)[1];
    info.steps_->SetGrooveStatsHash(value);
  }
  info.ssc_format_ = true;
}

void SetGrooveStatsHashVersion(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::string value = (*info.params_)[1];
    int hash_version = StringToInt(value);
    info.steps_->SetGrooveStatsHashVersion(hash_version);
  }
  info.ssc_format_ = true;
}

void SetCredit(StepsTagInfo& info) {
  info.steps_->SetCredit((*info.params_)[1]);
  info.ssc_format_ = true;
}
void SetStepsMusic(StepsTagInfo& info) {
  info.steps_->SetMusicFile((*info.params_)[1]);
}
void SetStepsBPMs(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessBPMs(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsStops(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessStops(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsDelays(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessDelays(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsTimeSignatures(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessTimeSignatures(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsTickCounts(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessTickcounts(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsCombos(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessCombos(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsWarps(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessWarps(
        *info.timing_, (*info.params_)[1], info.song_->version_);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsSpeeds(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessSpeeds(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsScrolls(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessScrolls(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsFakes(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessFakes(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsLabels(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessLabels(*info.timing_, (*info.params_)[1]);
    info.has_own_timing_ = true;
  }
  info.ssc_format_ = true;
}
void SetStepsAttacks(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.loader_->ProcessAttackString(
        info.steps_->GetStepsAttackStrings(), *info.params_);
    info.loader_->ProcessAttacks(info.steps_->GetStepsAttacks(), *info.params_);
  }
}
void SetStepsOffset(StepsTagInfo& info) {
  if (info.song_->version_ >= VERSION_SPLIT_TIMING || info.for_load_edit_) {
    info.timing_->beat0_offset_in_seconds_ = StringToFloat((*info.params_)[1]);
    info.has_own_timing_ = true;
  }
}
void SetStepsDisplayBPM(StepsTagInfo& info) {
  // #DISPLAYBPM:[xxx][xxx:xxx]|[*];
  if ((*info.params_)[1] == "*") {
    info.steps_->SetDisplayBPM(DISPLAY_BPM_RANDOM);
  } else if ((*info.params_)[1] != "") {
    info.steps_->SetDisplayBPM(DISPLAY_BPM_SPECIFIED);
    float min = StringToFloat((*info.params_)[1]);
    info.steps_->SetMinBPM(min);
    if ((*info.params_)[2].empty()) {
      info.steps_->SetMaxBPM(min);
    } else {
      info.steps_->SetMaxBPM(StringToFloat((*info.params_)[2]));
    }
  }
}

typedef std::map<std::string, steps_tag_func_t> steps_handler_map_t;
typedef std::map<std::string, song_tag_func_t> song_handler_map_t;
typedef std::map<std::string, LoadNoteDataTagIDs> load_note_data_handler_map_t;

struct ssc_parser_helper_t {
  steps_handler_map_t steps_tag_handlers_;
  song_handler_map_t song_tag_handlers_;
  load_note_data_handler_map_t load_note_data_handlers_;
  // Unless signed, the comments in this tag list are not by me.  They were
  // moved here when converting from the else if chain. -Kyz
  ssc_parser_helper_t() {
    song_tag_handlers_["VERSION"] = &SetVersion;
    song_tag_handlers_["TITLE"] = &SetTitle;
    song_tag_handlers_["SUBTITLE"] = &SetSubtitle;
    song_tag_handlers_["ARTIST"] = &SetArtist;
    song_tag_handlers_["TITLETRANSLIT"] = &SetMainTitleTranslit;
    song_tag_handlers_["SUBTITLETRANSLIT"] = &SetSubtitleTranslit;
    song_tag_handlers_["ARTISTTRANSLIT"] = &SetArtistTranslit;
    song_tag_handlers_["GENRE"] = &SetGenre;
    song_tag_handlers_["ORIGIN"] = &SetOrigin;
    song_tag_handlers_["CREDIT"] = &SetCredit;
    song_tag_handlers_["BANNER"] = &SetBanner;
    song_tag_handlers_["BACKGROUND"] = &SetBackground;
    song_tag_handlers_["PREVIEWVID"] = &SetPreviewVid;
    song_tag_handlers_["JACKET"] = &SetJacket;
    song_tag_handlers_["CDIMAGE"] = &SetCDImage;
    song_tag_handlers_["DISCIMAGE"] = &SetDiscImage;
    song_tag_handlers_["LYRICSPATH"] = &SetLyricsPath;
    song_tag_handlers_["CDTITLE"] = &SetCDTitle;
    song_tag_handlers_["MUSIC"] = &SetMusic;
    song_tag_handlers_["PREVIEW"] = &SetPreview;
    song_tag_handlers_["LASTSECONDHINT"] = &SetLastSecondHint;
    song_tag_handlers_["SAMPLESTART"] = &SetSampleStart;
    song_tag_handlers_["SAMPLELENGTH"] = &SetSampleLength;
    song_tag_handlers_["DISPLAYBPM"] = &SetDisplayBPM;
    song_tag_handlers_["SELECTABLE"] = &SetSelectable;
    // It's a bit odd to have the tag that exists for backwards compatibility
    // in this list and not the replacement, but the BGCHANGES tag has a
    // number on the end, allowing up to NUM_BackgroundLayer tags, so it
    // can't fit in the map. -Kyz
    song_tag_handlers_["ANIMATIONS"] = &SetBGChanges;
    song_tag_handlers_["FGCHANGES"] = &SetFGChanges;
    song_tag_handlers_["KEYSOUNDS"] = &SetKeysounds;
    song_tag_handlers_["ATTACKS"] = &SetAttacks;
    song_tag_handlers_["OFFSET"] = &SetOffset;
    /* Below are the song based timings that should only be used
     * if the steps do not have their own timing. */
    song_tag_handlers_["STOPS"] = &SetSongStops;
    song_tag_handlers_["DELAYS"] = &SetSongDelays;
    song_tag_handlers_["BPMS"] = &SetSongBPMs;
    song_tag_handlers_["WARPS"] = &SetSongWarps;
    song_tag_handlers_["LABELS"] = &SetSongLabels;
    song_tag_handlers_["TIMESIGNATURES"] = &SetSongTimeSignatures;
    song_tag_handlers_["TICKCOUNTS"] = &SetSongTickCounts;
    song_tag_handlers_["COMBOS"] = &SetSongCombos;
    song_tag_handlers_["SPEEDS"] = &SetSongSpeeds;
    song_tag_handlers_["SCROLLS"] = &SetSongScrolls;
    song_tag_handlers_["FAKES"] = &SetSongFakes;
    /* Tags that no longer exist, listed for posterity.  May their names
     * never be forgotten for their service to Stepmania. -Kyz
     * LASTBEATHINT: // unable to parse due to tag position. Ignore.
     * MUSICBYTES: // ignore
     * FIRSTBEAT: // no longer used.
     * LASTBEAT: // no longer used.
     */

    steps_tag_handlers_["VERSION"] = &SetStepsVersion;
    steps_tag_handlers_["CHARTNAME"] = &SetChartName;
    steps_tag_handlers_["STEPSTYPE"] = &SetStepsType;
    steps_tag_handlers_["CHARTSTYLE"] = &SetChartStyle;
    steps_tag_handlers_["DESCRIPTION"] = &SetDescription;
    steps_tag_handlers_["DIFFICULTY"] = &SetDifficulty;
    steps_tag_handlers_["METER"] = &SetMeter;
    steps_tag_handlers_["RADARVALUES"] = &SetRadarValues;
    steps_tag_handlers_["CREDIT"] = &SetCredit;
    steps_tag_handlers_["MUSIC"] = &SetStepsMusic;
    steps_tag_handlers_["BPMS"] = &SetStepsBPMs;
    steps_tag_handlers_["STOPS"] = &SetStepsStops;
    steps_tag_handlers_["DELAYS"] = &SetStepsDelays;
    steps_tag_handlers_["TIMESIGNATURES"] = &SetStepsTimeSignatures;
    steps_tag_handlers_["TICKCOUNTS"] = &SetStepsTickCounts;
    steps_tag_handlers_["COMBOS"] = &SetStepsCombos;
    steps_tag_handlers_["WARPS"] = &SetStepsWarps;
    steps_tag_handlers_["SPEEDS"] = &SetStepsSpeeds;
    steps_tag_handlers_["SCROLLS"] = &SetStepsScrolls;
    steps_tag_handlers_["FAKES"] = &SetStepsFakes;
    steps_tag_handlers_["LABELS"] = &SetStepsLabels;
    steps_tag_handlers_["TECHCOUNTS"] = &SetTechCounts;
    steps_tag_handlers_["NPSPERMEASURE"] = &SetNpsPerMeasure;
    steps_tag_handlers_["NOTESPERMEASURE"] = &SetNotesPerMeasure;
    steps_tag_handlers_["PEAKNPS"] = &SetPeakNps;
    steps_tag_handlers_["NOTEANNOTATIONS"] = &SetNoteAnnotations;
    steps_tag_handlers_["GROOVESTATSHASH"] = &SetGrooveStatsHash;
    steps_tag_handlers_["GROOVESTATSHASHVERSION"] = &SetGrooveStatsHashVersion;

    /* If this is called, the chart does not use the same attacks
     * as the Song's timing. No other changes are required. */
    steps_tag_handlers_["ATTACKS"] = &SetStepsAttacks;
    steps_tag_handlers_["OFFSET"] = &SetStepsOffset;
    steps_tag_handlers_["DISPLAYBPM"] = &SetStepsDisplayBPM;

    load_note_data_handlers_["VERSION"] = LNDID_version;
    load_note_data_handlers_["STEPSTYPE"] = LNDID_stepstype;
    load_note_data_handlers_["CHARTNAME"] = LNDID_chartname;
    load_note_data_handlers_["DESCRIPTION"] = LNDID_description;
    load_note_data_handlers_["DIFFICULTY"] = LNDID_difficulty;
    load_note_data_handlers_["METER"] = LNDID_meter;
    load_note_data_handlers_["CREDIT"] = LNDID_credit;
    load_note_data_handlers_["NOTES"] = LNDID_notes;
    load_note_data_handlers_["NOTES2"] = LNDID_notes2;
    load_note_data_handlers_["NOTEDATA"] = LNDID_notedata;
  }
};
ssc_parser_helper_t parser_helper;
// End parser_helper related functions. -Kyz
/****************************************************************/

void SSCLoader::ProcessBPMs(TimingData& out, const std::string param) {
  std::vector<std::string> array_bpm_expressions;
  split(param, ",", array_bpm_expressions);

  for (unsigned b = 0; b < array_bpm_expressions.size(); b++) {
    std::vector<std::string> array_bpm_values;
    Trim(array_bpm_expressions[b]);
    if (array_bpm_expressions[b].empty()) {
      continue;
    }
    split(array_bpm_expressions[b], "=", array_bpm_values);
    if (array_bpm_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #BPMS value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_bpm_expressions[b].c_str());
      continue;
    }

    const float beat = StringToFloat(array_bpm_values[0]);
    const float new_bpm = StringToFloat(array_bpm_values[1]);
    if (beat >= 0 && new_bpm > 0) {
      out.AddSegment(BPMSegment(BeatToNoteRow(beat), new_bpm));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid BPM at beat %f, BPM %f.", beat, new_bpm);
    }
  }
}

void SSCLoader::ProcessStops(TimingData& out, const std::string param) {
  std::vector<std::string> array_stop_expressions;
  split(param, ",", array_stop_expressions);

  for (unsigned b = 0; b < array_stop_expressions.size(); b++) {
    std::vector<std::string> array_stop_values;
    Trim(array_stop_expressions[b]);
    if (array_stop_expressions[b].empty()) {
      continue;
    }
    split(array_stop_expressions[b], "=", array_stop_values);
    if (array_stop_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #STOPS value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_stop_expressions[b].c_str());
      continue;
    }

    const float beat = StringToFloat(array_stop_values[0]);
    const float new_stop = StringToFloat(array_stop_values[1]);
    if (beat >= 0 && new_stop > 0) {
      out.AddSegment(StopSegment(BeatToNoteRow(beat), new_stop));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Stop at beat %f, length %f.", beat, new_stop);
    }
  }
}

void SSCLoader::ProcessWarps(
    TimingData& out, const std::string param, const float version) {
  std::vector<std::string> array_warp_expressions;
  split(param, ",", array_warp_expressions);

  for (unsigned b = 0; b < array_warp_expressions.size(); b++) {
    std::vector<std::string> array_warp_values;
    Trim(array_warp_expressions[b]);
    if (array_warp_expressions[b].empty()) {
      continue;
    }
    split(array_warp_expressions[b], "=", array_warp_values);
    if (array_warp_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #WARPS value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_warp_expressions[b].c_str());
      continue;
    }

    const float beat = StringToFloat(array_warp_values[0]);
    const float new_beat = StringToFloat(array_warp_values[1]);
    // Early versions were absolute in beats. They should be relative.
    if ((version < VERSION_SPLIT_TIMING && new_beat > beat)) {
      out.AddSegment(WarpSegment(BeatToNoteRow(beat), new_beat - beat));
    } else if (new_beat > 0) {
      out.AddSegment(WarpSegment(BeatToNoteRow(beat), new_beat));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Warp at beat %f, BPM %f.", beat, new_beat);
    }
  }
}

void SSCLoader::ProcessLabels(TimingData& out, const std::string param) {
  std::vector<std::string> array_label_expressions;
  split(param, ",", array_label_expressions);

  for (unsigned b = 0; b < array_label_expressions.size(); b++) {
    std::vector<std::string> array_label_values;
    Trim(array_label_expressions[b]);
    if (array_label_expressions[b].empty()) {
      continue;
    }
    split(array_label_expressions[b], "=", array_label_values);
    if (array_label_values.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #LABELS value \"%s\" (must have exactly one '='), "
          "ignored.",
          array_label_expressions[b].c_str());
      continue;
    }

    const float beat = StringToFloat(array_label_values[0]);
    std::string label = array_label_values[1];
    TrimRight(label);
    if (beat >= 0.0f) {
      out.AddSegment(LabelSegment(BeatToNoteRow(beat), label));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Label at beat %f called %s.", beat, label.c_str());
    }
  }
}

void SSCLoader::ProcessCombos(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> array_combo_expressions;
  split(line, ",", array_combo_expressions);

  for (unsigned f = 0; f < array_combo_expressions.size(); f++) {
    std::vector<std::string> array_combo_values;
    split(array_combo_expressions[f], "=", array_combo_values);
    unsigned size = array_combo_values.size();
    if (size < 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #COMBOS value \"%s\" (must have at least one '='), "
          "ignored.",
          array_combo_expressions[f].c_str());
      continue;
    }
    const float combo_beat = StringToFloat(array_combo_values[0]);
    const int combos = StringToInt(array_combo_values[1]);
    const int misses =
        (size == 2 ? combos : StringToInt(array_combo_values[2]));
    out.AddSegment(ComboSegment(BeatToNoteRow(combo_beat), combos, misses));
  }
}

void SSCLoader::ProcessScrolls(TimingData& out, const std::string param) {
  std::vector<std::string> vs1;
  split(param, ",", vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    split(s1, "=", vs2);

    if (vs2.size() < 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an scroll change with %i values.", static_cast<int>(vs2.size()));
      continue;
    }

    const float beat = StringToFloat(vs2[0]);
    const float ratio = StringToFloat(vs2[1]);

    if (beat < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an scroll change with beat %f.", beat);
      continue;
    }

    out.AddSegment(ScrollSegment(BeatToNoteRow(beat), ratio));
  }
}

bool SSCLoader::LoadNoteDataFromSimfile(const std::string& path, Steps& out) {
  LOG->Trace("Loading notes from %s", path.c_str());

  MsdFile msd;
  if (!msd.ReadFile(path, true)) {
    LOG->UserLog(
        "Unable to load any notes from", path, "for this reason: %s",
        msd.GetError().c_str());
    return false;
  }

  bool trying_steps = false;
  float stored_version = 0;
  const unsigned values = msd.GetNumValues();

  for (unsigned i = 0; i < values; i++) {
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);
    std::string matcher = params[1];  // mainly for debugging.
    Trim(matcher);

    load_note_data_handler_map_t::iterator handler =
        parser_helper.load_note_data_handlers_.find(value_name);
    if (handler != parser_helper.load_note_data_handlers_.end()) {
      if (trying_steps) {
        switch (handler->second) {
          case LNDID_version:
            // Note that version is in both switches.  Formerly, it was
            // checked before the tryingSteps condition. -Kyz
            stored_version = StringToFloat(matcher);
            break;
          case LNDID_stepstype:
            if (out.steps_type_ != GAMEMAN->StringToStepsType(matcher)) {
              trying_steps = false;
            }
            break;
          case LNDID_chartname:
            if (stored_version >= VERSION_CHART_NAME_TAG &&
                out.GetChartName() != matcher) {
              trying_steps = false;
            }
            break;
          case LNDID_description:
            if (stored_version < VERSION_CHART_NAME_TAG) {
              if (out.GetChartName() != matcher) {
                trying_steps = false;
              }
            } else if (out.GetDescription() != matcher) {
              trying_steps = false;
            }
            break;
          case LNDID_difficulty:
            // Accept any difficulty if it's an edit because LoadEditFromMsd
            // forces edits onto Edit difficulty even if they have a difficulty
            // tag. -Kyz
            if (out.GetDifficulty() != StringToDifficulty(matcher) &&
                !(out.GetDifficulty() == Difficulty_Edit &&
                  MakeLower(GetExtension(path)) == "edit")) {
              trying_steps = false;
            }
            break;
          case LNDID_meter:
            if (out.GetMeter() != StringToInt(matcher)) {
              trying_steps = false;
            }
            break;
          case LNDID_credit:
            if (out.GetCredit() != matcher) {
              trying_steps = false;
            }
            break;
          case LNDID_notes:
          case LNDID_notes2:
            out.SetSMNoteData(matcher);
            out.TidyUpData();
            return true;
          default:
            break;
        }
      } else {
        switch (handler->second) {
          case LNDID_version:
            // Note that version is in both switches.  Formerly, it was
            // checked before the tryingSteps condition. -Kyz
            stored_version = StringToFloat(matcher);
            break;
          case LNDID_notedata:
            trying_steps = true;
            break;
          default:
            break;
        }
      }
    } else {
      // Silently ignore unrecognized tags, as was done before. -Kyz
    }
  }
  return false;
}

bool SSCLoader::LoadFromSimfile(const std::string& path, Song& out) {
  // LOG->Trace( "Song::LoadFromSSCFile(%s)", sPath.c_str() );

  MsdFile msd;
  if (!msd.ReadFile(path, true)) {
    LOG->UserLog(
        "Song file", path, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }

  out.song_timing_.file_ = path;  // songs still have their fallback timing.
  out.song_file_name_ = path;

  int state = GETTING_SONG_INFO;
  const unsigned values = msd.GetNumValues();
  Steps* new_notes = nullptr;
  TimingData steps_timing;

  SongTagInfo reused_song_info(&*this, &out, path);
  StepsTagInfo reused_steps_info(&*this, &out, path);

  for (unsigned i = 0; i < values; i++) {
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);

    switch (state) {
      case GETTING_SONG_INFO: {
        reused_song_info.params_ = &params;
        song_handler_map_t::iterator handler =
            parser_helper.song_tag_handlers_.find(value_name);
        if (handler != parser_helper.song_tag_handlers_.end()) {
          handler->second(reused_song_info);
        } else if (Left(value_name, strlen("BGCHANGES")) == "BGCHANGES") {
          SetBGChanges(reused_song_info);
        }
        // This tag will get us to the next section.
        else if (value_name == "NOTEDATA") {
          state = GETTING_STEP_INFO;
          new_notes = out.CreateSteps();
          steps_timing = TimingData(out.song_timing_.beat0_offset_in_seconds_);
          reused_steps_info.has_own_timing_ = false;
          reused_steps_info.steps_ = new_notes;
          reused_steps_info.timing_ = &steps_timing;
        } else {
          // Silently ignore unrecognized tags, as was done before. -Kyz
        }
        break;
      }
      case GETTING_STEP_INFO: {
        reused_steps_info.params_ = &params;
        steps_handler_map_t::iterator handler =
            parser_helper.steps_tag_handlers_.find(value_name);
        if (handler != parser_helper.steps_tag_handlers_.end()) {
          handler->second(reused_steps_info);
        } else if (value_name == "NOTES" || value_name == "NOTES2") {
          state = GETTING_SONG_INFO;
          if (reused_steps_info.has_own_timing_) {
            new_notes->timing_ = steps_timing;
          }
          reused_steps_info.has_own_timing_ = false;
          new_notes->SetSMNoteData(params[1]);
          new_notes->TidyUpData();
          new_notes->SetFilename(path);
          out.AddSteps(new_notes);
        } else {
          // Silently ignore unrecognized tags, as was done before. -Kyz
        }
        break;
      }
    }
  }
  out.version_ = STEPFILE_VERSION_NUMBER;
  out.FixupBackgroundChanges();
  return true;
}

bool SSCLoader::LoadEditFromFile(
    std::string edit_file_path, ProfileSlot slot, bool add_steps_to_song,
    Song* given_song /* = nullptr */) {
  LOG->Trace("SSCLoader::LoadEditFromFile(%s)", edit_file_path.c_str());

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

bool SSCLoader::LoadEditFromMsd(
    const MsdFile& msd, const std::string& edit_file_path, ProfileSlot slot,
    bool add_steps_to_song, Song* given_song /* = nullptr */) {
  Song* song = given_song;
  Steps* new_notes = nullptr;
  TimingData steps_timing;

  StepsTagInfo reused_steps_info(&*this, song, edit_file_path);
  reused_steps_info.for_load_edit_ = true;
  reused_steps_info.timing_ = &steps_timing;

  for (unsigned int i = 0; i < msd.GetNumValues(); ++i) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);

    if (song != nullptr) {
      reused_steps_info.params_ = &params;
      steps_handler_map_t::iterator handler =
          parser_helper.steps_tag_handlers_.find(value_name);
      if (new_notes != nullptr &&
          handler != parser_helper.steps_tag_handlers_.end()) {
        handler->second(reused_steps_info);
      } else if (value_name == "NOTEDATA") {
        new_notes = song->CreateSteps();
        reused_steps_info.steps_ = new_notes;
        reused_steps_info.ssc_format_ = true;
      } else if (value_name == "NOTES") {
        if (song == nullptr) {
          LOG->UserLog(
              "Edit file", edit_file_path,
              "doesn't have a #SONG tag preceeding the first #NOTES tag,"
              " and is not in a valid song-specific folder.");
          return false;
        }

        if (!reused_steps_info.ssc_format_ && num_params < 7) {
          LOG->UserLog(
              "Edit file", edit_file_path,
              "has %d fields in a #NOTES tag, but should have at least 7.",
              num_params);
          continue;
        }

        // I have no idea what the purpose of this bAddStepsToSong flag is.
        // It looks like it causes LoadEditFromMsd to just throw away
        // whatever steps data was loaded, possibly leaking the memory used
        // by pNewNotes.  It was here before I rewrote LoadEditFromMsd to
        // use a map instead of an else if chain, so I preserved it. -Kyz
        if (!add_steps_to_song) {
          return true;
        }

        // Force the difficulty to edit in case the edit set its own
        // difficulty because IsEditAlreadyLoaded has an assert and edits
        // shouldn't be able to add charts of other difficulties. -Kyz
        if (new_notes != nullptr) {
          new_notes->SetDifficulty(Difficulty_Edit);
          if (song->IsEditAlreadyLoaded(new_notes)) {
            LOG->UserLog(
                "Edit file", edit_file_path,
                "is a duplicate of another edit that was already loaded.");
            RageUtil::SafeDelete(new_notes);
            return false;
          }
        }

        if (reused_steps_info.ssc_format_) {
          if (reused_steps_info.has_own_timing_) {
            new_notes->timing_ = steps_timing;
          }
          new_notes->SetSMNoteData(params[1]);
          new_notes->TidyUpData();
        } else {
          new_notes = song->CreateSteps();
          LoadFromTokens(
              params[1], params[2], params[3], params[4], params[5], params[6],
              *new_notes);
        }

        new_notes->SetLoadedFromProfile(slot);
        new_notes->SetDifficulty(Difficulty_Edit);
        new_notes->SetFilename(edit_file_path);

        song->AddSteps(new_notes);
        return true;  // Only allow one Steps per edit file!
      } else {
        LOG->UserLog(
            "Edit file", edit_file_path, "has an unexpected value \"%s\".",
            value_name.c_str());
      }
    } else {
      if (value_name == "SONG") {
        if (song) {
          /* LOG->UserLog("Edit file", sEditFilePath, "has more than one #SONG
             tag."); return false; */
          continue;
        }

        std::string song_full_title = params[1];
        this->SetSongTitle(params[1]);
        Replace(song_full_title, '\\', '/');
        song = SONGMAN->FindSong(song_full_title);
        reused_steps_info.song_ = song;
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
        reused_steps_info.song_ = song;
      }
    }
  }
  // Edit had no valid #NOTES sections
  return false;
}

/*
 * (c) 2011 Jason Felds
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
