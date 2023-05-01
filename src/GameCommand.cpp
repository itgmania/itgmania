#include "global.h"

#include "GameCommand.h"

#include <cstddef>
#include <vector>

#include "AnnouncerManager.h"
#include "Bookkeeper.h"
#include "Game.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ScreenPrompt.h"
#include "Song.h"
#include "SongManager.h"
#include "StepMania.h"
#include "Style.h"
#include "UnlockManager.h"
#include "arch/ArchHooks/ArchHooks.h"

static LocalizedString COULD_NOT_LAUNCH_BROWSER(
    "GameCommand", "Could not launch web browser.");

REGISTER_CLASS_TRAITS(GameCommand, new GameCommand(*pCopy));

void GameCommand::Init() {
  apply_commit_screens_ = true;
  name_ = "";
  text_ = "";
  is_invalid_ = true;
  index_ = -1;
  multiplayer_ = MultiPlayer_Invalid;
  style_ = nullptr;
  play_mode_ = PlayMode_Invalid;
  difficulty_ = Difficulty_Invalid;
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
  url_ = "";
  url_exists_ = true;

  insert_credits_ = false;
  clear_credits_ = false;
  stop_music_ = false;
  apply_default_options_ = false;
  fade_music_ = false;
  music_fade_out_volume_ = -1.0f;
  music_fade_out_seconds_ = -1.0f;
}

bool GameCommand::DescribesCurrentModeForAllPlayers() const {
  FOREACH_HumanPlayer(pn) if (!DescribesCurrentMode(pn)) return false;

  return true;
}

bool GameCommand::DescribesCurrentMode(PlayerNumber pn) const {
  if (play_mode_ != PlayMode_Invalid && GAMESTATE->m_PlayMode != play_mode_) {
    return false;
  }
  if (style_ && GAMESTATE->GetCurrentStyle(pn) != style_) {
    return false;
  }
  // HACK: don't compare difficulty_ if steps_ is set. This causes problems in
  // ScreenSelectOptionsMaster::ImportOptions if m_PreferredDifficulty doesn't
  // match the difficulty of m_pCurSteps.
  if (steps_ == nullptr && difficulty_ != Difficulty_Invalid) {
    // TODO: Why is this checking for all players?
    FOREACH_HumanPlayer(human) if (
        GAMESTATE->m_PreferredDifficulty[human] != difficulty_) return false;
  }

  if (announcer_ != "" && announcer_ != ANNOUNCER->GetCurAnnouncerName()) {
    return false;
  }

  if (preferred_modifiers_ != "") {
    PlayerOptions player_options =
        GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions.GetPreferred();
    SongOptions song_options = GAMESTATE->m_SongOptions.GetPreferred();
    player_options.FromString(preferred_modifiers_);
    song_options.FromString(preferred_modifiers_);

    if (player_options !=
        GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions.GetPreferred()) {
      return false;
    }
    if (song_options != GAMESTATE->m_SongOptions.GetPreferred()) {
      return false;
    }
  }
  if (stage_modifiers_ != "") {
    PlayerOptions player_options =
        GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions.GetStage();
    SongOptions song_options = GAMESTATE->m_SongOptions.GetStage();
    player_options.FromString(stage_modifiers_);
    song_options.FromString(stage_modifiers_);

    if (player_options !=
        GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions.GetStage()) {
      return false;
    }
    if (song_options != GAMESTATE->m_SongOptions.GetStage()) {
      return false;
    }
  }

  if (song_ && GAMESTATE->m_pCurSong.Get() != song_) {
    return false;
  }
  if (steps_ && GAMESTATE->m_pCurSteps[pn].Get() != steps_) {
    return false;
  }
  if (character_ && GAMESTATE->m_pCurCharacters[pn] != character_) {
    return false;
  }
  if (course_ && GAMESTATE->m_pCurCourse.Get() != course_) {
    return false;
  }
  if (trail_ && GAMESTATE->m_pCurTrail[pn].Get() != trail_) {
    return false;
  }
  if (!song_group_.empty() && GAMESTATE->m_sPreferredSongGroup != song_group_) {
    return false;
  }
  if (sort_order_ != SortOrder_Invalid &&
      GAMESTATE->m_PreferredSortOrder != sort_order_) {
    return false;
  }
  if (weight_pounds_ != -1 &&
      PROFILEMAN->GetProfile(pn)->m_iWeightPounds != weight_pounds_) {
    return false;
  }
  if (goal_calories_ != -1 &&
      PROFILEMAN->GetProfile(pn)->m_iGoalCalories != goal_calories_) {
    return false;
  }
  if (goal_type_ != GoalType_Invalid &&
      PROFILEMAN->GetProfile(pn)->m_GoalType != goal_type_) {
    return false;
  }
  if (!profile_id_.empty() &&
      ProfileManager::m_sDefaultLocalProfileID[pn].Get() != profile_id_) {
    return false;
  }

  return true;
}

