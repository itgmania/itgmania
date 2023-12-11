#include "global.h"

#include "AutoActor.h"

#include "Actor.h"
#include "ActorUtil.h"
#include "ThemeManager.h"

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

AutoActor &AutoActor::operator=(const AutoActor& cpy) {
  Unload();

  if (cpy.actor_ == nullptr) {
    actor_ = nullptr;
  } else {
    actor_ = cpy.actor_->Copy();
	}
  return *this;
}

void AutoActor::Load(Actor* actor) {
  Unload();
  actor_ = actor;
}

void AutoActor::Load(const RString& path) {
  Unload();
  actor_ = ActorUtil::MakeActor(path);

  // If a Condition is false, MakeActor will return nullptr.
  if (actor_ == nullptr) {
		actor_ = new Actor;
	}
}

void AutoActor::LoadB(const RString& metrics_group, const RString& element) {
  ThemeManager::PathInfo pi;
  bool b = THEME->GetPathInfo(pi, EC_BGANIMATIONS, metrics_group, element);
  ASSERT(b);
  LuaThreadVariable var1("MatchingMetricsGroup", pi.sMatchingMetricsGroup);
  LuaThreadVariable var2("MatchingElement", pi.sMatchingElement);
  Load(pi.sResolvedPath);
}

void AutoActor::LoadActorFromNode(const XNode* node, Actor* parent) {
  Unload();

  actor_ = ActorUtil::LoadFromNode(node, parent);
}

void AutoActor::LoadAndSetName(
		const RString& screen_name, const RString& actor_name) {
  Load(THEME->GetPathG(screen_name, actor_name));
  actor_->SetName(actor_name);
  ActorUtil::LoadAllCommands(*actor_, screen_name);
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
