#include "MeterDisplay.h"

#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "GameState.h"
#include "LuaManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(MeterDisplay);
REGISTER_ACTOR_CLASS(SongMeterDisplay);

MeterDisplay::MeterDisplay() {}

void MeterDisplay::Load(
    std::string stream_path, float stream_width, std::string tip_path) {
  spr_stream_.Load(stream_path);
  this->AddChild(spr_stream_);

  spr_tip_.Load(tip_path);
  this->AddChild(spr_tip_);

  SetStreamWidth(stream_width);
  SetPercent(0.5f);
}

void MeterDisplay::LoadFromNode(const XNode* node) {
  LOG->Trace(
      "MeterDisplay::LoadFromNode(%s)", ActorUtil::GetWhere(node).c_str());

  const XNode* stream = node->GetChild("Stream");
  if (stream == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: MeterDisplay: missing the \"Stream\" attribute",
        ActorUtil::GetWhere(node).c_str());
    return;
  }
  spr_stream_.LoadActorFromNode(stream, this);
  spr_stream_->SetName("Stream");
  // LOAD_ALL_COMMANDS( m_sprStream );
  this->AddChild(spr_stream_);

  const XNode* child = node->GetChild("Tip");
  if (child != nullptr) {
    spr_tip_.LoadActorFromNode(child, this);
    spr_tip_->SetName("Tip");
    // LOAD_ALL_COMMANDS( m_sprTip );
    this->AddChild(spr_tip_);
  }

  float stream_width = 0;
  node->GetAttrValue("StreamWidth", stream_width);
  SetStreamWidth(stream_width);

  SetPercent(0.5f);

  ActorFrame::LoadFromNode(node);
}

void MeterDisplay::SetPercent(float percent) {
  ASSERT(percent >= 0 && percent <= 1);

  spr_stream_->SetCropRight(1 - percent);

  if (spr_tip_.IsLoaded()) {
    spr_tip_->SetX(
        SCALE(percent, 0.f, 1.f, -stream_width_ / 2, stream_width_ / 2));
  }
}

void MeterDisplay::SetStreamWidth(float stream_width) {
  stream_width_ = stream_width;
  spr_stream_->SetZoomX(stream_width_ / spr_stream_->GetUnzoomedWidth());
}

void SongMeterDisplay::Update(float delta_time) {
  if (GAMESTATE->cur_song_) {
    float song_start_seconds = GAMESTATE->cur_song_->GetFirstSecond();
    float song_end_seconds = GAMESTATE->cur_song_->GetLastSecond();
    float percent_position_song = SCALE(
        GAMESTATE->position_.music_seconds_, song_start_seconds,
        song_end_seconds, 0.0f, 1.0f);
    rage_clamp(percent_position_song, 0, 1);

    SetPercent(percent_position_song);
  }

  MeterDisplay::Update(delta_time);
}

// lua start
#include "LuaBinding.h"

class LunaMeterDisplay : public Luna<MeterDisplay> {
 public:
  static int SetStreamWidth(T* p, lua_State* L) {
    p->SetStreamWidth(FArg(1));
    COMMON_RETURN_SELF;
  }

  LunaMeterDisplay() { ADD_METHOD(SetStreamWidth); }
};

LUA_REGISTER_DERIVED_CLASS(MeterDisplay, ActorFrame)
// lua end

/*
 * (c) 2003-2004 Chris Danford
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
