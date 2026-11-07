#ifndef PLAYER_OPTIONS_H
#define PLAYER_OPTIONS_H

#include "EnumHelper.h"
#include "RageUtil.h"
class Course;
class Song;
class Steps;
class Trail;
struct lua_State;

#define ONE(arr)                                                \
  {                                                             \
    for (unsigned Z = 0; Z < ARRAYLEN(arr); ++Z) arr[Z] = 1.0f; \
  }

#include <bitset>
#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"

enum LifeType {
  LifeType_Bar,
  LifeType_Battery,
  LifeType_Time,
  NUM_LifeType,
  LifeType_Invalid
};
const std::string& LifeTypeToString(LifeType cat);
const std::string& LifeTypeToLocalizedString(LifeType cat);
LuaDeclareType(LifeType);

enum DrainType {
  DrainType_Normal,
  DrainType_NoRecover,
  DrainType_SuddenDeath,
  NUM_DrainType,
  DrainType_Invalid
};
const std::string& DrainTypeToString(DrainType cat);
const std::string& DrainTypeToLocalizedString(DrainType cat);
LuaDeclareType(DrainType);

enum HideLightType {
  HideLightType_NoHideLights = 0,
  HideLightType_HideAllLights,
  HideLightType_HideMarqueeLights,
  HideLightType_HideBassLights,
  NUM_HideLightType,
  HideLightType_Invalid
};
const std::string& HideLightTypeToString(HideLightType cat);
const std::string& HideLightTypeToLocalizedString(HideLightType cat);
LuaDeclareType(HideLightType);

enum ModTimerType {
  ModTimerType_Game,
  ModTimerType_Beat,
  ModTimerType_Song,
  ModTimerType_Default,
  NUM_ModTimerType,
  ModTimerType_Invalid
};
const std::string& ModTimerTypeToString(ModTimerType cat);
const std::string& ModTimerTypeToLocalizedString(ModTimerType cat);
LuaDeclareType(ModTimerType);

/** @brief Per-player options that are not saved between sessions. */
class PlayerOptions {
 public:
  /**
   * @brief Set up the PlayerOptions with some reasonable defaults.
   *
   * This code was taken from Init() to use proper initialization. */
  PlayerOptions()
      : hide_light_type_(HideLightType_NoHideLights),
        life_type_(LifeType_Bar),
        drain_type_(DrainType_Normal),
        mod_timer_type_(ModTimerType_Default),
        battery_lives_(4),
        set_scroll_speed_(false),
        time_spacing_(0),
        speed_time_spacing_(1.0f),
        max_scroll_bpm_(0),
        speed_max_scroll_bpm_(1.0f),
        scroll_speed_(1.0f),
        speed_scroll_speed_(1.0f),
        scroll_bpm_(200),
        speed_scroll_bpm_(1.0f),
        dark_(0),
        speed_dark_(1.0f),
        blind_(0),
        speed_blind_(1.0f),
        cover_(0),
        speed_cover_(1.0f),
        rand_attack_(0),
        speed_rand_attack_(1.0f),
        no_attack_(0),
        speed_no_attack_(1.0f),
        player_auto_play_(0),
        speed_player_auto_play_(1.0f),
        perspective_tilt_(0),
        speed_perspective_tilt_(1.0f),
        skew_(0),
        speed_skew_(1.0f),
        passmark_(0),
        speed_passmark_(1.0f),
        random_speed_(0),
        speed_random_speed_(1.0f),
        mod_timer_mult_(0),
        speed_mod_timer_mult_(1.0f),
        mod_timer_offset_(0),
        speed_mod_timer_offset_(1.0f),
        draw_size_(0),
        speed_draw_size_(1.0f),
        draw_size_back_(0),
        speed_draw_size_back_(1.0f),
        mute_on_error_(false),
        stealth_type_(false),
        stealth_past_receptors_(false),
        dizzy_holds_(false),
        z_buffer_(false),
        cosecant_(false),
        fail_type_(FailType_Immediate),
        min_tns_to_hide_notes_(PREFSMAN->min_tns_to_hide_notes_) {
    note_skin_ = "";
    visual_delay_ = 0.0f;
    ZERO(accels_);
    ONE(speed_accels_);
    ZERO(effects_);
    ONE(speed_effects_);
    ZERO(appearances_);
    ONE(speed_appearances_);
    ZERO(scrolls_);
    ONE(speed_scrolls_);
    ZERO(turns_);
    ZERO(transforms_);
    ZERO(moves_x_);
    ONE(speed_moves_x_);
    ZERO(moves_y_);
    ONE(speed_moves_y_);
    ZERO(moves_z_);
    ONE(speed_moves_z_);
    ZERO(confusion_x_);
    ONE(speed_confusion_x_);
    ZERO(confusion_y_);
    ONE(speed_confusion_y_);
    ZERO(confusion_z_);
    ONE(speed_confusion_z_);
    ZERO(darks_);
    ONE(speed_darks_);
    ZERO(stealth_);
    ONE(speed_stealth_);
    ZERO(tiny_);
    ONE(speed_tiny_);
    ZERO(bumpy_);
    ONE(speed_bumpy_);
    ZERO(reverse_);
    ONE(speed_reverse_);
  };
  void Init();
  void Approach(const PlayerOptions& other, float delta_seconds);
  std::string GetString(bool force_note_skin = false) const;
  std::string GetSavedPrefsString()
      const;  // only the basic options that players would want for every song
  enum ResetPrefsType { saved_prefs, saved_prefs_invalid_for_course };
  void ResetPrefs(ResetPrefsType type);
  void ResetSavedPrefs() { ResetPrefs(saved_prefs); };
  void ResetSavedPrefsInvalidForCourse() {
    ResetPrefs(saved_prefs_invalid_for_course);
  }
  void GetMods(
      std::vector<std::string>& add_to, bool force_note_skin = false) const;
  void GetLocalizedMods(std::vector<std::string>& add_to) const;
  void FromString(const std::string& multiple_mods);
  bool FromOneModString(
      const std::string& one_mod,
      std::string& error_detail_out);  // On error, return false and optionally
                                       // set sErrorDetailOut
  void ChooseRandomModifiers();
  bool ContainsTransformOrTurn() const;

