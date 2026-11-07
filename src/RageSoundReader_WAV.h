/* RageSoundReader_WAV - WAV reader. */

#ifndef RAGE_SOUND_READER_WAV_H
#define RAGE_SOUND_READER_WAV_H

#include <cstdint>
#include <string>

#include "RageSoundReader_FileReader.h"

struct WavReader;

std::string ReadString(RageFileBasic& f, int size, std::string& error);

class RageSoundReader_WAV : public RageSoundReader_FileReader {
 public:
  OpenResult Open(RageFileBasic* file);
  void Close();
  int GetLength() const;
  int SetPosition(int frame);
  int Read(float* buf, int frames);
  int GetSampleRate() const { return wav_data_.sample_rate_; }
  unsigned GetNumChannels() const { return wav_data_.channels_; }
  int GetNextSourceFrame() const;
  RageSoundReader_WAV();
  ~RageSoundReader_WAV();
  RageSoundReader_WAV(const RageSoundReader_WAV&); /* not defined; don't use */
  RageSoundReader_WAV* Copy() const;

  struct WavData {
    int32_t data_chunk_pos_, data_chunk_size_, extra_fmt_pos_, sample_rate_,
        format_tag_;
    int16_t channels_, bits_per_sample_, block_align_, extra_fmt_bytes_;
  };

 private:
  WavData wav_data_;

  WavReader* impl_;
};

#endif

/*
 * (c) 2004 Glenn Maynard
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
