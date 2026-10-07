#include "SongCacheBinary.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Attack.h"
#include "BackgroundUtil.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "NoteAnnotation.h"
#include "PlayerNumber.h"
#include "RadarValues.h"
#include "RageFile.h"
#include "RageLog.h"
#include "Song.h"
#include "Steps.h"
#include "TechCounts.h"
#include "TimingData.h"
#include "TimingSegments.h"

namespace {

constexpr char MAGIC[] = "ITGC";

// Little-endian byte stream helpers. All multi-byte values are written out
// byte by byte so the format is not tied to the host's endianness.

class Writer {
 public:
  void U8(uint8_t v) { buf_.push_back(static_cast<char>(v)); }
  void U16(uint16_t v) {
    U8(static_cast<uint8_t>(v));
    U8(static_cast<uint8_t>(v >> 8));
  }
  void U32(uint32_t v) {
    U16(static_cast<uint16_t>(v));
    U16(static_cast<uint16_t>(v >> 16));
  }
  void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
  void F32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    U32(bits);
  }
  void Bool(bool v) { U8(v ? 1 : 0); }
  void Str(const std::string& s) {
    U32(static_cast<uint32_t>(s.size()));
    buf_.append(s);
  }

  const std::string& Data() const { return buf_; }

 private:
  std::string buf_;
};

class Reader {
 public:
  Reader(const char* data, size_t size)
      : pos_(data), end_(data + size), failed_(false) {}

  bool Ok() const { return !failed_; }
  void Fail() { failed_ = true; }

  uint8_t U8() {
    if (pos_ >= end_) {
      failed_ = true;
      return 0;
    }
    return static_cast<uint8_t>(*pos_++);
  }
  uint16_t U16() {
    uint16_t lo = U8();
    uint16_t hi = U8();
    return static_cast<uint16_t>(lo | (hi << 8));
  }
  uint32_t U32() {
    uint32_t lo = U16();
    uint32_t hi = U16();
    return lo | (hi << 16);
  }
  int32_t I32() { return static_cast<int32_t>(U32()); }
  float F32() {
    uint32_t bits = U32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
  }
  bool Bool() { return U8() != 0; }
  std::string Str() {
    uint32_t size = U32();
    if (!Ok() || size > Remaining()) {
      failed_ = true;
      return std::string();
    }
    std::string s(pos_, size);
    pos_ += size;
    return s;
  }

  // Reads a field count.
  uint32_t Count() {
    uint32_t count = U32();
    if (!Ok()) {
      return 0;
    }
    // A count larger than this is treated as corrupted, we don't want the
    // callers to reserve too much memory in vectors.
    constexpr uint32_t kMaxCount = 1u << 24;
    if (count > kMaxCount) {
      failed_ = true;
      return 0;
    }
    return count;
  }

 private:
  size_t Remaining() const { return static_cast<size_t>(end_ - pos_); }

  const char* pos_;
  const char* end_;
  bool failed_;
};

template <typename T>
T CheckRange(Reader& r, uint32_t value, uint32_t count) {
  if (value >= count) {
    r.Fail();
    return static_cast<T>(0);
  }
  return static_cast<T>(value);
}

void WriteSteps(Writer& w, const Steps& steps);
void ReadSteps(Reader& r, Song& song);

// Song fields so we can use this macro in reader and writer so they don't get
// out of sync.

#define SONG_STRINGS(F)   \
  F(m_sMainTitle)         \
  F(m_sSubTitle)          \
  F(m_sArtist)            \
  F(m_sMainTitleTranslit) \
  F(m_sSubTitleTranslit)  \
  F(m_sArtistTranslit)    \
  F(m_sGenre)             \
  F(m_sOrigin)            \
  F(m_sCredit)            \
  F(m_sBannerFile)        \
  F(m_sBackgroundFile)    \
  F(m_sPreviewVidFile)    \
  F(m_sJacketFile)        \
  F(m_sCDFile)            \
  F(m_sDiscFile)          \
  F(m_sLyricsFile)        \
  F(m_sCDTitleFile)       \
  F(m_sMusicFile)         \
  F(m_PreviewFile)        \
  F(m_sSongFileName)

