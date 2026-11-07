#include "CryptHelpers.h"

#include <cstring>
#include <string>

#include "global.h"
#include "tomcrypt.h"

PRNGWrapper::PRNGWrapper(const struct ltc_prng_descriptor* desc) {
  i_prng_ = register_prng(desc);
  ASSERT(i_prng_ >= 0);

  int ret = rng_make_prng(128, i_prng_, &prng_, nullptr);
  ASSERT_M(ret == CRYPT_OK, error_to_string(ret));
}

PRNGWrapper::~PRNGWrapper() {
  if (i_prng_ != -1) {
    prng_descriptor[i_prng_].done(&prng_);
  }
}

RSAKeyWrapper::RSAKeyWrapper() { memset(&key_, 0, sizeof(key_)); }

RSAKeyWrapper::~RSAKeyWrapper() { Unload(); }

void RSAKeyWrapper::Unload() { rsa_free(&key_); }

bool RSAKeyWrapper::Load(const std::string& key, std::string& error) {
  Unload();

  int ret = rsa_import((const unsigned char*)key.data(), key.size(), &key_);
  if (ret != CRYPT_OK) {
    memset(&key_, 0, sizeof(key_));
    error = error_to_string(ret);
    return false;
  }

  return true;
}
