#include "GameState.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <set>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "AdjustSync.h"
#include "Bookkeeper.h"
#include "Character.h"
#include "CharacterManager.h"
#include "Command.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "CryptManager.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameCommand.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GamePreferences.h"
#include "Grade.h"
#include "HighScore.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "LuaReference.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "NoteData.h"
#include "NoteSkinManager.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "SongOptions.h"
#include "SongUtil.h"
#include "StatsManager.h"
#include "StdString.h"
#include "StepMania.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "Trail.h"
#include "UnlockManager.h"
#include "global.h"

GameState* GAMESTATE =
    nullptr;  // global and accessible from anywhere in our program

#define NAME_BLACKLIST_FILE "/Data/NamesBlacklist.txt"

class GameStateMessageHandler : public MessageSubscriber {
  void HandleMessage(const Message& msg) {
    if (msg.GetName() == "RefreshCreditText") {
      std::string sJoined;
      FOREACH_HumanPlayer(pn) {
        if (sJoined != "") {
          sJoined += ", ";
        }
        sJoined += ssprintf("P%i", pn + 1);
      }

      if (sJoined == "") {
        sJoined = "none";
      }

      LOG->MapLog("JOINED", "Players joined: %s", sJoined.c_str());
    }
  }
};

struct GameStateImpl {
  GameStateMessageHandler subscriber_;
  GameStateImpl() { subscriber_.SubscribeToMessage("RefreshCreditText"); }
};
static GameStateImpl* g_pImpl = nullptr;

ThemeMetric<bool> ALLOW_LATE_JOIN("GameState", "AllowLateJoin");
ThemeMetric<bool> USE_NAME_BLACKLIST("GameState", "UseNameBlacklist");

ThemeMetric<std::string> DEFAULT_SORT("GameState", "DefaultSort");
SortOrder GetDefaultSort() { return StringToSortOrder(DEFAULT_SORT); }
ThemeMetric<std::string> DEFAULT_SONG("GameState", "DefaultSong");
Song* GameState::GetDefaultSong() const {
  SongID sid;
  sid.FromString(DEFAULT_SONG);
  return sid.ToSong();
}

static const ThemeMetric<bool> EDIT_ALLOWED_FOR_EXTRA(
    "GameState", "EditAllowedForExtra");
static const ThemeMetric<Difficulty> MIN_DIFFICULTY_FOR_EXTRA(
    "GameState", "MinDifficultyForExtra");
static const ThemeMetric<Grade> GRADE_TIER_FOR_EXTRA_1(
    "GameState", "GradeTierForExtra1");
static const ThemeMetric<bool> ALLOW_EXTRA_2("GameState", "AllowExtra2");
static const ThemeMetric<Grade> GRADE_TIER_FOR_EXTRA_2(
    "GameState", "GradeTierForExtra2");

static ThemeMetric<bool> ARE_STAGE_PLAYER_MODS_FORCED(
    "GameState", "AreStagePlayerModsForced");
static ThemeMetric<bool> ARE_STAGE_SONG_MODS_FORCED(
    "GameState", "AreStageSongModsForced");

static Preference<Premium> g_Premium("Premium", Premium_DoubleFor1Credit);
static Preference<int> g_iPremiumFreeMinutes("PremiumFreeMinutes", 10);
static Preference<float> g_fPremiumFreeSongGraceSeconds(
    "PremiumFreeSongGraceSeconds", 150.0f);
Preference<bool> GameState::auto_join_("AutoJoin", false);

GameState::GameState()
    : processed_timing_(nullptr),
      cur_game_(Message_CurrentGameChanged),
      cur_style_(Message_CurrentStyleChanged),
      play_mode_(Message_PlayModeChanged),
      coins_(Message_CoinsChanged),
      preferred_song_group_(Message_PreferredSongGroupChanged),
      preferred_course_group_(Message_PreferredCourseGroupChanged),
      preferred_steps_type_(Message_PreferredStepsTypeChanged),
      preferred_difficulty_(Message_PreferredDifficultyP1Changed),
      preferred_course_difficulty_(Message_PreferredCourseDifficultyP1Changed),
      sort_order_(Message_SortOrderChanged),
      cur_song_(Message_CurrentSongChanged),
      cur_steps_(Message_CurrentStepsP1Changed),
      cur_course_(Message_CurrentCourseChanged),
      cur_trail_(Message_CurrentTrailP1Changed),
      premium_free_active_(false),
      premium_free_minutes_(0),
      gameplay_lead_in_(Message_GameplayLeadInChanged),
      did_mode_change_note_skin_(false),
      is_using_step_timing_(true),
      in_step_editor_(false),
      st_edit_(Message_EditStepsTypeChanged),
      cd_edit_(Message_EditCourseDifficultyChanged),
      edit_source_steps_(Message_EditSourceStepsChanged),
      edit_source_(Message_EditSourceStepsTypeChanged),
      edit_course_entry_index_(Message_EditCourseEntryIndexChanged),
      edit_local_profile_id_(Message_EditLocalProfileIDChanged) {
  g_pImpl = new GameStateImpl;

  cur_style_.Set(nullptr);
  FOREACH_PlayerNumber(rpn) { separated_styles_[rpn] = nullptr; }

  cur_game_.Set(nullptr);
  coins_.Set(0);
  time_game_started_.SetZero();
  demonstration_or_jukebox_ = false;

  num_times_through_attract_ = -1;  // initial screen will bump this up to 0
  stage_seed_ = game_seed_ = 0;

  play_mode_.Set(
      PlayMode_Invalid);  // used by IsPlayerEnabled before the first screen
  FOREACH_PlayerNumber(p) {
    // used by GetNumSidesJoined before the first screen
    side_is_joined_[p] = false;
    player_has_committed_gameplay_[p] = false;
  }

  FOREACH_PlayerNumber(p) {
    player_state_[p] = new PlayerState;
    player_state_[p]->SetPlayerNumber(p);
  }

  environment_ = new LuaTable;

  dopefish_ = false;
  last_open_section_ = "";
  expanded_section_name_ = "";
  expanded_parent_section_name_ = "";

  // Don't reset yet; let the first screen do it, so we can use PREFSMAN and
  // THEME.
  // Reset();

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "GAMESTATE");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

GameState::~GameState() {
  // Unregister with Lua.
  LUA->UnsetGlobal("GAMESTATE");

  FOREACH_PlayerNumber(p) RageUtil::SafeDelete(player_state_[p]);

  RageUtil::SafeDelete(environment_);
  RageUtil::SafeDelete(g_pImpl);
  RageUtil::SafeDelete(processed_timing_);
}

PlayerNumber GameState::GetMasterPlayerNumber() const {
  return this->master_player_number_;
}

void GameState::SetMasterPlayerNumber(const PlayerNumber p) {
  this->master_player_number_ = p;
}

TimingData* GameState::GetProcessedTimingData() const {
  return this->processed_timing_;
}

void GameState::SetProcessedTimingData(TimingData* t) {
  this->processed_timing_ = t;
}

void GameState::ApplyGameCommand(const std::string& sCommand, PlayerNumber pn) {
  GameCommand m;
  m.Load(0, ParseCommands(sCommand));

  std::string sWhy;
  if (!m.IsPlayable(&sWhy)) {
    LuaHelpers::ReportScriptErrorFmt(
        "Can't apply GameCommand \"%s\": %s", sCommand.c_str(), sWhy.c_str());
    return;
  }

  if (pn == PLAYER_INVALID) {
    m.ApplyToAllPlayers();
  } else {
    m.Apply(pn);
  }
}

void GameState::ApplyCmdline() {
  // We need to join players before we can set the style.
  std::string sPlayer;
  for (int i = 0; GetCommandlineArgument("player", &sPlayer, i); ++i) {
    int pn = StringToInt(sPlayer) - 1;
    if (!IsAnInt(sPlayer) || pn < 0 || pn >= NUM_PLAYERS) {
      RageException::Throw(
          "Invalid argument \"--player=%s\".", sPlayer.c_str());
    }

    JoinPlayer((PlayerNumber)pn);
  }

  std::string sMode;
  for (int i = 0; GetCommandlineArgument("mode", &sMode, i); ++i) {
    ApplyGameCommand(sMode);
  }
}

void GameState::ResetPlayer(PlayerNumber pn) {
  preferred_steps_type_.Set(StepsType_Invalid);
  preferred_difficulty_[pn].Set(Difficulty_Invalid);
  preferred_course_difficulty_[pn].Set(Difficulty_Medium);
  player_stage_tokens_[pn] = 0;
  awarded_extra_stages_[pn] = 0;
  cur_steps_[pn].Set(nullptr);
  cur_trail_[pn].Set(nullptr);
  player_state_[pn]->Reset();
  PROFILEMAN->UnloadProfile(pn);
  player_has_committed_gameplay_[pn] = false;
  ResetPlayerOptions(pn);
}

void GameState::ResetPlayerOptions(PlayerNumber pn) {
  PlayerOptions po;
  GetDefaultPlayerOptions(po);
  player_state_[pn]->player_options_.Assign(ModsLevel_Preferred, po);
}

void GameState::Reset() {
  this->SetMasterPlayerNumber(
      PLAYER_INVALID);  // must initialize for UnjoinPlayer

  FOREACH_PlayerNumber(pn) UnjoinPlayer(pn);

  ASSERT(THEME != nullptr);

  time_game_started_.SetZero();
  SetCurrentStyle(nullptr, PLAYER_INVALID);
  FOREACH_PlayerNumber(pn) MEMCARDMAN->UnlockCard(pn);
  *environment_ = LuaTable();
  preferred_song_group_.Set(GROUP_ALL);
  preferred_course_group_.Set(GROUP_ALL);
  fail_type_was_explicitly_set_ = false;
  sort_order_.Set(SortOrder_Invalid);
  preferred_sort_order_ = GetDefaultSort();
  play_mode_.Set(PlayMode_Invalid);
  edit_mode_ = EditMode_Invalid;
  demonstration_or_jukebox_ = false;
  jukebox_uses_modifiers_ = false;
  current_stage_index_ = 0;

  gameplay_lead_in_.Set(false);
  num_stages_of_this_song_ = 0;
  loading_next_song_ = false;

  NOTESKIN->RefreshNoteSkinData(cur_game_);

  game_seed_ = rand();
  stage_seed_ = rand();

  adjust_tokens_by_song_cost_for_final_stage_check_ = true;

  cur_song_.Set(GetDefaultSong());
  preferred_song_ = nullptr;
  cur_course_.Set(nullptr);
  preferred_course_ = nullptr;
  premium_free_active_ = false;
  premium_free_minutes_ = 0;
  premium_free_start_time_.SetZero();

  song_options_.Init();

  paused_ = false;
  ResetMusicStatistics();
  ResetStageStatistics();
  AdjustSync::ResetOriginalSyncData();

  SONGMAN->UpdatePopular();
  SONGMAN->UpdateShuffled();

  /* We may have cached trails from before everything was loaded (eg. from
   * before SongManager::UpdatePopular could be called). Erase the cache. */
  SONGMAN->RegenerateNonFixedCourses();

  STATSMAN->Reset();

  FOREACH_PlayerNumber(p) {
    if (PREFSMAN->show_dancing_characters_ == SDC_Random) {
      cur_characters_[p] = CHARMAN->GetRandomCharacter();
    } else {
      cur_characters_[p] = CHARMAN->GetDefaultCharacter();
    }
    ASSERT(cur_characters_[p] != nullptr);
  }

  temporary_event_mode_ = false;

  LIGHTSMAN->SetLightsMode(LIGHTSMODE_ATTRACT);

  st_edit_.Set(StepsType_Invalid);
  edit_source_steps_.Set(nullptr);
  edit_source_.Set(StepsType_Invalid);
  edit_course_entry_index_.Set(-1);
  edit_local_profile_id_.Set("");

  backed_out_of_final_stage_ = false;
  earned_extra_stage_ = false;
  expanded_section_name_ = "";
  expanded_parent_section_name_ = "";

  ApplyCmdline();
}

void GameState::JoinPlayer(PlayerNumber pn) {
  // Make sure the join will be successful before doing it. -Kyz
  {
    int players_joined = 0;
    for (int i = 0; i < NUM_PLAYERS; ++i) {
      players_joined += side_is_joined_[i];
    }
    if (players_joined > 0) {
      const Style* cur_style = GetCurrentStyle(PLAYER_INVALID);
      if (cur_style) {
        const Style* new_style = GAMEMAN->GetFirstCompatibleStyle(
            cur_game_, players_joined + 1, cur_style->steps_type_);
        if (new_style == nullptr) {
          return;
        }
      }
    }
  }
  /* If joint premium and we're not taking away a credit for the 2nd join,
   * give the new player the same number of stage tokens that the old player
   * has. */
  if (GetCoinMode() == CoinMode_Pay &&
      GetPremium() == Premium_2PlayersFor1Credit && GetNumSidesJoined() == 1) {
    player_stage_tokens_[pn] =
        player_stage_tokens_[this->GetMasterPlayerNumber()];
  } else {
    player_stage_tokens_[pn] = PREFSMAN->songs_per_play_;
  }

  side_is_joined_[pn] = true;

  if (this->GetMasterPlayerNumber() == PLAYER_INVALID) {
    this->SetMasterPlayerNumber(pn);
  }

  // if first player to join, set start time
  if (GetNumSidesJoined() == 1) {
    BeginGame();
  }

  // Count each player join as a play.
  {
    Profile* pMachineProfile = PROFILEMAN->GetMachineProfile();
    pMachineProfile->total_sessions_++;
  }

  // Set the current style to something appropriate for the new number of joined
  // players. beat gametype's versus styles use a different stepstype from its
  // single styles, so when GameCommand tries to join both players for a versus
  // style, it hits the assert when joining the first player.  So if the first
  // player is being joined and the current styletype is for two players,
  // assume that the second player will be joined immediately afterwards and
  // don't try to change the style. -Kyz
  const Style* cur_style = GetCurrentStyle(PLAYER_INVALID);
  if (cur_style != nullptr &&
      !(pn == PLAYER_1 &&
        (cur_style->style_type_ == StyleType_TwoPlayersTwoSides ||
         cur_style->style_type_ == StyleType_TwoPlayersSharedSides))) {
    const Style* pStyle;
    // Only use one player for StyleType_OnePlayerTwoSides and StepsTypes
    // that can only be played by one player (e.g. dance-solo,
    // dance-threepanel, popn-nine). -aj
    // XXX?: still shows joined player as "Insert Card". May not be an issue?
    // -aj
    if (cur_style->style_type_ == StyleType_OnePlayerTwoSides ||
        cur_style->steps_type_ == StepsType_dance_solo ||
        cur_style->steps_type_ == StepsType_dance_threepanel) {
      pStyle = GAMEMAN->GetFirstCompatibleStyle(
          cur_game_, 1, cur_style->steps_type_);
    } else {
      pStyle = GAMEMAN->GetFirstCompatibleStyle(
          cur_game_, GetNumSidesJoined(), cur_style->steps_type_);
    }

    // use SetCurrentStyle in case of StyleType_OnePlayerTwoSides
    SetCurrentStyle(pStyle, pn);
  }

  Message msg(MessageIDToString(Message_PlayerJoined));
  msg.SetParam("Player", pn);
  MESSAGEMAN->Broadcast(msg);
}

