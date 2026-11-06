/* Handle loading and decoding of sounds.
 *
 * For small files, pre-decode the entire file into a regular buffer.  We
 * might want to play many samples at once, and we don't want to have to decode
 * 5-10 mp3s simultaneously during play.
 *
 * For larger files, decode them on the fly.  These are usually music, and
 * there's usually only one of those playing at a time.  When we get updates,
 * decode data at the same rate we're playing it.  If we don't do this, and
 * we're being read in large chunks, we're forced to decode in larger chunks as
 * well, which can cause framerate problems.
 *
 * Error handling:
 * Decoding errors (eg. CRC failures) will be recovered from when possible.
 *
 * When they can't be recovered, the sound will stop (unless loop or !autostop)
 * and the error will be available in GetError().
 *
 * Seeking past the end of the file will throw a warning and rewind.
 */

#include "RageSound.h"

#include <cmath>
#include <cstdint>
#include <string>

#include "LuaManager.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageSoundConstants.h"
#include "RageSoundManager.h"
#include "RageSoundReader_Extend.h"
#include "RageSoundReader_FileReader.h"
#include "RageSoundReader_Pan.h"
#include "RageSoundReader_PitchChange.h"
#include "RageSoundReader_PostBuffering.h"
#include "RageSoundReader_Preload.h"
#include "RageSoundReader_Resample_Good.h"
#include "RageSoundReader_ThreadedBuffer.h"
#include "RageSoundUtil.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "global.h"

RageSoundParams::RageSoundParams()
    : start_second_(0),
      length_seconds_(-1),
      fade_in_seconds_(0),
      fade_out_seconds_(0),
      volume_(1.0f),
      attract_volume_(1.0f),
      pitch_(1.0f),
      speed_(1.0f),
      start_time_(RageZeroTimer),
      stop_mode_(M_AUTO),
      is_critical_sound_(false) {}

RageSoundLoadParams::RageSoundLoadParams()
    : support_rate_changing_(false), support_pan_(false) {}

RageSound::RageSound()
    : mutex_("RageSound"),
      source_(nullptr),
      file_path_(""),
      param_(),
      stopped_source_frame_(0),
      playing_(false),
      delete_when_finished_(false),
      error_("") {
  ASSERT(SOUNDMAN != nullptr);
}

RageSound::~RageSound() { Unload(); }

RageSound::RageSound(const RageSound& cpy)
    : RageSoundBase(cpy), mutex_("RageSound"), source_(nullptr) {
  ASSERT(SOUNDMAN != nullptr);

  *this = cpy;
}

RageSound& RageSound::operator=(const RageSound& cpy) {
  LockMut(cpy.mutex_);

  /* If m_bDeleteWhenFinished, then nobody that has a reference to the sound
   * should be making copies. */
  ASSERT(!cpy.delete_when_finished_);

  param_ = cpy.param_;
  stopped_source_frame_ = cpy.stopped_source_frame_;
  playing_ = false;
  delete_when_finished_ = false;

  if (source_ != nullptr) {
    delete source_;
  }
  if (cpy.source_) {
    source_ = cpy.source_->Copy();
  } else {
    source_ = nullptr;
  }

  file_path_ = cpy.file_path_;

  return *this;
}

void RageSound::Unload() {
  if (IsPlaying()) {
    StopPlaying();
  }

  LockMut(mutex_);

  if (source_ != nullptr) {
    delete source_;
  }
  source_ = nullptr;

  file_path_ = "";
}

/* The sound will self-delete itself when it stops playing. If the sound is not
 * playing, the sound will be deleted immediately. The caller loses ownership
 * of the sound. */
void RageSound::DeleteSelfWhenFinishedPlaying() {
  mutex_.Lock();

  if (!playing_) {
    mutex_.Unlock();
    delete this;
    return;
  }

  delete_when_finished_ = true;
  mutex_.Unlock();
}

bool RageSound::IsLoaded() const { return source_ != nullptr; }

class RageSoundReader_Silence : public RageSoundReader {
 public:
  int GetLength() const { return 0; }
  int GetLength_Fast() const { return 0; }
  int SetPosition(int iFrame) { return 1; }
  int Read(float* pBuf, int iFrames) { return RageSoundReader::END_OF_FILE; }
  RageSoundReader* Copy() const { return new RageSoundReader_Silence; }
  int GetSampleRate() const { return FALLBACK_SAMPLE_RATE; }
  unsigned GetNumChannels() const { return 1; }
  int GetNextSourceFrame() const { return 0; }
  float GetStreamToSourceRatio() const { return 1.0f; }
  std::string GetError() const { return ""; }
};

