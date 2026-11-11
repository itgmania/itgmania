#include "NoteSkinManager.h"

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "CommonMetrics.h"
#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageException.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SpecialFiles.h"
#include "Sprite.h"
#include "StdString.h"
#include "Style.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"

/** @brief Have the NoteSkinManager available throughout the program. */
NoteSkinManager* NOTESKIN =
    nullptr;  // global and accessible from anywhere in our program

const std::string kGameCommonNoteskinName = "common";
const std::string kGameBaseNoteskinName = "default";

// this isn't a global because of nondeterministic global actor ordering
// might init this before SpecialFiles::NOTESKINS_DIR
#define GLOBAL_BASE_DIR \
  (SpecialFiles::kNoteskinsDir + kGameCommonNoteskinName + "/")

static std::map<std::string, std::string> path_cache;

struct VariantNoteSkinData {
  std::string name_;
  std::string parent_note_skin_;
};

struct NoteSkinData {
  std::string name_;
  IniFile metrics_;
  VariantNoteSkinData variant_data_;
  bool is_variant_;
  // When looking for an element, search these dirs from head to tail.
  std::vector<std::string> dir_search_order_;

  LuaReference loader_;
};

namespace {
static std::map<std::string, NoteSkinData> map_name_to_data;
static std::map<std::string, std::vector<std::string>>
    map_parent_name_to_variant_names;
};  // namespace

NoteSkinManager::NoteSkinManager() {
  cur_game_ = nullptr;
  player_number_ = PlayerNumber_Invalid;
  game_controller_ = GameController_Invalid;

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "NOTESKIN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

NoteSkinManager::~NoteSkinManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("NOTESKIN");

  map_name_to_data.clear();
}

void NoteSkinManager::RefreshNoteSkinData(const Game* game) {
  cur_game_ = game;

  // clear path cache
  path_cache.clear();

  std::string base_skin_folder =
      SpecialFiles::kNoteskinsDir + game->name_ + "/";
  std::vector<std::string> note_skin_names;
  GetDirListing(base_skin_folder + "*", note_skin_names, true);

  StripCvsAndSvn(note_skin_names);
  StripMacResourceForks(note_skin_names);

  map_name_to_data.clear();
  map_parent_name_to_variant_names.clear();
  for (unsigned j = 0; j < note_skin_names.size(); j++) {
    std::string name = note_skin_names[j];
    MakeLower(name);
    // Don't feel like changing the structure of this code to load the skin
    // into a temp variable and move it, so if the load fails, then just
    // delete it from the map. -Kyz
    if (!LoadNoteSkinData(name, map_name_to_data[name])) {
      std::map<std::string, NoteSkinData>::iterator entry =
          map_name_to_data.find(name);
      map_name_to_data.erase(entry);
    } else {
      // Determine if this noteskin has variants
      // If the Noteskin has a "variants" folder with at least one subfolder,
      // we consider it to have variants.
      std::string variant_dir =
          base_skin_folder + note_skin_names[j] + "/variants/";
      std::vector<std::string> variant_names;
      GetDirListing(variant_dir + "*", variant_names, true);
      StripCvsAndSvn(variant_names);
      StripMacResourceForks(variant_names);

      if (!variant_names.empty()) {
        for (unsigned k = 0; k < variant_names.size(); k++) {
          std::string variant_name = variant_names[k];
          MakeLower(variant_name);
          // Name in map should be parent name _<variant>
          std::string variant_key = name + "_" + variant_name;
          map_parent_name_to_variant_names[name].push_back(variant_key);
          map_name_to_data[variant_key].variant_data_.parent_note_skin_ = name;
          map_name_to_data[variant_key].variant_data_.name_ = variant_name;
          map_name_to_data[variant_key].is_variant_ = true;
          if (!LoadNoteSkinData(variant_key, map_name_to_data[variant_key])) {
            std::map<std::string, NoteSkinData>::iterator entry =
                map_name_to_data.find(variant_key);
            map_name_to_data.erase(entry);
            map_parent_name_to_variant_names[name].pop_back();
          }
        }
      }
    }
  }
}

