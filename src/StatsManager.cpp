#include "StatsManager.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "CourseUtil.h"
#include "CryptManager.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "Grade.h"
#include "LuaManager.h"
#include "ModsGroup.h"
#include "NotesWriterSM.h"
#include "Player.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileDriverMemory.h"
#include "RageLog.h"
#include "Song.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "StyleUtil.h"
#include "TrailUtil.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "global.h"

StatsManager* STATSMAN =
    nullptr;  // global object accessible from anywhere in the program

void AddPlayerStatsToProfile(
    Profile* profile, const StageStats& ss, PlayerNumber pn);
XNode* MakeRecentScoreNode(
    const StageStats& ss, Trail* trail, const PlayerStageStats& pss);
Preference<bool> padmiss_enabled("MemoryCardPadmissEnabled", false);

StatsManager::StatsManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "STATSMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

StatsManager::~StatsManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("STATSMAN");
}

void StatsManager::Reset() {
  cur_stage_stats_.Init();
  played_stage_stats_.clear();
  accum_played_stage_stats_.Init();

  CalcAccumPlayedStageStats();
}

static StageStats AccumPlayedStageStats(const std::vector<StageStats>& vss) {
  StageStats ssreturn;

  if (!vss.empty()) {
    ssreturn.play_mode_ = vss[0].play_mode_;
  }

  for (const StageStats& ss : vss) {
    ssreturn.AddStats(ss);
  }

  unsigned num_songs = ssreturn.played_songs_.size();

  if (num_songs == 0) {
    return ssreturn;  // don't divide by 0 below
  }

  /* Scale radar percentages back down to roughly 0..1.  Don't scale
   * RadarCategory_TapsAndHolds and the rest, which are counters. */
  // FIXME: Weight each song by the number of stages it took to account for
  // long, marathon.
  FOREACH_EnabledPlayer(p) {
    for (int r = 0; r < RadarCategory_TapsAndHolds; r++) {
      ssreturn.player_[p].radar_possible_[r] /= num_songs;
      ssreturn.player_[p].radar_actual_[r] /= num_songs;
    }
  }
  return ssreturn;
}

void StatsManager::GetFinalEvalStageStats(StageStats& stats_out) const {
  stats_out.Init();
  std::vector<StageStats> to_count;
  for (size_t i = 0; i < played_stage_stats_.size(); ++i) {
    to_count.push_back(played_stage_stats_[i]);
  }
  stats_out = AccumPlayedStageStats(to_count);
}

void StatsManager::CalcAccumPlayedStageStats() {
  accum_played_stage_stats_ = AccumPlayedStageStats(played_stage_stats_);
}

/* This data is added to each player profile, and to the machine profile
 * per-player. */
void AddPlayerStatsToProfile(
    Profile* profile, const StageStats& ss, PlayerNumber pn) {
  ss.AssertValid(pn);

  StyleID id;
  id.FromStyle(ss.player_[pn].style_);

  ASSERT((int)ss.played_songs_.size() == ss.player_[pn].steps_played_);
  for (int i = 0; i < ss.player_[pn].steps_played_; i++) {
    Steps* steps = ss.player_[pn].possible_steps_[i];

    profile->num_songs_played_by_play_mode_[ss.play_mode_]++;
    profile->num_songs_played_by_style_[id]++;
    profile->num_songs_played_by_difficulty_[steps->GetDifficulty()]++;

    int meter = std::clamp(steps->GetMeter(), 0, kMaxMeter);
    profile->num_songs_played_by_meter_[meter]++;
  }

  profile->total_dance_points_ += ss.player_[pn].actual_dance_points_;

  if (ss.stage_ == Stage_Extra1 || ss.stage_ == Stage_Extra2) {
    if (ss.player_[pn].failed_) {
      ++profile->num_extra_stages_failed_;
    } else {
      ++profile->num_extra_stages_passed_;
    }
  }

  // If you fail in a course, you passed all but the final song.
  // FIXME: Not true.  If playing with 2 players, one player could have failed
  // earlier.
  if (!ss.player_[pn].failed_) {
    profile->num_stages_passed_by_play_mode_[ss.play_mode_]++;
    profile->num_stages_passed_by_grade_[ss.player_[pn].GetGrade()]++;
  }
}

