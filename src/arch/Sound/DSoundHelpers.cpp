#include "DSoundHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "PrefsManager.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundConstants.h"
#include "RageUtil.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/GetFileInformation.h"
#include "global.h"

#if defined(_WIN32)
#include <mmsystem.h>
#endif
#define DIRECTSOUND_VERSION 0x0700
#include <dsound.h>

#if defined(_MSC_VER)
#pragma comment(lib, "dsound.lib")
#endif

BOOL CALLBACK DSound::EnumCallback(
    LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule,
    LPVOID lpContext) {
  std::string sLine = ssprintf("DirectSound Driver: %s", lpcstrDescription);
  if (lpcstrModule[0]) {
    sLine += ssprintf(" %s", lpcstrModule);

    std::string sPath = FindSystemFile(lpcstrModule);
    if (sPath != "") {
      std::string sVersion;
      if (GetFileVersion(sPath, sVersion)) {
        sLine += ssprintf(" %s", sVersion.c_str());
      }
    }
  }

  LOG->Info("%s", sLine.c_str());

  return TRUE;
}

void DSound::SetPrimaryBufferMode() {
  DSBUFFERDESC format;
  memset(&format, 0, sizeof(format));
  format.dwSize = sizeof(DSBUFFERDESC);
  format.dwFlags = DSBCAPS_PRIMARYBUFFER;
  format.dwBufferBytes = 0;
  format.lpwfxFormat = nullptr;

  IDirectSoundBuffer* pBuffer;
  HRESULT hr = this->GetDS()->CreateSoundBuffer(&format, &pBuffer, nullptr);
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "Couldn't create primary buffer").c_str());
    return;
  }

  WAVEFORMATEX waveformat;
  memset(&waveformat, 0, sizeof(waveformat));
  waveformat.cbSize = 0;
  waveformat.wFormatTag = WAVE_FORMAT_PCM;
  waveformat.wBitsPerSample = 16;
  waveformat.nChannels = 2;
  int preferredSampleRate = PREFSMAN->sound_preferred_sample_rate_;
  if (preferredSampleRate == 0) {
    preferredSampleRate = FALLBACK_SAMPLE_RATE;
  }
  waveformat.nSamplesPerSec = preferredSampleRate;
  waveformat.nBlockAlign =
      (waveformat.nChannels * waveformat.wBitsPerSample) / 8;
  waveformat.nAvgBytesPerSec =
      waveformat.nSamplesPerSec * waveformat.nBlockAlign;

  // Set the primary buffer's format
  hr = IDirectSoundBuffer_SetFormat(pBuffer, &waveformat);
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "SetFormat on primary buffer").c_str());
  }

  DWORD got;
  hr = pBuffer->GetFormat(&waveformat, sizeof(waveformat), &got);
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "GetFormat on primary buffer").c_str());
  } else if (waveformat.nSamplesPerSec != 44100) {
    LOG->Warn(
        "Primary buffer set to %i instead of 44100", waveformat.nSamplesPerSec);
  }

  /*
   * MS docs:
   *
   * When there are no sounds playing, DirectSound stops the mixer engine and
   * halts DMA (direct memory access) activity. If your application has frequent
   * short intervals of silence, the overhead of starting and stopping the mixer
   * each time a sound is played may be worse than the DMA overhead if you kept
   * the mixer active. Also, some sound hardware or drivers may produce unwanted
   * audible artifacts from frequent starting and stopping of playback. If your
   * application is playing audio almost continuously with only short breaks of
   * silence, you can force the mixer engine to remain active by calling the
   * IDirectSoundBuffer::Play method for the primary buffer. The mixer will
   * continue to run silently.
   *
   * However, I just added the above code and I don't want to change more until
   * it's tested.
   */
  pBuffer->Play(0, 0, DSBPLAY_LOOPING);

  pBuffer->Release();
}

DSound::DSound() {
  HRESULT hr;
  if (FAILED(hr = CoInitialize(nullptr))) {
    RageException::Throw(hr_ssprintf(hr, "CoInitialize").c_str());
  }
  ds_ = nullptr;
}

