#include "ScreenNetworkPermission.h"

#include "NetworkManager.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ThemeManager.h"

REGISTER_SCREEN_CLASS(ScreenNetworkPermission);

void ScreenNetworkPermission::Init() {
  m_Host = NETWORK->permissionHost;
  /* Reuse the three prompt slots for Deny, Allow Session, and Always Allow. */
  SetPromptSettings(
      ssprintf(THEME->GetString(m_sName, "Question").c_str(), m_Host.c_str()),
      PROMPT_YES_NO_CANCEL, ANSWER_YES);
  ScreenPrompt::Init();
}

void ScreenNetworkPermission::BeginScreen() {
  m_bAllow = false;
  m_bRemember = false;
  ScreenPrompt::BeginScreen();
  const char* labels[] = {"Deny", "AllowSession", "AlwaysAllow"};
  for (int i = 0; i < NUM_PromptAnswer; ++i) {
    m_textAnswer[i].SetText(THEME->GetString(m_sName, labels[i]));
  }
}

bool ScreenNetworkPermission::MenuUp(const InputEventPlus& input) {
  return ScreenPrompt::MenuLeft(input);
}

bool ScreenNetworkPermission::MenuDown(const InputEventPlus& input) {
  return ScreenPrompt::MenuRight(input);
}

void ScreenNetworkPermission::End(bool cancelled) {
  m_bAllow = !cancelled && m_Answer != ANSWER_YES;
  m_bRemember = m_bAllow && m_Answer == ANSWER_CANCEL;
  if (cancelled) {
    Cancel(SM_GoToNextScreen);
  } else {
    SCREENMAN->PlayStartSound();
    StartTransitioningScreen(SM_GoToNextScreen);
  }
}

void ScreenNetworkPermission::EndScreen() {
  ScreenPrompt::EndScreen();
  if (NETWORK != nullptr) {
    NETWORK->ResolvePermission(m_Host, m_bAllow, m_bRemember);
  }
}
