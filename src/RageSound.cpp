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
  int SetPosition(int frame) { return 1; }
  int Read(float* buf, int frames) { return RageSoundReader::END_OF_FILE; }
  RageSoundReader* Copy() const { return new RageSoundReader_Silence; }
  int GetSampleRate() const { return FALLBACK_SAMPLE_RATE; }
  unsigned GetNumChannels() const { return 1; }
  int GetNextSourceFrame() const { return 0; }
  float GetStreamToSourceRatio() const { return 1.0f; }
  std::string GetError() const { return ""; }
};

bool RageSound::Load(std::string sound_file_path) {
  /* Automatically determine whether to precache */
  /* TODO: Hook this up to a pref? */
  return Load(sound_file_path, false);
}

bool RageSound::Load(
    std::string sound_file_path, bool precache,
    const RageSoundLoadParams* params) {
  LOG->Trace(
      "RageSound: Load \"%s\" (precache: %i)", sound_file_path.c_str(),
      precache);

  if (params == nullptr) {
    static const RageSoundLoadParams kDefaults;
    params = &kDefaults;
  }

  /* If this sound is already preloaded and held by SOUNDMAN, just make a copy
   * of that.  Since RageSoundReader_Preload is refcounted, this is cheap. */
  RageSoundReader* sound = SOUNDMAN->GetLoadedSound(sound_file_path);
  if (sound == nullptr) {
    std::string error;
    bool prebuffer;
    sound = RageSoundReader_FileReader::OpenFile(
        sound_file_path, error, &prebuffer);
    if (sound == nullptr) {
      LOG->Warn(
          "RageSound::Load: error opening sound \"%s\": %s",
          sound_file_path.c_str(), error.c_str());

      sound = new RageSoundReader_Silence;
    }
  } else {
    /* The sound we were given from SOUNDMAN is already preloaded. */
    precache = false;
  }

  LoadSoundReader(sound);

  /* Try to precache.  Do this after calling LoadSoundReader() to put the
   * sound in this->m_pSource, so we preload after resampling. */
  if (precache) {
    if (RageSoundReader_Preload::PreloadSound(source_)) {
      /* We've preloaded the sound.  Pass it to SOUNDMAN, for reuse. */
      SOUNDMAN->AddLoadedSound(
          sound_file_path, (RageSoundReader_Preload*)source_);
    }
  }

  source_ = new RageSoundReader_Extend(source_);
  source_ = new RageSoundReader_ThreadedBuffer(source_);
  source_ = new RageSoundReader_PostBuffering(source_);

  if (params->support_rate_changing_) {
    RageSoundReader_PitchChange* rate =
        new RageSoundReader_PitchChange(source_);
    source_ = rate;
  }

  if (params->support_pan_) {
    source_ = new RageSoundReader_Pan(source_);
  }

  file_path_ = sound_file_path;

  mutex_.SetName(ssprintf("RageSound (%s)", Basename(sound_file_path).c_str()));

  return true;
}

void RageSound::LoadSoundReader(RageSoundReader* sound) {
  Unload();

  stopped_source_frame_ = 0;

  const int needed_rate = SOUNDMAN->GetDriverSampleRate();
  bool support_rate_change = false;
  if (needed_rate != sound->GetSampleRate() || support_rate_change) {
    RageSoundReader_Resample_Good* resample =
        new RageSoundReader_Resample_Good(sound, needed_rate);
    sound = resample;
  }

  source_ = sound;
}

