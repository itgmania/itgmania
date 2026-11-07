#include "InputFilter.h"

#include <cstddef>
#include <map>  // for mouse stuff: -aj
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "EnumHelper.h"
#include "GameInput.h"
#include "InputMapper.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"

static const char* InputEventTypeNames[] = {"FirstPress", "Repeat", "Release"};

XToString(InputEventType);
XToLocalizedString(InputEventType);
LuaXType(InputEventType);

static const char* PadPanelNames[] = {"UpLeft",   "Up",     "UpRight",
                                      "Left",     "Center", "Right",
                                      "DownLeft", "Down",   "DownRight"};
XToString(PadPanel);
StringToX(PadPanel);
LuaXType(PadPanel);

static const char* PadSensorNames[] = {
    "TopCenter",    "TopLeft",    "TopRight",    "RightCenter",
    "BottomCenter", "BottomLeft", "BottomRight", "LeftCenter",
};
XToString(PadSensor);
StringToX(PadSensor);
LuaXType(PadSensor);

struct ButtonState {
  ButtonState();
  bool being_held_;          // actual current state
  bool last_reported_held_;  // last state reported by Update()
  std::string comment_;
  float secs_held_;
  DeviceInput device_input_;

  // Timestamp of m_BeingHeld changing.
  RageTimer being_held_time_;

  // The time that we actually reported the last event (used for debouncing).
  RageTimer last_report_time_;

  // The timestamp of the last reported change. Unlike m_BeingHeldTime, this
  // value is debounced along with the input state. (This is the same as
  // m_fSecsHeld, except this isn't affected by Update scaling.)
  RageTimer last_input_time_;
};

struct DeviceButtonPair {
  InputDevice device_;
  DeviceButton button_;
  DeviceButtonPair(InputDevice d, DeviceButton b) : device_(d), button_(b) {}
};

inline bool operator<(
    const DeviceButtonPair& lhs, const DeviceButtonPair& rhs) {
  if (lhs.device_ != rhs.device_) {
    return lhs.device_ < rhs.device_;
  }
  return lhs.button_ < rhs.button_;
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
/* Maintain a set of all interesting buttons: buttons which are being held
 * down, or which were held down and need a RELEASE event. We use this to
 * optimize InputFilter::Update, so we don't have to process every button
 * we know about when most of them aren't in use. This set is protected
 * by queuemutex. */
typedef std::map<DeviceButtonPair, ButtonState> ButtonStateMap;
ButtonStateMap g_ButtonStates;
ButtonState& GetButtonState(const DeviceInput& di) {
  DeviceButtonPair db(di.device_, di.button_);
  ButtonState& bs = g_ButtonStates[db];
  bs.device_input_.button_ = di.button_;
  bs.device_input_.device_ = di.device_;
  return bs;
}

DeviceInputList g_CurrentState;
std::set<DeviceInput> g_DisableRepeat;
}  // namespace

/* Some input devices require debouncing. Do this on both press and release.
 * After reporting a change in state, don't report another for the debounce
 * period. If a button is reported pressed, report it. If the button is
 * immediately reported released, wait a period before reporting it; if the
 * button is repressed during that time, the release is never reported.
 * The detail is important: if a button is pressed for 1ms and released, we must
 * always report it, even if the debounce period is 10ms, since it might be a
 * coin counter with a very short signal. The only time we discard events is if
 * a button is pressed, released and then pressed again quickly.
 *
 * This delay in events is ordinarily not noticable, because we report initial
 * presses and releases immediately.  However, if a real press is ever delayed,
 * this won't cause timing problems, because the event timestamp is preserved.
 *
 * For pad play, a value of 20ms-50ms seems to result in a better experience.
 * For keyboard play, this is often set to 0.
 * */
static Preference<float> g_fInputDebounceTime("InputDebounceTime", 0.02f);

InputFilter* INPUTFILTER =
    nullptr;  // global and accessible from anywhere in our program

static const float TIME_BEFORE_REPEATS = 0.375f;

static const float REPEATS_PER_SEC = 8;

static float g_fTimeBeforeRepeats, g_fTimeBetweenRepeats;

