#ifndef ActorUtil_H
#define ActorUtil_H

#include <string>
#include <vector>

#include "Actor.h"
#include "LuaManager.h"
#include "RageUtil.h"
#include "global.h"

class XNode;

typedef Actor* (*CreateActorFn)();

template <typename T>
Actor* CreateActor() {
  return new T;
}

/**
 * @brief Register the requested Actor with the specified external name.
 *
 * This should be called manually only if needed. */
#define REGISTER_ACTOR_CLASS_WITH_NAME(className, externalClassName)   \
  struct Register##className {                                         \
    Register##className() {                                            \
      ActorUtil::Register(#externalClassName, CreateActor<className>); \
    }                                                                  \
  };                                                                   \
  className* className::Copy() const { return new className(*this); }  \
  static Register##className register##className

/**
 * @brief Register the requested Actor so that it's more accessible.
 *
 * Each Actor class should have a REGISTER_ACTOR_CLASS in its CPP file. */
#define REGISTER_ACTOR_CLASS(className) \
  REGISTER_ACTOR_CLASS_WITH_NAME(className, className)

enum FileType {
  FT_Bitmap,
  FT_Sprite,
  FT_Sound,
  FT_Movie,
  FT_Directory,
  FT_Xml,
  FT_Model,
  FT_Lua,
  FT_Ini,
  NUM_FileType,
  FileType_Invalid
};
const std::string& FileTypeToString(FileType ft);

/** @brief Utility functions for creating and manipulating Actors. */
namespace ActorUtil {
void InitFileTypeLists();
const std::vector<std::string>& GetTypeExtensionList(FileType ft);
// Every screen should register its class at program initialization.
void Register(const std::string& class_name, CreateActorFn pfn);

apActorCommands ParseActorCommands(
    const std::string& commands, const std::string& name = "");
void SetXY(Actor& actor, const std::string& metrics_group);
inline void PlayCommand(Actor& actor, const std::string& command_name) {
  actor.PlayCommand(command_name);
}
inline void OnCommand(Actor& actor) {
  ASSERT_M(
      actor.HasCommand("On"),
      ssprintf("%s is missing an OnCommand.", actor.GetLineage().c_str()));
  actor.PlayCommand("On");
}
inline void OffCommand(Actor& actor) {
  // HACK:  It's very often that we command things to TweenOffScreen
  // that we aren't drawing.  We know that an Actor is not being
  // used if its name is blank.  So, do nothing on Actors with a blank name.
  // (Do "playcommand" anyway; BGAs often have no name.)
  if (actor.GetName().empty()) {
    return;
  }
  ASSERT_M(
      actor.HasCommand("Off"),
      ssprintf(
          "Actor %s is missing an OffCommand.", actor.GetLineage().c_str()));
  actor.PlayCommand("Off");
}

void LoadCommand(
    Actor& actor, const std::string& metrics_group,
    const std::string& command_name);
void LoadCommandFromName(
    Actor& actor, const std::string& metrics_group,
    const std::string& command_name, const std::string& name);
void LoadAllCommands(Actor& actor, const std::string& metrics_group);
void LoadAllCommandsFromName(
    Actor& actor, const std::string& metrics_group, const std::string& name);

inline void LoadAllCommandsAndSetXY(
    Actor& actor, const std::string& metrics_group) {
  LoadAllCommands(actor, metrics_group);
  SetXY(actor, metrics_group);
}
inline void LoadAllCommandsAndOnCommand(
    Actor& actor, const std::string& metrics_group) {
  LoadAllCommands(actor, metrics_group);
  OnCommand(actor);
}
inline void SetXYAndOnCommand(Actor& actor, const std::string& metrics_group) {
  SetXY(actor, metrics_group);
  OnCommand(actor);
}
inline void LoadAllCommandsAndSetXYAndOnCommand(
    Actor& actor, const std::string& metrics_group) {
  LoadAllCommands(actor, metrics_group);
  SetXY(actor, metrics_group);
  OnCommand(actor);
}

/* convenience */
inline void SetXY(Actor* actor, const std::string& metrics_group) {
  SetXY(*actor, metrics_group);
}
inline void PlayCommand(Actor* actor, const std::string& command_name) {
  if (actor) {
    actor->PlayCommand(command_name);
  }
}
inline void OnCommand(Actor* actor) {
  if (actor) {
    ActorUtil::OnCommand(*actor);
  }
}
inline void OffCommand(Actor* actor) {
  if (actor) {
    ActorUtil::OffCommand(*actor);
  }
}

inline void LoadAllCommands(Actor* actor, const std::string& metrics_group) {
  if (actor) {
    LoadAllCommands(*actor, metrics_group);
  }
}

inline void LoadAllCommandsAndSetXY(
    Actor* actor, const std::string& metrics_group) {
  if (actor) {
    LoadAllCommandsAndSetXY(*actor, metrics_group);
  }
}
inline void LoadAllCommandsAndOnCommand(
    Actor* actor, const std::string& metrics_group) {
  if (actor) {
    LoadAllCommandsAndOnCommand(*actor, metrics_group);
  }
}
inline void SetXYAndOnCommand(Actor* actor, const std::string& metrics_group) {
  if (actor) {
    SetXYAndOnCommand(*actor, metrics_group);
  }
}
inline void LoadAllCommandsAndSetXYAndOnCommand(
    Actor* actor, const std::string& metrics_group) {
  if (actor) {
    LoadAllCommandsAndSetXYAndOnCommand(*actor, metrics_group);
  }
}

// Return a Sprite, BitmapText, or Model depending on the file type
Actor* LoadFromNode(const XNode* node, Actor* parent_actor = nullptr);
Actor* MakeActor(const std::string& path, Actor* parent_actor = nullptr);
std::string GetSourcePath(const XNode* node);
std::string GetWhere(const XNode* node);
bool GetAttrPath(
    const XNode* node, const std::string& name, std::string& out,
    bool optional = false);
bool LoadTableFromStackShowErrors(Lua* L);

bool ResolvePath(
    std::string& path, const std::string& name, bool optional = false);

void SortByZPosition(std::vector<Actor*>& actors);

FileType GetFileType(const std::string& path);
};  // namespace ActorUtil

#define SET_XY(actor) ActorUtil::SetXY(actor, name_)
#define ON_COMMAND(actor) ActorUtil::OnCommand(actor)
#define OFF_COMMAND(actor) ActorUtil::OffCommand(actor)
#define SET_XY_AND_ON_COMMAND(actor) ActorUtil::SetXYAndOnCommand(actor, name_)
#define COMMAND(actor, command_name) ActorUtil::PlayCommand(actor, command_name)
#define LOAD_ALL_COMMANDS(actor) ActorUtil::LoadAllCommands(actor, name_)
#define LOAD_ALL_COMMANDS_AND_SET_XY(actor) \
  ActorUtil::LoadAllCommandsAndSetXY(actor, name_)
#define LOAD_ALL_COMMANDS_AND_ON_COMMAND(actor) \
  ActorUtil::LoadAllCommandsAndOnCommand(actor, name_)
#define LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(actor) \
  ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(actor, name_)

#endif

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
