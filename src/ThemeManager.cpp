#include "ThemeManager.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "FontCharAliases.h"
#include "GameLoop.h"  // For ChangeTheme
#include "IniFile.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageException.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "SubscriptionManager.h"
#include "ThemeMetric.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"

ThemeManager* THEME =
    nullptr;  // global object accessible from anywhere in the program

static const std::string kThemeInfoIni = "ThemeInfo.ini";

static const char* ElementCategoryNames[] = {
    "BGAnimations", "Fonts", "Graphics", "Sounds", "Other"};
XToString(ElementCategory);
StringToX(ElementCategory);

struct Theme {
  std::string theme_name_;
};
// When looking for a metric or an element, search these from head to tail.
static std::deque<Theme> g_themes;
class LoadedThemeData {
 public:
  IniFile ini_metrics_;
  IniFile ini_strings_;
  void ClearAll() {
    ini_metrics_.Clear();
    ini_strings_.Clear();
  }
};
LoadedThemeData* loaded_theme_data = nullptr;

// For self-registering metrics
#include "SubscriptionManager.h"
static SubscriptionManager<IThemeMetric> subscribers;

class LocalizedStringImplThemeMetric : public ILocalizedStringImpl,
                                       public ThemeMetric<std::string> {
 public:
  static ILocalizedStringImpl* Create() {
    return new LocalizedStringImplThemeMetric;
  }

  void Load(const std::string& group, const std::string& name) {
    ThemeMetric<std::string>::Load(group, name);
  }

  virtual void Read() {
    if (name_ != "" && THEME && THEME->IsThemeLoaded()) {
      THEME->GetString(group_, name_, current_value_);
      value_.SetFromNil();
    }
  }

  const std::string& GetLocalized() const {
    if (IsLoaded()) {
      return GetValue();
    }
    const std::string& cur_language =
        (THEME && THEME->IsThemeLoaded() ? THEME->GetCurLanguage()
                                         : std::string("current"));
    LOG->Warn(
        "Missing translation for %s in the %s language.", name_.c_str(),
        cur_language.c_str());
    return name_;
  }
};

void ThemeManager::Subscribe(IThemeMetric* p) {
  subscribers.Subscribe(p);

  // It's ThemeManager's responsibility to make sure all of its subscribers
  // are updated with current data.  If a metric is created after
  // a theme is loaded, ThemeManager should update it right away (not just
  // when the theme changes).
  if (THEME && THEME->GetCurThemeName().size()) {
    p->Read();
  }
}

void ThemeManager::Unsubscribe(IThemeMetric* p) { subscribers.Unsubscribe(p); }

// We spend a lot of time doing redundant theme path lookups. Cache results.
static std::map<std::string, ThemeManager::PathInfo>
    theme_path_cache[NUM_ElementCategory];
void ThemeManager::ClearThemePathCache() {
  for (int i = 0; i < NUM_ElementCategory; ++i) {
    theme_path_cache[i].clear();
  }
}

static void FileNameToMetricsGroupAndElement(
    const std::string& file_name, std::string& metrics_group_out,
    std::string& element_out) {
  // split into class name and file name
  std::string::size_type index_of_first_space = file_name.find(" ");
  if (index_of_first_space == std::string::npos)  // no space
  {
    metrics_group_out = "";
    element_out = file_name;
  } else {
    metrics_group_out = Left(file_name, index_of_first_space);
    element_out = Right(file_name, file_name.size() - index_of_first_space - 1);
  }
}

static std::string MetricsGroupAndElementToFileName(
    const std::string& metrics_group, const std::string& element) {
  if (metrics_group.empty()) {
    return element;
  } else {
    return metrics_group + " " + element;
  }
}

ThemeManager::ThemeManager() {
  THEME = this;  // so that we can Register THEME on construction

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "THEME");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  // We don't have any theme loaded until SwitchThemeAndLanguage is called.
  cur_theme_name_ = "";
  pseudo_localize_ = false;

  std::vector<std::string> array_theme_names;
  GetThemeNames(array_theme_names);
}

ThemeManager::~ThemeManager() {
  g_themes.clear();
  RageUtil::SafeDelete(loaded_theme_data);

  // Unregister with Lua.
  LUA->UnsetGlobal("THEME");
}

void ThemeManager::GetThemeNames(std::vector<std::string>& add_to) {
  GetDirListing(SpecialFiles::kThemesDir + "*", add_to, true);
  StripCvsAndSvn(add_to);
  StripMacResourceForks(add_to);
}

void ThemeManager::GetSelectableThemeNames(std::vector<std::string>& add_to) {
  GetThemeNames(add_to);
  for (int i = add_to.size() - 1; i >= 0; i--) {
    if (!IsThemeNameValid(add_to[i])) {
      add_to.erase(add_to.begin() + i);
    }
  }
}

int ThemeManager::GetNumSelectableThemes() {
  std::vector<std::string> vs;
  GetSelectableThemeNames(vs);
  return vs.size();
}

bool ThemeManager::DoesThemeExist(const std::string& theme_name) {
  std::vector<std::string> theme_names;
  GetThemeNames(theme_names);
  for (unsigned i = 0; i < theme_names.size(); i++) {
    if (!CompareNoCase(theme_name, theme_names[i])) {
      return true;
    }
  }
  return false;
}

bool ThemeManager::IsThemeSelectable(const std::string& name) {
  return IsThemeNameValid(name) && DoesThemeExist(name);
}

bool ThemeManager::IsThemeNameValid(const std::string& name) {
  return Left(name, 1) != "_";
}

std::string ThemeManager::GetThemeDisplayName(const std::string& theme_name) {
  std::string dir = GetThemeDirFromName(theme_name);
  IniFile ini;
  ini.ReadFile(dir + kThemeInfoIni);

  std::string s;
  if (ini.GetValue("ThemeInfo", "DisplayName", s)) {
    return s;
  }

  return theme_name;
}

std::string ThemeManager::GetThemeAuthor(const std::string& theme_name) {
  std::string dir = GetThemeDirFromName(theme_name);
  IniFile ini;
  ini.ReadFile(dir + kThemeInfoIni);

  std::string s;
  if (ini.GetValue("ThemeInfo", "Author", s)) {
    return s;
  }

  return "[unknown author]";
}

