/* This stores a single note pattern for a song.
 *
 * We can have too much data to keep everything decompressed as NoteData, so
 * most songs are kept in memory compressed as SMData until requested.  NoteData
 * is normally not requested casually during gameplay; we can move through
 * screens, the music wheel, etc. without touching any NoteData.
 *
 * To save more memory, if data is cached on disk, read it from disk on demand.
 * Not all Steps will have an associated file for this purpose.  (Profile edits
 * don't do this yet.)
 *
 * Data can be on disk (always compressed), compressed in memory, and
 * uncompressed in memory. */
#include "Steps.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "ColumnCues.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MeasureInfo.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NoteTypes.h"
#include "NotesLoaderDWI.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSMA.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "StdString.h"
#include "StepParityDatastructs.h"
#include "StepParityGenerator.h"
#include "StepsUtil.h"
#include "TechCounts.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "global.h"

/* register DisplayBPM with StringConversion */
#include "EnumHelper.h"

// For hashing hart keys - Mina
#include "CryptManager.h"

// Constructing a StageLayout pre-generates every foot placement permutation,
// which is expensive, so each layout is built once on first use.
// (yes it is thread safe)
static const StepParity::StageLayout* getLayout(StepsType ty) {
  switch (ty) {
    case StepsType_dance_single: {
      static const StepParity::StageLayout layout(
          StepsType_dance_single,
          {
              {0, 1},  // Left
              {1, 0},  // Down
              {1, 2},  // Up
              {2, 1}   // Right
          },
          {2}, {1}, {0, 3});
      return &layout;
    }

    case StepsType_dance_couple: {
      static const StepParity::StageLayout layout(
          StepsType_dance_couple,
          {
              {0, 1},  // Left
              {1, 0},  // Down
              {1, 2},  // Up
              {2, 1}   // Right
          },
          {2}, {1}, {0, 3});
      return &layout;
    }

    case StepsType_dance_double: {
      static const StepParity::StageLayout layout(
          StepsType_dance_double,
          {
              {0, 1},  // P1 Left
              {1, 0},  // P1 Down
              {1, 2},  // P1 Up
              {2, 1},  // P1 Right

              {3, 1},  // P2 Left
              {4, 0},  // P2 Down
              {4, 2},  // P2 Up
              {5, 1}   // P2 Right
          },
          {2, 6}, {1, 5}, {0, 3, 4, 7});
      return &layout;
    }

    case StepsType_dance_routine: {
      static const StepParity::StageLayout layout(
          StepsType_dance_routine,
          {
              {0, 1},  // P1 Left
              {1, 0},  // P1 Down
              {1, 2},  // P1 Up
              {2, 1},  // P1 Right

              {3, 1},  // P2 Left
              {4, 0},  // P2 Down
              {4, 2},  // P2 Up
              {5, 1}   // P2 Right
          },
          {2, 6}, {1, 5}, {0, 3, 4, 7});
      return &layout;
    }

    default:
      return nullptr;
  }
}

static const char* DisplayBPMNames[] = {
    "Actual",
    "Specified",
    "Random",
};
XToString(DisplayBPM);
LuaXType(DisplayBPM);

Steps::Steps(Song* song)
    : steps_type_(StepsType_Invalid),
      song_(song),
      parent_(nullptr),
      note_data_(new NoteData),
      note_data_is_filled_(false),
      note_data_compressed_(""),
      filename_(""),
      saved_to_disk_(false),
      loaded_from_profile_(ProfileSlot_Invalid),
      hash_(0),
      description_(""),
      chart_style_(""),
      difficulty_(Difficulty_Invalid),
      meter_(0),
      groove_stats_hash_(""),
      groove_stats_hash_version_(0),
      credit_(""),
      display_bpm_type_(DISPLAY_BPM_ACTUAL),
      specified_bpm_min_(0),
      specified_bpm_max_(0) {}

Steps::~Steps() {}

void Steps::GetDisplayBpms(DisplayBpms& AddTo) const {
  if (this->GetDisplayBPM() == DISPLAY_BPM_SPECIFIED) {
    AddTo.Add(this->GetMinBPM());
    AddTo.Add(this->GetMaxBPM());
  } else {
    float fMinBPM, fMaxBPM;
    this->GetTimingData()->GetActualBPM(fMinBPM, fMaxBPM);
    AddTo.Add(fMinBPM);
    AddTo.Add(fMaxBPM);
  }
}

bool Steps::HasAttacks() const {
  return !this->attacks_.empty() ||
         (this->song_ != nullptr && this->song_->HasAttacks());
}

AttackArray Steps::GetAttacks() const {
  AttackArray attacks;
  if (this->song_ != nullptr) {
    attacks = this->song_->attacks_;
  }
  attacks.insert(attacks.end(), this->attacks_.begin(), this->attacks_.end());
  return attacks;
}

unsigned Steps::GetHash() const {
  if (parent_) {
    return parent_->GetHash();
  }
  if (hash_) {
    return hash_;
  }
  if (note_data_compressed_.empty()) {
    if (!note_data_is_filled_) {
      return 0;  // No data, no hash.
    }
    NoteDataUtil::GetSMNoteDataString(
        *note_data_, note_data_compressed_, /*bIncludeMeasureComments=*/true);
  }
  hash_ = GetHashForString(note_data_compressed_);
  return hash_;
}

