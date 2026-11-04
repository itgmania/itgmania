#include "GameCommand.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "AnnouncerManager.h"
#include "Bookkeeper.h"
#include "Command.h"
#include "Difficulty.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "ModsGroup.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"
#include "StepMania.h"
#include "Style.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

static LocalizedString COULD_NOT_LAUNCH_BROWSER(
    "GameCommand", "Could not launch web browser.");

REGISTER_CLASS_TRAITS(GameCommand, new GameCommand(*pCopy));

void GameCommand::Init() {
  apply_commits_screens_ = true;
  name_ = "";
  text_ = "";
  invalid_ = true;
  index_ = -1;
  style_ = nullptr;
  pm_ = PlayMode_Invalid;
  dc_ = Difficulty_Invalid;
  course_difficulty_ = Difficulty_Invalid;
  preferred_modifiers_ = "";
  stage_modifiers_ = "";
  announcer_ = "";
  screen_ = "";
  lua_function_.Unset();
  song_ = nullptr;
  steps_ = nullptr;
  course_ = nullptr;
  trail_ = nullptr;
  character_ = nullptr;
  sort_order_ = SortOrder_Invalid;
  sound_path_ = "";
  screens_to_prepare_.clear();
  weight_pounds_ = -1;
  goal_calories_ = -1;
  goal_type_ = GoalType_Invalid;
  profile_id_ = "";

  insert_credit_ = false;
  clear_credits_ = false;
  stop_music_ = false;
  apply_default_options_ = false;
  activate_premium_free_ = false;
  fade_music_ = false;
  music_fade_out_volume_ = -1.0f;
  music_fade_out_seconds_ = -1.0f;
}

bool GameCommand::DescribesCurrentModeForAllPlayers() const {
  FOREACH_HumanPlayer(pn) if (!DescribesCurrentMode(pn)) return false;

  return true;
}

bool GameCommand::DescribesCurrentMode(PlayerNumber pn) const {
  if (pm_ != PlayMode_Invalid && GAMESTATE->play_mode_ != pm_) {
    return false;
  }
  if (style_ && GAMESTATE->GetCurrentStyle(pn) != style_) {
    return false;
  }
  // HACK: don't compare m_dc if m_pSteps is set.  This causes problems
  // in ScreenSelectOptionsMaster::ImportOptions if m_PreferredDifficulty
  // doesn't match the difficulty of m_pCurSteps.
  if (steps_ == nullptr && dc_ != Difficulty_Invalid) {
    // Why is this checking for all players?
    FOREACH_HumanPlayer(
        human) if (GAMESTATE->preferred_difficulty_[human] != dc_) return false;
  }

  if (announcer_ != "" && announcer_ != ANNOUNCER->GetCurAnnouncerName()) {
    return false;
  }

  if (preferred_modifiers_ != "") {
    PlayerOptions po =
        GAMESTATE->player_state_[pn]->player_options_.GetPreferred();
    SongOptions so = GAMESTATE->song_options_.GetPreferred();
    po.FromString(preferred_modifiers_);
    so.FromString(preferred_modifiers_);

    if (po != GAMESTATE->player_state_[pn]->player_options_.GetPreferred()) {
      return false;
    }
    if (so != GAMESTATE->song_options_.GetPreferred()) {
      return false;
    }
  }
  if (stage_modifiers_ != "") {
    PlayerOptions po = GAMESTATE->player_state_[pn]->player_options_.GetStage();
    SongOptions so = GAMESTATE->song_options_.GetStage();
    po.FromString(stage_modifiers_);
    so.FromString(stage_modifiers_);

    if (po != GAMESTATE->player_state_[pn]->player_options_.GetStage()) {
      return false;
    }
    if (so != GAMESTATE->song_options_.GetStage()) {
      return false;
    }
  }

  if (song_ && GAMESTATE->cur_song_.Get() != song_) {
    return false;
  }
  if (steps_ && GAMESTATE->cur_steps_[pn].Get() != steps_) {
    return false;
  }
  if (character_ && GAMESTATE->cur_characters_[pn] != character_) {
    return false;
  }
  if (course_ && GAMESTATE->cur_course_.Get() != course_) {
    return false;
  }
  if (trail_ && GAMESTATE->cur_trail_[pn].Get() != trail_) {
    return false;
  }
  if (!song_group_.empty() && GAMESTATE->preferred_song_group_ != song_group_) {
    return false;
  }
  if (sort_order_ != SortOrder_Invalid &&
      GAMESTATE->preferred_sort_order_ != sort_order_) {
    return false;
  }
  if (weight_pounds_ != -1 &&
      PROFILEMAN->GetProfile(pn)->weight_pounds_ != weight_pounds_) {
    return false;
  }
  if (goal_calories_ != -1 &&
      PROFILEMAN->GetProfile(pn)->goal_calories_ != goal_calories_) {
    return false;
  }
  if (goal_type_ != GoalType_Invalid &&
      PROFILEMAN->GetProfile(pn)->goal_type_ != goal_type_) {
    return false;
  }
  if (!profile_id_.empty() &&
      ProfileManager::default_local_profile_id_[pn].Get() != profile_id_) {
    return false;
  }

  return true;
}

