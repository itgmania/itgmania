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

/* A top-down view of the parity solver's suggested footwork. All positions
 * are evaluated from song time, so seeking and looping need no special reset.
 */
class DancingBot : public ActorFrame {
 public:
  /* Creates the pad, footprints, and checkbox controls. */
  DancingBot();
  /* Copies the bot and its owned child actors. */
  DancingBot* Copy() const override;
  /* Exposes this bot and its methods to Lua. */
  void PushSelf(lua_State* L) override;

  /* Analyzes note data and caches the selected player's footwork. */
  void Load(
      const NoteData& notes, StepsType type, TimingData& timing,
      PlayerNumber player);
  /* Loads a chart's note data, or clears the bot when steps is null. */
  void SetSteps(Steps* steps, PlayerNumber player);
  /* Updates the footprints and panel highlights for an absolute song time. */
  void SetSecond(float second);
  /* Sets the maximum movement anticipation time, clamped to 0–1 seconds. */
  void SetMotionSeconds(float seconds);
  /* Enables red panel flashes for doublesteps. */
  void SetFlashDoublesteps(bool flash);
  /* Returns whether doublestep warning flashes are enabled. */
  bool GetFlashDoublesteps() const { return m_bFlashDoublesteps; }
  /* Sets whether ScreenEdit should stop playback on a doublestep. */
  void SetStopOnDoublesteps(bool stop);
  /* Returns whether stopping playback on doublesteps is enabled. */
  bool GetStopOnDoublesteps() const { return m_bStopOnDoublesteps; }
  /* Alias for setting doublestep warning flashes. */
  void SetShowDoublesteps(bool show) { SetFlashDoublesteps(show); }
  /* Returns whether red doublestep warnings are enabled. */
  bool GetShowDoublesteps() const { return m_bFlashDoublesteps; }
  /* Finds the first doublestep crossed between two song times and its beat. */
  bool GetDoublestepBeat(
      float afterSecond, float throughSecond, float& beat) const;
  /* Toggles a clicked checkbox and returns whether the click was handled. */
  bool HandleMousePress(float screenX, float screenY);
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
  /* Stores one step on a panel and its warning/highlight information. */
  struct PanelContact {
    /* Song time at which the panel is struck. */
    float second;
    /* Hold release time, or the step time for a tap. */
    float endSecond;
    /* Whether this step repeats the same foot on a different panel. */
    bool doublestep;
    /* Chart beat used to position the editor when playback stops. */
    float beat;
    /* Whether the striking foot matches the twisted-foot warning condition. */
    bool twisted;
  };
  /* Chronological contact events for each panel. */
  std::array<std::vector<PanelContact>, StepParity::MAX_COLUMNS> m_Contacts;
  /* Whether the current chart has a supported layout and usable analysis. */
  bool m_bSupported = false;
  /* Whether doublestep panel warnings flash red. */
  bool m_bFlashDoublesteps = false;
  /* Whether ScreenEdit should stop when a doublestep occurs during playback. */
  bool m_bStopOnDoublesteps = false;
  /* Updates the checkbox marks to reflect the current options. */
  void RefreshControls();
  /* Maximum song-time interval used to interpolate an approaching step. */
  float m_fMotionSeconds = 0.18f;
  /* Song time most recently supplied to SetSecond. */
  float m_fSecond = 0;
};

#endif