void GameState::UnjoinPlayer(PlayerNumber pn) {
  /* Unjoin STATSMAN first, so steps used by this player are released
   * and can be released by PROFILEMAN. */
  STATSMAN->UnjoinPlayer(pn);
  side_is_joined_[pn] = false;
  player_stage_tokens_[pn] = 0;

  ResetPlayer(pn);

  if (this->GetMasterPlayerNumber() == pn) {
    // We can't use GetFirstHumanPlayer() because if both players were joined,
    // GetFirstHumanPlayer() will always return PLAYER_1, even when PLAYER_1 is
    // the player we're unjoining.
    FOREACH_HumanPlayer(hp) {
      if (pn != hp) {
        this->SetMasterPlayerNumber(hp);
      }
    }
    if (this->GetMasterPlayerNumber() == pn) {
      this->SetMasterPlayerNumber(PLAYER_INVALID);
    }
  }

  Message msg(MessageIDToString(Message_PlayerUnjoined));
  msg.SetParam("Player", pn);
  MESSAGEMAN->Broadcast(msg);

  // If there are no players left, reset some non-player-specific stuff, too.
  if (this->GetMasterPlayerNumber() == PLAYER_INVALID) {
    SongOptions so;
    GetDefaultSongOptions(so);
    song_options_.Assign(ModsLevel_Preferred, so);
    did_mode_change_note_skin_ = false;
  }
}

namespace {
bool JoinInputInternal(PlayerNumber pn) {
  if (!GAMESTATE->PlayersCanJoin()) {
    return false;
  }

  // If this side is already in, don't re-join.
  if (GAMESTATE->side_is_joined_[pn]) {
    return false;
  }

  // subtract coins
  int iCoinsNeededToJoin = GAMESTATE->GetCoinsNeededToJoin();

  if (GAMESTATE->coins_ < iCoinsNeededToJoin) {
    return false;  // not enough coins
  }

  GAMESTATE->coins_.Set(GAMESTATE->coins_ - iCoinsNeededToJoin);

  GAMESTATE->JoinPlayer(pn);

  // On Join, make sure to update Coins File
  BOOKKEEPER->WriteCoinsFile(GAMESTATE->coins_.Get());

  return true;
}
};  // namespace

// Handle an input that can join a player. Return true if the player joined.
bool GameState::JoinInput(PlayerNumber pn) {
  // When AutoJoin is enabled, join all players on a single start press.
  if (GAMESTATE->auto_join_.Get()) {
    return JoinPlayers();
  } else {
    return JoinInputInternal(pn);
  }
}

// Attempt to join all players, as if each player pressed Start.
bool GameState::JoinPlayers() {
  bool bJoined = false;
  FOREACH_PlayerNumber(pn) {
    if (JoinInputInternal(pn)) {
      bJoined = true;
    }
  }
  return bJoined;
}

int GameState::GetCoinsNeededToJoin() const {
  int iCoinsToCharge = 0;

  if (GetCoinMode() == CoinMode_Pay) {
    iCoinsToCharge = PREFSMAN->coins_per_credit_;
  }

  // If joint premium, don't take away a credit for the second join.
  if (GetPremium() == Premium_2PlayersFor1Credit && GetNumSidesJoined() == 1) {
    iCoinsToCharge = 0;
  }

  return iCoinsToCharge;
}

/* Game flow:
 *
 * BeginGame() - the first player has joined; the game is starting.
 *
 * PlayersFinalized() - player memory cards are loaded; later joins won't have
 * memory cards this stage
 *
 * BeginStage() - gameplay is beginning
 *
 * optional: CancelStage() - gameplay aborted (Back pressed), undo BeginStage
 * and back up
 *
 * CommitStageStats() - gameplay is finished
 *   Saves STATSMAN->m_CurStageStats to the profiles, so profile information
 *   is up-to-date for Evaluation.
 *
 * FinishStage() - gameplay and evaluation is finished
 *   Clears data which was stored by CommitStageStats. */
void GameState::BeginGame() {
  time_game_started_.Touch();

  names_that_were_filled_.clear();
  players_that_were_filled_.clear();

  // Play attract on the ending screen, then on the ranking screen
  // even if attract sounds are set to off.
  num_times_through_attract_ = -1;

  FOREACH_PlayerNumber(pn) MEMCARDMAN->UnlockCard(pn);
}

bool GameState::CanAcceptProfile(PlayerNumber pn) const {
  return IsHumanPlayer(pn) && !PROFILEMAN->IsPersistentProfile(pn) &&
         !player_has_committed_gameplay_[pn];
}

bool GameState::LoadProfileOverGuest(PlayerNumber pn, bool bLoadEdits) {
  if (!CanAcceptProfile(pn)) {
    return false;
  }

  MEMCARDMAN->MountCard(pn);
  const bool bSuccess = PROFILEMAN->LoadFirstAvailableProfile(
      pn, bLoadEdits);  // load full profile
  MEMCARDMAN->UnmountCard(pn);

  if (!bSuccess) {
    return false;
  }

  // Lock the card on successful load, so we won't allow it to be changed.
  MEMCARDMAN->LockCard(pn);

  LoadCurrentSettingsFromProfile(pn);

  Profile* pPlayerProfile = PROFILEMAN->GetProfile(pn);
  if (pPlayerProfile) {
    pPlayerProfile->total_sessions_++;
  }

  return true;
}

void GameState::LoadProfiles(bool bLoadEdits) {
  // Unlock any cards that we might want to load.
  FOREACH_HumanPlayer(pn) if (!PROFILEMAN->IsPersistentProfile(pn))
      MEMCARDMAN->UnlockCard(pn);

  MEMCARDMAN->WaitForCheckingToComplete();

  FOREACH_HumanPlayer(pn) { LoadProfileOverGuest(pn, bLoadEdits); }
}

void GameState::SavePlayerProfiles() {
  FOREACH_HumanPlayer(pn) SavePlayerProfile(pn);
}

void GameState::SavePlayerProfile(PlayerNumber pn) {
  if (!PROFILEMAN->IsPersistentProfile(pn)) {
    return;
  }

  // AutoplayCPU should not save scores. -aj
  if (player_state_[pn]->player_controller_ != PC_HUMAN) {
    return;
  }

  bool bWasMemoryCard = PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn);
  if (bWasMemoryCard && !MEMCARDMAN->MountCard(pn)) {
    LOG->Warn(
        "Couldn't mount P%d memory card, profile '%s' won't be saved", pn + 1,
        PROFILEMAN->GetPlayerName(pn).c_str());
    return;
  }
  PROFILEMAN->SaveProfile(pn);
  if (bWasMemoryCard) {
    MEMCARDMAN->UnmountCard(pn);
  }
}

bool GameState::HaveProfileToLoad(PlayerNumber pn) const {
  if (!CanAcceptProfile(pn)) {
    return false;
  }

  return MEMCARDMAN->CardInserted(pn) ||
         !PROFILEMAN->default_local_profile_id_[pn].Get().empty();
}

bool GameState::HaveProfileToLoad() {
  FOREACH_HumanPlayer(pn) if (HaveProfileToLoad(pn)) return true;
  return false;
}

bool GameState::HaveProfileToSave() {
  FOREACH_HumanPlayer(pn) if (PROFILEMAN->IsPersistentProfile(pn)) return true;
  return false;
}

void GameState::SaveLocalData() {
  BOOKKEEPER->WriteToDisk();
  PROFILEMAN->SaveMachineProfile();
}

int GameState::GetNumStagesMultiplierForSong(const Song* pSong) {
  int iNumStages = 1;

  ASSERT(pSong != nullptr);
  if (pSong->IsMarathon()) {
    iNumStages *= 3;
  }
  if (pSong->IsLong()) {
    iNumStages *= 2;
  }

  return iNumStages;
}

int GameState::GetNumStagesForCurrentSongAndStepsOrCourse() const {
  int iNumStagesOfThisSong = 1;
  if (cur_song_) {
    /* Extra stages need to only count as one stage in case a multi-stage
     * song is chosen. */
    if (IsAnExtraStage()) {
      iNumStagesOfThisSong = 1;
    } else {
      iNumStagesOfThisSong =
          GameState::GetNumStagesMultiplierForSong(cur_song_);
    }
  } else if (cur_course_) {
    iNumStagesOfThisSong = PREFSMAN->songs_per_play_;
  } else {
    return -1;
  }

  iNumStagesOfThisSong = std::max(iNumStagesOfThisSong, 1);

  return iNumStagesOfThisSong;
}

// Called by ScreenGameplay. Set the length of the current song.
void GameState::BeginStage() {
  if (demonstration_or_jukebox_) {
    return;
  }

  // This should only be called once per stage.
  if (num_stages_of_this_song_ != 0) {
    LOG->Warn("XXX: num_stages_of_this_song_ == %i?", num_stages_of_this_song_);
  }

  ResetStageStatistics();
  AdjustSync::ResetOriginalSyncData();

  if (!ARE_STAGE_PLAYER_MODS_FORCED) {
    FOREACH_PlayerNumber(p) {
      ModsGroup<PlayerOptions>& po = player_state_[p]->player_options_;
      po.Assign(
          ModsLevel_Stage, player_state_[p]->player_options_.GetPreferred());
    }
  }
  if (!ARE_STAGE_SONG_MODS_FORCED) {
    song_options_.Assign(ModsLevel_Stage, song_options_.GetPreferred());
  }

  STATSMAN->cur_stage_stats_.music_rate_ = song_options_.GetSong().music_rate_;
  num_stages_of_this_song_ = GetNumStagesForCurrentSongAndStepsOrCourse();
  ASSERT(num_stages_of_this_song_ != -1);
  FOREACH_EnabledPlayer(p) {
    // only do this check with human players, assume CPU players (Rave)
    // always have tokens. -aj (this could probably be moved below, even.)
    if (!IsEventMode() && !premium_free_active_ && !IsCpuPlayer(p)) {
      if (player_stage_tokens_[p] < num_stages_of_this_song_) {
        LuaHelpers::ReportScriptErrorFmt(
            "Player %d only has %d stage tokens, but needs %d.", p,
            player_stage_tokens_[p], num_stages_of_this_song_);
      }
    }
    player_stage_tokens_[p] -= num_stages_of_this_song_;
  }
  FOREACH_HumanPlayer(pn) if (CurrentOptionsDisqualifyPlayer(pn))
      STATSMAN->cur_stage_stats_.player_[pn]
          .disqualified_ = true;
  earned_extra_stage_ = false;
  stage_guid_ = CryptManager::GenerateRandomUUID();
}

void GameState::CancelStage() {
  FOREACH_CpuPlayer(p) {
    switch (play_mode_) {
      case PLAY_MODE_BATTLE:
      case PLAY_MODE_RAVE:
        player_stage_tokens_[p] = PREFSMAN->songs_per_play_;
      default:
        break;
    }
  }

  FOREACH_EnabledPlayer(p) player_stage_tokens_[p] += num_stages_of_this_song_;
  num_stages_of_this_song_ = 0;
  ResetStageStatistics();
}

void GameState::CommitStageStats() {
  if (demonstration_or_jukebox_) {
    return;
  }

  STATSMAN->CommitStatsToProfiles(&STATSMAN->cur_stage_stats_);
  FOREACH_HumanPlayer(p) player_has_committed_gameplay_[p] = true;

  // Update TotalPlaySeconds.
  int iPlaySeconds = std::max(0, (int)time_game_started_.GetDeltaTime());

  Profile* pMachineProfile = PROFILEMAN->GetMachineProfile();
  pMachineProfile->total_session_seconds_ += iPlaySeconds;

  FOREACH_HumanPlayer(p) {
    Profile* pPlayerProfile = PROFILEMAN->GetProfile(p);
    if (pPlayerProfile) {
      pPlayerProfile->total_session_seconds_ += iPlaySeconds;
    }
  }
}

/* Called by ScreenSelectMusic (etc). Increment the stage counter if we just
 * played a song. Might be called more than once. */
void GameState::FinishStage() {
  // Increment the stage counter.
  const int iOldStageIndex = current_stage_index_;
  ++current_stage_index_;

  num_stages_of_this_song_ = 0;

  EarnedExtraStage e = CalculateEarnedExtraStage();
  STATSMAN->cur_stage_stats_.earned_extra_stage_ = e;
  if (e != EarnedExtraStage_No) {
    LOG->Trace("awarded extra stage");
    FOREACH_HumanPlayer(p) {
      // todo: unhardcode the extra stage limit? -aj
      if (awarded_extra_stages_[p] < 2) {
        ++awarded_extra_stages_[p];
        ++player_stage_tokens_[p];
        earned_extra_stage_ = true;
      }
    }
  }

  // Save the current combo to the profiles so it can be used for
  // ComboContinuesBetweenSongs.
  FOREACH_HumanPlayer(p) {
    Profile* pProfile = PROFILEMAN->GetProfile(p);
    pProfile->current_combo_ = STATSMAN->cur_stage_stats_.player_[p].cur_combo_;
    // If the sort order is Recent, move the profile to the top of the list.
    if (PREFSMAN->profile_sort_order_ == ProfileSortOrder_Recent &&
        PROFILEMAN->IsPersistentProfile(p)) {
      int numLocalProfiles = PROFILEMAN->GetNumLocalProfiles();
      for (int i = 0; i < numLocalProfiles; i++) {
        Profile* profile = PROFILEMAN->GetLocalProfileFromIndex(i);
        if (profile->guid_ == pProfile->guid_) {
          PROFILEMAN->MoveProfileTopBottom(
              i, PREFSMAN->profile_sort_order_ascending_);
          break;
        }
      }
    }
  }

  if (demonstration_or_jukebox_) {
    return;
  }

  // todo: simplify. profile saving is accomplished in ScreenProfileSave
  // now; all this code does differently is save machine profile as well. -aj
  if (IsEventMode()) {
    const int iSaveProfileEvery = 3;
    if (iOldStageIndex / iSaveProfileEvery <
        current_stage_index_ / iSaveProfileEvery) {
      LOG->Trace("Played %i stages; saving profiles ...", iSaveProfileEvery);
      PROFILEMAN->SaveMachineProfile();
      this->SavePlayerProfiles();
    }
  }
}

void GameState::LoadCurrentSettingsFromProfile(PlayerNumber pn) {
  if (!PROFILEMAN->IsPersistentProfile(pn)) {
    return;
  }

  const Profile* pProfile = PROFILEMAN->GetProfile(pn);

  // apply saved default modifiers if any
  std::string sModifiers;
  if (pProfile->GetDefaultModifiers(cur_game_, sModifiers)) {
    /* We don't save negative preferences (eg. "no reverse"). If the theme
     * sets a default of "reverse", and the player turns it off, we should
     * set it off. However, don't reset modifiers that aren't saved by the
     * profile, so we don't ignore unsaved modifiers when a profile is in use.
     */
    PO_GROUP_CALL(
        player_state_[pn]->player_options_, ModsLevel_Preferred,
        ResetSavedPrefs);
    ApplyPreferredModifiers(pn, sModifiers);
  }
  // Only set the sort order if it wasn't already set by a GameCommand (or by an
  // earlier profile)
  if (preferred_sort_order_ == SortOrder_Invalid &&
      pProfile->sort_order_ != SortOrder_Invalid) {
    preferred_sort_order_ = pProfile->sort_order_;
  }
  if (pProfile->last_difficulty_ != Difficulty_Invalid) {
    preferred_difficulty_[pn].Set(pProfile->last_difficulty_);
  }
  if (pProfile->last_course_difficulty_ != Difficulty_Invalid) {
    preferred_course_difficulty_[pn].Set(pProfile->last_course_difficulty_);
  }
  // Only set the PreferredStepsType if it wasn't already set by a GameCommand
  // (or by an earlier profile)
  if (preferred_steps_type_ == StepsType_Invalid &&
      pProfile->last_steps_type_ != StepsType_Invalid) {
    preferred_steps_type_.Set(pProfile->last_steps_type_);
  }
  if (preferred_song_ == nullptr) {
    preferred_song_ = pProfile->last_song_.ToSong();
  }
  if (preferred_course_ == nullptr) {
    preferred_course_ = pProfile->last_course_.ToCourse();
  }
}

