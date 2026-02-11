#ifndef STDSTRING_H
#define STDSTRING_H

#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

/* In RageUtil: */
void MakeUpper( char *p, size_t iLen );
void MakeLower( char *p, size_t iLen );
void MakeUpper( wchar_t *p, size_t iLen );
void MakeLower( wchar_t *p, size_t iLen );

// FIXME: separate these into functions that either modify the argument, or
// return a new string leaving the original unmodified.
inline std::string MakeLower(std::string&& s) {
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = tolower(s[i]);
  }
  return std::move(s);
}
inline std::string& MakeLower(std::string& s) {
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = tolower(s[i]);
  }
  return s;
}
inline std::string MakeUpper(std::string&& s) {
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = toupper(s[i]);
  }
  return std::move(s);
}
inline std::string& MakeUpper(std::string& s) {
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = toupper(s[i]);
  }
  return s;
}
inline bool EqualsNoCase(const std::string& s1, const std::string& s2) {
  if (s1.size() != s2.size()) {
    return false;
  }
  for (size_t i = 0; i < s1.size(); ++i) {
    if (tolower(s1[i]) != tolower(s2[i])) {
      return false;
    }
  }
  return true;
}
inline int CompareNoCase(const std::string& s1, const std::string& s2) {
  size_t len = s1.length();
  size_t other_len = s2.length();
  size_t end = len < other_len ? len : other_len;
  for (size_t i = 0; i < end; ++i) {
    int a = tolower(s1[i]);
    int b = tolower(s2[i]);
    if (a - b != 0) {
      return a - b;
    }
  }
  return len - other_len;
}
inline std::string Left(const std::string& s, int n) {
  if (n < 0) {
    n = 0;
  }
  if (n > static_cast<int>(s.size())) {
    n = static_cast<int>(s.size());
  }
  return s.substr(0, n);
}
inline std::string Right(const std::string& s, int n) {
  if (n < 0) {
    n = 0;
  }
  if (n > static_cast<int>(s.size())) {
    n = static_cast<int>(s.size());
  }
  return s.substr(static_cast<int>(s.size()) - n);
}
inline void Replace(std::string& s, const std::string& a, const std::string& b) {
  size_t idx = 0;
  size_t a_len = a.length();
  size_t b_len = b.length();
  while (idx = s.find(a, idx), idx != std::string::npos) {
    s.replace(idx, a_len, b);
    idx += b_len;
  }
}
inline void Replace(std::string& s, char a, char b) {
  size_t len = s.length();
  for (size_t i = 0; i < len; ++i) {
    if (s[i] == a) {
      s[i] = b;
    }
  }
}

#endif	// #ifndef STDSTRING_H
