#include "global.h"

#include "DynamicActorScroller.h"

#include <cmath>
#include <cstddef>

#include "ActorUtil.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "XmlFile.h"

DynamicActorScroller* DynamicActorScroller::Copy() const {
  return new DynamicActorScroller(*this);
}

void DynamicActorScroller::LoadFromNode(const XNode* node) {
  ActorScroller::LoadFromNode(node);

  // All of our children are identical, since they must be interchangeable.
  // The <children> node loads only one; we copy the rest.
  //
  // Make one extra copy if masking is enabled.
  if (m_SubActors.size() != 1) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: DynamicActorScroller: loaded %i nodes; require exactly one",
        ActorUtil::GetWhere(node).c_str(), (int)m_SubActors.size());
    // Remove all but one.
    for (std::size_t i = 1; i < m_SubActors.size(); ++i) {
      delete m_SubActors[i];
    }
    m_SubActors.resize(1);
  }

  int num_copies = (int)m_fNumItemsToDraw;
  if (m_quadMask.GetVisible()) {
    num_copies += 1;
  }
  for (int i = 1; i < num_copies; ++i) {
    Actor* pCopy = m_SubActors[0]->Copy();
    this->AddChild(pCopy);
  }

  {
    Lua* L = LUA->Get();
    node->PushAttrValue(L, "LoadFunction");
    load_function_.SetFromStack(L);
    LUA->Release(L);
  }

  // Call the expression with line = nil to find out the number of lines.
  {
    Lua* L = LUA->Get();
    load_function_.PushSelf(L);
    ASSERT(!lua_isnil(L, -1));
    lua_pushnil(L);
    lua_pushnil(L);

    RString Error = "Error running LoadFunction: ";
    LuaHelpers::RunScriptOnStack(L, Error, 2, 1, true);  // 2 args, 1 result

    m_iNumItems = (int)luaL_checknumber(L, -1);
    lua_pop(L, 1);
    LUA->Release(L);
  }

  // Reconfigure all items, so the loaded actors actually correspond with
  // m_iFirstSubActorIndex.
  ShiftSubActors(INT_MAX);
}

// Shift m_SubActors forward by dist, and then fill in the new entries.
//
// Important: under normal scrolling, with or without m_bLoop, at most one
// object is created per update, and this normally only happens when an
// object comes on screen.  Extra actor updates are avoided for efficiency.
void DynamicActorScroller::ShiftSubActors(int dist) {
  ActorScroller::ShiftSubActors(dist);

  if (dist == 0) {
    return;
  }

  if (m_bLoop) {
    // Optimization: in a loop of 10, when we loop around from 9 to 0,
    // dist will be -9.  Moving -9 is equivalent to moving +1, and
    // reconfigures much fewer actors.
    int wrapped = dist;
    wrap(wrapped, m_iNumItems);
    if (std::abs(wrapped) < std::abs(dist)) {
      dist = wrapped;
    }
  }

  int first_to_reconfigure = 0;
  int last_to_reconfigure = (int)m_SubActors.size();
  if (dist > 0 && dist < (int)m_SubActors.size()) {
    first_to_reconfigure = m_SubActors.size() - dist;
  } else if (dist < 0 && -dist < (int)m_SubActors.size()) {
    last_to_reconfigure = -dist;
  }

  for (int i = first_to_reconfigure; i < last_to_reconfigure; ++i) {
    int index = i;  // index into m_SubActors
    int item = i + m_iFirstSubActorIndex;
    if (m_bLoop) {
      wrap(index, m_SubActors.size());
      wrap(item, m_iNumItems);
    } else if (
        index < 0 || index >= m_iNumItems || item < 0 ||
        item >= m_iNumItems) {
      continue;
    }

    {
      Lua* L = LUA->Get();
      lua_pushnumber(L, i);
      m_SubActors[index]->m_pLuaInstance->Set(L, "ItemIndex");
      LUA->Release(L);
    }

    ConfigureActor(m_SubActors[index], item);
  }
}

void DynamicActorScroller::ConfigureActor(Actor* actor, int item) {
  Lua* L = LUA->Get();
  load_function_.PushSelf(L);
  ASSERT(!lua_isnil(L, -1));
  actor->PushSelf(L);
  LuaHelpers::Push(L, item);

  RString error = "Error running LoadFunction: ";
  LuaHelpers::RunScriptOnStack(L, error, 2, 0, true);  // 2 args, 0 results

  LUA->Release(L);
}

REGISTER_ACTOR_CLASS_WITH_NAME(
    DynamicActorScrollerAutoDeleteChildren, DynamicActorScroller);

/*
 * (c) 2005 Glenn Maynard
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
