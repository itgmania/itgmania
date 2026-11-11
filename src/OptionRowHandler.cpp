#include "OptionRowHandler.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "Character.h"
#include "CharacterManager.h"
#include "Command.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "FontCharAliases.h"
#include "GameCommand.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteSkinManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "ScreenMiniMenu.h"  // for MenuRowDef
#include "ScreenOptionsMasterPrefs.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "global.h"

#define ENTRY(s) THEME->GetMetric("ScreenOptionsMaster", s)
#define ENTRY_MODE(s, i) \
  THEME->GetMetric(      \
      "ScreenOptionsMaster", ssprintf("%s,%i", (s).c_str(), (i + 1)))
#define ENTRY_DEFAULT(s) \
  THEME->GetMetric("ScreenOptionsMaster", (s) + "Default")
#define NOTE_SKIN_SORT_ORDER \
  THEME->GetMetric("ScreenOptionsMaster", "NoteSkinSortOrder")
#define STEPS_ROW_LAYOUT_TYPE \
  THEME->GetMetric("ScreenOptionsMaster", "StepsRowLayoutType")
#define STEPS_USE_CHART_NAME \
  THEME->GetMetricB("ScreenOptionsMaster", "StepsUseChartName")

static const char* SelectTypeNames[] = {
    "SelectOne",
    "SelectMultiple",
    "SelectNone",
};
XToString(SelectType);
StringToX(SelectType);
LuaXType(SelectType);

static const char* LayoutTypeNames[] = {
    "ShowAllInRow",
    "ShowOneInRow",
};
XToString(LayoutType);
StringToX(LayoutType);
LuaXType(LayoutType);

std::string OptionRowHandler::OptionTitle() const {
  bool theme = false;

  // HACK: Always theme the NEXT_ROW and EXIT items, even if metrics says not to
  // theme.
  if (def_.allow_theme_title_) {
    theme = true;
  }

  std::string s = def_.name_;
  if (s.empty()) {
    return s;
  }

  return theme ? THEME->GetString("OptionTitles", s) : s;
}

std::string OptionRowHandler::GetThemedItemText(int choice) const {
  std::string s = def_.choices_[choice];
  if (s == "") {
    return "";
  }
  bool theme = false;

  if (def_.allow_theme_items_) {
    theme = true;
  }

  // Items beginning with a pipe mean "don't theme".
  // This allows us to disable theming on a per-choice basis for choice names
  // that are just a number and don't need to be localized.
  if (s[0] == '|') {
    s.erase(s.begin());
    theme = false;
  }

  if (theme) {
    s = CommonMetrics::LocalizeOptionItem(s, false);
  }
  return s;
}

void OptionRowHandler::GetIconTextAndGameCommand(
    int first_selection, std::string& icon_text_out, GameCommand& out) const {
  icon_text_out = "";
  out.Init();
}

void OptionRowHandlerUtil::SelectExactlyOne(
    int selection, std::vector<bool>& selected_out) {
  ASSERT_M(
      selection >= 0 && selection < (int)selected_out.size(),
      ssprintf("%d/%u", selection, unsigned(selected_out.size())));
  for (int i = 0; i < int(selected_out.size()); i++) {
    selected_out[i] = i == selection;
  }
}

int OptionRowHandlerUtil::GetOneSelection(const std::vector<bool>& selected) {
  int ret = -1;
  for (unsigned i = 0; i < selected.size(); i++) {
    if (selected[i]) {
      ASSERT(ret == -1);  // only one should be selected
      ret = i;
    }
  }
  ASSERT(ret != -1);  // shouldn't call this if not expecting one to be selected
  return ret;
}

static LocalizedString OFF("OptionRowHandler", "Off");

#define ROW_INVALID_IF(condition, message, retval)                        \
  if (condition) {                                                        \
    LuaHelpers::ReportScriptError("Parse error in option row: " message); \
    return retval;                                                        \
  }

#define CHECK_WRONG_NUM_ARGS(num)                                         \
  ROW_INVALID_IF(                                                         \
      command.args_.size() != num, "Wrong number of args to option row.", \
      false);
#define CHECK_BLANK_ARG \
  ROW_INVALID_IF(param.size() == 0, "Blank arg to Steps row.", false);

// begin OptionRow handlers
class OptionRowHandlerList : public OptionRowHandler {
 public:
  std::vector<GameCommand> list_entries_;
  GameCommand default_;
  bool use_mod_name_for_icon_;
  std::vector<std::string> broadcast_on_export_;

