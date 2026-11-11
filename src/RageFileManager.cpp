#include "RageFileManager.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "LuaManager.h"
#include "RageFile.h"
#include "RageFileDriver.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "StdString.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(UNIX) || defined(MACOSX)
#include <paths.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <miniz.h>

RageFileManager* FILEMAN = nullptr;

/* Lock this before touching any of these globals (except FILEMAN itself). */
static RageEvent* mutex;

std::string RageFileManagerUtil::g_dir_of_executable;

struct LoadedDriver {
  /* A loaded driver may have a base path, which modifies the path we
   * pass to the driver.  For example, if the base is "Songs/", and we
   * want to send the path "Songs/Foo/Bar" to it, then we actually
   * only send "Foo/Bar".  The path "Themes/Foo" is out of the scope
   * of the driver, and GetPath returns false. */
  RageFileDriver* driver_;
  std::string type_, root_, mount_point_;

  int refs_;

  LoadedDriver() {
    driver_ = nullptr;
    refs_ = 0;
  }
  std::string GetPath(const std::string& path) const;
};

static std::vector<LoadedDriver*> g_drivers;
static std::map<const RageFileBasic*, LoadedDriver*> file_driver_map;

static void ReferenceAllDrivers(std::vector<LoadedDriver*>& driver_list) {
  mutex->Lock();
  driver_list = g_drivers;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    ++driver_list[i]->refs_;
  }
  mutex->Unlock();
}

static void UnreferenceAllDrivers(std::vector<LoadedDriver*>& driver_list) {
  mutex->Lock();
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    --driver_list[i]->refs_;
  }
  mutex->Broadcast();
  mutex->Unlock();

  /* Clear the temporary list, to make it clear that the drivers may no longer
   * be accessed. */
  driver_list.clear();
}

RageFileDriver* RageFileManager::GetFileDriver(std::string mountpoint) {
  FixSlashesInPlace(mountpoint);
  if (mountpoint.size() && Right(mountpoint, 1) != "/") {
    mountpoint += '/';
  }

  mutex->Lock();
  RageFileDriver* ret = nullptr;
  for (unsigned i = 0; i < g_drivers.size(); ++i) {
    if (g_drivers[i]->type_ == "mountpoints") {
      continue;
    }
    if (CompareNoCase(g_drivers[i]->mount_point_, mountpoint)) {
      continue;
    }

    ret = g_drivers[i]->driver_;
    ++g_drivers[i]->refs_;
    break;
  }
  mutex->Unlock();

  return ret;
}

void RageFileManager::ReleaseFileDriver(RageFileDriver* driver) {
  ASSERT(driver != nullptr);

  mutex->Lock();
  unsigned i;
  for (i = 0; i < g_drivers.size(); ++i) {
    if (g_drivers[i]->driver_ == driver) {
      break;
    }
  }
  ASSERT(i != g_drivers.size());

  --g_drivers[i]->refs_;

  mutex->Broadcast();
  mutex->Unlock();
}

size_t zipRead(void* opaque, mz_uint64 file_ofs, void* buf, size_t n) {
  RageFile* f = static_cast<RageFile*>(opaque);

  const int pos = f->Seek(file_ofs);
  if (pos >= 0 && static_cast<uint64_t>(pos) != file_ofs) {
    return 0;
  }

  return f->Read(buf, n);
}

size_t zipWriteFile(
    void* opaque, mz_uint64 file_ofs, const void* buf, size_t n) {
  RageFile* f = static_cast<RageFile*>(opaque);

  /*
   * XXX: RageFile doesn't allow to Seek() in a file open for writing. We
   * rely on the fact that miniz writes the file in order. Tell() is not
   * allowed either, so we can't even check that the current offset is
   * correct.
   */
  /*
  int pos = f->Seek(file_ofs);
  if (pos != file_ofs)
  {
          return 0;
  }
  */

  return f->Write(buf, n);
}

