#ifndef DISCORD_SDK_H
#define DISCORD_SDK_H

#include <functional>
#include <memory>
#include <string>

#include "DiscordPresence.h"

// Keep the SDK's C++20 wrapper out of the engine's C++17 translation units.
class DiscordSDK {
 public:
  using Completion = std::function<void(bool, float)>;
  using Log = std::function<void(std::string)>;
  DiscordSDK(
      const std::string& applicationId, std::function<void(std::string)> onJoin,
      Log log);
  ~DiscordSDK();
  void RunCallbacks();
  void Update(
      const DiscordActivity& fields, bool publicParty, Completion completed);
  void Clear();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

#endif