  OptionRowHandlerList() { Init(); }
  virtual void Init() {
    OptionRowHandler::Init();
    list_entries_.clear();
    default_.Init();
    use_mod_name_for_icon_ = false;
    broadcast_on_export_.clear();
  }
  virtual bool LoadInternal(const Commands& cmds) {
    const Command& command = cmds.v_[0];
    std::string param = command.GetArg(1).s_;

    use_mod_name_for_icon_ = true;
    def_.name_ = param;
    default_.Load(-1, ParseCommands(ENTRY_DEFAULT(param)));

    {
      // Parse the basic configuration metric.
      Commands l_cmds = ParseCommands(ENTRY(param));
      ROW_INVALID_IF(l_cmds.v_.size() < 1, "Row command is empty.", false);

      def_.one_choice_for_all_players_ = false;
      ROW_INVALID_IF(
          l_cmds.v_[0].args_.size() != 1,
          "Row command has invalid args to number of entries.", false);
      const int num_cols = StringToInt(l_cmds.v_[0].args_[0]);
      ROW_INVALID_IF(num_cols < 1, "Not enough entries in list.", false);
      for (unsigned i = 1; i < l_cmds.v_.size(); i++) {
        const Command& cmd = l_cmds.v_[i];
        std::string name = cmd.GetName();

        if (name == "together") {
          def_.one_choice_for_all_players_ = true;
        } else if (name == "selectmultiple") {
          def_.select_type_ = SELECT_MULTIPLE;
        } else if (name == "selectone") {
          def_.select_type_ = SELECT_ONE;
        } else if (name == "selectnone") {
          def_.select_type_ = SELECT_NONE;
        } else if (name == "showoneinrow") {
          def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
        } else if (name == "default") {
          def_.default_ =
              StringToInt(cmd.GetArg(1).s_) - 1;  // match ENTRY_MODE
        } else if (name == "reloadrowmessages") {
          for (unsigned a = 1; a < cmd.args_.size(); a++) {
            reload_row_messages_.push_back(cmd.args_[a]);
          }
        } else if (name == "enabledforplayers") {
          def_.enabled_for_players_.clear();
          for (unsigned a = 1; a < cmd.args_.size(); a++) {
            std::string arg = cmd.args_[a];
            PlayerNumber pn = (PlayerNumber)(StringToInt(arg) - 1);
            ASSERT(pn >= 0 && pn < kNumPlayers);
            def_.enabled_for_players_.insert(pn);
          }
        } else if (name == "exportonchange") {
          def_.export_on_change_ = true;
        } else if (name == "hideondisable") {
          def_.hide_on_disable_ = true;
        } else if (name == "broadcastonexport") {
          for (unsigned j = 1; j < cmd.args_.size(); j++) {
            broadcast_on_export_.push_back(cmd.args_[j]);
          }
        } else {
          LuaHelpers::ReportScriptErrorFmt(
              "Unknown row flag \"%s\" on row %s.", name.c_str(),
              def_.name_.c_str());
        }
      }
      for (int col = 0; col < num_cols; ++col) {
        GameCommand mc;
        mc.ApplyCommitsScreens(false);
        mc.Load(0, ParseCommands(ENTRY_MODE(param, col)));
        /* If the row has just one entry, use the name of the row as the name of
         * the entry. If it has more than one, each one must be specified
         * explicitly. */
        if (mc.name_ == "" && num_cols == 1) {
          mc.name_ = param;
        }
        if (mc.name_ == "") {
          LuaHelpers::ReportScriptErrorFmt(
              "List \"%s\", choice %i has no name.", param.c_str(), col + 1);
          mc.name_ = "";
        }

        std::string why;
        if (!mc.IsPlayable(&why)) {
          LuaHelpers::ReportScriptErrorFmt(
              "\"%s\" choice %d is not playable: %s", param.c_str(), col,
              why.c_str());
          continue;
        }

        list_entries_.push_back(mc);
        std::string choice = mc.name_;
        def_.choices_.push_back(choice);
      }
    }

    if (def_.select_type_ != SELECT_MULTIPLE && def_.default_ == -1) {
      for (unsigned e = 0; e < list_entries_.size(); ++e) {
        const GameCommand& mc = list_entries_[e];
        if (mc.IsZero()) {
          def_.default_ = e;
        }
      }
    }
    return true;
  }
  void ImportOption(
      OptionRow* row, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& sel_out = selected_out[p];

      bool use_fallback_option = true;

      for (unsigned e = 0; e < list_entries_.size(); ++e) {
        const GameCommand& mc = list_entries_[e];

        sel_out[e] = false;

        if (mc.IsZero()) {
          /* The entry has no effect. This is usually a default "none
           * of the above" entry. It will always return true for
           * DescribesCurrentMode(). It's only the selected choice if
           * nothing else matches. */
          continue;
        }

        if (def_.one_choice_for_all_players_) {
          if (mc.DescribesCurrentModeForAllPlayers()) {
            use_fallback_option = false;
            if (def_.select_type_ != SELECT_MULTIPLE) {
              OptionRowHandlerUtil::SelectExactlyOne(e, sel_out);
            } else {
              sel_out[e] = true;
            }
          }
        } else {
          if (mc.DescribesCurrentMode(p)) {
            use_fallback_option = false;
            if (def_.select_type_ != SELECT_MULTIPLE) {
              OptionRowHandlerUtil::SelectExactlyOne(e, sel_out);
            } else {
              sel_out[e] = true;
            }
          }
        }
      }

      if (def_.select_type_ == SELECT_ONE && use_fallback_option) {
        int fallback_option = def_.default_;
        if (fallback_option == -1) {
          std::string s = ssprintf(
              "No options in row \"list,%s\" were selected, and no fallback "
              "row found; selected entry 0",
              def_.name_.c_str());
          LOG->Warn("%s", s.c_str());
          CHECKPOINT_M(s);
          fallback_option = 0;
        }

        OptionRowHandlerUtil::SelectExactlyOne(fallback_option, sel_out);
      }

      VerifySelected(def_.select_type_, sel_out, def_.name_);
    }
  }

  int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& sel = selected[p];

      default_.Apply(p);
      for (unsigned i = 0; i < sel.size(); i++) {
        if (sel[i]) {
          list_entries_[i].Apply(p);
        }
      }
    }
    for (const std::string& s : broadcast_on_export_) {
      MESSAGEMAN->Broadcast(s);
    }
    return 0;
  }

  virtual int GetDefaultOption() const { return def_.default_; }

  virtual void GetIconTextAndGameCommand(
      int first_selection, std::string& icon_text_out, GameCommand& out) const {
    icon_text_out = use_mod_name_for_icon_
                        ? list_entries_[first_selection].preferred_modifiers_
                        : def_.choices_[first_selection];

    out = list_entries_[first_selection];
  }
  virtual std::string GetScreen(int choice) const {
    const GameCommand& gc = list_entries_[choice];
    return gc.screen_;
  }

  virtual ReloadChanged Reload() {
    // HACK: always reload "speed", to update the BPM text in the name of the
    // speed line
    if (!CompareNoCase(def_.name_, "speed")) {
      return RELOAD_CHANGED_ALL;
    }

    return OptionRowHandler::Reload();
  }
};

static void SortNoteSkins(std::vector<std::string>& skin_names) {
  std::set<std::string> set_skin_names;
  set_skin_names.insert(skin_names.begin(), skin_names.end());

  std::vector<std::string> sorted;
  split(NOTE_SKIN_SORT_ORDER, ",", sorted);

  std::set<std::string> set_unused_skin_names(set_skin_names);
  skin_names.clear();

  for (const std::string& skin : sorted) {
    if (set_skin_names.find(skin) == set_skin_names.end()) {
      continue;
    }
    skin_names.push_back(skin);
    set_unused_skin_names.erase(skin);
  }

  skin_names.insert(
      skin_names.end(), set_unused_skin_names.begin(),
      set_unused_skin_names.end());
}

