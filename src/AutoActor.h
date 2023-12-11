#ifndef AutoActor_H
#define AutoActor_H

#include "Actor.h"
#include "XmlFile.h"

// A smart pointer for Actor.
// This creates the appropriate Actor derivative on load and
// automatically deletes the Actor on deconstruction.
class AutoActor {
 public:
  AutoActor() : actor_(nullptr) {}
  ~AutoActor() { Unload(); }
  AutoActor(const AutoActor& cpy);
  AutoActor &operator=(const AutoActor& cpy);
  operator const Actor* () const { return actor_; }
  operator Actor* () { return actor_; }
  const Actor* operator->() const { return actor_; }
  Actor* operator->() { return actor_; }
  void Unload();
  // Determine if this actor is presently loaded.
  bool IsLoaded() const { return actor_ != nullptr; }
	// Transfers pointer ownership.
  void Load(Actor* actor);
  void Load(const RString& path);
	// Load a background and set up LuaThreadVariables for recursive loading.
  void LoadB(const RString& metrics_group, const RString& element);  
  void LoadActorFromNode(const XNode* node, Actor* parent);
  void LoadAndSetName(const RString& screen_name, const RString& actor_name);

 protected:
  // The Actor for which there is a smart pointer to.
  Actor* actor_;
};

#endif  // AutoActor_H

/**
 * @file
 * @author Chris Danford (c) 2003-2004
 * @section LICENSE
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