void GameCommand::Load(int iIndex, const Commands& cmds) {
  index_ = iIndex;
  invalid_ = false;
  commands_ = cmds;

  for (const Command& cmd : cmds.v_) {
    LoadOne(cmd);
  }
}

void GameCommand::LoadOne(const Command& cmd) {
  std::string sName = cmd.GetName();
  if (sName.empty()) {
    return;
  }

  std::string sValue;
  for (unsigned i = 1; i < cmd.args_.size(); ++i) {
    if (i > 1) {
      sValue += ",";
    }
    sValue += cmd.args_[i];
  }

#define MAKE_INVALID(expr)                                                \
  invalid_reason_ = (expr);                                               \
  LuaHelpers::ReportScriptError(invalid_reason_, "INVALID_GAME_COMMAND"); \
  invalid_ = true;

#define CHECK_INVALID_COND(member, value, cond, message) \
  if (cond) {                                            \
    MAKE_INVALID(message);                               \
  } else {                                               \
    member = value;                                      \
  }

#define CHECK_INVALID_VALUE(member, value, invalid_value, value_name) \
  CHECK_INVALID_COND(                                                 \
      member, value, (value == invalid_value),                        \
      ssprintf("Invalid " #value_name " \"%s\".", sValue.c_str()));

  if (sName == "style") {
    const Style* style =
        GAMEMAN->GameAndStringToStyle(GAMESTATE->cur_game_, sValue);
    CHECK_INVALID_VALUE(style_, style, nullptr, style);
  }

  else if (sName == "playmode") {
    PlayMode pm = StringToPlayMode(sValue);
    CHECK_INVALID_VALUE(pm_, pm, PlayMode_Invalid, playmode);
  }

  else if (sName == "difficulty") {
    Difficulty dc = StringToDifficulty(sValue);
    CHECK_INVALID_VALUE(dc_, dc, Difficulty_Invalid, difficulty);
  }

  else if (sName == "announcer") {
    announcer_ = sValue;
  }

  else if (sName == "name") {
    name_ = sValue;
  }

  else if (sName == "text") {
    text_ = sValue;
  }

  else if (sName == "mod") {
    if (preferred_modifiers_ != "") {
      preferred_modifiers_ += ",";
    }
    preferred_modifiers_ += sValue;
  }

  else if (sName == "stagemod") {
    if (stage_modifiers_ != "") {
      stage_modifiers_ += ",";
    }
    stage_modifiers_ += sValue;
  }

  else if (sName == "lua") {
    lua_function_.SetFromExpression(sValue);
    if (lua_function_.IsNil()) {
      MAKE_INVALID(
          "Lua error in game command: \"" + sValue + "\" evaluated to nil");
    }
  }

  else if (sName == "screen") {
    // OptionsList uses the screen command to push onto its stack.
    // OptionsList "screen"s are OptionRow entries in ScreenOptionsMaster.
    // So if the metric exists in ScreenOptionsMaster, consider it valid.
    // Additionally, the screen value can be used to create an OptionRow.
    // When used to create an OptionRow, it pulls a metric from OptionsList.
    // -Kyz

    if (!THEME->HasMetric("ScreenOptionsMaster", sValue)) {
      if (!THEME->HasMetric("OptionsList", "Line" + sValue)) {
        if (!SCREENMAN->IsScreenNameValid(sValue)) {
          MAKE_INVALID(
              "screen arg '" + sValue +
              "' is not a screen name, ScreenOptionsMaster list or OptionsList "
              "entry.");
        }
      }
    }
    if (!invalid_) {
      screen_ = sValue;
    }
  }

  else if (sName == "song") {
    CHECK_INVALID_COND(
        song_, SONGMAN->FindSong(sValue),
        (SONGMAN->FindSong(sValue) == nullptr),
        (ssprintf("Song \"%s\" not found", sValue.c_str())));
  }

  else if (sName == "steps") {
    std::string sSteps = sValue;

    // This must be processed after "song" and "style" commands.
    if (!invalid_) {
      Song* pSong = (song_ != nullptr) ? song_ : GAMESTATE->cur_song_;
      const Style* pStyle =
          style_
              ? style_
              : GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
      if (pSong == nullptr || pStyle == nullptr) {
        MAKE_INVALID("Must set Song and Style to set Steps.");
      } else {
        Difficulty dc = StringToDifficulty(sSteps);
        Steps* st;
        if (dc < Difficulty_Edit) {
          st = SongUtil::GetStepsByDifficulty(pSong, pStyle->steps_type_, dc);
        } else {
          st = SongUtil::GetStepsByDescription(
              pSong, pStyle->steps_type_, sSteps);
        }
        CHECK_INVALID_COND(
            steps_, st, (st == nullptr),
            (ssprintf("Steps \"%s\" not found", sSteps.c_str())));
      }
    }
  }

  else if (sName == "course") {
    CHECK_INVALID_COND(
        course_, SONGMAN->FindCourse("", sValue),
        (SONGMAN->FindCourse("", sValue) == nullptr),
        (ssprintf("Course \"%s\" not found", sValue.c_str())));
  }

  else if (sName == "trail") {
    std::string sTrail = sValue;

    // This must be processed after "course" and "style" commands.
    if (!invalid_) {
      Course* pCourse = (course_ != nullptr) ? course_ : GAMESTATE->cur_course_;
      const Style* pStyle =
          style_
              ? style_
              : GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
      if (pCourse == nullptr || pStyle == nullptr) {
        MAKE_INVALID("Must set Course and Style to set Trail.");
      } else {
        const CourseDifficulty cd = StringToDifficulty(sTrail);
        if (cd == Difficulty_Invalid) {
          MAKE_INVALID(ssprintf("Invalid difficulty '%s'", sTrail.c_str()));
        } else {
          Trail* tr = pCourse->GetTrail(pStyle->steps_type_, cd);
          CHECK_INVALID_COND(
              trail_, tr, (tr == nullptr),
              ("Trail \"" + sTrail + "\" not found."));
        }
      }
    }
  }

  else if (sName == "setenv") {
    if ((cmd.args_.size() - 1) % 2 != 0) {
      MAKE_INVALID("Arguments to setenv game command must be key,value pairs.");
    } else {
      for (size_t i = 1; i < cmd.args_.size(); i += 2) {
        set_env_[cmd.args_[i]] = cmd.args_[i + 1];
      }
    }
  }

  else if (sName == "songgroup") {
    CHECK_INVALID_COND(
        song_group_, sValue, (!SONGMAN->DoesSongGroupExist(sValue)),
        ("Song group \"" + sValue + "\" does not exist."));
  }

  else if (sName == "sort") {
    SortOrder so = StringToSortOrder(sValue);
    CHECK_INVALID_VALUE(sort_order_, so, SortOrder_Invalid, sortorder);
  }

  else if (sName == "weight") {
    weight_pounds_ = StringToInt(sValue);
  }

  else if (sName == "goalcalories") {
    goal_calories_ = StringToInt(sValue);
  }

  else if (sName == "goaltype") {
    GoalType go = StringToGoalType(sValue);
    CHECK_INVALID_VALUE(goal_type_, go, GoalType_Invalid, goaltype);
  }

  else if (sName == "profileid") {
    profile_id_ = sValue;
  }

  else if (sName == "url") {
    // url usage has been deprecated. This block exists for backwards
    // compatibility.
  }

  else if (sName == "sound") {
    sound_path_ = sValue;
  }

  else if (sName == "preparescreen") {
    screens_to_prepare_.push_back(sValue);
  }

  else if (sName == "insertcredit") {
    insert_credit_ = true;
  }

  else if (sName == "clearcredits") {
    clear_credits_ = true;
  }

  else if (sName == "stopmusic") {
    stop_music_ = true;
  }

  else if (sName == "applydefaultoptions") {
    apply_default_options_ = true;
  }

  else if (sName == "premiumfree") {
    activate_premium_free_ = true;
  }

  // sm-ssc additions begin:
  else if (sName == "urlnoexit") {
    // url usage has been deprecated. This block exists for backwards
    // compatibility.
  }

  else if (sName == "setpref") {
    if ((cmd.args_.size() - 1) % 2 != 0) {
      MAKE_INVALID(
          "Arguments to setpref game command must be key,value pairs.");
    } else {
      for (size_t i = 1; i < cmd.args_.size(); i += 2) {
        if (IPreference::GetPreferenceByName(cmd.args_[i]) == nullptr) {
          MAKE_INVALID("Unknown preference \"" + cmd.args_[i] + "\".");
        } else {
          set_pref_[cmd.args_[i]] = cmd.args_[i + 1];
        }
      }
    }
  }

  else if (sName == "fademusic") {
    if (cmd.args_.size() == 3) {
      fade_music_ = true;
      music_fade_out_volume_ = static_cast<float>(atof(cmd.args_[1].c_str()));
      music_fade_out_seconds_ = static_cast<float>(atof(cmd.args_[2].c_str()));
    } else {
      MAKE_INVALID("Wrong number of args to fademusic.");
    }
  }

  else {
    MAKE_INVALID(ssprintf(
        "Command '%s' is not valid.", cmd.GetOriginalCommandString().c_str()));
  }
#undef CHECK_INVALID_VALUE
#undef CHECK_INVALID_COND
#undef MAKE_INVALID
}

int GetNumCreditsPaid() {
  int iNumCreditsPaid = GAMESTATE->GetNumSidesJoined();

  // players other than the first joined for free
  if (GAMESTATE->GetPremium() == Premium_2PlayersFor1Credit) {
    iNumCreditsPaid = std::min(iNumCreditsPaid, 1);
  }

  return iNumCreditsPaid;
}

int GetCreditsRequiredToPlayStyle(const Style* style) {
  // GameState::GetCoinsNeededToJoin returns 0 if the coin mode isn't
  // CoinMode_Pay, which means the theme can't make sure that there are
  // enough credits available.
  // So we have to check the coin mode here
  // and return 0 if the player doesn't have to pay.
  if (GAMESTATE->GetCoinMode() != CoinMode_Pay) {
    return 0;
  }
  if (GAMESTATE->GetPremium() == Premium_2PlayersFor1Credit) {
    return 1;
  }

  switch (style->style_type_) {
    case StyleType_OnePlayerOneSide:
      return 1;
    case StyleType_TwoPlayersSharedSides:
    case StyleType_TwoPlayersTwoSides:
      return 2;
    case StyleType_OnePlayerTwoSides:
      return (GAMESTATE->GetPremium() == Premium_DoubleFor1Credit) ? 1 : 2;
      DEFAULT_FAIL(style->style_type_);
  }
}

static bool AreStyleAndPlayModeCompatible(const Style* style, PlayMode pm) {
  if (style == nullptr || pm == PlayMode_Invalid) {
    return true;
  }

  switch (pm) {
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      // Can't play rave if there isn't enough room for two players.
      // This is correct for dance (ie, no rave for solo and doubles),
      // and should be okay for pump.. not sure about other game types.
      // Techno Motion scales down versus arrows, though, so allow this.
      if (style->cols_per_player_ >= 6 &&
          std::string(GAMESTATE->cur_game_->name_) != "techno") {
        return false;
      }

      // Don't allow battle modes if the style takes both sides.
      if (style->style_type_ == StyleType_OnePlayerTwoSides ||
          style->style_type_ == StyleType_TwoPlayersSharedSides) {
        return false;
      }
    default:
      break;
  }

  return true;
}

bool GameCommand::IsPlayable(std::string* why) const {
  if (invalid_) {
    if (why) {
      *why = invalid_reason_;
    }
    return false;
  }

  if (style_) {
    int iCredits;
    if (GAMESTATE->GetCoinMode() == CoinMode_Pay) {
      iCredits = GAMESTATE->coins_ / PREFSMAN->coins_per_credit_;
    } else {
      iCredits = NUM_PLAYERS;
    }

    const int iNumCreditsPaid = GetNumCreditsPaid();
    const int iNumCreditsRequired = GetCreditsRequiredToPlayStyle(style_);

    /* With PREFSMAN->m_bDelayedCreditsReconcile disabled, enough credits must
     * be paid. (This means that enough sides must be joined.)  Enabled, simply
     * having enough credits lying in the machine is sufficient; we'll deduct
     * the extra in Apply(). */
    int iNumCreditsAvailable = iNumCreditsPaid;
    if (PREFSMAN->delayed_credits_reconcile_) {
      iNumCreditsAvailable += iCredits;
    }

    if (iNumCreditsAvailable < iNumCreditsRequired) {
      if (why) {
        *why = ssprintf(
            "need %i credits, have %i", iNumCreditsRequired,
            iNumCreditsAvailable);
      }
      return false;
    }

    /* If both sides are joined, disallow singles modes, since easy to select
     * them accidentally, instead of versus mode. */
    if (style_->style_type_ == StyleType_OnePlayerOneSide &&
        GAMESTATE->GetNumPlayersEnabled() > 1) {
      if (why) {
        *why = "too many players joined for ONE_PLAYER_ONE_CREDIT";
      }
      return false;
    }
  }

  /* Don't allow a PlayMode that's incompatible with our current Style (if set),
   * and vice versa. */
  if (pm_ != PlayMode_Invalid || style_ != nullptr) {
    const PlayMode pm = (pm_ != PlayMode_Invalid) ? pm_ : GAMESTATE->play_mode_;
    const Style* style =
        (style_ != nullptr)
            ? style_
            : GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
    if (!AreStyleAndPlayModeCompatible(style, pm)) {
      if (why) {
        *why = ssprintf(
            "mode %s is incompatible with style %s",
            PlayModeToString(pm).c_str(), style->name_);
      }

      return false;
    }
  }

  if (!CompareNoCase(screen_, "ScreenEditCoursesMenu")) {
    std::vector<Course*> vCourses;
    SONGMAN->GetAllCourses(vCourses, false);

    if (vCourses.size() == 0) {
      if (why) {
        *why = "No courses are installed";
      }
      return false;
    }
  }

  if ((!CompareNoCase(screen_, "ScreenJukeboxMenu") ||
       !CompareNoCase(screen_, "ScreenEditMenu") ||
       !CompareNoCase(screen_, "ScreenEditCoursesMenu"))) {
    if (SONGMAN->GetNumSongs() == 0) {
      if (why) {
        *why = "No songs are installed";
      }
      return false;
    }
  }

  if (!preferred_modifiers_.empty()) {
    // TODO: Split this and check each modifier individually
    if (UNLOCKMAN->ModifierIsLocked(preferred_modifiers_)) {
      if (why) {
        *why = "Modifier is locked";
      }
      return false;
    }
  }

  if (!stage_modifiers_.empty()) {
    // TODO: Split this and check each modifier individually
    if (UNLOCKMAN->ModifierIsLocked(stage_modifiers_)) {
      if (why) {
        *why = "Modifier is locked";
      }
      return false;
    }
  }

  return true;
}

void GameCommand::ApplyToAllPlayers() const {
  std::vector<PlayerNumber> vpns;

  FOREACH_PlayerNumber(pn) vpns.push_back(pn);

  Apply(vpns);
}

void GameCommand::Apply(PlayerNumber pn) const {
  std::vector<PlayerNumber> vpns;
  vpns.push_back(pn);
  Apply(vpns);
}

void GameCommand::Apply(const std::vector<PlayerNumber>& vpns) const {
  if (commands_.v_.size()) {
    // We were filled using a GameCommand from metrics. Apply the options in
    // order.
    for (const Command& cmd : commands_.v_) {
      GameCommand gc;
      gc.invalid_ = false;
      gc.apply_commits_screens_ = apply_commits_screens_;
      gc.LoadOne(cmd);
      gc.ApplySelf(vpns);
    }
  } else {
    // We were filled by an OptionRowHandler in code. m_Commands isn't filled,
    // so just apply the values that are already set in this.
    this->ApplySelf(vpns);
  }
}

void GameCommand::ApplySelf(const std::vector<PlayerNumber>& vpns) const {
  const PlayMode OldPlayMode = GAMESTATE->play_mode_;

  if (pm_ != PlayMode_Invalid) {
    GAMESTATE->play_mode_.Set(pm_);
  }

  if (style_ != nullptr) {
    GAMESTATE->SetCurrentStyle(style_, GAMESTATE->GetMasterPlayerNumber());

    // It's possible to choose a style that didn't have enough players joined.
    // If enough players aren't joined, then  we need to subtract credits
    // for the sides that will be joined as a result of applying this option.
    if (GAMESTATE->GetCoinMode() == CoinMode_Pay) {
      int iNumCreditsRequired = GetCreditsRequiredToPlayStyle(style_);
      int iNumCreditsPaid = GetNumCreditsPaid();
      int iNumCreditsOwed = iNumCreditsRequired - iNumCreditsPaid;
      GAMESTATE->coins_.Set(
          GAMESTATE->coins_ - iNumCreditsOwed * PREFSMAN->coins_per_credit_);
      LOG->Trace(
          "Deducted %i coins, %i remaining",
          iNumCreditsOwed * PREFSMAN->coins_per_credit_,
          GAMESTATE->coins_.Get());

      // Credit Used, make sure to update CoinsFile
      BOOKKEEPER->WriteCoinsFile(GAMESTATE->coins_.Get());
    }

    // If only one side is joined and we picked a style that requires both
    // sides, join the other side.
    switch (style_->style_type_) {
      case StyleType_OnePlayerOneSide:
      case StyleType_OnePlayerTwoSides:
        break;
      case StyleType_TwoPlayersTwoSides:
      case StyleType_TwoPlayersSharedSides: {
        FOREACH_PlayerNumber(p) GAMESTATE->JoinPlayer(p);
      } break;
      default:
        LuaHelpers::ReportScriptErrorFmt(
            "Invalid StyleType: %d", style_->style_type_);
    }
  }
  if (dc_ != Difficulty_Invalid) {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->preferred_difficulty_[pn].Set(dc_);
    }
  }
  if (announcer_ != "") {
    ANNOUNCER->SwitchAnnouncer(announcer_);
  }
  if (preferred_modifiers_ != "") {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->ApplyPreferredModifiers(pn, preferred_modifiers_);
    }
  }
  if (stage_modifiers_ != "") {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->ApplyStageModifiers(pn, stage_modifiers_);
    }
  }
  if (lua_function_.IsSet() && !lua_function_.IsNil()) {
    Lua* L = LUA->Get();
    for (const PlayerNumber& pn : vpns) {
      lua_function_.PushSelf(L);
      ASSERT(!lua_isnil(L, -1));

      lua_pushnumber(L, pn);  // 1st parameter
      std::string error = "Lua GameCommand error: ";
      LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
    }
    LUA->Release(L);
  }
  if (screen_ != "" && apply_commits_screens_) {
    SCREENMAN->SetNewScreen(screen_);
  }
  if (song_) {
    GAMESTATE->cur_song_.Set(song_);
    GAMESTATE->preferred_song_ = song_;
  }
  if (steps_) {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->cur_steps_[pn].Set(steps_);
    }
  }
  if (course_) {
    GAMESTATE->cur_course_.Set(course_);
    GAMESTATE->preferred_course_ = course_;
  }
  if (trail_) {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->cur_trail_[pn].Set(trail_);
    }
  }
  if (course_difficulty_ != Difficulty_Invalid) {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->ChangePreferredCourseDifficulty(pn, course_difficulty_);
    }
  }
  if (character_) {
    for (const PlayerNumber& pn : vpns) {
      GAMESTATE->cur_characters_[pn] = character_;
    }
  }
  for (std::map<std::string, std::string>::const_iterator i = set_env_.begin();
       i != set_env_.end(); i++) {
    Lua* L = LUA->Get();
    GAMESTATE->environment_->PushSelf(L);
    lua_pushstring(L, i->first.c_str());
    lua_pushstring(L, i->second.c_str());
    lua_settable(L, -3);
    lua_pop(L, 1);
    LUA->Release(L);
  }
  for (std::map<std::string, std::string>::const_iterator setting =
           set_pref_.begin();
       setting != set_pref_.end(); ++setting) {
    IPreference* pref = IPreference::GetPreferenceByName(setting->first);
    if (pref != nullptr) {
      pref->FromString(setting->second);
    }
  }
  if (!song_group_.empty()) {
    GAMESTATE->preferred_song_group_.Set(song_group_);
  }
  if (sort_order_ != SortOrder_Invalid) {
    GAMESTATE->preferred_sort_order_ = sort_order_;
  }
  if (sound_path_ != "") {
    SOUND->PlayOnce(THEME->GetPathS("", sound_path_));
  }
  if (weight_pounds_ != -1) {
    for (const PlayerNumber& pn : vpns) {
      PROFILEMAN->GetProfile(pn)->weight_pounds_ = weight_pounds_;
    }
  }
  if (goal_calories_ != -1) {
    for (const PlayerNumber& pn : vpns) {
      PROFILEMAN->GetProfile(pn)->goal_calories_ = goal_calories_;
    }
  }
  if (goal_type_ != GoalType_Invalid) {
    for (const PlayerNumber& pn : vpns) {
      PROFILEMAN->GetProfile(pn)->goal_type_ = goal_type_;
    }
  }
  if (!profile_id_.empty()) {
    for (const PlayerNumber& pn : vpns) {
      ProfileManager::default_local_profile_id_[pn].Set(profile_id_);
    }
  }

  /* If we're going to stop music, do so before preparing new screens, so we
   * don't stop music between preparing screens and loading screens. */
  if (stop_music_) {
    SOUND->StopMusic();
  }
  if (fade_music_) {
    SOUND->DimMusic(music_fade_out_volume_, music_fade_out_seconds_);
  }

  for (const std::string& s : screens_to_prepare_) {
    SCREENMAN->PrepareScreen(s);
  }

  if (insert_credit_) {
    StepMania::InsertCredit();
  }

  if (clear_credits_) {
    StepMania::ClearCredits();
  }

  if (apply_default_options_) {
    // applying options affects only the current stage
    FOREACH_PlayerNumber(p) {
      PlayerOptions po;
      GAMESTATE->GetDefaultPlayerOptions(po);
      GAMESTATE->player_state_[p]->player_options_.Assign(ModsLevel_Stage, po);
    }

    SongOptions so;
    GAMESTATE->GetDefaultSongOptions(so);
    GAMESTATE->song_options_.Assign(ModsLevel_Stage, so);
  }

  if (activate_premium_free_) {
    GAMESTATE->ActivatePremiumFree();
  }
  // HACK: Set life type to BATTERY just once here so it happens once and
  // we don't override the user's changes if they back out.
  FOREACH_PlayerNumber(pn) {
    if (GAMESTATE->play_mode_ == PLAY_MODE_ONI &&
        GAMESTATE->play_mode_ != OldPlayMode &&
        GAMESTATE->player_state_[pn]->player_options_.GetStage().life_type_ ==
            LifeType_Bar) {
      PO_GROUP_ASSIGN(
          GAMESTATE->player_state_[pn]->player_options_, ModsLevel_Stage,
          life_type_, LifeType_Battery);
    }
  }
}