  // Lua
  void PushSelf(lua_State* L);

  bool operator==(const PlayerOptions& other) const;
  bool operator!=(const PlayerOptions& other) const {
    return !operator==(other);
  }
  PlayerOptions& operator=(const PlayerOptions& other);
  PlayerOptions(const PlayerOptions& other) { operator=(other); }

  /** @brief The various acceleration mods. */
  enum Accel {
    ACCEL_BOOST, /**< The arrows start slow, then zoom towards the targets. */
    ACCEL_BRAKE, /**< The arrows start fast, then slow down as they approach the
                    targets. */
    ACCEL_WAVE,
    ACCEL_WAVE_PERIOD,
    ACCEL_EXPAND,
    ACCEL_EXPAND_PERIOD,
    ACCEL_TAN_EXPAND,
    ACCEL_TAN_EXPAND_PERIOD,
    ACCEL_BOOMERANG, /**< The arrows start from above the targets, go down, then
                        come back up. */
    NUM_ACCELS
  };
  enum Effect {
    EFFECT_DRUNK,
    EFFECT_DRUNK_SPEED,
    EFFECT_DRUNK_OFFSET,
    EFFECT_DRUNK_PERIOD,
    EFFECT_TAN_DRUNK,
    EFFECT_TAN_DRUNK_SPEED,
    EFFECT_TAN_DRUNK_OFFSET,
    EFFECT_TAN_DRUNK_PERIOD,
    EFFECT_DRUNK_Z,
    EFFECT_DRUNK_Z_SPEED,
    EFFECT_DRUNK_Z_OFFSET,
    EFFECT_DRUNK_Z_PERIOD,
    EFFECT_TAN_DRUNK_Z,
    EFFECT_TAN_DRUNK_Z_SPEED,
    EFFECT_TAN_DRUNK_Z_OFFSET,
    EFFECT_TAN_DRUNK_Z_PERIOD,
    EFFECT_DIZZY,
    EFFECT_ATTENUATE_X,
    EFFECT_ATTENUATE_Y,
    EFFECT_ATTENUATE_Z,
    EFFECT_SHRINK_TO_MULT,
    EFFECT_SHRINK_TO_LINEAR,
    EFFECT_PULSE_INNER,
    EFFECT_PULSE_OUTER,
    EFFECT_PULSE_OFFSET,
    EFFECT_PULSE_PERIOD,
    EFFECT_CONFUSION,
    EFFECT_CONFUSION_OFFSET,
    EFFECT_CONFUSION_X,
    EFFECT_CONFUSION_X_OFFSET,
    EFFECT_CONFUSION_Y,
    EFFECT_CONFUSION_Y_OFFSET,
    EFFECT_BOUNCE,
    EFFECT_BOUNCE_PERIOD,
    EFFECT_BOUNCE_OFFSET,
    EFFECT_BOUNCE_Z,
    EFFECT_BOUNCE_Z_PERIOD,
    EFFECT_BOUNCE_Z_OFFSET,
    EFFECT_MINI,
    EFFECT_TINY,
    EFFECT_FLIP,
    EFFECT_INVERT,
    EFFECT_TORNADO,
    EFFECT_TORNADO_PERIOD,
    EFFECT_TORNADO_OFFSET,
    EFFECT_TAN_TORNADO,
    EFFECT_TAN_TORNADO_PERIOD,
    EFFECT_TAN_TORNADO_OFFSET,
    EFFECT_TORNADO_Z,
    EFFECT_TORNADO_Z_PERIOD,
    EFFECT_TORNADO_Z_OFFSET,
    EFFECT_TAN_TORNADO_Z,
    EFFECT_TAN_TORNADO_Z_PERIOD,
    EFFECT_TAN_TORNADO_Z_OFFSET,
    EFFECT_TIPSY,
    EFFECT_TIPSY_SPEED,
    EFFECT_TIPSY_OFFSET,
    EFFECT_TAN_TIPSY,
    EFFECT_TAN_TIPSY_SPEED,
    EFFECT_TAN_TIPSY_OFFSET,
    EFFECT_BUMPY,
    EFFECT_BUMPY_OFFSET,
    EFFECT_BUMPY_PERIOD,
    EFFECT_TAN_BUMPY,
    EFFECT_TAN_BUMPY_OFFSET,
    EFFECT_TAN_BUMPY_PERIOD,
    EFFECT_BUMPY_X,
    EFFECT_BUMPY_X_OFFSET,
    EFFECT_BUMPY_X_PERIOD,
    EFFECT_TAN_BUMPY_X,
    EFFECT_TAN_BUMPY_X_OFFSET,
    EFFECT_TAN_BUMPY_X_PERIOD,
    EFFECT_BEAT,
    EFFECT_BEAT_OFFSET,
    EFFECT_BEAT_PERIOD,
    EFFECT_BEAT_MULT,
    EFFECT_BEAT_Y,
    EFFECT_BEAT_Y_OFFSET,
    EFFECT_BEAT_Y_PERIOD,
    EFFECT_BEAT_Y_MULT,
    EFFECT_BEAT_Z,
    EFFECT_BEAT_Z_OFFSET,
    EFFECT_BEAT_Z_PERIOD,
    EFFECT_BEAT_Z_MULT,
    EFFECT_DIGITAL,
    EFFECT_DIGITAL_STEPS,
    EFFECT_DIGITAL_PERIOD,
    EFFECT_DIGITAL_OFFSET,
    EFFECT_TAN_DIGITAL,
    EFFECT_TAN_DIGITAL_STEPS,
    EFFECT_TAN_DIGITAL_PERIOD,
    EFFECT_TAN_DIGITAL_OFFSET,
    EFFECT_DIGITAL_Z,
    EFFECT_DIGITAL_Z_STEPS,
    EFFECT_DIGITAL_Z_PERIOD,
    EFFECT_DIGITAL_Z_OFFSET,
    EFFECT_TAN_DIGITAL_Z,
    EFFECT_TAN_DIGITAL_Z_STEPS,
    EFFECT_TAN_DIGITAL_Z_PERIOD,
    EFFECT_TAN_DIGITAL_Z_OFFSET,
    EFFECT_ZIGZAG,
    EFFECT_ZIGZAG_PERIOD,
    EFFECT_ZIGZAG_OFFSET,
    EFFECT_ZIGZAG_Z,
    EFFECT_ZIGZAG_Z_PERIOD,
    EFFECT_ZIGZAG_Z_OFFSET,
    EFFECT_SAWTOOTH,
    EFFECT_SAWTOOTH_PERIOD,
    EFFECT_SAWTOOTH_Z,
    EFFECT_SAWTOOTH_Z_PERIOD,
    EFFECT_SQUARE,
    EFFECT_SQUARE_PERIOD,
    EFFECT_SQUARE_OFFSET,
    EFFECT_SQUARE_Z,
    EFFECT_SQUARE_Z_PERIOD,
    EFFECT_SQUARE_Z_OFFSET,
    EFFECT_PARABOLA_X,
    EFFECT_PARABOLA_Y,
    EFFECT_PARABOLA_Z,
    EFFECT_XMODE,
    EFFECT_TWIRL,
    EFFECT_ROLL,
    NUM_EFFECTS
  };
  /** @brief The various appearance mods. */
  enum Appearance {
    APPEARANCE_HIDDEN,        /**< The arrows disappear partway up. */
    APPEARANCE_HIDDEN_OFFSET, /**< This determines when the arrows disappear. */
    APPEARANCE_SUDDEN,        /**< The arrows appear partway up. */
    APPEARANCE_SUDDEN_OFFSET, /**< This determines when the arrows appear. */
    APPEARANCE_STEALTH,       /**< The arrows are not shown at all. */
    APPEARANCE_BLINK,         /**< The arrows blink constantly. */
    APPEARANCE_RANDOMVANISH,  /**< The arrows disappear, and then reappear in a
                                 different column. */
    NUM_APPEARANCES
  };
  /** @brief The various turn mods. */
  enum Turn {
    TURN_NONE = 0, /**< No turning of the arrows is performed. */
    TURN_MIRROR,   /**< The arrows are mirrored from their normal position. */
    TURN_LRMIRROR, /**< The left and right arrows are mirrored from their normal
                      position. */
    TURN_UDMIRROR, /**< The up and down arrows are mirrored from their normal
                      position. */
    TURN_BACKWARDS, /**< The arrows are turned 180 degrees. This does NOT always
                       equal mirror. */
    TURN_LEFT,      /**< The arrows are turned 90 degrees to the left. */
    TURN_RIGHT,     /**< The arrows are turned 90 degress to the right. */
    TURN_SHUFFLE,   /**< Some of the arrow columns are changed throughout the
                       whole song. */
    TURN_SOFT_SHUFFLE,  /**< Only shuffle arrow columns on an axis of symmetry.
                         */
    TURN_SUPER_SHUFFLE, /**< Every arrow is placed on a random column. */
    TURN_HYPER_SHUFFLE, /**< Every arrow is placed on a random column, but more
                           fairly than SuperShuffle. */
    NUM_TURNS
  };
  enum Transform {
    TRANSFORM_NOHOLDS,
    TRANSFORM_NOROLLS,
    TRANSFORM_NOMINES,
    TRANSFORM_LITTLE,
    TRANSFORM_WIDE,
    TRANSFORM_BIG,
    TRANSFORM_QUICK,
    TRANSFORM_BMRIZE,
    TRANSFORM_SKIPPY,
    TRANSFORM_MINES,
    TRANSFORM_ATTACKMINES,
    TRANSFORM_ECHO,
    TRANSFORM_STOMP,
    TRANSFORM_PLANTED,
    TRANSFORM_FLOORED,
    TRANSFORM_TWISTER,
    TRANSFORM_HOLDROLLS,
    TRANSFORM_NOJUMPS,
    TRANSFORM_NOHANDS,
    TRANSFORM_NOLIFTS,
    TRANSFORM_NOFAKES,
    TRANSFORM_NOQUADS,
    TRANSFORM_NOSTRETCH,
    NUM_TRANSFORMS
  };
  enum Scroll {
    SCROLL_REVERSE = 0,
    SCROLL_SPLIT,
    SCROLL_ALTERNATE,
    SCROLL_CROSS,
    SCROLL_CENTERED,
    NUM_SCROLLS
  };

