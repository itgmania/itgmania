#include "ArrowEffects.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LuaManager.h"
#include "NoteDisplay.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "RageMath.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "Song.h"
#include "SongPosition.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeMetric.h"
#include "global.h"

static const char dimension_names[4] = "XYZ";

static ThemeMetric<float> ARROW_SPACING("ArrowEffects", "ArrowSpacing");
static ThemeMetric<bool> QUANTIZE_ARROW_Y(
    "ArrowEffects", "QuantizeArrowYPosition");

/* For better or for worse, allow the themes to modify the various mod
 * effects for the different mods. In general, it is recommended to not
 * edit the default values and instead use percentage mods when changes
 * are wanted. Still, the option is available for those that want it.
 *
 * Is this a good idea? We'll find out. -aj & Wolfman2000 */
static ThemeMetric<float> BLINK_MOD_FREQUENCY(
    "ArrowEffects", "BlinkModFrequency");
static ThemeMetric<float> BOOST_MOD_MIN_CLAMP(
    "ArrowEffects", "BoostModMinClamp");
static ThemeMetric<float> BOOST_MOD_MAX_CLAMP(
    "ArrowEffects", "BoostModMaxClamp");
static ThemeMetric<float> BRAKE_MOD_MIN_CLAMP(
    "ArrowEffects", "BrakeModMinClamp");
static ThemeMetric<float> BRAKE_MOD_MAX_CLAMP(
    "ArrowEffects", "BrakeModMaxClamp");
static ThemeMetric<float> WAVE_MOD_MAGNITUDE(
    "ArrowEffects", "WaveModMagnitude");
static ThemeMetric<float> WAVE_MOD_HEIGHT("ArrowEffects", "WaveModHeight");
static ThemeMetric<float> BOOMERANG_PEAK_PERCENTAGE(
    "ArrowEffects", "BoomerangPeakPercentage");
static ThemeMetric<float> EXPAND_MULTIPLIER_FREQUENCY(
    "ArrowEffects", "ExpandMultiplierFrequency");
static ThemeMetric<float> EXPAND_MULTIPLIER_SCALE_FROM_LOW(
    "ArrowEffects", "ExpandMultiplierScaleFromLow");
static ThemeMetric<float> EXPAND_MULTIPLIER_SCALE_FROM_HIGH(
    "ArrowEffects", "ExpandMultiplierScaleFromHigh");
static ThemeMetric<float> EXPAND_MULTIPLIER_SCALE_TO_LOW(
    "ArrowEffects", "ExpandMultiplierScaleToLow");
static ThemeMetric<float> EXPAND_MULTIPLIER_SCALE_TO_HIGH(
    "ArrowEffects", "ExpandMultiplierScaleToHigh");
static ThemeMetric<float> EXPAND_SPEED_SCALE_FROM_LOW(
    "ArrowEffects", "ExpandSpeedScaleFromLow");
static ThemeMetric<float> EXPAND_SPEED_SCALE_FROM_HIGH(
    "ArrowEffects", "ExpandSpeedScaleFromHigh");
static ThemeMetric<float> EXPAND_SPEED_SCALE_TO_LOW(
    "ArrowEffects", "ExpandSpeedScaleToLow");
static ThemeMetric<float> TIPSY_TIMER_FREQUENCY(
    "ArrowEffects", "TipsyTimerFrequency");
static ThemeMetric<float> TIPSY_COLUMN_FREQUENCY(
    "ArrowEffects", "TipsyColumnFrequency");
static ThemeMetric<float> TIPSY_ARROW_MAGNITUDE(
    "ArrowEffects", "TipsyArrowMagnitude");
static ThemeMetric<float> TIPSY_OFFSET_TIMER_FREQUENCY(
    "ArrowEffects", "TipsyOffsetTimerFrequency");
static ThemeMetric<float> TIPSY_OFFSET_COLUMN_FREQUENCY(
    "ArrowEffects", "TipsyOffsetColumnFrequency");
static ThemeMetric<float> TIPSY_OFFSET_ARROW_MAGNITUDE(
    "ArrowEffects", "TipsyOffsetArrowMagnitude");

static std::string TPSTL_NAME(size_t i) {
  return ssprintf("Tornado%cPositionScaleToLow", dimension_names[i]);
}
static ThemeMetric1D<float> TORNADO_POSITION_SCALE_TO_LOW(
    "ArrowEffects", TPSTL_NAME, 3);
static std::string TPSTH_NAME(size_t i) {
  return ssprintf("Tornado%cPositionScaleToHigh", dimension_names[i]);
}
static ThemeMetric1D<float> TORNADO_POSITION_SCALE_TO_HIGH(
    "ArrowEffects", TPSTH_NAME, 3);
static std::string TOF_NAME(size_t i) {
  return ssprintf("Tornado%cOffsetFrequency", dimension_names[i]);
}
static ThemeMetric1D<float> TORNADO_OFFSET_FREQUENCY(
    "ArrowEffects", TOF_NAME, 3);
static std::string TOSFL_NAME(size_t i) {
  return ssprintf("Tornado%cOffsetScaleFromLow", dimension_names[i]);
}
static ThemeMetric1D<float> TORNADO_OFFSET_SCALE_FROM_LOW(
    "ArrowEffects", TOSFL_NAME, 3);
static std::string TOSFH_NAME(size_t i) {
  return ssprintf("Tornado%cOffsetScaleFromHigh", dimension_names[i]);
}
static ThemeMetric1D<float> TORNADO_OFFSET_SCALE_FROM_HIGH(
    "ArrowEffects", TOSFH_NAME, 3);

static ThemeMetric<float> DRUNK_COLUMN_FREQUENCY(
    "ArrowEffects", "DrunkColumnFrequency");
static ThemeMetric<float> DRUNK_OFFSET_FREQUENCY(
    "ArrowEffects", "DrunkOffsetFrequency");
static ThemeMetric<float> DRUNK_ARROW_MAGNITUDE(
    "ArrowEffects", "DrunkArrowMagnitude");

static ThemeMetric<float> DRUNK_Z_COLUMN_FREQUENCY(
    "ArrowEffects", "DrunkZColumnFrequency");
static ThemeMetric<float> DRUNK_Z_OFFSET_FREQUENCY(
    "ArrowEffects", "DrunkZOffsetFrequency");
static ThemeMetric<float> DRUNK_Z_ARROW_MAGNITUDE(
    "ArrowEffects", "DrunkZArrowMagnitude");

static ThemeMetric<float> BEAT_OFFSET_HEIGHT(
    "ArrowEffects", "BeatOffsetHeight");
static ThemeMetric<float> BEAT_PI_HEIGHT("ArrowEffects", "BeatPIHeight");

static ThemeMetric<float> BEAT_Y_OFFSET_HEIGHT(
    "ArrowEffects", "BeatYOffsetHeight");
static ThemeMetric<float> BEAT_Y_PI_HEIGHT("ArrowEffects", "BeatYPIHeight");
static ThemeMetric<float> BEAT_Z_OFFSET_HEIGHT(
    "ArrowEffects", "BeatZOffsetHeight");
static ThemeMetric<float> BEAT_Z_PI_HEIGHT("ArrowEffects", "BeatZPIHeight");

static ThemeMetric<float> TINY_PERCENT_BASE("ArrowEffects", "TinyPercentBase");
static ThemeMetric<float> TINY_PERCENT_GATE("ArrowEffects", "TinyPercentGate");

static const PlayerOptions* curr_options = nullptr;

float ArrowGetPercentVisible(
    float y_pos_without_reverse, int col, float y_offset);

static float GetNoteFieldHeight() {
  return SCREEN_HEIGHT + std::abs(curr_options->perspective_tilt_) * 200;
}

float ArrowEffects::GetTime() {
  double mult = 1.0 + static_cast<double>(curr_options->mod_timer_mult_);
  double offset = static_cast<double>(curr_options->mod_timer_offset_);
  ModTimerType modtimer = curr_options->mod_timer_type_;
  double returned_time = 0;
  switch (modtimer) {
    case ModTimerType_Default:
    case ModTimerType_Game:
      returned_time = (RageTimer::GetTimeSinceStart() + offset) * mult;
      break;
    case ModTimerType_Beat:
      returned_time =
          (static_cast<double>(GAMESTATE->position_.song_beat_visible_) +
           offset) *
          mult;
      break;
    case ModTimerType_Song:
      returned_time =
          (static_cast<double>(GAMESTATE->position_.music_seconds_) + offset) *
          mult;
      break;
    default:
      returned_time = RageTimer::GetTimeSinceStart() + offset;
      break;
  }
  return static_cast<float>(returned_time);
}

namespace {
struct PerPlayerData {
  float min_tornado_[3][MAX_COLS_PER_PLAYER];
  float max_tornado_[3][MAX_COLS_PER_PLAYER];
  float invert_distance_[MAX_COLS_PER_PLAYER];
  float tipsy_result_[MAX_COLS_PER_PLAYER];
  float tipsy_offset_result_[MAX_COLS_PER_PLAYER];
  float tan_tipsy_result_[MAX_COLS_PER_PLAYER];
  float tan_tipsy_offset_result_[MAX_COLS_PER_PLAYER];
  float beat_factor_[3];
  float expand_seconds_;
  float tan_expand_seconds_;

  // m_prev_style is for checking whether ArrowEffects::Init needs to be
  // called.  Finding all the placed ArrowEffects is used and making sure
  // they all call Init after changing style is non-trivial and more likely
  // to cause bugs. -Kyz
  const Style* prev_style_;
};
PerPlayerData g_EffectData[NUM_PLAYERS];
const int dim_x = 0;
const int dim_y = 1;
const int dim_z = 2;

float tornado_position_scale_to_low[3];
float tornado_position_scale_to_high[3];
float tornado_offset_frequency[3];
float tornado_offset_scale_from_low[3];
float tornado_offset_scale_from_high[3];
};  // namespace

static float SelectTanType(float angle, bool is_cosec) {
  if (is_cosec) {
    return (1 / std::sin(angle));  // cosecant
  } else {
    return std::tan(angle);
  }
}