bool Steps::IsNoteDataEmpty() const {
  return this->note_data_compressed_.empty();
}

bool Steps::GetNoteDataFromSimfile() {
  // Replace the line below with the Steps' cache file.
  std::string stepFile = this->GetFilename();
  std::string extension = GetExtension(stepFile);
  MakeLower(extension);  // must do this because the code is expecting lowercase

  if (extension.empty() || extension == "ssc" ||
      extension == "ats")  // remember cache files.
  {
    SSCLoader loader;
    if (!loader.LoadNoteDataFromSimfile(stepFile, *this)) {
      /*
      HACK: 7/20/12 -- see bugzilla #740
      users who edit songs using the ever popular .sm file
      that remove or tamper with the .ssc file later on
      complain of blank steps in the editor after reloading.
      Despite the blank steps being well justified since
      the cache files contain only the SSC step file,
      give the user some leeway and search for a .sm replacement
      */
      SMLoader backup_loader;
      std::string transformedStepFile = stepFile;
      Replace(transformedStepFile, ".ssc", ".sm");

      return backup_loader.LoadNoteDataFromSimfile(transformedStepFile, *this);
    } else {
      return true;
    }
  } else if (extension == "sm") {
    SMLoader loader;
    return loader.LoadNoteDataFromSimfile(stepFile, *this);
  } else if (extension == "sma") {
    SMALoader loader;
    return loader.LoadNoteDataFromSimfile(stepFile, *this);
  } else if (extension == "dwi") {
    return DWILoader::LoadNoteDataFromSimfile(stepFile, *this);
  } else if (extension == "edit") {
    // Try SSC, then fallback to SM.
    SSCLoader ldSSC;
    if (ldSSC.LoadNoteDataFromSimfile(stepFile, *this) != true) {
      SMLoader ldSM;
      return ldSM.LoadNoteDataFromSimfile(stepFile, *this);
    } else {
      return true;
    }
  }
  return false;
}

void Steps::SetNoteData(const NoteData& noteDataNew) {
  ASSERT(
      noteDataNew.GetNumTracks() ==
      GAMEMAN->GetStepsTypeInfo(steps_type_).num_tracks_);

  DeAutogen(false);

  *note_data_ = noteDataNew;
  note_data_is_filled_ = true;

  note_data_compressed_ = std::string();
  hash_ = 0;
}

void Steps::GetNoteData(NoteData& noteDataOut) const {
  Decompress();

  if (note_data_is_filled_) {
    noteDataOut = *note_data_;
  } else {
    noteDataOut.ClearAll();
    noteDataOut.SetNumTracks(
        GAMEMAN->GetStepsTypeInfo(steps_type_).num_tracks_);
  }
}

NoteData Steps::GetNoteData() const {
  NoteData tmp;
  this->GetNoteData(tmp);
  return tmp;
}

void Steps::SetSMNoteData(const std::string& notes_comp_) {
  note_data_->Init();
  note_data_is_filled_ = false;

  note_data_compressed_ = notes_comp_;
  hash_ = 0;
}

/* XXX: this function should pull data from m_sFilename, like Decompress() */
void Steps::GetSMNoteData(std::string& notes_comp_out) const {
  if (note_data_compressed_.empty()) {
    if (!note_data_is_filled_) {
      /* no data is no data */
      notes_comp_out = "";
      return;
    }

    NoteDataUtil::GetSMNoteDataString(
        *note_data_, note_data_compressed_, /*bIncludeMeasureComments=*/true);
  }

  notes_comp_out = note_data_compressed_;
}

float Steps::PredictMeter() const {
  float pMeter = 0.775f;

  const float RadarCoeffs[NUM_RadarCategory] = {
      10.1f, 5.27f, -0.905f, -1.10f, 2.86f, 0, 0, 0, 0, 0, 0, 0, 0};
  const RadarValues& rv = GetRadarValues(PLAYER_1);
  for (int r = 0; r < NUM_RadarCategory; ++r) {
    pMeter += rv[r] * RadarCoeffs[r];
  }

  const float DifficultyCoeffs[NUM_Difficulty] = {-0.877f, -0.877f, 0,
                                                  0.722f,  0.722f,  0};
  pMeter += DifficultyCoeffs[this->GetDifficulty()];

  // Init non-radar values
  const float SV = rv[RadarCategory_Stream] * rv[RadarCategory_Voltage];
  const float ChaosSquare = rv[RadarCategory_Chaos] * rv[RadarCategory_Chaos];
  pMeter += -6.35f * SV;
  pMeter += -2.58f * ChaosSquare;
  if (pMeter < 1) {
    pMeter = 1;
  }
  return pMeter;
}

void Steps::TidyUpData() {
  // Don't set the StepsType to dance single if it's invalid.  That just
  // causes unrecognized charts to end up where they don't belong.
  // Leave it as StepsType_Invalid so the Song can handle it specially.  This
  // is a forwards compatibility feature, so that if a future version adds a
  // new style, editing a simfile with unrecognized Steps won't silently
  // delete them. -Kyz
  if (steps_type_ == StepsType_Invalid) {
    LOG->Warn(
        "Detected steps with unknown style '%s' in '%s'",
        steps_type_str_.c_str(), song_->song_file_name_.c_str());
  } else if (steps_type_str_ == "") {
    steps_type_str_ = GAMEMAN->GetStepsTypeInfo(steps_type_).name_;
  }

  if (GetDifficulty() == Difficulty_Invalid) {
    SetDifficulty(StringToDifficulty(GetDescription()));
  }

  if (GetDifficulty() == Difficulty_Invalid) {
    if (GetMeter() == 1) {
      SetDifficulty(Difficulty_Beginner);
    } else if (GetMeter() <= 3) {
      SetDifficulty(Difficulty_Easy);
    } else if (GetMeter() <= 6) {
      SetDifficulty(Difficulty_Medium);
    } else {
      SetDifficulty(Difficulty_Hard);
    }
  }

  if (GetMeter() < 1) {  // meter is invalid
    SetMeter(int(PredictMeter()));
  }
}