bool NoteSkinManager::LoadNoteSkinData(
    const std::string& note_skin_name, NoteSkinData& data_out) {
  data_out.name_ = note_skin_name;
  data_out.metrics_.Clear();
  data_out.dir_search_order_.clear();

  // Read the current NoteSkin and all of its fallbacks
  return LoadNoteSkinDataRecursive(note_skin_name, data_out);
}

bool NoteSkinManager::LoadNoteSkinDataRecursive(
    const std::string& raw_note_skin_name, NoteSkinData& data_out) {
  std::string note_skin_name(raw_note_skin_name);

  int depth = 0;
  bool loaded_common = false;
  bool loaded_base = false;
  for (;;) {
    ++depth;
    if (depth >= 20) {
      LuaHelpers::ReportScriptError(
          "Circular NoteSkin fallback references detected.", "NOTESKIN_ERROR");
      return false;
    }
    bool is_variant = false;
    std::string dir = SpecialFiles::kNoteskinsDir + cur_game_->name_ + "/" +
                      note_skin_name + "/";
    if (!FILEMAN->IsADirectory(dir)) {
      dir = GLOBAL_BASE_DIR + note_skin_name + "/";
      if (!FILEMAN->IsADirectory(dir)) {
        // Check if it's a variant
        if (map_name_to_data.find(note_skin_name) != map_name_to_data.end()) {
          is_variant = map_name_to_data[note_skin_name].is_variant_;
        }
        if (is_variant) {
          dir =
              SpecialFiles::kNoteskinsDir + cur_game_->name_ + "/" +
              map_name_to_data[note_skin_name].variant_data_.parent_note_skin_ +
              "/variants/" +
              map_name_to_data[note_skin_name].variant_data_.name_ + "/";
          if (!FILEMAN->IsADirectory(dir)) {
            LuaHelpers::ReportScriptError(
                "NoteSkin \"" + data_out.name_ +
                    "\" references variant skin \"" + note_skin_name +
                    "\" that is not present",
                "NOTESKIN_ERROR");
            LuaHelpers::ReportScriptError(
                "Checked for \"" + dir + "\".", "NOTESKIN_ERROR");
            return false;
          }
        }
      }
    }

    LOG->Trace(
        "LoadNoteSkinDataRecursive: %s (%s)", note_skin_name.c_str(),
        dir.c_str());

    // read global fallback the current NoteSkin (if any)
    IniFile ini;

    if (!ini.ReadFile(dir + "metrics.ini")) {
      if (is_variant) {
        // Read the metrics.ini from the parent Noteskin
        if (!map_name_to_data[note_skin_name]
                 .variant_data_.parent_note_skin_.empty()) {
          if (!ini.ReadFile(
                  SpecialFiles::kNoteskinsDir + cur_game_->name_ + "/" +
                  map_name_to_data[note_skin_name]
                      .variant_data_.parent_note_skin_ +
                  "/metrics.ini")) {
            LuaHelpers::ReportScriptError(
                "Failed to read metrics.ini for NoteSkin \"" + data_out.name_ +
                    "\" and its parent skin \"" +
                    map_name_to_data[note_skin_name]
                        .variant_data_.parent_note_skin_ +
                    "\".",
                "NOTESKIN_ERROR");
            return false;
          }
        } else {
          LuaHelpers::ReportScriptError(
              "Failed to read metrics.ini for NoteSkin \"" + data_out.name_ +
                  "\".",
              "NOTESKIN_ERROR");
          return false;
        }
      }
    }

    if (!CompareNoCase(note_skin_name, kGameBaseNoteskinName)) {
      loaded_base = true;
    }
    if (!CompareNoCase(note_skin_name, kGameCommonNoteskinName)) {
      loaded_common = true;
    }

    std::string fallback;
    if (!ini.GetValue("Global", "FallbackNoteSkin", fallback)) {
      if (!loaded_base) {
        fallback = kGameBaseNoteskinName;
      } else if (!loaded_common) {
        fallback = kGameCommonNoteskinName;
      }
    }

    XmlFileUtil::MergeIniUnder(&ini, &data_out.metrics_);

    data_out.dir_search_order_.push_back(dir);
    if (is_variant) {
      data_out.dir_search_order_.push_back(
          SpecialFiles::kNoteskinsDir + cur_game_->name_ + "/" +
          map_name_to_data[note_skin_name].variant_data_.parent_note_skin_ +
          "/");
    }

    if (fallback.empty()) {
      break;
    }
    note_skin_name = fallback;
  }

  LuaReference ref_script;
  for (std::vector<std::string>::reverse_iterator dir =
           data_out.dir_search_order_.rbegin();
       dir != data_out.dir_search_order_.rend(); ++dir) {
    std::string file = *dir + "NoteSkin.lua";
    std::string script;
    if (!FILEMAN->IsAFile(file)) {
      continue;
    }

    if (!GetFileContents(file, script)) {
      continue;
    }

    LOG->Trace("Load script \"%s\"", file.c_str());

    Lua* L = LUA->Get();
    std::string error = "Error running " + file + ": ";
    ref_script.PushSelf(L);
    if (!LuaHelpers::RunScript(L, script, "@" + file, error, 1, 1, true)) {
      lua_pop(L, 1);
    } else {
      ref_script.SetFromStack(L);
    }
    LUA->Release(L);
  }
  data_out.loader_ = ref_script;
  return true;
}

