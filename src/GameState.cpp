#include "global.h"

#include "GameState.h"

#include <cmath>
#include <cstddef>
#include <ctime>
#include <set>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "AdjustSync.h"
#include "AnnouncerManager.h"
#include "Bookkeeper.h"
#include "Character.h"
#include "CharacterManager.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "CryptManager.h"
#include "Game.h"
#include "GameCommand.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GamePreferences.h"
#include "HighScore.h"
#include "LightsManager.h"
#include "LuaReference.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "NoteData.h"
#include "NoteSkinManager.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StatsManager.h"
#include "StepMania.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "UnlockManager.h"

// Global and accessible from anywhere in our program.
GameState* GAMESTATE = nullptr;

#define NAME_BLACKLIST_FILE "/Data/NamesBlacklist.txt"

class GameStateMessageHandler : public MessageSubscriber {
  void HandleMessage(const Message& msg) {
    if (msg.GetName() == "RefreshCreditText") {
      RString joined;
      FOREACH_HumanPlayer(pn) {
        if (joined != "") {
          joined += ", ";
        }
        joined += ssprintf("P%i", pn + 1);
      }

      if (joined == "") {
        joined = "none";
      }

      LOG->MapLog("JOINED", "Players joined: %s", joined.c_str());
    }
  }
};

struct GameStateImpl {
  GameStateMessageHandler subscriber;
  GameStateImpl() { subscriber.SubscribeToMessage("RefreshCreditText"); }
};
static GameStateImpl* g_pImpl = nullptr;

ThemeMetric<bool> ALLOW_LATE_JOIN("GameState", "AllowLateJoin");
ThemeMetric<bool> USE_NAME_BLACKLIST("GameState", "UseNameBlacklist");

ThemeMetric<RString> DEFAULT_SORT("GameState", "DefaultSort");
SortOrder GetDefaultSort() { return StringToSortOrder(DEFAULT_SORT); }
ThemeMetric<RString> DEFAULT_SONG("GameState", "DefaultSong");
Song* GameState::GetDefaultSong() const {
  SongID song_id;
  song_id.FromString(DEFAULT_SONG);
  return song_id.ToSong();
}

static const ThemeMetric<bool> EDIT_ALLOWED_FOR_EXTRA(
    "GameState", "EditAllowedForExtra");
static const ThemeMetric<Difficulty> MIN_DIFFICULTY_FOR_EXTRA(
    "GameState", "MinDifficultyForExtra");
static const ThemeMetric<Grade> GRADE_TIER_FOR_EXTRA_1(
    "GameState", "GradeTierForExtra1");
static const ThemeMetric<bool> ALLOW_EXTRA_2(
		"GameState", "AllowExtra2");
static const ThemeMetric<Grade> GRADE_TIER_FOR_EXTRA_2(
    "GameState", "GradeTierForExtra2");

static ThemeMetric<bool> ARE_STAGE_PLAYER_MODS_FORCED(
    "GameState", "AreStagePlayerModsForced");
static ThemeMetric<bool> ARE_STAGE_SONG_MODS_FORCED(
    "GameState", "AreStageSongModsForced");

static Preference<Premium> g_Premium("Premium", Premium_DoubleFor1Credit);
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
      gameplay_lead_in_(Message_GameplayLeadInChanged),
      did_mode_change_noteskin_(false),
      is_using_step_timing_(true),
      in_step_editor_(false),
      steps_type_edit_(Message_EditStepsTypeChanged),
      course_difficulty_edit_(Message_EditCourseDifficultyChanged),
      edit_source_steps_(Message_EditSourceStepsChanged),
      edit_source_steps_type_(Message_EditSourceStepsTypeChanged),
      edit_course_entry_index_(Message_EditCourseEntryIndexChanged),
      edit_local_profile_id_(Message_EditLocalProfileIDChanged) {
  g_pImpl = new GameStateImpl;

  cur_style_.Set(nullptr);
  FOREACH_PlayerNumber(rpn) { separated_styles_[rpn] = nullptr; }

  cur_game_.Set(nullptr);
  coins_.Set(0);
  time_game_started_.SetZero();
  demonstration_or_jukebox_ = false;

	// Initial screen will bump this up to 0
  num_times_through_attract_ = -1;
  stage_seed_ = game_seed_ = 0;

	// Used by IsPlayerEnabled before the first screen.
  play_mode_.Set( PlayMode_Invalid);
	// Used by GetNumSidesJoined before the first screen.
  FOREACH_PlayerNumber(p) side_is_joined_[p] = false;

  FOREACH_PlayerNumber(p) {
    player_state_[p] = new PlayerState;
    player_state_[p]->SetPlayerNumber(p);
  }
  FOREACH_MultiPlayer(p) {
    multiplayer_state_[p] = new PlayerState;
    multiplayer_state_[p]->SetPlayerNumber(PLAYER_1);
    multiplayer_state_[p]->m_mp = p;
  }

  environment_ = new LuaTable;

  dopefish_ = false;

  expanded_section_name_ = "";

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

  FOREACH_PlayerNumber(p) SAFE_DELETE(player_state_[p]);
  FOREACH_MultiPlayer(p) SAFE_DELETE(multiplayer_state_[p]);

  SAFE_DELETE(environment_);
  SAFE_DELETE(g_pImpl);
  SAFE_DELETE(processed_timing_);
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

void GameState::ApplyGameCommand(const RString& command, PlayerNumber pn) {
  GameCommand m;
  m.Load(0, ParseCommands(command));

  RString reason;
  if (!m.IsPlayable(&reason)) {
    LuaHelpers::ReportScriptErrorFmt(
        "Can't apply GameCommand \"%s\": %s", command.c_str(), reason.c_str());
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
  RString player;
  for (int i = 0; GetCommandlineArgument("player", &player, i); ++i) {
    int pn = StringToInt(player) - 1;
    if (!IsAnInt(player) || pn < 0 || pn >= NUM_PLAYERS) {
      RageException::Throw(
          "Invalid argument \"--player=%s\".", player.c_str());
    }

    JoinPlayer((PlayerNumber)pn);
  }

  RString mode;
  for (int i = 0; GetCommandlineArgument("mode", &mode, i); ++i) {
    ApplyGameCommand(mode);
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
  ResetPlayerOptions(pn);
}

void GameState::ResetPlayerOptions(PlayerNumber pn) {
  PlayerOptions player_options;
  GetDefaultPlayerOptions(player_options);
  player_state_[pn]->m_PlayerOptions.Assign(
			ModsLevel_Preferred, player_options);
}

void GameState::Reset() {
	// Must initialize for UnjoinPlayer.
  this->SetMasterPlayerNumber(PLAYER_INVALID);

  FOREACH_PlayerNumber(pn) UnjoinPlayer(pn);

  ASSERT(THEME != nullptr);

  time_game_started_.SetZero();
  SetCurrentStyle(nullptr, PLAYER_INVALID);
  FOREACH_MultiPlayer(p) multiplayer_status_[p] = MultiPlayerStatus_NotJoined;
  FOREACH_PlayerNumber(pn) MEMCARDMAN->UnlockCard(pn);
  multiplayer_ = false;
  num_multiplayer_note_fields_ = 1;
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

  adjust_tokens_by_song_cost_for_final_stage_check = true;

  cur_song_.Set(GetDefaultSong());
  preferred_song_ = nullptr;
  cur_course_.Set(nullptr);
  preferred_course_ = nullptr;

  FOREACH_MultiPlayer(p) multiplayer_state_[p]->Reset();

  song_options_.Init();

  is_paused_ = false;
  ResetMusicStatistics();
  ResetStageStatistics();
  AdjustSync::ResetOriginalSyncData();

  SONGMAN->UpdatePopular();
  SONGMAN->UpdateShuffled();

  // We may have cached trails from before everything was loaded (eg. from
  // before SongManager::UpdatePopular could be called). Erase the cache.
  SONGMAN->RegenerateNonFixedCourses();

  STATSMAN->Reset();

  FOREACH_PlayerNumber(p) {
    if (PREFSMAN->m_ShowDancingCharacters == SDC_Random) {
      cur_characters_[p] = CHARMAN->GetRandomCharacter();
    } else {
      cur_characters_[p] = CHARMAN->GetDefaultCharacter();
    }
    ASSERT(cur_characters_[p] != nullptr);
  }

  temporary_event_mode_ = false;

  LIGHTSMAN->SetLightsMode(LIGHTSMODE_ATTRACT);

  steps_type_edit_.Set(StepsType_Invalid);
  edit_source_steps_.Set(nullptr);
  edit_source_steps_type_.Set(StepsType_Invalid);
  edit_course_entry_index_.Set(-1);
  edit_local_profile_id_.Set("");

  backed_out_of_final_stage_ = false;
  earned_extra_stage_ = false;
  expanded_section_name_ = "";

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
            cur_game_, players_joined + 1, cur_style->m_StepsType);
        if (new_style == nullptr) {
          return;
        }
      }
    }
  }
  // If joint premium and we're not taking away a credit for the 2nd join, give
	// the new player the same number of stage tokens that the old player has.
  if (GetCoinMode() == CoinMode_Pay &&
      GetPremium() == Premium_2PlayersFor1Credit && GetNumSidesJoined() == 1) {
    player_stage_tokens_[pn] =
        player_stage_tokens_[this->GetMasterPlayerNumber()];
  } else {
    player_stage_tokens_[pn] = PREFSMAN->m_iSongsPerPlay;
  }

  side_is_joined_[pn] = true;

  if (this->GetMasterPlayerNumber() == PLAYER_INVALID) {
    this->SetMasterPlayerNumber(pn);
  }

  // If first player to join, set start time.
  if (GetNumSidesJoined() == 1) {
    BeginGame();
  }

  // Count each player join as a play.
  {
    Profile* machine_profile = PROFILEMAN->GetMachineProfile();
    machine_profile->m_iTotalSessions++;
  }

  // NOTE(kyz): Set the current style to something appropriate for the new
  // number of joined players. beat gametype's versus styles use a different
  // stepstype from its single styles, so when GameCommand tries to join both
  // players for a versus style, it hits the assert when joining the first
  // player. So if the firstplayer is being joined and the current styletype is
  // for two players, assume that the second player will be joined immediately
  // afterwards and don't try to change the style.
  const Style* cur_style = GetCurrentStyle(PLAYER_INVALID);
  if (cur_style != nullptr &&
      !(pn == PLAYER_1 &&
        (cur_style->m_StyleType == StyleType_TwoPlayersTwoSides ||
         cur_style->m_StyleType == StyleType_TwoPlayersSharedSides))) {
    const Style* pStyle;
    // NOTE(aj): Only use one player for StyleType_OnePlayerTwoSides and
    // StepsTypes that can only be played by one player (e.g. dance-solo,
    // dance-threepanel, popn-nine).
    // NOTE(aj): This still shows joined player as "Insert Card". May not be an
		// issue?
    if (cur_style->m_StyleType == StyleType_OnePlayerTwoSides ||
        cur_style->m_StepsType == StepsType_dance_solo ||
        cur_style->m_StepsType == StepsType_dance_threepanel ||
        cur_style->m_StepsType == StepsType_popn_nine) {
      pStyle = GAMEMAN->GetFirstCompatibleStyle(
          cur_game_, 1, cur_style->m_StepsType);
    } else {
      pStyle = GAMEMAN->GetFirstCompatibleStyle(
          cur_game_, GetNumSidesJoined(), cur_style->m_StepsType);
    }

    // Use SetCurrentStyle in case of StyleType_OnePlayerTwoSides.
    SetCurrentStyle(pStyle, pn);
  }

  Message msg(MessageIDToString(Message_PlayerJoined));
  msg.SetParam("Player", pn);
  MESSAGEMAN->Broadcast(msg);
}

