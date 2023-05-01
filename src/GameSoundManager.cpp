#include "global.h"

#include "GameSoundManager.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include "AnnouncerManager.h"
#include "GameState.h"
#include "LightsManager.h"
#include "LuaManager.h"
#include "NoteData.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundManager.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongUtil.h"
#include "Steps.h"
#include "TimingData.h"
#include "arch/Sound/RageSoundDriver.h"

GameSoundManager* SOUND = nullptr;

// When playing music, automatically search for an SM file for timing data.  If
// one is found, automatically handle GAMESTATE->m_fSongBeat, etc.
//
// std::modf(GAMESTATE->m_fSongBeat) should always be continuously moving from 0
// to 1.  To do this, wait before starting a sound until the fractional portion
// of the beat will be the same.
//
// If PlayMusic(length_seconds) is set, peek at the beat, and extend the length
// so we'll be on the same fractional beat when we loop.

// Lock this before touching g_UpdatingTimer or g_Playing.
static RageEvent* g_Mutex;
static bool g_UpdatingTimer;
static bool g_Shutdown;
static bool g_bFlushing = false;

enum FadeState { FADE_NONE, FADE_OUT, FADE_WAIT, FADE_IN };
static FadeState g_FadeState = FADE_NONE;
static float g_fDimVolume = 1.0f;
static float g_fOriginalVolume = 1.0f;
static float g_fDimDurationRemaining = 0.0f;
static bool g_bWasPlayingOnLastUpdate = false;

struct MusicPlaying {
  bool timing_delayed;
  bool has_timing;
  bool apply_music_rate;
  // The timing data that we're currently using.
  TimingData timing;
  NoteData lights;

  // If timing_delayed is true, this will be the timing data for the song that's
  // starting. We'll copy it to timing once sound is heard.
  TimingData new_timing;
  RageSound* music;
  MusicPlaying(RageSound* Music) {
    timing.AddSegment(BPMSegment(0, 120));
    new_timing.AddSegment(BPMSegment(0, 120));
    has_timing = false;
    timing_delayed = false;
    apply_music_rate = false;
    music = Music;
  }

  ~MusicPlaying() { delete music; }
};

static MusicPlaying* g_Playing;

static RageThread MusicThread;

std::vector<RString> g_SoundsToPlayOnce;
std::vector<RString> g_SoundsToPlayOnceFromDir;
std::vector<RString> g_SoundsToPlayOnceFromAnnouncer;
// This should get updated to unordered_map when once C++11 is supported
std::map<RString, std::vector<int>> g_DirSoundOrder;

struct MusicToPlay {
  RString file;
  RString timing_file;
  bool has_timing;
  TimingData timing_data;
  NoteData lights_data;
  bool force_loop;
  float start_second;
  float length_seconds;
  float fade_in_length_seconds;
  float fade_out_length_seconds;
  bool align_beat;
  float apply_music_rate;
  MusicToPlay() { has_timing = false; }
};
std::vector<MusicToPlay> g_MusicsToPlay;
static GameSoundManager::PlayMusicParams g_FallbackMusicParams;

