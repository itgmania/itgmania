#include "GameSoundManager.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "AnnouncerManager.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LightsManager.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundManager.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Song.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "arch/Sound/RageSoundDriver.h"
#include "global.h"

GameSoundManager* SOUND = nullptr;

/*
 * When playing music, automatically search for an SM file for timing data.  If
 * one is found, automatically handle GAMESTATE->m_fSongBeat, etc.
 *
 * std::modf(GAMESTATE->m_fSongBeat) should always be continuously moving from 0
 * to 1.  To do this, wait before starting a sound until the fractional portion
 * of the beat will be the same.
 *
 * If PlayMusic(fLengthSeconds) is set, peek at the beat, and extend the length
 * so we'll be on the same fractional beat when we loop.
 */

/* Lock this before touching g_UpdatingTimer or g_Playing. */
static RageEvent* mutex;
static bool updating_timer;
static bool g_shutdown;
static bool g_flushing = false;

enum FadeState { FADE_NONE, FADE_OUT, FADE_WAIT, FADE_IN };
static FadeState fade_state = FADE_NONE;
static float dim_volume = 1.0f;
static float original_volume = 1.0f;
static float dim_duration_remaining = 0.0f;
static bool was_playing_on_last_update = false;

struct MusicPlaying {
  bool timing_delayed_;
  bool has_timing_;
  bool apply_music_rate_;
  // The timing data that we're currently using.
  TimingData timing_;
  NoteData lights_;

  /* If m_bTimingDelayed is true, this will be the timing data for the
   * song that's starting. We'll copy it to m_Timing once sound is heard. */
  TimingData new_timing_;
  RageSound* music_;
  MusicPlaying(RageSound* music) {
    timing_.AddSegment(BPMSegment(0, 120));
    new_timing_.AddSegment(BPMSegment(0, 120));
    has_timing_ = false;
    timing_delayed_ = false;
    apply_music_rate_ = false;
    music_ = music;
  }

  ~MusicPlaying() { delete music_; }
};

static MusicPlaying* playing;

static RageThread music_thread;

std::vector<std::string> g_sounds_to_play_once;
std::vector<std::string> g_sounds_to_play_once_from_dir;
std::vector<std::string> g_sounds_to_play_once_from_announcer;
// This should get updated to unordered_map when once C++11 is supported
std::map<std::string, std::vector<int>> dir_sound_order;

struct MusicToPlay {
  std::string file_, timing_file_;
  bool has_timing_;
  TimingData timing_data_;
  NoteData lights_data_;
  bool force_loop_;
  float start_second_, length_seconds_, fade_in_length_seconds_,
      fade_out_length_seconds_;
  bool align_beat_, apply_music_rate_;
  MusicToPlay() { has_timing_ = false; }
};
std::vector<MusicToPlay> g_musics_to_play;
static GameSoundManager::PlayMusicParams g_fallback_music_params;

