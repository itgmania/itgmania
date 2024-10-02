#include "global.h"
#include "Song.h"
#include "Group.h"
#include "Style.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageSurface.h"
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
    iTotalSongs = 0;
    m_sBannerPath = "";
    m_sCredits.clear();
    m_sAuthorsNotes = "";
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

    static int GetTotalSongs( T* p, lua_State *L )
    {
        lua_pushnumber(L, p->iTotalSongs);
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

	LunaGroup()
	{
		ADD_METHOD( GetGroupName );
		ADD_METHOD( GetSortTitle );
		ADD_METHOD( GetDisplayTitle );
		ADD_METHOD( GetTranslitTitle );
		ADD_METHOD( GetSeries );
		ADD_METHOD( GetSyncOffset );
		ADD_METHOD( HasGroupIni );
		ADD_METHOD( GetTotalSongs );
		ADD_METHOD( GetBannerPath );
		ADD_METHOD( GetStepArtistCredits );
		ADD_METHOD( GetAuthorsNotes );
	}

};

LUA_REGISTER_CLASS( Group )


// lua end