void GameCommand::Load(int index, const Commands& cmds) {
  index_ = index;
  is_invalid_ = false;
  commands_ = cmds;

  for (const Command& cmd : cmds.v) {
    LoadOne(cmd);
  }
}

void GameCommand::LoadOne(const Command& cmd) {
  RString cmd_name = cmd.GetName();
  if (cmd_name.empty()) {
    return;
  }

  RString cmd_value;
  for (unsigned i = 1; i < cmd.args_.size(); ++i) {
    if (i > 1) {
      cmd_value += ",";
    }
    cmd_value += cmd.args_[i];
  }

#define MAKE_INVALID(expr)                                                \
  invalid_reason_ = (expr);                                               \
  LuaHelpers::ReportScriptError(invalid_reason_, "INVALID_GAME_COMMAND"); \
  is_invalid_ = true;

#define CHECK_INVALID_COND(member, value, cond, message) \
  if (cond) {                                            \
    MAKE_INVALID(message);                               \
  } else {                                               \
    member = value;                                      \
  }

#define CHECK_INVALID_VALUE(member, value, invalid_value, value_name) \
  CHECK_INVALID_COND(                                                 \
      member, value, (value == invalid_value),                        \
      ssprintf("Invalid " #value_name " \"%s\".", cmd_value.c_str()));

  if (cmd_name == "style") {
    const Style* style =
        GAMEMAN->GameAndStringToStyle(GAMESTATE->m_pCurGame, cmd_value);
    CHECK_INVALID_VALUE(style_, style, nullptr, style);
  }
  else if (cmd_name == "playmode") {
    PlayMode play_mode = StringToPlayMode(cmd_value);
    CHECK_INVALID_VALUE(play_mode_, play_mode, PlayMode_Invalid, playmode);
  }
  else if (cmd_name == "difficulty") {
    Difficulty difficulty = StringToDifficulty(cmd_value);
    CHECK_INVALID_VALUE(
        difficulty_, difficulty, Difficulty_Invalid, difficulty);
  }
  else if (cmd_name == "announcer") {
    announcer_ = cmd_value;
  }
  else if (cmd_name == "name") {
    name_ = cmd_value;
  }
  else if (cmd_name == "text") {
    text_ = cmd_value;
  }
  else if (cmd_name == "mod") {
    if (preferred_modifiers_ != "") {
      preferred_modifiers_ += ",";
    }
    preferred_modifiers_ += cmd_value;
  }
  else if (cmd_name == "stagemod") {
    if (stage_modifiers_ != "") {
      stage_modifiers_ += ",";
    }
    stage_modifiers_ += cmd_value;
  }
  else if (cmd_name == "lua") {
    lua_function_.SetFromExpression(cmd_value);
    if (lua_function_.IsNil()) {
      MAKE_INVALID(
          "Lua error in game command: \"" + cmd_value + "\" evaluated to nil");
    }
  }
  else if (cmd_name == "screen") {
    // NOTE(kyz): OptionsList uses the screen command to push onto its stack.
    // OptionsList "screen"s are OptionRow entries in ScreenOptionsMaster.
    // So if the metric exists in ScreenOptionsMaster, consider it valid.
    // Additionally, the screen value can be used to create an OptionRow.
    // When used to create an OptionRow, it pulls a metric from OptionsList.
    if (!THEME->HasMetric("ScreenOptionsMaster", cmd_value)) {
      if (!THEME->HasMetric("OptionsList", "Line" + cmd_value)) {
        if (!SCREENMAN->IsScreenNameValid(cmd_value)) {
          MAKE_INVALID(
              "screen arg '" + cmd_value +
              "' is not a screen name, ScreenOptionsMaster list or OptionsList "
              "entry.");
        }
      }
    }
    if (!is_invalid_) {
      screen_ = cmd_value;
    }
  }
  else if (cmd_name == "song") {
    CHECK_INVALID_COND(
        song_, SONGMAN->FindSong(cmd_value),
        (SONGMAN->FindSong(cmd_value) == nullptr),
        (ssprintf("Song \"%s\" not found", cmd_value.c_str())));
  }
  else if (cmd_name == "steps") {
    RString steps = cmd_value;

    // This must be processed after "song" and "style" commands.
    if (!is_invalid_) {
      Song* song = (song_ != nullptr) ? song_ : GAMESTATE->m_pCurSong;
      const Style* style =
          style_
              ? style_
              : GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
      if (song == nullptr || style == nullptr) {
        MAKE_INVALID("Must set Song and Style to set Steps.");
      } else {
        Difficulty dc = StringToDifficulty(steps);
        Steps* st;
        if (dc < Difficulty_Edit) {
          st = SongUtil::GetStepsByDifficulty(song, style->m_StepsType, dc);
        } else {
          st = SongUtil::GetStepsByDescription(
              song, style->m_StepsType, steps);
        }
        CHECK_INVALID_COND(
            steps_, st, (st == nullptr),
            (ssprintf("Steps \"%s\" not found", steps.c_str())));
      }
    }
  }
  else if (cmd_name == "course") {
    CHECK_INVALID_COND(
        course_, SONGMAN->FindCourse("", cmd_value),
        (SONGMAN->FindCourse("", cmd_value) == nullptr),
        (ssprintf("Course \"%s\" not found", cmd_value.c_str())));
  }
  else if (cmd_name == "trail") {
    RString trail_str = cmd_value;

    // This must be processed after "course" and "style" commands.
    if (!is_invalid_) {
      Course* course =
          (course_ != nullptr) ? course_ : GAMESTATE->m_pCurCourse;
      const Style* style =
          style_
              ? style_
              : GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
      if (course == nullptr || style == nullptr) {
        MAKE_INVALID("Must set Course and Style to set Trail.");
      } else {
        const CourseDifficulty course_difficulty =
						StringToDifficulty(trail_str);
        if (course_difficulty == Difficulty_Invalid) {
          MAKE_INVALID(ssprintf("Invalid difficulty '%s'", trail_str.c_str()));
        } else {
          Trail* trail =
							course->GetTrail(style->m_StepsType, course_difficulty);
          CHECK_INVALID_COND(
              trail_, trail, (trail == nullptr),
              ("Trail \"" + trail_str + "\" not found."));
        }
      }
    }
  }
  else if (cmd_name == "setenv") {
    if ((cmd.args_.size() - 1) % 2 != 0) {
      MAKE_INVALID("Arguments to setenv game command must be key,value pairs.");
    } else {
      for (std::size_t i = 1; i < cmd.args_.size(); i += 2) {
        set_env_[cmd.args_[i]] = cmd.args_[i + 1];
      }
    }
  }
  else if (cmd_name == "songgroup") {
    CHECK_INVALID_COND(
        song_group_, cmd_value, (!SONGMAN->DoesSongGroupExist(cmd_value)),
        ("Song group \"" + cmd_value + "\" does not exist."));
  }
  else if (cmd_name == "sort") {
    SortOrder sort_order = StringToSortOrder(cmd_value);
    CHECK_INVALID_VALUE(sort_order_, sort_order, SortOrder_Invalid, sortorder);
  }
  else if (cmd_name == "weight") {
    weight_pounds_ = StringToInt(cmd_value);
  }
  else if (cmd_name == "goalcalories") {
    goal_calories_ = StringToInt(cmd_value);
  }
  else if (cmd_name == "goaltype") {
    GoalType goal_type = StringToGoalType(cmd_value);
    CHECK_INVALID_VALUE(goal_type_, goal_type, GoalType_Invalid, goaltype);
  }
  else if (cmd_name == "profileid") {
    profile_id_ = cmd_value;
  }
  else if (cmd_name == "url") {
    url_ = cmd_value;
    url_exists_ = true;
  }
  else if (cmd_name == "sound") {
    sound_path_ = cmd_value;
  }
  else if (cmd_name == "preparescreen") {
    screens_to_prepare_.push_back(cmd_value);
  }
  else if (cmd_name == "insertcredit") {
    insert_credits_ = true;
  }
  else if (cmd_name == "clearcredits") {
    clear_credits_ = true;
  }
  else if (cmd_name == "stopmusic") {
    stop_music_ = true;
  }
  else if (cmd_name == "applydefaultoptions") {
    apply_default_options_ = true;
  }
  // sm-ssc additions begin:
  else if (cmd_name == "urlnoexit") {
    url_ = cmd_value;
    url_exists_ = false;
  }
  else if (cmd_name == "setpref") {
    if ((cmd.args_.size() - 1) % 2 != 0) {
      MAKE_INVALID(
          "Arguments to setpref game command must be key,value pairs.");
    } else {
      for (std::size_t i = 1; i < cmd.args_.size(); i += 2) {
        if (IPreference::GetPreferenceByName(cmd.args_[i]) == nullptr) {
          MAKE_INVALID("Unknown preference \"" + cmd.args_[i] + "\".");
        } else {
          set_pref_[cmd.args_[i]] = cmd.args_[i + 1];
        }
      }
    }
  }
  else if (cmd_name == "fademusic") {
    if (cmd.args_.size() == 3) {
      fade_music_ = true;
      music_fade_out_volume_ = static_cast<float>(atof(cmd.args_[1]));
      music_fade_out_seconds_ = static_cast<float>(atof(cmd.args_[2]));
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
  int num_credits_paid = GAMESTATE->GetNumSidesJoined();

  // Players other than the first joined for free.
  if (GAMESTATE->GetPremium() == Premium_2PlayersFor1Credit) {
    num_credits_paid = std::min(num_credits_paid, 1);
  }

  return num_credits_paid;
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

  switch (style->m_StyleType) {
    case StyleType_OnePlayerOneSide:
      return 1;
    case StyleType_TwoPlayersSharedSides:
    case StyleType_TwoPlayersTwoSides:
      return 2;
    case StyleType_OnePlayerTwoSides:
      return (GAMESTATE->GetPremium() == Premium_DoubleFor1Credit) ? 1 : 2;
      DEFAULT_FAIL(style->m_StyleType);
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
      if (style->m_iColsPerPlayer >= 6 &&
          RString(GAMESTATE->m_pCurGame->name) != "techno") {
        return false;
      }

      // Don't allow battle modes if the style takes both sides.
      if (style->m_StyleType == StyleType_OnePlayerTwoSides ||
          style->m_StyleType == StyleType_TwoPlayersSharedSides) {
        return false;
      }
    default:
      break;
  }

  return true;
}

bool GameCommand::IsPlayable(RString* invalid_reason) const {
  if (is_invalid_) {
    if (invalid_reason) {
      *invalid_reason = invalid_reason_;
    }
    return false;
  }

  if (style_) {
    int credits;
    if (GAMESTATE->GetCoinMode() == CoinMode_Pay) {
      credits = GAMESTATE->m_iCoins / PREFSMAN->m_iCoinsPerCredit;
    } else {
      credits = NUM_PLAYERS;
    }

    const int num_credits_paid = GetNumCreditsPaid();
    const int num_credits_required = GetCreditsRequiredToPlayStyle(style_);

    // With PREFSMAN->m_bDelayedCreditsReconcile disabled, enough credits must
    // be paid. (This means that enough sides must be joined.)  Enabled, simply
    // having enough credits lying in the machine is sufficient; we'll deduct
    // the extra in Apply().
    int iNumCreditsAvailable = num_credits_paid;
    if (PREFSMAN->m_bDelayedCreditsReconcile) {
      iNumCreditsAvailable += credits;
    }

    if (iNumCreditsAvailable < num_credits_required) {
      if (invalid_reason) {
        *invalid_reason = ssprintf(
            "need %i credits, have %i", num_credits_required,
            iNumCreditsAvailable);
      }
      return false;
    }

    // If both sides are joined, disallow singles modes, since easy to select
    // them accidentally, instead of versus mode.
    if (style_->m_StyleType == StyleType_OnePlayerOneSide &&
        GAMESTATE->GetNumPlayersEnabled() > 1) {
      if (invalid_reason) {
        *invalid_reason = "too many players joined for ONE_PLAYER_ONE_CREDIT";
      }
      return false;
    }
  }

  // Don't allow a PlayMode that's incompatible with our current Style (if set),
  // and vice versa.
  if (play_mode_ != PlayMode_Invalid || style_ != nullptr) {
    const PlayMode pm =
        (play_mode_ != PlayMode_Invalid) ? play_mode_ : GAMESTATE->m_PlayMode;
    const Style* style =
        (style_ != nullptr)
            ? style_
            : GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
    if (!AreStyleAndPlayModeCompatible(style, pm)) {
      if (invalid_reason) {
        *invalid_reason = ssprintf(
            "mode %s is incompatible with style %s",
            PlayModeToString(pm).c_str(), style->m_szName);
      }

      return false;
    }
  }

  if (!screen_.CompareNoCase("ScreenEditCoursesMenu")) {
    std::vector<Course*> courses;
    SONGMAN->GetAllCourses(courses, false);

    if (courses.size() == 0) {
      if (invalid_reason) {
        *invalid_reason = "No courses are installed";
      }
      return false;
    }
  }

  if ((!screen_.CompareNoCase("ScreenJukeboxMenu") ||
       !screen_.CompareNoCase("ScreenEditMenu") ||
       !screen_.CompareNoCase("ScreenEditCoursesMenu"))) {
    if (SONGMAN->GetNumSongs() == 0) {
      if (invalid_reason) {
        *invalid_reason = "No songs are installed";
      }
      return false;
    }
  }

  if (!preferred_modifiers_.empty()) {
    // TODO: Split this and check each modifier individually
    if (UNLOCKMAN->ModifierIsLocked(preferred_modifiers_)) {
      if (invalid_reason) {
        *invalid_reason = "Modifier is locked";
      }
      return false;
    }
  }

  if (!stage_modifiers_.empty()) {
    // TODO: Split this and check each modifier individually
    if (UNLOCKMAN->ModifierIsLocked(stage_modifiers_)) {
      if (invalid_reason) {
        *invalid_reason = "Modifier is locked";
      }
      return false;
    }
  }

  return true;
}

void GameCommand::ApplyToAllPlayers() const {
  std::vector<PlayerNumber> pns;

  FOREACH_PlayerNumber(pn) pns.push_back(pn);

  Apply(pns);
}

void GameCommand::Apply(PlayerNumber pn) const {
  std::vector<PlayerNumber> pns;
  pns.push_back(pn);
  Apply(pns);
}

void GameCommand::Apply(const std::vector<PlayerNumber>& pns) const {
  if (commands_.v.size()) {
    // We were filled using a GameCommand from metrics. Apply the options in
    // order.
    for (const Command& cmd : commands_.v) {
      GameCommand gc;
      gc.is_invalid_ = false;
      gc.apply_commit_screens_ = apply_commit_screens_;
      gc.LoadOne(cmd);
      gc.ApplySelf(pns);
    }
  } else {
    // We were filled by an OptionRowHandler in code. m_Commands isn't filled,
    // so just apply the values that are already set in this.
    this->ApplySelf(pns);
  }
}

void GameCommand::ApplySelf(const std::vector<PlayerNumber>& pns) const {
  const PlayMode old_play_mode = GAMESTATE->m_PlayMode;

  if (play_mode_ != PlayMode_Invalid) {
    GAMESTATE->m_PlayMode.Set(play_mode_);
  }

  if (style_ != nullptr) {
    GAMESTATE->SetCurrentStyle(style_, GAMESTATE->GetMasterPlayerNumber());

    // It's possible to choose a style that didn't have enough players joined.
    // If enough players aren't joined, then  we need to subtract credits
    // for the sides that will be joined as a result of applying this option.
    if (GAMESTATE->GetCoinMode() == CoinMode_Pay) {
      int num_credits_required = GetCreditsRequiredToPlayStyle(style_);
      int num_credits_paid = GetNumCreditsPaid();
      int num_credits_owed = num_credits_required - num_credits_paid;
      GAMESTATE->m_iCoins.Set(
          GAMESTATE->m_iCoins - num_credits_owed * PREFSMAN->m_iCoinsPerCredit);
      LOG->Trace(
          "Deducted %i coins, %i remaining",
          num_credits_owed * PREFSMAN->m_iCoinsPerCredit,
          GAMESTATE->m_iCoins.Get());

      // Credit Used, make sure to update CoinsFile
      BOOKKEEPER->WriteCoinsFile(GAMESTATE->m_iCoins.Get());
    }

    // If only one side is joined and we picked a style that requires both
    // sides, join the other side.
    switch (style_->m_StyleType) {
      case StyleType_OnePlayerOneSide:
      case StyleType_OnePlayerTwoSides:
        break;
      case StyleType_TwoPlayersTwoSides:
      case StyleType_TwoPlayersSharedSides: {
        FOREACH_PlayerNumber(p) GAMESTATE->JoinPlayer(p);
      } break;
      default:
        LuaHelpers::ReportScriptErrorFmt(
            "Invalid StyleType: %d", style_->m_StyleType);
    }
  }
  if (difficulty_ != Difficulty_Invalid) {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->m_PreferredDifficulty[pn].Set(difficulty_);
    }
  }
  if (announcer_ != "") {
    ANNOUNCER->SwitchAnnouncer(announcer_);
  }
  if (preferred_modifiers_ != "") {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->ApplyPreferredModifiers(pn, preferred_modifiers_);
    }
  }
  if (stage_modifiers_ != "") {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->ApplyStageModifiers(pn, stage_modifiers_);
    }
  }
  if (lua_function_.IsSet() && !lua_function_.IsNil()) {
    Lua* L = LUA->Get();
    for (const PlayerNumber& pn : pns) {
      lua_function_.PushSelf(L);
      ASSERT(!lua_isnil(L, -1));

      lua_pushnumber(L, pn);  // 1st parameter
      RString error = "Lua GameCommand error: ";
      LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
    }
    LUA->Release(L);
  }
  if (screen_ != "" && apply_commit_screens_) {
    SCREENMAN->SetNewScreen(screen_);
  }
  if (song_) {
    GAMESTATE->m_pCurSong.Set(song_);
    GAMESTATE->m_pPreferredSong = song_;
  }
  if (steps_) {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->m_pCurSteps[pn].Set(steps_);
    }
  }
  if (course_) {
    GAMESTATE->m_pCurCourse.Set(course_);
    GAMESTATE->m_pPreferredCourse = course_;
  }
  if (trail_) {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->m_pCurTrail[pn].Set(trail_);
    }
  }
  if (course_difficulty_ != Difficulty_Invalid) {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->ChangePreferredCourseDifficulty(pn, course_difficulty_);
    }
  }
  if (character_) {
    for (const PlayerNumber& pn : pns) {
      GAMESTATE->m_pCurCharacters[pn] = character_;
    }
  }
  for (auto it = set_env_.begin(); it != set_env_.end(); ++it) {
    Lua* L = LUA->Get();
    GAMESTATE->m_Environment->PushSelf(L);
    lua_pushstring(L, it->first);
    lua_pushstring(L, it->second);
    lua_settable(L, -3);
    lua_pop(L, 1);
    LUA->Release(L);
  }
  for (auto it = set_pref_.begin(); it != set_pref_.end(); ++it) {
    IPreference* preference = IPreference::GetPreferenceByName(it->first);
    if (preference != nullptr) {
      preference->FromString(it->second);
    }
  }
  if (!song_group_.empty()) {
    GAMESTATE->m_sPreferredSongGroup.Set(song_group_);
  }
  if (sort_order_ != SortOrder_Invalid) {
    GAMESTATE->m_PreferredSortOrder = sort_order_;
  }
  if (sound_path_ != "") {
    SOUND->PlayOnce(THEME->GetPathS("", sound_path_));
  }
  if (weight_pounds_ != -1) {
    for (const PlayerNumber& pn : pns) {
      PROFILEMAN->GetProfile(pn)->m_iWeightPounds = weight_pounds_;
    }
  }
  if (goal_calories_ != -1) {
    for (const PlayerNumber& pn : pns) {
      PROFILEMAN->GetProfile(pn)->m_iGoalCalories = goal_calories_;
    }
  }
  if (goal_type_ != GoalType_Invalid) {
    for (const PlayerNumber& pn : pns) {
      PROFILEMAN->GetProfile(pn)->m_GoalType = goal_type_;
    }
  }
  if (!profile_id_.empty()) {
    for (const PlayerNumber& pn : pns) {
      ProfileManager::m_sDefaultLocalProfileID[pn].Set(profile_id_);
    }
  }
  if (!url_.empty()) {
    if (HOOKS->GoToURL(url_)) {
      if (url_exists_) {
        SCREENMAN->SetNewScreen("ScreenExit");
      }
    } else {
      ScreenPrompt::Prompt(SM_None, COULD_NOT_LAUNCH_BROWSER);
    }
  }

  // If we're going to stop music, do so before preparing new screens, so we
  // don't stop music between preparing screens and loading screens.
  if (stop_music_) {
    SOUND->StopMusic();
  }
  if (fade_music_) {
    SOUND->DimMusic(music_fade_out_volume_, music_fade_out_seconds_);
  }

  for (const RString& s : screens_to_prepare_) {
    SCREENMAN->PrepareScreen(s);
  }

  if (insert_credits_) {
    StepMania::InsertCredit();
  }

  if (clear_credits_) {
    StepMania::ClearCredits();
  }

  if (apply_default_options_) {
    // Applying options affects only the current stage.
    FOREACH_PlayerNumber(p) {
      PlayerOptions player_options;
      GAMESTATE->GetDefaultPlayerOptions(player_options);
      GAMESTATE->m_pPlayerState[p]->m_PlayerOptions.Assign(
					ModsLevel_Stage, player_options);
    }

    SongOptions song_options;
    GAMESTATE->GetDefaultSongOptions(song_options);
    GAMESTATE->m_SongOptions.Assign(ModsLevel_Stage, song_options);
  }
  // HACK: Set life type to BATTERY just once here so it happens once and
  // we don't override the user's changes if they back out.
  FOREACH_PlayerNumber(pn) {
    if (GAMESTATE->m_PlayMode == PLAY_MODE_ONI &&
        GAMESTATE->m_PlayMode != old_play_mode &&
        GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions.GetStage().m_LifeType ==
            LifeType_Bar) {
      PO_GROUP_ASSIGN(
          GAMESTATE->m_pPlayerState[pn]->m_PlayerOptions, ModsLevel_Stage,
          m_LifeType, LifeType_Battery);
    }
  }
}

