#ifndef SCREEN_NETWORK_PERMISSION_H
#define SCREEN_NETWORK_PERMISSION_H

#include <string>

#include "ScreenPrompt.h"

/* Network permission decisions are handled by the engine, not Lua. */
class ScreenNetworkPermission : public ScreenPrompt {
 public:
  void Init() override;
  void BeginScreen() override;
  void EndScreen() override;

 protected:
  bool MenuUp(const InputEventPlus& input) override;
  bool MenuDown(const InputEventPlus& input) override;
  void End(bool cancelled) override;

 private:
  std::string m_Host;
  bool m_bAllow = false;
  bool m_bRemember = false;
};

#endif
