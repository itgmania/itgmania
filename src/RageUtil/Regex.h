#ifndef RAGEUTIL_REGEX_H
#define RAGEUTIL_REGEX_H

#include <string>
#include <vector>

class Regex {
 public:
  Regex(const std::string& pat = "");
  Regex(const Regex& rhs);
  Regex& operator=(const Regex& rhs);
  ~Regex();
  bool IsSet() const { return !pattern_.empty(); }
  void Set(const std::string& str);
  bool Compare(const std::string& str);
  bool Compare(const std::string& str, std::vector<std::string>& matches);
  bool Replace(
      const std::string& replacement, const std::string& subject,
      std::string& out);

 private:
  void Compile();
  void Release();

  void* reg_;
  unsigned backrefs_;
  std::string pattern_;
};

#endif