void GameState::SaveCurrentSettingsToProfile(PlayerNumber pn) {
  if (!PROFILEMAN->IsPersistentProfile(pn)) {
    return;
  }
  if (demonstration_or_jukebox_) {
    return;
  }

  Profile* pProfile = PROFILEMAN->GetProfile(pn);

  pProfile->SetDefaultModifiers(
      cur_game_,
      player_state_[pn]->player_options_.GetPreferred().GetSavedPrefsString());
  if (IsSongSort(preferred_sort_order_)) {
    pProfile->sort_order_ = preferred_sort_order_;
  }
  if (preferred_difficulty_[pn] != Difficulty_Invalid) {
    pProfile->last_difficulty_ = preferred_difficulty_[pn];
  }
  if (preferred_course_difficulty_[pn] != Difficulty_Invalid) {
    pProfile->last_course_difficulty_ = preferred_course_difficulty_[pn];
  }
  if (preferred_steps_type_ != StepsType_Invalid) {
    pProfile->last_steps_type_ = preferred_steps_type_;
  }
  if (preferred_song_) {
    pProfile->last_song_.FromSong(preferred_song_);
  }
  if (preferred_course_) {
    pProfile->last_course_.FromCourse(preferred_course_);
  }
}

bool GameState::CanSafelyEnterGameplay(std::string& reason) {
  if (!IsCourseMode()) {
    const Song* song = cur_song_;
    if (song == nullptr) {
      reason = "Current song is null.";
      return false;
    }
  } else {
    const Course* song = cur_course_;
    if (song == nullptr) {
      reason = "Current course is null.";
      return false;
    }
  }
  FOREACH_EnabledPlayer(pn) {
    const Style* style = GetCurrentStyle(pn);
    if (style == nullptr) {
      reason = ssprintf("Style for player %d is null.", pn + 1);
      return false;
    }
    if (!IsCourseMode()) {
      const Steps* steps = cur_steps_[pn];
      if (steps == nullptr) {
        reason = ssprintf("Steps for player %d is null.", pn + 1);
        return false;
      }
      if (steps->steps_type_ != style->steps_type_) {
        reason = ssprintf(
            "Player %d StepsType %s for steps does not equal "
            "StepsType %s for style.",
            pn + 1, GAMEMAN->GetStepsTypeInfo(steps->steps_type_).name_,
            GAMEMAN->GetStepsTypeInfo(style->steps_type_).name_);
        return false;
      }
      if (steps->song_ != cur_song_) {
        reason = ssprintf(
            "Steps for player %d are not for the current song.", pn + 1);
        return false;
      }
      NoteData ndtemp;
      steps->GetNoteData(ndtemp);
      if (ndtemp.GetNumTracks() != style->cols_per_player_) {
        reason = ssprintf(
            "Steps for player %d have %d columns, style has %d "
            "columns.",
            pn + 1, ndtemp.GetNumTracks(), style->cols_per_player_);
        return false;
      }
    } else {
      const Trail* steps = cur_trail_[pn];
      if (steps == nullptr) {
        reason = ssprintf("Steps for player %d is null.", pn + 1);
        return false;
      }
      if (steps->steps_type_ != style->steps_type_) {
        reason = ssprintf(
            "Player %d StepsType %s for steps does not equal "
            "StepsType %s for style.",
            pn + 1, GAMEMAN->GetStepsTypeInfo(steps->steps_type_).name_,
            GAMEMAN->GetStepsTypeInfo(style->steps_type_).name_);
        return false;
      }
    }
  }
  return true;
}

void GameState::SetCompatibleStylesForPlayers() {
  bool style_set = false;
  if (IsCourseMode()) {
    if (cur_course_ != nullptr) {
      const Style* style =
          cur_course_->GetCourseStyle(cur_game_, GetNumSidesJoined());
      if (style != nullptr) {
        style_set = true;
        SetCurrentStyle(style, PLAYER_INVALID);
      }
    } else if (GetCurrentStyle(PLAYER_INVALID) == nullptr) {
      std::vector<StepsType> vst;
      GAMEMAN->GetStepsTypesForGame(cur_game_, vst);
      const Style* style = GAMEMAN->GetFirstCompatibleStyle(
          cur_game_, GetNumSidesJoined(), vst[0]);
      SetCurrentStyle(style, PLAYER_INVALID);
    }
  }
  if (!style_set) {
    FOREACH_EnabledPlayer(pn) {
      StepsType st = StepsType_Invalid;
      if (cur_steps_[pn] != nullptr) {
        st = cur_steps_[pn]->steps_type_;
      } else if (cur_trail_[pn] != nullptr) {
        st = cur_trail_[pn]->steps_type_;
      } else {
        std::vector<StepsType> vst;
        GAMEMAN->GetStepsTypesForGame(cur_game_, vst);
        st = vst[0];
      }
      const Style* style =
          GAMEMAN->GetFirstCompatibleStyle(cur_game_, GetNumSidesJoined(), st);
      SetCurrentStyle(style, pn);
    }
  }
}

void GameState::ForceSharedSidesMatch() {
  PlayerNumber pn_with_shared = PLAYER_INVALID;
  const Style* shared_style = nullptr;
  FOREACH_EnabledPlayer(pn) {
    const Style* style = GetCurrentStyle(pn);
    ASSERT_M(style != nullptr, "Style being null should not be possible.");
    if (style->style_type_ == StyleType_TwoPlayersSharedSides) {
      pn_with_shared = pn;
      shared_style = style;
    }
  }
  if (pn_with_shared != PLAYER_INVALID) {
    ASSERT_M(
        GetNumPlayersEnabled() == 2,
        "2 players must be enabled for shared sides.");
    PlayerNumber other_pn = OPPOSITE_PLAYER[pn_with_shared];
    const Style* other_style = GetCurrentStyle(other_pn);
    ASSERT_M(
        other_style != nullptr,
        "Other player's style being null should not be possible.");
    if (other_style->style_type_ != StyleType_TwoPlayersSharedSides) {
      SetCurrentStyle(shared_style, other_pn);
      if (IsCourseMode()) {
        cur_trail_[other_pn].Set(cur_trail_[pn_with_shared]);
      } else {
        cur_steps_[other_pn].Set(cur_steps_[pn_with_shared]);
      }
    }
  }
}

void GameState::ForceOtherPlayersToCompatibleSteps(PlayerNumber main) {
  if (IsCourseMode()) {
    Trail* steps_to_match = cur_trail_[main].Get();
    if (steps_to_match == nullptr) {
      return;
    }
    int num_players = GAMESTATE->GetNumPlayersEnabled();
    StyleType styletype_to_match =
        GAMEMAN
            ->GetFirstCompatibleStyle(
                GAMESTATE->GetCurrentGame(), num_players,
                steps_to_match->steps_type_)
            ->style_type_;
    FOREACH_EnabledPlayer(pn) {
      Trail* pn_steps = cur_trail_[pn].Get();
      bool match_failed = pn_steps == nullptr;
      if (steps_to_match != pn_steps && pn_steps != nullptr) {
        StyleType pn_styletype = GAMEMAN
                                     ->GetFirstCompatibleStyle(
                                         GAMESTATE->GetCurrentGame(),
                                         num_players, pn_steps->steps_type_)
                                     ->style_type_;
        if (styletype_to_match == StyleType_TwoPlayersSharedSides ||
            pn_styletype == StyleType_TwoPlayersSharedSides) {
          match_failed = true;
        }
      }
      if (match_failed) {
        cur_trail_[pn].Set(steps_to_match);
      }
    }
  } else {
    Steps* steps_to_match = cur_steps_[main].Get();
    if (steps_to_match == nullptr) {
      return;
    }
    int num_players = GAMESTATE->GetNumPlayersEnabled();
    StyleType styletype_to_match =
        GAMEMAN
            ->GetFirstCompatibleStyle(
                GAMESTATE->GetCurrentGame(), num_players,
                steps_to_match->steps_type_)
            ->style_type_;
    std::string music_to_match = steps_to_match->GetMusicFile();
    FOREACH_EnabledPlayer(pn) {
      Steps* pn_steps = cur_steps_[pn].Get();
      bool match_failed = pn_steps == nullptr;
      if (steps_to_match != pn_steps && pn_steps != nullptr) {
        StyleType pn_styletype = GAMEMAN
                                     ->GetFirstCompatibleStyle(
                                         GAMESTATE->GetCurrentGame(),
                                         num_players, pn_steps->steps_type_)
                                     ->style_type_;
        if (styletype_to_match == StyleType_TwoPlayersSharedSides ||
            pn_styletype == StyleType_TwoPlayersSharedSides) {
          match_failed = true;
        }
        if (music_to_match != pn_steps->GetMusicFile()) {
          match_failed = true;
        }
      }
      if (match_failed) {
        cur_steps_[pn].Set(steps_to_match);
      }
    }
  }
}

void GameState::Update(float fDelta) {
  song_options_.Update(fDelta);

  FOREACH_PlayerNumber(p) {
    player_state_[p]->Update(fDelta);

    if (!goal_complete_[p] && IsGoalComplete(p)) {
      goal_complete_[p] = true;
      MESSAGEMAN->Broadcast(
          (MessageID)(Message_GoalCompleteP1 + Enum::to_integral(p)));
    }
  }

  if (GAMESTATE->cur_course_) {
    if (GAMESTATE->cur_course_->goal_seconds_ > 0 && !workout_goal_complete_) {
      const StageStats& ssCurrent = STATSMAN->cur_stage_stats_;
      bool bGoalComplete =
          ssCurrent.gameplay_seconds_ > GAMESTATE->cur_course_->goal_seconds_;
      if (bGoalComplete) {
        MESSAGEMAN->Broadcast("WorkoutGoalComplete");
        workout_goal_complete_ = true;
      }
    }
  }
}

void GameState::SetCurGame(const Game* pGame) {
  cur_game_.Set(pGame);
  std::string sGame = pGame ? std::string(pGame->name_) : std::string();
  PREFSMAN->SetCurrentGame(sGame);
}

const float GameState::kMusicSecondsInvalid = -5000.0f;

void GameState::ResetMusicStatistics() {
  position_.Reset();
  last_position_timer_.Touch();
  last_position_seconds_ = 0.0f;

  Actor::SetBGMTime(0, 0, 0, 0);

  FOREACH_PlayerNumber(p) { player_state_[p]->position_.Reset(); }
}

void GameState::ResetStageStatistics() {
  StageStats OldStats = STATSMAN->cur_stage_stats_;
  STATSMAN->cur_stage_stats_ = StageStats();
  if (PREFSMAN->combo_continues_between_songs_) {
    FOREACH_PlayerNumber(p) {
      bool FirstSong = current_stage_index_ == 0;
      if (FirstSong) {
        Profile* pProfile = PROFILEMAN->GetProfile(p);
        STATSMAN->cur_stage_stats_.player_[p].cur_combo_ =
            pProfile->current_combo_;
      } else {
        STATSMAN->cur_stage_stats_.player_[p].cur_combo_ =
            OldStats.player_[p].cur_combo_;
      }
    }
  }

  RemoveAllActiveAttacks();
  opponent_health_percent_ = 1;
  haste_rate_ = 0;
  last_haste_update_music_seconds_ = 0;
  accumulated_haste_seconds_ = 0;
  tug_life_percent_p1_ = 0.5f;
  FOREACH_PlayerNumber(p) {
    player_state_[p]->super_meter_ = 0;
    player_state_[p]->health_state_ = HealthState_Alive;

    player_state_[p]->last_positive_sum_of_attack_levels_ = 0;
    player_state_[p]->seconds_until_attacks_phased_out_ =
        0;  // PlayerAI not affected

    goal_complete_[p] = false;
  }
  workout_goal_complete_ = false;

  FOREACH_PlayerNumber(p) {
    last_stage_awards_[p].clear();
    last_peak_combo_awards_[p].clear();
  }

  // Reset the round seed. Do this here and not in FinishStage so that players
  // get new shuffle patterns if they Back out of gameplay and play again.
  stage_seed_ = rand();
}

void GameState::UpdateSongPosition(
    float fPositionSeconds, const TimingData& timing,
    const RageTimer& timestamp) {
  /* It's not uncommon to get a lot of duplicated positions from the sound
   * driver, like so: 13.120953,13.130975,13.130975,13.130975,13.140998,...
   * This causes visual stuttering of the arrows. To compensate, keep a
   * RageTimer since the last change. */
  if (fPositionSeconds == last_position_seconds_ && !paused_) {
    fPositionSeconds += last_position_timer_.Ago();
  } else {
    // LOG->Info("Time difference: %+f",
    //	m_LastPositionTimer.Ago() - (fPositionSeconds - m_LastPositionSeconds)
    //);
    last_position_timer_.Touch();
    last_position_seconds_ = fPositionSeconds;
  }

  position_.UpdateSongPosition(fPositionSeconds, timing, timestamp);

  FOREACH_EnabledPlayer(pn) {
    if (cur_steps_[pn]) {
      float fAdditionalVisualDelay =
          player_state_[pn]->player_options_.GetPreferred().visual_delay_;
      player_state_[pn]->position_.UpdateSongPosition(
          fPositionSeconds, *cur_steps_[pn]->GetTimingData(), timestamp,
          fAdditionalVisualDelay);
      Actor::SetPlayerBGMBeat(
          pn, player_state_[pn]->position_.song_beat_visible_,
          player_state_[pn]->position_.song_beat_no_offset_);
    }
  }
  Actor::SetBGMTime(
      GAMESTATE->position_.music_seconds_visible_,
      GAMESTATE->position_.song_beat_visible_, fPositionSeconds,
      GAMESTATE->position_.song_beat_no_offset_);
  //	LOG->Trace( "m_fMusicSeconds = %f, m_fSongBeat = %f, m_fCurBPS = %f,
  // m_bFreeze = %f", m_fMusicSeconds, m_fSongBeat, m_fCurBPS, m_bFreeze );
}

/*
update player position code goes here
 */

float GameState::GetSongPercent(float beat) const {
  // 0 = first step; 1 = last step
  float curTime = this->cur_song_->song_timing_.GetElapsedTimeFromBeat(beat);
  return (curTime - cur_song_->GetFirstSecond()) / cur_song_->GetLastSecond();
}

int GameState::GetNumStagesLeft(PlayerNumber pn) const {
  return player_stage_tokens_[pn];
}

int GameState::GetSmallestNumStagesLeftForAnyHumanPlayer() const {
  if (IsEventMode() || premium_free_active_) {
    return 999;
  }
  int iSmallest = INT_MAX;
  FOREACH_HumanPlayer(p) iSmallest =
      std::min(iSmallest, player_stage_tokens_[p]);
  return iSmallest;
}

bool GameState::IsFinalStageForEveryHumanPlayer() const {
  int song_cost = 1;
  if (cur_song_ != nullptr) {
    if (cur_song_->IsLong()) {
      song_cost = 2;
    } else if (cur_song_->IsMarathon()) {
      song_cost = 3;
    }
  }
  // If we're on gameplay or evaluation, they set this to false because those
  // screens have already had the stage tokens subtracted.
  song_cost *= adjust_tokens_by_song_cost_for_final_stage_check_;
  int num_on_final = 0;
  int num_humans = 0;
  FOREACH_HumanPlayer(p) {
    if (player_stage_tokens_[p] - song_cost <= 0) {
      ++num_on_final;
    }
    ++num_humans;
  }
  return num_on_final >= num_humans;
}

