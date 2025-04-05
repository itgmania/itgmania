#include "global.h"
#include "GetFileInformation.h"

#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"

#include <cstdint>
#include <sys/stat.h>
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>

#pragma comment(lib, "version.lib")

bool GetFileVersion(const RString &sFile, RString &sOut)
{
	do {
		DWORD ignore;
		DWORD iSize = GetFileVersionInfoSize(sFile.c_str(), &ignore);
		if( !iSize )
			break;

		std::vector<char> VersionBuffer(iSize);
		if (!GetFileVersionInfo(sFile.c_str(), 0, iSize, VersionBuffer.data()))
			break;

		WORD *iTrans;
		UINT iTransCnt;

		if( !VerQueryValue(VersionBuffer.data(), "\\VarFileInfo\\Translation", (void**)&iTrans, &iTransCnt) )
			break;

		if( iTransCnt == 0 )
			break;

		char *str;
		UINT len;

		RString sRes = ssprintf("\\StringFileInfo\\%04x%04x\\FileVersion", iTrans[0], iTrans[1]);
		if (!VerQueryValue(VersionBuffer.data(), sRes.c_str(), (void**)&str, &len) || len < 1)
			break;

		sOut = RString( str, len-1 );
	} while(0);

	// Get the size and date.
	struct stat st;
	if (stat(sFile.c_str(), &st) != -1)
	{
		struct tm t;
		gmtime_r( &st.st_mtime, &t );
		if( !sOut.empty() )
			sOut += " ";
		sOut += ssprintf( "[%ib, %02i-%02i-%04i]", st.st_size, t.tm_mon+1, t.tm_mday, t.tm_year+1900 );
	}

	return true;
}

RString FindSystemFile(const RString& sFile)
{
	char szWindowsPath[MAX_PATH];
	GetWindowsDirectory( szWindowsPath, MAX_PATH );

	const char *szPaths[] =
	{
		"\\system32\\",
		"\\system32\\drivers\\",
		"\\system\\",
		"\\system\\drivers\\",
		"\\",
		nullptr
	};

	for( int i = 0; szPaths[i]; ++i )
	{
		RString sPath = ssprintf( "%s%s%s", szWindowsPath, szPaths[i], sFile.c_str() );
		struct stat buf;
		if (!stat(sPath.c_str(), &buf))
			return sPath;
	}

	return RString();
}

/* Get the full path of the process running in iProcessID. On error, false is
 * returned and an error message is placed in sName. */
bool GetProcessFileName( uint32_t iProcessID, RString &sName )
{
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, iProcessID);
	if (hSnap == INVALID_HANDLE_VALUE)
	{
		sName = werr_ssprintf(GetLastError(), "CreateToolhelp32Snapshot");
		return false;
	}

	MODULEENTRY32 me;
	me.dwSize = sizeof(MODULEENTRY32);
	if (Module32First(hSnap, &me))
	{
		sName = me.szExePath;
		CloseHandle(hSnap);
		return true;
	}

	CloseHandle(hSnap);
	sName = werr_ssprintf(GetLastError(), "Module32First");
	return false;
}

/*
 * (c) 2004 Glenn Maynard
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
