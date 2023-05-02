#include "global.h"

#include "InputFilter.h"

#include <set>
#include <vector>

#include "GameInput.h"
#include "InputMapper.h"
#include "LocalizedString.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"

static const char* InputEventTypeNames[] = {"FirstPress", "Repeat", "Release"};

XToString(InputEventType);
XToLocalizedString(InputEventType);
LuaXType(InputEventType);

struct ButtonState {
  ButtonState();
  bool being_held_;          // actual current state
  bool last_reported_held_;  // last state reported by Update()
  RString comment_;
  float seconds_held_;
  DeviceInput device_input_;

  // Timestamp of being_held_ changing.
  RageTimer being_held_time_;

  // The time that we actually reported the last event (used for debouncing).
  RageTimer last_report_time_;

  // The timestamp of the last reported change. Unlike being_held_time_, this
  // value is debounced along with the input state. (This is the same as
  // seconds_held_, except this isn't affected by Update scaling.)
  RageTimer last_input_time_;
};

struct DeviceButtonPair {
  InputDevice device;
  DeviceButton button;
  DeviceButtonPair(InputDevice d, DeviceButton b) : device(d), button(b) {}
};

inline bool operator<(
    const DeviceButtonPair& lhs, const DeviceButtonPair& rhs) {
  if (lhs.device != rhs.device) {
    return lhs.device < rhs.device;
  }
  return lhs.button < rhs.button;
}
inline bool operator>(
    const DeviceButtonPair& lhs, const DeviceButtonPair& rhs) {
  return operator<(rhs, lhs);
}
inline bool operator<=(
    const DeviceButtonPair& lhs, const DeviceButtonPair& rhs) {
  return !operator<(rhs, lhs);
}
inline bool operator>=(
    const DeviceButtonPair& lhs, const DeviceButtonPair& rhs) {
  return !operator<(lhs, rhs);
}

namespace {

// Maintain a set of all interesting buttons: buttons which are being held
// down, or which were held down and need a RELEASE event. We use this to
// optimize InputFilter::Update, so we don't have to process every button
// we know about when most of them aren't in use. This set is protected
// by queue_mutex_.
typedef std::map<DeviceButtonPair, ButtonState> ButtonStateMap;
ButtonStateMap g_ButtonStates;
ButtonState& GetButtonState(const DeviceInput& device_input) {
  DeviceButtonPair device_button(device_input.device, device_input.button);
  ButtonState& button_state = g_ButtonStates[device_button];
  button_state.device_input_.button = device_input.button;
  button_state.device_input_.device = device_input.device;
  return button_state;
}

DeviceInputList g_CurrentState;
std::set<DeviceInput> g_DisableRepeat;

}  // namespace

// Some input devices require debouncing. Do this on both press and release.
// After reporting a change in state, don't report another for the debounce
// period. If a button is reported pressed, report it. If the button is
// immediately reported released, wait a period before reporting it; if the
// button is repressed during that time, the release is never reported.
// The detail is important: if a button is pressed for 1ms and released, we must
// always report it, even if the debounce period is 10ms, since it might be a
// coin counter with a very short signal. The only time we discard events is if
// a button is pressed, released and then pressed again quickly.
//
// This delay in events is ordinarily not noticable, because we report initial
// presses and releases immediately.  However, if a real press is ever delayed,
// this won't cause timing problems, because the event timestamp is preserved.
//
// For pad play, a value of 20ms-50ms seems to result in a better experience.
// For keyboard play, this is often set to 0.
static Preference<float> g_fInputDebounceTime("InputDebounceTime", 0.02f);

// Global and accessible from anywhere in our program.
InputFilter* INPUTFILTER = nullptr;

static const float TIME_BEFORE_REPEATS = 0.375f;

static const float REPEATS_PER_SEC = 8;

static float g_fTimeBeforeRepeats, g_fTimeBetweenRepeats;

InputFilter::InputFilter() {
  queue_mutex_ = new RageMutex("InputFilter");

  Reset();
  ResetRepeatRate();

  mouse_coords_.x = 0;
  mouse_coords_.y = 0;
  mouse_coords_.z = 0;

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "INPUTFILTER");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