static void StartMusic(MusicToPlay& to_play) {
  LockMutex L(*g_Mutex);
  if (g_Playing->music->IsPlaying() &&
      g_Playing->music->GetLoadedFilePath().EqualsNoCase(to_play.file)) {
    return;
  }

  // We're changing or stopping the music. If we were dimming, reset.
  g_FadeState = FADE_NONE;

  if (to_play.file.empty()) {
    // StopPlaying() can take a while, so don't hold the lock while we stop the
    // sound. Be sure to leave the rest of g_Playing in place.
    RageSound* old_sound = g_Playing->music;
    g_Playing->music = new RageSound;
    L.Unlock();

    delete old_sound;
    return;
  }

  // Unlock, load the sound here, and relock.  Loading may take a while if we're
  // reading from CD and we have to seek far, which can throw off the timing
  // below.
  MusicPlaying* new_music;
  {
    g_Mutex->Unlock();
    RageSound* pSound = new RageSound;
    RageSoundLoadParams params;
    params.m_bSupportRateChanging = to_play.apply_music_rate;
    pSound->Load(to_play.file, false, &params);
    g_Mutex->Lock();

    new_music = new MusicPlaying(pSound);
  }

  new_music->timing = g_Playing->timing;
  new_music->lights = g_Playing->lights;

  // See if we can find timing data, if it's not already loaded.
  if (!to_play.has_timing && IsAFile(to_play.timing_file)) {
    LOG->Trace("Found '%s'", to_play.timing_file.c_str());
    Song song;
    SSCLoader ssc_loader;
    SMLoader sm_loader;
    if (GetExtension(to_play.timing_file) == ".ssc" &&
        ssc_loader.LoadFromSimfile(to_play.timing_file, song)) {
      to_play.has_timing = true;
      to_play.timing_data = song.m_SongTiming;
      // get cabinet lights if any
      Steps* steps_cabinet_lights =
          SongUtil::GetOneSteps(&song, StepsType_lights_cabinet);
      if (steps_cabinet_lights) {
        steps_cabinet_lights->GetNoteData(to_play.lights_data);
      }
    } else if (
        GetExtension(to_play.timing_file) == ".sm" &&
        sm_loader.LoadFromSimfile(to_play.timing_file, song)) {
      to_play.has_timing = true;
      to_play.timing_data = song.m_SongTiming;
      // get cabinet lights if any
      Steps* steps_cabinet_lights =
          SongUtil::GetOneSteps(&song, StepsType_lights_cabinet);
      if (steps_cabinet_lights) {
        steps_cabinet_lights->GetNoteData(to_play.lights_data);
      }
    }
  }

  if (to_play.has_timing) {
    new_music->new_timing = to_play.timing_data;
    new_music->lights = to_play.lights_data;
  }

  if (to_play.align_beat && to_play.has_timing && to_play.force_loop &&
      to_play.length_seconds != -1) {
    // Extend the loop period so it always starts and ends on the same
    // fractional beat. That is, if it starts on beat 1.5, and ends on
    // beat 10.2, extend it to end on beat 10.5. This way, effects always loop
    // cleanly.
    float start_beat = new_music->new_timing.GetBeatFromElapsedTimeNoOffset(
        to_play.start_second);
    float end_sec = to_play.start_second + to_play.length_seconds;
    float end_beat =
        new_music->new_timing.GetBeatFromElapsedTimeNoOffset(end_sec);

    const float start_beat_fraction = fmodfp(start_beat, 1);
    const float end_beat_fraction = fmodfp(end_beat, 1);

    float beat_difference = start_beat_fraction - end_beat_fraction;
    if (beat_difference < 0) {
      beat_difference += 1.0f; /* unwrap */
    }

    end_beat += beat_difference;

    const float real_end_sec =
        new_music->new_timing.GetElapsedTimeFromBeatNoOffset(end_beat);
    const float new_length_seconds = real_end_sec - to_play.start_second;

    /// Extend fade_out_length_seconds, so the added time is faded out.
    to_play.fade_out_length_seconds +=
        new_length_seconds - to_play.length_seconds;
    to_play.length_seconds = new_length_seconds;
  }

  bool start_immediately = false;
  if (!to_play.has_timing) {
    // This song has no real timing data. The offset is arbitrary. Change it so
    // the beat will line up to where we are now, so we don't have to delay.
    float dest_beat = fmodfp(GAMESTATE->m_Position.m_fSongBeatNoOffset, 1);
    float time =
        new_music->new_timing.GetElapsedTimeFromBeatNoOffset(dest_beat);

    new_music->new_timing.m_fBeat0OffsetInSeconds = time;

    start_immediately = true;
  }

  // If we have an active timer, try to start on the next update. Otherwise,
  // start now.
  if (!g_Playing->has_timing && !g_UpdatingTimer) {
    start_immediately = true;
  }
  if (!to_play.align_beat) {
    start_immediately = true;
  }

  RageTimer when; /* zero */
  if (!start_immediately) {
    // GetPlayLatency returns the minimum time until a sound starts. That's
    // common when starting a precached sound, but our sound isn't, so it'll
    // probably take a little longer. Nudge the latency up.
    const float presumed_latency = SOUNDMAN->GetPlayLatency() + 0.040f;
    const float cur_second =
        GAMESTATE->m_Position.m_fMusicSeconds + presumed_latency;
    const float cur_beat =
        g_Playing->timing.GetBeatFromElapsedTimeNoOffset(cur_second);

    /* The beat that the new sound will start on. */
    const float start_beat =
        new_music->new_timing.GetBeatFromElapsedTimeNoOffset(
            to_play.start_second);
    const float start_beat_fraction = fmodfp(start_beat, 1);

    float cur_beat_to_start_on = std::trunc(cur_beat) + start_beat_fraction;
    if (cur_beat_to_start_on < cur_beat) {
      cur_beat_to_start_on += 1.0f;
    }

    const float second_to_start_on =
        g_Playing->timing.GetElapsedTimeFromBeatNoOffset(cur_beat_to_start_on);
    const float maximum_distance = 2;
    const float distance = std::min(
        second_to_start_on - GAMESTATE->m_Position.m_fMusicSeconds,
        maximum_distance);

    when = GAMESTATE->m_Position.m_LastBeatUpdate + distance;
  }

  /* Important: don't hold the mutex while we load and seek the actual sound. */
  L.Unlock();
  {
    new_music->has_timing = to_play.has_timing;
    if (to_play.has_timing) {
      new_music->new_timing = to_play.timing_data;
    }
    new_music->timing_delayed = true;
    new_music->apply_music_rate = to_play.apply_music_rate;

    RageSoundParams rage_sound_params;
    rage_sound_params.m_StartSecond = to_play.start_second;
    rage_sound_params.m_LengthSeconds = to_play.length_seconds;
    rage_sound_params.m_fFadeInSeconds = to_play.fade_in_length_seconds;
    rage_sound_params.m_fFadeOutSeconds = to_play.fade_out_length_seconds;
    rage_sound_params.m_StartTime = when;
    if (to_play.force_loop) {
      rage_sound_params.StopMode = RageSoundParams::M_LOOP;
    }
    new_music->music->SetParams(rage_sound_params);
    new_music->music->StartPlaying();
  }

  LockMut(*g_Mutex);
  delete g_Playing;
  g_Playing = new_music;
}

