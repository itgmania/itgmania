#include "Transition.h"

#include <string>

#include "ActorFrame.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"

Transition::Transition() { state_ = waiting; }

void Transition::Load(std::string sBGAniDir) {
  this->RemoveAllChildren();

  spr_transition_.Load(sBGAniDir);
  this->AddChild(spr_transition_);

  state_ = waiting;
}

void Transition::UpdateInternal(float fDeltaTime) {
  if (state_ != transitioning) {
    return;
  }

  // Check this before running Update, so we draw the last frame of the finished
  // transition before sending m_MessageToSendWhenDone.
  if (spr_transition_->GetTweenTimeLeft() == 0)  // over
  {
    state_ = finished;
    SCREENMAN->SendMessageToTopScreen(message_to_send_when_done_);
  }

  ActorFrame::UpdateInternal(fDeltaTime);
}

void Transition::Reset() {
  state_ = waiting;
  first_update_ = true;

  if (spr_transition_.IsLoaded()) {
    spr_transition_->FinishTweening();
  }
}

bool Transition::EarlyAbortDraw() const { return state_ == waiting; }

void Transition::StartTransitioning(ScreenMessage send_when_done) {
  if (state_ != waiting) {
    return;  // ignore
  }

  // If transition isn't loaded don't set state to transitioning.
  // We assume elsewhere that m_sprTransition is loaded.
  if (!spr_transition_.IsLoaded()) {
    return;
  }

  spr_transition_->PlayCommand("StartTransitioning");

  message_to_send_when_done_ = send_when_done;
  state_ = transitioning;
}

float Transition::GetTweenTimeLeft() const {
  if (state_ != transitioning) {
    return 0;
  }

  return spr_transition_->GetTweenTimeLeft();
}

/*
 * (c) 2001-2004 Chris Danford
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