class OptionRowHandlerListNoteSkins : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.name_ = "NoteSkins";
    def_.one_choice_for_all_players_ = false;
    def_.allow_theme_items_ = false;  // we theme the text ourself

    std::vector<std::string> array_skin_names;
    NOTESKIN->GetNoteSkinNames(array_skin_names);
    SortNoteSkins(array_skin_names);

    for (unsigned skin = 0; skin < array_skin_names.size(); skin++) {
      if (array_skin_names[skin] ==
          CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue()) {
        def_.default_ = skin;
      }
      GameCommand mc;
      mc.preferred_modifiers_ = array_skin_names[skin];
      // ms.m_sName = arraySkinNames[skin];
      list_entries_.push_back(mc);
      def_.choices_.push_back(array_skin_names[skin]);
    }
    return true;
  }
};

// XXX: very similar to OptionRowHandlerSteps
class OptionRowHandlerListSteps : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.name_ = "Steps";
    def_.allow_theme_items_ = false;  // we theme the text ourself

    Reload();

    // don't call default
    // OptionRowHandlerList::LoadInternal( cmds );
    return true;
  }

  virtual ReloadChanged Reload() {
    def_.choices_.clear();
    list_entries_.clear();

    // fill in difficulty names
    if (GAMESTATE->IsEditing()) {
      def_.choices_.push_back("");
      list_entries_.push_back(GameCommand());
    }
    // TODO:  Fix this OptionRow to fetch steps for all styles available.
    // This is broken in kickbox game mode because kickbox uses separated
    // styles. -Kyz
    else if (
        GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber()) &&
        GAMESTATE->IsCourseMode() &&
        GAMESTATE->cur_course_)  // playing a course
    {
      def_.one_choice_for_all_players_ =
          (bool)PREFSMAN->lock_course_difficulties_;
      def_.layout_type_ = StringToLayoutType(STEPS_ROW_LAYOUT_TYPE);

      std::vector<Trail*> trails;
      GAMESTATE->cur_course_->GetTrails(
          trails, GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                      ->steps_type_);
      for (unsigned i = 0; i < trails.size(); i++) {
        Trail* trail = trails[i];

        std::string s =
            CourseDifficultyToLocalizedString(trail->course_difficulty_);
        s += ssprintf(" %d", trail->GetMeter());
        def_.choices_.push_back(s);
        GameCommand mc;
        mc.trail_ = trail;
        list_entries_.push_back(mc);
      }
    } else if (
        GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber()) &&
        GAMESTATE->cur_song_)  // playing a song
    {
      def_.layout_type_ = StringToLayoutType(STEPS_ROW_LAYOUT_TYPE);

      std::vector<Steps*> vp_steps;
      Song* song = GAMESTATE->cur_song_;
      SongUtil::GetSteps(
          song, vp_steps,
          GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->steps_type_);
      StepsUtil::RemoveLockedSteps(song, vp_steps);
      StepsUtil::SortNotesArrayByDifficulty(vp_steps);
      for (unsigned i = 0; i < vp_steps.size(); i++) {
        Steps* p_steps = vp_steps[i];

        std::string s;
        if (STEPS_USE_CHART_NAME) {
          s = p_steps->GetChartName();
        }
        // TODO: find a way to make this use lua or metrics.
        if (s == "" || s == "blank" || s == "Blank") {
          if (p_steps->GetDifficulty() == Difficulty_Edit) {
            s = p_steps->GetChartName();
            if (s == "" || s == "blank" || s == "Blank") {
              s = p_steps->GetDescription();
            }
          } else {
            if (p_steps->IsAnEdit()) {
              s = p_steps->GetChartName();
              if (s == "" || s == "blank" || s == "Blank") {
                s = p_steps->GetDescription();
              }
            } else {
              s = CustomDifficultyToLocalizedString(GetCustomDifficulty(
                  p_steps->steps_type_, p_steps->GetDifficulty(),
                  CourseType_Invalid));
            }
          }
        }
        s += ssprintf(" %d", p_steps->GetMeter());
        def_.choices_.push_back(s);
        GameCommand mc;
        mc.steps_ = p_steps;
        mc.dc_ = p_steps->GetDifficulty();
        list_entries_.push_back(mc);
      }
    } else {
      /* We have neither a song nor a course. We may be preloading the
       * screen for future use. */
      def_.choices_.push_back("n/a");
      list_entries_.push_back(GameCommand());
    }

    return RELOAD_CHANGED_ALL;
  }
};

class OptionRowHandlerSteps : public OptionRowHandler {
 public:
  BroadcastOnChangePtr<Steps>* steps_to_fill_;
  BroadcastOnChange<Difficulty>* difficulty_to_fill_;
  const BroadcastOnChange<StepsType>* pst_;
  std::vector<Steps*> steps_;
  std::vector<Difficulty> difficulties_;

  OptionRowHandlerSteps() { Init(); }
  void Init() {
    OptionRowHandler::Init();
    steps_to_fill_ = nullptr;
    difficulty_to_fill_ = nullptr;
    steps_.clear();
    difficulties_.clear();
  }