  float GetReversePercentForColumn(
      int col) const;  // accounts for all Directions

  PlayerNumber pn_;  // Needed for fetching the style.

  HideLightType hide_light_type_;
  LifeType life_type_;
  DrainType drain_type_;  // only used with LifeBar
  ModTimerType mod_timer_type_;
  int battery_lives_;
  /* All floats have a corresponding speed setting, which determines how fast
   * PlayerOptions::Approach approaches. */
  bool set_scroll_speed_;  // true if the scroll speed was set by FromString
  float time_spacing_,
      speed_time_spacing_;  // instead of Beat spacing (CMods, mMods)
  float max_scroll_bpm_, speed_max_scroll_bpm_;
  float scroll_speed_, speed_scroll_speed_;  // used if !m_bTimeSpacing (xMods)
  float scroll_bpm_, speed_scroll_bpm_;      // used if m_bTimeSpacing (CMod)
  float accels_[NUM_ACCELS], speed_accels_[NUM_ACCELS];
  float effects_[NUM_EFFECTS], speed_effects_[NUM_EFFECTS];
  float appearances_[NUM_APPEARANCES], speed_appearances_[NUM_APPEARANCES];
  float scrolls_[NUM_SCROLLS], speed_scrolls_[NUM_SCROLLS];
  float dark_, speed_dark_;
  float blind_, speed_blind_;
  float cover_, speed_cover_;  // hide the background per-player--can't think
                               // of a good name
  float rand_attack_, speed_rand_attack_;
  float no_attack_, speed_no_attack_;
  float player_auto_play_, speed_player_auto_play_;
  float perspective_tilt_,
      speed_perspective_tilt_;  // -1 = near, 0 = overhead, +1 = space
  float skew_, speed_skew_;     // 0 = vanish point is in center of player, 1 =
                                // vanish point is in center of screen