void ThemeManager::GetLanguages(std::vector<std::string>& add_to) {
  add_to.clear();

  for (unsigned i = 0; i < g_themes.size(); ++i) {
    GetLanguagesForTheme(g_themes[i].theme_name_, add_to);
  }

  // remove dupes
  sort(add_to.begin(), add_to.end());
  std::vector<std::string>::iterator it =
      unique(add_to.begin(), add_to.end(), EqualsNoCase);
  add_to.erase(it, add_to.end());
}

bool ThemeManager::DoesLanguageExist(const std::string& language) {
  std::vector<std::string> languages;
  GetLanguages(languages);

  for (unsigned i = 0; i < languages.size(); i++) {
    if (CompareNoCase(language, languages[i]) == 0) {
      return true;
    }
  }
  return false;
}

void ThemeManager::LoadThemeMetrics(
    const std::string& raw_theme_name, const std::string& raw_language) {
  if (loaded_theme_data == nullptr) {
    loaded_theme_data = new LoadedThemeData;
  }

  // Don't delete and recreate LoadedThemeData.  There are references iniMetrics
  // and iniStrings on the stack, so Clear them instead.
  loaded_theme_data->ClearAll();
  g_themes.clear();

  std::string theme_name(raw_theme_name);
  std::string language(raw_language);

  cur_theme_name_ = theme_name;
  cur_language_ = language;

  bool loaded_base = false;
  for (;;) {
    ASSERT_M(
        g_themes.size() < 20, "Circular theme fallback references detected.");

    g_themes.push_back(Theme());
    Theme& t = g_themes.back();
    t.theme_name_ = theme_name;

    IniFile ini_metrics;
    IniFile ini_strings;
    ini_metrics.ReadFile(GetMetricsIniPath(theme_name));
    // Load optional language inis (probably mounted by a package) first so that
    // they can be overridden by the current theme.
    {
      std::vector<std::string> vs;
      GetOptionalLanguageIniPaths(vs, theme_name, language);
      for (const std::string& s : vs) {
        ini_strings.ReadFile(s);
      }
    }
    ini_strings.ReadFile(
        GetLanguageIniPath(theme_name, SpecialFiles::kBaseLanguage));
    if (CompareNoCase(language, SpecialFiles::kBaseLanguage)) {
      ini_strings.ReadFile(GetLanguageIniPath(theme_name, language));
    }
    bool is_base_theme =
        !CompareNoCase(theme_name, SpecialFiles::kBaseThemeName);
    ini_metrics.GetValue("Global", "IsBaseTheme", is_base_theme);
    if (is_base_theme) {
      loaded_base = true;
    }
    /* Read the fallback theme. If no fallback theme is specified, and we
     * haven't already loaded it, fall back on SpecialFiles::BASE_THEME_NAME.
     * That way, default theme fallbacks can be disabled with
     * "FallbackTheme=". */
    std::string fallback;
    if (!ini_metrics.GetValue("Global", "FallbackTheme", fallback)) {
      if (CompareNoCase(theme_name, SpecialFiles::kBaseThemeName) &&
          !loaded_base) {
        fallback = SpecialFiles::kBaseThemeName;
      }
    }
    /* We actually want to load themes bottom-to-top, loading fallback themes
     * first, so derived themes overwrite metrics in fallback themes. But, we
     * need to load the derived theme first, to find out the name of the
     * fallback theme.  Avoid having to load IniFile twice, merging the fallback
     * theme into the derived theme that we've already loaded. */
    XmlFileUtil::MergeIniUnder(&ini_metrics, &loaded_theme_data->ini_metrics_);
    XmlFileUtil::MergeIniUnder(&ini_strings, &loaded_theme_data->ini_strings_);

    if (fallback.empty()) {
      break;
    }
    theme_name = fallback;
  }

  // Overlay metrics from the command line.
  std::string metric;
  for (int i = 0; GetCommandlineArgument("metric", &metric, i); ++i) {
    /* sMetric must be "foo::bar=baz". "foo" and "bar" never contain "=", so
     * in "foo::bar=1+1=2", "baz" is always "1+1=2". Neither foo nor bar may
     * be empty, but baz may be. */
    Regex re("^([^=]+)::([^=]+)=(.*)$");
    std::vector<std::string> bits;
    if (!re.Compare(metric, bits)) {
      RageException::Throw("Invalid argument \"--metric=%s\".", metric.c_str());
    }

    loaded_theme_data->ini_metrics_.SetValue(bits[0], bits[1], bits[2]);
  }

  LOG->MapLog("theme", "Theme: %s", cur_theme_name_.c_str());
  LOG->MapLog("language", "Language: %s", cur_language_.c_str());
}

std::string ThemeManager::GetDefaultLanguage() {
  std::string lang_code = HOOKS->GetPreferredLanguage();
  return lang_code;
}

