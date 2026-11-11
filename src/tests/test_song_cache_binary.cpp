#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "Attack.h"
#include "BackgroundUtil.h"
#include "GameManager.h"
#include "LuaManager.h"
#include "NoteAnnotation.h"
#include "PlayerNumber.h"
#include "RadarValues.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "Song.h"
#include "SongCacheBinary.h"
#include "Steps.h"
#include "StreamBreakdown.h"
#include "TechCounts.h"
#include "TimingData.h"
#include "TimingSegments.h"

namespace {

const char* kCacheDir = "/song_cache_binary_test/";

std::string CachePath(const std::string& name) {
  return std::string(kCacheDir) + name;
}

bool WriteBytes(const std::string& path, const std::string& data) {
  RageFile file;
  if (!file.Open(path, RageFile::WRITE)) {
    return false;
  }
  const bool ok = file.Write(data) == static_cast<int>(data.size());
  file.Close();
  return ok;
}

void AppendU32(std::string& out, uint32_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>((value >> 16) & 0xff));
  out.push_back(static_cast<char>((value >> 24) & 0xff));
}

void AddAttacks(AttackArray& attacks, std::vector<std::string>& strings) {
  strings = {"attack one", ""};
  attacks.push_back(
      Attack(ATTACK_LEVEL_1, 1.0f, 2.0f, "mod one", true, false, true));
  attacks.push_back(
      Attack(ATTACK_LEVEL_3, 3.0f, 4.0f, "mod two", false, true, false));
}

void ExpectAttacksEqual(
    const AttackArray& expected,
    const std::vector<std::string>& expected_strings, const AttackArray& actual,
    const std::vector<std::string>& actual_strings) {
  ASSERT_EQ(expected_strings, actual_strings);
  ASSERT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].level_, actual[i].level_);
    EXPECT_FLOAT_EQ(expected[i].start_second_, actual[i].start_second_);
    EXPECT_FLOAT_EQ(expected[i].secs_remaining_, actual[i].secs_remaining_);
    EXPECT_EQ(expected[i].modifiers_, actual[i].modifiers_);
    EXPECT_EQ(expected[i].on_, actual[i].on_);
    EXPECT_EQ(expected[i].global_, actual[i].global_);
    EXPECT_EQ(expected[i].show_in_attack_list_, actual[i].show_in_attack_list_);
  }
}

void ExpectBackgroundChangesEqual(
    const std::vector<BackgroundChange>& expected,
    const std::vector<BackgroundChange>& actual) {
  ASSERT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].def_.effect_, actual[i].def_.effect_);
    EXPECT_EQ(expected[i].def_.file1_, actual[i].def_.file1_);
    EXPECT_EQ(expected[i].def_.file2_, actual[i].def_.file2_);
    EXPECT_EQ(expected[i].def_.color1_, actual[i].def_.color1_);
    EXPECT_EQ(expected[i].def_.color2_, actual[i].def_.color2_);
    EXPECT_FLOAT_EQ(expected[i].start_beat_, actual[i].start_beat_);
    EXPECT_FLOAT_EQ(expected[i].rate_, actual[i].rate_);
    EXPECT_EQ(expected[i].transition_, actual[i].transition_);
  }
}

void AddTimingSegments(TimingData& timing) {
  timing.beat0_offset_in_seconds_ = 1.25f;
  timing.AddSegment(BPMSegment(0, 120.0f));
  timing.AddSegment(BPMSegment(BeatToNoteRow(16.0f), 93.5f));
  timing.AddSegment(StopSegment(BeatToNoteRow(8.0f), 0.25f));
  timing.AddSegment(DelaySegment(BeatToNoteRow(12.0f), 0.5f));
  timing.AddSegment(TimeSignatureSegment(0, 7, 8));
  timing.AddSegment(WarpSegment(BeatToNoteRow(32.0f), 2.0f));
  timing.AddSegment(LabelSegment(BeatToNoteRow(4.0f), "Intro"));
  timing.AddSegment(TickcountSegment(0, 8));
  timing.AddSegment(ComboSegment(BeatToNoteRow(20.0f), 5, 2));
  timing.AddSegment(SpeedSegment(
      BeatToNoteRow(24.0f), 1.5f, 0.1f, SpeedSegment::UNIT_SECONDS));
  timing.AddSegment(ScrollSegment(BeatToNoteRow(28.0f), 2.0f));
  timing.AddSegment(FakeSegment(BeatToNoteRow(36.0f), 1.5f));
}

