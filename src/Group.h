#ifndef GROUP_H
#define GROUP_H

#include <string>
#include <vector>

#include "Attack.h"

struct lua_State;
class Group {
 public:
  Group(
      const std::string& dir, const std::string& group_dir_name,
      ProfileSlot prof_slot = ProfileSlot_Invalid);
  ~Group() = default;
  // Lua
  void PushSelf(lua_State* L);

  /**
   * @brief This is the title of the group as its displayed to the user
   * and supersedes the actual folder name on disk.
   *
   * @return The display title of the group.
   */
  const std::string GetDisplayTitle() const { return display_title_; };

  /**
   * @brief This is the value considered when sorting the group by its title.
   *
   * @return const std::string
   */
  const std::string GetSortTitle() const { return sort_title_; };

  /**
   * @brief The path to the group folder.
   *
   * @return the path
   */
  const std::string GetPath() const { return path_; };

  /**
   * @brief The actual name of the group folder on disk.
   *
   * @return const std::string
   */
  const std::string GetGroupName() const { return group_name_; };

  /**
   * @brief Allows transliteration of the group title.
   *
   * @return const std::string
   */
  const std::string GetTranslitTitle() const { return translit_title_; };

  /**
   * @brief The series the group belongs to
   *
   * @return const std::string
   */
  const std::string GetSeries() const { return series_; };

  /**
   * @brief The sync offset of this group
   *
   * @return float
   */
  float GetSyncOffset() const { return sync_offset_; };

  /**
   * @brief Whether the group has a Pack.ini file.
   *
   * @return true if the group has a Pack.ini file, false otherwise.
   */
  bool HasPackIni() const { return has_pack_ini_; };

  /**
   * @brief The path to the group's banner.
   *
   * @return const std::string
   */
  const std::string GetBannerPath() const { return banner_path_; };

  /**
   * @brief The path to the banner the group attributes to its series.
   *
   * @return const std::string
   */
  const std::string GetSeriesBannerPath() const { return series_banner_path_; };

  /**
   * @brief The year the group was released
   *
   * @return int
   */
  int GetYearReleased() const { return year_released_; };

 private:
  /**
   * @brief This is the title of the group as its displayed to the user
   * and supersedes the actual folder name on disk. */
  std::string display_title_;

  /** @brief This is the value considered when sorting the group by its title.
   */
  std::string sort_title_;

  /** @brief The path to the group folder. */
  std::string path_;

  /** @brief The actual name of the group folder on disk. */
  std::string group_name_;

  /** @brief Allows transliteration of the group title. */
  std::string translit_title_;

  /** @brief The series the group belongs to */
  std::string series_;

  /** @brief The sync offset of the group */
  float sync_offset_;

  /** @brief Whether the group has a Pack.ini file. */
  bool has_pack_ini_;

  /** @brief The path to the group's banner. */
  std::string banner_path_;

  /** @brief The year the group was released */
  int year_released_;

  /** @brief The path to the group's series banner. */
  std::string series_banner_path_;
};

#endif
