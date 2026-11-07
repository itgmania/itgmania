#ifndef RAGEUTIL_LANGUAGEINFO_H
#define RAGEUTIL_LANGUAGEINFO_H

#include <string>
#include <vector>

struct LanguageInfo {
  const char* iso_code_;
  const char* english_name_;
};
const LanguageInfo* GetLanguageInfo(const std::string& iso_code);

#endif
