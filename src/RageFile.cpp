/*
 * This provides an interface to open files in RageFileManager's namespace
 * This is just a simple RageFileBasic wrapper on top of another RageFileBasic;
 * when a file is open, is acts like the underlying RageFileBasic, except that
 * a few extra sanity checks are made to check file modes.
 */

#include "RageFile.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "LuaManager.h"
#include "RageFileBasic.h"
#include "RageFileDriver.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "global.h"

RageFile::RageFile() : file_(nullptr), mode_(0) {}

RageFile::RageFile(const RageFile& cpy) : RageFileBasic(cpy) {
  /* This will copy the file driver, including its internal file pointer. */
  file_ = nullptr;
  if (cpy.file_ != nullptr) {
    file_ = cpy.file_->Copy();
  }
  path_ = cpy.path_;
  mode_ = cpy.mode_;
}

RageFile* RageFile::Copy() const { return new RageFile(*this); }

std::string RageFile::GetPath() const {
  if (!IsOpen()) {
    return std::string();
  }

  std::string ret = file_->GetDisplayPath();
  if (ret != "") {
    return ret;
  }

  return GetRealPath();
}

bool RageFile::Open(const std::string& path, int mode) {
  ASSERT(FILEMAN != nullptr);
  Close();

  path_ = path;
  FixSlashesInPlace(path_);

  mode_ = mode;

  if ((mode_ & READ) && (mode_ & WRITE)) {
    SetError("Reading and writing are mutually exclusive");
    return false;
  }

  if (!(mode_ & READ) && !(mode_ & WRITE)) {
    SetError("Neither reading nor writing specified");
    return false;
  }

  int error;
  file_ = FILEMAN->Open(path, mode, error);

  if (file_ == nullptr) {
    SetError(strerror(error));
    return false;
  }

  return true;
}

void RageFile::Close() {
  if (file_ == nullptr) {
    return;
  }
  RageFileBasic* file = file_;
  file_ = nullptr;
  if (mode_ & WRITE) {
    FILEMAN->CacheFile(file, path_);
  }
  delete file;
}

void RageFile::AssertOpen() const {
  if (!IsOpen()) {
    sm_crash(ssprintf("File \"%s\" is not open.", path_.c_str()));
  }
}

void RageFile::AssertRead() const {
  AssertOpen();
  if (!(mode_ & READ)) {
    sm_crash(ssprintf("File \"%s\" is not open for reading", path_.c_str()));
  }
}

void RageFile::AssertWrite() const {
  AssertOpen();
  if (!(mode_ & WRITE)) {
    sm_crash(ssprintf("File \"%s\" is not open for writing", path_.c_str()));
  }
}

int RageFile::GetLine(std::string& out) {
  AssertRead();
  return file_->GetLine(out);
}

int RageFile::PutLine(const std::string& str) {
  AssertWrite();
  return file_->PutLine(str);
}

void RageFile::EnableCRC32(bool on) {
  AssertOpen();
  file_->EnableCRC32(on);
}

bool RageFile::GetCRC32(uint32_t* ret) {
  AssertOpen();
  return file_->GetCRC32(ret);
}

bool RageFile::AtEOF() const {
  AssertRead();
  return file_->AtEOF();
}

void RageFile::ClearError() {
  if (file_ != nullptr) {
    file_->ClearError();
  }
  error_ = "";
}

std::string RageFile::GetError() const {
  if (file_ != nullptr && file_->GetError() != "") {
    return file_->GetError();
  }
  return error_;
}

void RageFile::SetError(const std::string& err) {
  if (file_ != nullptr) {
    file_->ClearError();
  }
  error_ = err;
}

int RageFile::Read(void* buffer, size_t bytes) {
  AssertRead();
  return file_->Read(buffer, bytes);
}

int RageFile::Seek(int offset) {
  AssertRead();
  return file_->Seek(offset);
}

int RageFile::Tell() const {
  AssertRead();
  return file_->Tell();
}

