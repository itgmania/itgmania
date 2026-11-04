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
  bool bTheme = false;

  // HACK: Always theme the NEXT_ROW and EXIT items, even if metrics says not to
  // theme.
  if (def_.allow_theme_title_) {
    bTheme = true;
  }

  std::string s = def_.name_;
  if (s.empty()) {
    return s;
  }

  return bTheme ? THEME->GetString("OptionTitles", s) : s;
}

std::string OptionRowHandler::GetThemedItemText(int iChoice) const {
  std::string s = def_.choices_[iChoice];
  if (s == "") {
    return "";
  }
  bool bTheme = false;

  if (def_.allow_theme_items_) {
    bTheme = true;
  }

  // Items beginning with a pipe mean "don't theme".
  // This allows us to disable theming on a per-choice basis for choice names
  // that are just a number and don't need to be localized.
  if (s[0] == '|') {
    s.erase(s.begin());
    bTheme = false;
  }

  if (bTheme) {
    s = CommonMetrics::LocalizeOptionItem(s, false);
  }
  return s;
}

void OptionRowHandler::GetIconTextAndGameCommand(
    int iFirstSelection, std::string& sIconTextOut, GameCommand& gcOut) const {
  sIconTextOut = "";
  gcOut.Init();
}

void OptionRowHandlerUtil::SelectExactlyOne(
    int iSelection, std::vector<bool>& vbSelectedOut) {
  ASSERT_M(
      iSelection >= 0 && iSelection < (int)vbSelectedOut.size(),
      ssprintf("%d/%u", iSelection, unsigned(vbSelectedOut.size())));
  for (int i = 0; i < int(vbSelectedOut.size()); i++) {
    vbSelectedOut[i] = i == iSelection;
  }
}

