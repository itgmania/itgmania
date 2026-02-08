#ifndef SMPackageUtil_H
#define SMPackageUtil_H

#include <vector>


struct LanguageInfo;

namespace SMPackageUtil
{
	void WriteGameInstallDirs( const std::vector<std::string>& asInstallDirsToWrite );
	void GetGameInstallDirs( std::vector<std::string>& asInstallDirsOut );
	void AddGameInstallDir( const std::string &sNewInstallDir );
	void SetDefaultInstallDir( int iInstallDirIndex );
	void SetDefaultInstallDir( const std::string &sInstallDir );
	bool IsValidInstallDir( const std::string &sInstallDir );

	bool GetPref( const std::string &name, bool &val );
	bool SetPref( const std::string &name, bool val );

	std::string GetPackageDirectory( const std::string &path );
	bool IsValidPackageDirectory( const std::string &path );

	bool LaunchGame();

	std::string GetLanguageDisplayString( const std::string &sIsoCode );
	std::string GetLanguageCodeFromDisplayString( const std::string &sDisplayString );

	void StripIgnoredSmzipFiles( std::vector<std::string> &vsFilesInOut );

	bool GetFileContentsOsAbsolute( const std::string &sAbsoluteOsFile, std::string &sOut );

	bool DoesOsAbsoluteFileExist( const std::string &sOsAbsoluteFile );
}

#include "RageFile.h"

class RageFileOsAbsolute : public RageFile
{
public:
	bool Open( const std::string& path, int mode = READ );
	~RageFileOsAbsolute();
private:
	std::string m_sOsDir;
};

#endif

/*
 * (c) 2002-2005 Chris Danford
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
