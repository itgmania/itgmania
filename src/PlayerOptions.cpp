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

void NextFloat(float fValues[], int size);
void NextBool(bool bValues[], int size);

ThemeMetric<float> RANDOM_SPEED_CHANCE("PlayerOptions", "RandomSpeedChance");
ThemeMetric<float> RANDOM_REVERSE_CHANCE(
    "PlayerOptions", "RandomReverseChance");
ThemeMetric<float> RANDOM_DARK_CHANCE("PlayerOptions", "RandomDarkChance");
ThemeMetric<float> RANDOM_ACCEL_CHANCE("PlayerOptions", "RandomAccelChance");
ThemeMetric<float> RANDOM_EFFECT_CHANCE("PlayerOptions", "RandomEffectChance");
ThemeMetric<float> RANDOM_HIDDEN_CHANCE("PlayerOptions", "RandomHiddenChance");
ThemeMetric<float> RANDOM_SUDDEN_CHANCE("PlayerOptions", "RandomSuddenChance");

static const float CMOD_DEFAULT = 200.0f;
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
  scroll_bpm_ = CMOD_DEFAULT;
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
    std::vector<std::string>& AddTo, float level, std::string name) {
  if (level == 0) {
    return;
  }

  const std::string LevelStr =
      (level == 1) ? std::string("")
                   : ssprintf("%ld%% ", std::lrint(level * 100));

  AddTo.push_back(LevelStr + name);
}

std::string PlayerOptions::GetString(bool bForceNoteSkin) const {
  std::vector<std::string> v;
  GetMods(v, bForceNoteSkin);
  return join(", ", v);
}