static void StartMusic(MusicToPlay& to_play) {
  LockMutex l(*mutex);
  if (playing->music_->IsPlaying() &&
      EqualsNoCase(playing->music_->GetLoadedFilePath(), to_play.file_)) {
    return;
  }

  /* We're changing or stopping the music.  If we were dimming, reset. */
  fade_state = FADE_NONE;

  if (to_play.file_.empty()) {
    /* StopPlaying() can take a while, so don't hold the lock while we stop the
     * sound. Be sure to leave the rest of g_Playing in place. */
    RageSound* old_sound = playing->music_;
    playing->music_ = new RageSound;
    l.Unlock();

    delete old_sound;
    return;
  }

  /* Unlock, load the sound here, and relock.  Loading may take a while if we're
   * reading from CD and we have to seek far, which can throw off the timing
   * below. */
  MusicPlaying* new_music;
  {
    mutex->Unlock();
    RageSound* sound = new RageSound;
    RageSoundLoadParams params;
    params.support_rate_changing_ = to_play.apply_music_rate_;
    sound->Load(to_play.file_, false, &params);
    mutex->Lock();

    new_music = new MusicPlaying(sound);
  }

  new_music->timing_ = playing->timing_;
  new_music->lights_ = playing->lights_;

  /* See if we can find timing data, if it's not already loaded. */
  if (!to_play.has_timing_ && IsAFile(to_play.timing_file_)) {
    LOG->Trace("Found '%s'", to_play.timing_file_.c_str());
    Song song;
    SSCLoader loader_ssc;
    SMLoader loader_sm;
    if (GetExtension(to_play.timing_file_) == ".ssc" &&
        loader_ssc.LoadFromSimfile(to_play.timing_file_, song)) {
      to_play.has_timing_ = true;
      to_play.timing_data_ = song.song_timing_;
      // get cabinet lights if any
      Steps* steps_cabinet_lights =
          SongUtil::GetOneSteps(&song, StepsType_lights_cabinet);
      if (steps_cabinet_lights) {
        steps_cabinet_lights->GetNoteData(to_play.lights_data_);
      }
    } else if (
        GetExtension(to_play.timing_file_) == ".sm" &&
        loader_sm.LoadFromSimfile(to_play.timing_file_, song)) {
      to_play.has_timing_ = true;
      to_play.timing_data_ = song.song_timing_;
      // get cabinet lights if any
      Steps* steps_cabinet_lights =
          SongUtil::GetOneSteps(&song, StepsType_lights_cabinet);
      if (steps_cabinet_lights) {
        steps_cabinet_lights->GetNoteData(to_play.lights_data_);
      }
    }
  }

  if (to_play.has_timing_) {
    new_music->new_timing_ = to_play.timing_data_;
    new_music->lights_ = to_play.lights_data_;
  }

  if (to_play.align_beat_ && to_play.has_timing_ && to_play.force_loop_ &&
      to_play.length_seconds_ != -1) {
    /* Extend the loop period so it always starts and ends on the same
     * fractional beat.  That is, if it starts on beat 1.5, and ends on
     * beat 10.2, extend it to end on beat 10.5.  This way, effects always loop
     * cleanly. */
    float start_beat = new_music->new_timing_.GetBeatFromElapsedTimeNoOffset(
        to_play.start_second_);
    float end_sec = to_play.start_second_ + to_play.length_seconds_;
    float end_beat =
        new_music->new_timing_.GetBeatFromElapsedTimeNoOffset(end_sec);

    const float start_beat_fraction = fmodfp(start_beat, 1);
    const float end_beat_fraction = fmodfp(end_beat, 1);

    float beat_difference = start_beat_fraction - end_beat_fraction;
    if (beat_difference < 0) {
      beat_difference += 1.0f; /* unwrap */
    }

    end_beat += beat_difference;

    const float real_end_sec =
        new_music->new_timing_.GetElapsedTimeFromBeatNoOffset(end_beat);
    const float new_length_sec = real_end_sec - to_play.start_second_;

    /* Extend fFadeOutLengthSeconds, so the added time is faded out. */
    to_play.fade_out_length_seconds_ +=
        new_length_sec - to_play.length_seconds_;
    to_play.length_seconds_ = new_length_sec;
  }

  bool start_immediately = false;
  if (!to_play.has_timing_) {
    /* This song has no real timing data.  The offset is arbitrary.  Change it
     * so the beat will line up to where we are now, so we don't have to delay.
     */
    float dest_beat = fmodfp(GAMESTATE->position_.song_beat_no_offset_, 1);
    float time =
        new_music->new_timing_.GetElapsedTimeFromBeatNoOffset(dest_beat);

    new_music->new_timing_.beat0_offset_in_seconds_ = time;

    start_immediately = true;
  }

  /* If we have an active timer, try to start on the next update.  Otherwise,
   * start now. */
  if (!playing->has_timing_ && !updating_timer) {
    start_immediately = true;
  }
  if (!to_play.align_beat_) {
    start_immediately = true;
  }

  RageTimer when; /* zero */
  if (!start_immediately) {
    /* GetPlayLatency returns the minimum time until a sound starts.  That's
     * common when starting a precached sound, but our sound isn't, so it'll
     * probably take a little longer.  Nudge the latency up. */
    const float presumed_latency = SOUNDMAN->GetPlayLatency() + 0.040f;
    const float cur_second =
        GAMESTATE->position_.music_seconds_ + presumed_latency;
    const float cur_beat =
        playing->timing_.GetBeatFromElapsedTimeNoOffset(cur_second);

    /* The beat that the new sound will start on. */
    const float start_beat =
        new_music->new_timing_.GetBeatFromElapsedTimeNoOffset(
            to_play.start_second_);
    const float start_beat_fraction = fmodfp(start_beat, 1);

    float cur_beat_to_start_on = std::trunc(cur_beat) + start_beat_fraction;
    if (cur_beat_to_start_on < cur_beat) {
      cur_beat_to_start_on += 1.0f;
    }

    const float second_to_start_on =
        playing->timing_.GetElapsedTimeFromBeatNoOffset(cur_beat_to_start_on);
    const float maximum_distance = 2;
    const float distance = std::min(
        second_to_start_on - GAMESTATE->position_.music_seconds_,
        maximum_distance);

    when = GAMESTATE->position_.last_beat_update_ + distance;
  }

  /* Important: don't hold the mutex while we load and seek the actual sound. */
  l.Unlock();
  {
    new_music->has_timing_ = to_play.has_timing_;
    if (to_play.has_timing_) {
      new_music->new_timing_ = to_play.timing_data_;
    }
    new_music->timing_delayed_ = true;
    new_music->apply_music_rate_ = to_play.apply_music_rate_;
    //		NewMusic->m_Music->Load( ToPlay.m_sFile, false );

    RageSoundParams p;
    p.start_second_ = to_play.start_second_;
    p.length_seconds_ = to_play.length_seconds_;
    p.fade_in_seconds_ = to_play.fade_in_length_seconds_;
    p.fade_out_seconds_ = to_play.fade_out_length_seconds_;
    p.start_time_ = when;
    if (to_play.force_loop_) {
      p.stop_mode_ = RageSoundParams::M_LOOP;
    }
    new_music->music_->SetParams(p);
    new_music->music_->StartPlaying();
  }

  LockMut(*mutex);
  delete playing;
  playing = new_music;
}