bool RageSound::Load(std::string sSoundFilePath) {
  /* Automatically determine whether to precache */
  /* TODO: Hook this up to a pref? */
  return Load(sSoundFilePath, false);
}

bool RageSound::Load(
    std::string sSoundFilePath, bool bPrecache,
    const RageSoundLoadParams* pParams) {
  LOG->Trace(
      "RageSound: Load \"%s\" (precache: %i)", sSoundFilePath.c_str(),
      bPrecache);

  if (pParams == nullptr) {
    static const RageSoundLoadParams Defaults;
    pParams = &Defaults;
  }

  /* If this sound is already preloaded and held by SOUNDMAN, just make a copy
   * of that.  Since RageSoundReader_Preload is refcounted, this is cheap. */
  RageSoundReader* pSound = SOUNDMAN->GetLoadedSound(sSoundFilePath);
  bool bNeedBuffer = true;
  if (pSound == nullptr) {
    std::string error;
    bool bPrebuffer;
    pSound = RageSoundReader_FileReader::OpenFile(
        sSoundFilePath, error, &bPrebuffer);
    if (pSound == nullptr) {
      LOG->Warn(
          "RageSound::Load: error opening sound \"%s\": %s",
          sSoundFilePath.c_str(), error.c_str());

      pSound = new RageSoundReader_Silence;
    }

    /* If the sound is prebuffered into memory, we don't need to buffer reads.
     */
    if (bPrebuffer) {
      bNeedBuffer = false;
    }
  } else {
    /* The sound we were given from SOUNDMAN is already preloaded. */
    bPrecache = false;
    bNeedBuffer = false;
  }

  LoadSoundReader(pSound);

  /* Try to precache.  Do this after calling LoadSoundReader() to put the
   * sound in this->m_pSource, so we preload after resampling. */
  if (bPrecache) {
    if (RageSoundReader_Preload::PreloadSound(source_)) {
      /* We've preloaded the sound.  Pass it to SOUNDMAN, for reuse. */
      SOUNDMAN->AddLoadedSound(
          sSoundFilePath, (RageSoundReader_Preload*)source_);
    }
    bNeedBuffer = false;
  }

  source_ = new RageSoundReader_Extend(source_);
  if (bNeedBuffer) {
    source_ = new RageSoundReader_ThreadedBuffer(source_);
  }
  source_ = new RageSoundReader_PostBuffering(source_);

  if (pParams->support_rate_changing_) {
    RageSoundReader_PitchChange* pRate =
        new RageSoundReader_PitchChange(source_);
    source_ = pRate;
  }

  if (pParams->support_pan_) {
    source_ = new RageSoundReader_Pan(source_);
  }

  file_path_ = sSoundFilePath;

  mutex_.SetName(ssprintf("RageSound (%s)", Basename(sSoundFilePath).c_str()));

  return true;
}

void RageSound::LoadSoundReader(RageSoundReader* pSound) {
  Unload();

  stopped_source_frame_ = 0;

  const int iNeededRate = SOUNDMAN->GetDriverSampleRate();
  bool bSupportRateChange = false;
  if (iNeededRate != pSound->GetSampleRate() || bSupportRateChange) {
    RageSoundReader_Resample_Good* Resample =
        new RageSoundReader_Resample_Good(pSound, iNeededRate);
    pSound = Resample;
  }

  source_ = pSound;
}

namespace {
void AppendMixPosition(
    RageSoundMixPosition* pPositions, int iMaxPositions, int& iPositionCount,
    int64_t iSourceFrame, int iGotFrames, float fRate) {
  if (iPositionCount > 0) {
    RageSoundMixPosition& previous = pPositions[iPositionCount - 1];
    const int64_t iExpectedSourceFrame =
        previous.source_frame_ +
        static_cast<int64_t>(std::llround(
            previous.frames_ *
            static_cast<double>(previous.source_to_stream_ratio_)));
    if (previous.source_to_stream_ratio_ == fRate &&
        iExpectedSourceFrame == iSourceFrame) {
      previous.frames_ += iGotFrames;
      return;
    }
  }

  ASSERT_M(iPositionCount < iMaxPositions, ssprintf("%d", iMaxPositions));
  RageSoundMixPosition& position = pPositions[iPositionCount++];
  position.source_frame_ = iSourceFrame;
  position.frames_ = iGotFrames;
  position.source_to_stream_ratio_ = fRate;
}
}  // namespace