void PlayerOptions::GetMods(
    std::vector<std::string>& AddTo, bool bForceNoteSkin) const {
  // std::string sReturn;

  switch (life_type_) {
    case LifeType_Bar:
      switch (drain_type_) {
        case DrainType_NoRecover:
          AddTo.push_back("NoRecover");
          break;
        case DrainType_SuddenDeath:
          AddTo.push_back("SuddenDeath");
          break;
        case DrainType_Normal:
        default:
          break;
      }
      break;
    case LifeType_Battery:
      AddTo.push_back(ssprintf("%dLives", battery_lives_));
      break;
    case LifeType_Time:
      AddTo.push_back("LifeTime");
      break;
    default:
      FAIL_M(ssprintf("Invalid LifeType: %i", life_type_));
  }

  switch (hide_light_type_) {
    case HideLightType_NoHideLights:
      AddTo.push_back("NoHideLights");
      break;
    case HideLightType_HideAllLights:
      AddTo.push_back("HideAllLights");
      break;
    case HideLightType_HideMarqueeLights:
      AddTo.push_back("HideMarqueeLights");
      break;
    case HideLightType_HideBassLights:
      AddTo.push_back("HideBassLights");
      break;
    default:
      FAIL_M(ssprintf("Invalid HideLightType: %i", hide_light_type_));
  }

  if (!time_spacing_) {
    if (max_scroll_bpm_) {
      std::string s = ssprintf("m%.0f", max_scroll_bpm_);
      AddTo.push_back(s);
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
      AddTo.push_back(s + "x");
    }
  } else {
    std::string s = ssprintf("C%.0f", scroll_bpm_);
    AddTo.push_back(s);
  }

  switch (mod_timer_type_) {
    case ModTimerType_Game:
      AddTo.push_back("ModTimerGame");
      break;
    case ModTimerType_Beat:
      AddTo.push_back("ModTimerBeat");
      break;
    case ModTimerType_Song:
      AddTo.push_back("ModTimerSong");
      break;
    case ModTimerType_Default:
      break;
    default:
      FAIL_M(ssprintf("Invalid ModTimerType: %i", mod_timer_type_));
  }

  AddPart(AddTo, accels_[ACCEL_BOOST], "Boost");
  AddPart(AddTo, accels_[ACCEL_BRAKE], "Brake");
  AddPart(AddTo, accels_[ACCEL_WAVE], "Wave");
  AddPart(AddTo, accels_[ACCEL_WAVE_PERIOD], "WavePeriod");
  AddPart(AddTo, accels_[ACCEL_EXPAND], "Expand");
  AddPart(AddTo, accels_[ACCEL_EXPAND_PERIOD], "ExpandPeriod");
  AddPart(AddTo, accels_[ACCEL_TAN_EXPAND], "TanExpand");
  AddPart(AddTo, accels_[ACCEL_TAN_EXPAND_PERIOD], "TanExpandPeriod");
  AddPart(AddTo, accels_[ACCEL_BOOMERANG], "Boomerang");

  AddPart(AddTo, effects_[EFFECT_DRUNK], "Drunk");
  AddPart(AddTo, effects_[EFFECT_DRUNK_SPEED], "DrunkSpeed");
  AddPart(AddTo, effects_[EFFECT_DRUNK_OFFSET], "DrunkOffset");
  AddPart(AddTo, effects_[EFFECT_DRUNK_PERIOD], "DrunkPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK], "TanDrunk");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_SPEED], "TanDrunkSpeed");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_OFFSET], "TanDrunkOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_PERIOD], "TanDrunkPeriod");
  AddPart(AddTo, effects_[EFFECT_DRUNK_Z], "DrunkZ");
  AddPart(AddTo, effects_[EFFECT_DRUNK_Z_SPEED], "DrunkZSpeed");
  AddPart(AddTo, effects_[EFFECT_DRUNK_Z_OFFSET], "DrunkZOffset");
  AddPart(AddTo, effects_[EFFECT_DRUNK_Z_PERIOD], "DrunkZPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_Z], "TanDrunkZ");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_Z_SPEED], "TanDrunkZSpeed");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_Z_OFFSET], "TanDrunkZOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_DRUNK_Z_PERIOD], "TanDrunkZPeriod");
  AddPart(AddTo, effects_[EFFECT_SHRINK_TO_LINEAR], "ShrinkLinear");
  AddPart(AddTo, effects_[EFFECT_SHRINK_TO_MULT], "ShrinkMult");
  AddPart(AddTo, effects_[EFFECT_PULSE_INNER], "PulseInner");
  AddPart(AddTo, effects_[EFFECT_PULSE_OUTER], "PulseOuter");
  AddPart(AddTo, effects_[EFFECT_PULSE_PERIOD], "PulsePeriod");
  AddPart(AddTo, effects_[EFFECT_PULSE_OFFSET], "PulseOffset");
  AddPart(AddTo, effects_[EFFECT_ATTENUATE_X], "AttenuateX");
  AddPart(AddTo, effects_[EFFECT_ATTENUATE_Y], "AttenuateY");
  AddPart(AddTo, effects_[EFFECT_ATTENUATE_Z], "AttenuateZ");
  AddPart(AddTo, effects_[EFFECT_DIZZY], "Dizzy");
  AddPart(AddTo, effects_[EFFECT_CONFUSION], "Confusion");
  AddPart(AddTo, effects_[EFFECT_CONFUSION_OFFSET], "ConfusionOffset");
  AddPart(AddTo, effects_[EFFECT_CONFUSION_X], "ConfusionX");
  AddPart(AddTo, effects_[EFFECT_CONFUSION_X_OFFSET], "ConfusionXOffset");
  AddPart(AddTo, effects_[EFFECT_CONFUSION_Y], "ConfusionY");
  AddPart(AddTo, effects_[EFFECT_CONFUSION_Y_OFFSET], "ConfusionYOffset");
  AddPart(AddTo, effects_[EFFECT_BOUNCE], "Bounce");
  AddPart(AddTo, effects_[EFFECT_BOUNCE_PERIOD], "BouncePeriod");
  AddPart(AddTo, effects_[EFFECT_BOUNCE_OFFSET], "BounceOffset");
  AddPart(AddTo, effects_[EFFECT_BOUNCE_Z], "BounceZ");
  AddPart(AddTo, effects_[EFFECT_BOUNCE_Z_PERIOD], "BounceZPeriod");
  AddPart(AddTo, effects_[EFFECT_BOUNCE_Z_OFFSET], "BounceZOffset");
  AddPart(AddTo, effects_[EFFECT_MINI], "Mini");
  AddPart(AddTo, effects_[EFFECT_TINY], "Tiny");
  AddPart(AddTo, effects_[EFFECT_FLIP], "Flip");
  AddPart(AddTo, effects_[EFFECT_INVERT], "Invert");
  AddPart(AddTo, effects_[EFFECT_TORNADO], "Tornado");
  AddPart(AddTo, effects_[EFFECT_TORNADO_PERIOD], "TornadoPeriod");
  AddPart(AddTo, effects_[EFFECT_TORNADO_OFFSET], "TornadoOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_TORNADO], "TanTornado");
  AddPart(AddTo, effects_[EFFECT_TAN_TORNADO_PERIOD], "TanTornadoPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_TORNADO_OFFSET], "TanTornadoOffset");
  AddPart(AddTo, effects_[EFFECT_TORNADO_Z], "TornadoZ");
  AddPart(AddTo, effects_[EFFECT_TORNADO_Z_PERIOD], "TornadoZPeriod");
  AddPart(AddTo, effects_[EFFECT_TORNADO_Z_OFFSET], "TornadoZOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_TORNADO_Z], "TanTornadoZ");
  AddPart(AddTo, effects_[EFFECT_TAN_TORNADO_Z_PERIOD], "TanTornadoZPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_TORNADO_Z_OFFSET], "TanTornadoZOffset");
  AddPart(AddTo, effects_[EFFECT_TIPSY], "Tipsy");
  AddPart(AddTo, effects_[EFFECT_TIPSY_SPEED], "TipsySpeed");
  AddPart(AddTo, effects_[EFFECT_TIPSY_OFFSET], "TipsyOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_TIPSY], "TanTipsy");
  AddPart(AddTo, effects_[EFFECT_TAN_TIPSY_SPEED], "TanTipsySpeed");
  AddPart(AddTo, effects_[EFFECT_TAN_TIPSY_OFFSET], "TanTipsyOffset");
  AddPart(AddTo, effects_[EFFECT_BUMPY], "Bumpy");
  AddPart(AddTo, effects_[EFFECT_BUMPY_OFFSET], "BumpyOffset");
  AddPart(AddTo, effects_[EFFECT_BUMPY_PERIOD], "BumpyPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_BUMPY], "TanBumpy");
  AddPart(AddTo, effects_[EFFECT_TAN_BUMPY_OFFSET], "TanBumpyOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_BUMPY_PERIOD], "TanBumpyPeriod");
  AddPart(AddTo, effects_[EFFECT_BUMPY_X], "BumpyX");
  AddPart(AddTo, effects_[EFFECT_BUMPY_X_OFFSET], "BumpyXOffset");
  AddPart(AddTo, effects_[EFFECT_BUMPY_X_PERIOD], "BumpyXPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_BUMPY_X], "TanBumpyX");
  AddPart(AddTo, effects_[EFFECT_TAN_BUMPY_X_OFFSET], "TanBumpyXOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_BUMPY_X_PERIOD], "TanBumpyXPeriod");
  AddPart(AddTo, effects_[EFFECT_BEAT], "Beat");
  AddPart(AddTo, effects_[EFFECT_BEAT_OFFSET], "BeatOffset");
  AddPart(AddTo, effects_[EFFECT_BEAT_PERIOD], "BeatPeriod");
  AddPart(AddTo, effects_[EFFECT_BEAT_MULT], "BeatMult");
  AddPart(AddTo, effects_[EFFECT_BEAT_Y], "BeatY");
  AddPart(AddTo, effects_[EFFECT_BEAT_Y_OFFSET], "BeatYOffset");
  AddPart(AddTo, effects_[EFFECT_BEAT_Y_PERIOD], "BeatYPeriod");
  AddPart(AddTo, effects_[EFFECT_BEAT_Y_MULT], "BeatYMult");
  AddPart(AddTo, effects_[EFFECT_BEAT_Z], "BeatZ");
  AddPart(AddTo, effects_[EFFECT_BEAT_Z_OFFSET], "BeatZOffset");
  AddPart(AddTo, effects_[EFFECT_BEAT_Z_PERIOD], "BeatZPeriod");
  AddPart(AddTo, effects_[EFFECT_BEAT_Z_MULT], "BeatZMult");
  AddPart(AddTo, effects_[EFFECT_ZIGZAG], "Zigzag");
  AddPart(AddTo, effects_[EFFECT_ZIGZAG_PERIOD], "ZigzagPeriod");
  AddPart(AddTo, effects_[EFFECT_ZIGZAG_OFFSET], "ZigzagOffset");
  AddPart(AddTo, effects_[EFFECT_ZIGZAG_Z], "ZigzagZ");
  AddPart(AddTo, effects_[EFFECT_ZIGZAG_Z_PERIOD], "ZigzagZPeriod");
  AddPart(AddTo, effects_[EFFECT_ZIGZAG_Z_OFFSET], "ZigzagZOffset");
  AddPart(AddTo, effects_[EFFECT_SAWTOOTH], "Sawtooth");
  AddPart(AddTo, effects_[EFFECT_SAWTOOTH_PERIOD], "SawtoothPeriod");
  AddPart(AddTo, effects_[EFFECT_SAWTOOTH_Z], "SawtoothZ");
  AddPart(AddTo, effects_[EFFECT_SAWTOOTH_Z_PERIOD], "SawtoothZPeriod");
  AddPart(AddTo, effects_[EFFECT_SQUARE], "Square");
  AddPart(AddTo, effects_[EFFECT_SQUARE_OFFSET], "SquareOffset");
  AddPart(AddTo, effects_[EFFECT_SQUARE_PERIOD], "SquarePeriod");
  AddPart(AddTo, effects_[EFFECT_SQUARE_Z], "SquareZ");
  AddPart(AddTo, effects_[EFFECT_SQUARE_Z_OFFSET], "SquareZOffset");
  AddPart(AddTo, effects_[EFFECT_SQUARE_Z_PERIOD], "SquareZPeriod");
  AddPart(AddTo, effects_[EFFECT_DIGITAL], "Digital");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_STEPS], "DigitalSteps");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_PERIOD], "DigitalPeriod");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_OFFSET], "DigitalOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL], "TanDigital");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_STEPS], "TanDigitalSteps");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_PERIOD], "TanDigitalPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_OFFSET], "TanDigitalOffset");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_Z], "DigitalZ");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_Z_STEPS], "DigitalZSteps");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_Z_PERIOD], "DigitalZPeriod");
  AddPart(AddTo, effects_[EFFECT_DIGITAL_Z_OFFSET], "DigitalZOffset");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_Z], "TanDigitalZ");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_Z_STEPS], "TanDigitalZSteps");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_Z_PERIOD], "TanDigitalZPeriod");
  AddPart(AddTo, effects_[EFFECT_TAN_DIGITAL_Z_OFFSET], "TanDigitalZOffset");
  AddPart(AddTo, effects_[EFFECT_PARABOLA_X], "ParabolaX");
  AddPart(AddTo, effects_[EFFECT_PARABOLA_Y], "ParabolaY");
  AddPart(AddTo, effects_[EFFECT_PARABOLA_Z], "ParabolaZ");
  AddPart(AddTo, effects_[EFFECT_XMODE], "XMode");
  AddPart(AddTo, effects_[EFFECT_TWIRL], "Twirl");
  AddPart(AddTo, effects_[EFFECT_ROLL], "Roll");
  AddPart(AddTo, stealth_type_, "StealthType");
  AddPart(AddTo, stealth_past_receptors_, "StealthPastReceptors");
  AddPart(AddTo, dizzy_holds_, "DizzyHolds");
  AddPart(AddTo, z_buffer_, "ZBuffer");
  AddPart(AddTo, cosecant_, "Cosecant");

  for (int i = 0; i < 16; i++) {
    std::string s = ssprintf("MoveX%d", i + 1);

    AddPart(AddTo, moves_x_[i], s);
    s = ssprintf("MoveY%d", i + 1);
    AddPart(AddTo, moves_y_[i], s);
    s = ssprintf("MoveZ%d", i + 1);
    AddPart(AddTo, moves_z_[i], s);
    s = ssprintf("ConfusionOffset%d", i + 1);
    AddPart(AddTo, confusion_x_[i], s);
    s = ssprintf("ConfusionYOffset%d", i + 1);
    AddPart(AddTo, confusion_y_[i], s);
    s = ssprintf("ConfusionZOffset%d", i + 1);
    AddPart(AddTo, confusion_z_[i], s);
    s = ssprintf("Dark%d", i + 1);
    AddPart(AddTo, darks_[i], s);
    s = ssprintf("Stealth%d", i + 1);
    AddPart(AddTo, stealth_[i], s);
    s = ssprintf("Tiny%d", i + 1);
    AddPart(AddTo, tiny_[i], s);
    s = ssprintf("Bumpy%d", i + 1);
    AddPart(AddTo, bumpy_[i], s);
    s = ssprintf("Reverse%d", i + 1);
    AddPart(AddTo, reverse_[i], s);
  }

  AddPart(AddTo, appearances_[APPEARANCE_HIDDEN], "Hidden");
  AddPart(AddTo, appearances_[APPEARANCE_HIDDEN_OFFSET], "HiddenOffset");
  AddPart(AddTo, appearances_[APPEARANCE_SUDDEN], "Sudden");
  AddPart(AddTo, appearances_[APPEARANCE_SUDDEN_OFFSET], "SuddenOffset");
  AddPart(AddTo, appearances_[APPEARANCE_STEALTH], "Stealth");
  AddPart(AddTo, appearances_[APPEARANCE_BLINK], "Blink");
  AddPart(AddTo, appearances_[APPEARANCE_RANDOMVANISH], "RandomVanish");

  AddPart(AddTo, scrolls_[SCROLL_REVERSE], "Reverse");
  AddPart(AddTo, scrolls_[SCROLL_SPLIT], "Split");
  AddPart(AddTo, scrolls_[SCROLL_ALTERNATE], "Alternate");
  AddPart(AddTo, scrolls_[SCROLL_CROSS], "Cross");
  AddPart(AddTo, scrolls_[SCROLL_CENTERED], "Centered");

  AddPart(AddTo, mod_timer_mult_, "ModTimerMult");
  AddPart(AddTo, mod_timer_offset_, "ModTimerOffset");
  AddPart(AddTo, draw_size_, "DrawSize");
  AddPart(AddTo, draw_size_back_, "DrawSizeBack");

  AddPart(AddTo, dark_, "Dark");

  AddPart(AddTo, blind_, "Blind");
  AddPart(AddTo, cover_, "Cover");

  AddPart(AddTo, rand_attack_, "RandomAttacks");
  AddPart(AddTo, no_attack_, "NoAttacks");
  AddPart(AddTo, player_auto_play_, "PlayerAutoPlay");

  AddPart(AddTo, passmark_, "Passmark");

  AddPart(AddTo, random_speed_, "RandomSpeed");

  if (turns_[TURN_MIRROR]) {
    AddTo.push_back("Mirror");
  }
  if (turns_[TURN_LRMIRROR]) {
    AddTo.push_back("LRMirror");
  }
  if (turns_[TURN_UDMIRROR]) {
    AddTo.push_back("UDMirror");
  }
  if (turns_[TURN_BACKWARDS]) {
    AddTo.push_back("Backwards");
  }
  if (turns_[TURN_LEFT]) {
    AddTo.push_back("Left");
  }
  if (turns_[TURN_RIGHT]) {
    AddTo.push_back("Right");
  }
  if (turns_[TURN_SHUFFLE]) {
    AddTo.push_back("Shuffle");
  }
  if (turns_[TURN_SOFT_SHUFFLE]) {
    AddTo.push_back("SoftShuffle");
  }
  if (turns_[TURN_SUPER_SHUFFLE]) {
    AddTo.push_back("SuperShuffle");
  }
  if (turns_[TURN_HYPER_SHUFFLE]) {
    AddTo.push_back("HyperShuffle");
  }

  if (transforms_[TRANSFORM_NOHOLDS]) {
    AddTo.push_back("NoHolds");
  }
  if (transforms_[TRANSFORM_NOROLLS]) {
    AddTo.push_back("NoRolls");
  }
  if (transforms_[TRANSFORM_NOMINES]) {
    AddTo.push_back("NoMines");
  }
  if (transforms_[TRANSFORM_LITTLE]) {
    AddTo.push_back("Little");
  }
  if (transforms_[TRANSFORM_WIDE]) {
    AddTo.push_back("Wide");
  }
  if (transforms_[TRANSFORM_BIG]) {
    AddTo.push_back("Big");
  }
  if (transforms_[TRANSFORM_QUICK]) {
    AddTo.push_back("Quick");
  }
  if (transforms_[TRANSFORM_BMRIZE]) {
    AddTo.push_back("BMRize");
  }
  if (transforms_[TRANSFORM_SKIPPY]) {
    AddTo.push_back("Skippy");
  }
  if (transforms_[TRANSFORM_MINES]) {
    AddTo.push_back("Mines");
  }
  if (transforms_[TRANSFORM_ATTACKMINES]) {
    AddTo.push_back("AttackMines");
  }
  if (transforms_[TRANSFORM_ECHO]) {
    AddTo.push_back("Echo");
  }
  if (transforms_[TRANSFORM_STOMP]) {
    AddTo.push_back("Stomp");
  }
  if (transforms_[TRANSFORM_PLANTED]) {
    AddTo.push_back("Planted");
  }
  if (transforms_[TRANSFORM_FLOORED]) {
    AddTo.push_back("Floored");
  }
  if (transforms_[TRANSFORM_TWISTER]) {
    AddTo.push_back("Twister");
  }
  if (transforms_[TRANSFORM_HOLDROLLS]) {
    AddTo.push_back("HoldsToRolls");
  }
  if (transforms_[TRANSFORM_NOJUMPS]) {
    AddTo.push_back("NoJumps");
  }
  if (transforms_[TRANSFORM_NOHANDS]) {
    AddTo.push_back("NoHands");
  }
  if (transforms_[TRANSFORM_NOLIFTS]) {
    AddTo.push_back("NoLifts");
  }
  if (transforms_[TRANSFORM_NOFAKES]) {
    AddTo.push_back("NoFakes");
  }
  if (transforms_[TRANSFORM_NOQUADS]) {
    AddTo.push_back("NoQuads");
  }
  if (transforms_[TRANSFORM_NOSTRETCH]) {
    AddTo.push_back("NoStretch");
  }
  if (mute_on_error_) {
    AddTo.push_back("MuteOnError");
  }

  switch (fail_type_) {
    case FailType_Immediate:
      break;
    case FailType_ImmediateContinue:
      AddTo.push_back("FailImmediateContinue");
      break;
    case FailType_EndOfSong:
      AddTo.push_back("FailAtEnd");
      break;
    case FailType_Off:
      AddTo.push_back("FailOff");
      break;
    default:
      FAIL_M(ssprintf("Invalid FailType: %i", fail_type_));
  }

  if (skew_ == 0 && perspective_tilt_ == 0) {
    AddTo.push_back("Overhead");
  } else if (skew_ == 0) {
    if (perspective_tilt_ > 0) {
      AddPart(AddTo, perspective_tilt_, "Distant");
    } else {
      AddPart(AddTo, -perspective_tilt_, "Hallway");
    }
  } else if (std::abs(skew_ - perspective_tilt_) < 0.0001f) {
    AddPart(AddTo, skew_, "Space");
  } else if (std::abs(skew_ + perspective_tilt_) < 0.0001f) {
    AddPart(AddTo, skew_, "Incoming");
  } else {
    AddPart(AddTo, skew_, "Skew");
    AddPart(AddTo, perspective_tilt_, "Tilt");
  }

  // Don't display a string if using the default NoteSkin unless we force it.
  if (bForceNoteSkin ||
      (!note_skin_.empty() &&
       note_skin_ != CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue())) {
    std::string s = note_skin_;
    Capitalize(s);
    AddTo.push_back(s);
  }

  if (std::abs(visual_delay_) > 0.0001f) {
    // Format the string to be something like "10ms VisualDelay".
    // Note that we don't process sub-millisecond visual delay.
    AddTo.push_back(ssprintf("%.0fms VisualDelay", visual_delay_ * 1000.0f));
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
    AddTo.push_back(ss.str());
  }
}