void ThemeManager::SwitchThemeAndLanguage(
    const std::string& raw_theme_name, const std::string& raw_language,
    bool pseudo_localize, bool force_theme_reload) {
  std::string theme_name = raw_theme_name;
  std::string language = raw_language;
  // todo: if the theme isn't selectable, find the next theme that is,
  // and change to that instead of asserting/crashing since
  // SpecialFiles::BASE_THEME_NAME is _fallback now. -aj
  if (!IsThemeSelectable(theme_name)) {
    std::string to_try = PREFSMAN->theme_.GetDefault();
    LOG->Warn(
        "Selected theme '%s' not found.  "
        "Trying Theme preference default value '%s'.",
        theme_name.c_str(), to_try.c_str());
    theme_name = to_try;
    // sm-ssc's SpecialFiles::BASE_THEME_NAME is _fallback, which you can't
    // select. This requires a preference, which allows it to be adapted for
    // other purposes (e.g. PARASTAR).
    if (!IsThemeSelectable(theme_name)) {
      to_try = PREFSMAN->default_theme_;
      LOG->Warn(
          "Theme preference defaults to '%s', which cannot be used."
          "  Trying DefaultTheme preference '%s'.",
          theme_name.c_str(), to_try.c_str());
      theme_name = to_try;
      if (!IsThemeSelectable(theme_name)) {
        std::vector<std::string> theme_names;
        GetSelectableThemeNames(theme_names);
        ASSERT_M(
            !theme_names.empty(),
            "No themes found, unable to start stepmania.");
        to_try = theme_names[0];
        LOG->Warn(
            "DefaultTheme preference is '%s', which cannot be found."
            "  Using '%s'.",
            theme_name.c_str(), to_try.c_str());
        theme_name = to_try;
        PREFSMAN->default_theme_.Set(to_try);
      }
    }
    PREFSMAN->theme_.Set(theme_name);
  }

  /* We haven't actually loaded the theme yet, so we can't check whether
   * sLanguage exists. Just check for empty. */
  if (language.empty()) {
    language = GetDefaultLanguage();
  }

  LOG->Trace(
      "ThemeManager::SwitchThemeAndLanguage: \"%s\", \"%s\"",
      theme_name.c_str(), language.c_str());

  bool nothing_changing = theme_name == cur_theme_name_ &&
                          language == cur_language_ &&
                          pseudo_localize_ == pseudo_localize;
  if (nothing_changing && !force_theme_reload) {
    return;
  }

  pseudo_localize_ = pseudo_localize;

  // Load theme metrics. If only the language is changing, this is all
  // we need to reload.
  bool theme_changing = (theme_name != cur_theme_name_);
  LoadThemeMetrics(theme_name, language);

  // Clear the theme path cache. This caches language-specific graphic paths,
  // so do this even if only the language is changing.
  ClearThemePathCache();
  if (theme_changing || force_theme_reload) {
    // reload common sounds
    if (SCREENMAN != nullptr) {
      SCREENMAN->ThemeChanged();
    }

    /* Lua globals can use metrics which are cached, and vice versa.  Update Lua
     * globals first; it's Lua's job to explicitly update cached metrics that it
     * uses. */
    UpdateLuaGlobals();

    // Reload MachineProfile with new theme's CustomLoadFunction
    if (PROFILEMAN != nullptr) {
      Profile* profile = PROFILEMAN->GetMachineProfile();
      profile->LoadCustomFunction(
          "/Save/MachineProfile/", PlayerNumber_Invalid);
    }
  }

  // Use theme metrics for localization.
  LocalizedString::RegisterLocalizer(LocalizedStringImplThemeMetric::Create);

  ReloadSubscribers();
}

void ThemeManager::ReloadSubscribers() {
  // reload subscribers
  if (subscribers.subscribers_) {
    for (IThemeMetric* metric : *subscribers.subscribers_) {
      metric->Read();
    }
  }
}

void ThemeManager::RunLuaScripts(const std::string& mask, bool use_theme_dir) {
  /* Run all script files with the given mask in Lua for all themes.  Start
   * from the deepest fallback theme and work outwards. */

  /* TODO: verify whether this final check is necessary. */
  const std::string cur_theme_name = cur_theme_name_;

  std::deque<Theme>::const_iterator iter = g_themes.end();
  do {
    --iter;

    /* HACK: pretend to be the theme these are under by setting m_sCurThemeName
     * to the name of the theme whose scripts are currently running; if those
     * scripts call GetThemeName(), it'll return the theme the script is in. */

    cur_theme_name_ = iter->theme_name_;
    const std::string& script_dir =
        use_theme_dir ? GetThemeDirFromName(cur_theme_name_) : std::string("/");

    std::vector<std::string> element_paths;
    // get files from directories
    std::vector<std::string> element_child_paths;
    std::vector<std::string> array_script_dirs;
    GetDirListing(script_dir + "Scripts/*", array_script_dirs, true);
    SortRStringArray(array_script_dirs);
    StripCvsAndSvn(array_script_dirs);
    StripMacResourceForks(array_script_dirs);
    for (const std::string& script_dir_name :
         array_script_dirs)  // foreach dir in /Scripts/
    {
      // Find all Lua files in this directory, add them to asElementPaths
      GetDirListing(
          script_dir + "Scripts/" + script_dir_name + "/" + mask,
          element_child_paths, false, true);
      for (unsigned i = 0; i < element_child_paths.size(); ++i) {
        // push these Lua files into the main element paths
        const std::string& path = element_child_paths[i];
        element_paths.push_back(path);
      }
    }

    // get regular Lua files
    GetDirListing(script_dir + "Scripts/" + mask, element_paths, false, true);

    // load Lua files
    for (unsigned i = 0; i < element_paths.size(); ++i) {
      const std::string& path = element_paths[i];
      LOG->Trace("Loading \"%s\" ...", path.c_str());
      LuaHelpers::RunScriptFile(path);
    }
  } while (iter != g_themes.begin());

  /* TODO: verify whether this final check is necessary. */
  if (cur_theme_name != cur_theme_name_) {
    LOG->Warn("ThemeManager: theme name was not restored after RunLuaScripts");
    cur_theme_name_ = cur_theme_name;
  }
}

void ThemeManager::UpdateLuaGlobals() {
  // explicitly refresh cached metrics that we use.
  ScreenDimensions::ReloadScreenDimensions();

  // run global scripts
  RunLuaScripts("*.lua");
  // run theme scripts
  RunLuaScripts("*.lua", true);
}

std::string ThemeManager::GetThemeDirFromName(const std::string& theme_name) {
  return SpecialFiles::kThemesDir + theme_name + "/";
}

struct CompareLanguageTag {
  std::string language_string_;
  CompareLanguageTag(const std::string& lang) {
    language_string_ = std::string("(lang ") + lang + ")";
    LOG->Trace("try \"%s\"", lang.c_str());
    MakeLower(language_string_);
  }

  bool operator()(const std::string& file) const {
    std::string lower(file);
    MakeLower(lower);
    size_t pos = lower.find(language_string_);
    return pos != std::string::npos;
  }
};

/* If there's more than one result, check for language tags.  For example,
 *
 * ScreenCompany graphic (lang English).png
 * ScreenCompany graphic (lang French).png
 *
 * We still want to warn for ambiguous results.  Use std::unique to filter
 * files with the current language tag to the top, so choosing "ignore" from
 * the multiple-match dialog will cause it to default to the first entry, so
 * it'll still use a preferred language match if there were any. */