void Steps::CalculateStepStats(float fMusicLengthSeconds) {
  NoteData tempNoteData;
  this->GetNoteData(tempNoteData);
  this->CalculateRadarValues(fMusicLengthSeconds, tempNoteData);
  this->CalculateTechCounts(tempNoteData);
  this->CalculateMeasureInfo(tempNoteData);
  this->CalculateGrooveStatsHash(tempNoteData);
}

void Steps::CalculateRadarValues(
    float fMusicLengthSeconds, const NoteData& tempNoteData) {
  // If we're autogen, don't calculate values.  GetRadarValues will take from
  // our parent.
  if (parent_ != nullptr) {
    return;
  }

  // Do write radar values, and leave it up to the reading app whether they want
  // to trust the cached values without recalculating them.
  /*
  // If we're an edit, leave the RadarValues invalid.
  if( IsAnEdit() )
          return;
  */

  FOREACH_PlayerNumber(pn) radar_values_[pn].Zero();

  TimingData* timing = this->GetTimingData();

  std::vector<NoteData> splitNoteData;
  if (NoteDataUtil::SplitCompositeOrStackedNoteData(
          tempNoteData, splitNoteData, steps_type_)) {
    FOREACH_PlayerNumber(pn) {
      NoteDataUtil::CalculateRadarValues(
          splitNoteData[pn], fMusicLengthSeconds, timing, radar_values_[pn]);
    }
  } else {
    NoteDataUtil::CalculateRadarValues(
        tempNoteData, fMusicLengthSeconds, timing, radar_values_[0]);
    std::fill_n(radar_values_ + 1, NUM_PLAYERS - 1, radar_values_[0]);
  }
}

void Steps::CalculateTechCounts(const NoteData& tempNoteData) {
  if (parent_ != nullptr) {
    return;
  }

  FOREACH_PlayerNumber(pn) tech_counts_[pn].Zero();

  const StepParity::StageLayout* layout = getLayout(this->steps_type_);
  // If we don't have a valid layout for this StepsType, then don't even
  // bother
  if (layout == nullptr) {
    return;
  }
  TimingData* timing = this->GetTimingData();

  std::vector<NoteData> splitNoteData;
  if (NoteDataUtil::SplitCompositeOrStackedNoteData(
          tempNoteData, splitNoteData, steps_type_)) {
    FOREACH_PlayerNumber(pn) {
      StepParity::StepParityGenerator gen{layout, timing};
      gen.analyzeNoteData(splitNoteData[pn]);

      std::vector<NoteAnnotation> tempNoteAnnotations;
      TechCounts::CalculateTechCountsFromRows(
          gen.rows_, layout, tech_counts_[pn], tempNoteAnnotations);

      NoteAnnotationCache cachedAnnotations;
      cachedAnnotations.decompressed_ = tempNoteAnnotations;
      cachedAnnotations.Compress();
      note_annotations_.push_back(cachedAnnotations);
    }
  } else {
    StepParity::StepParityGenerator gen{layout, timing};
    gen.analyzeNoteData(tempNoteData);

    std::vector<NoteAnnotation> tempNoteAnnotations;
    TechCounts::CalculateTechCountsFromRows(
        gen.rows_, layout, tech_counts_[0], tempNoteAnnotations);
    std::fill_n(tech_counts_ + 1, NUM_PLAYERS - 1, tech_counts_[0]);

    NoteAnnotationCache cachedAnnotations;
    cachedAnnotations.decompressed_ = tempNoteAnnotations;
    cachedAnnotations.Compress();
    note_annotations_.push_back(cachedAnnotations);
  }
}

void Steps::CalculateMeasureInfo(const NoteData& tempNoteData) {
  if (parent_ != nullptr) {
    return;
  }

  std::vector<MeasureInfo> measureInfoPerPlayer;

  TimingData* timing = this->GetTimingData();

  std::vector<NoteData> splitNoteData;
  if (NoteDataUtil::SplitCompositeOrStackedNoteData(
          tempNoteData, splitNoteData, steps_type_)) {
    measureInfoPerPlayer.resize(NUM_PLAYERS);
    FOREACH_PlayerNumber(pn) {
      MeasureInfo::CalculateMeasureInfo(
          splitNoteData[pn], timing, measureInfoPerPlayer[pn]);
    }
  } else {
    measureInfoPerPlayer.resize(1);
    MeasureInfo::CalculateMeasureInfo(
        tempNoteData, timing, measureInfoPerPlayer[0]);
  }

  notes_per_measure_.clear();
  nps_per_measure_.clear();
  peak_nps_.clear();
  stream_breakdown_.clear();

  for (MeasureInfo& mi : measureInfoPerPlayer) {
    notes_per_measure_.push_back(mi.notes_per_measure_);
    nps_per_measure_.push_back(mi.nps_per_measure_);
    peak_nps_.push_back(mi.peak_nps_);
    stream_breakdown_.push_back(
        StreamBreakdown::Calculate(mi.notes_per_measure_));
  }
}