static void DoPlayOnce(std::string path) {
  /* We want this to start quickly, so don't try to prebuffer it. */
  RageSound* sound = new RageSound;
  sound->Load(path, false);

  sound->Play(false);
  sound->DeleteSelfWhenFinishedPlaying();
}

static void DoPlayOnceFromDir(std::string path) {
  if (path == "") {
    return;
  }

  // make sure there's a slash at the end of this path
  if (Right(path, 1) != "/") {
    path += "/";
  }

  std::vector<std::string> array_sound_files;
  GetDirListing(path + "*.mp3", array_sound_files);
  GetDirListing(path + "*.wav", array_sound_files);
  GetDirListing(path + "*.ogg", array_sound_files);
  GetDirListing(path + "*.oga", array_sound_files);

  if (array_sound_files.empty()) {
    return;
  } else if (array_sound_files.size() == 1) {
    DoPlayOnce(path + array_sound_files[0]);
    return;
  }

  mutex->Lock();
  std::vector<int>& order =
      dir_sound_order.insert({path, std::vector<int>()}).first->second;
  // If order is exhausted, repopulate and reshuffle
  if (order.size() == 0) {
    for (int i = 0; i < (int)array_sound_files.size(); ++i) {
      order.push_back(i);
    }
    std::shuffle(order.begin(), order.end(), random_number_generator);
  }

  int index = order.back();
  order.pop_back();
  mutex->Unlock();

  DoPlayOnce(path + array_sound_files[index]);
}