  virtual bool LoadInternal(const Commands& cmds) {
    const Command& command = cmds.v_[0];
    std::string param = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    if (param == "EditSteps") {
      steps_to_fill_ = &GAMESTATE->cur_steps_[0];
      difficulty_to_fill_ = &GAMESTATE->preferred_difficulty_[0];
      pst_ = &GAMESTATE->st_edit_;
      reload_row_messages_.push_back(
          MessageIDToString(Message_EditStepsTypeChanged));
    } else if (param == "EditSourceSteps") {
      steps_to_fill_ = &GAMESTATE->edit_source_steps_;
      pst_ = &GAMESTATE->edit_source_;
      reload_row_messages_.push_back(
          MessageIDToString(Message_EditSourceStepsTypeChanged));
      if (GAMESTATE->cur_steps_[0].Get() != nullptr) {
        def_.enabled_for_players_.clear();  // hide row
      }
    } else {
      ROW_INVALID_IF(true, "Invalid StepsType param \"" + param + "\".", false);
    }

    def_.name_ = param;
    def_.one_choice_for_all_players_ = true;
    def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def_.export_on_change_ = true;
    def_.allow_theme_items_ = false;  // we theme the text ourself
    reload_row_messages_.push_back(
        MessageIDToString(Message_CurrentSongChanged));

    difficulties_.clear();
    steps_.clear();

    if (GAMESTATE->cur_song_) {
      FOREACH_ENUM(Difficulty, dc) {
        if (dc == Difficulty_Edit) {
          continue;
        }
        difficulties_.push_back(dc);
        Steps* steps =
            SongUtil::GetStepsByDifficulty(GAMESTATE->cur_song_, *pst_, dc);
        steps_.push_back(steps);
      }
      SongUtil::GetSteps(GAMESTATE->cur_song_, steps_, *pst_, Difficulty_Edit);
      difficulties_.resize(steps_.size(), Difficulty_Edit);

      if (param == "EditSteps") {
        steps_.push_back(nullptr);
        difficulties_.push_back(Difficulty_Edit);
      }

      for (unsigned i = 0; i < steps_.size(); i++) {
        Steps* steps = steps_[i];
        Difficulty dc = difficulties_[i];

        std::string s;
        if (dc == Difficulty_Edit) {
          if (steps) {
            s = steps->GetDescription();
          } else {
            s = "NewEdit";
          }
        } else {
          s = CustomDifficultyToLocalizedString(
              GetCustomDifficulty(GAMESTATE->st_edit_, dc, CourseType_Invalid));
        }
        def_.choices_.push_back(s);
      }
    } else {
      difficulties_.push_back(Difficulty_Edit);
      steps_.push_back(nullptr);
      def_.choices_.push_back("none");
    }

    if (difficulty_to_fill_) {
      difficulty_to_fill_->Set(difficulties_[0]);
    }
    steps_to_fill_->Set(steps_[0]);
    return true;
  }
  virtual void ImportOption(
      OptionRow* row, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& sel_out = selected_out[p];

      ASSERT(steps_.size() == sel_out.size());

      // look for matching steps
      std::vector<Steps*>::const_iterator iter =
          find(steps_.begin(), steps_.end(), steps_to_fill_->Get());
      if (iter != steps_.end()) {
        unsigned i = iter - steps_.begin();
        sel_out[i] = true;
        continue;
      }
      // look for matching difficulty
      bool matched = false;
      if (difficulty_to_fill_) {
        // use the old style for now.
        for (std::vector<Difficulty>::const_iterator d = difficulties_.begin();
             d != difficulties_.end(); ++d) {
          unsigned i = d - difficulties_.begin();
          if (*d == GAMESTATE->preferred_difficulty_[p]) {
            sel_out[i] = true;
            matched = true;
            std::vector<PlayerNumber> v;
            v.push_back(p);
            ExportOption(v, selected_out);  // current steps changed
            break;
          }
        }
      }
      if (!matched) {
        // default to 1st
        sel_out[0] = true;
      }
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& sel = selected[p];

      int index = OptionRowHandlerUtil::GetOneSelection(sel);
      Difficulty dc = difficulties_[index];
      Steps* steps = steps_[index];
      if (difficulty_to_fill_) {
        difficulty_to_fill_->Set(dc);
      }
      steps_to_fill_->Set(steps);
    }

    return 0;
  }
};

class OptionRowHandlerListCharacters : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.one_choice_for_all_players_ = false;
    def_.allow_theme_items_ = false;
    def_.name_ = "Characters";
    def_.default_ = 0;
    default_.character_ = CHARMAN->GetDefaultCharacter();

    {
      def_.choices_.push_back(OFF);
      GameCommand mc;
      mc.character_ = nullptr;
      list_entries_.push_back(mc);
    }

    std::vector<Character*> characters;
    CHARMAN->GetCharacters(characters);
    for (unsigned i = 0; i < characters.size(); i++) {
      Character* character = characters[i];
      std::string s = character->GetDisplayName();
      MakeUpper(s);

      def_.choices_.push_back(s);
      GameCommand mc;
      mc.character_ = character;
      list_entries_.push_back(mc);
    }
    return true;
  }
};

class OptionRowHandlerListStyles : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.one_choice_for_all_players_ = true;
    def_.name_ = "Style";
    def_.allow_theme_items_ = false;  // we theme the text ourself

    std::vector<const Style*> styles;
    GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, styles);
    ASSERT(styles.size() != 0);
    for (const Style* s : styles) {
      def_.choices_.push_back(GAMEMAN->StyleToLocalizedString(s));
      GameCommand mc;
      mc.style_ = s;
      list_entries_.push_back(mc);
    }

    default_.style_ = styles[0];
    return true;
  }
};

class OptionRowHandlerListGroups : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.one_choice_for_all_players_ = true;
    def_.allow_theme_items_ = false;  // we theme the text ourself
    def_.name_ = "Group";
    default_.song_group_ = kGroupAll;

    std::vector<std::string> song_groups;
    SONGMAN->GetSongGroupNames(song_groups);
    ASSERT(song_groups.size() != 0);

    {
      def_.choices_.push_back("AllGroups");
      GameCommand mc;
      mc.song_group_ = kGroupAll;
      list_entries_.push_back(mc);
    }

    for (const std::string& g : song_groups) {
      def_.choices_.push_back(g);
      GameCommand mc;
      mc.song_group_ = g;
      list_entries_.push_back(mc);
    }
    return true;
  }
};

class OptionRowHandlerListDifficulties : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.one_choice_for_all_players_ = true;
    def_.name_ = "Difficulty";
    default_.dc_ = Difficulty_Invalid;
    def_.allow_theme_items_ = false;  // we theme the text ourself

    {
      def_.choices_.push_back("AllDifficulties");
      GameCommand mc;
      mc.dc_ = Difficulty_Invalid;
      list_entries_.push_back(mc);
    }

    for (const Difficulty& d : CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue()) {
      // TODO: Is this the best thing we can do here?
      StepsType st =
          GAMEMAN->GetHowToPlayStyleForGame(GAMESTATE->cur_game_)->steps_type_;
      std::string s = CustomDifficultyToLocalizedString(
          GetCustomDifficulty(st, d, CourseType_Invalid));

      def_.choices_.push_back(s);
      GameCommand mc;
      mc.dc_ = d;
      list_entries_.push_back(mc);
    }
    return true;
  }
};