bool RageFileManager::Unzip(
    const std::string& zip_path, std::string target_path, int strip) {
  if (target_path.empty() || target_path.back() != '/') {
    target_path.push_back('/');
  }

  RageFile zip_file;
  if (!zip_file.Open(zip_path, RageFile::READ)) {
    std::string error = zip_file.GetError();
    LOG->Warn("Could not unzip %s: %s", zip_path.c_str(), error.c_str());
    return false;
  }

  mz_zip_archive zip = {};
  zip.m_pRead = zipRead;
  zip.m_pIO_opaque = &zip_file;

  if (!mz_zip_reader_init(&zip, zip_file.GetFileSize(), 0)) {
    LOG->Warn(
        "Could not unzip %s: %s", zip_path.c_str(),
        mz_zip_get_error_string(zip.m_last_error));
    mz_zip_reader_end(&zip);
    return false;
  }

  bool success = true;
  mz_uint file_count = mz_zip_reader_get_num_files(&zip);

  for (mz_uint file_index = 0; file_index < file_count; file_index++) {
    mz_zip_archive_file_stat info;
    if (!mz_zip_reader_file_stat(&zip, file_index, &info)) {
      LOG->Warn(
          "Could not unzip %s: %s", zip_path.c_str(),
          mz_zip_get_error_string(zip.m_last_error));
      success = false;
      break;
    }

    std::string filename(info.m_filename);
    if (filename.back() == '/') {
      filename.pop_back();
    }

    for (int i = 0; i < strip; i++) {
      size_t pos = filename.find('/');
      if (pos != std::string::npos) {
        pos++;
      }
      filename.erase(0, pos);
    }
    if (filename.empty()) {
      continue;
    }

    std::string filepath = target_path + filename;

    if (FILEMAN->IsPathProtected(filepath)) {
      LOG->Warn("Overwriting %s is not allowed", filepath.c_str());
      continue;
    }

    if (info.m_is_directory) {
      CreateDir(filepath);
    } else {
      RageFile f;
      if (!f.Open(filepath, RageFile::WRITE | RageFile::STREAMED)) {
        std::string error = zip_file.GetError();
        LOG->Warn("Could not write to %s: %s", filepath.c_str(), error.c_str());
        success = false;
        break;
      }

      success = mz_zip_reader_extract_to_callback(
          &zip, file_index, zipWriteFile, &f, 0);
      if (!success) {
        std::string error = f.GetError();
        LOG->Warn("Could not write to %s: %s", filepath.c_str(), error.c_str());
        FILEMAN->Remove(filepath);
        break;
      }
    }
  }

  mz_zip_reader_end(&zip);
  return success;
}

static void NormalizePath(std::string& path) {
  FixSlashesInPlace(path);
  CollapsePath(path, true);
  if (path.size() == 0) {
    path = '/';
  } else if (path[0] != '/') {
    path = '/' + path;
  }
}

void RageFileManager::ProtectPath(const std::string& path) {
  std::string normalized_path(path);
  NormalizePath(normalized_path);
  MakeLower(normalized_path);

  protected_paths_.insert(normalized_path);
}

bool RageFileManager::IsPathProtected(const std::string& path) {
  std::string normalized_path(path);
  NormalizePath(normalized_path);
  MakeLower(normalized_path);

  return protected_paths_.count(normalized_path) > 0;
}

// Mountpoints as directories cause a problem.  If "Themes/default" is a
// mountpoint, and doesn't exist anywhere else, then GetDirListing("Themes/*")
// must return "default".  The driver containing "Themes/default" won't do this;
// its world view begins at "BGAnimations" (inside "Themes/default").  We need a
// dummy driver that handles mountpoints. */
class RageFileDriverMountpoints : public RageFileDriver {
 public:
  RageFileDriverMountpoints() : RageFileDriver(new FilenameDB) {}
  RageFileBasic* Open(const std::string& path, int mode, int& error) {
    error = (mode == RageFile::WRITE) ? ERROR_WRITING_NOT_SUPPORTED : ENOENT;
    return nullptr;
  }
  /* Never flush FDB, except in LoadFromDrivers. */
  void FlushDirCache(const std::string& path) {}

  void LoadFromDrivers(const std::vector<LoadedDriver*>& drivers) {
    /* XXX: Even though these two operations lock on their own, lock around
     * them, too.  That way, nothing can sneak in and get incorrect
     * results between the flush and the re-population. */
    fdb_->FlushDirCache();
    for (unsigned i = 0; i < drivers.size(); ++i) {
      if (drivers[i]->mount_point_ != "/") {
        fdb_->AddFile(drivers[i]->mount_point_, 0, 0);
      }
    }
  }
};
static RageFileDriverMountpoints* mountpoints = nullptr;

static std::string ExtractDirectory(std::string path) {
  // return the directory containing sPath
  size_t n = path.find_last_of("/");
  if (n != path.npos) {
    path.erase(n);
  } else {
    path.erase();
  }
  return path;
}

#if defined(UNIX) || defined(MACOSX)
static std::string ReadlinkRecursive(std::string path) {
  // unices support symbolic links; dereference them
  std::string dereferenced = path;
  do {
    path = dereferenced;
    char deref_path[512];
    ssize_t link_size = readlink(path.c_str(), deref_path, sizeof(deref_path));
    if (link_size != -1 && link_size != sizeof(deref_path)) {
      dereferenced = std::string(deref_path, link_size);
      if (deref_path[0] != '/') {
        // relative link
        dereferenced = std::string(ExtractDirectory(path) + "/" + dereferenced);
      }
    }
  } while (path != dereferenced);

  return path;
}
#endif