InputFilter::~InputFilter() {
  delete queue_mutex_;
  g_ButtonStates.clear();
  // Unregister with Lua.
  LUA->UnsetGlobal("INPUTFILTER");
}

void InputFilter::Reset() {
  FOREACH_InputDevice(i) ResetDevice(InputDevice(i));
}

void InputFilter::SetRepeatRate(float repeat_rate) {
  g_fTimeBetweenRepeats = 1 / repeat_rate;
}

void InputFilter::SetRepeatDelay(float delay) {
  g_fTimeBeforeRepeats = delay;
}

void InputFilter::ResetRepeatRate() {
  SetRepeatRate(REPEATS_PER_SEC);
  SetRepeatDelay(TIME_BEFORE_REPEATS);
}

ButtonState::ButtonState()
    : being_held_time_(RageZeroTimer), last_report_time_(RageZeroTimer) {
  being_held_ = false;
  last_reported_held_ = false;
  seconds_held_ = 0;
}

void InputFilter::ButtonPressed(const DeviceInput& device_input) {
  LockMut(*queue_mutex_);

  if (device_input.ts.IsZero()) {
    LOG->Warn("InputFilter::ButtonPressed: zero timestamp is invalid");
  }

  // Filter out input that is beyond the range of the current system.
  if (device_input.device >= NUM_InputDevice) {
    LOG->Trace("InputFilter::ButtonPressed: Invalid device %i",
							 device_input.device);
    return;
  }
  if (device_input.button >= NUM_DeviceButton) {
    LOG->Trace("InputFilter::ButtonPressed: Invalid button %i",
							 device_input.button);
    return;
  }

  ButtonState& button_state = GetButtonState(device_input);

  // Flush any delayed input, like Update() (in case Update() isn't being
  // called).
  RageTimer now;
  CheckButtonChange(button_state, device_input, now);

  button_state.device_input_ = device_input;

  bool Down = device_input.bDown;
  if (button_state.being_held_ != Down) {
    button_state.being_held_ = Down;
    button_state.being_held_time_ = device_input.ts;
  }

  // Try to report presses immediately.
  MakeButtonStateList(g_CurrentState);
  CheckButtonChange(button_state, device_input, now);
}

void InputFilter::SetButtonComment(
    const DeviceInput& device_input, const RString& comment) {
  LockMut(*queue_mutex_);
  ButtonState& button_state = GetButtonState(device_input);
  button_state.comment_ = comment;
}

// Release all buttons on the given device.
void InputFilter::ResetDevice(InputDevice device) {
  LockMut(*queue_mutex_);
  RageTimer now;

  const ButtonStateMap button_states(g_ButtonStates);
  for (const auto& button : button_states) {
    const DeviceButtonPair& device_button = button.first;
    if (device_button.device == device) {
      ButtonPressed(DeviceInput(device, device_button.button, 0, now));
    }
  }
}

// Check for reportable presses.
void InputFilter::CheckButtonChange(
    ButtonState& button_state, DeviceInput device_input, const RageTimer& now) {
  if (button_state.being_held_ == button_state.last_reported_held_) {
    return;
  }

  GameInput game_input;

  // Possibly apply debounce,
  // If the input was coin, possibly apply distinct coin debounce in the else
  // below.
  if (!INPUTMAPPER->DeviceToGame(device_input, game_input) ||
			game_input.button != GAME_BUTTON_COIN) {
    // If the last IET_FIRST_PRESS or IET_RELEASE event was sent too recently,
    // wait a while before sending it.
    if (now - button_state.last_report_time_ < g_fInputDebounceTime) {
      return;
    }
  } else {
    if (now - button_state.last_report_time_ <
				PREFSMAN->m_fDebounceCoinInputTime) {
      return;
    }
  }

  button_state.last_report_time_ = now;
  button_state.last_reported_held_ = button_state.being_held_;
  button_state.seconds_held_ = 0;
  button_state.last_input_time_ = button_state.being_held_time_;

  device_input.ts = button_state.being_held_time_;
  if (!button_state.last_reported_held_) {
    device_input.level = 0;
  }

  MakeButtonStateList(g_CurrentState);
  ReportButtonChange(
      device_input, button_state.last_reported_held_
					? IET_FIRST_PRESS
					: IET_RELEASE);

  if (!button_state.last_reported_held_) {
    g_DisableRepeat.erase(device_input);
  }
}

