#include "MessageManager.h"

#include <map>
#include <set>
#include <string>

#include "EnumHelper.h"
#include "LuaManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "global.h"

MessageManager* MESSAGEMAN =
    nullptr;  // global and accessible from anywhere in our program

static const char* MessageIDNames[] = {
    "CurrentGameChanged",
    "CurrentStyleChanged",
    "PlayModeChanged",
    "CoinsChanged",
    "CurrentSongChanged",
    "CurrentStepsP1Changed",
    "CurrentStepsP2Changed",
    "CurrentCourseChanged",
    "CurrentTrailP1Changed",
    "CurrentTrailP2Changed",
    "GameplayLeadInChanged",
    "EditStepsTypeChanged",
    "EditCourseDifficultyChanged",
    "EditSourceStepsChanged",
    "EditSourceStepsTypeChanged",
    "PreferredStepsTypeChanged",
    "PreferredDifficultyP1Changed",
    "PreferredDifficultyP2Changed",
    "PreferredCourseDifficultyP1Changed",
    "PreferredCourseDifficultyP2Changed",
    "EditCourseEntryIndexChanged",
    "EditLocalProfileIDChanged",
    "GoalCompleteP1",
    "GoalCompleteP2",
    "NoteCrossed",
    "NoteWillCrossIn400Ms",
    "NoteWillCrossIn800Ms",
    "NoteWillCrossIn1200Ms",
    "CardRemovedP1",
    "CardRemovedP2",
    "BeatCrossed",
    "MenuUpP1",
    "MenuUpP2",
    "MenuDownP1",
    "MenuDownP2",
    "MenuLeftP1",
    "MenuLeftP2",
    "MenuRightP1",
    "MenuRightP2",
    "MenuStartP1",
    "MenuStartP2",
    "MenuSelectionChanged",
    "PlayerJoined",
    "PlayerUnjoined",
    "PlayerProfileSet",
    "AutosyncChanged",
    "PreferredSongGroupChanged",
    "PreferredCourseGroupChanged",
    "SortOrderChanged",
    "LessonTry1",
    "LessonTry2",
    "LessonTry3",
    "LessonCleared",
    "LessonFailed",
    "StorageDevicesChanged",
    "AutoJoyMappingApplied",
    "ScreenChanged",
    "SongModified",
    "ScoreMultiplierChangedP1",
    "ScoreMultiplierChangedP2",
    "StarPowerChangedP1",
    "StarPowerChangedP2",
    "CurrentComboChangedP1",
    "CurrentComboChangedP2",
    "StarMeterChangedP1",
    "StarMeterChangedP2",
    "LifeMeterChangedP1",
    "LifeMeterChangedP2",
    "UpdateScreenHeader",
    "LeftClick",
    "RightClick",
    "MiddleClick",
    "MouseWheelUp",
    "MouseWheelDown",
};
XToString(MessageID);

static RageMutex mutex("MessageManager");

typedef std::set<IMessageSubscriber*> SubscribersSet;
static std::map<std::string, SubscribersSet> message_to_subscribers;

Message::Message(const std::string& s) {
  name_ = s;
  params_ = new LuaTable;
  broadcast_ = false;
}

Message::Message(const MessageID id) {
  name_ = MessageIDToString(id);
  params_ = new LuaTable;
  broadcast_ = false;
}

Message::Message(const std::string& s, const LuaReference& params) {
  name_ = s;
  broadcast_ = false;
  Lua* L = LUA->Get();
  params_ = new LuaTable;  // XXX: creates an extra table
  params.PushSelf(L);
  params_->SetFromStack(L);
  LUA->Release(L);
  //	m_pParams = new LuaTable( params );
}

Message::~Message() { delete params_; }

const LuaReference& Message::GetParamTable() const { return *params_; }

void Message::GetParamFromStack(lua_State* L, const std::string& name) const {
  params_->Get(L, name);
}