static std::string GetDirOfExecutable(std::string argv0) {
  // argv[0] can be wrong in most OS's; try to avoid using it.

  std::string s_path;
#if defined(_WIN32)
  char buf[MAX_PATH];
  GetModuleFileName(nullptr, buf, sizeof(buf));
  s_path = buf;
#else
  s_path = argv0;
#endif

  Replace(s_path, "\\", "/");

  bool is_absolute_path = false;
  if (s_path.size() == 0 || s_path[0] == '/') {
    is_absolute_path = true;
  }
#if defined(_WIN32)
  if (s_path.size() > 2 && s_path[1] == ':' && s_path[2] == '/') {
    is_absolute_path = true;
  }
#endif

  // strip off executable name
  s_path = ExtractDirectory(s_path);

  if (!is_absolute_path) {
#if defined(UNIX) || defined(MACOSX)
    if (s_path.empty()) {
      // This is in our path so look for it.
      const char* path = getenv("PATH");

      if (!path) {
        path = _PATH_DEFPATH;
      }

      std::vector<std::string> v_path;
      split(path, ":", v_path);
      for (std::string& i : v_path) {
        if (access((i + "/" + argv0).c_str(), X_OK | R_OK)) {
          continue;
        }
        s_path = ExtractDirectory(ReadlinkRecursive(i + "/" + argv0));
        break;
      }
      if (s_path.empty()) {
        s_path = GetCwd();            // What?
      } else if (s_path[0] != '/') {  // For example, if . is in $PATH.
        s_path = GetCwd() + "/" + s_path;
      }

    } else {
      s_path = ExtractDirectory(ReadlinkRecursive(GetCwd() + "/" + argv0));
    }
#else
    s_path = GetCwd() + "/" + s_path;
    Replace(s_path, "\\", "/");
#endif
  }
  return s_path;
}

static void ChangeToDirOfExecutable(const std::string& argv0) {
  RageFileManagerUtil::g_dir_of_executable = GetDirOfExecutable(argv0);

  /* Set the CWD.  Any effects of this is platform-specific; most files are read
   * and written through RageFile.  See also RageFileManager::RageFileManager.
   */
#if defined(_WINDOWS)
  if (_chdir((RageFileManagerUtil::g_dir_of_executable + "/..").c_str()))
#elif defined(UNIX)
  if (chdir((RageFileManagerUtil::g_dir_of_executable + "/").c_str()))
#elif defined(MACOSX)
  /* If the basename is not MacOS, then we've likely been launched via the
   * command line through a symlink. Assume this is the case and change to the
   * dir of the symlink. */
  if (Basename(RageFileManagerUtil::g_dir_of_executable) == "MacOS") {
    CollapsePath(RageFileManagerUtil::g_dir_of_executable += "/../../../");
  }
  if (chdir(RageFileManagerUtil::g_dir_of_executable.c_str()))
#endif
  {
    LOG->Warn(
        "Can't set current working directory to %s",
        RageFileManagerUtil::g_dir_of_executable.c_str());
    return;
  }
}

RageFileManager::RageFileManager(const std::string& argv0) {
  CHECKPOINT_M(argv0);
  ChangeToDirOfExecutable(argv0);

  mutex = new RageEvent("RageFileManager");

  mountpoints = new RageFileDriverMountpoints;
  LoadedDriver* loaded_driver = new LoadedDriver;
  loaded_driver->driver_ = mountpoints;
  loaded_driver->mount_point_ = "/";
  loaded_driver->type_ = "mountpoints";
  g_drivers.push_back(loaded_driver);

  /* The mount path is unused, but must be nonempty. */
  RageFileManager::Mount("mem", "(cache)", "/@mem");

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "FILEMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

void RageFileManager::MountInitialFilesystems() {
  HOOKS->MountInitialFilesystems(RageFileManagerUtil::g_dir_of_executable);
}

void RageFileManager::MountUserFilesystems() {
  HOOKS->MountUserFilesystems(RageFileManagerUtil::g_dir_of_executable);
}

RageFileManager::~RageFileManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("FILEMAN");

  /* Note that drivers can use previously-loaded drivers, eg. to load a ZIP
   * from the FS.  Unload drivers in reverse order. */
  for (int i = g_drivers.size() - 1; i >= 0; --i) {
    delete g_drivers[i]->driver_;
    delete g_drivers[i];
  }
  g_drivers.clear();

  //	delete g_Mountpoints; // g_Mountpoints was in g_pDrivers
  mountpoints = nullptr;

  delete mutex;
  mutex = nullptr;
}

