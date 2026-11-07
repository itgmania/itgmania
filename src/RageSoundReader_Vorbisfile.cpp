#include "RageSoundReader_Vorbisfile.h"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <string>

#include "RageException.h"
#include "RageFileBasic.h"
#include "RageLog.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"
#include "ogg/os_types.h"
#include "vorbis/codec.h"

static size_t OggRageFile_read_func(
    void* ptr, size_t size, size_t nmemb, void* datasource) {
  RageFileBasic* f = (RageFileBasic*)datasource;
  return f->Read(ptr, size, nmemb);
}

static int OggRageFile_seek_func(
    void* datasource, ogg_int64_t offset, int whence) {
  RageFileBasic* f = (RageFileBasic*)datasource;
  return f->Seek((int)offset, whence);
}

static int OggRageFile_close_func(void* datasource) { return 0; }

static long OggRageFile_tell_func(void* datasource) {
  RageFileBasic* f = (RageFileBasic*)datasource;
  return f->Tell();
}

static std::string ov_ssprintf(int err, const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string s = vssprintf(fmt, va);
  va_end(va);

  std::string errstr;
  switch (err) {
    // OV_FALSE, OV_EOF, and OV_HOLE were added to this switch because
    // OV_EOF cases were being reported as unknown. -Kyz
    case OV_FALSE:
      errstr = "OV_FALSE";
      break;
    case OV_EOF:
      errstr = "OV_EOF";
      break;
    case OV_HOLE:
      errstr = "OV_HOLE";
      break;
    /* XXX: In the case of OV_EREAD, can we snoop at errno? */
    case OV_EREAD:
      errstr = "Read error";
      break;
    case OV_EFAULT:
      errstr = "Internal error";
      break;
    case OV_EIMPL:
      errstr = "Feature not implemented";
      break;
    case OV_EINVAL:
      errstr = "Invalid argument";
      break;
    case OV_ENOTVORBIS:
      errstr = "Not Vorbis data";
      break;
    case OV_EBADHEADER:
      errstr = "Invalid Vorbis bitstream header";
      break;
    case OV_EVERSION:
      errstr = "Vorbis version mismatch";
      break;
    case OV_ENOTAUDIO:
      errstr = "OV_ENOTAUDIO";
      break;
    case OV_EBADPACKET:
      errstr = "OV_EBADPACKET";
      break;
    case OV_EBADLINK:
      errstr = "Link corrupted";
      break;
    case OV_ENOSEEK:
      errstr = "Stream is not seekable";
      break;
    default:
      errstr = ssprintf("unknown error %i", err);
      break;
  }

  return s + ssprintf(" (%s)", errstr.c_str());
}

RageSoundReader_FileReader::OpenResult RageSoundReader_Vorbisfile::Open(
    RageFileBasic* file) {
  file_ = file;
  vf_ = new OggVorbis_File;
  memset(vf_, 0, sizeof(*vf_));

  ov_callbacks callbacks;
  callbacks.read_func = OggRageFile_read_func;
  callbacks.seek_func = OggRageFile_seek_func;
  callbacks.close_func = OggRageFile_close_func;
  callbacks.tell_func = OggRageFile_tell_func;

  int ret = ov_open_callbacks(file, vf_, nullptr, 0, callbacks);
  if (ret < 0) {
    SetError(ov_ssprintf(ret, "ov_open failed"));
    delete vf_;
    vf_ = nullptr;
    switch (ret) {
      case OV_ENOTVORBIS:
        return OPEN_UNKNOWN_FILE_FORMAT;
      default:
        return OPEN_FATAL_ERROR;
    }
  }

  eof_ = false;
  read_offset_ = (int)ov_pcm_tell(vf_);

  vorbis_info* vi = ov_info(vf_, -1);
  channels_ = vi->channels;

  return OPEN_OK;
}

int RageSoundReader_Vorbisfile::GetLength() const {
  int len = int(ov_time_total(vf_, -1) * 1000);
  if (len == OV_EINVAL) {
    RageException::Throw(
        "RageSoundReader_Vorbisfile::GetLength: ov_time_total returned "
        "OV_EINVAL.");
  }

  return len;
}

