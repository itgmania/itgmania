/*
 * Straightforward WAV reading.  This only supports 8-bit and 16-bit PCM,
 * 4-bit ADPCM with one or two channels.  No other decompressors are planned:
 * this format is only useful for fast uncompressed audio, and ADPCM is only
 * supported to retain compatibility.
 *
 * http://www.saettler.com/RIFFNEW/RIFFNEW.htm
 * http://www.kk.iij4u.or.jp/~kondo/wave/wavecomp.htm
 * http://www.sonicspot.com/guide/wavefiles.html
 */

#include "RageSoundReader_WAV.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RageFile.h"
#include "RageFileBasic.h"
#include "RageLog.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "global.h"

namespace {
/* pBuf contains iSamples 8-bit samples; convert to 16-bit.  pBuf must
 * have enough storage to hold the resulting data. */
void Convert8bitToFloat(void* buf, int samples) {
  /* Convert in reverse, so we can do it in-place. */
  const uint8_t* in = (uint8_t*)buf;
  float* out = (float*)buf;
  for (int i = samples - 1; i >= 0; --i) {
    int sample = in[i];
    sample -= 128; /* 0..255 -> -128..127 */
    out[i] = sample / 128.0f;
  }
}

/* Flip 16-bit samples if necessary.  On little-endian systems, this will
 * optimize out. */
void ConvertLittleEndian16BitToFloat(void* buf, int samples) {
  /* Convert in reverse, so we can do it in-place. */
  const int16_t* in = (int16_t*)buf;
  float* out = (float*)buf;
  for (int i = samples - 1; i >= 0; --i) {
    int16_t sample = Swap16LE(in[i]);
    out[i] = sample / 32768.0f;
  }
}

void ConvertLittleEndian24BitToFloat(void* buf, int samples) {
  /* Convert in reverse, so we can do it in-place. */
  const unsigned char* in = (unsigned char*)buf;
  float* out = (float*)buf;
  in += samples * 3;
  for (int i = samples - 1; i >= 0; --i) {
    in -= 3;

    int32_t sample = (int(in[0]) << 0) | (int(in[1]) << 8) | (int(in[2]) << 16);

    /* Sign-extend 24-bit to 32-bit: */
    if (sample & 0x800000) {
      sample |= 0xFF000000;
    }

    out[i] = sample / 8388608.0f;
  }
}

void ConvertLittleEndian32BitToFloat(void* buf, int samples) {
  /* Convert in reverse, so we can do it in-place. */
  const int32_t* in = (int32_t*)buf;
  float* out = (float*)buf;
  for (int i = samples - 1; i >= 0; --i) {
    int32_t sample = Swap32LE(in[i]);
    out[i] = sample / 2147483648.0f;
  }
}
};  // namespace

struct WavReader {
  WavReader(RageFileBasic& f, const RageSoundReader_WAV::WavData& data)
      : file_(f), wav_data_(data) {}
  virtual ~WavReader() {}
  virtual int Read(float* buf, int frames) = 0;
  virtual int GetLength() const = 0;
  virtual bool Init() = 0;
  virtual int SetPosition(int frame) = 0;
  virtual int GetNextSourceFrame() const = 0;
  std::string GetError() const { return error_; }

 protected:
  RageFileBasic& file_;
  const RageSoundReader_WAV::WavData& wav_data_;
  std::string error_;
};

struct WavReaderPCM : public WavReader {
  WavReaderPCM(RageFileBasic& f, const RageSoundReader_WAV::WavData& data)
      : WavReader(f, data) {}

  bool Init() {
    if (QuantizeUp(wav_data_.bits_per_sample_, 8) < 8 ||
        QuantizeUp(wav_data_.bits_per_sample_, 8) > 32) {
      error_ =
          ssprintf("Unsupported sample size %i", wav_data_.bits_per_sample_);
      return false;
    }

    if (wav_data_.format_tag_ == 3 && wav_data_.bits_per_sample_ != 32) {
      error_ = ssprintf(
          "Unsupported float sample size %i", wav_data_.bits_per_sample_);
      return false;
    }

    file_.Seek(wav_data_.data_chunk_pos_);
    return true;
  }