static float CalculateTornadoOffsetFromMagnitude(
    int dimension, int col_id, float magnitude, float effect_offset,
    float period, const Style::ColumnInfo* cols, float field_zoom,
    PerPlayerData& data, float y_offset, bool is_tan) {
  const float real_pixel_offset = cols[col_id].x_offset_ * field_zoom;
  const float position_between = SCALE(
      real_pixel_offset, data.min_tornado_[dimension][col_id] * field_zoom,
      data.max_tornado_[dimension][col_id] * field_zoom,
      tornado_position_scale_to_low[dimension],
      tornado_position_scale_to_high[dimension]);
  float rads = std::acos(position_between);
  float frequency = tornado_offset_frequency[dimension];
  rads += (y_offset + effect_offset) * ((period * frequency) + frequency) /
          SCREEN_HEIGHT;
  float processed_rads =
      is_tan ? SelectTanType(rads, curr_options->cosecant_) : std::cos(rads);

  const float adjusted_pixel_offset = SCALE(
      processed_rads, tornado_offset_scale_from_low[dimension],
      tornado_offset_scale_from_high[dimension],
      data.min_tornado_[dimension][col_id] * field_zoom,
      data.max_tornado_[dimension][col_id] * field_zoom);
  return (adjusted_pixel_offset - real_pixel_offset) * magnitude;
}

static float CalculateDrunkAngle(
    float speed, int col, float offset, float col_frequency, float y_offset,
    float period, float offset_frequency) {
  float time = ArrowEffects::GetTime();
  return time * (1 + speed) + col * ((offset * col_frequency) + col_frequency) +
         y_offset * ((period * offset_frequency) + offset_frequency) /
             SCREEN_HEIGHT;
}

static float CalculateBumpyAngle(float y_offset, float offset, float period) {
  return (y_offset + (100.0f * offset)) / ((period * 16.0f) + 16.0f);
}

static float CalculateDigitalAngle(float y_offset, float offset, float period) {
  return PI * (y_offset + (1.0f * offset)) /
         (ARROW_SIZE + (period * ARROW_SIZE));
}

static void UpdateBeat(
    int dimension, PerPlayerData& data, const SongPosition& position,
    float beat_offset, float beat_mult) {
  float accel_time = 0.2f, total_time = 0.5f;
  float beat =
      ((position.song_beat_visible_ + accel_time + beat_offset) *
       (beat_mult + 1));

  const bool even_beat = (int(beat) % 2) != 0;

  data.beat_factor_[dimension] = 0;
  if (beat < 0) {
    return;
  }

  // -100.2 -> -0.2 -> 0.2
  beat -= std::trunc(beat);
  beat += 1;
  beat -= std::trunc(beat);

  if (beat >= total_time) {
    return;
  }

  if (beat < accel_time) {
    data.beat_factor_[dimension] = SCALE(beat, 0.0f, accel_time, 0.0f, 1.0f);
    data.beat_factor_[dimension] *= data.beat_factor_[dimension];
  } else /* fBeat < fTotalTime */ {
    data.beat_factor_[dimension] =
        SCALE(beat, accel_time, total_time, 1.0f, 0.0f);
    data.beat_factor_[dimension] = 1 - (1 - data.beat_factor_[dimension]) *
                                           (1 - data.beat_factor_[dimension]);
  }

  if (even_beat) {
    data.beat_factor_[dimension] *= -1;
  }
  data.beat_factor_[dimension] *= 20.0f;
}

static void UpdateTipsy(
    float* tipsy_result, float* tipsy_offset_result, float offset, float speed,
    bool is_tan) {
  const float time = ArrowEffects::GetTime();
  const float time_times_timer =
      time * ((speed * TIPSY_TIMER_FREQUENCY) + TIPSY_TIMER_FREQUENCY);
  const float arrow_times_mag = ARROW_SIZE * TIPSY_ARROW_MAGNITUDE;
  const float time_times_offset_timer = time * TIPSY_OFFSET_TIMER_FREQUENCY;
  const float arrow_times_offset_mag =
      ARROW_SIZE * TIPSY_OFFSET_ARROW_MAGNITUDE;
  for (int col = 0; col < MAX_COLS_PER_PLAYER; ++col) {
    if (is_tan) {
      tipsy_result[col] =
          SelectTanType(
              time_times_timer + (col * ((offset * TIPSY_COLUMN_FREQUENCY) +
                                         TIPSY_COLUMN_FREQUENCY)),
              curr_options->cosecant_) *
          arrow_times_mag;
      tipsy_offset_result[col] =
          SelectTanType(
              time_times_offset_timer + (col * TIPSY_OFFSET_COLUMN_FREQUENCY),
              curr_options->cosecant_) *
          arrow_times_offset_mag;
    } else {
      tipsy_result[col] =
          std::cos(
              time_times_timer + (col * ((offset * TIPSY_COLUMN_FREQUENCY) +
                                         TIPSY_COLUMN_FREQUENCY))) *
          arrow_times_mag;
      tipsy_offset_result[col] =
          std::cos(
              time_times_offset_timer + (col * TIPSY_OFFSET_COLUMN_FREQUENCY)) *
          arrow_times_offset_mag;
    }
  }
}

void ArrowEffects::Init(PlayerNumber pn) {
  const Style* style = GAMESTATE->GetCurrentStyle(pn);
  const Style::ColumnInfo* cols = style->column_info_[pn];
  PerPlayerData& data = g_EffectData[pn];
  // Init tornado limits.
  // This used to run every frame, but it doesn't actually depend on anything
  // that changes every frame.  In openitg, it runs for every note. -Kyz

  // TRICKY: Tornado is very unplayable in doubles, so use a smaller
  // tornado width if there are many columns

  /* the wide_field check makes an assumption for dance mode.
   * perhaps check if we are actually playing on singles without,
   * say more than 6 columns. That would exclude IIDX, pop'n, and
   * techno-8, all of which would be very hectic.
   * certain non-singles modes (like halfdoubles 6cols)
   * could possibly have tornado enabled.
   * let's also take default resolution (640x480) into mind. -aj */
  bool wide_field = style->cols_per_player_ > 4;
  int max_player_col = style->cols_per_player_ - 1;
  for (int dimension = 0; dimension < 3; ++dimension) {
    int width = 3;
    // wide_field only matters for x, which is dimension 0. -Kyz
    if (dimension == 0 && wide_field) {
      width = 2;
    }
    for (int col_id = 0; col_id <= max_player_col; ++col_id) {
      int start_col = col_id - width;
      int end_col = col_id + width;
      rage_clamp(start_col, 0, max_player_col);
      rage_clamp(end_col, 0, max_player_col);
      data.min_tornado_[dimension][col_id] = FLT_MAX;
      data.max_tornado_[dimension][col_id] = FLT_MIN;
      for (int i = start_col; i <= end_col; ++i) {
        // Using the x offset when the dimension might be y or z feels so
        // wrong, but it provides min and max values when otherwise the
        // limits would just be zero, which would make it do nothing. -Kyz
        data.min_tornado_[dimension][col_id] =
            std::min(cols[i].x_offset_, data.min_tornado_[dimension][col_id]);
        data.max_tornado_[dimension][col_id] =
            std::max(cols[i].x_offset_, data.max_tornado_[dimension][col_id]);
      }
    }

    tornado_position_scale_to_low[dimension] =
        TORNADO_POSITION_SCALE_TO_LOW.GetValue(dimension);
    tornado_position_scale_to_high[dimension] =
        TORNADO_POSITION_SCALE_TO_HIGH.GetValue(dimension);
    tornado_offset_frequency[dimension] =
        TORNADO_OFFSET_FREQUENCY.GetValue(dimension);
    tornado_offset_scale_from_low[dimension] =
        TORNADO_OFFSET_SCALE_FROM_LOW.GetValue(dimension);
    tornado_offset_scale_from_high[dimension] =
        TORNADO_OFFSET_SCALE_FROM_HIGH.GetValue(dimension);
  }
}

void ArrowEffects::Update() {
  static double last_time = 0.0;
  double time = RageTimer::GetTimeSinceStart();

  FOREACH_EnabledPlayer(pn) {
    const Style* style = GAMESTATE->GetCurrentStyle(pn);
    const Style::ColumnInfo* cols = style->column_info_[pn];
    const SongPosition& position = GAMESTATE->is_using_step_timing_
                                       ? GAMESTATE->player_state_[pn]->position_
                                       : GAMESTATE->position_;
    const float* effects =
        GAMESTATE->player_state_[pn]->player_options_.GetCurrent().effects_;
    const float* accels =
        GAMESTATE->player_state_[pn]->player_options_.GetCurrent().accels_;

    PerPlayerData& data = g_EffectData[pn];

    if (style != data.prev_style_) {
      Init(pn);
      data.prev_style_ = style;
    }

    if (!position.freeze_ || !position.delay_) {
      data.expand_seconds_ += static_cast<float>(time - last_time);
      data.expand_seconds_ = std::fmod(
          data.expand_seconds_,
          (PI * 2) / (accels[PlayerOptions::ACCEL_EXPAND_PERIOD] + 1));
      data.tan_expand_seconds_ += static_cast<float>(time - last_time);
      data.tan_expand_seconds_ = std::fmod(
          data.tan_expand_seconds_,
          (PI * 2) / (accels[PlayerOptions::ACCEL_TAN_EXPAND_PERIOD] + 1));
    }

    // Update Invert
    for (int col_num = 0; col_num < MAX_COLS_PER_PLAYER; ++col_num) {
      const int num_cols = style->cols_per_player_;
      const int num_sides =
          (style->style_type_ == StyleType_OnePlayerTwoSides ||
           style->style_type_ == StyleType_TwoPlayersSharedSides)
              ? 2
              : 1;
      const int num_cols_per_side = num_cols / num_sides;
      const int side_index = col_num / num_cols_per_side;
      const int col_on_side = col_num % num_cols_per_side;

      const int col_left_of_middle = (num_cols_per_side - 1) / 2;
      const int col_right_of_middle = (num_cols_per_side + 1) / 2;

      int first_col_on_side = -1;
      int last_col_on_side = -1;
      if (col_on_side <= col_left_of_middle) {
        first_col_on_side = 0;
        last_col_on_side = col_left_of_middle;
      } else if (col_on_side >= col_right_of_middle) {
        first_col_on_side = col_right_of_middle;
        last_col_on_side = num_cols_per_side - 1;
      } else {
        first_col_on_side = col_on_side / 2;
        last_col_on_side = col_on_side / 2;
      }

      // mirror
      int new_col_on_side;
      if (first_col_on_side == last_col_on_side) {
        new_col_on_side = 0;
      } else {
        new_col_on_side = SCALE(
            col_on_side, first_col_on_side, last_col_on_side, last_col_on_side,
            first_col_on_side);
      }
      const int new_col = side_index * num_cols_per_side + new_col_on_side;

      const float old_pixel_offset = cols[col_num].x_offset_;
      const float new_pixel_offset = cols[new_col].x_offset_;
      data.invert_distance_[col_num] = new_pixel_offset - old_pixel_offset;
    }

    // Update Tipsy
    if (effects[PlayerOptions::EFFECT_TIPSY] != 0) {
      UpdateTipsy(
          data.tipsy_result_, data.tipsy_offset_result_,
          effects[PlayerOptions::EFFECT_TIPSY_OFFSET],
          effects[PlayerOptions::EFFECT_TIPSY_SPEED], false);
    } else {
      for (int col = 0; col < MAX_COLS_PER_PLAYER; ++col) {
        data.tipsy_result_[col] = 0;
      }
    }

    // Update TanTipsy
    if (effects[PlayerOptions::EFFECT_TAN_TIPSY] != 0) {
      UpdateTipsy(
          data.tan_tipsy_result_, data.tan_tipsy_offset_result_,
          effects[PlayerOptions::EFFECT_TAN_TIPSY_OFFSET],
          effects[PlayerOptions::EFFECT_TAN_TIPSY_SPEED], true);
    } else {
      for (int col = 0; col < MAX_COLS_PER_PLAYER; ++col) {
        data.tan_tipsy_result_[col] = 0;
      }
    }

    // Update Beat
    UpdateBeat(
        dim_x, data, position, effects[PlayerOptions::EFFECT_BEAT_OFFSET],
        effects[PlayerOptions::EFFECT_BEAT_MULT]);

    // Update BeatY
    UpdateBeat(
        dim_y, data, position, effects[PlayerOptions::EFFECT_BEAT_Y_OFFSET],
        effects[PlayerOptions::EFFECT_BEAT_Y_MULT]);

    // Update BeatZ
    UpdateBeat(
        dim_z, data, position, effects[PlayerOptions::EFFECT_BEAT_Z_OFFSET],
        effects[PlayerOptions::EFFECT_BEAT_Z_MULT]);
  }
  last_time = time;
}

