#include "global.h"

// tomcrypt_cfg.h redefines malloc, realloc, calloc
#pragma warning(push)
#pragma warning(disable : 4565)
#include <tomcrypt.h>

#pragma warning(pop)

// The libtomcrypt headers define a macro named byte(x,n) which collides
// with C++'s std::byte/byte helpers in <cstddef> on some libstdc++/gcc
// versions. Undefine the macro here so subsequent system headers (like
// <cstddef>) compile correctly.
#ifdef byte
#undef byte
#endif

#include <cstdint>
#include <string>
#include <vector>

#include "CryptHelpers.h"
#include "CryptManager.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "LuaReference.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"

CryptManager* CRYPTMAN =
    nullptr;  // global and accessible from anywhere in our program

static const std::string kPrivateKeyPath = "Data/private.rsa";
static const std::string kPublicKeyPath = "Data/public.rsa";
static const std::string kAlternatePublicKeyDir = "Data/keys/";

static bool HashFile(RageFileBasic& f, unsigned char buf_hash[20], int i_hash) {
  hash_state hash;
  int ret = hash_descriptor[i_hash].init(&hash);
  ASSERT_M(ret == CRYPT_OK, error_to_string(ret));

  std::string s;
  while (!f.AtEOF()) {
    s.erase();
    if (f.Read(s, 1024 * 4) == -1) {
      LOG->Warn(
          "Error reading %s: %s", f.GetDisplayPath().c_str(),
          f.GetError().c_str());
      hash_descriptor[i_hash].done(&hash, buf_hash);
      return false;
    }

    ret = hash_descriptor[i_hash].process(
        &hash, (const unsigned char*)s.data(), s.size());
    ASSERT_M(ret == CRYPT_OK, error_to_string(ret));
  }

  ret = hash_descriptor[i_hash].done(&hash, buf_hash);
  ASSERT_M(ret == CRYPT_OK, error_to_string(ret));

  return true;
}

static const int kKeyLength = 1024;
#define MAX_SIGNATURE_SIZE_BYTES 1024  // 1 KB

/*
 openssl genrsa -out testing -outform DER
 openssl rsa -in testing -out testing2 -outform DER
 openssl rsa -in testing -out testing2 -pubout -outform DER

 openssl pkcs8 -inform DER -outform DER -nocrypt -in private.rsa -out
 private.der
 *
 */

static PRNGWrapper* g_prng = nullptr;
ltc_math_descriptor ltc_mp = ltm_desc;

CryptManager::CryptManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "CRYPTMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  g_prng = new PRNGWrapper(&yarrow_desc);
}

void CryptManager::GenerateGlobalKeys() {
  //
  // generate keys if none are available
  //
  bool generate = false;
  RSAKeyWrapper key;
  std::string s_key;
  std::string error;
  if (!DoesFileExist(kPrivateKeyPath) ||
      !GetFileContents(kPrivateKeyPath, s_key) || !key.Load(s_key, error)) {
    generate = true;
  }
  if (!error.empty()) {
    LOG->Warn("Error loading RSA key: %s", error.c_str());
  }

  error.clear();
  if (!DoesFileExist(kPublicKeyPath) ||
      !GetFileContents(kPublicKeyPath, s_key) || !key.Load(s_key, error)) {
    generate = true;
  }
  if (!error.empty()) {
    LOG->Warn("Error loading RSA key: %s", error.c_str());
  }

  if (generate) {
    LOG->Warn("Keys missing or failed to load.  Generating new keys");
    GenerateRSAKeyToFile(kKeyLength, kPrivateKeyPath, kPublicKeyPath);
  }
}

CryptManager::~CryptManager() {
  RageUtil::SafeDelete(g_prng);
  // Unregister with Lua.
  LUA->UnsetGlobal("CRYPTMAN");
}