namespace {
void AppendMixPosition(
    RageSoundMixPosition* positions, int max_positions, int& position_count,
    int64_t source_frame, int got_frames, float rate) {
  if (position_count > 0) {
    RageSoundMixPosition& previous = positions[position_count - 1];
    const int64_t expected_source_frame =
        previous.source_frame_ +
        static_cast<int64_t>(std::llround(
            previous.frames_ *
            static_cast<double>(previous.source_to_stream_ratio_)));
    if (previous.source_to_stream_ratio_ == rate &&
        expected_source_frame == source_frame) {
      previous.frames_ += got_frames;
      return;
    }
  }

  ASSERT_M(position_count < max_positions, ssprintf("%d", max_positions));
  RageSoundMixPosition& position = positions[position_count++];
  position.source_frame_ = source_frame;
  position.frames_ = got_frames;
  position.source_to_stream_ratio_ = rate;
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
    float* buffer, int frames, RageSoundMixPosition* positions,
    int max_positions, int& position_count, int& frames_stored) {
  ASSERT_M(playing_, ssprintf("%p", static_cast<void*>(this)));
  ASSERT(source_ != nullptr);
  ASSERT(positions != nullptr);
  ASSERT(max_positions > 0);

  frames_stored = 0;
  position_count = 0;

  while (frames > 0) {
    float rate = 1.0f;
    int source_frame = 0;

    /* Read data from our source. */
    int got_frames = source_->RetriedRead(
        buffer + (frames_stored * source_->GetNumChannels()), frames,
        &source_frame, &rate);

    if (got_frames == RageSoundReader::ERROR) {
      error_ = source_->GetError();
      // This error probably indicates an I/O error, rather than a decoding
      // error.
      LOG->Warn(
          "Decoding %s failed: %s", GetLoadedFilePath().c_str(),
          error_.c_str());
    }

    if (got_frames < 0) {
      if (!frames_stored) {
        return got_frames;
      } else {
        break;
      }
    }

    AppendMixPosition(
        positions, max_positions, position_count, source_frame, got_frames,
        rate);
    frames_stored += got_frames;
    frames -= got_frames;
  }
  if (source_->GetNumChannels() == 1) {
    RageSoundUtil::ConvertMonoToStereoInPlace(buffer, frames_stored);
  }

  return frames_stored;
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
void RageSound::SoundIsFinishedPlaying(int source_frame) {
  if (!playing_) {
    return;
  }

  // Make the decision to delete while inside the lock.
  // Do the actual deletion after releasing the mutex.
  bool delete_this = false;
  {
    // Global sound mutex
    LockMut(mutex_);

    // Preserve the last known source frame when the driver was able to resolve
    // one; otherwise keep the previous stopped position.
    if (delete_when_finished_) {
      delete_when_finished_ = false;
      delete_this = true;
    } else {
      if (source_frame >= 0) {
        stopped_source_frame_ = source_frame;
      }

      playing_ = false;
    }
  }

  if (delete_this) {
    delete this;
    return;
  }
}

void RageSound::Play(bool is_action, const RageSoundParams* params) {
  if (source_ == nullptr) {
    LOG->Warn("RageSound::Play: sound not loaded");
    return;
  }
  if (is_action && PREFSMAN->mute_actions_) {
    return;
  }

  if (IsPlaying()) {
    PlayCopy(is_action, params);
    return;
  }

  if (params) {
    SetParams(*params);
  }

  StartPlaying();
}

void RageSound::PlayCopy(bool is_action, const RageSoundParams* params) const {
  if (is_action && PREFSMAN->mute_actions_) {
    return;
  }
  RageSound* sound = new RageSound(*this);

  if (params) {
    sound->SetParams(*params);
  }

  sound->StartPlaying();
  sound->DeleteSelfWhenFinishedPlaying();
}

void RageSound::Stop() { StopPlaying(); }

bool RageSound::Pause(bool pause) {
  if (source_ == nullptr) {
    LOG->Warn("RageSound::Pause: sound not loaded");
    return false;
  }

  return SOUNDMAN->Pause(this, pause);
}

float RageSound::GetLengthSeconds() {
  if (source_ == nullptr) {
    LOG->Warn("RageSound::GetLengthSeconds: sound not loaded");
    return -1;
  }

  int length = source_->GetLength();

  if (length < 0) {
    LOG->Warn(
        "GetLengthSeconds failed on %s: %s", GetLoadedFilePath().c_str(),
        source_->GetError().c_str());
    return -1;
  }

  return length / 1000.f;  // ms -> secs
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
float RageSound::GetPositionSeconds(RageTimer* timestamp) const {
  // cast the sample rate to be used for the remainder of the function.
  float sample_rate = static_cast<float>(source_->GetSampleRate());

  // Quick check without lock
  if (!IsPlaying()) {
    return static_cast<float>(stopped_source_frame_) / sample_rate;
  }

  int source_frame = 0;
  if (!SOUNDMAN->GetPlayingPosition(this, source_frame, timestamp)) {
    return static_cast<float>(stopped_source_frame_) / sample_rate;
  }

  return static_cast<float>(source_frame) / sample_rate;
}

bool RageSound::SetPositionFrames(int frames) {
  LockMut(mutex_);

  if (source_ == nullptr) {
    LOG->Warn("RageSound::SetPositionFrames(%d): sound not loaded", frames);
    return false;
  }

  int ret = source_->SetPosition(frames);
  std::string file_path = GetLoadedFilePath();
  if (ret == -1) {
    error_ = source_->GetError();
    LOG->Warn(
        "SetPositionFrames: seek %s failed: %s", file_path.c_str(),
        error_.c_str());
  } else if (ret == 0) {
    /* Seeked past EOF. */
    LOG->Warn(
        "SetPositionFrames: %i samples is beyond EOF in %s", frames,
        file_path.c_str());
  } else {
    stopped_source_frame_ = frames;
  }

  return ret == 1;
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

  float volume = param_.volume_;
  if (!param_.is_critical_sound_) {
    volume *= param_.attract_volume_;
  }
  source_->SetProperty("Volume", volume);

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

bool RageSound::SetProperty(const std::string& property, float value) {
  return source_->SetProperty(property, value);
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

void RageSound::SetStopModeFromString(const std::string& stop_mode) {
  if (stop_mode.find("stop") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_STOP;
  } else if (stop_mode.find("loop") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_LOOP;
  } else if (stop_mode.find("continue") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_CONTINUE;
  } else if (stop_mode.find("auto") != std::string::npos) {
    param_.stop_mode_ = RageSoundParams::M_AUTO;
  } else {
    LOG->Warn(
        "Invalid stop mode \"%s\" for sound \"%s\"", stop_mode.c_str(),
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