std::string DSound::Init() {
  HRESULT hr;
  if (FAILED(hr = DirectSoundCreate(nullptr, &ds_, nullptr))) {
    return hr_ssprintf(hr, "DirectSoundCreate");
  }

  static bool bShownInfo = false;
  if (!bShownInfo) {
    bShownInfo = true;
    DirectSoundEnumerate(EnumCallback, 0);

    DSCAPS Caps;
    Caps.dwSize = sizeof(Caps);
    if (FAILED(hr = ds_->GetCaps(&Caps))) {
      LOG->Warn(hr_ssprintf(hr, "ds_->GetCaps failed").c_str());
    } else {
      LOG->Info(
          "DirectSound sample rates: %i..%i %s", Caps.dwMinSecondarySampleRate,
          Caps.dwMaxSecondarySampleRate,
          (Caps.dwFlags & DSCAPS_CONTINUOUSRATE) ? "(continuous)" : "");
    }
  }

  /* Try to set primary mixing privileges */
  hr = ds_->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY);

  SetPrimaryBufferMode();

  return std::string();
}

DSound::~DSound() {
  if (ds_ != nullptr) {
    ds_->Release();
  }
  CoUninitialize();
}

bool DSound::IsEmulated() const {
  /* Don't bother wasting time trying to create buffers if we're
   * emulated.  This also gives us better diagnostic information. */
  DSCAPS Caps;
  Caps.dwSize = sizeof(Caps);
  HRESULT hr;
  if (FAILED(hr = ds_->GetCaps(&Caps))) {
    LOG->Warn(hr_ssprintf(hr, "ds_->GetCaps failed").c_str());
    /* This is strange, so let's be conservative. */
    return true;
  }

  return !!(Caps.dwFlags & DSCAPS_EMULDRIVER);
}

DSoundBuf::DSoundBuf()
    : buffer_(nullptr),
      last_cursors_{{0, 0}, {0, 0}, {0, 0}, {0, 0}},
      locked_buf1_(nullptr),
      locked_buf2_(nullptr),
      temp_buffer_(nullptr),
      locked_size1_(0),
      locked_size2_(0),
      write_cursor_(0),
      write_cursor_pos_(0),
      buffer_bytes_filled_(0),
      extra_writeahead_(0),
      buffer_locked_(false),
      playing_(false),
      buffer_size_(0),
      channels_(0),
      last_position_(0),
      sample_bits_(0),
      sample_rate_(0),
      volume_(-1),
      write_ahead_(0) {}

