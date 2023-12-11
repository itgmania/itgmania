/*
http://en.wikipedia.org/wiki/INI_file
 - names and values are trimmed on both sides
 - semicolons start a comment line
 - backslash followed by a newline doesn't break the line
*/
#include "global.h"

#include "IniFile.h"

#include <cstddef>

#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"

IniFile::IniFile() : XNode("IniFile") {}

bool IniFile::ReadFile(const RString& path) {
  path_ = path;
  CHECKPOINT_M(ssprintf("Reading '%s'", path_.c_str()));

  RageFile f;
  if (!f.Open(path_)) {
    LOG->Trace(
        "Reading '%s' failed: %s", path_.c_str(), f.GetError().c_str());
    error_ = f.GetError();
    return 0;
  }

  return ReadFile(f);
}

bool IniFile::ReadFile(RageFileBasic& file) {
  RString key_name;
  // NOTE(Kyz): key_child is used to cache the node that values are being added
	// to.
  XNode* key_child = nullptr;
  while (true) {
    RString line;
    // Read lines until we reach a line that doesn't end in a backslash
    while (true) {
      RString s;
      switch (file.GetLine(s)) {
        case -1:
          error_ = file.GetError();
          return false;
        case 0:
          return true;  // eof
      }

      utf8_remove_bom(s);

      line += s;

      if (line.empty() || line[line.size() - 1] != '\\') {
        break;
      }
      line.erase(line.end() - 1);
    }

    if (line.empty()) {
      continue;
    }
    switch (line[0]) {
      case ';':
      case '#':
        continue;  // comment
      case '/':
      case '-':
        if (line.size() > 1 && line[0] == line[1]) {
          continue;
        }  // comment (Lua or C++ style)
        goto key_value;
      case '[':
        if (line[line.size() - 1] == ']') {
          // New section.
          key_name = line.substr(1, line.size() - 2);
          key_child = GetChild(key_name);
          if (key_child == nullptr) {
            key_child = AppendChild(key_name);
          }
          break;
        }
        [[fallthrough]];
      default:
      key_value:
        if (key_child == nullptr) {
          break;
        }
        // New value.
        std::size_t equal_index = line.find("=");
        if (equal_index != std::string::npos) {
          RString value_name = line.Left((int)equal_index);
          RString value = line.Right(line.size() - value_name.size() - 1);
          Trim(value_name);
          if (!value_name.empty()) {
            SetKeyValue(key_child, value_name, value);
          }
        }
        break;
    }
  }
}

bool IniFile::WriteFile(const RString& path) const {
  RageFile file;
  if (!file.Open(path, RageFile::WRITE)) {
    LOG->Warn("Writing '%s' failed: %s", path.c_str(), file.GetError().c_str());
    error_ = file.GetError();
    return false;
  }

  bool success = IniFile::WriteFile(file);
  int flush = file.Flush();
  success &= (flush != -1);
  return success;
}

bool IniFile::WriteFile(RageFileBasic& file) const {
  FOREACH_CONST_Child(this, key) {
    if (file.PutLine(ssprintf("[%s]", key->GetName().c_str())) == -1) {
      error_ = file.GetError();
      return false;
    }

    FOREACH_CONST_Attr(key, attr) {
      const RString& name = attr->first;
      const RString& value = attr->second->GetValue<RString>();

      // TODO: Are there escape rules for these?
      // take a cue from how multi-line Lua functions are parsed
      DEBUG_ASSERT(name.find('\n') == name.npos);
      DEBUG_ASSERT(name.find('=') == name.npos);

      if (file.PutLine(ssprintf("%s=%s", name.c_str(), value.c_str())) == -1) {
        error_ = file.GetError();
        return false;
      }
    }

    if (file.PutLine("") == -1) {
      error_ = file.GetError();
      return false;
    }
  }
  return true;
}

bool IniFile::DeleteValue(const RString& key_name, const RString& value_name) {
  XNode* node = GetChild(key_name);
  if (node == nullptr) {
    return false;
  }
  return node->RemoveAttr(value_name);
}

bool IniFile::DeleteKey(const RString& key_name) {
  XNode* node = GetChild(key_name);
  if (node == nullptr) {
    return false;
  }
  return RemoveChild(node);
}

bool IniFile::RenameKey(const RString& from, const RString& to) {
  // If to already exists, do nothing.
  if (GetChild(to) != nullptr) {
    return false;
  }

  XNode* node = GetChild(from);
  if (node == nullptr) {
    return false;
  }

  node->SetName(to);
  RenameChildInByName(node);

  return true;
}

/*
 * (c) 2001-2004 Adam Clauss, Chris Danford
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
