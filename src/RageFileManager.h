#ifndef RAGE_FILE_MANAGER_H
#define RAGE_FILE_MANAGER_H

#include <string>
#include <unordered_set>
#include <vector>

/** @brief Constants for working with the RageFileManager. */
namespace RageFileManagerUtil {
extern std::string g_dir_of_executable;
}

class RageFileDriver;
class RageFileBasic;
struct lua_State;

bool ilt(const std::string& a, const std::string& b);
bool ieq(const std::string& a, const std::string& b);

/** @brief File utilities and high-level manager for RageFile objects. */
class RageFileManager {
 public:
  RageFileManager(const std::string& argv0);
  ~RageFileManager();
  void MountInitialFilesystems();
  void MountUserFilesystems();

  void GetDirListing(
      const std::string& path, std::vector<std::string>& add_to, bool only_dirs,
      bool return_path_too);
  void GetDirListingWithMultipleExtensions(
      const std::string& path, const std::vector<std::string>& extension_list,
      std::vector<std::string>& add_to, bool only_dirs = false,
      bool return_path_too = false);
  bool Move(const std::string& from_path, const std::string& to_path);
  bool Copy(const std::string& from_path, const std::string& to_path);
  bool Remove(const std::string& path);
  bool DeleteRecursive(const std::string& path);
  void CreateDir(const std::string& dir);

  enum FileType { TYPE_FILE, TYPE_DIR, TYPE_NONE };
  FileType GetFileType(const std::string& path);

  bool IsAFile(const std::string& path);
  bool IsADirectory(const std::string& path);
  bool DoesFileExist(const std::string& path);

  int GetFileSizeInBytes(const std::string& path);
  int GetFileHash(const std::string& path);

  bool Mount(
      const std::string& type, const std::string& real_path,
      const std::string& mount_point);
  void Unmount(
      const std::string& type, const std::string& root,
      const std::string& mount_point);

  struct DriverLocation {
    std::string type_, root_, mount_point_;
  };
  void GetLoadedDrivers(std::vector<DriverLocation>& mounts);

  void FlushDirCache(const std::string& path = std::string());

  /* Used only by RageFile: */
  RageFileBasic* Open(const std::string& path, int mode, int& error);
  void CacheFile(const RageFileBasic* fb, const std::string& path);

  /* Retrieve or release a reference to the low-level driver for a mountpoint.
   */
  RageFileDriver* GetFileDriver(std::string mountpoint);
  void ReleaseFileDriver(RageFileDriver* driver);

  bool Unzip(const std::string& zip_path, std::string target_path, int strip);

  // path protection
  void ProtectPath(const std::string& path);
  bool IsPathProtected(const std::string& path);

  // Lua
  void PushSelf(lua_State* L);

 private:
  RageFileBasic* OpenForReading(const std::string& path, int mode, int& error);
  RageFileBasic* OpenForWriting(const std::string& path, int mode, int& error);

  std::unordered_set<std::string> protected_paths_;
};

extern RageFileManager* FILEMAN;

#endif

/*
 * Copyright (c) 2001-2004 Glenn Maynard, Chris Danford
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
