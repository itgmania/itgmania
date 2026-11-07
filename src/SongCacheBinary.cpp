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
  F(main_title_)          \
  F(sub_title_)           \
  F(artist_)              \
  F(main_title_translit_) \
  F(sub_title_translit_)  \
  F(artist_translit_)     \
  F(genre_)               \
  F(origin_)              \
  F(credit_)              \
  F(banner_file_)         \
  F(background_file_)     \
  F(preview_vid_file_)    \
  F(jacket_file_)         \
  F(cd_file_)             \
  F(disc_file_)           \
  F(lyrics_file_)         \
  F(cd_title_file_)       \
  F(music_file_)          \
  F(preview_file_)        \
  F(song_file_name_)

void WriteTiming(Writer& w, const TimingData& timing) {
  w.F32(timing.beat0_offset_in_seconds_);
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
  timing.beat0_offset_in_seconds_ = r.F32();
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
          segs.push_back(new WarpSegment(row, NoteRowToBeat(r.I32())));
          break;
        case SEGMENT_LABEL:
          segs.push_back(new LabelSegment(row, r.Str()));
          break;
        case SEGMENT_TICKCOUNT:
          segs.push_back(new TickcountSegment(row, r.I32()));
          break;
        case SEGMENT_COMBO: {
          const int combo = r.I32();
          const int miss_combo = r.I32();
          segs.push_back(new ComboSegment(row, combo, miss_combo));
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
          segs.push_back(new FakeSegment(row, NoteRowToBeat(r.I32())));
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
    w.Str(c.def_.effect_);
    w.Str(c.def_.file1_);
    w.Str(c.def_.file2_);
    w.Str(c.def_.color1_);
    w.Str(c.def_.color2_);
    w.F32(c.start_beat_);
    w.F32(c.rate_);
    w.Str(c.transition_);
  }
}

void ReadBackgroundChanges(Reader& r, std::vector<BackgroundChange>& changes) {
  const uint32_t count = r.Count();
  changes.reserve(changes.size() + count);
  for (uint32_t i = 0; i < count && r.Ok(); ++i) {
    BackgroundChange c;
    c.def_.effect_ = r.Str();
    c.def_.file1_ = r.Str();
    c.def_.file2_ = r.Str();
    c.def_.color1_ = r.Str();
    c.def_.color2_ = r.Str();
    c.start_beat_ = r.F32();
    c.rate_ = r.F32();
    c.transition_ = r.Str();
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
    w.U8(static_cast<uint8_t>(a.level_));
    w.F32(a.start_second_);
    w.F32(a.secs_remaining_);
    w.Str(a.modifiers_);
    w.Bool(a.on_);
    w.Bool(a.global_);
    w.Bool(a.show_in_attack_list_);
  }
}

void ReadAttacks(
    Reader& r, std::vector<std::string>& strings, AttackArray& attacks) {
  strings.clear();
  attacks.clear();
  const uint32_t string_count = r.Count();
  strings.reserve(string_count);
  for (uint32_t i = 0; i < string_count && r.Ok(); ++i) {
    strings.push_back(r.Str());
  }
  const uint32_t attack_count = r.Count();
  attacks.reserve(attack_count);
  for (uint32_t i = 0; i < attack_count && r.Ok(); ++i) {
    Attack a;
    a.level_ = CheckRange<AttackLevel>(r, r.U8(), NUM_ATTACK_LEVELS);
    a.start_second_ = r.F32();
    a.secs_remaining_ = r.F32();
    a.modifiers_ = r.Str();
    a.on_ = r.Bool();
    a.global_ = r.Bool();
    a.show_in_attack_list_ = r.Bool();
    attacks.push_back(a);
  }
}

void WriteHeader(Writer& w) {
  w.Str(MAGIC);
  w.U32(static_cast<uint32_t>(FILE_CACHE_VERSION));
}