void Message::SetParamFromStack(lua_State* L, const std::string& name) {
  params_->Set(L, name);
}

MessageManager::MessageManager() {
  logging_ = false;
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "MESSAGEMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

MessageManager::~MessageManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("MESSAGEMAN");
}

void MessageManager::Subscribe(
    IMessageSubscriber* subscriber, const std::string& message) {
  LockMut(mutex);

  SubscribersSet& subs = message_to_subscribers[message];
#ifdef DEBUG
  SubscribersSet::iterator iter = subs.find(subscriber);
  ASSERT_M(
      iter == subs.end(),
      ssprintf("already subscribed to '%s'", message.c_str()));
#endif
  subs.insert(subscriber);
}

void MessageManager::Subscribe(IMessageSubscriber* subscriber, MessageID m) {
  Subscribe(subscriber, MessageIDToString(m));
}

void MessageManager::Unsubscribe(
    IMessageSubscriber* subscriber, const std::string& message) {
  LockMut(mutex);

  SubscribersSet& subs = message_to_subscribers[message];
  SubscribersSet::iterator iter = subs.find(subscriber);
  ASSERT(iter != subs.end());
  subs.erase(iter);
}

void MessageManager::Broadcast(Message& msg) const {
  if (logging_) {
    LOG->Trace("MESSAGEMAN:Broadcast: %s", msg.GetName().c_str());
  }
  msg.SetBroadcast(true);

  LockMut(mutex);

  std::map<std::string, SubscribersSet>::const_iterator iter =
      message_to_subscribers.find(msg.GetName());
  if (iter == message_to_subscribers.end()) {
    return;
  }

  for (IMessageSubscriber* subscriber : iter->second) {
    subscriber->HandleMessage(msg);
  }
}

void MessageManager::Broadcast(const std::string& message) const {
  ASSERT(!message.empty());
  Message msg(message);
  Broadcast(msg);
}

void MessageManager::Broadcast(MessageID m) const {
  Broadcast(MessageIDToString(m));
}

bool MessageManager::IsSubscribedToMessage(
    IMessageSubscriber* subscriber, const std::string& message) const {
  SubscribersSet& subs = message_to_subscribers[message];
  return subs.find(subscriber) != subs.end();
}

MessageSubscriber::MessageSubscriber(const MessageSubscriber& cpy)
    : IMessageSubscriber(cpy) {
  for (const std::string& msg : cpy.subscribed_to_) {
    this->SubscribeToMessage(msg);
  }
}

void MessageSubscriber::SubscribeToMessage(const std::string& message_name) {
  MESSAGEMAN->Subscribe(this, message_name);
  subscribed_to_.push_back(message_name);
}

void MessageSubscriber::SubscribeToMessage(MessageID message) {
  MESSAGEMAN->Subscribe(this, message);
  subscribed_to_.push_back(MessageIDToString(message));
}

void MessageSubscriber::UnsubscribeAll() {
  for (const std::string& s : subscribed_to_) {
    MESSAGEMAN->Unsubscribe(this, s);
  }
  subscribed_to_.clear();
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the MessageManager. */
class LunaMessageManager : public Luna<MessageManager> {
 public:
  static int Broadcast(T* p, lua_State* L) {
    if (!lua_istable(L, 2) && !lua_isnoneornil(L, 2)) {
      luaL_typerror(L, 2, "table or nil");
    }

    LuaReference param_table;
    lua_pushvalue(L, 2);
    param_table.SetFromStack(L);

    Message msg(SArg(1), param_table);
    p->Broadcast(msg);
    COMMON_RETURN_SELF;
  }
  static int SetLogging(T* p, lua_State* L) {
    p->SetLogging(lua_toboolean(L, -1));
    COMMON_RETURN_SELF;
  }

  LunaMessageManager() {
    ADD_METHOD(Broadcast);
    ADD_METHOD(SetLogging);
  }
};

LUA_REGISTER_CLASS(MessageManager)
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
