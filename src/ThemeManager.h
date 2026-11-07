#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <set>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "LuaReference.h"
#include "RageTypes.h"

class IThemeMetric;
class IniFile;
struct lua_State;

enum ElementCategory {
  EC_BGANIMATIONS,
  EC_FONTS,
  EC_GRAPHICS,
  EC_SOUNDS,
  EC_OTHER,
  NUM_ElementCategory,
  ElementCategory_Invalid
};
/** @brief A special foreach loop going through each ElementCategory. */
#define FOREACH_ElementCategory(ec) FOREACH_ENUM(ElementCategory, ec)
const std::string& ElementCategoryToString(ElementCategory ec);
ElementCategory StringToElementCategory(const std::string& s);

struct Theme;
/** @brief Manages theme paths and metrics. */
class ThemeManager {
 public:
  ThemeManager();
  ~ThemeManager();

  void GetThemeNames(std::vector<std::string>& add_to);
  void GetSelectableThemeNames(std::vector<std::string>& add_to);
  int GetNumSelectableThemes();
  bool DoesThemeExist(const std::string& theme_name);
  bool IsThemeSelectable(const std::string& name);
  bool IsThemeNameValid(const std::string& name);
  std::string GetThemeDisplayName(const std::string& theme_name);
  std::string GetThemeAuthor(const std::string& theme_name);
  void GetLanguages(std::vector<std::string>& add_to);
  bool DoesLanguageExist(const std::string& language);
  void SwitchThemeAndLanguage(
      const std::string& theme_name, const std::string& language,
      bool pseudo_localize, bool force_theme_reload = false);
  void UpdateLuaGlobals();
  std::string GetCurThemeName() const { return cur_theme_name_; };
  bool IsThemeLoaded() const { return !cur_theme_name_.empty(); };
  std::string GetCurLanguage() const { return cur_language_; };
  std::string GetCurThemeDir() const {
    return GetThemeDirFromName(cur_theme_name_);
  };
  std::string GetNextSelectableTheme();
  void ReloadMetrics();
  void ReloadSubscribers();

  static void EvaluateString(std::string& text);

  struct PathInfo {
    std::string resolved_path_;
    std::string matching_metrics_group_;
    std::string matching_element_;
  };

  bool GetPathInfo(
      PathInfo& out, ElementCategory category, const std::string& metrics_group,
      const std::string& element, bool optional = false);
  std::string GetPath(
      ElementCategory category, const std::string& metrics_group,
      const std::string& element, bool optional = false);
  std::string GetPathB(
      const std::string& metrics_group, const std::string& element,
      bool optional = false) {
    return GetPath(EC_BGANIMATIONS, metrics_group, element, optional);
  };
  std::string GetPathF(
      const std::string& metrics_group, const std::string& element,
      bool optional = false) {
    return GetPath(EC_FONTS, metrics_group, element, optional);
  };
  std::string GetPathG(
      const std::string& metrics_group, const std::string& element,
      bool optional = false) {
    return GetPath(EC_GRAPHICS, metrics_group, element, optional);
  };
  std::string GetPathS(
      const std::string& metrics_group, const std::string& element,
      bool optional = false) {
    return GetPath(EC_SOUNDS, metrics_group, element, optional);
  };
  std::string GetPathO(
      const std::string& metrics_group, const std::string& element,
      bool optional = false) {
    return GetPath(EC_OTHER, metrics_group, element, optional);
  };
  void ClearThemePathCache();

  bool HasMetric(
      const std::string& metrics_group, const std::string& value_name);
  void PushMetric(
      Lua* L, const std::string& metrics_group, const std::string& value_name);
  std::string GetMetric(
      const std::string& metrics_group, const std::string& value_name);
  int GetMetricI(
      const std::string& metrics_group, const std::string& value_name);
  float GetMetricF(
      const std::string& metrics_group, const std::string& value_name);
  bool GetMetricB(
      const std::string& metrics_group, const std::string& value_name);
  RageColor GetMetricC(
      const std::string& metrics_group, const std::string& value_name);
  LuaReference GetMetricR(
      const std::string& metrics_group, const std::string& value_name);
  apActorCommands GetMetricA(
      const std::string& metrics_group, const std::string& value_name);

  void GetMetric(
      const std::string& metrics_group, const std::string& value_name,
      LuaReference& value_out);

  // Languages
  bool HasString(
      const std::string& metrics_group, const std::string& value_name);
  std::string GetString(
      const std::string& metrics_group, const std::string& value_name);
  void GetString(
      const std::string& metrics_group, const std::string& value_name,
      std::string& value_out) {
    value_out = GetString(metrics_group, value_name);
  }
  void FilterFileLanguages(std::vector<std::string>& element_paths);

  void GetMetricsThatBeginWith(
      const std::string& metrics_group, const std::string& value_name,
      std::set<std::string>& value_names_out);

  std::string GetMetricsGroupFallback(const std::string& metrics_group);

  static std::string GetBlankGraphicPath();

  // needs to be public for its binding to work
  void RunLuaScripts(const std::string& mask, bool use_theme_dir = false);

  // For self-registering metrics
  static void Subscribe(IThemeMetric* p);
  static void Unsubscribe(IThemeMetric* p);

  // Lua
  void PushSelf(lua_State* L);

 protected:
  void LoadThemeMetrics(
      const std::string& theme_name, const std::string& language);
  std::string GetMetricRaw(
      const IniFile& ini, const std::string& metrics_group,
      const std::string& value_name);
  bool GetMetricRawRecursive(
      const IniFile& ini, const std::string& metrics_group,
      const std::string& value_name, std::string& ret);

  bool GetPathInfoToAndFallback(
      PathInfo& out, ElementCategory category, const std::string& metrics_group,
      const std::string& file);
  bool GetPathInfoToRaw(
      PathInfo& out, const std::string& theme_name, ElementCategory category,
      const std::string& metrics_group, const std::string& file);
  static std::string GetThemeDirFromName(const std::string& theme_name);
  static std::string GetMetricsIniPath(const std::string& theme_name);
  static void GetLanguagesForTheme(
      const std::string& theme_name, std::vector<std::string>& languages_out);
  static std::string GetLanguageIniPath(
      const std::string& theme_name, const std::string& language);
  void GetOptionalLanguageIniPaths(
      std::vector<std::string>& paths_out, const std::string& theme_name,
      const std::string& language);
  std::string GetDefaultLanguage();

  std::string cur_theme_name_;
  std::string cur_language_;
  bool pseudo_localize_;
};

extern ThemeManager*
    THEME;  // global and accessible from anywhere in our program

#endif

/*
 * (c) 2001-2004 Chris Danford
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
