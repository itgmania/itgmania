#ifndef RAGEUTIL_REGEX_H
#define RAGEUTIL_REGEX_H

#include <string>
#include <vector>

class Regex {
 public:
  Regex(const std::string& sPat = "");
  Regex(const Regex& rhs);
  Regex& operator=(const Regex& rhs);
  ~Regex();
  bool IsSet() const { return !pattern_.empty(); }
  void Set(const std::string& str);
  bool Compare(const std::string& sStr);
  bool Compare(const std::string& sStr, std::vector<std::string>& asMatches);
  bool Replace(
      const std::string& sReplacement, const std::string& sSubject,
      std::string& sOut);

 private:
  void Compile();
  void Release();

  void* reg_;
  unsigned backrefs_;
  std::string pattern_;
};

#endif
