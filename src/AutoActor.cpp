#include "AutoActor.h"

#include <string>

#include "Actor.h"
#include "ActorUtil.h"
#include "LuaManager.h"
#include "ThemeManager.h"
#include "global.h"

void AutoActor::Unload() {
  if (actor_ != nullptr) {
    delete actor_;
  }
  actor_ = nullptr;
}

AutoActor::AutoActor(const AutoActor& cpy) {
  if (cpy.actor_ == nullptr) {
    actor_ = nullptr;
  } else {
    actor_ = cpy.actor_->Copy();
  }
}

AutoActor& AutoActor::operator=(const AutoActor& cpy) {
  Unload();

  if (cpy.actor_ == nullptr) {
    actor_ = nullptr;
  } else {
    actor_ = cpy.actor_->Copy();
  }
  return *this;
}

void AutoActor::Load(Actor* pActor) {
  Unload();
  actor_ = pActor;
}

void AutoActor::Load(const std::string& sPath) {
  Unload();
  actor_ = ActorUtil::MakeActor(sPath);

  // If a Condition is false, MakeActor will return nullptr.
  if (actor_ == nullptr) {
    actor_ = new Actor;
  }
}

void AutoActor::LoadB(
    const std::string& sMetricsGroup, const std::string& sElement) {
  ThemeManager::PathInfo pi;
  bool b = THEME->GetPathInfo(pi, EC_BGANIMATIONS, sMetricsGroup, sElement);
  ASSERT(b);
  LuaThreadVariable var1("MatchingMetricsGroup", pi.matching_metrics_group_);
  LuaThreadVariable var2("MatchingElement", pi.matching_element_);
  Load(pi.resolved_path_);
}

void AutoActor::LoadActorFromNode(const XNode* pNode, Actor* pParent) {
  Unload();

  actor_ = ActorUtil::LoadFromNode(pNode, pParent);
}

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
