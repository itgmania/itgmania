#include "Group.h"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "PrefsManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"

/** @brief The file that contains the group information.
 * We name this Pack.ini over Group.ini to avoid conflict
 * with the Waiei Group.ini-lua project still actively
 * being developed.
 */
const std::string INI_FILE = "Pack.ini";

/** @brief The sync offset that removes the 9ms ITG bias. */
const float ITG_SYNC_OFFSET = -0.009f;

namespace {

typedef std::map<std::string, std::string> PackIniValues;

/**
 * @brief Read the [Group] section of a Pack.ini, keyed by lower case key name.
 *
 * Pack.ini files are written by hand, so this is more forgiving than IniFile:
 * section and key names are matched regardless of case, and lines may be
 * indented or carry trailing whitespace. A key given twice keeps its last
 * value.
 */
bool ReadPackIni(const std::string& sPath, PackIniValues& values) {
  RageFile file;
  if (!file.Open(sPath)) {
    LOG->Warn(
        "Group::Group: Failed to read Pack.ini file at '%s': %s", sPath.c_str(),
        file.GetError().c_str());
    return false;
  }

  bool bInGroupSection = false;
  for (;;) {
    std::string sLine;
    const int iRead = file.GetLine(sLine);
    if (iRead == 0) {
      return true;  // eof
    }
    if (iRead == -1) {
      LOG->Warn(
          "Group::Group: Failed to read Pack.ini file at '%s': %s",
          sPath.c_str(), file.GetError().c_str());
      return false;
    }

    utf8_remove_bom(sLine);
    Trim(sLine);
    // Same comment styles as IniFile.
    if (sLine.empty() || sLine[0] == ';' || sLine[0] == '#' ||
        BeginsWith(sLine, "//") || BeginsWith(sLine, "--")) {
      continue;
    }

    if (sLine[0] == '[' && sLine[sLine.size() - 1] == ']') {
      std::string sSection = sLine.substr(1, sLine.size() - 2);
      Trim(sSection);
      bInGroupSection = EqualsNoCase(sSection, "Group");
      continue;
    }

    const std::size_t iEquals = sLine.find('=');
    if (!bInGroupSection || iEquals == std::string::npos) {
      continue;
    }
    std::string sKey = sLine.substr(0, iEquals);
    std::string sValue = sLine.substr(iEquals + 1);
    Trim(sKey);
    Trim(sValue);
    if (!sKey.empty()) {
      values[MakeLower(sKey)] = sValue;
    }
  }
}

/** @brief The trimmed value of a Pack.ini key, or "" if it isn't set. */
std::string GetPackIniValue(const PackIniValues& values, std::string sKey) {
  const PackIniValues::const_iterator it = values.find(MakeLower(sKey));
  return it != values.end() ? it->second : std::string();
}

/**
 * @brief Turn a SyncOffset value into seconds.
 *
 * NULL and ITG are accepted in any case, in quotes, and followed by a ; or #
 * comment, none of which can be part of a valid value.
 *
 * @return true if the value was recognised, false otherwise (fOffset is left
 * alone).
 */
bool ParseSyncOffset(std::string sValue, float& fOffset) {
  const std::size_t iComment = sValue.find_first_of(";#");
  if (iComment != std::string::npos) {
    sValue.erase(iComment);
  }
  Trim(sValue);
  if (sValue.size() >= 2 && (sValue[0] == '"' || sValue[0] == '\'') &&
      sValue[sValue.size() - 1] == sValue[0]) {
    sValue = sValue.substr(1, sValue.size() - 2);
    Trim(sValue);
  }

  if (EqualsNoCase(sValue, "NULL")) {
    fOffset = 0.0f;
    return true;
  }
  if (EqualsNoCase(sValue, "ITG")) {
    fOffset = ITG_SYNC_OFFSET;
    return true;
  }
  return false;
}

/**
 * @brief The sync offset a Pack.ini asks for, or the default when it doesn't
 * set a recognised one.
 */
float GetPackIniSyncOffset(
    const PackIniValues& values, const std::string& sPackIniPath) {
  // If the Pack.ini is specified as SyncOffset=ITG (regardless of the
  // m_DefaultSyncOffset value, we need to always remove the 9ms ITG
  // bias.
  //
  // Otherwise, if SyncOffset=NULL, we don't need to do anything since
  // it's already NULL synced, which is what we shift everything to.
  float fOffset = Group::GetDefaultSyncOffset();
  const std::string sValue = GetPackIniValue(values, "SyncOffset");
  if (!sValue.empty() && !ParseSyncOffset(sValue, fOffset)) {
    LOG->Warn(
        "Group::Group: Invalid SyncOffset value: %s in Pack.ini file "
        "\"%s\". Valid values are NULL and ITG. Using default value.",
        sValue.c_str(), sPackIniPath.c_str());
  }
  return fOffset;
}

}  // namespace

