#include "Regex.h"

#include <pcre.h>

#include <string>
#include <vector>

#include "RageException.h"
#include "RageUtil.h"
#include "StdString.h"
#include "global.h"

void Regex::Compile()
{
	const char *error;
	int offset;
	m_pReg = pcre_compile( m_sPattern.c_str(), PCRE_CASELESS, &error, &offset, nullptr );

	if( m_pReg == nullptr )
		RageException::Throw( "Invalid regex: \"%s\" (%s).", m_sPattern.c_str(), error );

	int iRet = pcre_fullinfo( (pcre *) m_pReg, nullptr, PCRE_INFO_CAPTURECOUNT, &m_iBackrefs );
	ASSERT( iRet >= 0 );

	++m_iBackrefs;
	ASSERT( m_iBackrefs < 128 );
}

void Regex::Set( const std::string &sStr )
{
	Release();
	m_sPattern = sStr;
	Compile();
}

void Regex::Release()
{
	pcre_free( m_pReg );
	m_pReg = nullptr;
	m_sPattern = std::string();
}

Regex::Regex( const std::string &sStr ): m_pReg(nullptr), m_iBackrefs(0), m_sPattern(std::string())
{
	Set( sStr );
}

Regex::Regex( const Regex &rhs ): m_pReg(nullptr), m_iBackrefs(0), m_sPattern(std::string())
{
	Set( rhs.m_sPattern );
}

Regex &Regex::operator=( const Regex &rhs )
{
	if( this != &rhs )
		Set( rhs.m_sPattern );
	return *this;
}

Regex::~Regex()
{
	Release();
}

bool Regex::Compare( const std::string &sStr )
{
	int iMat[128*3];
	int iRet = pcre_exec( (pcre *) m_pReg, nullptr, sStr.data(), sStr.size(), 0, 0, iMat, 128*3 );

	if( iRet < -1 )
		RageException::Throw( "Unexpected return from pcre_exec('%s'): %i.", m_sPattern.c_str(), iRet );

	return iRet >= 0;
}

bool Regex::Compare( const std::string &sStr, std::vector<std::string> &asMatches )
{
	asMatches.clear();

	int iMat[128*3];
	int iRet = pcre_exec( (pcre *) m_pReg, nullptr, sStr.data(), sStr.size(), 0, 0, iMat, 128*3 );

	if( iRet < -1 )
		RageException::Throw( "Unexpected return from pcre_exec('%s'): %i.", m_sPattern.c_str(), iRet );

	if( iRet == -1 )
		return false;

	for( unsigned i = 1; i < m_iBackrefs; ++i )
	{
		const int iStart = iMat[i*2], end = iMat[i*2+1];
		if( iStart == -1 )
			asMatches.push_back( std::string() ); /* no match */
		else
			asMatches.push_back( sStr.substr(iStart, end - iStart) );
	}

	return true;
}

// Arguments and behavior are the same are similar to
// http://us3.php.net/manual/en/function.preg-replace.php
bool Regex::Replace( const std::string &sReplacement, const std::string &sSubject, std::string &sOut )
{
	std::vector<std::string> asMatches;
	if( !Compare(sSubject, asMatches) )
		return false;

	sOut = sReplacement;

	// TODO: optimize me by iterating only once over the string
	for( unsigned i=0; i<asMatches.size(); i++ )
	{
		std::string sFrom = ssprintf( "\\${%d}", i );
		std::string sTo = asMatches[i];
		::Replace(sOut, sFrom, sTo);
	}

	return true;
}
