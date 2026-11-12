#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <string>

#include "Difficulty.h"
#include "SongManager.h"

namespace {

std::function<bool(const std::string&)> MakeExistsFn(
    const std::set<std::string>& targets) {
  return [targets](const std::string& candidate) {
    return targets.count(candidate) > 0;
  };
}

void ExpectParsed(
    const std::string& line,
    const std::function<bool(const std::string&)>& exists_fn,
    const std::string& expected_target,
    Difficulty expected_difficulty = Difficulty_Invalid,
    const std::string& expected_edit_description = "") {
  SCOPED_TRACE(line);
  std::string target;
  Difficulty difficulty = Difficulty_Invalid;
  std::string edit_description;
  ASSERT_TRUE(
      SongManager::ParsePreferredSongLine(
          line, exists_fn, target, difficulty, edit_description));
  EXPECT_EQ(target, expected_target);
  EXPECT_EQ(difficulty, expected_difficulty);
  EXPECT_EQ(edit_description, expected_edit_description);
}

TEST(PlaylistPinning, DifficultyStandardNames) {
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Beginner"), Difficulty_Beginner);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Easy"), Difficulty_Easy);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Medium"), Difficulty_Medium);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Hard"), Difficulty_Hard);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Challenge"), Difficulty_Challenge);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Edit"), Difficulty_Edit);
}

TEST(PlaylistPinning, DifficultyCaseWhitespaceAndEnumPrefix) {
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("  challenge  "),
      Difficulty_Challenge);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Difficulty_Hard"),
      Difficulty_Hard);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("difficulty_expert"),
      Difficulty_Challenge);
}

TEST(PlaylistPinning, DifficultyOldStyleAliases) {
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Basic"), Difficulty_Easy);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Light"), Difficulty_Easy);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Standard"), Difficulty_Medium);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Difficult"), Difficulty_Medium);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Heavy"), Difficulty_Hard);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Maniac"), Difficulty_Hard);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Expert"), Difficulty_Challenge);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("Oni"), Difficulty_Challenge);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("SManiac"), Difficulty_Challenge);
}

TEST(PlaylistPinning, DifficultyInvalidStrings) {
  EXPECT_EQ(SongManager::ParsePreferredDifficulty(""), Difficulty_Invalid);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("   "), Difficulty_Invalid);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Difficulty_"), Difficulty_Invalid);
  EXPECT_EQ(
      SongManager::ParsePreferredDifficulty("Part 2"), Difficulty_Invalid);
  EXPECT_EQ(SongManager::ParsePreferredDifficulty("14"), Difficulty_Invalid);
}

TEST(PlaylistPinning, EmptyAndWhitespaceLines) {
  auto exists_fn = MakeExistsFn({"Pack/Song"});
  std::string target;
  Difficulty diff = Difficulty_Invalid;
  std::string edit_desc;

  EXPECT_FALSE(
      SongManager::ParsePreferredSongLine(
          "", exists_fn, target, diff, edit_desc));
  EXPECT_FALSE(
      SongManager::ParsePreferredSongLine(
          "   ", exists_fn, target, diff, edit_desc));
}

TEST(PlaylistPinning, MissingTargetAndNullCallbackReturnFalse) {
  auto exists_fn = MakeExistsFn({"Pack/Song"});
  std::string target;
  Difficulty diff = Difficulty_Invalid;
  std::string edit_desc;

  EXPECT_FALSE(
      SongManager::ParsePreferredSongLine(
          "Missing Pack/Missing Song", exists_fn, target, diff, edit_desc));
  EXPECT_FALSE(
      SongManager::ParsePreferredSongLine(
          "Missing Pack/Missing Song:Challenge", exists_fn, target, diff,
          edit_desc));
  EXPECT_FALSE(
      SongManager::ParsePreferredSongLine(
          "Pack/Song:Challenge", nullptr, target, diff, edit_desc));
}

TEST(PlaylistPinning, UnpinnedSongAndGroupWildcard) {
  auto exists_fn =
      MakeExistsFn({"Club Fantastic/Dance All Night", "Stamina Pack/*"});

  ExpectParsed(
      "Club Fantastic/Dance All Night", exists_fn,
      "Club Fantastic/Dance All Night");
  ExpectParsed("Stamina Pack/*", exists_fn, "Stamina Pack/*");
}