bool GameState::IsAnExtraStage() const {
  if (this->GetMasterPlayerNumber() == PlayerNumber_Invalid) {
    return false;
  }
  return !IsEventMode() && !IsCourseMode() &&
         awarded_extra_stages_[this->GetMasterPlayerNumber()] > 0;
}

static ThemeMetric<bool> LOCK_EXTRA_STAGE_SELECTION(
    "GameState", "LockExtraStageSelection");
bool GameState::IsAnExtraStageAndSelectionLocked() const {
  return IsAnExtraStage() && LOCK_EXTRA_STAGE_SELECTION;
}

bool GameState::IsExtraStage() const {
  if (this->GetMasterPlayerNumber() == PlayerNumber_Invalid) {
    return false;
  }
  return !IsEventMode() && !IsCourseMode() &&
         awarded_extra_stages_[this->GetMasterPlayerNumber()] == 1;
}

bool GameState::IsExtraStage2() const {
  if (this->GetMasterPlayerNumber() == PlayerNumber_Invalid) {
    return false;
  }
  return !IsEventMode() && !IsCourseMode() &&
         awarded_extra_stages_[this->GetMasterPlayerNumber()] == 2;
}

Stage GameState::GetCurrentStage() const {
  if (demonstration_or_jukebox_) {
    return Stage_Demo;
  }
  // "event" has precedence
  else if (IsEventMode()) {
    return Stage_Event;
  } else if (play_mode_ == PLAY_MODE_ONI) {
    return Stage_Oni;
  } else if (play_mode_ == PLAY_MODE_NONSTOP) {
    return Stage_Nonstop;
  } else if (play_mode_ == PLAY_MODE_ENDLESS) {
    return Stage_Endless;
  } else if (IsExtraStage()) {
    return Stage_Extra1;
  } else if (IsExtraStage2()) {
    return Stage_Extra2;
  }
  // Previous logic did not factor in current song length, or the fact that
  // players aren't allowed to start a song with 0 tokens.  This new
  // function also has logic for handling the Gameplay and Evaluation cases
  // which used to require workarounds on the theme side. -Kyz
  else if (IsFinalStageForEveryHumanPlayer()) {
    return Stage_Final;
  } else {
    switch (this->current_stage_index_) {
      case 0:
        return Stage_1st;
      case 1:
        return Stage_2nd;
      case 2:
        return Stage_3rd;
      case 3:
        return Stage_4th;
      case 4:
        return Stage_5th;
      case 5:
        return Stage_6th;
      default:
        return Stage_Next;
    }
  }
}

int GameState::GetCourseSongIndex() const {
  // iSongsPlayed includes the current song, so it's 1-based; subtract one.
  return STATSMAN->cur_stage_stats_.player_[this->GetMasterPlayerNumber()]
             .songs_played_ -
         1;
}

/* Hack: when we're loading a new course song, we want to display the new song
 * number, even though we haven't started that song yet. */
int GameState::GetLoadingCourseSongIndex() const {
  int iIndex = GetCourseSongIndex();
  if (loading_next_song_) {
    ++iIndex;
  }
  return iIndex;
}

void GameState::ActivatePremiumFree() {
  if (premium_free_active_) {
    return;
  }

  premium_free_active_ = true;
  premium_free_minutes_ = g_iPremiumFreeMinutes.Get();
  premium_free_start_time_.SetZero();
}

void GameState::StartPremiumFreeTimer() {
  if (!premium_free_start_time_.IsZero()) {
    return;
  }

  premium_free_start_time_.Touch();
}

float GameState::GetPremiumFreeSecondsLeft() const {
  if (!premium_free_active_) {
    return 0.0f;
  }

  const float seconds = GetPremiumFreeSeconds();
  if (premium_free_start_time_.IsZero()) {
    return seconds;
  }

  return std::max(0.0f, seconds - premium_free_start_time_.Ago());
}

float GameState::GetPremiumFreeSeconds() const {
  return std::clamp(
      static_cast<float>(premium_free_minutes_) * 60.0f, 0.0f, 3600.0f);
}

float GameState::GetPremiumFreeSongGraceSeconds() const {
  return std::clamp(g_fPremiumFreeSongGraceSeconds.Get(), 60.0f, 240.0f);
}

bool GameState::IsPremiumFreeExpired() const {
  return premium_free_active_ && !premium_free_start_time_.IsZero() &&
         GetPremiumFreeSecondsLeft() <= 0.0f;
}

bool GameState::IsSongAllowedByPremiumFree(const Song* song) const {
  if (!premium_free_active_) {
    return true;
  }
  if (song == nullptr) {
    return false;
  }

  return song->music_length_seconds_ <=
         GetPremiumFreeSecondsLeft() + GetPremiumFreeSongGraceSeconds();
}

static const char* prepare_song_failures[] = {
    "success",
    "no_current_song",
    "card_mount_failed",
    "load_interrupted",
};

int GameState::prepare_song_for_gameplay() {
  Song* curr = cur_song_;
  if (curr == nullptr) {
    return 1;
  }
  if (curr->loaded_from_profile_ == ProfileSlot_Invalid) {
    return 0;
  }
  ProfileSlot prof_slot = curr->loaded_from_profile_;
  PlayerNumber slot_as_pn = PlayerNumber(prof_slot);
  if (!PROFILEMAN->ProfileWasLoadedFromMemoryCard(slot_as_pn)) {
    return 0;
  }
  if (!MEMCARDMAN->MountCard(slot_as_pn)) {
    return 2;
  }
  std::string prof_dir = PROFILEMAN->GetProfileDir(prof_slot);
  // Song loading changes its paths to point to the cache area. -Kyz
  std::string to_dir = curr->GetSongDir();
  std::string from_dir = curr->GetPreCustomifyDir();
  // The problem of what files to copy is complicated by steps being able to
  // specify their own music file, and the variety of step file formats.
  // Complex logic to figure out what files the song actually uses would be
  // bug prone.  Just copy all audio files and step files. -Kyz
  std::vector<std::string> copy_exts =
      ActorUtil::GetTypeExtensionList(FT_Sound);
  copy_exts.push_back("sm");
  copy_exts.push_back("ssc");
  copy_exts.push_back("lrc");
  std::vector<std::string> files_in_dir;
  FILEMAN->GetDirListingWithMultipleExtensions(
      from_dir, copy_exts, files_in_dir);
  for (size_t i = 0; i < files_in_dir.size(); ++i) {
    std::string& fname = files_in_dir[i];
    if (!FileCopy(from_dir + fname, to_dir + fname)) {
      MEMCARDMAN->UnmountCard(slot_as_pn);
      return 3;
    }
  }
  MEMCARDMAN->UnmountCard(slot_as_pn);
  return 0;
}

static LocalizedString PLAYER1("GameState", "Player 1");
static LocalizedString PLAYER2("GameState", "Player 2");
static LocalizedString CPU("GameState", "CPU");
std::string GameState::GetPlayerDisplayName(PlayerNumber pn) const {
  ASSERT(IsPlayerEnabled(pn));
  const LocalizedString* pDefaultNames[] = {&PLAYER1, &PLAYER2};
  if (IsHumanPlayer(pn)) {
    if (!PROFILEMAN->GetPlayerName(pn).empty()) {
      return PROFILEMAN->GetPlayerName(pn);
    } else {
      return pDefaultNames[pn]->GetValue();
    }
  } else {
    return CPU.GetValue();
  }
}

bool GameState::PlayersCanJoin() const {
  if (GetNumSidesJoined() == 0) {
    return true;
  }
  // If we check the style and it comes up nullptr, either the style has not
  // been chosen, or we're on ScreenSelectMusic with AutoSetStyle. If the style
  // does not come up nullptr, we might be on a screen in a custom theme that
  // wants to allow joining after the style is set anyway. Either way, we can't
  // use the existence of a style to decide. -Kyz
  if (ALLOW_LATE_JOIN.IsLoaded() && ALLOW_LATE_JOIN) {
    Screen* pScreen = SCREENMAN->GetTopScreen();
    if (pScreen) {
      if (!pScreen->AllowLateJoin()) {
        return false;
      }
    }
    // We can't use FOREACH_EnabledPlayer because that uses PlayersCanJoin
    // in part of its logic chain. -Kyz
    FOREACH_PlayerNumber(pn) {
      const Style* style = GetCurrentStyle(pn);
      if (style) {
        const Style* compat_style =
            GAMEMAN->GetFirstCompatibleStyle(cur_game_, 2, style->steps_type_);
        if (compat_style == nullptr) {
          return false;
        }
      }
    }
    return true;
  }
  return false;
}

int GameState::GetNumSidesJoined() const {
  int iNumSidesJoined = 0;
  FOREACH_PlayerNumber(p) if (side_is_joined_[p])
      iNumSidesJoined++;  // left side, and right side
  return iNumSidesJoined;
}

const Game* GameState::GetCurrentGame() const {
  ASSERT(cur_game_ != nullptr);  // the game must be set before calling this
  return cur_game_;
}

const Style* GameState::GetCurrentStyle(PlayerNumber pn) const {
  if (GetCurrentGame() == nullptr) {
    return nullptr;
  }
  if (!GetCurrentGame()->players_have_separate_styles_) {
    return cur_style_;
  } else {
    if (pn >= NUM_PLAYERS) {
      return separated_styles_[PLAYER_1] == nullptr
                 ? separated_styles_[PLAYER_2]
                 : separated_styles_[PLAYER_1];
    }
    return separated_styles_[pn];
  }
}

void GameState::SetCurrentStyle(const Style* style, PlayerNumber pn) {
  if (!GetCurrentGame()->players_have_separate_styles_) {
    cur_style_.Set(style);
  } else {
    if (pn == PLAYER_INVALID) {
      FOREACH_PlayerNumber(rpn) { separated_styles_[rpn] = style; }
    } else {
      separated_styles_[pn] = style;
    }
  }
  if (INPUTMAPPER) {
    if (GetCurrentStyle(pn) &&
        GetCurrentStyle(pn)->style_type_ == StyleType_OnePlayerTwoSides) {
      // If the other player is joined, unjoin them because this style only
      // allows one player.
      PlayerNumber other_pn = OPPOSITE_PLAYER[this->GetMasterPlayerNumber()];
      if (GetNumSidesJoined() > 1) {
        UnjoinPlayer(other_pn);
      }
      INPUTMAPPER->SetJoinControllers(this->GetMasterPlayerNumber());
    } else {
      INPUTMAPPER->SetJoinControllers(PLAYER_INVALID);
    }
  }
}

bool GameState::SetCompatibleStyle(StepsType stype, PlayerNumber pn) {
  bool style_incompatible = false;
  if (!GetCurrentStyle(pn)) {
    style_incompatible = true;
  } else {
    style_incompatible = stype != GetCurrentStyle(pn)->steps_type_;
  }
  if (CommonMetrics::AUTO_SET_STYLE && style_incompatible) {
    const Style* compatible_style =
        GAMEMAN->GetFirstCompatibleStyle(cur_game_, GetNumSidesJoined(), stype);
    if (!compatible_style) {
      return false;
    }
    SetCurrentStyle(compatible_style, pn);
  }
  return stype == GetCurrentStyle(pn)->steps_type_;
}

bool GameState::IsPlayerEnabled(PlayerNumber pn) const {
  // In rave, all players are present.  Non-human players are CPU controlled.
  switch (play_mode_) {
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      return true;
    default:
      return IsHumanPlayer(pn);
  }
}

bool GameState::IsPlayerEnabled(const PlayerState* pPlayerState) const {
  if (pPlayerState->player_number_ != PLAYER_INVALID) {
    return IsPlayerEnabled(pPlayerState->player_number_);
  }
  return false;
}

int GameState::GetNumPlayersEnabled() const {
  int count = 0;
  FOREACH_EnabledPlayer(pn) count++;
  return count;
}

bool GameState::IsHumanPlayer(PlayerNumber pn) const {
  if (pn == PLAYER_INVALID) {
    return false;
  }

  if (GetCurrentGame()->players_have_separate_styles_) {
    if (GetCurrentStyle(pn) == nullptr)  // no style chosen
    {
      return side_is_joined_[pn];
    } else {
      StyleType type = GetCurrentStyle(pn)->style_type_;
      switch (type) {
        case StyleType_TwoPlayersTwoSides:
        case StyleType_TwoPlayersSharedSides:
          return true;
        case StyleType_OnePlayerOneSide:
        case StyleType_OnePlayerTwoSides:
          return pn == this->GetMasterPlayerNumber();
        default:
          FAIL_M(ssprintf("Invalid style type: %i", type));
      }
    }
  }
  if (GetCurrentStyle(pn) == nullptr)  // no style chosen
  {
    return side_is_joined_[pn];  // only allow input from sides that have
                                 // already joined
  }

  StyleType type = GetCurrentStyle(pn)->style_type_;
  switch (type) {
    case StyleType_TwoPlayersTwoSides:
    case StyleType_TwoPlayersSharedSides:
      return true;
    case StyleType_OnePlayerOneSide:
    case StyleType_OnePlayerTwoSides:
      return pn == this->GetMasterPlayerNumber();
    default:
      FAIL_M(ssprintf("Invalid style type: %i", type));
  }
}

int GameState::GetNumHumanPlayers() const {
  int count = 0;
  FOREACH_HumanPlayer(pn) count++;
  return count;
}

PlayerNumber GameState::GetFirstHumanPlayer() const {
  FOREACH_HumanPlayer(pn) return pn;
  return PLAYER_INVALID;
}

bool GameState::IsCpuPlayer(PlayerNumber pn) const {
  return IsPlayerEnabled(pn) && !IsHumanPlayer(pn);
}

bool GameState::AnyPlayersAreCpu() const {
  FOREACH_CpuPlayer(pn) return true;
  return false;
}

bool GameState::IsCourseMode() const {
  switch (play_mode_) {
    case PLAY_MODE_ONI:
    case PLAY_MODE_NONSTOP:
    case PLAY_MODE_ENDLESS:
      return true;
    default:
      return false;
  }
}

bool GameState::IsBattleMode() const {
  switch (play_mode_) {
    case PLAY_MODE_BATTLE:
      return true;
    default:
      return false;
  }
}

EarnedExtraStage GameState::CalculateEarnedExtraStage() const {
  if (IsEventMode() || premium_free_active_) {
    return EarnedExtraStage_No;
  }

  if (!PREFSMAN->allow_extra_stage_) {
    return EarnedExtraStage_No;
  }

  if (play_mode_ != PLAY_MODE_REGULAR) {
    return EarnedExtraStage_No;
  }

  if (backed_out_of_final_stage_) {
    return EarnedExtraStage_No;
  }

  if (GetSmallestNumStagesLeftForAnyHumanPlayer() > 0) {
    return EarnedExtraStage_No;
  }

  if (awarded_extra_stages_[this->GetMasterPlayerNumber()] >= 2) {
    return EarnedExtraStage_No;
  }

  FOREACH_EnabledPlayer(pn) {
    Difficulty dc = cur_steps_[pn]->GetDifficulty();
    switch (dc) {
      case Difficulty_Edit:
        if (!EDIT_ALLOWED_FOR_EXTRA) {
          continue;  // can't use edit steps
        }
        break;
      default:
        if (dc < MIN_DIFFICULTY_FOR_EXTRA) {
          continue;  // not hard enough!
        }
        break;
    }

    if (IsExtraStage()) {
      if (ALLOW_EXTRA_2 && STATSMAN->cur_stage_stats_.player_[pn].GetGrade() <=
                               GRADE_TIER_FOR_EXTRA_2) {
        return EarnedExtraStage_Extra2;
      }
    } else if (
        STATSMAN->cur_stage_stats_.player_[pn].GetGrade() <=
        GRADE_TIER_FOR_EXTRA_1) {
      return EarnedExtraStage_Extra1;
    }
  }

  return EarnedExtraStage_No;
}