void GameState::UnjoinPlayer(PlayerNumber pn) {
  // Unjoin STATSMAN first, so steps used by this player are released
  // and can be released by PROFILEMAN.
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
    SongOptions song_options;
    GetDefaultSongOptions(song_options);
    song_options_.Assign(ModsLevel_Preferred, song_options);
    did_mode_change_noteskin_ = false;
  }
}

// TODO(aj): handle multiplayer join?

namespace {

bool JoinInputInternal(PlayerNumber pn) {
  if (!GAMESTATE->PlayersCanJoin()) {
    return false;
  }

  // If this side is already in, don't re-join.
  if (GAMESTATE->side_is_joined_[pn]) {
    return false;
  }

  // Subtract coins.
  int iCoinsNeededToJoin = GAMESTATE->GetCoinsNeededToJoin();

  if (GAMESTATE->coins_ < iCoinsNeededToJoin) {
		// Not enough coins.
    return false;
  }

  GAMESTATE->coins_.Set(GAMESTATE->coins_ - iCoinsNeededToJoin);

  GAMESTATE->JoinPlayer(pn);

  // On Join, make sure to update Coins File
  BOOKKEEPER->WriteCoinsFile(GAMESTATE->coins_.Get());

  return true;
}

}  // namespace

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
  bool joined = false;
  FOREACH_PlayerNumber(pn) {
    if (JoinInputInternal(pn)) {
      joined = true;
    }
  }
  return joined;
}

int GameState::GetCoinsNeededToJoin() const {
  int coins_to_charge = 0;

  if (GetCoinMode() == CoinMode_Pay) {
    coins_to_charge = PREFSMAN->m_iCoinsPerCredit;
  }

  // If joint premium, don't take away a credit for the second join.
  if (GetPremium() == Premium_2PlayersFor1Credit && GetNumSidesJoined() == 1) {
    coins_to_charge = 0;
  }

  return coins_to_charge;
}

// Game flow:
//
// BeginGame() - the first player has joined; the game is starting.
//
// PlayersFinalized() - player memory cards are loaded; later joins won't have
// memory cards this stage
//
// BeginStage() - gameplay is beginning
//
// optional: CancelStage() - gameplay aborted (Back pressed), undo BeginStage
// and back up
//
// CommitStageStats() - gameplay is finished
//   Saves STATSMAN->m_CurStageStats to the profiles, so profile information
//   is up-to-date for Evaluation.
//
// FinishStage() - gameplay and evaluation is finished
//   Clears data which was stored by CommitStageStats.
void GameState::BeginGame() {
  time_game_started_.Touch();

  names_that_were_filled_.clear();
  players_that_were_filled_.clear();

  // Play attract on the ending screen, then on the ranking screen
  // even if attract sounds are set to off.
  num_times_through_attract_ = -1;

  FOREACH_PlayerNumber(pn) MEMCARDMAN->UnlockCard(pn);
}

void GameState::LoadProfiles(bool load_edits) {
  // Unlock any cards that we might want to load.
  FOREACH_HumanPlayer(pn) if (!PROFILEMAN->IsPersistentProfile(pn))
      MEMCARDMAN->UnlockCard(pn);

  MEMCARDMAN->WaitForCheckingToComplete();

  FOREACH_HumanPlayer(pn) {
    // If a profile is already loaded, this was already called.
    if (PROFILEMAN->IsPersistentProfile(pn)) {
      continue;
    }

    MEMCARDMAN->MountCard(pn);
		// Load full profile.
    bool success = PROFILEMAN->LoadFirstAvailableProfile(pn, load_edits);
    MEMCARDMAN->UnmountCard(pn);

    if (!success) {
      continue;
    }

    // Lock the card on successful load, so we won't allow it to be changed.
    MEMCARDMAN->LockCard(pn);

    LoadCurrentSettingsFromProfile(pn);

    Profile* player_profile = PROFILEMAN->GetProfile(pn);
    if (player_profile) {
      player_profile->m_iTotalSessions++;
    }
  }
}

void GameState::SavePlayerProfiles() {
  FOREACH_HumanPlayer(pn) SavePlayerProfile(pn);
}

void GameState::SavePlayerProfile(PlayerNumber pn) {
  if (!PROFILEMAN->IsPersistentProfile(pn)) {
    return;
  }

  // NOTE(aj): AutoplayCPU should not save scores. this MAY cause issues with
	// Multiplayer. However, without a working Multiplayer build, we'll never
	// know.
  if (player_state_[pn]->m_PlayerController != PC_HUMAN) {
    return;
  }

  bool was_memory_card = PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn);
  if (was_memory_card) {
    MEMCARDMAN->MountCard(pn);
  }
  PROFILEMAN->SaveProfile(pn);
  if (was_memory_card) {
    MEMCARDMAN->UnmountCard(pn);
  }
}

bool GameState::HaveProfileToLoad() {
  FOREACH_HumanPlayer(pn) {
    // We won't load this profile if it's already loaded.
    if (PROFILEMAN->IsPersistentProfile(pn)) {
      continue;
    }

    // If a memory card is inserted, we'l try to load it.
    if (MEMCARDMAN->CardInserted(pn)) {
      return true;
    }
    if (!PROFILEMAN->m_sDefaultLocalProfileID[pn].Get().empty()) {
      return true;
    }
  }

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

int GameState::GetNumStagesMultiplierForSong(const Song* song) {
  int num_stages = 1;

  ASSERT(song != nullptr);
  if (song->IsMarathon()) {
    num_stages *= 3;
  }
  if (song->IsLong()) {
    num_stages *= 2;
  }

  return num_stages;
}

int GameState::GetNumStagesForCurrentSongAndStepsOrCourse() const {
  int num_stages_of_this_song = 1;
  if (cur_song_) {
    // Extra stages need to only count as one stage in case a multi-stage song
		// is chosen.
    if (IsAnExtraStage()) {
      num_stages_of_this_song = 1;
    } else {
      num_stages_of_this_song =
          GameState::GetNumStagesMultiplierForSong(cur_song_);
    }
  } else if (cur_course_) {
    num_stages_of_this_song = PREFSMAN->m_iSongsPerPlay;
  } else {
    return -1;
  }

  num_stages_of_this_song = std::max(num_stages_of_this_song, 1);

  return num_stages_of_this_song;
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
    FOREACH_PlayerNumber(pn) {
      ModsGroup<PlayerOptions>& player_options =
					player_state_[pn]->m_PlayerOptions;
      player_options.Assign(
          ModsLevel_Stage, player_state_[pn]->m_PlayerOptions.GetPreferred());
    }
  }
  if (!ARE_STAGE_SONG_MODS_FORCED) {
    song_options_.Assign(ModsLevel_Stage, song_options_.GetPreferred());
  }

  STATSMAN->m_CurStageStats.m_fMusicRate = song_options_.GetSong().m_fMusicRate;
  num_stages_of_this_song_ = GetNumStagesForCurrentSongAndStepsOrCourse();
  ASSERT(num_stages_of_this_song_ != -1);
  FOREACH_EnabledPlayer(p) {
    // NOTE(aj): Only do this check with human players. Assume that CPU players
    // (Rave) always have tokens (this could probably be moved below, even).
    if (!IsEventMode() && !IsCpuPlayer(p)) {
      if (player_stage_tokens_[p] < num_stages_of_this_song_) {
        LuaHelpers::ReportScriptErrorFmt(
            "Player %d only has %d stage tokens, but needs %d.", p,
            player_stage_tokens_[p], num_stages_of_this_song_);
      }
    }
    player_stage_tokens_[p] -= num_stages_of_this_song_;
  }
  FOREACH_HumanPlayer(pn) if (CurrentOptionsDisqualifyPlayer(pn))
      STATSMAN->m_CurStageStats.m_player[pn]
          .m_bDisqualified = true;
  earned_extra_stage_ = false;
  stage_guid_ = CryptManager::GenerateRandomUUID();
}