Steps* MakeBasicSteps(
    Song& song, StepsType type, Difficulty difficulty,
    const std::string& chart_name) {
  Steps* steps = song.CreateSteps();
  steps->steps_type_ = type;
  steps->steps_type_str_ = "dance-single";
  steps->SetChartName(chart_name);
  steps->SetDescription("Description");
  steps->SetChartStyle("Style");
  steps->SetDifficulty(difficulty);
  steps->SetMeter(11);
  steps->SetCredit("Credit");
  steps->SetMusicFile("music.ogg");
  steps->SetFilename("song.ssc");
  return steps;
}

Steps* MakeFullSteps(Song& song) {
  Steps* steps =
      MakeBasicSteps(song, StepsType_dance_single, Difficulty_Hard, "Full");
  steps->SetDescription("Full chart");
  steps->SetChartStyle("Full style");
  steps->SetMeter(13);
  steps->SetCredit("Full credit");

  RadarValues radar[kNumPlayers];
  TechCounts tech[kNumPlayers];
  for (int pn = 0; pn < kNumPlayers; ++pn) {
    for (int rc = 0; rc < NUM_RadarCategory; ++rc) {
      radar[pn][rc] = static_cast<float>(100 * pn + rc) + 0.5f;
    }
    for (int tc = 0; tc < NUM_TechCountsCategory; ++tc) {
      tech[pn][tc] = static_cast<float>(200 * pn + tc) + 0.25f;
    }
  }
  steps->SetRadarValues(radar);
  steps->SetTechCounts(tech);

  std::vector<std::vector<float>> nps = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f}};
  std::vector<std::vector<int>> notes = {{10, 20, 30}, {40, 50}};
  std::vector<float> peak_nps = {1.5f, 4.5f};
  steps->SetNpsPerMeasure(nps);
  steps->SetNotesPerMeasure(notes);
  steps->SetPeakNps(peak_nps);

  std::vector<StreamBreakdown> breakdowns(notes.size());
  breakdowns[0].texts_ = {"first-a", "first-b", "first-c"};
  breakdowns[0].total_stream_measures_ = 7;
  breakdowns[0].total_break_measures_ = 3;
  breakdowns[1].texts_ = {"second-a", "second-b", "second-c"};
  breakdowns[1].total_stream_measures_ = 4;
  breakdowns[1].total_break_measures_ = 6;
  steps->SetStreamBreakdown(breakdowns);

  std::vector<NoteAnnotationCache> annotations(2);
  annotations[0].compressed_ = "annotation-one";
  annotations[1].compressed_ = "";
  steps->SetNoteAnnotations(annotations);

  steps->SetGrooveStatsHash("groove-hash");
  steps->SetGrooveStatsHashVersion(3);
  steps->SetDisplayBPM(DISPLAY_BPM_SPECIFIED);
  steps->SetMinBPM(101.0f);
  steps->SetMaxBPM(202.0f);

  AddAttacks(steps->GetStepsAttacks(), steps->GetStepsAttackStrings());
  AddTimingSegments(steps->timing_);
  return steps;
}