void Steps::ChangeFilenamesForCustomSong() {
  filename_ = custom_songify_path(filename_);
}

void Steps::Decompress() const { const_cast<Steps*>(this)->Decompress(); }

void Steps::Decompress() {
  if (note_data_is_filled_) {
    return;  // already decompressed
  }

  if (parent_) {
    // get autogen m_pNoteData
    NoteData notedata;
    parent_->GetNoteData(notedata);

    note_data_is_filled_ = true;

    int iNewTracks = GAMEMAN->GetStepsTypeInfo(steps_type_).num_tracks_;

    NoteDataUtil::LoadTransformedSlidingWindow(
        notedata, *note_data_, iNewTracks);

    NoteDataUtil::RemoveStretch(*note_data_, steps_type_);
    return;
  }

  if (!filename_.empty() && note_data_compressed_.empty()) {
    // We have NoteData on disk and not in memory. Load it.
    if (!this->GetNoteDataFromSimfile()) {
      LOG->Warn(
          "Couldn't load the %s chart's NoteData from \"%s\"",
          DifficultyToString(difficulty_).c_str(), filename_.c_str());
      return;
    }

    this->GetSMNoteData(note_data_compressed_);
  }

  if (note_data_compressed_.empty()) {
    /* there is no data, do nothing */
  } else {
    // load from compressed
    bool bComposite =
        GAMEMAN->GetStepsTypeInfo(steps_type_).steps_type_category_ ==
        StepsTypeCategory_Routine;
    note_data_is_filled_ = true;
    note_data_->SetNumTracks(
        GAMEMAN->GetStepsTypeInfo(steps_type_).num_tracks_);

    NoteDataUtil::LoadFromSMNoteDataString(
        *note_data_, note_data_compressed_, bComposite);
  }
}

void Steps::Compress() const {
  // Always leave lights data uncompressed.
  if (this->steps_type_ == StepsType_lights_cabinet && note_data_is_filled_) {
    note_data_compressed_ = std::string();
    return;
  }

  // Don't compress data in the editor: it's still in use.
  if (GAMESTATE->in_step_editor_) {
    return;
  }

  for (NoteAnnotationCache annotationCache : note_annotations_) {
    annotationCache.Compress();
  }

  if (!filename_.empty() && loaded_from_profile_ == ProfileSlot_Invalid) {
    /* We have a file on disk; clear all data in memory.
     * Data on profiles can't be accessed normally (need to mount and time-out
     * the device), and when we start a game and load edits, we want to be
     * sure that it'll be available if the user picks it and pulls the device.
     * Also, Decompress() doesn't know how to load .edits. */
    note_data_->Init();
    note_data_is_filled_ = false;

    /* Be careful; 'x = ""', m_sNoteDataCompressed.clear() and
     * m_sNoteDataCompressed.reserve(0) don't always free the allocated memory.
     */
    note_data_compressed_ = std::string();
    return;
  }

  // We have no file on disk. Compress the data, if necessary.
  if (note_data_compressed_.empty()) {
    if (!note_data_is_filled_) {
      return; /* no data is no data */
    }
    NoteDataUtil::GetSMNoteDataString(
        *note_data_, note_data_compressed_, /*bIncludeMeasureComments=*/true);
  }

  note_data_->Init();
  note_data_is_filled_ = false;
}

/* Copy our parent's data. This is done when we're being changed from autogen
 * to normal. (needed?) */
void Steps::DeAutogen(bool bCopyNoteData) {
  if (!parent_) {
    return;  // OK
  }

  if (bCopyNoteData) {
    Decompress();  // fills in m_pNoteData with sliding window transform
  }

  description_ = Real()->description_;
  chart_style_ = Real()->chart_style_;
  difficulty_ = Real()->difficulty_;
  meter_ = Real()->meter_;
  std::copy(
      Real()->radar_values_, Real()->radar_values_ + NUM_PLAYERS,
      radar_values_);
  std::copy(
      Real()->tech_counts_, Real()->tech_counts_ + NUM_PLAYERS, tech_counts_);

  nps_per_measure_.assign(
      Real()->nps_per_measure_.begin(), Real()->nps_per_measure_.end());
  notes_per_measure_.assign(
      Real()->notes_per_measure_.begin(), Real()->notes_per_measure_.end());
  stream_breakdown_.assign(
      Real()->stream_breakdown_.begin(), Real()->stream_breakdown_.end());

  credit_ = Real()->credit_;
  parent_ = nullptr;

  if (bCopyNoteData) {
    Compress();
  }
}

void Steps::AutogenFrom(const Steps* parent, StepsType ntTo) {
  parent_ = parent;
  steps_type_ = ntTo;
  steps_type_str_ = GAMEMAN->GetStepsTypeInfo(ntTo).name_;
  timing_ = parent->timing_;
}