void WriteTiming(Writer& w, const TimingData& timing) {
  w.F32(timing.m_fBeat0OffsetInSeconds);
  FOREACH_TimingSegmentType(tst) {
    const std::vector<TimingSegment*>& segs = timing.GetTimingSegments(tst);
    w.U32(static_cast<uint32_t>(segs.size()));
    for (const TimingSegment* seg : segs) {
      w.I32(seg->GetRow());
      switch (tst) {
        case SEGMENT_BPM:
          w.F32(ToBPM(seg)->GetBPS());
          break;
        case SEGMENT_STOP:
          w.F32(ToStop(seg)->GetPause());
          break;
        case SEGMENT_DELAY:
          w.F32(ToDelay(seg)->GetPause());
          break;
        case SEGMENT_TIME_SIG: {
          const TimeSignatureSegment* s = ToTimeSignature(seg);
          w.I32(s->GetNum());
          w.I32(s->GetDen());
          break;
        }
        case SEGMENT_WARP:
          w.I32(ToWarp(seg)->GetLengthRows());
          break;
        case SEGMENT_LABEL:
          w.Str(static_cast<const LabelSegment*>(seg)->GetLabel());
          break;
        case SEGMENT_TICKCOUNT:
          w.I32(ToTickcount(seg)->GetTicks());
          break;
        case SEGMENT_COMBO: {
          const ComboSegment* s = ToCombo(seg);
          w.I32(s->GetCombo());
          w.I32(s->GetMissCombo());
          break;
        }
        case SEGMENT_SPEED: {
          const SpeedSegment* s = ToSpeed(seg);
          w.F32(s->GetRatio());
          w.F32(s->GetDelay());
          w.I32(static_cast<int32_t>(s->GetUnit()));
          break;
        }
        case SEGMENT_SCROLL:
          w.F32(ToScroll(seg)->GetRatio());
          break;
        case SEGMENT_FAKE:
          w.I32(ToFake(seg)->GetLengthRows());
          break;
        default:
          break;
      }
    }
  }
}

void ReadTiming(Reader& r, TimingData& timing) {
  timing.m_fBeat0OffsetInSeconds = r.F32();
  FOREACH_TimingSegmentType(tst) {
    std::vector<TimingSegment*>& segs = timing.GetTimingSegments(tst);
    // A label segment is at least its row plus a string length.
    const uint32_t count = r.Count();
    segs.reserve(count);
    for (uint32_t i = 0; i < count && r.Ok(); ++i) {
      const int row = r.I32();
      switch (tst) {
        case SEGMENT_BPM: {
          const float bps = r.F32();
          BPMSegment* seg = new BPMSegment(row);
          seg->SetBPS(bps);
          segs.push_back(seg);
          break;
        }
        case SEGMENT_STOP:
          segs.push_back(new StopSegment(row, r.F32()));
          break;
        case SEGMENT_DELAY:
          segs.push_back(new DelaySegment(row, r.F32()));
          break;
        case SEGMENT_TIME_SIG: {
          const int num = r.I32();
          const int den = r.I32();
          segs.push_back(new TimeSignatureSegment(row, num, den));
          break;
        }
        case SEGMENT_WARP:
          segs.push_back(new WarpSegment(row, r.I32()));
          break;
        case SEGMENT_LABEL:
          segs.push_back(new LabelSegment(row, r.Str()));
          break;
        case SEGMENT_TICKCOUNT:
          segs.push_back(new TickcountSegment(row, r.I32()));
          break;
        case SEGMENT_COMBO: {
          const int combo = r.I32();
          const int missCombo = r.I32();
          segs.push_back(new ComboSegment(row, combo, missCombo));
          break;
        }
        case SEGMENT_SPEED: {
          const float ratio = r.F32();
          const float delay = r.F32();
          const uint32_t unit = r.U32();
          const uint32_t kNumSpeedUnits = 2;
          segs.push_back(new SpeedSegment(
              row, ratio, delay,
              CheckRange<SpeedSegment::BaseUnit>(r, unit, kNumSpeedUnits)));
          break;
        }
        case SEGMENT_SCROLL:
          segs.push_back(new ScrollSegment(row, r.F32()));
          break;
        case SEGMENT_FAKE:
          segs.push_back(new FakeSegment(row, r.I32()));
          break;
        default:
          break;
      }
    }
  }
}