  /* If this is > 0, then the player must have life above this value at the end
   * of the song to pass.  This is independent of SongOptions::m_FailType. */
  float passmark_, speed_passmark_;

  float random_speed_, speed_random_speed_;
  float mod_timer_mult_, speed_mod_timer_mult_;
  float mod_timer_offset_, speed_mod_timer_offset_;
  float draw_size_, speed_draw_size_;
  float draw_size_back_, speed_draw_size_back_;
  /* The maximum column number is 16.*/
  float moves_x_[16], speed_moves_x_[16];
  float moves_y_[16], speed_moves_y_[16];
  float moves_z_[16], speed_moves_z_[16];
  float confusion_x_[16], speed_confusion_x_[16];
  float confusion_y_[16], speed_confusion_y_[16];
  float confusion_z_[16], speed_confusion_z_[16];
  float darks_[16], speed_darks_[16];
  float stealth_[16], speed_stealth_[16];
  float tiny_[16], speed_tiny_[16];
  float bumpy_[16], speed_bumpy_[16];
  float reverse_[16], speed_reverse_[16];

  bool turns_[NUM_TURNS];
  bool transforms_[NUM_TRANSFORMS];
  bool mute_on_error_;
  bool stealth_type_;
  bool stealth_past_receptors_;
  bool dizzy_holds_;
  bool z_buffer_;
  bool cosecant_;
  /** @brief The method for which a player can fail a song. */
  FailType fail_type_;
  TapNoteScore min_tns_to_hide_notes_;

