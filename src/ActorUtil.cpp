#include "ActorUtil.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Actor.h"
#include "Course.h"
#include "EnumHelper.h"
#include "GameState.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "PrefsManager.h"
#include "RageException.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"

// Actor registration
static std::map<std::string, CreateActorFn>* registrees = nullptr;

static bool IsRegistered(const std::string& class_name) {
  return registrees->find(class_name) != registrees->end();
}

void ActorUtil::Register(const std::string& class_name, CreateActorFn pfn) {
  if (registrees == nullptr) {
    registrees = new std::map<std::string, CreateActorFn>;
  }

  std::map<std::string, CreateActorFn>::iterator iter =
      registrees->find(class_name);
  ASSERT_M(
      iter == registrees->end(),
      ssprintf("Actor class '%s' already registered.", class_name.c_str()));

  (*registrees)[class_name] = pfn;
}

/* Resolves actor paths a la LoadActor("..."), with autowildcarding and .redir
 * files.  Returns a path *within* the Rage filesystem, unlike the FILEMAN
 * function of the same name. */
bool ActorUtil::ResolvePath(
    std::string& path, const std::string& name, bool optional) {
  CollapsePath(path);

  // If we know this is an exact match, don't bother with the GetDirListing,
  // so "foo" doesn't partial match "foobar" if "foo" exists.
  RageFileManager::FileType ft = FILEMAN->GetFileType(path);
  if (ft != RageFileManager::TYPE_FILE && ft != RageFileManager::TYPE_DIR) {
    std::vector<std::string> paths;
    GetDirListing(path + "*", paths, false, true);  // return path too

    if (paths.empty()) {
      if (optional) {
        return false;
      }
      std::string error = ssprintf(
          "%s: references a file \"%s\" which doesn't exist", name.c_str(),
          path.c_str());
      switch (
          LuaHelpers::ReportScriptError(error, "BROKEN_FILE_REFERENCE", true)) {
        case Dialog::abort:
          RageException::Throw("%s", error.c_str());
          break;
        case Dialog::retry:
          FILEMAN->FlushDirCache();
          return ResolvePath(path, name);
        case Dialog::ignore:
          return false;
        default:
          FAIL_M("Invalid response to Abort/Retry/Ignore dialog");
      }
    }

    THEME->FilterFileLanguages(paths);

    if (paths.size() > 1) {
      std::string error = ssprintf(
          "%s: references a file \"%s\" which has multiple matches",
          name.c_str(), path.c_str());
      error += "\n" + join("\n", paths);
      switch (
          LuaHelpers::ReportScriptError(error, "BROKEN_FILE_REFERENCE", true)) {
        case Dialog::abort:
          RageException::Throw("%s", error.c_str());
          break;
        case Dialog::retry:
          FILEMAN->FlushDirCache();
          return ResolvePath(path, name);
        case Dialog::ignore:
          paths.erase(paths.begin() + 1, paths.end());
          break;
        default:
          FAIL_M("Invalid response to Abort/Retry/Ignore dialog");
      }
    }

    path = paths[0];
  }

  if (ft == RageFileManager::TYPE_DIR) {
    std::string lua_path = path + "/default.lua";
    if (DoesFileExist(lua_path)) {
      path = lua_path;
      return true;
    }
  }

  path = DerefRedir(path);
  return true;
}

namespace {
std::string GetLegacyActorClass(XNode* actor) {
  DEBUG_ASSERT(PREFSMAN->quirks_mode_);
  ASSERT(actor);

  // The non-legacy LoadFromNode has already checked the Class and
  // Type attributes.

  if (actor->GetAttr("Text") != nullptr) {
    return "BitmapText";
  }

  std::string file;
  if (actor->GetAttrValue("File", file) && file != "") {
    // Backward compatibility hacks for "special" filenames
    if (EqualsNoCase(file, "songbackground")) {
      XNodeStringValue* val = new XNodeStringValue;
      Song* song = GAMESTATE->cur_song_;
      if (song && song->HasBackground()) {
        val->SetValue(song->GetBackgroundPath());
      } else {
        val->SetValue(THEME->GetPathG("Common", "fallback background"));
      }
      actor->AppendAttrFrom("Texture", val, false);
      return "Sprite";
    } else if (EqualsNoCase(file, "songbanner")) {
      XNodeStringValue* val = new XNodeStringValue;
      Song* song = GAMESTATE->cur_song_;
      if (song && song->HasBanner()) {
        val->SetValue(song->GetBannerPath());
      } else {
        val->SetValue(THEME->GetPathG("Common", "fallback banner"));
      }
      actor->AppendAttrFrom("Texture", val, false);
      return "Sprite";
    } else if (EqualsNoCase(file, "coursebanner")) {
      XNodeStringValue* val = new XNodeStringValue;
      Course* course = GAMESTATE->cur_course_;
      if (course && course->HasBanner()) {
        val->SetValue(course->GetBannerPath());
      } else {
        val->SetValue(THEME->GetPathG("Common", "fallback banner"));
      }
      actor->AppendAttrFrom("Texture", val, false);
      return "Sprite";
    }
  }

  // Fallback: use XML tag name for actor class
  return actor->name_;
}
}  // namespace