void NoteSkinManager::GetNoteSkinNames(
    std::vector<std::string>& add_to, bool include_variants) {
  GetNoteSkinNames(GAMESTATE->cur_game_, add_to, include_variants);
}

void NoteSkinManager::GetNoteSkinNames(
    const Game* game, std::vector<std::string>& add_to, bool include_variants) {
  GetAllNoteSkinNamesForGame(game, add_to, include_variants);
  FilterNoteSkinsByStyle(
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber()), add_to);
}

void NoteSkinManager::GetVariantNamesForNoteSkin(
    const std::string& note_skin, std::vector<std::string>& add_to) {
  GetVariantNamesForNoteSkin(GAMESTATE->cur_game_, note_skin, add_to);
}

void NoteSkinManager::GetVariantNamesForNoteSkin(
    const Game* game, const std::string& note_skin,
    std::vector<std::string>& add_to) {
  std::map<std::string, std::vector<std::string>>::const_iterator it =
      map_parent_name_to_variant_names.find(note_skin);
  if (it != map_parent_name_to_variant_names.end()) {
    add_to = it->second;
  }
}

bool NoteSkinManager::NoteSkinNameInList(
    const std::string name, std::vector<std::string> name_list) {
  for (size_t i = 0; i < name_list.size(); ++i) {
    if (0 == strcasecmp(name.c_str(), name_list[i].c_str())) {
      return true;
    }
  }
  return false;
}

bool NoteSkinManager::HasVariants(const std::string& note_skin) {
  std::map<std::string, std::vector<std::string>>::const_iterator it =
      map_parent_name_to_variant_names.find(note_skin);
  return it != map_parent_name_to_variant_names.end() && !it->second.empty();
}

bool NoteSkinManager::IsNoteSkinVariant(const std::string& note_skin) {
  std::map<std::string, NoteSkinData>::const_iterator it =
      map_name_to_data.find(note_skin);
  return it != map_name_to_data.end() && it->second.is_variant_;
}

bool NoteSkinManager::DoesNoteSkinExist(const std::string& skin_name) {
  std::vector<std::string> skin_names;
  GetAllNoteSkinNamesForGame(GAMESTATE->cur_game_, skin_names);
  return NoteSkinNameInList(skin_name, skin_names);
}

bool NoteSkinManager::DoNoteSkinsExistForGame(const Game* game) {
  std::vector<std::string> skin_names;
  GetAllNoteSkinNamesForGame(game, skin_names);
  return !skin_names.empty();
}

std::string NoteSkinManager::GetDefaultNoteSkinName() {
  std::string name = THEME->GetMetric("Common", "DefaultNoteSkinName");
  std::vector<std::string> all_names;
  GetAllNoteSkinNamesForGame(GAMESTATE->cur_game_, all_names);
  if (all_names.empty()) {
    return "";
  }
  if (!NoteSkinNameInList(name, all_names)) {
    name = "default";
    if (!NoteSkinNameInList(name, all_names)) {
      name = all_names[1];
    }
  }
  return name;
}