int RageSoundReader_Vorbisfile::SetPosition(int frame) {
  eof_ = false;

  const ogg_int64_t sample = ogg_int64_t(frame);

  int ret = ov_pcm_seek(vf_, sample);
  if (ret < 0) {
    /* Returns OV_EINVAL on EOF. */
    if (ret == OV_EINVAL || ret == OV_EOF) {
      eof_ = true;
      return 0;
    }
    SetError(ov_ssprintf(ret, "ogg: SetPosition failed"));
    return -1;
  }
  read_offset_ = (int)ov_pcm_tell(vf_);

  return 1;
}

int RageSoundReader_Vorbisfile::Read(float* buf, int frames) {
  int frames_read = 0;

  while (frames && !eof_) {
    const int bytes_per_frame = sizeof(float) * channels_;

    int i_frames_read = 0;

    {
      int curofs = (int)ov_pcm_tell(vf_);
      if (curofs < read_offset_) {
        /* The timestamps moved backwards.  Ignore it.  This file probably
         * won't sync correctly. */
        LOG->Trace(
            "p ahead %p %i < %i, we're ahead by %i", static_cast<void*>(this),
            curofs, read_offset_, read_offset_ - curofs);
        read_offset_ = curofs;
      } else if (curofs > read_offset_) {
        /* Our offset doesn't match.  We have a hole in the data, or corruption.
         * If we're reading with accurate syncing, insert silence to line it up.
         * That way, corruptions in the file won't casue desyncs. */

        /* In bytes: */
        int silent_frames = curofs - read_offset_;
        silent_frames = std::min(silent_frames, (int)frames);
        int silence = silent_frames * bytes_per_frame;
        CHECKPOINT_M(ssprintf(
            "p %i,%i: %i frames of silence needed", curofs, read_offset_,
            silence));

        memset(buf, 0, silence);
        i_frames_read = silent_frames;
      }
    }

    if (i_frames_read == 0) {
      int bstream;
      float** pcm;
      int ret = ov_read_float(vf_, &pcm, frames, &bstream);

      {
        vorbis_info* vi = ov_info(vf_, -1);
        ASSERT(vi != nullptr);

        if ((unsigned)vi->channels != channels_) {
          RageException::Throw(
              "File \"%s\" changes channel count from %i to %i; not supported.",
              filename_.c_str(), channels_, (int)vi->channels);
        }
      }

      if (ret == OV_HOLE) {
        continue;
      }
      if (ret == OV_EBADLINK) {
        SetError(ssprintf("Read: OV_EBADLINK"));
        return ERROR;
      }

      if (ret == 0) {
        eof_ = true;
        continue;
      }

      if (ret > 0) {
        i_frames_read = ret;

        int num_channels = channels_;
        for (int channel = 0; channel < num_channels; ++channel) {
          const float* channel_in = pcm[channel];
          float* channel_out = &buf[channel];
          for (int i = 0; i < i_frames_read; ++i) {
            *channel_out = *channel_in;
            ++channel_in;
            channel_out += num_channels;
          }
        }
      }
    }

    read_offset_ += i_frames_read;

    buf += i_frames_read * channels_;
    frames_read += i_frames_read;
    frames -= i_frames_read;
  }

  if (!frames_read) {
    return END_OF_FILE;
  }

  return frames_read;
}

int RageSoundReader_Vorbisfile::GetSampleRate() const {
  ASSERT(vf_ != nullptr);

  vorbis_info* vi = ov_info(vf_, -1);
  ASSERT(vi != nullptr);

  return vi->rate;
}

int RageSoundReader_Vorbisfile::GetNextSourceFrame() const {
  ASSERT(vf_ != nullptr);

  int frame = (int)ov_pcm_tell(vf_);
  return frame;
}

RageSoundReader_Vorbisfile::RageSoundReader_Vorbisfile() { vf_ = nullptr; }

RageSoundReader_Vorbisfile::~RageSoundReader_Vorbisfile() {
  if (vf_) {
    ov_clear(vf_);
  }
  delete vf_;
}

RageSoundReader_Vorbisfile* RageSoundReader_Vorbisfile::Copy() const {
  RageFileBasic* file = file_->Copy();
  file->Seek(0);
  RageSoundReader_Vorbisfile* ret = new RageSoundReader_Vorbisfile;

  /* If we were able to open the sound in the first place, we expect to
   * be able to reopen it. */
  if (ret->Open(file) != OPEN_OK) {
    FAIL_M(ssprintf("Copying sound failed: %s", ret->GetError().c_str()));
  }

  return ret;
}

/*
 * Copyright (c) 2003 Glenn Maynard
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
