#include "global.h"

#include "Command.h"

#include <cstddef>
#include <numeric>
#include <vector>

#include "RageLog.h"
#include "RageUtil.h"
#include "arch/Dialog/Dialog.h"

RString Command::GetName() const {
  if (args_.empty()) {
    return RString();
  }
  RString s = args_[0];
  Trim(s);
  return s;
}

Command::Arg Command::GetArg(unsigned index) const {
  Arg a;
  if (index < args_.size()) {
    a.s = args_[index];
  }
  return a;
}

void Command::Load(const RString& commands_str) {
  args_.clear();
  split(commands_str, ",", args_, false);  // don't ignore empty
}

RString Command::GetOriginalCommandString() const {
  return join(",", args_);
}

static void SplitWithQuotes(
    const RString source, const char delimitor, std::vector<RString>& out,
    const bool ignore_empty) {
  // Short-circuit if the source is empty; we want to return an empty vector if
  // the string is empty, even if ignore_empty is true.
  if (source.empty()) {
    return;
  }

  size_t startpos = 0;
  do {
    size_t pos = startpos;
    while (pos < source.size()) {
      if (source[pos] == delimitor) {
        break;
      }

      if (source[pos] == '"' || source[pos] == '\'') {
        // We've found a quote. Search for the close.
        pos = source.find(source[pos], pos + 1);
        if (pos == std::string::npos) {
          pos = source.size();
        } else {
          ++pos;
        }
      } else {
        ++pos;
      }
    }

    if (pos - startpos > 0 || !ignore_empty) {
      // Optimization: if we're copying the whole string, avoid substr; this
      // allows this copy to be refcounted, which is much faster.
      if (startpos == 0 && pos - startpos == source.size()) {
        out.push_back(source);
      } else {
        const RString AddCString = source.substr(startpos, pos - startpos);
        out.push_back(AddCString);
      }
    }

    startpos = pos + 1;
  } while (startpos <= source.size());
}

RString Commands::GetOriginalCommandString() const {
  return std::accumulate(
      v.begin(), v.end(), RString(), [](const RString& res, const Command& c) {
        return res + c.GetOriginalCommandString();
      });
}

void ParseCommands(
    const RString& commands_str, Commands& commands_out, bool legacy) {
  std::vector<RString> temp_out;
  if (legacy) {
    split(commands_str, ";", temp_out, /*ignore_empty=*/true);
  } else {
    SplitWithQuotes(commands_str, ';', temp_out, /*ignore_empty=*/true);
  }
  commands_out.v.resize(temp_out.size());

  for (unsigned i = 0; i < temp_out.size(); i++) {
    Command& cmd = commands_out.v[i];
    cmd.Load(temp_out[i]);
  }
}

Commands ParseCommands(const RString& commands_str) {
  Commands commands;
  ParseCommands(commands_str, commands, /*legacy=*/false);
  return commands;
}

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
