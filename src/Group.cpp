#include "global.h"
#include "SongManager.h"
#include "RageFile.h"
#include "RageUtil.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "Song.h"
#include "SongCacheIndex.h"
#include "SongUtil.h"
#include "TitleSubstitution.h"
#include "Group.h"

#include <cstddef>
#include <tuple>
#include <vector>

Group::Group() {
    m_sDisplayTitle = "";
    m_sSortTitle = "";
    m_sPath = "";
    m_sGroupName = "";
    m_sTranslitTitle = "";
    m_sSeries = "";
    m_iSyncOffset = 0;
    m_bHasGroupIni = false;
    m_iYearReleased = 0;
    m_sBannerPath = "";
    m_sCredits.clear();
    m_sAuthorsNotes = "";
}

Group::Group(const RString &sPath) {
    RString sGroupIniPath = sPath + "/Group.ini";
    RString credits = "";
    if (FILEMAN->DoesFileExist(sGroupIniPath)) {
        IniFile ini;
        ini.ReadFile(sGroupIniPath);
        ini.GetValue("Group", "DisplayTitle", m_sDisplayTitle);
        if (m_sDisplayTitle.empty()) {
            m_sDisplayTitle = m_sGroupName;
        }
        ini.GetValue("Group", "Banner", m_sBannerPath);
        ini.GetValue("Group", "SortTitle", m_sSortTitle);
        if (m_sSortTitle.empty()) {
            m_sSortTitle = m_sDisplayTitle;
        }
        ini.GetValue("Group", "TranslitTitle", m_sTranslitTitle);
        if (m_sTranslitTitle.empty()) {
            m_sTranslitTitle = m_sDisplayTitle;
        }
        ini.GetValue("Group", "Series", m_sSeries);
        RString sValue = "";
        ini.GetValue("Group", "SyncOffset", sValue);
        if (sValue.CompareNoCase("null") == 0) {
            m_iSyncOffset = 0;
        } else if (sValue.CompareNoCase("itg") == 0) {
            m_iSyncOffset = 0.009f;
        } else {
            m_iSyncOffset = StringToFloat(sValue);
        }
        ini.GetValue("Group", "Year", m_iYearReleased);
        ini.GetValue("Group", "AuthorsNotes", m_sAuthorsNotes);

        std::vector<RString> credits_vector;
        ini.GetValue("Group", "Credits", credits);
        split(credits, ";", credits_vector);
        m_sCredits = credits_vector;
        m_bHasGroupIni = true;
    } else {
        m_bHasGroupIni = false;
    }
    
}

const std::vector<Song *> &Group::GetSongs() const
{
    return SONGMAN->GetSongs(m_sGroupName);
}



#include "LuaBinding.h"

class LunaGroup: public Luna<Group>
{
public:
	static int GetGroupName( T* p, lua_State *L )
	{
		lua_pushstring(L, p->GetGroupName());
		return 1;
	}
	static int GetSortTitle( T* p, lua_State *L )
	{
		lua_pushstring(L, p->GetSortTitle());
        return 1;
    }

    static int GetDisplayTitle( T* p, lua_State *L )
    {
        lua_pushstring(L, p->GetDisplayTitle());
        return 1;
    }

    static int GetTranslitTitle( T* p, lua_State *L )
    {
        lua_pushstring(L, p->GetTranslitTitle());
        return 1;
    }

    static int GetSeries( T* p, lua_State *L )
    {
        lua_pushstring(L, p->GetSeries());
        return 1;
    }

    static int GetSyncOffset( T* p, lua_State *L )
    {
        lua_pushnumber(L, p->GetSyncOffset());
        return 1;
    }

    static int HasGroupIni( T* p, lua_State *L )
    {
        lua_pushboolean(L, p->HasGroupIni());
        return 1;
    }

    static int GetSongs( T* p, lua_State *L )
    {
        const std::vector<Song*> &v = p->GetSongs();
        LuaHelpers::CreateTableFromArray<Song*>( v, L );
        return 1;
    }

    static int GetBannerPath( T* p, lua_State *L )
    {
        lua_pushstring(L, p->GetBannerPath());
        return 1;
    }

    static int GetStepArtistCredits( T* p, lua_State *L )
    {
        std::vector<RString> v = p->GetStepArtistCredits();
        LuaHelpers::CreateTableFromArray<RString>( v, L );
        return 1;
    }

    static int GetAuthorsNotes( T* p, lua_State *L )
    {
        lua_pushstring(L, p->GetAuthorsNotes());
        return 1;
    }

    static int GetYearReleased( T* p, lua_State *L )
    {
        lua_pushnumber(L, p->GetYearReleased());
        return 1;
    }

	LunaGroup()
	{
		ADD_METHOD( GetGroupName );
		ADD_METHOD( GetSortTitle );
		ADD_METHOD( GetDisplayTitle );
		ADD_METHOD( GetTranslitTitle );
		ADD_METHOD( GetSeries );
		ADD_METHOD( GetSyncOffset );
		ADD_METHOD( HasGroupIni );
		ADD_METHOD( GetSongs );
		ADD_METHOD( GetBannerPath );
		ADD_METHOD( GetStepArtistCredits );
		ADD_METHOD( GetAuthorsNotes );
        ADD_METHOD( GetYearReleased );
	}

};

LUA_REGISTER_CLASS( Group )


// lua end