Actor* ActorUtil::LoadFromNode(const XNode* p_node, Actor* parent_actor) {
  ASSERT(p_node != nullptr);

  XNode node = *p_node;

  // Remove this in favor of using conditionals in Lua. -Chris
  // There are a number of themes out there that depend on this (including
  // sm-ssc default). Probably for the best to leave this in. -aj
  {
    bool cond;
    if (node.GetAttrValue("Condition", cond) && !cond) {
      return nullptr;
    }
  }

  std::string s_class;
  bool has_class = node.GetAttrValue("Class", s_class);
  if (!has_class) {
    has_class = node.GetAttrValue("Type", s_class);
  }

  bool legacy = (node.GetAttr("_LegacyXml") != nullptr);
  if (!has_class && legacy) {
    s_class = GetLegacyActorClass(&node);
  }

  std::map<std::string, CreateActorFn>::iterator iter =
      registrees->find(s_class);
  if (iter == registrees->end()) {
    std::string file;
    if (legacy && node.GetAttrValue("File", file) && file != "") {
      std::string path;
      // Handle absolute paths correctly
      if (Left(file, 1) == "/") {
        path = file;
      } else {
        path = Dirname(GetSourcePath(&node)) + file;
      }
      if (ResolvePath(path, GetWhere(&node))) {
        Actor* new_actor = MakeActor(path, parent_actor);
        if (new_actor == nullptr) {
          return nullptr;
        }
        if (parent_actor) {
          new_actor->SetParent(parent_actor);
        }
        new_actor->LoadFromNode(&node);
        return new_actor;
      }
    }

    // sClass is invalid
    std::string error = ssprintf(
        "%s: invalid Class \"%s\"", ActorUtil::GetWhere(&node).c_str(),
        s_class.c_str());
    LuaHelpers::ReportScriptError(error);
    return new Actor;  // Return a dummy object so that we don't crash in
                       // AutoActor later.
  }

  const CreateActorFn& pfn = iter->second;
  Actor* ret = pfn();

  if (parent_actor) {
    ret->SetParent(parent_actor);
  }

  ret->LoadFromNode(&node);
  return ret;
}

namespace {
XNode* LoadXNodeFromLuaShowErrors(const std::string& file) {
  std::string script;
  if (!GetFileContents(file, script)) {
    return nullptr;
  }

  Lua* L = LUA->Get();

  std::string error;
  if (!LuaHelpers::LoadScript(L, script, "@" + file, error)) {
    LUA->Release(L);
    error = ssprintf("Lua runtime error: %s", error.c_str());
    LuaHelpers::ReportScriptError(error);
    return nullptr;
  }

  XNode* ret = nullptr;
  if (ActorUtil::LoadTableFromStackShowErrors(L)) {
    ret = XmlFileUtil::XNodeFromTable(L);
  }

  LUA->Release(L);
  return ret;
}
}  // namespace

/* Run the function at the top of the stack, which returns an actor description
 * table.  If the table was returned, return true and leave it on the stack.
 * If not, display an error, return false, and leave nothing on the stack. */
