/* RageSoundReader_ThreadedBuffer - Buffer sounds into memory. */

#ifndef RAGE_SOUND_READER_THREADED_BUFFER
#define RAGE_SOUND_READER_THREADED_BUFFER

#include <list>
#include <string>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageThreads.h"
#include "RageUtil_CircularBuffer.h"

class RageThread;
class RageSoundReader_ThreadedBuffer : public RageSoundReader_Filter {
 public:
  RageSoundReader_ThreadedBuffer(RageSoundReader* source);
  RageSoundReader_ThreadedBuffer(const RageSoundReader_ThreadedBuffer& cpy);
  ~RageSoundReader_ThreadedBuffer();
  RageSoundReader_ThreadedBuffer* Copy() const {
    return new RageSoundReader_ThreadedBuffer(*this);
  }

  virtual int SetPosition(int frame);
  virtual int Read(float* buffer, int length);
  virtual int GetNextSourceFrame() const;

  virtual int GetLength() const;
  virtual int GetLength_Fast() const;
  virtual int GetSampleRate() const { return sample_rate_; }
  virtual unsigned GetNumChannels() const { return channels_; }
  virtual bool SetProperty(const std::string& property, float value);
  virtual float GetStreamToSourceRatio() const;
  virtual RageSoundReader* GetSource() { return nullptr; }

  /* Enable and disable threaded buffering.  Disable buffering before accessing
   * the underlying sound.  DisableBuffering returns true if buffering was
   * enabled. */
  void EnableBuffering();
  void EnableBuffering() const {
    const_cast<RageSoundReader_ThreadedBuffer*>(this)->EnableBuffering();
  }
  bool DisableBuffering();
  bool DisableBuffering() const {
    return const_cast<RageSoundReader_ThreadedBuffer*>(this)
        ->DisableBuffering();
  }

 private:
  int FillFrames(int bytes);
  int FillBlock();
  int GetFilledFrames() const;
  int GetEmptyFrames() const;
  int sample_rate_;
  int channels_;

  CircBuf<float> data_buffer_;

  struct Mapping {
    int frames_buffered_;
    int position_of_first_frame_;
    float rate_;
    Mapping() : frames_buffered_(0), position_of_first_frame_(0), rate_(1.0f) {}
  };
  std::list<Mapping> stream_position_;

  bool eof_;

  bool enabled_;

  /* If this is true, the buffering thread owns m_pSource, even
   * if m_Event is unlocked. */
  bool filling_;

  mutable RageEvent event_;

  RageThread thread_;
  bool shutdown_thread_;
  static int StartBufferingThread(void* p) {
    ((RageSoundReader_ThreadedBuffer*)p)->BufferingThread();
    return 0;
  }

  void BufferingThread();
};

#endif

/*
 * Copyright (c) 2006 Glenn Maynard
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
