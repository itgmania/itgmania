/* InputFilter - Checks RageInput and generates a list of InputEvents,
 * representing button presses, releases, and repeats. */

#ifndef INPUT_FILTER_H
#define INPUT_FILTER_H

#include <algorithm>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"
#include "RageTimer.h"

enum InputEventType {
  // The device was just pressed.
  IET_FIRST_PRESS,

  /* The device is auto-repeating. This event is guaranteed to be sent only
   * between IET_FIRST_PRESS and IET_RELEASE pairs. */
  IET_REPEAT,

  /* The device is no longer pressed. Exactly one IET_RELEASE event will be
   * sent for each IET_FIRST_PRESS. */
  IET_RELEASE,

  NUM_InputEventType,
  InputEventType_Invalid
};

const std::string& InputEventTypeToString(InputEventType cat);
const std::string& InputEventTypeToLocalizedString(InputEventType cat);
LuaDeclareType(InputEventType);

struct InputEvent {
  InputEvent() : type_(IET_FIRST_PRESS) {}

  DeviceInput di_;
  InputEventType type_;

  // A list of all buttons that were pressed at the time of this event:
  DeviceInputList button_state_;
};

struct MouseCoordinates {
  float x_;
  float y_;
  float z_;
};

// describes each of the panels of a single player dance pad
enum PadPanel {
  PadPanel_UpLeft = 0,
  PadPanel_Up,
  PadPanel_UpRight,
  PadPanel_Left,
  PadPanel_Center,
  PadPanel_Right,
  PadPanel_DownLeft,
  PadPanel_Down,
  PadPanel_DownRight,

  NUM_PadPanel,
  PadPanel_Invalid
};

enum PadSensor {
  PadSensor_TopCenter = 0,
  PadSensor_TopLeft,
  PadSensor_TopRight,
  PadSensor_RightCenter,
  PadSensor_BottomCenter,
  PadSensor_BottomLeft,
  PadSensor_BottomRight,
  PadSensor_LeftCenter,

  NUM_PadSensor,
  PadSensor_Invalid
};

struct PadSensorState {
 private:
  unsigned int current_value_[NUM_PadPanel][NUM_PadSensor] = {};
  unsigned int min_ = 0;
  unsigned int max_ = 100;

 public:
  float GetIntensity(PadPanel panel, PadSensor sensor) const {
    return static_cast<float>(
               current_value_[static_cast<int>(panel)]
                             [static_cast<int>(sensor)] -
               min_) /
           static_cast<float>(max_ - min_);
  }

  void Set(PadPanel panel, PadSensor sensor, unsigned int value) {
    value = std::clamp(value, min_, max_);
    this->current_value_[static_cast<int>(panel)][static_cast<int>(sensor)] =
        value;
  }

  void SetBinary(PadPanel panel, PadSensor sensor, bool is_pressed) {
    Set(panel, sensor, is_pressed ? max_ : min_);
  }

  void SetMax(unsigned int value) { max_ = std::max(value, min_); }

  void Clear() { std::memset(current_value_, 0, sizeof(current_value_)); }
};

class RageMutex;
struct ButtonState;
class InputFilter {
 public:
  void ButtonPressed(const DeviceInput& di);
  void ResetDevice(InputDevice dev);

  InputFilter();
  ~InputFilter();
  void Reset();
  void Update(float delta_time);

  void SetRepeatRate(float repeat_rate);
  void SetRepeatDelay(float delay);
  void ResetRepeatRate();
  void ResetKeyRepeat(const DeviceInput& di);
  void RepeatStopKey(const DeviceInput& di);

  // If aButtonState is nullptr, use the last reported state.
  bool IsBeingPressed(
      const DeviceInput& di,
      const DeviceInputList* button_state = nullptr) const;
  float GetSecsHeld(
      const DeviceInput& di,
      const DeviceInputList* button_state = nullptr) const;
  float GetLevel(
      const DeviceInput& di,
      const DeviceInputList* button_state = nullptr) const;
  std::string GetButtonComment(const DeviceInput& di) const;

  void GetInputEvents(std::vector<InputEvent>& event_out);
  void GetPressedButtons(std::vector<DeviceInput>& array) const;

  // cursor
  void UpdateCursorLocation(float x, float y);
  void UpdateMouseWheel(float z);
  float GetCursorX() { return mouse_coords_.x_; }
  float GetCursorY() { return mouse_coords_.y_; }
  float GetMouseWheel() { return mouse_coords_.z_; }

  PadSensorState* getFullSensorState(PlayerNumber pn) {
    if (pn < kNumPlayers) {
      return &sensors_[pn];
    }
    return nullptr;
  }

  bool setFullSensorStateBinary(
      PlayerNumber pn, PadPanel panel, PadSensor sensor, bool is_pressed);

  // Lua
  void PushSelf(lua_State* L);

 private:
  void CheckButtonChange(ButtonState& bs, DeviceInput di, const RageTimer& now);
  void ReportButtonChange(const DeviceInput& di, InputEventType t);
  void MakeButtonStateList(std::vector<DeviceInput>& input_out) const;

  std::vector<InputEvent> queue_;
  RageMutex* queuemutex_;
  MouseCoordinates mouse_coords_;

  // debug sensors.
  PadSensorState sensors_[kNumPlayers];

  InputFilter(const InputFilter& rhs);
  InputFilter& operator=(const InputFilter& rhs);
};

extern InputFilter*
    INPUTFILTER;  // global and accessible from anywhere in our program

#endif

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
