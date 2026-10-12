#ifndef RAGEUTIL_LANGUAGEINFO_H
#define RAGEUTIL_LANGUAGEINFO_H

#include <string>
#include <vector>

struct LanguageInfo {
  const char* szIsoCode;
  const char* szEnglishName;
};
const LanguageInfo* GetLanguageInfo(const std::string& sIsoCode);

#endif
