#ifndef GameCommand_H
#define GameCommand_H

#include <map>
#include <vector>

#include "Character.h"
#include "Command.h"
#include "Course.h"
#include "Difficulty.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "LuaReference.h"
#include "PlayerNumber.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"
#include "Trail.h"

struct lua_State;

int GetNumCreditsPaid();
int GetCreditsRequiredToPlayStyle(const Style* style);

class GameCommand {
 public:
  GameCommand()
      : commands_(),
        name_(""),
        text_(""),
        is_invalid_(true),
        invalid_reason_(""),
        index_(-1),
        multiplayer_(MultiPlayer_Invalid),
        style_(nullptr),
        play_mode_(PlayMode_Invalid),
        difficulty_(Difficulty_Invalid),
        course_difficulty_(Difficulty_Invalid),
        announcer_(""),
        preferred_modifiers_(""),
        stage_modifiers_(""),
        screen_(""),
        lua_function_(),
        song_(nullptr),
        steps_(nullptr),
        course_(nullptr),
        trail_(nullptr),
        character_(nullptr),
        set_env_(),
        set_pref_(),
        song_group_(""),
        sort_order_(SortOrder_Invalid),
        sound_path_(""),
        screens_to_prepare_(),
        weight_pounds_(-1),
        goal_calories_(-1),
        goal_type_(GoalType_Invalid),
        profile_id_(""),
        url_(""),
        url_exists_(true),
        insert_credits_(false),
        clear_credits_(false),
        stop_music_(false),
        apply_default_options_(false),
        fade_music_(false),
        music_fade_out_volume_(-1),
        music_fade_out_seconds_(-1),
        apply_commit_screens_(true) {
    lua_function_.Unset();
  }
  void Init();

  void Load(int index, const Commands& cmds);
  void LoadOne(const Command& cmd);

  void ApplyToAllPlayers() const;
  void Apply(PlayerNumber pn) const;

  bool DescribesCurrentMode(PlayerNumber pn) const;
  bool DescribesCurrentModeForAllPlayers() const;
  bool IsPlayable(RString* invalid_reason = nullptr) const;
  bool IsZero() const;

  // If true, Apply() will apply screen_. If false, it won't, and you need to
	// do it yourself.
  void ApplyCommitsScreens(bool apply_commit_screens) {
		apply_commit_screens_ = apply_commit_screens;
	}

  // Same as what was passed to Load. We need to keep the original commands
  // so that we know the order of commands when it comes time to Apply.
  Commands commands_;

	// Choice name.
  RString name_;
	// Display text.
  RString text_;
  bool is_invalid_;
  RString invalid_reason_;
  int index_;
  MultiPlayer multiplayer_;
  const Style* style_;
  PlayMode play_mode_;
  Difficulty difficulty_;
  CourseDifficulty course_difficulty_;
  RString announcer_;
  RString preferred_modifiers_;
  RString stage_modifiers_;
  RString screen_;
  LuaReference lua_function_;
  Song* song_;
  Steps* steps_;
  Course* course_;
  Trail* trail_;
  Character* character_;
  std::map<RString, RString> set_env_;
  std::map<RString, RString> set_pref_;
  RString song_group_;
  SortOrder sort_order_;
	// "" for no sound
  RString sound_path_;
  std::vector<RString> screens_to_prepare_;
  // What is the player's weight in pounds?
  // If this value is -1, then no weight was specified.
  int weight_pounds_;
	// -1 == none specified
  int goal_calories_;
  GoalType goal_type_;
  RString profile_id_;
  RString url_;
  // sm-ssc adds:
	// For making stepmania not exit on url.
  bool url_exists_;

  bool insert_credits_;
  bool clear_credits_;
  bool stop_music_;
  bool apply_default_options_;
  // sm-ssc also adds:
  bool fade_music_;
  float music_fade_out_volume_;
  // NOTE(aj): Currently, GameSoundManager uses consts for fade out/in times, so
  // this is kind of pointless, but I want to have it working eventually.
  float music_fade_out_seconds_;

  // Lua
  void PushSelf(lua_State* L);

 private:
	void Apply(const std::vector<PlayerNumber>& pns) const;
  void ApplySelf(const std::vector<PlayerNumber>& pns) const;

  bool apply_commit_screens_;
};

#endif  // GameCommand_H

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
