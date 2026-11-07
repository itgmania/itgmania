#ifndef CRYPT_HELPERS_H
#define CRYPT_HELPERS_H

// tomcrypt_cfg.h redefines malloc, realloc, calloc
#pragma warning(push)
#pragma warning(disable : 4565)
#include <tomcrypt.h>

#include <string>
#pragma warning(pop)

class PRNGWrapper {
 public:
  PRNGWrapper(const struct ltc_prng_descriptor* desc);
  ~PRNGWrapper();
  int i_prng_;
  prng_state prng_;
};

class RSAKeyWrapper {
 public:
  RSAKeyWrapper();
  ~RSAKeyWrapper();
  void Unload();
  bool Load(const std::string& key, std::string& error);

  rsa_key key_;
};

#endif