static void DoPlayOnce(RString path) {
  // We want this to start quickly, so don't try to prebuffer it.
  RageSound* sound = new RageSound;
  sound->Load(path, false);

  sound->Play(false);
  sound->DeleteSelfWhenFinishedPlaying();
}

static void DoPlayOnceFromDir(RString path) {
  if (path == "") {
    return;
  }

  // make sure there's a slash at the end of this path
  if (path.Right(1) != "/") {
    path += "/";
  }

  std::vector<RString> sound_files;
  GetDirListing(path + "*.mp3", sound_files);
  GetDirListing(path + "*.wav", sound_files);
  GetDirListing(path + "*.ogg", sound_files);
  GetDirListing(path + "*.oga", sound_files);

  if (sound_files.empty()) {
    return;
  } else if (sound_files.size() == 1) {
    DoPlayOnce(path + sound_files[0]);
    return;
  }

  g_Mutex->Lock();
  std::vector<int>& order =
      g_DirSoundOrder.insert({path, std::vector<int>()}).first->second;
  // If order is exhausted, repopulate and reshuffle
  if (order.size() == 0) {
    for (int i = 0; i < (int)sound_files.size(); ++i) {
      order.push_back(i);
    }
    std::shuffle(order.begin(), order.end(), g_RandomNumberGenerator);
  }

  int index = order.back();
  order.pop_back();
  g_Mutex->Unlock();

  DoPlayOnce(path + sound_files[index]);
}

