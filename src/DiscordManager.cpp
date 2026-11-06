#include "DiscordManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>

#include "CommonMetrics.h"
#include "GameState.h"
#include "Group.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "Preference.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#if defined(WITH_DISCORD_SDK)
#include "DiscordSDK.h"
#endif

DiscordManager* DISCORD = nullptr;
static Preference<bool> enabled("DiscordEnabled", true);
static Preference<bool> shareSongs("DiscordShowSongInfo", true);
static Preference<bool> shareEditorSongs("DiscordShowEditorSongInfo", false);
static Preference<bool> shareLobbies("DiscordShowLobbyInfo", true);
static Preference<bool> allowJoining("DiscordAllowJoin", true);
static Preference<bool> publicParty("DiscordPublicParty", true);
static Preference<bool> showElapsed("DiscordShowElapsedTime", true);
static Preference<std::string> applicationId(
    "DiscordApplicationId", "1568037250422407288");

namespace {
std::string PresenceString(const std::string& key) {
  return THEME->GetString("DiscordPresence", key);
}

enum class PresenceContext {
  Menus,
  Editing,
  Playing,
  Results,
  Selecting,
  Title,
  Configuring
};

std::string StylePresence(const Style* style) {
  if (!style) {
    return "";
  }

  const std::string name = std::string(style->m_szName) == "couple-edit"
                               ? "couple"
                               : style->m_szName;
  std::string label = name;
  if (!label.empty()) {
    label[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
  }
  if (THEME->HasString("ScreenSelectMusic", label)) {
    label = THEME->GetString("ScreenSelectMusic", label);
  }
  std::replace(label.begin(), label.end(), '\n', ' ');
  if (name == "single" || name == "versus") {
    label = PresenceString("Singles");
  } else if (name == "double") {
    label = PresenceString("Doubles");
  }

  return label;
}

std::string ChartPresence(PlayerNumber pn) {
  const Steps* steps = GAMESTATE->m_pCurSteps[pn].Get();
  const Style* style = GAMESTATE->GetCurrentStyle(pn);
  if (!steps || !style) {
    return "";
  }
  return ssprintf(
      PresenceString("ChartFormat").c_str(), StylePresence(style).c_str(),
      steps->GetMeter());
}

std::string CurrentChartPresence(bool editing) {
  std::vector<std::pair<PlayerNumber, std::string>> charts;
  FOREACH_PlayerNumber(pn) {
    if (!GAMESTATE->IsPlayerEnabled(pn) && !editing) {
      continue;
    }
    auto text = ChartPresence(pn);
    if (!text.empty()) {
      charts.emplace_back(pn, std::move(text));
    }
  }
  if (charts.empty()) {
    return "";
  }
  if (charts.size() == 1 || charts[0].second == charts[1].second) {
    return charts[0].second;
  }
  return ssprintf(
      PresenceString("TwoPlayersFormat").c_str(), charts[0].second.c_str(),
      charts[1].second.c_str());
}
}  // namespace

struct DiscordManager::Impl {
  DiscordPresence presence;
  DiscordActivity sent;
  std::string appId;
  int privacyMask = -1;
  std::deque<std::string> joins;
  struct Delivery {
    bool inFlight = false;
    bool succeeded = false;
    std::chrono::steady_clock::time_point retryAt{};
  };
  std::shared_ptr<Delivery> delivery = std::make_shared<Delivery>();
  std::chrono::steady_clock::time_point nextSend{};
  std::chrono::steady_clock::time_point refreshAt{};
  const std::string started = std::to_string(std::time(nullptr));
#if defined(WITH_DISCORD_SDK)
  std::unique_ptr<DiscordSDK> sdk;
#endif
};

DiscordManager::DiscordManager() : impl(std::make_unique<Impl>()) {
  Lua* L = LUA->Get();
  PushSelf(L);
  lua_setglobal(L, "DISCORD");
  LUA->Release(L);
  // Start IPC before loading songs/themes, which can take a long time.
  Update();
}

DiscordManager::~DiscordManager() {
#if defined(WITH_DISCORD_SDK)
  if (impl->sdk) {
    impl->sdk->Clear();
    impl->sdk->RunCallbacks();
  }
#endif
  LUA->UnsetGlobal("DISCORD");
}

void DiscordManager::Reset() { impl->presence.Clear(); }

void DiscordManager::SetPresence(
    const std::string& owner, int priority, DiscordActivity fields) {
  impl->presence.Set(owner, priority, std::move(fields));
}

void DiscordManager::ClearPresence(const std::string& owner) {
  impl->presence.Remove(owner);
}

bool DiscordManager::IsAvailable() const {
#if defined(WITH_DISCORD_SDK)
  return true;
#else
  return false;
#endif
}

bool DiscordManager::IsEnabled() const {
  return IsAvailable() && enabled.Get();
}
bool DiscordManager::IsJoinAllowed() const {
  return IsEnabled() && shareLobbies.Get() && allowJoining.Get();
}

void DiscordManager::Update() {
#if defined(WITH_DISCORD_SDK)
  const auto now = std::chrono::steady_clock::now();
  if (impl->sdk) {
    impl->sdk->RunCallbacks();
  }

  // Retain overrides while disabled, but never retain a pending join.
  if (!enabled.Get()) {
    impl->joins.clear();
    if (impl->sdk) {
      impl->sdk->Clear();
      impl->sdk->RunCallbacks();
      impl->sdk.reset();
      impl->delivery = std::make_shared<Impl::Delivery>();
    }
    return;
  }
  const auto id = applicationId.Get();
  // Snowflakes are unsigned 64-bit values; validate before entering the SDK.
  const bool validId =
      !id.empty() && id.size() <= 20 &&
      id.find_first_not_of("0123456789") == std::string::npos &&
      id.find_first_not_of('0') != std::string::npos &&
      (id.size() < 20 || id <= "18446744073709551615");
  if (!validId) {
    if (impl->sdk) {
      impl->sdk->Clear();
      impl->sdk.reset();
    }
    impl->joins.clear();
    return;
  }
  if (!impl->sdk || impl->appId != id) {
    if (impl->sdk) {
      impl->sdk->Clear();
    }
    impl->sdk.reset();
    impl->delivery = std::make_shared<Impl::Delivery>();
    impl->joins.clear();
    impl->sdk = std::make_unique<DiscordSDK>(
        id,
        [this](std::string secret) {
          if (IsJoinAllowed() && !secret.empty() && secret.size() <= 128) {
            // Bound the queue even if a theme never consumes it.
            if (impl->joins.size() == 4) {
              impl->joins.pop_front();
            }
            impl->joins.push_back(std::move(secret));
          }
        },
        [](std::string message) {
          LOG->Trace("Discord SDK: %s", message.c_str());
        });
    impl->appId = id;
    LOG->Trace(
        "Discord Social SDK initialized for application %s (desktop RPC).",
        id.c_str());
    impl->nextSend = now;
  }
  if (!SCREENMAN || !SCREENMAN->GetTopScreen()) {
    return;
  }
  if (!IsJoinAllowed()) {
    impl->joins.clear();
  }
  if (!impl->joins.empty()) {
    auto secret = std::move(impl->joins.front());
    impl->joins.pop_front();
    Message message("DiscordJoin");
    message.SetParam("secret", secret);
    MESSAGEMAN->Broadcast(message);
  }

  auto screen = SCREENMAN->GetTopScreen();
  const auto& name = screen->GetName();
  const bool editing = GAMESTATE->IsEditing() || name.find("ScreenEdit") == 0;
  const DiscordPrivacy privacy{
      editing ? shareEditorSongs.Get() : shareSongs.Get(), shareLobbies.Get(),
      allowJoining.Get(), showElapsed.Get()};
  const int mask = (privacy.shareSong ? 1 : 0) | (privacy.shareLobby ? 2 : 0) |
                   (privacy.allowJoin ? 4 : 0) | (privacy.showElapsed ? 8 : 0) |
                   (publicParty.Get() ? 16 : 0);
  if (impl->privacyMask != mask) {
    // Clear previously shared information immediately when privacy changes;
    // do not leave it visible while waiting for the normal update interval.
    if (impl->privacyMask != -1) {
      impl->sdk->Clear();
    }
    impl->privacyMask = mask;
    impl->delivery = std::make_shared<Impl::Delivery>();
    impl->nextSend = now;
  }
  // Keep activity classification independent of translated display text.
  PresenceContext context = PresenceContext::Menus;
  if (editing) {
    context = PresenceContext::Editing;
  } else if (screen->GetScreenType() == gameplay) {
    context = PresenceContext::Playing;
  } else if (name.find("Evaluation") != std::string::npos) {
    context = PresenceContext::Results;
  } else if (
      name.find("SelectMusic") != std::string::npos ||
      name.find("SelectCourse") != std::string::npos) {
    context = PresenceContext::Selecting;
  } else if (name.find("Edit") != std::string::npos) {
    context = PresenceContext::Editing;
  } else if (screen->GetScreenType() == attract) {
    context = PresenceContext::Title;
  } else if (screen->GetScreenType() == system_menu) {
    context = PresenceContext::Configuring;
  }
  std::string details;
  std::string icon;
  switch (context) {
    case PresenceContext::Menus:
      details = PresenceString("Menus");
      break;
    case PresenceContext::Editing:
      details = PresenceString("Editing");
      icon = "editing";
      break;
    case PresenceContext::Playing:
      details = PresenceString("Playing");
      icon = "playing";
      break;
    case PresenceContext::Results:
      details = PresenceString("Results");
      icon = "results";
      break;
    case PresenceContext::Selecting:
      details = PresenceString("SelectingMusic");
      icon = "selecting";
      break;
    case PresenceContext::Title:
      details = PresenceString("Title");
      break;
    case PresenceContext::Configuring:
      details = PresenceString("Configuring");
      break;
  }
  if (context == PresenceContext::Selecting && !CommonMetrics::AUTO_SET_STYLE) {
    const auto style =
        StylePresence(GAMESTATE->GetCurrentStyle(PLAYER_INVALID));
    if (!style.empty()) {
      details = ssprintf(
          PresenceString("WithStyleFormat").c_str(), details.c_str(),
          style.c_str());
    }
  }
  // Theme branding supplies the small badge when no activity icon is selected.
  // The large image remains the regular ITGmania logo.
  std::string fallbackIcon;
  if (THEME->HasMetric("DiscordPresence", "DefaultSmallImage")) {
    fallbackIcon = THEME->GetMetric("DiscordPresence", "DefaultSmallImage");
  }
  if (icon.empty()) {
    icon = fallbackIcon;
  }
  const DiscordActivity generic{
      {"details", details},
      {"state", ""},
      {"startTimestamp", impl->started},
      {"largeImage", "default"},
      {"largeText", PresenceString("GameName")},
      {"smallImage", icon},
      {"smallText", details}};
  auto base = generic;
  if (GAMESTATE->m_pCurSong &&
      (editing || context == PresenceContext::Playing ||
       context == PresenceContext::Results)) {
    const auto* song = GAMESTATE->m_pCurSong.Get();
    base["details"] = ssprintf(
        PresenceString("WithSongFormat").c_str(), details.c_str(),
        song->GetDisplayFullTitle().c_str());
    const auto chart = CurrentChartPresence(editing);
    base["state"] = chart;
    std::string groupName = song->m_sGroupName;
    if (const Group* group = SONGMAN->GetGroup(song)) {
      if (!group->GetDisplayTitle().empty()) {
        groupName = group->GetDisplayTitle();
      }
    }
    base["largeText"] =
        ssprintf(PresenceString("PackFormat").c_str(), groupName.c_str());
    if (!chart.empty()) {
      base["largeText"] = ssprintf(
          PresenceString("PackAndChartFormat").c_str(),
          base["largeText"].c_str(), chart.c_str());
    }
  }
  auto activity = ApplyDiscordPrivacy(
      impl->presence.Resolve(base), privacy.shareSong ? base : generic,
      privacy);
  if (activity["smallImage"].empty() && !fallbackIcon.empty()) {
    activity["smallImage"] = fallbackIcon;
    activity["smallText"] = details;
  }
  // Song metadata can exceed Discord's field limit. Cut at a UTF-8 boundary.
  for (auto& field : activity) {
    if (field.second.size() > 128) {
      size_t length = 128;
      while (length && (static_cast<unsigned char>(field.second[length]) &
                        0xc0) == 0x80) {
        --length;
      }
      field.second.resize(length);
    }
  }
  if (impl->delivery->inFlight &&
      now >= impl->nextSend + std::chrono::seconds(15)) {
    impl->delivery = std::make_shared<Impl::Delivery>();
  }
  if (impl->delivery->inFlight || now < impl->nextSend ||
      now < impl->delivery->retryAt ||
      (impl->delivery->succeeded && activity == impl->sent &&
       now < impl->refreshAt)) {
    return;
  }

  impl->sent = activity;
  impl->delivery->inFlight = true;
  // Coalesce rapid changes, retry failures, and never publish every frame.
  impl->nextSend = now + std::chrono::seconds(15);
  // Restore presence after Discord restarts, even when the game is idle.
  impl->refreshAt = now + std::chrono::minutes(2);
  std::weak_ptr<Impl::Delivery> delivery = impl->delivery;
  impl->sdk->Update(
      activity, publicParty.Get(), [delivery](bool success, float retryAfter) {
        if (auto state = delivery.lock()) {
          state->inFlight = false;
          state->succeeded = success;
          if (!success) {
            const double delay =
                std::isfinite(retryAfter)
                    ? std::max(15.0, static_cast<double>(retryAfter))
                    : 15.0;
            state->retryAt =
                std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(std::min(delay, 86400.0)));
            LOG->Trace(
                "Discord presence update failed; will retry after %.0f "
                "seconds.",
                delay);
          }
        }
      });
#endif
}