// XXX: very similar to OptionRowHandlerSongChoices
class OptionRowHandlerListSongsInCurrentSongGroup
    : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    const std::vector<Song*>& songs =
        SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);

    if (GAMESTATE->cur_song_ == nullptr) {
      GAMESTATE->cur_song_.Set(songs[0]);
    }

    def_.name_ = "SongsInCurrentSongGroup";
    def_.one_choice_for_all_players_ = true;
    def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def_.export_on_change_ = true;

    for (Song* p : songs) {
      def_.choices_.push_back(p->GetTranslitFullTitle());
      GameCommand mc;
      mc.song_ = p;
      list_entries_.push_back(mc);
    }
    return true;
  }
};

class OptionRowHandlerLua : public OptionRowHandler {
 public:
  LuaReference* lua_table_;
  LuaReference enabled_for_players_func_;
  LuaReference reload_func_;

  bool table_is_sane_;
  bool go_to_first_on_start_;

  OptionRowHandlerLua() : table_is_sane_(false), go_to_first_on_start_(false) {
    lua_table_ = new LuaReference;
    Init();
  }
  virtual ~OptionRowHandlerLua() { delete lua_table_; }
  void Init() {
    OptionRowHandler::Init();
    lua_table_->Unset();
  }

  bool SanityCheckTable(lua_State* L, std::string& row_name) {
    if (lua_table_->GetLuaType() != LUA_TTABLE) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  Result of \"%s\" is not a table.", row_name.c_str());
      return false;
    }
    lua_table_->PushSelf(L);
    lua_getfield(L, -1, "Name");
    const char* str = lua_tostring(L, -1);
    if (str == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"Name\" entry is not a string.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "LayoutType");
    str = lua_tostring(L, -1);
    if (str == nullptr || StringToLayoutType(str) == LayoutType_Invalid) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"LayoutType\" entry is not a string.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "SelectType");
    str = lua_tostring(L, -1);
    if (str == nullptr || StringToSelectType(str) == SelectType_Invalid) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"SelectType\" entry is not a string.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "Choices");
    if (!lua_istable(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"Choices\" is not a table.", row_name.c_str());
      return false;
    }
    if (!TableContainsOnlyStrings(L, lua_gettop(L))) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"Choices\" table contains a non-string.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "EnabledForPlayers");
    if (!lua_isnil(L, -1)) {
      if (!lua_isfunction(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"EnabledForPlayers\" is not a function.",
            row_name.c_str());
        return false;
      }
      lua_table_->PushSelf(L);
      std::string error = row_name + " \"EnabledForPlayers\": ";
      LuaHelpers::RunScriptOnStack(L, error, 1, 1, true);
      if (!lua_istable(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"EnabledForPlayers\" did not return a table.",
            row_name.c_str());
        return false;
      }
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        PlayerNumber pn = Enum::Check<PlayerNumber>(L, -1, true, true);
        if (pn == PlayerNumber_Invalid) {
          LuaHelpers::ReportScriptErrorFmt(
              "LUA_ERROR:  \"%s\" \"EnabledForPlayers\" contains a "
              "non-PlayerNumber.",
              row_name.c_str());
          return false;
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "ReloadRowMessages");
    if (!lua_isnil(L, -1)) {
      if (!lua_istable(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"ReloadRowMessages\" is not a table.",
            row_name.c_str());
        return false;
      }
      if (!TableContainsOnlyStrings(L, lua_gettop(L))) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"ReloadRowMessages\" table contains a "
            "non-string.",
            row_name.c_str());
        return false;
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "Reload");
    if (!lua_isnil(L, -1)) {
      if (!lua_isfunction(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"Reload\" entry is not a function.",
            row_name.c_str());
        return false;
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "LoadSelections");
    if (!lua_isfunction(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"LoadSelections\" entry is not a function.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "SaveSelections");
    if (!lua_isfunction(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"SaveSelections\" entry is not a function.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "NotifyOfSelection");
    if (!lua_isnil(L, -1) && !lua_isfunction(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"NotifyOfSelection\" entry is not a function.",
          row_name.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_pop(L, 1);
    return true;
  }

  void SetEnabledForPlayers() {
    if (!table_is_sane_) {
      return;
    }
    Lua* L = LUA->Get();

    if (enabled_for_players_func_.IsNil()) {
      LUA->Release(L);
      return;
    }

    enabled_for_players_func_.PushSelf(L);

    // Argument 1 (self):
    lua_table_->PushSelf(L);

    std::string error = "EnabledForPlayers: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 1, true);
    def_.enabled_for_players_
        .clear();  // and fill in with supplied PlayerNumbers below

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      // `key' is at index -2 and `value' at index -1
      PlayerNumber pn = Enum::Check<PlayerNumber>(L, -1);

      def_.enabled_for_players_.insert(pn);

      lua_pop(L, 1);  // removes `value'; keeps `key' for next iteration
    }
    lua_pop(L, 1);

    LUA->Release(L);
  }

  void LoadChoices(Lua* L) {
    // Iterate over the "Choices" table.
    lua_getfield(L, -1, "Choices");
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      // `key' is at index -2 and `value' at index -1
      const char* value = lua_tostring(L, -1);
      // LOG->Trace( "choice: '%s'", pValue);
      def_.choices_.push_back(value);
      lua_pop(L, 1);  // removes `value'; keeps `key' for next iteration
    }
    lua_pop(L, 1);  // pop choices table
  }

  virtual bool LoadInternal(const Commands& cmds) {
    const Command& command = cmds.v_[0];
    std::string param = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    def_.allow_theme_items_ =
        false;  // Lua options are always dynamic and can theme themselves.

    Lua* L = LUA->Get();

    // Run the Lua expression.  It should return a table.
    lua_table_->SetFromExpression(param);
    table_is_sane_ = SanityCheckTable(L, param);
    if (!table_is_sane_) {
      lua_settop(L, 0);  // Release has an assert that forces a clear stack.
      LUA->Release(L);
      return false;
    }
    lua_table_->PushSelf(L);

    lua_getfield(L, -1, "Name");
    const char* str = lua_tostring(L, -1);
    def_.name_ = str;
    lua_pop(L, 1);

    lua_getfield(L, -1, "GoToFirstOnStart");
    go_to_first_on_start_ = lua_toboolean(L, -1) > 0;
    lua_pop(L, 1);

    lua_getfield(L, -1, "OneChoiceForAllPlayers");
    def_.one_choice_for_all_players_ = lua_toboolean(L, -1) > 0;
    lua_pop(L, 1);

    lua_getfield(L, -1, "ExportOnChange");
    def_.export_on_change_ = lua_toboolean(L, -1) > 0;
    lua_pop(L, 1);

    lua_getfield(L, -1, "HideOnDisable");
    def_.hide_on_disable_ = lua_toboolean(L, -1) > 0;
    lua_pop(L, 1);

    // TODO:  Change these to use the proper enum strings like everything
    // else.  This will break theme compatibility, so it has to wait until
    // after SM5.  -Kyz
    lua_getfield(L, -1, "LayoutType");
    str = lua_tostring(L, -1);
    def_.layout_type_ = StringToLayoutType(str);
    lua_pop(L, 1);

    lua_getfield(L, -1, "SelectType");
    str = lua_tostring(L, -1);
    def_.select_type_ = StringToSelectType(str);
    lua_pop(L, 1);

    LoadChoices(L);

    // Set the EnabledForPlayers function.
    lua_getfield(L, -1, "EnabledForPlayers");
    enabled_for_players_func_.SetFromStack(L);
    SetEnabledForPlayers();

    // Iterate over the "ReloadRowMessages" table.
    lua_getfield(L, -1, "ReloadRowMessages");
    if (!lua_isnil(L, -1)) {
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        // `key' is at index -2 and `value' at index -1
        const char* value = lua_tostring(L, -1);
        // LOG->Trace( "Found ReloadRowMessage '%s'", pValue);
        reload_row_messages_.push_back(value);
        lua_pop(L, 1);  // removes `value'; keeps `key' for next iteration
      }
    }
    lua_pop(L, 1);  // pop ReloadRowMessages table

    // Set the Reload function
    lua_getfield(L, -1, "Reload");
    reload_func_.SetFromStack(L);

    lua_pop(L, 1);  // pop main table
    ASSERT(lua_gettop(L) == 0);

    LUA->Release(L);
    return table_is_sane_;
  }

  virtual ReloadChanged Reload() {
    if (!table_is_sane_) {
      return RELOAD_CHANGED_NONE;
    }

    /* We'll always call SetEnabledForPlayers, and
     * return at least RELOAD_CHANGED_ENABLED,
     * to preserve original OptionRowHandlerLua behavior.
     *
     * Will also call the standard OptionRowHandler::Reload
     * function to determine whether we should declare a full
     * RELOAD_CHANGED_ALL
     */
    ReloadChanged effect = RELOAD_CHANGED_ENABLED;

    if (!reload_func_.IsNil()) {
      Lua* L = LUA->Get();
      reload_func_.PushSelf(L);

      // Argument 1: (self)
      lua_table_->PushSelf(L);
      std::string error = "Reload: ";

      LuaHelpers::RunScriptOnStack(L, error, 1, 1, true);
      effect = std::max(effect, Enum::Check<ReloadChanged>(L, -1));
      lua_pop(L, 1);

      if (effect == RELOAD_CHANGED_ALL) {
        def_.choices_.clear();
        lua_table_->PushSelf(L);
        LoadChoices(L);
        lua_pop(L, 1);
        ASSERT(lua_gettop(L) == 0);
      }
      LUA->Release(L);
    }

    SetEnabledForPlayers();
    return effect;
  }

  virtual void ImportOption(
      OptionRow* row, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[kNumPlayers]) const {
    if (!table_is_sane_) {
      return;
    }
    Lua* L = LUA->Get();

    ASSERT(lua_gettop(L) == 0);

    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& sel_out = selected_out[p];

      /* Evaluate the LoadSelections(self,array,pn) function, where
       * array is a table representing vbSelectedOut. */

      // All selections default to false.
      for (unsigned i = 0; i < sel_out.size(); ++i) {
        sel_out[i] = false;
      }

      // Create the vbSelectedOut table
      LuaHelpers::CreateTableFromArrayB(L, sel_out);
      ASSERT(lua_gettop(L) == 1);  // vbSelectedOut table

      // Get the function to call from m_LuaTable.
      lua_table_->PushSelf(L);
      ASSERT(lua_istable(L, -1));

      lua_getfield(L, -1, "LoadSelections");

      // Argument 1 (self):
      lua_table_->PushSelf(L);

      // Argument 2 (vbSelectedOut):
      lua_pushvalue(L, 1);

      // Argument 3 (pn):
      LuaHelpers::Push(L, p);

      ASSERT(
          lua_gettop(L) ==
          6);  // vbSelectedOut, m_iLuaTable, function, self, arg, arg

      std::string error = "LoadSelections: ";
      LuaHelpers::RunScriptOnStack(L, error, 3, 0, true);
      ASSERT(lua_gettop(L) == 2);

      lua_pop(L, 1);  // pop option table

      LuaHelpers::ReadArrayFromTableB(L, sel_out);

      lua_pop(L, 1);  // pop vbSelectedOut table

      ASSERT(lua_gettop(L) == 0);
    }

    LUA->Release(L);
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[kNumPlayers]) const {
    if (!table_is_sane_) {
      return 0;
    }
    Lua* L = LUA->Get();

    ASSERT(lua_gettop(L) == 0);

    int effects = 0;
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& sel = selected[p];

      /* Evaluate SaveSelections(self,array,pn) function, where array is
       * a table representing vbSelectedOut. */

      std::vector<bool> selected_copy = sel;

      // Create the vbSelectedOut table.
      LuaHelpers::CreateTableFromArrayB(L, selected_copy);
      ASSERT(lua_gettop(L) == 1);  // vbSelectedOut table

      // Get the function to call.
      lua_table_->PushSelf(L);
      ASSERT(lua_istable(L, -1));

      lua_getfield(L, -1, "SaveSelections");

      // Argument 1 (self):
      lua_table_->PushSelf(L);

      // Argument 2 (vbSelectedOut):
      lua_pushvalue(L, 1);

      // Argument 3 (pn):
      LuaHelpers::Push(L, p);

      ASSERT(
          lua_gettop(L) ==
          6);  // vbSelectedOut, m_iLuaTable, function, self, arg, arg

      std::string error = "SaveSelections: ";
      LuaHelpers::RunScriptOnStack(L, error, 3, 1, true);
      ASSERT(
          lua_gettop(L) ==
          3);  // SaveSelections *may* return effects flags, otherwise nil
      double ret = lua_tonumber(L, -1);
      ASSERT_M(
          (lua_isnumber(L, -1) && std::floor(ret) == ret) || lua_isnil(L, -1),
          "SaveSelections must return integer flags, or nill");
      effects |= static_cast<int>(ret);

      lua_pop(L, 1);  // pop effects
      lua_pop(L, 1);  // pop option table
      lua_pop(L, 1);  // pop vbSelected table

      ASSERT(lua_gettop(L) == 0);
    }

    LUA->Release(L);

    return effects;
  }
  virtual bool NotifyOfSelection(PlayerNumber pn, int choice) {
    if (!table_is_sane_) {
      return false;
    }
    Lua* L = LUA->Get();
    lua_table_->PushSelf(L);

    lua_getfield(L, -1, "NotifyOfSelection");
    bool changed = false;
    if (lua_isfunction(L, -1)) {
      lua_table_->PushSelf(L);
      LuaHelpers::Push(L, pn);
      // Convert choice to a lua index so it matches up with the Choices table.
      lua_pushinteger(L, choice + 1);
      std::string error = "NotifyOfSelection: ";
      LuaHelpers::RunScriptOnStack(L, error, 3, 1, true);
      if (lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        changed = true;
        def_.choices_.clear();
        // Iterate over the "Choices" table.
        lua_getfield(L, -1, "Choices");
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
          // `key' is at index -2 and `value' at index -1
          const char* value = lua_tostring(L, -1);
          // LOG->Trace( "choice: '%s'", pValue);
          def_.choices_.push_back(value);
          lua_pop(L, 1);  // removes `value'; keeps `key' for next iteration
        }
      }
    }
    lua_settop(L, 0);  // Release has an assert that forces a clear stack.
    LUA->Release(L);
    return changed;
  }
  virtual bool GoToFirstOnStart() const { return go_to_first_on_start_; }
};