/*
 * Retrieve audio data, for mixing. The driver owns the audible-position
 * history, so we only need to return the source spans that describe the block
 * we decoded.
 *
 * RageSound::GetDataToPlay and RageSound::FillBuf are the main threaded API.
 * These need to execute without blocking other threads from calling eg.
 * GetPositionSeconds, since they may take some time to run.
 *
 * On underrun, if no data was read, returns WOULD_BLOCK.  On end of file, if no
 * data was read, returns END_OF_FILE.  If any data is read, it is returned;
 * these conditions are masked and will be seen on the next call.  Otherwise,
 * the requested number of frames will always be returned.
 */
int RageSound::GetDataToPlay(
    float* pBuffer, int iFrames, RageSoundMixPosition* pPositions,
    int iMaxPositions, int& iPositionCount, int& iFramesStored) {
  ASSERT_M(playing_, ssprintf("%p", static_cast<void*>(this)));
  ASSERT(source_ != nullptr);
  ASSERT(pPositions != nullptr);
  ASSERT(iMaxPositions > 0);

  iFramesStored = 0;
  iPositionCount = 0;

  while (iFrames > 0) {
    float fRate = 1.0f;
    int iSourceFrame = 0;

    /* Read data from our source. */
    int iGotFrames = source_->RetriedRead(
        pBuffer + (iFramesStored * source_->GetNumChannels()), iFrames,
        &iSourceFrame, &fRate);

    if (iGotFrames == RageSoundReader::ERROR) {
      error_ = source_->GetError();
      // This error probably indicates an I/O error, rather than a decoding
      // error.
      LOG->Warn(
          "Decoding %s failed: %s", GetLoadedFilePath().c_str(),
          error_.c_str());
    }

    if (iGotFrames < 0) {
      if (!iFramesStored) {
        return iGotFrames;
      } else {
        break;
      }
    }

    AppendMixPosition(
        pPositions, iMaxPositions, iPositionCount, iSourceFrame, iGotFrames,
        fRate);
    iFramesStored += iGotFrames;
    iFrames -= iGotFrames;
  }
  if (source_->GetNumChannels() == 1) {
    RageSoundUtil::ConvertMonoToStereoInPlace(pBuffer, iFramesStored);
  }

  return iFramesStored;
}

/* Start playing from the current position. */
void RageSound::StartPlaying() {
  ASSERT(!playing_);

  // Move to the start position.
  SetPositionFrames(
      static_cast<int>(param_.start_second_ * source_->GetSampleRate() + 0.5));

  /* If m_StartTime is in the past, then we probably set a start time but took
   * too long loading.  We don't want that; log it, since it can be unobvious.
   */
  if (!param_.start_time_.IsZero() && param_.start_time_.Ago() > 0) {
    LOG->Trace(
        "Sound \"%s\" has a start time %f seconds in the past",
        GetLoadedFilePath().c_str(), param_.start_time_.Ago());
  }

  /* Tell the sound manager to start mixing us. */
  //	LOG->Trace("set playing true for %p (StartPlaying) (%s)", this,
  // this->GetLoadedFilePath().c_str());

  playing_ = true;

  /* Save the attract volume, so changes don't affect previously played
   * sounds. */
  param_.attract_volume_ = SOUNDMAN->GetVolumeOfNonCriticalSounds();
  ApplyParams();

  /* Don't lock while calling SOUNDMAN driver calls. */
  ASSERT(!mutex_.IsLockedByThisThread());

  SOUNDMAN->StartMixing(this);

  //	LOG->Trace("StartPlaying %p finished (%s)", this,
  // this->GetLoadedFilePath().c_str());
}

void RageSound::StopPlaying() {
  /* Don't lock while calling SOUNDMAN driver calls. */
  ASSERT(!mutex_.IsLockedByThisThread());

  /* Tell the sound driver to stop mixing this sound. */
  SOUNDMAN->StopMixing(this);
}

