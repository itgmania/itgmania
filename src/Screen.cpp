#include "Screen.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ThemeManager.h"
#include "global.h"

#define NEXT_SCREEN THEME->GetMetric(name_, "NextScreen")
#define PREV_SCREEN THEME->GetMetric(name_, "PrevScreen")
#define PREPARE_SCREENS THEME->GetMetric(name_, "PrepareScreens")
#define PERSIST_SCREENS THEME->GetMetric(name_, "PersistScreens")
#define GROUPED_SCREENS THEME->GetMetric(name_, "GroupedScreens")

static const char* ScreenTypeNames[] = {
    "Attract",
    "GameMenu",
    "Gameplay",
    "SystemMenu",
};
XToString(ScreenType);
LuaXType(ScreenType);

void Screen::InitScreen(Screen* screen) { screen->Init(); }

Screen::~Screen() {}

bool Screen::SortMessagesByDelayRemaining(
    const Screen::QueuedScreenMessage& m1,
    const Screen::QueuedScreenMessage& m2) {
  return m1.delay_remaining_ < m2.delay_remaining_;
}

void Screen::Init() {
  ALLOW_OPERATOR_MENU_BUTTON.Load(name_, "AllowOperatorMenuButton");
  HANDLE_BACK_BUTTON.Load(name_, "HandleBackButton");
  REPEAT_RATE.Load(name_, "RepeatRate");
  REPEAT_DELAY.Load(name_, "RepeatDelay");
  LIGHTS_MODE.Load(name_, "LightsMode");

  codes_.Load(name_);

  SetFOV(0);

  send_on_pop_ = SM_None;
  running_ = false;

  calling_input_callbacks_ = false;

  ActorUtil::LoadAllCommandsFromName(*this, name_, "Screen");

  PlayCommandNoRecurse(Message("Init"));

  std::vector<std::string> list;
  split(PREPARE_SCREENS, ",", list);
  for (unsigned i = 0; i < list.size(); ++i) {
    LOG->Trace(
        "Screen \"%s\" preparing \"%s\"", name_.c_str(), list[i].c_str());
    SCREENMAN->PrepareScreen(list[i]);
  }

  list.clear();
  split(GROUPED_SCREENS, ",", list);
  for (unsigned i = 0; i < list.size(); ++i) {
    SCREENMAN->GroupScreen(list[i]);
  }

  list.clear();
  split(PERSIST_SCREENS, ",", list);
  for (unsigned i = 0; i < list.size(); ++i) {
    SCREENMAN->PersistantScreen(list[i]);
  }
}

void Screen::BeginScreen() {
  running_ = true;
  first_update_ = true;

  /* Screens set these when they determine their next screen dynamically. Reset
   * them here, so a reused screen doesn't inherit these from the last time it
   * was used. */
  next_screen_ = std::string();

  lock_input_secs_ = 0;

  this->RunCommands(THEME->GetMetricA(name_, "ScreenOnCommand"));

  if (lock_input_secs_ == 0) {
    lock_input_secs_ =
        0.0001f;  // always lock for a tiny amount of time so that we throw away
                  // any queued inputs during the load.
  }

  this->PlayCommand("Begin");
}

void Screen::EndScreen() {
  this->PlayCommand("End");
  running_ = false;

  input_callbacks_.clear();
  delayed_callback_removals_.clear();
}

