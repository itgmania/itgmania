#include "CryptHelpers.h"

#include <cstring>
#include <string>

#include "global.h"
#include "tomcrypt.h"

PRNGWrapper::PRNGWrapper(const struct ltc_prng_descriptor* pPRNGDescriptor) {
  i_prng_ = register_prng(pPRNGDescriptor);
  ASSERT(i_prng_ >= 0);

  int iRet = rng_make_prng(128, i_prng_, &prng_, nullptr);
  ASSERT_M(iRet == CRYPT_OK, error_to_string(iRet));
}

PRNGWrapper::~PRNGWrapper() {
  if (i_prng_ != -1) {
    prng_descriptor[i_prng_].done(&prng_);
  }
}

RSAKeyWrapper::RSAKeyWrapper() { memset(&key_, 0, sizeof(key_)); }

RSAKeyWrapper::~RSAKeyWrapper() { Unload(); }

void RSAKeyWrapper::Unload() { rsa_free(&key_); }

bool RSAKeyWrapper::Load(const std::string& sKey, std::string& sError) {
  Unload();

  int iRet = rsa_import((const unsigned char*)sKey.data(), sKey.size(), &key_);
  if (iRet != CRYPT_OK) {
    memset(&key_, 0, sizeof(key_));
    sError = error_to_string(iRet);
    return false;
  }

  return true;
}