void ArrowEffects::SetCurrentOptions(const PlayerOptions* options) {
  curr_options = options;
}

static float GetDisplayedBeat(const PlayerState* player_state, float beat) {
  // do a binary search here
  const std::vector<CacheDisplayedBeat>& data =
      player_state->cache_displayed_beat_;
  int max = data.size() - 1;
  int l = 0, r = max;
  while (l <= r) {
    int m = (l + r) / 2;
    if ((m == 0 || data[m].beat_ <= beat) &&
        (m == max || beat < data[m + 1].beat_)) {
      return data[m].displayed_beat_ +
             data[m].velocity_ * (beat - data[m].beat_);
    } else if (data[m].beat_ <= beat) {
      l = m + 1;
    } else {
      r = m - 1;
    }
  }
  return beat;
}

/* For visibility testing: if bAbsolute is false, random modifiers must return
 * the minimum possible scroll speed. */
float ArrowEffects::GetYOffset(
    const PlayerState* player_state, int col, float note_beat,
    float& peak_y_offset_out, bool& is_past_peak_out, bool absolute) {
  // Default values that are returned if boomerang is off.
  peak_y_offset_out = FLT_MAX;
  is_past_peak_out = true;

  float y_offset = 0;
  const SongPosition& position = player_state->GetDisplayedPosition();

  float song_beat = position.song_beat_visible_;

  Steps* cur_steps = GAMESTATE->cur_steps_[player_state->player_number_];

  /* Usually, fTimeSpacing is 0 or 1, in which case we use entirely beat spacing
   * or entirely time spacing (respectively). Occasionally, we tween between
   * them. */
  if (curr_options->time_spacing_ != 1.0f) {
    if (GAMESTATE->in_step_editor_) {
      // Use constant spacing in step editor
      y_offset = note_beat - song_beat;
    } else {
      y_offset = GetDisplayedBeat(player_state, note_beat) -
                 GetDisplayedBeat(player_state, song_beat);
      y_offset *= cur_steps->GetTimingData()->GetDisplayedSpeedPercent(
          position.song_beat_visible_, position.music_seconds_visible_);
    }
    y_offset *= 1 - curr_options->time_spacing_;
  }

  if (curr_options->time_spacing_ != 0.0f) {
    float song_seconds = player_state->position_.music_seconds_visible_;
    float note_seconds =
        cur_steps->GetTimingData()->GetElapsedTimeFromBeat(note_beat);
    float seconds_until_step = note_seconds - song_seconds;
    float bpm = curr_options->scroll_bpm_;
    float bps = bpm / 60.f / GAMESTATE->song_options_.GetCurrent().music_rate_;
    float y_offset_time_spacing = seconds_until_step * bps;
    y_offset += y_offset_time_spacing * curr_options->time_spacing_;
  }

  // TODO: If we allow noteskins to have metricable row spacing
  // (per issue 24), edit this to reflect that. -aj
  y_offset *= ARROW_SPACING;

  // Factor in scroll speed
  float scroll_speed = curr_options->scroll_speed_;
  if (curr_options->max_scroll_bpm_ != 0) {
    scroll_speed = curr_options->max_scroll_bpm_ /
                   (player_state->read_bpm_ *
                    GAMESTATE->song_options_.GetCurrent().music_rate_);
  }

  // don't mess with the arrows after they've crossed 0
  if (y_offset < 0) {
    return y_offset * scroll_speed;
  }

  const float* accels = curr_options->accels_;
  const float* effects = curr_options->effects_;

  // TODO: Don't index by PlayerNumber.
  PerPlayerData& data = g_EffectData[player_state->player_number_];

  float y_adjust = 0;  // fill this in depending on PlayerOptions

  if (accels[PlayerOptions::ACCEL_BOOST] != 0) {
    float effect_height = GetNoteFieldHeight();
    float new_y_offset =
        y_offset * 1.5f / ((y_offset + effect_height / 1.2f) / effect_height);
    float accel_y_adjust =
        accels[PlayerOptions::ACCEL_BOOST] * (new_y_offset - y_offset);
    // TRICKY: Clamp this value, or else BOOST+BOOMERANG will draw a ton of
    // arrows on the screen.
    rage_clamp(accel_y_adjust, BOOST_MOD_MIN_CLAMP, BOOST_MOD_MAX_CLAMP);
    y_adjust += accel_y_adjust;
  }
  if (accels[PlayerOptions::ACCEL_BRAKE] != 0) {
    float effect_height = GetNoteFieldHeight();
    float scale = SCALE(y_offset, 0.f, effect_height, 0, 1.f);
    float new_y_offset = y_offset * scale;
    float brake_y_adjust =
        accels[PlayerOptions::ACCEL_BRAKE] * (new_y_offset - y_offset);
    // TRICKY: Clamp this value the same way as BOOST so that in BOOST+BRAKE,
    // BRAKE doesn't overpower BOOST
    rage_clamp(brake_y_adjust, BRAKE_MOD_MIN_CLAMP, BRAKE_MOD_MAX_CLAMP);
    y_adjust += brake_y_adjust;
  }
  if (accels[PlayerOptions::ACCEL_WAVE] != 0) {
    y_adjust +=
        accels[PlayerOptions::ACCEL_WAVE] * WAVE_MOD_MAGNITUDE *
        std::sin(
            y_offset /
            ((accels[PlayerOptions::ACCEL_WAVE_PERIOD] * WAVE_MOD_HEIGHT) +
             WAVE_MOD_HEIGHT));
  }

  if (effects[PlayerOptions::EFFECT_PARABOLA_Y] != 0) {
    y_adjust += effects[PlayerOptions::EFFECT_PARABOLA_Y] *
                (y_offset / ARROW_SIZE) * (y_offset / ARROW_SIZE);
  }

  y_offset += y_adjust;

  // Factor in boomerang
  if (accels[PlayerOptions::ACCEL_BOOMERANG] != 0) {
    float peak_at_y_offset =
        SCREEN_HEIGHT *
        BOOMERANG_PEAK_PERCENTAGE;  // zero point of boomerang function
    peak_y_offset_out =
        (-1 * peak_at_y_offset * peak_at_y_offset / SCREEN_HEIGHT) +
        1.5f * peak_at_y_offset;
    is_past_peak_out = y_offset < peak_at_y_offset;

    y_offset = (-1 * y_offset * y_offset / SCREEN_HEIGHT) + 1.5f * y_offset;
  }

  if (curr_options->random_speed_ > 0 && !absolute) {
    // Generate a deterministically "random" speed for each arrow.
    unsigned seed =
        GAMESTATE->stage_seed_ + (BeatToNoteRow(note_beat) << 8) + (col * 100);

    for (int i = 0; i < 3; ++i) {
      seed = ((seed * 1664525u) + 1013904223u) & 0xFFFFFFFF;
    }
    float random = seed / 4294967296.0f;

    /* Random speed always increases speed: a random speed of 10 indicates
     * [1,11]. This keeps it consistent with other mods: 0 means no effect. */
    scroll_speed *=
        SCALE(random, 0.0f, 1.0f, 1.0f, curr_options->random_speed_ + 1.0f);
  }

  if (accels[PlayerOptions::ACCEL_EXPAND] != 0) {
    float expand_multiplier = SCALE(
        std::cos(
            data.expand_seconds_ * EXPAND_MULTIPLIER_FREQUENCY *
            (accels[PlayerOptions::ACCEL_EXPAND_PERIOD] + 1)),
        EXPAND_MULTIPLIER_SCALE_FROM_LOW, EXPAND_MULTIPLIER_SCALE_FROM_HIGH,
        EXPAND_MULTIPLIER_SCALE_TO_LOW, EXPAND_MULTIPLIER_SCALE_TO_HIGH);
    scroll_speed *= SCALE(
        accels[PlayerOptions::ACCEL_EXPAND], EXPAND_SPEED_SCALE_FROM_LOW,
        EXPAND_SPEED_SCALE_FROM_HIGH, EXPAND_SPEED_SCALE_TO_LOW,
        expand_multiplier);
  }

  if (accels[PlayerOptions::ACCEL_TAN_EXPAND] != 0) {
    float tan_expand_multiplier = SCALE(
        SelectTanType(
            data.tan_expand_seconds_ * EXPAND_MULTIPLIER_FREQUENCY *
                (accels[PlayerOptions::ACCEL_TAN_EXPAND_PERIOD] + 1),
            curr_options->cosecant_),
        EXPAND_MULTIPLIER_SCALE_FROM_LOW, EXPAND_MULTIPLIER_SCALE_FROM_HIGH,
        EXPAND_MULTIPLIER_SCALE_TO_LOW, EXPAND_MULTIPLIER_SCALE_TO_HIGH);
    scroll_speed *= SCALE(
        accels[PlayerOptions::ACCEL_TAN_EXPAND], EXPAND_SPEED_SCALE_FROM_LOW,
        EXPAND_SPEED_SCALE_FROM_HIGH, EXPAND_SPEED_SCALE_TO_LOW,
        tan_expand_multiplier);
  }

  y_offset *= scroll_speed;
  peak_y_offset_out *= scroll_speed;

  return y_offset;
}

