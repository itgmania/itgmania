#include "Foreground.h"

#include <algorithm>
#include <string>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "BackgroundUtil.h"
#include "GameState.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "Song.h"

Foreground::~Foreground() { Unload(); }

void Foreground::Unload() {
  for (unsigned i = 0; i < bg_animations_.size(); ++i) {
    delete bg_animations_[i].bga_;
  }
  bg_animations_.clear();
  sub_actors_.clear();
  last_music_seconds_ = -9999;
  song_ = nullptr;
}

void Foreground::LoadFromSong(const Song* song) {
  // Song graphics can get very big; never keep them in memory.
  RageTextureID::TexPolicy old_policy = TEXTUREMAN->GetDefaultTexturePolicy();
  TEXTUREMAN->SetDefaultTexturePolicy(RageTextureID::TEX_VOLATILE);

  song_ = song;
  for (const BackgroundChange& change : song->GetForegroundChanges()) {
    std::string bg_name = change.def_.file1_,
                lua_file = song->GetSongDir() + bg_name + "/default.lua",
                xml_file = song->GetSongDir() + bg_name + "/default.xml";

    LoadedBGA bga;
    if (DoesFileExist(lua_file)) {
      bga.bga_ = ActorUtil::MakeActor(lua_file, this);
    } else if (PREFSMAN->quirks_mode_ && DoesFileExist(xml_file)) {
      bga.bga_ = ActorUtil::MakeActor(xml_file, this);
    } else {
      bga.bga_ = ActorUtil::MakeActor(song->GetSongDir() + bg_name, this);
    }
    if (bga.bga_ == nullptr) {
      continue;
    }
    bga.bga_->SetName(bg_name);
    // ActorUtil::MakeActor calls LoadFromNode to load the actor, and
    // LoadFromNode takes care of running the InitCommand, so do not run the
    // InitCommand here. -Kyz
    bga.start_beat_ = change.start_beat_;
    bga.finished_ = false;

    bga.bga_->SetVisible(false);

    this->AddChild(bga.bga_);
    bg_animations_.push_back(bga);
  }

  TEXTUREMAN->SetDefaultTexturePolicy(old_policy);

  this->SortByDrawOrder();
}

void Foreground::Update(float f_delta_time) {
  // Calls to Update() should *not* be scaled by music rate unless
  // RateModsAffectFGChanges is enabled. Undo it.
  const float rate = PREFSMAN->rate_mods_affect_tweens_
                         ? 1.0f
                         : GAMESTATE->song_options_.GetCurrent().music_rate_;

  for (unsigned i = 0; i < bg_animations_.size(); ++i) {
    LoadedBGA& bga = bg_animations_[i];

    if (GAMESTATE->position_.song_beat_ < bga.start_beat_) {
      // The animation hasn't started yet.
      continue;
    }

    if (bga.finished_) {
      continue;
    }

    /* Update the actor even if we're about to hide it, so queued commands
     * are always run. */
    float l_delta_time;
    if (!bga.bga_->GetVisible()) {
      bga.bga_->SetVisible(true);
      bga.bga_->PlayCommand("On");

      const float start_second =
          song_->song_timing_.GetElapsedTimeFromBeat(bga.start_beat_);
      const float stop_second = start_second + bga.bga_->GetTweenTimeLeft();
      bga.stop_beat_ = song_->song_timing_.GetBeatFromElapsedTime(stop_second);

      l_delta_time = GAMESTATE->position_.music_seconds_ - start_second;
    } else {
      l_delta_time = GAMESTATE->position_.music_seconds_ - last_music_seconds_;
    }

    // This shouldn't go down, but be safe:
    l_delta_time = std::max(l_delta_time, 0.0f);

    bga.bga_->Update(l_delta_time / rate);

    if (GAMESTATE->position_.song_beat_ > bga.stop_beat_) {
      // Finished.
      bga.bga_->SetVisible(false);
      bga.finished_ = true;
      continue;
    }
  }

  last_music_seconds_ = GAMESTATE->position_.music_seconds_;
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
