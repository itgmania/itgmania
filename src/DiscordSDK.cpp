#define DISCORDPP_IMPLEMENTATION
#include "DiscordSDK.h"

#include <discordpp.h>

#include <cstdint>
#include <utility>

struct DiscordSDK::Impl {
  discordpp::Client client;
  Log log;
};

DiscordSDK::DiscordSDK(
    const std::string& applicationId, std::function<void(std::string)> onJoin,
    Log log)
    : impl(std::make_unique<Impl>()) {
  impl->log = std::move(log);
  impl->client.AddLogCallback(
      [log = impl->log](std::string message, auto) { log(std::move(message)); },
      discordpp::LoggingSeverity::Warning);
  impl->client.SetApplicationId(std::stoull(applicationId));
  impl->client.SetActivityJoinCallback(std::move(onJoin));
  if (!impl->client.RegisterLaunchCommand(std::stoull(applicationId), "")) {
    impl->log("Could not register the Discord game launch command.");
  }
}

DiscordSDK::~DiscordSDK() = default;

void DiscordSDK::RunCallbacks() { discordpp::RunCallbacks(); }

void DiscordSDK::Clear() { impl->client.ClearRichPresence(); }

void DiscordSDK::Update(
    const DiscordActivity& fields, bool publicParty, Completion completed) {
  auto get = [&fields](const char* key) -> std::string {
    auto it = fields.find(key);
    return it == fields.end() ? "" : it->second;
  };
  auto number = [&get](const char* key) -> int64_t {
    auto value = get(key);
    return value.empty() ? 0 : std::stoll(value);
  };
  discordpp::Activity activity;
  activity.SetType(discordpp::ActivityTypes::Playing);
  activity.SetDetails(DiscordOptionalText(get("details")));
  activity.SetState(DiscordOptionalText(get("state")));
  discordpp::ActivityAssets assets;
  if (!get("largeImage").empty()) {
    assets.SetLargeImage(DiscordOptionalText(get("largeImage"), 1));
    assets.SetLargeText(DiscordOptionalText(get("largeText")));
  }
  if (!get("smallImage").empty()) {
    assets.SetSmallImage(DiscordOptionalText(get("smallImage"), 1));
    assets.SetSmallText(DiscordOptionalText(get("smallText")));
  }
  if (!get("largeImage").empty() || !get("smallImage").empty()) {
    activity.SetAssets(assets);
  }
  discordpp::ActivityTimestamps timestamps;
  const auto start = number("startTimestamp");
  const auto end = number("endTimestamp");
  if (start > 0) {
    timestamps.SetStart(start);
  }
  if (end > 0) {
    timestamps.SetEnd(end);
  }
  if (start > 0 || end > 0) {
    activity.SetTimestamps(timestamps);
  }
  const auto current = number("partySize");
  const auto maximum = number("partyMax");
  if (!get("partyId").empty() && current > 0 && maximum >= current) {
    discordpp::ActivityParty party;
    party.SetId(get("partyId"));
    party.SetCurrentSize(static_cast<int32_t>(current));
    party.SetMaxSize(static_cast<int32_t>(maximum));
    // Explicitly set this: the SDK otherwise defaults to Private.
    // Discord eligibility/privacy settings and lobby passwords still apply.
    party.SetPrivacy(
        publicParty ? discordpp::ActivityPartyPrivacy::Public
                    : discordpp::ActivityPartyPrivacy::Private);
    activity.SetParty(party);
    if (!get("joinSecret").empty()) {
      discordpp::ActivitySecrets secrets;
      secrets.SetJoin(get("joinSecret"));
      activity.SetSecrets(secrets);
      activity.SetSupportedPlatforms(discordpp::ActivityGamePlatforms::Desktop);
    }
  }
  impl->client.UpdateRichPresence(
      activity, [completed, log = impl->log](auto result) {
        if (!result.Successful()) {
          log("UpdateRichPresence failed: " + result.ToString() +
              " (type=" + std::to_string(static_cast<int>(result.Type())) +
              ", code=" + std::to_string(result.ErrorCode()) + ")");
        }
        completed(result.Successful(), result.RetryAfter());
      });
}
