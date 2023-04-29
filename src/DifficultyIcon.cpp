#include "global.h"

#include "DifficultyIcon.h"

#include "ActorUtil.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Steps.h"
#include "Trail.h"
#include "XmlFile.h"

REGISTER_ACTOR_CLASS(DifficultyIcon);

DifficultyIcon::DifficultyIcon() {
  blank_ = false;
  player_number_ = PLAYER_1;
}

bool DifficultyIcon::Load(RString path) {
  Sprite::Load(path);
  int num_states = GetNumStates();
  bool warn = num_states != NUM_Difficulty && num_states != NUM_Difficulty * 2;
  if (path.find("_blank") != std::string::npos) {
    warn = false;
  }
  if (warn) {
    RString error = ssprintf(
        "The difficulty icon graphic '%s' must have %d or %d frames.  It has "
        "%d states.",
        path.c_str(), NUM_Difficulty, NUM_Difficulty * 2, num_states);
    LuaHelpers::ReportScriptError(error);
  }
  StopAnimating();
  return true;
}

void DifficultyIcon::LoadFromNode(const XNode* node) {
  RString file;
  if (!ActorUtil::GetAttrPath(node, "File", file)) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: DifficultyIcon: missing the \"File\" attribute.",
        ActorUtil::GetWhere(node).c_str());
  }

  Load(file);

  // skip Sprite::LoadFromNode
  Actor::LoadFromNode(node);
}

void DifficultyIcon::SetPlayer(PlayerNumber pn) { player_number_ = pn; }

void DifficultyIcon::SetFromSteps(PlayerNumber pn, const Steps* steps) {
  SetPlayer(pn);
  if (steps == nullptr) {
    Unset();
  } else {
    SetFromDifficulty(steps->GetDifficulty());
  }
}

void DifficultyIcon::SetFromTrail(PlayerNumber pn, const Trail* trail) {
  SetPlayer(pn);
  if (trail == nullptr) {
    Unset();
  } else {
    SetFromDifficulty(trail->m_CourseDifficulty);
  }
}

void DifficultyIcon::Unset() { blank_ = true; }

void DifficultyIcon::SetFromDifficulty(Difficulty difficulty) {
  blank_ = false;
  switch (GetNumStates()) {
    case NUM_Difficulty:
      SetState(difficulty);
      break;
    case NUM_Difficulty * 2:
      SetState(difficulty * 2 + player_number_);
      break;
    default:
      blank_ = true;
      break;
  }
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the DifficultyIcon.
class LunaDifficultyIcon : public Luna<DifficultyIcon> {
 public:
  static int SetFromSteps(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->Unset();
    } else {
      Steps* pS = Luna<Steps>::check(L, 1);
      p->SetFromSteps(PLAYER_1, pS);
    }
    COMMON_RETURN_SELF;
  }
  static int SetFromTrail(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->Unset();
    } else {
      Trail* pT = Luna<Trail>::check(L, 1);
      p->SetFromTrail(PLAYER_1, pT);
    }
    COMMON_RETURN_SELF;
  }
  static int Unset(T* p, lua_State* L) {
    p->Unset();
    COMMON_RETURN_SELF;
  }
  static int SetPlayer(T* p, lua_State* L) {
    p->SetPlayer(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int SetFromDifficulty(T* p, lua_State* L) {
    p->SetFromDifficulty(Enum::Check<Difficulty>(L, 1));
    COMMON_RETURN_SELF;
  }

  LunaDifficultyIcon() {
    ADD_METHOD(Unset);
    ADD_METHOD(SetPlayer);
    ADD_METHOD(SetFromSteps);
    ADD_METHOD(SetFromTrail);
    ADD_METHOD(SetFromDifficulty);
  }
};

LUA_REGISTER_DERIVED_CLASS(DifficultyIcon, Sprite)
// lua end

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