static void ArrowGetReverseShiftAndScale(
    int col, float y_reverse_offset_pixels, float& shift_out,
    float& scale_out) {
  // XXX: Hack: we need to scale the reverse shift by the zoom.
  float mini_percent = curr_options->effects_[PlayerOptions::EFFECT_MINI];
  float zoom = 1 - mini_percent * 0.5f;

  // don't divide by 0
  if (std::abs(zoom) < 0.01) {
    zoom = 0.01f;
  }

  float percent_reverse = curr_options->GetReversePercentForColumn(col);
  shift_out = SCALE(
      percent_reverse, 0.f, 1.f, -y_reverse_offset_pixels / zoom / 2,
      y_reverse_offset_pixels / zoom / 2);
  float percent_centered =
      curr_options->scrolls_[PlayerOptions::SCROLL_CENTERED];
  shift_out = SCALE(percent_centered, 0.f, 1.f, shift_out, 0.0f);

  scale_out = SCALE(percent_reverse, 0.f, 1.f, 1.f, -1.f);
}

float ArrowEffects::GetYPos(
    const PlayerState* player_state, int col, float y_offset,
    float y_reverse_offset_pixels, bool with_reverse) {
  float f = y_offset;

  if (with_reverse) {
    float shift, scale;
    ArrowGetReverseShiftAndScale(col, y_reverse_offset_pixels, shift, scale);

    f *= scale;
    f += shift;
  }

  // TODO: Don't index by PlayerNumber.
  const Style* style = GAMESTATE->GetCurrentStyle(player_state->player_number_);
  const Style::ColumnInfo* cols =
      style->column_info_[player_state->player_number_];
  const float* effects = curr_options->effects_;

  // Doing the math with a precalculated result of 0 should be faster than
  // checking whether tipsy is on. -Kyz
  // TODO: Don't index by PlayerNumber.
  PerPlayerData& data = g_EffectData[curr_options->pn_];
  f += effects[PlayerOptions::EFFECT_TIPSY] * data.tipsy_result_[col];
  f += effects[PlayerOptions::EFFECT_TAN_TIPSY] * data.tan_tipsy_result_[col];

  if (effects[PlayerOptions::EFFECT_ATTENUATE_Y] != 0) {
    const float x_offset = cols[col].x_offset_;
    f += effects[PlayerOptions::EFFECT_ATTENUATE_Y] * (y_offset / ARROW_SIZE) *
         (y_offset / ARROW_SIZE) * (x_offset / ARROW_SIZE);
  }

  if (effects[PlayerOptions::EFFECT_BEAT_Y] != 0) {
    const float shift =
        data.beat_factor_[dim_y] *
        std::sin(
            y_offset / ((effects[PlayerOptions::EFFECT_BEAT_Y_PERIOD] *
                         BEAT_Y_OFFSET_HEIGHT) +
                        BEAT_Y_OFFSET_HEIGHT) +
            PI / BEAT_Y_PI_HEIGHT);
    f += effects[PlayerOptions::EFFECT_BEAT_Y] * shift;
  }

  // In beware's DDR Extreme-focused fork of StepMania 3.9, this value is
  // floored, making arrows show on integer Y coordinates. Supposedly it makes
  // the arrows look better, but testing needs to be done.
  // todo: make this a noteskin metric instead of a theme metric? -aj
  return QUANTIZE_ARROW_Y ? std::floor(f) : f;
}

float ArrowEffects::GetYOffsetFromYPos(
    int col, float y_pos, float y_reverse_offset_pixels) {
  float f = y_pos;

  const float* effects = curr_options->effects_;
  // Doing the math with a precalculated result of 0 should be faster than
  // checking whether tipsy is on. -Kyz
  // TODO: Don't index by PlayerNumber.
  PerPlayerData& data = g_EffectData[curr_options->pn_];
  f += effects[PlayerOptions::EFFECT_TIPSY] * data.tipsy_offset_result_[col];
  f += effects[PlayerOptions::EFFECT_TAN_TIPSY] *
       data.tan_tipsy_offset_result_[col];

  f += effects[PlayerOptions::EFFECT_PARABOLA_Y] * (y_pos / ARROW_SIZE) *
       (y_pos / ARROW_SIZE);

  float shift, scale;
  ArrowGetReverseShiftAndScale(col, y_reverse_offset_pixels, shift, scale);

  f -= shift;
  if (scale) {
    f /= scale;
  }

  return f;
}

float ArrowEffects::GetXPos(
    const PlayerState* player_state, int col_num, float y_offset) {
  float pixel_offset_from_center = 0;  // fill this in below

  const Style* style = GAMESTATE->GetCurrentStyle(player_state->player_number_);
  const float* effects = curr_options->effects_;

  // TODO: Don't index by PlayerNumber.
  const Style::ColumnInfo* cols =
      style->column_info_[player_state->player_number_];
  PerPlayerData& data = g_EffectData[player_state->player_number_];

  if (effects[PlayerOptions::EFFECT_TORNADO] != 0) {
    pixel_offset_from_center += CalculateTornadoOffsetFromMagnitude(
        dim_x, col_num, effects[PlayerOptions::EFFECT_TORNADO],
        effects[PlayerOptions::EFFECT_TORNADO_OFFSET],
        effects[PlayerOptions::EFFECT_TORNADO_PERIOD], cols,
        player_state->notefield_zoom_, data, y_offset, false);
  }

  if (effects[PlayerOptions::EFFECT_TAN_TORNADO] != 0) {
    pixel_offset_from_center += CalculateTornadoOffsetFromMagnitude(
        dim_x, col_num, effects[PlayerOptions::EFFECT_TAN_TORNADO],
        effects[PlayerOptions::EFFECT_TAN_TORNADO_OFFSET],
        effects[PlayerOptions::EFFECT_TAN_TORNADO_PERIOD], cols,
        player_state->notefield_zoom_, data, y_offset, true);
  }

  if (effects[PlayerOptions::EFFECT_BUMPY_X] != 0) {
    pixel_offset_from_center +=
        effects[PlayerOptions::EFFECT_BUMPY_X] * 40 *
        std::sin(CalculateBumpyAngle(
            y_offset, effects[PlayerOptions::EFFECT_BUMPY_X_OFFSET],
            effects[PlayerOptions::EFFECT_BUMPY_X_PERIOD]));
  }

  if (effects[PlayerOptions::EFFECT_TAN_BUMPY_X] != 0) {
    pixel_offset_from_center +=
        effects[PlayerOptions::EFFECT_TAN_BUMPY_X] * 40 *
        SelectTanType(
            CalculateBumpyAngle(
                y_offset, effects[PlayerOptions::EFFECT_TAN_BUMPY_X_OFFSET],
                effects[PlayerOptions::EFFECT_TAN_BUMPY_X_PERIOD]),
            curr_options->cosecant_);
  }

  if (effects[PlayerOptions::EFFECT_DRUNK] != 0) {
    pixel_offset_from_center +=
        effects[PlayerOptions::EFFECT_DRUNK] *
        (std::cos(CalculateDrunkAngle(
             effects[PlayerOptions::EFFECT_DRUNK_SPEED], col_num,
             effects[PlayerOptions::EFFECT_DRUNK_OFFSET],
             DRUNK_COLUMN_FREQUENCY, y_offset,
             effects[PlayerOptions::EFFECT_DRUNK_PERIOD],
             DRUNK_OFFSET_FREQUENCY)) *
         ARROW_SIZE * DRUNK_ARROW_MAGNITUDE);
  }

  if (effects[PlayerOptions::EFFECT_TAN_DRUNK] != 0) {
    pixel_offset_from_center +=
        effects[PlayerOptions::EFFECT_TAN_DRUNK] *
        (SelectTanType(
             CalculateDrunkAngle(
                 effects[PlayerOptions::EFFECT_TAN_DRUNK_SPEED], col_num,
                 effects[PlayerOptions::EFFECT_TAN_DRUNK_OFFSET],
                 DRUNK_COLUMN_FREQUENCY, y_offset,
                 effects[PlayerOptions::EFFECT_TAN_DRUNK_PERIOD],
                 DRUNK_OFFSET_FREQUENCY),
             curr_options->cosecant_) *
         ARROW_SIZE * DRUNK_ARROW_MAGNITUDE);
  }

  if (effects[PlayerOptions::EFFECT_FLIP] != 0) {
    const int first_col = 0;
    const int last_col = style->cols_per_player_ - 1;
    const int new_col =
        SCALE(col_num, first_col, last_col, last_col, first_col);
    const float old_pixel_offset =
        cols[col_num].x_offset_ * player_state->notefield_zoom_;
    const float new_pixel_offset =
        cols[new_col].x_offset_ * player_state->notefield_zoom_;
    const float distance = new_pixel_offset - old_pixel_offset;
    pixel_offset_from_center += distance * effects[PlayerOptions::EFFECT_FLIP];
  }
  if (effects[PlayerOptions::EFFECT_INVERT] != 0) {
    pixel_offset_from_center +=
        data.invert_distance_[col_num] * effects[PlayerOptions::EFFECT_INVERT];
  }

  if (effects[PlayerOptions::EFFECT_BEAT] != 0) {
    const float shift =
        data.beat_factor_[dim_x] *
        std::sin(
            y_offset / ((effects[PlayerOptions::EFFECT_BEAT_PERIOD] *
                         BEAT_OFFSET_HEIGHT) +
                        BEAT_OFFSET_HEIGHT) +
            PI / BEAT_PI_HEIGHT);
    pixel_offset_from_center += effects[PlayerOptions::EFFECT_BEAT] * shift;
  }

  if (effects[PlayerOptions::EFFECT_ZIGZAG] != 0) {
    float result = RageTriangle(
        (PI * (1 / (effects[PlayerOptions::EFFECT_ZIGZAG_PERIOD] + 1)) *
         ((y_offset +
           (100.0f * (effects[PlayerOptions::EFFECT_ZIGZAG_OFFSET]))) /
          ARROW_SIZE)));

    pixel_offset_from_center +=
        (effects[PlayerOptions::EFFECT_ZIGZAG] * ARROW_SIZE / 2) * result;
  }

  if (effects[PlayerOptions::EFFECT_SAWTOOTH] != 0) {
    pixel_offset_from_center +=
        (effects[PlayerOptions::EFFECT_SAWTOOTH] * ARROW_SIZE) *
        ((0.5f / (effects[PlayerOptions::EFFECT_SAWTOOTH_PERIOD] + 1) *
          y_offset) /
             ARROW_SIZE -
         std::floor(
             (0.5f / (effects[PlayerOptions::EFFECT_SAWTOOTH_PERIOD] + 1) *
              y_offset) /
             ARROW_SIZE));
  }

  if (effects[PlayerOptions::EFFECT_PARABOLA_X] != 0) {
    pixel_offset_from_center += effects[PlayerOptions::EFFECT_PARABOLA_X] *
                                (y_offset / ARROW_SIZE) *
                                (y_offset / ARROW_SIZE);
  }

  if (effects[PlayerOptions::EFFECT_ATTENUATE_X] != 0) {
    const float x_offset = cols[col_num].x_offset_;
    pixel_offset_from_center +=
        effects[PlayerOptions::EFFECT_ATTENUATE_X] * (y_offset / ARROW_SIZE) *
        (y_offset / ARROW_SIZE) * (x_offset / ARROW_SIZE);
  }

  if (effects[PlayerOptions::EFFECT_DIGITAL] != 0) {
    pixel_offset_from_center +=
        (effects[PlayerOptions::EFFECT_DIGITAL] * ARROW_SIZE * 0.5f) *
        std::round(
            (effects[PlayerOptions::EFFECT_DIGITAL_STEPS] + 1) *
            std::sin(CalculateDigitalAngle(
                y_offset, effects[PlayerOptions::EFFECT_DIGITAL_OFFSET],
                effects[PlayerOptions::EFFECT_DIGITAL_PERIOD]))) /
        (effects[PlayerOptions::EFFECT_DIGITAL_STEPS] + 1);
  }

  if (effects[PlayerOptions::EFFECT_TAN_DIGITAL] != 0) {
    pixel_offset_from_center +=
        (effects[PlayerOptions::EFFECT_TAN_DIGITAL] * ARROW_SIZE * 0.5f) *
        std::round(
            (effects[PlayerOptions::EFFECT_TAN_DIGITAL_STEPS] + 1) *
            SelectTanType(
                CalculateDigitalAngle(
                    y_offset, effects[PlayerOptions::EFFECT_TAN_DIGITAL_OFFSET],
                    effects[PlayerOptions::EFFECT_TAN_DIGITAL_PERIOD]),
                curr_options->cosecant_)) /
        (effects[PlayerOptions::EFFECT_TAN_DIGITAL_STEPS] + 1);
  }

  if (effects[PlayerOptions::EFFECT_SQUARE] != 0) {
    float result = RageSquare(
        (PI *
         (y_offset + (1.0f * (effects[PlayerOptions::EFFECT_SQUARE_OFFSET]))) /
         (ARROW_SIZE +
          (effects[PlayerOptions::EFFECT_SQUARE_PERIOD] * ARROW_SIZE))));

    pixel_offset_from_center +=
        (effects[PlayerOptions::EFFECT_SQUARE] * ARROW_SIZE * 0.5f) * result;
  }

  if (effects[PlayerOptions::EFFECT_BOUNCE] != 0) {
    float bounce_amt = std::abs(
        std::sin(
            ((y_offset +
              (1.0f * (effects[PlayerOptions::EFFECT_BOUNCE_OFFSET]))) /
             (60 + (effects[PlayerOptions::EFFECT_BOUNCE_PERIOD] * 60)))));

    pixel_offset_from_center +=
        effects[PlayerOptions::EFFECT_BOUNCE] * ARROW_SIZE * 0.5f * bounce_amt;
  }

  if (effects[PlayerOptions::EFFECT_XMODE] != 0) {
    // based off of code by v1toko for StepNXA, except it should work on
    // any gametype now.
    switch (style->style_type_) {
      case StyleType_OnePlayerTwoSides:
      case StyleType_TwoPlayersSharedSides: {
        // find the middle, and split based on iColNum
        // it's unknown if this will work for routine.
        const int middle_column = std::floor(style->cols_per_player_ / 2.0f);
        if (col_num > middle_column - 1) {
          pixel_offset_from_center +=
              effects[PlayerOptions::EFFECT_XMODE] * -(y_offset);
        } else {
          pixel_offset_from_center +=
              effects[PlayerOptions::EFFECT_XMODE] * y_offset;
        }
      } break;
      case StyleType_OnePlayerOneSide:
      case StyleType_TwoPlayersTwoSides: {
        // the code was the same for both of these cases in StepNXA.
        if (player_state->player_number_ == PLAYER_2) {
          pixel_offset_from_center +=
              effects[PlayerOptions::EFFECT_XMODE] * -(y_offset);
        } else {
          pixel_offset_from_center +=
              effects[PlayerOptions::EFFECT_XMODE] * y_offset;
        }
      } break;
        DEFAULT_FAIL(style->style_type_);
    }
  }

  pixel_offset_from_center +=
      cols[col_num].x_offset_ * player_state->notefield_zoom_;

  if (effects[PlayerOptions::EFFECT_TINY] != 0) {
    // Allow Tiny to pull tracks together, but not to push them apart.
    float tiny_percent = effects[PlayerOptions::EFFECT_TINY];
    tiny_percent = std::min(
        std::pow(TINY_PERCENT_BASE, tiny_percent), (float)TINY_PERCENT_GATE);
    pixel_offset_from_center *= tiny_percent;
  }

  return pixel_offset_from_center;
}