static bool SoundWaiting() {
  return !g_SoundsToPlayOnce.empty() || !g_SoundsToPlayOnceFromDir.empty() ||
         !g_SoundsToPlayOnceFromAnnouncer.empty() || !g_MusicsToPlay.empty();
}

static void StartQueuedSounds() {
  g_Mutex->Lock();
  std::vector<RString> sounds_to_play_once = g_SoundsToPlayOnce;
  g_SoundsToPlayOnce.clear();
  std::vector<RString> sounds_to_play_once_from_dir = g_SoundsToPlayOnceFromDir;
  g_SoundsToPlayOnceFromDir.clear();
  std::vector<RString> sounds_to_play_once_from_announcer =
      g_SoundsToPlayOnceFromAnnouncer;
  g_SoundsToPlayOnceFromAnnouncer.clear();
  std::vector<MusicToPlay> music_to_play = g_MusicsToPlay;
  g_MusicsToPlay.clear();
  g_Mutex->Unlock();

  for (unsigned i = 0; i < sounds_to_play_once.size(); ++i) {
    if (sounds_to_play_once[i] != "") {
      DoPlayOnce(sounds_to_play_once[i]);
    }
  }

  for (unsigned i = 0; i < sounds_to_play_once_from_dir.size(); ++i) {
    DoPlayOnceFromDir(sounds_to_play_once_from_dir[i]);
  }

  for (unsigned i = 0; i < sounds_to_play_once_from_announcer.size(); ++i) {
    RString path = sounds_to_play_once_from_announcer[i];
    if (path != "") {
      path = ANNOUNCER->GetPathTo(path);
      DoPlayOnceFromDir(path);
    }
  }

  for (unsigned i = 0; i < music_to_play.size(); ++i) {
    // Don't bother starting this music if there's another one in the queue
    // after it.
    //
    // Actually, it's a little trickier: the editor gives us a stop and then a
    // sound in quick succession; if we ignore the stop, we won't rewind the
    // sound if it was already playing. We don't want to waste time loading a
    // sound if it's going to be replaced immediately, though. So, if we have
    // more music in the queue, then forcibly stop the current sound.
    if (i + 1 == music_to_play.size()) {
      StartMusic(music_to_play[i]);
    } else {
      CHECKPOINT_M(ssprintf("Removing old sound at index %d", i));
      // StopPlaying() can take a while, so don't hold the lock while we stop
      // the sound.
      g_Mutex->Lock();
      RageSound* old_sound = g_Playing->music;
      g_Playing->music = new RageSound;
      g_Mutex->Unlock();

      delete old_sound;
    }
  }
}

void GameSoundManager::Flush() {
  g_Mutex->Lock();
  g_bFlushing = true;

  g_Mutex->Broadcast();

  while (g_bFlushing) {
    g_Mutex->Wait();
  }
  g_Mutex->Unlock();
}

int MusicThread_start(void* p) {
  while (!g_Shutdown) {
    g_Mutex->Lock();
    while (!SoundWaiting() && !g_Shutdown && !g_bFlushing) {
      g_Mutex->Wait();
    }
    g_Mutex->Unlock();

    // This is a little hack: we want to make sure that we go through
    // StartQueuedSounds after Flush() is called, to be sure we're flushed, so
    // check g_bFlushing before calling. This won't work if more than one thread
    // might call Flush(), but only the main thread is allowed to make SOUND
    // calls.
    bool flushing = g_bFlushing;

    StartQueuedSounds();

    if (flushing) {
      g_Mutex->Lock();
      g_Mutex->Signal();
      g_bFlushing = false;
      g_Mutex->Unlock();
    }
  }

  return 0;
}

GameSoundManager::GameSoundManager() {
  // Init RageSoundMan first:
  ASSERT(SOUNDMAN != nullptr);

  g_Mutex = new RageEvent("GameSoundManager");
  g_Playing = new MusicPlaying(new RageSound);

  g_UpdatingTimer = true;

  g_Shutdown = false;
  MusicThread.SetName("Music thread");
  MusicThread.Create(MusicThread_start, this);

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "SOUND");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