std::string DSoundBuf::Init(
    DSound& ds, DSoundBuf::hw hardware, int iChannels, int iSampleRate,
    int iSampleBits, int iWriteAhead) {
  channels_ = iChannels;
  sample_rate_ = iSampleRate;
  sample_bits_ = iSampleBits;
  write_ahead_ = iWriteAhead * bytes_per_frame();
  volume_ = -1; /* unset */
  buffer_locked_ = false;
  write_cursor_pos_ = write_cursor_ = buffer_bytes_filled_ = 0;
  extra_writeahead_ = 0;
  last_position_ = 0;
  playing_ = false;
  memset(&(last_cursors_), 0, sizeof(last_cursors_));

  /* The size of the actual DSound buffer.  This can be large; we generally
   * won't fill it completely. */
  buffer_size_ = 1024 * 64;
  buffer_size_ = std::max(buffer_size_, write_ahead_);

  WAVEFORMATEX waveformat;
  memset(&waveformat, 0, sizeof(waveformat));
  waveformat.cbSize = 0;
  waveformat.wFormatTag = WAVE_FORMAT_PCM;

  bool bNeedCtrlFrequency = false;
  // DYNAMIC_SAMPLERATE is usually 0 or some special value
  if (sample_rate_ == DYNAMIC_SAMPLERATE) {
    sample_rate_ = FALLBACK_SAMPLE_RATE;
    bNeedCtrlFrequency = true;
  }

  int bytes = sample_bits_ / 8;
  waveformat.wBitsPerSample = WORD(sample_bits_);
  waveformat.nChannels = WORD(channels_);
  waveformat.nSamplesPerSec = DWORD(sample_rate_);
  waveformat.nBlockAlign = WORD(bytes * channels_);
  waveformat.nAvgBytesPerSec = sample_rate_ * bytes * channels_;

  /* Try to create the secondary buffer */
  DSBUFFERDESC format;
  memset(&format, 0, sizeof(format));
  format.dwSize = sizeof(DSBUFFERDESC);

  if (IsWindowsVistaOrGreater()) {
    format.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS |
                     DSBCAPS_CTRLVOLUME | DSBCAPS_TRUEPLAYPOSITION;
  } else {
    format.dwFlags =
        DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;
  }

  /* Don't use DSBCAPS_STATIC.  It's meant for static buffers, and we
   * only use streaming buffers. */
  if (hardware == HW_HARDWARE) {
    format.dwFlags |= DSBCAPS_LOCHARDWARE;
  } else {
    format.dwFlags |= DSBCAPS_LOCSOFTWARE;
  }

  if (bNeedCtrlFrequency) {
    format.dwFlags |= DSBCAPS_CTRLFREQUENCY;
  }

  format.dwBufferBytes = buffer_size_;

  format.dwReserved = 0;

  format.lpwfxFormat = &waveformat;

  HRESULT hr = ds.GetDS()->CreateSoundBuffer(&format, &buffer_, nullptr);
  if (FAILED(hr)) {
    return hr_ssprintf(hr, "CreateSoundBuffer failed (%i hz)", sample_bits_);
  }

  /* I'm not sure this should ever be needed, but ... */
  DSBCAPS bcaps;
  bcaps.dwSize = sizeof(bcaps);
  hr = buffer_->GetCaps(&bcaps);
  if (FAILED(hr)) {
    return hr_ssprintf(hr, "buffer_->GetCaps").c_str();
  }
  if (int(bcaps.dwBufferBytes) != buffer_size_) {
    LOG->Warn(
        "bcaps.dwBufferBytes (%i) != buffer_size_(%i); adjusting",
        bcaps.dwBufferBytes, buffer_size_);
    buffer_size_ = bcaps.dwBufferBytes;
    write_ahead_ = std::min(write_ahead_, buffer_size_);
  }

  if (!(bcaps.dwFlags & DSBCAPS_CTRLVOLUME)) {
    LOG->Warn("Sound channel missing DSBCAPS_CTRLVOLUME");
  }
  if (!(bcaps.dwFlags & DSBCAPS_GETCURRENTPOSITION2)) {
    LOG->Warn("Sound channel missing DSBCAPS_GETCURRENTPOSITION2");
  }

  DWORD got;
  hr = buffer_->GetFormat(&waveformat, sizeof(waveformat), &got);
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "GetFormat on secondary buffer").c_str());
  } else if ((int)waveformat.nSamplesPerSec != sample_rate_) {
    LOG->Warn(
        "Secondary buffer set to %i instead of %i", waveformat.nSamplesPerSec,
        sample_rate_);
  }

  temp_buffer_ = new char[buffer_size_];

  return std::string();
}

void DSoundBuf::SetSampleRate(int hz) {
  sample_rate_ = hz;
  HRESULT hr = buffer_->SetFrequency(hz);
  if (FAILED(hr)) {
    RageException::Throw(
        hr_ssprintf(hr, "buffer_->SetFrequency(%i)", hz).c_str());
  }
}

void DSoundBuf::SetVolume(float fVolume) {
  ASSERT_M(fVolume >= 0 && fVolume <= 1, ssprintf("%f", fVolume));

  if (fVolume == 0) {
    fVolume = 0.001f;  // fix log10(0) == -INF
  }
  float iVolumeLog2 = std::log10(fVolume) / std::log10(2); /* vol log 2 */

  /* Volume is a multiplier; SetVolume wants attenuation in hundredths of a
   * decibel. */
  const int iNewVolume = std::max(int(1000 * iVolumeLog2), DSBVOLUME_MIN);

  if (volume_ == iNewVolume) {
    return;
  }

  HRESULT hr = buffer_->SetVolume(iNewVolume);
  if (FAILED(hr)) {
    static bool bWarned = false;
    if (!bWarned) {
      LOG->Warn(
          hr_ssprintf(hr, "DirectSoundBuffer::SetVolume(%i) failed", iNewVolume)
              .c_str());
    }
    bWarned = true;
    return;
  }

  volume_ = iNewVolume;
}

/* Determine if "pos" is between "start" and "end", for a circular buffer.  Note
 * that a start/end pos is ambiguous when start == end; it can mean that the
 * buffer is completely full or completely empty; this function treats it as
 * completely empty. */
static bool contained(int iStart, int iEnd, int iPos) {
  if (iEnd >= iStart) { /* iStart ... iPos ... iEnd */
    return iStart <= iPos && iPos < iEnd;
  } else {
    return iPos >= iStart || iPos < iEnd;
  }
}

