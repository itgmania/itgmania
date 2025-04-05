#include "global.h"
#include "ArchHooks.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "archutils/Win32/SpecialDirs.h"
#include "ProductInfo.h"
#include "RageFileManager.h"

#include <cstdint>
#include <vector>

// for QueryPerformanceCounter
#include <windows.h>
#include <mmsystem.h>
#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif

static bool g_bTimerInitialized;

/* QueryPerformanceCounter variables below.
 * QueryPerformanceCounter and QueryPerformanceFrequency expect
 * a LARGE_INTEGER, which is a union. These functions store data
 * in the QuadPart of the LARGE_INTEGER, which is a 64-bit integer. */
namespace {
    LARGE_INTEGER g_liFrequency;
    LARGE_INTEGER g_liCurrentTime;
}  // namespace

static void InitTimer()
{
	if( g_bTimerInitialized ) {
		return;
	}
	
	g_bTimerInitialized = true;

	// Retrieve the number of ticks per second.
	QueryPerformanceFrequency(&g_liFrequency);
}

int64_t ArchHooks::GetSystemTimeInMicroseconds()
{
	// Make sure the timer is initialized.
	if (!g_bTimerInitialized) {
		InitTimer();
	}

	// Get the current time.
	QueryPerformanceCounter(&g_liCurrentTime);

	// Calculate the elapsed time in microseconds.
	return (g_liCurrentTime.QuadPart * 1000000) / g_liFrequency.QuadPart;
}

static RString GetMountDir( const RString &sDirOfExecutable )
{
	/* All Windows data goes in the directory one level above the executable. */
	CHECKPOINT_M( ssprintf( "DOE \"%s\"", sDirOfExecutable.c_str()) );
	std::vector<RString> asParts;
	split( sDirOfExecutable, "/", asParts );
	CHECKPOINT_M( ssprintf( "... %i asParts", asParts.size()) );
	ASSERT_M( asParts.size() > 1, ssprintf("Strange sDirOfExecutable: %s", sDirOfExecutable.c_str()) );
	RString sDir = join( "/", asParts.begin(), asParts.end()-1 );
	return sDir;
}

void MountDirectories(const RString& baseDir) {
	const std::vector<RString> winDirectoryStructureITGm = {
		"/Announcers",
		"/BGAnimations",
		"/BackgroundEffects",
		"/BackgroundTransitions",
		"/Cache",
		"/CDTitles",
		"/Characters",
		"/Courses",
		"/Downloads",
		"/Logs",
		"/NoteSkins",
		"/Packages",
		"/Save",
		"/Screenshots",
		"/Songs",
		"/RandomMovies",
		"/Themes"
	};

	for (const RString& dir : winDirectoryStructureITGm) {
		FILEMAN->Mount("dir", baseDir + dir, dir);
	}
}

void ArchHooks::MountInitialFilesystems(const RString& sDirOfExecutable) {
	RString sDir = GetMountDir(sDirOfExecutable);
	FILEMAN->Mount("dirro", sDir, "/");

	if (DoesFileExist("/Portable.ini")) {
		MountDirectories(sDir);
	}
}

void ArchHooks::MountUserFilesystems(const RString& sDirOfExecutable) {
	RString sAppDataDir = SpecialDirs::GetAppDataDir() + PRODUCT_ID;
	MountDirectories(sAppDataDir);
}