GameSoundManager::~GameSoundManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("SOUND");

  // Signal the mixing thread to quit.
  LOG->Trace("Shutting down music start thread ...");
  g_Mutex->Lock();
  g_Shutdown = true;
  g_Mutex->Broadcast();
  g_Mutex->Unlock();
  MusicThread.Wait();
  LOG->Trace("Music start thread shut down.");

  SAFE_DELETE(g_Playing);
  SAFE_DELETE(g_Mutex);
}

float GameSoundManager::GetFrameTimingAdjustment(float fDeltaTime) {
  // We get one update per frame, and we're updated early, almost immediately
  // after vsync, near the beginning of the game loop.  However, it's very
  // likely that we'll lose the scheduler while waiting for vsync, and some
  // other thread will be working.  Especially with a low-resolution scheduler
  // (Linux 2.4, Win9x), we may not get the scheduler back immediately after the
  // vsync; there may be up to a ~10ms delay.  This can cause jitter in the
  // rendered arrows.
  //
  // Compensate.  If vsync is enabled, and we're maintaining the refresh rate
  // consistently, we should have a very precise game loop interval.  If we have
  // that, but we're off by a small amount (less than the interval), adjust the
  // time to line it up.  As long as we adjust both the sound time and the
  // timestamp, this won't adversely affect input timing. If we're off by more
  // than that, we probably had a frame skip, in which case we have bigger skip
  // problems, so don't adjust.
  static int last_fps = 0;
  int this_fps = DISPLAY->GetFPS();

  if (this_fps != DISPLAY->GetActualVideoModeParams().rate ||
      this_fps != last_fps) {
    last_fps = this_fps;
    return 0;
  }

  const float fExpectedDelay = 1.0f / this_fps;
  const float fExtraDelay = fDeltaTime - fExpectedDelay;
  if (std::abs(fExtraDelay) >= fExpectedDelay / 2) {
    return 0;
  }

  /* Subtract the extra delay. */
  return std::min(-fExtraDelay, 0.0f);
}

