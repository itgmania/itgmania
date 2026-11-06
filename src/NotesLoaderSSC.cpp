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
  std::vector<std::vector<std::string>> aFGChanges;
  info.loader_->ParseBGChangesString(
      (*info.params_)[1], aFGChanges, info.song_->GetSongDir());

  for (const auto& b : aFGChanges) {
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
    std::vector<std::string> valuesPerPlayer;
    split((*info.params_)[1], "|", valuesPerPlayer, true);

    if (valuesPerPlayer.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#NOTEANNOTATIONS has more sections (%zu) than possible number of "
          "players (%d)!",
          valuesPerPlayer.size(), NUM_PlayerNumber);
    }

    std::vector<NoteAnnotationCache> annotationsPerPlayer;

    for (std::size_t pn = 0;
         pn < valuesPerPlayer.size() && pn < NUM_PlayerNumber; pn++) {
      NoteAnnotationCache annotationCache;
      annotationCache.compressed_ = valuesPerPlayer[pn];
      annotationsPerPlayer.push_back(annotationCache);
    }
    info.steps_->SetNoteAnnotations(annotationsPerPlayer);
  }
  info.ssc_format_ = true;
}

template <typename T, typename Convert>
static std::vector<T> ParseMeasureValues(
    const std::string& sValue, Convert convert) {
  std::vector<T> values;
  for (std::size_t start = 0; start < sValue.size();) {
    std::size_t end = sValue.find(',', start);
    if (end == std::string::npos) {
      end = sValue.size();
    }
    if (end > start) {
      values.push_back(convert(sValue.c_str() + start));
    }
    start = end + 1;
  }
  return values;
}

void SetNpsPerMeasure(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> valuesPerPlayer;
    split((*info.params_)[1], "|", valuesPerPlayer, true);

    if (valuesPerPlayer.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#NPSPERMEASURE has more sections (%zu) than possible number of "
          "players (%d)!",
          valuesPerPlayer.size(), NUM_PlayerNumber);
    }

    std::vector<std::vector<float>> npsPerMeasures;
    for (std::size_t pn = 0;
         pn < valuesPerPlayer.size() && pn < NUM_PlayerNumber; pn++) {
      npsPerMeasures.push_back(
          ParseMeasureValues<float>(
              valuesPerPlayer[pn], [](const char* pValue) {
                const float fValue = std::strtof(pValue, nullptr);
                return std::isfinite(fValue) ? fValue : 0.0f;
              }));
    }
    info.steps_->SetNpsPerMeasure(npsPerMeasures);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetNotesPerMeasure(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> valuesPerPlayer;
    split((*info.params_)[1], "|", valuesPerPlayer, true);

    if (valuesPerPlayer.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#NOTESPERMEASURE has more sections (%zu) than possible number of "
          "players (%d)!",
          valuesPerPlayer.size(), NUM_PlayerNumber);
    }

    std::vector<std::vector<int>> notesPerMeasures;
    for (std::size_t pn = 0;
         pn < valuesPerPlayer.size() && pn < NUM_PlayerNumber; pn++) {
      notesPerMeasures.push_back(
          ParseMeasureValues<int>(valuesPerPlayer[pn], [](const char* pValue) {
            return static_cast<int>(std::strtol(pValue, nullptr, 10));
          }));
    }
    info.steps_->SetNotesPerMeasure(notesPerMeasures);
  } else {
    // just recalc at time.
  }
  info.ssc_format_ = true;
}