static bool WriteFile(std::string file, std::string buf) {
  RageFile output;
  if (!output.Open(file, RageFile::WRITE)) {
    LOG->Warn(
        "WriteFile: opening %s failed: %s", file.c_str(),
        output.GetError().c_str());
    return false;
  }

  if (output.Write(buf) == -1 || output.Flush() == -1) {
    LOG->Warn(
        "WriteFile: writing %s failed: %s", file.c_str(),
        output.GetError().c_str());
    output.Close();
    FILEMAN->Remove(file);
    return false;
  }

  return true;
}

void CryptManager::GenerateRSAKey(
    unsigned int key_length, std::string& priv_key, std::string& pub_key) {
  int ret;

  rsa_key key;
  ret = rsa_make_key(
      &g_prng->prng_, g_prng->i_prng_, key_length / 8, 65537, &key);
  if (ret != CRYPT_OK) {
    LOG->Warn("GenerateRSAKey(%i) error: %s", key_length, error_to_string(ret));
    return;
  }

  unsigned char buf[1024];
  unsigned long size = sizeof(buf);
  ret = rsa_export(buf, &size, PK_PUBLIC, &key);
  if (ret != CRYPT_OK) {
    LOG->Warn("Export error: %s", error_to_string(ret));
    return;
  }

  pub_key = std::string((const char*)buf, size);

  size = sizeof(buf);
  ret = rsa_export(buf, &size, PK_PRIVATE, &key);
  if (ret != CRYPT_OK) {
    LOG->Warn("Export error: %s", error_to_string(ret));
    return;
  }

  priv_key = std::string((const char*)buf, size);
}

void CryptManager::GenerateRSAKeyToFile(
    unsigned int key_length, std::string priv_filename,
    std::string pub_filename) {
  std::string priv_key, pub_key;
  GenerateRSAKey(key_length, priv_key, pub_key);

  if (!WriteFile(pub_filename, pub_key)) {
    return;
  }

  if (!WriteFile(priv_filename, priv_key)) {
    FILEMAN->Remove(priv_filename);
    return;
  }
}

void CryptManager::SignFileToFile(
    std::string path, std::string signature_file) {
  std::string priv_filename = kPrivateKeyPath;
  if (signature_file.empty()) {
    signature_file = path + kSignatureAppend;
  }

  std::string priv_key;
  if (!GetFileContents(priv_filename, priv_key)) {
    return;
  }

  std::string signature;
  if (!Sign(path, signature, priv_key)) {
    return;
  }

  WriteFile(signature_file, signature);
}

bool CryptManager::Sign(
    std::string path, std::string& signature_out, std::string priv_key) {
  if (!IsAFile(path)) {
    LOG->Trace("SignFileToFile: \"%s\" doesn't exist", path.c_str());
    return false;
  }

  RageFile file;
  if (!file.Open(path)) {
    LOG->Warn(
        "SignFileToFile: open(%s) failed: %s", path.c_str(),
        file.GetError().c_str());
    return false;
  }

  RSAKeyWrapper key;
  std::string error;
  if (!key.Load(priv_key, error)) {
    LOG->Warn("Error loading RSA key: %s", error.c_str());
    return false;
  }

  int hash = register_hash(&sha1_desc);
  ASSERT(hash >= 0);

  unsigned char buf_hash[20];
  if (!HashFile(file, buf_hash, hash)) {
    return false;
  }

  unsigned char signature[256];
  unsigned long signature_len = sizeof(signature);

  int ret = rsa_sign_hash_ex(
      buf_hash, sizeof(buf_hash), signature, &signature_len, LTC_PKCS_1_V1_5,
      &g_prng->prng_, g_prng->i_prng_, hash, 0, &key.key_);
  if (ret != CRYPT_OK) {
    LOG->Warn("SignFileToFile error: %s", error_to_string(ret));
    return false;
  }

  signature_out.assign((const char*)signature, signature_len);
  return true;
}

bool CryptManager::VerifyFileWithFile(
    std::string path, std::string signature_file) {
  if (VerifyFileWithFile(path, signature_file, kPublicKeyPath)) {
    return true;
  }

  std::vector<std::string> keys;
  GetDirListing(kAlternatePublicKeyDir, keys, false, true);
  for (unsigned i = 0; i < keys.size(); ++i) {
    const std::string& key = keys[i];
    LOG->Trace("Trying alternate key \"%s\" ...", key.c_str());

    if (VerifyFileWithFile(path, signature_file, key)) {
      return true;
    }
  }

  return false;
}