bool ReadHeader(Reader& r) {
  const std::string magic = r.Str();
  const uint32_t cache_version = r.U32();
  return r.Ok() && magic == MAGIC &&
         cache_version == static_cast<uint32_t>(FILE_CACHE_VERSION);
}

void WriteSong(Writer& w, const Song& song) {
#define WRITE_STRING(member) w.Str(song.member);
  SONG_STRINGS(WRITE_STRING)
#undef WRITE_STRING

  w.F32(song.music_length_seconds_);
  w.F32(song.music_sample_start_seconds_);
  w.F32(song.music_sample_length_seconds_);
  w.F32(song.specified_bpm_min_);
  w.F32(song.specified_bpm_max_);
  w.U8(static_cast<uint8_t>(song.display_bpm_type_));
  w.U8(static_cast<uint8_t>(song.selection_display_));
  w.Bool(song.has_music_);
  w.Bool(song.has_banner_);
  w.F32(song.GetFirstSecondNoOffset());
  w.F32(song.GetLastSecondNoOffset());
  w.F32(song.GetSpecifiedLastSecond());

  w.U32(static_cast<uint32_t>(song.keysound_file_.size()));
  for (const std::string& s : song.keysound_file_) {
    w.Str(s);
  }

  WriteAttacks(w, song.attack_string_, song.attacks_);
  WriteTiming(w, song.song_timing_);
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

  song.music_length_seconds_ = r.F32();
  song.music_sample_start_seconds_ = r.F32();
  song.music_sample_length_seconds_ = r.F32();
  song.specified_bpm_min_ = r.F32();
  song.specified_bpm_max_ = r.F32();
  song.display_bpm_type_ = CheckRange<DisplayBPM>(r, r.U8(), NUM_DisplayBPM);
  song.selection_display_ = CheckRange<Song::SelectionDisplay>(r, r.U8(), 2);
  song.has_music_ = r.Bool();
  song.has_banner_ = r.Bool();
  song.SetFirstSecondNoOffset(r.F32());
  song.SetLastSecondNoOffset(r.F32());
  song.SetSpecifiedLastSecond(r.F32());

  const uint32_t keysound_count = r.Count();
  song.keysound_file_.reserve(keysound_count);
  for (uint32_t i = 0; i < keysound_count && r.Ok(); ++i) {
    song.keysound_file_.push_back(r.Str());
  }

  ReadAttacks(r, song.attack_string_, song.attacks_);
  ReadTiming(r, song.song_timing_);
  FOREACH_BackgroundLayer(bl) {
    ReadBackgroundChanges(r, song.GetBackgroundChanges(bl));
  }
  ReadBackgroundChanges(r, song.GetForegroundChanges());

  const uint32_t step_count = r.Count();
  for (uint32_t i = 0; i < step_count && r.Ok(); ++i) {
    ReadSteps(r, song);
  }
}