void WriteBackgroundChanges(
    Writer& w, const std::vector<BackgroundChange>& changes) {
  w.U32(static_cast<uint32_t>(changes.size()));
  for (const BackgroundChange& c : changes) {
    w.Str(c.m_def.m_sEffect);
    w.Str(c.m_def.m_sFile1);
    w.Str(c.m_def.m_sFile2);
    w.Str(c.m_def.m_sColor1);
    w.Str(c.m_def.m_sColor2);
    w.F32(c.m_fStartBeat);
    w.F32(c.m_fRate);
    w.Str(c.m_sTransition);
  }
}

void ReadBackgroundChanges(Reader& r, std::vector<BackgroundChange>& changes) {
  const uint32_t count = r.Count();
  changes.reserve(changes.size() + count);
  for (uint32_t i = 0; i < count && r.Ok(); ++i) {
    BackgroundChange c;
    c.m_def.m_sEffect = r.Str();
    c.m_def.m_sFile1 = r.Str();
    c.m_def.m_sFile2 = r.Str();
    c.m_def.m_sColor1 = r.Str();
    c.m_def.m_sColor2 = r.Str();
    c.m_fStartBeat = r.F32();
    c.m_fRate = r.F32();
    c.m_sTransition = r.Str();
    changes.push_back(c);
  }
}

void WriteAttacks(
    Writer& w, const std::vector<std::string>& strings,
    const AttackArray& attacks) {
  w.U32(static_cast<uint32_t>(strings.size()));
  for (const std::string& s : strings) {
    w.Str(s);
  }
  w.U32(static_cast<uint32_t>(attacks.size()));
  for (const Attack& a : attacks) {
    w.U8(static_cast<uint8_t>(a.level));
    w.F32(a.fStartSecond);
    w.F32(a.fSecsRemaining);
    w.Str(a.sModifiers);
    w.Bool(a.bOn);
    w.Bool(a.bGlobal);
    w.Bool(a.bShowInAttackList);
  }
}

void ReadAttacks(
    Reader& r, std::vector<std::string>& strings, AttackArray& attacks) {
  strings.clear();
  attacks.clear();
  const uint32_t stringCount = r.Count();
  strings.reserve(stringCount);
  for (uint32_t i = 0; i < stringCount && r.Ok(); ++i) {
    strings.push_back(r.Str());
  }
  const uint32_t attackCount = r.Count();
  attacks.reserve(attackCount);
  for (uint32_t i = 0; i < attackCount && r.Ok(); ++i) {
    Attack a;
    a.level = CheckRange<AttackLevel>(r, r.U8(), NUM_ATTACK_LEVELS);
    a.fStartSecond = r.F32();
    a.fSecsRemaining = r.F32();
    a.sModifiers = r.Str();
    a.bOn = r.Bool();
    a.bGlobal = r.Bool();
    a.bShowInAttackList = r.Bool();
    attacks.push_back(a);
  }
}

void WriteHeader(Writer& w) {
  w.Str(MAGIC);
  w.U32(static_cast<uint32_t>(FILE_CACHE_VERSION));
}

bool ReadHeader(Reader& r) {
  const std::string magic = r.Str();
  const uint32_t cacheVersion = r.U32();
  return r.Ok() && magic == MAGIC &&
         cacheVersion == static_cast<uint32_t>(FILE_CACHE_VERSION);
}