int RageFile::GetFileSize() const {
  AssertRead();
  return file_->GetFileSize();
}

int RageFile::GetFD() {
  AssertRead();
  return file_->GetFD();
}

int RageFile::Read(std::string& buffer, int bytes) {
  AssertRead();
  return file_->Read(buffer, bytes);
}

int RageFile::Write(const void* buffer, size_t bytes) {
  AssertWrite();
  return file_->Write(buffer, bytes);
}

int RageFile::Write(const void* buffer, size_t bytes, int nmemb) {
  AssertWrite();
  return file_->Write(buffer, bytes, nmemb);
}

int RageFile::Flush() {
  if (!file_) {
    SetError("Not open");
    return -1;
  }

  return file_->Flush();
}

int RageFile::Read(void* buffer, size_t bytes, int nmemb) {
  AssertRead();
  return file_->Read(buffer, bytes, nmemb);
}

int RageFile::Seek(int offset, int whence) {
  AssertRead();
  return file_->Seek(offset, whence);
}

void FileReading::ReadBytes(
    RageFileBasic& f, void* buf, int size, std::string& error) {
  if (error.size() != 0) {
    return;
  }

  int ret = f.Read(buf, size);
  if (ret == -1) {
    error = f.GetError();
  } else if (ret < size) {
    error = "Unexpected end of file";
  }
}

std::string FileReading::ReadString(
    RageFileBasic& f, int size, std::string& error) {
  if (error.size() != 0) {
    return std::string();
  }

  std::string buf;
  int ret = f.Read(buf, size);
  if (ret == -1) {
    error = f.GetError();
  } else if (ret < size) {
    error = "Unexpected end of file";
  }
  return buf;
}

void FileReading::SkipBytes(RageFileBasic& f, int bytes, std::string& error) {
  if (error.size() != 0) {
    return;
  }

  bytes += f.Tell();
  FileReading::Seek(f, bytes, error);
}

void FileReading::Seek(RageFileBasic& f, int offset, std::string& error) {
  if (error.size() != 0) {
    return;
  }

  int got = f.Seek(offset);
  if (got == offset) {
    return;
  }
  if (got == -1) {
    error = f.GetError();
  } else if (got < offset) {
    error = "Unexpected end of file";
  }
}

uint8_t FileReading::read_8(RageFileBasic& f, std::string& error) {
  uint8_t val;
  ReadBytes(f, &val, sizeof(uint8_t), error);
  if (error.size() == 0) {
    return val;
  } else {
    return 0;
  }
}

uint16_t FileReading::read_u16_le(RageFileBasic& f, std::string& error) {
  uint16_t val;
  ReadBytes(f, &val, sizeof(uint16_t), error);
  if (error.size() == 0) {
    return Swap16LE(val);
  } else {
    return 0;
  }
}

int16_t FileReading::read_16_le(RageFileBasic& f, std::string& error) {
  int16_t val;
  ReadBytes(f, &val, sizeof(int16_t), error);
  if (error.size() == 0) {
    return Swap16LE(val);
  } else {
    return 0;
  }
}

uint32_t FileReading::read_u32_le(RageFileBasic& f, std::string& error) {
  uint32_t val;
  ReadBytes(f, &val, sizeof(uint32_t), error);
  if (error.size() == 0) {
    return Swap32LE(val);
  } else {
    return 0;
  }
}