void NoteSkinManager::ValidateNoteSkinName(std::string& name) {
  if (name.empty() || !DoesNoteSkinExist(name)) {
    LuaHelpers::ReportScriptError(
        "Someone set a noteskin that doesn't exist.  Good job.");
    name = GetDefaultNoteSkinName();
  }
}

void NoteSkinManager::GetAllNoteSkinNamesForGame(
    const Game* game, std::vector<std::string>& add_to, bool include_variants) {
  if (game == cur_game_) {
    // Faster:
    for (std::map<std::string, NoteSkinData>::const_iterator iter =
             map_name_to_data.begin();
         iter != map_name_to_data.end(); ++iter) {
      if (!include_variants && iter->second.is_variant_) {
        continue;
      }
      add_to.push_back(iter->second.name_);
    }
  } else {
    std::string base_skin_folder =
        SpecialFiles::kNoteskinsDir + game->name_ + "/";
    GetDirListing(base_skin_folder + "*", add_to, true);
    StripCvsAndSvn(add_to);
    StripMacResourceForks(add_to);
  }
}

void NoteSkinManager::FilterNoteSkinsByStyle(
    const Style* style, std::vector<std::string>& add_to) {
  if (style == nullptr) {
    return;
  }
  // If the styletype is TwoPlayersSharedSides then filter noteskins where the
  // metric "TwoPlayersSharedSides" is not "true"
  if (style->style_type_ == StyleType_TwoPlayersSharedSides) {
    for (std::vector<std::string>::iterator iter = add_to.begin();
         iter != add_to.end();) {
      std::string note_skin_name = *iter;
      MakeLower(note_skin_name);
      std::map<std::string, NoteSkinData>::const_iterator it =
          map_name_to_data.find(note_skin_name);
      ASSERT_M(
          it != map_name_to_data.end(),
          note_skin_name);  // this NoteSkin doesn't exist!
      const NoteSkinData& data = it->second;

      std::string s_is_routine;
      if (!data.metrics_.GetValue(
              "Global", "IsRoutineNoteSkin", s_is_routine)) {
        // If the metric doesn't exist, then it's not a TwoPlayersSharedSides
        // noteskin.
        iter = add_to.erase(iter);
        continue;
      }

      if (s_is_routine.empty() || s_is_routine != "true") {
        // If the metric exists but isn't "true", then it's not a
        // TwoPlayersSharedSides noteskin.
        iter = add_to.erase(iter);
        continue;
      }
      ++iter;
    }
  } else if (!CommonMetrics::AUTO_SET_STYLE) {
    // First make sure we're in game
    for (std::vector<std::string>::iterator iter = add_to.begin();
         iter != add_to.end();) {
      std::string note_skin_name = *iter;
      MakeLower(note_skin_name);
      std::map<std::string, NoteSkinData>::const_iterator it =
          map_name_to_data.find(note_skin_name);
      ASSERT_M(
          it != map_name_to_data.end(),
          note_skin_name);  // this NoteSkin doesn't exist!
      const NoteSkinData& data = it->second;

      std::string b_is_routine;
      if (data.metrics_.GetValue("Global", "IsRoutineNoteSkin", b_is_routine)) {
        if (b_is_routine == "true") {
          // If the metric exists but isn't "true", then it's not a
          // TwoPlayersSharedSides noteskin.
          iter = add_to.erase(iter);
          continue;
        }
      }
      ++iter;
    }
  }
}

std::string NoteSkinManager::GetMetric(
    const std::string& button_name, const std::string& value) {
  if (current_note_skin_.empty()) {
    LuaHelpers::ReportScriptError(
        "NOTESKIN:GetMetric: No noteskin currently set.", "NOTESKIN_ERROR");
    return "";
  }
  std::string note_skin_name = current_note_skin_;
  MakeLower(note_skin_name);
  std::map<std::string, NoteSkinData>::const_iterator it =
      map_name_to_data.find(note_skin_name);
  ASSERT_M(
      it != map_name_to_data.end(),
      note_skin_name);  // this NoteSkin doesn't exist!
  const NoteSkinData& data = it->second;

  std::string s_return;
  if (data.metrics_.GetValue(button_name, value, s_return)) {
    return s_return;
  }
  if (!data.metrics_.GetValue("NoteDisplay", value, s_return)) {
    LuaHelpers::ReportScriptError(
        "Could not read metric \"" + button_name + "::" + value +
            "\" or \"NoteDisplay::" + value + "\" in \"" + note_skin_name +
            "\".",
        "NOTESKIN_ERROR");
    return "";
  }
  return s_return;
}