  int Read(float* buf, int frames) {
    int bytes_per_sample = QuantizeUp(wav_data_.bits_per_sample_, 8) / 8;
    int len = frames * wav_data_.channels_;
    len *= bytes_per_sample;

    const int bytes_left_in_data_chunk =
        wav_data_.data_chunk_size_ - (file_.Tell() - wav_data_.data_chunk_pos_);
    if (!bytes_left_in_data_chunk) {
      return RageSoundReader::END_OF_FILE;
    }

    len = std::min(len, bytes_left_in_data_chunk);
    int got = file_.Read(buf, len);

    int got_samples = got / bytes_per_sample;
    if (wav_data_.format_tag_ == 1) {
      switch (bytes_per_sample) {
        case 1:
          Convert8bitToFloat(buf, got_samples);
          break;
        case 2:
          ConvertLittleEndian16BitToFloat(buf, got_samples);
          break;
        case 3:
          ConvertLittleEndian24BitToFloat(buf, got_samples);
          break;
        case 4:
          ConvertLittleEndian32BitToFloat(buf, got_samples);
          /* otherwise 3; already a float */
          break;
      }
    }
    return got_samples / wav_data_.channels_;
  }

  int GetLength() const {
    const int bytes_per_sec = wav_data_.sample_rate_ * wav_data_.channels_ *
                              wav_data_.bits_per_sample_ / 8;
    int64_t ms = (int64_t(wav_data_.data_chunk_size_) * 1000) / bytes_per_sec;
    return (int)ms;
  }

  int SetPosition(int frame) {
    int byte = (int)(int64_t(frame) * wav_data_.channels_ *
                     wav_data_.bits_per_sample_ / 8);
    if (byte > wav_data_.data_chunk_size_) {
      file_.Seek(wav_data_.data_chunk_size_ + wav_data_.data_chunk_pos_);
      return 0;
    }

    file_.Seek(byte + wav_data_.data_chunk_pos_);
    return 1;
  }

  // XXX: untested
  int GetNextSourceFrame() const {
    int byte = file_.Tell() - wav_data_.data_chunk_pos_;
    int frame = byte / (wav_data_.channels_ * wav_data_.bits_per_sample_ / 8);
    return frame;
  }
};

struct WavReaderADPCM : public WavReader {
 public:
  std::vector<int16_t> coef1_, coef2_;
  int16_t frames_per_block_;
  float* buffer_;
  int buffer_avail_, buffer_used_;

  WavReaderADPCM(RageFileBasic& f, const RageSoundReader_WAV::WavData& data)
      : WavReader(f, data) {
    buffer_ = nullptr;
  }

  virtual ~WavReaderADPCM() { delete[] buffer_; }

  bool Init() {
    if (wav_data_.bits_per_sample_ != 4) {
      error_ = ssprintf(
          "Unsupported ADPCM sample size %i", wav_data_.bits_per_sample_);
      return false;
    }

    file_.Seek(wav_data_.extra_fmt_pos_);

    frames_per_block_ = FileReading::read_16_le(file_, error_);
    int16_t num_coef = FileReading::read_16_le(file_, error_);
    coef1_.resize(num_coef);
    coef2_.resize(num_coef);
    for (int i = 0; i < num_coef; ++i) {
      coef1_[i] = FileReading::read_16_le(file_, error_);
      coef2_[i] = FileReading::read_16_le(file_, error_);
    }

    if (error_.size() != 0) {
      return false;
    }

    buffer_ = new float[frames_per_block_ * wav_data_.channels_];
    buffer_avail_ = buffer_used_ = 0;

    file_.Seek(wav_data_.data_chunk_pos_);
    return true;
  }

  void SetEOF() {
    buffer_used_ = buffer_avail_ = 0;
    file_.Seek(wav_data_.data_chunk_size_ + wav_data_.data_chunk_pos_);
  }

