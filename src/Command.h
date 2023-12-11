
#ifndef Commands_H
#define Commands_H

#include "global.h"

#include <vector>

// - Actor command parsing and reading helpers.
class Command {
 public:
  void Load(const RString& commands_str);

  // Used when reporting an error in number of args.
  RString GetOriginalCommandString() const;
  // The command name is the first argument in all-lowercase .
  RString GetName() const;

  void Clear() { args_.clear(); }

  struct Arg {
    RString s;
    Arg() : s("") {}
  };
  Arg GetArg(unsigned index) const;

  std::vector<RString> args_;

  Command() : args_() {}
};

class Commands {
 public:
  std::vector<Command> v;

  // Used when reporting an error in number of args.
  RString GetOriginalCommandString() const;
};

// Take a command list string and return pointers to each of the tokens in the
// string. sCommand list is a list of commands separated by ';'.
//
// TODO: This is expensive to do during the game. Eventually,  move all calls to
// ParseCommands to happen during load, then execute from the parsed Command
// structures.
void ParseCommands(
    const RString& commands_str, Commands& commands_out, bool legacy);
Commands ParseCommands(const RString& commands_str);

#endif  // Commands_H

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
