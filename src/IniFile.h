#ifndef INIFILE_H
#define INIFILE_H

#include "RageFileBasic.h"
#include "XmlFile.h"

// The functions to read and write .INI files.
class IniFile : public XNode {
 public:
  // Set up an INI file with the defaults.
  IniFile();

  // Retrieve the filename of the last file loaded.
  RString GetPath() const { return path_; }
  // Retrieve any errors that have occurred.
  const RString& GetError() const { return error_; }

  bool ReadFile(const RString& path);
  bool ReadFile(RageFileBasic& file);
  bool WriteFile(const RString& path) const;
  bool WriteFile(RageFileBasic& file) const;

  template <typename T>
  bool GetValue(const RString& key, const RString& value_name, T& value) const {
    const XNode* node = GetChild(key);
    if (node == nullptr) {
      return false;
    }
    return node->GetAttrValue<T>(value_name, value);
  }

  template <typename T>
  void SetValue(
      const RString& key, const RString& value_name, const T& value) {
    XNode* node = GetChild(key);
    if (node == nullptr) {
      node = AppendChild(key);
    }
    node->AppendAttr<T>(value_name, value);
  }

  template <typename T>
  void SetKeyValue(XNode* key_node, const RString& value_name, const T& value) {
    key_node->AppendAttr<T>(value_name, value);
  }

  bool DeleteKey(const RString& key_name);
  bool DeleteValue(const RString& key_name, const RString& value_name);

  // Rename a key.
  //
  // For example, call RenameKey("foo", "main") after reading an INI where [foo]
	// is an alias to [main]. If to already exists, nothing happens.
  bool RenameKey(const RString& from, const RString& to);

 private:
  RString path_;

  mutable RString error_;
};

#endif  // INIFILE_H

/**
 * @file
 * @author Adam Clauss, Chris Danford (c) 2001-2004
 * @section LICENSE
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