static bool SoundWaiting() {
  return !g_sounds_to_play_once.empty() ||
         !g_sounds_to_play_once_from_dir.empty() ||
         !g_sounds_to_play_once_from_announcer.empty() ||
         !g_musics_to_play.empty();
}

static void StartQueuedSounds() {
  mutex->Lock();
  std::vector<std::string> sounds_to_play_once = g_sounds_to_play_once;
  g_sounds_to_play_once.clear();
  std::vector<std::string> sounds_to_play_once_from_dir =
      g_sounds_to_play_once_from_dir;
  g_sounds_to_play_once_from_dir.clear();
  std::vector<std::string> sounds_to_play_once_from_announcer =
      g_sounds_to_play_once_from_announcer;
  g_sounds_to_play_once_from_announcer.clear();
  std::vector<MusicToPlay> musics_to_play = g_musics_to_play;
  g_musics_to_play.clear();
  mutex->Unlock();

  for (unsigned i = 0; i < sounds_to_play_once.size(); ++i) {
    if (sounds_to_play_once[i] != "") {
      DoPlayOnce(sounds_to_play_once[i]);
    }
  }

  for (unsigned i = 0; i < sounds_to_play_once_from_dir.size(); ++i) {
    DoPlayOnceFromDir(sounds_to_play_once_from_dir[i]);
  }

  for (unsigned i = 0; i < sounds_to_play_once_from_announcer.size(); ++i) {
    std::string path = sounds_to_play_once_from_announcer[i];
    if (path != "") {
      path = ANNOUNCER->GetPathTo(path);
      DoPlayOnceFromDir(path);
    }
  }

  for (unsigned i = 0; i < musics_to_play.size(); ++i) {
    /* Don't bother starting this music if there's another one in the queue
     * after it. */
    /* Actually, it's a little trickier: the editor gives us a stop and then a
     * sound in quick succession; if we ignore the stop, we won't rewind the
     * sound if it was already playing.  We don't want to waste time loading a
     * sound if it's going to be replaced immediately, though.  So, if we have
     * more music in the queue, then forcibly stop the current sound. */
    if (i + 1 == musics_to_play.size()) {
      StartMusic(musics_to_play[i]);
    } else {
      CHECKPOINT_M(ssprintf("Removing old sound at index %d", i));
      /* StopPlaying() can take a while, so don't hold the lock while we stop
       * the sound. */
      mutex->Lock();
      RageSound* old_sound = playing->music_;
      playing->music_ = new RageSound;
      mutex->Unlock();

      delete old_sound;
    }
  }
}

void GameSoundManager::Flush() {
  mutex->Lock();
  g_flushing = true;

  mutex->Broadcast();

  while (g_flushing) {
    mutex->Wait();
  }
  mutex->Unlock();
}

int MusicThread_start(void* p) {
  while (!g_shutdown) {
    mutex->Lock();
    while (!SoundWaiting() && !g_shutdown && !g_flushing) {
      mutex->Wait();
    }
    mutex->Unlock();

    /* This is a little hack: we want to make sure that we go through
     * StartQueuedSounds after Flush() is called, to be sure we're flushed,
     * so check g_bFlushing before calling.  This won't work if more than
     * one thread might call Flush(), but only the main thread is allowed
     * to make SOUND calls. */
    bool flushing = g_flushing;

    StartQueuedSounds();

    if (flushing) {
      mutex->Lock();
      mutex->Signal();
      g_flushing = false;
      mutex->Unlock();
    }
  }

  return 0;
}