  /**
   * @brief The Noteskin to use.
   *
   * If an empty string, it means to not change from the default. */
  std::string note_skin_;

  /** @brief The Visual Delay additionally applied on a per-player basis in ms.
   */
  float visual_delay_;

  /** @brief The TimingWindow that can be disabled.
   *  Valid values are only W1-W5 which map to indices 0-5 respectively.
   *  Other values are ignored. */
  std::bitset<5> disabled_windows_;

  void NextAccel();
  void NextEffect();
  void NextAppearance();
  void NextTurn();
  void NextTransform();
  void NextScroll();

  void ToggleOneTurn(Turn t);

  // return true if any mods being used will make the song(s) easier
  bool IsEasierForSongAndSteps(Song* song, Steps* steps, PlayerNumber pn) const;
  bool IsEasierForCourseAndTrail(Course* course, Trail* trail) const;
};

#define ADD_MULTICOL_METHOD(method_name) \
  ADD_METHOD(method_name##1);            \
  ADD_METHOD(method_name##2);            \
  ADD_METHOD(method_name##3);            \
  ADD_METHOD(method_name##4);            \
  ADD_METHOD(method_name##5);            \
  ADD_METHOD(method_name##6);            \
  ADD_METHOD(method_name##7);            \
  ADD_METHOD(method_name##8);            \
  ADD_METHOD(method_name##9);            \
  ADD_METHOD(method_name##10);           \
  ADD_METHOD(method_name##11);           \
  ADD_METHOD(method_name##12);           \
  ADD_METHOD(method_name##13);           \
  ADD_METHOD(method_name##14);           \
  ADD_METHOD(method_name##15);           \
  ADD_METHOD(method_name##16);
#define MULTICOL_FLOAT_INTERFACE(func_name, member, valid) \
  FLOAT_INTERFACE(func_name##1, member[0], valid);         \
  FLOAT_INTERFACE(func_name##2, member[1], valid);         \
  FLOAT_INTERFACE(func_name##3, member[2], valid);         \
  FLOAT_INTERFACE(func_name##4, member[3], valid);         \
  FLOAT_INTERFACE(func_name##5, member[4], valid);         \
  FLOAT_INTERFACE(func_name##6, member[5], valid);         \
  FLOAT_INTERFACE(func_name##7, member[6], valid);         \
  FLOAT_INTERFACE(func_name##8, member[7], valid);         \
  FLOAT_INTERFACE(func_name##9, member[8], valid);         \
  FLOAT_INTERFACE(func_name##10, member[9], valid);        \
  FLOAT_INTERFACE(func_name##11, member[10], valid);       \
  FLOAT_INTERFACE(func_name##12, member[11], valid);       \
  FLOAT_INTERFACE(func_name##13, member[12], valid);       \
  FLOAT_INTERFACE(func_name##14, member[13], valid);       \
  FLOAT_INTERFACE(func_name##15, member[14], valid);       \
  FLOAT_INTERFACE(func_name##16, member[15], valid);

#endif

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
