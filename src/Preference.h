/* Preference - Holds user-chosen preferences that are saved between sessions.
 */

#ifndef PREFERENCE_H
#define PREFERENCE_H

#include <cstddef>
#include <string>
#include <vector>

#include "LuaManager.h"
#include "RageUtil.h"

class XNode;

enum class PreferenceType {
  // Allow reading and writing of the preference through Lua.
  // This is the default behavior.
  Mutable,

  // Mark the preference as read-only i.e. don't allow setting of the
  // preference through Lua.
  Immutable,

  // The preference is deprecated.
  // This deprecated flag will not be written to the Preferences file.
  // If this preference was deprecated in favor of a new one, see
  // TranslateDeprecatedFlags() to see the what it was deprecated in favor of.
  Deprecated,
};

struct lua_State;
class IPreference {
 public:
  IPreference(const std::string& sName, PreferenceType type);
  virtual ~IPreference();
  void ReadFrom(const XNode* pNode, bool bIsStatic);
  void WriteTo(XNode* pNode) const;
  void ReadDefaultFrom(const XNode* pNode);

  virtual void LoadDefault() = 0;
  virtual void SetDefaultFromString(const std::string& s) = 0;

  virtual std::string ToString() const = 0;
  virtual void FromString(const std::string& s) = 0;

  virtual void SetFromStack(lua_State* L);
  virtual void PushValue(lua_State* L) const;

  const std::string& GetName() const { return name_; }

  static IPreference* GetPreferenceByName(const std::string& sName);
  static void LoadAllDefaults();
  static void ReadAllPrefsFromNode(const XNode* pNode, bool bIsStatic);
  static void SavePrefsToNode(XNode* pNode);
  static void ReadAllDefaultsFromNode(const XNode* pNode);

  std::string GetName() { return name_; }
  bool IsImmutable() { return immutable_; }

 private:
  std::string name_;
  bool do_not_write_;
  bool immutable_;
};

void BroadcastPreferenceChanged(const std::string& sPreferenceName);

template <class T>
class Preference : public IPreference {
 public:
  Preference(
      const std::string& sName, const T& defaultValue,
      void(pfnValidate)(T& val) = nullptr,
      PreferenceType type = PreferenceType::Mutable)
      : IPreference(sName, type),
        current_value_(defaultValue),
        default_value_(defaultValue),
        validate_(pfnValidate) {
    LoadDefault();
  }

  std::string ToString() const {
    return StringConversion::ToString<T>(current_value_);
  }
  void FromString(const std::string& s) {
    if (!StringConversion::FromString<T>(s, current_value_)) {
      current_value_ = default_value_;
    }
    if (validate_) {
      validate_(current_value_);
    }
  }
  void SetFromStack(lua_State* L) {
    LuaHelpers::Pop<T>(L, current_value_);
    if (validate_) {
      validate_(current_value_);
    }
  }
  void PushValue(lua_State* L) const { LuaHelpers::Push<T>(L, current_value_); }

  void LoadDefault() { current_value_ = default_value_; }
  void SetDefaultFromString(const std::string& s) {
    T def = default_value_;
    if (!StringConversion::FromString<T>(s, default_value_)) {
      default_value_ = def;
    }
  }

  const T& Get() const { return current_value_; }

  const T& GetDefault() const { return default_value_; }

  operator const T&() const { return Get(); }

  void Set(const T& other) {
    current_value_ = other;
    BroadcastPreferenceChanged(GetName());
  }

  static Preference<T>* GetPreferenceByName(const std::string& sName) {
    IPreference* pPreference = IPreference::GetPreferenceByName(sName);
    Preference<T>* pRet = dynamic_cast<Preference<T>*>(pPreference);
    return pRet;
  }

 private:
  T current_value_;
  T default_value_;
  void (*validate_)(T& val);
};

/** @brief Utilities for working with Lua. */
namespace LuaHelpers {
template <typename T>
void Push(lua_State* L, const Preference<T>& Object) {
  LuaHelpers::Push<T>(L, Object.Get());
}
}  // namespace LuaHelpers

template <class T>
class Preference1D {
 public:
  typedef Preference<T> PreferenceT;
  std::vector<PreferenceT*> v_;

  Preference1D(
      void pfn(size_t i, std::string& sNameOut, T& defaultValueOut), size_t N,
      PreferenceType type = PreferenceType::Mutable) {
    for (size_t i = 0; i < N; ++i) {
      std::string sName;
      T defaultValue;
      pfn(i, sName, defaultValue);
      v_.push_back(new Preference<T>(sName, defaultValue, nullptr, type));
    }
  }

  ~Preference1D() {
    for (size_t i = 0; i < v_.size(); ++i) {
      RageUtil::SafeDelete(v_[i]);
    }
  }
  const Preference<T>& operator[](size_t i) const { return *v_[i]; }
  Preference<T>& operator[](size_t i) { return *v_[i]; }
};

#endif

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez
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