void ThemeManager::FilterFileLanguages(std::vector<std::string>& paths) {
  if (paths.size() <= 1) {
    return;
  }
  std::vector<std::string>::iterator it =
      partition(paths.begin(), paths.end(), CompareLanguageTag(cur_language_));

  int dist = distance(paths.begin(), it);
  if (dist == 0) {
    // We didn't find any for the current language.  Try BASE_LANGUAGE.
    it = partition(
        paths.begin(), paths.end(),
        CompareLanguageTag(SpecialFiles::kBaseLanguage));
    dist = distance(paths.begin(), it);
  }

  if (dist == 1) {
    paths.erase(it, paths.end());
  }
}

bool ThemeManager::GetPathInfoToRaw(
    PathInfo& out, const std::string& raw_theme_name, ElementCategory category,
    const std::string& raw_metrics_group, const std::string& raw_element) {
  /* Ugly: the parameters to this function may be a reference into g_vThemes,
   * or something else that might suddenly go away when we call ReloadMetrics,
   * so make a copy. */
  const std::string theme_name = raw_theme_name;
  const std::string metrics_group = raw_metrics_group;
  const std::string element = raw_element;

  const std::string theme_dir = GetThemeDirFromName(theme_name);
  const std::string& category_str = ElementCategoryToString(category);

  std::vector<std::string> element_paths;

  // If sFileName already has an extension, we're looking for a specific file
  bool looking_for_specific_file = element.find_last_of('.') != element.npos;

  if (looking_for_specific_file) {
    GetDirListing(
        theme_dir + category_str + "/" +
            MetricsGroupAndElementToFileName(metrics_group, element),
        element_paths, false, true);
  } else  // look for all files starting with sFileName that have types we can
          // use
  {
    std::vector<std::string> paths;
    GetDirListing(
        theme_dir + category_str + "/" +
            MetricsGroupAndElementToFileName(metrics_group, element) + "*",
        paths, false, true);

    for (unsigned p = 0; p < paths.size(); ++p) {
      // BGAnimations, Fonts, Graphics, Sounds, Other
      const std::string ext = GetExtension(paths[p]);
      bool matches = category == EC_OTHER || ext == "redir";
      if (!matches) {
        FileType ft = ActorUtil::GetFileType(paths[p]);
        switch (ft) {
          case FT_Bitmap:
          case FT_Sprite:
          case FT_Movie:
          case FT_Xml:
          case FT_Model:
          case FT_Lua:
            matches = category == EC_BGANIMATIONS || category == EC_GRAPHICS ||
                      category == EC_SOUNDS;
            break;
          case FT_Ini:
            matches = category == EC_FONTS;
            break;
          case FT_Directory: {
            std::string xml_path = paths[p] + "/default.xml";
            if (DoesFileExist(xml_path)) {
              element_paths.push_back(xml_path);
              break;
            }
            std::string lua_path = paths[p] + "/default.lua";
            if (DoesFileExist(lua_path)) {
              element_paths.push_back(lua_path);
              break;
            }
          }
            matches = category == EC_BGANIMATIONS || category == EC_GRAPHICS;
            break;
          case FT_Sound:
            matches = category == EC_SOUNDS;
            break;
          default:
            matches = false;
            break;
        }
      }
      if (matches) {
        element_paths.push_back(paths[p]);
      }
    }
  }

  if (element_paths.size() == 0) {
    return false;  // This isn't fatal.
  }

  FilterFileLanguages(element_paths);

  if (element_paths.size() > 1) {
    theme_path_cache[category].clear();

    std::string message = ssprintf(
        "ThemeManager:  There is more than one theme element that matches "
        "'%s/%s/%s'.  Please remove all but one of these matches: ",
        theme_name.c_str(), category_str.c_str(),
        MetricsGroupAndElementToFileName(metrics_group, element).c_str());
    message += element_paths[1];
    for (size_t i = 1; i < element_paths.size(); ++i) {
      message += ", " + element_paths[i];
    }

    switch (LuaHelpers::ReportScriptError(message, "", true)) {
      case Dialog::abort:
        RageException::Throw("%s", message.c_str());
        break;
      case Dialog::retry:
        ReloadMetrics();
        return GetPathInfoToRaw(
            out, raw_theme_name, category, raw_metrics_group, raw_element);
      case Dialog::ignore:
      default:
        break;
    }
  }

  std::string path = element_paths[0];
  bool is_a_redirect = CompareNoCase(GetExtension(path), "redir") == 0;

  if (!is_a_redirect) {
    out.resolved_path_ = path;
    out.matching_metrics_group_ = metrics_group;
    out.matching_element_ = element;
    return true;
  }

  std::string new_file_name;
  GetFileContents(path, new_file_name, true);

  std::string new_class_name, new_file;
  FileNameToMetricsGroupAndElement(new_file_name, new_class_name, new_file);

  /* Important: We need to do a full search.  For example, BG redirs in
   * the default theme point to "_shared background", and themes override
   * just "_shared background"; the redirs in the default theme should end
   * up resolving to the overridden background. */
  /* Use GetPathToOptional because we don't want report that there's an
   * element missing. Instead we want to report that the redirect is invalid. */
  if (GetPathInfo(out, category, new_class_name, new_file, true)) {
    return true;
  }

  std::string message = ssprintf(
      "ThemeManager:  The redirect '%s' points to the file '%s', which does "
      "not exist. "
      "Verify that this redirect is correct.",
      path.c_str(), new_file_name.c_str());

  switch (LuaHelpers::ReportScriptError(message, "", true)) {
    case Dialog::retry:
      ReloadMetrics();
      return GetPathInfoToRaw(
          out, raw_theme_name, category, raw_metrics_group, raw_element);
    case Dialog::ignore:
      GetPathInfo(out, category, "", "_missing");
      return true;
    default:
      RageException::Throw("%s", message.c_str());
  }
}

