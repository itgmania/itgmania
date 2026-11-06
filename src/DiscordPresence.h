#ifndef DISCORD_PRESENCE_H
#define DISCORD_PRESENCE_H

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// SDK-independent arbitration. Missing fields inherit; empty fields erase.
using DiscordActivity = std::map<std::string, std::string>;

// Discord validates optional strings when present. Clearing a field must
// produce an absent value, not an explicitly present empty string.
inline std::optional<std::string> DiscordOptionalText(
    const std::string& value, size_t minimumLength = 2) {
  if (value.size() < minimumLength) {
    return std::nullopt;
  }
  return value;
}

struct DiscordPrivacy {
  bool shareSong = true;
  bool shareLobby = true;
  bool allowJoin = true;
  bool showElapsed = true;
};

inline DiscordActivity ApplyDiscordPrivacy(
    DiscordActivity activity, DiscordActivity base,
    const DiscordPrivacy& privacy) {
  // Arbitrary theme text/art may contain song or lobby information. In a
  // private context use only engine-owned generic activity, including no
  // custom party IDs/secrets. Themes cannot opt out of the user's choice.
  if (!privacy.shareSong || !privacy.shareLobby) {
    activity = std::move(base);
  }
  if (!privacy.shareSong) {
    activity["state"] = "";
  }
  if (!privacy.shareLobby) {
    for (auto key : {"partyId", "partySize", "partyMax", "joinSecret"}) {
      activity.erase(key);
    }
  }
  if (!privacy.allowJoin) {
    activity.erase("joinSecret");
  }
  if (!privacy.showElapsed) {
    activity.erase("startTimestamp");
    activity.erase("endTimestamp");
  }
  return activity;
}

class DiscordPresence {
 public:
  struct Layer {
    int priority;
    DiscordActivity fields;
  };

  void Set(const std::string& owner, int priority, DiscordActivity fields) {
    // A party and its secret must always come from the same owner. Otherwise
    // an overlay could advertise one room while joining a lower layer's room.
    if (fields.count("partyId") || fields.count("partySize") ||
        fields.count("partyMax") || fields.count("joinSecret")) {
      fields.emplace("partyId", "");
      fields.emplace("partySize", "0");
      fields.emplace("partyMax", "0");
      fields.emplace("joinSecret", "");
    }
    layers[owner] = {priority, std::move(fields)};
  }
  void Remove(const std::string& owner) { layers.erase(owner); }
  void Clear() { layers.clear(); }

  DiscordActivity Resolve(DiscordActivity base) const {
    std::vector<std::pair<std::string, const Layer*>> sorted;
    for (const auto& layer : layers) {
      sorted.emplace_back(layer.first, &layer.second);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
      if (a.second->priority != b.second->priority) {
        return a.second->priority < b.second->priority;
      }
      return a.first < b.first;
    });
    for (const auto& layer : sorted) {
      for (const auto& field : layer.second->fields) {
        base[field.first] = field.second;
      }
    }
    return base;
  }

 private:
  std::map<std::string, Layer> layers;
};

#endif