int OptionRowHandlerUtil::GetOneSelection(const std::vector<bool>& vbSelected) {
  int iRet = -1;
  for (unsigned i = 0; i < vbSelected.size(); i++) {
    if (vbSelected[i]) {
      ASSERT(iRet == -1);  // only one should be selected
      iRet = i;
    }
  }
  ASSERT(
      iRet != -1);  // shouldn't call this if not expecting one to be selected
  return iRet;
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
  ROW_INVALID_IF(sParam.size() == 0, "Blank arg to Steps row.", false);

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
    std::string sParam = command.GetArg(1).s_;

    use_mod_name_for_icon_ = true;
    def_.name_ = sParam;
    default_.Load(-1, ParseCommands(ENTRY_DEFAULT(sParam)));

    {
      // Parse the basic configuration metric.
      Commands lCmds = ParseCommands(ENTRY(sParam));
      ROW_INVALID_IF(lCmds.v_.size() < 1, "Row command is empty.", false);

      def_.one_choice_for_all_players_ = false;
      ROW_INVALID_IF(
          lCmds.v_[0].args_.size() != 1,
          "Row command has invalid args to number of entries.", false);
      const int NumCols = StringToInt(lCmds.v_[0].args_[0]);
      ROW_INVALID_IF(NumCols < 1, "Not enough entries in list.", false);
      for (unsigned i = 1; i < lCmds.v_.size(); i++) {
        const Command& cmd = lCmds.v_[i];
        std::string sName = cmd.GetName();

        if (sName == "together") {
          def_.one_choice_for_all_players_ = true;
        } else if (sName == "selectmultiple") {
          def_.select_type_ = SELECT_MULTIPLE;
        } else if (sName == "selectone") {
          def_.select_type_ = SELECT_ONE;
        } else if (sName == "selectnone") {
          def_.select_type_ = SELECT_NONE;
        } else if (sName == "showoneinrow") {
          def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
        } else if (sName == "default") {
          def_.default_ =
              StringToInt(cmd.GetArg(1).s_) - 1;  // match ENTRY_MODE
        } else if (sName == "reloadrowmessages") {
          for (unsigned a = 1; a < cmd.args_.size(); a++) {
            reload_row_messages_.push_back(cmd.args_[a]);
          }
        } else if (sName == "enabledforplayers") {
          def_.enabled_for_players_.clear();
          for (unsigned a = 1; a < cmd.args_.size(); a++) {
            std::string sArg = cmd.args_[a];
            PlayerNumber pn = (PlayerNumber)(StringToInt(sArg) - 1);
            ASSERT(pn >= 0 && pn < NUM_PLAYERS);
            def_.enabled_for_players_.insert(pn);
          }
        } else if (sName == "exportonchange") {
          def_.export_on_change_ = true;
        } else if (sName == "hideondisable") {
          def_.hide_on_disable_ = true;
        } else if (sName == "broadcastonexport") {
          for (unsigned j = 1; j < cmd.args_.size(); j++) {
            broadcast_on_export_.push_back(cmd.args_[j]);
          }
        } else {
          LuaHelpers::ReportScriptErrorFmt(
              "Unknown row flag \"%s\" on row %s.", sName.c_str(),
              def_.name_.c_str());
        }
      }
      for (int col = 0; col < NumCols; ++col) {
        GameCommand mc;
        mc.ApplyCommitsScreens(false);
        mc.Load(0, ParseCommands(ENTRY_MODE(sParam, col)));
        /* If the row has just one entry, use the name of the row as the name of
         * the entry. If it has more than one, each one must be specified
         * explicitly. */
        if (mc.name_ == "" && NumCols == 1) {
          mc.name_ = sParam;
        }
        if (mc.name_ == "") {
          LuaHelpers::ReportScriptErrorFmt(
              "List \"%s\", choice %i has no name.", sParam.c_str(), col + 1);
          mc.name_ = "";
        }

        std::string why;
        if (!mc.IsPlayable(&why)) {
          LuaHelpers::ReportScriptErrorFmt(
              "\"%s\" choice %d is not playable: %s", sParam.c_str(), col,
              why.c_str());
          continue;
        }

        list_entries_.push_back(mc);
        std::string sChoice = mc.name_;
        def_.choices_.push_back(sChoice);
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
      OptionRow* pRow, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> vbSelectedOut[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& vbSelOut = vbSelectedOut[p];

      bool bUseFallbackOption = true;

      for (unsigned e = 0; e < list_entries_.size(); ++e) {
        const GameCommand& mc = list_entries_[e];

        vbSelOut[e] = false;

        if (mc.IsZero()) {
          /* The entry has no effect. This is usually a default "none
           * of the above" entry. It will always return true for
           * DescribesCurrentMode(). It's only the selected choice if
           * nothing else matches. */
          continue;
        }

        if (def_.one_choice_for_all_players_) {
          if (mc.DescribesCurrentModeForAllPlayers()) {
            bUseFallbackOption = false;
            if (def_.select_type_ != SELECT_MULTIPLE) {
              OptionRowHandlerUtil::SelectExactlyOne(e, vbSelOut);
            } else {
              vbSelOut[e] = true;
            }
          }
        } else {
          if (mc.DescribesCurrentMode(p)) {
            bUseFallbackOption = false;
            if (def_.select_type_ != SELECT_MULTIPLE) {
              OptionRowHandlerUtil::SelectExactlyOne(e, vbSelOut);
            } else {
              vbSelOut[e] = true;
            }
          }
        }
      }

      if (def_.select_type_ == SELECT_ONE && bUseFallbackOption) {
        int iFallbackOption = def_.default_;
        if (iFallbackOption == -1) {
          std::string s = ssprintf(
              "No options in row \"list,%s\" were selected, and no fallback "
              "row found; selected entry 0",
              def_.name_.c_str());
          LOG->Warn("%s", s.c_str());
          CHECKPOINT_M(s);
          iFallbackOption = 0;
        }

        OptionRowHandlerUtil::SelectExactlyOne(iFallbackOption, vbSelOut);
      }

      VerifySelected(def_.select_type_, vbSelOut, def_.name_);
    }
  }

  int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> vbSelected[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& vbSel = vbSelected[p];

      default_.Apply(p);
      for (unsigned i = 0; i < vbSel.size(); i++) {
        if (vbSel[i]) {
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
      int iFirstSelection, std::string& sIconTextOut,
      GameCommand& gcOut) const {
    sIconTextOut = use_mod_name_for_icon_
                       ? list_entries_[iFirstSelection].preferred_modifiers_
                       : def_.choices_[iFirstSelection];

    gcOut = list_entries_[iFirstSelection];
  }
  virtual std::string GetScreen(int iChoice) const {
    const GameCommand& gc = list_entries_[iChoice];
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

static void SortNoteSkins(std::vector<std::string>& asSkinNames) {
  std::set<std::string> setSkinNames;
  setSkinNames.insert(asSkinNames.begin(), asSkinNames.end());

  std::vector<std::string> asSorted;
  split(NOTE_SKIN_SORT_ORDER, ",", asSorted);

  std::set<std::string> setUnusedSkinNames(setSkinNames);
  asSkinNames.clear();

  for (const std::string& sSkin : asSorted) {
    if (setSkinNames.find(sSkin) == setSkinNames.end()) {
      continue;
    }
    asSkinNames.push_back(sSkin);
    setUnusedSkinNames.erase(sSkin);
  }

  asSkinNames.insert(
      asSkinNames.end(), setUnusedSkinNames.begin(), setUnusedSkinNames.end());
}

class OptionRowHandlerListNoteSkins : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.name_ = "NoteSkins";
    def_.one_choice_for_all_players_ = false;
    def_.allow_theme_items_ = false;  // we theme the text ourself

    std::vector<std::string> arraySkinNames;
    NOTESKIN->GetNoteSkinNames(arraySkinNames);
    SortNoteSkins(arraySkinNames);

    for (unsigned skin = 0; skin < arraySkinNames.size(); skin++) {
      if (arraySkinNames[skin] ==
          CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue()) {
        def_.default_ = skin;
      }
      GameCommand mc;
      mc.preferred_modifiers_ = arraySkinNames[skin];
      // ms.m_sName = arraySkinNames[skin];
      list_entries_.push_back(mc);
      def_.choices_.push_back(arraySkinNames[skin]);
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

      std::vector<Trail*> vTrails;
      GAMESTATE->cur_course_->GetTrails(
          vTrails,
          GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->steps_type_);
      for (unsigned i = 0; i < vTrails.size(); i++) {
        Trail* pTrail = vTrails[i];

        std::string s =
            CourseDifficultyToLocalizedString(pTrail->course_difficulty_);
        s += ssprintf(" %d", pTrail->GetMeter());
        def_.choices_.push_back(s);
        GameCommand mc;
        mc.trail_ = pTrail;
        list_entries_.push_back(mc);
      }
    } else if (
        GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber()) &&
        GAMESTATE->cur_song_)  // playing a song
    {
      def_.layout_type_ = StringToLayoutType(STEPS_ROW_LAYOUT_TYPE);

      std::vector<Steps*> vpSteps;
      Song* pSong = GAMESTATE->cur_song_;
      SongUtil::GetSteps(
          pSong, vpSteps,
          GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->steps_type_);
      StepsUtil::RemoveLockedSteps(pSong, vpSteps);
      StepsUtil::SortNotesArrayByDifficulty(vpSteps);
      for (unsigned i = 0; i < vpSteps.size(); i++) {
        Steps* pSteps = vpSteps[i];

        std::string s;
        if (STEPS_USE_CHART_NAME) {
          s = pSteps->GetChartName();
        }
        // TODO: find a way to make this use lua or metrics.
        if (s == "" || s == "blank" || s == "Blank") {
          if (pSteps->GetDifficulty() == Difficulty_Edit) {
            s = pSteps->GetChartName();
            if (s == "" || s == "blank" || s == "Blank") {
              s = pSteps->GetDescription();
            }
          } else {
            if (pSteps->IsAnEdit()) {
              s = pSteps->GetChartName();
              if (s == "" || s == "blank" || s == "Blank") {
                s = pSteps->GetDescription();
              }
            } else {
              s = CustomDifficultyToLocalizedString(GetCustomDifficulty(
                  pSteps->steps_type_, pSteps->GetDifficulty(),
                  CourseType_Invalid));
            }
          }
        }
        s += ssprintf(" %d", pSteps->GetMeter());
        def_.choices_.push_back(s);
        GameCommand mc;
        mc.steps_ = pSteps;
        mc.dc_ = pSteps->GetDifficulty();
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
    std::string sParam = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    if (sParam == "EditSteps") {
      steps_to_fill_ = &GAMESTATE->cur_steps_[0];
      difficulty_to_fill_ = &GAMESTATE->preferred_difficulty_[0];
      pst_ = &GAMESTATE->st_edit_;
      reload_row_messages_.push_back(
          MessageIDToString(Message_EditStepsTypeChanged));
    } else if (sParam == "EditSourceSteps") {
      steps_to_fill_ = &GAMESTATE->edit_source_steps_;
      pst_ = &GAMESTATE->edit_source_;
      reload_row_messages_.push_back(
          MessageIDToString(Message_EditSourceStepsTypeChanged));
      if (GAMESTATE->cur_steps_[0].Get() != nullptr) {
        def_.enabled_for_players_.clear();  // hide row
      }
    } else {
      ROW_INVALID_IF(
          true, "Invalid StepsType param \"" + sParam + "\".", false);
    }

    def_.name_ = sParam;
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
        Steps* pSteps =
            SongUtil::GetStepsByDifficulty(GAMESTATE->cur_song_, *pst_, dc);
        steps_.push_back(pSteps);
      }
      SongUtil::GetSteps(GAMESTATE->cur_song_, steps_, *pst_, Difficulty_Edit);
      difficulties_.resize(steps_.size(), Difficulty_Edit);

      if (sParam == "EditSteps") {
        steps_.push_back(nullptr);
        difficulties_.push_back(Difficulty_Edit);
      }

      for (unsigned i = 0; i < steps_.size(); i++) {
        Steps* pSteps = steps_[i];
        Difficulty dc = difficulties_[i];

        std::string s;
        if (dc == Difficulty_Edit) {
          if (pSteps) {
            s = pSteps->GetDescription();
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
      OptionRow* pRow, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> vbSelectedOut[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& vbSelOut = vbSelectedOut[p];

      ASSERT(steps_.size() == vbSelOut.size());

      // look for matching steps
      std::vector<Steps*>::const_iterator iter =
          find(steps_.begin(), steps_.end(), steps_to_fill_->Get());
      if (iter != steps_.end()) {
        unsigned i = iter - steps_.begin();
        vbSelOut[i] = true;
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
            vbSelOut[i] = true;
            matched = true;
            std::vector<PlayerNumber> v;
            v.push_back(p);
            ExportOption(v, vbSelectedOut);  // current steps changed
            break;
          }
        }
      }
      if (!matched) {
        // default to 1st
        vbSelOut[0] = true;
      }
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> vbSelected[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& vbSel = vbSelected[p];

      int index = OptionRowHandlerUtil::GetOneSelection(vbSel);
      Difficulty dc = difficulties_[index];
      Steps* pSteps = steps_[index];
      if (difficulty_to_fill_) {
        difficulty_to_fill_->Set(dc);
      }
      steps_to_fill_->Set(pSteps);
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

    std::vector<Character*> vpCharacters;
    CHARMAN->GetCharacters(vpCharacters);
    for (unsigned i = 0; i < vpCharacters.size(); i++) {
      Character* pCharacter = vpCharacters[i];
      std::string s = pCharacter->GetDisplayName();
      MakeUpper(s);

      def_.choices_.push_back(s);
      GameCommand mc;
      mc.character_ = pCharacter;
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

    std::vector<const Style*> vStyles;
    GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, vStyles);
    ASSERT(vStyles.size() != 0);
    for (const Style* s : vStyles) {
      def_.choices_.push_back(GAMEMAN->StyleToLocalizedString(s));
      GameCommand mc;
      mc.style_ = s;
      list_entries_.push_back(mc);
    }

    default_.style_ = vStyles[0];
    return true;
  }
};

class OptionRowHandlerListGroups : public OptionRowHandlerList {
  virtual bool LoadInternal(const Commands&) {
    def_.one_choice_for_all_players_ = true;
    def_.allow_theme_items_ = false;  // we theme the text ourself
    def_.name_ = "Group";
    default_.song_group_ = GROUP_ALL;

    std::vector<std::string> vSongGroups;
    SONGMAN->GetSongGroupNames(vSongGroups);
    ASSERT(vSongGroups.size() != 0);

    {
      def_.choices_.push_back("AllGroups");
      GameCommand mc;
      mc.song_group_ = GROUP_ALL;
      list_entries_.push_back(mc);
    }

    for (const std::string& g : vSongGroups) {
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
    const std::vector<Song*>& vpSongs =
        SONGMAN->GetSongs(GAMESTATE->preferred_song_group_);

    if (GAMESTATE->cur_song_ == nullptr) {
      GAMESTATE->cur_song_.Set(vpSongs[0]);
    }

    def_.name_ = "SongsInCurrentSongGroup";
    def_.one_choice_for_all_players_ = true;
    def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def_.export_on_change_ = true;

    for (Song* p : vpSongs) {
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

  bool SanityCheckTable(lua_State* L, std::string& RowName) {
    if (lua_table_->GetLuaType() != LUA_TTABLE) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  Result of \"%s\" is not a table.", RowName.c_str());
      return false;
    }
    lua_table_->PushSelf(L);
    lua_getfield(L, -1, "Name");
    const char* pStr = lua_tostring(L, -1);
    if (pStr == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"Name\" entry is not a string.",
          RowName.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "LayoutType");
    pStr = lua_tostring(L, -1);
    if (pStr == nullptr || StringToLayoutType(pStr) == LayoutType_Invalid) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"LayoutType\" entry is not a string.",
          RowName.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "SelectType");
    pStr = lua_tostring(L, -1);
    if (pStr == nullptr || StringToSelectType(pStr) == SelectType_Invalid) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"SelectType\" entry is not a string.",
          RowName.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "Choices");
    if (!lua_istable(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"Choices\" is not a table.", RowName.c_str());
      return false;
    }
    if (!TableContainsOnlyStrings(L, lua_gettop(L))) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"Choices\" table contains a non-string.",
          RowName.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "EnabledForPlayers");
    if (!lua_isnil(L, -1)) {
      if (!lua_isfunction(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"EnabledForPlayers\" is not a function.",
            RowName.c_str());
        return false;
      }
      lua_table_->PushSelf(L);
      std::string error = RowName + " \"EnabledForPlayers\": ";
      LuaHelpers::RunScriptOnStack(L, error, 1, 1, true);
      if (!lua_istable(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"EnabledForPlayers\" did not return a table.",
            RowName.c_str());
        return false;
      }
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        PlayerNumber pn = Enum::Check<PlayerNumber>(L, -1, true, true);
        if (pn == PlayerNumber_Invalid) {
          LuaHelpers::ReportScriptErrorFmt(
              "LUA_ERROR:  \"%s\" \"EnabledForPlayers\" contains a "
              "non-PlayerNumber.",
              RowName.c_str());
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
            RowName.c_str());
        return false;
      }
      if (!TableContainsOnlyStrings(L, lua_gettop(L))) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"ReloadRowMessages\" table contains a "
            "non-string.",
            RowName.c_str());
        return false;
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "Reload");
    if (!lua_isnil(L, -1)) {
      if (!lua_isfunction(L, -1)) {
        LuaHelpers::ReportScriptErrorFmt(
            "LUA_ERROR:  \"%s\" \"Reload\" entry is not a function.",
            RowName.c_str());
        return false;
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "LoadSelections");
    if (!lua_isfunction(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"LoadSelections\" entry is not a function.",
          RowName.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "SaveSelections");
    if (!lua_isfunction(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"SaveSelections\" entry is not a function.",
          RowName.c_str());
      return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "NotifyOfSelection");
    if (!lua_isnil(L, -1) && !lua_isfunction(L, -1)) {
      LuaHelpers::ReportScriptErrorFmt(
          "LUA_ERROR:  \"%s\" \"NotifyOfSelection\" entry is not a function.",
          RowName.c_str());
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
      const char* pValue = lua_tostring(L, -1);
      // LOG->Trace( "choice: '%s'", pValue);
      def_.choices_.push_back(pValue);
      lua_pop(L, 1);  // removes `value'; keeps `key' for next iteration
    }
    lua_pop(L, 1);  // pop choices table
  }

  virtual bool LoadInternal(const Commands& cmds) {
    const Command& command = cmds.v_[0];
    std::string sParam = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    def_.allow_theme_items_ =
        false;  // Lua options are always dynamic and can theme themselves.

    Lua* L = LUA->Get();

    // Run the Lua expression.  It should return a table.
    lua_table_->SetFromExpression(sParam);
    table_is_sane_ = SanityCheckTable(L, sParam);
    if (!table_is_sane_) {
      lua_settop(L, 0);  // Release has an assert that forces a clear stack.
      LUA->Release(L);
      return false;
    }
    lua_table_->PushSelf(L);

    lua_getfield(L, -1, "Name");
    const char* pStr = lua_tostring(L, -1);
    def_.name_ = pStr;
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
    pStr = lua_tostring(L, -1);
    def_.layout_type_ = StringToLayoutType(pStr);
    lua_pop(L, 1);

    lua_getfield(L, -1, "SelectType");
    pStr = lua_tostring(L, -1);
    def_.select_type_ = StringToSelectType(pStr);
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
        const char* pValue = lua_tostring(L, -1);
        // LOG->Trace( "Found ReloadRowMessage '%s'", pValue);
        reload_row_messages_.push_back(pValue);
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
      OptionRow* pRow, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> vbSelectedOut[NUM_PLAYERS]) const {
    if (!table_is_sane_) {
      return;
    }
    Lua* L = LUA->Get();

    ASSERT(lua_gettop(L) == 0);

    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& vbSelOut = vbSelectedOut[p];

      /* Evaluate the LoadSelections(self,array,pn) function, where
       * array is a table representing vbSelectedOut. */

      // All selections default to false.
      for (unsigned i = 0; i < vbSelOut.size(); ++i) {
        vbSelOut[i] = false;
      }

      // Create the vbSelectedOut table
      LuaHelpers::CreateTableFromArrayB(L, vbSelOut);
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

      LuaHelpers::ReadArrayFromTableB(L, vbSelOut);

      lua_pop(L, 1);  // pop vbSelectedOut table

      ASSERT(lua_gettop(L) == 0);
    }

    LUA->Release(L);
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> vbSelected[NUM_PLAYERS]) const {
    if (!table_is_sane_) {
      return 0;
    }
    Lua* L = LUA->Get();

    ASSERT(lua_gettop(L) == 0);

    int effects = 0;
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& vbSel = vbSelected[p];

      /* Evaluate SaveSelections(self,array,pn) function, where array is
       * a table representing vbSelectedOut. */

      std::vector<bool> vbSelectedCopy = vbSel;

      // Create the vbSelectedOut table.
      LuaHelpers::CreateTableFromArrayB(L, vbSelectedCopy);
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
          const char* pValue = lua_tostring(L, -1);
          // LOG->Trace( "choice: '%s'", pValue);
          def_.choices_.push_back(pValue);
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
    std::string sParam = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    Init();

    // Configuration values are never per-player.
    def_.one_choice_for_all_players_ = true;

    ConfOption* pConfOption = ConfOption::Find(sParam);
    ROW_INVALID_IF(
        pConfOption == nullptr, "Invalid Conf type \"" + sParam + "\".", false);

    pConfOption->UpdateAvailableOptions();

    opt_ = pConfOption;
    opt_->MakeOptionsList(def_.choices_);

    def_.allow_theme_items_ = opt_->allow_theme_items_;

    def_.name_ = opt_->name_;
    return true;
  }
  virtual void ImportOption(
      OptionRow*, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> vbSelectedOut[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& vbSelOut = vbSelectedOut[p];

      int iSelection = opt_->Get();
      OptionRowHandlerUtil::SelectExactlyOne(iSelection, vbSelOut);
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> vbSelected[NUM_PLAYERS]) const {
    bool bChanged = false;

    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& vbSel = vbSelected[p];

      int iSel = OptionRowHandlerUtil::GetOneSelection(vbSel);

      // Get the original choice.
      int iOriginal = opt_->Get();

      // Apply.
      opt_->Put(iSel);

      // Get the new choice.
      int iNew = opt_->Get();

      // If it didn't change, don't return any side-effects.
      if (iOriginal != iNew) {
        bChanged = true;
      }
    }

    return bChanged ? opt_->GetEffects() : 0;
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
    std::string sParam = command.GetArg(1).s_;
    CHECK_WRONG_NUM_ARGS(2);
    CHECK_BLANK_ARG;

    if (sParam == "EditStepsType") {
      to_fill_ = &GAMESTATE->st_edit_;
    } else if (sParam == "EditSourceStepsType") {
      to_fill_ = &GAMESTATE->edit_source_;
      reload_row_messages_.push_back(
          MessageIDToString(Message_CurrentStepsP1Changed));
      reload_row_messages_.push_back(
          MessageIDToString(Message_EditStepsTypeChanged));
      if (GAMESTATE->cur_steps_[0].Get() != nullptr) {
        def_.enabled_for_players_.clear();  // hide row
      }
    } else {
      ROW_INVALID_IF(
          true, "Invalid StepsType param \"" + sParam + "\".", false);
    }

    def_.name_ = sParam;
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
      OptionRow* pRow, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> vbSelectedOut[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      std::vector<bool>& vbSelOut = vbSelectedOut[p];

      if (GAMESTATE->cur_steps_[0]) {
        StepsType st = GAMESTATE->cur_steps_[0]->steps_type_;
        std::vector<StepsType>::const_iterator iter =
            find(steps_types_to_show_.begin(), steps_types_to_show_.end(), st);
        if (iter != steps_types_to_show_.end()) {
          unsigned i = iter - steps_types_to_show_.begin();
          vbSelOut[i] = true;
          continue;  // done with this player
        }
      }
      vbSelOut[0] = true;
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> vbSelected[NUM_PLAYERS]) const {
    for (const PlayerNumber& p : vpns) {
      const std::vector<bool>& vbSel = vbSelected[p];

      int index = OptionRowHandlerUtil::GetOneSelection(vbSel);
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
      OptionRow* pRow, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> vbSelectedOut[NUM_PLAYERS]) const {}
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> vbSelected[NUM_PLAYERS]) const {
    if (vbSelected[PLAYER_1][0] || vbSelected[PLAYER_2][0]) {
      gc_.ApplyToAllPlayers();
    }
    return 0;
  }
  virtual void GetIconTextAndGameCommand(
      int iFirstSelection, std::string& sIconTextOut,
      GameCommand& gcOut) const {
    sIconTextOut = "";
    gcOut = gc_;
  }
  virtual std::string GetScreen(int iChoice) const { return gc_.screen_; }
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
    std::string sParam = command.GetArg(1).s_;
    ROW_INVALID_IF(
        command.args_.size() != 2 || !sParam.size(),
        "list row command must be 'list,name' or 'list,type'.", nullptr);

    if (CompareNoCase(sParam, "NoteSkins") == 0) {
      MAKE(OptionRowHandlerListNoteSkins)
    } else if (CompareNoCase(sParam, "Steps") == 0) {
      MAKE(OptionRowHandlerListSteps)
    } else if (CompareNoCase(sParam, "StepsLocked") == 0) {
      MAKE(OptionRowHandlerListSteps);
      pHand->def_.one_choice_for_all_players_ = true;
    } else if (CompareNoCase(sParam, "Characters") == 0) {
      MAKE(OptionRowHandlerListCharacters)
    } else if (CompareNoCase(sParam, "Styles") == 0) {
      MAKE(OptionRowHandlerListStyles)
    } else if (CompareNoCase(sParam, "Groups") == 0) {
      MAKE(OptionRowHandlerListGroups)
    } else if (CompareNoCase(sParam, "Difficulties") == 0) {
      MAKE(OptionRowHandlerListDifficulties)
    } else if (CompareNoCase(sParam, "SongsInCurrentSongGroup") == 0) {
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
  OptionRowHandler* pHand = OptionRowHandlerUtil::MakeNull();

  pHand->def_.name_ = mr.name_;
  FontCharAliases::ReplaceMarkers(
      pHand->def_.name_);  // Allow special characters

  pHand->def_.enabled_for_players_.clear();
  if (mr.pfn_enabled_ ? mr.pfn_enabled_() : mr.b_enabled_) {
    FOREACH_EnabledPlayer(pn) pHand->def_.enabled_for_players_.insert(pn);
  }

  pHand->def_.one_choice_for_all_players_ = true;
  pHand->def_.select_type_ = SELECT_ONE;
  pHand->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
  pHand->def_.export_on_change_ = false;  // true;

  // MISTER CHOICES!
  pHand->def_.choices_ = mr.choices_;

  // Each row must have at least one choice.
  if (pHand->def_.choices_.empty()) {
    pHand->def_.choices_.push_back("");
  }

  pHand->def_.allow_theme_title_ = mr.theme_title_;
  pHand->def_.allow_theme_items_ = mr.theme_items_;

  for (std::string& c : pHand->def_.choices_) {
    FontCharAliases::ReplaceMarkers(c);  // Allow special characters
  }

  return pHand;
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
