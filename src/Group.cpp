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

Group::~Group() {
    SONGMAN->GetGroupGroupMap().clear();
    m_sCredits.clear();
}

Group::Group(const RString sDir, const RString &sGroupDirName) {
    RString sGroupIniPath = sDir + sGroupDirName + "/Group.ini";
    RString credits = "";
    RString sDisplayTitle = sGroupDirName;
    RString SortTitle = sGroupDirName;
    RString TranslitTitle = "";
    RString Series = "";
    RString bannerPath = "";
    RString authorsNotes = "";
    float fOffset = 0;

    if (FILEMAN->DoesFileExist(sGroupIniPath)) {
        IniFile ini;
        ini.ReadFile( sGroupIniPath );
        ini.GetValue( "Group", "DisplayTitle", sDisplayTitle );
        if (sDisplayTitle.empty()) {
            sDisplayTitle = Basename(sGroupDirName);
        }
        ini.GetValue( "Group", "Banner", bannerPath );
        ini.GetValue( "Group", "SortTitle", SortTitle );
        if (SortTitle.empty()) {
            SortTitle = sDisplayTitle;
        }
        ini.GetValue( "Group", "TranslitTitle", TranslitTitle );
        if (TranslitTitle.empty()) {
            TranslitTitle = sDisplayTitle;
        }
        ini.GetValue( "Group", "Series", Series );
        RString sValue = "";
        
        ini.GetValue("Group", "SyncOffset", sValue);
        if (sValue.CompareNoCase("null") == 0) {
            fOffset = 0;
        } else if (sValue.CompareNoCase("itg") == 0) {
           fOffset = 0.009f;
        } else {
            fOffset = StringToFloat(sValue);
        }
        ini.GetValue("Group", "Year", m_iYearReleased);
        ini.GetValue( "Group", "Credits", credits );
        ini.GetValue( "Group", "AuthorsNotes", authorsNotes );
    } else {
        m_bHasGroupIni = false;
    }
    
	// Look for a group banner in this group folder
	std::vector<RString> arrayGroupBanners;
	
	// First check if there is a banner provided in group.ini
	if( bannerPath != "" )
	{
		GetDirListing( sDir+sGroupDirName+"/"+bannerPath, arrayGroupBanners );
	}
	GetDirListing( sDir+sGroupDirName+"/*.png", arrayGroupBanners );
	GetDirListing( sDir+sGroupDirName+"/*.jpg", arrayGroupBanners );
	GetDirListing( sDir+sGroupDirName+"/*.jpeg", arrayGroupBanners );
	GetDirListing( sDir+sGroupDirName+"/*.gif", arrayGroupBanners );
	GetDirListing( sDir+sGroupDirName+"/*.bmp", arrayGroupBanners );

	if( !arrayGroupBanners.empty() ) {
		m_sBannerPath = sDir+sGroupDirName+"/"+arrayGroupBanners[0] ;
    }
	else
	{
		// Look for a group banner in the parent folder
		GetDirListing( sDir+sGroupDirName+".png", arrayGroupBanners );
		GetDirListing( sDir+sGroupDirName+".jpg", arrayGroupBanners );
		GetDirListing( sDir+sGroupDirName+".jpeg", arrayGroupBanners );
		GetDirListing( sDir+sGroupDirName+".gif", arrayGroupBanners );
		GetDirListing( sDir+sGroupDirName+".bmp", arrayGroupBanners );
		if( !arrayGroupBanners.empty() )
			m_sBannerPath = sDir+arrayGroupBanners[0];
	}

        /* Other group graphics are a bit trickier, and usually don't exist.
        * A themer has a few options, namely checking the aspect ratio and
        * operating on it. -aj
        * TODO: Once the files are implemented in Song, bring the extensions
        * from there into here. -aj */
        // Group background

        //vector<RString> arrayGroupBackgrounds;
        //GetDirListing( sDir+sGroupDirName+"/*-bg.png", arrayGroupBanners );
        //GetDirListing( sDir+sGroupDirName+"/*-bg.jpg", arrayGroupBanners );
        //GetDirListing( sDir+sGroupDirName+"/*-bg.jpeg", arrayGroupBanners );
        //GetDirListing( sDir+sGroupDirName+"/*-bg.gif", arrayGroupBanners );
        //GetDirListing( sDir+sGroupDirName+"/*-bg.bmp", arrayGroupBanners );
    /*
        RString sBackgroundPath;
        if( !arrayGroupBackgrounds.empty() )
            sBackgroundPath = sDir+sGroupDirName+"/"+arrayGroupBackgrounds[0];
        else
        {
            // Look for a group background in the parent folder
            GetDirListing( sDir+sGroupDirName+"-bg.png", arrayGroupBackgrounds );
            GetDirListing( sDir+sGroupDirName+"-bg.jpg", arrayGroupBackgrounds );
            GetDirListing( sDir+sGroupDirName+"-bg.jpeg", arrayGroupBackgrounds );
            GetDirListing( sDir+sGroupDirName+"-bg.gif", arrayGroupBackgrounds );
            GetDirListing( sDir+sGroupDirName+"-bg.bmp", arrayGroupBackgrounds );
            if( !arrayGroupBackgrounds.empty() )
                sBackgroundPath = sDir+arrayGroupBackgrounds[0];
        }
    */

    m_sDisplayTitle = sDisplayTitle;
    m_sSortTitle = SortTitle;
    m_sTranslitTitle = TranslitTitle;
    m_sSeries = Series;
    m_sPath = sDir + sGroupDirName;
    m_sGroupName = sGroupDirName;
    std::vector<RString> credits_vector;
    split(credits, ";", credits_vector);
    m_sCredits = credits_vector;
    m_sAuthorsNotes = authorsNotes;    
    m_iSyncOffset = fOffset;
    
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