void Screen::Update(float delta_time) {
  ActorFrame::Update(delta_time);

  lock_input_secs_ = std::max(0.0f, lock_input_secs_ - delta_time);

  /* We need to ensure two things:
   * 1. Messages must be sent in the order of delay. If two messages are sent
   *    simultaneously, one with a .001 delay and another with a .002 delay,
   *    the .001 delay message must be sent first.
   * 2. Messages to be delivered simultaneously must be sent in the order
   * queued.
   *
   * Sort by time to ensure #1; use a stable sort to ensure #2. */
  stable_sort(
      queued_messages_.begin(), queued_messages_.end(),
      SortMessagesByDelayRemaining);

  // Update the times of queued ScreenMessages.
  for (unsigned i = 0; i < queued_messages_.size(); i++) {
    /* Hack:
     * If we simply subtract time and then send messages, we have a problem.
     * Messages are queued to arrive at specific times, and those times line
     * up with things like tweens finishing. If we send the message at the
     * exact time given, then it'll be on the same cycle that would be rendering
     * the last frame of a tween (such as an object going off the screen).
     * However, when we send the message, we're likely to set up a new screen,
     * which causes everything to stop in place; this results in actors
     * occasionally not quite finishing their tweens.
     * Let's delay all messages that have a non-zero time an extra frame. */
    if (queued_messages_[i].delay_remaining_ > 0.0001f) {
      queued_messages_[i].delay_remaining_ -= delta_time;
      queued_messages_[i].delay_remaining_ =
          std::max(queued_messages_[i].delay_remaining_, 0.0001f);
    } else {
      queued_messages_[i].delay_remaining_ -= delta_time;
    }
  }

  /* Now dispatch messages. If the number of messages on the queue changes
   * within HandleScreenMessage, someone cleared messages on the queue. This
   * means we have no idea where 'i' is, so start over. Since we applied time
   * already, this won't cause messages to be mistimed. */
  for (unsigned i = 0; i < queued_messages_.size(); i++) {
    if (queued_messages_[i].delay_remaining_ > 0.0f) {
      continue; /* not yet */
    }

    // Remove the message from the list.
    const ScreenMessage sm = queued_messages_[i].sm_;
    queued_messages_.erase(queued_messages_.begin() + i);
    i--;

    unsigned size = queued_messages_.size();

    // send this sucker!
    CHECKPOINT_M(ssprintf(
        "ScreenMessage(%s)",
        ScreenMessageHelpers::ScreenMessageToString(sm).c_str()));
    this->HandleScreenMessage(sm);

    // If the size changed, start over.
    if (size != queued_messages_.size()) {
      i = 0;
    }
  }
}

/* Returns true if the input was handled, or false if not handled.  For
 * overlays, this determines whether the event will be propagated to lower
 * screens (i.e. it propagates from an overlay only when this returns false). */
bool Screen::Input(const InputEventPlus& input) {
  Message msg("");
  if (codes_.InputMessage(input, msg)) {
    this->HandleMessage(msg);
    return true;
  }

  // Don't send release messages with the default handler.
  switch (input.type_) {
    case IET_FIRST_PRESS:
    case IET_REPEAT:
      break;  // OK
    default:
      return false;  // don't care
  }

  // Always broadcast mouse input so themers can grab it. -aj
  if (input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_LEFT)) {
    MESSAGEMAN->Broadcast((MessageID)(Message_LeftClick));
  }
  if (input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_RIGHT)) {
    MESSAGEMAN->Broadcast((MessageID)(Message_RightClick));
  }
  if (input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_MIDDLE)) {
    MESSAGEMAN->Broadcast((MessageID)(Message_MiddleClick));
  }
  // Can't do MouseWheelUp and MouseWheelDown at the same time. -aj
  if (input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_WHEELUP)) {
    MESSAGEMAN->Broadcast((MessageID)(Message_MouseWheelUp));
  } else if (input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_WHEELDOWN)) {
    MESSAGEMAN->Broadcast((MessageID)(Message_MouseWheelDown));
  }

  // default input handler used by most menus
  switch (input.menu_i_) {
    case GAME_BUTTON_MENUUP:
      return this->MenuUp(input);
    case GAME_BUTTON_MENUDOWN:
      return this->MenuDown(input);
    case GAME_BUTTON_MENULEFT:
      return this->MenuLeft(input);
    case GAME_BUTTON_MENURIGHT:
      return this->MenuRight(input);
    case GAME_BUTTON_BACK:
      // Only go back on first press.  If somebody is backing out of the
      // options screen, they might still be holding it when select music
      // appears, and accidentally back out of that too. -Kyz
      if (input.type_ == IET_FIRST_PRESS) {
        if (HANDLE_BACK_BUTTON) {
          return this->MenuBack(input);
        }
      }
      return false;
    case GAME_BUTTON_START:
      return this->MenuStart(input);
    case GAME_BUTTON_SELECT:
      return this->MenuSelect(input);
    case GAME_BUTTON_RESTART:
      return this->MenuRestart(input);
    case GAME_BUTTON_COIN:
      return this->MenuCoin(input);
    default:
      return false;
  }
}

