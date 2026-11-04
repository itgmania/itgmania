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
void Convert8bitToFloat(void* pBuf, int iSamples) {
  /* Convert in reverse, so we can do it in-place. */
  const uint8_t* pIn = (uint8_t*)pBuf;
  float* pOut = (float*)pBuf;
  for (int i = iSamples - 1; i >= 0; --i) {
    int iSample = pIn[i];
    iSample -= 128; /* 0..255 -> -128..127 */
    pOut[i] = iSample / 128.0f;
  }
}

/* Flip 16-bit samples if necessary.  On little-endian systems, this will
 * optimize out. */
void ConvertLittleEndian16BitToFloat(void* pBuf, int iSamples) {
  /* Convert in reverse, so we can do it in-place. */
  const int16_t* pIn = (int16_t*)pBuf;
  float* pOut = (float*)pBuf;
  for (int i = iSamples - 1; i >= 0; --i) {
    int16_t iSample = Swap16LE(pIn[i]);
    pOut[i] = iSample / 32768.0f;
  }
}

void ConvertLittleEndian24BitToFloat(void* pBuf, int iSamples) {
  /* Convert in reverse, so we can do it in-place. */
  const unsigned char* pIn = (unsigned char*)pBuf;
  float* pOut = (float*)pBuf;
  pIn += iSamples * 3;
  for (int i = iSamples - 1; i >= 0; --i) {
    pIn -= 3;

    int32_t iSample =
        (int(pIn[0]) << 0) | (int(pIn[1]) << 8) | (int(pIn[2]) << 16);

    /* Sign-extend 24-bit to 32-bit: */
    if (iSample & 0x800000) {
      iSample |= 0xFF000000;
    }

    pOut[i] = iSample / 8388608.0f;
  }
}

void ConvertLittleEndian32BitToFloat(void* pBuf, int iSamples) {
  /* Convert in reverse, so we can do it in-place. */
  const int32_t* pIn = (int32_t*)pBuf;
  float* pOut = (float*)pBuf;
  for (int i = iSamples - 1; i >= 0; --i) {
    int32_t iSample = Swap32LE(pIn[i]);
    pOut[i] = iSample / 2147483648.0f;
  }
}
};  // namespace

struct WavReader {
  WavReader(RageFileBasic& f, const RageSoundReader_WAV::WavData& data)
      : file_(f), wav_data_(data) {}
  virtual ~WavReader() {}
  virtual int Read(float* pBuf, int iFrames) = 0;
  virtual int GetLength() const = 0;
  virtual bool Init() = 0;
  virtual int SetPosition(int iFrame) = 0;
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

  int Read(float* buf, int iFrames) {
    int iBytesPerSample = QuantizeUp(wav_data_.bits_per_sample_, 8) / 8;
    int len = iFrames * wav_data_.channels_;
    len *= iBytesPerSample;

    const int iBytesLeftInDataChunk =
        wav_data_.data_chunk_size_ - (file_.Tell() - wav_data_.data_chunk_pos_);
    if (!iBytesLeftInDataChunk) {
      return RageSoundReader::END_OF_FILE;
    }

    len = std::min(len, iBytesLeftInDataChunk);
    int iGot = file_.Read(buf, len);

    int iGotSamples = iGot / iBytesPerSample;
    if (wav_data_.format_tag_ == 1) {
      switch (iBytesPerSample) {
        case 1:
          Convert8bitToFloat(buf, iGotSamples);
          break;
        case 2:
          ConvertLittleEndian16BitToFloat(buf, iGotSamples);
          break;
        case 3:
          ConvertLittleEndian24BitToFloat(buf, iGotSamples);
          break;
        case 4:
          ConvertLittleEndian32BitToFloat(buf, iGotSamples);
          /* otherwise 3; already a float */
          break;
      }
    }
    return iGotSamples / wav_data_.channels_;
  }

  int GetLength() const {
    const int iBytesPerSec = wav_data_.sample_rate_ * wav_data_.channels_ *
                             wav_data_.bits_per_sample_ / 8;
    int64_t iMS = (int64_t(wav_data_.data_chunk_size_) * 1000) / iBytesPerSec;
    return (int)iMS;
  }

  int SetPosition(int iFrame) {
    int iByte = (int)(int64_t(iFrame) * wav_data_.channels_ *
                      wav_data_.bits_per_sample_ / 8);
    if (iByte > wav_data_.data_chunk_size_) {
      file_.Seek(wav_data_.data_chunk_size_ + wav_data_.data_chunk_pos_);
      return 0;
    }

    file_.Seek(iByte + wav_data_.data_chunk_pos_);
    return 1;
  }

