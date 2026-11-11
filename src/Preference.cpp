#include "Preference.h"

#include <string>

#include "LuaManager.h"
#include "MessageManager.h"
#include "RageLog.h"
#include "StdString.h"
#include "SubscriptionManager.h"
#include "XmlFile.h"
#include "global.h"

static SubscriptionManager<IPreference> subscribers;

IPreference::IPreference(const std::string& name, PreferenceType type)
    : name_(name),
      do_not_write_(type == PreferenceType::Deprecated),
      immutable_(type == PreferenceType::Immutable) {
  subscribers.Subscribe(this);
}

IPreference::~IPreference() { subscribers.Unsubscribe(this); }

IPreference* IPreference::GetPreferenceByName(const std::string& name) {
  for (IPreference* p : *subscribers.subscribers_) {
    if (!CompareNoCase(p->GetName(), name)) {
      return p;
    }
  }

  return nullptr;
}

void IPreference::LoadAllDefaults() {
  for (IPreference* p : *subscribers.subscribers_) {
    p->LoadDefault();
  }
}

void IPreference::ReadAllPrefsFromNode(const XNode* node, bool is_static) {
  ASSERT(node != nullptr);
  for (IPreference* p : *subscribers.subscribers_) {
    p->ReadFrom(node, is_static);
  }
}

void IPreference::SavePrefsToNode(XNode* node) {
  for (IPreference* p : *subscribers.subscribers_) {
    p->WriteTo(node);
  }
}

void IPreference::ReadAllDefaultsFromNode(const XNode* node) {
  if (node == nullptr) {
    return;
  }
  for (IPreference* p : *subscribers.subscribers_) {
    p->ReadDefaultFrom(node);
  }
}

void IPreference::PushValue(lua_State* L) const {
  if (LOG) {
    LOG->Trace(
        "The preference value \"%s\" is of a type not supported by Lua",
        name_.c_str());
  }

  lua_pushnil(L);
}

void IPreference::SetFromStack(lua_State* L) {
  if (LOG) {
    LOG->Trace(
        "The preference value \"%s\" is of a type not supported by Lua",
        name_.c_str());
  }

  lua_pop(L, 1);
}

void IPreference::ReadFrom(const XNode* node, bool is_static) {
  std::string val;
  if (node->GetAttrValue(name_, val)) {
    FromString(val);
    if (is_static) {
      do_not_write_ = true;
    }
  }
}

void IPreference::WriteTo(XNode* node) const {
  if (do_not_write_) {
    return;
  }

  node->AppendAttr(name_, ToString());
}

/* Load our value from the node, and make it the new default. */
void IPreference::ReadDefaultFrom(const XNode* node) {
  std::string val;
  if (!node->GetAttrValue(name_, val)) {
    return;
  }
  SetDefaultFromString(val);
}

void BroadcastPreferenceChanged(const std::string& preference_name) {
  if (MESSAGEMAN) {
    MESSAGEMAN->Broadcast(preference_name + "Changed");
  }
}

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez
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
