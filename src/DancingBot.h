#ifndef DANCING_BOT_H
#define DANCING_BOT_H

#include <array>
#include <limits>
#include <vector>

#include "ActorFrame.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "StepParityDatastructs.h"

class NoteData;
class Steps;
class TimingData;

/* Provides a Visualization of the suggested footwork for given steps.
 */
class DancingBot : public ActorFrame {
  /* TODO: Support controlling which tech the bot can perform and whether
   * playback should stop when an invalid step occurs. */

 public:
  DancingBot();
  DancingBot* Copy() const override;
  void PushSelf(lua_State* L) override;

  void Load(
      const NoteData& notes, StepsType type, TimingData& timing,
      PlayerNumber player);
  /* Loads a chart's note data, or clears the bot when steps is null. */
  void SetSteps(Steps* steps, PlayerNumber player);
  /* Updates the footprints and panel highlights for an absolute song time. */
  void SetSecond(float second);
  /* Sets the maximum movement anticipation time, clamped to 0–1 seconds. */
  void SetMotionSeconds(float seconds);
  /* Returns whether the loaded chart can be displayed by the bot. */
  bool IsSupported() const { return m_bSupported; }
  /* Returns the maximum movement anticipation time in song seconds. */
  float GetMotionSeconds() const { return m_fMotionSeconds; }

 private:
  /* Stores one foot's position and timing at a chart event. */
  struct FootPose {
    /* Song time at which this pose is reached. */
    float second = 0;
    /* Most recent step time, used for the footprint's press animation. */
    float lastHit = -std::numeric_limits<float>::infinity();
    /* Heel and toe positions in the bot's local coordinates. */
    std::array<StepParity::StagePoint, 2> points = {};
    /* Earliest song time each contact can leave its held panel. */
    std::array<float, 2> release = {};
  };
  /* Chronological pose events for the left and right feet. */
  std::array<std::vector<FootPose>, 2> m_Feet;
  /* Neutral poses used before either foot's first event. */
  std::array<FootPose, 2> m_Rest;
  /* Stores the timing of one step on a panel. */
  struct PanelContact {
    /* Song time at which the panel is stepped on. */
    float second;
    /* Hold release time, or the step time for a tap. */
    float endSecond;
  };
  /* contact events for each panel. */
  std::array<std::vector<PanelContact>, StepParity::MAX_COLUMNS> m_Contacts;
  /* Whether the current chart has a supported layout */
  bool m_bSupported = false;
  /* Maximum song-time interval used to interpolate an approaching step. */
  float m_fMotionSeconds = 0.18f;
  /* Song time most recently supplied to SetSecond. */
  float m_fSecond = 0;
};

#endif