void SetPeakNps(StepsTagInfo& info) {
  if (info.for_load_edit_) {
    std::vector<std::string> valuesPerPlayer;
    split((*info.params_)[1], "|", valuesPerPlayer, true);

    if (valuesPerPlayer.size() > NUM_PlayerNumber) {
      LOG->Warn(
          "#PEAKNPS has more sections (%zu) than possible number of players "
          "(%d)!",
          valuesPerPlayer.size(), NUM_PlayerNumber);
    }

    std::vector<float> peakNps;
    for (std::size_t pn = 0;
         pn < valuesPerPlayer.size() && pn < NUM_PlayerNumber; pn++) {
      peakNps.push_back(StringToFloat(valuesPerPlayer[pn]));
    }
    info.steps_->SetPeakNps(peakNps);
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
    int hashVersion = StringToInt(value);
    info.steps_->SetGrooveStatsHashVersion(hashVersion);
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

void SSCLoader::ProcessBPMs(TimingData& out, const std::string sParam) {
  std::vector<std::string> arrayBPMExpressions;
  split(sParam, ",", arrayBPMExpressions);

  for (unsigned b = 0; b < arrayBPMExpressions.size(); b++) {
    std::vector<std::string> arrayBPMValues;
    Trim(arrayBPMExpressions[b]);
    if (arrayBPMExpressions[b].empty()) {
      continue;
    }
    split(arrayBPMExpressions[b], "=", arrayBPMValues);
    if (arrayBPMValues.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #BPMS value \"%s\" (must have exactly one '='), "
          "ignored.",
          arrayBPMExpressions[b].c_str());
      continue;
    }

    const float fBeat = StringToFloat(arrayBPMValues[0]);
    const float fNewBPM = StringToFloat(arrayBPMValues[1]);
    if (fBeat >= 0 && fNewBPM > 0) {
      out.AddSegment(BPMSegment(BeatToNoteRow(fBeat), fNewBPM));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid BPM at beat %f, BPM %f.", fBeat, fNewBPM);
    }
  }
}

void SSCLoader::ProcessStops(TimingData& out, const std::string sParam) {
  std::vector<std::string> arrayStopExpressions;
  split(sParam, ",", arrayStopExpressions);

  for (unsigned b = 0; b < arrayStopExpressions.size(); b++) {
    std::vector<std::string> arrayStopValues;
    Trim(arrayStopExpressions[b]);
    if (arrayStopExpressions[b].empty()) {
      continue;
    }
    split(arrayStopExpressions[b], "=", arrayStopValues);
    if (arrayStopValues.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #STOPS value \"%s\" (must have exactly one '='), "
          "ignored.",
          arrayStopExpressions[b].c_str());
      continue;
    }

    const float fBeat = StringToFloat(arrayStopValues[0]);
    const float fNewStop = StringToFloat(arrayStopValues[1]);
    if (fBeat >= 0 && fNewStop > 0) {
      out.AddSegment(StopSegment(BeatToNoteRow(fBeat), fNewStop));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Stop at beat %f, length %f.", fBeat, fNewStop);
    }
  }
}

void SSCLoader::ProcessWarps(
    TimingData& out, const std::string sParam, const float fVersion) {
  std::vector<std::string> arrayWarpExpressions;
  split(sParam, ",", arrayWarpExpressions);

  for (unsigned b = 0; b < arrayWarpExpressions.size(); b++) {
    std::vector<std::string> arrayWarpValues;
    Trim(arrayWarpExpressions[b]);
    if (arrayWarpExpressions[b].empty()) {
      continue;
    }
    split(arrayWarpExpressions[b], "=", arrayWarpValues);
    if (arrayWarpValues.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #WARPS value \"%s\" (must have exactly one '='), "
          "ignored.",
          arrayWarpExpressions[b].c_str());
      continue;
    }

    const float fBeat = StringToFloat(arrayWarpValues[0]);
    const float fNewBeat = StringToFloat(arrayWarpValues[1]);
    // Early versions were absolute in beats. They should be relative.
    if ((fVersion < VERSION_SPLIT_TIMING && fNewBeat > fBeat)) {
      out.AddSegment(WarpSegment(BeatToNoteRow(fBeat), fNewBeat - fBeat));
    } else if (fNewBeat > 0) {
      out.AddSegment(WarpSegment(BeatToNoteRow(fBeat), fNewBeat));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Warp at beat %f, BPM %f.", fBeat, fNewBeat);
    }
  }
}

void SSCLoader::ProcessLabels(TimingData& out, const std::string sParam) {
  std::vector<std::string> arrayLabelExpressions;
  split(sParam, ",", arrayLabelExpressions);

  for (unsigned b = 0; b < arrayLabelExpressions.size(); b++) {
    std::vector<std::string> arrayLabelValues;
    Trim(arrayLabelExpressions[b]);
    if (arrayLabelExpressions[b].empty()) {
      continue;
    }
    split(arrayLabelExpressions[b], "=", arrayLabelValues);
    if (arrayLabelValues.size() != 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #LABELS value \"%s\" (must have exactly one '='), "
          "ignored.",
          arrayLabelExpressions[b].c_str());
      continue;
    }

    const float fBeat = StringToFloat(arrayLabelValues[0]);
    std::string sLabel = arrayLabelValues[1];
    TrimRight(sLabel);
    if (fBeat >= 0.0f) {
      out.AddSegment(LabelSegment(BeatToNoteRow(fBeat), sLabel));
    } else {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid Label at beat %f called %s.", fBeat, sLabel.c_str());
    }
  }
}