void GameSoundManager::Update(float delta) {
  {
    g_Mutex->Lock();
    if (g_Playing->apply_music_rate) {
      RageSoundParams rage_sound_params = g_Playing->music->GetParams();
      float rate = GAMESTATE->m_SongOptions.GetPreferred().m_fMusicRate;
      if (rage_sound_params.m_fSpeed != rate) {
        rage_sound_params.m_fSpeed = rate;
        g_Playing->music->SetParams(rage_sound_params);
      }
    }

    bool is_playing = g_Playing->music->IsPlaying();
    g_Mutex->Unlock();
    if (!is_playing && g_bWasPlayingOnLastUpdate &&
        !g_FallbackMusicParams.file.empty()) {
      PlayMusic(g_FallbackMusicParams);

      g_FallbackMusicParams.file = "";
    }
    g_bWasPlayingOnLastUpdate = is_playing;
  }

  LockMut(*g_Mutex);

  {
    // Duration of the fade-in and fade-out:
    float fade_in_speed = g_Playing->music->GetParams().m_fFadeInSeconds;
    float fade_out_speed = g_Playing->music->GetParams().m_fFadeOutSeconds;
    float volume = g_Playing->music->GetParams().m_Volume;
    switch (g_FadeState) {
      case FADE_NONE:
        break;
      case FADE_OUT:
        fapproach(volume, g_fDimVolume, delta / fade_out_speed);
        if (std::abs(volume - g_fDimVolume) < 0.001f) {
          g_FadeState = FADE_WAIT;
        }
        break;
      case FADE_WAIT:
        g_fDimDurationRemaining -= delta;
        if (g_fDimDurationRemaining <= 0) {
          g_FadeState = FADE_IN;
        }
        break;
      case FADE_IN:
        fapproach(volume, g_fOriginalVolume, delta / fade_in_speed);
        if (std::abs(volume - g_fOriginalVolume) < 0.001f) {
          g_FadeState = FADE_NONE;
        }
        break;
    }

    RageSoundParams rage_sound_params = g_Playing->music->GetParams();
    if (rage_sound_params.m_Volume != volume) {
      rage_sound_params.m_Volume = volume;
      g_Playing->music->SetParams(rage_sound_params);
    }
  }

  if (!g_UpdatingTimer) {
    return;
  }

  const float adjust = GetFrameTimingAdjustment(delta);
  if (!g_Playing->music->IsPlaying()) {
    // There's no song playing. Fake it.
    CHECKPOINT_M(
        ssprintf("%f, delta %f", GAMESTATE->m_Position.m_fMusicSeconds, delta));
    GAMESTATE->UpdateSongPosition(
        GAMESTATE->m_Position.m_fMusicSeconds +
            delta * g_Playing->music->GetPlaybackRate(),
        g_Playing->timing);
    return;
  }

  // There's a delay between us calling Play() and the sound actually playing.
  // During this time, approximate will be true.  Keep using the previous
  // timing data until we get a non-approximate time, indicating that the sound
  // has actually started playing.
  bool approximate;
  RageTimer timer;
  const float seconds =
      g_Playing->music->GetPositionSeconds(&approximate, &timer);

  // Check for song timing skips.
  if (PREFSMAN->m_bLogSkips && !g_Playing->timing_delayed) {
    const float expected_time_passed =
        (timer - GAMESTATE->m_Position.m_LastBeatUpdate) *
        g_Playing->music->GetPlaybackRate();
    const float sound_time_passed =
        seconds - GAMESTATE->m_Position.m_fMusicSeconds;
    const float difference = expected_time_passed - sound_time_passed;

    static RString last_file = "";
    const RString this_file = g_Playing->music->GetLoadedFilePath();

    // If sound_time_passed < 0, the sound has probably looped.
    if (last_file == this_file && sound_time_passed >= 0 &&
        std::abs(difference) > 0.003f) {
      LOG->Trace(
          "Song position skip in %s: expected %.3f, got %.3f (cur %f, prev %f) "
          "(%.3f difference)",
          Basename(this_file).c_str(), expected_time_passed, sound_time_passed,
          seconds, GAMESTATE->m_Position.m_fMusicSeconds, difference);
    }
    last_file = this_file;
  }

  // If g_Playing->timing_delayed, we're waiting for the new music to actually
  // start playing.
  if (g_Playing->timing_delayed && !approximate) {
    // Load up the new timing data.
    g_Playing->timing = g_Playing->new_timing;
    g_Playing->timing_delayed = false;
  }

  if (g_Playing->timing_delayed) {
    // We're still waiting for the new sound to start playing, so keep using the
    // old timing data and fake the time.
    GAMESTATE->UpdateSongPosition(
        GAMESTATE->m_Position.m_fMusicSeconds + delta, g_Playing->timing);
  } else {
    GAMESTATE->UpdateSongPosition(
        seconds + adjust, g_Playing->timing, timer + adjust);
  }

  // Send crossed messages
  if (GAMESTATE->m_pCurSong) {
    static int beat_last_crossed = 0;

    float song_beat = GAMESTATE->m_Position.m_fSongBeat;

    int row_now = BeatToNoteRowNotRounded(song_beat);
    row_now = std::max(0, row_now);

    int beat_now = row_now / ROWS_PER_BEAT;

    for (int beat = beat_last_crossed + 1; beat <= beat_now; ++beat) {
      Message msg("CrossedBeat");
      msg.SetParam("Beat", beat);
      MESSAGEMAN->Broadcast(msg);
    }

    beat_last_crossed = beat_now;
  }

  // Update lights
  NoteData& lights = g_Playing->lights;
  // Lights data was loaded
  if (lights.GetNumTracks() > 0) {
    const float song_beat = GAMESTATE->m_Position.m_fLightSongBeat;
    const int song_row = BeatToNoteRowNotRounded(song_beat);

    static int row_last_crossed = 0;

    FOREACH_CabinetLight(cl) {
      // Are we "holding" the light?
      if (lights.IsHoldNoteAtRow(cl, song_row)) {
        LIGHTSMAN->BlinkCabinetLight(cl);
        continue;
      }

      // Otherwise, for each index we crossed since the last update:
      FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(
          lights, cl, r, row_last_crossed + 1, song_row + 1) {
        if (lights.GetTapNote(cl, r).type != TapNoteType_Empty) {
          LIGHTSMAN->BlinkCabinetLight(cl);
          break;
        }
      }
    }

    row_last_crossed = song_row;
  }
}