DSoundBuf::~DSoundBuf() {
  if (buffer_ != nullptr) {
    buffer_->Release();
  }
  delete[] temp_buffer_;
}

/* Check to make sure that, given the current writeahead and chunksize, we're
 * capable of filling the prefetch region entirely.  If we aren't, increase
 * the writeahead.  If this happens, we're underruning. */
void DSoundBuf::CheckWriteahead(int iCursorStart, int iCursorEnd) {
  /* If we're in a recovering-from-underrun state, stop. */
  if (extra_writeahead_) {
    return;
  }

  /* If the driver is requesting an unreasonably large prefetch, ignore it
   * entirely. Some drivers seem to give broken write cursors sporadically,
   * requesting that almost the entire buffer be filled.  There's no reason a
   * driver should ever need more than 8k frames of writeahead. */
  int iPrefetch = iCursorEnd - iCursorStart;
  wrap(iPrefetch, buffer_size_);

  constexpr int kMaxAllowedPrefetch = 1024 * 32;  // 32 KB
  if (iPrefetch >= kMaxAllowedPrefetch) {
    static bool bLogged = false;
    if (bLogged) {
      return;
    }
    bLogged = true;

    LOG->Warn(
        "Sound driver is requesting an overly large prefetch: wants %i (cursor "
        "at %i..%i), writeahead not adjusted",
        iPrefetch / bytes_per_frame(), iCursorStart, iCursorEnd);
    return;
  }

  if (write_ahead_ >= iPrefetch) {
    return;
  }

  /* We need to increase the writeahead. */
  LOG->Trace(
      "insufficient writeahead: wants %i (cursor at %i..%i), writeahead "
      "adjusted from %i to %i",
      iPrefetch / bytes_per_frame(), iCursorStart, iCursorEnd, write_ahead_,
      iPrefetch);

  write_ahead_ = iPrefetch;
}

/* Figure out if we've underrun, and act if appropriate. */
void DSoundBuf::CheckUnderrun(int iCursorStart, int iCursorEnd) {
  /* If the buffer is full, we can't be underrunning. */
  if (buffer_bytes_filled_ >= buffer_size_) {
    return;
  }

  /* If nothing is expected to be filled, we can't underrun. */
  if (iCursorStart == iCursorEnd) {
    return;
  }

  /* If we're already in a recovering-from-underrun state, stop. */
  if (extra_writeahead_) {
    return;
  }

  int iFirstByteFilled = write_cursor_ - buffer_bytes_filled_;
  wrap(iFirstByteFilled, buffer_size_);

  /* If the end of the play cursor has data, we haven't underrun. */
  if (buffer_bytes_filled_ > 0 &&
      contained(iFirstByteFilled, write_cursor_, iCursorEnd)) {
    return;
  }

  /* Extend the writeahead to force fill as much as required to stop
   * underrunning. This has a major benefit: if we havn't skipped so long we've
   * passed a whole buffer (64k = ~350ms), this doesn't break stride.  We'll
   * skip forward, but the beat won't be lost, which is a lot easier to recover
   * from in play. */
  /* XXX: If this happens repeatedly over a period of time, increase writeahead.
   */
  /* XXX: What was I doing here?  This isn't working.  We want to know the
   * writeahead value needed to fill from the current iFirstByteFilled all the
   * way to iCursorEnd. */
  // int iNeededWriteahead = (iCursorStart + writeahead) - write_cursor_;
  int iNeededWriteahead = iCursorEnd - iFirstByteFilled;
  wrap(iNeededWriteahead, buffer_size_);
  if (iNeededWriteahead > write_ahead_) {
    extra_writeahead_ = iNeededWriteahead - write_ahead_;
    write_ahead_ = iNeededWriteahead;
  }

  int iMissedBy = iCursorEnd - write_cursor_;
  wrap(iMissedBy, buffer_size_);

  std::string s = ssprintf(
      "underrun: %i..%i (%i) filled but cursor at %i..%i; missed it by %i",
      iFirstByteFilled, write_cursor_, buffer_bytes_filled_, iCursorStart,
      iCursorEnd, iMissedBy);

  if (extra_writeahead_) {
    s += ssprintf(
        "; extended writeahead by %i to %i", extra_writeahead_, write_ahead_);
  }

  s += "; last: ";
  for (int i = 0; i < 4; ++i) {
    s += ssprintf("%i, %i; ", last_cursors_[i][0], last_cursors_[i][1]);
  }

  LOG->Trace("%s", s.c_str());
}