float ArrowEffects::GetRotationX(
    const PlayerState* player_state, float y_offset, bool is_hold_cap,
    int col) {
  const float* effects = curr_options->effects_;
  float rotation = 0;
  if (effects[PlayerOptions::EFFECT_CONFUSION_X] != 0 ||
      effects[PlayerOptions::EFFECT_CONFUSION_X_OFFSET] != 0 ||
      curr_options->confusion_x_[col] != 0) {
    rotation += ReceptorGetRotationX(player_state, col);
  }
  if (effects[PlayerOptions::EFFECT_ROLL] != 0 && !is_hold_cap) {
    rotation += effects[PlayerOptions::EFFECT_ROLL] * y_offset / 2;
  }
  return rotation;
}

float ArrowEffects::GetRotationY(
    const PlayerState* player_state, float y_offset, int col) {
  const float* effects = curr_options->effects_;
  float rotation = 0;
  if (effects[PlayerOptions::EFFECT_CONFUSION_Y] != 0 ||
      effects[PlayerOptions::EFFECT_CONFUSION_Y_OFFSET] != 0 ||
      curr_options->confusion_y_[col] != 0) {
    rotation += ReceptorGetRotationY(player_state, col);
  }
  if (effects[PlayerOptions::EFFECT_TWIRL] != 0) {
    rotation += effects[PlayerOptions::EFFECT_TWIRL] * y_offset / 2;
  }
  return rotation;
}

float ArrowEffects::GetRotationZ(
    const PlayerState* player_state, float note_beat, bool is_hold_head,
    int col) {
  const float* effects = curr_options->effects_;
  float rotation = 0;
  if (effects[PlayerOptions::EFFECT_CONFUSION] != 0 ||
      effects[PlayerOptions::EFFECT_CONFUSION_OFFSET] != 0 ||
      curr_options->confusion_z_[col] != 0) {
    rotation += ReceptorGetRotationZ(player_state, col);
  }

  // As usual, enable dizzy hold heads at your own risk. -Wolfman2000
  if (effects[PlayerOptions::EFFECT_DIZZY] != 0 &&
      (curr_options->dizzy_holds_ || !is_hold_head)) {
    const float song_beat = player_state->position_.song_beat_visible_;
    float dizzy_rotation = note_beat - song_beat;
    dizzy_rotation *= effects[PlayerOptions::EFFECT_DIZZY];
    dizzy_rotation = std::fmod(dizzy_rotation, 2 * PI);
    dizzy_rotation *= 180 / PI;
    rotation += dizzy_rotation;
  }
  return rotation;
}

float ArrowEffects::ReceptorGetRotationZ(
    const PlayerState* player_state, int col) {
  const float* effects = curr_options->effects_;
  float rotation = 0;

  if (curr_options->confusion_z_[col] != 0) {
    rotation += curr_options->confusion_z_[col] * 180.0f / PI;
  }

  if (effects[PlayerOptions::EFFECT_CONFUSION_OFFSET] != 0) {
    rotation += effects[PlayerOptions::EFFECT_CONFUSION_OFFSET] * 180.0f / PI;
  }

  if (effects[PlayerOptions::EFFECT_CONFUSION] != 0) {
    float conf_rotation = player_state->position_.song_beat_visible_;
    conf_rotation *= effects[PlayerOptions::EFFECT_CONFUSION];
    conf_rotation = std::fmod(conf_rotation, 2 * PI);
    conf_rotation *= -180 / PI;
    rotation += conf_rotation;
  }

  return rotation;
}

float ArrowEffects::ReceptorGetRotationX(
    const PlayerState* player_state, int col) {
  const float* effects = curr_options->effects_;
  float rotation = 0;

  if (curr_options->confusion_x_[col] != 0) {
    rotation += curr_options->confusion_x_[col] * 180.0f / PI;
  }

  if (effects[PlayerOptions::EFFECT_CONFUSION_X_OFFSET] != 0) {
    rotation += effects[PlayerOptions::EFFECT_CONFUSION_X_OFFSET] * 180.0f / PI;
  }

  if (effects[PlayerOptions::EFFECT_CONFUSION_X] != 0) {
    float conf_rotation = player_state->position_.song_beat_visible_;
    conf_rotation *= effects[PlayerOptions::EFFECT_CONFUSION_X];
    conf_rotation = std::fmod(conf_rotation, 2 * PI);
    conf_rotation *= -180 / PI;
    rotation += conf_rotation;
  }

  return rotation;
}

float ArrowEffects::ReceptorGetRotationY(
    const PlayerState* player_state, int col) {
  const float* effects = curr_options->effects_;
  float rotation = 0;

  if (curr_options->confusion_y_[col] != 0) {
    rotation += curr_options->confusion_y_[col] * 180.0f / PI;
  }

  if (effects[PlayerOptions::EFFECT_CONFUSION_Y_OFFSET] != 0) {
    rotation += effects[PlayerOptions::EFFECT_CONFUSION_Y_OFFSET] * 180.0f / PI;
  }

  if (effects[PlayerOptions::EFFECT_CONFUSION_Y] != 0) {
    float conf_rotation = player_state->position_.song_beat_visible_;
    conf_rotation *= effects[PlayerOptions::EFFECT_CONFUSION_Y];
    conf_rotation = std::fmod(conf_rotation, 2 * PI);
    conf_rotation *= -180 / PI;
    rotation += conf_rotation;
  }

  return rotation;
}

float ArrowEffects::GetMoveX(int col) {
  const float* moves = curr_options->moves_x_;
  float f = 0;
  if (moves[col] != 0) {
    f += ARROW_SIZE * moves[col];
  }
  return f;
}

