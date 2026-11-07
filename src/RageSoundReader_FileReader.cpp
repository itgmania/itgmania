#include "RageSoundReader_FileReader.h"

#include <set>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageSoundReader_MP3.h"
#include "RageSoundReader_Vorbisfile.h"
#include "RageSoundReader_WAV.h"
#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "StdString.h"

RageSoundReader_FileReader* RageSoundReader_FileReader::TryOpenFile(
    RageFileBasic* file, std::string& error, std::string format,
    bool& keep_trying) {
  RageSoundReader_FileReader* sample = nullptr;

  if (!CompareNoCase(format, "wav")) {
    sample = new RageSoundReader_WAV;
  }

  if (!CompareNoCase(format, "mp3")) {
    sample = new RageSoundReader_MP3;
  }

  if (!CompareNoCase(format, "oga") || !CompareNoCase(format, "ogg")) {
    sample = new RageSoundReader_Vorbisfile;
  }

  if (!sample) {
    return nullptr;
  }

  OpenResult ret = sample->Open(file);
  file = nullptr;  // Sample owns it now
  if (ret == OPEN_OK) {
    return sample;
  }

  std::string err = sample->GetError();
  delete sample;

  LOG->Trace("Format %s failed: %s", format.c_str(), err.c_str());

  /*
   * The file failed to open, or failed to read.  This indicates a problem that
   * will affect all readers, so don't waste time trying more readers.
   * (OPEN_IO_ERROR)
   *
   * Errors fall in two categories:
   * OPEN_UNKNOWN_FILE_FORMAT: Data was successfully read from the file, but
   * it's the wrong file format.  The error message always looks like "unknown
   * file format" or "Not Vorbis data"; ignore it so we always give a consistent
   * error message, and continue trying other file formats.
   *
   * OPEN_FATAL_ERROR: Either the file was opened successfully and appears to be
   * the correct format, but a fatal format-specific error was encountered that
   * will probably not be fixed by using a different reader (for example, an Ogg
   * file that doesn't actually contain any audio streams); or the file failed
   * to open or read ("I/O error", "permission denied"), in which case all other
   * readers will probably fail, too.  The returned error is used, and no other
   * formats will be tried.
   */
  keep_trying = (ret != OPEN_FATAL_ERROR);
  switch (ret) {
    case OPEN_UNKNOWN_FILE_FORMAT:
      keep_trying = true;
      error = "Unknown file format";
      break;

    case OPEN_FATAL_ERROR:
      /* The file matched, but failed to load.  We know it's this type of data;
       * don't bother trying the other file types. */
      keep_trying = false;
      error = err;
      break;
    default:
      break;
  }

  return nullptr;
}

#include "RageFileDriverMemory.h"

RageSoundReader_FileReader* RageSoundReader_FileReader::OpenFile(
    std::string filename, std::string& error, bool* prebuffer) {
  HiddenPtr<RageFileBasic> file;
  {
    RageFile* file_open = new RageFile;
    if (!file_open->Open(filename)) {
      error = file_open->GetError();
      delete file_open;
      return nullptr;
    }
    file = file_open;
  }

  if (prebuffer) {
    if (file->GetFileSize() < 1024 * 50) {
      RageFileObjMem* mem = new RageFileObjMem;
      bool ret = FileCopy(*file, *mem, error, nullptr);
      if (!ret) {
        delete mem;
        return nullptr;
      }

      file = mem;
      file->Seek(0);
      *prebuffer = true;
    } else {
      *prebuffer = false;
    }
  }
  std::set<std::string> file_types;
  const std::vector<std::string>& sound_exts =
      ActorUtil::GetTypeExtensionList(FT_Sound);
  for (std::vector<std::string>::const_iterator curr = sound_exts.begin();
       curr != sound_exts.end(); ++curr) {
    file_types.insert(*curr);
  }

  std::string format = GetExtension(filename);
  MakeLower(format);

  error = "";

  bool keep_trying = true;

  /* If the extension matches a format, try that first. */
  if (file_types.find(format) != file_types.end()) {
    RageSoundReader_FileReader* new_sample =
        TryOpenFile(file->Copy(), error, format, keep_trying);
    if (new_sample) {
      return new_sample;
    }
    file_types.erase(format);
  }

  for (std::set<std::string>::iterator it = file_types.begin();
       keep_trying && it != file_types.end(); ++it) {
    RageSoundReader_FileReader* new_sample =
        TryOpenFile(file->Copy(), error, *it, keep_trying);
    if (new_sample) {
      LOG->UserLog(
          "Sound file", file->GetDisplayPath(), "is really %s.", it->c_str());
      return new_sample;
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
