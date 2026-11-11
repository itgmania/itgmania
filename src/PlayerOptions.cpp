#include "PlayerOptions.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "CommonMetrics.h"
#include "Course.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LuaManager.h"
#include "NoteSkinManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RadarValues.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "RageUtil/Regex.h"
#include "Song.h"
#include "StdString.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeMetric.h"
#include "Trail.h"
#include "global.h"

static const char* LifeTypeNames[] = {
    "Bar",
    "Battery",
    "Time",
};
XToString(LifeType);
XToLocalizedString(LifeType);
LuaXType(LifeType);

static const char* DrainTypeNames[] = {
    "Normal",
    "NoRecover",
    "SuddenDeath",
};
XToString(DrainType);
XToLocalizedString(DrainType);
LuaXType(DrainType);

static const char* HideLightTypeNames[] = {
    "NoHideLights",
    "HideAllLights",
    "HideMarqueeLights",
    "HideBassLights",
};
XToString(HideLightType);
XToLocalizedString(HideLightType);
LuaXType(HideLightType);

static const char* ModTimerTypeNames[] = {
    "Game",
    "Beat",
    "Song",
    "Default",
};
XToString(ModTimerType);
XToLocalizedString(ModTimerType);
LuaXType(ModTimerType);

void NextFloat(float values[], int size);
void NextBool(bool values[], int size);

ThemeMetric<float> RANDOM_SPEED_CHANCE("PlayerOptions", "RandomSpeedChance");
ThemeMetric<float> RANDOM_REVERSE_CHANCE(
    "PlayerOptions", "RandomReverseChance");
ThemeMetric<float> RANDOM_DARK_CHANCE("PlayerOptions", "RandomDarkChance");
ThemeMetric<float> RANDOM_ACCEL_CHANCE("PlayerOptions", "RandomAccelChance");
ThemeMetric<float> RANDOM_EFFECT_CHANCE("PlayerOptions", "RandomEffectChance");
ThemeMetric<float> RANDOM_HIDDEN_CHANCE("PlayerOptions", "RandomHiddenChance");
ThemeMetric<float> RANDOM_SUDDEN_CHANCE("PlayerOptions", "RandomSuddenChance");

static const float kCmodDefault = 200.0f;
// Is there a better place for this?
// It needs to be a named constant because it's used in several places in
// this file, but nothing else has a named constant for its default value.
// -Kyz

void PlayerOptions::Init() {
  life_type_ = LifeType_Bar;
  drain_type_ = DrainType_Normal;
  hide_light_type_ = HideLightType_NoHideLights;
  mod_timer_type_ = ModTimerType_Default;
  battery_lives_ = 4;
  min_tns_to_hide_notes_ = PREFSMAN->min_tns_to_hide_notes_;
  set_scroll_speed_ = false;
  max_scroll_bpm_ = 0;
  speed_max_scroll_bpm_ = 1.0f;
  time_spacing_ = 0;
  speed_time_spacing_ = 1.0f;
  scroll_speed_ = 1.0f;
  speed_scroll_speed_ = 1.0f;
  scroll_bpm_ = kCmodDefault;
  speed_scroll_bpm_ = 1.0f;
  ZERO(accels_);
  ONE(speed_accels_);
  ZERO(effects_);
  ONE(speed_effects_);
  ZERO(appearances_);
  ONE(speed_appearances_);
  ZERO(scrolls_);
  ONE(speed_scrolls_);
  dark_ = 0;
  speed_dark_ = 1.0f;
  blind_ = 0;
  speed_blind_ = 1.0f;
  cover_ = 0;
  speed_cover_ = 1.0f;
  rand_attack_ = 0;
  speed_rand_attack_ = 1.0f;
  no_attack_ = 0;
  speed_no_attack_ = 1.0f;
  player_auto_play_ = 0;
  speed_player_auto_play_ = 1.0f;
  perspective_tilt_ = 0;
  speed_perspective_tilt_ = 1.0f;
  skew_ = 0;
  speed_skew_ = 1.0f;
  passmark_ = 0;
  speed_passmark_ = 1.0f;
  random_speed_ = 0;
  speed_random_speed_ = 1.0f;
  mod_timer_mult_ = 0;
  speed_mod_timer_mult_ = 1.0f;
  mod_timer_offset_ = 0;
  speed_mod_timer_offset_ = 1.0f;
  draw_size_ = 0;
  speed_draw_size_ = 1.0f;
  draw_size_back_ = 0;
  speed_draw_size_back_ = 1.0f;
  ZERO(turns_);
  ZERO(transforms_);
  mute_on_error_ = false;
  stealth_type_ = false;
  stealth_past_receptors_ = false;
  dizzy_holds_ = false;
  z_buffer_ = false;
  cosecant_ = false;
  note_skin_ = "";
  visual_delay_ = 0.0f;
  disabled_windows_.reset();
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
}