float ArrowEffects::GetMoveY(int col) {
  const float* moves = curr_options->moves_y_;
  float f = 0;
  if (moves[col] != 0) {
    f += ARROW_SIZE * moves[col];
  }
  return f;
}

float ArrowEffects::GetMoveZ(int col) {
  const float* moves = curr_options->moves_z_;
  float f = 0;
  if (moves[col] != 0) {
    f += ARROW_SIZE * moves[col];
  }
  return f;
}

#define CENTER_LINE_Y 160  // from fYOffset == 0
#define FADE_DIST_Y 40

static float GetCenterLine() {
  /* Another mini hack: if EFFECT_MINI is on, then our center line is at
   * eg. 320, not 160. */
  const float mini_percent = curr_options->effects_[PlayerOptions::EFFECT_MINI];
  const float zoom = 1 - mini_percent * 0.5f;
  return CENTER_LINE_Y / zoom;
}

static float GetHiddenSudden() {
  const float* appearances = curr_options->appearances_;
  return appearances[PlayerOptions::APPEARANCE_HIDDEN] *
         appearances[PlayerOptions::APPEARANCE_SUDDEN];
}

//
//  -gray arrows-
//
//  ...invisible...
//  -hidden end line-
//  -hidden start line-
//  ...visible...
//  -sudden end line-
//  -sudden start line-
//  ...invisible...
//
// TRICKY:  We fudge hidden and sudden to be farther apart if they're both on.
static float GetHiddenEndLine() {
  return GetCenterLine() +
         FADE_DIST_Y * SCALE(GetHiddenSudden(), 0.f, 1.f, -1.0f, -1.25f) +
         GetCenterLine() *
             curr_options
                 ->appearances_[PlayerOptions::APPEARANCE_HIDDEN_OFFSET];
}

static float GetHiddenStartLine() {
  return GetCenterLine() +
         FADE_DIST_Y * SCALE(GetHiddenSudden(), 0.f, 1.f, +0.0f, -0.25f) +
         GetCenterLine() *
             curr_options
                 ->appearances_[PlayerOptions::APPEARANCE_HIDDEN_OFFSET];
}

static float GetSuddenEndLine() {
  return GetCenterLine() +
         FADE_DIST_Y * SCALE(GetHiddenSudden(), 0.f, 1.f, -0.0f, +0.25f) +
         GetCenterLine() *
             curr_options
                 ->appearances_[PlayerOptions::APPEARANCE_SUDDEN_OFFSET];
}

static float GetSuddenStartLine() {
  return GetCenterLine() +
         FADE_DIST_Y * SCALE(GetHiddenSudden(), 0.f, 1.f, +1.0f, +1.25f) +
         GetCenterLine() *
             curr_options
                 ->appearances_[PlayerOptions::APPEARANCE_SUDDEN_OFFSET];
}

// used by ArrowGetAlpha and ArrowGetGlow below
float ArrowGetPercentVisible(
    float y_pos_without_reverse, int col, float y_offset) {
  const float dist_from_center_line = y_pos_without_reverse - GetCenterLine();

  float y_pos;
  if (curr_options->stealth_type_) {
    y_pos = y_offset;
  } else {
    y_pos = y_pos_without_reverse;
  }

  if (y_pos < 0 &&
      curr_options->stealth_past_receptors_ == false) {  // past Gray Arrows
    return 1;                                            // totally visible
  }

  const float* appearances = curr_options->appearances_;

  float visible_adjust = 0;

  if (appearances[PlayerOptions::APPEARANCE_HIDDEN] != 0) {
    float hidden_visible_adjust =
        SCALE(y_pos, GetHiddenStartLine(), GetHiddenEndLine(), 0, -1);
    rage_clamp(hidden_visible_adjust, -1, 0);
    visible_adjust +=
        appearances[PlayerOptions::APPEARANCE_HIDDEN] * hidden_visible_adjust;
  }
  if (appearances[PlayerOptions::APPEARANCE_SUDDEN] != 0) {
    float sudden_visible_adjust =
        SCALE(y_pos, GetSuddenStartLine(), GetSuddenEndLine(), -1, 0);
    rage_clamp(sudden_visible_adjust, -1, 0);
    visible_adjust +=
        appearances[PlayerOptions::APPEARANCE_SUDDEN] * sudden_visible_adjust;
  }

  if (appearances[PlayerOptions::APPEARANCE_STEALTH] != 0) {
    visible_adjust -= appearances[PlayerOptions::APPEARANCE_STEALTH];
  }
  if (curr_options->stealth_[col] != 0) {
    visible_adjust -= curr_options->stealth_[col];
  }
  if (appearances[PlayerOptions::APPEARANCE_BLINK] != 0) {
    float f = std::sin(ArrowEffects::GetTime() * 10);
    f = Quantize(f, BLINK_MOD_FREQUENCY);
    visible_adjust += SCALE(f, 0, 1, -1, 0);
  }
  if (appearances[PlayerOptions::APPEARANCE_RANDOMVANISH] != 0) {
    const float real_fade_dist = 80;
    visible_adjust += SCALE(
                          std::abs(dist_from_center_line), real_fade_dist,
                          2 * real_fade_dist, -1, 0) *
                      appearances[PlayerOptions::APPEARANCE_RANDOMVANISH];
  }

  return std::clamp(1 + visible_adjust, 0.0f, 1.0f);
}

float ArrowEffects::GetAlpha(
    const PlayerState* player_state, int col, float y_offset,
    float percent_fade_to_fail, float y_reverse_offset_pixels,
    float draw_distance_before_targets_pixels,
    float fade_in_percent_of_draw_far) {
  // Get the YPos without reverse (that is, factor in EFFECT_TIPSY).
  float y_pos_without_reverse = ArrowEffects::GetYPos(
      player_state, col, y_offset, y_reverse_offset_pixels, false);

  float percent_visible =
      ArrowGetPercentVisible(y_pos_without_reverse, col, y_offset);

  if (percent_fade_to_fail != -1) {
    percent_visible = 1 - percent_fade_to_fail;
  }

  float full_alpha_y =
      draw_distance_before_targets_pixels * (1 - fade_in_percent_of_draw_far);
  if (y_pos_without_reverse > full_alpha_y) {
    float f = SCALE(
        y_pos_without_reverse, full_alpha_y,
        draw_distance_before_targets_pixels, 1.0f, 0.0f);
    return f;
  }
  return (percent_visible > 0.5f) ? 1.0f : 0.0f;
}

float ArrowEffects::GetGlow(
    const PlayerState* player_state, int col, float y_offset,
    float percent_fade_to_fail, float y_reverse_offset_pixels,
    float draw_distance_before_targets_pixels,
    float fade_in_percent_of_draw_far) {
  // Get the YPos without reverse (that is, factor in EFFECT_TIPSY).
  float y_pos_without_reverse = ArrowEffects::GetYPos(
      player_state, col, y_offset, y_reverse_offset_pixels, false);

  float percent_visible =
      ArrowGetPercentVisible(y_pos_without_reverse, col, y_offset);

  if (percent_fade_to_fail != -1) {
    percent_visible = 1 - percent_fade_to_fail;
  }

  const float dist_from_half = std::abs(percent_visible - 0.5f);
  return SCALE(dist_from_half, 0, 0.5f, 1.3f, 0);
}

float ArrowEffects::GetBrightness(
    const PlayerState* player_state, float note_beat) {
  if (GAMESTATE->IsEditing()) {
    return 1;
  }

  float song_beat = player_state->position_.song_beat_visible_;
  float beats_until_step = note_beat - song_beat;

  float brightness = SCALE(beats_until_step, 0, -1, 1.f, 0.f);
  rage_clamp(brightness, 0, 1);
  return brightness;
}

