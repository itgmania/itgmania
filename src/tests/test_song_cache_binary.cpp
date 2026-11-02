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
    const std::vector<std::string>& expectedStrings, const AttackArray& actual,
    const std::vector<std::string>& actualStrings) {
  ASSERT_EQ(expectedStrings, actualStrings);
  ASSERT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].level, actual[i].level);
    EXPECT_FLOAT_EQ(expected[i].fStartSecond, actual[i].fStartSecond);
    EXPECT_FLOAT_EQ(expected[i].fSecsRemaining, actual[i].fSecsRemaining);
    EXPECT_EQ(expected[i].sModifiers, actual[i].sModifiers);
    EXPECT_EQ(expected[i].bOn, actual[i].bOn);
    EXPECT_EQ(expected[i].bGlobal, actual[i].bGlobal);
    EXPECT_EQ(expected[i].bShowInAttackList, actual[i].bShowInAttackList);
  }
}

void ExpectBackgroundChangesEqual(
    const std::vector<BackgroundChange>& expected,
    const std::vector<BackgroundChange>& actual) {
  ASSERT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].m_def.m_sEffect, actual[i].m_def.m_sEffect);
    EXPECT_EQ(expected[i].m_def.m_sFile1, actual[i].m_def.m_sFile1);
    EXPECT_EQ(expected[i].m_def.m_sFile2, actual[i].m_def.m_sFile2);
    EXPECT_EQ(expected[i].m_def.m_sColor1, actual[i].m_def.m_sColor1);
    EXPECT_EQ(expected[i].m_def.m_sColor2, actual[i].m_def.m_sColor2);
    EXPECT_FLOAT_EQ(expected[i].m_fStartBeat, actual[i].m_fStartBeat);
    EXPECT_FLOAT_EQ(expected[i].m_fRate, actual[i].m_fRate);
    EXPECT_EQ(expected[i].m_sTransition, actual[i].m_sTransition);
  }
}