void Screen::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_GoToNextScreen || sm == SM_GoToPrevScreen) {
    if (SCREENMAN->IsStackedScreen(this)) {
      SCREENMAN->PopTopScreen(send_on_pop_);
    } else {
      std::string to_screen =
          (sm == SM_GoToNextScreen ? GetNextScreenName() : GetPrevScreen());
      if (to_screen == "") {
        LuaHelpers::ReportScriptError("Error:  Tried to go to empty screen.");
      } else {
        SCREENMAN->SetNewScreen(to_screen);
      }
    }
  } else if (sm == SM_GainFocus) {
    if (REPEAT_RATE != -1.0f) {
      INPUTFILTER->SetRepeatRate(REPEAT_RATE);
    }
    if (REPEAT_DELAY != -1.0f) {
      INPUTFILTER->SetRepeatDelay(REPEAT_DELAY);
    }

    LIGHTSMAN->SetLightsMode(LIGHTS_MODE);
  } else if (sm == SM_LoseFocus) {
    INPUTFILTER->ResetRepeatRate();
  }
}

std::string Screen::GetNextScreenName() const {
  if (!next_screen_.empty()) {
    return next_screen_;
  }
  return NEXT_SCREEN;
}

void Screen::SetNextScreenName(const std::string& name) { next_screen_ = name; }

void Screen::SetPrevScreenName(const std::string& name) { prev_screen_ = name; }

std::string Screen::GetPrevScreen() const {
  if (!prev_screen_.empty()) {
    return prev_screen_;
  }
  return PREV_SCREEN;
}

void Screen::PostScreenMessage(const ScreenMessage sm, float delay) {
  ASSERT(delay >= 0.0);

  QueuedScreenMessage qsm;
  qsm.sm_ = sm;
  qsm.delay_remaining_ = delay;
  queued_messages_.push_back(qsm);
}

void Screen::ClearMessageQueue() { queued_messages_.clear(); }

void Screen::ClearMessageQueue(const ScreenMessage sm) {
  for (int i = queued_messages_.size() - 1; i >= 0; i--) {
    if (queued_messages_[i].sm_ == sm) {
      queued_messages_.erase(queued_messages_.begin() + i);
    }
  }
}

