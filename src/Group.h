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
    Group( const RString sDir, const RString &sGroupDirName);
    ~Group();
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
     * @brief Defines the offset applied to all songs within the group.
     * 
     * @return float 
     */
    float GetSyncOffset() const { return m_iSyncOffset; };

    /**
     * @brief Whether the group has a group.ini file.
     * 
     * @return true if the group has a group.ini file, false otherwise.
     */
    bool HasGroupIni() const { return m_bHasGroupIni; };

    
    /**
     * @brief The path to the group's banner.
     * 
     * @return const RString 
     */
    const RString GetBannerPath() const { return m_sBannerPath; };

    /**
     * @brief The persons who worked with the group who should be credited.
     * 
     * @return const std::vector<RString> 
     */
    const std::vector<RString> GetStepArtistCredits() const { return m_sCredits; };

    /**
     * @brief Additional notes about the group.
     * 
     * @return const RString 
     */
    const RString GetAuthorsNotes() const { return m_sAuthorsNotes; };

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

        /** @brief Defines the offset applied to all songs within the group. */
        float m_iSyncOffset;

        /** @brief Whether the group has a group.ini file. */
        bool m_bHasGroupIni;

        /** @brief The path to the group's banner. */
        RString m_sBannerPath;

        /** @brief The persons who worked with the group who should be credited. */
        std::vector<RString> m_sCredits;

        /** @brief Additional notes about the group. */
        RString m_sAuthorsNotes;

        /** @brief The year the group was released */
        int m_iYearReleased;

};

#endif