StageResult GameState::GetStageResult(PlayerNumber pn) const {
  switch (play_mode_) {
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      if (std::abs(tug_life_percent_p1_ - 0.5f) < 0.0001f) {
        return RESULT_DRAW;
      }
      switch (pn) {
        case PLAYER_1:
          return (tug_life_percent_p1_ >= 0.5f) ? RESULT_WIN : RESULT_LOSE;
        case PLAYER_2:
          return (tug_life_percent_p1_ < 0.5f) ? RESULT_WIN : RESULT_LOSE;
        default:
          FAIL_M("Invalid player for battle! Aborting...");
      }
    default:
      break;
  }

  StageResult win = RESULT_WIN;
  FOREACH_PlayerNumber(p) {
    if (p == pn) {
      continue;
    }

    // If anyone did just as well, at best it's a draw.
    if (STATSMAN->cur_stage_stats_.player_[p].actual_dance_points_ ==
        STATSMAN->cur_stage_stats_.player_[pn].actual_dance_points_) {
      win = RESULT_DRAW;
    }

    // If anyone did better, we lost.
    if (STATSMAN->cur_stage_stats_.player_[p].actual_dance_points_ >
        STATSMAN->cur_stage_stats_.player_[pn].actual_dance_points_) {
      return RESULT_LOSE;
    }
  }
  return win;
}

void GameState::GetDefaultPlayerOptions(PlayerOptions& po) {
  po.Init();
  po.FromString(PREFSMAN->default_modifiers_);
  po.FromString(CommonMetrics::DEFAULT_MODIFIERS);
  if (po.note_skin_.empty()) {
    po.note_skin_ = CommonMetrics::DEFAULT_NOTESKIN_NAME;
  }
}

void GameState::GetDefaultSongOptions(SongOptions& so) {
  so.Init();
  so.FromString(PREFSMAN->default_modifiers_);
  so.FromString(CommonMetrics::DEFAULT_MODIFIERS);
}

void GameState::ResetToDefaultSongOptions(ModsLevel l) {
  SongOptions so;
  GetDefaultSongOptions(so);
  song_options_.Assign(l, so);
}

void GameState::ApplyPreferredModifiers(
    PlayerNumber pn, std::string sModifiers) {
  player_state_[pn]->player_options_.FromString(
      ModsLevel_Preferred, sModifiers);
  song_options_.FromString(ModsLevel_Preferred, sModifiers);
}

void GameState::ApplyStageModifiers(PlayerNumber pn, std::string sModifiers) {
  player_state_[pn]->player_options_.FromString(ModsLevel_Stage, sModifiers);
  song_options_.FromString(ModsLevel_Stage, sModifiers);
}

void GameState::ClearStageModifiersIllegalForCourse() {
  FOREACH_EnabledPlayer(pn) PO_GROUP_CALL(
      player_state_[pn]->player_options_, ModsLevel_Stage,
      ResetSavedPrefsInvalidForCourse);
}

bool GameState::CurrentOptionsDisqualifyPlayer(PlayerNumber pn) {
  if (!PREFSMAN->disqualification_) {
    return false;
  }

  if (!IsHumanPlayer(pn)) {
    return false;
  }

  const PlayerOptions& po = player_state_[pn]->player_options_.GetPreferred();
  const SongOptions& so = song_options_.GetPreferred();

  // Playing a song/course at a slower music rate should disqualify score.
  // -x0rbl
  if (so.music_rate_ < 1.0) {
    return true;
  }

  // Check the stored player options for disqualify.  Don't disqualify because
  // of mods that were forced.
  if (IsCourseMode()) {
    return po.IsEasierForCourseAndTrail(cur_course_, cur_trail_[pn]);
  } else {
    return po.IsEasierForSongAndSteps(cur_song_, cur_steps_[pn], pn);
  }
}

/* reset noteskins (?)
 * GameState::ResetNoteSkins()
 * GameState::ResetNoteSkinsForPlayer( PlayerNumber pn )
 *
 */

void GameState::GetAllUsedNoteSkins(std::vector<std::string>& out) const {
  FOREACH_EnabledPlayer(pn) {
    out.push_back(player_state_[pn]->player_options_.GetCurrent().note_skin_);

    // Add noteskins that are used in courses.
    if (IsCourseMode()) {
      const Trail* pTrail = cur_trail_[pn];
      ASSERT(pTrail != nullptr);

      for (const TrailEntry& e : pTrail->entries_) {
        PlayerOptions po;
        po.FromString(e.modifiers_);
        if (!po.note_skin_.empty()) {
          out.push_back(po.note_skin_);
        }
      }
    }
  }

  // Remove duplicates.
  sort(out.begin(), out.end());
  out.erase(unique(out.begin(), out.end()), out.end());
}

void GameState::RemoveAllActiveAttacks()  // called on end of song
{
  FOREACH_PlayerNumber(p) player_state_[p]->RemoveActiveAttacks();
}

void GameState::AddStageToPlayer(PlayerNumber pn) {
  // Add one stage more to player (bonus) -cerbo
  ++player_stage_tokens_[pn];
}

template <class T>
void setmax(T& a, const T& b) {
  a = std::max(a, b);
}

FailType GameState::GetPlayerFailType(const PlayerState* pPlayerState) const {
  PlayerNumber pn = pPlayerState->player_number_;
  FailType ft = pPlayerState->player_options_.GetCurrent().fail_type_;

  // If the player changed the fail mode explicitly, leave it alone.
  if (fail_type_was_explicitly_set_) {
    return ft;
  }

  if (IsCourseMode()) {
    if (PREFSMAN->minimum1_full_song_in_courses_ && GetCourseSongIndex() == 0) {
      ft = std::max(ft, FailType_ImmediateContinue);  // take the least harsh of
                                                      // the two FailTypes
    }
  } else {
    Difficulty dc = Difficulty_Invalid;
    if (cur_steps_[pn]) {
      dc = cur_steps_[pn]->GetDifficulty();
    }

    bool bFirstStage = false;
    if (!IsEventMode()) {
      bFirstStage |= player_stage_tokens_[pPlayerState->player_number_] ==
                     PREFSMAN->songs_per_play_ -
                         1;  // HACK; -1 because this is called during gameplay
    }

    // Easy and beginner are never harder than FAIL_IMMEDIATE_CONTINUE.
    if (dc <= Difficulty_Easy) {
      setmax(ft, FailType_ImmediateContinue);
    }

    if (dc <= Difficulty_Easy && bFirstStage &&
        PREFSMAN->fail_off_for_first_stage_easy_) {
      setmax(ft, FailType_Off);
    }

    /* If beginner's steps were chosen, and this is the first stage,
     * turn off failure completely. */
    if (dc == Difficulty_Beginner && bFirstStage) {
      setmax(ft, FailType_Off);
    }

    if (dc == Difficulty_Beginner && PREFSMAN->fail_off_in_beginner_) {
      setmax(ft, FailType_Off);
    }
  }

  return ft;
}

bool GameState::ShowW1() const {
  AllowW1 pref = PREFSMAN->allow_w1_;
  switch (pref) {
    case ALLOW_W1_NEVER:
      return false;
    case ALLOW_W1_COURSES_ONLY:
      return IsCourseMode();
    case ALLOW_W1_EVERYWHERE:
      return true;
    default:
      FAIL_M(ssprintf("Invalid AllowW1 preference: %i", pref));
  }
}

static ThemeMetric<bool> PROFILE_RECORD_FEATS(
    "GameState", "ProfileRecordFeats");
static ThemeMetric<bool> CATEGORY_RECORD_FEATS(
    "GameState", "CategoryRecordFeats");
void GameState::GetRankingFeats(
    PlayerNumber pn, std::vector<RankingFeat>& asFeatsOut) const {
  if (!IsHumanPlayer(pn)) {
    return;
  }

  Profile* pProf = PROFILEMAN->GetProfile(pn);

  // Check for feats even if the PlayMode is rave or battle because the player
  // may have made high scores then switched modes.
  PlayMode mode = play_mode_.Get();
  const char* modeStr = PlayModeToString(mode).c_str();

  CHECKPOINT_M(ssprintf("Getting the feats for %s", modeStr));
  switch (mode) {
    case PLAY_MODE_REGULAR:
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE: {
      StepsType st = GetCurrentStyle(pn)->steps_type_;

      // Find unique Song and Steps combinations that were played.
      // We must keep only the unique combination or else we'll double-count
      // high score markers.
      std::vector<SongAndSteps> vSongAndSteps;

      for (unsigned i = 0; i < STATSMAN->played_stage_stats_.size(); i++) {
        CHECKPOINT_M(
            ssprintf("%u/%i", i, (int)STATSMAN->played_stage_stats_.size()));
        SongAndSteps sas;
        ASSERT(!STATSMAN->played_stage_stats_[i].played_songs_.empty());
        sas.song_ = STATSMAN->played_stage_stats_[i].played_songs_[0];
        ASSERT(sas.song_ != nullptr);
        if (STATSMAN->played_stage_stats_[i]
                .player_[pn]
                .possible_steps_.empty()) {
          continue;
        }
        sas.steps_ =
            STATSMAN->played_stage_stats_[i].player_[pn].possible_steps_[0];
        ASSERT(sas.steps_ != nullptr);
        vSongAndSteps.push_back(sas);
      }
      CHECKPOINT_M(ssprintf("All songs/steps from %s gathered", modeStr));

      sort(vSongAndSteps.begin(), vSongAndSteps.end());

      std::vector<SongAndSteps>::iterator toDelete =
          unique(vSongAndSteps.begin(), vSongAndSteps.end());
      vSongAndSteps.erase(toDelete, vSongAndSteps.end());

      CHECKPOINT_M("About to find records from the gathered.");
      for (unsigned i = 0; i < vSongAndSteps.size(); i++) {
        Song* pSong = vSongAndSteps[i].song_;
        Steps* pSteps = vSongAndSteps[i].steps_;

        // Find Machine Records
        {
          HighScoreList& hsl =
              PROFILEMAN->GetMachineProfile()->GetStepsHighScoreList(
                  pSong, pSteps);
          for (unsigned j = 0; j < hsl.high_scores_.size(); j++) {
            HighScore& hs = hsl.high_scores_[j];

            if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
              continue;
            }

            RankingFeat feat;
            feat.type_ = RankingFeat::SONG;
            feat.song_ = pSong;
            feat.steps_ = pSteps;
            feat.feat_ = ssprintf(
                "MR #%d in %s %s", j + 1, pSong->GetTranslitMainTitle().c_str(),
                DifficultyToString(pSteps->GetDifficulty()).c_str());
            feat.string_to_fill_ = hs.GetNameMutable();
            feat.grade_ = hs.GetGrade();
            feat.percent_dp_ = hs.GetPercentDP();
            feat.score_ = hs.GetScore();

            if (pSong->HasBanner()) {
              feat.banner_ = pSong->GetBannerPath();
            }

            asFeatsOut.push_back(feat);
          }
        }

        // Find Personal Records
        if (pProf && PROFILE_RECORD_FEATS) {
          HighScoreList& hsl = pProf->GetStepsHighScoreList(pSong, pSteps);
          for (unsigned j = 0; j < hsl.high_scores_.size(); j++) {
            HighScore& hs = hsl.high_scores_[j];

            if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
              continue;
            }

            RankingFeat feat;
            feat.song_ = pSong;
            feat.steps_ = pSteps;
            feat.type_ = RankingFeat::SONG;
            feat.feat_ = ssprintf(
                "PR #%d in %s %s", j + 1, pSong->GetTranslitMainTitle().c_str(),
                DifficultyToString(pSteps->GetDifficulty()).c_str());
            feat.string_to_fill_ = hs.GetNameMutable();
            feat.grade_ = hs.GetGrade();
            feat.percent_dp_ = hs.GetPercentDP();
            feat.score_ = hs.GetScore();

            // XXX: temporary hack
            // Why is this here? -aj
            if (pSong->HasBackground()) {
              feat.banner_ = pSong->GetBackgroundPath();
            }
            //					if( pSong->HasBanner() )
            //						feat.Banner =
            // pSong->GetBannerPath();

            asFeatsOut.push_back(feat);
          }
        }
      }

      CHECKPOINT_M("Getting the final evaluation stage stats.");
      StageStats stats;
      STATSMAN->GetFinalEvalStageStats(stats);

      // Find Machine Category Records
      FOREACH_ENUM(RankingCategory, rc) {
        if (!CATEGORY_RECORD_FEATS) {
          continue;
        }
        HighScoreList& hsl =
            PROFILEMAN->GetMachineProfile()->GetCategoryHighScoreList(st, rc);
        for (unsigned j = 0; j < hsl.high_scores_.size(); j++) {
          HighScore& hs = hsl.high_scores_[j];
          if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
            continue;
          }

          RankingFeat feat;
          feat.type_ = RankingFeat::CATEGORY;
          feat.feat_ = ssprintf(
              "MR #%d in Type %c (%d)", j + 1, 'A' + rc,
              stats.GetAverageMeter(pn));
          feat.string_to_fill_ = hs.GetNameMutable();
          feat.grade_ = Grade_NoData;
          feat.score_ = hs.GetScore();
          feat.percent_dp_ = hs.GetPercentDP();
          asFeatsOut.push_back(feat);
        }
      }

      // Find Personal Category Records
      FOREACH_ENUM(RankingCategory, rc) {
        if (!CATEGORY_RECORD_FEATS) {
          continue;
        }

        if (pProf && PROFILE_RECORD_FEATS) {
          HighScoreList& hsl = pProf->GetCategoryHighScoreList(st, rc);
          for (unsigned j = 0; j < hsl.high_scores_.size(); j++) {
            HighScore& hs = hsl.high_scores_[j];
            if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
              continue;
            }

            RankingFeat feat;
            feat.type_ = RankingFeat::CATEGORY;
            feat.feat_ = ssprintf(
                "PR #%d in Type %c (%d)", j + 1, 'A' + rc,
                stats.GetAverageMeter(pn));
            feat.string_to_fill_ = hs.GetNameMutable();
            feat.grade_ = Grade_NoData;
            feat.score_ = hs.GetScore();
            feat.percent_dp_ = hs.GetPercentDP();
            asFeatsOut.push_back(feat);
          }
        }
      }
    } break;
    case PLAY_MODE_NONSTOP:
    case PLAY_MODE_ONI:
    case PLAY_MODE_ENDLESS: {
      Course* pCourse = cur_course_;
      ASSERT(pCourse != nullptr);
      Trail* pTrail = cur_trail_[pn];
      ASSERT(pTrail != nullptr);
      CourseDifficulty cd = pTrail->course_difficulty_;

      // Find Machine Records
      {
        Profile* pProfile = PROFILEMAN->GetMachineProfile();
        HighScoreList& hsl = pProfile->GetCourseHighScoreList(pCourse, pTrail);
        for (unsigned i = 0; i < hsl.high_scores_.size(); i++) {
          HighScore& hs = hsl.high_scores_[i];
          if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
            continue;
          }

          RankingFeat feat;
          feat.type_ = RankingFeat::COURSE;
          feat.course_ = pCourse;
          feat.feat_ = ssprintf(
              "MR #%d in %s", i + 1, pCourse->GetDisplayFullTitle().c_str());
          if (cd != Difficulty_Medium) {
            feat.feat_ += " " + CourseDifficultyToLocalizedString(cd);
          }
          feat.string_to_fill_ = hs.GetNameMutable();
          feat.grade_ = Grade_NoData;
          feat.score_ = hs.GetScore();
          feat.percent_dp_ = hs.GetPercentDP();
          if (pCourse->HasBanner()) {
            feat.banner_ = pCourse->GetBannerPath();
          }
          asFeatsOut.push_back(feat);
        }
      }

      // Find Personal Records
      if (PROFILE_RECORD_FEATS && PROFILEMAN->IsPersistentProfile(pn)) {
        HighScoreList& hsl = pProf->GetCourseHighScoreList(pCourse, pTrail);
        for (unsigned i = 0; i < hsl.high_scores_.size(); i++) {
          HighScore& hs = hsl.high_scores_[i];
          if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
            continue;
          }

          RankingFeat feat;
          feat.type_ = RankingFeat::COURSE;
          feat.course_ = pCourse;
          feat.feat_ = ssprintf(
              "PR #%d in %s", i + 1, pCourse->GetDisplayFullTitle().c_str());
          feat.string_to_fill_ = hs.GetNameMutable();
          feat.grade_ = Grade_NoData;
          feat.score_ = hs.GetScore();
          feat.percent_dp_ = hs.GetPercentDP();
          if (pCourse->HasBanner()) {
            feat.banner_ = pCourse->GetBannerPath();
          }
          asFeatsOut.push_back(feat);
        }
      }
    } break;
    default:
      FAIL_M(ssprintf("Invalid play mode: %i", int(play_mode_)));
  }
}