int NoteSkinManager::GetMetricI(
    const std::string& button_name, const std::string& value_name) {
  return StringToInt(GetMetric(button_name, value_name));
}

float NoteSkinManager::GetMetricF(
    const std::string& button_name, const std::string& value_name) {
  return StringToFloat(GetMetric(button_name, value_name));
}

bool NoteSkinManager::GetMetricB(
    const std::string& button_name, const std::string& value_name) {
  // Could also call GetMetricI here...hmm.
  return StringToInt(GetMetric(button_name, value_name)) != 0;
}

apActorCommands NoteSkinManager::GetMetricA(
    const std::string& button_name, const std::string& value_name) {
  return ActorUtil::ParseActorCommands(GetMetric(button_name, value_name));
}

std::string NoteSkinManager::GetPath(
    const std::string& button_name, const std::string& element) {
  const std::string cache_string =
      current_note_skin_ + "/" + button_name + "/" + element;
  std::map<std::string, std::string>::iterator it =
      path_cache.find(cache_string);
  if (it != path_cache.end()) {
    return it->second;
  }

  if (current_note_skin_.empty()) {
    LuaHelpers::ReportScriptError(
        "NOTESKIN:GetPath: No noteskin currently set.", "NOTESKIN_ERROR");
    return "";
  }
  std::string note_skin_name = current_note_skin_;
  MakeLower(note_skin_name);
  std::map<std::string, NoteSkinData>::const_iterator iter =
      map_name_to_data.find(note_skin_name);

  ASSERT(iter != map_name_to_data.end());
  const NoteSkinData& data = iter->second;

  std::string path;  // fill this in below
  for (const std::string& directory : data.dir_search_order_) {
    if (button_name.empty()) {
      path = GetPathFromDirAndFile(directory, element);
    } else {
      path = GetPathFromDirAndFile(directory, button_name + " " + element);
    }
    if (!path.empty()) {
      break;  // done searching
    }
  }

  if (path.empty()) {
    for (const std::string& directory : data.dir_search_order_) {
      if (!button_name.empty()) {
        path = GetPathFromDirAndFile(directory, "Fallback " + element);
      }
      if (!path.empty()) {
        break;  // done searching
      }
    }
  }

  if (path.empty()) {
    std::string paths;

    // TODO: Find a more elegant way of doing this.
    for (const std::string& dir : data.dir_search_order_) {
      if (!paths.empty()) {
        paths += ", ";
      }

      paths += dir;
    }

    std::string message = ssprintf(
        "The NoteSkin element \"%s %s\" could not be found in any of the "
        "following directories:\n%s",
        button_name.c_str(), element.c_str(), paths.c_str());

    switch (LuaHelpers::ReportScriptError(message, "NOTESKIN_ERROR", true)) {
      case Dialog::retry:
        for (const std::string& dir : data.dir_search_order_) {
          FILEMAN->FlushDirCache(dir);
        }
        path_cache.clear();
        return GetPath(button_name, element);
      case Dialog::abort:
        RageException::Throw("%s", message.c_str());
      case Dialog::ignore:
        return "";
      default:
        break;
    }
  }

  int level = 0;
  while (GetExtension(path) == "redir") {
    level++;
    if (level >= 100) {
      LuaHelpers::ReportScriptError(
          "Infinite recursion while looking up " + button_name + " - " +
              element,
          "NOTESKIN_ERROR");
      return "";
    }
    std::string new_file_name;
    GetFileContents(path, new_file_name, true);
    std::string real_path;

    for (const std::string& directory : data.dir_search_order_) {
      real_path = GetPathFromDirAndFile(directory, new_file_name);
      if (!real_path.empty()) {
        break;  // done searching
      }
    }

    if (real_path == "") {
      std::string message = ssprintf(
          "NoteSkinManager:  The redirect \"%s\" points to the file \"%s\", "
          "which does not exist. "
          "Verify that this redirect is correct.",
          path.c_str(), new_file_name.c_str());

      switch (LuaHelpers::ReportScriptError(message, "NOTESKIN_ERROR", true)) {
        case Dialog::retry:
          for (const std::string& dir : data.dir_search_order_) {
            FILEMAN->FlushDirCache(dir);
          }
          path_cache.clear();
          return GetPath(button_name, element);
        case Dialog::abort:
          RageException::Throw("%s", message.c_str());
        case Dialog::ignore:
          return "";
        default:
          break;
      }
    }

    path = real_path;
  }

  path_cache[cache_string] = path;
  return path;
}