void Steps::CopyFrom(
    Steps* pSource, StepsType ntTo,
    float fMusicLengthSeconds)  // pSource does not have to be of the same
                                // StepsType
{
  steps_type_ = ntTo;
  steps_type_str_ = GAMEMAN->GetStepsTypeInfo(ntTo).name_;
  NoteData noteData;
  pSource->GetNoteData(noteData);
  noteData.SetNumTracks(GAMEMAN->GetStepsTypeInfo(ntTo).num_tracks_);
  parent_ = nullptr;
  timing_ = pSource->timing_;
  this->song_ = pSource->song_;
  this->attacks_ = pSource->attacks_;
  this->attack_string_ = pSource->attack_string_;
  this->SetNoteData(noteData);
  this->SetDescription(pSource->GetDescription());
  this->SetDifficulty(pSource->GetDifficulty());
  this->SetMeter(pSource->GetMeter());
  this->CalculateStepStats(fMusicLengthSeconds);
}

void Steps::CreateBlank(StepsType ntTo) {
  steps_type_ = ntTo;
  steps_type_str_ = GAMEMAN->GetStepsTypeInfo(ntTo).name_;
  NoteData noteData;
  noteData.SetNumTracks(GAMEMAN->GetStepsTypeInfo(ntTo).num_tracks_);
  this->SetNoteData(noteData);
}

void Steps::SetDifficultyAndDescription(
    Difficulty dc, std::string sDescription) {
  DeAutogen();
  difficulty_ = dc;
  description_ = sDescription;
  if (GetDifficulty() == Difficulty_Edit) {
    MakeValidEditDescription(description_);
  }
}

void Steps::SetCredit(std::string sCredit) {
  DeAutogen();
  credit_ = sCredit;
}

void Steps::SetChartStyle(std::string sChartStyle) {
  DeAutogen();
  chart_style_ = sChartStyle;
}

bool Steps::MakeValidEditDescription(std::string& sPreferredDescription) {
  if (int(sPreferredDescription.size()) > MAX_STEPS_DESCRIPTION_LENGTH) {
    sPreferredDescription =
        Left(sPreferredDescription, MAX_STEPS_DESCRIPTION_LENGTH);
    return true;
  }
  return false;
}

void Steps::SetMeter(int meter) {
  DeAutogen();
  meter_ = meter;
}

const TimingData* Steps::GetTimingData() const {
  return timing_.empty() ? &song_->song_timing_ : &timing_;
}

bool Steps::HasSignificantTimingChanges() const {
  const TimingData* timing = GetTimingData();
  if (timing->HasStops() || timing->HasDelays() || timing->HasWarps() ||
      timing->HasSpeedChanges() || timing->HasScrollChanges()) {
    return true;
  }

  if (timing->HasBpmChanges()) {
    // check to see if these changes are significant.
    DisplayBpms bpms;
    song_->GetDisplayBpms(bpms);
    if (bpms.GetMax() - bpms.GetMin() > 3.000f) {
      return true;
    }
  }

  return false;
}

const std::string Steps::GetMusicPath() const {
  return Song::GetSongAssetPath(
      music_file_.empty() ? song_->music_file_ : music_file_,
      song_->GetSongDir());
}

const std::string& Steps::GetMusicFile() const { return music_file_; }

void Steps::SetMusicFile(const std::string& file) { music_file_ = file; }

void Steps::SetRadarValues(const RadarValues v[NUM_PLAYERS]) {
  DeAutogen();
  std::copy(v, v + NUM_PLAYERS, radar_values_);
}

void Steps::SetTechCounts(const TechCounts ts[NUM_PLAYERS]) {
  DeAutogen();
  std::copy(ts, ts + NUM_PLAYERS, tech_counts_);
}

void Steps::SetNoteAnnotations(
    std::vector<NoteAnnotationCache>& noteAnnotations) {
  DeAutogen();
  note_annotations_.assign(noteAnnotations.begin(), noteAnnotations.end());
}

void Steps::SetNpsPerMeasure(std::vector<std::vector<float>>& npsPerMeasure) {
  DeAutogen();
  nps_per_measure_.assign(npsPerMeasure.begin(), npsPerMeasure.end());
}

void Steps::SetNotesPerMeasure(std::vector<std::vector<int>>& notesPerMeasure) {
  DeAutogen();
  notes_per_measure_.assign(notesPerMeasure.begin(), notesPerMeasure.end());
}

void Steps::SetStreamBreakdown(std::vector<StreamBreakdown>& streamBreakdown) {
  DeAutogen();
  stream_breakdown_.assign(streamBreakdown.begin(), streamBreakdown.end());
}

void Steps::SetPeakNps(std::vector<float>& peakNps) {
  peak_nps_.assign(peakNps.begin(), peakNps.end());
}

const std::string Steps::GetGrooveStatsHash() const {
  return groove_stats_hash_;
}

int Steps::GetGrooveStatsHashVersion() const {
  return groove_stats_hash_version_;
}

void Steps::CalculateGrooveStatsHash(const NoteData& noteData) {
  std::string smNoteData = this->MinimizedChartString(noteData);

  TimingData* timingData = this->GetTimingData();
  std::vector<TimingSegment*> segments =
      timingData->GetTimingSegments(SEGMENT_BPM);
  std::vector<std::string> bpmStrings;
  bpmStrings.reserve(segments.size());
  for (TimingSegment* segment : segments) {
    BPMSegment* bpmSegment = ToBPM(segment);
    float beat = bpmSegment->GetBeat();
    float bpm = bpmSegment->GetBPM();
    std::string segmentStr = ssprintf(
        "%s=%s", NormalizeDecimal(beat).c_str(), NormalizeDecimal(bpm).c_str());
    bpmStrings.push_back(segmentStr);
  }
  std::string bpmString = join(",", bpmStrings);

  smNoteData.append(bpmString);
  std::string gsKey = BinaryToHex(CryptManager::GetSHA1ForString(smNoteData));
  gsKey = gsKey.substr(0, 16);
  groove_stats_hash_ = gsKey;
  groove_stats_hash_version_ = CURRENT_GROOVE_STATS_HASH_VERSION;
}