bool ThemeManager::GetPathInfoToAndFallback(
    PathInfo& out, ElementCategory category,
    const std::string& raw_metrics_group, const std::string& element) {
  std::string metrics_group(raw_metrics_group);

  int n = 100;
  while (n--) {
    for (const Theme& theme : g_themes) {
      // search with requested name
      if (GetPathInfoToRaw(
              out, theme.theme_name_, category, metrics_group, element)) {
        return true;
      }
    }

    if (metrics_group.empty()) {
      return false;
    }

    // search fallback name (if any)
    metrics_group = GetMetricsGroupFallback(metrics_group);
    if (metrics_group.empty()) {
      return false;
    }
  }

  LuaHelpers::ReportScriptErrorFmt(
      "Infinite recursion looking up theme element \"%s\"",
      MetricsGroupAndElementToFileName(metrics_group, element).c_str());
  return false;
}

bool ThemeManager::GetPathInfo(
    PathInfo& out, ElementCategory category,
    const std::string& raw_metrics_group, const std::string& raw_element,
    bool optional) {
  /* Ugly: the parameters to this function may be a reference into g_vThemes,
   * or something else that might suddenly go away when we call ReloadMetrics.
   */
  const std::string metrics_group = raw_metrics_group;
  const std::string element = raw_element;

  std::string file_name =
      MetricsGroupAndElementToFileName(metrics_group, element);

  std::map<std::string, PathInfo>& cache = theme_path_cache[category];
  {
    std::map<std::string, PathInfo>::const_iterator i;

    i = cache.find(file_name);
    if (i != cache.end()) {
      out = i->second;
      return true;
    }
  }

try_element_again:

  // search the current theme
  if (GetPathInfoToAndFallback(
          out, category, metrics_group, element))  // we found something
  {
    cache[file_name] = out;
    return true;
  }

  if (optional) {
    cache[file_name] = PathInfo();  // clear cache entry
    return false;
  }

  const std::string& category_str = ElementCategoryToString(category);

  // We can't fall back on _missing in Other: the file types are unknown.
  std::string message = "The theme element \"" + category_str + "/" +
                        file_name + "\" is missing.";
  Dialog::Result res;
  if (category != EC_OTHER) {
    res = Dialog::AbortRetryIgnore(message, "MissingThemeElement");
  } else {
    res = Dialog::AbortRetry(message, "MissingThemeElement");
  }
  switch (res) {
    case Dialog::retry:
      ReloadMetrics();
      goto try_element_again;
    case Dialog::ignore: {
      std::string element_name = category_str + '/' + file_name;
      std::string error = "could not be found in \"" +
                          GetThemeDirFromName(cur_theme_name_) + "\" or \"" +
                          GetThemeDirFromName(SpecialFiles::kBaseThemeName) +
                          "\".";
      LOG->UserLog("Theme element", element_name.c_str(), "%s", error.c_str());
      LOG->Warn("%s %s", element_name.c_str(), error.c_str());
      LuaHelpers::ScriptErrorMessage("'" + element_name + "' " + error);
    }

      // Err?
      if (file_name == "_missing") {
        RageException::Throw(
            "\"_missing\" isn't present in \"%s%s\".",
            GetThemeDirFromName(SpecialFiles::kBaseThemeName).c_str(),
            category_str.c_str());
      }

      GetPathInfo(out, category, "", "_missing");
      cache[file_name] = out;
      return true;
    case Dialog::abort:
      LOG->UserLog(
          "Theme element", category_str + '/' + file_name,
          "could not be found in \"%s\" or \"%s\".",
          GetThemeDirFromName(cur_theme_name_).c_str(),
          GetThemeDirFromName(SpecialFiles::kBaseThemeName).c_str());
      RageException::Throw(
          "Theme element \"%s/%s\" could not be found in \"%s\" or \"%s\".",
          category_str.c_str(), file_name.c_str(),
          GetThemeDirFromName(cur_theme_name_).c_str(),
          GetThemeDirFromName(SpecialFiles::kBaseThemeName).c_str());
      DEFAULT_FAIL(res);
  }
}

std::string ThemeManager::GetPath(
    ElementCategory category, const std::string& metrics_group,
    const std::string& element, bool optional) {
  PathInfo pi;
  GetPathInfo(pi, category, metrics_group, element, optional);
  if (!optional && pi.resolved_path_.empty()) {
    LuaHelpers::ReportScriptErrorFmt(
        "Theme element not found and not "
        "optional: Category: %s  Metrics group: %s  Element name: %s.",
        ElementCategoryToString(category).c_str(), metrics_group.c_str(),
        element.c_str());
  }
  return pi.resolved_path_;
}

std::string ThemeManager::GetMetricsIniPath(const std::string& theme_name) {
  return GetThemeDirFromName(theme_name) + SpecialFiles::kMetricsFile;
}

bool ThemeManager::HasMetric(
    const std::string& metrics_group, const std::string& value_name) {
  std::string throw_away;
  if (metrics_group == "" || value_name == "") {
    return false;
  }
  return GetMetricRawRecursive(
      loaded_theme_data->ini_metrics_, metrics_group, value_name, throw_away);
}

bool ThemeManager::HasString(
    const std::string& metrics_group, const std::string& value_name) {
  std::string throw_away;
  if (metrics_group == "" || value_name == "") {
    return false;
  }
  return GetMetricRawRecursive(
      loaded_theme_data->ini_strings_, metrics_group, value_name, throw_away);
}

// the strings that were here before were moved to StepMania.cpp;
// sorry for the inconvienence/sloppy coding. -aj
void ThemeManager::ReloadMetrics() {
  FILEMAN->FlushDirCache(GetCurThemeDir());

  // Reloading Lua scripts can cause crashes; don't do this. -aj
  // UpdateLuaGlobals();

  // force a reload of the metrics cache
  LoadThemeMetrics(cur_theme_name_, cur_language_);
  ReloadSubscribers();

  ClearThemePathCache();
}

std::string ThemeManager::GetMetricsGroupFallback(
    const std::string& metrics_group) {
  ASSERT(loaded_theme_data != nullptr);

  // always look in iniMetrics for "Fallback"
  std::string fallback;
  if (!GetMetricRawRecursive(
          loaded_theme_data->ini_metrics_, metrics_group, "Fallback",
          fallback)) {
    return std::string();
  }

  Lua* L = LUA->Get();
  LuaHelpers::RunExpression(L, fallback);
  std::string ret;
  LuaHelpers::Pop(L, ret);
  LUA->Release(L);

  return ret;
}

