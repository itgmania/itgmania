#ifndef GROUP_H
#define GROUP_H

#include <string>
#include <vector>

#include "Attack.h"

struct lua_State;
class Group {
 public:
  Group();
  Group(
      const std::string& sDir, const std::string& sGroupDirName,
      bool bFromProfile = false);
  ~Group() = default;
  // Lua
  void PushSelf(lua_State* L);

  /**
   * @brief This is the title of the group as its displayed to the user
   * and supersedes the actual folder name on disk.
   *
   * @return The display title of the group.
   */
  const std::string GetDisplayTitle() const { return m_sDisplayTitle; };

  /**
   * @brief This is the value considered when sorting the group by its title.
   *
   * @return const std::string
   */
  const std::string GetSortTitle() const { return m_sSortTitle; };

  /**
   * @brief The path to the group folder.
   *
   * @return the path
   */
  const std::string GetPath() const { return m_sPath; };

  /**
   * @brief The actual name of the group folder on disk.
   *
   * @return const std::string
   */
  const std::string GetGroupName() const { return m_sGroupName; };

  /**
   * @brief Allows transliteration of the group title.
   *
   * @return const std::string
   */
  const std::string GetTranslitTitle() const { return m_sTranslitTitle; };

  /**
   * @brief The series the group belongs to
   *
   * @return const std::string
   */
  const std::string GetSeries() const { return m_sSeries; };

  /**
   * @brief The sync offset of this group
   *
   * @return float
   */
  float GetSyncOffset() const { return m_fSyncOffset; };

  /**
   * @brief Whether the group has a Pack.ini file.
   *
   * @return true if the group has a Pack.ini file, false otherwise.
   */
  bool HasPackIni() const { return m_bHasPackIni; };

  /**
   * @brief The path to the group's banner.
   *
   * @return const std::string
   */
  const std::string GetBannerPath() const { return m_sBannerPath; };

  /**
   * @brief The path to the banner the group attributes to its series.
   *
   * @return const std::string
   */
  const std::string GetSeriesBannerPath() const {
    return m_sSeriesBannerPath;
  };

  /**
   * @brief Get the songs in the group.
   * @return the songs that belong in the group. */
  const std::vector<Song*>& GetSongs() const;

  /**
   * @brief The year the group was released
   *
   * @return int
   */
  int GetYearReleased() const { return m_iYearReleased; };

  /**
   * @brief The version of the Pack.ini info
   *
   */
  int GetVersion() const { return m_iVersion; };

 private:
  /**
   * @brief This is the title of the group as its displayed to the user
   * and supersedes the actual folder name on disk. */
  std::string m_sDisplayTitle;

  /** @brief This is the value considered when sorting the group by its title.
   */
  std::string m_sSortTitle;

  /** @brief The path to the group folder. */
  std::string m_sPath;

  /** @brief The actual name of the group folder on disk. */
  std::string m_sGroupName;

  /** @brief Allows transliteration of the group title. */
  std::string m_sTranslitTitle;

  /** @brief The series the group belongs to */
  std::string m_sSeries;

  /** @brief The sync offset of the group */
  float m_fSyncOffset;

  /** @brief Whether the group has a Pack.ini file. */
  bool m_bHasPackIni;

  /** @brief The path to the group's banner. */
  std::string m_sBannerPath;

  /** @brief The year the group was released */
  int m_iYearReleased;

  /** @brief The version of the Pack.ini info */
  int m_iVersion;

  /** @brief The path to the group's series banner. */
  std::string m_sSeriesBannerPath;
};

#endif