XNode* MakeRecentScoreNode(
    const StageStats& ss, Trail* trail, const PlayerStageStats& pss) {
  XNode* node = nullptr;
  if (GAMESTATE->IsCourseMode()) {
    node = new XNode("HighScoreForACourseAndTrail");

    CourseID course_id;
    course_id.FromCourse(GAMESTATE->cur_course_);
    node->AppendChild(course_id.CreateNode());

    TrailID trail_id;
    trail_id.FromTrail(trail);
    node->AppendChild(trail_id.CreateNode());

  } else {
    node = new XNode("HighScoreForASongAndSteps");

    SongID song_id;
    song_id.FromSong(ss.possible_songs_[0]);
    node->AppendChild(song_id.CreateNode());

    StepsID steps_id;
    steps_id.FromSteps(pss.possible_steps_[0]);
    node->AppendChild(steps_id.CreateNode());
  }

  XNode* high_score = pss.high_score_.CreateNode();
  high_score->AppendChild("StageGuid", GAMESTATE->stage_guid_);
  high_score->AppendChild("Guid", CryptManager::GenerateRandomUUID());

  node->AppendChild(high_score);

  return node;
}

void StatsManager::CommitStatsToProfiles(const StageStats* ss) {
  // Add step totals.  Use radarActual, since the player might have failed part
  // way through the song, in which case we don't want to give credit for the
  // rest of the song.
  FOREACH_HumanPlayer(pn) {
    int num_taps_and_holds =
        (int)ss->player_[pn].radar_actual_[RadarCategory_TapsAndHolds];
    int num_jumps = (int)ss->player_[pn].radar_actual_[RadarCategory_Jumps];
    int num_holds = (int)ss->player_[pn].radar_actual_[RadarCategory_Holds];
    int num_rolls = (int)ss->player_[pn].radar_actual_[RadarCategory_Rolls];
    int num_mines = (int)ss->player_[pn].radar_actual_[RadarCategory_Mines];
    int num_hands = (int)ss->player_[pn].radar_actual_[RadarCategory_Hands];
    int num_lifts = (int)ss->player_[pn].radar_actual_[RadarCategory_Lifts];
    float calories_burned = ss->player_[pn].calories_burned_;
    PROFILEMAN->AddStepTotals(
        pn, num_taps_and_holds, num_jumps, num_holds, num_rolls, num_mines,
        num_hands, num_lifts, calories_burned);
  }

  // Update profile stats
  Profile* machine_profile = PROFILEMAN->GetMachineProfile();

  int gameplay_seconds = std::trunc(ss->gameplay_seconds_);

  machine_profile->total_gameplay_seconds_ += gameplay_seconds;
  machine_profile->num_total_songs_played_ += ss->played_songs_.size();

  FOREACH_HumanPlayer(pn) {
    Profile* player_profile = PROFILEMAN->GetProfile(pn);
    if (player_profile) {
      player_profile->total_gameplay_seconds_ += gameplay_seconds;
      player_profile->num_total_songs_played_ += ss->played_songs_.size();
    }

    LOG->Trace("Adding stats to machine profile...");
    AddPlayerStatsToProfile(machine_profile, *ss, pn);

    if (player_profile) {
      LOG->Trace("Adding stats to player profile...");
      AddPlayerStatsToProfile(player_profile, *ss, pn);
    }

    // No marathons etc for now...
    if (padmiss_enabled.Get() && ss->play_mode_ == PLAY_MODE_REGULAR) {
      SavePadmissScore(ss, pn);
    }
  }

  // Not sure what the Save/Upload folder was originally for, but the files
  // in it just accumulate uselessly, wasting several seconds when finishing
  // a song.  So this pref disables it. -Kyz
  if (!PREFSMAN->disable_upload_dir_) {
    SaveUploadFile(ss);
  }

  // FileCopy( "Data/TempTestGroups.xml", "Save/Upload/data.xml" );
}

void StatsManager::SaveUploadFile(const StageStats* ss) {
  // Save recent scores
  std::unique_ptr<XNode> xml(new XNode("Stats"));
  xml->AppendChild("MachineGuid", PROFILEMAN->GetMachineProfile()->guid_);

  XNode* recent = nullptr;
  if (GAMESTATE->IsCourseMode()) {
    recent = xml->AppendChild(new XNode("RecentCourseScores"));
  } else {
    recent = xml->AppendChild(new XNode("RecentSongScores"));
  }

  FOREACH_HumanPlayer(p) {
    if (ss->player_[p].high_score_.IsEmpty()) {
      continue;
    }
    recent->AppendChild(
        MakeRecentScoreNode(*ss, GAMESTATE->cur_trail_[p], ss->player_[p]));
  }

  std::string date = DateTime::GetNowDate().GetString();
  Replace(date, ":", "-");

  const std::string kUploadDir = "/Save/Upload/";
  std::string file_name_no_extension =
      Profile::MakeUniqueFileNameNoExtension(kUploadDir, date + " ");
  std::string fn = kUploadDir + file_name_no_extension + ".xml";

  bool saved = XmlFileUtil::SaveToFile(xml.get(), fn, "", false);

  if (saved) {
    std::string stats_xml_sig_file = fn + kSignatureAppend;
    CryptManager::SignFileToFile(fn, stats_xml_sig_file);
  }
}