bool ThemeManager::GetMetricRawRecursive(
    const IniFile& ini, const std::string& raw_metrics_group,
    const std::string& value_name, std::string& out) {
  ASSERT(value_name != "");
  std::string metrics_group(raw_metrics_group);

  int n = 100;
  while (n--) {
    if (ini.GetValue(metrics_group, value_name, out)) {
      return true;
    }

    if (!value_name.compare("Fallback")) {
      return false;
    }

    metrics_group = GetMetricsGroupFallback(metrics_group);
    if (metrics_group.empty()) {
      return false;
    }
  }

  LuaHelpers::ReportScriptErrorFmt(
      "Infinite recursion looking up theme metric \"%s::%s\".",
      metrics_group.c_str(), value_name.c_str());
  return false;
}

std::string ThemeManager::GetMetricRaw(
    const IniFile& ini, const std::string& raw_metrics_group,
    const std::string& raw_value_name) {
  /* Ugly: the parameters to this function may be a reference into g_vThemes, or
   * something else that might suddenly go away when we call ReloadMetrics. */
  const std::string metrics_group = raw_metrics_group;
  const std::string value_name = raw_value_name;

  for (;;) {
    std::string ret;
    if (ThemeManager::GetMetricRawRecursive(
            ini, metrics_group, value_name, ret)) {
      return ret;
    }
    std::string cur_metric_path = GetMetricsIniPath(cur_theme_name_);
    std::string default_metric_path =
        GetMetricsIniPath(SpecialFiles::kBaseThemeName);

    std::string type;
    if (&ini == &loaded_theme_data->ini_strings_) {
      type = "String";
    } else if (&ini == &loaded_theme_data->ini_metrics_) {
      type = "Metric";
    } else {
      FAIL_M("");
    }

    std::string message = ssprintf(
        "%s \"%s::%s\" is missing.", type.c_str(), metrics_group.c_str(),
        value_name.c_str());

    switch (LuaHelpers::ReportScriptError(message, "", true)) {
      case Dialog::abort: {
        RageException::Throw(
            "%s \"%s::%s\" could not be found in \"%s\"' or \"%s\".",
            type.c_str(), metrics_group.c_str(), value_name.c_str(),
            cur_metric_path.c_str(), default_metric_path.c_str());
      }
      case Dialog::retry:
        ReloadMetrics();
        continue;
      case Dialog::ignore:
        LOG->UserLog(
            type, metrics_group + "::" + value_name,
            "could not be found in \"%s\" or \"%s\".", cur_metric_path.c_str(),
            default_metric_path.c_str());
        return std::string();
      default:
        FAIL_M("Unexpected answer to Abort/Retry/Ignore dialog");
    }
  }
}

template <typename T>
void GetAndConvertMetric(
    const std::string& metrics_group, const std::string& value_name, T& out) {
  Lua* L = LUA->Get();

  THEME->PushMetric(L, metrics_group, value_name);
  LuaHelpers::FromStack(L, out, -1);
  lua_pop(L, 1);

  LUA->Release(L);
}

/* Get a string metric. */
std::string ThemeManager::GetMetric(
    const std::string& metrics_group, const std::string& value_name) {
  std::string ret;
  GetAndConvertMetric(metrics_group, value_name, ret);
  return ret;
}

int ThemeManager::GetMetricI(
    const std::string& metrics_group, const std::string& value_name) {
  int ret = 0;
  GetAndConvertMetric(metrics_group, value_name, ret);
  return ret;
}

float ThemeManager::GetMetricF(
    const std::string& metrics_group, const std::string& value_name) {
  float ret = 0;
  GetAndConvertMetric(metrics_group, value_name, ret);
  return ret;
}

bool ThemeManager::GetMetricB(
    const std::string& metrics_group, const std::string& value_name) {
  bool ret = 0;
  GetAndConvertMetric(metrics_group, value_name, ret);
  return ret;
}

RageColor ThemeManager::GetMetricC(
    const std::string& metrics_group, const std::string& value_name) {
  RageColor ret;
  GetAndConvertMetric(metrics_group, value_name, ret);
  return ret;
}

LuaReference ThemeManager::GetMetricR(
    const std::string& metrics_group, const std::string& value_name) {
  LuaReference ref;
  GetMetric(metrics_group, value_name, ref);
  return ref;
}

void ThemeManager::PushMetric(
    Lua* L, const std::string& metrics_group, const std::string& value_name) {
  if (metrics_group == "" || value_name == "") {
    LuaHelpers::ReportScriptError(
        "PushMetric:  Attempted to fetch metric with empty group name or empty "
        "value name.");
    lua_pushnil(L);
    return;
  }
  std::string value =
      GetMetricRaw(loaded_theme_data->ini_metrics_, metrics_group, value_name);

  std::string name =
      ssprintf("%s::%s", metrics_group.c_str(), value_name.c_str());
  if (EndsWith(value_name, "Command")) {
    LuaHelpers::ParseCommandList(L, value, name, false);
  } else {
    // Remove unary +, eg. "+50"; Lua doesn't support that.
    if (value.size() >= 1 && value[0] == '+') {
      value.erase(0, 1);
    }

    LuaHelpers::RunExpression(L, value, name);
  }
}

void ThemeManager::GetMetric(
    const std::string& metrics_group, const std::string& value_name,
    LuaReference& value_out) {
  Lua* L = LUA->Get();
  PushMetric(L, metrics_group, value_name);
  value_out.SetFromStack(L);
  LUA->Release(L);
}

apActorCommands ThemeManager::GetMetricA(
    const std::string& metrics_group, const std::string& value_name) {
  LuaReference* ref = new LuaReference;
  GetMetric(metrics_group, value_name, *ref);
  return apActorCommands(ref);
}

void ThemeManager::EvaluateString(std::string& text) {
  FontCharAliases::ReplaceMarkers(text);
}

std::string ThemeManager::GetNextSelectableTheme() {
  std::vector<std::string> as;
  GetSelectableThemeNames(as);
  unsigned i;
  for (i = 0; i < as.size(); i++) {
    if (CompareNoCase(as[i], cur_theme_name_) == 0) {
      break;
    }
  }
  int new_index = (i + 1) % as.size();
  return as[new_index];
}