InputFilter::InputFilter() {
  queuemutex_ = new RageMutex("InputFilter");

  Reset();
  ResetRepeatRate();

  mouse_coords_.x_ = 0;
  mouse_coords_.y_ = 0;
  mouse_coords_.z_ = 0;

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
  delete queuemutex_;
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

void InputFilter::SetRepeatDelay(float delay) { g_fTimeBeforeRepeats = delay; }

void InputFilter::ResetRepeatRate() {
  SetRepeatRate(REPEATS_PER_SEC);
  SetRepeatDelay(TIME_BEFORE_REPEATS);
}

ButtonState::ButtonState()
    : being_held_time_(RageZeroTimer), last_report_time_(RageZeroTimer) {
  being_held_ = false;
  last_reported_held_ = false;
  secs_held_ = 0;
}

void InputFilter::ButtonPressed(const DeviceInput& di) {
  LockMut(*queuemutex_);

  if (di.ts_.IsZero()) {
    LOG->Warn("InputFilter::ButtonPressed: zero timestamp is invalid");
  }

  // Filter out input that is beyond the range of the current system.
  if (di.device_ >= NUM_InputDevice) {
    LOG->Trace("InputFilter::ButtonPressed: Invalid device %i", di.device_);
    return;
  }
  if (di.button_ >= NUM_DeviceButton) {
    LOG->Trace("InputFilter::ButtonPressed: Invalid button %i", di.button_);
    return;
  }

  ButtonState& bs = GetButtonState(di);

  // Flush any delayed input, like Update() (in case Update() isn't being
  // called).
  RageTimer now;
  CheckButtonChange(bs, di, now);

  bs.device_input_ = di;

  bool down = di.down_;
  if (bs.being_held_ != down) {
    bs.being_held_ = down;
    bs.being_held_time_ = di.ts_;
  }

  // Try to report presses immediately.
  MakeButtonStateList(g_CurrentState);
  CheckButtonChange(bs, di, now);
}

/** @brief Release all buttons on the given device. */
void InputFilter::ResetDevice(InputDevice device) {
  LockMut(*queuemutex_);
  RageTimer now;

  const ButtonStateMap button_states(g_ButtonStates);
  for (const std::pair<const DeviceButtonPair, ButtonState>& b :
       button_states) {
    const DeviceButtonPair& db = b.first;
    if (db.device_ == device) {
      ButtonPressed(DeviceInput(device, db.button_, 0, now));
    }
  }
}

/** @brief Check for reportable presses. */
void InputFilter::CheckButtonChange(
    ButtonState& bs, DeviceInput di, const RageTimer& now) {
  if (bs.being_held_ == bs.last_reported_held_) {
    return;
  }

  GameInput gi;

  /* Possibly apply debounce,
   * If the input was coin, possibly apply distinct coin debounce in the else
   * below. */
  if (!INPUTMAPPER->DeviceToGame(di, gi) || gi.button_ != GAME_BUTTON_COIN) {
    /* If the last IET_FIRST_PRESS or IET_RELEASE event was sent too recently,
     * wait a while before sending it. */
    if (now - bs.last_report_time_ < g_fInputDebounceTime) {
      return;
    }
  } else {
    if (now - bs.last_report_time_ < PREFSMAN->debounce_coin_input_time_) {
      return;
    }
  }

  bs.last_report_time_ = now;
  bs.last_reported_held_ = bs.being_held_;
  bs.secs_held_ = 0;
  bs.last_input_time_ = bs.being_held_time_;

  di.ts_ = bs.being_held_time_;
  if (!bs.last_reported_held_) {
    di.level_ = 0;
  }

  MakeButtonStateList(g_CurrentState);
  ReportButtonChange(
      di, bs.last_reported_held_ ? IET_FIRST_PRESS : IET_RELEASE);

  if (!bs.last_reported_held_) {
    g_DisableRepeat.erase(di);
  }
}

void InputFilter::ReportButtonChange(const DeviceInput& di, InputEventType t) {
  queue_.push_back(InputEvent());
  InputEvent& ie = queue_.back();
  ie.type_ = t;
  ie.di_ = di;

  /* Include a list of all buttons that were pressed at the time of this event.
   * We can create this efficiently using g_ButtonStates. Use a vector and not
   * a map, for efficiency; most code will not use this information. Iterating
   * over g_ButtonStates will be in DeviceInput order, so users can binary
   * search this list (eg. std::lower_bound). */
  ie.button_state_ = g_CurrentState;
}

void InputFilter::MakeButtonStateList(
    std::vector<DeviceInput>& input_out) const {
  input_out.clear();
  input_out.reserve(g_ButtonStates.size());
  for (ButtonStateMap::const_iterator it = g_ButtonStates.begin();
       it != g_ButtonStates.end(); ++it) {
    const ButtonState& bs = it->second;
    input_out.push_back(bs.device_input_);
    input_out.back().ts_ = bs.last_input_time_;
    input_out.back().down_ = bs.last_reported_held_;
  }
}

void InputFilter::Update(float delta_time) {
  RageTimer now;

  INPUTMAN->Update();

  /* Make sure that nothing gets inserted while we do this, to prevent things
   * like "key pressed, key release, key repeat". */
  LockMut(*queuemutex_);

  DeviceInput di(InputDevice_Invalid, DeviceButton_Invalid, 1.0f, now);

  MakeButtonStateList(g_CurrentState);

  std::vector<ButtonStateMap::iterator> buttons_to_erase;

  for (std::map<DeviceButtonPair, ButtonState>::iterator b =
           g_ButtonStates.begin();
       b != g_ButtonStates.end(); ++b) {
    di.device_ = b->first.device_;
    di.button_ = b->first.button_;
    ButtonState& bs = b->second;

    // Generate IET_FIRST_PRESS and IET_RELEASE events that were delayed.
    CheckButtonChange(bs, di, now);

    // Generate IET_REPEAT events.
    if (!bs.last_reported_held_) {
      // If the key isn't pressed, and hasn't been pressed for a while
      // (so debouncing isn't interested in it), purge the entry.
      if (now - bs.last_report_time_ > g_fInputDebounceTime &&
          bs.device_input_.level_ == 0.0f) {
        buttons_to_erase.push_back(b);
      }
      continue;
    }

    // If repeats are disabled for this button, skip.
    if (g_DisableRepeat.find(di) != g_DisableRepeat.end()) {
      continue;
    }

    const float old_hold_time = bs.secs_held_;
    bs.secs_held_ += delta_time;
    const float new_hold_time = bs.secs_held_;

    if (new_hold_time <= g_fTimeBeforeRepeats) {
      continue;
    }

    float repeat_time;
    if (old_hold_time < g_fTimeBeforeRepeats) {
      repeat_time = g_fTimeBeforeRepeats;
    } else {
      float adjusted_old_hold_time = old_hold_time - g_fTimeBeforeRepeats;
      float adjusted_new_hold_time = new_hold_time - g_fTimeBeforeRepeats;
      if (int(adjusted_old_hold_time / g_fTimeBetweenRepeats) ==
          int(adjusted_new_hold_time / g_fTimeBetweenRepeats)) {
        continue;
      }
      repeat_time = ftruncf(new_hold_time, g_fTimeBetweenRepeats);
    }

    /* Set the timestamp to the exact time of the repeat. This way, as long
     * as tab/` aren't being used, the timestamp will always increase steadily
     * during repeats. */
    di.ts_ = bs.last_input_time_ + repeat_time;

    ReportButtonChange(di, IET_REPEAT);
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
    const DeviceInput& di, const DeviceInputList* button_state) const {
  LockMut(*queuemutex_);
  if (button_state == nullptr) {
    button_state = &g_CurrentState;
  }
  const DeviceInput* p_di =
      FindItemBinarySearch(button_state->begin(), button_state->end(), di);
  return p_di != nullptr && p_di->down_;
}

float InputFilter::GetSecsHeld(
    const DeviceInput& di, const DeviceInputList* button_state) const {
  LockMut(*queuemutex_);
  if (button_state == nullptr) {
    button_state = &g_CurrentState;
  }
  const DeviceInput* p_di =
      FindItemBinarySearch(button_state->begin(), button_state->end(), di);
  if (p_di == nullptr) {
    return 0;
  }
  return p_di->ts_.Ago();
}

float InputFilter::GetLevel(
    const DeviceInput& di, const DeviceInputList* button_state) const {
  LockMut(*queuemutex_);
  if (button_state == nullptr) {
    button_state = &g_CurrentState;
  }
  const DeviceInput* p_di =
      FindItemBinarySearch(button_state->begin(), button_state->end(), di);
  if (p_di == nullptr) {
    return 0.0f;
  }
  return p_di->level_;
}

std::string InputFilter::GetButtonComment(const DeviceInput& di) const {
  LockMut(*queuemutex_);
  return GetButtonState(di).comment_;
}

void InputFilter::ResetKeyRepeat(const DeviceInput& di) {
  LockMut(*queuemutex_);
  GetButtonState(di).secs_held_ = 0;
}

/** @brief Stop repeating the specified key until released. */
void InputFilter::RepeatStopKey(const DeviceInput& di) {
  LockMut(*queuemutex_);

  // If the button is up, do nothing.
  ButtonState& bs = GetButtonState(di);
  if (!bs.last_reported_held_) {
    return;
  }

  g_DisableRepeat.insert(di);
}

void InputFilter::GetInputEvents(std::vector<InputEvent>& array) {
  array.clear();
  LockMut(*queuemutex_);
  array.swap(queue_);
}

void InputFilter::GetPressedButtons(std::vector<DeviceInput>& array) const {
  LockMut(*queuemutex_);
  array = g_CurrentState;
}

void InputFilter::UpdateCursorLocation(float x, float y) {
  mouse_coords_.x_ = x;
  mouse_coords_.y_ = y;
}

void InputFilter::UpdateMouseWheel(float z) { mouse_coords_.z_ = z; }

bool InputFilter::setFullSensorStateBinary(
    PlayerNumber pn, PadPanel panel, PadSensor sensor, bool is_pressed) {
  if (pn < NUM_PlayerNumber && panel < NUM_PadPanel && sensor < NUM_PadSensor) {
    sensors_[pn].SetBinary(panel, sensor, is_pressed);
    return true;
  }
  return false;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to InputFilter. */
class LunaInputFilter : public Luna<InputFilter> {
 public:
  // todo: Should the input be locked to the theme's width/height instead of
  // the window's width/height? -aj
  static int GetMouseX(T* p, lua_State* L) {
    float x = p->GetCursorX();
    // Scale input to the theme's dimensions
    x = SCALE(
        x, 0, (PREFSMAN->display_height_ * PREFSMAN->display_aspect_ratio_),
        SCREEN_LEFT, SCREEN_RIGHT);
    lua_pushnumber(L, x);
    return 1;
  }
  static int GetMouseY(T* p, lua_State* L) {
    float y = p->GetCursorY();
    // Scale input to the theme's dimensions
    y = SCALE(y, 0, PREFSMAN->display_height_, SCREEN_TOP, SCREEN_BOTTOM);
    lua_pushnumber(L, y);
    return 1;
  }
  static int GetMouseWheel(T* p, lua_State* L) {
    float z = p->GetMouseWheel();
    lua_pushnumber(L, z);
    return 1;
  }

  static int GetFullSensorState(T* p, lua_State* L) {
    lua_createtable(L, NUM_PLAYERS, 0);

    for (size_t player = 0; player < NUM_PLAYERS; ++player) {
      lua_createtable(L, 0, NUM_PadPanel);

      for (size_t panel = 0; panel < NUM_PadPanel; ++panel) {
        lua_createtable(L, 0, NUM_PadSensor);

        for (size_t sensor = 0; sensor < NUM_PadSensor; ++sensor) {
          lua_pushnumber(
              L, p->getFullSensorState((PlayerNumber)player)
                     ->GetIntensity((PadPanel)panel, (PadSensor)sensor));

          lua_setfield(L, -2, PadSensorNames[sensor]);
        }

        lua_setfield(L, -2, PadPanelNames[panel]);
      }

      lua_setfield(L, -2, PlayerNumberToString((PlayerNumber)player).c_str());
    }

    return 1;
  }

  LunaInputFilter() {
    ADD_METHOD(GetMouseX);
    ADD_METHOD(GetMouseY);
    ADD_METHOD(GetMouseWheel);
    ADD_METHOD(GetFullSensorState);
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
