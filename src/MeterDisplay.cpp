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
    std::string sStreamPath, float fStreamWidth, std::string sTipPath) {
  spr_stream_.Load(sStreamPath);
  this->AddChild(spr_stream_);

  spr_tip_.Load(sTipPath);
  this->AddChild(spr_tip_);

  SetStreamWidth(fStreamWidth);
  SetPercent(0.5f);
}

void MeterDisplay::LoadFromNode(const XNode* pNode) {
  LOG->Trace(
      "MeterDisplay::LoadFromNode(%s)", ActorUtil::GetWhere(pNode).c_str());

  const XNode* pStream = pNode->GetChild("Stream");
  if (pStream == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: MeterDisplay: missing the \"Stream\" attribute",
        ActorUtil::GetWhere(pNode).c_str());
    return;
  }
  spr_stream_.LoadActorFromNode(pStream, this);
  spr_stream_->SetName("Stream");
  // LOAD_ALL_COMMANDS( m_sprStream );
  this->AddChild(spr_stream_);

  const XNode* pChild = pNode->GetChild("Tip");
  if (pChild != nullptr) {
    spr_tip_.LoadActorFromNode(pChild, this);
    spr_tip_->SetName("Tip");
    // LOAD_ALL_COMMANDS( m_sprTip );
    this->AddChild(spr_tip_);
  }

  float fStreamWidth = 0;
  pNode->GetAttrValue("StreamWidth", fStreamWidth);
  SetStreamWidth(fStreamWidth);

  SetPercent(0.5f);

  ActorFrame::LoadFromNode(pNode);
}

void MeterDisplay::SetPercent(float fPercent) {
  ASSERT(fPercent >= 0 && fPercent <= 1);

  spr_stream_->SetCropRight(1 - fPercent);

  if (spr_tip_.IsLoaded()) {
    spr_tip_->SetX(
        SCALE(fPercent, 0.f, 1.f, -stream_width_ / 2, stream_width_ / 2));
  }
}

void MeterDisplay::SetStreamWidth(float fStreamWidth) {
  stream_width_ = fStreamWidth;
  spr_stream_->SetZoomX(stream_width_ / spr_stream_->GetUnzoomedWidth());
}

void SongMeterDisplay::Update(float fDeltaTime) {
  if (GAMESTATE->cur_song_) {
    float fSongStartSeconds = GAMESTATE->cur_song_->GetFirstSecond();
    float fSongEndSeconds = GAMESTATE->cur_song_->GetLastSecond();
    float fPercentPositionSong = SCALE(
        GAMESTATE->position_.music_seconds_, fSongStartSeconds, fSongEndSeconds,
        0.0f, 1.0f);
    rage_clamp(fPercentPositionSong, 0, 1);

    SetPercent(fPercentPositionSong);
  }

  MeterDisplay::Update(fDeltaTime);
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