void ThemeManager::GetLanguagesForTheme(
    const std::string& theme_name, std::vector<std::string>& languages_out) {
  std::string language_dir =
      GetThemeDirFromName(theme_name) + SpecialFiles::kLanguagesSubdir;
  std::vector<std::string> as;
  GetDirListing(language_dir + "*.ini", as);

  for (const std::string& s : as) {
    // ignore metrics.ini
    if (CompareNoCase(s, SpecialFiles::kMetricsFile) == 0) {
      continue;
    }

    // Ignore filenames with a space.  These are optional language inis that
    // probably came from a mounted package.
    if (s.find(" ") != std::string::npos) {
      continue;
    }

    // strip ".ini"
    std::string s2 = Left(s, s.size() - 4);

    languages_out.push_back(s2);
  }
}

std::string ThemeManager::GetLanguageIniPath(
    const std::string& theme_name, const std::string& language) {
  return GetThemeDirFromName(theme_name) + SpecialFiles::kLanguagesSubdir +
         language + ".ini";
}

void ThemeManager::GetOptionalLanguageIniPaths(
    std::vector<std::string>& paths_out, const std::string& theme_name,
    const std::string& language) {
  // optional ini names look like: "en PackageName.ini"
  GetDirListing(
      GetThemeDirFromName(theme_name) + SpecialFiles::kLanguagesSubdir +
          language + " *.ini",
      paths_out, false, true);
}

static std::string PseudoLocalize(std::string s) {
  Replace(s, "a", "\xc3\xa0\xc3\xa1");  // àá
  Replace(s, "A", "\xc3\x80\xc3\x80");  // ÀÀ
  Replace(s, "e", "\xc3\xa9\xc3\xa9");  // éé
  Replace(s, "E", "\xc3\x89\xc3\x89");  // ÉÉ
  Replace(s, "i", "\xc3\xad\xc3\xad");  // íí
  Replace(s, "I", "\xc3\x8d\xc3\x8d");  // ÍÍ
  Replace(s, "o", "\xc3\xb3\xc3\xb3");  // óó
  Replace(s, "O", "\xc3\x93\xc3\x93");  // ÓÓ
  Replace(s, "u", "\xc3\xbc\xc3\xbc");  // üü
  Replace(s, "U", "\xc3\x9c\xc3\x9c");  // ÜÜ
  Replace(s, "n", "\xc3\xb1");          // ñ
  Replace(s, "N", "\xc3\x91");          // Ñ
  Replace(s, "c", "\xc3\xa7");          // ç
  Replace(s, "C", "\xc3\x87");          // Ç
  // transformations that help expose punctuation assumptions
  // s.Replace( ":", " :" );	// this messes up "::" help text tip separator
  // markers
  Replace(s, "?", " ?");
  Replace(s, "!", " !");

  return s;
}

std::string ThemeManager::GetString(
    const std::string& metrics_group, const std::string& raw_value_name) {
  std::string value_name = raw_value_name;
  if (metrics_group == "" || value_name == "") {
    LuaHelpers::ReportScriptError(
        "PushMetric:  Attempted to fetch metric with empty group name or empty "
        "value name.");
    return "";
  }

  // TODO: Handle escaping = with \=
  DEBUG_ASSERT(value_name.find('=') == value_name.npos);

  // TODO: Move this escaping into IniFile?
  Replace(value_name, "\r\n", "\\n");
  Replace(value_name, "\n", "\\n");

  ASSERT(loaded_theme_data != nullptr);
  std::string s =
      GetMetricRaw(loaded_theme_data->ini_strings_, metrics_group, value_name);
  FontCharAliases::ReplaceMarkers(s);

  // Don't EvalulateString.  Strings are raw and shouldn't allow Lua.
  // EvaluateString( s );

  Replace(s, "\\n", "\n");

  if (pseudo_localize_) {
    // pseudolocalize ignoring replace markers.  e.g.: "%{steps} steps:
    // %{author}"
    std::string translated;

    for (; true;) {
      std::string::size_type pos = s.find("%{");
      if (pos == s.npos) {
        translated += PseudoLocalize(s);
        s = std::string();
        break;
      } else {
        translated += PseudoLocalize(s.substr(0, pos));
        s.erase(s.begin(), s.begin() + pos);
      }

      pos = s.find("}");
      translated += s.substr(0, pos + 1);
      s.erase(s.begin(), s.begin() + pos + 1);
    }

    s = translated;
  }

  return s;
}

void ThemeManager::GetMetricsThatBeginWith(
    const std::string& raw_metrics_group, const std::string& value_name,
    std::set<std::string>& value_names_out) {
  std::string metrics_group(raw_metrics_group);
  while (!metrics_group.empty()) {
    const XNode* cur = loaded_theme_data->ini_metrics_.GetChild(metrics_group);
    if (cur != nullptr) {
      // Iterate over all metrics that match.
      for (XAttrs::const_iterator j = cur->attrs_.lower_bound(value_name);
           j != cur->attrs_.end(); ++j) {
        const std::string& sv = j->first;
        if (Left(sv, value_name.size()) == value_name) {
          value_names_out.insert(sv);
        } else {  // we passed the last metric that matched sValueName
          break;
        }
      }
    }

    // put the fallback (if any) in sMetricsGroup
    metrics_group = GetMetricsGroupFallback(metrics_group);
  }
}

