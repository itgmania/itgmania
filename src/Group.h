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
    // Lua
	void PushSelf( lua_State *L );
    RString m_sDisplayTitle;
    RString m_sSortTitle;
    RString m_sPath;
    RString m_sGroupName;
    RString m_sTranslitTitle;
    RString m_sSeries;
    float m_iSyncOffset;
    bool m_bHasGroupIni;
    RString m_sBannerPath;
    std::vector<RString> m_sCredits;
    RString m_sAuthorsNotes;
	// Get the display title of the group.
    const RString GetDisplayTitle() const { return m_sDisplayTitle; };

    // Set the display title of the group.
    void SetDisplayTitle(const RString sDisplayTitle) { m_sDisplayTitle = sDisplayTitle; };

    // Get the sort title of the group.
    const RString GetSortTitle() const { return m_sSortTitle; };

    // Set the sort title of the group.
    void SetSortTitle(const RString sSortTitle) { m_sSortTitle = sSortTitle; };

    // Get the path of the group.
    const RString GetPath() const { return m_sPath; };

    // Set the path of the group.
    void SetPath(const RString sPath) { m_sPath = sPath; };

    // Get the group name.
    const RString GetGroupName() const { return m_sGroupName; };

    // Set the group name.
    void SetGroupName(const RString sGroupName) { m_sGroupName = sGroupName; };

    // Get the transliterated title of the group.
    const RString GetTranslitTitle() const { return m_sTranslitTitle; };

    // Set the transliterated title of the group.
    void SetTranslitTitle(const RString sTranslitTitle) { m_sTranslitTitle = sTranslitTitle; };

    // Get the series of the group.
    const RString GetSeries() const { return m_sSeries; };

    // Set the series of the group.
    void SetSeries(const RString sSeries) { m_sSeries = sSeries; };

    // Get the sync offset of the group.
    float GetSyncOffset() const { return m_iSyncOffset; };

    // Set the sync offset of the group.
    void SetSyncOffset(const float fSyncOffset) { m_iSyncOffset = fSyncOffset; };

    // Determine if the group has a Group.ini file.

    bool HasGroupIni() const { return m_bHasGroupIni; };

    // Set if the group has a Group.ini file.
    void SetGroupIni(const bool bHasGroupIni) { m_bHasGroupIni = bHasGroupIni; };
    
    // Get the banner path of the group.
    const RString GetBannerPath() const { return m_sBannerPath; };

    // Set the banner path of the group.
    void SetBannerPath(const RString sBannerPath) { m_sBannerPath = sBannerPath; };
    
    // Get the credits of the group.
    const std::vector<RString> GetStepArtistCredits() const { return m_sCredits; };

    // Set the credits of the group.
    void SetStepArtistCredits(const std::vector<RString> sCredits) { m_sCredits = sCredits; };

    // Get the authors notes of the group.
    const RString GetAuthorsNotes() const { return m_sAuthorsNotes; };

    // Set the authors notes of the group.
    void SetAuthorsNotes(const RString sAuthorsNotes) { m_sAuthorsNotes = sAuthorsNotes; };

    int iTotalSongs;

};

#endif