void WriteSong(Writer& w, const Song& song) {
#define WRITE_STRING(member) w.Str(song.member);
  SONG_STRINGS(WRITE_STRING)
#undef WRITE_STRING

  w.F32(song.m_fMusicLengthSeconds);
  w.F32(song.m_fMusicSampleStartSeconds);
  w.F32(song.m_fMusicSampleLengthSeconds);
  w.F32(song.m_fSpecifiedBPMMin);
  w.F32(song.m_fSpecifiedBPMMax);
  w.U8(static_cast<uint8_t>(song.m_DisplayBPMType));
  w.U8(static_cast<uint8_t>(song.m_SelectionDisplay));
  w.Bool(song.m_bHasMusic);
  w.Bool(song.m_bHasBanner);
  w.F32(song.GetFirstSecondNoOffset());
  w.F32(song.GetLastSecondNoOffset());
  w.F32(song.GetSpecifiedLastSecond());

  FOREACH_ENUM(InstrumentTrack, it) { w.Str(song.m_sInstrumentTrackFile[it]); }

  w.U32(static_cast<uint32_t>(song.m_vsKeysoundFile.size()));
  for (const std::string& s : song.m_vsKeysoundFile) {
    w.Str(s);
  }

  WriteAttacks(w, song.m_sAttackString, song.m_Attacks);
  WriteTiming(w, song.m_SongTiming);
  FOREACH_BackgroundLayer(bl) {
    WriteBackgroundChanges(w, song.GetBackgroundChanges(bl));
  }
  WriteBackgroundChanges(w, song.GetForegroundChanges());

  std::vector<const Steps*> steps;
  for (const Steps* s : song.GetAllSteps()) {
    if (!s->IsAutogen() && !s->WasLoadedFromProfile()) {
      steps.push_back(s);
    }
  }
  for (const Steps* s : song.GetUnknownStyleSteps()) {
    steps.push_back(s);
  }
  w.U32(static_cast<uint32_t>(steps.size()));
  for (const Steps* s : steps) {
    WriteSteps(w, *s);
  }
}

void ReadSong(Reader& r, Song& song) {
#define READ_STRING(member) song.member = r.Str();
  SONG_STRINGS(READ_STRING)
#undef READ_STRING

  song.m_fMusicLengthSeconds = r.F32();
  song.m_fMusicSampleStartSeconds = r.F32();
  song.m_fMusicSampleLengthSeconds = r.F32();
  song.m_fSpecifiedBPMMin = r.F32();
  song.m_fSpecifiedBPMMax = r.F32();
  song.m_DisplayBPMType = CheckRange<DisplayBPM>(r, r.U8(), NUM_DisplayBPM);
  song.m_SelectionDisplay = CheckRange<Song::SelectionDisplay>(r, r.U8(), 2);
  song.m_bHasMusic = r.Bool();
  song.m_bHasBanner = r.Bool();
  song.SetFirstSecondNoOffset(r.F32());
  song.SetLastSecondNoOffset(r.F32());
  song.SetSpecifiedLastSecond(r.F32());

  FOREACH_ENUM(InstrumentTrack, it) {
    song.m_sInstrumentTrackFile[it] = r.Str();
  }

  const uint32_t keysoundCount = r.Count();
  song.m_vsKeysoundFile.reserve(keysoundCount);
  for (uint32_t i = 0; i < keysoundCount && r.Ok(); ++i) {
    song.m_vsKeysoundFile.push_back(r.Str());
  }

  ReadAttacks(r, song.m_sAttackString, song.m_Attacks);
  ReadTiming(r, song.m_SongTiming);
  FOREACH_BackgroundLayer(bl) {
    ReadBackgroundChanges(r, song.GetBackgroundChanges(bl));
  }
  ReadBackgroundChanges(r, song.GetForegroundChanges());

  const uint32_t stepCount = r.Count();
  for (uint32_t i = 0; i < stepCount && r.Ok(); ++i) {
    ReadSteps(r, song);
  }
}