void InputFilter::ReportButtonChange(
		const DeviceInput& device_input, InputEventType input_event_type) {
  queue_.push_back(InputEvent());
  InputEvent& ie = queue_.back();
  ie.type_ = input_event_type;
  ie.device_input_ = device_input;

  // Include a list of all buttons that were pressed at the time of this event.
  // We can create this efficiently using g_ButtonStates. Use a vector and not
  // a map, for efficiency; most code will not use this information. Iterating
  // over g_ButtonStates will be in DeviceInput order, so users can binary
  // search this list (eg. std::lower_bound).
  ie.button_state_ = g_CurrentState;
}

void InputFilter::MakeButtonStateList(
    std::vector<DeviceInput>& input_out) const {
  input_out.clear();
  input_out.reserve(g_ButtonStates.size());
  for (auto it = g_ButtonStates.begin(); it != g_ButtonStates.end(); ++it) {
    const ButtonState& button_state = it->second;
    input_out.push_back(button_state.device_input_);
    input_out.back().ts = button_state.last_input_time_;
    input_out.back().bDown = button_state.last_reported_held_;
  }
}

void InputFilter::Update(float delta) {
  RageTimer now;

  INPUTMAN->Update();

  // Make sure that nothing gets inserted while we do this, to prevent things
  // like "key pressed, key release, key repeat".
  LockMut(*queue_mutex_);

  DeviceInput device_input(
			InputDevice_Invalid, DeviceButton_Invalid, 1.0f, now);

  MakeButtonStateList(g_CurrentState);

  std::vector<ButtonStateMap::iterator> buttons_to_erase;

  for (auto it = g_ButtonStates.begin(); it != g_ButtonStates.end(); ++it) {
    device_input.device = it->first.device;
    device_input.button = it->first.button;
    ButtonState& button_state = it->second;

    // Generate IET_FIRST_PRESS and IET_RELEASE events that were delayed.
    CheckButtonChange(button_state, device_input, now);

    // Generate IET_REPEAT events.
    if (!button_state.last_reported_held_) {
      // If the key isn't pressed, and hasn't been pressed for a while
      // (so debouncing isn't interested in it), purge the entry.
      if (now - button_state.last_report_time_ > g_fInputDebounceTime &&
          button_state.device_input_.level == 0.0f) {
        buttons_to_erase.push_back(it);
      }
      continue;
    }

    // If repeats are disabled for this button, skip.
    if (g_DisableRepeat.find(device_input) != g_DisableRepeat.end()) {
      continue;
    }

    const float old_hold_time = button_state.seconds_held_;
    button_state.seconds_held_ += delta;
    const float new_hold_time = button_state.seconds_held_;

    if (new_hold_time <= g_fTimeBeforeRepeats) {
      continue;
    }

    float fRepeatTime;
    if (old_hold_time < g_fTimeBeforeRepeats) {
      fRepeatTime = g_fTimeBeforeRepeats;
    } else {
      float fAdjustedOldHoldTime = old_hold_time - g_fTimeBeforeRepeats;
      float fAdjustedNewHoldTime = new_hold_time - g_fTimeBeforeRepeats;
      if (int(fAdjustedOldHoldTime / g_fTimeBetweenRepeats) ==
          int(fAdjustedNewHoldTime / g_fTimeBetweenRepeats)) {
        continue;
      }
      fRepeatTime = ftruncf(new_hold_time, g_fTimeBetweenRepeats);
    }

    // Set the timestamp to the exact time of the repeat. This way, as long as
		// tab/` aren't being used, the timestamp will always increase steadily
    // during repeats.
    device_input.ts = button_state.last_input_time_ + fRepeatTime;

    ReportButtonChange(device_input, IET_REPEAT);
  }

  for (ButtonStateMap::iterator& it : buttons_to_erase) {
    g_ButtonStates.erase(it);
  }
}