GameSoundManager::GameSoundManager() {
  /* Init RageSoundMan first: */
  ASSERT(SOUNDMAN != nullptr);

  mutex = new RageEvent("GameSoundManager");
  playing = new MusicPlaying(new RageSound);

  updating_timer = true;

  g_shutdown = false;
  music_thread.SetName("Music thread");
  music_thread.Create(MusicThread_start, this);

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

  /* Signal the mixing thread to quit. */
  LOG->Trace("Shutting down music start thread ...");
  mutex->Lock();
  g_shutdown = true;
  mutex->Broadcast();
  mutex->Unlock();
  music_thread.Wait();
  LOG->Trace("Music start thread shut down.");

  RageUtil::SafeDelete(playing);
  RageUtil::SafeDelete(mutex);
}

void GameSoundManager::Update(float delta_time) {
  {
    mutex->Lock();
    if (playing->apply_music_rate_) {
      RageSoundParams p = playing->music_->GetParams();
      float rate = GAMESTATE->song_options_.GetPreferred().music_rate_;
      if (p.speed_ != rate) {
        p.speed_ = rate;
        playing->music_->SetParams(p);
      }
    }

    bool is_playing = playing->music_->IsPlaying();
    mutex->Unlock();
    if (!is_playing && was_playing_on_last_update &&
        !g_fallback_music_params.file_.empty()) {
      PlayMusic(g_fallback_music_params);

      g_fallback_music_params.file_ = "";
    }
    was_playing_on_last_update = is_playing;
  }

  LockMut(*mutex);

  {
    /* Duration of the fade-in and fade-out: */
    // const float fFadeInSpeed = 1.5f;
    // const float fFadeOutSpeed = 0.3f;
    float fade_in_speed = playing->music_->GetParams().fade_in_seconds_;
    float fade_out_speed = playing->music_->GetParams().fade_out_seconds_;
    float volume = playing->music_->GetParams().volume_;
    switch (fade_state) {
      case FADE_NONE:
        break;
      case FADE_OUT:
        if (fade_out_speed > 0.0) {
          fapproach(volume, dim_volume, delta_time / fade_out_speed);
        } else {
          // Treat an invalid fade speed as instant
          volume = dim_volume;
        }
        if (std::abs(volume - dim_volume) < 0.001f) {
          fade_state = FADE_WAIT;
        }
        break;
      case FADE_WAIT:
        dim_duration_remaining -= delta_time;
        if (dim_duration_remaining <= 0) {
          fade_state = FADE_IN;
        }
        break;
      case FADE_IN:
        if (fade_in_speed > 0.0) {
          fapproach(volume, original_volume, delta_time / fade_in_speed);
        } else {
          // Treat an invalid fade speed as instant
          volume = original_volume;
        }
        if (std::abs(volume - original_volume) < 0.001f) {
          fade_state = FADE_NONE;
        }
        break;
    }

    RageSoundParams p = playing->music_->GetParams();
    if (p.volume_ != volume) {
      p.volume_ = volume;
      playing->music_->SetParams(p);
    }
  }

  if (!updating_timer) {
    return;
  }

  if (!playing->music_->IsPlaying()) {
    /* There's no song playing.  Fake it. */
    CHECKPOINT_M(ssprintf(
        "%f, delta %f", GAMESTATE->position_.music_seconds_, delta_time));
    GAMESTATE->UpdateSongPosition(
        GAMESTATE->position_.music_seconds_ +
            delta_time * playing->music_->GetPlaybackRate(),
        playing->timing_);
    return;
  }

  /* There's a delay between us calling Play() and the sound actually playing.
   * Keep using the previous timing data until we get a non-approximate time,
   * indicating that the sound has actually started playing. */
  RageTimer tm;
  const float seconds = playing->music_->GetPositionSeconds(&tm);

  // Check for song timing skips.
  if (PREFSMAN->log_skips_ && !playing->timing_delayed_) {
    const float expected_time_passed =
        (tm - GAMESTATE->position_.last_beat_update_) *
        playing->music_->GetPlaybackRate();
    const float sound_time_passed =
        seconds - GAMESTATE->position_.music_seconds_;
    const float diff = expected_time_passed - sound_time_passed;

    static std::string last_file = "";
    const std::string this_file = playing->music_->GetLoadedFilePath();

    /* If fSoundTimePassed < 0, the sound has probably looped. */
    if (last_file == this_file && sound_time_passed >= 0 &&
        std::abs(diff) > 0.003f) {
      LOG->Trace(
          "Song position skip in %s: expected %.3f, got %.3f (cur %f, prev %f) "
          "(%.3f difference)",
          Basename(this_file).c_str(), expected_time_passed, sound_time_passed,
          seconds, GAMESTATE->position_.music_seconds_, diff);
    }
    last_file = this_file;
  }

  // If g_Playing->m_bTimingDelayed, we're waiting for the new music to actually
  // start playing.
  if (playing->timing_delayed_) {
    /* Load up the new timing data. */
    playing->timing_ = playing->new_timing_;
    playing->timing_delayed_ = false;
  }

  if (playing->timing_delayed_) {
    /* We're still waiting for the new sound to start playing, so keep using the
     * old timing data and fake the time. */
    GAMESTATE->UpdateSongPosition(
        GAMESTATE->position_.music_seconds_ + delta_time, playing->timing_);
  } else {
    GAMESTATE->UpdateSongPosition(seconds, playing->timing_, tm);
  }

  // Send crossed messages
  if (GAMESTATE->cur_song_) {
    static int beat_last_crossed = 0;

    float song_beat = GAMESTATE->position_.song_beat_;

    int row_now = BeatToNoteRowNotRounded(song_beat);
    row_now = std::max(0, row_now);

    int beat_now = row_now / kRowsPerBeat;

    for (int beat = beat_last_crossed + 1; beat <= beat_now; ++beat) {
      Message msg("CrossedBeat");
      msg.SetParam("Beat", beat);
      MESSAGEMAN->Broadcast(msg);
    }

    beat_last_crossed = beat_now;
  }

  // Update lights
  NoteData& lights = playing->lights_;
  if (lights.GetNumTracks() > 0)  // lights data was loaded
  {
    const float song_beat = GAMESTATE->position_.light_song_beat_;
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
        if (lights.GetTapNote(cl, r).type_ != TapNoteType_Empty) {
          LIGHTSMAN->BlinkCabinetLight(cl);
          break;
        }
      }
    }

    row_last_crossed = song_row;
  }
}