bool DSoundBuf::get_output_buf(
    char** pBuffer, unsigned* pBufferSize, int iChunksize) {
  ASSERT(!buffer_locked_);

  iChunksize *= bytes_per_frame();

  DWORD iCursorStart, iCursorEnd;

  HRESULT result;

  /* It's easiest to think of the cursor as a block, starting and ending at
   * the two values returned by GetCurrentPosition, that we can't write to. */
  result = buffer_->GetCurrentPosition(&iCursorStart, &iCursorEnd);
  if (result == DSERR_BUFFERLOST) {
    buffer_->Restore();
    result = buffer_->GetCurrentPosition(&iCursorStart, &iCursorEnd);
  }
  if (result != DS_OK) {
    LOG->Warn(
        hr_ssprintf(result, "DirectSound::GetCurrentPosition failed").c_str());
    return false;
  }

  memmove(&last_cursors_[0][0], &last_cursors_[1][0], sizeof(int) * 6);
  last_cursors_[3][0] = iCursorStart;
  last_cursors_[3][1] = iCursorEnd;

  /* Some cards (Creative AudioPCI) have a no-write area even when not playing.
   * I'm not sure what that means, but it breaks the assumption that we can fill
   * the whole writeahead when prebuffering. */
  if (!playing_) {
    iCursorEnd = iCursorStart;
  }

  /*
   * Some cards (Game Theater XP 7.1 hercwdm.sys 5.12.01.4101 [466688b,
   * 01-10-2003]) have odd behavior when starting a sound: the start/end cursors
   * go:
   *
   * 0,0             end cursor forced equal to start above (normal)
   * 4608, 1764      end cursor trailing the write cursor; except with old
   * emulated WaveOut devices, this shouldn't happen; it indicates that the
   *                   driver expects almost the whole buffer to be filled.
   * Also, the play cursor is too far ahead from the last call for the amount of
   * actual time passed. 704, XXX        start cursor moves back to where it
   * should be.  I don't have an exact end cursor position, but in general from
   * now on it stays about 5kb ahead of start (which is where it should be).
   *
   * The second call is completely wrong; both the start and end cursors are
   * meaningless. Detect this: if the end cursor is close behind the start
   * cursor, don't do anything. (We can't; we have no idea what the cursors
   * actually are.)
   */
  {
    int iPrefetch = iCursorEnd - iCursorStart;
    wrap(iPrefetch, buffer_size_);

    if (buffer_size_ - iPrefetch < 1024 * 4) {
      LOG->Trace(
          "Strange DirectSound cursor ignored: %i..%i", iCursorStart,
          iCursorEnd);
      return false;
    }
  }

  /* Update buffer_bytes_filled_. */
  {
    int iFirstByteFilled = write_cursor_ - buffer_bytes_filled_;
    wrap(iFirstByteFilled, buffer_size_);

    /* The number of bytes that have been played since the last time we got
     * here: */
    int bytes_played = iCursorStart - iFirstByteFilled;
    wrap(bytes_played, buffer_size_);

    buffer_bytes_filled_ -= bytes_played;
    buffer_bytes_filled_ = std::max(0, buffer_bytes_filled_);

    if (extra_writeahead_) {
      int used = std::min(extra_writeahead_, bytes_played);
      std::string s = ssprintf(
          "used %i of %i (%i..%i)", used, extra_writeahead_, iCursorStart,
          iCursorEnd);
      s += "; last: ";
      for (int i = 0; i < 4; ++i) {
        s += ssprintf("%i, %i; ", last_cursors_[i][0], last_cursors_[i][1]);
      }
      LOG->Trace("%s", s.c_str());
      write_ahead_ -= used;
      extra_writeahead_ -= used;
    }
  }

  CheckWriteahead(iCursorStart, iCursorEnd);
  CheckUnderrun(iCursorStart, iCursorEnd);

  /* If we already have enough bytes written ahead, stop. */
  if (buffer_bytes_filled_ >= write_ahead_) {
    return false;
  }

  /* If we don't have enough free space in the buffer to fill a whole chunk,
   * stop. */
  if (buffer_size_ - buffer_bytes_filled_ < iChunksize) {
    return false;
  }

  int iNumBytesEmpty = write_ahead_ - buffer_bytes_filled_;
  iNumBytesEmpty = QuantizeUp(iNumBytesEmpty, iChunksize);

  //	LOG->Trace("gave %i at %i (%i, %i) %i filled", iNumBytesEmpty,
  // write_cursor_, cursor, write, buffer_bytes_filled_);

  /* Lock the audio buffer. */
  result = buffer_->Lock(
      write_cursor_, iNumBytesEmpty, (LPVOID*)&locked_buf1_,
      (DWORD*)&locked_size1_, (LPVOID*)&locked_buf2_, (DWORD*)&locked_size2_,
      0);

  if (result == DSERR_BUFFERLOST) {
    buffer_->Restore();
    result = buffer_->Lock(
        write_cursor_, iNumBytesEmpty, (LPVOID*)&locked_buf1_,
        (DWORD*)&locked_size1_, (LPVOID*)&locked_buf2_, (DWORD*)&locked_size2_,
        0);
  }

  if (result != DS_OK) {
    LOG->Warn(
        hr_ssprintf(result, "Couldn't lock the DirectSound buffer.").c_str());
    return false;
  }

  *pBuffer = temp_buffer_;
  *pBufferSize = locked_size1_ + locked_size2_;

  write_cursor_ += iNumBytesEmpty;
  if (write_cursor_ >= buffer_size_) {
    write_cursor_ -= buffer_size_;
  }

  buffer_bytes_filled_ += iNumBytesEmpty;
  write_cursor_pos_ += iNumBytesEmpty / bytes_per_frame();

  buffer_locked_ = true;

  return true;
}