void StatsManager::SavePadmissScore(const StageStats* ss, PlayerNumber pn) {
  const PlayerStageStats* player_stats = &ss->player_[pn];

  std::unique_ptr<XNode> xml(new XNode("SongScore"));

  std::string date = DateTime::GetNowDate().GetString();
  Replace(date, ":", "-");

  XNode* taps = xml->AppendChild("TapNoteScores");
  FOREACH_ENUM(TapNoteScore, tns)
  if (tns != TNS_None) {
    taps->AppendChild(
        TapNoteScoreToString(tns), player_stats->tap_note_scores_[tns]);
  }

  XNode* radar_actual = xml->AppendChild("RadarActual");
  radar_actual->AppendChild(
      player_stats->radar_actual_.CreateNode(true, false));

  XNode* radar_possible = xml->AppendChild("RadarPossible");
  radar_possible->AppendChild(
      player_stats->radar_possible_.CreateNode(true, false));

  Profile* pp = PROFILEMAN->GetProfile(player_stats->player_number_);
  xml->AppendChild("ScoreValue", player_stats->GetPercentDancePoints());
  xml->AppendChild("PlayerNumber", player_stats->player_number_);
  xml->AppendChild("PlayerName", pp->display_name_);
  xml->AppendChild("PlayerGuid", pp->guid_);

  Steps* steps = player_stats->possible_steps_[0];  // XXX Courses and such
  Song* song = steps->song_;
  steps->Decompress();  // Hashing won't work unless the steps are decompressed
  XNode* stepdata = xml->AppendChild("Steps");
  stepdata->AppendChild("Hash", steps->GetHash());
  stepdata->AppendChild("Meter", steps->GetMeter());
  stepdata->AppendChild("StepArtist", steps->GetCredit());
  stepdata->AppendChild("StepsType", steps->steps_type_str_);
  RageFileObjMem f;
  std::vector<Steps*> stepv;
  stepv.push_back(steps);
  NotesWriterSM::Write(f, *song, stepv);
  stepdata->AppendChild("StepData", f.GetString());

  XNode* songdata = xml->AppendChild("SongData");
  songdata->AppendChild("Title", song->main_title_);
  songdata->AppendChild("TitleTranslit", song->main_title_translit_);
  songdata->AppendChild("SubTitle", song->sub_title_);
  songdata->AppendChild("SubTitleTranslit", song->sub_title_translit_);
  songdata->AppendChild("Artist", song->artist_);
  songdata->AppendChild("ArtistTranslit", song->artist_translit_);
  songdata->AppendChild("Duration", song->music_length_seconds_);

  const PlayerOptions& opts =
      GAMESTATE->player_state_[pn]->player_options_.Get(ModsLevel_Preferred);
  XNode* mods = xml->AppendChild("Mods");
  mods->AppendChild("MusicRate", ss->music_rate_);
#define ADD_BOOLEAN_OPTION(parent, name, opts) \
  if (opts[PlayerOptions::name]) parent->AppendChild(#name)

  XNode* turns = mods->AppendChild("Turns");
  ADD_BOOLEAN_OPTION(turns, TURN_MIRROR, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_LRMIRROR, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_UDMIRROR, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_BACKWARDS, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_LEFT, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_RIGHT, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_SHUFFLE, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_SOFT_SHUFFLE, opts.turns_);
  ADD_BOOLEAN_OPTION(turns, TURN_SUPER_SHUFFLE, opts.turns_);

  XNode* transforms = mods->AppendChild("Transforms");
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOHOLDS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOROLLS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOMINES, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_LITTLE, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_WIDE, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_BIG, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_QUICK, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_BMRIZE, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_SKIPPY, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_MINES, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_ATTACKMINES, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_ECHO, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_STOMP, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_PLANTED, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_FLOORED, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_TWISTER, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_HOLDROLLS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOJUMPS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOHANDS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOLIFTS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOFAKES, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOQUADS, opts.transforms_);
  ADD_BOOLEAN_OPTION(transforms, TRANSFORM_NOSTRETCH, opts.transforms_);

#define ADD_FLOAT_OPTION(parent, name, opts)          \
  do {                                                \
    float val = opts[PlayerOptions::name];            \
    if (val != 0.0f) parent->AppendChild(#name, val); \
  } while (0)

  XNode* accels = mods->AppendChild("Accels");
  ADD_FLOAT_OPTION(accels, ACCEL_BOOST, opts.accels_);
  ADD_FLOAT_OPTION(accels, ACCEL_BRAKE, opts.accels_);
  ADD_FLOAT_OPTION(accels, ACCEL_WAVE, opts.accels_);
  ADD_FLOAT_OPTION(accels, ACCEL_EXPAND, opts.accels_);
  ADD_FLOAT_OPTION(accels, ACCEL_BOOMERANG, opts.accels_);

  XNode* effects = mods->AppendChild("Effects");
  ADD_FLOAT_OPTION(effects, EFFECT_DRUNK, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_DIZZY, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_CONFUSION, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_MINI, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_TINY, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_FLIP, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_INVERT, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_TORNADO, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_TIPSY, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_BUMPY, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_BEAT, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_XMODE, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_TWIRL, opts.effects_);
  ADD_FLOAT_OPTION(effects, EFFECT_ROLL, opts.effects_);

  XNode* appearances = mods->AppendChild("Appearances");
  ADD_FLOAT_OPTION(appearances, APPEARANCE_HIDDEN, opts.appearances_);
  ADD_FLOAT_OPTION(appearances, APPEARANCE_HIDDEN_OFFSET, opts.appearances_);
  ADD_FLOAT_OPTION(appearances, APPEARANCE_SUDDEN, opts.appearances_);
  ADD_FLOAT_OPTION(appearances, APPEARANCE_SUDDEN_OFFSET, opts.appearances_);
  ADD_FLOAT_OPTION(appearances, APPEARANCE_STEALTH, opts.appearances_);
  ADD_FLOAT_OPTION(appearances, APPEARANCE_BLINK, opts.appearances_);
  ADD_FLOAT_OPTION(appearances, APPEARANCE_RANDOMVANISH, opts.appearances_);

  XNode* scrolls = mods->AppendChild("Scrolls");
  ADD_FLOAT_OPTION(scrolls, SCROLL_REVERSE, opts.scrolls_);
  ADD_FLOAT_OPTION(scrolls, SCROLL_SPLIT, opts.scrolls_);
  ADD_FLOAT_OPTION(scrolls, SCROLL_ALTERNATE, opts.scrolls_);
  ADD_FLOAT_OPTION(scrolls, SCROLL_CROSS, opts.scrolls_);
  ADD_FLOAT_OPTION(scrolls, SCROLL_CENTERED, opts.scrolls_);

  mods->AppendChild("NoteSkin", opts.note_skin_);

  XNode* perspectives = mods->AppendChild("Perspectives");
  perspectives->AppendChild("Tilt", opts.perspective_tilt_);
  perspectives->AppendChild("Skew", opts.skew_);

  std::string speed_mod_type;
  float speed_mod_value;
  if (opts.time_spacing_) {
    speed_mod_type = "ConstantBPM";
    speed_mod_value = opts.scroll_bpm_;
  } else if (opts.max_scroll_bpm_) {
    speed_mod_type = "MaxBPM";
    speed_mod_value = opts.max_scroll_bpm_;
  } else {
    speed_mod_type = "Multiplier";
    speed_mod_value = opts.scroll_speed_;
  }
  mods->AppendChild("ScrollSpeed", speed_mod_value)
      ->AppendAttr("Type", speed_mod_type);

  XNode* timing_windows = xml->AppendChild("TimingWindows");
  FOREACH_ENUM(TimingWindow, tw)
  timing_windows->AppendChild(
      TimingWindowToString(tw), Player::GetWindowSeconds(tw));

  std::string dir = "/Save/Padmiss/";
  std::string fn =
      dir + Profile::MakeUniqueFileNameNoExtension(dir, date + " ") + ".xml";
  XmlFileUtil::SaveToFile(xml.get(), fn, "", true);
}

void StatsManager::UnjoinPlayer(PlayerNumber pn) {
  /* A player has been unjoined.  Clear his data from m_vPlayedStageStats, and
   * purge any m_vPlayedStageStats that no longer have any player data because
   * all of the players that were playing at the time have been unjoined. */
  for (StageStats& ss : played_stage_stats_) {
    ss.player_[pn] = PlayerStageStats();
  }

  for (int i = 0; i < (int)played_stage_stats_.size(); ++i) {
    StageStats& ss = played_stage_stats_[i];
    bool is_active = false;
    FOREACH_PlayerNumber(p) if (ss.player_[p].joined_) is_active = true;
    if (is_active) {
      continue;
    }

    played_stage_stats_.erase(played_stage_stats_.begin() + i);
    --i;
  }
}

void StatsManager::GetStepsInUse(std::set<Steps*>& in_use_out) const {
  for (int i = 0; i < (int)played_stage_stats_.size(); ++i) {
    FOREACH_PlayerNumber(pn) {
      const PlayerStageStats& pss = played_stage_stats_[i].player_[pn];
      in_use_out.insert(pss.possible_steps_.begin(), pss.possible_steps_.end());
    }
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the StatsManager. */
class LunaStatsManager : public Luna<StatsManager> {
 public:
  static int GetCurStageStats(T* p, lua_State* L) {
    p->cur_stage_stats_.PushSelf(L);
    return 1;
  }
  static int GetPlayedStageStats(T* p, lua_State* L) {
    int ago = IArg(1);
    int index = p->played_stage_stats_.size() - ago;
    if (index < 0 || index >= (int)p->played_stage_stats_.size()) {
      return 0;
    }

    p->played_stage_stats_[index].PushSelf(L);
    return 1;
  }
  static int Reset(T* p, lua_State* L) {
    p->Reset();
    return 0;
  }
  static int GetAccumPlayedStageStats(T* p, lua_State* L) {
    p->GetAccumPlayedStageStats().PushSelf(L);
    return 1;
  }
  static int GetFinalEvalStageStats(T* p, lua_State* L) {
    StageStats stats;
    p->GetFinalEvalStageStats(stats);
    stats.PushSelf(L);
    return 1;
  }
  static int GetFinalGrade(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);

    if (!GAMESTATE->IsHumanPlayer(pn)) {
      lua_pushnumber(L, Grade_NoData);
    } else {
      StageStats stats;
      p->GetFinalEvalStageStats(stats);
      lua_pushnumber(L, stats.player_[pn].GetGrade());
    }
    return 1;
  }
  static int GetStagesPlayed(T* p, lua_State* L) {
    lua_pushnumber(L, p->played_stage_stats_.size());
    return 1;
  }

  static int GetBestGrade(T* p, lua_State* L) {
    Grade g = NUM_Grade;
    FOREACH_EnabledPlayer(pn) g =
        std::min(g, STATSMAN->cur_stage_stats_.player_[pn].GetGrade());
    lua_pushnumber(L, g);
    return 1;
  }

  static int GetWorstGrade(T* p, lua_State* L) {
    Grade g = Grade_Tier01;
    FOREACH_EnabledPlayer(pn) g =
        std::max(g, STATSMAN->cur_stage_stats_.player_[pn].GetGrade());
    lua_pushnumber(L, g);
    return 1;
  }

  static int GetBestFinalGrade(T* t, lua_State* L) {
    Grade top_grade = Grade_Failed;
    StageStats stats;
    t->GetFinalEvalStageStats(stats);
    FOREACH_HumanPlayer(p) {
      // If this player failed any stage, then their final grade is an F.
      if (std::any_of(
              STATSMAN->played_stage_stats_.begin(),
              STATSMAN->played_stage_stats_.end(),
              [&](const StageStats& ss) { return ss.player_[p].failed_; })) {
        continue;
      }

      top_grade = std::min(top_grade, stats.player_[p].GetGrade());
    }

    Enum::Push(L, top_grade);
    return 1;
  }

  LunaStatsManager() {
    ADD_METHOD(GetCurStageStats);
    ADD_METHOD(GetPlayedStageStats);
    ADD_METHOD(GetAccumPlayedStageStats);
    ADD_METHOD(GetFinalEvalStageStats);
    ADD_METHOD(Reset);
    ADD_METHOD(GetFinalGrade);
    ADD_METHOD(GetStagesPlayed);
    ADD_METHOD(GetBestGrade);
    ADD_METHOD(GetWorstGrade);
    ADD_METHOD(GetBestFinalGrade);
  }
};

LUA_REGISTER_CLASS(StatsManager)
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