std::string Steps::MinimizedChartString(const NoteData& noteData) {
  // We can potentially minimize the chart to get the most compressed
  // form of the actual chart data.
  // NOTE(teejusb): This can be more compressed than the data actually
  // generated by StepMania. This is okay because the charts would still
  // be considered equivalent.
  // E.g. 0000                      0000
  // 		0000  -- minimized to -->
  // 		0000
  // 		0000
  // 		StepMania will always generate the former since quarter notes
  // are 		the smallest quantization.

  // Instead of calling GetSMNoteData(), call
  // NoteDataUtil::GetSMNoteDataString() to ensure that we have a consistent,
  // valid stepchart representation.
  std::string smNoteData = "";
  NoteDataUtil::GetSMNoteDataString(
      noteData, smNoteData, /*bIncludeMeasureComments=*/false);

  if (smNoteData == "") {
    return "";
  }

  std::string minimizedNoteData = "";

  std::vector<std::string> measures;

  split(smNoteData, ",", measures, true);
  for (unsigned m = 0; m < measures.size(); m++) {
    Trim(measures[m]);
    bool allZeroes = true;
    bool minimal = false;
    std::vector<std::string> lines;
    split(measures[m], "\n", lines, true);
    while (lines.size() > 0 && !minimal && lines.size() % 2 == 0) {
      // If every other line is all 0s, we can minimize the measure
      for (unsigned i = 1; i < lines.size(); i += 2) {
        Trim(lines[i]);
        if (lines[i].find_first_not_of('0') != std::string::npos) {
          allZeroes = false;
          break;
        }
      }

      if (allZeroes) {
        // Keep the even indices, dropping every other line.
        for (size_t i = 1; i * 2 < lines.size(); ++i) {
          lines[i] = std::move(lines[i * 2]);
        }
        lines.resize(lines.size() / 2);
      } else {
        minimal = true;
      }
    }

    // Once the measure has been minimized, make sure all of the lines are
    // actually trimmed.
    // (for some reason, the chart returned by GetSMNoteData() have a lot
    //  of extra newlines)
    for (unsigned l = 0; l < lines.size(); l++) {
      Trim(lines[l]);
    }

    // Then, rejoin the lines together to make a measure,
    // and add it to minimizedNoteData.
    minimizedNoteData += join("\n", lines);
    if (m < measures.size() - 1) {
      minimizedNoteData += "\n,\n";
    }
  }
  return minimizedNoteData;
}

void Steps::SetGrooveStatsHash(const std::string& key) {
  groove_stats_hash_ = key;
}

void Steps::SetGrooveStatsHashVersion(int version) {
  groove_stats_hash_version_ = version;
}

std::vector<ColumnCue> Steps::GetColumnCues(
    PlayerNumber pn, float minDuration) {
  std::vector<ColumnCue> cues;
  NoteData noteData;
  this->GetNoteData(noteData);
  TimingData* timing = this->GetTimingData();
  ColumnCue::CalculateColumnCues(
      noteData, timing, cues, pn, minDuration, steps_type_);
  return cues;
}

const std::vector<float>& Steps::GetNpsPerMeasure(PlayerNumber pn) const {
  // CachedNpsPerMeasure will only have separate sets of values per-player if
  // the steps type has different steps for each player (eg dance-couples,
  // dance-routine). Otherwise, it will only store one copy of the values (which
  // will be the case for like 99.9% of charts).

  static const std::vector<float> EMPTY_VECTOR;
  if (Real()->nps_per_measure_.size() == 0) {
    return EMPTY_VECTOR;
  } else if (Real()->nps_per_measure_.size() <= pn) {
    return Real()->nps_per_measure_[PLAYER_1];
  } else {
    return Real()->nps_per_measure_[pn];
  }
}

const std::vector<int>& Steps::GetNotesPerMeasure(PlayerNumber pn) const {
  // m_NotesPerMeasure will only have separate sets of values per-player if
  // the steps type has different steps for each player (eg dance-couples,
  // dance-routine). Otherwise, it will only have one copy of the values (which
  // will be the case for like 99.9% of charts).
  static const std::vector<int> EMPTY_VECTOR;
  if (Real()->notes_per_measure_.size() == 0) {
    return EMPTY_VECTOR;
  } else if (Real()->notes_per_measure_.size() <= pn) {
    return Real()->notes_per_measure_[PLAYER_1];
  } else {
    return Real()->notes_per_measure_[pn];
  }
}

const StreamBreakdown& Steps::GetStreamBreakdown(PlayerNumber pn) const {
  // m_StreamBreakdown will only have separate sets of values per-player if
  // the steps type has different steps for each player (eg dance-couples,
  // dance-routine). Otherwise, it will only have one copy of the values (which
  // will be the case for like 99.9% of charts).
  static const StreamBreakdown EMPTY_BREAKDOWN;
  if (Real()->stream_breakdown_.size() == 0) {
    return EMPTY_BREAKDOWN;
  } else if (Real()->stream_breakdown_.size() <= pn) {
    return Real()->stream_breakdown_[PLAYER_1];
  } else {
    return Real()->stream_breakdown_[pn];
  }
}