  /* Return false on error, true on success (even if we hit EOF). */
  bool DecodeADPCMBlock() {
    ASSERT_M(buffer_used_ == buffer_avail_, ssprintf("%i", buffer_used_));

    buffer_used_ = buffer_avail_ = 0;
    error_ = "";

    if (file_.Tell() >=
            wav_data_.data_chunk_size_ + wav_data_.data_chunk_pos_ ||
        file_.AtEOF()) {
      return true; /* past the data chunk */
    }

    int8_t predictor[2];
    int16_t delta[2], samp1[2], samp2[2];
    for (int i = 0; i < wav_data_.channels_; ++i) {
      predictor[i] = FileReading::read_8(file_, error_);
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      delta[i] = FileReading::read_16_le(file_, error_);
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      samp1[i] = FileReading::read_16_le(file_, error_);
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      samp2[i] = FileReading::read_16_le(file_, error_);
    }

    if (error_.size() != 0) {
      return false;
    }

    float* buffer = buffer_;
    int coef1[2], coef2[2];
    for (int i = 0; i < wav_data_.channels_; ++i) {
      if (predictor[i] >= (int)coef1_.size()) {
        LOG->Trace(
            "%s: predictor out of range", file_.GetDisplayPath().c_str());

        /* XXX: silence this block? */
        predictor[i] = 0;
      }

      coef1[i] = coef1_[predictor[i]];
      coef2[i] = coef2_[predictor[i]];
    }

    /* We've read the block header; read the rest.  Don't read past the end of
     * the data chunk. */
    int max_size = std::min(
        (int)wav_data_.block_align_ - 7 * wav_data_.channels_,
        (wav_data_.data_chunk_size_ + wav_data_.data_chunk_pos_) -
            file_.Tell());

    char* p_buf = (char*)alloca(max_size);

    int block_size = file_.Read(p_buf, max_size);
    if (block_size == 0) {
      return true;
    }
    if (block_size == -1) {
      error_ = file_.GetError();
      return false;
    }

    for (int i = 0; i < wav_data_.channels_; ++i) {
      buffer[buffer_avail_++] = (int16_t)samp2[i] / 32768.0f;
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      buffer[buffer_avail_++] = (int16_t)samp1[i] / 32768.0f;
    }

    int8_t buf_size = 0;
    uint8_t i_buf = 0;

    bool done = false;
    for (int i = 2; !done && i < frames_per_block_; ++i) {
      for (int c = 0; !done && c < wav_data_.channels_; ++c) {
        if (buf_size == 0) {
          if (!block_size) {
            done = true;
            continue;
          }
          i_buf = *p_buf;
          ++p_buf;
          --block_size;
          buf_size = 2;
        }

        /* Store the nibble in signed char, so we get an arithmetic shift. */
        int8_t error_delta = (int8_t)(i_buf) >> 4;
        uint8_t error_delta_unsigned = i_buf >> 4;
        i_buf <<= 4;
        --buf_size;

        int32_t pred_sample =
            (samp1[c] * coef1[c] + samp2[c] * coef2[c]) / (1 << 8);
        if (pred_sample < -32768) {
          pred_sample = -32768;
        }
        if (pred_sample > 32767) {
          pred_sample = 32767;
        }

        int16_t new_sample = (int16_t)pred_sample + (delta[c] * error_delta);
        buffer[buffer_avail_++] = new_sample / 32768.0f;

        static const int kAdaptionTable[] = {230, 230, 230, 230, 307, 409,
                                             512, 614, 768, 614, 512, 409,
                                             307, 230, 230, 230};
        delta[c] = int16_t(
            (delta[c] * kAdaptionTable[error_delta_unsigned]) / (1 << 8));
        delta[c] = std::max((int16_t)16, delta[c]);

        samp2[c] = samp1[c];
        samp1[c] = new_sample;
      }
    }

    return true;
  }

