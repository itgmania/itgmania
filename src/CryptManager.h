#ifndef CryptManager_H
#define CryptManager_H

#include "RageFileBasic.h"

struct lua_State;

const RString SIGNATURE_APPEND = ".sig";

class CryptManager {
 public:
  CryptManager();
  ~CryptManager();

  static void GenerateGlobalKeys();
  static void GenerateRSAKey(
      unsigned int key_length, RString& private_key, RString& public_key);
  static void GenerateRSAKeyToFile(
      unsigned int key_length, RString private_key_filename,
      RString public_key_filename);
  static void SignFileToFile(RString path, RString signature_file = "");
  static bool Sign(RString path, RString& signature_out, RString private_key);
  static bool VerifyFileWithFile(RString path, RString signature_file = "");
  static bool VerifyFileWithFile(
      RString path, RString signature_file, RString public_key_file);
  static bool Verify(
      RageFileBasic& file, RString signature, RString public_key);

  static void GetRandomBytes(void* data, int bytes);
  static RString GenerateRandomUUID();

  static RString GetMD5ForFile(RString file_name);
  static RString GetMD5ForString(RString data);
  static RString GetSHA1ForString(RString data);
  static RString GetSHA1ForFile(RString file_name);
  static RString GetSHA256ForString(RString data);
  static RString GetSHA256ForFile(RString file_name);

  static RString GetPublicKeyFileName();

  // Lua
  void PushSelf(lua_State* L);
};

// Global and accessible from anywhere in our program.
extern CryptManager* CRYPTMAN;

#endif  // CryptManager_H

/*
 * (c) 2004 Chris Danford
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