void DSoundBuf::release_output_buf(char* pBuffer, unsigned iBufferSize) {
  memcpy(locked_buf1_, pBuffer, locked_size1_);
  memcpy(locked_buf2_, pBuffer + locked_size1_, locked_size2_);
  buffer_->Unlock(locked_buf1_, locked_size1_, locked_buf2_, locked_size2_);
  buffer_locked_ = false;
}

int64_t DSoundBuf::GetPosition() const {
  DWORD iCursor, iJunk;
  HRESULT hr = buffer_->GetCurrentPosition(&iCursor, &iJunk);

  if (hr == DSERR_BUFFERLOST) {
    buffer_->Restore();
    hr = buffer_->GetCurrentPosition(&iCursor, &iJunk);
  }
  if (hr != DS_OK) {
    LOG->Warn(hr_ssprintf(hr, "DirectSound::GetPosition failed").c_str());
    iCursor = 0;
  }

  /* This happens occasionally on "Realtek AC97 Audio". */
  if ((int)iCursor == buffer_size_) {
    iCursor = 0;
  }
  ASSERT_M(
      (int)iCursor < buffer_size_, ssprintf("%i, %i", iCursor, buffer_size_));

  int iCursorFrames = int(iCursor) / bytes_per_frame();
  int iWriteCursorFrames = write_cursor_ / bytes_per_frame();

  int iFramesBehind = iWriteCursorFrames - iCursorFrames;
  /* iFramesBehind will be 0 if we're called before the buffer starts playing:
   * both iWriteCursorFrames and iCursorFrames will be 0. */
  if (iFramesBehind < 0) {
    iFramesBehind += buffersize_frames(); /* unwrap */
  }

  int64_t iRet = write_cursor_pos_ - iFramesBehind;

  /* Failsafe: never return a value smaller than we've already returned.
   * This can happen once in a while in underrun conditions. */
  iRet = std::max(last_position_, iRet);
  last_position_ = iRet;

  return iRet;
}

void DSoundBuf::Play() {
  if (playing_) {
    return;
  }
  buffer_->Play(0, 0, DSBPLAY_LOOPING);
  playing_ = true;
}

void DSoundBuf::Stop() {
  if (!playing_) {
    return;
  }

  buffer_->Stop();
  buffer_->SetCurrentPosition(0);

  write_cursor_pos_ = write_cursor_ = buffer_bytes_filled_ = 0;
  last_position_ = 0;

  write_ahead_ -= extra_writeahead_;
  extra_writeahead_ = 0;

  /* When stopped and rewound, the play and write cursors should both be 0. */
  /* This isn't true on some broken cards. */
  //	DWORD iPlay, iWrite;
  //	buffer_->GetCurrentPosition( &iPlay, &iWrite );
  //	ASSERT_M( iPlay == 0 && iWrite == 0, ssprintf("%i, %i", iPlay, iWrite)
  //);

  playing_ = false;
}

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
