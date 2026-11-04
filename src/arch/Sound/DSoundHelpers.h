#ifndef DSOUND_HELPERS
#define DSOUND_HELPERS 1

#include <cstdint>
#include <string>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <wtypes.h>
// clang-format on
#endif

struct IDirectSound;
struct IDirectSoundBuffer;

class DSound {
 public:
  IDirectSound* GetDS() const { return ds_; }
  bool IsEmulated() const;

  DSound();
  ~DSound();
  std::string Init();

 private:
  IDirectSound* ds_;
  static BOOL CALLBACK EnumCallback(
      LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule,
      LPVOID lpContext);

  void SetPrimaryBufferMode();
};

class DSoundBuf {
 public:
  enum hw { HW_HARDWARE, HW_SOFTWARE, HW_DONT_CARE };

  /* If samplerate is DYNAMIC_SAMPLERATE, then call SetSampleRate before
   * you use the sample. */
  enum { DYNAMIC_SAMPLERATE = -1 };

  DSoundBuf();
  std::string Init(
      DSound& ds, hw hardware, int iChannels, int iSampleRate, int iSampleBits,
      int iWriteAhead);

  bool get_output_buf(char** pBuffer, unsigned* iBuffersize, int iChunksize);
  void release_output_buf(char* pBuffer, unsigned iBuffersize);

  void Play();
  void Stop();
  void SetVolume(float fVolume);
  void SetSampleRate(int iRate);
  int GetSampleRate() const { return sample_rate_; }

  ~DSoundBuf();
  int64_t GetPosition() const;
  int64_t GetOutputPosition() const { return write_cursor_pos_; }

 private:
  int buffersize_frames() const { return buffer_size_ / bytes_per_frame(); }
  int bytes_per_frame() const { return channels_ * sample_bits_ / 8; }

  void CheckWriteahead(int iCursorStart, int iCursorEnd);
  void CheckUnderrun(int iCursorStart, int iCursorEnd);

  IDirectSoundBuffer* buffer_;

  int channels_, sample_rate_, sample_bits_, write_ahead_;
  int volume_;

  int buffer_size_;

  int write_cursor_, buffer_bytes_filled_; /* bytes */
  int extra_writeahead_;
  int64_t write_cursor_pos_; /* frames */
  mutable int64_t last_position_;
  bool playing_;

  bool buffer_locked_;
  char *locked_buf1_, *locked_buf2_;
  int locked_size1_, locked_size2_;
  char* temp_buffer_;

  int last_cursors_[4][2];
};

#endif

/*
 * (c) 2002-2004 Glenn Maynard
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