template <typename T, typename IT>
const T* FindItemBinarySearch(IT begin, IT end, const T& i) {
  IT it = lower_bound(begin, end, i);
  if (it == end || *it != i) {
    return nullptr;
  }

  return &*it;
}

bool InputFilter::IsBeingPressed(
    const DeviceInput& device_input,
		const DeviceInputList* button_state) const {
  LockMut(*queue_mutex_);
  if (button_state == nullptr) {
    button_state = &g_CurrentState;
  }
  const DeviceInput* di = FindItemBinarySearch(
			button_state->begin(), button_state->end(), device_input);
  return di != nullptr && di->bDown;
}

float InputFilter::GetSecsHeld(
    const DeviceInput& device_input,
		const DeviceInputList* button_state) const {
  LockMut(*queue_mutex_);
  if (button_state == nullptr) {
    button_state = &g_CurrentState;
  }
  const DeviceInput* di = FindItemBinarySearch(
			button_state->begin(), button_state->end(), device_input);
  if (di == nullptr) {
    return 0;
  }
  return di->ts.Ago();
}

float InputFilter::GetLevel(
    const DeviceInput& device_input,
		const DeviceInputList* button_state) const {
  LockMut(*queue_mutex_);
  if (button_state == nullptr) {
    button_state = &g_CurrentState;
  }
  const DeviceInput* di = FindItemBinarySearch(
			button_state->begin(), button_state->end(), device_input);
  if (di == nullptr) {
    return 0.0f;
  }
  return di->level;
}

RString InputFilter::GetButtonComment(const DeviceInput& device_input) const {
  LockMut(*queue_mutex_);
  return GetButtonState(device_input).comment_;
}

void InputFilter::ResetKeyRepeat(const DeviceInput& device_input) {
  LockMut(*queue_mutex_);
  GetButtonState(device_input).seconds_held_ = 0;
}

// Stop repeating the specified key until released.
void InputFilter::RepeatStopKey(const DeviceInput& device_input) {
  LockMut(*queue_mutex_);

  // If the button is up, do nothing.
  ButtonState& bs = GetButtonState(device_input);
  if (!bs.last_reported_held_) {
    return;
  }

  g_DisableRepeat.insert(device_input);
}

void InputFilter::GetInputEvents(std::vector<InputEvent>& array) {
  array.clear();
  LockMut(*queue_mutex_);
  array.swap(queue_);
}

void InputFilter::GetPressedButtons(std::vector<DeviceInput>& array) const {
  LockMut(*queue_mutex_);
  array = g_CurrentState;
}

void InputFilter::UpdateCursorLocation(float x, float y) {
  mouse_coords_.x = x;
  mouse_coords_.y = y;
}

void InputFilter::UpdateMouseWheel(float z) { mouse_coords_.z = z; }

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to InputFilter.
class LunaInputFilter : public Luna<InputFilter> {
 public:
  // TODO(aj): Should the input be locked to the theme's width/height instead of
  // the window's width/height?
  static int GetMouseX(T* p, lua_State* L) {
    float x = p->GetCursorX();
    // Scale input to the theme's dimensions
    x = SCALE(
        x, 0, (PREFSMAN->m_iDisplayHeight * PREFSMAN->m_fDisplayAspectRatio),
        SCREEN_LEFT, SCREEN_RIGHT);
    lua_pushnumber(L, x);
    return 1;
  }
  static int GetMouseY(T* p, lua_State* L) {
    float y = p->GetCursorY();
    // Scale input to the theme's dimensions
    y = SCALE(y, 0, PREFSMAN->m_iDisplayHeight, SCREEN_TOP, SCREEN_BOTTOM);
    lua_pushnumber(L, y);
    return 1;
  }
  static int GetMouseWheel(T* p, lua_State* L) {
    float z = p->GetMouseWheel();
    lua_pushnumber(L, z);
    return 1;
  }

  LunaInputFilter() {
    ADD_METHOD(GetMouseX);
    ADD_METHOD(GetMouseY);
    ADD_METHOD(GetMouseWheel);
  }
};

LUA_REGISTER_CLASS(InputFilter)
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