void WriteSteps(Writer& w, const Steps& steps) {
  w.Str(steps.m_StepsTypeStr);
  w.U32(static_cast<uint32_t>(steps.m_StepsType));
  w.Str(steps.GetChartName());
  w.Str(steps.GetDescription());
  w.Str(steps.GetChartStyle());
  w.U8(static_cast<uint8_t>(steps.GetDifficulty()));
  w.I32(steps.GetMeter());
  w.Str(steps.GetCredit());
  w.Str(steps.GetMusicFile());
  w.Str(steps.GetFilename());

  FOREACH_PlayerNumber(pn) {
    const RadarValues& rv = steps.GetRadarValues(pn);
    FOREACH_ENUM(RadarCategory, rc) { w.F32(rv[rc]); }
  }
  FOREACH_PlayerNumber(pn) {
    const TechCounts& tc = steps.GetTechCounts(pn);
    FOREACH_ENUM(TechCountsCategory, tcc) { w.F32(tc[tcc]); }
  }

  const std::vector<std::vector<float>>& nps = steps.GetAllNpsPerMeasures();
  w.U32(static_cast<uint32_t>(nps.size()));
  for (const std::vector<float>& playerNps : nps) {
    w.U32(static_cast<uint32_t>(playerNps.size()));
    for (float v : playerNps) {
      w.F32(v);
    }
  }
  const std::vector<std::vector<int>>& notes = steps.GetAllNotesPerMeasures();
  w.U32(static_cast<uint32_t>(notes.size()));
  for (const std::vector<int>& playerNotes : notes) {
    w.U32(static_cast<uint32_t>(playerNotes.size()));
    for (int v : playerNotes) {
      w.I32(v);
    }
  }
  const std::vector<float>& peakNps = steps.GetAllPeakNps();
  w.U32(static_cast<uint32_t>(peakNps.size()));
  for (float v : peakNps) {
    w.F32(v);
  }

  const std::vector<NoteAnnotationCache>& annotations =
      steps.GetNoteAnnotationCaches();
  w.U32(static_cast<uint32_t>(annotations.size()));
  for (const NoteAnnotationCache& a : annotations) {
    w.Str(a.GetCompressed());
  }

  w.Str(steps.GetGrooveStatsHash());
  w.I32(steps.GetGrooveStatsHashVersion());
  w.U8(static_cast<uint8_t>(steps.GetDisplayBPM()));
  w.F32(steps.GetMinBPM());
  w.F32(steps.GetMaxBPM());

  WriteAttacks(w, steps.m_sAttackString, steps.m_Attacks);

  w.Bool(!steps.m_Timing.empty());
  if (!steps.m_Timing.empty()) {
    WriteTiming(w, steps.m_Timing);
  }
}