void PopulateFullSong(Song& song) {
  song.song_file_name_ = "song.ssc";
  song.main_title_ = "Main Title";
  song.sub_title_ = "Sub Title";
  song.artist_ = "Artist";
  song.main_title_translit_ = "Main Title Translit";
  song.sub_title_translit_ = "Sub Title Translit";
  song.artist_translit_ = "Artist Translit";
  song.genre_ = "Genre";
  song.origin_ = "Origin";
  song.credit_ = std::string("Cred\0it", 7);
  song.banner_file_ = "banner.png";
  song.background_file_ = "background.png";
  song.preview_vid_file_ = "preview.mp4";
  song.jacket_file_ = "jacket.png";
  song.cd_file_ = "cd.png";
  song.disc_file_ = "disc.png";
  song.lyrics_file_ = "lyrics.lrc";
  song.cd_title_file_ = "cdtitle.png";
  song.music_file_ = "music.ogg";
  song.preview_file_ = "preview.ogg";

  song.music_length_seconds_ = 123.5f;
  song.music_sample_start_seconds_ = 12.25f;
  song.music_sample_length_seconds_ = 8.75f;
  song.specified_bpm_min_ = 100.0f;
  song.specified_bpm_max_ = 200.0f;
  song.display_bpm_type_ = DISPLAY_BPM_SPECIFIED;
  song.selection_display_ = Song::SHOW_NEVER;
  song.has_music_ = true;
  song.has_banner_ = true;
  song.SetFirstSecondNoOffset(2.0f);
  song.SetLastSecondNoOffset(42.0f);
  song.SetSpecifiedLastSecond(45.0f);

  song.keysound_file_ = {"one.wav", "", "three.wav"};
  AddAttacks(song.attacks_, song.attack_string_);
  AddTimingSegments(song.song_timing_);

  BackgroundChange bg1;
  bg1.def_.effect_ = "effect1";
  bg1.def_.file1_ = "bg1.png";
  bg1.def_.file2_ = "bg1b.png";
  bg1.def_.color1_ = "0,1,0,1";
  bg1.def_.color2_ = "0,0,1,1";
  bg1.start_beat_ = 1.5f;
  bg1.rate_ = 1.25f;
  bg1.transition_ = "transition1";
  song.AddBackgroundChange(BACKGROUND_LAYER_1, bg1);

  BackgroundChange bg2;
  bg2.def_.effect_ = "effect2";
  bg2.def_.file1_ = "bg2.png";
  bg2.def_.file2_ = "";
  bg2.def_.color1_ = "";
  bg2.def_.color2_ = "1,1,1,1";
  bg2.start_beat_ = 2.5f;
  bg2.rate_ = 0.75f;
  bg2.transition_ = "";
  song.AddBackgroundChange(BACKGROUND_LAYER_2, bg2);

  BackgroundChange foreground;
  foreground.def_.effect_ = "foreground effect";
  foreground.def_.file1_ = "foreground.png";
  foreground.def_.file2_ = "foreground2.png";
  foreground.def_.color1_ = "1,0,0,1";
  foreground.def_.color2_ = "";
  foreground.start_beat_ = 3.5f;
  foreground.rate_ = 1.5f;
  foreground.transition_ = "foreground transition";
  song.AddForegroundChange(foreground);

  song.AddSteps(MakeFullSteps(song));

  Steps* unknown = MakeBasicSteps(
      song, StepsType_Invalid, Difficulty_Medium, "Unknown style");
  unknown->steps_type_str_ = "future-single";
  unknown->SetDescription("Unknown description");
  unknown->SetChartStyle("Unknown style");
  unknown->SetMeter(7);
  unknown->SetCredit("Unknown credit");
  song.AddSteps(unknown);
}

void ExpectStepsEqual(const Steps& expected, const Steps& actual) {
  EXPECT_EQ(expected.steps_type_, actual.steps_type_);
  EXPECT_EQ(expected.steps_type_str_, actual.steps_type_str_);
  EXPECT_EQ(expected.GetChartName(), actual.GetChartName());
  EXPECT_EQ(expected.GetDescription(), actual.GetDescription());
  EXPECT_EQ(expected.GetChartStyle(), actual.GetChartStyle());
  EXPECT_EQ(expected.GetDifficulty(), actual.GetDifficulty());
  EXPECT_EQ(expected.GetMeter(), actual.GetMeter());
  EXPECT_EQ(expected.GetCredit(), actual.GetCredit());
  EXPECT_EQ(expected.GetMusicFile(), actual.GetMusicFile());
  EXPECT_EQ(expected.GetFilename(), actual.GetFilename());

  for (int pn = 0; pn < kNumPlayers; ++pn) {
    const PlayerNumber player = static_cast<PlayerNumber>(pn);
    EXPECT_EQ(expected.GetRadarValues(player), actual.GetRadarValues(player));
    EXPECT_EQ(expected.GetTechCounts(player), actual.GetTechCounts(player));
  }
  EXPECT_EQ(expected.GetAllNpsPerMeasures(), actual.GetAllNpsPerMeasures());
  EXPECT_EQ(expected.GetAllNotesPerMeasures(), actual.GetAllNotesPerMeasures());
  EXPECT_EQ(expected.GetAllPeakNps(), actual.GetAllPeakNps());

  const std::vector<StreamBreakdown>& expected_breakdowns =
      expected.GetAllStreamBreakdowns();
  const std::vector<StreamBreakdown>& actual_breakdowns =
      actual.GetAllStreamBreakdowns();
  ASSERT_EQ(expected_breakdowns.size(), actual_breakdowns.size());
  for (size_t i = 0; i < expected_breakdowns.size(); ++i) {
    EXPECT_EQ(expected_breakdowns[i].texts_, actual_breakdowns[i].texts_);
    EXPECT_EQ(
        expected_breakdowns[i].total_stream_measures_,
        actual_breakdowns[i].total_stream_measures_);
    EXPECT_EQ(
        expected_breakdowns[i].total_break_measures_,
        actual_breakdowns[i].total_break_measures_);
  }

  const std::vector<NoteAnnotationCache>& expected_annotations =
      expected.GetNoteAnnotationCaches();
  const std::vector<NoteAnnotationCache>& actual_annotations =
      actual.GetNoteAnnotationCaches();
  ASSERT_EQ(expected_annotations.size(), actual_annotations.size());
  for (size_t i = 0; i < expected_annotations.size(); ++i) {
    EXPECT_EQ(
        expected_annotations[i].GetCompressed(),
        actual_annotations[i].GetCompressed());
  }

  EXPECT_EQ(expected.GetGrooveStatsHash(), actual.GetGrooveStatsHash());
  EXPECT_EQ(
      expected.GetGrooveStatsHashVersion(), actual.GetGrooveStatsHashVersion());
  EXPECT_EQ(expected.GetDisplayBPM(), actual.GetDisplayBPM());
  EXPECT_FLOAT_EQ(expected.GetMinBPM(), actual.GetMinBPM());
  EXPECT_FLOAT_EQ(expected.GetMaxBPM(), actual.GetMaxBPM());

  ExpectAttacksEqual(
      expected.GetStepsAttacks(), expected.GetStepsAttackStrings(),
      actual.GetStepsAttacks(), actual.GetStepsAttackStrings());
  EXPECT_EQ(expected.timing_, actual.timing_);
}

