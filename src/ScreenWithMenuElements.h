#ifndef ScreenWithMenuElements_H
#define ScreenWithMenuElements_H

#include <string>
#include <vector>

#include "Actor.h"
#include "AutoActor.h"
#include "PlayerNumber.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ThemeMetric.h"
#include "Transition.h"

class MenuTimer;
class MemoryCardDisplay;

class ScreenWithMenuElements : public Screen {
 public:
  ScreenWithMenuElements();
  virtual void Init();
  virtual void BeginScreen();
  virtual ~ScreenWithMenuElements();

  virtual void HandleScreenMessage(const ScreenMessage sm);
  void Update(float delta_time);
  void StartTransitioningScreen(ScreenMessage send_when_done);
  virtual void Cancel(ScreenMessage send_when_done);
  bool IsTransitioning();
  virtual bool AllowCallbackInput() { return !IsTransitioning(); }

  void StopTimer();
  void ResetTimer();

  // Sub-classes can hook these and do special actions that won't be triggered
  // automatically by an "On"/"Off" command
  virtual void TweenOnScreen();
  virtual void TweenOffScreen();

  // Lua
  virtual void PushSelf(lua_State* L);

  virtual bool AllowLateJoin() const { return should_allow_late_join_; }
  bool should_allow_late_join_;  // So that it can be exposed to Lua.

 protected:
  std::string HandleLuaMusicFile(const std::string& path);
  virtual void StartPlayingMusic();

  AutoActor spr_underlay_;
  MemoryCardDisplay* memory_card_display_[kNumPlayers];
  MenuTimer* menu_timer_;
  AutoActor spr_overlay_;
  std::vector<Actor*> decorations_;

  Transition in_;
  Transition out_;
  Transition cancel_;

  ThemeMetric<bool> PLAY_MUSIC;
  ThemeMetric<bool> MUSIC_ALIGN_BEAT;
  ThemeMetric<float> DELAY_MUSIC_SECONDS;
  ThemeMetric<bool> CANCEL_TRANSITIONS_OUT;
  ThemeMetric<float> TIMER_SECONDS;
  ThemeMetric<std::string> TIMER_METRICS_GROUP;

 private:
  std::string path_to_music_;
};

class ScreenWithMenuElementsSimple : public ScreenWithMenuElements {
 public:
  bool MenuStart(const InputEventPlus& input);
  bool MenuBack(const InputEventPlus& input);

  // Lua
  virtual void PushSelf(lua_State* L);

 protected:
};

#endif

/*
 * (c) 2004 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