class OptionRowHandlerConfig : public OptionRowHandler {
 public:
  const ConfOption* opt_;

  OptionRowHandlerConfig() { Init(); }
  void Init() {
    OptionRowHandler::Init();
    opt_ = nullptr;
  }
  virtual bool LoadInternal(const Commands& cmds) {
    const Command& command = cmds.v_[0];
    std::string param = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    Init();

    // Configuration values are never per-player.
    def_.one_choice_for_all_players_ = true;

    ConfOption* conf_option = ConfOption::Find(param);
    ROW_INVALID_IF(
        conf_option == nullptr, "Invalid Conf type \"" + param + "\".", false);

    conf_option->UpdateAvailableOptions();

    opt_ = conf_option;
    opt_->MakeOptionsList(def_.choices_);

    def_.allow_theme_items_ = opt_->allow_theme_items_;

    def_.name_ = opt_->name_;
    return true;
  }
  virtual void ImportOption(
      OptionRow*, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& sel_out = selected_out[p];

      int selection = opt_->Get();
      OptionRowHandlerUtil::SelectExactlyOne(selection, sel_out);
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[kNumPlayers]) const {
    bool changed = false;

    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& vb_sel = selected[p];

      int i_sel = OptionRowHandlerUtil::GetOneSelection(vb_sel);

      // Get the original choice.
      int original = opt_->Get();

      // Apply.
      opt_->Put(i_sel);

      // Get the new choice.
      int i_new = opt_->Get();

      // If it didn't change, don't return any side-effects.
      if (original != i_new) {
        changed = true;
      }
    }