std::string GameSoundManager::GetMusicPath() const {
  LockMut(*mutex);
  return playing->music_->GetLoadedFilePath();
}

void GameSoundManager::PlayMusic(
    std::string file, const TimingData* timing, bool force_loop,
    float start_second, float length_seconds, float fade_in_length_seconds,
    float fade_out_length_seconds, bool align_beat, bool apply_music_rate) {
  PlayMusicParams params;
  params.file_ = file;
  params.timing_ = timing;
  params.force_loop_ = force_loop;
  params.start_second_ = start_second;
  params.length_seconds_ = length_seconds;
  params.fade_in_length_seconds_ = fade_in_length_seconds;
  params.fade_out_length_seconds_ = fade_out_length_seconds;
  params.align_beat_ = align_beat;
  params.apply_music_rate_ = apply_music_rate;
  PlayMusic(params);
}

void GameSoundManager::PlayMusic(
    PlayMusicParams params, PlayMusicParams fallback_music_params) {
  g_fallback_music_params = fallback_music_params;

  //	LOG->Trace("play '%s' (current '%s')", file.c_str(),
  // g_Playing->m_Music->GetLoadedFilePath().c_str());

  MusicToPlay to_play;

  to_play.file_ = params.file_;
  if (params.timing_) {
    to_play.has_timing_ = true;
    to_play.timing_data_ = *params.timing_;
  } else {
    /* If no timing data was provided, look for it in the same place as the
     * music file. */
    // todo: allow loading .ssc files as well -aj
    to_play.timing_file_ = SetExtension(params.file_, "sm");
  }

  to_play.force_loop_ = params.force_loop_;
  to_play.start_second_ = params.start_second_;
  to_play.length_seconds_ = params.length_seconds_;
  to_play.fade_in_length_seconds_ = params.fade_in_length_seconds_;
  to_play.fade_out_length_seconds_ = params.fade_out_length_seconds_;
  to_play.align_beat_ = params.align_beat_;
  to_play.apply_music_rate_ = params.apply_music_rate_;

  /* Add the MusicToPlay to the g_MusicsToPlay queue. */
  mutex->Lock();
  g_musics_to_play.push_back(to_play);
  mutex->Broadcast();
  mutex->Unlock();
}