RString GameSoundManager::GetMusicPath() const {
  LockMut(*g_Mutex);
  return g_Playing->music->GetLoadedFilePath();
}

void GameSoundManager::PlayMusic(
    RString file, const TimingData* timing, bool force_loop, float start_second,
    float length_seconds, float fade_in_length_seconds,
    float fade_out_length_seconds, bool align_beat, bool apply_music_rate) {
  PlayMusicParams params;
  params.file = file;
  params.timing = timing;
  params.force_loop = force_loop;
  params.start_second = start_second;
  params.length_seconds = length_seconds;
  params.fade_in_length_seconds = fade_in_length_seconds;
  params.fade_out_length_seconds = fade_out_length_seconds;
  params.align_beat = align_beat;
  params.apply_music_rate = apply_music_rate;
  PlayMusic(params);
}

void GameSoundManager::PlayMusic(
    PlayMusicParams params, PlayMusicParams fallback_music_params) {
  g_FallbackMusicParams = fallback_music_params;

  MusicToPlay to_play;

  to_play.file = params.file;
  if (params.timing) {
    to_play.has_timing = true;
    to_play.timing_data = *params.timing;
  } else {
    // If no timing data was provided, look for it in the same place as the
    // music file.
    // TODO(aj): allow loading .ssc files as well
    to_play.timing_file = SetExtension(params.file, "sm");
  }

  to_play.force_loop = params.force_loop;
  to_play.start_second = params.start_second;
  to_play.length_seconds = params.length_seconds;
  to_play.fade_in_length_seconds = params.fade_in_length_seconds;
  to_play.fade_out_length_seconds = params.fade_out_length_seconds;
  to_play.align_beat = params.align_beat;
  to_play.apply_music_rate = params.apply_music_rate;

  // Add the MusicToPlay to the g_MusicsToPlay queue.
  g_Mutex->Lock();
  g_MusicsToPlay.push_back(to_play);
  g_Mutex->Broadcast();
  g_Mutex->Unlock();
}

void GameSoundManager::DimMusic(float volume, float duration_seconds) {
  LockMut(*g_Mutex);

  if (g_FadeState == FADE_NONE) {
    g_fOriginalVolume = g_Playing->music->GetParams().m_Volume;
  }
  // otherwise, g_fOriginalVolume is already set and m_Volume will be the
  // current state, not the original state

  g_fDimDurationRemaining = duration_seconds;
  g_fDimVolume = volume;
  g_FadeState = FADE_OUT;
}

void GameSoundManager::HandleSongTimer(bool on) {
  LockMut(*g_Mutex);
  g_UpdatingTimer = on;
}

void GameSoundManager::PlayOnce(RString path) {
  // Add the sound to the g_SoundsToPlayOnce queue.
  g_Mutex->Lock();
  g_SoundsToPlayOnce.push_back(path);
  g_Mutex->Broadcast();
  g_Mutex->Unlock();
}

void GameSoundManager::PlayOnceFromDir(RString path) {
  // Add the path to the g_SoundsToPlayOnceFromDir queue.
  g_Mutex->Lock();
  g_SoundsToPlayOnceFromDir.push_back(path);
  g_Mutex->Broadcast();
  g_Mutex->Unlock();
}