bool GameState::AnyPlayerHasRankingFeats() const {
  std::vector<RankingFeat> vFeats;
  FOREACH_PlayerNumber(p) {
    GetRankingFeats(p, vFeats);
    if (!vFeats.empty()) {
      return true;
    }
  }
  return false;
}

void GameState::StoreRankingName(PlayerNumber pn, std::string sName) {
  // The theme can upper it if desired. -Kyz
  // sName.MakeUpper();

  if (USE_NAME_BLACKLIST) {
    RageFile file;
    if (file.Open(NAME_BLACKLIST_FILE)) {
      std::string sLine;

      while (!file.AtEOF()) {
        if (file.GetLine(sLine) == -1) {
          LuaHelpers::ReportScriptErrorFmt(
              "Error reading \"%s\": %s", NAME_BLACKLIST_FILE,
              file.GetError().c_str());
          break;
        }

        MakeUpper(sLine);
        if (!sLine.empty() &&
            sName.find(sLine) != std::string::npos)  // name contains a bad word
        {
          LOG->Trace(
              "entered '%s' matches blacklisted item '%s'", sName.c_str(),
              sLine.c_str());
          sName = "";
          break;
        }
      }
    }
  }

  std::vector<RankingFeat> aFeats;
  GetRankingFeats(pn, aFeats);

  for (unsigned i = 0; i < aFeats.size(); i++) {
    *aFeats[i].string_to_fill_ = sName;

    // save name pointers as we fill them
    names_that_were_filled_.push_back(aFeats[i].string_to_fill_);
  }

  players_that_were_filled_.insert(pn);

  // Only attempt to remove/clamp scores after the last enabled player has saved
  // their scores.
  if (GAMESTATE->GetNumPlayersEnabled() <=
      static_cast<int>(players_that_were_filled_.size())) {
    StepsType st = GetCurrentStyle(pn)->steps_type_;
    PlayMode mode = play_mode_.Get();
    Profile* pProfile = PROFILEMAN->GetMachineProfile();
    switch (mode) {
      case PLAY_MODE_REGULAR:
      case PLAY_MODE_BATTLE:
      case PLAY_MODE_RAVE: {
        // Find unique Song and Steps combinations that were played.
        // Code is taken from implementation in GetRankingFeats()
        std::vector<SongAndSteps> vSongAndSteps;

        for (unsigned i = 0; i < STATSMAN->played_stage_stats_.size(); i++) {
          CHECKPOINT_M(
              ssprintf("%u/%i", i, (int)STATSMAN->played_stage_stats_.size()));
          SongAndSteps sas;
          ASSERT(!STATSMAN->played_stage_stats_[i].played_songs_.empty());
          sas.song_ = STATSMAN->played_stage_stats_[i].played_songs_[0];
          ASSERT(sas.song_ != nullptr);

          if (STATSMAN->played_stage_stats_[i]
                  .player_[pn]
                  .possible_steps_.empty()) {
            continue;
          }
          sas.steps_ =
              STATSMAN->played_stage_stats_[i].player_[pn].possible_steps_[0];
          ASSERT(sas.steps_ != nullptr);
          vSongAndSteps.push_back(sas);
        }

        std::vector<SongAndSteps>::iterator toDelete =
            std::unique(vSongAndSteps.begin(), vSongAndSteps.end());
        vSongAndSteps.erase(toDelete, vSongAndSteps.end());

        for (unsigned i = 0; i < vSongAndSteps.size(); ++i) {
          HighScoreList& hsl = pProfile->GetStepsHighScoreList(
              vSongAndSteps[i].song_, vSongAndSteps[i].steps_);
          if (!PREFSMAN->allow_multiple_high_score_with_same_name_) {
            // erase all but the highest score for each name
            hsl.RemoveAllButOneOfEachName();
          }
          hsl.ClampSize(true);
        }
      } break;
      case PLAY_MODE_NONSTOP:
      case PLAY_MODE_ONI:
      case PLAY_MODE_ENDLESS: {
        // Code is taken from implementation in GetRankingFeats()
        Course* pCourse = cur_course_;
        ASSERT(pCourse != nullptr);
        Trail* pTrail = cur_trail_[pn];
        ASSERT(pTrail != nullptr);
        CourseDifficulty cd = pTrail->course_difficulty_;
        HighScoreList& hsl = pProfile->GetCourseHighScoreList(pCourse, pTrail);
        if (!PREFSMAN->allow_multiple_high_score_with_same_name_) {
          // erase all but the highest score for each name
          hsl.RemoveAllButOneOfEachName();
        }
        hsl.ClampSize(true);
      } break;
      default:
        FAIL_M(ssprintf("Invalid play mode: %i", int(play_mode_)));
    }
  }
}

bool GameState::AllAreInDangerOrWorse() const {
  FOREACH_EnabledPlayer(
      p) if (player_state_[p]->health_state_ < HealthState_Danger) return false;
  return true;
}

bool GameState::OneIsHot() const {
  FOREACH_EnabledPlayer(
      p) if (player_state_[p]->health_state_ == HealthState_Hot) return true;
  return false;
}

bool GameState::IsTimeToPlayAttractSounds() const {
  // m_iNumTimesThroughAttract will be -1 from the first attract screen after
  // the end of a game until the next time FIRST_ATTRACT_SCREEN is reached.
  // Play attract sounds for this sort span of time regardless of
  // m_AttractSoundFrequency because it's awkward to have the machine go
  // silent immediately after the end of a game.
  if (num_times_through_attract_ == -1) {
    return true;
  }

  if (PREFSMAN->attract_sound_frequency_ == ASF_NEVER) {
    return false;
  }

  // play attract sounds once every m_iAttractSoundFrequency times through
  if ((num_times_through_attract_ % PREFSMAN->attract_sound_frequency_) == 0) {
    return true;
  }

  return false;
}

void GameState::VisitAttractScreen(const std::string sScreenName) {
  if (sScreenName == CommonMetrics::FIRST_ATTRACT_SCREEN.GetValue()) {
    num_times_through_attract_++;
  }
}

bool GameState::DifficultiesLocked() const {
  if (play_mode_ == PLAY_MODE_RAVE) {
    return true;
  }
  if (IsCourseMode()) {
    return PREFSMAN->lock_course_difficulties_;
  }
  if (GetCurrentStyle(PLAYER_INVALID)->lock_difficulties_) {
    return true;
  }
  return false;
}

bool GameState::ChangePreferredDifficultyAndStepsType(
    PlayerNumber pn, Difficulty dc, StepsType st) {
  preferred_difficulty_[pn].Set(dc);
  preferred_steps_type_.Set(st);
  if (DifficultiesLocked()) {
    FOREACH_PlayerNumber(p) if (p != pn) preferred_difficulty_[p].Set(
        preferred_difficulty_[pn]);
  }

  return true;
}

/* When only displaying difficulties in DIFFICULTIES_TO_SHOW, use
 * GetClosestShownDifficulty to find which difficulty to show, and
 * ChangePreferredDifficulty(pn, dir) to change difficulty. */
bool GameState::ChangePreferredDifficulty(PlayerNumber pn, int dir) {
  const std::vector<Difficulty>& v =
      CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue();

  Difficulty d = GetClosestShownDifficulty(pn);
  for (;;) {
    d = enum_add2(d, dir);
    if (d < 0 || d >= NUM_Difficulty) {
      return false;
    }
    if (find(v.begin(), v.end(), d) != v.end()) {
      break;  // found
    }
  }
  preferred_difficulty_[pn].Set(d);
  return true;
}

/* The user may be set to prefer a difficulty that isn't always shown;
 * typically, Difficulty_Edit. Return the closest shown difficulty <=
 * m_PreferredDifficulty. */
Difficulty GameState::GetClosestShownDifficulty(PlayerNumber pn) const {
  const std::vector<Difficulty>& v =
      CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue();

  Difficulty iClosest = (Difficulty)0;
  int iClosestDist = -1;
  for (const Difficulty& dc : v) {
    int iDist = preferred_difficulty_[pn] - dc;
    if (iDist < 0) {
      continue;
    }
    if (iClosestDist != -1 && iDist > iClosestDist) {
      continue;
    }
    iClosestDist = iDist;
    iClosest = dc;
  }

  return iClosest;
}

bool GameState::ChangePreferredCourseDifficultyAndStepsType(
    PlayerNumber pn, CourseDifficulty cd, StepsType st) {
  preferred_course_difficulty_[pn].Set(cd);
  preferred_steps_type_.Set(st);
  if (PREFSMAN->lock_course_difficulties_) {
    FOREACH_PlayerNumber(p) if (p != pn) preferred_course_difficulty_[p].Set(
        preferred_course_difficulty_[pn]);
  }

  return true;
}

bool GameState::ChangePreferredCourseDifficulty(PlayerNumber pn, int dir) {
  /* If we have a course selected, only choose among difficulties available in
   * the course. */
  const Course* pCourse = cur_course_;

  const std::vector<CourseDifficulty>& v =
      CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue();

  CourseDifficulty cd = preferred_course_difficulty_[pn];
  for (;;) {
    cd = enum_add2(cd, dir);
    if (cd < 0 || cd >= NUM_Difficulty) {
      return false;
    }
    if (find(v.begin(), v.end(), cd) == v.end()) {
      continue; /* not available */
    }
    if (!pCourse || pCourse->GetTrail(GetCurrentStyle(pn)->steps_type_, cd)) {
      break;
    }
  }
  return ChangePreferredCourseDifficulty(pn, cd);
}

bool GameState::IsCourseDifficultyShown(CourseDifficulty cd) {
  const std::vector<CourseDifficulty>& v =
      CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue();
  return find(v.begin(), v.end(), cd) != v.end();
}

Difficulty GameState::GetEasiestStepsDifficulty() const {
  Difficulty dc = Difficulty_Invalid;
  FOREACH_HumanPlayer(p) {
    if (cur_steps_[p] == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "GetEasiestStepsDifficulty called but p%i hasn't chosen notes",
          p + 1);
      continue;
    }
    dc = std::min(dc, cur_steps_[p]->GetDifficulty());
  }
  return dc;
}

Difficulty GameState::GetHardestStepsDifficulty() const {
  Difficulty dc = Difficulty_Beginner;
  FOREACH_HumanPlayer(p) {
    if (cur_steps_[p] == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "GetHardestStepsDifficulty called but p%i hasn't chosen notes",
          p + 1);
      continue;
    }
    dc = std::max(dc, cur_steps_[p]->GetDifficulty());
  }
  return dc;
}

void GameState::SetNewStageSeed() { stage_seed_ = rand(); }

bool GameState::IsEventMode() const {
  return temporary_event_mode_ || PREFSMAN->event_mode_;
}

CoinMode GameState::GetCoinMode() const {
  // XXX: Event mode shouldn't be valid if CoinMode_Pay
  if (IsEventMode() && GamePreferences::m_CoinMode == CoinMode_Pay) {
    return CoinMode_Free;
  } else {
    return GamePreferences::m_CoinMode;
  }
}

ThemeMetric<bool> DISABLE_PREMIUM_IN_EVENT_MODE(
    "GameState", "DisablePremiumInEventMode");
Premium GameState::GetPremium() const {
  return DISABLE_PREMIUM_IN_EVENT_MODE ? Premium_Off : g_Premium;
}

float GameState::GetGoalPercentComplete(PlayerNumber pn) {
  const Profile* pProfile = PROFILEMAN->GetProfile(pn);
  const StageStats& ssCurrent = STATSMAN->cur_stage_stats_;
  const PlayerStageStats& pssCurrent = ssCurrent.player_[pn];

  float fActual = 0;
  float fGoal = 0;
  switch (pProfile->goal_type_) {
    case GoalType_Calories:
      fActual = pssCurrent.calories_burned_;
      fGoal = (float)pProfile->goal_calories_;
      break;
    case GoalType_Time:
      fActual = ssCurrent.gameplay_seconds_;
      fGoal = (float)pProfile->goal_seconds_;
      break;
    case GoalType_None:
      return 0;  // never complete
    default:
      FAIL_M(ssprintf("Invalid GoalType: %i", pProfile->goal_type_));
  }
  if (fGoal == 0) {
    return 0;
  } else {
    return fActual / fGoal;
  }
}

bool GameState::PlayerIsUsingModifier(
    PlayerNumber pn, const std::string& sModifier) {
  PlayerOptions po = player_state_[pn]->player_options_.GetCurrent();
  SongOptions so = song_options_.GetCurrent();
  po.FromString(sModifier);
  so.FromString(sModifier);

  return po == player_state_[pn]->player_options_.GetCurrent() &&
         so == song_options_.GetCurrent();
}

Profile* GameState::GetEditLocalProfile() {
  if (edit_local_profile_id_.Get().empty()) {
    return nullptr;
  }
  return PROFILEMAN->GetLocalProfile(edit_local_profile_id_);
}

PlayerNumber GetNextHumanPlayer(PlayerNumber pn) {
  for (enum_add(pn, 1); pn < NUM_PLAYERS; enum_add(pn, 1)) {
    if (GAMESTATE->IsHumanPlayer(pn)) {
      return pn;
    }
  }
  return PLAYER_INVALID;
}

PlayerNumber GetNextEnabledPlayer(PlayerNumber pn) {
  for (enum_add(pn, 1); pn < NUM_PLAYERS; enum_add(pn, 1)) {
    if (GAMESTATE->IsPlayerEnabled(pn)) {
      return pn;
    }
  }
  return PLAYER_INVALID;
}

PlayerNumber GetNextCpuPlayer(PlayerNumber pn) {
  for (enum_add(pn, 1); pn < NUM_PLAYERS; enum_add(pn, 1)) {
    if (GAMESTATE->IsCpuPlayer(pn)) {
      return pn;
    }
  }
  return PLAYER_INVALID;
}

PlayerNumber GetNextPotentialCpuPlayer(PlayerNumber pn) {
  for (enum_add(pn, 1); pn < NUM_PLAYERS; enum_add(pn, 1)) {
    if (!GAMESTATE->IsHumanPlayer(pn)) {
      return pn;
    }
  }
  return PLAYER_INVALID;
}