void ReadSteps(Reader& r, Song& song) {
  Steps* steps = song.CreateSteps();
  steps->m_StepsTypeStr = r.Str();
  // StepsType_Invalid (== NUM_StepsType) is written for unknown-style steps.
  steps->m_StepsType = CheckRange<StepsType>(r, r.U32(), NUM_StepsType + 1);
  steps->SetChartName(r.Str());
  steps->SetDescription(r.Str());
  steps->SetChartStyle(r.Str());
  steps->SetDifficulty(CheckRange<Difficulty>(r, r.U8(), NUM_Difficulty));
  steps->SetMeter(r.I32());
  steps->SetCredit(r.Str());
  steps->SetMusicFile(r.Str());
  steps->SetFilename(r.Str());

  RadarValues radar[NUM_PLAYERS];
  FOREACH_PlayerNumber(pn) {
    FOREACH_ENUM(RadarCategory, rc) { radar[pn][rc] = r.F32(); }
  }
  steps->SetRadarValues(radar);

  TechCounts tech[NUM_PLAYERS];
  FOREACH_PlayerNumber(pn) {
    FOREACH_ENUM(TechCountsCategory, tcc) { tech[pn][tcc] = r.F32(); }
  }
  steps->SetTechCounts(tech);

  std::vector<std::vector<float>> nps;
  const uint32_t npsPlayers = r.Count();
  nps.reserve(npsPlayers);
  for (uint32_t i = 0; i < npsPlayers && r.Ok(); ++i) {
    const uint32_t measures = r.Count();
    std::vector<float> playerNps(measures);
    for (uint32_t j = 0; j < measures && r.Ok(); ++j) {
      playerNps[j] = r.F32();
    }
    nps.push_back(playerNps);
  }
  steps->SetNpsPerMeasure(nps);

  std::vector<std::vector<int>> notes;
  const uint32_t notesPlayers = r.Count();
  notes.reserve(notesPlayers);
  for (uint32_t i = 0; i < notesPlayers && r.Ok(); ++i) {
    const uint32_t measures = r.Count();
    std::vector<int> playerNotes(measures);
    for (uint32_t j = 0; j < measures && r.Ok(); ++j) {
      playerNotes[j] = r.I32();
    }
    notes.push_back(playerNotes);
  }
  steps->SetNotesPerMeasure(notes);

  std::vector<float> peakNps;
  const uint32_t peakCount = r.Count();
  peakNps.reserve(peakCount);
  for (uint32_t i = 0; i < peakCount && r.Ok(); ++i) {
    peakNps.push_back(r.F32());
  }
  steps->SetPeakNps(peakNps);

  const uint32_t annotationCount = r.Count();
  std::vector<NoteAnnotationCache> annotations;
  annotations.reserve(annotationCount);
  for (uint32_t i = 0; i < annotationCount && r.Ok(); ++i) {
    NoteAnnotationCache cache;
    cache.compressed = r.Str();
    annotations.push_back(cache);
  }
  steps->SetNoteAnnotations(annotations);

  steps->SetGrooveStatsHash(r.Str());
  steps->SetGrooveStatsHashVersion(r.I32());
  steps->SetDisplayBPM(CheckRange<DisplayBPM>(r, r.U8(), NUM_DisplayBPM));
  steps->SetMinBPM(r.F32());
  steps->SetMaxBPM(r.F32());

  ReadAttacks(r, steps->m_sAttackString, steps->m_Attacks);

  if (r.Bool()) {
    ReadTiming(r, steps->m_Timing);
  }

  song.AddSteps(steps);
}

}  // namespace

namespace SongCacheBinary {

bool Write(const Song& song, const std::string& path) {
  Writer w;
  WriteHeader(w);
  WriteSong(w, song);

  RageFile f;
  if (!f.Open(path, RageFile::WRITE)) {
    LOG->UserLog(
        "Song cache file", path, "couldn't be opened for writing: %s",
        f.GetError().c_str());
    return false;
  }
  if (f.Write(w.Data()) != static_cast<int>(w.Data().size())) {
    LOG->UserLog(
        "Song cache file", path, "couldn't be written: %s",
        f.GetError().c_str());
    return false;
  }
  return f.Flush() != -1;
}

bool Read(Song& song, const std::string& path) {
  RageFile f;
  if (!f.Open(path, RageFile::READ)) {
    return false;
  }
  std::string data;
  if (f.Read(data) == -1) {
    return false;
  }

  Reader r(data.data(), data.size());
  if (!ReadHeader(r)) {
    return false;
  }

  song.m_SongTiming.m_sFile = path;
  song.m_sSongFileName = path;
  song.m_fVersion = STEPFILE_VERSION_NUMBER;

  ReadSong(r, song);
  if (!r.Ok()) {
    return false;
  }

  song.TidyUpData(true);
  return true;
}

}  // namespace SongCacheBinary