void GameSoundManager::DimMusic(float volume, float duration_seconds) {
  LockMut(*mutex);

  if (fade_state == FADE_NONE) {
    original_volume = playing->music_->GetParams().volume_;
  }
  // otherwise, g_fOriginalVolume is already set and m_Volume will be the
  // current state, not the original state

  dim_duration_remaining = duration_seconds;
  dim_volume = volume;
  fade_state = FADE_OUT;
}

void GameSoundManager::HandleSongTimer(bool on) {
  LockMut(*mutex);
  updating_timer = on;
}

void GameSoundManager::PlayOnce(std::string path) {
  /* Add the sound to the g_SoundsToPlayOnce queue. */
  mutex->Lock();
  g_sounds_to_play_once.push_back(path);
  mutex->Broadcast();
  mutex->Unlock();
}

void GameSoundManager::PlayOnceFromDir(std::string path) {
  /* Add the path to the g_SoundsToPlayOnceFromDir queue. */
  mutex->Lock();
  g_sounds_to_play_once_from_dir.push_back(path);
  mutex->Broadcast();
  mutex->Unlock();
}

void GameSoundManager::PlayOnceFromAnnouncer(std::string path) {
  /* Add the path to the g_SoundsToPlayOnceFromAnnouncer queue. */
  mutex->Lock();
  g_sounds_to_play_once_from_announcer.push_back(path);
  mutex->Broadcast();
  mutex->Unlock();
}

float GameSoundManager::GetPlayerBalance(PlayerNumber pn) {
  /* If two players are active, play sounds on each players' side. */
  if (GAMESTATE->GetNumPlayersEnabled() == 2) {
    return (pn == PLAYER_1) ? -1.0f : 1.0f;
  } else {
    return 0;
  }
}

#include "LuaBinding.h"

/** @brief Allow Lua to have access to the GameSoundManager. */
class LunaGameSoundManager : public Luna<GameSoundManager> {
 public:
  static int DimMusic(T* p, lua_State* L) {
    float volume = FArg(1);
    float duration_seconds = FArg(2);
    p->DimMusic(volume, duration_seconds);
    COMMON_RETURN_SELF;
  }
  static int PlayOnce(T* p, lua_State* L) {
    std::string path = SArg(1);
    if (lua_toboolean(L, 2) && PREFSMAN->mute_actions_) {
      COMMON_RETURN_SELF;
    }
    p->PlayOnce(path);
    COMMON_RETURN_SELF;
  }
  static int PlayAnnouncer(T* p, lua_State* L) {
    std::string path = SArg(1);
    p->PlayOnceFromAnnouncer(path);
    COMMON_RETURN_SELF;
  }
  static int GetPlayerBalance(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushnumber(L, p->GetPlayerBalance(pn));
    return 1;
  }
  static int PlayMusicPart(T* p, lua_State* L) {
    std::string music_path = SArg(1);
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
    lua_pushboolean(L, playing->timing_delayed_);
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
  std::vector<std::string> driver_names = RageSoundDriver::GetSoundDriverList();
  lua_createtable(L, driver_names.size(), 0);
  for (size_t n = 0; n < driver_names.size(); ++n) {
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