const std::vector<NoteAnnotation>& Steps::GetNoteAnnotations(
    PlayerNumber pn) const {
  // For couples/routine charts, this functions the same as GetNotesPerMeasure
  // and GetNpsPerMeasure.

  static const std::vector<NoteAnnotation> EMPTY_VECTOR;
  if (Real()->note_annotations_.size() == 0) {
    return EMPTY_VECTOR;
  } else if (Real()->note_annotations_.size() <= pn) {
    return Real()->note_annotations_[PLAYER_1].GetNoteAnnotations();
  } else {
    return Real()->note_annotations_[pn].GetNoteAnnotations();
  }
}

const std::vector<NoteAnnotationCache>& Steps::GetNoteAnnotationCaches() const {
  return note_annotations_;
}

float Steps::GetPeakNps(PlayerNumber pn) const {
  if (Real()->peak_nps_.size() == 0) {
    return 0;
  } else if (Real()->peak_nps_.size() <= pn) {
    return Real()->peak_nps_[PLAYER_1];
  } else {
    return Real()->peak_nps_[pn];
  }
}

// lua start
#include "LuaBinding.h"
/** @brief Allow Lua to have access to the Steps. */
class LunaSteps : public Luna<Steps> {
 public:
  DEFINE_METHOD(GetStepsType, steps_type_)
  DEFINE_METHOD(GetDifficulty, GetDifficulty())
  DEFINE_METHOD(GetDescription, GetDescription())
  DEFINE_METHOD(GetChartStyle, GetChartStyle())
  DEFINE_METHOD(GetAuthorCredit, GetCredit())
  DEFINE_METHOD(GetMeter, GetMeter())
  DEFINE_METHOD(GetFilename, GetFilename())
  DEFINE_METHOD(IsAutogen, IsAutogen())
  DEFINE_METHOD(IsAnEdit, IsAnEdit())
  DEFINE_METHOD(IsAPlayerEdit, IsAPlayerEdit())

