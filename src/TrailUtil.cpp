#include "TrailUtil.h"

#include <numeric>
#include <string>

#include "Course.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "LuaManager.h"
#include "RageException.h"
#include "Song.h"
#include "Trail.h"
#include "XmlFile.h"
#include "global.h"

int TrailUtil::GetNumSongs(const Trail* trail) {
  return trail->entries_.size();
}

float TrailUtil::GetTotalSeconds(const Trail* trail) {
  const auto& entries = trail->entries_;
  return std::accumulate(
      entries.begin(), entries.end(), 0.f,
      [](float total, const TrailEntry& e) {
        return total + e.song_->music_length_seconds_;
      });
}

///////////////////////////////////////////////////////////////////////

void TrailID::FromTrail(const Trail* p) {
  if (p == nullptr) {
    st_ = StepsType_Invalid;
    cd_ = Difficulty_Invalid;
  } else {
    st_ = p->steps_type_;
    cd_ = p->course_difficulty_;
  }
}

XNode* TrailID::CreateNode() const {
  XNode* node = new XNode("Trail");

  node->AppendAttr("StepsType", GAMEMAN->GetStepsTypeInfo(st_).name_);
  node->AppendAttr("CourseDifficulty", DifficultyToString(cd_));

  return node;
}

void TrailID::LoadFromNode(const XNode* node) {
  ASSERT(node->GetName() == "Trail");

  std::string temp;

  node->GetAttrValue("StepsType", temp);
  st_ = GAMEMAN->StringToStepsType(temp);

  node->GetAttrValue("CourseDifficulty", temp);
  cd_ = StringToDifficulty(temp);
}

bool TrailID::IsValid() const {
  return st_ != StepsType_Invalid && cd_ != Difficulty_Invalid;
}

bool TrailID::operator<(const TrailID& rhs) const {
#define COMP(a)               \
  if (a < rhs.a) return true; \
  if (a > rhs.a) return false;
  COMP(st_);
  COMP(cd_);
#undef COMP
  return false;
}

// lua start
#include "LuaBinding.h"

namespace {
int GetNumSongs(lua_State* L) {
  Trail* trail = Luna<Trail>::check(L, 1, true);
  int num = TrailUtil::GetNumSongs(trail);
  LuaHelpers::Push(L, num);
  return 1;
}
int GetTotalSeconds(lua_State* L) {
  Trail* trail = Luna<Trail>::check(L, 1, true);
  float secs = TrailUtil::GetTotalSeconds(trail);
  LuaHelpers::Push(L, secs);
  return 1;
}

const luaL_Reg TrailUtilTable[] = {
    LIST_METHOD(GetNumSongs), LIST_METHOD(GetTotalSeconds), {nullptr, nullptr}};
}  // namespace

LUA_REGISTER_NAMESPACE(TrailUtil)

/*
 * (c) 2004 Chris Danford
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