int32_t FileReading::read_32_le(RageFileBasic& f, std::string& error) {
  int32_t val;
  ReadBytes(f, &val, sizeof(int32_t), error);
  if (error.size() == 0) {
    return Swap32LE(val);
  } else {
    return 0;
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the RageFile. */
class LunaRageFile : public Luna<RageFile> {
 public:
  static void safely_opened(T* p, lua_State* L) {
    if (!p->IsOpen()) {
      luaL_error(L, "File '%s' is not open.", p->GetPath().c_str());
    }
  }
  static void can_safely_read(T* p, lua_State* L) {
    safely_opened(p, L);
    if (!(p->GetMode() & RageFile::READ)) {
      luaL_error(L, "File '%s' is not open for reading.", p->GetPath().c_str());
    }
  }
  static void can_safely_write(T* p, lua_State* L) {
    safely_opened(p, L);
    if (!(p->GetMode() & RageFile::WRITE)) {
      luaL_error(L, "File '%s' is not open for writing.", p->GetPath().c_str());
    }
  }
  static int destroy(T* p, lua_State* L) {
    RageUtil::SafeDelete(p);
    return 1;
  }

  static int Open(T* p, lua_State* L) {
    const std::string path = SArg(1);
    int mode = IArg(2);

    if ((mode & RageFile::WRITE) && FILEMAN->IsPathProtected(path)) {
      LOG->Warn("Writing to %s is not allowed", path.c_str());
      lua_pushboolean(L, false);
      return 1;
    }

    lua_pushboolean(L, p->Open(path, mode));
    return 1;
  }

  static int Close(T* p, lua_State* L) {
    p->Close();
    return 1;
  }

  static int Write(T* p, lua_State* L) {
    can_safely_write(p, L);
    lua_pushinteger(L, p->Write(SArg(1)));
    return 1;
  }

  static int Flush(T* p, lua_State* L) {
    p->Flush();
    COMMON_RETURN_SELF;
  }

  static int Read(T* p, lua_State* L) {
    can_safely_read(p, L);
    std::string string;
    p->Read(string);
    lua_pushstring(L, string.c_str());
    return 1;
  }

  static int ReadBytes(T* p, lua_State* L) {
    can_safely_read(p, L);
    std::string string;
    p->Read(string, IArg(1));
    lua_pushstring(L, string.c_str());
    return 1;
  }

  static int Seek(T* p, lua_State* L) {
    can_safely_read(p, L);
    lua_pushinteger(L, p->Seek(IArg(1)));
    return 1;
  }

  static int Tell(T* p, lua_State* L) {
    can_safely_read(p, L);
    lua_pushinteger(L, p->Tell());
    return 1;
  }

  static int GetLine(T* p, lua_State* L) {
    can_safely_read(p, L);
    std::string string;
    p->GetLine(string);
    lua_pushstring(L, string.c_str());
    return 1;
  }

  static int PutLine(T* p, lua_State* L) {
    can_safely_write(p, L);
    lua_pushinteger(L, p->PutLine(SArg(1)));
    return 1;
  }

  static int GetError(T* p, lua_State* L) {
    std::string error;
    error = p->GetError();
    lua_pushstring(L, error.c_str());
    return 1;
  }

  static int ClearError(T* p, lua_State* L) {
    p->ClearError();
    return 1;
  }

  static int AtEOF(T* p, lua_State* L) {
    can_safely_read(p, L);
    lua_pushboolean(L, p->AtEOF());
    return 1;
  }

  LunaRageFile() {
    ADD_METHOD(Open);
    ADD_METHOD(Close);
    ADD_METHOD(Write);
    ADD_METHOD(Flush);
    ADD_METHOD(Read);
    ADD_METHOD(ReadBytes);
    ADD_METHOD(Seek);
    ADD_METHOD(Tell);
    ADD_METHOD(GetLine);
    ADD_METHOD(PutLine);
    ADD_METHOD(destroy);
    ADD_METHOD(GetError);
    ADD_METHOD(ClearError);
    ADD_METHOD(AtEOF);
  }
};

LUA_REGISTER_CLASS(RageFile)
/** @brief Utilities for working with RageFiles. */
namespace RageFileUtil {
int CreateRageFile(lua_State* L) {
  RageFile* file = new RageFile;
  file->PushSelf(L);
  return 1;
}
const luaL_Reg RageFileUtilTable[] = {
    LIST_METHOD(CreateRageFile), {nullptr, nullptr}};
LUA_REGISTER_NAMESPACE(RageFileUtil);
}  // namespace RageFileUtil

/*
 * Copyright (c) 2003-2004 Glenn Maynard, Chris Danford
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