bool GameCommand::IsZero() const {
  if (play_mode_ != PlayMode_Invalid || style_ != nullptr ||
      difficulty_ != Difficulty_Invalid || announcer_ != "" ||
      preferred_modifiers_ != "" || stage_modifiers_ != "" ||
      song_ != nullptr || steps_ != nullptr || course_ != nullptr ||
      trail_ != nullptr || character_ != nullptr ||
      course_difficulty_ != Difficulty_Invalid || !song_group_.empty() ||
      sort_order_ != SortOrder_Invalid || weight_pounds_ != -1 ||
      goal_calories_ != -1 || goal_type_ != GoalType_Invalid ||
      !profile_id_.empty() || !url_.empty()) {
    return false;
  }

  return true;
}

// lua start
#include "Character.h"
#include "Game.h"
#include "LuaBinding.h"
#include "Steps.h"

// Allow Lua to have access to the GameCommand.
class LunaGameCommand : public Luna<GameCommand> {
 public:
  static int GetName(T* p, lua_State* L) {
    lua_pushstring(L, p->name_);
    return 1;
  }
  static int GetText(T* p, lua_State* L) {
    lua_pushstring(L, p->text_);
    return 1;
  }
  static int GetIndex(T* p, lua_State* L) {
    lua_pushnumber(L, p->index_);
    return 1;
  }
  static int GetMultiPlayer(T* p, lua_State* L) {
    lua_pushnumber(L, p->multiplayer_);
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
    lua_pushstring(L, p->screen_);
    return 1;
  }
  static int GetProfileID(T* p, lua_State* L) {
    lua_pushstring(L, p->profile_id_);
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
    lua_pushstring(L, p->song_group_);
    return 1;
  }
  static int GetUrl(T* p, lua_State* L) {
    lua_pushstring(L, p->url_);
    return 1;
  }
  static int GetAnnouncer(T* p, lua_State* L) {
    lua_pushstring(L, p->announcer_);
    return 1;
  }
  static int GetPreferredModifiers(T* p, lua_State* L) {
    lua_pushstring(L, p->preferred_modifiers_);
    return 1;
  }
  static int GetStageModifiers(T* p, lua_State* L) {
    lua_pushstring(L, p->stage_modifiers_);
    return 1;
  }

  DEFINE_METHOD(GetCourseDifficulty, course_difficulty_)
  DEFINE_METHOD(GetDifficulty, difficulty_)
  DEFINE_METHOD(GetPlayMode, play_mode_)
  DEFINE_METHOD(GetSortOrder, sort_order_)

  LunaGameCommand() {
    ADD_METHOD(GetName);
    ADD_METHOD(GetText);
    ADD_METHOD(GetIndex);
    ADD_METHOD(GetMultiPlayer);
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