/* path must be normalized (FixSlashesInPlace, CollapsePath). */
std::string LoadedDriver::GetPath(const std::string& path) const {
  /* If the path begins with /@, only match mountpoints that begin with /@. */
  if (path.size() >= 2 && path[1] == '@') {
    if (mount_point_.size() < 2 || mount_point_[1] != '@') {
      return std::string();
    }
  }

  if (CompareNoCase(Left(path, mount_point_.size()), mount_point_)) {
    return std::string(); /* no match */
  }

  /* Add one, so we don't cut off the leading slash. */
  std::string ret = Right(path, path.size() - mount_point_.size() + 1);
  return ret;
}

bool ilt(const std::string& a, const std::string& b) {
  return CompareNoCase(a, b) < 0;
}
bool ieq(const std::string& a, const std::string& b) {
  return CompareNoCase(a, b) == 0;
}
void RageFileManager::GetDirListing(
    const std::string& raw_path, std::vector<std::string>& add_to,
    bool only_dirs, bool return_path_too) {
  std::string path = raw_path;
  NormalizePath(path);

  // NormalizePath() calls CollapsePath() which will remove "dir/.." pairs.
  // So if a "/.." is still present, they're trying to go below the root,
  // which isn't valid.
  if (path.find("/..") != std::string::npos) {
    return;
  }

  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  int drivers_that_returned_files = 0;
  int old_size = add_to.size();
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    LoadedDriver* loaded_driver = driver_list[i];
    const std::string p = loaded_driver->GetPath(path);
    if (p.size() == 0) {
      continue;
    }

    const unsigned old_start = add_to.size();

    loaded_driver->driver_->GetDirListing(
        p, add_to, only_dirs, return_path_too);
    if (add_to.size() != old_start) {
      ++drivers_that_returned_files;
    }

    /* If returning the path, prepend the mountpoint name to the files this
     * driver returned. */
    if (return_path_too && loaded_driver->mount_point_.size() > 0) {
      const std::string& mount_point = loaded_driver->mount_point_;
      /* Skip the trailing slash on the mountpoint; there's already a slash
       * there. */
      const std::string& trim_point =
          mount_point.substr(0, mount_point.size() - 1);
      for (unsigned j = old_start; j < add_to.size(); ++j) {
        add_to[j] = trim_point + add_to[j];
      }
    }
  }

  UnreferenceAllDrivers(driver_list);

  // Remove files that start with ._ from the list because these are special
  // macOS files that cause interference on other platforms. -Kyz
  StripMacResourceForks(add_to);

  if (drivers_that_returned_files > 1) {
    /* More than one driver returned files.  Remove duplicates
     * (case-insensitively). */
    sort(add_to.begin() + old_size, add_to.end(), ilt);
    std::vector<std::string>::iterator it =
        unique(add_to.begin() + old_size, add_to.end(), ieq);
    add_to.erase(it, add_to.end());
  }
}

void RageFileManager::GetDirListingWithMultipleExtensions(
    const std::string& path, const std::vector<std::string>& extension_list,
    std::vector<std::string>& add_to, bool only_dirs, bool return_path_too) {
  for (std::vector<std::string>::const_iterator curr_ext =
           extension_list.begin();
       curr_ext != extension_list.end(); ++curr_ext) {
    GetDirListing(
        path + "*." + (*curr_ext), add_to, only_dirs, return_path_too);
  }
}

/* Files may only be moved within the same file driver. */
bool RageFileManager::Move(
    const std::string& raw_from_path, const std::string& raw_to_path) {
  std::string from_path = raw_from_path;
  std::string to_path = raw_to_path;

  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  NormalizePath(from_path);
  NormalizePath(to_path);

  /* Multiple drivers may have the same file. */
  bool deleted = false;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    const std::string old_driver_path = driver_list[i]->GetPath(from_path);
    const std::string new_driver_path = driver_list[i]->GetPath(to_path);
    if (old_driver_path.size() == 0 || new_driver_path.size() == 0) {
      continue;
    }

    bool ret = driver_list[i]->driver_->Move(old_driver_path, new_driver_path);
    if (ret) {
      deleted = true;
    }
  }

  UnreferenceAllDrivers(driver_list);

  return deleted;
}

bool RageFileManager::Copy(
    const std::string& from_path, const std::string& to_path) {
  RageFile from_file, to_file;

  if (!from_file.Open(from_path, RageFile::READ)) {
    return false;
  }

  if (!to_file.Open(to_path, RageFile::WRITE)) {
    return false;
  }

  char buf[4096];
  for (;;) {
    int read_bytes = from_file.Read(buf, sizeof(buf));
    if (read_bytes <= 0) {
      break;
    }

    int written_bytes = to_file.Write(buf, read_bytes);
    if (written_bytes != read_bytes) {
      break;
    }
  }

  if (!from_file.GetError().empty() || !to_file.GetError().empty()) {
    return false;
  }

  return true;
}