  int Read(float* buf, int frames) {
    int got_frames = 0;

    int sample = 0;

    while (got_frames < (int)frames) {
      if (buffer_used_ == buffer_avail_) {
        if (!DecodeADPCMBlock()) {
          return RageSoundReader::ERROR;
        }
      }
      if (buffer_avail_ == 0) {
        if (!got_frames) {
          return RageSoundReader::END_OF_FILE;
        } else {
          return got_frames;
        }
      }
      for (int c = 0; c < wav_data_.channels_; c++) {
        buf[sample++] = buffer_[buffer_used_++];
      }
      got_frames++;
    }
    return got_frames;
  }

  int GetLength() const {
    const int num_whole_blocks =
        wav_data_.data_chunk_size_ / wav_data_.block_align_;
    const int extra_bytes = wav_data_.data_chunk_size_ -
                            (num_whole_blocks * wav_data_.block_align_);

    int frames = num_whole_blocks * frames_per_block_;

    const int block_header_size = 7 * wav_data_.channels_;
    if (extra_bytes > block_header_size) {
      const int extra_adpcm_nibbles =
          std::max(0, extra_bytes - block_header_size) * 2;
      const int extra_adpcm_frames = extra_adpcm_nibbles / wav_data_.channels_;

      frames += 2 + extra_adpcm_frames;
    }

    int ms = int((int64_t(frames) * 1000) / wav_data_.sample_rate_);
    return ms;
  }

  int SetPosition(int frame) {
    const int block = frame / frames_per_block_;

    buffer_used_ = buffer_avail_ = 0;

    {
      const int byte = block * wav_data_.block_align_;
      if (byte >= wav_data_.data_chunk_size_) {
        /* Past EOF. */
        SetEOF();
        return 0;
      }
      file_.Seek(byte + wav_data_.data_chunk_pos_);
    }

    if (!DecodeADPCMBlock()) {
      return -1;
    }

    const int remaining_frames = frame - block * frames_per_block_;
    buffer_used_ = remaining_frames * wav_data_.channels_;
    if (buffer_used_ > buffer_avail_) {
      SetEOF();
      return 0;
    }

    return 1;
  }

  // XXX: untested
  int GetNextSourceFrame() const {
    int byte = file_.Tell() - wav_data_.data_chunk_pos_;
    int block = byte / wav_data_.block_align_;
    int frame = block * frames_per_block_;

    int buffer_remaining_bytes = buffer_avail_ - buffer_used_;
    int buffer_remaining_frames =
        buffer_remaining_bytes / (wav_data_.channels_ * sizeof(int16_t));
    frame -= buffer_remaining_frames;

    return frame;
  }
};

std::string ReadString(RageFileBasic& f, int size, std::string& error) {
  if (error.size() != 0) {
    return std::string();
  }

  char* buf = new char[size + 1];
  std::fill(buf, buf + size + 1, '\0');
  FileReading::ReadBytes(f, buf, size, error);
  std::string ret(buf);
  delete[] buf;
  return ret;
}

#define FATAL_ERROR(s)                    \
  {                                       \
    if (sError.size() == 0) sError = (s); \
    SetError(sError);                     \
    return OPEN_FATAL_ERROR;              \
  }

