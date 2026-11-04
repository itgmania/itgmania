#include "Regex.h"

#include <pcre.h>

#include <string>
#include <vector>

#include "RageException.h"
#include "RageUtil.h"
#include "StdString.h"
#include "global.h"

void Regex::Compile() {
  const char* error;
  int offset;
  reg_ =
      pcre_compile(pattern_.c_str(), PCRE_CASELESS, &error, &offset, nullptr);

  if (reg_ == nullptr) {
    RageException::Throw(
        "Invalid regex: \"%s\" (%s).", pattern_.c_str(), error);
  }

  int iRet =
      pcre_fullinfo((pcre*)reg_, nullptr, PCRE_INFO_CAPTURECOUNT, &backrefs_);
  ASSERT(iRet >= 0);

  ++backrefs_;
  ASSERT(backrefs_ < 128);
}

void Regex::Set(const std::string& sStr) {
  Release();
  pattern_ = sStr;
  Compile();
}

void Regex::Release() {
  pcre_free(reg_);
  reg_ = nullptr;
  pattern_ = std::string();
}

Regex::Regex(const std::string& sStr)
    : reg_(nullptr), backrefs_(0), pattern_(std::string()) {
  Set(sStr);
}

Regex::Regex(const Regex& rhs)
    : reg_(nullptr), backrefs_(0), pattern_(std::string()) {
  Set(rhs.pattern_);
}

Regex& Regex::operator=(const Regex& rhs) {
  if (this != &rhs) {
    Set(rhs.pattern_);
  }
  return *this;
}

Regex::~Regex() { Release(); }

bool Regex::Compare(const std::string& sStr) {
  int iMat[128 * 3];
  int iRet = pcre_exec(
      (pcre*)reg_, nullptr, sStr.data(), sStr.size(), 0, 0, iMat, 128 * 3);

  if (iRet < -1) {
    RageException::Throw(
        "Unexpected return from pcre_exec('%s'): %i.", pattern_.c_str(), iRet);
  }

  return iRet >= 0;
}

bool Regex::Compare(
    const std::string& sStr, std::vector<std::string>& asMatches) {
  asMatches.clear();

  int iMat[128 * 3];
  int iRet = pcre_exec(
      (pcre*)reg_, nullptr, sStr.data(), sStr.size(), 0, 0, iMat, 128 * 3);

  if (iRet < -1) {
    RageException::Throw(
        "Unexpected return from pcre_exec('%s'): %i.", pattern_.c_str(), iRet);
  }

  if (iRet == -1) {
    return false;
  }

  for (unsigned i = 1; i < backrefs_; ++i) {
    const int iStart = iMat[i * 2], end = iMat[i * 2 + 1];
    if (iStart == -1) {
      asMatches.push_back(std::string()); /* no match */
    } else {
      asMatches.push_back(sStr.substr(iStart, end - iStart));
    }
  }

  return true;
}

// Arguments and behavior are the same are similar to
// http://us3.php.net/manual/en/function.preg-replace.php
bool Regex::Replace(
    const std::string& sReplacement, const std::string& sSubject,
    std::string& sOut) {
  std::vector<std::string> asMatches;
  if (!Compare(sSubject, asMatches)) {
    return false;
  }

  sOut = sReplacement;

  // TODO: optimize me by iterating only once over the string
  for (unsigned i = 0; i < asMatches.size(); i++) {
    std::string sFrom = ssprintf("\\${%d}", i);
    std::string sTo = asMatches[i];
    ::Replace(sOut, sFrom, sTo);
  }

  return true;
}