/* Options are added to the current settings; call Init() beforehand if
 * you don't want this. */
void PlayerOptions::FromString(const std::string& sMultipleMods) {
  std::string sTemp = sMultipleMods;
  std::vector<std::string> vs;
  split(sTemp, ",", vs, true);
  std::string sThrowAway;
  for (std::string& s : vs) {
    if (!FromOneModString(s, sThrowAway)) {
      LOG->Trace(
          "Attempted to load a non-existing mod \'%s\' for the Player. "
          "Ignoring.",
          s.c_str());
    }
  }
}

bool PlayerOptions::FromOneModString(
    const std::string& sOneMod, std::string& sErrorOut) {
  ASSERT_M(
      NOTESKIN != nullptr,
      "The Noteskin Manager must be loaded in order to process mods.");

  std::string sBit = sOneMod;
  std::string sMod = "";
  MakeLower(sBit);
  Trim(sBit);

  /* "drunk"
   * "no drunk"
   * "150% drunk"
   * "*2 100% drunk": approach at 2x normal speed */

  float level = 1;
  float speed = 1;
  std::vector<std::string> asParts;
  split(sBit, " ", asParts, true);

  for (const std::string& s : asParts) {
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
        sErrorOut = ssprintf(
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

  sBit = asParts.back();

#define SET_FLOAT(opt)   \
  {                      \
    opt = level;         \
    speed_##opt = speed; \
  }
  const bool on = (level > 0.5f);

  static Regex mult("^([0-9]+(\\.[0-9]+)?)x$");
  static Regex disabledWindows("(w[1-5])");
  std::vector<std::string> matches;
  if (mult.Compare(sBit, matches)) {
    StringConversion::FromString(matches[0], level);
    SET_FLOAT(scroll_speed_)
    SET_FLOAT(time_spacing_)
    time_spacing_ = 0;
    max_scroll_bpm_ = 0;
  } else if (sscanf(sBit.c_str(), "c%f", &level) == 1) {
    if (!std::isfinite(level) || level <= 0.0f) {
      level = CMOD_DEFAULT;
    }
    SET_FLOAT(scroll_bpm_)
    SET_FLOAT(time_spacing_)
    time_spacing_ = 1;
    max_scroll_bpm_ = 0;
  }
  // oITG's m-mods
  else if (sscanf(sBit.c_str(), "m%f", &level) == 1) {
    // OpenITG doesn't have this block:
    /*
    if( !std::isfinite(level) || level <= 0.0f )
            level = CMOD_DEFAULT;
    */
    SET_FLOAT(max_scroll_bpm_)
    time_spacing_ = 0;
  }

  else if (sBit == "clearall") {
    Init();
    note_skin_ = NOTESKIN->GetDefaultNoteSkinName();
  } else if (sBit == "resetspeed") {
    /* level is set to the values from Init() because all speed related
       fields are being reset to initial values, and they each have different
       initial values.  -kyz */
    level = 0;
    SET_FLOAT(max_scroll_bpm_);
    SET_FLOAT(time_spacing_);
    level = 1.0f;
    SET_FLOAT(scroll_speed_);
    level = CMOD_DEFAULT;
    SET_FLOAT(scroll_bpm_)
  } else if (sBit == "life" || sBit == "lives") {
    // level is a percentage for every other option, so multiply by 100. -Kyz
    battery_lives_ = level * 100.0f;
  } else if (sBit.find("modtimer") != sBit.npos) {
    if (sBit == "modtimerdefault") {
      mod_timer_type_ = ModTimerType_Default;
    } else if (sBit == "modtimersong") {
      mod_timer_type_ = ModTimerType_Song;
    } else if (sBit == "modtimerbeat") {
      mod_timer_type_ = ModTimerType_Beat;
    } else if (sBit == "modtimergame") {
      mod_timer_type_ = ModTimerType_Game;
    } else if (sBit == "modtimermult") {
      SET_FLOAT(mod_timer_mult_)
    } else if (sBit == "modtimeroffset") {
      SET_FLOAT(mod_timer_offset_)
    }
  } else if (sBit.find("drawsize") != sBit.npos) {
    if (sBit == "drawsize") {
      SET_FLOAT(draw_size_)
    } else if (sBit == "drawsizeback") {
      SET_FLOAT(draw_size_back_)
    }
  } else if (sBit == "bar") {
    life_type_ = LifeType_Bar;
  } else if (sBit == "battery") {
    life_type_ = LifeType_Battery;
  } else if (sBit == "lifetime") {
    life_type_ = LifeType_Time;
  } else if (sBit == "norecover" || sBit == "power-drop") {
    drain_type_ = DrainType_NoRecover;
  } else if (sBit == "suddendeath" || sBit == "death") {
    drain_type_ = DrainType_SuddenDeath;
  } else if (sBit == "normal-drain") {
    drain_type_ = DrainType_Normal;
  } else if (sBit.find("lights") != sBit.npos) {
    if (sBit == "nohidelights") {
      hide_light_type_ = HideLightType_NoHideLights;
    } else if (sBit == "hidealllights") {
      hide_light_type_ = HideLightType_HideAllLights;
    } else if (sBit == "hidemarqueelights") {
      hide_light_type_ = HideLightType_HideMarqueeLights;
    } else if (sBit == "hidebasslights") {
      hide_light_type_ = HideLightType_HideBassLights;
    }
  } else if (sBit == "boost") {
    SET_FLOAT(accels_[ACCEL_BOOST])
  } else if (sBit == "brake" || sBit == "land") {
    SET_FLOAT(accels_[ACCEL_BRAKE])
  } else if (sBit.find("wave") != sBit.npos) {
    if (sBit == "wave") {
      SET_FLOAT(accels_[ACCEL_WAVE])
    } else if (sBit == "waveperiod") {
      SET_FLOAT(accels_[ACCEL_WAVE_PERIOD])
    }
  } else if (
      (sBit.find("expand") != sBit.npos) ||
      (sBit.find("dwiwave") != sBit.npos)) {
    if (sBit == "expand" || sBit == "dwiwave") {
      SET_FLOAT(accels_[ACCEL_EXPAND])
    } else if (sBit == "expandperiod") {
      SET_FLOAT(accels_[ACCEL_EXPAND_PERIOD])
    } else if (sBit == "tanexpand") {
      SET_FLOAT(accels_[ACCEL_TAN_EXPAND])
    } else if (sBit == "tanexpandperiod") {
      SET_FLOAT(accels_[ACCEL_TAN_EXPAND_PERIOD])
    }
  } else if (sBit == "boomerang") {
    SET_FLOAT(accels_[ACCEL_BOOMERANG])
  } else if (sBit.find("drunk") != sBit.npos) {
    if (sBit == "drunk") {
      SET_FLOAT(effects_[EFFECT_DRUNK])
    } else if (sBit == "drunkspeed") {
      SET_FLOAT(effects_[EFFECT_DRUNK_SPEED])
    } else if (sBit == "drunkoffset") {
      SET_FLOAT(effects_[EFFECT_DRUNK_OFFSET])
    } else if (sBit == "drunkperiod") {
      SET_FLOAT(effects_[EFFECT_DRUNK_PERIOD])
    } else if (sBit == "tandrunk") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK])
    } else if (sBit == "tandrunkspeed") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_SPEED])
    } else if (sBit == "tandrunkoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_OFFSET])
    } else if (sBit == "tandrunkperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_PERIOD])
    } else if (sBit == "drunkz") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z])
    } else if (sBit == "drunkzspeed") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z_SPEED])
    } else if (sBit == "drunkzoffset") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z_OFFSET])
    } else if (sBit == "drunkzperiod") {
      SET_FLOAT(effects_[EFFECT_DRUNK_Z_PERIOD])
    } else if (sBit == "tandrunkz") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z])
    } else if (sBit == "tandrunkzspeed") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z_SPEED])
    } else if (sBit == "tandrunkzoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z_OFFSET])
    } else if (sBit == "tandrunkzperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DRUNK_Z_PERIOD])
    }
  } else if (sBit.find("shrink") != sBit.npos) {
    if (sBit == "shrinklinear") {
      SET_FLOAT(effects_[EFFECT_SHRINK_TO_LINEAR])
    } else if (sBit == "shrinkmult") {
      SET_FLOAT(effects_[EFFECT_SHRINK_TO_MULT])
    }
  } else if (sBit.find("pulse") != sBit.npos) {
    if (sBit == "pulseinner") {
      SET_FLOAT(effects_[EFFECT_PULSE_INNER])
    } else if (sBit == "pulseouter") {
      SET_FLOAT(effects_[EFFECT_PULSE_OUTER])
    } else if (sBit == "pulseoffset") {
      SET_FLOAT(effects_[EFFECT_PULSE_OFFSET])
    } else if (sBit == "pulseperiod") {
      SET_FLOAT(effects_[EFFECT_PULSE_PERIOD])
    }
  } else if (sBit.find("dizzy") != sBit.npos) {
    if (sBit == "dizzy") {
      SET_FLOAT(effects_[EFFECT_DIZZY])
    } else if (sBit == "dizzyholds") {
      dizzy_holds_ = on;
    }
  } else if (sBit.find("confusion") != sBit.npos) {
    if (sBit.find("x") != sBit.npos) {
      if (sBit == "confusionx") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_X])
      } else if (sBit == "confusionxoffset") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_X_OFFSET])
      } else {
        for (int i = 0; i < 16; i++) {
          sMod = ssprintf("confusionxoffset%d", i + 1);
          if (sBit == sMod) {
            SET_FLOAT(confusion_x_[i])
            break;
          }
        }
      }
    } else if (sBit.find("y") != sBit.npos) {
      if (sBit == "confusiony") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_Y])
      } else if (sBit == "confusionyoffset") {
        SET_FLOAT(effects_[EFFECT_CONFUSION_Y_OFFSET])
      } else {
        for (int i = 0; i < 16; i++) {
          sMod = ssprintf("confusionyoffset%d", i + 1);
          if (sBit == sMod) {
            SET_FLOAT(confusion_y_[i])
            break;
          }
        }
      }
    }
    if (sBit == "confusion") {
      SET_FLOAT(effects_[EFFECT_CONFUSION])
    } else if (sBit == "confusionoffset") {
      SET_FLOAT(effects_[EFFECT_CONFUSION_OFFSET])
    } else {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("confusionoffset%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(confusion_z_[i])
          break;
        }
      }
    }
  } else if (sBit.find("bounce") != sBit.npos) {
    if (sBit == "bounce") {
      SET_FLOAT(effects_[EFFECT_BOUNCE])
    } else if (sBit == "bounceperiod") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_PERIOD])
    } else if (sBit == "bounceoffset") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_OFFSET])
    } else if (sBit == "bouncez") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_Z])
    } else if (sBit == "bouncezperiod") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_Z_PERIOD])
    } else if (sBit == "bouncezoffset") {
      SET_FLOAT(effects_[EFFECT_BOUNCE_Z_OFFSET])
    }
  } else if (sBit == "mini") {
    SET_FLOAT(effects_[EFFECT_MINI])
  } else if (sBit.find("tiny") != sBit.npos) {
    if (sBit == "tiny") {
      SET_FLOAT(effects_[EFFECT_TINY])
    } else {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("tiny%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(tiny_[i])
          break;
        }
      }
    }
  } else if (sBit == "flip") {
    SET_FLOAT(effects_[EFFECT_FLIP])
  } else if (sBit == "invert") {
    SET_FLOAT(effects_[EFFECT_INVERT])
  } else if (sBit.find("tornado") != sBit.npos) {
    if (sBit == "tornado") {
      SET_FLOAT(effects_[EFFECT_TORNADO])
    } else if (sBit == "tornadoperiod") {
      SET_FLOAT(effects_[EFFECT_TORNADO_PERIOD])
    } else if (sBit == "tornadooffset") {
      SET_FLOAT(effects_[EFFECT_TORNADO_OFFSET])
    } else if (sBit == "tantornado") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO])
    } else if (sBit == "tantornadoperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_PERIOD])
    } else if (sBit == "tantornadooffset") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_OFFSET])
    } else if (sBit == "tornadoz") {
      SET_FLOAT(effects_[EFFECT_TORNADO_Z])
    } else if (sBit == "tornadozperiod") {
      SET_FLOAT(effects_[EFFECT_TORNADO_Z_PERIOD])
    } else if (sBit == "tornadozoffset") {
      SET_FLOAT(effects_[EFFECT_TORNADO_Z_OFFSET])
    } else if (sBit == "tantornadoz") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_Z])
    } else if (sBit == "tantornadozperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_Z_PERIOD])
    } else if (sBit == "tantornadozoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_TORNADO_Z_OFFSET])
    }
  } else if (sBit.find("tipsy") != sBit.npos) {
    if (sBit == "tipsy") {
      SET_FLOAT(effects_[EFFECT_TIPSY])
    } else if (sBit == "tipsyspeed") {
      SET_FLOAT(effects_[EFFECT_TIPSY_SPEED])
    } else if (sBit == "tipsyoffset") {
      SET_FLOAT(effects_[EFFECT_TIPSY_OFFSET])
    } else if (sBit == "tantipsy") {
      SET_FLOAT(effects_[EFFECT_TAN_TIPSY])
    } else if (sBit == "tantipsyspeed") {
      SET_FLOAT(effects_[EFFECT_TAN_TIPSY_SPEED])
    } else if (sBit == "tantipsyoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_TIPSY_OFFSET])
    }
  } else if (sBit.find("bumpy") != sBit.npos) {
    if (sBit == "bumpy") {
      SET_FLOAT(effects_[EFFECT_BUMPY])
    } else if (sBit == "bumpyoffset") {
      SET_FLOAT(effects_[EFFECT_BUMPY_OFFSET])
    } else if (sBit == "bumpyperiod") {
      SET_FLOAT(effects_[EFFECT_BUMPY_PERIOD])
    } else if (sBit == "tanbumpy") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY])
    } else if (sBit == "tanbumpyoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_OFFSET])
    } else if (sBit == "tanbumpyperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_PERIOD])
    } else if (sBit == "bumpyx") {
      SET_FLOAT(effects_[EFFECT_BUMPY_X])
    } else if (sBit == "bumpyxoffset") {
      SET_FLOAT(effects_[EFFECT_BUMPY_X_OFFSET])
    } else if (sBit == "bumpyxperiod") {
      SET_FLOAT(effects_[EFFECT_BUMPY_X_PERIOD])
    } else if (sBit == "tanbumpyx") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_X])
    } else if (sBit == "tanbumpyxoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_X_OFFSET])
    } else if (sBit == "tanbumpyxperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_BUMPY_X_PERIOD])
    } else {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("bumpy%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(bumpy_[i])
          break;
        }
      }
    }
  } else if (sBit.find("beat") != sBit.npos) {
    if (sBit == "beat") {
      SET_FLOAT(effects_[EFFECT_BEAT])
    } else if (sBit == "beatoffset") {
      SET_FLOAT(effects_[EFFECT_BEAT_OFFSET])
    } else if (sBit == "beatperiod") {
      SET_FLOAT(effects_[EFFECT_BEAT_PERIOD])
    } else if (sBit == "beatmult") {
      SET_FLOAT(effects_[EFFECT_BEAT_MULT])
    } else if (sBit == "beaty") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y])
    } else if (sBit == "beatyoffset") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y_OFFSET])
    } else if (sBit == "beatyperiod") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y_PERIOD])
    } else if (sBit == "beatymult") {
      SET_FLOAT(effects_[EFFECT_BEAT_Y_MULT])
    } else if (sBit == "beatz") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z])
    } else if (sBit == "beatzoffset") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z_OFFSET])
    } else if (sBit == "beatzperiod") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z_PERIOD])
    } else if (sBit == "beatzmult") {
      SET_FLOAT(effects_[EFFECT_BEAT_Z_MULT])
    }
  } else if (sBit.find("digital") != sBit.npos) {
    if (sBit == "digital") {
      SET_FLOAT(effects_[EFFECT_DIGITAL])
    } else if (sBit == "digitalsteps") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_STEPS])
    } else if (sBit == "digitalperiod") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_PERIOD])
    } else if (sBit == "digitaloffset") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_OFFSET])
    } else if (sBit == "tandigital") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL])
    } else if (sBit == "tandigitalsteps") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_STEPS])
    } else if (sBit == "tandigitalperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_PERIOD])
    } else if (sBit == "tandigitaloffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_OFFSET])
    } else if (sBit == "digitalz") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z])
    } else if (sBit == "digitalzsteps") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z_STEPS])
    } else if (sBit == "digitalzperiod") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z_PERIOD])
    } else if (sBit == "digitalzoffset") {
      SET_FLOAT(effects_[EFFECT_DIGITAL_Z_OFFSET])
    } else if (sBit == "tandigitalz") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z])
    } else if (sBit == "tandigitalzsteps") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z_STEPS])
    } else if (sBit == "tandigitalzperiod") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z_PERIOD])
    } else if (sBit == "tandigitalzoffset") {
      SET_FLOAT(effects_[EFFECT_TAN_DIGITAL_Z_OFFSET])
    }
  } else if (sBit.find("zigzag") != sBit.npos) {
    if (sBit == "zigzag") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG])
    } else if (sBit == "zigzagperiod") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_PERIOD])
    } else if (sBit == "zigzagoffset") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_OFFSET])
    } else if (sBit == "zigzagz") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_Z])
    } else if (sBit == "zigzagzperiod") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_Z_PERIOD])
    } else if (sBit == "zigzagzoffset") {
      SET_FLOAT(effects_[EFFECT_ZIGZAG_Z_OFFSET])
    }
  } else if (sBit.find("sawtooth") != sBit.npos) {
    if (sBit == "sawtooth") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH])
    } else if (sBit == "sawtoothperiod") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH_PERIOD])
    } else if (sBit == "sawtoothz") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH_Z])
    } else if (sBit == "sawtoothzperiod") {
      SET_FLOAT(effects_[EFFECT_SAWTOOTH_Z_PERIOD])
    }
  } else if (sBit.find("square") != sBit.npos) {
    if (sBit == "square") {
      SET_FLOAT(effects_[EFFECT_SQUARE])
    } else if (sBit == "squareoffset") {
      SET_FLOAT(effects_[EFFECT_SQUARE_OFFSET])
    } else if (sBit == "squareperiod") {
      SET_FLOAT(effects_[EFFECT_SQUARE_PERIOD])
    } else if (sBit == "squarez") {
      SET_FLOAT(effects_[EFFECT_SQUARE_Z])
    } else if (sBit == "squarezoffset") {
      SET_FLOAT(effects_[EFFECT_SQUARE_Z_OFFSET])
    } else if (sBit == "squarezperiod") {
      SET_FLOAT(effects_[EFFECT_SQUARE_Z_PERIOD])
    }
  } else if (sBit.find("parabola") != sBit.npos) {
    if (sBit == "parabolax") {
      SET_FLOAT(effects_[EFFECT_PARABOLA_X])
    } else if (sBit == "parabolay") {
      SET_FLOAT(effects_[EFFECT_PARABOLA_Y])
    } else if (sBit == "parabolaz") {
      SET_FLOAT(effects_[EFFECT_PARABOLA_Z])
    }
  } else if (sBit.find("attenuate") != sBit.npos) {
    if (sBit == "attenuatex") {
      SET_FLOAT(effects_[EFFECT_ATTENUATE_X])
    } else if (sBit == "attenuatey") {
      SET_FLOAT(effects_[EFFECT_ATTENUATE_Y])
    } else if (sBit == "attenuatez") {
      SET_FLOAT(effects_[EFFECT_ATTENUATE_Z])
    }
  } else if (sBit == "xmode") {
    SET_FLOAT(effects_[EFFECT_XMODE])
  } else if (sBit == "twirl") {
    SET_FLOAT(effects_[EFFECT_TWIRL])
  } else if (sBit == "roll") {
    SET_FLOAT(effects_[EFFECT_ROLL])
  } else if (sBit == "hidden") {
    SET_FLOAT(appearances_[APPEARANCE_HIDDEN])
  } else if (sBit == "hiddenoffset") {
    SET_FLOAT(appearances_[APPEARANCE_HIDDEN_OFFSET])
  } else if (sBit == "sudden") {
    SET_FLOAT(appearances_[APPEARANCE_SUDDEN])
  } else if (sBit == "suddenoffset") {
    SET_FLOAT(appearances_[APPEARANCE_SUDDEN_OFFSET])
  } else if (sBit.find("stealth") != sBit.npos) {
    if (sBit == "stealth") {
      SET_FLOAT(appearances_[APPEARANCE_STEALTH])
    } else if (sBit == "stealthtype") {
      stealth_type_ = on;
    } else if (sBit == "stealthpastreceptors") {
      stealth_past_receptors_ = on;
    } else {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("stealth%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(stealth_[i])
          break;
        }
      }
    }
  } else if (sBit == "blink") {
    SET_FLOAT(appearances_[APPEARANCE_BLINK])
  } else if (sBit == "randomvanish") {
    SET_FLOAT(appearances_[APPEARANCE_RANDOMVANISH])
  } else if (sBit == "turn" && !on) {
    ZERO(turns_); /* "no turn" */
  } else if (sBit == "mirror") {
    turns_[TURN_MIRROR] = on;
  } else if (sBit == "lrmirror") {
    turns_[TURN_LRMIRROR] = on;
  } else if (sBit == "udmirror") {
    turns_[TURN_UDMIRROR] = on;
  } else if (sBit == "backwards") {
    turns_[TURN_BACKWARDS] = on;
  } else if (sBit == "left") {
    turns_[TURN_LEFT] = on;
  } else if (sBit == "right") {
    turns_[TURN_RIGHT] = on;
  } else if (sBit == "shuffle") {
    turns_[TURN_SHUFFLE] = on;
  } else if (sBit == "softshuffle") {
    turns_[TURN_SOFT_SHUFFLE] = on;
  } else if (sBit == "supershuffle") {
    turns_[TURN_SUPER_SHUFFLE] = on;
  } else if (sBit == "hypershuffle") {
    turns_[TURN_HYPER_SHUFFLE] = on;
  } else if (sBit == "little") {
    transforms_[TRANSFORM_LITTLE] = on;
  } else if (sBit == "wide") {
    transforms_[TRANSFORM_WIDE] = on;
  } else if (sBit == "big") {
    transforms_[TRANSFORM_BIG] = on;
  } else if (sBit == "quick") {
    transforms_[TRANSFORM_QUICK] = on;
  } else if (sBit == "bmrize") {
    transforms_[TRANSFORM_BMRIZE] = on;
  } else if (sBit == "skippy") {
    transforms_[TRANSFORM_SKIPPY] = on;
  } else if (sBit == "mines") {
    transforms_[TRANSFORM_MINES] = on;
  } else if (sBit == "attackmines") {
    transforms_[TRANSFORM_ATTACKMINES] = on;
  } else if (sBit == "echo") {
    transforms_[TRANSFORM_ECHO] = on;
  } else if (sBit == "stomp") {
    transforms_[TRANSFORM_STOMP] = on;
  } else if (sBit == "planted") {
    transforms_[TRANSFORM_PLANTED] = on;
  } else if (sBit == "floored") {
    transforms_[TRANSFORM_FLOORED] = on;
  } else if (sBit == "twister") {
    transforms_[TRANSFORM_TWISTER] = on;
  } else if (sBit == "holdrolls") {
    transforms_[TRANSFORM_HOLDROLLS] = on;
  } else if (sBit == "nojumps") {
    transforms_[TRANSFORM_NOJUMPS] = on;
  } else if (sBit == "nohands") {
    transforms_[TRANSFORM_NOHANDS] = on;
  } else if (sBit == "noquads") {
    transforms_[TRANSFORM_NOQUADS] = on;
  } else if (sBit.find("reverse") != sBit.npos) {
    if (sBit == "reverse") {
      SET_FLOAT(scrolls_[SCROLL_REVERSE])
    } else {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("reverse%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(reverse_[i])
          break;
        }
      }
    }
  } else if (sBit == "split") {
    SET_FLOAT(scrolls_[SCROLL_SPLIT])
  } else if (sBit == "alternate") {
    SET_FLOAT(scrolls_[SCROLL_ALTERNATE])
  } else if (sBit == "cross") {
    SET_FLOAT(scrolls_[SCROLL_CROSS])
  } else if (sBit == "centered") {
    SET_FLOAT(scrolls_[SCROLL_CENTERED])
  } else if (sBit == "noholds") {
    transforms_[TRANSFORM_NOHOLDS] = on;
  } else if (sBit == "norolls") {
    transforms_[TRANSFORM_NOROLLS] = on;
  } else if (sBit == "nomines") {
    transforms_[TRANSFORM_NOMINES] = on;
  } else if (sBit == "nostretch") {
    transforms_[TRANSFORM_NOSTRETCH] = on;
  } else if (sBit == "nolifts") {
    transforms_[TRANSFORM_NOLIFTS] = on;
  } else if (sBit == "nofakes") {
    transforms_[TRANSFORM_NOFAKES] = on;
  } else if (sBit.find("dark") != sBit.npos) {
    if (sBit == "dark") {
      SET_FLOAT(dark_)
    } else {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("dark%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(darks_[i])
          break;
        }
      }
    }
  } else if (sBit == "blind") {
    SET_FLOAT(blind_)
  } else if (sBit == "cover") {
    SET_FLOAT(cover_)
  } else if (sBit == "randomattacks") {
    SET_FLOAT(rand_attack_)
  } else if (sBit == "noattacks") {
    SET_FLOAT(no_attack_)
  } else if (sBit == "playerautoplay") {
    SET_FLOAT(player_auto_play_)
  } else if (sBit == "passmark") {
    SET_FLOAT(passmark_)
  } else if (sBit == "overhead") {
    skew_ = 0;
    perspective_tilt_ = 0;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (sBit == "incoming") {
    skew_ = level;
    perspective_tilt_ = -level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (sBit == "space") {
    skew_ = level;
    perspective_tilt_ = +level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (sBit == "hallway") {
    skew_ = 0;
    perspective_tilt_ = -level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (sBit == "distant") {
    skew_ = 0;
    perspective_tilt_ = +level;
    speed_skew_ = speed_perspective_tilt_ = speed;
  } else if (NOTESKIN && NOTESKIN->DoesNoteSkinExist(sBit)) {
    note_skin_ = sBit;
  } else if (sBit == "skew") {
    SET_FLOAT(skew_)
  } else if (sBit == "tilt") {
    SET_FLOAT(perspective_tilt_)
  } else if (sBit == "noteskin" && !on) /* "no noteskin" */
  {
    note_skin_ = CommonMetrics::DEFAULT_NOTESKIN_NAME;
  } else if (sBit == "randomspeed") {
    SET_FLOAT(random_speed_)
  } else if (sBit == "failarcade" || sBit == "failimmediate") {
    fail_type_ = FailType_Immediate;
  } else if (sBit == "failendofsong" || sBit == "failimmediatecontinue") {
    fail_type_ = FailType_ImmediateContinue;
  } else if (sBit == "failatend") {
    fail_type_ = FailType_EndOfSong;
  } else if (sBit == "failoff") {
    fail_type_ = FailType_Off;
  } else if (sBit == "faildefault") {
    PlayerOptions po;
    GAMESTATE->GetDefaultPlayerOptions(po);
    fail_type_ = po.fail_type_;
  } else if (sBit == "muteonerror") {
    mute_on_error_ = on;
  } else if (sBit == "random") {
    ChooseRandomModifiers();
  }

  else if (sBit.find("move") != sBit.npos) {
    if (sBit.find("x") != sBit.npos) {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("movex%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(moves_x_[i])
          break;
        }
      }
    } else if (sBit.find("y") != sBit.npos) {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("movey%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(moves_y_[i])
          break;
        }
      }
    } else if (sBit.find("z") != sBit.npos) {
      for (int i = 0; i < 16; i++) {
        sMod = ssprintf("movez%d", i + 1);
        if (sBit == sMod) {
          SET_FLOAT(moves_z_[i])
          break;
        }
      }
    }
  } else if (sBit == "zbuffer") {
    z_buffer_ = on;
  } else if (sBit == "cosecant") {
    cosecant_ = on;
  } else if (sBit == "visualdelay") {
    visual_delay_ = level;
  } else if (level == 0 && disabledWindows.Compare(sBit))  // "No w1" etc.
  {
    // We come into this condition if there is at least a single window present
    // but there may be more. To get all of the windows, we go through in a loop
    // to extract all of them.
    static Regex allDisabledWindows("(w[1-5])(.*)$");
    std::string input = sBit;
    while (true) {
      if (!allDisabledWindows.Compare(input, matches)) {
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
  else if (sBit == "converge") {
    SET_FLOAT(scrolls_[SCROLL_CENTERED])
  }
  // end of the list
  else {
    // Maybe the original string is a noteskin name with a space. -Kyz
    std::string name = sOneMod;
    MakeLower(name);
    if (NOTESKIN && NOTESKIN->DoesNoteSkinExist(name)) {
      note_skin_ = name;
    }
    return false;
  }

  return true;
}

void NextFloat(float fValues[], int size) {
  int index = -1;
  for (int i = 0; i < size; i++) {
    if (fValues[i] == 1) {
      index = i;
      break;
    }
  }

  for (int i = 0; i < size; i++) {
    fValues[i] = 0;
  }

  if (index ==
      size - 1)  // if true, then the last float in the list was selected
    ;            // leave all off
  else {
    fValues[index + 1] = 1;
  }
}

void NextBool(bool bValues[], int size) {
  int index = -1;
  for (int i = 0; i < size; i++) {
    if (bValues[i]) {
      index = i;
      break;
    }
  }

  for (int i = 0; i < size; i++) {
    bValues[i] = false;
  }

  if (index ==
      size - 1)  // if true, then the last float in the list was selected
    ;            // leave all off
  else {
    bValues[index + 1] = 1;
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
  bool bWasOn = turns_[t];
  ZERO(turns_);
  turns_[t] = !bWasOn;
}

float PlayerOptions::GetReversePercentForColumn(int iCol) const {
  float f = 0;
  ASSERT(pn_ == PLAYER_1 || pn_ == PLAYER_2);
  ASSERT(GAMESTATE->GetCurrentStyle(pn_) != nullptr);
  int iNumCols = GAMESTATE->GetCurrentStyle(pn_)->cols_per_player_;

  f += scrolls_[SCROLL_REVERSE];
  f += reverse_[iCol];

  if (iCol >= iNumCols / 2) {
    f += scrolls_[SCROLL_SPLIT];
  }

  if ((iCol % 2) == 1) {
    f += scrolls_[SCROLL_ALTERNATE];
  }

  int iFirstCrossCol = iNumCols / 4;
  int iLastCrossCol = iNumCols - 1 - iFirstCrossCol;
  if (iCol >= iFirstCrossCol && iCol <= iLastCrossCol) {
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
    Song* pSong, Steps* pSteps, PlayerNumber pn) const {
  if (time_spacing_ && pSteps->HasSignificantTimingChanges()) {
    return true;
  }
  const RadarValues& rv = pSteps->GetRadarValues(pn);
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
  if ((no_attack_ && pSteps->HasAttacks()) || rand_attack_) {
    return true;
  }

  if (cover_ && (!pSong->GetBackgroundChanges(BACKGROUND_LAYER_1).empty() ||
                 !pSong->GetForegroundChanges().empty())) {
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
      Trail* pTrail = GAMESTATE->cur_course_->GetTrail(
          GAMESTATE->GetCurrentStyle(pn_)->steps_type_);
      pTrail->GetDisplayBpms(bpms);
    } else {
      GAMESTATE->cur_song_->GetDisplayBpms(bpms);
    }
    pSong->GetDisplayBpms(bpms);

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
    Course* pCourse, Trail* pTrail) const {
  ASSERT(pCourse != nullptr);
  ASSERT(pTrail != nullptr);

  return std::any_of(
      pTrail->entries_.begin(), pTrail->entries_.end(),
      [&](const TrailEntry& e) {
        return e.song_ && IsEasierForSongAndSteps(e.song_, e.steps_, PLAYER_1);
      });
}

void PlayerOptions::GetLocalizedMods(std::vector<std::string>& AddTo) const {
  std::vector<std::string> vMods;
  GetMods(vMods);
  for (const std::string& sOneMod : vMods) {
    ASSERT(!sOneMod.empty());

    std::vector<std::string> asTokens;
    split(sOneMod, " ", asTokens);

    if (asTokens.empty()) {
      continue;
    }

    // Strip the approach speed token, if any
    if (asTokens[0][0] == '*') {
      asTokens.erase(asTokens.begin());
    }

    // capitalize NoteSkin names
    asTokens.back() = Capitalize(asTokens.back());

    /* Theme the mod name (the last string).  Allow this to not exist, since
     * characters might use modifiers that don't exist in the theme. */
    asTokens.back() = CommonMetrics::LocalizeOptionItem(asTokens.back(), true);

    std::string sLocalizedMod = join(" ", asTokens);
    AddTo.push_back(sLocalizedMod);
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
    Song* pSong = Luna<Song>::check(L, 1);
    Steps* pSteps = Luna<Steps>::check(L, 2);
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 3);
    lua_pushboolean(L, p->IsEasierForSongAndSteps(pSong, pSteps, pn));
    return 1;
  }
  static int IsEasierForCourseAndTrail(T* p, lua_State* L) {
    // course, trail
    Course* pCourse = Luna<Course>::check(L, 1);
    Trail* pTrail = Luna<Trail>::check(L, 2);
    lua_pushboolean(L, p->IsEasierForCourseAndTrail(pCourse, pTrail));
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
      p->scroll_bpm_ = CMOD_DEFAULT;
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
      p->scroll_bpm_ = CMOD_DEFAULT;
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
    const int colNum = IArg(1);
    const int numColumns = GAMESTATE->GetCurrentStyle(p->pn_)->cols_per_player_;

    // We don't want to go outside the bounds.
    if (colNum < 0 || colNum > numColumns) {
      lua_pushnil(L);
    } else {
      lua_pushnumber(L, p->GetReversePercentForColumn(colNum));
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