    return changed ? opt_->GetEffects() : 0;
  }
};

class OptionRowHandlerStepsType : public OptionRowHandler {
 public:
  BroadcastOnChange<StepsType>* to_fill_;
  std::vector<StepsType> steps_types_to_show_;

  OptionRowHandlerStepsType() { Init(); }
  void Init() {
    OptionRowHandler::Init();
    to_fill_ = nullptr;
    steps_types_to_show_.clear();
  }

  virtual bool LoadInternal(const Commands& cmds) {
    const Command& command = cmds.v_[0];
    std::string param = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    if (param == "EditStepsType") {
      to_fill_ = &GAMESTATE->st_edit_;
    } else if (param == "EditSourceStepsType") {
      to_fill_ = &GAMESTATE->edit_source_;
      reload_row_messages_.push_back(
          MessageIDToString(Message_CurrentStepsP1Changed));
      reload_row_messages_.push_back(
          MessageIDToString(Message_EditStepsTypeChanged));
      if (GAMESTATE->cur_steps_[0].Get() != nullptr) {
        def_.enabled_for_players_.clear();  // hide row
      }
    } else {
      ROW_INVALID_IF(true, "Invalid StepsType param \"" + param + "\".", false);
    }

    def_.name_ = param;
    def_.one_choice_for_all_players_ = true;
    def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def_.export_on_change_ = true;
    def_.allow_theme_items_ = false;  // we theme the text ourself

    // calculate which StepsTypes to show
    steps_types_to_show_ = CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue();

    def_.choices_.clear();
    for (const StepsType& st : steps_types_to_show_) {
      std::string s = GAMEMAN->GetStepsTypeInfo(st).GetLocalizedString();
      def_.choices_.push_back(s);
    }

    if (*to_fill_ == StepsType_Invalid) {
      to_fill_->Set(steps_types_to_show_[0]);
    }
    return true;
  }

  virtual void ImportOption(
      OptionRow* row, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& sel_out = selected_out[p];

      if (GAMESTATE->cur_steps_[0]) {
        StepsType st = GAMESTATE->cur_steps_[0]->steps_type_;
        std::vector<StepsType>::const_iterator iter =
            find(steps_types_to_show_.begin(), steps_types_to_show_.end(), st);
        if (iter != steps_types_to_show_.end()) {
          unsigned i = iter - steps_types_to_show_.begin();
          sel_out[i] = true;
          continue;  // done with this player
        }
      }
      sel_out[0] = true;
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[kNumPlayers]) const {
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& sel = selected[p];

      int index = OptionRowHandlerUtil::GetOneSelection(sel);
      to_fill_->Set(steps_types_to_show_[index]);
    }

    return 0;
  }
};