class LunaDiscordManager : public Luna<DiscordManager> {
 public:
  static int IsAvailable(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsAvailable());
    return 1;
  }
  static int IsEnabled(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsEnabled());
    return 1;
  }
  static int IsJoinAllowed(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsJoinAllowed());
    return 1;
  }
  static int ClearPresence(T* p, lua_State* L) {
    p->ClearPresence(SArg(1));
    return 0;
  }
  static int SetPresence(T* p, lua_State* L) {
    const std::string owner = SArg(1);
    if (owner.empty() || owner.size() > 128) {
      return luaL_error(L, "Expected a presence owner (1-128 bytes)");
    }
    luaL_checktype(L, 2, LUA_TTABLE);
    int priority = lua_isnoneornil(L, 3) ? 100 : IArg(3);
    if (priority < 1 || priority > 1000) {
      return luaL_error(L, "Presence priority must be 1-1000");
    }
    DiscordActivity fields;
    const char* textFields[] = {"details",   "state",      "largeImage",
                                "largeText", "smallImage", "smallText",
                                "partyId",   "joinSecret"};
    for (auto key : textFields) {
      lua_getfield(L, 2, key);
      if (!lua_isnil(L, -1)) {
        size_t length;
        const char* value = luaL_checklstring(L, -1, &length);
        if (length > 128 ||
            std::string(value, length).find('\0') != std::string::npos) {
          return luaL_error(
              L, "Presence field %s exceeds 128 bytes or contains NUL", key);
        }
        fields[key] = std::string(value, length);
      }
      lua_pop(L, 1);
    }
    const char* numberFields[] = {
        "startTimestamp", "endTimestamp", "partySize", "partyMax"};
    for (auto key : numberFields) {
      lua_getfield(L, 2, key);
      if (!lua_isnil(L, -1)) {
        double value = luaL_checknumber(L, -1);
        const double maximum = key[0] == 'p' ? 2147483647.0 : 253402300799.0;
        if (!std::isfinite(value) || value < 0 || value > maximum ||
            std::floor(value) != value) {
          return luaL_error(
              L, "Presence field %s must be a nonnegative integer in range",
              key);
        }
        fields[key] = std::to_string(static_cast<int64_t>(value));
      }
      lua_pop(L, 1);
    }
    p->SetPresence(owner, priority, std::move(fields));
    return 0;
  }
  LunaDiscordManager() {
    ADD_METHOD(IsAvailable);
    ADD_METHOD(IsEnabled);
    ADD_METHOD(IsJoinAllowed);
    ADD_METHOD(SetPresence);
    ADD_METHOD(ClearPresence);
  }
};

LUA_REGISTER_CLASS(DiscordManager)
