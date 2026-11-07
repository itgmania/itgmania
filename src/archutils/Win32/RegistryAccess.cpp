#include "RegistryAccess.h"

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "StdString.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

/* Given "HKEY_LOCAL_MACHINE\hardware\foo", return "hardware\foo", and place
 * the HKEY_LOCAL_MACHINE constant in key. */
static bool GetRegKeyType(const std::string& in, std::string& out, HKEY& key) {
  size_t backslash = in.find('\\');
  if (backslash == in.npos) {
    LOG->Warn("Invalid registry key: \"%s\" ", in.c_str());
    return false;
  }

  std::string type = in.substr(0, backslash);

  if (!CompareNoCase(type, "HKEY_CLASSES_ROOT")) {
    key = HKEY_CLASSES_ROOT;
  } else if (!CompareNoCase(type, "HKEY_CURRENT_CONFIG")) {
    key = HKEY_CURRENT_CONFIG;
  } else if (!CompareNoCase(type, "HKEY_CURRENT_USER")) {
    key = HKEY_CURRENT_USER;
  } else if (!CompareNoCase(type, "HKEY_LOCAL_MACHINE")) {
    key = HKEY_LOCAL_MACHINE;
  } else if (!CompareNoCase(type, "HKEY_USERS")) {
    key = HKEY_USERS;
  } else {
    LOG->Warn("Invalid registry key: \"%s\" ", in.c_str());
    return false;
  }

  out = in.substr(backslash + 1);

  return true;
}

/* Given a full key, eg. "HKEY_LOCAL_MACHINE\hardware\foo", open it and return
 * it. On error, return nullptr. */
enum RegKeyMode { READ, WRITE };
static HKEY OpenRegKey(
    const std::string& key, RegKeyMode mode, bool warn_on_error = true) {
  std::string subkey;
  HKEY type;
  if (!GetRegKeyType(key, subkey, type)) {
    return nullptr;
  }

  HKEY ret_key;
  LONG retval = RegOpenKeyEx(
      type, subkey.c_str(), 0, (mode == READ) ? KEY_READ : KEY_WRITE, &ret_key);
  if (retval != ERROR_SUCCESS) {
    if (warn_on_error) {
      LOG->Warn(werr_ssprintf(
                    retval, "RegOpenKeyEx(%x,%s) error", type, subkey.c_str())
                    .c_str());
    }
    return nullptr;
  }

  return ret_key;
}

bool RegistryAccess::GetRegValue(
    const std::string& key, const std::string& name, std::string& val) {
  HKEY hkey = OpenRegKey(key, READ);
  if (hkey == nullptr) {
    return false;
  }

  char buffer[MAX_PATH];
  DWORD size = sizeof(buffer);
  DWORD type;
  LONG ret = RegQueryValueEx(
      hkey, name.c_str(), nullptr, &type, (LPBYTE)buffer, &size);
  RegCloseKey(hkey);
  if (ret != ERROR_SUCCESS) {
    return false;
  }

  /* Actually, CStrings are 8-bit clean, so we can accept any type of data.
   * Remove this if that becomes useful. */
  if (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ &&
      type != REG_BINARY) {
    return false; /* type mismatch */
  }

  if (size &&
      (type == REG_SZ || type == REG_MULTI_SZ || type == REG_EXPAND_SZ)) {
    --size; /* remove nul terminator */
  }

  val = std::string(buffer, size);
  return true;
}

bool RegistryAccess::GetRegValue(
    const std::string& key, const std::string& name, int& val,
    bool warn_on_error) {
  HKEY hkey = OpenRegKey(key, READ, warn_on_error);
  if (hkey == nullptr) {
    return false;
  }

  DWORD value;
  DWORD size = sizeof(value);
  DWORD type;
  LONG ret = RegQueryValueEx(
      hkey, name.c_str(), nullptr, &type, (LPBYTE)&value, &size);
  RegCloseKey(hkey);
  if (ret != ERROR_SUCCESS) {
    return false;
  }

  if (type != REG_DWORD) {
    return false; /* type mismatch */
  }

  val = value;
  return true;
}

bool RegistryAccess::GetRegValue(
    const std::string& key, const std::string& name, bool& val) {
  int int_val;
  bool b = GetRegValue(key, name, int_val);
  val = !!int_val;
  return b;
}

bool RegistryAccess::GetRegSubKeys(
    const std::string& key, std::vector<std::string>& lst,
    const std::string& regex, bool return_path_too) {
  HKEY hkey = OpenRegKey(key, READ);
  if (hkey == nullptr) {
    return false;
  }

  Regex re(regex);

  bool error = false;
  for (int index = 0;; ++index) {
    FILETIME ft;
    char buffer[MAX_PATH];
    DWORD size = sizeof(buffer);
    LONG ret = RegEnumKeyEx(
        hkey, index, buffer, &size, nullptr, nullptr, nullptr, &ft);
    if (ret == ERROR_NO_MORE_ITEMS) {
      break;
    }

    if (ret != ERROR_SUCCESS) {
      LOG->Warn(werr_ssprintf(ret, "GetRegSubKeys(%p,%i) error", hkey, index)
                    .c_str());
      error = true;
      break;
    }

    std::string str(buffer, size);

    if (re.Compare(str)) {
      if (return_path_too) {
        str = key + "\\" + str;
      }
      lst.push_back(str);
    }
  }

  RegCloseKey(hkey);

  return !error;
}

bool RegistryAccess::SetRegValue(
    const std::string& key, const std::string& name, const std::string& val) {
  HKEY hkey = OpenRegKey(key, WRITE);
  if (hkey == nullptr) {
    return false;
  }

  bool success = true;
  TCHAR sz[255];

  if (val.size() > 254) {
    return false;
  }

  strcpy(sz, val.c_str());

  LONG result = ::RegSetValueEx(
      hkey, LPCTSTR(name.c_str()), 0, REG_SZ, (LPBYTE)sz, strlen(sz) + 1);
  if (result != ERROR_SUCCESS) {
    success = false;
  }

  ::RegCloseKey(hkey);
  return success;
}

bool RegistryAccess::SetRegValue(
    const std::string& key, const std::string& name, bool val) {
  HKEY hkey = OpenRegKey(key, WRITE);
  if (hkey == nullptr) {
    return false;
  }

  bool success = true;

  if (::RegSetValueEx(
          hkey, LPCTSTR(name.c_str()), 0, REG_BINARY, (LPBYTE)&val,
          sizeof(val)) != ERROR_SUCCESS) {
    success = false;
  }

  ::RegCloseKey(hkey);
  return success;
}

bool RegistryAccess::CreateKey(const std::string& key) {
  std::string subkey;
  HKEY type;
  if (!GetRegKeyType(key, subkey, type)) {
    return false;
  }

  HKEY hkey;
  DWORD disposition = 0;
  if (::RegCreateKeyEx(
          type, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
          KEY_ALL_ACCESS, nullptr, &hkey, &disposition) != ERROR_SUCCESS) {
    return false;
  }
  ::RegCloseKey(hkey);
  return true;
}

/*
 * (c) 2004 Glenn Maynard
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