class OptionRowHandlerGameCommand : public OptionRowHandler {
 public:
  GameCommand gc_;

  OptionRowHandlerGameCommand() { Init(); }
  void Init() {
    OptionRowHandler::Init();
    gc_.Init();
    gc_.ApplyCommitsScreens(false);
  }
  virtual bool LoadInternal(const Commands& cmds) {
    ROW_INVALID_IF(
        cmds.v_.size() <= 1, "No args to construct GameCommand.", false);

    Commands temp = cmds;
    temp.v_.erase(temp.v_.begin());
    gc_.Load(0, temp);
    ROW_INVALID_IF(gc_.name_.empty(), "GameCommand row has no name.", false);
    def_.name_ = gc_.name_;
    def_.one_choice_for_all_players_ = true;
    def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def_.select_type_ = SELECT_NONE;
    def_.choices_.push_back("");
    return true;
  }
  virtual void ImportOption(
      OptionRow* row, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[kNumPlayers]) const {}
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[kNumPlayers]) const {
    if (selected[PLAYER_1][0] || selected[PLAYER_2][0]) {
      gc_.ApplyToAllPlayers();
    }
    return 0;
  }
  virtual void GetIconTextAndGameCommand(
      int first_selection, std::string& icon_text_out, GameCommand& out) const {
    icon_text_out = "";
    out = gc_;
  }
  virtual std::string GetScreen(int choice) const { return gc_.screen_; }
};

class OptionRowHandlerNull : public OptionRowHandler {
 public:
  OptionRowHandlerNull() { Init(); }
};

///////////////////////////////////////////////////////////////////////////////////

OptionRowHandler* OptionRowHandlerUtil::Make(const Commands& cmds) {
  OptionRowHandler* pHand = nullptr;

  ROW_INVALID_IF(
      cmds.v_.size() == 0, "No commands for constructing row.", nullptr);
  const std::string& name = cmds.v_[0].GetName();
  ROW_INVALID_IF(
      name != "gamecommand" && cmds.v_.size() != 1,
      "Row must be constructed from single command.", nullptr);

  bool load_succeeded = false;
#define MAKE(type)                  \
  {                                 \
    type* p = new type;             \
    load_succeeded = p->Load(cmds); \
    pHand = p;                      \
  }

  // XXX: merge these, and merge "Steps" and "list,Steps"
  if (name == "list") {
    const Command& command = cmds.v_[0];
    std::string param = command.GetArg(1).s_;
    ROW_INVALID_IF(
        command.args_.size() != 2 || !param.size(),
        "list row command must be 'list,name' or 'list,type'.", nullptr);

    if (CompareNoCase(param, "NoteSkins") == 0) {
      MAKE(OptionRowHandlerListNoteSkins)
    } else if (CompareNoCase(param, "Steps") == 0) {
      MAKE(OptionRowHandlerListSteps)
    } else if (CompareNoCase(param, "StepsLocked") == 0) {
      MAKE(OptionRowHandlerListSteps);
      pHand->def_.one_choice_for_all_players_ = true;
    } else if (CompareNoCase(param, "Characters") == 0) {
      MAKE(OptionRowHandlerListCharacters)
    } else if (CompareNoCase(param, "Styles") == 0) {
      MAKE(OptionRowHandlerListStyles)
    } else if (CompareNoCase(param, "Groups") == 0) {
      MAKE(OptionRowHandlerListGroups)
    } else if (CompareNoCase(param, "Difficulties") == 0) {
      MAKE(OptionRowHandlerListDifficulties)
    } else if (CompareNoCase(param, "SongsInCurrentSongGroup") == 0) {
      MAKE(OptionRowHandlerListSongsInCurrentSongGroup)
    } else {
      MAKE(OptionRowHandlerList)
    }
  } else if (name == "lua") {
    MAKE(OptionRowHandlerLua)
  } else if (name == "conf") {
    MAKE(OptionRowHandlerConfig)
  } else if (name == "stepstype") {
    MAKE(OptionRowHandlerStepsType)
  } else if (name == "steps") {
    MAKE(OptionRowHandlerSteps)
  } else if (name == "gamecommand") {
    MAKE(OptionRowHandlerGameCommand)
  } else {
    ROW_INVALID_IF(true, "Invalid row type.", nullptr);
  }

  if (load_succeeded) {
    return pHand;
  }
  return nullptr;
}

OptionRowHandler* OptionRowHandlerUtil::MakeNull() {
  OptionRowHandler* pHand = nullptr;
  bool load_succeeded = false;  // Part of the MAKE macro, but unused.
  Commands cmds;
  MAKE(OptionRowHandlerNull)
  if (load_succeeded)  // Just to get rid of the warning for not using it.
  {
    return pHand;
  }
  return nullptr;
}

OptionRowHandler* OptionRowHandlerUtil::MakeSimple(const MenuRowDef& mr) {
  OptionRowHandler* hand = OptionRowHandlerUtil::MakeNull();

  hand->def_.name_ = mr.name_;
  FontCharAliases::ReplaceMarkers(
      hand->def_.name_);  // Allow special characters

  hand->def_.enabled_for_players_.clear();
  if (mr.pfn_enabled_ ? mr.pfn_enabled_() : mr.b_enabled_) {
    FOREACH_EnabledPlayer(pn) hand->def_.enabled_for_players_.insert(pn);
  }

  hand->def_.one_choice_for_all_players_ = true;
  hand->def_.select_type_ = SELECT_ONE;
  hand->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
  hand->def_.export_on_change_ = false;  // true;

  // MISTER CHOICES!
  hand->def_.choices_ = mr.choices_;

  // Each row must have at least one choice.
  if (hand->def_.choices_.empty()) {
    hand->def_.choices_.push_back("");
  }

  hand->def_.allow_theme_title_ = mr.theme_title_;
  hand->def_.allow_theme_items_ = mr.theme_items_;

  for (std::string& c : hand->def_.choices_) {
    FontCharAliases::ReplaceMarkers(c);  // Allow special characters
  }

  return hand;
}

// Expose ReloadChanged to Lua
static const char* ReloadChangedNames[] = {"None", "Enabled", "All"};
XToString(ReloadChanged);
StringToX(ReloadChanged);
LuaXType(ReloadChanged);

/*
 * (c) 2002-2004 Chris Danford
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