TEST(PlaylistPinning, PinnedStandardDifficulty) {
  auto exists_fn = MakeExistsFn({"ITG1/Pandemonium", "ITG1/Queen of Light"});

  ExpectParsed(
      "ITG1/Pandemonium:Challenge", exists_fn, "ITG1/Pandemonium",
      Difficulty_Challenge);
  ExpectParsed(
      "ITG1/Queen of Light:Hard", exists_fn, "ITG1/Queen of Light",
      Difficulty_Hard);
}

TEST(PlaylistPinning, PinnedDifficultyAliasesAndTrailingWhitespace) {
  auto exists_fn = MakeExistsFn({"ITG1/Vertex", "DDR Extreme/Max 300"});

  ExpectParsed(
      "ITG1/Vertex:Expert", exists_fn, "ITG1/Vertex", Difficulty_Challenge);
  ExpectParsed(
      "DDR Extreme/Max 300:Heavy  ", exists_fn, "DDR Extreme/Max 300",
      Difficulty_Hard);
}

TEST(PlaylistPinning, PinnedGroupWildcard) {
  auto exists_fn = MakeExistsFn({"Stamina Pack/*"});

  ExpectParsed(
      "Stamina Pack/*:Hard", exists_fn, "Stamina Pack/*", Difficulty_Hard);
}

TEST(PlaylistPinning, PinnedUnnamedEdit) {
  auto exists_fn = MakeExistsFn({"Pack/My Song"});

  ExpectParsed(
      "Pack/My Song:Edit", exists_fn, "Pack/My Song", Difficulty_Edit, "");
}

TEST(PlaylistPinning, PinnedNamedEdit) {
  auto exists_fn = MakeExistsFn({"Pack/My Song"});

  ExpectParsed(
      "Pack/My Song:Edit:Tech 14", exists_fn, "Pack/My Song", Difficulty_Edit,
      "Tech 14");
  ExpectParsed(
      "Pack/My Song:Edit:  Trimmed Name  ", exists_fn, "Pack/My Song",
      Difficulty_Edit, "Trimmed Name");
}

TEST(PlaylistPinning, PinnedNamedEditWithColonsOrDifficultyWords) {
  auto exists_fn = MakeExistsFn({"Pack/My Song"});

  ExpectParsed(
      "Pack/My Song:Edit:Author: 15", exists_fn, "Pack/My Song",
      Difficulty_Edit, "Author: 15");
  ExpectParsed(
      "Pack/My Song:Edit:Challenge", exists_fn, "Pack/My Song", Difficulty_Edit,
      "Challenge");
}

TEST(PlaylistPinning, ColonInSongTitle) {
  auto exists_fn = MakeExistsFn({"Pack/Song: Part 2"});

  ExpectParsed("Pack/Song: Part 2", exists_fn, "Pack/Song: Part 2");
  ExpectParsed(
      "Pack/Song: Part 2:Challenge", exists_fn, "Pack/Song: Part 2",
      Difficulty_Challenge);
}

TEST(PlaylistPinning, SongTitleEndingInDifficultyName) {
  auto exists_fn = MakeExistsFn({"Pack/Tricky:Hard"});

  ExpectParsed("Pack/Tricky:Hard", exists_fn, "Pack/Tricky:Hard");
  ExpectParsed(
      "Pack/Tricky:Hard:Challenge", exists_fn, "Pack/Tricky:Hard",
      Difficulty_Challenge);
}

TEST(PlaylistPinning, SongTitleContainingEditToken) {
  auto exists_fn = MakeExistsFn({"Pack/Remix:Edit:2024"});

  ExpectParsed("Pack/Remix:Edit:2024", exists_fn, "Pack/Remix:Edit:2024");
  ExpectParsed(
      "Pack/Remix:Edit:2024:Edit:Stream", exists_fn, "Pack/Remix:Edit:2024",
      Difficulty_Edit, "Stream");
}

}  // namespace