bool CryptManager::VerifyFileWithFile(
    std::string path, std::string signature_file, std::string public_key_file) {
  if (signature_file.empty()) {
    signature_file = path + kSignatureAppend;
  }

  std::string public_key;
  if (!GetFileContents(public_key_file, public_key)) {
    return false;
  }

  int bytes = FILEMAN->GetFileSizeInBytes(signature_file);
  if (bytes > MAX_SIGNATURE_SIZE_BYTES) {
    return false;
  }

  std::string signature;
  if (!GetFileContents(signature_file, signature)) {
    return false;
  }

  RageFile file;
  if (!file.Open(path)) {
    LOG->Warn(
        "Verify: open(%s) failed: %s", path.c_str(), file.GetError().c_str());
    return false;
  }

  return Verify(file, signature, public_key);
}

bool CryptManager::Verify(
    RageFileBasic& file, std::string signature, std::string public_key) {
  RSAKeyWrapper key;
  std::string error;
  if (!key.Load(public_key, error)) {
    LOG->Warn("Error loading RSA key: %s", error.c_str());
    return false;
  }

  int hash = register_hash(&sha1_desc);
  ASSERT(hash >= 0);

  unsigned char buf_hash[20];
  HashFile(file, buf_hash, hash);

  int match;
  int ret = rsa_verify_hash_ex(
      (const unsigned char*)signature.data(), signature.size(), buf_hash,
      sizeof(buf_hash), LTC_PKCS_1_EMSA, hash, 0, &match, &key.key_);

  if (ret != CRYPT_OK) {
    LOG->Warn(
        "Verify(%s) failed: %s", file.GetDisplayPath().c_str(),
        error_to_string(ret));
    return false;
  }

  if (!match) {
    LOG->Warn(
        "Verify(%s) failed: signature mismatch", file.GetDisplayPath().c_str());
    return false;
  }

  return true;
}

void CryptManager::GetRandomBytes(void* data, int bytes) {
  int ret = prng_descriptor[g_prng->i_prng_].read(
      (unsigned char*)data, bytes, &g_prng->prng_);
  ASSERT(ret == bytes);
}

std::string CryptManager::GetMD5ForFile(std::string fn) {
  RageFile file;
  if (!file.Open(fn, RageFile::READ)) {
    LOG->Warn("GetMD5: Failed to open file '%s'", fn.c_str());
    return std::string();
  }
  int hash = register_hash(&md5_desc);
  ASSERT(hash >= 0);

  unsigned char digest[16];
  HashFile(file, digest, hash);

  return std::string((const char*)digest, sizeof(digest));
}

std::string CryptManager::GetMD5ForString(std::string data) {
  unsigned char digest[16];

  int i_hash = register_hash(&md5_desc);

  hash_state hash;
  hash_descriptor[i_hash].init(&hash);
  hash_descriptor[i_hash].process(
      &hash, (const unsigned char*)data.data(), data.size());
  hash_descriptor[i_hash].done(&hash, digest);

  return std::string((const char*)digest, sizeof(digest));
}

std::string CryptManager::GetSHA1ForString(std::string data) {
  unsigned char digest[20];

  int i_hash = register_hash(&sha1_desc);

  hash_state hash;
  hash_descriptor[i_hash].init(&hash);
  hash_descriptor[i_hash].process(
      &hash, (const unsigned char*)data.data(), data.size());
  hash_descriptor[i_hash].done(&hash, digest);

  return std::string((const char*)digest, sizeof(digest));
}

std::string CryptManager::GetSHA1ForFile(std::string fn) {
  RageFile file;
  if (!file.Open(fn, RageFile::READ)) {
    LOG->Warn("GetSHA1: Failed to open file '%s'", fn.c_str());
    return std::string();
  }
  int hash = register_hash(&sha1_desc);
  ASSERT(hash >= 0);

  unsigned char digest[20];
  HashFile(file, digest, hash);

  return std::string((const char*)digest, sizeof(digest));
}

