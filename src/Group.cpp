#include "Group.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "IniFile.h"
#include "PrefsManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"

/** @brief The file that contains the group information.
 * We name this Pack.ini over Group.ini to avoid conflict
 * with the Waiei Group.ini-lua project still actively
 * being developed.
 */
const std::string INI_FILE = "Pack.ini";

namespace {

// Sets fOffset from a Pack.ini SyncOffset value. NULL and ITG are the only
// valid values; an empty one leaves fOffset alone, and anything else is
// reported and leaves it alone too.
void ApplySyncOffsetValue(
    const std::string& sValue, const std::string& sPackIniPath,
    const char* sCaller, float& fOffset) {
  // If the Pack.ini is specified as SyncOffset=ITG (regardless of the
  // m_DefaultSyncOffset value, we need to always remove the 9ms ITG
  // bias.
  //
  // Otherwise, if SyncOffset=NULL, we don't need to do anything since
  // it's already NULL synced, which is what we shift everything to.
  if (sValue.empty()) {
    return;
  }
  if (sValue == "NULL") {
    fOffset = 0.0f;
  } else if (sValue == "ITG") {
    fOffset = -0.009f;
  } else {
    LOG->Warn(
        "%s: Invalid SyncOffset value: %s in Pack.ini file \"%s\". Valid "
        "values are NULL and ITG. Using default value.",
        sCaller, sValue.c_str(), sPackIniPath.c_str());
  }
}

}  // namespace

Group::Group(
    const std::string& sDir, const std::string& sGroupDirName,
    bool bFromProfile) {
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
  // The m_DefaultSyncOffset preference corresponds to what offset the user
  // would like to assume as a "default" for packs without a Pack.ini.
  //
  // If the m_DefaultSyncOffset is set to ITG, we want to remove the 9ms ITG
  // bias for all packs without a Pack.ini.
  //
  // We can start with that as our default value, and potentially update it
  // below.
  m_fSyncOffset = GetDefaultSyncOffset();
  m_bHasPackIni = false;
  m_iYearReleased = 0;
  m_sBannerPath = "";
  m_sSeriesBannerPath = "";

  if (FILEMAN->DoesFileExist(sPackIniPath)) {
    IniFile ini;
    if (!ini.ReadFile(sPackIniPath)) {
      LOG->Warn(
          "Group::Group: Failed to read Pack.ini file at '%s': %s",
          sPackIniPath.c_str(), ini.GetError().c_str());
      return;
    }

    std::string sVersion = "";
    ini.GetValue("Group", "Version", sVersion);
    Trim(sVersion);

    // Only read the Pack.ini if the version is set
    // Otherwise log a warning and use default Pack.ini values
    if (!sVersion.empty()) {
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
        ini.GetValue("Group", key, value);
        Trim(value);

        if (value.empty()) {
          if (key == "DisplayTitle" || key == "SortTitle") {
            value = m_sGroupName;
          } else if (key == "TranslitTitle") {
            value = m_sDisplayTitle;
          }
        }
      }

      std::string sValue = "";
      ini.GetValue("Group", "SyncOffset", sValue);
      Trim(sValue);
      ApplySyncOffsetValue(sValue, sPackIniPath, "Group::Group", m_fSyncOffset);

      ini.GetValue("Group", "Year", m_iYearReleased);
    } else {
      LOG->Warn(
          "Group::Group: Pack.ini version not set in file \"%s\". Using "
          "default values.",
          sPackIniPath.c_str());
    }
  }
}

bool Group::ReloadSyncOffset() {
  if (m_sPath.empty()) {
    return false;
  }

  const std::string sPackIniPath = m_sPath + "/" + INI_FILE;
  float fOffset = GetDefaultSyncOffset();
  if (FILEMAN->DoesFileExist(sPackIniPath)) {
    IniFile ini;
    if (!ini.ReadFile(sPackIniPath)) {
      // A file that is there but can't be read leaves the offset as it was.
      LOG->Warn(
          "Group::ReloadSyncOffset: Failed to read Pack.ini file at '%s': %s",
          sPackIniPath.c_str(), ini.GetError().c_str());
      return false;
    }

    // As when the group was loaded, SyncOffset counts only with a Version.
    std::string sVersion = "";
    ini.GetValue("Group", "Version", sVersion);
    Trim(sVersion);
    if (!sVersion.empty()) {
      std::string sValue = "";
      ini.GetValue("Group", "SyncOffset", sValue);
      Trim(sValue);
      ApplySyncOffsetValue(
          sValue, sPackIniPath, "Group::ReloadSyncOffset", fOffset);
    }
  }

  const float fOldOffset = m_fSyncOffset;
  m_fSyncOffset = fOffset;
  return m_fSyncOffset != fOldOffset;
}

float Group::GetDefaultSyncOffset() {
  // The m_DefaultSyncOffset preference corresponds to what offset the user
  // would like to assume as a "default" for packs without a Pack.ini. If it
  // is set to ITG, we remove the 9ms ITG bias from those packs.
  return (PREFSMAN->m_DefaultSyncOffset == SyncOffset_NULL) ? 0.0f : -0.009f;
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