std::string ThemeManager::GetBlankGraphicPath() {
  return SpecialFiles::kThemesDir + SpecialFiles::kBaseThemeName + "/" +
         ElementCategoryToString(EC_GRAPHICS) + "/_blank.png";
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ThemeManager. */
class LunaThemeManager : public Luna<ThemeManager> {
 public:
  static int ReloadMetrics(T* p, lua_State* L) {
    p->ReloadMetrics();
    return 0;
  }

  static int HasMetric(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasMetric(SArg(1), SArg(2)));
    return 1;
  }
  static int GetMetric(T* p, lua_State* L) {
    std::string group = SArg(1);
    std::string name = SArg(2);
    if (group == "" || name == "") {
      luaL_error(
          L, "Cannot fetch metric with empty group name or empty value name.");
    }
    p->PushMetric(L, group, name);
    return 1;
  }
  static int HasString(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasString(SArg(1), SArg(2)));
    return 1;
  }
  static int GetString(T* p, lua_State* L) {
    std::string group = SArg(1);
    std::string name = SArg(2);
    if (group == "" || name == "") {
      luaL_error(
          L, "Cannot fetch string with empty group name or empty value name.");
    }
    lua_pushstring(L, p->GetString(group, name).c_str());
    return 1;
  }
  static int GetPathInfoB(T* p, lua_State* L) {
    ThemeManager::PathInfo pi;
    p->GetPathInfo(pi, EC_BGANIMATIONS, SArg(1), SArg(2));
    lua_pushstring(L, pi.resolved_path_.c_str());
    lua_pushstring(L, pi.matching_metrics_group_.c_str());
    lua_pushstring(L, pi.matching_element_.c_str());
    return 3;
  }
  // GENERAL_GET_PATH uses lua_toboolean instead of BArg because that makes
  // it optional. -Kyz
#define GENERAL_GET_PATH(get_path_name)                                      \
  static int get_path_name(T* p, lua_State* L) {                             \
    lua_pushstring(                                                          \
        L, p->get_path_name(SArg(1), SArg(2), lua_toboolean(L, 3)).c_str()); \
    return 1;                                                                \
  }
  GENERAL_GET_PATH(GetPathF);
  GENERAL_GET_PATH(GetPathG);
  GENERAL_GET_PATH(GetPathB);
  GENERAL_GET_PATH(GetPathS);
  GENERAL_GET_PATH(GetPathO);
#undef GENERAL_GET_PATH

  static int RunLuaScripts(T* p, lua_State* L) {
    p->RunLuaScripts(SArg(1));
    return 1;
  }

  static int GetSelectableThemeNames(T* p, lua_State* L) {
    // pushes a table of theme folders from GetSelectableThemeNames()
    // lua_pushnumber(L, p->GetNumSelectableThemes() );
    std::vector<std::string> themes;
    p->GetSelectableThemeNames(themes);
    LuaHelpers::CreateTableFromArray<std::string>(themes, L);
    return 1;
  }

  static int GetNumSelectableThemes(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumSelectableThemes());
    return 1;
  }

  DEFINE_METHOD(GetCurrentThemeDirectory, GetCurThemeDir());
  DEFINE_METHOD(GetCurLanguage, GetCurLanguage());
  static int GetThemeDisplayName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetThemeDisplayName(p->GetCurThemeName()).c_str());
    return 1;
  }
  static int GetThemeAuthor(T* p, lua_State* L) {
    lua_pushstring(L, p->GetThemeAuthor(p->GetCurThemeName()).c_str());
    return 1;
  }
  DEFINE_METHOD(DoesThemeExist, DoesThemeExist(SArg(1)));
  DEFINE_METHOD(IsThemeSelectable, IsThemeSelectable(SArg(1)));
  DEFINE_METHOD(DoesLanguageExist, DoesLanguageExist(SArg(1)));
  DEFINE_METHOD(GetCurThemeName, GetCurThemeName());

  static void PushMetricNamesInGroup(const IniFile& ini, lua_State* L) {
    std::string group_name = SArg(1);
    const XNode* metric_node = ini.GetChild(group_name);
    if (metric_node != nullptr) {
      // Placed in a table indexed by number, so the order is always the same.
      lua_createtable(L, metric_node->attrs_.size(), 0);
      int next_index = 1;
      for (XAttrs::const_iterator n = metric_node->attrs_.begin();
           n != metric_node->attrs_.end(); ++n) {
        LuaHelpers::Push(L, n->first);
        lua_rawseti(L, -2, next_index);
        ++next_index;
      }
    } else {
      lua_pushnil(L);
    }
  }

  static int GetMetricNamesInGroup(T* p, lua_State* L) {
    PushMetricNamesInGroup(loaded_theme_data->ini_metrics_, L);
    return 1;
  }

  static int GetStringNamesInGroup(T* p, lua_State* L) {
    PushMetricNamesInGroup(loaded_theme_data->ini_strings_, L);
    return 1;
  }

  static int SetTheme(T* p, lua_State* L) {
    std::string theme_name = SArg(1);
    if (!p->IsThemeSelectable(theme_name)) {
      luaL_error(L, "SetTheme: Invalid Theme: '%s'", theme_name.c_str());
    }
    GameLoop::ChangeTheme(theme_name);
    return 0;
  }

  static int get_theme_fallback_list(T* p, lua_State* L) {
    lua_createtable(L, g_themes.size(), 0);
    int ret = lua_gettop(L);
    for (size_t tid = 0; tid < g_themes.size(); ++tid) {
      lua_pushstring(L, g_themes[tid].theme_name_.c_str());
      lua_rawseti(L, ret, tid + 1);
    }
    return 1;
  }

  LunaThemeManager() {
    ADD_METHOD(ReloadMetrics);
    ADD_METHOD(GetMetric);
    ADD_METHOD(GetString);
    ADD_METHOD(GetPathInfoB);
    ADD_METHOD(GetPathF);
    ADD_METHOD(GetPathG);
    ADD_METHOD(GetPathB);
    ADD_METHOD(GetPathS);
    ADD_METHOD(GetPathO);
    ADD_METHOD(RunLuaScripts);
    ADD_METHOD(GetSelectableThemeNames);
    ADD_METHOD(GetNumSelectableThemes);
    ADD_METHOD(GetCurrentThemeDirectory);
    ADD_METHOD(GetCurLanguage);
    ADD_METHOD(GetThemeDisplayName);
    ADD_METHOD(GetThemeAuthor);
    ADD_METHOD(DoesThemeExist);
    ADD_METHOD(IsThemeSelectable);
    ADD_METHOD(DoesLanguageExist);
    ADD_METHOD(GetCurThemeName);
    ADD_METHOD(HasMetric);
    ADD_METHOD(HasString);
    ADD_METHOD(GetMetricNamesInGroup);
    ADD_METHOD(GetStringNamesInGroup);
    ADD_METHOD(SetTheme);
    ADD_METHOD(get_theme_fallback_list);
  }
};

LUA_REGISTER_CLASS(ThemeManager)
// lua end

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