bool GameCommand::IsZero() const {
  if (pm_ != PlayMode_Invalid || style_ != nullptr ||
      dc_ != Difficulty_Invalid || announcer_ != "" ||
      preferred_modifiers_ != "" || stage_modifiers_ != "" ||
      song_ != nullptr || steps_ != nullptr || course_ != nullptr ||
      trail_ != nullptr || character_ != nullptr ||
      course_difficulty_ != Difficulty_Invalid || !song_group_.empty() ||
      sort_order_ != SortOrder_Invalid || weight_pounds_ != -1 ||
      goal_calories_ != -1 || goal_type_ != GoalType_Invalid ||
      !profile_id_.empty() || activate_premium_free_) {
    return false;
  }

  return true;
}

// lua start
#include "Character.h"
#include "Game.h"
#include "LuaBinding.h"
#include "Steps.h"

/** @brief Allow Lua to have access to the GameCommand. */
class LunaGameCommand : public Luna<GameCommand> {
 public:
  static int GetName(T* p, lua_State* L) {
    lua_pushstring(L, p->name_.c_str());
    return 1;
  }
  static int GetText(T* p, lua_State* L) {
    lua_pushstring(L, p->text_.c_str());
    return 1;
  }
  static int GetIndex(T* p, lua_State* L) {
    lua_pushnumber(L, p->index_);
    return 1;
  }
  static int GetStyle(T* p, lua_State* L) {
    if (p->style_ == nullptr) {
      lua_pushnil(L);
    } else {
      Style* pStyle = (Style*)p->style_;
      pStyle->PushSelf(L);
    }
    return 1;
  }
  static int GetScreen(T* p, lua_State* L) {
    lua_pushstring(L, p->screen_.c_str());
    return 1;
  }
  static int GetProfileID(T* p, lua_State* L) {
    lua_pushstring(L, p->profile_id_.c_str());
    return 1;
  }
  static int GetSong(T* p, lua_State* L) {
    if (p->song_ == nullptr) {
      lua_pushnil(L);
    } else {
      p->song_->PushSelf(L);
    }
    return 1;
  }
  static int GetSteps(T* p, lua_State* L) {
    if (p->steps_ == nullptr) {
      lua_pushnil(L);
    } else {
      p->steps_->PushSelf(L);
    }
    return 1;
  }
  static int GetCourse(T* p, lua_State* L) {
    if (p->course_ == nullptr) {
      lua_pushnil(L);
    } else {
      p->course_->PushSelf(L);
    }
    return 1;
  }
  static int GetTrail(T* p, lua_State* L) {
    if (p->trail_ == nullptr) {
      lua_pushnil(L);
    } else {
      p->trail_->PushSelf(L);
    }
    return 1;
  }
  static int GetCharacter(T* p, lua_State* L) {
    if (p->character_ == nullptr) {
      lua_pushnil(L);
    } else {
      p->character_->PushSelf(L);
    }
    return 1;
  }
  static int GetSongGroup(T* p, lua_State* L) {
    lua_pushstring(L, p->song_group_.c_str());
    return 1;
  }
  static int GetUrl(T* p, lua_State* L) {
    LOG->Warn("GetUrl usage has been deprecated.");
    return 1;
  }
  static int GetAnnouncer(T* p, lua_State* L) {
    lua_pushstring(L, p->announcer_.c_str());
    return 1;
  }
  static int GetPreferredModifiers(T* p, lua_State* L) {
    lua_pushstring(L, p->preferred_modifiers_.c_str());
    return 1;
  }
  static int GetStageModifiers(T* p, lua_State* L) {
    lua_pushstring(L, p->stage_modifiers_.c_str());
    return 1;
  }

  DEFINE_METHOD(GetCourseDifficulty, course_difficulty_)
  DEFINE_METHOD(GetDifficulty, dc_)
  DEFINE_METHOD(GetPlayMode, pm_)
  DEFINE_METHOD(GetSortOrder, sort_order_)

  LunaGameCommand() {
    ADD_METHOD(GetName);
    ADD_METHOD(GetText);
    ADD_METHOD(GetIndex);
    ADD_METHOD(GetStyle);
    ADD_METHOD(GetDifficulty);
    ADD_METHOD(GetCourseDifficulty);
    ADD_METHOD(GetScreen);
    ADD_METHOD(GetPlayMode);
    ADD_METHOD(GetProfileID);
    ADD_METHOD(GetSong);
    ADD_METHOD(GetSteps);
    ADD_METHOD(GetCourse);
    ADD_METHOD(GetTrail);
    ADD_METHOD(GetCharacter);
    ADD_METHOD(GetSongGroup);
    ADD_METHOD(GetSortOrder);
    ADD_METHOD(GetUrl);
    ADD_METHOD(GetAnnouncer);
    ADD_METHOD(GetPreferredModifiers);
    ADD_METHOD(GetStageModifiers);
  }
};

LUA_REGISTER_CLASS(GameCommand)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
