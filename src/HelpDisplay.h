#ifndef HELP_DISPLAY_H
#define HELP_DISPLAY_H

#include <vector>

#include "BitmapText.h"

struct lua_State;

// A BitmapText that cycles through messages.
class HelpDisplay : public BitmapText {
 public:
  HelpDisplay();
  void Load(const RString& type);

  virtual HelpDisplay* Copy() const;

  void SetTips(const std::vector<RString>& array_tips) {
    SetTips(array_tips, array_tips);
  }
  void SetTips(
      const std::vector<RString>& array_tips,
      const std::vector<RString>& array_tips_alt);
  void GetTips(
      std::vector<RString>& array_tips_out,
      std::vector<RString>& array_tips_alt_out) const {
    array_tips_out = array_tips_;
    array_tips_alt_out = array_tips_alt_;
  }
  void SetSecsBetweenSwitches(float seconds) {
    seconds_between_switches_ = seconds_until_switch_ = seconds;
  }

  virtual void Update(float delta);

  // Lua
  virtual void PushSelf(lua_State* L);

 protected:
  std::vector<RString> array_tips_;
	std::vector<RString> array_tips_alt_;
  int cur_tip_index_;

  float seconds_between_switches_;
  float seconds_until_switch_;
};

#endif  // HELP_DISPLAY_H

/*
 * (c) 2001-2003 Chris Danford, Glenn Maynard
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
