#include "CodeSet.h"

#include <string>
#include <vector>

#include "InputEventPlus.h"
#include "InputQueue.h"
#include "MessageManager.h"
#include "RageUtil.h"
#include "ThemeManager.h"

#define CODE_NAMES THEME->GetMetric(sType, "CodeNames")
#define CODE(s) THEME->GetMetric(sType, ssprintf("Code%s", s.c_str()))
void InputQueueCodeSet::Load(const std::string& sType) {
  //
  // Load codes
  //
  split(CODE_NAMES, ",", code_names_, true);

  for (unsigned c = 0; c < code_names_.size(); c++) {
    std::vector<std::string> bits;
    split(code_names_[c], "=", bits, true);
    std::string code_name = bits[0];
    if (bits.size() > 1) {
      code_names_[c] = bits[1];
    }

    InputQueueCode code;

    // Invalid codes are empty and we still want to make sure m_asCodeNames
    // and m_aCodes are the same size to prevent off by ones.
    code.Load(CODE(code_name));

    codes_.push_back(code);
  }
}

std::string InputQueueCodeSet::Input(const InputEventPlus& input) const {
  for (unsigned i = 0; i < codes_.size(); ++i) {
    if (!codes_[i].EnteredCode(input.game_i_.controller_)) {
      continue;
    }

    return code_names_[i];
  }
  return "";
}

bool InputQueueCodeSet::InputMessage(
    const InputEventPlus& input, Message& msg) const {
  std::string code_name = Input(input);
  if (code_name.empty()) {
    return false;
  }

  msg.SetName("Code");
  msg.SetParam("PlayerNumber", input.pn_);
  msg.SetParam("Name", code_name);
  return true;
}

/*
 * (c) 2007 Glenn Maynard
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