void GameSoundManager::PlayOnceFromAnnouncer(RString path) {
  // Add the path to the g_SoundsToPlayOnceFromAnnouncer queue.
  g_Mutex->Lock();
  g_SoundsToPlayOnceFromAnnouncer.push_back(path);
  g_Mutex->Broadcast();
  g_Mutex->Unlock();
}

float GameSoundManager::GetPlayerBalance(PlayerNumber pn) {
  // If two players are active, play sounds on each players' side.
  if (GAMESTATE->GetNumPlayersEnabled() == 2) {
    return (pn == PLAYER_1) ? -1.0f : 1.0f;
  } else {
    return 0;
  }
}

#include "LuaBinding.h"

// Allow Lua to have access to the GameSoundManager.
class LunaGameSoundManager : public Luna<GameSoundManager> {
 public:
  static int DimMusic(T* p, lua_State* L) {
    float volume = FArg(1);
    float duration_seconds = FArg(2);
    p->DimMusic(volume, duration_seconds);
    COMMON_RETURN_SELF;
  }
  static int PlayOnce(T* p, lua_State* L) {
    RString path = SArg(1);
    if (lua_toboolean(L, 2) && PREFSMAN->m_MuteActions) {
      COMMON_RETURN_SELF;
    }
    p->PlayOnce(path);
    COMMON_RETURN_SELF;
  }
  static int PlayAnnouncer(T* p, lua_State* L) {
    RString path = SArg(1);
    p->PlayOnceFromAnnouncer(path);
    COMMON_RETURN_SELF;
  }
  static int GetPlayerBalance(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushnumber(L, p->GetPlayerBalance(pn));
    return 1;
  }
  static int PlayMusicPart(T* p, lua_State* L) {
    RString music_path = SArg(1);
    float music_start = FArg(2);
    float music_length = FArg(3);
    float fade_in = 0;
    float fade_out = 0;
    bool loop = false;
    bool apply_rate = false;
    bool align_beat = true;
    if (!lua_isnoneornil(L, 4)) {
      fade_in = FArg(4);
    }
    if (!lua_isnoneornil(L, 5)) {
      fade_out = FArg(5);
    }
    if (!lua_isnoneornil(L, 6)) {
      loop = BArg(6);
    }
    if (!lua_isnoneornil(L, 7)) {
      apply_rate = BArg(7);
    }
    if (!lua_isnoneornil(L, 8)) {
      align_beat = BArg(8);
    }
    p->PlayMusic(
        music_path, nullptr, loop, music_start, music_length, fade_in, fade_out,
        align_beat, apply_rate);
    COMMON_RETURN_SELF;
  }

  static int StopMusic(T* p, lua_State* L) {
    p->StopMusic();
    COMMON_RETURN_SELF;
  }
  static int IsTimingDelayed(T* p, lua_State* L) {
    lua_pushboolean(L, g_Playing->timing_delayed);
    return 1;
  }

  LunaGameSoundManager() {
    ADD_METHOD(DimMusic);
    ADD_METHOD(PlayOnce);
    ADD_METHOD(PlayAnnouncer);
    ADD_METHOD(GetPlayerBalance);
    ADD_METHOD(PlayMusicPart);
    ADD_METHOD(StopMusic);
    ADD_METHOD(IsTimingDelayed);
  }
};

LUA_REGISTER_CLASS(GameSoundManager);

int LuaFunc_get_sound_driver_list(lua_State* L);
int LuaFunc_get_sound_driver_list(lua_State* L) {
  std::vector<RString> driver_names;
  split(RageSoundDriver::GetDefaultSoundDriverList(), ",", driver_names, true);
  lua_createtable(L, driver_names.size(), 0);
  for (std::size_t n = 0; n < driver_names.size(); ++n) {
    lua_pushstring(L, driver_names[n].c_str());
    lua_rawseti(L, -2, n + 1);
  }
  return 1;
}
LUAFUNC_REGISTER_COMMON(get_sound_driver_list);

/*
 * Copyright (c) 2003-2005 Glenn Maynard
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