/* This is called by sound drivers when we're done playing. */
void RageSound::SoundIsFinishedPlaying(int iSourceFrame) {
  if (!playing_) {
    return;
  }

  // Make the decision to delete while inside the lock.
  // Do the actual deletion after releasing the mutex.
  bool bDeleteThis = false;
  {
    // Global sound mutex
    LockMut(mutex_);

    // Preserve the last known source frame when the driver was able to resolve
    // one; otherwise keep the previous stopped position.
    if (delete_when_finished_) {
      delete_when_finished_ = false;
      bDeleteThis = true;
    } else {
      if (iSourceFrame >= 0) {
        stopped_source_frame_ = iSourceFrame;
      }

      playing_ = false;
    }
  }

  if (bDeleteThis) {
    delete this;
    return;
  }
}

void RageSound::Play(bool is_action, const RageSoundParams* pParams) {
  if (source_ == nullptr) {
    LOG->Warn("RageSound::Play: sound not loaded");
    return;
  }
  if (is_action && PREFSMAN->mute_actions_) {
    return;
  }

  if (IsPlaying()) {
    PlayCopy(is_action, pParams);
    return;
  }

  if (pParams) {
    SetParams(*pParams);
  }

  StartPlaying();
}

void RageSound::PlayCopy(bool is_action, const RageSoundParams* pParams) const {
  if (is_action && PREFSMAN->mute_actions_) {
    return;
  }
  RageSound* pSound = new RageSound(*this);

  if (pParams) {
    pSound->SetParams(*pParams);
  }

  pSound->StartPlaying();
  pSound->DeleteSelfWhenFinishedPlaying();
}

void RageSound::Stop() { StopPlaying(); }

bool RageSound::Pause(bool bPause) {
  if (source_ == nullptr) {
    LOG->Warn("RageSound::Pause: sound not loaded");
    return false;
  }

  return SOUNDMAN->Pause(this, bPause);
}

float RageSound::GetLengthSeconds() {
  if (source_ == nullptr) {
    LOG->Warn("RageSound::GetLengthSeconds: sound not loaded");
    return -1;
  }

  int iLength = source_->GetLength();

  if (iLength < 0) {
    LOG->Warn(
        "GetLengthSeconds failed on %s: %s", GetLoadedFilePath().c_str(),
        source_->GetError().c_str());
    return -1;
  }

  return iLength / 1000.f;  // ms -> secs
}

/* If non-nullptr, approximate is set to true if the returned time is
 * approximated because of underrun, the sound not having started (after Play())
 * or finished (after EOF) yet.
 *
 * If non-nullptr, Timestamp is set to the real clock time associated with the
 * returned sound position.  We might take a variable amount of time before
 * grabbing the timestamp (to lock SOUNDMAN); we might lose the scheduler after
 * grabbing it, when releasing SOUNDMAN.
 */
float RageSound::GetPositionSeconds(RageTimer* pTimestamp) const {
  // cast the sample rate to be used for the remainder of the function.
  float fSampleRate = static_cast<float>(source_->GetSampleRate());

  // Quick check without lock
  if (!IsPlaying()) {
    return static_cast<float>(stopped_source_frame_) / fSampleRate;
  }

  int iSourceFrame = 0;
  if (!SOUNDMAN->GetPlayingPosition(this, iSourceFrame, pTimestamp)) {
    return static_cast<float>(stopped_source_frame_) / fSampleRate;
  }

  return static_cast<float>(iSourceFrame) / fSampleRate;
}

bool RageSound::SetPositionFrames(int iFrames) {
  LockMut(mutex_);

  if (source_ == nullptr) {
    LOG->Warn("RageSound::SetPositionFrames(%d): sound not loaded", iFrames);
    return false;
  }

  int iRet = source_->SetPosition(iFrames);
  std::string filePath = GetLoadedFilePath();
  if (iRet == -1) {
    error_ = source_->GetError();
    LOG->Warn(
        "SetPositionFrames: seek %s failed: %s", filePath.c_str(),
        error_.c_str());
  } else if (iRet == 0) {
    /* Seeked past EOF. */
    LOG->Warn(
        "SetPositionFrames: %i samples is beyond EOF in %s", iFrames,
        filePath.c_str());
  } else {
    stopped_source_frame_ = iFrames;
  }

  return iRet == 1;
}

float RageSound::GetPlaybackRate() const { return param_.speed_; }

RageTimer RageSound::GetStartTime() const { return param_.start_time_; }

void RageSound::SetParams(const RageSoundParams& p) {
  param_ = p;
  ApplyParams();
}

