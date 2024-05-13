/*
http://en.wikipedia.org/wiki/INI_file
 - names and values are trimmed on both sides
 - semicolons start a comment line
 - backslash followed by a newline doesn't break the line
*/
#include "global.h"
#include "IniFile.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageFile.h"

#include <cstddef>


IniFile::IniFile(): XNode("IniFile")
{
}

bool IniFile::ReadFile( const RString &sPath )
{
	m_sPath = sPath;
	CHECKPOINT_M( ssprintf("Reading '%s'",m_sPath.c_str()) );

	RageFile f;
	if( !f.Open( m_sPath ) )
	{
		LOG->Trace( "Reading '%s' failed: %s", m_sPath.c_str(), f.GetError().c_str() );
		m_sError = f.GetError();
		return 0;
	}

	return ReadFile( f );
}

bool IniFile::ReadFile(RageFileBasic& f)
{
	RString keyname;
	XNode* keychild = nullptr;

	while (true)
	{
		RString line;
		while (true)
		{
			RString s;
			int result = f.GetLine(s);
			if (result == -1)
			{
				m_sError = f.GetError();
				return false;
			}
			else if (result == 0)
			{
				return true;
			}

			utf8_remove_bom(s);

			line += s;

			if (line.empty() || line.back() != '\\')
			{
				break;
			}
			line.pop_back();
		}

		if (line.empty())
			continue;

		if (line[0] == ';' || line[0] == '#' || (line.size() > 1 && line[0] == line[1] && (line[0] == '/' || line[0] == '-')))
			continue;

		if (line[0] == '[' && line.back() == ']')
		{
			keyname = line.substr(1, line.size() - 2);
			keychild = GetChild(keyname);
			if (keychild == nullptr)
			{
				keychild = AppendChild(keyname);
			}
			continue;
		}

		if (keychild != nullptr)
		{
			std::size_t iEqualIndex = line.find("=");
			if (iEqualIndex != std::string::npos)
			{
				RString valuename = line.substr(0, iEqualIndex);
				RString value = line.substr(iEqualIndex + 1);
				Trim(valuename);
				if (!valuename.empty())
				{
					SetKeyValue(keychild, valuename, value);
				}
			}
			else
			{
				LOG->Warn("Encountered line without '=' in INI file: %s", line.c_str());
			}
		}
	}
}

bool IniFile::WriteFile( const RString &sPath ) const
{
	RageFile f;
	if( !f.Open( sPath, RageFile::WRITE ) )
	{
		LOG->Warn( "Writing '%s' failed: %s", sPath.c_str(), f.GetError().c_str() );
		m_sError = f.GetError();
		return false;
	}

	bool bSuccess = IniFile::WriteFile( f );
	int iFlush = f.Flush();
	bSuccess &= (iFlush != -1);
	return bSuccess;
}

bool IniFile::WriteFile( RageFileBasic &f ) const
{
	FOREACH_CONST_Child( this, pKey )
	{
		if( f.PutLine( ssprintf("[%s]", pKey->GetName().c_str()) ) == -1 )
		{
			m_sError = f.GetError();
			return false;
		}

		FOREACH_CONST_Attr( pKey, pAttr )
		{
			const RString &sName = pAttr->first;
			const RString &sValue = pAttr->second->GetValue<RString>();

			// Ensure the name does not contain newline or equals sign characters.
			// These characters are not valid INI file structure.
			DEBUG_ASSERT( sName.find('\n') == sName.npos );
			DEBUG_ASSERT( sName.find('=') == sName.npos );

			if( f.PutLine( ssprintf("%s=%s", sName.c_str(), sValue.c_str()) ) == -1 )
			{
				LOG->Warn( "IniFile: failed to write the attribute to the file!" );
				m_sError = f.GetError();
				return false;
			}
		}

		if( f.PutLine( "" ) == -1 )
		{
			LOG->Warn( "IniFile: failed to write an empty line to the file!" );
			m_sError = f.GetError();
			return false;
		}
	}
	return true;
}

bool IniFile::DeleteValue(const RString &keyname, const RString &valuename)
{
	XNode* pNode = GetChild( keyname );
	if( pNode == nullptr )
		return false;
	return pNode->RemoveAttr( valuename );
}


bool IniFile::DeleteKey(const RString &keyname)
{
	XNode* pNode = GetChild( keyname );
	if( pNode == nullptr )
		return false;
	return RemoveChild( pNode );
}

bool IniFile::RenameKey(const RString &from, const RString &to)
{
	// If to already exists, do nothing.
	if( GetChild(to) != nullptr )
		return false;

	XNode* pNode = GetChild( from );
	if( pNode == nullptr )
		return false;

	pNode->SetName( to );
	RenameChildInByName(pNode);

	return true;
}


/*
 * (c) 2001-2004 Adam Clauss, Chris Danford
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
