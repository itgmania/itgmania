#ifndef RAGEUTIL_LANGUAGEINFO_H
#define RAGEUTIL_LANGUAGEINFO_H

#include <vector>
#include "global.h"

struct LanguageInfo
{
	const char *szIsoCode;
	const char *szEnglishName;
};
void GetLanguageInfos( std::vector<const LanguageInfo*> &vAddTo );
const LanguageInfo *GetLanguageInfo( const RString &sIsoCode );
RString GetLanguageNameFromISO639Code( RString sName );

#endif