void ExpectSongsEqual(const Song& expected, const Song& actual) {
  EXPECT_EQ(expected.main_title_, actual.main_title_);
  EXPECT_EQ(expected.sub_title_, actual.sub_title_);
  EXPECT_EQ(expected.artist_, actual.artist_);
  EXPECT_EQ(expected.main_title_translit_, actual.main_title_translit_);
  EXPECT_EQ(expected.sub_title_translit_, actual.sub_title_translit_);
  EXPECT_EQ(expected.artist_translit_, actual.artist_translit_);
  EXPECT_EQ(expected.genre_, actual.genre_);
  EXPECT_EQ(expected.origin_, actual.origin_);
  EXPECT_EQ(expected.credit_, actual.credit_);
  EXPECT_EQ(expected.banner_file_, actual.banner_file_);
  EXPECT_EQ(expected.background_file_, actual.background_file_);
  EXPECT_EQ(expected.preview_vid_file_, actual.preview_vid_file_);
  EXPECT_EQ(expected.jacket_file_, actual.jacket_file_);
  EXPECT_EQ(expected.cd_file_, actual.cd_file_);
  EXPECT_EQ(expected.disc_file_, actual.disc_file_);
  EXPECT_EQ(expected.lyrics_file_, actual.lyrics_file_);
  EXPECT_EQ(expected.cd_title_file_, actual.cd_title_file_);
  EXPECT_EQ(expected.music_file_, actual.music_file_);
  EXPECT_EQ(expected.preview_file_, actual.preview_file_);
  EXPECT_EQ(expected.song_file_name_, actual.song_file_name_);

  EXPECT_FLOAT_EQ(expected.music_length_seconds_, actual.music_length_seconds_);
  EXPECT_FLOAT_EQ(
      expected.music_sample_start_seconds_, actual.music_sample_start_seconds_);
  EXPECT_FLOAT_EQ(
      expected.music_sample_length_seconds_,
      actual.music_sample_length_seconds_);
  EXPECT_FLOAT_EQ(expected.specified_bpm_min_, actual.specified_bpm_min_);
  EXPECT_FLOAT_EQ(expected.specified_bpm_max_, actual.specified_bpm_max_);
  EXPECT_EQ(expected.display_bpm_type_, actual.display_bpm_type_);
  EXPECT_EQ(expected.selection_display_, actual.selection_display_);
  EXPECT_EQ(expected.has_music_, actual.has_music_);
  EXPECT_EQ(expected.has_banner_, actual.has_banner_);
  EXPECT_FLOAT_EQ(
      expected.GetFirstSecondNoOffset(), actual.GetFirstSecondNoOffset());
  EXPECT_FLOAT_EQ(
      expected.GetLastSecondNoOffset(), actual.GetLastSecondNoOffset());
  EXPECT_FLOAT_EQ(
      expected.GetSpecifiedLastSecond(), actual.GetSpecifiedLastSecond());

  EXPECT_EQ(expected.keysound_file_, actual.keysound_file_);
  ExpectAttacksEqual(
      expected.attacks_, expected.attack_string_, actual.attacks_,
      actual.attack_string_);
  EXPECT_EQ(expected.song_timing_, actual.song_timing_);

  for (int layer = 0; layer < NUM_BackgroundLayer; ++layer) {
    ExpectBackgroundChangesEqual(
        expected.GetBackgroundChanges(static_cast<BackgroundLayer>(layer)),
        actual.GetBackgroundChanges(static_cast<BackgroundLayer>(layer)));
  }
  ExpectBackgroundChangesEqual(
      expected.GetForegroundChanges(), actual.GetForegroundChanges());

  ASSERT_EQ(expected.GetAllSteps().size(), actual.GetAllSteps().size());
  for (size_t i = 0; i < expected.GetAllSteps().size(); ++i) {
    ExpectStepsEqual(*expected.GetAllSteps()[i], *actual.GetAllSteps()[i]);
  }
  ASSERT_EQ(
      expected.GetUnknownStyleSteps().size(),
      actual.GetUnknownStyleSteps().size());
  for (size_t i = 0; i < expected.GetUnknownStyleSteps().size(); ++i) {
    ExpectStepsEqual(
        *expected.GetUnknownStyleSteps()[i], *actual.GetUnknownStyleSteps()[i]);
  }
}

class SongCacheBinaryTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    FILEMAN->FlushDirCache(kCacheDir);
    FILEMAN->DeleteRecursive(kCacheDir);
    FILEMAN->CreateDir(kCacheDir);
    FILEMAN->FlushDirCache(kCacheDir);
  }

  static void TearDownTestSuite() {
    FILEMAN->FlushDirCache(kCacheDir);
    FILEMAN->DeleteRecursive(kCacheDir);
  }
};

TEST_F(SongCacheBinaryTest, FullRoundTrip) {
  Song expected;
  PopulateFullSong(expected);
  const std::string path = CachePath("full.bin");

  ASSERT_TRUE(SongCacheBinary::Write(expected, path));

  Song actual;
  ASSERT_TRUE(SongCacheBinary::Read(actual, path, false));
  EXPECT_EQ(actual.song_timing_.file_, path);
  ExpectSongsEqual(expected, actual);
}

TEST_F(SongCacheBinaryTest, DefaultSongRoundTrip) {
  Song expected;
  const std::string path = CachePath("default.bin");

  ASSERT_TRUE(SongCacheBinary::Write(expected, path));

  Song actual;
  ASSERT_TRUE(SongCacheBinary::Read(actual, path, false));
  ExpectSongsEqual(expected, actual);
}

TEST_F(SongCacheBinaryTest, SkipsAutogenAndProfileSteps) {
  Song expected;
  Steps* normal = MakeBasicSteps(
      expected, StepsType_dance_single, Difficulty_Easy, "Normal");
  expected.AddSteps(normal);

  Steps* autogen = expected.CreateSteps();
  autogen->AutogenFrom(normal, StepsType_dance_double);
  expected.AddSteps(autogen);

  Steps* fromProfile = MakeBasicSteps(
      expected, StepsType_dance_single, Difficulty_Hard, "Profile");
  fromProfile->SetLoadedFromProfile(ProfileSlot_Machine);
  expected.AddSteps(fromProfile);

  const std::string path = CachePath("filtered.bin");

  ASSERT_TRUE(SongCacheBinary::Write(expected, path));

  Song actual;
  ASSERT_TRUE(SongCacheBinary::Read(actual, path, false));
  ASSERT_EQ(actual.GetAllSteps().size(), 1u);
  EXPECT_TRUE(actual.GetUnknownStyleSteps().empty());
  EXPECT_EQ(actual.GetAllSteps()[0]->GetChartName(), "Normal");
  EXPECT_EQ(actual.GetAllSteps()[0]->GetDifficulty(), Difficulty_Easy);
}

TEST_F(SongCacheBinaryTest, MissingFileFails) {
  Song song;
  EXPECT_FALSE(SongCacheBinary::Read(song, CachePath("missing.bin"), false));
}

TEST_F(SongCacheBinaryTest, EmptyFileFails) {
  const std::string path = CachePath("empty.bin");
  ASSERT_TRUE(WriteBytes(path, ""));

  Song song;
  EXPECT_FALSE(SongCacheBinary::Read(song, path, false));
}

TEST_F(SongCacheBinaryTest, BadMagicFails) {
  const std::string path = CachePath("bad_magic.bin");
  ASSERT_TRUE(WriteBytes(path, "not a cache file"));

  Song song;
  EXPECT_FALSE(SongCacheBinary::Read(song, path, false));
}

TEST_F(SongCacheBinaryTest, WrongVersionFails) {
  std::string data;
  AppendU32(data, 4);
  data += "ITGC";
  AppendU32(data, static_cast<uint32_t>(kFileCacheVersion + 1));

  const std::string path = CachePath("wrong_version.bin");
  ASSERT_TRUE(WriteBytes(path, data));

  Song song;
  EXPECT_FALSE(SongCacheBinary::Read(song, path, false));
}

}  // namespace