  static int HasSignificantTimingChanges(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasSignificantTimingChanges());
    return 1;
  }
  static int HasAttacks(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasAttacks());
    return 1;
  }
  static int GetRadarValues(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }

    RadarValues& rv = const_cast<RadarValues&>(p->GetRadarValues(pn));
    rv.PushSelf(L);
    return 1;
  }

  static int GetTechCounts(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }
    TechCounts& ts = const_cast<TechCounts&>(p->GetTechCounts(pn));
    ts.PushSelf(L);
    return 1;
  }

  static int CalculateTechCounts(T* p, lua_State* L) {
    return GetTechCounts(p, L);
  }

  static int GetNpsPerMeasure(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }
    std::vector<float>& ts =
        const_cast<std::vector<float>&>(p->GetNpsPerMeasure(pn));
    LuaHelpers::CreateTableFromArray(ts, L);
    return 1;
  }

  static int GetNotesPerMeasure(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }
    std::vector<int>& ts =
        const_cast<std::vector<int>&>(p->GetNotesPerMeasure(pn));
    LuaHelpers::CreateTableFromArray(ts, L);
    return 1;
  }

  static int GetPeakNps(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }
    lua_pushnumber(L, p->GetPeakNps(pn));
    return 1;
  }

  static int GetStreamBreakdown(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnoneornil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }
    const StreamBreakdown& breakdown = p->GetStreamBreakdown(pn);

    lua_createtable(L, 0, 3);
    lua_createtable(L, static_cast<int>(breakdown.texts_.size()), 0);
    for (size_t i = 0; i < breakdown.texts_.size(); ++i) {
      lua_pushlstring(
          L, breakdown.texts_[i].data(), breakdown.texts_[i].size());
      lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    lua_setfield(L, -2, "Texts");

    lua_pushinteger(L, breakdown.total_stream_measures_);
    lua_setfield(L, -2, "TotalStream");
    lua_pushinteger(L, breakdown.total_break_measures_);
    lua_setfield(L, -2, "TotalBreak");
    return 1;
  }

  static int GetTimingData(T* p, lua_State* L) {
    p->GetTimingData()->PushSelf(L);
    return 1;
  }
  static int GetHash(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetHash());
    return 1;
  }
  // untested
  /*
  static int GetSMNoteData( T* p, lua_State *L )
  {
          std::string out;
          p->GetSMNoteData( out );
          lua_pushstring( L, out );
          return 1;
  }
  */

  static int GetGrooveStatsHash(T* p, lua_State* L) {
    lua_pushstring(L, p->GetGrooveStatsHash().c_str());
    return 1;
  }

  static int GetGrooveStatsHashVersion(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetGrooveStatsHashVersion());
    return 1;
  }

  static int GetChartName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetChartName().c_str());
    return 1;
  }
  static int GetDisplayBpms(T* p, lua_State* L) {
    DisplayBpms temp;
    p->GetDisplayBpms(temp);
    float fMin = temp.GetMin();
    float fMax = temp.GetMax();
    std::vector<float> fBPMs;
    fBPMs.push_back(fMin);
    fBPMs.push_back(fMax);
    LuaHelpers::CreateTableFromArray(fBPMs, L);
    return 1;
  }
  static int IsDisplayBpmSecret(T* p, lua_State* L) {
    DisplayBpms temp;
    p->GetDisplayBpms(temp);
    lua_pushboolean(L, temp.IsSecret());
    return 1;
  }
  static int IsDisplayBpmConstant(T* p, lua_State* L) {
    DisplayBpms temp;
    p->GetDisplayBpms(temp);
    lua_pushboolean(L, temp.BpmIsConstant());
    return 1;
  }
  static int IsDisplayBpmRandom(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetDisplayBPM() == DISPLAY_BPM_RANDOM);
    return 1;
  }
  DEFINE_METHOD(PredictMeter, PredictMeter())
  static int GetDisplayBPMType(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetDisplayBPM());
    return 1;
  }

  static int GetColumnCues(T* p, lua_State* L) {
    float minDuration = 1.5;
    if (lua_isnumber(L, 1)) {
      minDuration = lua_tonumber(L, 1);
    }
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnoneornil(L, 2)) {
      pn = Enum::Check<PlayerNumber>(L, 2);
    }
    std::vector<ColumnCue> cues = p->GetColumnCues(pn, minDuration);
    lua_createtable(L, cues.size(), 0);

    for (unsigned i = 0; i < cues.size(); i++) {
      lua_newtable(L);
      lua_pushstring(L, "startTime");
      lua_pushnumber(L, cues[i].start_time_);
      lua_settable(L, -3);

      lua_pushstring(L, "duration");
      lua_pushnumber(L, cues[i].duration_);
      lua_settable(L, -3);

      lua_pushstring(L, "columns");
      lua_createtable(L, cues[i].columns_.size(), 0);

      for (unsigned c = 0; c < cues[i].columns_.size(); c++) {
        lua_newtable(L);
        lua_pushstring(L, "colNum");
        lua_pushinteger(L, cues[i].columns_[c].col_num_);
        lua_settable(L, -3);

        lua_pushstring(L, "noteType");
        lua_pushinteger(L, cues[i].columns_[c].note_type_);
        lua_settable(L, -3);

        lua_rawseti(L, -2, c + 1);
      }

      lua_settable(L, -3);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }

  static int GetNoteAnnotations(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnoneornil(L, 1)) {
      pn = Enum::Check<PlayerNumber>(L, 1);
    }
    const std::vector<NoteAnnotation> rows = p->GetNoteAnnotations(pn);

    lua_createtable(L, static_cast<int>(rows.size()), 0);

    for (unsigned i = 0; i < rows.size(); i++) {
      lua_createtable(L, 0, 4);
      lua_pushstring(L, "beat");
      lua_pushnumber(L, rows[i].beat_);
      lua_settable(L, -3);

      // add columns
      lua_pushstring(L, "footPlacement");
      lua_createtable(L, 0, 0);
      int colIndex = 1;
      for (StepParity::Foot f : StepParity::FEET) {
        if (rows[i].where_the_feet_are_[f] != StepParity::INVALID_COLUMN) {
          lua_pushnumber(L, rows[i].where_the_feet_are_[f] + 1);
          Enum::Push(L, f);
          lua_settable(L, -3);
        }
      }
      lua_settable(L, -3);

      // add tech
      lua_pushstring(L, "tech");
      lua_createtable(L, 0, 0);
      int techIndex = 1;
      for (const TechCountsCategory techVal : rows[i].tech_) {
        Enum::Push(L, techVal);
        lua_rawseti(L, -2, techIndex++);
      }
      lua_settable(L, -3);

      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }

  LunaSteps() {
    ADD_METHOD(GetAuthorCredit);
    ADD_METHOD(GetChartStyle);
    ADD_METHOD(GetDescription);
    ADD_METHOD(GetDifficulty);
    ADD_METHOD(GetFilename);
    ADD_METHOD(GetHash);
    ADD_METHOD(GetMeter);
    ADD_METHOD(HasSignificantTimingChanges);
    ADD_METHOD(HasAttacks);
    ADD_METHOD(GetRadarValues);
    ADD_METHOD(GetTechCounts);
    // TODO: remove CalculateTechCounts
    ADD_METHOD(CalculateTechCounts);
    ADD_METHOD(GetTimingData);
    ADD_METHOD(GetChartName);
    // ADD_METHOD( GetSMNoteData );
    ADD_METHOD(GetStepsType);
    ADD_METHOD(IsAnEdit);
    ADD_METHOD(IsAutogen);
    ADD_METHOD(IsAPlayerEdit);
    ADD_METHOD(GetDisplayBpms);
    ADD_METHOD(IsDisplayBpmSecret);
    ADD_METHOD(IsDisplayBpmConstant);
    ADD_METHOD(IsDisplayBpmRandom);
    ADD_METHOD(PredictMeter);
    ADD_METHOD(GetDisplayBPMType);
    // TODO: Below are experimental and may change in the future until we
    // actually document them in lua.
    // We expose them for testing purposes.
    ADD_METHOD(GetColumnCues);
    ADD_METHOD(GetNpsPerMeasure);
    ADD_METHOD(GetNotesPerMeasure);
    ADD_METHOD(GetStreamBreakdown);
    ADD_METHOD(GetPeakNps);
    ADD_METHOD(GetGrooveStatsHash);
    ADD_METHOD(GetGrooveStatsHashVersion);
    ADD_METHOD(GetNoteAnnotations);
  }
};

LUA_REGISTER_CLASS(Steps)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard, David Wilson
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
