#ifndef RAGEUTIL_REGEX_H
#define RAGEUTIL_REGEX_H

#include "global.h"

#include <vector>

class Regex
{
public:
	Regex( const RString &sPat = "" );
	Regex( const Regex &rhs );
	Regex &operator=( const Regex &rhs );
	~Regex();
	bool IsSet() const { return !m_sPattern.empty(); }
	void Set( const RString &str );
	bool Compare( const RString &sStr );
	bool Compare( const RString &sStr, std::vector<RString> &asMatches );
	bool Replace( const RString &sReplacement, const RString &sSubject, RString &sOut );

private:
	void Compile();
	void Release();

	void *m_pReg;
	unsigned m_iBackrefs;
	RString m_sPattern;
};

#endif