bool NoteSkinManager::PushActorTemplate(
    Lua* L, const std::string& button, const std::string& element,
    bool sprite_only, const std::string& color) {
  std::map<std::string, NoteSkinData>::const_iterator iter =
      map_name_to_data.find(current_note_skin_);
  if (iter == map_name_to_data.end()) {
    LuaHelpers::ReportScriptError("No current noteskin set!", "NOTESKIN_ERROR");
    return false;
  }
  const NoteSkinData& data = iter->second;

  LuaThreadVariable var_player("Player", LuaReference::Create(player_number_));
  LuaThreadVariable var_controller(
      "Controller", LuaReference::Create(game_controller_));
  LuaThreadVariable var_button("Button", button);
  LuaThreadVariable var_element("Element", element);
  LuaThreadVariable var_sprite_only(
      "SpriteOnly", LuaReference::Create(sprite_only));
  LuaThreadVariable var_color("Color", color);
  if (data.loader_.IsNil()) {
    LuaHelpers::ReportScriptError("No loader for noteskin!", "NOTESKIN_ERROR");
    return false;
  }
  data.loader_.PushSelf(L);
  lua_remove(L, -2);
  lua_getfield(L, -1, "Load");

  return ActorUtil::LoadTableFromStackShowErrors(L);
}

Actor* NoteSkinManager::LoadActor(
    const std::string& button, const std::string& element, Actor* parent,
    bool sprite_only, const std::string& color) {
  Lua* L = LUA->Get();

  if (!PushActorTemplate(L, button, element, sprite_only, color)) {
    LUA->Release(L);
    // ActorUtil will warn about the error
    return Sprite::NewBlankSprite();
  }

  std::unique_ptr<XNode> node(XmlFileUtil::XNodeFromTable(L));
  if (node.get() == nullptr) {
    LUA->Release(L);
    // XNode will warn about the error
    return Sprite::NewBlankSprite();
  }

  LUA->Release(L);

  Actor* ret = ActorUtil::LoadFromNode(node.get(), parent);

  if (sprite_only) {
    // Make sure pActor is a Sprite (or something derived from Sprite).
    Sprite* sprite = dynamic_cast<Sprite*>(ret);
    if (sprite == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "%s: %s %s must be a Sprite", current_note_skin_.c_str(),
          button.c_str(), element.c_str());
      delete ret;
      return Sprite::NewBlankSprite();
    }
  }

  return ret;
}

