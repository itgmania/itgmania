#include "RageFileDriver.h"

#include <string>
#include <vector>

#include "RageFileManager.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "StdString.h"

RageFileDriver::~RageFileDriver() { delete fdb_; }

int RageFileDriver::GetPathValue(const std::string& path) {
  std::vector<std::string> parts;
  split(path, "/", parts, true);

  std::string partial_path;

  for (unsigned i = 0; i < parts.size(); ++i) {
    partial_path += parts[i];
    if (i + 1 < parts.size()) {
      partial_path += "/";
    }

    const RageFileManager::FileType type = GetFileType(partial_path);
    switch (type) {
      case RageFileManager::TYPE_NONE:
        return parts.size() - i;

      /* If this is the last part (the whole path), it needs to be a file;
       * otherwise a directory. */
      case RageFileManager::TYPE_FILE:
        if (i != parts.size() - 1) {
          return -1;
        }
        break;
      case RageFileManager::TYPE_DIR:
        if (i == parts.size() - 1) {
          return -1;
        }
        break;
    }
  }

  return 0;
}

void RageFileDriver::GetDirListing(
    const std::string& path, std::vector<std::string>& add_to, bool only_dirs,
    bool return_path_too) {
  fdb_->GetDirListing(path, add_to, only_dirs, return_path_too);
}

RageFileManager::FileType RageFileDriver::GetFileType(const std::string& path) {
  return fdb_->GetFileType(path);
}

int RageFileDriver::GetFileSizeInBytes(const std::string& path) {
  return fdb_->GetFileSize(path);
}

int RageFileDriver::GetFileHash(const std::string& path) {
  return fdb_->GetFileHash(path);
}

void RageFileDriver::FlushDirCache(const std::string& path) {
  fdb_->FlushDirCache(path);
}

const struct FileDriverEntry* g_pFileDriverList = nullptr;

FileDriverEntry::FileDriverEntry(const std::string& type) {
  link_ = g_pFileDriverList;
  g_pFileDriverList = this;
  type_ = type;
}

FileDriverEntry::~FileDriverEntry() {
  g_pFileDriverList = nullptr; /* invalidate */
}

RageFileDriver* MakeFileDriver(
    const std::string& type, const std::string& root) {
  for (const FileDriverEntry* p = g_pFileDriverList; p; p = p->link_) {
    if (!CompareNoCase(p->type_, type)) {
      return p->Create(root);
    }
  }
  return nullptr;
}

/*
 * Copyright (c) 2003-2004 Glenn Maynard
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