float ArrowEffects::GetZPos(
    const PlayerState* player_state, int col, float y_offset) {
  float z_pos = 0;
  const float* effects = curr_options->effects_;
  const Style* style = GAMESTATE->GetCurrentStyle(player_state->player_number_);

  // TODO: Don't index by PlayerNumber.
  const Style::ColumnInfo* cols =
      style->column_info_[player_state->player_number_];
  PerPlayerData& data = g_EffectData[player_state->player_number_];

  if (effects[PlayerOptions::EFFECT_TORNADO_Z] != 0) {
    z_pos += CalculateTornadoOffsetFromMagnitude(
        dim_z, col, effects[PlayerOptions::EFFECT_TORNADO_Z],
        effects[PlayerOptions::EFFECT_TORNADO_Z_OFFSET],
        effects[PlayerOptions::EFFECT_TORNADO_Z_PERIOD], cols,
        player_state->notefield_zoom_, data, y_offset, false);
  }

  if (effects[PlayerOptions::EFFECT_TAN_TORNADO_Z] != 0) {
    z_pos += CalculateTornadoOffsetFromMagnitude(
        dim_z, col, effects[PlayerOptions::EFFECT_TAN_TORNADO_Z],
        effects[PlayerOptions::EFFECT_TAN_TORNADO_Z_OFFSET],
        effects[PlayerOptions::EFFECT_TAN_TORNADO_Z_PERIOD], cols,
        player_state->notefield_zoom_, data, y_offset, true);
  }

  if (effects[PlayerOptions::EFFECT_BUMPY] != 0) {
    z_pos += effects[PlayerOptions::EFFECT_BUMPY] * 40 *
             std::sin(CalculateBumpyAngle(
                 y_offset, effects[PlayerOptions::EFFECT_BUMPY_OFFSET],
                 effects[PlayerOptions::EFFECT_BUMPY_PERIOD]));
  }

  if (curr_options->bumpy_[col] != 0) {
    z_pos += curr_options->bumpy_[col] * 40 *
             std::sin(CalculateBumpyAngle(
                 y_offset, effects[PlayerOptions::EFFECT_BUMPY_OFFSET],
                 effects[PlayerOptions::EFFECT_BUMPY_PERIOD]));
  }

  if (effects[PlayerOptions::EFFECT_TAN_BUMPY] != 0) {
    z_pos += effects[PlayerOptions::EFFECT_TAN_BUMPY] * 40 *
             SelectTanType(
                 CalculateBumpyAngle(
                     y_offset, effects[PlayerOptions::EFFECT_TAN_BUMPY_OFFSET],
                     effects[PlayerOptions::EFFECT_TAN_BUMPY_PERIOD]),
                 curr_options->cosecant_);
  }

  if (effects[PlayerOptions::EFFECT_ZIGZAG_Z] != 0) {
    float result = RageTriangle(
        (PI * (1 / (effects[PlayerOptions::EFFECT_ZIGZAG_Z_PERIOD] + 1)) *
         ((y_offset +
           (100.0f * (effects[PlayerOptions::EFFECT_ZIGZAG_Z_OFFSET]))) /
          ARROW_SIZE)));

    z_pos +=
        (effects[PlayerOptions::EFFECT_ZIGZAG_Z] * ARROW_SIZE / 2) * result;
  }

  if (effects[PlayerOptions::EFFECT_SAWTOOTH_Z] != 0) {
    z_pos +=
        (effects[PlayerOptions::EFFECT_SAWTOOTH_Z] * ARROW_SIZE) *
        ((0.5f / (effects[PlayerOptions::EFFECT_SAWTOOTH_Z_PERIOD] + 1) *
          y_offset) /
             ARROW_SIZE -
         std::floor(
             (0.5f / (effects[PlayerOptions::EFFECT_SAWTOOTH_Z_PERIOD] + 1) *
              y_offset) /
             ARROW_SIZE));
  }

  if (effects[PlayerOptions::EFFECT_PARABOLA_Z] != 0) {
    z_pos += effects[PlayerOptions::EFFECT_PARABOLA_Z] *
             (y_offset / ARROW_SIZE) * (y_offset / ARROW_SIZE);
  }

  if (effects[PlayerOptions::EFFECT_ATTENUATE_Z] != 0) {
    const float x_offset = cols[col].x_offset_;
    z_pos += effects[PlayerOptions::EFFECT_ATTENUATE_Z] *
             (y_offset / ARROW_SIZE) * (y_offset / ARROW_SIZE) *
             (x_offset / ARROW_SIZE);
  }

  if (effects[PlayerOptions::EFFECT_DRUNK_Z] != 0) {
    z_pos += effects[PlayerOptions::EFFECT_DRUNK_Z] *
             (std::cos(CalculateDrunkAngle(
                  effects[PlayerOptions::EFFECT_DRUNK_Z_SPEED], col,
                  effects[PlayerOptions::EFFECT_DRUNK_Z_OFFSET],
                  DRUNK_Z_COLUMN_FREQUENCY, y_offset,
                  effects[PlayerOptions::EFFECT_DRUNK_Z_PERIOD],
                  DRUNK_Z_OFFSET_FREQUENCY)) *
              ARROW_SIZE * DRUNK_Z_ARROW_MAGNITUDE);
  }

  if (effects[PlayerOptions::EFFECT_TAN_DRUNK_Z] != 0) {
    z_pos += effects[PlayerOptions::EFFECT_TAN_DRUNK_Z] *
             (SelectTanType(
                  CalculateDrunkAngle(
                      effects[PlayerOptions::EFFECT_TAN_DRUNK_Z_SPEED], col,
                      effects[PlayerOptions::EFFECT_TAN_DRUNK_Z_OFFSET],
                      DRUNK_Z_COLUMN_FREQUENCY, y_offset,
                      effects[PlayerOptions::EFFECT_TAN_DRUNK_Z_PERIOD],
                      DRUNK_Z_OFFSET_FREQUENCY),
                  curr_options->cosecant_) *
              ARROW_SIZE * DRUNK_Z_ARROW_MAGNITUDE);
  }

  if (effects[PlayerOptions::EFFECT_BEAT_Z] != 0) {
    const float shift =
        data.beat_factor_[dim_z] *
        std::sin(
            y_offset / ((effects[PlayerOptions::EFFECT_BEAT_Z_PERIOD] *
                         BEAT_Z_OFFSET_HEIGHT) +
                        BEAT_Z_OFFSET_HEIGHT) +
            PI / BEAT_Z_PI_HEIGHT);
    z_pos += effects[PlayerOptions::EFFECT_BEAT_Z] * shift;
  }

  if (effects[PlayerOptions::EFFECT_DIGITAL_Z] != 0) {
    z_pos += (effects[PlayerOptions::EFFECT_DIGITAL_Z] * ARROW_SIZE * 0.5f) *
             std::round(
                 (effects[PlayerOptions::EFFECT_DIGITAL_Z_STEPS] + 1) *
                 std::sin(CalculateDigitalAngle(
                     y_offset, effects[PlayerOptions::EFFECT_DIGITAL_Z_OFFSET],
                     effects[PlayerOptions::EFFECT_DIGITAL_Z_PERIOD]))) /
             (effects[PlayerOptions::EFFECT_DIGITAL_Z_STEPS] + 1);
  }

  if (effects[PlayerOptions::EFFECT_TAN_DIGITAL_Z] != 0) {
    z_pos +=
        (effects[PlayerOptions::EFFECT_TAN_DIGITAL_Z] * ARROW_SIZE * 0.5f) *
        std::round(
            (effects[PlayerOptions::EFFECT_TAN_DIGITAL_Z_STEPS] + 1) *
            SelectTanType(
                CalculateDigitalAngle(
                    y_offset,
                    effects[PlayerOptions::EFFECT_TAN_DIGITAL_Z_OFFSET],
                    effects[PlayerOptions::EFFECT_TAN_DIGITAL_Z_PERIOD]),
                curr_options->cosecant_)) /
        (effects[PlayerOptions::EFFECT_TAN_DIGITAL_Z_STEPS] + 1);
  }

  if (effects[PlayerOptions::EFFECT_SQUARE_Z] != 0) {
    float result = RageSquare((
        PI *
        (y_offset + (1.0f * (effects[PlayerOptions::EFFECT_SQUARE_Z_OFFSET]))) /
        (ARROW_SIZE +
         (effects[PlayerOptions::EFFECT_SQUARE_Z_PERIOD] * ARROW_SIZE))));
    z_pos +=
        (effects[PlayerOptions::EFFECT_SQUARE_Z] * ARROW_SIZE * 0.5f) * result;
  }

  if (effects[PlayerOptions::EFFECT_BOUNCE_Z] != 0) {
    float bounce_amt = std::abs(
        std::sin(
            ((y_offset +
              (1.0f * (effects[PlayerOptions::EFFECT_BOUNCE_Z_OFFSET]))) /
             (60 + (effects[PlayerOptions::EFFECT_BOUNCE_Z_PERIOD] * 60)))));

    z_pos += effects[PlayerOptions::EFFECT_BOUNCE_Z] * ARROW_SIZE * 0.5f *
             bounce_amt;
  }

  return z_pos;
}

bool ArrowEffects::NeedZBuffer() {
  const float* effects = curr_options->effects_;
  // We also need to use the Z buffer if twirl is in play, because of
  // hold modulation. -vyhd (OpenITG r623)
  if (effects[PlayerOptions::EFFECT_BUMPY] != 0 ||
      effects[PlayerOptions::EFFECT_TWIRL] != 0) {
    return true;
  }
  if (effects[PlayerOptions::EFFECT_BEAT_Z] != 0 ||
      effects[PlayerOptions::EFFECT_DIGITAL_Z] != 0) {
    return true;
  }
  if (effects[PlayerOptions::EFFECT_ZIGZAG_Z] != 0 ||
      effects[PlayerOptions::EFFECT_SAWTOOTH_Z] != 0) {
    return true;
  }
  if (effects[PlayerOptions::EFFECT_PARABOLA_Z] != 0 ||
      effects[PlayerOptions::EFFECT_SQUARE_Z] != 0) {
    return true;
  }
  if (curr_options->z_buffer_ ||
      effects[PlayerOptions::EFFECT_ATTENUATE_Z] != 0) {
    return true;
  }
  if (effects[PlayerOptions::EFFECT_BOUNCE_Z] != 0) {
    return true;
  }
  return false;
}

float ArrowEffects::GetZoom(
    const PlayerState* player_state, float y_offset, int col) {
  float zoom = 1.0f;
  // Design change:  Instead of having a flag in the style that toggles a
  // fixed zoom (0.6) that is only applied to the columns, ScreenGameplay now
  // calculates a zoom factor to apply to the notefield and puts it in the
  // PlayerState. -Kyz
  zoom *= player_state->notefield_zoom_;

  zoom = GetZoomVariable(y_offset, col, zoom);

  float tiny_percent = curr_options->effects_[PlayerOptions::EFFECT_TINY];
  if (tiny_percent != 0) {
    tiny_percent = std::pow(0.5f, tiny_percent);
    zoom *= tiny_percent;
  }
  if (curr_options->tiny_[col] != 0) {
    tiny_percent = std::pow(0.5f, curr_options->tiny_[col]);
    zoom *= tiny_percent;
  }
  return zoom;
}

float ArrowEffects::GetZoomVariable(float y_offset, int col, float cur_zoom) {
  float zoom = cur_zoom;
  if (curr_options->effects_[PlayerOptions::EFFECT_PULSE_INNER] != 0 ||
      curr_options->effects_[PlayerOptions::EFFECT_PULSE_OUTER] != 0) {
    float sine = std::sin(
        ((y_offset +
          (100.0f *
           (curr_options->effects_[PlayerOptions::EFFECT_PULSE_OFFSET]))) /
         (0.4f * (ARROW_SIZE +
                  (curr_options->effects_[PlayerOptions::EFFECT_PULSE_PERIOD] *
                   ARROW_SIZE)))));

    zoom *= (sine * (curr_options->effects_[PlayerOptions::EFFECT_PULSE_OUTER] *
                     0.5f)) +
            GetPulseInner();
  }
  if (curr_options->effects_[PlayerOptions::EFFECT_SHRINK_TO_MULT] != 0 &&
      y_offset >= 0) {
    zoom *=
        1 /
        (1 + (y_offset *
              (curr_options->effects_[PlayerOptions::EFFECT_SHRINK_TO_MULT] /
               100.0f)));
  }

  if (curr_options->effects_[PlayerOptions::EFFECT_SHRINK_TO_LINEAR] != 0 &&
      y_offset >= 0) {
    zoom +=
        y_offset *
        (0.5f * curr_options->effects_[PlayerOptions::EFFECT_SHRINK_TO_LINEAR] /
         ARROW_SIZE);
  }
  return zoom;
}

float ArrowEffects::GetPulseInner() {
  float pulse_inner = 1.0f;
  if (curr_options->effects_[PlayerOptions::EFFECT_PULSE_INNER] != 0 ||
      curr_options->effects_[PlayerOptions::EFFECT_PULSE_OUTER] != 0) {
    pulse_inner =
        ((curr_options->effects_[PlayerOptions::EFFECT_PULSE_INNER] * 0.5f) +
         1);
    if (pulse_inner == 0) {
      pulse_inner = 0.01f;
    }
  }
  return pulse_inner;
}

static ThemeMetric<float> FRAME_WIDTH_EFFECTS_PIXELS_PER_SECOND(
    "ArrowEffects", "FrameWidthEffectsPixelsPerSecond");