bool RageFileManager::Remove(const std::string& raw_path) {
  std::string path = raw_path;

  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  NormalizePath(path);

  /* Multiple drivers may have the same file. */
  bool deleted = false;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    const std::string p = driver_list[i]->GetPath(path);
    if (p.size() == 0) {
      continue;
    }

    bool ret = driver_list[i]->driver_->Remove(p);
    if (ret) {
      deleted = true;
    }
  }

  UnreferenceAllDrivers(driver_list);

  return deleted;
}

bool RageFileManager::DeleteRecursive(const std::string& path) {
  // On some OS's, non-empty directories cannot be deleted.
  // This is a work-around that can delete both files and non-empty directories
  return ::DeleteRecursive(path);
}

void RageFileManager::CreateDir(const std::string& dir) {
  if (DoesFileExist(dir)) {
    return;
  }
  std::string temp_file = dir + "newdir.temp.newdir";
  RageFile f;
  f.Open(temp_file, RageFile::WRITE);
  f.Close();

  Remove(temp_file);
}

static void AdjustMountpoint(std::string& mount_point) {
  FixSlashesInPlace(mount_point);

  ASSERT_M(
      Left(mount_point, 1) == "/",
      "Mountpoints must be absolute: " + mount_point);

  if (mount_point.size() && Right(mount_point, 1) != "/") {
    mount_point += '/';
  }

  if (Left(mount_point, 1) != "/") {
    mount_point = "/" + mount_point;
  }
}

static void AddFilesystemDriver(LoadedDriver* loaded_driver) {
  mutex->Lock();
  g_drivers.insert(g_drivers.begin(), loaded_driver);
  mountpoints->LoadFromDrivers(g_drivers);
  mutex->Unlock();
}

bool RageFileManager::Mount(
    const std::string& type, const std::string& raw_root,
    const std::string& raw_mount_point) {
  std::string root = raw_root;
  std::string mount_point = raw_mount_point;

  FixSlashesInPlace(root);
  AdjustMountpoint(mount_point);

  ASSERT(!root.empty());

  const std::string& paths = ssprintf(
      "\"%s\", \"%s\", \"%s\"", type.c_str(), root.c_str(),
      mount_point.c_str());
  CHECKPOINT_M(paths);
#if defined(DEBUG)
  puts(paths.c_str());
#endif

  // Unmount anything that was previously mounted here.
  Unmount(type, root, mount_point);

  CHECKPOINT_M(ssprintf(
      "About to make a driver with \"%s\", \"%s\"", type.c_str(),
      root.c_str()));
  RageFileDriver* driver = MakeFileDriver(type, root);
  if (driver == nullptr) {
    CHECKPOINT_M(ssprintf(
        "Can't mount unknown VFS type \"%s\", root \"%s\"", type.c_str(),
        root.c_str()));

    if (LOG) {
      LOG->Warn(
          "Can't mount unknown VFS type \"%s\", root \"%s\"", type.c_str(),
          root.c_str());
    } else {
      fprintf(
          stderr, "Can't mount unknown VFS type \"%s\", root \"%s\"\n",
          type.c_str(), root.c_str());
    }
    return false;
  }

  CHECKPOINT_M(ssprintf("Driver %s successfully made.", type.c_str()));

  LoadedDriver* loaded_driver = new LoadedDriver;
  loaded_driver->driver_ = driver;
  loaded_driver->type_ = type;
  loaded_driver->root_ = root;
  loaded_driver->mount_point_ = mount_point;

  AddFilesystemDriver(loaded_driver);
  return true;
}

void RageFileManager::Unmount(
    const std::string& type, const std::string& raw_root,
    const std::string& raw_mount_point) {
  std::string root = raw_root;
  std::string mount_point = raw_mount_point;

  FixSlashesInPlace(root);
  FixSlashesInPlace(mount_point);

  if (mount_point.size() && Right(mount_point, 1) != "/") {
    mount_point += '/';
  }

  /* Find all drivers we want to delete.  Remove them from g_pDrivers, and move
   * them into aDriverListToUnmount. */
  std::vector<LoadedDriver*> driver_list_to_unmount;
  mutex->Lock();
  for (unsigned i = 0; i < g_drivers.size(); ++i) {
    if (!type.empty() && CompareNoCase(g_drivers[i]->type_, type)) {
      continue;
    }
    if (!root.empty() && CompareNoCase(g_drivers[i]->root_, root)) {
      continue;
    }
    if (!mount_point.empty() &&
        CompareNoCase(g_drivers[i]->mount_point_, mount_point)) {
      continue;
    }

    ++g_drivers[i]->refs_;
    driver_list_to_unmount.push_back(g_drivers[i]);
    g_drivers.erase(g_drivers.begin() + i);
    --i;
  }

  mountpoints->LoadFromDrivers(g_drivers);

  mutex->Unlock();

  /* Now we have a list of drivers to remove. */
  while (driver_list_to_unmount.size()) {
    /* If the driver has more than one reference, somebody other than us is
     * using it; wait for that operation to complete. Note that two Unmount()
     * calls that want to remove the same mountpoint will deadlock here. */
    mutex->Lock();
    while (driver_list_to_unmount[0]->refs_ > 1) {
      mutex->Wait();
    }
    mutex->Unlock();

    delete driver_list_to_unmount[0]->driver_;
    delete driver_list_to_unmount[0];
    driver_list_to_unmount.erase(driver_list_to_unmount.begin());
  }
}