bool ActorUtil::LoadTableFromStackShowErrors(Lua* L) {
  LuaReference func;
  lua_pushvalue(L, -1);
  func.SetFromStack(L);

  std::string error = "Lua runtime error: ";
  if (!LuaHelpers::RunScriptOnStack(L, error, 0, 1, true)) {
    lua_pop(L, 1);
    return false;
  }

  if (lua_type(L, -1) != LUA_TTABLE) {
    lua_pop(L, 1);

    func.PushSelf(L);
    lua_Debug debug;
    lua_getinfo(L, ">nS", &debug);

    error = ssprintf("%s: must return a table", debug.short_src);

    LuaHelpers::ReportScriptError(error, "LUA_ERROR");
    return false;
  }
  return true;
}

// NOTE: This function can return nullptr if the actor should not be displayed.
// Callers should be aware of this and handle it appropriately.
Actor* ActorUtil::MakeActor(const std::string& raw_path, Actor* parent_actor) {
  std::string path(raw_path);

  FileType ft = GetFileType(path);
  switch (ft) {
    case FT_Lua: {
      std::unique_ptr<XNode> node(LoadXNodeFromLuaShowErrors(path));
      if (node.get() == nullptr) {
        // XNode will warn about the error
        return new Actor;
      }

      Actor* ret = ActorUtil::LoadFromNode(node.get(), parent_actor);
      return ret;
    }
    case FT_Xml: {
      // Legacy actors; only supported in quirks mode
      if (!PREFSMAN->quirks_mode_) {
        return new Actor;
      }

      XNode xml;
      if (!XmlFileUtil::LoadFromFileShowErrors(xml, path)) {
        return new Actor;
      }
      XmlFileUtil::CompileXNodeTree(&xml, path);
      XmlFileUtil::AnnotateXNodeTree(&xml, path);
      return LoadFromNode(&xml, parent_actor);
    }
    case FT_Directory: {
      if (Right(path, 1) != "/") {
        path += '/';
      }

      std::string xml_path = path + "default.xml";
      if (DoesFileExist(xml_path)) {
        return MakeActor(xml_path, parent_actor);
      }

      XNode xml;
      xml.AppendAttr("Class", "BGAnimation");
      xml.AppendAttr("AniDir", path);

      return ActorUtil::LoadFromNode(&xml, parent_actor);
    }
    case FT_Bitmap:
    case FT_Movie: {
      XNode xml;
      xml.AppendAttr("Class", "Sprite");
      xml.AppendAttr("Texture", path);

      return ActorUtil::LoadFromNode(&xml, parent_actor);
    }
    case FT_Sprite: {
      // Legacy actor; only supported in quirks mode
      if (!PREFSMAN->quirks_mode_) {
        return new Actor;
      }

      IniFile ini;
      ini.ReadFile(path);
      XmlFileUtil::AnnotateXNodeTree(&ini, path);

      return ActorUtil::LoadFromNode(ini.GetChild("Sprite"), parent_actor);
    }
    case FT_Model: {
      XNode xml;
      xml.AppendAttr("Class", "Model");
      xml.AppendAttr("Meshes", path);
      xml.AppendAttr("Materials", path);
      xml.AppendAttr("Bones", path);

      return ActorUtil::LoadFromNode(&xml, parent_actor);
    }
    default: {
      LOG->Warn(
          "File \"%s\" has unknown type, \"%s\".", path.c_str(),
          FileTypeToString(ft).c_str());

      XNode xml;
      xml.AppendAttr("Class", "Actor");
      return ActorUtil::LoadFromNode(&xml, parent_actor);
    }
  }
}

std::string ActorUtil::GetSourcePath(const XNode* node) {
  std::string ret;
  node->GetAttrValue("_Source", ret);
  if (ret.substr(0, 1) == "@") {
    ret.erase(0, 1);
  }

  return ret;
}

std::string ActorUtil::GetWhere(const XNode* node) {
  std::string path = GetSourcePath(node);

  int line;
  if (node->GetAttrValue("_Line", line)) {
    path += ssprintf(":%i", line);
  }
  return path;
}

bool ActorUtil::GetAttrPath(
    const XNode* node, const std::string& name, std::string& out,
    bool optional) {
  if (!node->GetAttrValue(name, out)) {
    return false;
  }

  bool is_relative_path = Left(out, 1) != "/";
  if (is_relative_path) {
    std::string dir;
    if (!node->GetAttrValue("_Dir", dir)) {
      if (!optional) {
        LOG->Warn("Relative path \"%s\", but path is unknown", out.c_str());
      }
      return false;
    }
    out = dir + out;
  }

  return ActorUtil::ResolvePath(out, ActorUtil::GetWhere(node), optional);
}