static RString LangIdToString( LANGID l )
{
	switch( PRIMARYLANGID(l) )
	{
	case LANG_ARABIC: return "ar";
	case LANG_BULGARIAN: return "bg";
	case LANG_CATALAN: return "ca";
	case LANG_CHINESE:
	{
		switch (SUBLANGID(l))
		{
		case SUBLANG_CHINESE_TRADITIONAL:
		case SUBLANG_CHINESE_HONGKONG:
		case SUBLANG_CHINESE_MACAU:
			return "zh-Hant";
		case SUBLANG_CHINESE_SIMPLIFIED:
		case SUBLANG_CHINESE_SINGAPORE:
			return "zh-Hans";
		}
	}
	case LANG_CZECH: return "cs";
	case LANG_DANISH: return "da";
	case LANG_GERMAN: return "de";
	case LANG_GREEK: return "el";
	case LANG_SPANISH: return "es";
	case LANG_FINNISH: return "fi";
	case LANG_FRENCH: return "fr";
	case LANG_HEBREW: return "iw";
	case LANG_HUNGARIAN: return "hu";
	case LANG_ICELANDIC: return "is";
	case LANG_ITALIAN: return "it";
	case LANG_JAPANESE: return "ja";
	case LANG_KOREAN: return "ko";
	case LANG_DUTCH: return "nl";
	case LANG_NORWEGIAN: return "no";
	case LANG_POLISH: return "pl";
	case LANG_PORTUGUESE: return "pt";
	case LANG_ROMANIAN: return "ro";
	case LANG_RUSSIAN: return "ru";
	case LANG_CROATIAN: return "hr";
	// case LANG_SERBIAN: return "sr"; // same as LANG_CROATIAN?
	case LANG_SLOVAK: return "sk";
	case LANG_ALBANIAN: return "sq";
	case LANG_SWEDISH: return "sv";
	case LANG_THAI: return "th";
	case LANG_TURKISH: return "tr";
	case LANG_URDU: return "ur";
	case LANG_INDONESIAN: return "in";
	case LANG_UKRAINIAN: return "uk";
	case LANG_SLOVENIAN: return "sl";
	case LANG_ESTONIAN: return "et";
	case LANG_LATVIAN: return "lv";
	case LANG_LITHUANIAN: return "lt";
	case LANG_VIETNAMESE: return "vi";
	case LANG_ARMENIAN: return "hy";
	case LANG_BASQUE: return "eu";
	case LANG_MACEDONIAN: return "mk";
	case LANG_AFRIKAANS: return "af";
	case LANG_GEORGIAN: return "ka";
	case LANG_FAEROESE: return "fo";
	case LANG_HINDI: return "hi";
	case LANG_MALAY: return "ms";
	case LANG_KAZAK: return "kk";
	case LANG_SWAHILI: return "sw";
	case LANG_UZBEK: return "uz";
	case LANG_TATAR: return "tt";
	case LANG_PUNJABI: return "pa";
	case LANG_GUJARATI: return "gu";
	case LANG_TAMIL: return "ta";
	case LANG_KANNADA: return "kn";
	case LANG_MARATHI: return "mr";
	case LANG_SANSKRIT: return "sa";
	// These aren't present in the VC6 headers. We'll never have translations to these languages anyway. -C
	//case LANG_MONGOLIAN: return "mn";
	//case LANG_GALICIAN: return "gl";
	default: LOG->Warn("Unable to determine system language. Using English.");
	case LANG_ENGLISH: return "en";
	}
}

static LANGID GetLanguageID()
{
	HINSTANCE hDLL = LoadLibrary( "kernel32.dll" );
	if( hDLL )
	{
		typedef LANGID(GET_USER_DEFAULT_UI_LANGUAGE)(void);

		GET_USER_DEFAULT_UI_LANGUAGE *pGetUserDefaultUILanguage = (GET_USER_DEFAULT_UI_LANGUAGE*) GetProcAddress( hDLL, "GetUserDefaultUILanguage" );
		if( pGetUserDefaultUILanguage )
		{
			LANGID ret = pGetUserDefaultUILanguage();
			FreeLibrary( hDLL );
			return ret;
		}
		FreeLibrary( hDLL );
	}

	return GetUserDefaultLangID();
}

RString ArchHooks::GetPreferredLanguage()
{
	return LangIdToString( GetLanguageID() );
}

/*
 * (c) 2003-2004 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