void RageFileManager::GetLoadedDrivers(std::vector<DriverLocation>& mounts) {
  LockMut(*mutex);

  for (unsigned i = 0; i < g_drivers.size(); ++i) {
    DriverLocation l;
    l.mount_point_ = g_drivers[i]->mount_point_;
    l.type_ = g_drivers[i]->type_;
    l.root_ = g_drivers[i]->root_;
    mounts.push_back(l);
  }
}

void RageFileManager::FlushDirCache(const std::string& raw_path) {
  std::string path = raw_path;

  LockMut(*mutex);

  if (path == "") {
    for (unsigned i = 0; i < g_drivers.size(); ++i) {
      g_drivers[i]->driver_->FlushDirCache("");
    }
    return;
  }

  /* Flush a specific path. */
  NormalizePath(path);
  for (unsigned i = 0; i < g_drivers.size(); ++i) {
    const std::string& driver_path = g_drivers[i]->GetPath(path);
    if (driver_path.size() == 0) {
      continue;
    }
    g_drivers[i]->driver_->FlushDirCache(driver_path);
  }
}

RageFileManager::FileType RageFileManager::GetFileType(
    const std::string& raw_path) {
  std::string path = raw_path;

  NormalizePath(path);

  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  RageFileManager::FileType ret = TYPE_NONE;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    const std::string p = driver_list[i]->GetPath(path);
    if (p.size() == 0) {
      continue;
    }
    ret = driver_list[i]->driver_->GetFileType(p);
    if (ret != TYPE_NONE) {
      break;
    }
  }

  UnreferenceAllDrivers(driver_list);

  return ret;
}

int RageFileManager::GetFileSizeInBytes(const std::string& raw_path) {
  std::string path = raw_path;

  NormalizePath(path);

  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  int ret = -1;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    const std::string p = driver_list[i]->GetPath(path);
    if (p.size() == 0) {
      continue;
    }
    ret = driver_list[i]->driver_->GetFileSizeInBytes(p);
    if (ret != -1) {
      break;
    }
  }
  UnreferenceAllDrivers(driver_list);

  return ret;
}

int RageFileManager::GetFileHash(const std::string& raw_path) {
  std::string path = raw_path;

  NormalizePath(path);

  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  int ret = -1;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    const std::string p = driver_list[i]->GetPath(path);
    if (p.size() == 0) {
      continue;
    }
    ret = driver_list[i]->driver_->GetFileHash(p);
    if (ret != -1) {
      break;
    }
  }
  UnreferenceAllDrivers(driver_list);

  return ret;
}

static bool SortBySecond(
    const std::pair<int, int>& a, const std::pair<int, int>& b) {
  return a.second < b.second;
}

/*
 * Return true if the given path should use slow, reliable writes.
 *
 * I haven't decided if it's better to do this here, or to specify SLOW_FLUSH
 * manually each place we want it.  This seems more reliable (we might forget
 * somewhere and not notice), and easier (don't have to pass flags down to
 * IniFile::Write, etc).
 */
static bool PathUsesSlowFlush(const std::string& path) {
  static const char* flush_paths[] = {"/Save/", "Save/"};

  for (unsigned i = 0; i < ARRAYLEN(flush_paths); ++i) {
    if (!strncmp(path.c_str(), flush_paths[i], strlen(flush_paths[i]))) {
      return true;
    }
  }
  return false;
}

/* Used only by RageFile: */
RageFileBasic* RageFileManager::Open(
    const std::string& raw_path, int mode, int& err) {
  std::string path = raw_path;

  err = ENOENT;

  if ((mode & RageFile::WRITE) && PathUsesSlowFlush(path)) {
    mode |= RageFile::SLOW_FLUSH;
  }

  NormalizePath(path);

  /* If writing, we need to do a heuristic to figure out which driver to write
   * with--there may be several that will work. */
  if (mode & RageFile::WRITE) {
    return OpenForWriting(path, mode, err);
  } else {
    return OpenForReading(path, mode, err);
  }
}