std::string CryptManager::GetSHA256ForString(std::string data) {
  unsigned char digest[32];

  int i_hash = register_hash(&sha256_desc);

  hash_state hash;
  hash_descriptor[i_hash].init(&hash);
  hash_descriptor[i_hash].process(
      &hash, (const unsigned char*)data.data(), data.size());
  hash_descriptor[i_hash].done(&hash, digest);

  return std::string((const char*)digest, sizeof(digest));
}

std::string CryptManager::GetSHA256ForFile(std::string fn) {
  RageFile file;
  if (!file.Open(fn, RageFile::READ)) {
    LOG->Warn("GetSHA256: Failed to open file '%s'", fn.c_str());
    return std::string();
  }
  int hash = register_hash(&sha256_desc);
  ASSERT(hash >= 0);

  unsigned char digest[32];
  HashFile(file, digest, hash);

  return std::string((const char*)digest, sizeof(digest));
}

/* Generate a version 4 random UUID. */
std::string CryptManager::GenerateRandomUUID() {
  uint32_t buf[4];
  CryptManager::GetRandomBytes(buf, sizeof(buf));

  buf[1] &= 0xFFFF0FFF;
  buf[1] |= 0x00004000;
  buf[2] &= 0x0FFFFFFF;
  buf[2] |= 0xA0000000;

  return ssprintf(
      "%08x-%04x-%04x-%04x-%04x%08x", buf[0], buf[1] >> 16, buf[1] & 0xFFFF,
      buf[2] >> 16, buf[2] & 0xFFFF, buf[3]);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the CryptManager. */
class LunaCryptManager : public Luna<CryptManager> {
 public:
  static int MD5String(T* p, lua_State* L) {
    std::string md5out;
    md5out = p->GetMD5ForString(SArg(1));
    lua_pushlstring(L, md5out.c_str(), md5out.size());
    return 1;
  }
  static int MD5File(T* p, lua_State* L) {
    std::string md5fout;
    md5fout = p->GetMD5ForFile(SArg(1));
    lua_pushlstring(L, md5fout.c_str(), md5fout.size());
    return 1;
  }
  static int SHA1String(T* p, lua_State* L) {
    std::string sha1out;
    sha1out = p->GetSHA1ForString(SArg(1));
    lua_pushlstring(L, sha1out.c_str(), sha1out.size());
    return 1;
  }
  static int SHA1File(T* p, lua_State* L) {
    std::string sha1fout;
    sha1fout = p->GetSHA1ForFile(SArg(1));
    lua_pushlstring(L, sha1fout.c_str(), sha1fout.size());
    return 1;
  }
  static int SHA256String(T* p, lua_State* L) {
    std::string sha256out;
    sha256out = p->GetSHA256ForString(SArg(1));
    lua_pushlstring(L, sha256out.c_str(), sha256out.size());
    return 1;
  }
  static int SHA256File(T* p, lua_State* L) {
    std::string sha256fout;
    sha256fout = p->GetSHA256ForFile(SArg(1));
    lua_pushlstring(L, sha256fout.c_str(), sha256fout.size());
    return 1;
  }
  static int GenerateRandomUUID(T* p, lua_State* L) {
    std::string uuid_out;
    uuid_out = p->GenerateRandomUUID();
    lua_pushlstring(L, uuid_out.c_str(), uuid_out.size());
    return 1;
  }

  LunaCryptManager() {
    ADD_METHOD(MD5String);
    ADD_METHOD(MD5File);
    ADD_METHOD(SHA1String);
    ADD_METHOD(SHA1File);
    ADD_METHOD(SHA256String);
    ADD_METHOD(SHA256File);
    ADD_METHOD(GenerateRandomUUID);
  }
};

LUA_REGISTER_CLASS(CryptManager)

// lua end

/*
 * (c) 2004-2007 Chris Danford, Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