std::string NoteSkinManager::GetPathFromDirAndFile(
    const std::string& dir, const std::string& file_name) {
  std::vector<std::string> matches;  // fill this with the possible files

  GetDirListing(dir + file_name + "*", matches, false, true);

  if (matches.empty()) {
    return std::string();
  }

  if (matches.size() > 1) {
    std::string error = "Multiple files match '" + dir + file_name +
                        "'.  Please remove all but one of these files: ";
    error += join(", ", matches);
    LuaHelpers::ReportScriptError(error, "NOTESKIN_ERROR");
  }

  return matches[0];
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the NoteSkinManager. */
class LunaNoteSkinManager : public Luna<NoteSkinManager> {
 public:
  DEFINE_METHOD(GetPath, GetPath(SArg(1), SArg(2)));
  DEFINE_METHOD(GetMetric, GetMetric(SArg(1), SArg(2)));
  DEFINE_METHOD(GetMetricI, GetMetricI(SArg(1), SArg(2)));
  DEFINE_METHOD(GetMetricF, GetMetricF(SArg(1), SArg(2)));
  DEFINE_METHOD(GetMetricB, GetMetricB(SArg(1), SArg(2)));
  static int GetMetricA(T* p, lua_State* L) {
    p->GetMetricA(SArg(1), SArg(2))->PushSelf(L);
    return 1;
  }
  static int LoadActor(T* p, lua_State* L) {
    std::string button = SArg(1);
    std::string element = SArg(2);
    if (!p->PushActorTemplate(L, button, element, false, "4th")) {
      lua_pushnil(L);
    }

    return 1;
  }
#define FOR_NOTESKIN(x, n)                                              \
  static int x##ForNoteSkin(T* p, lua_State* L) {                       \
    const std::string sOldNoteSkin = p->GetCurrentNoteSkin();           \
    std::string nsname = SArg(n + 1);                                   \
    if (!p->DoesNoteSkinExist(nsname)) {                                \
      luaL_error(L, "Noteskin \"%s\" does not exist.", nsname.c_str()); \
    }                                                                   \
    p->SetCurrentNoteSkin(nsname);                                      \
    x(p, L);                                                            \
    p->SetCurrentNoteSkin(sOldNoteSkin);                                \
    return 1;                                                           \
  }
  FOR_NOTESKIN(GetPath, 2);
  FOR_NOTESKIN(GetMetric, 2);
  FOR_NOTESKIN(GetMetricI, 2);
  FOR_NOTESKIN(GetMetricF, 2);
  FOR_NOTESKIN(GetMetricB, 2);
  FOR_NOTESKIN(GetMetricA, 2);
  FOR_NOTESKIN(LoadActor, 2);
#undef FOR_NOTESKIN
  static int GetNoteSkinNames(T* p, lua_State* L) {
    std::vector<std::string> noteskins;
    bool include_variants = true;
    if (lua_gettop(L) > 0) {
      include_variants = lua_toboolean(L, 1) != 0;
    }
    p->GetNoteSkinNames(noteskins, include_variants);
    LuaHelpers::CreateTableFromArray(noteskins, L);
    return 1;
  }
  static int GetVariantNamesForNoteSkin(T* p, lua_State* L) {
    std::string note_skin = SArg(1);
    std::vector<std::string> variants;
    p->GetVariantNamesForNoteSkin(note_skin, variants);
    LuaHelpers::CreateTableFromArray(variants, L);
    return 1;
  }

  /*
  static int GetNoteSkinNamesForGame( T* p, lua_State *L )
  {
          Game *pGame = Luna<Game>::check( L, 1 );
          std::vector<std::string> vGameNoteskins;
          p->GetNoteSkinNames( pGame, vGameNoteskins );
          LuaHelpers::CreateTableFromArray(vGameNoteskins, L);
          return 1;
  }
  */
  static int DoesNoteSkinExist(T* p, lua_State* L) {
    lua_pushboolean(L, p->DoesNoteSkinExist(SArg(1)));
    return 1;
  }

  static int HasVariants(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasVariants(SArg(1)));
    return 1;
  }

  static int IsNoteSkinVariant(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsNoteSkinVariant(SArg(1)));
    return 1;
  }

  LunaNoteSkinManager() {
    ADD_METHOD(GetPath);
    ADD_METHOD(GetMetric);
    ADD_METHOD(GetMetricI);
    ADD_METHOD(GetMetricF);
    ADD_METHOD(GetMetricB);
    ADD_METHOD(GetMetricA);
    ADD_METHOD(LoadActor);
    ADD_METHOD(GetPathForNoteSkin);
    ADD_METHOD(GetMetricForNoteSkin);
    ADD_METHOD(GetMetricIForNoteSkin);
    ADD_METHOD(GetMetricFForNoteSkin);
    ADD_METHOD(GetMetricBForNoteSkin);
    ADD_METHOD(GetMetricAForNoteSkin);
    ADD_METHOD(LoadActorForNoteSkin);
    ADD_METHOD(GetNoteSkinNames);
    ADD_METHOD(DoesNoteSkinExist);  // for the current game
    ADD_METHOD(HasVariants);
    ADD_METHOD(IsNoteSkinVariant);
    ADD_METHOD(GetVariantNamesForNoteSkin);
  }
};

LUA_REGISTER_CLASS(NoteSkinManager)
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