void RageFileManager::CacheFile(
    const RageFileBasic* fb, const std::string& raw_path) {
  mutex->Lock();
  std::map<const RageFileBasic*, LoadedDriver*>::iterator it =
      file_driver_map.find(fb);

  ASSERT_M(
      it != file_driver_map.end(),
      ssprintf("No recorded driver for file: %s", raw_path.c_str()));

  std::string path = raw_path;
  NormalizePath(path);
  path = it->second->GetPath(path);
  it->second->driver_->fdb_->CacheFile(path);
  file_driver_map.erase(it);
  mutex->Unlock();
}

RageFileBasic* RageFileManager::OpenForReading(
    const std::string& path, int mode, int& err) {
  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  for (unsigned i = 0; i < driver_list.size(); ++i) {
    LoadedDriver& ld = *driver_list[i];
    const std::string driver_path = ld.GetPath(path);
    if (driver_path.size() == 0) {
      continue;
    }
    int error;
    RageFileBasic* ret = ld.driver_->Open(driver_path, mode, error);
    if (ret) {
      UnreferenceAllDrivers(driver_list);
      return ret;
    }

    /* ENOENT (File not found) is low-priority: if some other error
     was reported, return that instead. */
    if (error != ENOENT) {
      err = error;
    }
  }
  UnreferenceAllDrivers(driver_list);

  return nullptr;
}

RageFileBasic* RageFileManager::OpenForWriting(
    const std::string& s_path, int mode, int& error) {
  /*
   * The value for a driver to open a file is the number of directories and/or
   * files that would have to be created in order to write it, or 0 if the file
   * already exists. For example, if we're opening "foo/bar/baz.txt", and only
   * "foo/" exists in a driver, we'd have to create the "bar" directory and the
   * "baz.txt" file, so the value is 2.  If "foo/bar/" exists, we'd only have to
   * create the file, so the value is 1.  Create the file with the driver that
   * returns the lowest value; in case of a tie, earliest-loaded driver wins.
   *
   * The purpose of this is to create files in the expected place.  For example,
   * if we have both C:/games/StepMania and C:/games/DWI loaded, and we're
   * writing "Songs/Music/Waltz/waltz.ssc", and the song was loaded out of
   * "C:/games/DWI/Songs/Music/Waltz/waltz.dwi", we want to write the new SSC
   * into the same directory (if possible).  Don't split up files in the same
   * directory any more than we have to.
   *
   * If the given path can not be created, return -1.  This happens if a path
   * that needs to be a directory is a file, or vice versa.
   */
  std::vector<LoadedDriver*> driver_list;
  ReferenceAllDrivers(driver_list);

  std::vector<std::pair<int, int>> values;
  for (unsigned i = 0; i < driver_list.size(); ++i) {
    LoadedDriver& ld = *driver_list[i];
    const std::string path = ld.GetPath(s_path);
    if (path.size() == 0) {
      continue;
    }

    const int value = ld.driver_->GetPathValue(path);
    if (value == -1) {
      continue;
    }

    values.push_back(std::make_pair(i, value));
  }

  stable_sort(values.begin(), values.end(), SortBySecond);

  /* Only write files if they'll be read.  If a file exists in any driver, don't
   * create or write files in any driver mounted after it, because when we later
   * try to read it, we'll get that file and not the one we wrote. */
  int maximum_driver = driver_list.size();
  if (values.size() > 0 && values[0].second == 0) {
    maximum_driver = values[0].first;
  }

  error = 0;
  for (unsigned i = 0; i < values.size(); ++i) {
    const int driver = values[i].first;
    if (driver > maximum_driver) {
      continue;
    }
    LoadedDriver& ld = *driver_list[driver];
    const std::string driver_path = ld.GetPath(s_path);
    ASSERT(!driver_path.empty());

    int this_error;
    RageFileBasic* ret = ld.driver_->Open(driver_path, mode, this_error);
    if (ret) {
      mutex->Lock();
      file_driver_map[ret] = &ld;
      mutex->Unlock();
      UnreferenceAllDrivers(driver_list);
      return ret;
    }

    /* The drivers are in order of priority; if they all return error, return
     * the first.  Never return ERROR_WRITING_NOT_SUPPORTED. */
    if (!error && this_error != RageFileDriver::ERROR_WRITING_NOT_SUPPORTED) {
      error = this_error;
    }
  }

  if (!error) {
    error = EEXIST; /* no driver could write */
  }

  UnreferenceAllDrivers(driver_list);

  return nullptr;
}