// lua start
#include "Game.h"
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the GameState. */
class LunaGameState : public Luna<GameState> {
 public:
  DEFINE_METHOD(
      IsPlayerEnabled, IsPlayerEnabled(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(IsHumanPlayer, IsHumanPlayer(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(
      GetPlayerDisplayName,
      GetPlayerDisplayName(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(GetMasterPlayerNumber, GetMasterPlayerNumber())
  DEFINE_METHOD(InStepEditor, in_step_editor_);
  DEFINE_METHOD(ShowW1, ShowW1())
  static int GetPlayerState(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    p->player_state_[pn]->PushSelf(L);
    return 1;
  }
  static int ApplyGameCommand(T* p, lua_State* L) {
    PlayerNumber pn = PLAYER_INVALID;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      // Legacy behavior: if an old-style numerical argument
      // is given, decrement it before trying to parse
      if (lua_isnumber(L, 2)) {
        int arg = (int)lua_tonumber(L, 2);
        arg--;
        LuaHelpers::Push(L, arg);
        lua_replace(L, -2);
      }
      pn = Enum::Check<PlayerNumber>(L, 2);
    }
    p->ApplyGameCommand(SArg(1), pn);
    COMMON_RETURN_SELF;
  }
  static int GetCurrentSong(T* p, lua_State* L) {
    if (p->cur_song_) {
      p->cur_song_->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetCurrentSong(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->cur_song_.Set(nullptr);
    } else {
      Song* pS = Luna<Song>::check(L, 1, true);
      p->cur_song_.Set(pS);
    }
    COMMON_RETURN_SELF;
  }
  static int CanSafelyEnterGameplay(T* p, lua_State* L) {
    std::string reason;
    bool can = p->CanSafelyEnterGameplay(reason);
    lua_pushboolean(L, can);
    LuaHelpers::Push(L, reason);
    return 2;
  }
  static void SetCompatibleStyleOrError(
      T* p, lua_State* L, StepsType stype, PlayerNumber pn) {
    if (!p->SetCompatibleStyle(stype, pn)) {
      luaL_error(L, "No compatible style for steps/trail.");
    }
    if (!p->GetCurrentStyle(pn)) {
      luaL_error(
          L, "No style set and AutoSetStyle is false, cannot set steps/trail.");
    }
  }
  static int GetCurrentSteps(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    Steps* pSteps = p->cur_steps_[pn];
    if (pSteps) {
      pSteps->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetCurrentSteps(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    if (lua_isnil(L, 2)) {
      p->cur_steps_[pn].Set(nullptr);
    } else {
      Steps* pS = Luna<Steps>::check(L, 2);
      SetCompatibleStyleOrError(p, L, pS->steps_type_, pn);
      p->cur_steps_[pn].Set(pS);
      p->ForceOtherPlayersToCompatibleSteps(pn);
    }
    COMMON_RETURN_SELF;
  }
  static int GetCurrentCourse(T* p, lua_State* L) {
    if (p->cur_course_) {
      p->cur_course_->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetCurrentCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->cur_course_.Set(nullptr);
    } else {
      Course* pC = Luna<Course>::check(L, 1);
      p->cur_course_.Set(pC);
    }
    COMMON_RETURN_SELF;
  }
  static int GetCurrentTrail(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    Trail* pTrail = p->cur_trail_[pn];
    if (pTrail) {
      pTrail->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetCurrentTrail(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    if (lua_isnil(L, 2)) {
      p->cur_trail_[pn].Set(nullptr);
    } else {
      Trail* pS = Luna<Trail>::check(L, 2);
      SetCompatibleStyleOrError(p, L, pS->steps_type_, pn);
      p->cur_trail_[pn].Set(pS);
      p->ForceOtherPlayersToCompatibleSteps(pn);
    }
    COMMON_RETURN_SELF;
  }
  static int GetPreferredSong(T* p, lua_State* L) {
    if (p->preferred_song_) {
      p->preferred_song_->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetPreferredSong(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->preferred_song_ = nullptr;
    } else {
      Song* pS = Luna<Song>::check(L, 1);
      p->preferred_song_ = pS;
    }
    COMMON_RETURN_SELF;
  }
  static int SetTemporaryEventMode(T* p, lua_State* L) {
    p->temporary_event_mode_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int Env(T* p, lua_State* L) {
    p->environment_->PushSelf(L);
    return 1;
  }
  static int GetEditSourceSteps(T* p, lua_State* L) {
    Steps* pSteps = p->edit_source_steps_;
    if (pSteps) {
      pSteps->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetPreferredDifficulty(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    Difficulty dc = Enum::Check<Difficulty>(L, 2);
    p->preferred_difficulty_[pn].Set(dc);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(
      GetPreferredDifficulty,
      preferred_difficulty_[Enum::Check<PlayerNumber>(L, 1)])
  DEFINE_METHOD(AnyPlayerHasRankingFeats, AnyPlayerHasRankingFeats())
  DEFINE_METHOD(IsCourseMode, IsCourseMode())
  DEFINE_METHOD(IsBattleMode, IsBattleMode())
  DEFINE_METHOD(IsDemonstration, demonstration_or_jukebox_)
  DEFINE_METHOD(GetPlayMode, play_mode_)
  DEFINE_METHOD(GetSortOrder, sort_order_)
  DEFINE_METHOD(GetCurrentStageIndex, current_stage_index_)
  DEFINE_METHOD(IsGoalComplete, IsGoalComplete(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(
      PlayerIsUsingModifier,
      PlayerIsUsingModifier(Enum::Check<PlayerNumber>(L, 1), SArg(2)))
  DEFINE_METHOD(GetCourseSongIndex, GetCourseSongIndex())
  DEFINE_METHOD(GetLoadingCourseSongIndex, GetLoadingCourseSongIndex())
  DEFINE_METHOD(IsPremiumFreeActive, IsPremiumFreeActive())
  DEFINE_METHOD(GetPremiumFreeSecondsLeft, GetPremiumFreeSecondsLeft())
  DEFINE_METHOD(IsPremiumFreeExpired, IsPremiumFreeExpired())
  DEFINE_METHOD(
      GetSmallestNumStagesLeftForAnyHumanPlayer,
      GetSmallestNumStagesLeftForAnyHumanPlayer())
  DEFINE_METHOD(IsAnExtraStage, IsAnExtraStage())
  DEFINE_METHOD(IsExtraStage, IsExtraStage())
  DEFINE_METHOD(IsExtraStage2, IsExtraStage2())
  DEFINE_METHOD(GetCurrentStage, GetCurrentStage())
  DEFINE_METHOD(HasEarnedExtraStage, HasEarnedExtraStage())
  DEFINE_METHOD(GetEarnedExtraStage, GetEarnedExtraStage())
  DEFINE_METHOD(GetEasiestStepsDifficulty, GetEasiestStepsDifficulty())
  DEFINE_METHOD(GetHardestStepsDifficulty, GetHardestStepsDifficulty())
  DEFINE_METHOD(IsEventMode, IsEventMode())
  DEFINE_METHOD(GetNumPlayersEnabled, GetNumPlayersEnabled())
  /*DEFINE_METHOD( GetSongBeat,			m_Position.m_fSongBeat )
  DEFINE_METHOD( GetSongBeatVisible,		m_Position.m_fSongBeatVisible )
  DEFINE_METHOD( GetSongBPS,			m_Position.m_fCurBPS )
  DEFINE_METHOD( GetSongFreeze,			m_Position.m_bFreeze )
  DEFINE_METHOD( GetSongDelay,			m_Position.m_bDelay )*/
  static int GetSongPosition(T* p, lua_State* L) {
    p->position_.PushSelf(L);
    return 1;
  }
  DEFINE_METHOD(GetLastGameplayDuration, dance_duration_)
  DEFINE_METHOD(GetGameplayLeadIn, gameplay_lead_in_)
  DEFINE_METHOD(GetCoins, coins_)
  DEFINE_METHOD(IsSideJoined, side_is_joined_[Enum::Check<PlayerNumber>(L, 1)])
  DEFINE_METHOD(GetCoinsNeededToJoin, GetCoinsNeededToJoin())
  DEFINE_METHOD(EnoughCreditsToJoin, EnoughCreditsToJoin())
  DEFINE_METHOD(PlayersCanJoin, PlayersCanJoin())
  DEFINE_METHOD(GetNumSidesJoined, GetNumSidesJoined())
  DEFINE_METHOD(GetCoinMode, GetCoinMode())
  DEFINE_METHOD(GetPremium, GetPremium())
  DEFINE_METHOD(GetSongOptionsString, song_options_.GetCurrent().GetString())
  static int GetSongOptions(T* p, lua_State* L) {
    ModsLevel m = Enum::Check<ModsLevel>(L, 1);
    std::string s = p->song_options_.Get(m).GetString();
    LuaHelpers::Push(L, s);
    return 1;
  }
  static int GetSongOptionsObject(T* p, lua_State* L) {
    ModsLevel m = Enum::Check<ModsLevel>(L, 1);
    p->song_options_.Get(m).PushSelf(L);
    return 1;
  }
  static int GetDefaultSongOptions(T* p, lua_State* L) {
    SongOptions so;
    p->GetDefaultSongOptions(so);
    lua_pushstring(L, so.GetString().c_str());
    return 1;
  }
  static int ApplyPreferredSongOptionsToOtherLevels(T* p, lua_State* L) {
    p->song_options_.Assign(
        ModsLevel_Preferred, p->song_options_.Get(ModsLevel_Preferred));
    return 0;
  }
  static int ApplyStageModifiers(T* p, lua_State* L) {
    p->ApplyStageModifiers(Enum::Check<PlayerNumber>(L, 1), SArg(2));
    COMMON_RETURN_SELF;
  }
  static int ApplyPreferredModifiers(T* p, lua_State* L) {
    p->ApplyPreferredModifiers(Enum::Check<PlayerNumber>(L, 1), SArg(2));
    COMMON_RETURN_SELF;
  }
  static int ClearStageModifiersIllegalForCourse(T* p, lua_State* L) {
    p->ClearStageModifiersIllegalForCourse();
    COMMON_RETURN_SELF;
  }
  static int SetSongOptions(T* p, lua_State* L) {
    ModsLevel m = Enum::Check<ModsLevel>(L, 1);

    SongOptions so;

    so.FromString(SArg(2));
    p->song_options_.Assign(m, so);
    COMMON_RETURN_SELF;
  }
  static int GetStageResult(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    LuaHelpers::Push(L, p->GetStageResult(pn));
    return 1;
  }
  static int IsWinner(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushboolean(L, p->GetStageResult(pn) == RESULT_WIN);
    return 1;
  }
  static int IsDraw(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetStageResult(PLAYER_1) == RESULT_DRAW);
    return 1;
  }
  static int GetCurrentGame(T* p, lua_State* L) {
    const_cast<Game*>(p->GetCurrentGame())->PushSelf(L);
    return 1;
  }
  DEFINE_METHOD(GetEditCourseEntryIndex, edit_course_entry_index_)
  DEFINE_METHOD(GetEditLocalProfileID, edit_local_profile_id_.Get())
  static int GetEditLocalProfile(T* p, lua_State* L) {
    Profile* pProfile = p->GetEditLocalProfile();
    if (pProfile) {
      pProfile->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  static int GetCurrentStepsCredits(T* t, lua_State* L) {
    const Song* pSong = t->cur_song_;
    if (pSong == nullptr) {
      return 0;
    }

    // use a vector and not a set so that ordering is maintained
    std::vector<const Steps*> vpStepsToShow;
    FOREACH_HumanPlayer(p) {
      const Steps* pSteps = GAMESTATE->cur_steps_[p];
      if (pSteps == nullptr) {
        return 0;
      }
      bool bAlreadyAdded =
          find(vpStepsToShow.begin(), vpStepsToShow.end(), pSteps) !=
          vpStepsToShow.end();
      if (!bAlreadyAdded) {
        vpStepsToShow.push_back(pSteps);
      }
    }

    for (unsigned i = 0; i < vpStepsToShow.size(); i++) {
      const Steps* pSteps = vpStepsToShow[i];
      std::string sDifficulty =
          CustomDifficultyToLocalizedString(GetCustomDifficulty(
              pSteps->steps_type_, pSteps->GetDifficulty(),
              CourseType_Invalid));

      lua_pushstring(L, sDifficulty.c_str());
      lua_pushstring(L, pSteps->GetDescription().c_str());
    }

    return vpStepsToShow.size() * 2;
  }

  static int SetPreferredSongGroup(T* p, lua_State* L) {
    p->preferred_song_group_.Set(SArg(1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetPreferredSongGroup, preferred_song_group_.Get());
  static int GetHumanPlayers(T* p, lua_State* L) {
    std::vector<PlayerNumber> vHP;
    FOREACH_HumanPlayer(pn) vHP.push_back(pn);
    LuaHelpers::CreateTableFromArray(vHP, L);
    return 1;
  }
  static int GetEnabledPlayers(T*, lua_State* L) {
    std::vector<PlayerNumber> vEP;
    FOREACH_EnabledPlayer(pn) vEP.push_back(pn);
    LuaHelpers::CreateTableFromArray(vEP, L);
    return 1;
  }
  static int GetCurrentStyle(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1, true, true);
    Style* pStyle = const_cast<Style*>(p->GetCurrentStyle(pn));
    LuaHelpers::Push(L, pStyle);
    return 1;
  }
  static int IsAnyHumanPlayerUsingMemoryCard(T*, lua_State* L) {
    bool bUsingMemoryCard = false;
    FOREACH_HumanPlayer(pn) {
      if (MEMCARDMAN->GetCardState(pn) == MemoryCardState_Ready) {
        bUsingMemoryCard = true;
      }
    }
    lua_pushboolean(L, bUsingMemoryCard);
    return 1;
  }
  static int GetNumStagesForCurrentSongAndStepsOrCourse(T*, lua_State* L) {
    lua_pushnumber(L, GAMESTATE->GetNumStagesForCurrentSongAndStepsOrCourse());
    return 1;
  }
  static int GetNumStagesLeft(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushnumber(L, p->GetNumStagesLeft(pn));
    return 1;
  }
  static int GetGameSeed(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->game_seed_);
    return 1;
  }
  static int GetStageSeed(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->stage_seed_);
    return 1;
  }
  static int SaveLocalData(T* p, lua_State* L) {
    p->SaveLocalData();
    COMMON_RETURN_SELF;
  }

  static int SetJukeboxUsesModifiers(T* p, lua_State* L) {
    p->jukebox_uses_modifiers_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int Reset(T* p, lua_State* L) {
    p->Reset();
    COMMON_RETURN_SELF;
  }
  static int JoinPlayer(T* p, lua_State* L) {
    p->JoinPlayer(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int UnjoinPlayer(T* p, lua_State* L) {
    p->UnjoinPlayer(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int JoinInput(T* p, lua_State* L) {
    lua_pushboolean(L, p->JoinInput(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int GetSongPercent(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSongPercent(FArg(1)));
    return 1;
  }
  DEFINE_METHOD(GetCurMusicSeconds, position_.music_seconds_)

  DEFINE_METHOD(GetWorkoutGoalComplete, workout_goal_complete_)
  static int GetCharacter(T* p, lua_State* L) {
    p->cur_characters_[Enum::Check<PlayerNumber>(L, 1)]->PushSelf(L);
    return 1;
  }
  static int SetCharacter(T* p, lua_State* L) {
    Character* c = CHARMAN->GetCharacterFromID(SArg(2));
    if (c) {
      p->cur_characters_[Enum::Check<PlayerNumber>(L, 1)] = c;
    }
    COMMON_RETURN_SELF;
  }
  static int GetExpandedSectionName(T* p, lua_State* L) {
    lua_pushstring(L, p->expanded_section_name_.c_str());
    return 1;
  }

  static int GetExpandedParentSectionName(T* p, lua_State* L) {
    lua_pushstring(L, p->expanded_parent_section_name_.c_str());
    return 1;
  }
  static int AddStageToPlayer(T* p, lua_State* L) {
    p->AddStageToPlayer(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int InsertCoin(T* p, lua_State* L) {
    int numCoins = IArg(1);
    if (GAMESTATE->coins_ + numCoins >= 0) {
      StepMania::InsertCoin(numCoins, false);
    } else {
      // Warn themers if they attempt to set credits to a negative value.
      luaL_error(L, "Credits may not be negative.");
    }
    COMMON_RETURN_SELF;
  }
  static int InsertCredit(T* p, lua_State* L) {
    StepMania::InsertCredit();
    COMMON_RETURN_SELF;
  }
  static int CurrentOptionsDisqualifyPlayer(T* p, lua_State* L) {
    lua_pushboolean(
        L, p->CurrentOptionsDisqualifyPlayer(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }

  static int ResetPlayerOptions(T* p, lua_State* L) {
    p->ResetPlayerOptions(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }

  static int RefreshNoteSkinData(T* p, lua_State* L) {
    NOTESKIN->RefreshNoteSkinData(p->cur_game_);
    COMMON_RETURN_SELF;
  }

  static int Dopefish(T* p, lua_State* L) {
    lua_pushboolean(L, p->dopefish_);
    return 1;
  }

  static int LoadProfiles(T* p, lua_State* L) {
    bool LoadEdits = true;
    if (lua_isboolean(L, 1)) {
      LoadEdits = BArg(1);
    }
    p->LoadProfiles(LoadEdits);
    SCREENMAN->ZeroNextUpdate();
    COMMON_RETURN_SELF;
  }

  static int SaveProfiles(T* p, lua_State* L) {
    p->SavePlayerProfiles();
    SCREENMAN->ZeroNextUpdate();
    COMMON_RETURN_SELF;
  }

  static int SetFailTypeExplicitlySet(T* p, lua_State* L) {
    p->fail_type_was_explicitly_set_ = true;
    COMMON_RETURN_SELF;
  }

  static int StoreRankingName(T* p, lua_State* L) {
    p->StoreRankingName(Enum::Check<PlayerNumber>(L, 1), SArg(2));
    COMMON_RETURN_SELF;
  }

  DEFINE_METHOD(HaveProfileToLoad, HaveProfileToLoad())
  DEFINE_METHOD(HaveProfileToSave, HaveProfileToSave())

  static bool AreStyleAndPlayModeCompatible(
      T* p, lua_State* L, const Style* style, PlayMode pm) {
    if (pm != PLAY_MODE_BATTLE && pm != PLAY_MODE_RAVE) {
      return true;
    }

    // Do not allow styles with StepsTypes with shared sides or that are one
    // player only with Battle or Rave.
    if (style->style_type_ != StyleType_TwoPlayersSharedSides) {
      std::vector<const Style*> vpStyles;
      GAMEMAN->GetCompatibleStyles(p->cur_game_, 2, vpStyles);
      for (const Style* s : vpStyles) {
        if (s->steps_type_ == style->steps_type_) {
          return true;
        }
      }
    }
    luaL_error(
        L, "Style %s is incompatible with PlayMode %s", style->name_,
        PlayModeToString(pm).c_str());
    return false;
  }

  static void ClearIncompatibleStepsAndTrails(T* p, lua_State* L) {
    FOREACH_HumanPlayer(pn) {
      const Style* style = p->GetCurrentStyle(pn);
      if (p->cur_steps_[pn] &&
          (!style || style->steps_type_ != p->cur_steps_[pn]->steps_type_)) {
        p->cur_steps_[pn].Set(nullptr);
      }
      if (p->cur_trail_[pn] &&
          (!style || style->steps_type_ != p->cur_trail_[pn]->steps_type_)) {
        p->cur_trail_[pn].Set(nullptr);
      }
    }
  }

  static int SetCurrentStyle(T* p, lua_State* L) {
    const Style* pStyle = nullptr;
    if (lua_isstring(L, 1)) {
      std::string style = SArg(1);
      pStyle = GAMEMAN->GameAndStringToStyle(GAMESTATE->cur_game_, style);
      if (!pStyle) {
        luaL_error(
            L, "SetCurrentStyle: %s is not a valid style.", style.c_str());
      }
    } else {
      pStyle = Luna<Style>::check(L, 1);
    }

    StyleType st = pStyle->style_type_;
    if (p->GetNumSidesJoined() == 2 && (st == StyleType_OnePlayerOneSide ||
                                        st == StyleType_OnePlayerTwoSides)) {
      luaL_error(L, "Too many sides joined for style %s", pStyle->name_);
    } else if (
        p->GetNumSidesJoined() == 1 &&
        (st == StyleType_TwoPlayersTwoSides ||
         st == StyleType_TwoPlayersSharedSides)) {
      luaL_error(L, "Too few sides joined for style %s", pStyle->name_);
    }

    if (!AreStyleAndPlayModeCompatible(p, L, pStyle, p->play_mode_)) {
      COMMON_RETURN_SELF;
    }
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 2, true, true);

    p->SetCurrentStyle(pStyle, pn);
    ClearIncompatibleStepsAndTrails(p, L);

    COMMON_RETURN_SELF;
  }

  static int SetCurrentPlayMode(T* p, lua_State* L) {
    PlayMode pm = Enum::Check<PlayMode>(L, 1);
    if (AreStyleAndPlayModeCompatible(
            p, L, p->GetCurrentStyle(PLAYER_INVALID), pm)) {
      p->play_mode_.Set(pm);
    }
    COMMON_RETURN_SELF;
  }

  static int SetStepsForEditMode(T* p, lua_State* L) {
    // Arg forms:
    // 1.  Edit existing steps:
    //    song, steps
    // 2.  Create new steps to edit:
    //    song, nil, stepstype, difficulty
    // 3.  Copy steps to new difficulty to edit:
    //    song, steps, stepstype, difficulty
    Song* song = Luna<Song>::check(L, 1);
    Steps* steps = nullptr;
    if (!lua_isnil(L, 2)) {
      steps = Luna<Steps>::check(L, 2);
    }
    // Form 1.
    if (steps != nullptr && lua_gettop(L) == 2) {
      p->cur_song_.Set(song);
      p->cur_steps_[PLAYER_1].Set(steps);
      p->SetCurrentStyle(
          GAMEMAN->GetEditorStyleForStepsType(steps->steps_type_),
          PLAYER_INVALID);
      p->cur_course_.Set(nullptr);
      return 0;
    }
    StepsType stype = Enum::Check<StepsType>(L, 3);
    Difficulty diff = Enum::Check<Difficulty>(L, 4);
    Steps* new_steps = song->CreateSteps();
    std::string edit_name;
    // Form 2.
    if (steps == nullptr) {
      new_steps->CreateBlank(stype);
      new_steps->SetMeter(1);
      edit_name = "";
    }
    // Form 3.
    else {
      new_steps->CopyFrom(steps, stype, song->music_length_seconds_);
      edit_name = steps->GetDescription();
    }
    SongUtil::MakeUniqueEditDescription(song, stype, edit_name);
    steps->SetDescription(edit_name);
    song->AddSteps(new_steps);
    p->cur_song_.Set(song);
    p->cur_steps_[PLAYER_1].Set(steps);
    p->SetCurrentStyle(
        GAMEMAN->GetEditorStyleForStepsType(steps->steps_type_),
        PLAYER_INVALID);
    p->cur_course_.Set(nullptr);
    return 0;
  }

  static int GetAutoGenFarg(T* p, lua_State* L) {
    int i = IArg(1) - 1;
    if (i < 0) {
      lua_pushnil(L);
      return 1;
    }
    size_t si = static_cast<size_t>(i);
    if (si >= p->autogen_fargs_.size()) {
      lua_pushnil(L);
      return 1;
    }
    lua_pushnumber(L, p->GetAutoGenFarg(si));
    return 1;
  }
  static int SetAutoGenFarg(T* p, lua_State* L) {
    int i = IArg(1) - 1;
    if (i < 0) {
      luaL_error(L, "%i is not a valid autogen arg index.", i);
    }
    float v = FArg(2);
    size_t si = static_cast<size_t>(i);
    while (si >= p->autogen_fargs_.size()) {
      p->autogen_fargs_.push_back(0.0f);
    }
    p->autogen_fargs_[si] = v;
    COMMON_RETURN_SELF;
  }

  static int GetPlayerFailType(T* p, lua_State* L) {
    Enum::Push(L, p->GetPlayerFailType(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }

  static int prepare_song_for_gameplay(T* p, lua_State* L) {
    int result = p->prepare_song_for_gameplay();
    lua_pushstring(L, prepare_song_failures[result]);
    return 1;
  }

  LunaGameState() {
    ADD_METHOD(IsPlayerEnabled);
    ADD_METHOD(IsHumanPlayer);
    ADD_METHOD(GetPlayerDisplayName);
    ADD_METHOD(GetMasterPlayerNumber);
    ADD_METHOD(InStepEditor);
    ADD_METHOD(ShowW1);
    ADD_METHOD(GetPlayerState);
    ADD_METHOD(ApplyGameCommand);
    ADD_METHOD(CanSafelyEnterGameplay);
    ADD_METHOD(GetCurrentSong);
    ADD_METHOD(SetCurrentSong);
    ADD_METHOD(GetCurrentSteps);
    ADD_METHOD(SetCurrentSteps);
    ADD_METHOD(GetCurrentCourse);
    ADD_METHOD(SetCurrentCourse);
    ADD_METHOD(GetCurrentTrail);
    ADD_METHOD(SetCurrentTrail);
    ADD_METHOD(SetPreferredSong);
    ADD_METHOD(GetPreferredSong);
    ADD_METHOD(SetTemporaryEventMode);
    ADD_METHOD(Env);
    ADD_METHOD(GetEditSourceSteps);
    ADD_METHOD(SetPreferredDifficulty);
    ADD_METHOD(GetPreferredDifficulty);
    ADD_METHOD(AnyPlayerHasRankingFeats);
    ADD_METHOD(IsCourseMode);
    ADD_METHOD(IsBattleMode);
    ADD_METHOD(IsDemonstration);
    ADD_METHOD(GetPlayMode);
    ADD_METHOD(GetSortOrder);
    ADD_METHOD(GetCurrentStageIndex);
    ADD_METHOD(IsGoalComplete);
    ADD_METHOD(PlayerIsUsingModifier);
    ADD_METHOD(GetCourseSongIndex);
    ADD_METHOD(GetLoadingCourseSongIndex);
    ADD_METHOD(IsPremiumFreeActive);
    ADD_METHOD(GetPremiumFreeSecondsLeft);
    ADD_METHOD(IsPremiumFreeExpired);
    ADD_METHOD(GetSmallestNumStagesLeftForAnyHumanPlayer);
    ADD_METHOD(IsAnExtraStage);
    ADD_METHOD(IsExtraStage);
    ADD_METHOD(IsExtraStage2);
    ADD_METHOD(GetCurrentStage);
    ADD_METHOD(HasEarnedExtraStage);
    ADD_METHOD(GetEarnedExtraStage);
    ADD_METHOD(GetEasiestStepsDifficulty);
    ADD_METHOD(GetHardestStepsDifficulty);
    ADD_METHOD(IsEventMode);
    ADD_METHOD(GetNumPlayersEnabled);
    /*ADD_METHOD( GetSongBeat );
    ADD_METHOD( GetSongBeatVisible );
    ADD_METHOD( GetSongBPS );
    ADD_METHOD( GetSongFreeze );
    ADD_METHOD( GetSongDelay );*/
    ADD_METHOD(GetSongPosition);
    ADD_METHOD(GetLastGameplayDuration);
    ADD_METHOD(GetGameplayLeadIn);
    ADD_METHOD(GetCoins);
    ADD_METHOD(IsSideJoined);
    ADD_METHOD(GetCoinsNeededToJoin);
    ADD_METHOD(EnoughCreditsToJoin);
    ADD_METHOD(PlayersCanJoin);
    ADD_METHOD(GetNumSidesJoined);
    ADD_METHOD(GetCoinMode);
    ADD_METHOD(GetPremium);
    ADD_METHOD(GetSongOptionsString);
    ADD_METHOD(GetSongOptions);
    ADD_METHOD(GetSongOptionsObject);
    ADD_METHOD(GetDefaultSongOptions);
    ADD_METHOD(ApplyPreferredSongOptionsToOtherLevels);
    ADD_METHOD(ApplyPreferredModifiers);
    ADD_METHOD(ApplyStageModifiers);
    ADD_METHOD(ClearStageModifiersIllegalForCourse);
    ADD_METHOD(SetSongOptions);
    ADD_METHOD(GetStageResult);
    ADD_METHOD(IsWinner);
    ADD_METHOD(IsDraw);
    ADD_METHOD(GetCurrentGame);
    ADD_METHOD(GetEditCourseEntryIndex);
    ADD_METHOD(GetEditLocalProfileID);
    ADD_METHOD(GetEditLocalProfile);
    ADD_METHOD(GetCurrentStepsCredits);
    ADD_METHOD(SetPreferredSongGroup);
    ADD_METHOD(GetPreferredSongGroup);
    ADD_METHOD(GetHumanPlayers);
    ADD_METHOD(GetEnabledPlayers);
    ADD_METHOD(GetCurrentStyle);
    ADD_METHOD(IsAnyHumanPlayerUsingMemoryCard);
    ADD_METHOD(GetNumStagesForCurrentSongAndStepsOrCourse);
    ADD_METHOD(GetNumStagesLeft);
    ADD_METHOD(GetGameSeed);
    ADD_METHOD(GetStageSeed);
    ADD_METHOD(SaveLocalData);
    ADD_METHOD(SetJukeboxUsesModifiers);
    ADD_METHOD(GetWorkoutGoalComplete);
    ADD_METHOD(Reset);
    ADD_METHOD(JoinPlayer);
    ADD_METHOD(UnjoinPlayer);
    ADD_METHOD(JoinInput);
    ADD_METHOD(GetSongPercent);
    ADD_METHOD(GetCurMusicSeconds);
    ADD_METHOD(GetCharacter);
    ADD_METHOD(SetCharacter);
    ADD_METHOD(GetExpandedSectionName);
    ADD_METHOD(GetExpandedParentSectionName);
    ADD_METHOD(AddStageToPlayer);
    ADD_METHOD(InsertCoin);
    ADD_METHOD(InsertCredit);
    ADD_METHOD(CurrentOptionsDisqualifyPlayer);
    ADD_METHOD(ResetPlayerOptions);
    ADD_METHOD(RefreshNoteSkinData);
    ADD_METHOD(Dopefish);
    ADD_METHOD(LoadProfiles);
    ADD_METHOD(SaveProfiles);
    ADD_METHOD(HaveProfileToLoad);
    ADD_METHOD(HaveProfileToSave);
    ADD_METHOD(SetFailTypeExplicitlySet);
    ADD_METHOD(StoreRankingName);
    ADD_METHOD(SetCurrentStyle);
    ADD_METHOD(SetCurrentPlayMode);
    ADD_METHOD(SetStepsForEditMode);
    ADD_METHOD(GetAutoGenFarg);
    ADD_METHOD(SetAutoGenFarg);
    ADD_METHOD(GetPlayerFailType);
    ADD_METHOD(prepare_song_for_gameplay);
  }
};

LUA_REGISTER_CLASS(GameState)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard, Chris Gomez
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