void SSCLoader::ProcessCombos(
    TimingData& out, const std::string line, const int rowsPerBeat) {
  std::vector<std::string> arrayComboExpressions;
  split(line, ",", arrayComboExpressions);

  for (unsigned f = 0; f < arrayComboExpressions.size(); f++) {
    std::vector<std::string> arrayComboValues;
    split(arrayComboExpressions[f], "=", arrayComboValues);
    unsigned size = arrayComboValues.size();
    if (size < 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #COMBOS value \"%s\" (must have at least one '='), "
          "ignored.",
          arrayComboExpressions[f].c_str());
      continue;
    }
    const float fComboBeat = StringToFloat(arrayComboValues[0]);
    const int iCombos = StringToInt(arrayComboValues[1]);
    const int iMisses =
        (size == 2 ? iCombos : StringToInt(arrayComboValues[2]));
    out.AddSegment(ComboSegment(BeatToNoteRow(fComboBeat), iCombos, iMisses));
  }
}

void SSCLoader::ProcessScrolls(TimingData& out, const std::string sParam) {
  std::vector<std::string> vs1;
  split(sParam, ",", vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    split(s1, "=", vs2);

    if (vs2.size() < 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an scroll change with %i values.", static_cast<int>(vs2.size()));
      continue;
    }

    const float fBeat = StringToFloat(vs2[0]);
    const float fRatio = StringToFloat(vs2[1]);

    if (fBeat < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an scroll change with beat %f.", fBeat);
      continue;
    }

    out.AddSegment(ScrollSegment(BeatToNoteRow(fBeat), fRatio));
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

  bool tryingSteps = false;
  float storedVersion = 0;
  const unsigned values = msd.GetNumValues();

  for (unsigned i = 0; i < values; i++) {
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string valueName = params[0];
    MakeUpper(valueName);
    std::string matcher = params[1];  // mainly for debugging.
    Trim(matcher);

    load_note_data_handler_map_t::iterator handler =
        parser_helper.load_note_data_handlers_.find(valueName);
    if (handler != parser_helper.load_note_data_handlers_.end()) {
      if (tryingSteps) {
        switch (handler->second) {
          case LNDID_version:
            // Note that version is in both switches.  Formerly, it was
            // checked before the tryingSteps condition. -Kyz
            storedVersion = StringToFloat(matcher);
            break;
          case LNDID_stepstype:
            if (out.steps_type_ != GAMEMAN->StringToStepsType(matcher)) {
              tryingSteps = false;
            }
            break;
          case LNDID_chartname:
            if (storedVersion >= VERSION_CHART_NAME_TAG &&
                out.GetChartName() != matcher) {
              tryingSteps = false;
            }
            break;
          case LNDID_description:
            if (storedVersion < VERSION_CHART_NAME_TAG) {
              if (out.GetChartName() != matcher) {
                tryingSteps = false;
              }
            } else if (out.GetDescription() != matcher) {
              tryingSteps = false;
            }
            break;
          case LNDID_difficulty:
            // Accept any difficulty if it's an edit because LoadEditFromMsd
            // forces edits onto Edit difficulty even if they have a difficulty
            // tag. -Kyz
            if (out.GetDifficulty() != StringToDifficulty(matcher) &&
                !(out.GetDifficulty() == Difficulty_Edit &&
                  MakeLower(GetExtension(path)) == "edit")) {
              tryingSteps = false;
            }
            break;
          case LNDID_meter:
            if (out.GetMeter() != StringToInt(matcher)) {
              tryingSteps = false;
            }
            break;
          case LNDID_credit:
            if (out.GetCredit() != matcher) {
              tryingSteps = false;
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
            storedVersion = StringToFloat(matcher);
            break;
          case LNDID_notedata:
            tryingSteps = true;
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

bool SSCLoader::LoadFromSimfile(const std::string& sPath, Song& out) {
  // LOG->Trace( "Song::LoadFromSSCFile(%s)", sPath.c_str() );

  MsdFile msd;
  if (!msd.ReadFile(sPath, true)) {
    LOG->UserLog(
        "Song file", sPath, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }

  out.song_timing_.file_ = sPath;  // songs still have their fallback timing.
  out.song_file_name_ = sPath;

  int state = GETTING_SONG_INFO;
  const unsigned values = msd.GetNumValues();
  Steps* pNewNotes = nullptr;
  TimingData stepsTiming;

  SongTagInfo reused_song_info(&*this, &out, sPath);
  StepsTagInfo reused_steps_info(&*this, &out, sPath);

  for (unsigned i = 0; i < values; i++) {
    const MsdFile::value_t& sParams = msd.GetValue(i);
    std::string sValueName = sParams[0];
    MakeUpper(sValueName);

    switch (state) {
      case GETTING_SONG_INFO: {
        reused_song_info.params_ = &sParams;
        song_handler_map_t::iterator handler =
            parser_helper.song_tag_handlers_.find(sValueName);
        if (handler != parser_helper.song_tag_handlers_.end()) {
          handler->second(reused_song_info);
        } else if (Left(sValueName, strlen("BGCHANGES")) == "BGCHANGES") {
          SetBGChanges(reused_song_info);
        }
        // This tag will get us to the next section.
        else if (sValueName == "NOTEDATA") {
          state = GETTING_STEP_INFO;
          pNewNotes = out.CreateSteps();
          stepsTiming = TimingData(out.song_timing_.beat0_offset_in_seconds_);
          reused_steps_info.has_own_timing_ = false;
          reused_steps_info.steps_ = pNewNotes;
          reused_steps_info.timing_ = &stepsTiming;
        } else {
          // Silently ignore unrecognized tags, as was done before. -Kyz
        }
        break;
      }
      case GETTING_STEP_INFO: {
        reused_steps_info.params_ = &sParams;
        steps_handler_map_t::iterator handler =
            parser_helper.steps_tag_handlers_.find(sValueName);
        if (handler != parser_helper.steps_tag_handlers_.end()) {
          handler->second(reused_steps_info);
        } else if (sValueName == "NOTES" || sValueName == "NOTES2") {
          state = GETTING_SONG_INFO;
          if (reused_steps_info.has_own_timing_) {
            pNewNotes->timing_ = stepsTiming;
          }
          reused_steps_info.has_own_timing_ = false;
          pNewNotes->SetSMNoteData(sParams[1]);
          pNewNotes->TidyUpData();
          pNewNotes->SetFilename(sPath);
          out.AddSteps(pNewNotes);
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
    std::string sEditFilePath, ProfileSlot slot, bool bAddStepsToSong,
    Song* givenSong /* = nullptr */) {
  LOG->Trace("SSCLoader::LoadEditFromFile(%s)", sEditFilePath.c_str());

  int iBytes = FILEMAN->GetFileSizeInBytes(sEditFilePath);
  if (iBytes > MAX_EDIT_STEPS_SIZE_BYTES) {
    LOG->UserLog(
        "Edit file", sEditFilePath,
        "is unreasonably large. It won't be loaded.");
    return false;
  }

  MsdFile msd;
  if (!msd.ReadFile(sEditFilePath, true))  // unescape
  {
    LOG->UserLog(
        "Edit file", sEditFilePath, "couldn't be opened: %s",
        msd.GetError().c_str());
    return false;
  }

  return LoadEditFromMsd(msd, sEditFilePath, slot, bAddStepsToSong, givenSong);
}

bool SSCLoader::LoadEditFromMsd(
    const MsdFile& msd, const std::string& sEditFilePath, ProfileSlot slot,
    bool bAddStepsToSong, Song* givenSong /* = nullptr */) {
  Song* pSong = givenSong;
  Steps* pNewNotes = nullptr;
  TimingData stepsTiming;

  StepsTagInfo reused_steps_info(&*this, pSong, sEditFilePath);
  reused_steps_info.for_load_edit_ = true;
  reused_steps_info.timing_ = &stepsTiming;

  for (unsigned int i = 0; i < msd.GetNumValues(); ++i) {
    int iNumParams = msd.GetNumParams(i);
    const MsdFile::value_t& sParams = msd.GetValue(i);
    std::string sValueName = sParams[0];
    MakeUpper(sValueName);

    if (pSong != nullptr) {
      reused_steps_info.params_ = &sParams;
      steps_handler_map_t::iterator handler =
          parser_helper.steps_tag_handlers_.find(sValueName);
      if (pNewNotes != nullptr &&
          handler != parser_helper.steps_tag_handlers_.end()) {
        handler->second(reused_steps_info);
      } else if (sValueName == "NOTEDATA") {
        pNewNotes = pSong->CreateSteps();
        reused_steps_info.steps_ = pNewNotes;
        reused_steps_info.ssc_format_ = true;
      } else if (sValueName == "NOTES") {
        if (pSong == nullptr) {
          LOG->UserLog(
              "Edit file", sEditFilePath,
              "doesn't have a #SONG tag preceeding the first #NOTES tag,"
              " and is not in a valid song-specific folder.");
          return false;
        }

        if (!reused_steps_info.ssc_format_ && iNumParams < 7) {
          LOG->UserLog(
              "Edit file", sEditFilePath,
              "has %d fields in a #NOTES tag, but should have at least 7.",
              iNumParams);
          continue;
        }

        // I have no idea what the purpose of this bAddStepsToSong flag is.
        // It looks like it causes LoadEditFromMsd to just throw away
        // whatever steps data was loaded, possibly leaking the memory used
        // by pNewNotes.  It was here before I rewrote LoadEditFromMsd to
        // use a map instead of an else if chain, so I preserved it. -Kyz
        if (!bAddStepsToSong) {
          return true;
        }

        // Force the difficulty to edit in case the edit set its own
        // difficulty because IsEditAlreadyLoaded has an assert and edits
        // shouldn't be able to add charts of other difficulties. -Kyz
        if (pNewNotes != nullptr) {
          pNewNotes->SetDifficulty(Difficulty_Edit);
          if (pSong->IsEditAlreadyLoaded(pNewNotes)) {
            LOG->UserLog(
                "Edit file", sEditFilePath,
                "is a duplicate of another edit that was already loaded.");
            RageUtil::SafeDelete(pNewNotes);
            return false;
          }
        }

        if (reused_steps_info.ssc_format_) {
          if (reused_steps_info.has_own_timing_) {
            pNewNotes->timing_ = stepsTiming;
          }
          pNewNotes->SetSMNoteData(sParams[1]);
          pNewNotes->TidyUpData();
        } else {
          pNewNotes = pSong->CreateSteps();
          LoadFromTokens(
              sParams[1], sParams[2], sParams[3], sParams[4], sParams[5],
              sParams[6], *pNewNotes);
        }

        pNewNotes->SetLoadedFromProfile(slot);
        pNewNotes->SetDifficulty(Difficulty_Edit);
        pNewNotes->SetFilename(sEditFilePath);

        pSong->AddSteps(pNewNotes);
        return true;  // Only allow one Steps per edit file!
      } else {
        LOG->UserLog(
            "Edit file", sEditFilePath, "has an unexpected value \"%s\".",
            sValueName.c_str());
      }
    } else {
      if (sValueName == "SONG") {
        if (pSong) {
          /* LOG->UserLog("Edit file", sEditFilePath, "has more than one #SONG
             tag."); return false; */
          continue;
        }

        std::string sSongFullTitle = sParams[1];
        this->SetSongTitle(sParams[1]);
        Replace(sSongFullTitle, '\\', '/');
        pSong = SONGMAN->FindSong(sSongFullTitle);
        reused_steps_info.song_ = pSong;
        if (pSong == nullptr) {
          LOG->UserLog(
              "Edit file", sEditFilePath,
              "requires a song \"%s\" that isn't present.",
              sSongFullTitle.c_str());
          return false;
        }
        if (pSong->GetNumStepsLoadedFromProfile(slot) >=
            MAX_EDITS_PER_SONG_PER_PROFILE) {
          LOG->UserLog(
              "Song file", sSongFullTitle,
              "already has the maximum number of edits allowed for "
              "ProfileSlotP%d.",
              slot + 1);
          return false;
        }
        reused_steps_info.song_ = pSong;
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