void AddTimingSegments(TimingData& timing) {
  timing.m_fBeat0OffsetInSeconds = 1.25f;
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
    const std::string& chartName) {
  Steps* steps = song.CreateSteps();
  steps->m_StepsType = type;
  steps->m_StepsTypeStr = "dance-single";
  steps->SetChartName(chartName);
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

  RadarValues radar[NUM_PLAYERS];
  TechCounts tech[NUM_PLAYERS];
  for (int pn = 0; pn < NUM_PLAYERS; ++pn) {
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
  std::vector<float> peakNps = {1.5f, 4.5f};
  steps->SetNpsPerMeasure(nps);
  steps->SetNotesPerMeasure(notes);
  steps->SetPeakNps(peakNps);

  std::vector<StreamBreakdown> breakdowns(notes.size());
  breakdowns[0].texts = {"first-a", "first-b", "first-c"};
  breakdowns[0].totalStreamMeasures = 7;
  breakdowns[0].totalBreakMeasures = 3;
  breakdowns[1].texts = {"second-a", "second-b", "second-c"};
  breakdowns[1].totalStreamMeasures = 4;
  breakdowns[1].totalBreakMeasures = 6;
  steps->SetStreamBreakdown(breakdowns);

  std::vector<NoteAnnotationCache> annotations(2);
  annotations[0].compressed = "annotation-one";
  annotations[1].compressed = "";
  steps->SetNoteAnnotations(annotations);

  steps->SetGrooveStatsHash("groove-hash");
  steps->SetGrooveStatsHashVersion(3);
  steps->SetDisplayBPM(DISPLAY_BPM_SPECIFIED);
  steps->SetMinBPM(101.0f);
  steps->SetMaxBPM(202.0f);

  AddAttacks(steps->GetStepsAttacks(), steps->GetStepsAttackStrings());
  AddTimingSegments(steps->m_Timing);
  return steps;
}

void PopulateFullSong(Song& song) {
  song.m_sSongFileName = "song.ssc";
  song.m_sMainTitle = "Main Title";
  song.m_sSubTitle = "Sub Title";
  song.m_sArtist = "Artist";
  song.m_sMainTitleTranslit = "Main Title Translit";
  song.m_sSubTitleTranslit = "Sub Title Translit";
  song.m_sArtistTranslit = "Artist Translit";
  song.m_sGenre = "Genre";
  song.m_sOrigin = "Origin";
  song.m_sCredit = std::string("Cred\0it", 7);
  song.m_sBannerFile = "banner.png";
  song.m_sBackgroundFile = "background.png";
  song.m_sPreviewVidFile = "preview.mp4";
  song.m_sJacketFile = "jacket.png";
  song.m_sCDFile = "cd.png";
  song.m_sDiscFile = "disc.png";
  song.m_sLyricsFile = "lyrics.lrc";
  song.m_sCDTitleFile = "cdtitle.png";
  song.m_sMusicFile = "music.ogg";
  song.m_PreviewFile = "preview.ogg";

  song.m_fMusicLengthSeconds = 123.5f;
  song.m_fMusicSampleStartSeconds = 12.25f;
  song.m_fMusicSampleLengthSeconds = 8.75f;
  song.m_fSpecifiedBPMMin = 100.0f;
  song.m_fSpecifiedBPMMax = 200.0f;
  song.m_DisplayBPMType = DISPLAY_BPM_SPECIFIED;
  song.m_SelectionDisplay = Song::SHOW_NEVER;
  song.m_bHasMusic = true;
  song.m_bHasBanner = true;
  song.SetFirstSecondNoOffset(2.0f);
  song.SetLastSecondNoOffset(42.0f);
  song.SetSpecifiedLastSecond(45.0f);

  song.m_vsKeysoundFile = {"one.wav", "", "three.wav"};
  AddAttacks(song.m_Attacks, song.m_sAttackString);
  AddTimingSegments(song.m_SongTiming);

  BackgroundChange bg1;
  bg1.m_def.m_sEffect = "effect1";
  bg1.m_def.m_sFile1 = "bg1.png";
  bg1.m_def.m_sFile2 = "bg1b.png";
  bg1.m_def.m_sColor1 = "0,1,0,1";
  bg1.m_def.m_sColor2 = "0,0,1,1";
  bg1.m_fStartBeat = 1.5f;
  bg1.m_fRate = 1.25f;
  bg1.m_sTransition = "transition1";
  song.AddBackgroundChange(BACKGROUND_LAYER_1, bg1);

  BackgroundChange bg2;
  bg2.m_def.m_sEffect = "effect2";
  bg2.m_def.m_sFile1 = "bg2.png";
  bg2.m_def.m_sFile2 = "";
  bg2.m_def.m_sColor1 = "";
  bg2.m_def.m_sColor2 = "1,1,1,1";
  bg2.m_fStartBeat = 2.5f;
  bg2.m_fRate = 0.75f;
  bg2.m_sTransition = "";
  song.AddBackgroundChange(BACKGROUND_LAYER_2, bg2);

  BackgroundChange foreground;
  foreground.m_def.m_sEffect = "foreground effect";
  foreground.m_def.m_sFile1 = "foreground.png";
  foreground.m_def.m_sFile2 = "foreground2.png";
  foreground.m_def.m_sColor1 = "1,0,0,1";
  foreground.m_def.m_sColor2 = "";
  foreground.m_fStartBeat = 3.5f;
  foreground.m_fRate = 1.5f;
  foreground.m_sTransition = "foreground transition";
  song.AddForegroundChange(foreground);

  song.AddSteps(MakeFullSteps(song));

  Steps* unknown = MakeBasicSteps(
      song, StepsType_Invalid, Difficulty_Medium, "Unknown style");
  unknown->m_StepsTypeStr = "future-single";
  unknown->SetDescription("Unknown description");
  unknown->SetChartStyle("Unknown style");
  unknown->SetMeter(7);
  unknown->SetCredit("Unknown credit");
  song.AddSteps(unknown);
}

void ExpectStepsEqual(const Steps& expected, const Steps& actual) {
  EXPECT_EQ(expected.m_StepsType, actual.m_StepsType);
  EXPECT_EQ(expected.m_StepsTypeStr, actual.m_StepsTypeStr);
  EXPECT_EQ(expected.GetChartName(), actual.GetChartName());
  EXPECT_EQ(expected.GetDescription(), actual.GetDescription());
  EXPECT_EQ(expected.GetChartStyle(), actual.GetChartStyle());
  EXPECT_EQ(expected.GetDifficulty(), actual.GetDifficulty());
  EXPECT_EQ(expected.GetMeter(), actual.GetMeter());
  EXPECT_EQ(expected.GetCredit(), actual.GetCredit());
  EXPECT_EQ(expected.GetMusicFile(), actual.GetMusicFile());
  EXPECT_EQ(expected.GetFilename(), actual.GetFilename());

  for (int pn = 0; pn < NUM_PLAYERS; ++pn) {
    const PlayerNumber player = static_cast<PlayerNumber>(pn);
    EXPECT_EQ(expected.GetRadarValues(player), actual.GetRadarValues(player));
    EXPECT_EQ(expected.GetTechCounts(player), actual.GetTechCounts(player));
  }
  EXPECT_EQ(expected.GetAllNpsPerMeasures(), actual.GetAllNpsPerMeasures());
  EXPECT_EQ(expected.GetAllNotesPerMeasures(), actual.GetAllNotesPerMeasures());
  EXPECT_EQ(expected.GetAllPeakNps(), actual.GetAllPeakNps());

  const std::vector<StreamBreakdown>& expectedBreakdowns =
      expected.GetAllStreamBreakdowns();
  const std::vector<StreamBreakdown>& actualBreakdowns =
      actual.GetAllStreamBreakdowns();
  ASSERT_EQ(expectedBreakdowns.size(), actualBreakdowns.size());
  for (size_t i = 0; i < expectedBreakdowns.size(); ++i) {
    EXPECT_EQ(expectedBreakdowns[i].texts, actualBreakdowns[i].texts);
    EXPECT_EQ(
        expectedBreakdowns[i].totalStreamMeasures,
        actualBreakdowns[i].totalStreamMeasures);
    EXPECT_EQ(
        expectedBreakdowns[i].totalBreakMeasures,
        actualBreakdowns[i].totalBreakMeasures);
  }

  const std::vector<NoteAnnotationCache>& expectedAnnotations =
      expected.GetNoteAnnotationCaches();
  const std::vector<NoteAnnotationCache>& actualAnnotations =
      actual.GetNoteAnnotationCaches();
  ASSERT_EQ(expectedAnnotations.size(), actualAnnotations.size());
  for (size_t i = 0; i < expectedAnnotations.size(); ++i) {
    EXPECT_EQ(
        expectedAnnotations[i].GetCompressed(),
        actualAnnotations[i].GetCompressed());
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
  EXPECT_EQ(expected.m_Timing, actual.m_Timing);
}

void ExpectSongsEqual(const Song& expected, const Song& actual) {
  EXPECT_EQ(expected.m_sMainTitle, actual.m_sMainTitle);
  EXPECT_EQ(expected.m_sSubTitle, actual.m_sSubTitle);
  EXPECT_EQ(expected.m_sArtist, actual.m_sArtist);
  EXPECT_EQ(expected.m_sMainTitleTranslit, actual.m_sMainTitleTranslit);
  EXPECT_EQ(expected.m_sSubTitleTranslit, actual.m_sSubTitleTranslit);
  EXPECT_EQ(expected.m_sArtistTranslit, actual.m_sArtistTranslit);
  EXPECT_EQ(expected.m_sGenre, actual.m_sGenre);
  EXPECT_EQ(expected.m_sOrigin, actual.m_sOrigin);
  EXPECT_EQ(expected.m_sCredit, actual.m_sCredit);
  EXPECT_EQ(expected.m_sBannerFile, actual.m_sBannerFile);
  EXPECT_EQ(expected.m_sBackgroundFile, actual.m_sBackgroundFile);
  EXPECT_EQ(expected.m_sPreviewVidFile, actual.m_sPreviewVidFile);
  EXPECT_EQ(expected.m_sJacketFile, actual.m_sJacketFile);
  EXPECT_EQ(expected.m_sCDFile, actual.m_sCDFile);
  EXPECT_EQ(expected.m_sDiscFile, actual.m_sDiscFile);
  EXPECT_EQ(expected.m_sLyricsFile, actual.m_sLyricsFile);
  EXPECT_EQ(expected.m_sCDTitleFile, actual.m_sCDTitleFile);
  EXPECT_EQ(expected.m_sMusicFile, actual.m_sMusicFile);
  EXPECT_EQ(expected.m_PreviewFile, actual.m_PreviewFile);
  EXPECT_EQ(expected.m_sSongFileName, actual.m_sSongFileName);

  EXPECT_FLOAT_EQ(expected.m_fMusicLengthSeconds, actual.m_fMusicLengthSeconds);
  EXPECT_FLOAT_EQ(
      expected.m_fMusicSampleStartSeconds, actual.m_fMusicSampleStartSeconds);
  EXPECT_FLOAT_EQ(
      expected.m_fMusicSampleLengthSeconds, actual.m_fMusicSampleLengthSeconds);
  EXPECT_FLOAT_EQ(expected.m_fSpecifiedBPMMin, actual.m_fSpecifiedBPMMin);
  EXPECT_FLOAT_EQ(expected.m_fSpecifiedBPMMax, actual.m_fSpecifiedBPMMax);
  EXPECT_EQ(expected.m_DisplayBPMType, actual.m_DisplayBPMType);
  EXPECT_EQ(expected.m_SelectionDisplay, actual.m_SelectionDisplay);
  EXPECT_EQ(expected.m_bHasMusic, actual.m_bHasMusic);
  EXPECT_EQ(expected.m_bHasBanner, actual.m_bHasBanner);
  EXPECT_FLOAT_EQ(
      expected.GetFirstSecondNoOffset(), actual.GetFirstSecondNoOffset());
  EXPECT_FLOAT_EQ(
      expected.GetLastSecondNoOffset(), actual.GetLastSecondNoOffset());
  EXPECT_FLOAT_EQ(
      expected.GetSpecifiedLastSecond(), actual.GetSpecifiedLastSecond());

  EXPECT_EQ(expected.m_vsKeysoundFile, actual.m_vsKeysoundFile);
  ExpectAttacksEqual(
      expected.m_Attacks, expected.m_sAttackString, actual.m_Attacks,
      actual.m_sAttackString);
  EXPECT_EQ(expected.m_SongTiming, actual.m_SongTiming);

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
  EXPECT_EQ(actual.m_SongTiming.m_sFile, path);
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
  AppendU32(data, static_cast<uint32_t>(FILE_CACHE_VERSION + 1));

  const std::string path = CachePath("wrong_version.bin");
  ASSERT_TRUE(WriteBytes(path, data));

  Song song;
  EXPECT_FALSE(SongCacheBinary::Read(song, path, false));
}

}  // namespace
