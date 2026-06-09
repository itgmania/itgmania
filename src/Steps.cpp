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
#include <optional>
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
#include "RageUtil/Regex.h"
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

std::optional<StepParity::StageLayout> getLayout(StepsType ty) {
  switch (ty) {
    case StepsType_dance_single:
      return StepParity::StageLayout(
          StepsType_dance_single,
          {
              {0, 1},  // Left
              {1, 0},  // Down
              {1, 2},  // Up
              {2, 1}   // Right
          },
          {2}, {1}, {0, 3});

    case StepsType_dance_double:
      return StepParity::StageLayout(
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
    default:
      return std::nullopt;
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
    : m_StepsType(StepsType_Invalid),
      m_pSong(song),
      parent(nullptr),
      m_pNoteData(new NoteData),
      m_bNoteDataIsFilled(false),
      m_sNoteDataCompressed(""),
      m_sFilename(""),
      m_bSavedToDisk(false),
      m_LoadedFromProfile(ProfileSlot_Invalid),
      m_iHash(0),
      m_sDescription(""),
      m_sChartStyle(""),
      m_Difficulty(Difficulty_Invalid),
      m_iMeter(0),
      m_sGrooveStatsHash(""),
      m_iGrooveStatsHashVersion(0),
      m_sCredit(""),
      displayBPMType(DISPLAY_BPM_ACTUAL),
      specifiedBPMMin(0),
      specifiedBPMMax(0) {}

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

bool Steps::HasAttacks() const { return !this->m_Attacks.empty(); }

unsigned Steps::GetHash() const {
  if (parent) {
    return parent->GetHash();
  }
  if (m_iHash) {
    return m_iHash;
  }
  if (m_sNoteDataCompressed.empty()) {
    if (!m_bNoteDataIsFilled) {
      return 0;  // No data, no hash.
    }
    NoteDataUtil::GetSMNoteDataString(
        *m_pNoteData, m_sNoteDataCompressed, /*bIncludeMeasureComments=*/true);
  }
  m_iHash = GetHashForString(m_sNoteDataCompressed);
  return m_iHash;
}

bool Steps::IsNoteDataEmpty() const {
  return this->m_sNoteDataCompressed.empty();
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
      GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks);

  DeAutogen(false);

  *m_pNoteData = noteDataNew;
  m_bNoteDataIsFilled = true;

  m_sNoteDataCompressed = std::string();
  m_iHash = 0;
}

void Steps::GetNoteData(NoteData& noteDataOut) const {
  Decompress();

  if (m_bNoteDataIsFilled) {
    noteDataOut = *m_pNoteData;
  } else {
    noteDataOut.ClearAll();
    noteDataOut.SetNumTracks(GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks);
  }
}

NoteData Steps::GetNoteData() const {
  NoteData tmp;
  this->GetNoteData(tmp);
  return tmp;
}

void Steps::SetSMNoteData(const std::string& notes_comp_) {
  m_pNoteData->Init();
  m_bNoteDataIsFilled = false;

  m_sNoteDataCompressed = notes_comp_;
  m_iHash = 0;
}

/* XXX: this function should pull data from m_sFilename, like Decompress() */
void Steps::GetSMNoteData(std::string& notes_comp_out) const {
  if (m_sNoteDataCompressed.empty()) {
    if (!m_bNoteDataIsFilled) {
      /* no data is no data */
      notes_comp_out = "";
      return;
    }

    NoteDataUtil::GetSMNoteDataString(
        *m_pNoteData, m_sNoteDataCompressed, /*bIncludeMeasureComments=*/true);
  }

  notes_comp_out = m_sNoteDataCompressed;
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
  if (m_StepsType == StepsType_Invalid) {
    LOG->Warn(
        "Detected steps with unknown style '%s' in '%s'",
        m_StepsTypeStr.c_str(), m_pSong->m_sSongFileName.c_str());
  } else if (m_StepsTypeStr == "") {
    m_StepsTypeStr = GAMEMAN->GetStepsTypeInfo(m_StepsType).szName;
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
  this->CalculateRadarValues(fMusicLengthSeconds);
  this->CalculateTechCounts();
  this->CalculateMeasureInfo();
  this->CalculateGrooveStatsHash();
}

void Steps::CalculateRadarValues(float fMusicLengthSeconds) {
  // If we're autogen, don't calculate values.  GetRadarValues will take from
  // our parent.
  if (parent != nullptr) {
    return;
  }

  // Do write radar values, and leave it up to the reading app whether they want
  // to trust the cached values without recalculating them.
  /*
  // If we're an edit, leave the RadarValues invalid.
  if( IsAnEdit() )
          return;
  */

  NoteData tempNoteData;
  this->GetNoteData(tempNoteData);

  FOREACH_PlayerNumber(pn) m_RadarValues[pn].Zero();

  TimingData* timing = this->GetTimingData();
  if (tempNoteData.IsComposite()) {
    std::vector<NoteData> vParts;

    NoteDataUtil::SplitCompositeNoteData(tempNoteData, vParts);
    for (size_t pn = 0; pn < std::min(vParts.size(), size_t(NUM_PLAYERS));
         ++pn) {
      NoteDataUtil::CalculateRadarValues(
          vParts[pn], fMusicLengthSeconds, timing, m_RadarValues[pn]);
    }
  } else if (
      GAMEMAN->GetStepsTypeInfo(this->m_StepsType).m_StepsTypeCategory ==
      StepsTypeCategory_Couple) {
    NoteData p1 = tempNoteData;
    // XXX: Assumption that couple will always have an even number of notes.
    const int tracks = tempNoteData.GetNumTracks() / 2;
    p1.SetNumTracks(tracks);
    NoteDataUtil::CalculateRadarValues(
        p1, fMusicLengthSeconds, timing, m_RadarValues[PLAYER_1]);
    // at this point, p2 is tempNoteData.
    NoteDataUtil::ShiftTracks(tempNoteData, tracks);
    tempNoteData.SetNumTracks(tracks);
    NoteDataUtil::CalculateRadarValues(
        tempNoteData, fMusicLengthSeconds, timing, m_RadarValues[PLAYER_2]);
  } else {
    NoteDataUtil::CalculateRadarValues(
        tempNoteData, fMusicLengthSeconds, timing, m_RadarValues[0]);
    std::fill_n(m_RadarValues + 1, NUM_PLAYERS - 1, m_RadarValues[0]);
  }
}

void Steps::CalculateTechCounts() {
  if (parent != nullptr) {
    return;
  }

  NoteData tempNoteData;
  this->GetNoteData(tempNoteData);

  FOREACH_PlayerNumber(pn) m_TechCounts[pn].Zero();

  const std::optional<StepParity::StageLayout> layout =
      getLayout(this->m_StepsType);
  // If we don't have a valid layout for this StepsType, then don't even
  // bother
  if (!layout) {
    return;
  }
  TimingData* timing = this->GetTimingData();
  StepParity::StepParityGenerator gen =
      StepParity::StepParityGenerator(&*layout, timing);
  gen.analyzeNoteData(tempNoteData);
  TechCounts::CalculateTechCountsFromRows(gen.rows, &*layout, m_TechCounts[0]);
  std::fill_n(m_TechCounts + 1, NUM_PLAYERS - 1, m_TechCounts[0]);
}

void Steps::CalculateMeasureInfo() {
  if (parent != nullptr) {
    return;
  }

  NoteData tempNoteData;
  this->GetNoteData(tempNoteData);

  std::vector<MeasureInfo> measureInfoPerPlayer;

  TimingData* timing = this->GetTimingData();
  if (tempNoteData.IsComposite()) {
    measureInfoPerPlayer.resize(NUM_PLAYERS);
    std::vector<NoteData> vParts;
    NoteDataUtil::SplitCompositeNoteData(tempNoteData, vParts);
    for (std::size_t pn = 0;
         pn < std::min(vParts.size(), std::size_t(NUM_PLAYERS)); ++pn) {
      MeasureInfo::CalculateMeasureInfo(
          vParts[pn], timing, measureInfoPerPlayer[pn]);
    }
  } else if (
      GAMEMAN->GetStepsTypeInfo(this->m_StepsType).m_StepsTypeCategory ==
      StepsTypeCategory_Couple) {
    measureInfoPerPlayer.resize(NUM_PLAYERS);
    NoteData p1 = tempNoteData;
    // XXX: Assumption that couple will always have an even number of notes.
    const int tracks = tempNoteData.GetNumTracks() / 2;
    p1.SetNumTracks(tracks);
    MeasureInfo::CalculateMeasureInfo(
        tempNoteData, timing, measureInfoPerPlayer[PLAYER_1]);
    NoteDataUtil::ShiftTracks(tempNoteData, tracks);
    tempNoteData.SetNumTracks(tracks);
    MeasureInfo::CalculateMeasureInfo(
        tempNoteData, timing, measureInfoPerPlayer[PLAYER_2]);
  } else {
    measureInfoPerPlayer.resize(1);
    MeasureInfo::CalculateMeasureInfo(
        tempNoteData, timing, measureInfoPerPlayer[0]);
  }

  m_NotesPerMeasure.clear();
  m_NpsPerMeasure.clear();
  m_PeakNps.clear();

  for (MeasureInfo& mi : measureInfoPerPlayer) {
    m_NotesPerMeasure.push_back(mi.notesPerMeasure);
    m_NpsPerMeasure.push_back(mi.npsPerMeasure);
    m_PeakNps.push_back(mi.peakNps);
  }
}

void Steps::ChangeFilenamesForCustomSong() {
  m_sFilename = custom_songify_path(m_sFilename);
  if (!m_MusicFile.empty()) {
    m_MusicFile = custom_songify_path(m_MusicFile);
  }
}

void Steps::Decompress() const { const_cast<Steps*>(this)->Decompress(); }

void Steps::Decompress() {
  if (m_bNoteDataIsFilled) {
    return;  // already decompressed
  }

  if (parent) {
    // get autogen m_pNoteData
    NoteData notedata;
    parent->GetNoteData(notedata);

    m_bNoteDataIsFilled = true;

    int iNewTracks = GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks;

    if (this->m_StepsType == StepsType_lights_cabinet) {
      NoteDataUtil::LoadTransformedLights(notedata, *m_pNoteData, iNewTracks);
    } else {
      NoteDataUtil::LoadTransformedSlidingWindow(
          notedata, *m_pNoteData, iNewTracks);

      NoteDataUtil::RemoveStretch(*m_pNoteData, m_StepsType);
    }
    return;
  }

  if (!m_sFilename.empty() && m_sNoteDataCompressed.empty()) {
    // We have NoteData on disk and not in memory. Load it.
    if (!this->GetNoteDataFromSimfile()) {
      LOG->Warn(
          "Couldn't load the %s chart's NoteData from \"%s\"",
          DifficultyToString(m_Difficulty).c_str(), m_sFilename.c_str());
      return;
    }

    this->GetSMNoteData(m_sNoteDataCompressed);
  }

  if (m_sNoteDataCompressed.empty()) {
    /* there is no data, do nothing */
  } else {
    // load from compressed
    bool bComposite =
        GAMEMAN->GetStepsTypeInfo(m_StepsType).m_StepsTypeCategory ==
        StepsTypeCategory_Routine;
    m_bNoteDataIsFilled = true;
    m_pNoteData->SetNumTracks(
        GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks);

    NoteDataUtil::LoadFromSMNoteDataString(
        *m_pNoteData, m_sNoteDataCompressed, bComposite);
  }
}

void Steps::Compress() const {
  // Always leave lights data uncompressed.
  if (this->m_StepsType == StepsType_lights_cabinet && m_bNoteDataIsFilled) {
    m_sNoteDataCompressed = std::string();
    return;
  }

  // Don't compress data in the editor: it's still in use.
  if (GAMESTATE->m_bInStepEditor) {
    return;
  }

  if (!m_sFilename.empty() && m_LoadedFromProfile == ProfileSlot_Invalid) {
    /* We have a file on disk; clear all data in memory.
     * Data on profiles can't be accessed normally (need to mount and time-out
     * the device), and when we start a game and load edits, we want to be
     * sure that it'll be available if the user picks it and pulls the device.
     * Also, Decompress() doesn't know how to load .edits. */
    m_pNoteData->Init();
    m_bNoteDataIsFilled = false;

    /* Be careful; 'x = ""', m_sNoteDataCompressed.clear() and
     * m_sNoteDataCompressed.reserve(0) don't always free the allocated memory.
     */
    m_sNoteDataCompressed = std::string();
    return;
  }

  // We have no file on disk. Compress the data, if necessary.
  if (m_sNoteDataCompressed.empty()) {
    if (!m_bNoteDataIsFilled) {
      return; /* no data is no data */
    }
    NoteDataUtil::GetSMNoteDataString(
        *m_pNoteData, m_sNoteDataCompressed, /*bIncludeMeasureComments=*/true);
  }

  m_pNoteData->Init();
  m_bNoteDataIsFilled = false;
}

/* Copy our parent's data. This is done when we're being changed from autogen
 * to normal. (needed?) */
void Steps::DeAutogen(bool bCopyNoteData) {
  if (!parent) {
    return;  // OK
  }

  if (bCopyNoteData) {
    Decompress();  // fills in m_pNoteData with sliding window transform
  }

  m_sDescription = Real()->m_sDescription;
  m_sChartStyle = Real()->m_sChartStyle;
  m_Difficulty = Real()->m_Difficulty;
  m_iMeter = Real()->m_iMeter;
  std::copy(
      Real()->m_RadarValues, Real()->m_RadarValues + NUM_PLAYERS,
      m_RadarValues);
  std::copy(
      Real()->m_TechCounts, Real()->m_TechCounts + NUM_PLAYERS, m_TechCounts);

  m_NpsPerMeasure.assign(
      Real()->m_NpsPerMeasure.begin(), Real()->m_NpsPerMeasure.end());
  m_NotesPerMeasure.assign(
      Real()->m_NotesPerMeasure.begin(), Real()->m_NotesPerMeasure.end());

  m_sCredit = Real()->m_sCredit;
  parent = nullptr;

  if (bCopyNoteData) {
    Compress();
  }
}

void Steps::AutogenFrom(const Steps* parent_, StepsType ntTo) {
  parent = parent_;
  m_StepsType = ntTo;
  m_StepsTypeStr = GAMEMAN->GetStepsTypeInfo(ntTo).szName;
  m_Timing = parent->m_Timing;
}

void Steps::CopyFrom(
    Steps* pSource, StepsType ntTo,
    float fMusicLengthSeconds)  // pSource does not have to be of the same
                                // StepsType
{
  m_StepsType = ntTo;
  m_StepsTypeStr = GAMEMAN->GetStepsTypeInfo(ntTo).szName;
  NoteData noteData;
  pSource->GetNoteData(noteData);
  noteData.SetNumTracks(GAMEMAN->GetStepsTypeInfo(ntTo).iNumTracks);
  parent = nullptr;
  m_Timing = pSource->m_Timing;
  this->m_pSong = pSource->m_pSong;
  this->m_Attacks = pSource->m_Attacks;
  this->m_sAttackString = pSource->m_sAttackString;
  this->SetNoteData(noteData);
  this->SetDescription(pSource->GetDescription());
  this->SetDifficulty(pSource->GetDifficulty());
  this->SetMeter(pSource->GetMeter());
  this->CalculateStepStats(fMusicLengthSeconds);
}

void Steps::CreateBlank(StepsType ntTo) {
  m_StepsType = ntTo;
  m_StepsTypeStr = GAMEMAN->GetStepsTypeInfo(ntTo).szName;
  NoteData noteData;
  noteData.SetNumTracks(GAMEMAN->GetStepsTypeInfo(ntTo).iNumTracks);
  this->SetNoteData(noteData);
}

void Steps::SetDifficultyAndDescription(
    Difficulty dc, std::string sDescription) {
  DeAutogen();
  m_Difficulty = dc;
  m_sDescription = sDescription;
  if (GetDifficulty() == Difficulty_Edit) {
    MakeValidEditDescription(m_sDescription);
  }
}

void Steps::SetCredit(std::string sCredit) {
  DeAutogen();
  m_sCredit = sCredit;
}

void Steps::SetChartStyle(std::string sChartStyle) {
  DeAutogen();
  m_sChartStyle = sChartStyle;
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
  m_iMeter = meter;
}

const TimingData* Steps::GetTimingData() const {
  return m_Timing.empty() ? &m_pSong->m_SongTiming : &m_Timing;
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
    m_pSong->GetDisplayBpms(bpms);
    if (bpms.GetMax() - bpms.GetMin() > 3.000f) {
      return true;
    }
  }

  return false;
}

const std::string Steps::GetMusicPath() const {
  return Song::GetSongAssetPath(
      m_MusicFile.empty() ? m_pSong->m_sMusicFile : m_MusicFile,
      m_pSong->GetSongDir());
}

const std::string& Steps::GetMusicFile() const { return m_MusicFile; }

void Steps::SetMusicFile(const std::string& file) { m_MusicFile = file; }

void Steps::SetRadarValues(const RadarValues v[NUM_PLAYERS]) {
  DeAutogen();
  std::copy(v, v + NUM_PLAYERS, m_RadarValues);
}

void Steps::SetTechCounts(const TechCounts ts[NUM_PLAYERS]) {
  DeAutogen();
  std::copy(ts, ts + NUM_PLAYERS, m_TechCounts);
}

void Steps::SetNpsPerMeasure(std::vector<std::vector<float>>& npsPerMeasure) {
  DeAutogen();
  m_NpsPerMeasure.assign(npsPerMeasure.begin(), npsPerMeasure.end());
}

void Steps::SetNotesPerMeasure(std::vector<std::vector<int>>& notesPerMeasure) {
  DeAutogen();
  m_NotesPerMeasure.assign(notesPerMeasure.begin(), notesPerMeasure.end());
}

void Steps::SetPeakNps(std::vector<float>& peakNps) {
  m_PeakNps.assign(peakNps.begin(), peakNps.end());
}

const std::string Steps::GetGrooveStatsHash() const {
  return m_sGrooveStatsHash;
}

int Steps::GetGrooveStatsHashVersion() const {
  return m_iGrooveStatsHashVersion;
}

void Steps::CalculateGrooveStatsHash() {
  this->Decompress();

  std::string smNoteData = this->MinimizedChartString();

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
  m_sGrooveStatsHash = gsKey;
  m_iGrooveStatsHashVersion = CURRENT_GROOVE_STATS_HASH_VERSION;
}

std::string Steps::MinimizedChartString() {
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
  NoteData noteData;
  this->GetNoteData(noteData);
  NoteDataUtil::GetSMNoteDataString(
      noteData, smNoteData, /*bIncludeMeasureComments=*/false);

  if (smNoteData == "") {
    return "";
  }

  std::string minimizedNoteData = "";

  std::vector<std::string> measures;
  Regex anyNote("[^0]");

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
        if (anyNote.Compare(lines[i]) == true) {
          allZeroes = false;
          break;
        }
      }

      if (allZeroes) {
        // Iterate through lines, removing every other item.
        // Note that we're removing the odd indices, so we
        // call `++it;` and then erase the following line
        auto it = lines.begin();
        while (it != lines.end()) {
          ++it;
          if (it != lines.end()) {
            it = lines.erase(it);
          }
        }
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
  m_sGrooveStatsHash = key;
}

void Steps::SetGrooveStatsHashVersion(int version) {
  m_iGrooveStatsHashVersion = version;
}

std::string Steps::GenerateChartKey() {
  ChartKey = this->GenerateChartKey(*m_pNoteData, this->GetTimingData());
  return ChartKey;
}
std::string Steps::GetChartKey() {
  if (ChartKey.empty()) {
    this->Decompress();
    ChartKey = this->GenerateChartKey(*m_pNoteData, this->GetTimingData());
    this->Compress();
  }
  return ChartKey;
}
std::string Steps::GenerateChartKey(NoteData& nd, TimingData* td) {
  std::string k = "";
  std::string o = "";
  float bpm;
  nd.LogNonEmptyRows();
  std::vector<int>& nerv = nd.GetNonEmptyRowVector();

  std::string firstHalf = "";
  std::string secondHalf = "";

#pragma omp parallel sections
  {
#pragma omp section
    {
      for (size_t r = 0; r < nerv.size() / 2; r++) {
        int row = nerv[r];
        for (int t = 0; t < nd.GetNumTracks(); ++t) {
          const TapNote& tn = nd.GetTapNote(t, row);
          std::ostringstream os;
          os << tn.type;
          firstHalf.append(os.str());
        }
        bpm = td->GetBPMAtRow(row);
        std::ostringstream os;
        os << static_cast<int>(bpm + 0.374643f);
        firstHalf.append(os.str());
      }
    }

#pragma omp section
    {
      for (size_t r = nerv.size() / 2; r < nerv.size(); r++) {
        int row = nerv[r];
        for (int t = 0; t < nd.GetNumTracks(); ++t) {
          const TapNote& tn = nd.GetTapNote(t, row);
          std::ostringstream os;
          os << tn.type;
          secondHalf.append(os.str());
        }
        bpm = td->GetBPMAtRow(row);
        std::ostringstream os;
        os << static_cast<int>(bpm + 0.374643f);
        firstHalf.append(os.str());
      }
    }
  }
  k = firstHalf + secondHalf;

  // ChartKeyRecord = k;
  o.append("X");  // I was thinking of using "C" to indicate chart.. however.. X
                  // is cooler... - Mina
  o.append(BinaryToHex(CryptManager::GetSHA1ForString(k)));
  return o;
}

std::vector<ColumnCue> Steps::GetColumnCues(float minDuration) {
  // TODO: Should we worry about getting the right steps per player?
  // It seems like this is only necessary when dealing with Couples charts

  std::vector<ColumnCue> cues;
  NoteData noteData;
  this->GetNoteData(noteData);
  TimingData* timing = this->GetTimingData();
  ColumnCue::CalculateColumnCues(noteData, timing, cues, minDuration);
  return cues;
}

const std::vector<float>& Steps::GetNpsPerMeasure(PlayerNumber pn) const {
  // CachedNpsPerMeasure will only have separate sets of values per-player if
  // the steps type has different steps for each player (eg dance-couples,
  // dance-routine). Otherwise, it will only store one copy of the values (which
  // will be the case for like 99.9% of charts).

  static const std::vector<float> EMPTY_VECTOR;
  if (Real()->m_NpsPerMeasure.size() == 0) {
    return EMPTY_VECTOR;
  } else if (Real()->m_NpsPerMeasure.size() <= pn) {
    return Real()->m_NpsPerMeasure[PLAYER_1];
  } else {
    return Real()->m_NpsPerMeasure[pn];
  }
}

const std::vector<int>& Steps::GetNotesPerMeasure(PlayerNumber pn) const {
  // CachedNotesPerMeasure will only have separate sets of values per-player if
  // the steps type has different steps for each player (eg dance-couples,
  // dance-routine). Otherwise, it will only have one copy of the values (which
  // will be the case for like 99.9% of charts).
  static const std::vector<int> EMPTY_VECTOR;
  if (Real()->m_NotesPerMeasure.size() == 0) {
    return EMPTY_VECTOR;
  } else if (Real()->m_NotesPerMeasure.size() <= pn) {
    return Real()->m_NotesPerMeasure[PLAYER_1];
  } else {
    return Real()->m_NotesPerMeasure[pn];
  }
}

float Steps::GetPeakNps(PlayerNumber pn) const {
  if (Real()->m_PeakNps.size() == 0) {
    return 0;
  } else if (Real()->m_PeakNps.size() <= pn) {
    return Real()->m_PeakNps[PLAYER_1];
  } else {
    return Real()->m_PeakNps[pn];
  }
}

// lua start
#include "LuaBinding.h"
/** @brief Allow Lua to have access to the Steps. */
class LunaSteps : public Luna<Steps> {
 public:
  DEFINE_METHOD(GetStepsType, m_StepsType)
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
    std::vector<ColumnCue> cues = p->GetColumnCues(minDuration);
    lua_createtable(L, cues.size(), 0);

    for (unsigned i = 0; i < cues.size(); i++) {
      lua_newtable(L);
      lua_pushstring(L, "startTime");
      lua_pushnumber(L, cues[i].startTime);
      lua_settable(L, -3);

      lua_pushstring(L, "duration");
      lua_pushnumber(L, cues[i].duration);
      lua_settable(L, -3);

      lua_pushstring(L, "columns");
      lua_createtable(L, cues[i].columns.size(), 0);

      for (unsigned c = 0; c < cues[i].columns.size(); c++) {
        lua_newtable(L);
        lua_pushstring(L, "colNum");
        lua_pushinteger(L, cues[i].columns[c].colNum);
        lua_settable(L, -3);

        lua_pushstring(L, "noteType");
        lua_pushinteger(L, cues[i].columns[c].noteType);
        lua_settable(L, -3);

        lua_rawseti(L, -2, c + 1);
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
    ADD_METHOD(GetPeakNps);
    ADD_METHOD(GetGrooveStatsHash);
    ADD_METHOD(GetGrooveStatsHashVersion);
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
