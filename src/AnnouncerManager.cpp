#include "AnnouncerManager.h"

#include <cstring>
#include <string>
#include <vector>

#include "LuaManager.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"

AnnouncerManager* ANNOUNCER =
    nullptr;  // global and accessible from anywhere in our program

const std::string kEmptyAnnouncerName = "Empty";
const std::string kAnnouncersDir = "Announcers/";

AnnouncerManager::AnnouncerManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "ANNOUNCER");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

AnnouncerManager::~AnnouncerManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("ANNOUNCER");
}

void AnnouncerManager::GetAnnouncerNames(std::vector<std::string>& add_to) {
  GetDirListing(kAnnouncersDir + "*", add_to, true);

  StripCvsAndSvn(add_to);
  StripMacResourceForks(add_to);

  // strip out the empty announcer folder
  for (int i = add_to.size() - 1; i >= 0; i--) {
    if (!strcasecmp(add_to[i].c_str(), kEmptyAnnouncerName.c_str())) {
      add_to.erase(add_to.begin() + i, add_to.begin() + i + 1);
    }
  }
}

bool AnnouncerManager::DoesAnnouncerExist(std::string announcer_name) {
  if (announcer_name == "") {
    return true;
  }

  std::vector<std::string> announcer_names;
  GetAnnouncerNames(announcer_names);
  for (unsigned i = 0; i < announcer_names.size(); i++) {
    if (0 == strcasecmp(announcer_name.c_str(), announcer_names[i].c_str())) {
      return true;
    }
  }
  return false;
}

std::string AnnouncerManager::GetAnnouncerDirFromName(
    std::string announcer_name) {
  return kAnnouncersDir + announcer_name + "/";
}

void AnnouncerManager::SwitchAnnouncer(std::string new_announcer_name) {
  if (!DoesAnnouncerExist(new_announcer_name)) {
    cur_announcer_name_ = "";
  } else {
    cur_announcer_name_ = new_announcer_name;
  }
}

/* Aliases for announcer paths.  This is for compatibility, so we don't force
 * announcer changes along with everything else.  We could use it to support
 * DWI announcers transparently, too. */
static const char* aliases[][2] = {
    /* ScreenSelectDifficulty compatibility: */
    {"ScreenSelectDifficulty comment beginner",
     "select difficulty comment beginner"},
    {"ScreenSelectDifficulty comment easy", "select difficulty comment easy"},
    {"ScreenSelectDifficulty comment medium",
     "select difficulty comment medium"},
    {"ScreenSelectDifficulty comment hard", "select difficulty comment hard"},
    {"ScreenSelectDifficulty comment oni", "select difficulty comment oni"},
    {"ScreenSelectDifficulty comment nonstop",
     "select difficulty comment nonstop"},
    {"ScreenSelectDifficulty comment endless",
     "select difficulty comment endless"},
    {"ScreenSelectDifficulty intro", "select difficulty intro"},

    /* ScreenSelectStyle compatibility: */
    {"ScreenSelectStyle intro", "select style intro"},
    {"ScreenSelectStyle comment single", "select style comment single"},
    {"ScreenSelectStyle comment double", "select style comment double"},
    {"ScreenSelectStyle comment solo", "select style comment solo"},
    {"ScreenSelectStyle comment versus", "select style comment versus"},

    /* Combo compatibility: */
    {"gameplay combo 100", "gameplay 100 combo"},
    {"gameplay combo 200", "gameplay 200 combo"},
    {"gameplay combo 300", "gameplay 300 combo"},
    {"gameplay combo 400", "gameplay 400 combo"},
    {"gameplay combo 500", "gameplay 500 combo"},
    {"gameplay combo 600", "gameplay 600 combo"},
    {"gameplay combo 700", "gameplay 700 combo"},
    {"gameplay combo 800", "gameplay 800 combo"},
    {"gameplay combo 900", "gameplay 900 combo"},
    {"gameplay combo 1000", "gameplay 1000 combo"},

    {nullptr, nullptr}};

