#include "global.h"

#include "CodeSet.h"

#include <vector>

#include "InputEventPlus.h"
#include "MessageManager.h"
#include "ThemeManager.h"

#define CODE_NAMES THEME->GetMetric(type, "CodeNames")
#define CODE(s) THEME->GetMetric(type, ssprintf("Code%s", s.c_str()))

void InputQueueCodeSet::Load(const RString& type) {
  // Load codes
  split(CODE_NAMES, ",", code_names_, true);

  for (unsigned c = 0; c < code_names_.size(); c++) {
    std::vector<RString> parts;
    split(code_names_[c], "=", parts, true);
    const RString& code_name = parts[0];
    if (parts.size() > 1) {
      code_names_[c] = parts[1];
    }

    InputQueueCode code;
    if (!code.Load(CODE(code_name))) {
      continue;
    }

    codes_.push_back(code);
  }
}

RString InputQueueCodeSet::Input(const InputEventPlus& input) const {
  for (unsigned i = 0; i < codes_.size(); ++i) {
    if (!codes_[i].EnteredCode(input.GameI.controller)) {
      continue;
    }

    return code_names_[i];
  }
  return "";
}

bool InputQueueCodeSet::InputMessage(
    const InputEventPlus& input, Message& msg) const {
  const RString& code_name = Input(input);
  if (code_name.empty()) {
    return false;
  }

  msg.SetName("Code");
  msg.SetParam("PlayerNumber", input.pn);
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