RageSoundReader_FileReader::OpenResult RageSoundReader_WAV::Open(
    RageFileBasic* file) {
  file_ = file;

  std::string sError;

  /* RIFF header: */
  if (ReadString(*file_, 4, sError) != "RIFF") {
    SetError("Not a WAV file");
    return OPEN_UNKNOWN_FILE_FORMAT;
  }

  FileReading::read_32_le(*file_, sError); /* file size */
  if (ReadString(*file_, 4, sError) != "WAVE") {
    SetError("Not a WAV file");
    return OPEN_UNKNOWN_FILE_FORMAT;
  }

  bool got_format_chunk = false, got_data_chunk = false;
  while (!got_format_chunk || !got_data_chunk) {
    std::string chunk_id = ReadString(*file_, 4, sError);
    int32_t chunk_size = FileReading::read_32_le(*file_, sError);

    if (sError.size() != 0) {
      SetError(sError);
      return OPEN_FATAL_ERROR;
    }

    int next_chunk = file_->Tell() + chunk_size;
    /* Chunks are always word-aligned: */
    next_chunk = (next_chunk + 1) & ~1;

    if (chunk_id == "fmt ") {
      if (got_format_chunk) {
        LOG->Warn(
            "File %s has more than one fmt chunk",
            file_->GetDisplayPath().c_str());
      }

      wav_data_.format_tag_ = FileReading::read_16_le(*file_, sError);
      wav_data_.channels_ = FileReading::read_16_le(*file_, sError);
      wav_data_.sample_rate_ = FileReading::read_32_le(*file_, sError);
      FileReading::read_32_le(*file_, sError); /* BytesPerSec */
      wav_data_.block_align_ = FileReading::read_16_le(*file_, sError);
      wav_data_.bits_per_sample_ = FileReading::read_16_le(*file_, sError);
      wav_data_.extra_fmt_bytes_ = FileReading::read_16_le(*file_, sError);

      if (wav_data_.channels_ < 1 || wav_data_.channels_ > 2) {
        FATAL_ERROR(
            ssprintf("Unsupported channel count: %i", wav_data_.channels_));
      }

      if (wav_data_.sample_rate_ < 4000 ||
          wav_data_.sample_rate_ > 100000) { /* unlikely */
        FATAL_ERROR(
            ssprintf("Invalid sample rate: %i", wav_data_.sample_rate_));
      }

      wav_data_.extra_fmt_pos_ = file_->Tell();

      got_format_chunk = true;
    }

    if (chunk_id == "data") {
      wav_data_.data_chunk_pos_ = file_->Tell();
      wav_data_.data_chunk_size_ = chunk_size;

      int file_size = file_->GetFileSize();
      int max_size = file_size - wav_data_.data_chunk_pos_;
      if (max_size < wav_data_.data_chunk_size_) {
        LOG->Warn(
            "File %s truncated (%i < data chunk size %i)",
            file_->GetDisplayPath().c_str(), max_size,
            wav_data_.data_chunk_size_);

        wav_data_.data_chunk_size_ = max_size;
      }

      got_data_chunk = true;
    }
    file_->Seek(next_chunk);
  }

  if (sError.size() != 0) {
    SetError(sError);
    return OPEN_FATAL_ERROR;
  }

  switch (wav_data_.format_tag_) {
    case 1:  // PCM
    case 3:  // FLOAT
      impl_ = new WavReaderPCM(*file_, wav_data_);
      break;
    case 2:  // ADPCM
      impl_ = new WavReaderADPCM(*file_, wav_data_);
      break;
    case 85:  // MP3
      /* Return unknown, so other decoders will be tried.  MAD can read MP3s
       * embedded in WAVs. */
      return OPEN_UNKNOWN_FILE_FORMAT;
    default:
      FATAL_ERROR(
          ssprintf("Unsupported data format %i", wav_data_.format_tag_));
  }

  if (!impl_->Init()) {
    SetError(impl_->GetError());
    return OPEN_FATAL_ERROR;
  }

  return OPEN_OK;
}

int RageSoundReader_WAV::GetLength() const {
  ASSERT(impl_ != nullptr);
  return impl_->GetLength();
}

int RageSoundReader_WAV::SetPosition(int frame) {
  ASSERT(impl_ != nullptr);
  return impl_->SetPosition(frame);
}

int RageSoundReader_WAV::GetNextSourceFrame() const {
  ASSERT(impl_ != nullptr);
  return impl_->GetNextSourceFrame();
}

int RageSoundReader_WAV::Read(float* buf, int frames) {
  ASSERT(impl_ != nullptr);
  return impl_->Read(buf, frames);
}

RageSoundReader_WAV::RageSoundReader_WAV() { impl_ = nullptr; }

RageSoundReader_WAV::~RageSoundReader_WAV() { delete impl_; }

RageSoundReader_WAV* RageSoundReader_WAV::Copy() const {
  RageSoundReader_WAV* ret = new RageSoundReader_WAV;
  RageFileBasic* file = file_->Copy();
  file->Seek(0);
  ret->Open(file);
  return ret;
}

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
