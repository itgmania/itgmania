/* ThemeMetric - Theme specific data. */

#ifndef THEME_METRIC_H
#define THEME_METRIC_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "LuaManager.h"
#include "LuaReference.h"
#include "ThemeManager.h"
#include "global.h"

/** @brief The general interface for reading ThemeMetrics. */
class IThemeMetric {
 public:
  virtual ~IThemeMetric() {}
  virtual void Read() = 0;
  virtual void Clear() = 0;
};

template <class T>
struct ThemeMetricTypeTraits {
  enum { Callable = 1 };
};

/* LuaReference and apActorCommands return the function directly without calling
 * it. */
template <>
struct ThemeMetricTypeTraits<LuaReference> {
  enum { Callable = 0 };
};
template <>
struct ThemeMetricTypeTraits<apActorCommands> {
  enum { Callable = 0 };
};

/** @brief The theme specific data.
 *
 * Each piece of data is to correspond to a type. */
template <class T>
class ThemeMetric : public IThemeMetric {
 protected:
  /** @brief the metric's group.
   *
   * In metrics.ini, it is usually done as such: [GroupName] */
  std::string group_;
  /** @brief the metric's name. */
  std::string name_;
  /** @brief the metric's value. */
  LuaReference value_;
  mutable T current_value_;
  bool call_each_time_;

 public:
  /* Initializing with no group and name is allowed; if you do this, you must
   * call Load() to set them.  This is done to allow initializing cached metrics
   * in one place for classes that don't receive their m_sName in the
   * constructor (everything except screens). */
  ThemeMetric(const std::string& group = "", const std::string& name = "")
      : group_(group),
        name_(name),
        value_(),
        current_value_(T()),
        call_each_time_(false) {
    ThemeManager::Subscribe(this);
  }

  ThemeMetric(const ThemeMetric<T>& cpy)
      : IThemeMetric(cpy),
        group_(cpy.group_),
        name_(cpy.name_),
        value_(cpy.value_)
  // do we transfer the current value or bCallEachTime?
  {
    ThemeManager::Subscribe(this);
  }

  ~ThemeMetric() { ThemeManager::Unsubscribe(this); }
  /**
   * @brief Load the chosen metric from the .ini file.
   * @param sGroup the group the metric is in.
   * @param sName the name of the metric. */
  void Load(const std::string& group, const std::string& name) {
    group_ = group;
    name_ = name;
    Read();
  }
  /**
   * @brief Actually read the metric and get its data. */
  void Read() {
    if (name_ != "" && THEME && THEME->IsThemeLoaded()) {
      Lua* L = LUA->Get();
      THEME->GetMetric(group_, name_, value_);
      value_.PushSelf(L);
      LuaHelpers::FromStack(L, current_value_, -1);
      lua_pop(L, 1);
      LUA->Release(L);

      /* If the value is a function, evaluate it every time. */
      call_each_time_ = ThemeMetricTypeTraits<T>::Callable &&
                        value_.GetLuaType() == LUA_TFUNCTION;
    } else {
      value_.Unset();
      call_each_time_ = false;
    }
  }

  void PushSelf(Lua* L) {
    ASSERT(value_.IsSet());
    value_.PushSelf(L);
  }

  void Clear() { value_.Unset(); }

  /**
   * @brief Retrieve the metric's name.
   * @return the metric's name. */
  const std::string& GetName() const { return name_; }

  /**
   * @brief Retrieve the metric's value.
   * @return the metric's value. */
  const T& GetValue() const {
    ASSERT(name_ != "");
    ASSERT_M(value_.IsSet(), group_ + " " + name_);

    if (call_each_time_) {
      Lua* L = LUA->Get();

      // call function with 0 arguments and 1 result
      value_.PushSelf(L);
      std::string error = group_ + ": " + name_ + ": ";
      LuaHelpers::RunScriptOnStack(L, error, 0, 1, true);
      if (!lua_isnil(L, -1)) {
        LuaHelpers::Pop(L, current_value_);
      } else {
        lua_pop(L, 1);
      }
      LUA->Release(L);
    }

    return current_value_;
  }

  operator const T&() const { return GetValue(); }

  bool IsLoaded() const { return value_.IsSet(); }

  // Hacks for VC6 for all boolean operators.
  // These three no longer appear to be required:
  // bool operator ! () const { return !GetValue(); }
  // bool operator && ( const T& input ) const { return GetValue() && input; }
  // bool operator || ( const T& input ) const { return GetValue() || input; }

  // This one is still required in at least Visual Studio 2008:
  bool operator==(const T& input) const { return GetValue() == input; }
};

typedef std::string (*MetricName1D)(size_t N);

template <class T>
class ThemeMetric1D {
  typedef ThemeMetric<T> ThemeMetricT;
  std::vector<ThemeMetricT> metric_;

 public:
  ThemeMetric1D(const std::string& group, MetricName1D pfn, size_t n) {
    Load(group, pfn, n);
  }
  ThemeMetric1D() { Load(std::string(), nullptr, 0); }
  void Load(const std::string& group, MetricName1D pfn, size_t n) {
    metric_.resize(n);
    for (unsigned i = 0; i < n; i++) {
      metric_[i].Load(group, pfn(i));
    }
  }
  void Clear() {
    for (unsigned i = 0; i < metric_.size(); i++) {
      metric_[i].Clear();
    }
  }
  const T& GetValue(size_t i) const { return metric_[i].GetValue(); }
};

typedef std::string (*MetricName2D)(size_t N, size_t M);

template <class T>
class ThemeMetric2D {
  typedef ThemeMetric<T> ThemeMetricT;
  typedef std::vector<ThemeMetricT> ThemeMetricTVector;
  std::vector<ThemeMetricTVector> metric_;

 public:
  ThemeMetric2D(
      const std::string& group = "", MetricName2D pfn = nullptr, size_t n = 0,
      size_t m = 0) {
    Load(group, pfn, n, m);
  }
  void Load(const std::string& group, MetricName2D pfn, size_t n, size_t m) {
    metric_.resize(n);
    for (unsigned i = 0; i < n; i++) {
      metric_[i].resize(m);
      for (unsigned j = 0; j < m; j++) {
        metric_[i][j].Load(group, pfn(i, j));
      }
    }
  }
  const T& GetValue(size_t i, size_t j) const {
    return metric_[i][j].GetValue();
  }
};

typedef std::string (*MetricNameMap)(std::string s);

template <class T>
class ThemeMetricMap {
  typedef ThemeMetric<T> ThemeMetricT;
  std::map<std::string, ThemeMetricT> metric_;

 public:
  ThemeMetricMap(
      const std::string& group = "", MetricNameMap pfn = nullptr,
      const std::vector<std::string> value_names = std::vector<std::string>()) {
    Load(group, pfn, value_names);
  }
  void Load(
      const std::string& group, MetricNameMap pfn,
      const std::vector<std::string> value_names) {
    metric_.clear();
    for (const std::string& s : value_names) {
      metric_[s].Load(group, pfn(s));
    }
  }
  const T& GetValue(std::string s) const {
    // HACK: GCC (3.4) takes this and pretty much nothing else.
    // I don't know why.
    typename std::map<std::string, ThemeMetric<T> >::const_iterator iter =
        metric_.find(s);
    ASSERT(iter != metric_.end());
    return iter->second.GetValue();
  }
};

#endif

/**
 * @file
 * @author Chris Danford, Chris Gomez (c) 2001-2004
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