apActorCommands ActorUtil::ParseActorCommands(
    const std::string& commands, const std::string& name) {
  Lua* L = LUA->Get();
  LuaHelpers::ParseCommandList(L, commands, name, false);
  LuaReference* ret = new LuaReference;
  ret->SetFromStack(L);
  LUA->Release(L);

  return apActorCommands(ret);
}

void ActorUtil::SetXY(Actor& actor, const std::string& metrics_group) {
  ASSERT(!actor.GetName().empty());

  /*
   * Hack: This is run after InitCommand, and InitCommand might set X/Y.  If
   * these are both 0, leave the actor where it is.  If InitCommand doesn't,
   * then 0,0 is the default, anyway.
   */
  float x = THEME->GetMetricF(metrics_group, actor.GetName() + "X");
  float y = THEME->GetMetricF(metrics_group, actor.GetName() + "Y");
  if (x != 0 || y != 0) {
    actor.SetXY(x, y);
  }
}

void ActorUtil::LoadCommand(
    Actor& actor, const std::string& metrics_group,
    const std::string& command_name) {
  ActorUtil::LoadCommandFromName(
      actor, metrics_group, command_name, actor.GetName());
}

void ActorUtil::LoadCommandFromName(
    Actor& actor, const std::string& metrics_group,
    const std::string& command_name, const std::string& name) {
  actor.AddCommand(
      command_name,
      THEME->GetMetricA(metrics_group, name + command_name + "Command"));
}

void ActorUtil::LoadAllCommands(
    Actor& actor, const std::string& metrics_group) {
  LoadAllCommandsFromName(actor, metrics_group, actor.GetName());
}

void ActorUtil::LoadAllCommandsFromName(
    Actor& actor, const std::string& metrics_group, const std::string& name) {
  std::set<std::string> value_names;
  THEME->GetMetricsThatBeginWith(metrics_group, name, value_names);

  for (const std::string& sv : value_names) {
    static const std::string kEnding = "Command";
    if (EndsWith(sv, kEnding)) {
      std::string command_name(
          sv.begin() + name.size(), sv.end() - kEnding.size());
      LoadCommandFromName(actor, metrics_group, command_name, name);
    }
  }
}

static bool CompareActorsByZPosition(const Actor* p1, const Actor* p2) {
  return p1->GetZ() < p2->GetZ();
}

void ActorUtil::SortByZPosition(std::vector<Actor*>& actors) {
  // Preserve ordering of Actors with equal Z positions.
  stable_sort(actors.begin(), actors.end(), CompareActorsByZPosition);
}

static const char* FileTypeNames[] = {
    "Bitmap", "Sprite", "Sound", "Movie", "Directory",
    "Xml",    "Model",  "Lua",   "Ini",
};
XToString(FileType);
LuaXType(FileType);

// convenience so the for-loop lines can be shorter.
typedef std::map<std::string, FileType> etft_cont_t;
typedef std::map<FileType, std::vector<std::string> > fttel_cont_t;
etft_cont_t extension_to_file_type;
fttel_cont_t file_type_to_extension_list;

void ActorUtil::InitFileTypeLists() {
  // This function creates things to serve two purposes:
  // 1.  A map from extensions to filetypes, so extensions can be converted.
  // 2.  A reverse map for things that need a list of extensions to look for.
  // The first section creates the map from extensions to filetypes, then the
  // second section uses that map to build the reverse map.
  extension_to_file_type["lua"] = FT_Lua;

  extension_to_file_type["xml"] = FT_Xml;

  extension_to_file_type["ini"] = FT_Ini;

  // Update RageSurfaceUtils when adding new image formats.
  extension_to_file_type["bmp"] = FT_Bitmap;
  extension_to_file_type["gif"] = FT_Bitmap;
  extension_to_file_type["jpeg"] = FT_Bitmap;
  extension_to_file_type["jpg"] = FT_Bitmap;
  extension_to_file_type["png"] = FT_Bitmap;

  // Update RageSoundReader_FileReader when adding new sound formats.
  extension_to_file_type["mp3"] = FT_Sound;
  extension_to_file_type["oga"] = FT_Sound;
  extension_to_file_type["ogg"] = FT_Sound;
  extension_to_file_type["wav"] = FT_Sound;

  // ffmpeg takes care of loading videos, not sure whether this list should
  // have everything ffmpeg supports.
  extension_to_file_type["avi"] = FT_Movie;
  extension_to_file_type["f4v"] = FT_Movie;
  extension_to_file_type["flv"] = FT_Movie;
  extension_to_file_type["mkv"] = FT_Movie;
  extension_to_file_type["mp4"] = FT_Movie;
  extension_to_file_type["mpeg"] = FT_Movie;
  extension_to_file_type["mpg"] = FT_Movie;
  extension_to_file_type["mov"] = FT_Movie;
  extension_to_file_type["ogv"] = FT_Movie;
  extension_to_file_type["webm"] = FT_Movie;
  extension_to_file_type["wmv"] = FT_Movie;

  extension_to_file_type["sprite"] = FT_Sprite;

  extension_to_file_type["txt"] = FT_Model;

  // When adding new extensions, do not add them below this line.  This line
  // marks the point where the function switches to building the reverse map.
  for (etft_cont_t::iterator curr_ext = extension_to_file_type.begin();
       curr_ext != extension_to_file_type.end(); ++curr_ext) {
    file_type_to_extension_list[curr_ext->second].push_back(curr_ext->first);
  }
}

