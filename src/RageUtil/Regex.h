#ifndef RAGEUTIL_REGEX_H
#define RAGEUTIL_REGEX_H

#include <string>
#include <vector>

class Regex
{
public:
	Regex( const std::string &sPat = "" );
	Regex( const Regex &rhs );
	Regex &operator=( const Regex &rhs );
	~Regex();
	bool IsSet() const { return !m_sPattern.empty(); }
	void Set( const std::string &str );
	bool Compare( const std::string &sStr );
	bool Compare( const std::string &sStr, std::vector<std::string> &asMatches );
	bool Replace( const std::string &sReplacement, const std::string &sSubject, std::string &sOut );

private:
	void Compile();
	void Release();

	void *m_pReg;
	unsigned m_iBackrefs;
	std::string m_sPattern;
};

#endif
