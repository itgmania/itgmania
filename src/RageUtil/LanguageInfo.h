#ifndef RAGEUTIL_LANGUAGEINFO_H
#define RAGEUTIL_LANGUAGEINFO_H

#include <string>
#include <vector>

struct LanguageInfo
{
	const char *szIsoCode;
	const char *szEnglishName;
};
void GetLanguageInfos( std::vector<const LanguageInfo*> &vAddTo );
const LanguageInfo *GetLanguageInfo( const std::string &sIsoCode );
std::string GetLanguageNameFromISO639Code( std::string sName );

#endif