void RageSound::ApplyParams() {
  if (source_ == nullptr) {
    return;
  }

  source_->SetProperty("Pitch", param_.pitch_);
  source_->SetProperty("Speed", param_.speed_);
  source_->SetProperty("StartSecond", param_.start_second_);
  source_->SetProperty("LengthSeconds", param_.length_seconds_);
  source_->SetProperty("FadeInSeconds", param_.fade_in_seconds_);
  source_->SetProperty("FadeSeconds", param_.fade_out_seconds_);

  float fVolume = param_.volume_;
  if (!param_.is_critical_sound_) {
    fVolume *= param_.attract_volume_;
  }
  source_->SetProperty("Volume", fVolume);

  switch (GetStopMode()) {
    case RageSoundParams::M_LOOP:
      source_->SetProperty("Loop", 1.0f);
      break;
    case RageSoundParams::M_STOP:
      source_->SetProperty("Stop", 1.0f);
      break;
    case RageSoundParams::M_CONTINUE:
      source_->SetProperty("Continue", 1.0f);
      break;
    default:
      break;
  }
}

bool RageSound::SetProperty(const std::string& sProperty, float fValue) {
  return source_->SetProperty(sProperty, fValue);
}

RageSoundParams::StopMode_t RageSound::GetStopMode() const {
  if (param_.stop_mode_ != RageSoundParams::M_AUTO) {
    return param_.stop_mode_;
  }

  if (file_path_.find("loop") != std::string::npos) {
    return RageSoundParams::M_LOOP;
  } else {
    return RageSoundParams::M_STOP;
  }
}

void RageSound::SetStopModeFromString(const std::string& sStopMode) {
  if (sStopMode.find("stop") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_STOP;
  } else if (sStopMode.find("loop") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_LOOP;
  } else if (sStopMode.find("continue") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_CONTINUE;
  } else if (sStopMode.find("auto") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_AUTO;
  } else {
    LOG->Warn(
        "Invalid stop mode \"%s\" for sound \"%s\"", sStopMode.c_str(),
        file_path_.c_str());
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the RageSound. */
class LunaRageSound : public Luna<RageSound> {
 public:
  static int get_length(T* p, lua_State* L) {
    RageSoundReader* reader = p->GetSoundReader();
    if (reader == nullptr) {
      lua_pushnumber(L, -1.0f);
    } else {
      lua_pushnumber(L, reader->GetLength() / 1000.0f);
    }
    return 1;
  }
  static int pitch(T* p, lua_State* L) {
    RageSoundParams params(p->GetParams());
    params.pitch_ = FArg(1);
    p->SetParams(params);
    COMMON_RETURN_SELF;
  }

  static int speed(T* p, lua_State* L) {
    RageSoundParams params(p->GetParams());
    params.speed_ = FArg(1);
    p->SetParams(params);
    COMMON_RETURN_SELF;
  }

  static int volume(T* p, lua_State* L) {
    RageSoundParams params(p->GetParams());
    params.volume_ = FArg(1);
    p->SetParams(params);
    COMMON_RETURN_SELF;
  }

  static int SetProperty(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->SetProperty(SArg(1), FArg(2)));
    return 1;
  }

  // Rename me and deprecate the above one? -DaisuMaster
  static int SetParam(T* p, lua_State* L) {
    RageSoundParams params(p->GetParams());

    std::string val = SArg(1);
    if (val == "StartSecond") {
      params.start_second_ = FArg(2);
    } else if (val == "LengthSeconds") {
      params.length_seconds_ = FArg(2);
    } else if (val == "FadeInSeconds") {
      params.fade_in_seconds_ = FArg(2);
    } else if (val == "FadeSeconds") {
      params.fade_out_seconds_ = FArg(2);
    } else if (val == "Pitch") {
      params.pitch_ = FArg(2);
    } else if (val == "Speed") {
      params.speed_ = FArg(2);
    } else if (val == "Volume") {
      params.volume_ = FArg(2);
    }

    p->SetParams(params);
    COMMON_RETURN_SELF;
  }

  /*
  static int SetStopMode( T* p, lua_State *L )
  {
          LuaHelpers::Push( L, p->SetStopModeFromString(SArg(1)) );
          return 1;
  }
  */

  LunaRageSound() {
    ADD_METHOD(get_length);
    ADD_METHOD(pitch);
    ADD_METHOD(speed);
    ADD_METHOD(volume);
    ADD_METHOD(SetProperty);
    ADD_METHOD(SetParam);
    // ADD_METHOD( SetStopMode );
  }
};

LUA_REGISTER_CLASS(RageSound)
// lua end

/*
 * Copyright (c) 2002-2004 Glenn Maynard
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
