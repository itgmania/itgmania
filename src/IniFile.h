/** @brief IniFile - Reading and writing .INI files. */

#ifndef INIFILE_H
#define INIFILE_H

#include <string>

#include "XmlFile.h"

class RageFileBasic;
/** @brief The functions to read and write .INI files. */
class IniFile : public XNode {
 public:
  /** @brief Set up an INI file with the defaults. */
  IniFile();

  /**
   * @brief Retrieve the filename of the last file loaded.
   * @return the filename. */
  std::string GetPath() const { return path_; }
  /**
   * @brief Retrieve any errors that have occurred.
   * @return the latest error. */
  const std::string& GetError() const { return error_; }

  bool ReadFile(const std::string& path);
  bool ReadFile(RageFileBasic& file);
  bool WriteFile(const std::string& path) const;
  bool WriteFile(RageFileBasic& file) const;

  template <typename T>
  bool GetValue(
      const std::string& key, const std::string& value_name, T& value) const {
    const XNode* node = GetChild(key);
    if (node == nullptr) {
      return false;
    }
    return node->GetAttrValue<T>(value_name, value);
  }
  template <typename T>
  void SetValue(
      const std::string& key, const std::string& value_name, const T& value) {
    XNode* node = GetChild(key);
    if (node == nullptr) {
      node = AppendChild(key);
    }
    node->AppendAttr<T>(value_name, value);
  }
  template <typename T>
  void SetKeyValue(
      XNode* keynode, const std::string& value_name, const T& value) {
    keynode->AppendAttr<T>(value_name, value);
  }

  bool DeleteKey(const std::string& keyname);
  /**
   * @brief Rename a key.
   *
   * For example, call RenameKey("foo", "main") after reading an INI
   * where [foo] is an alias to [main].  If to already exists,
   * nothing happens.
   * @param from the key to rename.
   * @param to the new key name.
   * @return its success or failure. */
  bool RenameKey(const std::string& from, const std::string& to);

 private:
  std::string path_;

  mutable std::string error_;
};

#endif

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