void GameState::CancelStage() {
  FOREACH_CpuPlayer(p) {
    switch (play_mode_) {
      case PLAY_MODE_BATTLE:
      case PLAY_MODE_RAVE:
        player_stage_tokens_[p] = PREFSMAN->m_iSongsPerPlay;
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

  STATSMAN->CommitStatsToProfiles(&STATSMAN->m_CurStageStats);

  // Update TotalPlaySeconds.
  int play_seconds = std::max(0, (int)time_game_started_.GetDeltaTime());

  Profile* machine_profile = PROFILEMAN->GetMachineProfile();
  machine_profile->m_iTotalSessionSeconds += play_seconds;

  FOREACH_HumanPlayer(p) {
    Profile* player_profile = PROFILEMAN->GetProfile(p);
    if (player_profile) {
      player_profile->m_iTotalSessionSeconds += play_seconds;
    }
  }
}

// Called by ScreenSelectMusic (etc). Increment the stage counter if we just
// played a song. Might be called more than once.
void GameState::FinishStage() {
  // Increment the stage counter.
  const int old_stage_index = current_stage_index_;
  ++current_stage_index_;

  num_stages_of_this_song_ = 0;

  EarnedExtraStage earned_extra_stage = CalculateEarnedExtraStage();
  STATSMAN->m_CurStageStats.m_EarnedExtraStage = earned_extra_stage;
  if (earned_extra_stage != EarnedExtraStage_No) {
    LOG->Trace("awarded extra stage");
    FOREACH_HumanPlayer(p) {
      // TODO(aj): Unhardcode the extra stage limit?
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
    Profile* profile = PROFILEMAN->GetProfile(p);
    profile->m_iCurrentCombo =
        STATSMAN->m_CurStageStats.m_player[p].m_iCurCombo;
    // If the sort order is Recent, move the profile to the top of the list.
    if (PREFSMAN->m_ProfileSortOrder == ProfileSortOrder_Recent &&
        PROFILEMAN->IsPersistentProfile(p)) {
      int num_local_profiles = PROFILEMAN->GetNumLocalProfiles();
      for (int i = 0; i < num_local_profiles; ++i) {
        Profile* local_profile = PROFILEMAN->GetLocalProfileFromIndex(i);
        if (local_profile->m_sGuid == profile->m_sGuid) {
          PROFILEMAN->MoveProfileTopBottom(
              i, PREFSMAN->m_bProfileSortOrderAscending);
          break;
        }
      }
    }
  }

  if (demonstration_or_jukebox_) {
    return;
  }

  // TODO(aj): Simplify. Profile saving is accomplished in ScreenProfileSave
  // now; all this code does differently is save machine profile as well.
  if (IsEventMode()) {
    const int save_profile_every = 3;
    if (old_stage_index / save_profile_every <
        current_stage_index_ / save_profile_every) {
      LOG->Trace("Played %i stages; saving profiles ...", save_profile_every);
      PROFILEMAN->SaveMachineProfile();
      this->SavePlayerProfiles();
    }
  }
}

void GameState::LoadCurrentSettingsFromProfile(PlayerNumber pn) {
  if (!PROFILEMAN->IsPersistentProfile(pn)) {
    return;
  }

  const Profile* profile = PROFILEMAN->GetProfile(pn);

  // apply saved default modifiers if any
  RString modifiers;
  if (profile->GetDefaultModifiers(cur_game_, modifiers)) {
    // We don't save negative preferences (eg. "no reverse"). If the theme
    // sets a default of "reverse", and the player turns it off, we should
    // set it off. However, don't reset modifiers that aren't saved by the
    // profile, so we don't ignore unsaved modifiers when a profile is in use.
    PO_GROUP_CALL(
        player_state_[pn]->m_PlayerOptions, ModsLevel_Preferred,
        ResetSavedPrefs);
    ApplyPreferredModifiers(pn, modifiers);
  }
  // Only set the sort order if it wasn't already set by a GameCommand (or by an
  // earlier profile)
  if (preferred_sort_order_ == SortOrder_Invalid &&
      profile->m_SortOrder != SortOrder_Invalid) {
    preferred_sort_order_ = profile->m_SortOrder;
  }
  if (profile->m_LastDifficulty != Difficulty_Invalid) {
    preferred_difficulty_[pn].Set(profile->m_LastDifficulty);
  }
  if (profile->m_LastCourseDifficulty != Difficulty_Invalid) {
    preferred_course_difficulty_[pn].Set(profile->m_LastCourseDifficulty);
  }
  // Only set the PreferredStepsType if it wasn't already set by a GameCommand
  // (or by an earlier profile)
  if (preferred_steps_type_ == StepsType_Invalid &&
      profile->m_LastStepsType != StepsType_Invalid) {
    preferred_steps_type_.Set(profile->m_LastStepsType);
  }
  if (preferred_song_ == nullptr) {
    preferred_song_ = profile->m_lastSong.ToSong();
  }
  if (preferred_course_ == nullptr) {
    preferred_course_ = profile->m_lastCourse.ToCourse();
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
      player_state_[pn]->m_PlayerOptions.GetPreferred().GetSavedPrefsString());
  if (IsSongSort(preferred_sort_order_)) {
    pProfile->m_SortOrder = preferred_sort_order_;
  }
  if (preferred_difficulty_[pn] != Difficulty_Invalid) {
    pProfile->m_LastDifficulty = preferred_difficulty_[pn];
  }
  if (preferred_course_difficulty_[pn] != Difficulty_Invalid) {
    pProfile->m_LastCourseDifficulty = preferred_course_difficulty_[pn];
  }
  if (preferred_steps_type_ != StepsType_Invalid) {
    pProfile->m_LastStepsType = preferred_steps_type_;
  }
  if (preferred_song_) {
    pProfile->m_lastSong.FromSong(preferred_song_);
  }
  if (preferred_course_) {
    pProfile->m_lastCourse.FromCourse(preferred_course_);
  }
}

bool GameState::CanSafelyEnterGameplay(RString& reason) {
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
      if (steps->m_StepsType != style->m_StepsType) {
        reason = ssprintf(
            "Player %d StepsType %s for steps does not equal "
            "StepsType %s for style.",
            pn + 1, GAMEMAN->GetStepsTypeInfo(steps->m_StepsType).name,
            GAMEMAN->GetStepsTypeInfo(style->m_StepsType).name);
        return false;
      }
      if (steps->m_pSong != cur_song_) {
        reason = ssprintf(
            "Steps for player %d are not for the current song.", pn + 1);
        return false;
      }
      NoteData ndtemp;
      steps->GetNoteData(ndtemp);
      if (ndtemp.GetNumTracks() != style->m_iColsPerPlayer) {
        reason = ssprintf(
            "Steps for player %d have %d columns, style has %d "
            "columns.",
            pn + 1, ndtemp.GetNumTracks(), style->m_iColsPerPlayer);
        return false;
      }
    } else {
      const Trail* steps = cur_trail_[pn];
      if (steps == nullptr) {
        reason = ssprintf("Steps for player %d is null.", pn + 1);
        return false;
      }
      if (steps->m_StepsType != style->m_StepsType) {
        reason = ssprintf(
            "Player %d StepsType %s for steps does not equal "
            "StepsType %s for style.",
            pn + 1, GAMEMAN->GetStepsTypeInfo(steps->m_StepsType).name,
            GAMEMAN->GetStepsTypeInfo(style->m_StepsType).name);
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
      std::vector<StepsType> steps_types;
      GAMEMAN->GetStepsTypesForGame(cur_game_, steps_types);
      const Style* style = GAMEMAN->GetFirstCompatibleStyle(
          cur_game_, GetNumSidesJoined(), steps_types[0]);
      SetCurrentStyle(style, PLAYER_INVALID);
    }
  }
  if (!style_set) {
    FOREACH_EnabledPlayer(pn) {
      StepsType steps_type = StepsType_Invalid;
      if (cur_steps_[pn] != nullptr) {
        steps_type = cur_steps_[pn]->m_StepsType;
      } else if (cur_trail_[pn] != nullptr) {
        steps_type = cur_trail_[pn]->m_StepsType;
      } else {
        std::vector<StepsType> steps_types;
        GAMEMAN->GetStepsTypesForGame(cur_game_, steps_types);
        steps_type = steps_types[0];
      }
      const Style* style = GAMEMAN->GetFirstCompatibleStyle(
					cur_game_, GetNumSidesJoined(), steps_type);
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
    if (style->m_StyleType == StyleType_TwoPlayersSharedSides) {
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
    if (other_style->m_StyleType != StyleType_TwoPlayersSharedSides) {
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
                steps_to_match->m_StepsType)
            ->m_StyleType;
    FOREACH_EnabledPlayer(pn) {
      Trail* pn_steps = cur_trail_[pn].Get();
      bool match_failed = pn_steps == nullptr;
      if (steps_to_match != pn_steps && pn_steps != nullptr) {
        StyleType pn_styletype = GAMEMAN
                                     ->GetFirstCompatibleStyle(
                                         GAMESTATE->GetCurrentGame(),
                                         num_players, pn_steps->m_StepsType)
                                     ->m_StyleType;
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
                steps_to_match->m_StepsType)
            ->m_StyleType;
    RString music_to_match = steps_to_match->GetMusicFile();
    FOREACH_EnabledPlayer(pn) {
      Steps* pn_steps = cur_steps_[pn].Get();
      bool match_failed = pn_steps == nullptr;
      if (steps_to_match != pn_steps && pn_steps != nullptr) {
        StyleType pn_styletype = GAMEMAN
                                     ->GetFirstCompatibleStyle(
                                         GAMESTATE->GetCurrentGame(),
                                         num_players, pn_steps->m_StepsType)
                                     ->m_StyleType;
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

void GameState::Update(float delta) {
  song_options_.Update(delta);

  FOREACH_PlayerNumber(p) {
    player_state_[p]->Update(delta);

    if (!goal_complete_[p] && IsGoalComplete(p)) {
      goal_complete_[p] = true;
      MESSAGEMAN->Broadcast(
          (MessageID)(Message_GoalCompleteP1 + Enum::to_integral(p)));
    }
  }

  if (GAMESTATE->cur_course_) {
    if (GAMESTATE->cur_course_->m_fGoalSeconds > 0 &&
        !workout_goal_complete_) {
      const StageStats& cur_stage_stats = STATSMAN->m_CurStageStats;
      bool goal_complete = cur_stage_stats.m_fGameplaySeconds >
                           GAMESTATE->cur_course_->m_fGoalSeconds;
      if (goal_complete) {
        MESSAGEMAN->Broadcast("WorkoutGoalComplete");
        workout_goal_complete_ = true;
      }
    }
  }
}

void GameState::SetCurGame(const Game* game) {
  cur_game_.Set(game);
  RString game_str = game ? RString(game->name) : RString();
  PREFSMAN->SetCurrentGame(game_str);
}

const float GameState::MUSIC_SECONDS_INVALID = -5000.0f;

void GameState::ResetMusicStatistics() {
  position_.Reset();
  last_position_timer_.Touch();
  last_position_seconds_ = 0.0f;

  Actor::SetBGMTime(0, 0, 0, 0);

  FOREACH_PlayerNumber(p) { player_state_[p]->m_Position.Reset(); }
}

void GameState::ResetStageStatistics() {
  StageStats old_stats = STATSMAN->m_CurStageStats;
  STATSMAN->m_CurStageStats = StageStats();
  if (PREFSMAN->m_bComboContinuesBetweenSongs) {
    FOREACH_PlayerNumber(p) {
      bool first_song = current_stage_index_ == 0;
      if (first_song) {
        Profile* profile = PROFILEMAN->GetProfile(p);
        STATSMAN->m_CurStageStats.m_player[p].m_iCurCombo =
            profile->m_iCurrentCombo;
      } else {
        STATSMAN->m_CurStageStats.m_player[p].m_iCurCombo =
            old_stats.m_player[p].m_iCurCombo;
      }
    }
  }

  RemoveAllActiveAttacks();
  FOREACH_PlayerNumber(p) player_state_[p]->RemoveAllInventory();
  opponent_health_percent_ = 1;
  haste_rate_ = 0;
  last_haste_update_music_seconds_ = 0;
  accumulated_haste_seconds_ = 0;
  tug_life_percent_p1_ = 0.5f;
  FOREACH_PlayerNumber(pn) {
    player_state_[pn]->m_fSuperMeter = 0;
    player_state_[pn]->m_HealthState = HealthState_Alive;

    player_state_[pn]->m_iLastPositiveSumOfAttackLevels = 0;
		// PlayerAI not affected.
    player_state_[pn]->m_fSecondsUntilAttacksPhasedOut = 0;

    goal_complete_[pn] = false;
  }
  workout_goal_complete_ = false;

  FOREACH_PlayerNumber(pn) {
    last_stage_awards_[pn].clear();
    last_peak_combo_awards_[pn].clear();
  }

  // Reset the round seed. Do this here and not in FinishStage so that players
  // get new shuffle patterns if they Back out of gameplay and play again.
  stage_seed_ = rand();
}

void GameState::UpdateSongPosition(
    float position_seconds, const TimingData& timing,
    const RageTimer& timestamp) {
  // It's not uncommon to get a lot of duplicated positions from the sound
  // driver, like so: 13.120953,13.130975,13.130975,13.130975,13.140998,...
  // This causes visual stuttering of the arrows. To compensate, keep a
  // RageTimer since the last change.
  if (position_seconds == last_position_seconds_ && !is_paused_) {
    position_seconds += last_position_timer_.Ago();
  } else {
    last_position_timer_.Touch();
    last_position_seconds_ = position_seconds;
  }

  position_.UpdateSongPosition(position_seconds, timing, timestamp);

  FOREACH_EnabledPlayer(pn) {
    if (cur_steps_[pn]) {
      float additional_visual_delay =
          player_state_[pn]->m_PlayerOptions.GetPreferred().m_fVisualDelay;
      player_state_[pn]->m_Position.UpdateSongPosition(
          position_seconds, *cur_steps_[pn]->GetTimingData(), timestamp,
          additional_visual_delay);
      Actor::SetPlayerBGMBeat(
          pn, player_state_[pn]->m_Position.m_fSongBeatVisible,
          player_state_[pn]->m_Position.m_fSongBeatNoOffset);
    }
  }
  Actor::SetBGMTime(
      GAMESTATE->position_.m_fMusicSecondsVisible,
      GAMESTATE->position_.m_fSongBeatVisible, position_seconds,
      GAMESTATE->position_.m_fSongBeatNoOffset);
}

float GameState::GetSongPercent(float beat) const {
  // 0 = first step; 1 = last step
  float curTime = this->cur_song_->m_SongTiming.GetElapsedTimeFromBeat(beat);
  return (curTime - cur_song_->GetFirstSecond()) / cur_song_->GetLastSecond();
}

int GameState::GetNumStagesLeft(PlayerNumber pn) const {
  return player_stage_tokens_[pn];
}

int GameState::GetSmallestNumStagesLeftForAnyHumanPlayer() const {
  if (IsEventMode()) {
    return 999;
  }
  int smallest = INT_MAX;
  FOREACH_HumanPlayer(p) smallest = std::min(smallest, player_stage_tokens_[p]);
  return smallest;
}

bool GameState::IsFinalStageForAnyHumanPlayer() const {
  return GetSmallestNumStagesLeftForAnyHumanPlayer() == 1;
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
  song_cost *= adjust_tokens_by_song_cost_for_final_stage_check;
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
  // NOTE(kyz): Previous logic did not factor in current song length, or the
  // fact that players aren't allowed to start a song with 0 tokens. This new
  // function also has logic for handling the Gameplay and Evaluation cases
  // which used to require workarounds on the theme side.
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
  if (GAMESTATE->multiplayer_) {
    FOREACH_EnabledMultiPlayer(mp) return STATSMAN->m_CurStageStats
            .m_multiPlayer[mp]
            .m_iSongsPlayed -
        1;
    FAIL_M("At least one MultiPlayer must be joined.");
  } else {
    return STATSMAN->m_CurStageStats.m_player[this->GetMasterPlayerNumber()]
               .m_iSongsPlayed -
           1;
  }
}

// HACK: when we're loading a new course song, we want to display the new song
// number, even though we haven't started that song yet.
int GameState::GetLoadingCourseSongIndex() const {
  int iIndex = GetCourseSongIndex();
  if (loading_next_song_) {
    ++iIndex;
  }
  return iIndex;
}

static const char* prepare_song_failures[] = {
    "success",
    "no_current_song",
    "card_mount_failed",
    "load_interrupted",
};

int GameState::prepare_song_for_gameplay() {
  Song* cur_song = cur_song_;
  if (cur_song == nullptr) {
    return 1;
  }
  if (cur_song->m_LoadedFromProfile == ProfileSlot_Invalid) {
    return 0;
  }
  ProfileSlot profile_slot = cur_song->m_LoadedFromProfile;
  PlayerNumber slot_as_pn = PlayerNumber(profile_slot);
  if (!PROFILEMAN->ProfileWasLoadedFromMemoryCard(slot_as_pn)) {
    return 0;
  }
  if (!MEMCARDMAN->MountCard(slot_as_pn)) {
    return 2;
  }
  RString prof_dir = PROFILEMAN->GetProfileDir(profile_slot);
  // NOTE(Kyz): Song loading changes its paths to point to the cache area.
  RString to_dir = cur_song->GetSongDir();
  RString from_dir = cur_song->GetPreCustomifyDir();
  // NOTE(Kyz): The problem of what files to copy is complicated by steps being
  // able to specify their own music file, and the variety of step file formats.
  // Complex logic to figure out what files the song actually uses would be
  // bug prone.  Just copy all audio files and step files.
  std::vector<RString> copy_exts = ActorUtil::GetTypeExtensionList(FT_Sound);
  copy_exts.push_back("sm");
  copy_exts.push_back("ssc");
  copy_exts.push_back("lrc");
  std::vector<RString> files_in_dir;
  FILEMAN->GetDirListingWithMultipleExtensions(
      from_dir, copy_exts, files_in_dir);
  for (std::size_t i = 0; i < files_in_dir.size(); ++i) {
    RString& fname = files_in_dir[i];
    if (!FileCopy(from_dir + fname, to_dir + fname)) {
      return 3;
    }
  }
  MEMCARDMAN->UnmountCard(slot_as_pn);
  return 0;
}

static LocalizedString PLAYER1("GameState", "Player 1");
static LocalizedString PLAYER2("GameState", "Player 2");
static LocalizedString CPU("GameState", "CPU");
RString GameState::GetPlayerDisplayName(PlayerNumber pn) const {
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
  // NOTE(Kyz): If we check the style and it comes up nullptr, either the style
  // has not been chosen, or we're on ScreenSelectMusic with AutoSetStyle. If
  // the style does not come up nullptr, we might be on a screen in a custom
  // theme that wants to allow joining after the style is set anyway. Either
  // way, we can't use the existence of a style to decide.
  if (ALLOW_LATE_JOIN.IsLoaded() && ALLOW_LATE_JOIN) {
    Screen* screen = SCREENMAN->GetTopScreen();
    if (screen) {
      if (!screen->AllowLateJoin()) {
        return false;
      }
    }
    // NOTE(Kyz): We can't use FOREACH_EnabledPlayer because that uses
    // PlayersCanJoin in part of its logic chain.
    FOREACH_PlayerNumber(pn) {
      const Style* style = GetCurrentStyle(pn);
      if (style) {
        const Style* compat_style =
            GAMEMAN->GetFirstCompatibleStyle(cur_game_, 2, style->m_StepsType);
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
  int num_sides_joined = 0;
	// left side, and right side
  FOREACH_PlayerNumber(p) if (side_is_joined_[p]) num_sides_joined++;
  return num_sides_joined;
}

const Game* GameState::GetCurrentGame() const {
	// The game must be set before calling this.
  ASSERT(cur_game_ != nullptr);
  return cur_game_;
}

const Style* GameState::GetCurrentStyle(PlayerNumber pn) const {
  if (GetCurrentGame() == nullptr) {
    return nullptr;
  }
  if (!GetCurrentGame()->players_have_separate_styles) {
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
  if (!GetCurrentGame()->players_have_separate_styles) {
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
        GetCurrentStyle(pn)->m_StyleType == StyleType_OnePlayerTwoSides) {
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

bool GameState::SetCompatibleStyle(StepsType steps_type, PlayerNumber pn) {
  bool style_incompatible = false;
  if (!GetCurrentStyle(pn)) {
    style_incompatible = true;
  } else {
    style_incompatible = steps_type != GetCurrentStyle(pn)->m_StepsType;
  }
  if (CommonMetrics::AUTO_SET_STYLE && style_incompatible) {
    const Style* compatible_style = GAMEMAN->GetFirstCompatibleStyle(
        cur_game_, GetNumSidesJoined(), steps_type);
    if (!compatible_style) {
      return false;
    }
    SetCurrentStyle(compatible_style, pn);
  }
  return steps_type == GetCurrentStyle(pn)->m_StepsType;
}

bool GameState::IsPlayerEnabled(PlayerNumber pn) const {
  // In rave, all players are present. Non-human players are CPU controlled.
  switch (play_mode_) {
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      return true;
    default:
      return IsHumanPlayer(pn);
  }
}

bool GameState::IsMultiPlayerEnabled(MultiPlayer multiplayer) const {
  return multiplayer_status_[multiplayer] == MultiPlayerStatus_Joined;
}

bool GameState::IsPlayerEnabled(const PlayerState* player_state) const {
  if (player_state->m_mp != MultiPlayer_Invalid) {
    return IsMultiPlayerEnabled(player_state->m_mp);
  }
  if (player_state->m_PlayerNumber != PLAYER_INVALID) {
    return IsPlayerEnabled(player_state->m_PlayerNumber);
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

  if (GetCurrentGame()->players_have_separate_styles) {
		// No style chosen.
    if (GetCurrentStyle(pn) == nullptr) {
      return side_is_joined_[pn];
    } else {
      StyleType style_type = GetCurrentStyle(pn)->m_StyleType;
      switch (style_type) {
        case StyleType_TwoPlayersTwoSides:
        case StyleType_TwoPlayersSharedSides:
          return true;
        case StyleType_OnePlayerOneSide:
        case StyleType_OnePlayerTwoSides:
          return pn == this->GetMasterPlayerNumber();
        default:
          FAIL_M(ssprintf("Invalid style type: %i", style_type));
      }
    }
  }
	// No style chosen.
  if (GetCurrentStyle(pn) == nullptr) {
		// Only allow input from sides that have already joined.
    return side_is_joined_[pn];
  }

  StyleType style_type = GetCurrentStyle(pn)->m_StyleType;
  switch (style_type) {
    case StyleType_TwoPlayersTwoSides:
    case StyleType_TwoPlayersSharedSides:
      return true;
    case StyleType_OnePlayerOneSide:
    case StyleType_OnePlayerTwoSides:
      return pn == this->GetMasterPlayerNumber();
    default:
      FAIL_M(ssprintf("Invalid style type: %i", style_type));
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

PlayerNumber GameState::GetFirstDisabledPlayer() const {
  FOREACH_PlayerNumber(pn) if (!IsPlayerEnabled(pn)) return pn;
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
  if (IsEventMode()) {
    return EarnedExtraStage_No;
  }

  if (!PREFSMAN->m_bAllowExtraStage) {
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
    Difficulty difficulty = cur_steps_[pn]->GetDifficulty();
    switch (difficulty) {
      case Difficulty_Edit:
        if (!EDIT_ALLOWED_FOR_EXTRA) {
					// Can't use edit steps.
          continue;
        }
        break;
      default:
        if (difficulty < MIN_DIFFICULTY_FOR_EXTRA) {
					// Not hard enough!
          continue;
        }
        break;
    }

    if (IsExtraStage()) {
      if (ALLOW_EXTRA_2 && STATSMAN->m_CurStageStats.m_player[pn].GetGrade() <=
                               GRADE_TIER_FOR_EXTRA_2) {
        return EarnedExtraStage_Extra2;
      }
    } else if (
        STATSMAN->m_CurStageStats.m_player[pn].GetGrade() <=
        GRADE_TIER_FOR_EXTRA_1) {
      return EarnedExtraStage_Extra1;
    }
  }

  return EarnedExtraStage_No;
}

PlayerNumber GameState::GetBestPlayer() const {
  FOREACH_PlayerNumber(pn) if (GetStageResult(pn) == RESULT_WIN) return pn;
	// Draw
  return PLAYER_INVALID;
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
    if (STATSMAN->m_CurStageStats.m_player[p].m_iActualDancePoints ==
        STATSMAN->m_CurStageStats.m_player[pn].m_iActualDancePoints) {
      win = RESULT_DRAW;
    }

    // If anyone did better, we lost.
    if (STATSMAN->m_CurStageStats.m_player[p].m_iActualDancePoints >
        STATSMAN->m_CurStageStats.m_player[pn].m_iActualDancePoints) {
      return RESULT_LOSE;
    }
  }
  return win;
}

void GameState::GetDefaultPlayerOptions(PlayerOptions& player_options) {
  player_options.Init();
  player_options.FromString(PREFSMAN->m_sDefaultModifiers);
  player_options.FromString(CommonMetrics::DEFAULT_MODIFIERS);
  if (player_options.m_sNoteSkin.empty()) {
    player_options.m_sNoteSkin = CommonMetrics::DEFAULT_NOTESKIN_NAME;
  }
}

void GameState::GetDefaultSongOptions(SongOptions& song_options) {
  song_options.Init();
  song_options.FromString(PREFSMAN->m_sDefaultModifiers);
  song_options.FromString(CommonMetrics::DEFAULT_MODIFIERS);
}

void GameState::ResetToDefaultSongOptions(ModsLevel mods_level) {
  SongOptions song_options;
  GetDefaultSongOptions(song_options);
  song_options_.Assign(mods_level, song_options);
}

void GameState::ApplyPreferredModifiers(PlayerNumber pn, RString modifiers) {
  player_state_[pn]->m_PlayerOptions.FromString(
      ModsLevel_Preferred, modifiers);
  song_options_.FromString(ModsLevel_Preferred, modifiers);
}

void GameState::ApplyStageModifiers(PlayerNumber pn, RString modifiers) {
  player_state_[pn]->m_PlayerOptions.FromString(ModsLevel_Stage, modifiers);
  song_options_.FromString(ModsLevel_Stage, modifiers);
}

void GameState::ClearStageModifiersIllegalForCourse() {
  FOREACH_EnabledPlayer(pn) PO_GROUP_CALL(
      player_state_[pn]->m_PlayerOptions, ModsLevel_Stage,
      ResetSavedPrefsInvalidForCourse);
}

bool GameState::CurrentOptionsDisqualifyPlayer(PlayerNumber pn) {
  if (!PREFSMAN->m_bDisqualification) {
    return false;
  }

  if (!IsHumanPlayer(pn)) {
    return false;
  }

  const PlayerOptions& player_options =
			player_state_[pn]->m_PlayerOptions.GetPreferred();
  const SongOptions& song_options = song_options_.GetPreferred();

  // NOTE(x0rbl): Playing a song/course at a slower music rate should disqualify
	// score.
  if (song_options.m_fMusicRate < 1.0) {
    return true;
  }

  // Check the stored player options for disqualify. Don't disqualify because of
	// mods that were forced.
  if (IsCourseMode()) {
    return player_options.IsEasierForCourseAndTrail(
				cur_course_, cur_trail_[pn]);
  } else {
    return player_options.IsEasierForSongAndSteps(
				cur_song_, cur_steps_[pn], pn);
  }
}

void GameState::GetAllUsedNoteSkins(std::vector<RString>& out) const {
  FOREACH_EnabledPlayer(pn) {
    out.push_back(player_state_[pn]->m_PlayerOptions.GetCurrent().m_sNoteSkin);

    // Add noteskins that are used in courses.
    if (IsCourseMode()) {
      const Trail* trail = cur_trail_[pn];
      ASSERT(trail != nullptr);

      for (const TrailEntry& trail_entry : trail->m_vEntries) {
        PlayerOptions player_options;
        player_options.FromString(trail_entry.Modifiers);
        if (!player_options.m_sNoteSkin.empty()) {
          out.push_back(player_options.m_sNoteSkin);
        }
      }
    }
  }

  // Remove duplicates.
  sort(out.begin(), out.end());
  out.erase(unique(out.begin(), out.end()), out.end());
}

// Called on end of song.
void GameState::RemoveAllActiveAttacks() {
  FOREACH_PlayerNumber(p) player_state_[p]->RemoveActiveAttacks();
}

void GameState::AddStageToPlayer(PlayerNumber pn) {
  // NOTE(cerbo): Add one stage more to player (bonus)
  ++player_stage_tokens_[pn];
}

template <class T>
void setmin(T& a, const T& b) {
  a = std::min(a, b);
}

template <class T>
void setmax(T& a, const T& b) {
  a = std::max(a, b);
}

FailType GameState::GetPlayerFailType(const PlayerState* player_state) const {
  PlayerNumber pn = player_state->m_PlayerNumber;
  FailType fail_type = player_state->m_PlayerOptions.GetCurrent().m_FailType;

  // If the player changed the fail mode explicitly, leave it alone.
  if (fail_type_was_explicitly_set_) {
    return fail_type;
  }

  if (IsCourseMode()) {
    if (PREFSMAN->m_bMinimum1FullSongInCourses && GetCourseSongIndex() == 0) {
			// Take the least harsh of the two FailTypes.
      fail_type = std::max(fail_type, FailType_ImmediateContinue);
    }
  } else {
    Difficulty difficulty = Difficulty_Invalid;
    if (cur_steps_[pn]) {
      difficulty = cur_steps_[pn]->GetDifficulty();
    }

    bool is_first_stage = false;
    if (!IsEventMode()) {
			// HACK; -1 because this is called during gameplay
      is_first_stage |= player_stage_tokens_[player_state->m_PlayerNumber] ==
                     		PREFSMAN->m_iSongsPerPlay - 1;
    }

    // Easy and beginner are never harder than FAIL_IMMEDIATE_CONTINUE.
    if (difficulty <= Difficulty_Easy) {
      setmax(fail_type, FailType_ImmediateContinue);
    }

    if (difficulty <= Difficulty_Easy && is_first_stage &&
        PREFSMAN->m_bFailOffForFirstStageEasy) {
      setmax(fail_type, FailType_Off);
    }

    // If beginner's steps were chosen, and this is the first stage,
    // turn off failure completely.
    if (difficulty == Difficulty_Beginner && is_first_stage) {
      setmax(fail_type, FailType_Off);
    }

    if (difficulty == Difficulty_Beginner && PREFSMAN->m_bFailOffInBeginner) {
      setmax(fail_type, FailType_Off);
    }
  }

  return fail_type;
}

bool GameState::ShowW1() const {
  AllowW1 allow_w1_pref = PREFSMAN->m_AllowW1;
  switch (allow_w1_pref) {
    case ALLOW_W1_NEVER:
      return false;
    case ALLOW_W1_COURSES_ONLY:
      return IsCourseMode();
    case ALLOW_W1_EVERYWHERE:
      return true;
    default:
      FAIL_M(ssprintf("Invalid AllowW1 preference: %i", allow_w1_pref));
  }
}

static ThemeMetric<bool> PROFILE_RECORD_FEATS(
    "GameState", "ProfileRecordFeats");
static ThemeMetric<bool> CATEGORY_RECORD_FEATS(
    "GameState", "CategoryRecordFeats");

void GameState::GetRankingFeats(
    PlayerNumber pn, std::vector<RankingFeat>& feats_out) const {
  if (!IsHumanPlayer(pn)) {
    return;
  }

  Profile* profile = PROFILEMAN->GetProfile(pn);

  // Check for feats even if the PlayMode is rave or battle because the player
  // may have made high scores then switched modes.
  PlayMode play_mode = play_mode_.Get();
  const char* play_mode_str = PlayModeToString(play_mode).c_str();

  CHECKPOINT_M(ssprintf("Getting the feats for %s", play_mode_str));
  switch (play_mode) {
    case PLAY_MODE_REGULAR:
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE: {
      // Find unique Song and Steps combinations that were played.
      // We must keep only the unique combination or else we'll double-count
      // high score markers.
      std::vector<SongAndSteps> song_and_steps;

      for (unsigned i = 0; i < STATSMAN->m_vPlayedStageStats.size(); ++i) {
        CHECKPOINT_M(
            ssprintf("%u/%i", i, (int)STATSMAN->m_vPlayedStageStats.size()));
        SongAndSteps sas;
        ASSERT(!STATSMAN->m_vPlayedStageStats[i].m_vpPlayedSongs.empty());
        sas.pSong = STATSMAN->m_vPlayedStageStats[i].m_vpPlayedSongs[0];
        ASSERT(sas.pSong != nullptr);
        if (STATSMAN->m_vPlayedStageStats[i]
                .m_player[pn]
                .m_vpPossibleSteps.empty()) {
          continue;
        }
        sas.pSteps =
            STATSMAN->m_vPlayedStageStats[i].m_player[pn].m_vpPossibleSteps[0];
        ASSERT(sas.pSteps != nullptr);
        song_and_steps.push_back(sas);
      }
      CHECKPOINT_M(ssprintf("All songs/steps from %s gathered", play_mode_str));

      sort(song_and_steps.begin(), song_and_steps.end());

      std::vector<SongAndSteps>::iterator to_delete =
          unique(song_and_steps.begin(), song_and_steps.end());
      song_and_steps.erase(to_delete, song_and_steps.end());

      CHECKPOINT_M("About to find records from the gathered.");
      for (unsigned i = 0; i < song_and_steps.size(); ++i) {
        Song* song = song_and_steps[i].pSong;
        Steps* steps = song_and_steps[i].pSteps;

        // Find Machine Records
        {
          HighScoreList& hsl =
              PROFILEMAN->GetMachineProfile()->GetStepsHighScoreList(
                  song, steps);
          for (unsigned j = 0; j < hsl.high_scores_.size(); ++j) {
            HighScore& high_score = hsl.high_scores_[j];

            if (high_score.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
              continue;
            }

            RankingFeat feat;
            feat.type = RankingFeat::SONG;
            feat.song = song;
            feat.steps = steps;
            feat.feat = ssprintf(
                "MR #%d in %s %s", j + 1, song->GetTranslitMainTitle().c_str(),
                DifficultyToString(steps->GetDifficulty()).c_str());
            feat.string_to_fill = high_score.GetNameMutable();
            feat.grade = high_score.GetGrade();
            feat.percent_dp = high_score.GetPercentDP();
            feat.score = high_score.GetScore();

            if (song->HasBanner()) {
              feat.banner = song->GetBannerPath();
            }

            feats_out.push_back(feat);
          }
        }

        // Find Personal Records
        if (profile && PROFILE_RECORD_FEATS) {
          HighScoreList& hsl = profile->GetStepsHighScoreList(song, steps);
          for (unsigned j = 0; j < hsl.high_scores_.size(); j++) {
            HighScore& high_score = hsl.high_scores_[j];

            if (high_score.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
              continue;
            }

            RankingFeat feat;
            feat.song = song;
            feat.steps = steps;
            feat.type = RankingFeat::SONG;
            feat.feat = ssprintf(
                "PR #%d in %s %s", j + 1, song->GetTranslitMainTitle().c_str(),
                DifficultyToString(steps->GetDifficulty()).c_str());
            feat.string_to_fill = high_score.GetNameMutable();
            feat.grade = high_score.GetGrade();
            feat.percent_dp = high_score.GetPercentDP();
            feat.score = high_score.GetScore();

            // NOTE(aj): Why is this here?
            if (song->HasBackground()) {
              feat.banner = song->GetBackgroundPath();
            }

            feats_out.push_back(feat);
          }
        }
      }

      CHECKPOINT_M("Getting the final evaluation stage stats.");
      StageStats stats;
      STATSMAN->GetFinalEvalStageStats(stats);
      StepsType steps_type = GetCurrentStyle(pn)->m_StepsType;

      // Find Machine Category Records
      FOREACH_ENUM(RankingCategory, ranking_category) {
        if (!CATEGORY_RECORD_FEATS) {
          continue;
        }
        HighScoreList& hsl =
            PROFILEMAN->GetMachineProfile()->GetCategoryHighScoreList(
								steps_type, ranking_category);
        for (unsigned j = 0; j < hsl.high_scores_.size(); ++j) {
          HighScore& hs = hsl.high_scores_[j];
          if (hs.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
            continue;
          }

          RankingFeat feat;
          feat.type = RankingFeat::CATEGORY;
          feat.feat = ssprintf(
              "MR #%d in Type %c (%d)", j + 1, 'A' + ranking_category,
              stats.GetAverageMeter(pn));
          feat.string_to_fill = hs.GetNameMutable();
          feat.grade = Grade_NoData;
          feat.score = hs.GetScore();
          feat.percent_dp = hs.GetPercentDP();
          feats_out.push_back(feat);
        }
      }

      // Find Personal Category Records
      FOREACH_ENUM(RankingCategory, ranking_category) {
        if (!CATEGORY_RECORD_FEATS) {
          continue;
        }

        if (profile && PROFILE_RECORD_FEATS) {
          HighScoreList& hsl = profile->GetCategoryHighScoreList(
							steps_type, ranking_category);
          for (unsigned j = 0; j < hsl.high_scores_.size(); ++j) {
            HighScore& high_score = hsl.high_scores_[j];
            if (high_score.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
              continue;
            }

            RankingFeat feat;
            feat.type = RankingFeat::CATEGORY;
            feat.feat = ssprintf(
                "PR #%d in Type %c (%d)", j + 1, 'A' + ranking_category,
                stats.GetAverageMeter(pn));
            feat.string_to_fill = high_score.GetNameMutable();
            feat.grade = Grade_NoData;
            feat.score = high_score.GetScore();
            feat.percent_dp = high_score.GetPercentDP();
            feats_out.push_back(feat);
          }
        }
      }
    } break;
    case PLAY_MODE_NONSTOP:
    case PLAY_MODE_ONI:
    case PLAY_MODE_ENDLESS: {
      Course* course = cur_course_;
      ASSERT(course != nullptr);
      Trail* trail = cur_trail_[pn];
      ASSERT(trail != nullptr);
      CourseDifficulty course_difficulty = trail->m_CourseDifficulty;

      // Find Machine Records
      {
        Profile* pProfile = PROFILEMAN->GetMachineProfile();
        HighScoreList& hsl = pProfile->GetCourseHighScoreList(course, trail);
        for (unsigned i = 0; i < hsl.high_scores_.size(); ++i) {
          HighScore& high_score = hsl.high_scores_[i];
          if (high_score.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
            continue;
          }

          RankingFeat feat;
          feat.type = RankingFeat::COURSE;
          feat.course = course;
          feat.feat = ssprintf(
              "MR #%d in %s", i + 1, course->GetDisplayFullTitle().c_str());
          if (course_difficulty != Difficulty_Medium) {
            feat.feat += " " + CourseDifficultyToLocalizedString(
								course_difficulty);
          }
          feat.string_to_fill = high_score.GetNameMutable();
          feat.grade = Grade_NoData;
          feat.score = high_score.GetScore();
          feat.percent_dp = high_score.GetPercentDP();
          if (course->HasBanner()) {
            feat.banner = course->GetBannerPath();
          }
          feats_out.push_back(feat);
        }
      }

      // Find Personal Records
      if (PROFILE_RECORD_FEATS && PROFILEMAN->IsPersistentProfile(pn)) {
        HighScoreList& hsl = profile->GetCourseHighScoreList(course, trail);
        for (unsigned i = 0; i < hsl.high_scores_.size(); ++i) {
          HighScore& high_score = hsl.high_scores_[i];
          if (high_score.GetName() != RANKING_TO_FILL_IN_MARKER[pn]) {
            continue;
          }

          RankingFeat feat;
          feat.type = RankingFeat::COURSE;
          feat.course = course;
          feat.feat = ssprintf(
              "PR #%d in %s", i + 1, course->GetDisplayFullTitle().c_str());
          feat.string_to_fill = high_score.GetNameMutable();
          feat.grade = Grade_NoData;
          feat.score = high_score.GetScore();
          feat.percent_dp = high_score.GetPercentDP();
          if (course->HasBanner()) {
            feat.banner = course->GetBannerPath();
          }
          feats_out.push_back(feat);
        }
      }
    } break;
    default:
      FAIL_M(ssprintf("Invalid play mode: %i", int(play_mode_)));
  }
}

bool GameState::AnyPlayerHasRankingFeats() const {
  std::vector<RankingFeat> feats;
  FOREACH_PlayerNumber(p) {
    GetRankingFeats(p, feats);
    if (!feats.empty()) {
      return true;
    }
  }
  return false;
}

void GameState::StoreRankingName(PlayerNumber pn, RString name) {
  // NOTE(Kyz): The theme can upper it if desired.
  // name.MakeUpper();

  if (USE_NAME_BLACKLIST) {
    RageFile file;
    if (file.Open(NAME_BLACKLIST_FILE)) {
      RString line;

      while (!file.AtEOF()) {
        if (file.GetLine(line) == -1) {
          LuaHelpers::ReportScriptErrorFmt(
              "Error reading \"%s\": %s", NAME_BLACKLIST_FILE,
              file.GetError().c_str());
          break;
        }

        line.MakeUpper();
				// Name contains a bad word.
        if (!line.empty() && name.find(line) != std::string::npos) {
          LOG->Trace(
              "entered '%s' matches blacklisted item '%s'", name.c_str(),
              line.c_str());
          name = "";
          break;
        }
      }
    }
  }

  std::vector<RankingFeat> feats;
  GetRankingFeats(pn, feats);

  for (unsigned i = 0; i < feats.size(); i++) {
    *feats[i].string_to_fill = name;

    // Save name pointers as we fill them.
    names_that_were_filled_.push_back(feats[i].string_to_fill);
  }

  players_that_were_filled_.insert(pn);

  // Only attempt to remove/clamp scores after the last enabled player has saved
  // their scores.
  if (GAMESTATE->GetNumPlayersEnabled() <=
      static_cast<int>(players_that_were_filled_.size())) {
    StepsType steps_type = GetCurrentStyle(pn)->m_StepsType;
    PlayMode play_mode = play_mode_.Get();
    Profile* profile = PROFILEMAN->GetMachineProfile();
    switch (play_mode) {
      case PLAY_MODE_REGULAR:
      case PLAY_MODE_BATTLE:
      case PLAY_MODE_RAVE: {
        // Find unique Song and Steps combinations that were played.
        // Code is taken from implementation in GetRankingFeats().
        std::vector<SongAndSteps> song_and_steps;

        for (unsigned i = 0; i < STATSMAN->m_vPlayedStageStats.size(); ++i) {
          CHECKPOINT_M(
              ssprintf("%u/%i", i, (int)STATSMAN->m_vPlayedStageStats.size()));
          SongAndSteps sas;
          ASSERT(!STATSMAN->m_vPlayedStageStats[i].m_vpPlayedSongs.empty());
          sas.pSong = STATSMAN->m_vPlayedStageStats[i].m_vpPlayedSongs[0];
          ASSERT(sas.pSong != nullptr);

          if (STATSMAN->m_vPlayedStageStats[i]
                  .m_player[pn]
                  .m_vpPossibleSteps.empty()) {
            continue;
          }
          sas.pSteps = STATSMAN->m_vPlayedStageStats[i]
                           .m_player[pn]
                           .m_vpPossibleSteps[0];
          ASSERT(sas.pSteps != nullptr);
          song_and_steps.push_back(sas);
        }

        std::vector<SongAndSteps>::iterator to_delete =
            std::unique(song_and_steps.begin(), song_and_steps.end());
        song_and_steps.erase(to_delete, song_and_steps.end());

        for (unsigned i = 0; i < song_and_steps.size(); ++i) {
          HighScoreList& hsl = profile->GetStepsHighScoreList(
              song_and_steps[i].pSong, song_and_steps[i].pSteps);
          if (!PREFSMAN->m_bAllowMultipleHighScoreWithSameName) {
            // Erase all but the highest score for each name.
            hsl.RemoveAllButOneOfEachName();
          }
          hsl.ClampSize(true);
        }
      } break;
      case PLAY_MODE_NONSTOP:
      case PLAY_MODE_ONI:
      case PLAY_MODE_ENDLESS: {
        // Code is taken from implementation in GetRankingFeats().
        Course* course = cur_course_;
        ASSERT(course != nullptr);
        Trail* trail = cur_trail_[pn];
        ASSERT(trail != nullptr);
        CourseDifficulty course_difficulty = trail->m_CourseDifficulty;
        HighScoreList& hsl = profile->GetCourseHighScoreList(course, trail);
        if (!PREFSMAN->m_bAllowMultipleHighScoreWithSameName) {
          // Erase all but the highest score for each name.
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
  FOREACH_EnabledPlayer(p) if (
      player_state_[p]->m_HealthState < HealthState_Danger) return false;
  return true;
}

bool GameState::OneIsHot() const {
  FOREACH_EnabledPlayer(
      p) if (player_state_[p]->m_HealthState == HealthState_Hot) return true;
  return false;
}

bool GameState::IsTimeToPlayAttractSounds() const {
  // num_times_through_attract_ will be -1 from the first attract screen after
  // the end of a game until the next time FIRST_ATTRACT_SCREEN is reached.
  // Play attract sounds for this sort span of time regardless of
  // m_AttractSoundFrequency because it's awkward to have the machine go
  // silent immediately after the end of a game.
  if (num_times_through_attract_ == -1) {
    return true;
  }

  if (PREFSMAN->m_AttractSoundFrequency == ASF_NEVER) {
    return false;
  }

  // Play attract sounds once every m_iAttractSoundFrequency times through.
  if ((num_times_through_attract_ % PREFSMAN->m_AttractSoundFrequency) == 0) {
    return true;
  }

  return false;
}

void GameState::VisitAttractScreen(const RString screem_name) {
  if (screem_name == CommonMetrics::FIRST_ATTRACT_SCREEN.GetValue()) {
    num_times_through_attract_++;
  }
}

bool GameState::DifficultiesLocked() const {
  if (play_mode_ == PLAY_MODE_RAVE) {
    return true;
  }
  if (IsCourseMode()) {
    return PREFSMAN->m_bLockCourseDifficulties;
  }
  if (GetCurrentStyle(PLAYER_INVALID)->m_bLockDifficulties) {
    return true;
  }
  return false;
}

bool GameState::ChangePreferredDifficultyAndStepsType(
    PlayerNumber pn, Difficulty difficulty, StepsType steps_type) {
  preferred_difficulty_[pn].Set(difficulty);
  preferred_steps_type_.Set(steps_type);
  if (DifficultiesLocked()) {
    FOREACH_PlayerNumber(p) if (p != pn) preferred_difficulty_[p].Set(
        preferred_difficulty_[pn]);
  }

  return true;
}

// When only displaying difficulties in DIFFICULTIES_TO_SHOW, use
// GetClosestShownDifficulty to find which difficulty to show, and
// ChangePreferredDifficulty(pn, dir) to change difficulty.
bool GameState::ChangePreferredDifficulty(PlayerNumber pn, int dir) {
  const std::vector<Difficulty>& difficulties =
      CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue();

  Difficulty difficulty = GetClosestShownDifficulty(pn);
  while(true) {
    difficulty = enum_add2(difficulty, dir);
    if (difficulty < 0 || difficulty >= NUM_Difficulty) {
      return false;
    }
		auto it = std::find(difficulties.begin(), difficulties.end(), difficulty);
    if (it != difficulties.end()) {
			// Found.
      break;
    }
  }
  preferred_difficulty_[pn].Set(difficulty);
  return true;
}

// The user may be set to prefer a difficulty that isn't always shown;
// typically, Difficulty_Edit. Return the closest shown difficulty <=
// preferred_difficulty_.
Difficulty GameState::GetClosestShownDifficulty(PlayerNumber pn) const {
  const std::vector<Difficulty>& difficulties =
      CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue();

  Difficulty closest = (Difficulty)0;
  int closest_dist = -1;
  for (const Difficulty& difficulty : difficulties) {
    int dist = preferred_difficulty_[pn] - difficulty;
    if (dist < 0) {
      continue;
    }
    if (closest_dist != -1 && dist > closest_dist) {
      continue;
    }
    closest_dist = dist;
    closest = difficulty;
  }

  return closest;
}

bool GameState::ChangePreferredCourseDifficultyAndStepsType(
    PlayerNumber pn, CourseDifficulty course_difficulty, StepsType steps_type) {
  preferred_course_difficulty_[pn].Set(course_difficulty);
  preferred_steps_type_.Set(steps_type);
  if (PREFSMAN->m_bLockCourseDifficulties) {
    FOREACH_PlayerNumber(p) if (p != pn) preferred_course_difficulty_[p].Set(
        preferred_course_difficulty_[pn]);
  }

  return true;
}

bool GameState::ChangePreferredCourseDifficulty(PlayerNumber pn, int dir) {
  // If we have a course selected, only choose among difficulties available in
  // the course.
  const Course* course = cur_course_;

  const std::vector<CourseDifficulty>& course_difficulties =
      CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue();

  CourseDifficulty course_difficulty = preferred_course_difficulty_[pn];
  while(true) {
    course_difficulty = enum_add2(course_difficulty, dir);
    if (course_difficulty < 0 || course_difficulty >= NUM_Difficulty) {
      return false;
    }
		auto it = std::find(course_difficulties.begin(), course_difficulties.end(),
												course_difficulty);
    if (it == course_difficulties.end()) {
			// Not available.
      continue;
    }
    if (!course ||
			  course->GetTrail(GetCurrentStyle(pn)->m_StepsType, course_difficulty)) {
      break;
    }
  }
  return ChangePreferredCourseDifficulty(pn, course_difficulty);
}

bool GameState::IsCourseDifficultyShown(CourseDifficulty course_difficulty) {
  const std::vector<CourseDifficulty>& course_difficulties =
      CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue();
	auto it = std::find(course_difficulties.begin(), course_difficulties.end(),
											course_difficulty);
  return it != course_difficulties.end();
}

Difficulty GameState::GetEasiestStepsDifficulty() const {
  Difficulty difficulty = Difficulty_Invalid;
  FOREACH_HumanPlayer(p) {
    if (cur_steps_[p] == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "GetEasiestStepsDifficulty called but p%i hasn't chosen notes",
          p + 1);
      continue;
    }
    difficulty = std::min(difficulty, cur_steps_[p]->GetDifficulty());
  }
  return difficulty;
}

Difficulty GameState::GetHardestStepsDifficulty() const {
  Difficulty difficulty = Difficulty_Beginner;
  FOREACH_HumanPlayer(p) {
    if (cur_steps_[p] == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "GetHardestStepsDifficulty called but p%i hasn't chosen notes",
          p + 1);
      continue;
    }
    difficulty = std::max(difficulty, cur_steps_[p]->GetDifficulty());
  }
  return difficulty;
}

void GameState::SetNewStageSeed() { stage_seed_ = rand(); }

bool GameState::IsEventMode() const {
  return temporary_event_mode_ || PREFSMAN->m_bEventMode;
}

CoinMode GameState::GetCoinMode() const {
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
  const Profile* profile = PROFILEMAN->GetProfile(pn);
  const StageStats& cur_stage_stats = STATSMAN->m_CurStageStats;
  const PlayerStageStats& cur_player_stage_stats = cur_stage_stats.m_player[pn];

  float actual = 0;
  float goal = 0;
  switch (profile->m_GoalType) {
    case GoalType_Calories:
      actual = cur_player_stage_stats.m_fCaloriesBurned;
      goal = (float)profile->m_iGoalCalories;
      break;
    case GoalType_Time:
      actual = cur_stage_stats.m_fGameplaySeconds;
      goal = (float)profile->m_iGoalSeconds;
      break;
    case GoalType_None:
			// Never complete.
      return 0;
    default:
      FAIL_M(ssprintf("Invalid GoalType: %i", profile->m_GoalType));
  }
  if (goal == 0) {
    return 0;
  } else {
    return actual / goal;
  }
}

bool GameState::PlayerIsUsingModifier(
    PlayerNumber pn, const RString& modifier) {
  PlayerOptions player_options =
			player_state_[pn]->m_PlayerOptions.GetCurrent();
  SongOptions song_options = song_options_.GetCurrent();
  player_options.FromString(modifier);
  song_options.FromString(modifier);

  return player_options == player_state_[pn]->m_PlayerOptions.GetCurrent() &&
         song_options == song_options_.GetCurrent();
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

MultiPlayer GetNextEnabledMultiPlayer(MultiPlayer mp) {
  for (enum_add(mp, 1); mp < NUM_MultiPlayer; enum_add(mp, 1)) {
    if (GAMESTATE->IsMultiPlayerEnabled(mp)) {
      return mp;
    }
  }
  return MultiPlayer_Invalid;
}

// lua start
#include "Game.h"
#include "LuaBinding.h"

// Allow Lua to have access to the GameState.
class LunaGameState : public Luna<GameState> {
 public:
  DEFINE_METHOD(
      IsPlayerEnabled, IsPlayerEnabled(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(IsHumanPlayer, IsHumanPlayer(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(
      GetPlayerDisplayName,
      GetPlayerDisplayName(Enum::Check<PlayerNumber>(L, 1)))
  DEFINE_METHOD(GetMasterPlayerNumber, GetMasterPlayerNumber())
  DEFINE_METHOD(GetMultiplayer, multiplayer_)
  static int SetMultiplayer(T* p, lua_State* L) {
    p->multiplayer_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(InStepEditor, in_step_editor_);
  DEFINE_METHOD(GetNumMultiplayerNoteFields, num_multiplayer_note_fields_)
  DEFINE_METHOD(ShowW1, ShowW1())

  static int SetNumMultiplayerNoteFields(T* p, lua_State* L) {
    p->num_multiplayer_note_fields_ = IArg(1);
    COMMON_RETURN_SELF;
  }
  static int GetPlayerState(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    p->player_state_[pn]->PushSelf(L);
    return 1;
  }
  static int GetMultiPlayerState(T* p, lua_State* L) {
    MultiPlayer multiplayer = Enum::Check<MultiPlayer>(L, 1);
    p->multiplayer_state_[multiplayer]->PushSelf(L);
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
    RString reason;
    bool can = p->CanSafelyEnterGameplay(reason);
    lua_pushboolean(L, can);
    LuaHelpers::Push(L, reason);
    return 2;
  }
  static void SetCompatibleStyleOrError(
      T* p, lua_State* L, StepsType steps_type, PlayerNumber pn) {
    if (!p->SetCompatibleStyle(steps_type, pn)) {
      luaL_error(L, "No compatible style for steps/trail.");
    }
    if (!p->GetCurrentStyle(pn)) {
      luaL_error(
          L, "No style set and AutoSetStyle is false, cannot set steps/trail.");
    }
  }
  static int GetCurrentSteps(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    Steps* steps = p->cur_steps_[pn];
    if (steps) {
      steps->PushSelf(L);
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
      Steps* steps = Luna<Steps>::check(L, 2);
      SetCompatibleStyleOrError(p, L, steps->m_StepsType, pn);
      p->cur_steps_[pn].Set(steps);
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
    Trail* trail = p->cur_trail_[pn];
    if (trail) {
      trail->PushSelf(L);
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
      Trail* trail = Luna<Trail>::check(L, 2);
      SetCompatibleStyleOrError(p, L, trail->m_StepsType, pn);
      p->cur_trail_[pn].Set(trail);
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
    Steps* steps = p->edit_source_steps_;
    if (steps) {
      steps->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetPreferredDifficulty(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    Difficulty difficulty = Enum::Check<Difficulty>(L, 2);
    p->preferred_difficulty_[pn].Set(difficulty);
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
  // DEFINE_METHOD( GetSongBeat,			position_.m_fSongBeat )
  // DEFINE_METHOD( GetSongBeatVisible,		position_.m_fSongBeatVisible )
  // DEFINE_METHOD( GetSongBPS,			position_.m_fCurBPS )
  // DEFINE_METHOD( GetSongFreeze,			position_.m_bFreeze )
  // DEFINE_METHOD( GetSongDelay,			position_.m_bDelay )
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
    RString song_options_str = p->song_options_.Get(m).GetString();
    LuaHelpers::Push(L, song_options_str);
    return 1;
  }
  static int GetSongOptionsObject(T* p, lua_State* L) {
    ModsLevel mods_level = Enum::Check<ModsLevel>(L, 1);
    p->song_options_.Get(mods_level).PushSelf(L);
    return 1;
  }
  static int GetDefaultSongOptions(T* p, lua_State* L) {
    SongOptions song_options;
    p->GetDefaultSongOptions(song_options);
    lua_pushstring(L, song_options.GetString());
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
    ModsLevel mods_level = Enum::Check<ModsLevel>(L, 1);

    SongOptions song_options;

    song_options.FromString(SArg(2));
    p->song_options_.Assign(mods_level, song_options);
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
    Profile* profile = p->GetEditLocalProfile();
    if (profile) {
      profile->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  static int GetCurrentStepsCredits(T* t, lua_State* L) {
    const Song* song = t->cur_song_;
    if (song == nullptr) {
      return 0;
    }

    // use a vector and not a set so that ordering is maintained
    std::vector<const Steps*> steps_to_show;
    FOREACH_HumanPlayer(p) {
      const Steps* steps = GAMESTATE->cur_steps_[p];
      if (steps == nullptr) {
        return 0;
      }
      bool already_added =
          std::find(steps_to_show.begin(), steps_to_show.end(), steps) !=
          steps_to_show.end();
      if (!already_added) {
        steps_to_show.push_back(steps);
      }
    }

    for (unsigned i = 0; i < steps_to_show.size(); ++i) {
      const Steps* steps = steps_to_show[i];
      RString difficulty_str =
          CustomDifficultyToLocalizedString(GetCustomDifficulty(
              steps->m_StepsType, steps->GetDifficulty(),
              CourseType_Invalid));

      lua_pushstring(L, difficulty_str);
      lua_pushstring(L, steps->GetDescription());
    }

    return steps_to_show.size() * 2;
  }

  static int SetPreferredSongGroup(T* p, lua_State* L) {
    p->preferred_song_group_.Set(SArg(1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetPreferredSongGroup, preferred_song_group_.Get());
  static int GetHumanPlayers(T* p, lua_State* L) {
    std::vector<PlayerNumber> human_pns;
    FOREACH_HumanPlayer(pn) human_pns.push_back(pn);
    LuaHelpers::CreateTableFromArray(human_pns, L);
    return 1;
  }
  static int GetEnabledPlayers(T*, lua_State* L) {
    std::vector<PlayerNumber> enabled_pns;
    FOREACH_EnabledPlayer(pn) enabled_pns.push_back(pn);
    LuaHelpers::CreateTableFromArray(enabled_pns, L);
    return 1;
  }
  static int GetCurrentStyle(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1, true, true);
    Style* style = const_cast<Style*>(p->GetCurrentStyle(pn));
    LuaHelpers::Push(L, style);
    return 1;
  }
  static int IsAnyHumanPlayerUsingMemoryCard(T*, lua_State* L) {
    bool using_memory_card = false;
    FOREACH_HumanPlayer(pn) {
      if (MEMCARDMAN->GetCardState(pn) == MemoryCardState_Ready) {
        using_memory_card = true;
      }
    }
    lua_pushboolean(L, using_memory_card);
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
  DEFINE_METHOD(GetCurMusicSeconds, position_.m_fMusicSeconds)

  DEFINE_METHOD(GetWorkoutGoalComplete, workout_goal_complete_)
  static int GetCharacter(T* p, lua_State* L) {
    p->cur_characters_[Enum::Check<PlayerNumber>(L, 1)]->PushSelf(L);
    return 1;
  }
  static int SetCharacter(T* p, lua_State* L) {
    Character* character = CHARMAN->GetCharacterFromID(SArg(2));
    if (character) {
      p->cur_characters_[Enum::Check<PlayerNumber>(L, 1)] = character;
    }
    COMMON_RETURN_SELF;
  }
  static int GetExpandedSectionName(T* p, lua_State* L) {
    lua_pushstring(L, p->expanded_section_name_);
    return 1;
  }
  static int AddStageToPlayer(T* p, lua_State* L) {
    p->AddStageToPlayer(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int InsertCoin(T* p, lua_State* L) {
    int num_coins = IArg(1);
    if (GAMESTATE->coins_ + num_coins >= 0) {
      StepMania::InsertCoin(num_coins, false);
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
    bool load_edits = true;
    if (lua_isboolean(L, 1)) {
      load_edits = BArg(1);
    }
    p->LoadProfiles(load_edits);
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
      T* p, lua_State* L, const Style* style, PlayMode play_mode) {
    if (play_mode != PLAY_MODE_BATTLE && play_mode != PLAY_MODE_RAVE) {
      return true;
    }

    // Do not allow styles with StepsTypes with shared sides or that are one
    // player only with Battle or Rave.
    if (style->m_StyleType != StyleType_TwoPlayersSharedSides) {
      std::vector<const Style*> styles;
      GAMEMAN->GetCompatibleStyles(p->cur_game_, 2, styles);
      for (const Style* s : styles) {
        if (s->m_StepsType == style->m_StepsType) {
          return true;
        }
      }
    }
    luaL_error(
        L, "Style %s is incompatible with PlayMode %s", style->m_szName,
        PlayModeToString(play_mode).c_str());
    return false;
  }

  static void ClearIncompatibleStepsAndTrails(T* p, lua_State* L) {
    FOREACH_HumanPlayer(pn) {
      const Style* style = p->GetCurrentStyle(pn);
      if (p->cur_steps_[pn] &&
          (!style || style->m_StepsType != p->cur_steps_[pn]->m_StepsType)) {
        p->cur_steps_[pn].Set(nullptr);
      }
      if (p->cur_trail_[pn] &&
          (!style || style->m_StepsType != p->cur_trail_[pn]->m_StepsType)) {
        p->cur_trail_[pn].Set(nullptr);
      }
    }
  }

  static int SetCurrentStyle(T* p, lua_State* L) {
    const Style* style = nullptr;
    if (lua_isstring(L, 1)) {
      RString style_str = SArg(1);
      style = GAMEMAN->GameAndStringToStyle(GAMESTATE->cur_game_, style_str);
      if (!style) {
        luaL_error(
            L, "SetCurrentStyle: %s is not a valid style.", style_str.c_str());
      }
    } else {
      style = Luna<Style>::check(L, 1);
    }

    StyleType style_type = style->m_StyleType;
    if (p->GetNumSidesJoined() == 2 &&
				(style_type == StyleType_OnePlayerOneSide ||
         style_type == StyleType_OnePlayerTwoSides)) {
      luaL_error(L, "Too many sides joined for style %s", style->m_szName);
    } else if (
        p->GetNumSidesJoined() == 1 &&
        (style_type == StyleType_TwoPlayersTwoSides ||
         style_type == StyleType_TwoPlayersSharedSides)) {
      luaL_error(L, "Too few sides joined for style %s", style->m_szName);
    }

    if (!AreStyleAndPlayModeCompatible(p, L, style, p->play_mode_)) {
      COMMON_RETURN_SELF;
    }
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 2, true, true);

    p->SetCurrentStyle(style, pn);
    ClearIncompatibleStepsAndTrails(p, L);

    COMMON_RETURN_SELF;
  }

  static int SetCurrentPlayMode(T* p, lua_State* L) {
    PlayMode play_mode = Enum::Check<PlayMode>(L, 1);
    if (AreStyleAndPlayModeCompatible(
            p, L, p->GetCurrentStyle(PLAYER_INVALID), play_mode)) {
      p->play_mode_.Set(play_mode);
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
          GAMEMAN->GetEditorStyleForStepsType(steps->m_StepsType),
          PLAYER_INVALID);
      p->cur_course_.Set(nullptr);
      return 0;
    }
    StepsType steps_type = Enum::Check<StepsType>(L, 3);
    Difficulty difficulty = Enum::Check<Difficulty>(L, 4);
    Steps* new_steps = song->CreateSteps();
    RString edit_name;
    // Form 2.
    if (steps == nullptr) {
      new_steps->CreateBlank(steps_type);
      new_steps->SetMeter(1);
      edit_name = "";
    }
    // Form 3.
    else {
      new_steps->CopyFrom(steps, steps_type, song->m_fMusicLengthSeconds);
      edit_name = steps->GetDescription();
    }
    SongUtil::MakeUniqueEditDescription(song, steps_type, edit_name);
    steps->SetDescription(edit_name);
    song->AddSteps(new_steps);
    p->cur_song_.Set(song);
    p->cur_steps_[PLAYER_1].Set(steps);
    p->SetCurrentStyle(
        GAMEMAN->GetEditorStyleForStepsType(steps->m_StepsType),
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
    std::size_t si = static_cast<std::size_t>(i);
    if (si >= p->autogen_args_.size()) {
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
    std::size_t si = static_cast<std::size_t>(i);
    while (si >= p->autogen_args_.size()) {
      p->autogen_args_.push_back(0.0f);
    }
    p->autogen_args_[si] = v;
    COMMON_RETURN_SELF;
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
    ADD_METHOD(GetMultiplayer);
    ADD_METHOD(SetMultiplayer);
    ADD_METHOD(InStepEditor);
    ADD_METHOD(GetNumMultiplayerNoteFields);
    ADD_METHOD(SetNumMultiplayerNoteFields);
    ADD_METHOD(ShowW1);
    ADD_METHOD(GetPlayerState);
    ADD_METHOD(GetMultiPlayerState);
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
    // ADD_METHOD( GetSongBeat );
    // ADD_METHOD( GetSongBeatVisible );
    // ADD_METHOD( GetSongBPS );
    // ADD_METHOD( GetSongFreeze );
    // ADD_METHOD( GetSongDelay );
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