/* Find an announcer directory with sounds in it.  First search sFolderName,
 * then all aliases above.  Ignore directories that are empty, since we might
 * have "select difficulty intro" with sounds and an empty
 * "ScreenSelectDifficulty intro". */
std::string AnnouncerManager::GetPathTo(
    std::string announcer_name, std::string folder_name) {
  if (announcer_name == "") {
    return std::string(); /* announcer disabled */
  }

  const std::string announcer_path = GetAnnouncerDirFromName(announcer_name);

  if (!DirectoryIsEmpty(announcer_path + folder_name + "/")) {
    return announcer_path + folder_name + "/";
  }

  /* Search for the announcer folder in the list of aliases. */
  int i;
  for (i = 0; aliases[i][0] != nullptr; ++i) {
    if (!EqualsNoCase(folder_name, aliases[i][0])) {
      continue; /* no match */
    }

    if (!DirectoryIsEmpty(announcer_path + aliases[i][1] + "/")) {
      return announcer_path + aliases[i][1] + "/";
    }
  }

  /* No announcer directory matched.  In debug, create the directory by
   * its preferred name. */
#ifdef DEBUG
  LOG->Trace(
      "The announcer in '%s' is missing the folder '%s'.",
      announcer_path.c_str(), folder_name.c_str());
  //	MessageBeep( MB_OK );
  RageFile temp;
  temp.Open(
      announcer_path + folder_name + "/announcer files go here.txt",
      RageFile::WRITE);
#endif

  return std::string();
}

std::string AnnouncerManager::GetPathTo(std::string folder_name) {
  return GetPathTo(cur_announcer_name_, folder_name);
}

bool AnnouncerManager::HasSoundsFor(std::string folder_name) {
  return !DirectoryIsEmpty(GetPathTo(folder_name));
}

void AnnouncerManager::NextAnnouncer() {
  std::vector<std::string> as;
  GetAnnouncerNames(as);
  if (as.size() == 0) {
    return;
  }

  if (cur_announcer_name_ == "") {
    SwitchAnnouncer(as[0]);
  } else {
    unsigned i;
    for (i = 0; i < as.size(); i++) {
      if (EqualsNoCase(as[i], cur_announcer_name_)) {
        break;
      }
    }
    if (i == as.size() - 1) {
      SwitchAnnouncer("");
    } else {
      int new_index = (i + 1) % as.size();
      SwitchAnnouncer(as[new_index]);
    }
  }
}
// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the AnnouncerManager. */
class LunaAnnouncerManager : public Luna<AnnouncerManager> {
 public:
  static int DoesAnnouncerExist(T* p, lua_State* L) {
    lua_pushboolean(L, p->DoesAnnouncerExist(SArg(1)));
    return 1;
  }
  static int GetAnnouncerNames(T* p, lua_State* L) {
    std::vector<std::string> announcers;
    p->GetAnnouncerNames(announcers);
    LuaHelpers::CreateTableFromArray(announcers, L);
    return 1;
  }
  static int GetCurrentAnnouncer(T* p, lua_State* L) {
    std::string s = p->GetCurAnnouncerName();
    if (s.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, s.c_str());
    }
    return 1;
  }
  static int SetCurrentAnnouncer(T* p, lua_State* L) {
    std::string s = SArg(1);
    // only bother switching if the announcer exists. -aj
    if (p->DoesAnnouncerExist(s)) {
      p->SwitchAnnouncer(s);
    }
    COMMON_RETURN_SELF;
  }

  LunaAnnouncerManager() {
    ADD_METHOD(DoesAnnouncerExist);
    ADD_METHOD(GetAnnouncerNames);
    ADD_METHOD(GetCurrentAnnouncer);
    ADD_METHOD(SetCurrentAnnouncer);
  }
};

LUA_REGISTER_CLASS(AnnouncerManager)
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