void PlayerOptions::Approach(const PlayerOptions& other, float fDeltaSeconds) {
  const float fRateMult =
      PREFSMAN->rate_mods_affect_tweens_
          ? GAMESTATE->song_options_.GetCurrent().music_rate_
          : 1.0f;

#define APPROACH(opt) \
  fapproach(opt, other.opt, fRateMult * fDeltaSeconds * other.speed_##opt);
#define DO_COPY(x) x = other.x;

  DO_COPY(life_type_);
  DO_COPY(drain_type_);
  DO_COPY(hide_light_type_);
  DO_COPY(mod_timer_type_);
  DO_COPY(battery_lives_);
  APPROACH(mod_timer_mult_);
  APPROACH(mod_timer_offset_);
  APPROACH(draw_size_);
  APPROACH(draw_size_back_);
  APPROACH(time_spacing_);
  APPROACH(scroll_speed_);
  APPROACH(max_scroll_bpm_);
  fapproach(
      scroll_bpm_, other.scroll_bpm_,
      fDeltaSeconds * other.speed_scroll_bpm_ * 150);
  for (int i = 0; i < NUM_ACCELS; i++) {
    APPROACH(accels_[i]);
  }
  for (int i = 0; i < NUM_EFFECTS; i++) {
    APPROACH(effects_[i]);
  }
  for (int i = 0; i < NUM_APPEARANCES; i++) {
    APPROACH(appearances_[i]);
  }
  for (int i = 0; i < NUM_SCROLLS; i++) {
    APPROACH(scrolls_[i]);
  }
  APPROACH(dark_);
  APPROACH(blind_);
  APPROACH(cover_);
  APPROACH(rand_attack_);
  APPROACH(no_attack_);
  APPROACH(player_auto_play_);
  APPROACH(perspective_tilt_);
  APPROACH(skew_);
  APPROACH(passmark_);
  APPROACH(random_speed_);
  for (int i = 0; i < 16; i++) {
    APPROACH(moves_x_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(moves_y_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(moves_z_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(confusion_x_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(confusion_y_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(confusion_z_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(darks_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(stealth_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(tiny_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(bumpy_[i]);
  }
  for (int i = 0; i < 16; i++) {
    APPROACH(reverse_[i]);
  }

  DO_COPY(set_scroll_speed_);
  for (int i = 0; i < NUM_TURNS; i++) {
    DO_COPY(turns_[i]);
  }
  for (int i = 0; i < NUM_TRANSFORMS; i++) {
    DO_COPY(transforms_[i]);
  }
  DO_COPY(mute_on_error_);
  DO_COPY(stealth_type_);
  DO_COPY(stealth_past_receptors_);
  DO_COPY(dizzy_holds_);
  DO_COPY(z_buffer_);
  DO_COPY(cosecant_);
  DO_COPY(fail_type_);
  DO_COPY(min_tns_to_hide_notes_);
  DO_COPY(note_skin_);
  DO_COPY(visual_delay_);
  DO_COPY(disabled_windows_);
#undef APPROACH
#undef DO_COPY
}

static void AddPart(
    std::vector<std::string>& add_to, float level, std::string name) {
  if (level == 0) {
    return;
  }

  const std::string level_str =
      (level == 1) ? std::string("")
                   : ssprintf("%ld%% ", std::lrint(level * 100));

  add_to.push_back(level_str + name);
}

std::string PlayerOptions::GetString(bool force_note_skin) const {
  std::vector<std::string> v;
  GetMods(v, force_note_skin);
  return join(", ", v);
}

void PlayerOptions::GetMods(
    std::vector<std::string>& add_to, bool force_note_skin) const {
  // std::string sReturn;

  switch (life_type_) {
    case LifeType_Bar:
      switch (drain_type_) {
        case DrainType_NoRecover:
          add_to.push_back("NoRecover");
          break;
        case DrainType_SuddenDeath:
          add_to.push_back("SuddenDeath");
          break;
        case DrainType_Normal:
        default:
          break;
      }
      break;
    case LifeType_Battery:
      add_to.push_back(ssprintf("%dLives", battery_lives_));
      break;
    case LifeType_Time:
      add_to.push_back("LifeTime");
      break;
    default:
      FAIL_M(ssprintf("Invalid LifeType: %i", life_type_));
  }

  switch (hide_light_type_) {
    case HideLightType_NoHideLights:
      add_to.push_back("NoHideLights");
      break;
    case HideLightType_HideAllLights:
      add_to.push_back("HideAllLights");
      break;
    case HideLightType_HideMarqueeLights:
      add_to.push_back("HideMarqueeLights");
      break;
    case HideLightType_HideBassLights:
      add_to.push_back("HideBassLights");
      break;
    default:
      FAIL_M(ssprintf("Invalid HideLightType: %i", hide_light_type_));
  }

  if (!time_spacing_) {
    if (max_scroll_bpm_) {
      std::string s = ssprintf("m%.0f", max_scroll_bpm_);
      add_to.push_back(s);
    } else if (set_scroll_speed_ || scroll_speed_ != 1) {
      /* -> 1.00 */
      std::string s = ssprintf("%2.2f", scroll_speed_);
      if (s[s.size() - 1] == '0') {
        /* -> 1.0 */
        s.erase(s.size() - 1);  // delete last char
        if (s[s.size() - 1] == '0') {
          /* -> 1 */
          s.erase(s.size() - 2);  // delete last 2 chars
        }
      }
      add_to.push_back(s + "x");
    }
  } else {
    std::string s = ssprintf("C%.0f", scroll_bpm_);
    add_to.push_back(s);
  }

  switch (mod_timer_type_) {
    case ModTimerType_Game:
      add_to.push_back("ModTimerGame");
      break;
    case ModTimerType_Beat:
      add_to.push_back("ModTimerBeat");
      break;
    case ModTimerType_Song:
      add_to.push_back("ModTimerSong");
      break;
    case ModTimerType_Default:
      break;
    default:
      FAIL_M(ssprintf("Invalid ModTimerType: %i", mod_timer_type_));
  }

  AddPart(add_to, accels_[ACCEL_BOOST], "Boost");
  AddPart(add_to, accels_[ACCEL_BRAKE], "Brake");
  AddPart(add_to, accels_[ACCEL_WAVE], "Wave");
  AddPart(add_to, accels_[ACCEL_WAVE_PERIOD], "WavePeriod");
  AddPart(add_to, accels_[ACCEL_EXPAND], "Expand");
  AddPart(add_to, accels_[ACCEL_EXPAND_PERIOD], "ExpandPeriod");
  AddPart(add_to, accels_[ACCEL_TAN_EXPAND], "TanExpand");
  AddPart(add_to, accels_[ACCEL_TAN_EXPAND_PERIOD], "TanExpandPeriod");
  AddPart(add_to, accels_[ACCEL_BOOMERANG], "Boomerang");

  AddPart(add_to, effects_[EFFECT_DRUNK], "Drunk");
  AddPart(add_to, effects_[EFFECT_DRUNK_SPEED], "DrunkSpeed");
  AddPart(add_to, effects_[EFFECT_DRUNK_OFFSET], "DrunkOffset");
  AddPart(add_to, effects_[EFFECT_DRUNK_PERIOD], "DrunkPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK], "TanDrunk");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_SPEED], "TanDrunkSpeed");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_OFFSET], "TanDrunkOffset");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_PERIOD], "TanDrunkPeriod");
  AddPart(add_to, effects_[EFFECT_DRUNK_Z], "DrunkZ");
  AddPart(add_to, effects_[EFFECT_DRUNK_Z_SPEED], "DrunkZSpeed");
  AddPart(add_to, effects_[EFFECT_DRUNK_Z_OFFSET], "DrunkZOffset");
  AddPart(add_to, effects_[EFFECT_DRUNK_Z_PERIOD], "DrunkZPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_Z], "TanDrunkZ");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_Z_SPEED], "TanDrunkZSpeed");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_Z_OFFSET], "TanDrunkZOffset");
  AddPart(add_to, effects_[EFFECT_TAN_DRUNK_Z_PERIOD], "TanDrunkZPeriod");
  AddPart(add_to, effects_[EFFECT_SHRINK_TO_LINEAR], "ShrinkLinear");
  AddPart(add_to, effects_[EFFECT_SHRINK_TO_MULT], "ShrinkMult");
  AddPart(add_to, effects_[EFFECT_PULSE_INNER], "PulseInner");
  AddPart(add_to, effects_[EFFECT_PULSE_OUTER], "PulseOuter");
  AddPart(add_to, effects_[EFFECT_PULSE_PERIOD], "PulsePeriod");
  AddPart(add_to, effects_[EFFECT_PULSE_OFFSET], "PulseOffset");
  AddPart(add_to, effects_[EFFECT_ATTENUATE_X], "AttenuateX");
  AddPart(add_to, effects_[EFFECT_ATTENUATE_Y], "AttenuateY");
  AddPart(add_to, effects_[EFFECT_ATTENUATE_Z], "AttenuateZ");
  AddPart(add_to, effects_[EFFECT_DIZZY], "Dizzy");
  AddPart(add_to, effects_[EFFECT_CONFUSION], "Confusion");
  AddPart(add_to, effects_[EFFECT_CONFUSION_OFFSET], "ConfusionOffset");
  AddPart(add_to, effects_[EFFECT_CONFUSION_X], "ConfusionX");
  AddPart(add_to, effects_[EFFECT_CONFUSION_X_OFFSET], "ConfusionXOffset");
  AddPart(add_to, effects_[EFFECT_CONFUSION_Y], "ConfusionY");
  AddPart(add_to, effects_[EFFECT_CONFUSION_Y_OFFSET], "ConfusionYOffset");
  AddPart(add_to, effects_[EFFECT_BOUNCE], "Bounce");
  AddPart(add_to, effects_[EFFECT_BOUNCE_PERIOD], "BouncePeriod");
  AddPart(add_to, effects_[EFFECT_BOUNCE_OFFSET], "BounceOffset");
  AddPart(add_to, effects_[EFFECT_BOUNCE_Z], "BounceZ");
  AddPart(add_to, effects_[EFFECT_BOUNCE_Z_PERIOD], "BounceZPeriod");
  AddPart(add_to, effects_[EFFECT_BOUNCE_Z_OFFSET], "BounceZOffset");
  AddPart(add_to, effects_[EFFECT_MINI], "Mini");
  AddPart(add_to, effects_[EFFECT_TINY], "Tiny");
  AddPart(add_to, effects_[EFFECT_FLIP], "Flip");
  AddPart(add_to, effects_[EFFECT_INVERT], "Invert");
  AddPart(add_to, effects_[EFFECT_TORNADO], "Tornado");
  AddPart(add_to, effects_[EFFECT_TORNADO_PERIOD], "TornadoPeriod");
  AddPart(add_to, effects_[EFFECT_TORNADO_OFFSET], "TornadoOffset");
  AddPart(add_to, effects_[EFFECT_TAN_TORNADO], "TanTornado");
  AddPart(add_to, effects_[EFFECT_TAN_TORNADO_PERIOD], "TanTornadoPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_TORNADO_OFFSET], "TanTornadoOffset");
  AddPart(add_to, effects_[EFFECT_TORNADO_Z], "TornadoZ");
  AddPart(add_to, effects_[EFFECT_TORNADO_Z_PERIOD], "TornadoZPeriod");
  AddPart(add_to, effects_[EFFECT_TORNADO_Z_OFFSET], "TornadoZOffset");
  AddPart(add_to, effects_[EFFECT_TAN_TORNADO_Z], "TanTornadoZ");
  AddPart(add_to, effects_[EFFECT_TAN_TORNADO_Z_PERIOD], "TanTornadoZPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_TORNADO_Z_OFFSET], "TanTornadoZOffset");
  AddPart(add_to, effects_[EFFECT_TIPSY], "Tipsy");
  AddPart(add_to, effects_[EFFECT_TIPSY_SPEED], "TipsySpeed");
  AddPart(add_to, effects_[EFFECT_TIPSY_OFFSET], "TipsyOffset");
  AddPart(add_to, effects_[EFFECT_TAN_TIPSY], "TanTipsy");
  AddPart(add_to, effects_[EFFECT_TAN_TIPSY_SPEED], "TanTipsySpeed");
  AddPart(add_to, effects_[EFFECT_TAN_TIPSY_OFFSET], "TanTipsyOffset");
  AddPart(add_to, effects_[EFFECT_BUMPY], "Bumpy");
  AddPart(add_to, effects_[EFFECT_BUMPY_OFFSET], "BumpyOffset");
  AddPart(add_to, effects_[EFFECT_BUMPY_PERIOD], "BumpyPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_BUMPY], "TanBumpy");
  AddPart(add_to, effects_[EFFECT_TAN_BUMPY_OFFSET], "TanBumpyOffset");
  AddPart(add_to, effects_[EFFECT_TAN_BUMPY_PERIOD], "TanBumpyPeriod");
  AddPart(add_to, effects_[EFFECT_BUMPY_X], "BumpyX");
  AddPart(add_to, effects_[EFFECT_BUMPY_X_OFFSET], "BumpyXOffset");
  AddPart(add_to, effects_[EFFECT_BUMPY_X_PERIOD], "BumpyXPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_BUMPY_X], "TanBumpyX");
  AddPart(add_to, effects_[EFFECT_TAN_BUMPY_X_OFFSET], "TanBumpyXOffset");
  AddPart(add_to, effects_[EFFECT_TAN_BUMPY_X_PERIOD], "TanBumpyXPeriod");
  AddPart(add_to, effects_[EFFECT_BEAT], "Beat");
  AddPart(add_to, effects_[EFFECT_BEAT_OFFSET], "BeatOffset");
  AddPart(add_to, effects_[EFFECT_BEAT_PERIOD], "BeatPeriod");
  AddPart(add_to, effects_[EFFECT_BEAT_MULT], "BeatMult");
  AddPart(add_to, effects_[EFFECT_BEAT_Y], "BeatY");
  AddPart(add_to, effects_[EFFECT_BEAT_Y_OFFSET], "BeatYOffset");
  AddPart(add_to, effects_[EFFECT_BEAT_Y_PERIOD], "BeatYPeriod");
  AddPart(add_to, effects_[EFFECT_BEAT_Y_MULT], "BeatYMult");
  AddPart(add_to, effects_[EFFECT_BEAT_Z], "BeatZ");
  AddPart(add_to, effects_[EFFECT_BEAT_Z_OFFSET], "BeatZOffset");
  AddPart(add_to, effects_[EFFECT_BEAT_Z_PERIOD], "BeatZPeriod");
  AddPart(add_to, effects_[EFFECT_BEAT_Z_MULT], "BeatZMult");
  AddPart(add_to, effects_[EFFECT_ZIGZAG], "Zigzag");
  AddPart(add_to, effects_[EFFECT_ZIGZAG_PERIOD], "ZigzagPeriod");
  AddPart(add_to, effects_[EFFECT_ZIGZAG_OFFSET], "ZigzagOffset");
  AddPart(add_to, effects_[EFFECT_ZIGZAG_Z], "ZigzagZ");
  AddPart(add_to, effects_[EFFECT_ZIGZAG_Z_PERIOD], "ZigzagZPeriod");
  AddPart(add_to, effects_[EFFECT_ZIGZAG_Z_OFFSET], "ZigzagZOffset");
  AddPart(add_to, effects_[EFFECT_SAWTOOTH], "Sawtooth");
  AddPart(add_to, effects_[EFFECT_SAWTOOTH_PERIOD], "SawtoothPeriod");
  AddPart(add_to, effects_[EFFECT_SAWTOOTH_Z], "SawtoothZ");
  AddPart(add_to, effects_[EFFECT_SAWTOOTH_Z_PERIOD], "SawtoothZPeriod");
  AddPart(add_to, effects_[EFFECT_SQUARE], "Square");
  AddPart(add_to, effects_[EFFECT_SQUARE_OFFSET], "SquareOffset");
  AddPart(add_to, effects_[EFFECT_SQUARE_PERIOD], "SquarePeriod");
  AddPart(add_to, effects_[EFFECT_SQUARE_Z], "SquareZ");
  AddPart(add_to, effects_[EFFECT_SQUARE_Z_OFFSET], "SquareZOffset");
  AddPart(add_to, effects_[EFFECT_SQUARE_Z_PERIOD], "SquareZPeriod");
  AddPart(add_to, effects_[EFFECT_DIGITAL], "Digital");
  AddPart(add_to, effects_[EFFECT_DIGITAL_STEPS], "DigitalSteps");
  AddPart(add_to, effects_[EFFECT_DIGITAL_PERIOD], "DigitalPeriod");
  AddPart(add_to, effects_[EFFECT_DIGITAL_OFFSET], "DigitalOffset");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL], "TanDigital");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_STEPS], "TanDigitalSteps");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_PERIOD], "TanDigitalPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_OFFSET], "TanDigitalOffset");
  AddPart(add_to, effects_[EFFECT_DIGITAL_Z], "DigitalZ");
  AddPart(add_to, effects_[EFFECT_DIGITAL_Z_STEPS], "DigitalZSteps");
  AddPart(add_to, effects_[EFFECT_DIGITAL_Z_PERIOD], "DigitalZPeriod");
  AddPart(add_to, effects_[EFFECT_DIGITAL_Z_OFFSET], "DigitalZOffset");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_Z], "TanDigitalZ");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_Z_STEPS], "TanDigitalZSteps");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_Z_PERIOD], "TanDigitalZPeriod");
  AddPart(add_to, effects_[EFFECT_TAN_DIGITAL_Z_OFFSET], "TanDigitalZOffset");
  AddPart(add_to, effects_[EFFECT_PARABOLA_X], "ParabolaX");
  AddPart(add_to, effects_[EFFECT_PARABOLA_Y], "ParabolaY");
  AddPart(add_to, effects_[EFFECT_PARABOLA_Z], "ParabolaZ");
  AddPart(add_to, effects_[EFFECT_XMODE], "XMode");
  AddPart(add_to, effects_[EFFECT_TWIRL], "Twirl");
  AddPart(add_to, effects_[EFFECT_ROLL], "Roll");
  AddPart(add_to, stealth_type_, "StealthType");
  AddPart(add_to, stealth_past_receptors_, "StealthPastReceptors");
  AddPart(add_to, dizzy_holds_, "DizzyHolds");
  AddPart(add_to, z_buffer_, "ZBuffer");
  AddPart(add_to, cosecant_, "Cosecant");

  for (int i = 0; i < 16; i++) {
    std::string s = ssprintf("MoveX%d", i + 1);

    AddPart(add_to, moves_x_[i], s);
    s = ssprintf("MoveY%d", i + 1);
    AddPart(add_to, moves_y_[i], s);
    s = ssprintf("MoveZ%d", i + 1);
    AddPart(add_to, moves_z_[i], s);
    s = ssprintf("ConfusionOffset%d", i + 1);
    AddPart(add_to, confusion_x_[i], s);
    s = ssprintf("ConfusionYOffset%d", i + 1);
    AddPart(add_to, confusion_y_[i], s);
    s = ssprintf("ConfusionZOffset%d", i + 1);
    AddPart(add_to, confusion_z_[i], s);
    s = ssprintf("Dark%d", i + 1);
    AddPart(add_to, darks_[i], s);
    s = ssprintf("Stealth%d", i + 1);
    AddPart(add_to, stealth_[i], s);
    s = ssprintf("Tiny%d", i + 1);
    AddPart(add_to, tiny_[i], s);
    s = ssprintf("Bumpy%d", i + 1);
    AddPart(add_to, bumpy_[i], s);
    s = ssprintf("Reverse%d", i + 1);
    AddPart(add_to, reverse_[i], s);
  }

  AddPart(add_to, appearances_[APPEARANCE_HIDDEN], "Hidden");
  AddPart(add_to, appearances_[APPEARANCE_HIDDEN_OFFSET], "HiddenOffset");
  AddPart(add_to, appearances_[APPEARANCE_SUDDEN], "Sudden");
  AddPart(add_to, appearances_[APPEARANCE_SUDDEN_OFFSET], "SuddenOffset");
  AddPart(add_to, appearances_[APPEARANCE_STEALTH], "Stealth");
  AddPart(add_to, appearances_[APPEARANCE_BLINK], "Blink");
  AddPart(add_to, appearances_[APPEARANCE_RANDOMVANISH], "RandomVanish");

  AddPart(add_to, scrolls_[SCROLL_REVERSE], "Reverse");
  AddPart(add_to, scrolls_[SCROLL_SPLIT], "Split");
  AddPart(add_to, scrolls_[SCROLL_ALTERNATE], "Alternate");
  AddPart(add_to, scrolls_[SCROLL_CROSS], "Cross");
  AddPart(add_to, scrolls_[SCROLL_CENTERED], "Centered");

  AddPart(add_to, mod_timer_mult_, "ModTimerMult");
  AddPart(add_to, mod_timer_offset_, "ModTimerOffset");
  AddPart(add_to, draw_size_, "DrawSize");
  AddPart(add_to, draw_size_back_, "DrawSizeBack");

  AddPart(add_to, dark_, "Dark");

  AddPart(add_to, blind_, "Blind");
  AddPart(add_to, cover_, "Cover");

  AddPart(add_to, rand_attack_, "RandomAttacks");
  AddPart(add_to, no_attack_, "NoAttacks");
  AddPart(add_to, player_auto_play_, "PlayerAutoPlay");

  AddPart(add_to, passmark_, "Passmark");

  AddPart(add_to, random_speed_, "RandomSpeed");

  if (turns_[TURN_MIRROR]) {
    add_to.push_back("Mirror");
  }
  if (turns_[TURN_LRMIRROR]) {
    add_to.push_back("LRMirror");
  }
  if (turns_[TURN_UDMIRROR]) {
    add_to.push_back("UDMirror");
  }
  if (turns_[TURN_BACKWARDS]) {
    add_to.push_back("Backwards");
  }
  if (turns_[TURN_LEFT]) {
    add_to.push_back("Left");
  }
  if (turns_[TURN_RIGHT]) {
    add_to.push_back("Right");
  }
  if (turns_[TURN_SHUFFLE]) {
    add_to.push_back("Shuffle");
  }
  if (turns_[TURN_SOFT_SHUFFLE]) {
    add_to.push_back("SoftShuffle");
  }
  if (turns_[TURN_SUPER_SHUFFLE]) {
    add_to.push_back("SuperShuffle");
  }
  if (turns_[TURN_HYPER_SHUFFLE]) {
    add_to.push_back("HyperShuffle");
  }

  if (transforms_[TRANSFORM_NOHOLDS]) {
    add_to.push_back("NoHolds");
  }
  if (transforms_[TRANSFORM_NOROLLS]) {
    add_to.push_back("NoRolls");
  }
  if (transforms_[TRANSFORM_NOMINES]) {
    add_to.push_back("NoMines");
  }
  if (transforms_[TRANSFORM_LITTLE]) {
    add_to.push_back("Little");
  }
  if (transforms_[TRANSFORM_WIDE]) {
    add_to.push_back("Wide");
  }
  if (transforms_[TRANSFORM_BIG]) {
    add_to.push_back("Big");
  }
  if (transforms_[TRANSFORM_QUICK]) {
    add_to.push_back("Quick");
  }
  if (transforms_[TRANSFORM_BMRIZE]) {
    add_to.push_back("BMRize");
  }
  if (transforms_[TRANSFORM_SKIPPY]) {
    add_to.push_back("Skippy");
  }
  if (transforms_[TRANSFORM_MINES]) {
    add_to.push_back("Mines");
  }
  if (transforms_[TRANSFORM_ATTACKMINES]) {
    add_to.push_back("AttackMines");
  }
  if (transforms_[TRANSFORM_ECHO]) {
    add_to.push_back("Echo");
  }
  if (transforms_[TRANSFORM_STOMP]) {
    add_to.push_back("Stomp");
  }
  if (transforms_[TRANSFORM_PLANTED]) {
    add_to.push_back("Planted");
  }
  if (transforms_[TRANSFORM_FLOORED]) {
    add_to.push_back("Floored");
  }
  if (transforms_[TRANSFORM_TWISTER]) {
    add_to.push_back("Twister");
  }
  if (transforms_[TRANSFORM_HOLDROLLS]) {
    add_to.push_back("HoldsToRolls");
  }
  if (transforms_[TRANSFORM_NOJUMPS]) {
    add_to.push_back("NoJumps");
  }
  if (transforms_[TRANSFORM_NOHANDS]) {
    add_to.push_back("NoHands");
  }
  if (transforms_[TRANSFORM_NOLIFTS]) {
    add_to.push_back("NoLifts");
  }
  if (transforms_[TRANSFORM_NOFAKES]) {
    add_to.push_back("NoFakes");
  }
  if (transforms_[TRANSFORM_NOQUADS]) {
    add_to.push_back("NoQuads");
  }
  if (transforms_[TRANSFORM_NOSTRETCH]) {
    add_to.push_back("NoStretch");
  }
  if (mute_on_error_) {
    add_to.push_back("MuteOnError");
  }

  switch (fail_type_) {
    case FailType_Immediate:
      break;
    case FailType_ImmediateContinue:
      add_to.push_back("FailImmediateContinue");
      break;
    case FailType_EndOfSong:
      add_to.push_back("FailAtEnd");
      break;
    case FailType_Off:
      add_to.push_back("FailOff");
      break;
    default:
      FAIL_M(ssprintf("Invalid FailType: %i", fail_type_));
  }

  if (skew_ == 0 && perspective_tilt_ == 0) {
    add_to.push_back("Overhead");
  } else if (skew_ == 0) {
    if (perspective_tilt_ > 0) {
      AddPart(add_to, perspective_tilt_, "Distant");
    } else {
      AddPart(add_to, -perspective_tilt_, "Hallway");
    }
  } else if (std::abs(skew_ - perspective_tilt_) < 0.0001f) {
    AddPart(add_to, skew_, "Space");
  } else if (std::abs(skew_ + perspective_tilt_) < 0.0001f) {
    AddPart(add_to, skew_, "Incoming");
  } else {
    AddPart(add_to, skew_, "Skew");
    AddPart(add_to, perspective_tilt_, "Tilt");
  }

  // Don't display a string if using the default NoteSkin unless we force it.
  if (force_note_skin ||
      (!note_skin_.empty() &&
       note_skin_ != CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue())) {
    std::string s = note_skin_;
    Capitalize(s);
    add_to.push_back(s);
  }

  if (std::abs(visual_delay_) > 0.0001f) {
    // Format the string to be something like "10ms VisualDelay".
    // Note that we don't process sub-millisecond visual delay.
    add_to.push_back(ssprintf("%.0fms VisualDelay", visual_delay_ * 1000.0f));
  }

  if (disabled_windows_.count() != 0) {
    std::stringstream ss;
    bool is_first = true;
    ss << "No ";
    for (int i = TW_W1; i <= TW_W5; ++i) {
      if (disabled_windows_[i]) {
        if (!is_first) {
          ss << "/";
        } else {
          is_first = false;
        }
        ss << TimingWindowToString(static_cast<TimingWindow>(i)).c_str();
      }
    }

    // Final string will be something like "No W4/W5"
    add_to.push_back(ss.str());
  }
}

/* Options are added to the current settings; call Init() beforehand if
 * you don't want this. */
void PlayerOptions::FromString(const std::string& multiple_mods) {
  std::string temp = multiple_mods;
  std::vector<std::string> vs;
  split(temp, ",", vs, true);
  std::string throw_away;
  for (std::string& s : vs) {
    if (!FromOneModString(s, throw_away)) {
      LOG->Trace(
          "Attempted to load a non-existing mod \'%s\' for the Player. "
          "Ignoring.",
          s.c_str());
    }
  }
}

bool PlayerOptions::FromOneModString(
    const std::string& one_mod, std::string& error_out) {
  ASSERT_M(
      NOTESKIN != nullptr,
      "The Noteskin Manager must be loaded in order to process mods.");

  std::string bit = one_mod;
  std::string mod = "";
  MakeLower(bit);
  Trim(bit);

  /* "drunk"
   * "no drunk"
   * "150% drunk"
   * "*2 100% drunk": approach at 2x normal speed */

  float level = 1;
  float speed = 1;
  std::vector<std::string> parts;
  split(bit, " ", parts, true);

  for (const std::string& s : parts) {
    if (s == "no") {
      level = 0;
    } else if (isdigit(s[0]) || s[0] == '-') {
      if (EndsWith(s, "ms")) {
        // Strip off the "ms" before parsing and convert to seconds.
        std::string ms_value = s.substr(0, s.size() - 2);
        level = StringToFloat(ms_value) / 1000.0f;
      }
      /* If the last character is a *, they probably said "123*" when
       * they meant "*123". */
      else if (Right(s, 1) == "*") {
        // XXX: We know what they want, is there any reason not to handle it?
        // Yes. We should be strict in handling the format. -Chris
        error_out = ssprintf(
            "Invalid player options \"%s\"; did you mean '*%d'?", s.c_str(),
            StringToInt(s));
        return false;
      } else {
        level = StringToFloat(s) / 100.0f;
      }
    } else if (s[0] == '*') {
      sscanf(s.c_str(), "*%f", &speed);
      if (!std::isfinite(speed)) {
        speed = 1.0f;
      }
    }
  }

  bit = parts.back();

#define SET_FLOAT(opt)   \
  {                      \
    opt = level;         \
    speed_##opt = speed; \
  }
  const bool on = (level > 0.5f);

  static Regex mult("^([0-9]+(\\.[0-9]+)?)x$");
  static Regex disabled_windows("(w[1-5])");
  std::vector<std::string> matches;
  if (mult.Compare(bit, matches)) {
    StringConversion::FromString(matches[0], level);
    SET_FLOAT(scroll_speed_)
    SET_FLOAT(time_spacing_)
    time_spacing_ = 0;
    max_scroll_bpm_ = 0;
  } else if (sscanf(bit.c_str(), "c%f", &level) == 1) {
    if (!std::isfinite(level) || level <= 0.0f) {
      level = kCmodDefault;
    }
    SET_FLOAT(scroll_bpm_)
    SET_FLOAT(time_spacing_)
    time_spacing_ = 1;
    max_scroll_bpm_ = 0;
  }
  // oITG's m-mods
  else if (sscanf(bit.c_str(), "m%f", &level) == 1) {
    // OpenITG doesn't have this block:
    /*
    if( !std::isfinite(level) || level <= 0.0f )
            level = CMOD_DEFAULT;
    */
    SET_FLOAT(max_scroll_bpm_)
    time_spacing_ = 0;
  }

  else if (bit == "clearall") {
    Init();
    note_skin_ = NOTESKIN->GetDefaultNoteSkinName();
  } else if (bit == "resetspeed") {
    /* level is set to the values from Init() because all speed related
       fields are being reset to initial values, and they each have different
       initial values.  -kyz */
    level = 0;
    SET_FLOAT(max_scroll_bpm_);
    SET_FLOAT(time_spacing_);
    level = 1.0f;
    SET_FLOAT(scroll_speed_);
    level = kCmodDefault;
    SET_FLOAT(scroll_bpm_)
  } else if (bit == "life" || bit == "lives") {
    // level is a percentage for every other option, so multiply by 100. -Kyz
    battery_lives_ = level * 100.0f;
  } else if (bit.find("modtimer") != bit.npos) {
    if (bit == "modtimerdefault") {
      mod_timer_type_ = ModTimerType_Default;
    } else if (bit == "modtimersong") {
      mod_timer_type_ = ModTimerType_Song;
    } else if (bit == "modtimerbeat") {
      mod_timer_type_ = ModTimerType_Beat;
    } else if (bit == "modtimergame") {
      mod_timer_type_ = ModTimerType_Game;
    } else if (bit == "modtimermult") {
      SET_FLOAT(mod_timer_mult_)
    } else if (bit == "modtimeroffset") {
      SET_FLOAT(mod_timer_offset_)
    }
  } else if (bit.find("drawsize") != bit.npos) {
    if (bit == "drawsize") {
      SET_FLOAT(draw_size_)
    } else if (bit == "drawsizeback") {
      SET_FLOAT(draw_size_back_)
    }
  } else if (bit == "bar") {
    life_type_ = LifeType_Bar;
  } else if (bit == "battery") {
    life_type_ = LifeType_Battery;
  } else if (bit == "lifetime") {
    life_type_ = LifeType_Time;
  } else if (bit == "norecover" || bit == "power-drop") {
    drain_type_ = DrainType_NoRecover;
  } else if (bit == "suddendeath" || bit == "death") {
    drain_type_ = DrainType_SuddenDeath;
  } else if (bit == "normal-drain") {
    drain_type_ = DrainType_Normal;
  } else if (bit.find("lights") != bit.npos) {
    if (bit == "nohidelights") {
      hide_light_type_ = HideLightType_NoHideLights;
    } else if (bit == "hidealllights") {
      hide_light_type_ = HideLightType_HideAllLights;
    } else if (bit == "hidemarqueelights") {
      hide_light_type_ = HideLightType_HideMarqueeLights;
    } else if (bit == "hidebasslights") {
      hide_light_type_ = HideLightType_HideBassLights;
    }
  } else if (bit == "boost") {
    SET_FLOAT(accels_[ACCEL_BOOST])
  } else if (bit == "brake" || bit == "land") {
    SET_FLOAT(accels_[ACCEL_BRAKE])
  } else if (bit.find("wave") != bit.npos) {
    if (bit == "wave") {
      SET_FLOAT(accels_[ACCEL_WAVE])
    } else if (bit == "waveperiod") {
      SET_FLOAT(accels_[ACCEL_WAVE_PERIOD])
    }
  } else if (
      (bit.find("expand") != bit.npos) || (bit.find("dwiwave") != bit.npos)) {
    if (bit == "expand" || bit == "dwiwave") {
      SET_FLOAT(accels_[ACCEL_EXPAND])
    } else if (bit == "expandperiod") {
      SET_FLOAT(accels_[ACCEL_EXPAND_PERIOD])
    } else if (bit == "tanexpand") {
      SET_FLOAT(accels_[ACCEL_TAN_EXPAND])
    } else if (bit == "tanexpandperiod") {
      SET_FLOAT(accels_[ACCEL_TAN_EXPAND_PERIOD])
    }
  } else if (bit == "boomerang") {
    SET_FLOAT(accels_[ACCEL_BOOMERANG])
  } else if (bit.find("drunk") != bit.npos) {
    if (bit == "drunk") {
      SET_FLOAT(effects_[EFFECT_DRUNK])
    } else if (bit == "drunkspeed") {
      SET_FLOAT(effects_[EFFECT_DRUNK_SPEED])
    } else if (bit == "drunkoffset") {
      SET_FLOAT(effects_[EFFECT_DRUNK_OFFSET])
    } else if (bit == "drunkperiod") {
      SET_FLOAT(effects_[EFFECT_DRUNK_PERIOD])
    } else if (bit == "tandrunk") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK])
    } else if (bit == "tandrunkspeed") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_SPEED])
    } else if (bit == "tandrunkoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_OFFSET])
    } else if (bit == "tandrunkperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_PERIOD])
    } else if (bit == "drunkz") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z])
    } else if (bit == "drunkzspeed") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z_SPEED])
    } else if (bit == "drunkzoffset") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z_OFFSET])
    } else if (bit == "drunkzperiod") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z_PERIOD])
    } else if (bit == "tandrunkz") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z])
    } else if (bit == "tandrunkzspeed") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z_SPEED])
    } else if (bit == "tandrunkzoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z_OFFSET])
    } else if (bit == "tandrunkzperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z_PERIOD])
    }
  } else if (bit.find("shrink") != bit.npos) {
    if (bit == "shrinklinear") {
      SET_FLOAT(effects_[EFFECT_SHRINK_TO_LINEAR])
    } else if (bit == "shrinkmult") {
      SET_FLOAT(effects_[EFFECT_SHRINK_TO_MULT])
    }
  } else if (bit.find("pulse") != bit.npos) {
    if (bit == "pulseinner") {
      SET_FLOAT(effects_[EFFECT_PULSE_INNER])
    } else if (bit == "pulseouter") {
      SET_FLOAT(effects_[EFFECT_PULSE_OUTER])
    } else if (bit == "pulseoffset") {
      SET_FLOAT(effects_[EFFECT_PULSE_OFFSET])
    } else if (bit == "pulseperiod") {
      SET_FLOAT(effects_[EFFECT_PULSE_PERIOD])
    }
  } else if (bit.find("dizzy") != bit.npos) {
    if (bit == "dizzy") {
      SET_FLOAT(effects_[EFFECT_DIZZY])
    } else if (bit == "dizzyholds") {
      dizzy_holds_ = on;
    }
  } else if (bit.find("confusion") != bit.npos) {
    if (bit.find("x") != bit.npos) {
      if (bit == "confusionx") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_X])
      } else if (bit == "confusionxoffset") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_X_OFFSET])
      } else {
        for (int i = 0; i < 16; i++) {
          mod = ssprintf("confusionxoffset%d", i + 1);
          if (bit == mod) {
            SET_FLOAT(confusion_x_[i])
            break;
          }
        }
      }
    } else if (bit.find("y") != bit.npos) {
      if (bit == "confusiony") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_Y])
      } else if (bit == "confusionyoffset") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_Y_OFFSET])
      } else {
        for (int i = 0; i < 16; i++) {
          mod = ssprintf("confusionyoffset%d", i + 1);
          if (bit == mod) {
            SET_FLOAT(confusion_y_[i])
            break;
          }
        }
      }
    }
    if (bit == "confusion") {
      SET_FLOAT(effects_[EFFECT_CONFUSION])
    } else if (bit == "confusionoffset") {
      SET_FLOAT(effects_[EFFECT_CONFUSION_OFFSET])
    } else {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("confusionoffset%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(confusion_z_[i])
          break;
        }
      }
    }
  } else if (bit.find("bounce") != bit.npos) {
    if (bit == "bounce") {
      SET_FLOAT(effects_[EFFECT_BOUNCE])
    } else if (bit == "bounceperiod") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_PERIOD])
    } else if (bit == "bounceoffset") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_OFFSET])
    } else if (bit == "bouncez") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_Z])
    } else if (bit == "bouncezperiod") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_Z_PERIOD])
    } else if (bit == "bouncezoffset") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_Z_OFFSET])
    }
  } else if (bit == "mini") {
    SET_FLOAT(effects_[EFFECT_MINI])
  } else if (bit.find("tiny") != bit.npos) {
    if (bit == "tiny") {
      SET_FLOAT(effects_[EFFECT_TINY])
    } else {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("tiny%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(tiny_[i])
          break;
        }
      }
    }
  } else if (bit == "flip") {
    SET_FLOAT(effects_[EFFECT_FLIP])
  } else if (bit == "invert") {
    SET_FLOAT(effects_[EFFECT_INVERT])
  } else if (bit.find("tornado") != bit.npos) {
    if (bit == "tornado") {
      SET_FLOAT(effects_[EFFECT_TORNADO])
    } else if (bit == "tornadoperiod") {
      SET_FLOAT(effects_[EFFECT_TORNADO_PERIOD])
    } else if (bit == "tornadooffset") {
      SET_FLOAT(effects_[EFFECT_TORNADO_OFFSET])
    } else if (bit == "tantornado") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO])
    } else if (bit == "tantornadoperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_PERIOD])
    } else if (bit == "tantornadooffset") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_OFFSET])
    } else if (bit == "tornadoz") {
      SET_FLOAT(effects_[EFFECT_TORNADO_Z])
    } else if (bit == "tornadozperiod") {
      SET_FLOAT(effects_[EFFECT_TORNADO_Z_PERIOD])
    } else if (bit == "tornadozoffset") {
      SET_FLOAT(effects_[EFFECT_TORNADO_Z_OFFSET])
    } else if (bit == "tantornadoz") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_Z])
    } else if (bit == "tantornadozperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_Z_PERIOD])
    } else if (bit == "tantornadozoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_Z_OFFSET])
    }
  } else if (bit.find("tipsy") != bit.npos) {
    if (bit == "tipsy") {
      SET_FLOAT(effects_[EFFECT_TIPSY])
    } else if (bit == "tipsyspeed") {
      SET_FLOAT(effects_[EFFECT_TIPSY_SPEED])
    } else if (bit == "tipsyoffset") {
      SET_FLOAT(effects_[EFFECT_TIPSY_OFFSET])
    } else if (bit == "tantipsy") {
      SET_FLOAT(effects_[EFFECT_TAN_TIPSY])
    } else if (bit == "tantipsyspeed") {
      SET_FLOAT(effects_[EFFECT_TAN_TIPSY_SPEED])
    } else if (bit == "tantipsyoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_TIPSY_OFFSET])
    }
  } else if (bit.find("bumpy") != bit.npos) {
    if (bit == "bumpy") {
      SET_FLOAT(effects_[EFFECT_BUMPY])
    } else if (bit == "bumpyoffset") {
      SET_FLOAT(effects_[EFFECT_BUMPY_OFFSET])
    } else if (bit == "bumpyperiod") {
      SET_FLOAT(effects_[EFFECT_BUMPY_PERIOD])
    } else if (bit == "tanbumpy") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY])
    } else if (bit == "tanbumpyoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_OFFSET])
    } else if (bit == "tanbumpyperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_PERIOD])
    } else if (bit == "bumpyx") {
      SET_FLOAT(effects_[EFFECT_BUMPY_X])
    } else if (bit == "bumpyxoffset") {
      SET_FLOAT(effects_[EFFECT_BUMPY_X_OFFSET])
    } else if (bit == "bumpyxperiod") {
      SET_FLOAT(effects_[EFFECT_BUMPY_X_PERIOD])
    } else if (bit == "tanbumpyx") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_X])
    } else if (bit == "tanbumpyxoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_X_OFFSET])
    } else if (bit == "tanbumpyxperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_X_PERIOD])
    } else {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("bumpy%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(bumpy_[i])
          break;
        }
      }
    }
  } else if (bit.find("beat") != bit.npos) {
    if (bit == "beat") {
      SET_FLOAT(effects_[EFFECT_BEAT])
    } else if (bit == "beatoffset") {
      SET_FLOAT(effects_[EFFECT_BEAT_OFFSET])
    } else if (bit == "beatperiod") {
      SET_FLOAT(effects_[EFFECT_BEAT_PERIOD])
    } else if (bit == "beatmult") {
      SET_FLOAT(effects_[EFFECT_BEAT_MULT])
    } else if (bit == "beaty") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y])
    } else if (bit == "beatyoffset") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y_OFFSET])
    } else if (bit == "beatyperiod") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y_PERIOD])
    } else if (bit == "beatymult") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y_MULT])
    } else if (bit == "beatz") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z])
    } else if (bit == "beatzoffset") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z_OFFSET])
    } else if (bit == "beatzperiod") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z_PERIOD])
    } else if (bit == "beatzmult") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z_MULT])
    }
  } else if (bit.find("digital") != bit.npos) {
    if (bit == "digital") {
      SET_FLOAT(effects_[EFFECT_DIGITAL])
    } else if (bit == "digitalsteps") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_STEPS])
    } else if (bit == "digitalperiod") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_PERIOD])
    } else if (bit == "digitaloffset") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_OFFSET])
    } else if (bit == "tandigital") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL])
    } else if (bit == "tandigitalsteps") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_STEPS])
    } else if (bit == "tandigitalperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_PERIOD])
    } else if (bit == "tandigitaloffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_OFFSET])
    } else if (bit == "digitalz") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z])
    } else if (bit == "digitalzsteps") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z_STEPS])
    } else if (bit == "digitalzperiod") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z_PERIOD])
    } else if (bit == "digitalzoffset") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z_OFFSET])
    } else if (bit == "tandigitalz") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z])
    } else if (bit == "tandigitalzsteps") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z_STEPS])
    } else if (bit == "tandigitalzperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z_PERIOD])
    } else if (bit == "tandigitalzoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z_OFFSET])
    }
  } else if (bit.find("zigzag") != bit.npos) {
    if (bit == "zigzag") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG])
    } else if (bit == "zigzagperiod") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_PERIOD])
    } else if (bit == "zigzagoffset") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_OFFSET])
    } else if (bit == "zigzagz") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_Z])
    } else if (bit == "zigzagzperiod") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_Z_PERIOD])
    } else if (bit == "zigzagzoffset") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_Z_OFFSET])
    }
  } else if (bit.find("sawtooth") != bit.npos) {
    if (bit == "sawtooth") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH])
    } else if (bit == "sawtoothperiod") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH_PERIOD])
    } else if (bit == "sawtoothz") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH_Z])
    } else if (bit == "sawtoothzperiod") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH_Z_PERIOD])
    }
  } else if (bit.find("square") != bit.npos) {
    if (bit == "square") {
      SET_FLOAT(effects_[EFFECT_SQUARE])
    } else if (bit == "squareoffset") {
      SET_FLOAT(effects_[EFFECT_SQUARE_OFFSET])
    } else if (bit == "squareperiod") {
      SET_FLOAT(effects_[EFFECT_SQUARE_PERIOD])
    } else if (bit == "squarez") {
      SET_FLOAT(effects_[EFFECT_SQUARE_Z])
    } else if (bit == "squarezoffset") {
      SET_FLOAT(effects_[EFFECT_SQUARE_Z_OFFSET])
    } else if (bit == "squarezperiod") {
      SET_FLOAT(effects_[EFFECT_SQUARE_Z_PERIOD])
    }
  } else if (bit.find("parabola") != bit.npos) {
    if (bit == "parabolax") {
      SET_FLOAT(effects_[EFFECT_PARABOLA_X])
    } else if (bit == "parabolay") {
      SET_FLOAT(effects_[EFFECT_PARABOLA_Y])
    } else if (bit == "parabolaz") {
      SET_FLOAT(effects_[EFFECT_PARABOLA_Z])
    }
  } else if (bit.find("attenuate") != bit.npos) {
    if (bit == "attenuatex") {
      SET_FLOAT(effects_[EFFECT_ATTENUATE_X])
    } else if (bit == "attenuatey") {
      SET_FLOAT(effects_[EFFECT_ATTENUATE_Y])
    } else if (bit == "attenuatez") {
      SET_FLOAT(effects_[EFFECT_ATTENUATE_Z])
    }
  } else if (bit == "xmode") {
    SET_FLOAT(effects_[EFFECT_XMODE])
  } else if (bit == "twirl") {
    SET_FLOAT(effects_[EFFECT_TWIRL])
  } else if (bit == "roll") {
    SET_FLOAT(effects_[EFFECT_ROLL])
  } else if (bit == "hidden") {
    SET_FLOAT(appearances_[APPEARANCE_HIDDEN])
  } else if (bit == "hiddenoffset") {
    SET_FLOAT(appearances_[APPEARANCE_HIDDEN_OFFSET])
  } else if (bit == "sudden") {
    SET_FLOAT(appearances_[APPEARANCE_SUDDEN])
  } else if (bit == "suddenoffset") {
    SET_FLOAT(appearances_[APPEARANCE_SUDDEN_OFFSET])
  } else if (bit.find("stealth") != bit.npos) {
    if (bit == "stealth") {
      SET_FLOAT(appearances_[APPEARANCE_STEALTH])
    } else if (bit == "stealthtype") {
      stealth_type_ = on;
    } else if (bit == "stealthpastreceptors") {
      stealth_past_receptors_ = on;
    } else {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("stealth%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(stealth_[i])
          break;
        }
      }
    }
  } else if (bit == "blink") {
    SET_FLOAT(appearances_[APPEARANCE_BLINK])
  } else if (bit == "randomvanish") {
    SET_FLOAT(appearances_[APPEARANCE_RANDOMVANISH])
  } else if (bit == "turn" && !on) {
    ZERO(turns_); /* "no turn" */
  } else if (bit == "mirror") {
    turns_[TURN_MIRROR] = on;
  } else if (bit == "lrmirror") {
    turns_[TURN_LRMIRROR] = on;
  } else if (bit == "udmirror") {
    turns_[TURN_UDMIRROR] = on;
  } else if (bit == "backwards") {
    turns_[TURN_BACKWARDS] = on;
  } else if (bit == "left") {
    turns_[TURN_LEFT] = on;
  } else if (bit == "right") {
    turns_[TURN_RIGHT] = on;
  } else if (bit == "shuffle") {
    turns_[TURN_SHUFFLE] = on;
  } else if (bit == "softshuffle") {
    turns_[TURN_SOFT_SHUFFLE] = on;
  } else if (bit == "supershuffle") {
    turns_[TURN_SUPER_SHUFFLE] = on;
  } else if (bit == "hypershuffle") {
    turns_[TURN_HYPER_SHUFFLE] = on;
  } else if (bit == "little") {
    transforms_[TRANSFORM_LITTLE] = on;
  } else if (bit == "wide") {
    transforms_[TRANSFORM_WIDE] = on;
  } else if (bit == "big") {
    transforms_[TRANSFORM_BIG] = on;
  } else if (bit == "quick") {
    transforms_[TRANSFORM_QUICK] = on;
  } else if (bit == "bmrize") {
    transforms_[TRANSFORM_BMRIZE] = on;
  } else if (bit == "skippy") {
    transforms_[TRANSFORM_SKIPPY] = on;
  } else if (bit == "mines") {
    transforms_[TRANSFORM_MINES] = on;
  } else if (bit == "attackmines") {
    transforms_[TRANSFORM_ATTACKMINES] = on;
  } else if (bit == "echo") {
    transforms_[TRANSFORM_ECHO] = on;
  } else if (bit == "stomp") {
    transforms_[TRANSFORM_STOMP] = on;
  } else if (bit == "planted") {
    transforms_[TRANSFORM_PLANTED] = on;
  } else if (bit == "floored") {
    transforms_[TRANSFORM_FLOORED] = on;
  } else if (bit == "twister") {
    transforms_[TRANSFORM_TWISTER] = on;
  } else if (bit == "holdrolls") {
    transforms_[TRANSFORM_HOLDROLLS] = on;
  } else if (bit == "nojumps") {
    transforms_[TRANSFORM_NOJUMPS] = on;
  } else if (bit == "nohands") {
    transforms_[TRANSFORM_NOHANDS] = on;
  } else if (bit == "noquads") {
    transforms_[TRANSFORM_NOQUADS] = on;
  } else if (bit.find("reverse") != bit.npos) {
    if (bit == "reverse") {
      SET_FLOAT(scrolls_[SCROLL_REVERSE])
    } else {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("reverse%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(reverse_[i])
          break;
        }
      }
    }
  } else if (bit == "split") {
    SET_FLOAT(scrolls_[SCROLL_SPLIT])
  } else if (bit == "alternate") {
    SET_FLOAT(scrolls_[SCROLL_ALTERNATE])
  } else if (bit == "cross") {
    SET_FLOAT(scrolls_[SCROLL_CROSS])
  } else if (bit == "centered") {
    SET_FLOAT(scrolls_[SCROLL_CENTERED])
  } else if (bit == "noholds") {
    transforms_[TRANSFORM_NOHOLDS] = on;
  } else if (bit == "norolls") {
    transforms_[TRANSFORM_NOROLLS] = on;
  } else if (bit == "nomines") {
    transforms_[TRANSFORM_NOMINES] = on;
  } else if (bit == "nostretch") {
    transforms_[TRANSFORM_NOSTRETCH] = on;
  } else if (bit == "nolifts") {
    transforms_[TRANSFORM_NOLIFTS] = on;
  } else if (bit == "nofakes") {
    transforms_[TRANSFORM_NOFAKES] = on;
  } else if (bit.find("dark") != bit.npos) {
    if (bit == "dark") {
      SET_FLOAT(dark_)
    } else {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("dark%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(darks_[i])
          break;
        }
      }
    }
  } else if (bit == "blind") {
    SET_FLOAT(blind_)
  } else if (bit == "cover") {
    SET_FLOAT(cover_)
  } else if (bit == "randomattacks") {
    SET_FLOAT(rand_attack_)
  } else if (bit == "noattacks") {
    SET_FLOAT(no_attack_)
  } else if (bit == "playerautoplay") {
    SET_FLOAT(player_auto_play_)
  } else if (bit == "passmark") {
    SET_FLOAT(passmark_)
  } else if (bit == "overhead") {
    skew_ = 0;
    perspective_tilt_ = 0;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (bit == "incoming") {
    skew_ = level;
    perspective_tilt_ = -level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (bit == "space") {
    skew_ = level;
    perspective_tilt_ = +level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (bit == "hallway") {
    skew_ = 0;
    perspective_tilt_ = -level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (bit == "distant") {
    skew_ = 0;
    perspective_tilt_ = +level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (NOTESKIN && NOTESKIN->DoesNoteSkinExist(bit)) {
    note_skin_ = bit;
  } else if (bit == "skew") {
    SET_FLOAT(skew_)
  } else if (bit == "tilt") {
    SET_FLOAT(perspective_tilt_)
  } else if (bit == "noteskin" && !on) /* "no noteskin" */
  {
    note_skin_ = CommonMetrics::DEFAULT_NOTESKIN_NAME;
  } else if (bit == "randomspeed") {
    SET_FLOAT(random_speed_)
  } else if (bit == "failarcade" || bit == "failimmediate") {
    fail_type_ = FailType_Immediate;
  } else if (bit == "failendofsong" || bit == "failimmediatecontinue") {
    fail_type_ = FailType_ImmediateContinue;
  } else if (bit == "failatend") {
    fail_type_ = FailType_EndOfSong;
  } else if (bit == "failoff") {
    fail_type_ = FailType_Off;
  } else if (bit == "faildefault") {
    PlayerOptions po;
    GAMESTATE->GetDefaultPlayerOptions(po);
    fail_type_ = po.fail_type_;
  } else if (bit == "muteonerror") {
    mute_on_error_ = on;
  } else if (bit == "random") {
    ChooseRandomModifiers();
  }

  else if (bit.find("move") != bit.npos) {
    if (bit.find("x") != bit.npos) {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("movex%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(moves_x_[i])
          break;
        }
      }
    } else if (bit.find("y") != bit.npos) {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("movey%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(moves_y_[i])
          break;
        }
      }
    } else if (bit.find("z") != bit.npos) {
      for (int i = 0; i < 16; i++) {
        mod = ssprintf("movez%d", i + 1);
        if (bit == mod) {
          SET_FLOAT(moves_z_[i])
          break;
        }
      }
    }
  } else if (bit == "zbuffer") {
    z_buffer_ = on;
  } else if (bit == "cosecant") {
    cosecant_ = on;
  } else if (bit == "visualdelay") {
    visual_delay_ = level;
  } else if (level == 0 && disabled_windows.Compare(bit))  // "No w1" etc.
  {
    // We come into this condition if there is at least a single window present
    // but there may be more. To get all of the windows, we go through in a loop
    // to extract all of them.
    static Regex all_disabled_windows("(w[1-5])(.*)$");
    std::string input = bit;
    while (true) {
      if (!all_disabled_windows.Compare(input, matches)) {
        break;
      }

      TimingWindow tw;
      bool ret = StringConversion::FromString(MakeUpper(matches[0]), tw);
      if (ret && TW_W1 <= tw && tw <= TW_W5) {
        disabled_windows_.set(tw);
      }
      input = matches[1];
    }
  }
  // deprecated mods/left in for compatibility
  else if (bit == "converge") {
    SET_FLOAT(scrolls_[SCROLL_CENTERED])
  }
  // end of the list
  else {
    // Maybe the original string is a noteskin name with a space. -Kyz
    std::string name = one_mod;
    MakeLower(name);
    if (NOTESKIN && NOTESKIN->DoesNoteSkinExist(name)) {
      note_skin_ = name;
    }
    return false;
  }

  return true;
}

void NextFloat(float values[], int size) {
  int index = -1;
  for (int i = 0; i < size; i++) {
    if (values[i] == 1) {
      index = i;
      break;
    }
  }

  for (int i = 0; i < size; i++) {
    values[i] = 0;
  }

  if (index ==
      size - 1)  // if true, then the last float in the list was selected
    ;            // leave all off
  else {
    values[index + 1] = 1;
  }
}

void NextBool(bool values[], int size) {
  int index = -1;
  for (int i = 0; i < size; i++) {
    if (values[i]) {
      index = i;
      break;
    }
  }

  for (int i = 0; i < size; i++) {
    values[i] = false;
  }

  if (index ==
      size - 1)  // if true, then the last float in the list was selected
    ;            // leave all off
  else {
    values[index + 1] = 1;
  }
}

void PlayerOptions::NextAccel() { NextFloat(accels_, NUM_ACCELS); }

void PlayerOptions::NextEffect() { NextFloat(effects_, NUM_EFFECTS); }

void PlayerOptions::NextAppearance() {
  NextFloat(appearances_, NUM_APPEARANCES);
}

void PlayerOptions::NextTurn() { NextBool(turns_, NUM_TURNS); }

void PlayerOptions::NextTransform() { NextBool(transforms_, NUM_TRANSFORMS); }

void PlayerOptions::NextScroll() { NextFloat(scrolls_, NUM_SCROLLS); }

void PlayerOptions::ChooseRandomModifiers() {
  if (RandomFloat(0, 1) < RANDOM_SPEED_CHANCE) {
    scroll_speed_ = 1.5f;
  }
  if (RandomFloat(0, 1) < RANDOM_REVERSE_CHANCE) {
    scrolls_[SCROLL_REVERSE] = 1;
  }
  if (RandomFloat(0, 1) < RANDOM_DARK_CHANCE) {
    dark_ = 1;
  }
  float f;
  f = RandomFloat(0, 1);
  if (f < RANDOM_ACCEL_CHANCE) {
    accels_[RandomInt(NUM_ACCELS)] = 1;
  } else if (f < RANDOM_EFFECT_CHANCE) {
    effects_[RandomInt(NUM_EFFECTS)] = 1;
  }
  f = RandomFloat(0, 1);
  if (f < RANDOM_HIDDEN_CHANCE) {
    appearances_[APPEARANCE_HIDDEN] = 1;
  } else if (f < RANDOM_SUDDEN_CHANCE) {
    appearances_[APPEARANCE_SUDDEN] = 1;
  }
}

void PlayerOptions::ToggleOneTurn(Turn t) {
  bool was_on = turns_[t];
  ZERO(turns_);
  turns_[t] = !was_on;
}

float PlayerOptions::GetReversePercentForColumn(int col) const {
  float f = 0;
  ASSERT(pn_ == PLAYER_1 || pn_ == PLAYER_2);
  ASSERT(GAMESTATE->GetCurrentStyle(pn_) != nullptr);
  int num_cols = GAMESTATE->GetCurrentStyle(pn_)->cols_per_player_;

  f += scrolls_[SCROLL_REVERSE];
  f += reverse_[col];

  if (col >= num_cols / 2) {
    f += scrolls_[SCROLL_SPLIT];
  }

  if ((col % 2) == 1) {
    f += scrolls_[SCROLL_ALTERNATE];
  }

  int first_cross_col = num_cols / 4;
  int last_cross_col = num_cols - 1 - first_cross_col;
  if (col >= first_cross_col && col <= last_cross_col) {
    f += scrolls_[SCROLL_CROSS];
  }

  if (f > 2) {
    f = std::fmod(f, 2);
  }
  if (f > 1) {
    f = SCALE(f, 1.f, 2.f, 1.f, 0.f);
  }
  return f;
}

bool PlayerOptions::operator==(const PlayerOptions& other) const {
#define COMPARE(x)                  \
  {                                 \
    if (x != other.x) return false; \
  }
  COMPARE(life_type_);
  COMPARE(drain_type_);
  COMPARE(hide_light_type_);
  COMPARE(mod_timer_type_);
  COMPARE(mod_timer_mult_);
  COMPARE(mod_timer_offset_);
  COMPARE(draw_size_);
  COMPARE(draw_size_back_);
  COMPARE(battery_lives_);
  COMPARE(time_spacing_);
  COMPARE(scroll_speed_);
  COMPARE(scroll_bpm_);
  COMPARE(max_scroll_bpm_);
  COMPARE(random_speed_);
  COMPARE(fail_type_);
  COMPARE(min_tns_to_hide_notes_);
  COMPARE(mute_on_error_);
  COMPARE(stealth_type_);
  COMPARE(stealth_past_receptors_);
  COMPARE(dizzy_holds_);
  COMPARE(z_buffer_);
  COMPARE(cosecant_);
  COMPARE(dark_);
  COMPARE(blind_);
  COMPARE(cover_);
  COMPARE(rand_attack_);
  COMPARE(no_attack_);
  COMPARE(player_auto_play_);
  COMPARE(perspective_tilt_);
  COMPARE(skew_);
  // The noteskin name needs to be compared case-insensitively because the
  // manager forces lowercase, but some obscure part of PlayerOptions
  // uppercases the first letter.  The previous code that used != probably
  // relied on std::string::operator!= misbehaving. -Kyz
  if (strcasecmp(note_skin_.c_str(), other.note_skin_.c_str()) != 0) {
    return false;
  }
  COMPARE(visual_delay_);
  COMPARE(disabled_windows_);  // != is defined correctly for ordered sets.
  for (int i = 0; i < PlayerOptions::NUM_ACCELS; ++i) {
    COMPARE(accels_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_EFFECTS; ++i) {
    COMPARE(effects_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_APPEARANCES; ++i) {
    COMPARE(appearances_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_SCROLLS; ++i) {
    COMPARE(scrolls_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_TURNS; ++i) {
    COMPARE(turns_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_TRANSFORMS; ++i) {
    COMPARE(transforms_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(moves_x_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(moves_y_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(moves_z_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(confusion_x_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(confusion_y_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(confusion_z_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(darks_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(stealth_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(tiny_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(bumpy_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    COMPARE(reverse_[i]);
  }
#undef COMPARE
  return true;
}

PlayerOptions& PlayerOptions::operator=(const PlayerOptions& other) {
  // Do not copy m_pn from the other, it must be preserved as what PlayerState
  // set it to.
#define CPY(x) x = other.x;
#define CPY_SPEED(x) \
  x = other.x;       \
  speed_##x = other.speed_##x;
  CPY(life_type_);
  CPY(drain_type_);
  CPY(hide_light_type_);
  CPY(mod_timer_type_);
  CPY_SPEED(mod_timer_mult_);
  CPY_SPEED(mod_timer_offset_);
  CPY_SPEED(draw_size_);
  CPY_SPEED(draw_size_back_);
  CPY(battery_lives_);
  CPY_SPEED(time_spacing_);
  CPY_SPEED(scroll_speed_);
  CPY_SPEED(scroll_bpm_);
  CPY_SPEED(max_scroll_bpm_);
  CPY_SPEED(random_speed_);
  CPY(fail_type_);
  CPY(min_tns_to_hide_notes_);
  CPY(mute_on_error_);
  CPY(stealth_type_);
  CPY(stealth_past_receptors_);
  CPY(dizzy_holds_);
  CPY(z_buffer_);
  CPY(cosecant_);
  CPY(visual_delay_);
  CPY(disabled_windows_);
  CPY_SPEED(dark_);
  CPY_SPEED(blind_);
  CPY_SPEED(cover_);
  CPY_SPEED(rand_attack_);
  CPY_SPEED(no_attack_);
  CPY_SPEED(player_auto_play_);
  CPY_SPEED(perspective_tilt_);
  CPY_SPEED(skew_);
  if (!other.note_skin_.empty() &&
      NOTESKIN->DoesNoteSkinExist(other.note_skin_)) {
    CPY(note_skin_);
  }
  for (int i = 0; i < PlayerOptions::NUM_ACCELS; ++i) {
    CPY_SPEED(accels_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_EFFECTS; ++i) {
    CPY_SPEED(effects_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_APPEARANCES; ++i) {
    CPY_SPEED(appearances_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_SCROLLS; ++i) {
    CPY_SPEED(scrolls_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_TURNS; ++i) {
    CPY(turns_[i]);
  }
  for (int i = 0; i < PlayerOptions::NUM_TRANSFORMS; ++i) {
    CPY(transforms_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(moves_x_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(moves_y_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(moves_z_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(confusion_x_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(confusion_y_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(confusion_z_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(darks_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(stealth_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(tiny_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(bumpy_[i]);
  }
  for (int i = 0; i < 16; ++i) {
    CPY_SPEED(reverse_[i]);
  }
#undef CPY
#undef CPY_SPEED
  return *this;
}

bool PlayerOptions::IsEasierForSongAndSteps(
    Song* song, Steps* steps, PlayerNumber pn) const {
  if (time_spacing_ && steps->HasSignificantTimingChanges()) {
    return true;
  }
  const RadarValues& rv = steps->GetRadarValues(pn);
  if (transforms_[TRANSFORM_NOHOLDS] && rv[RadarCategory_Holds] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOROLLS] && rv[RadarCategory_Rolls] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOMINES] && rv[RadarCategory_Mines] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOHANDS] && rv[RadarCategory_Hands] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOQUADS] && rv[RadarCategory_Hands] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOJUMPS] && rv[RadarCategory_Jumps] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOLIFTS] && rv[RadarCategory_Lifts] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOFAKES] && rv[RadarCategory_Fakes] > 0) {
    return true;
  }
  if (transforms_[TRANSFORM_NOSTRETCH]) {
    return true;
  }

  // Inserted holds can be really easy on some songs, and scores will be
  // highly hold-weighted, and very little tap score weighted.
  if (transforms_[TRANSFORM_LITTLE]) {
    return true;
  }
  if (transforms_[TRANSFORM_PLANTED]) {
    return true;
  }
  if (transforms_[TRANSFORM_FLOORED]) {
    return true;
  }
  if (transforms_[TRANSFORM_TWISTER]) {
    return true;
  }

  // This makes songs with sparse notes easier.
  if (transforms_[TRANSFORM_ECHO]) {
    return true;
  }

  // Removing attacks is easier in general.
  if ((no_attack_ && steps->HasAttacks()) || rand_attack_) {
    return true;
  }

  if (cover_ && (!song->GetBackgroundChanges(BACKGROUND_LAYER_1).empty() ||
                 !song->GetForegroundChanges().empty())) {
    return true;
  }

  // M-mods make songs with indefinite BPMs easier because
  // they ensure that the song has a scrollable speed.
  if (max_scroll_bpm_ != 0) {
    // BPM display is obfuscated
    //		if( pSong->m_DisplayBPMType == DISPLAY_BPM_RANDOM )
    //			return true;

    DisplayBpms bpms;
    if (GAMESTATE->IsCourseMode()) {
      Trail* trail = GAMESTATE->cur_course_->GetTrail(
          GAMESTATE->GetCurrentStyle(pn_)->steps_type_);
      trail->GetDisplayBpms(bpms);
    } else {
      GAMESTATE->cur_song_->GetDisplayBpms(bpms);
    }
    song->GetDisplayBpms(bpms);

    // maximum BPM is obfuscated, so M-mods will set a playable speed.
    if (bpms.GetMax() <= 0) {
      return true;
    }
  }
  if (player_auto_play_) {
    return true;
  }
  return false;
}

bool PlayerOptions::IsEasierForCourseAndTrail(
    Course* course, Trail* trail) const {
  ASSERT(course != nullptr);
  ASSERT(trail != nullptr);

  return std::any_of(
      trail->entries_.begin(), trail->entries_.end(), [&](const TrailEntry& e) {
        return e.song_ && IsEasierForSongAndSteps(e.song_, e.steps_, PLAYER_1);
      });
}

void PlayerOptions::GetLocalizedMods(std::vector<std::string>& add_to) const {
  std::vector<std::string> mods;
  GetMods(mods);
  for (const std::string& one_mod : mods) {
    ASSERT(!one_mod.empty());

    std::vector<std::string> tokens;
    split(one_mod, " ", tokens);

    if (tokens.empty()) {
      continue;
    }

    // Strip the approach speed token, if any
    if (tokens[0][0] == '*') {
      tokens.erase(tokens.begin());
    }

    // capitalize NoteSkin names
    tokens.back() = Capitalize(tokens.back());

    /* Theme the mod name (the last string).  Allow this to not exist, since
     * characters might use modifiers that don't exist in the theme. */
    tokens.back() = CommonMetrics::LocalizeOptionItem(tokens.back(), true);

    std::string localized_mod = join(" ", tokens);
    add_to.push_back(localized_mod);
  }
}

bool PlayerOptions::ContainsTransformOrTurn() const {
  for (int i = 0; i < NUM_TRANSFORMS; i++) {
    if (transforms_[i]) {
      return true;
    }
  }
  for (int i = 0; i < NUM_TURNS; i++) {
    if (turns_[i]) {
      return true;
    }
  }
  return false;
}

std::string PlayerOptions::GetSavedPrefsString() const {
  PlayerOptions po_prefs;
#define SAVE(x) po_prefs.x = this->x;
  SAVE(time_spacing_);
  SAVE(scroll_speed_);
  SAVE(scroll_bpm_);
  SAVE(max_scroll_bpm_);
  SAVE(scrolls_[SCROLL_REVERSE]);
  SAVE(perspective_tilt_);
  SAVE(transforms_[TRANSFORM_NOHOLDS]);
  SAVE(transforms_[TRANSFORM_NOROLLS]);
  SAVE(transforms_[TRANSFORM_NOMINES]);
  SAVE(transforms_[TRANSFORM_NOJUMPS]);
  SAVE(transforms_[TRANSFORM_NOHANDS]);
  SAVE(transforms_[TRANSFORM_NOQUADS]);
  SAVE(transforms_[TRANSFORM_NOSTRETCH]);
  SAVE(transforms_[TRANSFORM_NOLIFTS]);
  SAVE(transforms_[TRANSFORM_NOFAKES]);
  SAVE(mute_on_error_);
  SAVE(note_skin_);
  SAVE(visual_delay_);
  SAVE(disabled_windows_);
#undef SAVE
  return po_prefs.GetString();
}

void PlayerOptions::ResetPrefs(ResetPrefsType type) {
  PlayerOptions defaults;
#define CPY(x) this->x = defaults.x;
  switch (type) {
    DEFAULT_FAIL(type);
    case saved_prefs:
      CPY(time_spacing_);
      CPY(scroll_speed_);
      CPY(scroll_bpm_);
      CPY(max_scroll_bpm_);
      break;
    case saved_prefs_invalid_for_course:
      break;
  }
  CPY(life_type_);
  CPY(drain_type_);
  CPY(hide_light_type_);
  CPY(battery_lives_);
  CPY(mod_timer_type_);
  CPY(mod_timer_mult_);
  CPY(mod_timer_offset_);
  CPY(draw_size_);
  CPY(draw_size_back_);
  CPY(stealth_type_);
  CPY(stealth_past_receptors_);
  CPY(dizzy_holds_);
  CPY(z_buffer_);
  CPY(cosecant_);
  CPY(min_tns_to_hide_notes_);

  CPY(perspective_tilt_);
  CPY(transforms_[TRANSFORM_NOHOLDS]);
  CPY(transforms_[TRANSFORM_NOROLLS]);
  CPY(transforms_[TRANSFORM_NOMINES]);
  CPY(transforms_[TRANSFORM_NOJUMPS]);
  CPY(transforms_[TRANSFORM_NOHANDS]);
  CPY(transforms_[TRANSFORM_NOQUADS]);
  CPY(transforms_[TRANSFORM_NOSTRETCH]);
  CPY(transforms_[TRANSFORM_NOLIFTS]);
  CPY(transforms_[TRANSFORM_NOFAKES]);
  // Don't clear this.
  // CPY( m_sNoteSkin );
  CPY(visual_delay_);
  CPY(disabled_windows_);
#undef CPY
}

// lua start
#include "LuaBinding.h"
#include "OptionsBinding.h"

/** @brief Allow Lua to have access to PlayerOptions. */
class LunaPlayerOptions : public Luna<PlayerOptions> {
 public:
  static int IsEasierForSongAndSteps(T* p, lua_State* L) {
    Song* song = Luna<Song>::check(L, 1);
    Steps* steps = Luna<Steps>::check(L, 2);
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 3);
    lua_pushboolean(L, p->IsEasierForSongAndSteps(song, steps, pn));
    return 1;
  }
  static int IsEasierForCourseAndTrail(T* p, lua_State* L) {
    // course, trail
    Course* course = Luna<Course>::check(L, 1);
    Trail* trail = Luna<Trail>::check(L, 2);
    lua_pushboolean(L, p->IsEasierForCourseAndTrail(course, trail));
    return 1;
  }

  // Direct control functions, for themes that can handle it.

  ENUM_INTERFACE(LifeSetting, life_type_, LifeType);
  ENUM_INTERFACE(DrainSetting, drain_type_, DrainType);
  ENUM_INTERFACE(HideLightSetting, hide_light_type_, HideLightType);
  ENUM_INTERFACE(ModTimerSetting, mod_timer_type_, ModTimerType)
  INT_INTERFACE(BatteryLives, battery_lives_);
  FLOAT_INTERFACE(ModTimerMult, mod_timer_mult_, true);
  FLOAT_INTERFACE(ModTimerOffset, mod_timer_offset_, true);
  FLOAT_INTERFACE(DrawSize, draw_size_, true);
  FLOAT_INTERFACE(DrawSizeBack, draw_size_back_, true);
  FLOAT_INTERFACE(TimeSpacing, time_spacing_, true);
  FLOAT_INTERFACE(MaxScrollBPM, max_scroll_bpm_, true);
  FLOAT_INTERFACE(ScrollSpeed, scroll_speed_, true);
  FLOAT_INTERFACE(ScrollBPM, scroll_bpm_, true);
  FLOAT_INTERFACE(Boost, accels_[PlayerOptions::ACCEL_BOOST], true);
  FLOAT_INTERFACE(Brake, accels_[PlayerOptions::ACCEL_BRAKE], true);
  FLOAT_INTERFACE(Wave, accels_[PlayerOptions::ACCEL_WAVE], true);
  FLOAT_INTERFACE(WavePeriod, accels_[PlayerOptions::ACCEL_WAVE_PERIOD], true);
  FLOAT_INTERFACE(Expand, accels_[PlayerOptions::ACCEL_EXPAND], true);
  FLOAT_INTERFACE(
      ExpandPeriod, accels_[PlayerOptions::ACCEL_EXPAND_PERIOD], true);
  FLOAT_INTERFACE(TanExpand, accels_[PlayerOptions::ACCEL_TAN_EXPAND], true);
  FLOAT_INTERFACE(
      TanExpandPeriod, accels_[PlayerOptions::ACCEL_TAN_EXPAND_PERIOD], true);
  FLOAT_INTERFACE(Boomerang, accels_[PlayerOptions::ACCEL_BOOMERANG], true);
  FLOAT_INTERFACE(Drunk, effects_[PlayerOptions::EFFECT_DRUNK], true);
  FLOAT_INTERFACE(
      DrunkSpeed, effects_[PlayerOptions::EFFECT_DRUNK_SPEED], true);
  FLOAT_INTERFACE(
      DrunkOffset, effects_[PlayerOptions::EFFECT_DRUNK_OFFSET], true);
  FLOAT_INTERFACE(
      DrunkPeriod, effects_[PlayerOptions::EFFECT_DRUNK_PERIOD], true);
  FLOAT_INTERFACE(TanDrunk, effects_[PlayerOptions::EFFECT_TAN_DRUNK], true);
  FLOAT_INTERFACE(
      TanDrunkSpeed, effects_[PlayerOptions::EFFECT_TAN_DRUNK_SPEED], true);
  FLOAT_INTERFACE(
      TanDrunkOffset, effects_[PlayerOptions::EFFECT_TAN_DRUNK_OFFSET], true);
  FLOAT_INTERFACE(
      TanDrunkPeriod, effects_[PlayerOptions::EFFECT_TAN_DRUNK_PERIOD], true);
  FLOAT_INTERFACE(DrunkZ, effects_[PlayerOptions::EFFECT_DRUNK_Z], true);
  FLOAT_INTERFACE(
      DrunkZSpeed, effects_[PlayerOptions::EFFECT_DRUNK_Z_SPEED], true);
  FLOAT_INTERFACE(
      DrunkZOffset, effects_[PlayerOptions::EFFECT_DRUNK_Z_OFFSET], true);
  FLOAT_INTERFACE(
      DrunkZPeriod, effects_[PlayerOptions::EFFECT_DRUNK_Z_PERIOD], true);
  FLOAT_INTERFACE(TanDrunkZ, effects_[PlayerOptions::EFFECT_TAN_DRUNK_Z], true);
  FLOAT_INTERFACE(
      TanDrunkZSpeed, effects_[PlayerOptions::EFFECT_TAN_DRUNK_Z_SPEED], true);
  FLOAT_INTERFACE(
      TanDrunkZOffset, effects_[PlayerOptions::EFFECT_TAN_DRUNK_Z_OFFSET],
      true);
  FLOAT_INTERFACE(
      TanDrunkZPeriod, effects_[PlayerOptions::EFFECT_TAN_DRUNK_Z_PERIOD],
      true);
  FLOAT_INTERFACE(Dizzy, effects_[PlayerOptions::EFFECT_DIZZY], true);
  FLOAT_INTERFACE(
      AttenuateX, effects_[PlayerOptions::EFFECT_ATTENUATE_X], true);
  FLOAT_INTERFACE(
      AttenuateY, effects_[PlayerOptions::EFFECT_ATTENUATE_Y], true);
  FLOAT_INTERFACE(
      AttenuateZ, effects_[PlayerOptions::EFFECT_ATTENUATE_Z], true);
  FLOAT_INTERFACE(
      ShrinkLinear, effects_[PlayerOptions::EFFECT_SHRINK_TO_LINEAR], true);
  FLOAT_INTERFACE(
      ShrinkMult, effects_[PlayerOptions::EFFECT_SHRINK_TO_MULT], true);
  FLOAT_INTERFACE(
      PulseInner, effects_[PlayerOptions::EFFECT_PULSE_INNER], true);
  FLOAT_INTERFACE(
      PulseOuter, effects_[PlayerOptions::EFFECT_PULSE_OUTER], true);
  FLOAT_INTERFACE(
      PulsePeriod, effects_[PlayerOptions::EFFECT_PULSE_PERIOD], true);
  FLOAT_INTERFACE(
      PulseOffset, effects_[PlayerOptions::EFFECT_PULSE_OFFSET], true);
  FLOAT_INTERFACE(Confusion, effects_[PlayerOptions::EFFECT_CONFUSION], true);
  FLOAT_INTERFACE(
      ConfusionOffset, effects_[PlayerOptions::EFFECT_CONFUSION_OFFSET], true);
  FLOAT_INTERFACE(
      ConfusionX, effects_[PlayerOptions::EFFECT_CONFUSION_X], true);
  FLOAT_INTERFACE(
      ConfusionXOffset, effects_[PlayerOptions::EFFECT_CONFUSION_X_OFFSET],
      true);
  FLOAT_INTERFACE(
      ConfusionY, effects_[PlayerOptions::EFFECT_CONFUSION_Y], true);
  FLOAT_INTERFACE(
      ConfusionYOffset, effects_[PlayerOptions::EFFECT_CONFUSION_Y_OFFSET],
      true);
  FLOAT_INTERFACE(Bounce, effects_[PlayerOptions::EFFECT_BOUNCE], true);
  FLOAT_INTERFACE(
      BouncePeriod, effects_[PlayerOptions::EFFECT_BOUNCE_PERIOD], true);
  FLOAT_INTERFACE(
      BounceOffset, effects_[PlayerOptions::EFFECT_BOUNCE_OFFSET], true);
  FLOAT_INTERFACE(BounceZ, effects_[PlayerOptions::EFFECT_BOUNCE_Z], true);
  FLOAT_INTERFACE(
      BounceZPeriod, effects_[PlayerOptions::EFFECT_BOUNCE_Z_PERIOD], true);
  FLOAT_INTERFACE(
      BounceZOffset, effects_[PlayerOptions::EFFECT_BOUNCE_Z_OFFSET], true);
  FLOAT_INTERFACE(Mini, effects_[PlayerOptions::EFFECT_MINI], true);
  FLOAT_INTERFACE(Tiny, effects_[PlayerOptions::EFFECT_TINY], true);
  FLOAT_INTERFACE(Flip, effects_[PlayerOptions::EFFECT_FLIP], true);
  FLOAT_INTERFACE(Invert, effects_[PlayerOptions::EFFECT_INVERT], true);
  FLOAT_INTERFACE(Tornado, effects_[PlayerOptions::EFFECT_TORNADO], true);
  FLOAT_INTERFACE(
      TornadoPeriod, effects_[PlayerOptions::EFFECT_TORNADO_PERIOD], true);
  FLOAT_INTERFACE(
      TornadoOffset, effects_[PlayerOptions::EFFECT_TORNADO_OFFSET], true);
  FLOAT_INTERFACE(
      TanTornado, effects_[PlayerOptions::EFFECT_TAN_TORNADO], true);
  FLOAT_INTERFACE(
      TanTornadoPeriod, effects_[PlayerOptions::EFFECT_TAN_TORNADO_PERIOD],
      true);
  FLOAT_INTERFACE(
      TanTornadoOffset, effects_[PlayerOptions::EFFECT_TAN_TORNADO_OFFSET],
      true);
  FLOAT_INTERFACE(TornadoZ, effects_[PlayerOptions::EFFECT_TORNADO_Z], true);
  FLOAT_INTERFACE(
      TornadoZPeriod, effects_[PlayerOptions::EFFECT_TORNADO_Z_PERIOD], true);
  FLOAT_INTERFACE(
      TornadoZOffset, effects_[PlayerOptions::EFFECT_TORNADO_Z_OFFSET], true);
  FLOAT_INTERFACE(
      TanTornadoZ, effects_[PlayerOptions::EFFECT_TAN_TORNADO_Z], true);
  FLOAT_INTERFACE(
      TanTornadoZPeriod, effects_[PlayerOptions::EFFECT_TAN_TORNADO_Z_PERIOD],
      true);
  FLOAT_INTERFACE(
      TanTornadoZOffset, effects_[PlayerOptions::EFFECT_TAN_TORNADO_Z_OFFSET],
      true);
  FLOAT_INTERFACE(Tipsy, effects_[PlayerOptions::EFFECT_TIPSY], true);
  FLOAT_INTERFACE(
      TipsySpeed, effects_[PlayerOptions::EFFECT_TIPSY_SPEED], true);
  FLOAT_INTERFACE(
      TipsyOffset, effects_[PlayerOptions::EFFECT_TIPSY_OFFSET], true);
  FLOAT_INTERFACE(TanTipsy, effects_[PlayerOptions::EFFECT_TAN_TIPSY], true);
  FLOAT_INTERFACE(
      TanTipsySpeed, effects_[PlayerOptions::EFFECT_TAN_TIPSY_SPEED], true);
  FLOAT_INTERFACE(
      TanTipsyOffset, effects_[PlayerOptions::EFFECT_TAN_TIPSY_OFFSET], true);
  FLOAT_INTERFACE(Bumpy, effects_[PlayerOptions::EFFECT_BUMPY], true);
  FLOAT_INTERFACE(
      BumpyOffset, effects_[PlayerOptions::EFFECT_BUMPY_OFFSET], true);
  FLOAT_INTERFACE(
      BumpyPeriod, effects_[PlayerOptions::EFFECT_BUMPY_PERIOD], true);
  FLOAT_INTERFACE(TanBumpy, effects_[PlayerOptions::EFFECT_TAN_BUMPY], true);
  FLOAT_INTERFACE(
      TanBumpyOffset, effects_[PlayerOptions::EFFECT_TAN_BUMPY_OFFSET], true);
  FLOAT_INTERFACE(
      TanBumpyPeriod, effects_[PlayerOptions::EFFECT_TAN_BUMPY_PERIOD], true);
  FLOAT_INTERFACE(BumpyX, effects_[PlayerOptions::EFFECT_BUMPY_X], true);
  FLOAT_INTERFACE(
      BumpyXOffset, effects_[PlayerOptions::EFFECT_BUMPY_X_OFFSET], true);
  FLOAT_INTERFACE(
      BumpyXPeriod, effects_[PlayerOptions::EFFECT_BUMPY_X_PERIOD], true);
  FLOAT_INTERFACE(TanBumpyX, effects_[PlayerOptions::EFFECT_TAN_BUMPY_X], true);
  FLOAT_INTERFACE(
      TanBumpyXOffset, effects_[PlayerOptions::EFFECT_TAN_BUMPY_X_OFFSET],
      true);
  FLOAT_INTERFACE(
      TanBumpyXPeriod, effects_[PlayerOptions::EFFECT_TAN_BUMPY_X_PERIOD],
      true);
  FLOAT_INTERFACE(Beat, effects_[PlayerOptions::EFFECT_BEAT], true);
  FLOAT_INTERFACE(
      BeatOffset, effects_[PlayerOptions::EFFECT_BEAT_OFFSET], true);
  FLOAT_INTERFACE(
      BeatPeriod, effects_[PlayerOptions::EFFECT_BEAT_PERIOD], true);
  FLOAT_INTERFACE(BeatMult, effects_[PlayerOptions::EFFECT_BEAT_MULT], true);
  FLOAT_INTERFACE(BeatY, effects_[PlayerOptions::EFFECT_BEAT_Y], true);
  FLOAT_INTERFACE(
      BeatYOffset, effects_[PlayerOptions::EFFECT_BEAT_Y_OFFSET], true);
  FLOAT_INTERFACE(
      BeatYPeriod, effects_[PlayerOptions::EFFECT_BEAT_Y_PERIOD], true);
  FLOAT_INTERFACE(BeatYMult, effects_[PlayerOptions::EFFECT_BEAT_Y_MULT], true);
  FLOAT_INTERFACE(BeatZ, effects_[PlayerOptions::EFFECT_BEAT_Z], true);
  FLOAT_INTERFACE(
      BeatZOffset, effects_[PlayerOptions::EFFECT_BEAT_Z_OFFSET], true);
  FLOAT_INTERFACE(
      BeatZPeriod, effects_[PlayerOptions::EFFECT_BEAT_Z_PERIOD], true);
  FLOAT_INTERFACE(BeatZMult, effects_[PlayerOptions::EFFECT_BEAT_Z_MULT], true);
  FLOAT_INTERFACE(Zigzag, effects_[PlayerOptions::EFFECT_ZIGZAG], true);
  FLOAT_INTERFACE(
      ZigzagPeriod, effects_[PlayerOptions::EFFECT_ZIGZAG_PERIOD], true);
  FLOAT_INTERFACE(
      ZigzagOffset, effects_[PlayerOptions::EFFECT_ZIGZAG_OFFSET], true);
  FLOAT_INTERFACE(ZigzagZ, effects_[PlayerOptions::EFFECT_ZIGZAG_Z], true);
  FLOAT_INTERFACE(
      ZigzagZPeriod, effects_[PlayerOptions::EFFECT_ZIGZAG_Z_PERIOD], true);
  FLOAT_INTERFACE(
      ZigzagZOffset, effects_[PlayerOptions::EFFECT_ZIGZAG_Z_OFFSET], true);
  FLOAT_INTERFACE(Sawtooth, effects_[PlayerOptions::EFFECT_SAWTOOTH], true);
  FLOAT_INTERFACE(
      SawtoothPeriod, effects_[PlayerOptions::EFFECT_SAWTOOTH_PERIOD], true);
  FLOAT_INTERFACE(SawtoothZ, effects_[PlayerOptions::EFFECT_SAWTOOTH_Z], true);
  FLOAT_INTERFACE(
      SawtoothZPeriod, effects_[PlayerOptions::EFFECT_SAWTOOTH_Z_PERIOD], true);
  FLOAT_INTERFACE(Square, effects_[PlayerOptions::EFFECT_SQUARE], true);
  FLOAT_INTERFACE(
      SquareOffset, effects_[PlayerOptions::EFFECT_SQUARE_OFFSET], true);
  FLOAT_INTERFACE(
      SquarePeriod, effects_[PlayerOptions::EFFECT_SQUARE_PERIOD], true);
  FLOAT_INTERFACE(SquareZ, effects_[PlayerOptions::EFFECT_SQUARE_Z], true);
  FLOAT_INTERFACE(
      SquareZOffset, effects_[PlayerOptions::EFFECT_SQUARE_Z_OFFSET], true);
  FLOAT_INTERFACE(
      SquareZPeriod, effects_[PlayerOptions::EFFECT_SQUARE_Z_PERIOD], true);
  FLOAT_INTERFACE(Digital, effects_[PlayerOptions::EFFECT_DIGITAL], true);
  FLOAT_INTERFACE(
      DigitalSteps, effects_[PlayerOptions::EFFECT_DIGITAL_STEPS], true);
  FLOAT_INTERFACE(
      DigitalPeriod, effects_[PlayerOptions::EFFECT_DIGITAL_PERIOD], true);
  FLOAT_INTERFACE(
      DigitalOffset, effects_[PlayerOptions::EFFECT_DIGITAL_OFFSET], true);
  FLOAT_INTERFACE(
      TanDigital, effects_[PlayerOptions::EFFECT_TAN_DIGITAL], true);
  FLOAT_INTERFACE(
      TanDigitalSteps, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_STEPS], true);
  FLOAT_INTERFACE(
      TanDigitalPeriod, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_PERIOD],
      true);
  FLOAT_INTERFACE(
      TanDigitalOffset, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_OFFSET],
      true);
  FLOAT_INTERFACE(DigitalZ, effects_[PlayerOptions::EFFECT_DIGITAL_Z], true);
  FLOAT_INTERFACE(
      DigitalZSteps, effects_[PlayerOptions::EFFECT_DIGITAL_Z_STEPS], true);
  FLOAT_INTERFACE(
      DigitalZPeriod, effects_[PlayerOptions::EFFECT_DIGITAL_Z_PERIOD], true);
  FLOAT_INTERFACE(
      DigitalZOffset, effects_[PlayerOptions::EFFECT_DIGITAL_Z_OFFSET], true);
  FLOAT_INTERFACE(
      TanDigitalZ, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_Z], true);
  FLOAT_INTERFACE(
      TanDigitalZSteps, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_Z_STEPS],
      true);
  FLOAT_INTERFACE(
      TanDigitalZPeriod, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_Z_PERIOD],
      true);
  FLOAT_INTERFACE(
      TanDigitalZOffset, effects_[PlayerOptions::EFFECT_TAN_DIGITAL_Z_OFFSET],
      true);
  FLOAT_INTERFACE(ParabolaX, effects_[PlayerOptions::EFFECT_PARABOLA_X], true);
  FLOAT_INTERFACE(ParabolaY, effects_[PlayerOptions::EFFECT_PARABOLA_Y], true);
  FLOAT_INTERFACE(ParabolaZ, effects_[PlayerOptions::EFFECT_PARABOLA_Z], true);
  FLOAT_INTERFACE(Xmode, effects_[PlayerOptions::EFFECT_XMODE], true);
  FLOAT_INTERFACE(Twirl, effects_[PlayerOptions::EFFECT_TWIRL], true);
  FLOAT_INTERFACE(Roll, effects_[PlayerOptions::EFFECT_ROLL], true);
  FLOAT_INTERFACE(Hidden, appearances_[PlayerOptions::APPEARANCE_HIDDEN], true);
  FLOAT_INTERFACE(
      HiddenOffset, appearances_[PlayerOptions::APPEARANCE_HIDDEN_OFFSET],
      true);
  FLOAT_INTERFACE(Sudden, appearances_[PlayerOptions::APPEARANCE_SUDDEN], true);
  FLOAT_INTERFACE(
      SuddenOffset, appearances_[PlayerOptions::APPEARANCE_SUDDEN_OFFSET],
      true);
  FLOAT_INTERFACE(
      Stealth, appearances_[PlayerOptions::APPEARANCE_STEALTH], true);
  FLOAT_INTERFACE(Blink, appearances_[PlayerOptions::APPEARANCE_BLINK], true);
  FLOAT_INTERFACE(
      RandomVanish, appearances_[PlayerOptions::APPEARANCE_RANDOMVANISH], true);
  FLOAT_INTERFACE(Reverse, scrolls_[PlayerOptions::SCROLL_REVERSE], true);
  FLOAT_INTERFACE(Split, scrolls_[PlayerOptions::SCROLL_SPLIT], true);
  FLOAT_INTERFACE(Alternate, scrolls_[PlayerOptions::SCROLL_ALTERNATE], true);
  FLOAT_INTERFACE(Cross, scrolls_[PlayerOptions::SCROLL_CROSS], true);
  FLOAT_INTERFACE(Centered, scrolls_[PlayerOptions::SCROLL_CENTERED], true);
  FLOAT_INTERFACE(Dark, dark_, true);
  FLOAT_INTERFACE(Blind, blind_, true);
  FLOAT_INTERFACE(Cover, cover_, true);
  FLOAT_INTERFACE(RandAttack, rand_attack_, true);
  FLOAT_INTERFACE(NoAttack, no_attack_, true);
  FLOAT_INTERFACE(PlayerAutoPlay, player_auto_play_, true);
  FLOAT_INTERFACE(Skew, skew_, true);
  FLOAT_INTERFACE(Tilt, perspective_tilt_, true);
  FLOAT_INTERFACE(
      Passmark, passmark_,
      true);  // Passmark is not sanity checked to the [0, 1] range because
              // LifeMeterBar::IsFailing is the only thing that uses it, and
              // it's used in a <= test.  Any theme passing a value outside the
              // [0, 1] range probably expects the result they get. -Kyz
  FLOAT_INTERFACE(RandomSpeed, random_speed_, true);

  MULTICOL_FLOAT_INTERFACE(MoveX, moves_x_, true);
  MULTICOL_FLOAT_INTERFACE(MoveY, moves_y_, true);
  MULTICOL_FLOAT_INTERFACE(MoveZ, moves_z_, true);
  MULTICOL_FLOAT_INTERFACE(ConfusionXOffset, confusion_x_, true);
  MULTICOL_FLOAT_INTERFACE(ConfusionYOffset, confusion_y_, true);
  MULTICOL_FLOAT_INTERFACE(ConfusionOffset, confusion_z_, true);
  MULTICOL_FLOAT_INTERFACE(Dark, darks_, true);
  MULTICOL_FLOAT_INTERFACE(Stealth, stealth_, true);
  MULTICOL_FLOAT_INTERFACE(Tiny, tiny_, true);
  MULTICOL_FLOAT_INTERFACE(Bumpy, bumpy_, true);
  MULTICOL_FLOAT_INTERFACE(Reverse, reverse_, true);

  BOOL_INTERFACE(StealthType, stealth_type_);
  BOOL_INTERFACE(StealthPastReceptors, stealth_past_receptors_);
  BOOL_INTERFACE(DizzyHolds, dizzy_holds_);
  BOOL_INTERFACE(ZBuffer, z_buffer_);
  BOOL_INTERFACE(Cosecant, cosecant_);
  BOOL_INTERFACE(TurnNone, turns_[PlayerOptions::TURN_NONE]);
  BOOL_INTERFACE(Mirror, turns_[PlayerOptions::TURN_MIRROR]);
  BOOL_INTERFACE(LRMirror, turns_[PlayerOptions::TURN_LRMIRROR]);
  BOOL_INTERFACE(UDMirror, turns_[PlayerOptions::TURN_UDMIRROR]);
  BOOL_INTERFACE(Backwards, turns_[PlayerOptions::TURN_BACKWARDS]);
  BOOL_INTERFACE(Left, turns_[PlayerOptions::TURN_LEFT]);
  BOOL_INTERFACE(Right, turns_[PlayerOptions::TURN_RIGHT]);
  BOOL_INTERFACE(Shuffle, turns_[PlayerOptions::TURN_SHUFFLE]);
  BOOL_INTERFACE(SoftShuffle, turns_[PlayerOptions::TURN_SOFT_SHUFFLE]);
  BOOL_INTERFACE(SuperShuffle, turns_[PlayerOptions::TURN_SUPER_SHUFFLE]);
  BOOL_INTERFACE(HyperShuffle, turns_[PlayerOptions::TURN_HYPER_SHUFFLE]);
  BOOL_INTERFACE(NoHolds, transforms_[PlayerOptions::TRANSFORM_NOHOLDS]);
  BOOL_INTERFACE(NoRolls, transforms_[PlayerOptions::TRANSFORM_NOROLLS]);
  BOOL_INTERFACE(NoMines, transforms_[PlayerOptions::TRANSFORM_NOMINES]);
  BOOL_INTERFACE(Little, transforms_[PlayerOptions::TRANSFORM_LITTLE]);
  BOOL_INTERFACE(Wide, transforms_[PlayerOptions::TRANSFORM_WIDE]);
  BOOL_INTERFACE(Big, transforms_[PlayerOptions::TRANSFORM_BIG]);
  BOOL_INTERFACE(Quick, transforms_[PlayerOptions::TRANSFORM_QUICK]);
  BOOL_INTERFACE(BMRize, transforms_[PlayerOptions::TRANSFORM_BMRIZE]);
  BOOL_INTERFACE(Skippy, transforms_[PlayerOptions::TRANSFORM_SKIPPY]);
  BOOL_INTERFACE(Mines, transforms_[PlayerOptions::TRANSFORM_MINES]);
  BOOL_INTERFACE(
      AttackMines, transforms_[PlayerOptions::TRANSFORM_ATTACKMINES]);
  BOOL_INTERFACE(Echo, transforms_[PlayerOptions::TRANSFORM_ECHO]);
  BOOL_INTERFACE(Stomp, transforms_[PlayerOptions::TRANSFORM_STOMP]);
  BOOL_INTERFACE(Planted, transforms_[PlayerOptions::TRANSFORM_PLANTED]);
  BOOL_INTERFACE(Floored, transforms_[PlayerOptions::TRANSFORM_FLOORED]);
  BOOL_INTERFACE(Twister, transforms_[PlayerOptions::TRANSFORM_TWISTER]);
  BOOL_INTERFACE(HoldRolls, transforms_[PlayerOptions::TRANSFORM_HOLDROLLS]);
  BOOL_INTERFACE(NoJumps, transforms_[PlayerOptions::TRANSFORM_NOJUMPS]);
  BOOL_INTERFACE(NoHands, transforms_[PlayerOptions::TRANSFORM_NOHANDS]);
  BOOL_INTERFACE(NoLifts, transforms_[PlayerOptions::TRANSFORM_NOLIFTS]);
  BOOL_INTERFACE(NoFakes, transforms_[PlayerOptions::TRANSFORM_NOFAKES]);
  BOOL_INTERFACE(NoQuads, transforms_[PlayerOptions::TRANSFORM_NOQUADS]);
  BOOL_INTERFACE(NoStretch, transforms_[PlayerOptions::TRANSFORM_NOSTRETCH]);
  BOOL_INTERFACE(MuteOnError, mute_on_error_);
  ENUM_INTERFACE(FailSetting, fail_type_, FailType);
  ENUM_INTERFACE(MinTNSToHideNotes, min_tns_to_hide_notes_, TapNoteScore);

  FLOAT_NO_SPEED_INTERFACE(VisualDelay, visual_delay_, true);

  static int DisableTimingWindow(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (original_top >= 1 && !lua_isnil(L, 1)) {
      // Insert the specified TimingWindow into the disabled windows set.
      p->disabled_windows_.set(Enum::Check<TimingWindow>(L, 1));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 1;
  }

  static int ResetDisabledTimingWindows(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    p->disabled_windows_.reset();
    OPTIONAL_RETURN_SELF(original_top);
    return 1;
  }

  static int GetDisabledTimingWindows(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    lua_newtable(L);
    int j = 0;
    for (int i = TW_W1; i <= TW_W5; ++i) {
      if (p->disabled_windows_[i]) {
        Enum::Push(L, static_cast<TimingWindow>(i));
        lua_rawseti(L, -2, j + 1);
        ++j;
      }
    }
    return 1;
  }

  // NoteSkins
  static int NoteSkin(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (p->note_skin_.empty()) {
      lua_pushstring(
          L, CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue().c_str());
    } else {
      lua_pushstring(L, p->note_skin_.c_str());
    }
    if (original_top >= 1 && lua_isstring(L, 1)) {
      std::string skin = SArg(1);
      if (NOTESKIN->DoesNoteSkinExist(skin)) {
        p->note_skin_ = skin;
        lua_pushboolean(L, true);
      } else {
        lua_pushnil(L);
      }
    } else {
      lua_pushnil(L);
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static void SetSpeedModApproaches(T* p, float speed) {
    p->speed_scroll_bpm_ = speed;
    p->speed_scroll_speed_ = speed;
    p->speed_max_scroll_bpm_ = speed;
    p->speed_time_spacing_ = speed;
  }

  // Speed Mods
  // Sanity checked functions for speed mods, for themes that want to use the
  // engine's enforcement of sane separation between speed mod types.
  static int CMod(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (p->time_spacing_) {
      lua_pushnumber(L, p->scroll_bpm_);
      lua_pushnumber(L, p->speed_scroll_bpm_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (original_top >= 1 && lua_isnumber(L, 1)) {
      float speed = FArg(1);
      if (!std::isfinite(speed) || speed <= 0.0f) {
        luaL_error(L, "CMod speed must be finite and greater than 0.");
      }
      p->scroll_bpm_ = speed;
      p->time_spacing_ = 1;
      p->scroll_speed_ = 1;
      p->max_scroll_bpm_ = 0;
    }
    if (original_top >= 2 && lua_isnumber(L, 2)) {
      SetSpeedModApproaches(p, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static int XMod(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (!p->time_spacing_) {
      lua_pushnumber(L, p->scroll_speed_);
      lua_pushnumber(L, p->speed_scroll_speed_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (lua_isnumber(L, 1) && original_top >= 1) {
      p->scroll_speed_ = FArg(1);
      p->time_spacing_ = 0;
      p->scroll_bpm_ = kCmodDefault;
      p->max_scroll_bpm_ = 0;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetSpeedModApproaches(p, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static int MMod(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (!p->time_spacing_ && p->max_scroll_bpm_) {
      lua_pushnumber(L, p->max_scroll_bpm_);
      lua_pushnumber(L, p->speed_max_scroll_bpm_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (lua_isnumber(L, 1) && original_top >= 1) {
      float speed = FArg(1);
      if (!std::isfinite(speed) || speed <= 0.0f) {
        luaL_error(L, "MMod speed must be finite and greater than 0.");
      }
      p->scroll_bpm_ = kCmodDefault;
      p->time_spacing_ = 0;
      p->scroll_speed_ = 1;
      p->max_scroll_bpm_ = speed;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetSpeedModApproaches(p, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static void SetPerspectiveApproach(T* p, lua_State* L, float speed) {
    p->speed_perspective_tilt_ = speed;
    p->speed_skew_ = speed;
  }

  static int Overhead(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    lua_pushboolean(L, (p->perspective_tilt_ == 0.0f && p->skew_ == 0.0f));
    if (lua_toboolean(L, 1)) {
      p->perspective_tilt_ = 0;
      p->skew_ = 0;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetPerspectiveApproach(p, L, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 1;
  }

  static int Incoming(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if ((p->skew_ > 0.0f && p->perspective_tilt_ < 0.0f) ||
        (p->skew_ < 0.0f && p->perspective_tilt_ > 0.0f)) {
      lua_pushnumber(L, p->skew_);
      lua_pushnumber(L, p->speed_skew_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (lua_isnumber(L, 1) && original_top >= 1) {
      float value = FArg(1);
      p->perspective_tilt_ = -value;
      p->skew_ = value;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetPerspectiveApproach(p, L, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static int Space(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if ((p->skew_ > 0.0f && p->perspective_tilt_ > 0.0f) ||
        (p->skew_ < 0.0f && p->perspective_tilt_ < 0.0f)) {
      lua_pushnumber(L, p->skew_);
      lua_pushnumber(L, p->speed_skew_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (lua_isnumber(L, 1) && original_top >= 1) {
      float value = FArg(1);
      p->perspective_tilt_ = value;
      p->skew_ = value;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetPerspectiveApproach(p, L, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static int Hallway(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (p->skew_ == 0.0f && p->perspective_tilt_ < 0.0f) {
      lua_pushnumber(L, -p->perspective_tilt_);
      lua_pushnumber(L, p->speed_perspective_tilt_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (lua_isnumber(L, 1) && original_top >= 1) {
      p->perspective_tilt_ = -FArg(1);
      p->skew_ = 0;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetPerspectiveApproach(p, L, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  static int Distant(T* p, lua_State* L) {
    int original_top = lua_gettop(L);
    if (p->skew_ == 0.0f && p->perspective_tilt_ > 0.0f) {
      lua_pushnumber(L, p->perspective_tilt_);
      lua_pushnumber(L, p->speed_perspective_tilt_);
    } else {
      lua_pushnil(L);
      lua_pushnil(L);
    }
    if (lua_isnumber(L, 1) && original_top >= 1) {
      p->perspective_tilt_ = FArg(1);
      p->skew_ = 0;
    }
    if (lua_isnumber(L, 2) && original_top >= 2) {
      SetPerspectiveApproach(p, L, FArgGTEZero(L, 2));
    }
    OPTIONAL_RETURN_SELF(original_top);
    return 2;
  }

  DEFINE_METHOD(UsingReverse, scrolls_[PlayerOptions::SCROLL_REVERSE] == 1.0f);

  static int GetReversePercentForColumn(T* p, lua_State* L) {
    const int col_num = IArg(1);
    const int num_columns =
        GAMESTATE->GetCurrentStyle(p->pn_)->cols_per_player_;

    // We don't want to go outside the bounds.
    if (col_num < 0 || col_num > num_columns) {
      lua_pushnil(L);
    } else {
      lua_pushnumber(L, p->GetReversePercentForColumn(col_num));
    }

    return 1;
  }

  static int GetStepAttacks(T* p, lua_State* L) {
    lua_pushnumber(
        L, (p->no_attack_ > 0 || p->rand_attack_ > 0 ? false : true));
    return 1;
  }

  static int FromString(T* p, lua_State* L) {
    p->FromString(SArg(1));
    COMMON_RETURN_SELF;
  }

  LunaPlayerOptions() {
    ADD_METHOD(IsEasierForSongAndSteps);
    ADD_METHOD(IsEasierForCourseAndTrail);

    ADD_METHOD(LifeSetting);
    ADD_METHOD(DrainSetting);
    ADD_METHOD(HideLightSetting);
    ADD_METHOD(ModTimerSetting);
    ADD_METHOD(ModTimerMult);
    ADD_METHOD(ModTimerOffset);
    ADD_METHOD(DrawSize);
    ADD_METHOD(DrawSizeBack);
    ADD_METHOD(BatteryLives);
    ADD_METHOD(TimeSpacing);
    ADD_METHOD(MaxScrollBPM);
    ADD_METHOD(ScrollSpeed);
    ADD_METHOD(ScrollBPM);
    ADD_METHOD(Boost);
    ADD_METHOD(Brake);
    ADD_METHOD(Wave);
    ADD_METHOD(WavePeriod);
    ADD_METHOD(Expand);
    ADD_METHOD(ExpandPeriod);
    ADD_METHOD(TanExpand);
    ADD_METHOD(TanExpandPeriod);
    ADD_METHOD(Boomerang);
    ADD_METHOD(Drunk);
    ADD_METHOD(DrunkSpeed);
    ADD_METHOD(DrunkOffset);
    ADD_METHOD(DrunkPeriod);
    ADD_METHOD(TanDrunk);
    ADD_METHOD(TanDrunkSpeed);
    ADD_METHOD(TanDrunkOffset);
    ADD_METHOD(TanDrunkPeriod);
    ADD_METHOD(DrunkZ);
    ADD_METHOD(DrunkZSpeed);
    ADD_METHOD(DrunkZOffset);
    ADD_METHOD(DrunkZPeriod);
    ADD_METHOD(TanDrunkZ);
    ADD_METHOD(TanDrunkZSpeed);
    ADD_METHOD(TanDrunkZOffset);
    ADD_METHOD(TanDrunkZPeriod);
    ADD_METHOD(Dizzy);
    ADD_METHOD(ShrinkLinear);
    ADD_METHOD(ShrinkMult);
    ADD_METHOD(PulseInner);
    ADD_METHOD(PulseOuter);
    ADD_METHOD(PulseOffset);
    ADD_METHOD(PulsePeriod);
    ADD_METHOD(AttenuateX);
    ADD_METHOD(AttenuateY);
    ADD_METHOD(AttenuateZ);
    ADD_METHOD(Confusion);
    ADD_METHOD(ConfusionOffset);
    ADD_METHOD(ConfusionX);
    ADD_METHOD(ConfusionXOffset);
    ADD_METHOD(ConfusionY);
    ADD_METHOD(ConfusionYOffset);
    ADD_METHOD(Bounce);
    ADD_METHOD(BouncePeriod);
    ADD_METHOD(BounceOffset);
    ADD_METHOD(BounceZ);
    ADD_METHOD(BounceZPeriod);
    ADD_METHOD(BounceZOffset);
    ADD_METHOD(Mini);
    ADD_METHOD(Tiny);
    ADD_METHOD(Flip);
    ADD_METHOD(Invert);
    ADD_METHOD(Tornado);
    ADD_METHOD(TornadoPeriod);
    ADD_METHOD(TornadoOffset);
    ADD_METHOD(TanTornado);
    ADD_METHOD(TanTornadoPeriod);
    ADD_METHOD(TanTornadoOffset);
    ADD_METHOD(TornadoZ);
    ADD_METHOD(TornadoZPeriod);
    ADD_METHOD(TornadoZOffset);
    ADD_METHOD(TanTornadoZ);
    ADD_METHOD(TanTornadoZPeriod);
    ADD_METHOD(TanTornadoZOffset);
    ADD_METHOD(Tipsy);
    ADD_METHOD(TipsySpeed);
    ADD_METHOD(TipsyOffset);
    ADD_METHOD(TanTipsy);
    ADD_METHOD(TanTipsySpeed);
    ADD_METHOD(TanTipsyOffset);
    ADD_METHOD(Bumpy);
    ADD_METHOD(BumpyOffset);
    ADD_METHOD(BumpyPeriod);
    ADD_METHOD(TanBumpy);
    ADD_METHOD(TanBumpyOffset);
    ADD_METHOD(TanBumpyPeriod);
    ADD_METHOD(BumpyX);
    ADD_METHOD(BumpyXOffset);
    ADD_METHOD(BumpyXPeriod);
    ADD_METHOD(TanBumpyX);
    ADD_METHOD(TanBumpyXOffset);
    ADD_METHOD(TanBumpyXPeriod);
    ADD_METHOD(Beat);
    ADD_METHOD(BeatOffset);
    ADD_METHOD(BeatPeriod);
    ADD_METHOD(BeatMult);
    ADD_METHOD(BeatY);
    ADD_METHOD(BeatYOffset);
    ADD_METHOD(BeatYPeriod);
    ADD_METHOD(BeatYMult);
    ADD_METHOD(BeatZ);
    ADD_METHOD(BeatZOffset);
    ADD_METHOD(BeatZPeriod);
    ADD_METHOD(BeatZMult);
    ADD_METHOD(Zigzag);
    ADD_METHOD(ZigzagPeriod);
    ADD_METHOD(ZigzagOffset);
    ADD_METHOD(ZigzagZ);
    ADD_METHOD(ZigzagZPeriod);
    ADD_METHOD(ZigzagZOffset);
    ADD_METHOD(Sawtooth);
    ADD_METHOD(SawtoothPeriod);
    ADD_METHOD(SawtoothZ);
    ADD_METHOD(SawtoothZPeriod);
    ADD_METHOD(Square);
    ADD_METHOD(SquareOffset);
    ADD_METHOD(SquarePeriod);
    ADD_METHOD(SquareZ);
    ADD_METHOD(SquareZOffset);
    ADD_METHOD(SquareZPeriod);
    ADD_METHOD(Digital);
    ADD_METHOD(DigitalSteps);
    ADD_METHOD(DigitalPeriod);
    ADD_METHOD(DigitalOffset);
    ADD_METHOD(TanDigital);
    ADD_METHOD(TanDigitalSteps);
    ADD_METHOD(TanDigitalPeriod);
    ADD_METHOD(TanDigitalOffset);
    ADD_METHOD(DigitalZ);
    ADD_METHOD(DigitalZSteps);
    ADD_METHOD(DigitalZPeriod);
    ADD_METHOD(DigitalZOffset);
    ADD_METHOD(TanDigitalZ);
    ADD_METHOD(TanDigitalZSteps);
    ADD_METHOD(TanDigitalZPeriod);
    ADD_METHOD(TanDigitalZOffset);
    ADD_METHOD(ParabolaX);
    ADD_METHOD(ParabolaY);
    ADD_METHOD(ParabolaZ);
    ADD_METHOD(Xmode);
    ADD_METHOD(Twirl);
    ADD_METHOD(Roll);
    ADD_METHOD(Hidden);
    ADD_METHOD(HiddenOffset);
    ADD_METHOD(Sudden);
    ADD_METHOD(SuddenOffset);
    ADD_METHOD(Stealth);
    ADD_METHOD(Blink);
    ADD_METHOD(RandomVanish);
    ADD_METHOD(Reverse);
    ADD_METHOD(Split);
    ADD_METHOD(Alternate);
    ADD_METHOD(Cross);
    ADD_METHOD(Centered);
    ADD_METHOD(Dark);
    ADD_METHOD(Blind);
    ADD_METHOD(Cover);
    ADD_METHOD(StealthType);
    ADD_METHOD(StealthPastReceptors);
    ADD_METHOD(DizzyHolds);
    ADD_METHOD(ZBuffer);
    ADD_METHOD(Cosecant);
    ADD_METHOD(RandAttack);
    ADD_METHOD(NoAttack);
    ADD_METHOD(PlayerAutoPlay);
    ADD_METHOD(Tilt);
    ADD_METHOD(Skew);
    ADD_METHOD(Passmark);
    ADD_METHOD(RandomSpeed);
    ADD_METHOD(TurnNone);
    ADD_METHOD(Mirror);
    ADD_METHOD(LRMirror);
    ADD_METHOD(UDMirror);
    ADD_METHOD(Backwards);
    ADD_METHOD(Left);
    ADD_METHOD(Right);
    ADD_METHOD(Shuffle);
    ADD_METHOD(SoftShuffle);
    ADD_METHOD(SuperShuffle);
    ADD_METHOD(HyperShuffle);
    ADD_METHOD(NoHolds);
    ADD_METHOD(NoRolls);
    ADD_METHOD(NoMines);
    ADD_METHOD(Little);
    ADD_METHOD(Wide);
    ADD_METHOD(Big);
    ADD_METHOD(Quick);
    ADD_METHOD(BMRize);
    ADD_METHOD(Skippy);
    ADD_METHOD(Mines);
    ADD_METHOD(AttackMines);
    ADD_METHOD(Echo);
    ADD_METHOD(Stomp);
    ADD_METHOD(Planted);
    ADD_METHOD(Floored);
    ADD_METHOD(Twister);
    ADD_METHOD(HoldRolls);
    ADD_METHOD(NoJumps);
    ADD_METHOD(NoHands);
    ADD_METHOD(NoLifts);
    ADD_METHOD(NoFakes);
    ADD_METHOD(NoQuads);
    ADD_METHOD(NoStretch);
    ADD_METHOD(MuteOnError);

    ADD_MULTICOL_METHOD(MoveX);
    ADD_MULTICOL_METHOD(MoveY);
    ADD_MULTICOL_METHOD(MoveZ);
    ADD_MULTICOL_METHOD(ConfusionOffset);
    ADD_MULTICOL_METHOD(ConfusionXOffset);
    ADD_MULTICOL_METHOD(ConfusionYOffset);
    ADD_MULTICOL_METHOD(Dark);
    ADD_MULTICOL_METHOD(Stealth);
    ADD_MULTICOL_METHOD(Tiny);
    ADD_MULTICOL_METHOD(Bumpy);
    ADD_MULTICOL_METHOD(Reverse);

    ADD_METHOD(NoteSkin);
    ADD_METHOD(FailSetting);
    ADD_METHOD(MinTNSToHideNotes);
    ADD_METHOD(VisualDelay);

    ADD_METHOD(DisableTimingWindow);
    ADD_METHOD(ResetDisabledTimingWindows);
    ADD_METHOD(GetDisabledTimingWindows);

    // Speed
    ADD_METHOD(CMod);
    ADD_METHOD(XMod);
    ADD_METHOD(MMod);

    ADD_METHOD(Overhead);
    ADD_METHOD(Incoming);
    ADD_METHOD(Space);
    ADD_METHOD(Hallway);
    ADD_METHOD(Distant);

    ADD_METHOD(UsingReverse);
    ADD_METHOD(GetReversePercentForColumn);
    ADD_METHOD(GetStepAttacks);

    ADD_METHOD(FromString);
  }
};

LUA_REGISTER_CLASS(PlayerOptions)
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