bool Screen::PassInputToLua(const InputEventPlus& input) {
  if (input_callbacks_.empty() || lock_input_secs_ > 0.0f ||
      !AllowCallbackInput()) {
    return false;
  }
  calling_input_callbacks_ = true;
  bool handled = false;
  Lua* L = LUA->Get();

  // Construct the table once, and reuse it.
  lua_createtable(L, 0, 7);
  {  // This block is meant to improve clarity.  A subtable is created for
    // storing the DeviceInput member.
    lua_createtable(L, 0, 8);
    Enum::Push(L, input.device_i_.device_);
    lua_setfield(L, -2, "device");
    Enum::Push(L, input.device_i_.button_);
    lua_setfield(L, -2, "button");
    lua_pushnumber(L, input.device_i_.level_);
    lua_setfield(L, -2, "level");
    lua_pushinteger(L, input.device_i_.z_);
    lua_setfield(L, -2, "z");
    lua_pushboolean(L, input.device_i_.down_);
    lua_setfield(L, -2, "down");
    lua_pushnumber(L, input.device_i_.ts_.Ago());
    lua_setfield(L, -2, "ago");
    lua_pushboolean(L, input.device_i_.IsJoystick());
    lua_setfield(L, -2, "is_joystick");
    lua_pushboolean(L, input.device_i_.IsMouse());
    lua_setfield(L, -2, "is_mouse");
  }
  lua_setfield(L, -2, "DeviceInput");
  Enum::Push(L, input.game_i_.controller_);
  lua_setfield(L, -2, "controller");
  LuaHelpers::Push(
      L,
      GameButtonToString(INPUTMAPPER->GetInputScheme(), input.game_i_.button_));
  lua_setfield(L, -2, "button");
  Enum::Push(L, input.type_);
  lua_setfield(L, -2, "type");
  LuaHelpers::Push(
      L, GameButtonToString(INPUTMAPPER->GetInputScheme(), input.menu_i_));
  lua_setfield(L, -2, "GameButton");
  Enum::Push(L, input.pn_);
  lua_setfield(L, -2, "PlayerNumber");
  for (std::map<callback_key_t, LuaReference>::iterator callback =
           input_callbacks_.begin();
       callback != input_callbacks_.end() && !handled; ++callback) {
    callback->second.PushSelf(L);
    lua_pushvalue(L, -2);
    std::string error = "Error running input callback: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 1, true);
    handled = lua_toboolean(L, -1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  LUA->Release(L);
  calling_input_callbacks_ = false;
  if (!delayed_callback_removals_.empty()) {
    for (std::vector<callback_key_t>::iterator key =
             delayed_callback_removals_.begin();
         key != delayed_callback_removals_.end(); ++key) {
      InternalRemoveCallback(*key);
    }
  }
  return handled;
}

void Screen::AddInputCallbackFromStack(lua_State* L) {
  callback_key_t key = lua_topointer(L, 1);
  input_callbacks_[key] = LuaReference(L);
}

void Screen::RemoveInputCallback(lua_State* L) {
  callback_key_t key = lua_topointer(L, 1);
  if (calling_input_callbacks_) {
    delayed_callback_removals_.push_back(key);
  } else {
    InternalRemoveCallback(key);
  }
}

void Screen::InternalRemoveCallback(callback_key_t key) {
  std::map<callback_key_t, LuaReference>::iterator iter =
      input_callbacks_.find(key);
  if (iter != input_callbacks_.end()) {
    input_callbacks_.erase(iter);
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Screen. */
class LunaScreen : public Luna<Screen> {
 public:
  static int GetNextScreenName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetNextScreenName().c_str());
    return 1;
  }
  static int SetNextScreenName(T* p, lua_State* L) {
    p->SetNextScreenName(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetPrevScreenName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetPrevScreen().c_str());
    return 1;
  }
  static int SetPrevScreenName(T* p, lua_State* L) {
    p->SetPrevScreenName(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int lockinput(T* p, lua_State* L) {
    p->SetLockInputSecs(FArg(1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetScreenType, GetScreenType())

  static int PostScreenMessage(T* p, lua_State* L) {
    std::string message = SArg(1);
    ScreenMessage sm = ScreenMessageHelpers::ToScreenMessage(message);
    p->PostScreenMessage(sm, IArg(2));
    COMMON_RETURN_SELF;
  }

  static int AddInputCallback(T* p, lua_State* L) {
    if (!lua_isfunction(L, 1)) {
      luaL_error(L, "Input callback must be a function.");
    }
    p->AddInputCallbackFromStack(L);
    COMMON_RETURN_SELF;
  }

  static int RemoveInputCallback(T* p, lua_State* L) {
    if (!lua_isfunction(L, 1)) {
      luaL_error(L, "Input callback must be a function.");
    }
    p->RemoveInputCallback(L);
    COMMON_RETURN_SELF;
  }

  LunaScreen() {
    ADD_METHOD(GetNextScreenName);
    ADD_METHOD(SetNextScreenName);
    ADD_METHOD(GetPrevScreenName);
    ADD_METHOD(SetPrevScreenName);
    ADD_METHOD(PostScreenMessage);
    ADD_METHOD(lockinput);
    ADD_METHOD(GetScreenType);
    ADD_METHOD(AddInputCallback);
    ADD_METHOD(RemoveInputCallback);
  }
};

LUA_REGISTER_DERIVED_CLASS(Screen, ActorFrame)
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