void WriteSteps(Writer& w, const Steps& steps) {
  w.Str(steps.steps_type_str_);
  w.U32(static_cast<uint32_t>(steps.steps_type_));
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
  for (const std::vector<float>& player_nps : nps) {
    w.U32(static_cast<uint32_t>(player_nps.size()));
    for (float v : player_nps) {
      w.F32(v);
    }
  }
  const std::vector<std::vector<int>>& notes = steps.GetAllNotesPerMeasures();
  w.U32(static_cast<uint32_t>(notes.size()));
  for (const std::vector<int>& player_notes : notes) {
    w.U32(static_cast<uint32_t>(player_notes.size()));
    for (int v : player_notes) {
      w.I32(v);
    }
  }
  const std::vector<float>& peak_nps = steps.GetAllPeakNps();
  w.U32(static_cast<uint32_t>(peak_nps.size()));
  for (float v : peak_nps) {
    w.F32(v);
  }

  const std::vector<StreamBreakdown>& breakdowns =
      steps.GetAllStreamBreakdowns();
  for (const StreamBreakdown& breakdown : breakdowns) {
    for (const std::string& text : breakdown.texts_) {
      w.Str(text);
    }
    w.I32(breakdown.total_stream_measures_);
    w.I32(breakdown.total_break_measures_);
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

  WriteAttacks(w, steps.GetStepsAttackStrings(), steps.GetStepsAttacks());

  w.Bool(!steps.timing_.empty());
  if (!steps.timing_.empty()) {
    WriteTiming(w, steps.timing_);
  }
}

void ReadSteps(Reader& r, Song& song) {
  Steps* steps = song.CreateSteps();
  steps->steps_type_str_ = r.Str();
  // StepsType_Invalid is written for unknown-style steps.
  steps->steps_type_ = CheckRange<StepsType>(
      r, r.U32(), static_cast<uint32_t>(StepsType_Invalid) + 1);
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
  const uint32_t nps_players = r.Count();
  nps.reserve(nps_players);
  for (uint32_t i = 0; i < nps_players && r.Ok(); ++i) {
    const uint32_t measures = r.Count();
    std::vector<float> player_nps(measures);
    for (uint32_t j = 0; j < measures && r.Ok(); ++j) {
      player_nps[j] = r.F32();
    }
    nps.push_back(player_nps);
  }
  steps->SetNpsPerMeasure(nps);

  std::vector<std::vector<int>> notes;
  const uint32_t notes_players = r.Count();
  notes.reserve(notes_players);
  for (uint32_t i = 0; i < notes_players && r.Ok(); ++i) {
    const uint32_t measures = r.Count();
    std::vector<int> player_notes(measures);
    for (uint32_t j = 0; j < measures && r.Ok(); ++j) {
      player_notes[j] = r.I32();
    }
    notes.push_back(player_notes);
  }
  steps->SetNotesPerMeasure(notes);

  std::vector<float> peak_nps;
  const uint32_t peak_count = r.Count();
  peak_nps.reserve(peak_count);
  for (uint32_t i = 0; i < peak_count && r.Ok(); ++i) {
    peak_nps.push_back(r.F32());
  }
  steps->SetPeakNps(peak_nps);

  std::vector<StreamBreakdown> breakdowns(notes.size());
  for (size_t i = 0; i < notes.size() && r.Ok(); ++i) {
    StreamBreakdown& breakdown = breakdowns[i];
    for (std::string& text : breakdown.texts_) {
      text = r.Str();
    }
    breakdown.total_stream_measures_ = r.I32();
    breakdown.total_break_measures_ = r.I32();
  }
  steps->SetStreamBreakdown(breakdowns);

  const uint32_t annotation_count = r.Count();
  std::vector<NoteAnnotationCache> annotations;
  annotations.reserve(annotation_count);
  for (uint32_t i = 0; i < annotation_count && r.Ok(); ++i) {
    NoteAnnotationCache cache;
    cache.compressed_ = r.Str();
    annotations.push_back(cache);
  }
  steps->SetNoteAnnotations(annotations);

  steps->SetGrooveStatsHash(r.Str());
  steps->SetGrooveStatsHashVersion(r.I32());
  steps->SetDisplayBPM(CheckRange<DisplayBPM>(r, r.U8(), NUM_DisplayBPM));
  steps->SetMinBPM(r.F32());
  steps->SetMaxBPM(r.F32());

  ReadAttacks(r, steps->GetStepsAttackStrings(), steps->GetStepsAttacks());

  if (r.Bool()) {
    ReadTiming(r, steps->timing_);
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

bool Read(Song& song, const std::string& path, bool tidy_up_data) {
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

  song.song_timing_.file_ = path;
  song.song_file_name_ = path;
  song.version_ = STEPFILE_VERSION_NUMBER;

  ReadSong(r, song);
  if (!r.Ok()) {
    return false;
  }

  if (tidy_up_data) {
    song.TidyUpData(true);
  }
  return true;
}

}  // namespace SongCacheBinary