bool RageFileManager::IsAFile(const std::string& path) {
  return GetFileType(path) == TYPE_FILE;
}
bool RageFileManager::IsADirectory(const std::string& path) {
  return GetFileType(path) == TYPE_DIR;
}
bool RageFileManager::DoesFileExist(const std::string& path) {
  return GetFileType(path) != TYPE_NONE;
}

bool DoesFileExist(const std::string& path) {
  return FILEMAN->DoesFileExist(path);
}

bool IsAFile(const std::string& path) { return FILEMAN->IsAFile(path); }

bool IsADirectory(const std::string& path) {
  return FILEMAN->IsADirectory(path);
}

void GetDirListing(
    const std::string& path, std::vector<std::string>& add_to, bool only_dirs,
    bool return_path_too) {
  FILEMAN->GetDirListing(path, add_to, only_dirs, return_path_too);
}

void GetDirListingRecursive(
    const std::string& dir, const std::string& match,
    std::vector<std::string>& add_to) {
  ASSERT(Right(dir, 1) == "/");
  std::vector<std::string> files;
  GetDirListing(dir + match, files, false, true);
  std::vector<std::string> dirs;
  GetDirListing(dir + "*", dirs, true, true);
  for (int i = 0; i < (int)dirs.size(); i++) {
    GetDirListing(dirs[i] + "/" + match, files, false, true);
    GetDirListing(dirs[i] + "/*", dirs, true, true);
    dirs.erase(dirs.begin() + i);
    i--;
  }
  for (int i = files.size() - 1; i >= 0; i--) {
    if (!IsADirectory(files[i])) {
      add_to.push_back(files[i]);
    }
  }
}

bool DeleteRecursive(const std::string& dir) {
  ASSERT(Right(dir, 1) == "/");

  std::vector<std::string> files;
  GetDirListing(dir + "*", files, false, true);
  for (const std::string& s : files) {
    if (IsADirectory(s)) {
      DeleteRecursive(s + "/");
    } else {
      FILEMAN->Remove(s);
    }
  }

  return FILEMAN->Remove(dir);
}

unsigned int GetHashForFile(const std::string& path) {
  return FILEMAN->GetFileHash(path);
}

unsigned int GetHashForDirectory(const std::string& dir) {
  unsigned int hash = 0;

  hash += GetHashForString(dir);

  std::vector<std::string> array_files;
  GetDirListing(dir + "*", array_files, false);
  for (unsigned i = 0; i < array_files.size(); i++) {
    const std::string file_path = dir + array_files[i];
    hash += GetHashForFile(file_path);
  }

  return hash;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the RageFileManager. */
class LunaRageFileManager : public Luna<RageFileManager> {
 public:
  static int Copy(T* p, lua_State* L) {
    const std::string from_path = SArg(1);
    const std::string to_path = SArg(2);

    if (p->IsPathProtected(to_path)) {
      LOG->Warn("Overwriting %s is not allowed", to_path.c_str());
      lua_pushboolean(L, false);
      return 1;
    }

    lua_pushboolean(L, p->Copy(from_path, to_path));
    return 1;
  }
  static int DoesFileExist(T* p, lua_State* L) {
    lua_pushboolean(L, p->DoesFileExist(SArg(1)));
    return 1;
  }
  static int GetFileSizeBytes(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetFileSizeInBytes(SArg(1)));
    return 1;
  }
  static int GetHashForFile(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetFileHash(SArg(1)));
    return 1;
  }
  static int GetDirListing(T* p, lua_State* L) {
    std::vector<std::string> dirs;
    bool only_dirs = false;
    bool return_path_too = false;

    // the last two arguments of GetDirListing are optional;
    // let's reflect that in the Lua too. -aj
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      only_dirs = BArg(2);
      if (!lua_isnil(L, 3)) {
        return_path_too = BArg(3);
      }
    }
    //( Path, addTo, OnlyDirs=false, ReturnPathToo=false );
    p->GetDirListing(SArg(1), dirs, only_dirs, return_path_too);
    LuaHelpers::CreateTableFromArray(dirs, L);
    return 1;
  }
  static int Unzip(T* p, lua_State* L) {
    std::string zip_path = SArg(1);
    std::string target_path = SArg(2);

    int strip = 0;
    if (lua_gettop(L) >= 3) {
      strip = IArg(3);
    }

    bool success = p->Unzip(zip_path, target_path, strip);

    lua_pushboolean(L, success);
    return 1;
  }

  LunaRageFileManager() {
    ADD_METHOD(Copy);
    ADD_METHOD(DoesFileExist);
    ADD_METHOD(GetFileSizeBytes);
    ADD_METHOD(GetHashForFile);
    ADD_METHOD(GetDirListing);
    ADD_METHOD(Unzip);
  }
};

LUA_REGISTER_CLASS(RageFileManager)
// lua end

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
