#ifndef DISCORD_MANAGER_H
#define DISCORD_MANAGER_H

#include <memory>
#include <string>

#include "DiscordPresence.h"

struct lua_State;

class DiscordManager {
 public:
  DiscordManager();
  ~DiscordManager();
  void Update();
  void Reset();
  void SetPresence(
      const std::string& owner, int priority, DiscordActivity fields);
  void ClearPresence(const std::string& owner);
  bool IsAvailable() const;
  bool IsEnabled() const;
  bool IsJoinAllowed() const;
  void PushSelf(lua_State* L);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

extern DiscordManager* DISCORD;

#endif
