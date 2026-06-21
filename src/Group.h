#ifndef GROUP_H
#define GROUP_H

#include "global.h"

#include "ActorFrame.h"
#include "GameplayAssist.h"
#include "Player.h"
#include "NoteData.h"

#include <iterator>
#include <map>
#include <unordered_map>
#include <vector>


struct lua_State;
class Group
{
public:
	Group();
    Group( const RString& sDir, const RString& sGroupDirName, bool bFromProfile = false);
    ~Group() = default;
    // Lua
	void PushSelf( lua_State *L );

	/**
	 * @brief This is the title of the group as its displayed to the user 
     * and supersedes the actual folder name on disk.
	 * 
	 * @return The display title of the group. 
	 */
    const RString GetDisplayTitle() const { return m_sDisplayTitle; };

    /**
     * @brief This is the value considered when sorting the group by its title.
     * 
     * @return const RString
     */
    const RString GetSortTitle() const { return m_sSortTitle; };

    /**
     * @brief The path to the group folder.
     * 
     * @return the path
     */
    const RString GetPath() const { return m_sPath; };

    /**
     * @brief The actual name of the group folder on disk.
     * 
     * @return const RString 
     */
    const RString GetGroupName() const { return m_sGroupName; };


    /**
     * @brief Allows transliteration of the group title.
     * 
     * @return const RString 
     */
    const RString GetTranslitTitle() const { return m_sTranslitTitle; };


    /**
     * @brief The series the group belongs to
     * 
     * @return const RString 
     */
    const RString GetSeries() const { return m_sSeries; };


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
     * @return const RString 
     */
    const RString GetBannerPath() const { return m_sBannerPath; };

    /**
     * @brief Get the songs in the group.
	 * @return the songs that belong in the group. */
	const std::vector<Song*> &GetSongs() const;

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
        RString m_sDisplayTitle;

        /** @brief This is the value considered when sorting the group by its title. */
        RString m_sSortTitle;

        /** @brief The path to the group folder. */
        RString m_sPath;

        /** @brief The actual name of the group folder on disk. */
        RString m_sGroupName;

        /** @brief Allows transliteration of the group title. */
        RString m_sTranslitTitle;

        /** @brief The series the group belongs to */
        RString m_sSeries;

        /** @brief The sync offset of the group */
        float m_fSyncOffset;

        /** @brief Whether the group has a Pack.ini file. */
        bool m_bHasPackIni;

        /** @brief The path to the group's banner. */
        RString m_sBannerPath;

        /** @brief The year the group was released */
        int m_iYearReleased;

        /** @brief The version of the Pack.ini info */
        int m_iVersion;

};

#endif
