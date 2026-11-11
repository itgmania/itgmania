#include "Group.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "IniFile.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
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
const std::string kIniFile = "Pack.ini";

static std::string GetInternalGroupName(
    ProfileSlot prof_slot, std::string group_name) {
  if (prof_slot == ProfileSlot_Invalid) {
    return group_name;
  }

  Profile* prof = PROFILEMAN->GetProfile(prof_slot);
  // root Songs folder
  if (group_name == "") {
    return prof->GetCustomSongsGroupNamePrefix();
  }

  return prof->GetCustomSongsGroupNamePrefix() + " - " + group_name;
}

Group::Group(
    const std::string& dir, const std::string& group_dir_name,
    ProfileSlot prof_slot) {
  /* groupDirName empty for root songs on profile */
  if (dir.empty() ||
      (group_dir_name.empty() && prof_slot == ProfileSlot_Invalid)) {
    LOG->Warn("Group::Group: Empty directory or group name provided.");
    return;
  }

  std::string pack_ini_path;
  // songs from profile
  if (prof_slot != ProfileSlot_Invalid) {
    pack_ini_path = dir + "/" + kIniFile;
    path_ = dir;
    group_name_ = GetInternalGroupName(prof_slot, group_dir_name);
  } else {
    pack_ini_path = dir + group_dir_name + "/" + kIniFile;
    path_ = dir + group_dir_name;
    group_name_ = Basename(group_dir_name);
  }
  display_title_ = group_name_;
  sort_title_ = group_name_;
  translit_title_ = group_name_;
  series_ = "";
  // The m_DefaultSyncOffset preference corresponds to what offset the user
  // would like to assume as a "default" for packs without a Pack.ini.
  //
  // If the m_DefaultSyncOffset is set to ITG, we want to remove the 9ms ITG
  // bias for all packs without a Pack.ini.
  //
  // We can start with that as our default value, and potentially update it
  // below.
  sync_offset_ =
      (PREFSMAN->default_sync_offset_ == SyncOffset_NULL) ? 0.0f : -0.009f;
  has_pack_ini_ = false;
  year_released_ = 0;
  banner_path_ = "";
  series_banner_path_ = "";

  if (FILEMAN->DoesFileExist(pack_ini_path)) {
    IniFile ini;
    if (!ini.ReadFile(pack_ini_path)) {
      LOG->Warn(
          "Group::Group: Failed to read Pack.ini file at '%s': %s",
          pack_ini_path.c_str(), ini.GetError().c_str());
      return;
    }

    std::string version = "";
    ini.GetValue("Group", "Version", version);
    Trim(version);

    // Only read the Pack.ini if the version is set
    // Otherwise log a warning and use default Pack.ini values
    if (!version.empty()) {
      has_pack_ini_ = true;

      // Define a vector of key-value pairs to cleanly iterate
      std::vector<std::pair<std::string, std::string&>> packfields = {
          {"DisplayTitle", display_title_},
          {"SortTitle", sort_title_},
          {"TranslitTitle", translit_title_},
          {"Series", series_},
          {"Banner", banner_path_},
          {"SeriesBanner", series_banner_path_}};

      for (auto& [key, value] : packfields) {
        ini.GetValue("Group", key, value);
        Trim(value);

        if (value.empty()) {
          if (key == "DisplayTitle" || key == "SortTitle") {
            value = group_name_;
          } else if (key == "TranslitTitle") {
            value = display_title_;
          }
        }

        if (prof_slot != ProfileSlot_Invalid &&
            (key == "DisplayTitle" || key == "SortTitle" ||
             key == "TranslitTitle")) {
          value = GetInternalGroupName(prof_slot, value);
        }
      }

      std::string s_value = "";
      ini.GetValue("Group", "SyncOffset", s_value);
      Trim(s_value);
      // If the Pack.ini is specified as SyncOffset=ITG (regardless of the
      // m_DefaultSyncOffset value, we need to always remove the 9ms ITG
      // bias.
      //
      // Otherwise, if SyncOffset=NULL, we don't need to do anything since
      // it's already NULL synced, which is what we shift everything to.
      if (!s_value.empty()) {
        if (s_value == "NULL") {
          sync_offset_ = 0.0f;
        } else if (s_value == "ITG") {
          sync_offset_ = -0.009f;
        } else {
          LOG->Warn(
              "Group::Group: Invalid SyncOffset value: %s in Pack.ini file "
              "\"%s\". Valid values are NULL and ITG. Using default value.",
              s_value.c_str(), pack_ini_path.c_str());
        }
      }

      ini.GetValue("Group", "Year", year_released_);
    } else {
      LOG->Warn(
          "Group::Group: Pack.ini version not set in file \"%s\". Using "
          "default values.",
          pack_ini_path.c_str());
    }
  }
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
