#include <gtest/gtest.h>

#include "DiscordPresence.h"

TEST(DiscordPresence, EmptyOptionalStringsAreAbsentInSDKPayloads) {
  EXPECT_FALSE(DiscordOptionalText("").has_value());
  EXPECT_FALSE(DiscordOptionalText("A").has_value());
  EXPECT_EQ(DiscordOptionalText("Playing").value(), "Playing");
  EXPECT_EQ(DiscordOptionalText("a", 1).value(), "a");
  EXPECT_FALSE(DiscordOptionalText("", 1).has_value());
}

TEST(DiscordPresence, MergesByPriorityAndRestoresDefaults) {
  DiscordPresence presence;
  DiscordActivity base{{"details", "Playing"}, {"state", "Song"}};
  presence.Set("theme", 100, {{"details", "Dancing"}});
  presence.Set("online", 200, {{"state", "Online Lobby"}});
  EXPECT_EQ(presence.Resolve(base).at("details"), "Dancing");
  EXPECT_EQ(presence.Resolve(base).at("state"), "Online Lobby");
  presence.Remove("online");
  EXPECT_EQ(presence.Resolve(base).at("state"), "Song");
  presence.Clear();
  EXPECT_EQ(presence.Resolve(base), base);
}

TEST(DiscordPresence, ReplacesOwnerAndClearsExplicitFields) {
  DiscordPresence presence;
  presence.Set(
      "theme", 100, {{"state", "Old state"}, {"details", "Old details"}});
  presence.Set("theme", 100, {{"state", ""}});
  auto resolved = presence.Resolve({{"details", "Base"}, {"state", "Base"}});
  EXPECT_EQ(resolved.at("state"), "");
  EXPECT_EQ(resolved.at("details"), "Base");
}

TEST(DiscordPresence, EqualPrioritiesAreIndependentOfUpdateOrder) {
  DiscordPresence a, b;
  a.Set("a", 100, {{"state", "A"}});
  a.Set("z", 100, {{"state", "Z"}});
  b.Set("z", 100, {{"state", "Z"}});
  b.Set("a", 100, {{"state", "A"}});
  EXPECT_EQ(a.Resolve({}), b.Resolve({}));
  EXPECT_EQ(a.Resolve({}).at("state"), "Z");
}

TEST(DiscordPresence, PartyAndSecretCannotComeFromDifferentOwners) {
  DiscordPresence presence;
  presence.Set(
      "online", 200,
      {{"partyId", "room-a"},
       {"partySize", "1"},
       {"partyMax", "4"},
       {"joinSecret", "secret-a"}});
  presence.Set("module", 300, {{"partyId", "room-b"}});
  auto resolved = presence.Resolve({});
  EXPECT_EQ(resolved.at("partyId"), "room-b");
  EXPECT_EQ(resolved.at("joinSecret"), "");
  EXPECT_EQ(resolved.at("partySize"), "0");
  presence.Remove("module");
  EXPECT_EQ(presence.Resolve({}).at("joinSecret"), "secret-a");
}

TEST(DiscordPresence, PrivacyOverridesThemeAndModulePayloads) {
  DiscordActivity base{
      {"details", "Playing"},
      {"state", "Secret song"},
      {"startTimestamp", "123"}};
  DiscordActivity custom{
      {"details", "Secret song"},
      {"state", "Secret pack"},
      {"largeImage", "secret-cover"},
      {"partyId", "secret-party"},
      {"joinSecret", "secret"}};
  auto hidden = ApplyDiscordPrivacy(custom, base, {false, true, true, true});
  EXPECT_EQ(hidden.at("details"), "Playing");
  EXPECT_EQ(hidden.at("state"), "");
  EXPECT_EQ(hidden.count("largeImage"), 0u);
  EXPECT_EQ(hidden.count("partyId"), 0u);
  EXPECT_EQ(hidden.count("joinSecret"), 0u);
}

TEST(DiscordPresence, JoinabilityAndTimerAreIndependent) {
  DiscordActivity custom{
      {"details", "Playing"},
      {"partyId", "party"},
      {"joinSecret", "secret"},
      {"startTimestamp", "123"},
      {"endTimestamp", "456"}};
  auto hidden = ApplyDiscordPrivacy(custom, {}, {true, true, false, false});
  EXPECT_EQ(hidden.at("partyId"), "party");
  EXPECT_EQ(hidden.count("joinSecret"), 0u);
  EXPECT_EQ(hidden.count("startTimestamp"), 0u);
  EXPECT_EQ(hidden.count("endTimestamp"), 0u);
}