const std::vector<std::string>& ActorUtil::GetTypeExtensionList(FileType ft) {
  return file_type_to_extension_list[ft];
}

FileType ActorUtil::GetFileType(const std::string& path) {
  std::string ext = GetExtension(path);
  MakeLower(ext);

  etft_cont_t::iterator conversion_entry = extension_to_file_type.find(ext);
  if (conversion_entry != extension_to_file_type.end()) {
    return conversion_entry->second;
  } else if (path.size() > 0 && path[path.size() - 1] == '/') {
    return FT_Directory;
  }
  /* Do this last, to avoid the IsADirectory in most cases. */
  else if (IsADirectory(path)) {
    return FT_Directory;
  }
  return FileType_Invalid;
}

// lua start
#include "LuaBinding.h"

namespace {
int GetFileType(lua_State* L) {
  Enum::Push(L, ActorUtil::GetFileType(SArg(1)));
  return 1;
}
int ResolvePath(lua_State* L) {
  std::string path(SArg(1));
  int level = IArg(2);
  bool optional = lua_toboolean(L, 3);
  luaL_where(L, level);
  std::string where = lua_tostring(L, -1);
  if (where.size() > 2 && where.substr(where.size() - 2, 2) == ": ") {
    where = where.substr(0, where.size() - 2);  // remove trailing ": "
  }

  LUA->YieldLua();
  bool ret = ActorUtil::ResolvePath(path, where, optional);
  LUA->UnyieldLua();

  if (!ret) {
    return 0;
  }
  LuaHelpers::Push(L, path);
  return 1;
}
int IsRegisteredClass(lua_State* L) {
  lua_pushboolean(L, IsRegistered(SArg(1)));
  return 1;
}
static void name_error(Actor* p, lua_State* L) {
  if (p->GetName() == "") {
    luaL_error(L, "LoadAllCommands requires the actor to have a name.");
  }
}
static int LoadAllCommands(lua_State* L) {
  Actor* p = Luna<Actor>::check(L, 1);
  name_error(p, L);
  ActorUtil::LoadAllCommands(p, SArg(2));
  return 0;
}
static int LoadAllCommandsFromName(lua_State* L) {
  Actor* p = Luna<Actor>::check(L, 1);
  name_error(p, L);
  ActorUtil::LoadAllCommandsFromName(*p, SArg(2), SArg(3));
  return 0;
}
static int LoadAllCommandsAndSetXY(lua_State* L) {
  Actor* p = Luna<Actor>::check(L, 1);
  name_error(p, L);
  ActorUtil::LoadAllCommandsAndSetXY(p, SArg(2));
  return 0;
}

const luaL_Reg ActorUtilTable[] = {
    LIST_METHOD(GetFileType),
    LIST_METHOD(ResolvePath),
    LIST_METHOD(IsRegisteredClass),
    LIST_METHOD(LoadAllCommands),
    LIST_METHOD(LoadAllCommandsFromName),
    LIST_METHOD(LoadAllCommandsAndSetXY),
    {nullptr, nullptr}};
}  // namespace

LUA_REGISTER_NAMESPACE(ActorUtil)

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
