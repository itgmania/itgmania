#include "CryptHelpers.h"

#include <cstring>
#include <string>

#include "global.h"
#include "tomcrypt.h"

PRNGWrapper::PRNGWrapper(const struct ltc_prng_descriptor* pPRNGDescriptor) {
  m_iPRNG = register_prng(pPRNGDescriptor);
  ASSERT(m_iPRNG >= 0);

  int iRet = rng_make_prng(128, m_iPRNG, &m_PRNG, nullptr);
  ASSERT_M(iRet == CRYPT_OK, error_to_string(iRet));
}

PRNGWrapper::~PRNGWrapper() {
  if (m_iPRNG != -1) {
    prng_descriptor[m_iPRNG].done(&m_PRNG);
  }
}

RSAKeyWrapper::RSAKeyWrapper() { memset(&m_Key, 0, sizeof(m_Key)); }

RSAKeyWrapper::~RSAKeyWrapper() { Unload(); }

void RSAKeyWrapper::Unload() { rsa_free(&m_Key); }

bool RSAKeyWrapper::Load(const std::string& sKey, std::string& sError) {
  Unload();

  int iRet = rsa_import((const unsigned char*)sKey.data(), sKey.size(), &m_Key);
  if (iRet != CRYPT_OK) {
    memset(&m_Key, 0, sizeof(m_Key));
    sError = error_to_string(iRet);
    return false;
  }

  return true;
}
