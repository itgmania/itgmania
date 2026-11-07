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

  int ret =
      pcre_fullinfo((pcre*)reg_, nullptr, PCRE_INFO_CAPTURECOUNT, &backrefs_);
  ASSERT(ret >= 0);

  ++backrefs_;
  ASSERT(backrefs_ < 128);
}

void Regex::Set(const std::string& str) {
  Release();
  pattern_ = str;
  Compile();
}

void Regex::Release() {
  pcre_free(reg_);
  reg_ = nullptr;
  pattern_ = std::string();
}

Regex::Regex(const std::string& str)
    : reg_(nullptr), backrefs_(0), pattern_(std::string()) {
  Set(str);
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

bool Regex::Compare(const std::string& str) {
  int mat[128 * 3];
  int ret = pcre_exec(
      (pcre*)reg_, nullptr, str.data(), str.size(), 0, 0, mat, 128 * 3);

  if (ret < -1) {
    RageException::Throw(
        "Unexpected return from pcre_exec('%s'): %i.", pattern_.c_str(), ret);
  }

  return ret >= 0;
}

bool Regex::Compare(const std::string& str, std::vector<std::string>& matches) {
  matches.clear();

  int mat[128 * 3];
  int ret = pcre_exec(
      (pcre*)reg_, nullptr, str.data(), str.size(), 0, 0, mat, 128 * 3);

  if (ret < -1) {
    RageException::Throw(
        "Unexpected return from pcre_exec('%s'): %i.", pattern_.c_str(), ret);
  }

  if (ret == -1) {
    return false;
  }

  for (unsigned i = 1; i < backrefs_; ++i) {
    const int start = mat[i * 2], end = mat[i * 2 + 1];
    if (start == -1) {
      matches.push_back(std::string()); /* no match */
    } else {
      matches.push_back(str.substr(start, end - start));
    }
  }

  return true;
}

// Arguments and behavior are the same are similar to
// http://us3.php.net/manual/en/function.preg-replace.php
bool Regex::Replace(
    const std::string& replacement, const std::string& subject,
    std::string& out) {
  std::vector<std::string> matches;
  if (!Compare(subject, matches)) {
    return false;
  }

  out = replacement;

  // TODO: optimize me by iterating only once over the string
  for (unsigned i = 0; i < matches.size(); i++) {
    std::string from = ssprintf("\\${%d}", i);
    std::string to = matches[i];
    ::Replace(out, from, to);
  }

  return true;
}