  // XXX: untested
  int GetNextSourceFrame() const {
    int iByte = file_.Tell() - wav_data_.data_chunk_pos_;
    int iFrame = iByte / (wav_data_.channels_ * wav_data_.bits_per_sample_ / 8);
    return iFrame;
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
    int16_t iNumCoef = FileReading::read_16_le(file_, error_);
    coef1_.resize(iNumCoef);
    coef2_.resize(iNumCoef);
    for (int i = 0; i < iNumCoef; ++i) {
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

    int8_t iPredictor[2];
    int16_t iDelta[2], iSamp1[2], iSamp2[2];
    for (int i = 0; i < wav_data_.channels_; ++i) {
      iPredictor[i] = FileReading::read_8(file_, error_);
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      iDelta[i] = FileReading::read_16_le(file_, error_);
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      iSamp1[i] = FileReading::read_16_le(file_, error_);
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      iSamp2[i] = FileReading::read_16_le(file_, error_);
    }

    if (error_.size() != 0) {
      return false;
    }

    float* pBuffer = buffer_;
    int iCoef1[2], iCoef2[2];
    for (int i = 0; i < wav_data_.channels_; ++i) {
      if (iPredictor[i] >= (int)coef1_.size()) {
        LOG->Trace(
            "%s: predictor out of range", file_.GetDisplayPath().c_str());

        /* XXX: silence this block? */
        iPredictor[i] = 0;
      }

      iCoef1[i] = coef1_[iPredictor[i]];
      iCoef2[i] = coef2_[iPredictor[i]];
    }

    /* We've read the block header; read the rest.  Don't read past the end of
     * the data chunk. */
    int iMaxSize = std::min(
        (int)wav_data_.block_align_ - 7 * wav_data_.channels_,
        (wav_data_.data_chunk_size_ + wav_data_.data_chunk_pos_) -
            file_.Tell());

    char* pBuf = (char*)alloca(iMaxSize);

    int iBlockSize = file_.Read(pBuf, iMaxSize);
    if (iBlockSize == 0) {
      return true;
    }
    if (iBlockSize == -1) {
      error_ = file_.GetError();
      return false;
    }

    for (int i = 0; i < wav_data_.channels_; ++i) {
      pBuffer[buffer_avail_++] = (int16_t)iSamp2[i] / 32768.0f;
    }
    for (int i = 0; i < wav_data_.channels_; ++i) {
      pBuffer[buffer_avail_++] = (int16_t)iSamp1[i] / 32768.0f;
    }

    int8_t iBufSize = 0;
    uint8_t iBuf = 0;

    bool bDone = false;
    for (int i = 2; !bDone && i < frames_per_block_; ++i) {
      for (int c = 0; !bDone && c < wav_data_.channels_; ++c) {
        if (iBufSize == 0) {
          if (!iBlockSize) {
            bDone = true;
            continue;
          }
          iBuf = *pBuf;
          ++pBuf;
          --iBlockSize;
          iBufSize = 2;
        }

        /* Store the nibble in signed char, so we get an arithmetic shift. */
        int8_t iErrorDelta = (int8_t)(iBuf) >> 4;
        uint8_t iErrorDeltaUnsigned = iBuf >> 4;
        iBuf <<= 4;
        --iBufSize;

        int32_t iPredSample =
            (iSamp1[c] * iCoef1[c] + iSamp2[c] * iCoef2[c]) / (1 << 8);
        if (iPredSample < -32768) {
          iPredSample = -32768;
        }
        if (iPredSample > 32767) {
          iPredSample = 32767;
        }

        int16_t iNewSample = (int16_t)iPredSample + (iDelta[c] * iErrorDelta);
        pBuffer[buffer_avail_++] = iNewSample / 32768.0f;

        static const int aAdaptionTable[] = {230, 230, 230, 230, 307, 409,
                                             512, 614, 768, 614, 512, 409,
                                             307, 230, 230, 230};
        iDelta[c] = int16_t(
            (iDelta[c] * aAdaptionTable[iErrorDeltaUnsigned]) / (1 << 8));
        iDelta[c] = std::max((int16_t)16, iDelta[c]);

        iSamp2[c] = iSamp1[c];
        iSamp1[c] = iNewSample;
      }
    }

    return true;
  }

  int Read(float* buf, int iFrames) {
    int iGotFrames = 0;

    int iSample = 0;

    while (iGotFrames < (int)iFrames) {
      if (buffer_used_ == buffer_avail_) {
        if (!DecodeADPCMBlock()) {
          return RageSoundReader::ERROR;
        }
      }
      if (buffer_avail_ == 0) {
        if (!iGotFrames) {
          return RageSoundReader::END_OF_FILE;
        } else {
          return iGotFrames;
        }
      }
      for (int c = 0; c < wav_data_.channels_; c++) {
        buf[iSample++] = buffer_[buffer_used_++];
      }
      iGotFrames++;
    }
    return iGotFrames;
  }

  int GetLength() const {
    const int iNumWholeBlocks =
        wav_data_.data_chunk_size_ / wav_data_.block_align_;
    const int iExtraBytes =
        wav_data_.data_chunk_size_ - (iNumWholeBlocks * wav_data_.block_align_);

    int iFrames = iNumWholeBlocks * frames_per_block_;

    const int iBlockHeaderSize = 7 * wav_data_.channels_;
    if (iExtraBytes > iBlockHeaderSize) {
      const int iExtraADPCMNibbles =
          std::max(0, iExtraBytes - iBlockHeaderSize) * 2;
      const int iExtraADPCMFrames = iExtraADPCMNibbles / wav_data_.channels_;

      iFrames += 2 + iExtraADPCMFrames;
    }

    int iMS = int((int64_t(iFrames) * 1000) / wav_data_.sample_rate_);
    return iMS;
  }

  int SetPosition(int iFrame) {
    const int iBlock = iFrame / frames_per_block_;

    buffer_used_ = buffer_avail_ = 0;

    {
      const int iByte = iBlock * wav_data_.block_align_;
      if (iByte >= wav_data_.data_chunk_size_) {
        /* Past EOF. */
        SetEOF();
        return 0;
      }
      file_.Seek(iByte + wav_data_.data_chunk_pos_);
    }

    if (!DecodeADPCMBlock()) {
      return -1;
    }

    const int iRemainingFrames = iFrame - iBlock * frames_per_block_;
    buffer_used_ = iRemainingFrames * wav_data_.channels_;
    if (buffer_used_ > buffer_avail_) {
      SetEOF();
      return 0;
    }

    return 1;
  }

  // XXX: untested
  int GetNextSourceFrame() const {
    int iByte = file_.Tell() - wav_data_.data_chunk_pos_;
    int iBlock = iByte / wav_data_.block_align_;
    int iFrame = iBlock * frames_per_block_;

    int iBufferRemainingBytes = buffer_avail_ - buffer_used_;
    int iBufferRemainingFrames =
        iBufferRemainingBytes / (wav_data_.channels_ * sizeof(int16_t));
    iFrame -= iBufferRemainingFrames;

    return iFrame;
  }
};

std::string ReadString(RageFileBasic& f, int iSize, std::string& sError) {
  if (sError.size() != 0) {
    return std::string();
  }

  char* buf = new char[iSize + 1];
  std::fill(buf, buf + iSize + 1, '\0');
  FileReading::ReadBytes(f, buf, iSize, sError);
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
    RageFileBasic* pFile) {
  file_ = pFile;

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

  bool bGotFormatChunk = false, bGotDataChunk = false;
  while (!bGotFormatChunk || !bGotDataChunk) {
    std::string ChunkID = ReadString(*file_, 4, sError);
    int32_t iChunkSize = FileReading::read_32_le(*file_, sError);

    if (sError.size() != 0) {
      SetError(sError);
      return OPEN_FATAL_ERROR;
    }

    int iNextChunk = file_->Tell() + iChunkSize;
    /* Chunks are always word-aligned: */
    iNextChunk = (iNextChunk + 1) & ~1;

    if (ChunkID == "fmt ") {
      if (bGotFormatChunk) {
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

      bGotFormatChunk = true;
    }

    if (ChunkID == "data") {
      wav_data_.data_chunk_pos_ = file_->Tell();
      wav_data_.data_chunk_size_ = iChunkSize;

      int iFileSize = file_->GetFileSize();
      int iMaxSize = iFileSize - wav_data_.data_chunk_pos_;
      if (iMaxSize < wav_data_.data_chunk_size_) {
        LOG->Warn(
            "File %s truncated (%i < data chunk size %i)",
            file_->GetDisplayPath().c_str(), iMaxSize,
            wav_data_.data_chunk_size_);

        wav_data_.data_chunk_size_ = iMaxSize;
      }

      bGotDataChunk = true;
    }
    file_->Seek(iNextChunk);
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

int RageSoundReader_WAV::SetPosition(int iFrame) {
  ASSERT(impl_ != nullptr);
  return impl_->SetPosition(iFrame);
}

int RageSoundReader_WAV::GetNextSourceFrame() const {
  ASSERT(impl_ != nullptr);
  return impl_->GetNextSourceFrame();
}

int RageSoundReader_WAV::Read(float* pBuf, int iFrames) {
  ASSERT(impl_ != nullptr);
  return impl_->Read(pBuf, iFrames);
}

RageSoundReader_WAV::RageSoundReader_WAV() { impl_ = nullptr; }

RageSoundReader_WAV::~RageSoundReader_WAV() { delete impl_; }

RageSoundReader_WAV* RageSoundReader_WAV::Copy() const {
  RageSoundReader_WAV* ret = new RageSoundReader_WAV;
  RageFileBasic* pFile = file_->Copy();
  pFile->Seek(0);
  ret->Open(pFile);
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