float Group::GetDefaultSyncOffset() {
  // The m_DefaultSyncOffset preference corresponds to what offset the user
  // would like to assume as a "default" for packs without a Pack.ini.
  //
  // If the m_DefaultSyncOffset is set to ITG, we want to remove the 9ms ITG
  // bias for all packs without a Pack.ini.
  return (PREFSMAN->m_DefaultSyncOffset == SyncOffset_NULL) ? 0.0f
                                                            : ITG_SYNC_OFFSET;
}

Group::Group(
    const std::string& sDir, const std::string& sGroupDirName,
    bool bFromProfile) {
  // Start from the defaults, so that an early return still leaves a usable
  // group.
  m_fSyncOffset = GetDefaultSyncOffset();
  m_bHasPackIni = false;
  m_iYearReleased = 0;

  if (sDir.empty() || sGroupDirName.empty()) {
    LOG->Warn("Group::Group: Empty directory or group name provided.");
    return;
  }

  std::string sPackIniPath;
  if (bFromProfile) {
    sPackIniPath = sDir + "/" + INI_FILE;
    m_sPath = sDir;
    m_sGroupName = sDir.substr(1, sDir.find('/', 1) - 1);
    m_sDisplayTitle = sGroupDirName;
  } else {
    sPackIniPath = sDir + sGroupDirName + "/" + INI_FILE;
    m_sPath = sDir + sGroupDirName;
    m_sGroupName = Basename(sGroupDirName);
    m_sDisplayTitle = m_sGroupName;
  }
  m_sSortTitle = m_sGroupName;
  m_sTranslitTitle = m_sGroupName;
  m_sSeries = "";
  m_sBannerPath = "";
  m_sSeriesBannerPath = "";

  PackIniValues values;
  if (!FILEMAN->DoesFileExist(sPackIniPath) ||
      !ReadPackIni(sPackIniPath, values)) {
    return;
  }

  // SyncOffset is read even when the Version is missing. The engine has only
  // ever checked that a Version is there, never its number, and SyncOffset has
  // meant the same thing since the first version, so skipping it would just
  // make the pack play 9ms off without saying so.
  m_fSyncOffset = GetPackIniSyncOffset(values, sPackIniPath);

  // Only read the rest of the Pack.ini if the version is set
  // Otherwise log a warning and use default Pack.ini values
  if (GetPackIniValue(values, "Version").empty()) {
    LOG->Warn(
        "Group::Group: Pack.ini version not set in file \"%s\". Only its "
        "SyncOffset is used.",
        sPackIniPath.c_str());
    return;
  }
  m_bHasPackIni = true;

  // Define a vector of key-value pairs to cleanly iterate
  std::vector<std::pair<std::string, std::string&>> vPackfields = {
      {"DisplayTitle", m_sDisplayTitle},
      {"SortTitle", m_sSortTitle},
      {"TranslitTitle", m_sTranslitTitle},
      {"Series", m_sSeries},
      {"Banner", m_sBannerPath},
      {"SeriesBanner", m_sSeriesBannerPath}};

  for (auto& [key, value] : vPackfields) {
    const PackIniValues::const_iterator it =
        values.find(MakeLower(std::string(key)));
    if (it != values.end()) {
      value = it->second;
    }

    if (value.empty()) {
      if (key == "DisplayTitle" || key == "SortTitle") {
        value = m_sGroupName;
      } else if (key == "TranslitTitle") {
        value = m_sDisplayTitle;
      }
    }
  }

  m_iYearReleased = StringToInt(GetPackIniValue(values, "Year"));
}

#include "LuaBinding.h"

class LunaGroup : public Luna<Group> {
 public:
  static int GetGroupName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetGroupName().c_str());
    return 1;
  }
  static int GetSortTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetSortTitle().c_str());
    return 1;
  }

  static int GetDisplayTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayTitle().c_str());
    return 1;
  }

  static int GetTranslitTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitTitle().c_str());
    return 1;
  }

  static int GetSeries(T* p, lua_State* L) {
    lua_pushstring(L, p->GetSeries().c_str());
    return 1;
  }

  static int GetSyncOffset(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSyncOffset());
    return 1;
  }

  static int HasPackIni(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasPackIni());
    return 1;
  }

  static int GetBannerPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetBannerPath().c_str());
    return 1;
  }

  static int GetYearReleased(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetYearReleased());
    return 1;
  }

  LunaGroup() {
    ADD_METHOD(GetGroupName);
    ADD_METHOD(GetSortTitle);
    ADD_METHOD(GetDisplayTitle);
    ADD_METHOD(GetTranslitTitle);
    ADD_METHOD(GetSeries);
    ADD_METHOD(GetSyncOffset);
    ADD_METHOD(HasPackIni);
    ADD_METHOD(GetBannerPath);
    ADD_METHOD(GetYearReleased);
  }
};

LUA_REGISTER_CLASS(Group)

// lua end
