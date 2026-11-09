#include "RageInput.h"

#include <map>

#include "LocalizedString.h"
#include "LuaManager.h"
#include "Preference.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/InputHandler/InputHandler.h"
#include "global.h"

#if LINUX
// FIXME: bit gross to include this here, see GH issue #73:
// https://github.com/itgmania/itgmania/issues/73#issuecomment-2597176788
#include "arch/InputHandler/LinuxInputManager.h"
#endif

#include <string>
#include <vector>

RageInput* INPUTMAN =
    nullptr;  // global and accessible from anywhere in our program

Preference<std::string> input_drivers(
    "InputDrivers", "");  // "" == DEFAULT_INPUT_DRIVER_LIST
Preference<std::string> input_device_order(
    "InputDeviceOrder", "");  // "" == DEFAULT_LINUX_INPUT_DEVICE_ORDER_LIST

namespace {
struct LoadedInputHandler {
  InputHandler* device_;
};
std::vector<LoadedInputHandler> input_handlers;
std::map<InputDevice, InputHandler*> map_device_to_handler;
}  // namespace

RageInput::RageInput() {
  LOG->Trace("RageInput::RageInput()");

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "INPUTMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  LoadDrivers();
}

RageInput::~RageInput() {
  // Delete optional devices.
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    delete input_handlers[i].device_;
  }
  input_handlers.clear();
  map_device_to_handler.clear();

  // Unregister with Lua.
  LUA->UnsetGlobal("INPUTMAN");
}

static LocalizedString NO_INPUT_DEVICES_LOADED(
    "RageInput", "No input devices were loaded.");
void RageInput::LoadDrivers() {
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    delete input_handlers[i].device_;
  }
  input_handlers.clear();
  map_device_to_handler.clear();

#if LINUX
  // Recreating this forces it to re-scan for devices.
  if (LINUXINPUT != nullptr) {
    delete LINUXINPUT;
  }
  LINUXINPUT = new LinuxInputManager;
#endif

  // Init optional devices.
  std::vector<InputHandler*> devices;

  InputHandler::Create(input_drivers, devices);
  for (unsigned i = 0; i < devices.size(); ++i) {
    AddHandler(devices[i]);
  }

  // If no input devices are loaded, the user won't be able to input anything.
  if (devices.size() == 0) {
    LOG->Warn("%s", NO_INPUT_DEVICES_LOADED.GetValue().c_str());
  }
}

void RageInput::Update() {
  // Update optional devices.
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    input_handlers[i].device_->Update();
  }
}

bool RageInput::DevicesChanged() {
  // Update optional devices.
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    if (input_handlers[i].device_->DevicesChanged()) {
      return true;
    }
  }
  return false;
}

void RageInput::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) const {
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    input_handlers[i].device_->GetDevicesAndDescriptions(devices_out);
  }
}

void RageInput::WindowReset() {
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    input_handlers[i].device_->WindowReset();
  }
}

void RageInput::AddHandler(InputHandler* handler) {
  ASSERT(handler != nullptr);

  LoadedInputHandler hand;
  hand.device_ = handler;
  input_handlers.push_back(hand);

  std::vector<InputDeviceInfo> device_info;
  hand.device_->GetDevicesAndDescriptions(device_info);
  for (const InputDeviceInfo& idi : device_info) {
    map_device_to_handler[idi.id_] = handler;
  }
}

/** @brief Return the first InputDriver for the requested InputDevice. */
InputHandler* RageInput::GetHandlerForDevice(const InputDevice id) {
  std::map<InputDevice, InputHandler*>::iterator it =
      map_device_to_handler.find(id);
  if (it == map_device_to_handler.end()) {
    return nullptr;
  }
  return it->second;
}

std::string RageInput::GetDeviceSpecificInputString(const DeviceInput& di) {
  InputHandler* driver = GetHandlerForDevice(di.device_);
  if (driver != nullptr) {
    return driver->GetDeviceSpecificInputString(di);
  } else {
    return di.ToString();
  }
}

std::string RageInput::GetLocalizedInputString(const DeviceInput& di) {
  InputHandler* driver = GetHandlerForDevice(di.device_);
  if (driver != nullptr) {
    return driver->GetLocalizedInputString(di);
  } else {
    return Capitalize(DeviceButtonToString(di.button_));
  }
}

wchar_t RageInput::DeviceInputToChar(
    DeviceInput di, bool use_current_key_modifiers) {
  InputHandler* driver = GetHandlerForDevice(di.device_);
  if (driver != nullptr) {
    return driver->DeviceButtonToChar(di.button_, use_current_key_modifiers);
  } else {
    return '\0';
  }
}

std::string RageInput::GetDisplayDevicesString() const {
  std::vector<InputDeviceInfo> devices;
  GetDevicesAndDescriptions(devices);

  std::vector<std::string> vs;
  for (unsigned i = 0; i < devices.size(); ++i) {
    const std::string& description = devices[i].desc_;
    InputDevice id = devices[i].id_;
    if (description == "MonkeyKeyboard") {
      continue;  // hide this
    }
    vs.push_back(ssprintf(
        "%s (%s)", description.c_str(), InputDeviceToString(id).c_str()));
  }
  return join("\n", vs);
}

void RageInput::StartSensorTest() {
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    input_handlers[i].device_->StartSensorDebugging();
  }
}
void RageInput::StopSensorTest() {
  for (unsigned i = 0; i < input_handlers.size(); ++i) {
    input_handlers[i].device_->StopSensorDebugging();
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to RageInput. */
class LunaRageInput : public Luna<RageInput> {
 public:
  static int GetDescriptions(T* p, lua_State* L) {
    std::vector<InputDeviceInfo> devices;
    p->GetDevicesAndDescriptions(devices);
    std::vector<std::string> descriptions;
    for (const InputDeviceInfo& idi : devices) {
      descriptions.push_back(idi.desc_);
    }
    LuaHelpers::CreateTableFromArray(descriptions, L);
    return 1;
  }

  static int StartSensorTest(T* p, lua_State* L) {
    p->StartSensorTest();
    COMMON_RETURN_SELF;
  }

  static int StopSensorTest(T* p, lua_State* L) {
    p->StopSensorTest();
    COMMON_RETURN_SELF;
  }

  LunaRageInput() {
    ADD_METHOD(GetDescriptions);
    ADD_METHOD(StartSensorTest);
    ADD_METHOD(StopSensorTest);
  }
};

LUA_REGISTER_CLASS(RageInput)
// lua end

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
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