static ThemeMetric<float> FRAME_WIDTH_EFFECTS_MIN_MULTIPLIER(
    "ArrowEffects", "FrameWidthEffectsMinMultiplier");
static ThemeMetric<float> FRAME_WIDTH_EFFECTS_MAX_MULTIPLIER(
    "ArrowEffects", "FrameWidthEffectsMaxMultiplier");
static ThemeMetric<bool> FRAME_WIDTH_LOCK_EFFECTS_TO_OVERLAPPING(
    "ArrowEffects", "FrameWidthLockEffectsToOverlapping");
static ThemeMetric<float> FRAME_WIDTH_LOCK_EFFECTS_TWEEN_PIXELS(
    "ArrowEffects", "FrameWidthLockEffectsTweenPixels");

float ArrowEffects::GetFrameWidthScale(
    const PlayerState* player_state, float y_offset, float overlapped_time) {
  float frame_width_multiplier = 1.0f;

  float pixels_per_second = FRAME_WIDTH_EFFECTS_PIXELS_PER_SECOND;
  float second = y_offset / pixels_per_second;
  float width_effect = player_state->effect_history_.GetSample(second);
  if (width_effect != 0 && FRAME_WIDTH_LOCK_EFFECTS_TO_OVERLAPPING) {
    // Don't display effect data that happened before this hold overlapped the
    // top.
    float from_end_of_overlapped = overlapped_time - second;
    float trailing_pixels = FRAME_WIDTH_LOCK_EFFECTS_TWEEN_PIXELS;
    float trailing_seconds = trailing_pixels / pixels_per_second;
    float scale_effect =
        SCALE(from_end_of_overlapped, 0.0f, trailing_seconds, 0.0f, 1.0f);
    rage_clamp(scale_effect, 0.0f, 1.0f);
    width_effect *= scale_effect;
  }

  if (width_effect > 0) {
    frame_width_multiplier *= SCALE(
        width_effect, 0.0f, 1.0f, 1.0f, FRAME_WIDTH_EFFECTS_MAX_MULTIPLIER);
  } else if (width_effect < 0) {
    frame_width_multiplier *= SCALE(
        width_effect, 0.0f, -1.0f, 1.0f, FRAME_WIDTH_EFFECTS_MIN_MULTIPLIER);
  }

  return frame_width_multiplier;
}

// To provide reasonable defaults to methods below.
ThemeMetric<float> FADE_BEFORE_TARGETS_PERCENT(
    "NoteField", "FadeBeforeTargetsPercent");
ThemeMetric<float> DRAW_DISTANCE_BEFORE_TARGET_PIXELS(
    "Player", "DrawDistanceBeforeTargetsPixels");
ThemeMetric<float> GRAY_ARROWS_Y_STANDARD("Player", "ReceptorArrowsYStandard");
ThemeMetric<float> GRAY_ARROWS_Y_REVERSE("Player", "ReceptorArrowsYReverse");

// lua start
#include "LuaBinding.h"

namespace {
/* Update() need to be exposed to use ArrowEffects off ScreenGameplay. It is
 * harmless.	 */
int Update(lua_State* L) {
  ArrowEffects::Update();
  return 0;
}

// Provide a reasonable default value for fYReverseOffset
float YReverseOffset(lua_State* L, int argnum) {
  float y_reverse_offset_pixels =
      GRAY_ARROWS_Y_REVERSE - GRAY_ARROWS_Y_STANDARD;
  if (lua_gettop(L) >= argnum && !lua_isnil(L, argnum)) {
    y_reverse_offset_pixels = FArg(argnum);
  }
  return y_reverse_offset_pixels;
}

// ( PlayerState ps, int iCol, float fNoteBeat )
int GetYOffset(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  float peak_y_offset;
  bool is_past_peak;

  lua_pushnumber(
      L, ArrowEffects::GetYOffset(
             ps, IArg(2) - 1, FArg(3), peak_y_offset, is_past_peak));
  lua_pushnumber(L, peak_y_offset);
  lua_pushboolean(L, is_past_peak);
  return 3;
}

// ( PlayerState ps, int iCol, float fYOffset, float fYReverseOffsetPixels )
int GetYPos(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  float y_reverse_offset_pixels = YReverseOffset(L, 4);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(
      L,
      ArrowEffects::GetYPos(ps, IArg(2) - 1, FArg(3), y_reverse_offset_pixels));
  return 1;
}

// ( PlayerState ps, int iCol, float fYPos, float fYReverseOffsetPixels )
int GetYOffsetFromYPos(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  float y_reverse_offset_pixels = YReverseOffset(L, 4);
  lua_pushnumber(
      L, ArrowEffects::GetYOffsetFromYPos(
             IArg(2) - 1, FArg(3), y_reverse_offset_pixels));
  return 1;
}

// ( PlayerState ps, int iCol, float fYOffset )
int GetXPos(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(L, ArrowEffects::GetXPos(ps, IArg(2) - 1, FArg(3)));
  return 1;
}

// ( PlayerState ps, int iCol, float fYOffset )
int GetZPos(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(L, ArrowEffects::GetZPos(ps, IArg(2) - 1, FArg(3)));
  return 1;
}

// ( PlayerState ps, float fYOffset, int iCol )
int GetRotationX(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  bool is_hold_cap = false;
  lua_pushnumber(
      L, ArrowEffects::GetRotationX(ps, FArg(2), is_hold_cap, IArg(3) - 1));
  return 1;
}

// ( PlayerState ps, float fYOffset, int iCol )
int GetRotationY(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(L, ArrowEffects::GetRotationY(ps, FArg(2), IArg(3) - 1));
  return 1;
}

// ( PlayerState ps, float fNoteBeat, bool bIsHoldHead, int iCol )
int GetRotationZ(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  // Make bIsHoldHead optional.
  bool is_hold_head = false;
  if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    is_hold_head = BArg(3);
  }
  lua_pushnumber(
      L, ArrowEffects::GetRotationZ(ps, FArg(2), is_hold_head, IArg(4) - 1));
  return 1;
}

// ( PlayerState ps, int iCol )
int ReceptorGetRotationZ(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(L, ArrowEffects::ReceptorGetRotationZ(ps, IArg(2) - 1));
  return 1;
}

//( PlayerState ps, int iCol, float fYOffset, float fPercentFadeToFail, float
// fYReverseOffsetPixels, float fDrawDistanceBeforeTargetsPixels, float
// fFadeInPercentOfDrawFar )
int GetAlpha(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  // Provide reasonable default values.
  float percent_fade_to_fail = -1;
  float y_reverse_offset_pixels = YReverseOffset(L, 5);
  float draw_distance_before_targets_pixels =
      DRAW_DISTANCE_BEFORE_TARGET_PIXELS;
  float fade_in_percent_of_draw_far = FADE_BEFORE_TARGETS_PERCENT;
  if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) {
    percent_fade_to_fail = FArg(4);
  }
  if (lua_gettop(L) >= 6 && !lua_isnil(L, 6)) {
    draw_distance_before_targets_pixels = FArg(6);
  }
  if (lua_gettop(L) >= 7 && !lua_isnil(L, 7)) {
    fade_in_percent_of_draw_far = FArg(7);
  }
  lua_pushnumber(
      L, ArrowEffects::GetAlpha(
             ps, IArg(2) - 1, FArg(3), percent_fade_to_fail,
             y_reverse_offset_pixels, draw_distance_before_targets_pixels,
             fade_in_percent_of_draw_far));
  return 1;
}

//( PlayerState ps, int iCol, float fYOffset, float fPercentFadeToFail, float
// fYReverseOffsetPixels, float fDrawDistanceBeforeTargetsPixels, float
// fFadeInPercentOfDrawFar )
int GetGlow(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  // Provide reasonable default values.
  float percent_fade_to_fail = -1;  //
  float y_reverse_offset_pixels = YReverseOffset(L, 5);
  float draw_distance_before_targets_pixels =
      DRAW_DISTANCE_BEFORE_TARGET_PIXELS;
  float fade_in_percent_of_draw_far = FADE_BEFORE_TARGETS_PERCENT;
  if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) {
    percent_fade_to_fail = FArg(4);
  }
  if (lua_gettop(L) >= 6 && !lua_isnil(L, 6)) {
    draw_distance_before_targets_pixels = FArg(6);
  }
  if (lua_gettop(L) >= 7 && !lua_isnil(L, 7)) {
    fade_in_percent_of_draw_far = FArg(7);
  }
  lua_pushnumber(
      L, ArrowEffects::GetGlow(
             ps, IArg(2) - 1, FArg(3), percent_fade_to_fail,
             y_reverse_offset_pixels, draw_distance_before_targets_pixels,
             fade_in_percent_of_draw_far));
  return 1;
}

// ( PlayerState ps, float fNoteBeat )
int GetBrightness(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(L, ArrowEffects::GetBrightness(ps, FArg(2)));
  return 1;
}

// ( PlayerState ps )
int NeedZBuffer(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushboolean(L, ArrowEffects::NeedZBuffer());
  return 1;
}

// ( PlayerState ps, float fYOffset, int iCol )
int GetZoom(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());
  lua_pushnumber(L, ArrowEffects::GetZoom(ps, FArg(2), IArg(3) - 1));
  return 1;
}

// ( PlayerState ps, float fYOffset, fOverlappedTime )
int GetFrameWidthScale(lua_State* L) {
  PlayerState* ps = Luna<PlayerState>::check(L, 1);
  ArrowEffects::SetCurrentOptions(&ps->player_options_.GetCurrent());

  // Make fOverlappedTime optional.
  float overlapped_time = 0;
  if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    overlapped_time = FArg(3);
  }
  lua_pushnumber(
      L, ArrowEffects::GetFrameWidthScale(ps, FArg(2), overlapped_time));
  return 1;
}

const luaL_Reg ArrowEffectsTable[] = {
    LIST_METHOD(Update),        LIST_METHOD(GetYOffset),
    LIST_METHOD(GetYPos),       LIST_METHOD(GetYOffsetFromYPos),
    LIST_METHOD(GetXPos),       LIST_METHOD(GetZPos),
    LIST_METHOD(GetRotationX),  LIST_METHOD(GetRotationY),
    LIST_METHOD(GetRotationZ),  LIST_METHOD(ReceptorGetRotationZ),
    LIST_METHOD(GetAlpha),      LIST_METHOD(GetGlow),
    LIST_METHOD(GetBrightness), LIST_METHOD(NeedZBuffer),
    LIST_METHOD(GetZoom),       LIST_METHOD(GetFrameWidthScale),
    {nullptr, nullptr}};
}  // namespace

LUA_REGISTER_NAMESPACE(ArrowEffects)

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
