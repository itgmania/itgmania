#include "global.h"

#include "Foreground.h"

#include "ActorUtil.h"
#include "BackgroundUtil.h"
#include "GameState.h"
#include "PrefsManager.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "Song.h"

Foreground::~Foreground() { Unload(); }

void Foreground::Unload() {
  for (unsigned i = 0; i < bg_animations_.size(); ++i) {
    delete bg_animations_[i].bga;
  }
  bg_animations_.clear();
  m_SubActors.clear();
  last_music_seconds_ = -9999;
  song_ = nullptr;
}

void Foreground::LoadFromSong(const Song* song) {
  // Song graphics can get very big; never keep them in memory.
  RageTextureID::TexPolicy old_policy = TEXTUREMAN->GetDefaultTexturePolicy();
  TEXTUREMAN->SetDefaultTexturePolicy(RageTextureID::TEX_VOLATILE);

  song_ = song;
  for (const BackgroundChange& change : song->GetForegroundChanges()) {
    RString bg_name = change.background_def_.file1_;
    RString lua_file = song->GetSongDir() + bg_name + "/default.lua";
    RString xml_file = song->GetSongDir() + bg_name + "/default.xml";

    LoadedBGA bga;
    if (DoesFileExist(lua_file)) {
      bga.bga = ActorUtil::MakeActor(lua_file, this);
    } else if (PREFSMAN->m_bQuirksMode && DoesFileExist(xml_file)) {
      bga.bga = ActorUtil::MakeActor(xml_file, this);
    } else {
      bga.bga = ActorUtil::MakeActor(song->GetSongDir() + bg_name, this);
    }
    if (bga.bga == nullptr) {
      continue;
    }
    bga.bga->SetName(bg_name);
    // ActorUtil::MakeActor calls LoadFromNode to load the actor, and
    // LoadFromNode takes care of running the InitCommand, so do not run the
    // InitCommand here. -Kyz
    bga.start_beat = change.start_beat_;
    bga.is_finished = false;

    bga.bga->SetVisible(false);

    AddChild(bga.bga);
    bg_animations_.push_back(bga);
  }

  TEXTUREMAN->SetDefaultTexturePolicy(old_policy);

  SortByDrawOrder();
}

void Foreground::Update(float /*delta*/) {
  // Calls to Update() should *not* be scaled by music rate unless
  // RateModsAffectFGChanges is enabled. Undo it.
  const float rate = PREFSMAN->m_bRateModsAffectTweens
                          ? 1.0f
                          : GAMESTATE->m_SongOptions.GetCurrent().m_fMusicRate;

  for (unsigned i = 0; i < bg_animations_.size(); ++i) {
    LoadedBGA& bga = bg_animations_[i];

    if (GAMESTATE->m_Position.m_fSongBeat < bga.start_beat) {
      // The animation hasn't started yet.
      continue;
    }

    if (bga.is_finished) {
      continue;
    }

    // Update the actor even if we're about to hide it, so queued commands
    // are always run.
    float delta_time;
    if (!bga.bga->GetVisible()) {
      bga.bga->SetVisible(true);
      bga.bga->PlayCommand("On");

      const float fStartSecond =
          song_->m_SongTiming.GetElapsedTimeFromBeat(bga.start_beat);
      const float fStopSecond = fStartSecond + bga.bga->GetTweenTimeLeft();
      bga.stop_beat =
          song_->m_SongTiming.GetBeatFromElapsedTime(fStopSecond);

      delta_time = GAMESTATE->m_Position.m_fMusicSeconds - fStartSecond;
    } else {
      delta_time = GAMESTATE->m_Position.m_fMusicSeconds - last_music_seconds_;
    }

    // This shouldn't go down, but be safe:
    delta_time = std::max(delta_time, 0.0f);

    bga.bga->Update(delta_time / rate);

    if (GAMESTATE->m_Position.m_fSongBeat > bga.stop_beat) {
      // Finished.
      bga.bga->SetVisible(false);
      bga.is_finished = true;
      continue;
    }
  }

  last_music_seconds_ = GAMESTATE->m_Position.m_fMusicSeconds;
}

void Foreground::HandleMessage(const Message& msg) {
  // We want foregrounds to behave as if their On command happens at the
  // starting beat, not when the Foreground object receives an On command.
  // So don't propagate that; we'll call it ourselves.
  if (msg.GetName() == "On") {
    Actor::HandleMessage(msg);
  } else {
    ActorFrame::HandleMessage(msg);
  }
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
