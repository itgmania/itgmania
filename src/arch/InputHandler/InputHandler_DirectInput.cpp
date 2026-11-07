#include "InputHandler_DirectInput.h"

#include "GamePreferences.h"  //needed for Axis Fix
#include "InputFilter.h"
#include "InputHandler_DirectInputHelper.h"
#include "PrefsManager.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/AppInstance.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/ErrorStrings.h"
#include "archutils/Win32/GraphicsWindow.h"
#include "archutils/Win32/RegistryAccess.h"
#include "global.h"

#ifdef NTDDI_WIN8  // Link to Xinput9_1_0.lib on Windows 8 SDK and above to
                   // ensure linkage to Xinput9_1_0.dll
#pragma comment(lib, "Xinput9_1_0.lib")
#else
#pragma comment(lib, "xinput.lib")
#endif

#include <OleAuto.h>
#include <WbemIdl.h>
#include <XInput.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

// this may not be defined if we are using an older Windows SDK. (for instance,
// toolsetversion v140_xp does not define it) the number was taken from the
// documentation
#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif

REGISTER_INPUT_HANDLER_CLASS2(DirectInput, DInput);

static std::vector<DIDevice> Devices;
static std::vector<XIDevice> XDevices;

// Number of joysticks found:
static int g_iNumJoysticks;

template <typename T>
inline void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

static BOOL IsXInputDevice(const GUID* guid_product_from_direct_input) {
  IWbemLocator* wbem_locator = nullptr;
  IEnumWbemClassObject* enum_devices = nullptr;
  IWbemClassObject* devices[20] = {0};
  IWbemServices* wbem_services = nullptr;
  BSTR bstr_namespace = nullptr;
  BSTR bstr_device_id = nullptr;
  BSTR bstr_class_name = nullptr;
  DWORD returned = 0;
  bool is_xinput_device = false;
  UINT device = 0;
  VARIANT var;
  HRESULT hr;

  // CoInit if needed
  hr = CoInitialize(nullptr);
  bool cleanup_com = SUCCEEDED(hr);

  // Create WMI
  hr = CoCreateInstance(
      __uuidof(WbemLocator), nullptr, CLSCTX_INPROC_SERVER,
      __uuidof(IWbemLocator), (LPVOID*)&wbem_locator);
  if (FAILED(hr) || wbem_locator == nullptr) {
    goto LCleanup;
  }

  bstr_namespace = SysAllocString(L"\\\\.\\root\\cimv2");
  if (bstr_namespace == nullptr) {
    goto LCleanup;
  }
  bstr_class_name = SysAllocString(L"Win32_PNPEntity");
  if (bstr_class_name == nullptr) {
    goto LCleanup;
  }
  bstr_device_id = SysAllocString(L"DeviceID");
  if (bstr_device_id == nullptr) {
    goto LCleanup;
  }

  // Connect to WMI
  hr = wbem_locator->ConnectServer(
      bstr_namespace, nullptr, nullptr, 0L, 0L, nullptr, nullptr,
      &wbem_services);
  if (FAILED(hr) || wbem_services == nullptr) {
    goto LCleanup;
  }

  // Switch security level to IMPERSONATE.
  CoSetProxyBlanket(
      wbem_services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

  hr = wbem_services->CreateInstanceEnum(
      bstr_class_name, 0, nullptr, &enum_devices);
  if (FAILED(hr) || enum_devices == nullptr) {
    goto LCleanup;
  }

  // Loop over all devices
  for (;;) {
    // Get 20 at a time
    hr = enum_devices->Next(10000, 20, devices, &returned);
    if (FAILED(hr)) {
      goto LCleanup;
    }
    if (returned == 0) {
      break;
    }

    for (device = 0; device < returned; device++) {
      // For each device, get its device ID
      hr = devices[device]->Get(bstr_device_id, 0L, &var, nullptr, nullptr);
      if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal != nullptr) {
        // Check if the device ID contains "IG_".  If it does, then it's an
        // XInput device This information can not be found from DirectInput
        if (wcsstr(var.bstrVal, L"IG_")) {
          // If it does, then get the VID/PID from var.bstrVal
          DWORD pid = 0, vid = 0;
          WCHAR* str_vid = wcsstr(var.bstrVal, L"VID_");
          if (str_vid && swscanf(str_vid, L"VID_%4X", &vid) != 1) {
            vid = 0;
          }
          WCHAR* str_pid = wcsstr(var.bstrVal, L"PID_");
          if (str_pid && swscanf(str_pid, L"PID_%4X", &pid) != 1) {
            pid = 0;
          }

          // Compare the VID/PID to the DInput device
          DWORD vid_pid = MAKELONG(vid, pid);
          if (vid_pid == guid_product_from_direct_input->Data1) {
            is_xinput_device = true;
            goto LCleanup;
          }
        }
      }
      SafeRelease(devices[device]);
    }
  }

LCleanup:
  if (bstr_namespace) {
    SysFreeString(bstr_namespace);
  }
  if (bstr_device_id) {
    SysFreeString(bstr_device_id);
  }
  if (bstr_class_name) {
    SysFreeString(bstr_class_name);
  }
  for (device = 0; device < 20; device++) {
    SafeRelease(devices[device]);
  }
  SafeRelease(enum_devices);
  SafeRelease(wbem_locator);
  SafeRelease(wbem_services);

  if (cleanup_com) {
    CoUninitialize();
  }

  return is_xinput_device;
}

static BOOL CALLBACK
EnumDevicesCallback(const DIDEVICEINSTANCE* did_instance, void* context) {
  DIDevice device;

  LOG->Info(
      "DInput: Enumerating device - Type: 0x%08X Instance Name: \"%s\" Product "
      "Name: \"%s\"",
      did_instance->dwDevType, did_instance->tszInstanceName,
      did_instance->tszProductName);

  switch (GET_DIDEVICE_TYPE(did_instance->dwDevType)) {
    case DI8DEVTYPE_JOYSTICK:
    case DI8DEVTYPE_GAMEPAD:
    case DI8DEVTYPE_DRIVING:
    case DI8DEVTYPE_FLIGHT:
    case DI8DEVTYPE_1STPERSON:
    case DI8DEVTYPE_DEVICECTRL:
    case DI8DEVTYPE_SCREENPOINTER:
    case DI8DEVTYPE_REMOTE:
    case DI8DEVTYPE_SUPPLEMENTAL: {
      device.type_ = device.JOYSTICK;
      break;
    }

    case DI8DEVTYPE_KEYBOARD:
      device.type_ = device.KEYBOARD;
      break;
    case DI8DEVTYPE_MOUSE:
      device.type_ = device.MOUSE;
      break;
    default:
      LOG->Info("DInput: Unrecognized device ignored.");
      return DIENUM_CONTINUE;
  }

  device.joystick_inst_ = *did_instance;

  switch (device.type_) {
    case DIDevice::JOYSTICK:
      if (g_iNumJoysticks == NUM_JOYSTICKS) {
        return DIENUM_CONTINUE;
      }

      if (IsXInputDevice(&did_instance->guidProduct)) {
        return DIENUM_CONTINUE;
      }

      device.dev_ = enum_add2(DEVICE_JOY1, g_iNumJoysticks);
      g_iNumJoysticks++;
      break;

    case DIDevice::KEYBOARD:
      device.dev_ = DEVICE_KEYBOARD;
      break;

    case DIDevice::MOUSE:
      device.dev_ = DEVICE_MOUSE;
      break;
  }

  Devices.push_back(device);

  return DIENUM_CONTINUE;
}

static void CheckForDirectInputDebugMode() {
  int val;
  if (RegistryAccess::GetRegValue(
          "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\DirectInput", "emulation",
          val)) {
    if (val & 0x8) {
      LOG->Warn(
          "DirectInput keyboard debug mode appears to be enabled. This "
          "reduces\n"
          "input timing accuracy significantly. Disabling this is strongly "
          "recommended.");
    }
  }
}

static BOOL CALLBACK
CountDevicesCallback(const DIDEVICEINSTANCE* did_instance, void* context) {
  (*(int*)context)++;
  return DIENUM_CONTINUE;
}

static int GetNumHidDevices() {
  int i = 0;
  RegistryAccess::GetRegValue(
      "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\HidUsb\\Enum",
      "Count", i, false);  // don't warn on error
  return i;
}

static int GetNumJoysticksSlow() {
  int count = 0;
  HRESULT hr = g_dinput->EnumDevices(
      DI8DEVCLASS_GAMECTRL, CountDevicesCallback, &count, DIEDFL_ATTACHEDONLY);
  if (hr != DI_OK) {
    LOG->Warn(hr_ssprintf(hr, "g_dinput->EnumDevices").c_str());
  }
  return count;
}

InputHandler_DInput::InputHandler_DInput() {
  LOG->Trace("InputHandler_DInput::InputHandler_DInput()");

  CheckForDirectInputDebugMode();

  shutdown_ = false;
  g_iNumJoysticks = 0;

  // find xinput joysticks first
  for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    if (XInputGetState(i, &state) == ERROR_SUCCESS) {
      XIDevice xdevice;
      xdevice.name_ = ssprintf("XInput Device %u", i + 1);
      xdevice.dev_ = enum_add2(InputDevice::DEVICE_JOY1, g_iNumJoysticks);
      xdevice.x_input_slot_ = i;
      g_iNumJoysticks++;

      XDevices.push_back(xdevice);
    }
  }
  LOG->Info("Found %u XInput devices.", XDevices.size());

  AppInstance inst;
  HRESULT hr = DirectInput8Create(
      inst.Get(), DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID*)&g_dinput,
      nullptr);
  if (hr != DI_OK) {
    RageException::Throw(
        hr_ssprintf(hr, "InputHandler_DInput: DirectInputCreate").c_str());
  }

  LOG->Trace(
      "InputHandler_DInput: IDirectInput::EnumDevices(DIDEVTYPE_KEYBOARD)");
  hr = g_dinput->EnumDevices(
      DI8DEVCLASS_KEYBOARD, EnumDevicesCallback, nullptr, DIEDFL_ATTACHEDONLY);
  if (hr != DI_OK) {
    RageException::Throw(
        hr_ssprintf(hr, "InputHandler_DInput: IDirectInput::EnumDevices")
            .c_str());
  }

  LOG->Trace(
      "InputHandler_DInput: IDirectInput::EnumDevices(DIDEVTYPE_JOYSTICK)");
  hr = g_dinput->EnumDevices(
      DI8DEVCLASS_GAMECTRL, EnumDevicesCallback, nullptr, DIEDFL_ATTACHEDONLY);
  if (hr != DI_OK) {
    RageException::Throw(
        hr_ssprintf(hr, "InputHandler_DInput: IDirectInput::EnumDevices")
            .c_str());
  }

  // mouse
  LOG->Trace("InputHandler_DInput: IDirectInput::EnumDevices(DIDEVTYPE_MOUSE)");
  hr = g_dinput->EnumDevices(
      DI8DEVCLASS_POINTER, EnumDevicesCallback, nullptr, DIEDFL_ATTACHEDONLY);
  if (hr != DI_OK) {
    RageException::Throw(
        hr_ssprintf(hr, "InputHandler_DInput: IDirectInput::EnumDevices")
            .c_str());
  }

  for (unsigned i = 0; i < Devices.size(); ++i) {
    if (Devices[i].Open()) {
      continue;
    }

    Devices.erase(Devices.begin() + i);
    i--;
    continue;
  }

  LOG->Info("Found %u DirectInput devices:", Devices.size());
  for (unsigned i = 0; i < Devices.size(); ++i) {
    LOG->Info(
        "   %d: '%s' axes: %d, hats: %d, buttons: %d (%s)", i,
        Devices[i].name_.c_str(), Devices[i].axes_, Devices[i].hats_,
        Devices[i].buttons_, Devices[i].buffered_ ? "buffered" : "unbuffered");
  }

  last_seen_num_hid_devices_ = GetNumHidDevices();
  num_times_left_to_poll_for_joysticks_changed_ = 0;
  last_seen_num_joysticks_ = GetNumJoysticksSlow();

  StartThread();
}

void InputHandler_DInput::StartThread() {
  ASSERT(!input_thread_.IsCreated());
  if (PREFSMAN->threaded_input_) {
    input_thread_.SetName("DirectInput thread");
    input_thread_.Create(InputThread_Start, this);
  }
}

void InputHandler_DInput::ShutdownThread() {
  shutdown_ = true;
  if (input_thread_.IsCreated()) {
    LOG->Trace("Shutting down DirectInput thread ...");
    input_thread_.Wait();
    LOG->Trace("DirectInput thread shut down.");
  }
  shutdown_ = false;
}

InputHandler_DInput::~InputHandler_DInput() {
  XDevices.clear();

  ShutdownThread();

  for (unsigned i = 0; i < Devices.size(); ++i) {
    Devices[i].Close();
  }

  Devices.clear();
  g_dinput->Release();
  g_dinput = nullptr;
}

void InputHandler_DInput::WindowReset() {
  // We need to reopen keyboards.
  ShutdownThread();

  for (unsigned i = 0; i < Devices.size(); ++i) {
    if (Devices[i].type_ != Devices[i].KEYBOARD) {
      continue;
    }

    Devices[i].Close();

    // We lose buffered inputs here, so we need to clear all pressed keys.
    INPUTFILTER->ResetDevice(Devices[i].dev_);

    bool ret = Devices[i].Open();

    // Reopening it should succeed.
    ASSERT(ret);
  }

  StartThread();
}

#define HAT_UP_MASK 1
#define HAT_DOWN_MASK 2
#define HAT_LEFT_MASK 4
#define HAT_RIGHT_MASK 8

static int TranslatePOV(DWORD value) {
  const int kHatVals[] = {HAT_UP_MASK,    HAT_UP_MASK | HAT_RIGHT_MASK,
                          HAT_RIGHT_MASK, HAT_DOWN_MASK | HAT_RIGHT_MASK,
                          HAT_DOWN_MASK,  HAT_DOWN_MASK | HAT_LEFT_MASK,
                          HAT_LEFT_MASK,  HAT_UP_MASK | HAT_LEFT_MASK};

  if (LOWORD(value) == 0xFFFF) {
    return 0;
  }

  // Round the value up:
  value += 4500 / 2;
  value %= 36000;
  value /= 4500;

  if (value >= 8) {
    return 0;  // shouldn't happen
  }

  return kHatVals[value];
}

static HRESULT GetDeviceState(LPDIRECTINPUTDEVICE8 dev, int size, void* ptr) {
  HRESULT hr = dev->GetDeviceState(size, ptr);
  if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
    hr = dev->Acquire();
    if (hr != DI_OK) {
      LOG->Trace(hr_ssprintf(hr, "?").c_str());
      return hr;
    }

    hr = dev->GetDeviceState(size, ptr);
  }

  return hr;
}

/* This doesn't take a timestamp; instead, we let InputHandler::ButtonPressed
 * figure it out. Be sure to call InputHandler::Update() between each poll. */
void InputHandler_DInput::UpdatePolled(DIDevice& device, const RageTimer& tm) {
  switch (device.type_) {
    default:
      FAIL_M(ssprintf("Unsupported DI device type: %i", device.type_));
    case DIDevice::KEYBOARD: {
      unsigned char keys[256];

      HRESULT hr = GetDeviceState(device.device_, 256, keys);
      if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        return;
      }

      if (hr != DI_OK) {
        LOG->MapLog(
            "UpdatePolled",
            hr_ssprintf(hr, "Failures on polled keyboard update").c_str());
        return;
      }

      for (int k = 0; k < 256; ++k) {
        const DeviceButton key = (DeviceButton)device.inputs_[k].num_;
        ButtonPressed(DeviceInput(device.dev_, key, !!(keys[k] & 0x80)));
      }
    } break;
    case DIDevice::JOYSTICK: {
      DIJOYSTATE state;

      HRESULT hr = GetDeviceState(device.device_, sizeof(state), &state);
      if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        return;
      }

      // Set each known axis, button and POV.
      for (unsigned i = 0; i < device.inputs_.size(); ++i) {
        const input_t& in = device.inputs_[i];
        const InputDevice dev = device.dev_;

        switch (in.type_) {
          case input_t::BUTTON: {
            DeviceInput di(
                dev, enum_add2(JOY_BUTTON_1, in.num_),
                !!state.rgbButtons[in.ofs_ - DIJOFS_BUTTON0], tm);
            ButtonPressed(di);
            break;
          }

          case input_t::AXIS: {
            DeviceButton neg = DeviceButton_Invalid, pos = DeviceButton_Invalid;
            int val = 0;
            if (in.ofs_ == DIJOFS_X) {
              neg = JOY_LEFT;
              pos = JOY_RIGHT;
              val = state.lX;
            } else if (in.ofs_ == DIJOFS_Y) {
              neg = JOY_UP;
              pos = JOY_DOWN;
              val = state.lY;
            } else if (in.ofs_ == DIJOFS_Z) {
              neg = JOY_Z_UP;
              pos = JOY_Z_DOWN;
              val = state.lZ;
            } else if (in.ofs_ == DIJOFS_RX) {
              neg = JOY_ROT_LEFT;
              pos = JOY_ROT_RIGHT;
              val = state.lRx;
            } else if (in.ofs_ == DIJOFS_RY) {
              neg = JOY_ROT_UP;
              pos = JOY_ROT_DOWN;
              val = state.lRy;
            } else if (in.ofs_ == DIJOFS_RZ) {
              neg = JOY_ROT_Z_UP;
              pos = JOY_ROT_Z_DOWN;
              val = state.lRz;
            } else if (in.ofs_ == DIJOFS_SLIDER(0)) {
              neg = JOY_AUX_1;
              pos = JOY_AUX_2;
              val = state.rglSlider[0];
            } else if (in.ofs_ == DIJOFS_SLIDER(1)) {
              neg = JOY_AUX_3;
              pos = JOY_AUX_4;
              val = state.rglSlider[1];
            } else {
              LOG->MapLog(
                  "unknown input",
                  "Controller '%s' is returning an unknown joystick offset, %i",
                  device.name_.c_str(), in.ofs_);
            }

            if (neg != DeviceButton_Invalid) {
              float l = SCALE(int(val), 0.0f, 100.0f, 0.0f, 1.0f);
              ButtonPressed(DeviceInput(dev, neg, std::max(-l, 0.0f), tm));
              ButtonPressed(DeviceInput(dev, pos, std::max(+l, 0.0f), tm));
            }

            break;
          }

          case input_t::HAT:
            if (in.num_ == 0) {
              const int pos =
                  TranslatePOV(state.rgdwPOV[in.ofs_ - DIJOFS_POV(0)]);
              ButtonPressed(
                  DeviceInput(dev, JOY_HAT_UP, !!(pos & HAT_UP_MASK), tm));
              ButtonPressed(
                  DeviceInput(dev, JOY_HAT_DOWN, !!(pos & HAT_DOWN_MASK), tm));
              ButtonPressed(
                  DeviceInput(dev, JOY_HAT_LEFT, !!(pos & HAT_LEFT_MASK), tm));
              ButtonPressed(DeviceInput(
                  dev, JOY_HAT_RIGHT, !!(pos & HAT_RIGHT_MASK), tm));
            }

            break;
        }
      }
    } break;
    case DIDevice::MOUSE:
      // Not 100% perfect (or tested much) yet. -aj
      DIMOUSESTATE state;

      HRESULT hr = GetDeviceState(device.device_, sizeof(state), &state);
      if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        return;
      }

      // reset mousewheel
      ButtonPressed(DeviceInput(device.dev_, MOUSE_WHEELUP, 0, tm));
      ButtonPressed(DeviceInput(device.dev_, MOUSE_WHEELDOWN, 0, tm));

      for (unsigned i = 0; i < device.inputs_.size(); ++i) {
        const input_t& in = device.inputs_[i];
        const InputDevice dev = device.dev_;

        switch (in.type_) {
          case input_t::BUTTON: {
            DeviceInput di(
                dev, enum_add2(MOUSE_LEFT, in.num_),
                !!state.rgbButtons[in.ofs_ - DIMOFS_BUTTON0], tm);
            ButtonPressed(di);
            break;
          }
          case input_t::AXIS: {
            DeviceButton neg = DeviceButton_Invalid, pos = DeviceButton_Invalid;
            int val = 0;
            if (in.ofs_ == DIMOFS_X) {
              neg = MOUSE_X_LEFT;
              pos = MOUSE_X_RIGHT;
              val = state.lX;
            } else if (in.ofs_ == DIMOFS_Y) {
              neg = MOUSE_Y_UP;
              pos = MOUSE_Y_DOWN;
              val = state.lY;
            } else if (in.ofs_ == DIMOFS_Z) {
              neg = MOUSE_WHEELDOWN;
              pos = MOUSE_WHEELUP;
              val = state.lZ;
              // LOG->Trace("MouseWheel polled: %i",val);
              INPUTFILTER->UpdateMouseWheel(val);
              if (val == 0) {
                // release all
                ButtonPressed(DeviceInput(dev, pos, 0, tm));
                ButtonPressed(DeviceInput(dev, neg, 0, tm));
              } else if (int(val) > 0) {
                // positive values: WheelUp
                ButtonPressed(DeviceInput(dev, pos, 1, tm));
                ButtonPressed(DeviceInput(dev, neg, 0, tm));
              } else if (int(val) < 0) {
                // negative values: WheelDown
                ButtonPressed(DeviceInput(dev, neg, 1, tm));
                ButtonPressed(DeviceInput(dev, pos, 0, tm));
              }
            } else {
              LOG->MapLog(
                  "unknown input",
                  "Mouse '%s' is returning an unknown mouse offset, %i",
                  device.name_.c_str(), in.ofs_);
            }
            break;
          }
        }
      }
      break;
  }
}

void InputHandler_DInput::UpdateBuffered(
    DIDevice& device, const RageTimer& tm) {
  DWORD numevents;
  DIDEVICEOBJECTDATA evtbuf[INPUT_QSIZE];

  numevents = INPUT_QSIZE;
  HRESULT hr = device.device_->GetDeviceData(
      sizeof(DIDEVICEOBJECTDATA), evtbuf, &numevents, 0);
  if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
    INPUTFILTER->ResetDevice(device.dev_);
    return;
  }

  if (hr != DI_OK) {
    LOG->Trace(
        hr_ssprintf(hr, "UpdateBuffered: IDirectInputDevice2_GetDeviceData")
            .c_str());
    return;
  }

  if (GetForegroundWindow() != GraphicsWindow::GetHwnd()) {
    // Discard input when not focused, and release all keys.
    INPUTFILTER->ResetDevice(device.dev_);
    return;
  }

  // reset mousewheel
  ButtonPressed(DeviceInput(device.dev_, MOUSE_WHEELUP, 0.0f, tm));
  ButtonPressed(DeviceInput(device.dev_, MOUSE_WHEELDOWN, 0.0f, tm));

  for (int i = 0; i < (int)numevents; ++i) {
    for (unsigned j = 0; j < device.inputs_.size(); ++j) {
      const input_t& in = device.inputs_[j];
      const InputDevice dev = device.dev_;

      if (evtbuf[i].dwOfs != in.ofs_) {
        continue;
      }

      switch (in.type_) {
        case input_t::KEY:
          ButtonPressed(DeviceInput(
              dev, (DeviceButton)in.num_, !!(evtbuf[i].dwData & 0x80), tm));
          break;

        case input_t::BUTTON:
          if (dev == DEVICE_MOUSE) {
            DeviceButton mouse_input = DeviceButton_Invalid;

            if (in.ofs_ == DIMOFS_BUTTON0) {
              mouse_input = MOUSE_LEFT;
            } else if (in.ofs_ == DIMOFS_BUTTON1) {
              mouse_input = MOUSE_RIGHT;
            } else if (in.ofs_ == DIMOFS_BUTTON2) {
              mouse_input = MOUSE_MIDDLE;
            } else {
              LOG->MapLog(
                  "unknown input",
                  "Mouse '%s' is returning an unknown mouse offset [button], "
                  "%i",
                  device.name_.c_str(), in.ofs_);
            }
            ButtonPressed(
                DeviceInput(dev, mouse_input, !!evtbuf[i].dwData, tm));
          } else {
            ButtonPressed(DeviceInput(
                dev, enum_add2(JOY_BUTTON_1, in.num_), !!evtbuf[i].dwData, tm));
          }
          break;

        case input_t::AXIS: {
          DeviceButton up = DeviceButton_Invalid, down = DeviceButton_Invalid;
          if (dev == DEVICE_MOUSE) {
            float l = int(evtbuf[i].dwData);
            POINT cursor_pos;
            GetCursorPos(&cursor_pos);
            // convert screen coordinates to client
            ScreenToClient(GraphicsWindow::GetHwnd(), &cursor_pos);

            if (in.ofs_ == DIMOFS_X) {
              INPUTFILTER->UpdateCursorLocation(
                  (float)cursor_pos.x, (float)cursor_pos.y);
            } else if (in.ofs_ == DIMOFS_Y) {
              INPUTFILTER->UpdateCursorLocation(
                  (float)cursor_pos.x, (float)cursor_pos.y);
            } else if (in.ofs_ == DIMOFS_Z) {
              // positive values: WheelUp
              // negative values: WheelDown
              INPUTFILTER->UpdateMouseWheel(l);
              {
                up = MOUSE_WHEELUP;
                down = MOUSE_WHEELDOWN;
                float wheel_delta = l;
                // l = SCALE( int(evtbuf[i].dwData), -WHEEL_DELTA,
                // WHEEL_DELTA, 1.0f, -1.0f );
                if (l > 0) {
                  DeviceInput di_up = DeviceInput(dev, up, 1.0f, tm);
                  DeviceInput di_down = DeviceInput(dev, down, 0.0f, tm);
                  // This if statement used to be a while loop.  But Kevin
                  // reported that scrolling the mouse wheel locked up input.
                  // I assume that fWheelDelta was some absurdly large value,
                  // causing an infinite loop.  Changing the while loop to an
                  // if fixed the input lockup.  Anything that wants to
                  // imitate multiple presses when the wheel is scrolled will
                  // have to do it manually. -Kyz
                  if (wheel_delta >= WHEEL_DELTA) {
                    ButtonPressed(di_up);
                    ButtonPressed(di_down);
                    INPUTFILTER->UpdateMouseWheel(wheel_delta);
                    wheel_delta -= WHEEL_DELTA;
                  }
                } else if (l < 0) {
                  DeviceInput di_down = DeviceInput(dev, down, 1.0f, tm);
                  DeviceInput di_up = DeviceInput(dev, up, 0.0f, tm);
                  // See comment for the l > 0 case. -Kyz
                  if (wheel_delta <= -WHEEL_DELTA) {
                    ButtonPressed(di_down);
                    ButtonPressed(di_up);
                    INPUTFILTER->UpdateMouseWheel(wheel_delta);
                    wheel_delta += WHEEL_DELTA;
                  }
                } else {
                  DeviceInput di_up = DeviceInput(dev, up, 0.0f, tm);
                  ButtonPressed(di_up);
                  DeviceInput di_down = DeviceInput(dev, down, 0.0f, tm);
                  ButtonPressed(di_down);
                }
              }
            } else {
              LOG->MapLog(
                  "unknown input",
                  "Mouse '%s' is returning an unknown mouse offset [axis], %i",
                  device.name_.c_str(), in.ofs_);
            }
          } else {
            // joystick
            if (in.ofs_ == DIJOFS_X) {
              up = JOY_LEFT;
              down = JOY_RIGHT;
            } else if (in.ofs_ == DIJOFS_Y) {
              up = JOY_UP;
              down = JOY_DOWN;
            } else if (in.ofs_ == DIJOFS_Z) {
              up = JOY_Z_UP;
              down = JOY_Z_DOWN;
            } else if (in.ofs_ == DIJOFS_RX) {
              up = JOY_ROT_UP;
              down = JOY_ROT_DOWN;
            } else if (in.ofs_ == DIJOFS_RY) {
              up = JOY_ROT_LEFT;
              down = JOY_ROT_RIGHT;
            } else if (in.ofs_ == DIJOFS_RZ) {
              up = JOY_ROT_Z_UP;
              down = JOY_ROT_Z_DOWN;
            } else if (in.ofs_ == DIJOFS_SLIDER(0)) {
              up = JOY_AUX_1;
              down = JOY_AUX_2;
            } else if (in.ofs_ == DIJOFS_SLIDER(1)) {
              up = JOY_AUX_3;
              down = JOY_AUX_4;
            } else {
              LOG->MapLog(
                  "unknown input",
                  "Controller '%s' is returning an unknown joystick offset, %i",
                  device.name_.c_str(), in.ofs_);
            }

            float l = SCALE(int(evtbuf[i].dwData), 0.0f, 100.0f, 0.0f, 1.0f);
            if (GamePreferences::m_AxisFix) {
              ButtonPressed(DeviceInput(dev, up, (l == 0) || (l == -1), tm));
              ButtonPressed(DeviceInput(dev, down, (l == 0) || (l == 1), tm));

            } else {
              ButtonPressed(DeviceInput(dev, up, std::max(-l, 0.0f), tm));
              ButtonPressed(DeviceInput(dev, down, std::max(+l, 0.0f), tm));
            }
          }
          break;
        }

        case input_t::HAT: {
          const int pos = TranslatePOV(evtbuf[i].dwData);
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_UP, !!(pos & HAT_UP_MASK), tm));
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_DOWN, !!(pos & HAT_DOWN_MASK), tm));
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_LEFT, !!(pos & HAT_LEFT_MASK), tm));
          ButtonPressed(
              DeviceInput(dev, JOY_HAT_RIGHT, !!(pos & HAT_RIGHT_MASK), tm));
        }
      }
    }
  }
}

inline constexpr short XINPUT_GAMEPAD_THUMB_MIN =
    std::numeric_limits<short>::min();
inline constexpr short XINPUT_GAMEPAD_THUMB_MAX =
    std::numeric_limits<short>::max();

void InputHandler_DInput::UpdateXInput(XIDevice& device, const RageTimer& tm) {
  XINPUT_STATE state;
  ZeroMemory(&state, sizeof(XINPUT_STATE));
  if (XInputGetState(device.x_input_slot_, &state) == ERROR_SUCCESS) {
    // map joysticks
    float lx = 0.f;
    float ly = 0.f;
    if (std::sqrt(
            std::pow(state.Gamepad.sThumbLX, 2) +
            std::pow(state.Gamepad.sThumbLY, 2)) >
        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
      lx = SCALE(
          state.Gamepad.sThumbLX + 0.f, XINPUT_GAMEPAD_THUMB_MIN + 0.f,
          XINPUT_GAMEPAD_THUMB_MAX + 0.f, -1.0f, 1.0f);
      ly = SCALE(
          state.Gamepad.sThumbLY + 0.f, XINPUT_GAMEPAD_THUMB_MIN + 0.f,
          XINPUT_GAMEPAD_THUMB_MAX + 0.f, -1.0f, 1.0f);
    }
    ButtonPressed(DeviceInput(device.dev_, JOY_LEFT, std::max(-lx, 0.f), tm));
    ButtonPressed(DeviceInput(device.dev_, JOY_RIGHT, std::max(+lx, 0.f), tm));
    ButtonPressed(DeviceInput(device.dev_, JOY_UP, std::max(+ly, 0.f), tm));
    ButtonPressed(DeviceInput(device.dev_, JOY_DOWN, std::max(-ly, 0.f), tm));

    float rx = 0.f;
    float ry = 0.f;
    if (std::sqrt(
            std::pow(state.Gamepad.sThumbRX, 2) +
            std::pow(state.Gamepad.sThumbRY, 2)) >
        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
      rx = SCALE(
          state.Gamepad.sThumbRX + 0.f, XINPUT_GAMEPAD_THUMB_MIN + 0.f,
          XINPUT_GAMEPAD_THUMB_MAX + 0.f, -1.0f, 1.0f);
      ry = SCALE(
          state.Gamepad.sThumbRY + 0.f, XINPUT_GAMEPAD_THUMB_MIN + 0.f,
          XINPUT_GAMEPAD_THUMB_MAX + 0.f, -1.0f, 1.0f);
    }
    ButtonPressed(DeviceInput(device.dev_, JOY_LEFT_2, std::max(-rx, 0.f), tm));
    ButtonPressed(
        DeviceInput(device.dev_, JOY_RIGHT_2, std::max(+rx, 0.f), tm));
    ButtonPressed(DeviceInput(device.dev_, JOY_UP_2, std::max(+ry, 0.f), tm));
    ButtonPressed(DeviceInput(device.dev_, JOY_DOWN_2, std::max(-ry, 0.f), tm));

    // map buttons
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_1,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_A), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_2,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_B), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_3,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_X), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_4,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_Y), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_5,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_START), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_6,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_7,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_8,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_9,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_10,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER), tm));

    // map triggers to buttons
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_11,
        !!(state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_BUTTON_12,
        !!(state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD),
        tm));

    // map hat buttons
    ButtonPressed(DeviceInput(
        device.dev_, JOY_HAT_UP,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_HAT_DOWN,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_HAT_LEFT,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT), tm));
    ButtonPressed(DeviceInput(
        device.dev_, JOY_HAT_RIGHT,
        !!(state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT), tm));
  } else {
    INPUTFILTER->ResetDevice(device.dev_);
    return;
  }
}

void InputHandler_DInput::PollAndAcquireDevices(bool buffered) {
  for (unsigned i = 0; i < Devices.size(); ++i) {
    if (Devices[i].buffered_ != buffered) {
      continue;
    }

    HRESULT hr = Devices[i].device_->Poll();
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
      INPUTFILTER->ResetDevice(Devices[i].dev_);

      /* This will fail with "access denied" on the keyboard if we don't
       * have focus. */
      hr = Devices[i].device_->Acquire();
      if (hr != DI_OK) {
        continue;
      }

      Devices[i].device_->Poll();
    }
  }
}

void InputHandler_DInput::Update() {
  // Handle polled devices. Handle buffered, too, if there's no input thread to
  // do it.
  PollAndAcquireDevices(false);
  if (!input_thread_.IsCreated()) {
    PollAndAcquireDevices(true);
  }

  for (unsigned i = 0; i < XDevices.size(); ++i) {
    UpdateXInput(XDevices[i], RageZeroTimer);
  }

  for (unsigned i = 0; i < Devices.size(); ++i) {
    if (!Devices[i].buffered_) {
      UpdatePolled(Devices[i], RageZeroTimer);
    } else if (!input_thread_.IsCreated()) {
      // If we have an input thread, it'll handle buffered devices.
      UpdateBuffered(Devices[i], RageZeroTimer);
    }
  }

  InputHandler::UpdateTimer();
}

const float POLL_FOR_JOYSTICK_CHANGES_LENGTH_SECONDS = 15.0f;
const float POLL_FOR_JOYSTICK_CHANGES_EVERY_SECONDS = 0.25f;

bool InputHandler_DInput::DevicesChanged() {
  /* GetNumJoysticksSlow() blocks DirectInput for a while even if called from a
   * different thread, so we can't poll with it. GetNumHidDevices() is fast,
   * but sometimes the DirectInput joysticks haven't updated by  the time the
   * HID registry value changes. So, poll using GetNumHidDevices(). When that
   * changes, poll using GetNumJoysticksSlow() for a little while to give
   * DirectInput time to catch up. On this XP machine (which? -aj), it takes
   * 2-10 DirectInput polls (0.5-2.5 seconds) to catch a newly installed device
   * after the registry value changes, and catches non-new plugged/unplugged
   * devices on the first DirectInputPoll. Note that this "poll for N seconds"
   * method will not work if the Add New Hardware wizard halts device
   * installation to wait for a driver. Most of the joysticks people would want
   * to use don't prompt for a driver though, and the wizard adds them pretty
   * quickly. */

  int old_num_hid_devices = last_seen_num_hid_devices_;
  last_seen_num_hid_devices_ = GetNumHidDevices();
  if (old_num_hid_devices != last_seen_num_hid_devices_) {
    LOG->Warn("HID devices changes");
    num_times_left_to_poll_for_joysticks_changed_ =
        (int)(POLL_FOR_JOYSTICK_CHANGES_LENGTH_SECONDS /
              POLL_FOR_JOYSTICK_CHANGES_EVERY_SECONDS);
  }

  if (num_times_left_to_poll_for_joysticks_changed_ > 0) {
    static RageTimer timer_poll_joysticks;
    if (timer_poll_joysticks.Ago() >= POLL_FOR_JOYSTICK_CHANGES_EVERY_SECONDS) {
      num_times_left_to_poll_for_joysticks_changed_--;
      timer_poll_joysticks.Touch();
      LOG->Warn("polling for joystick changes");

      int old_num_joysticks = last_seen_num_joysticks_;
      last_seen_num_joysticks_ = GetNumJoysticksSlow();
      if (old_num_joysticks != last_seen_num_joysticks_) {
        LOG->Warn("joysticks changed");
        num_times_left_to_poll_for_joysticks_changed_ = 0;
        return true;
      }
    }
  }

  return false;
}

void InputHandler_DInput::InputThreadMain() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
    LOG->Warn(werr_ssprintf(
                  GetLastError(), "Failed to set DirectInput thread priority")
                  .c_str());
  }

  // Enable priority boosting.
  SetThreadPriorityBoost(GetCurrentThread(), FALSE);

  std::vector<DIDevice*> buffered_devices;
  HANDLE handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  for (unsigned i = 0; i < Devices.size(); ++i) {
    if (!Devices[i].buffered_) {
      continue;
    }

    buffered_devices.push_back(&Devices[i]);

    Devices[i].device_->Unacquire();
    HRESULT hr = Devices[i].device_->SetEventNotification(handle);
    if (FAILED(hr)) {
      LOG->Warn("IDirectInputDevice2_SetEventNotification failed on %i", i);
    }
    Devices[i].device_->Acquire();
  }

  while (!shutdown_) {
    CHECKPOINT;
    if (buffered_devices.size()) {
      // Update buffered devices.
      PollAndAcquireDevices(true);

      int ret = WaitForSingleObjectEx(handle, 50, true);
      if (ret == -1) {
        LOG->Trace(werr_ssprintf(GetLastError(), "WaitForSingleObjectEx failed")
                       .c_str());
        continue;
      }

      /* Update devices even if no event was triggered, since this also
       * checks for focus loss. */
      RageTimer now;
      for (unsigned i = 0; i < buffered_devices.size(); ++i) {
        UpdateBuffered(*buffered_devices[i], now);
      }
    }
    CHECKPOINT;

    // If we have no buffered devices, we didn't delay at
    // WaitForMultipleObjectsEx.
    if (buffered_devices.size() == 0) {
      usleep(50000);
    }
    CHECKPOINT;
  }
  CHECKPOINT;

  for (unsigned i = 0; i < Devices.size(); ++i) {
    if (!Devices[i].buffered_) {
      continue;
    }

    Devices[i].device_->Unacquire();
    Devices[i].device_->SetEventNotification(nullptr);
  }

  CloseHandle(handle);
}

void InputHandler_DInput::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) {
  for (unsigned i = 0; i < XDevices.size(); ++i) {
    devices_out.push_back(InputDeviceInfo(XDevices[i].dev_, XDevices[i].name_));
  }

  for (unsigned i = 0; i < Devices.size(); ++i) {
    devices_out.push_back(InputDeviceInfo(Devices[i].dev_, Devices[i].name_));
  }
}

static wchar_t ScancodeAndKeysToChar(DWORD scancode, unsigned char keys[256]) {
  static HKL layout = GetKeyboardLayout(0);  // 0 == current thread
  UINT vk = MapVirtualKeyEx(scancode, 1, layout);

  static bool initialized = false;

  typedef int(WINAPI TOUNICODEEX)(
      IN UINT wVirtKey, IN UINT wScanCode, IN CONST BYTE * lpKeyState,
      OUT LPWSTR pwszBuff, IN int cchBuff, IN UINT wFlags, IN HKL dwhkl);
  static TOUNICODEEX* to_unicode_ex;

  if (!initialized) {
    initialized = true;
    HMODULE module = GetModuleHandle("user32.dll");
    to_unicode_ex = (TOUNICODEEX*)GetProcAddress(module, "ToUnicodeEx");
  }

  unsigned short result[2];  // ToAscii writes a max of 2 chars
  ZERO(result);

  if (to_unicode_ex != nullptr) {
    int num = to_unicode_ex(vk, scancode, keys, (LPWSTR)result, 2, 0, layout);
    if (num == 1) {
      return result[0];
    }
  } else {
    int num = ToAsciiEx(vk, scancode, keys, result, 0, layout);
    // iNum == 2 will happen only for dead keys. See MSDN for ToAsciiEx.
    if (num == 1) {
      std::string s = std::string() + (char)result[0];
      return ConvertCodepageToWString(s, CP_ACP)[0];
    }
  }

  return '\0';
}

wchar_t InputHandler_DInput::DeviceButtonToChar(
    DeviceButton button, bool use_current_key_modifiers) {
  // ToAsciiEx maps these keys to a character. They shouldn't be mapped to any
  // character.
  switch (button) {
    case KEY_ESC:
    case KEY_TAB:
    case KEY_ENTER:
    case KEY_BACK:
      return '\0';
  }

  for (const DIDevice& d : Devices) {
    if (d.type_ != DIDevice::KEYBOARD) {
      continue;
    }

    for (const input_t& i : d.inputs_) {
      if (button != i.num_) {
        continue;
      }

      unsigned char keys[256];
      ZERO(keys);
      if (use_current_key_modifiers) {
        GetKeyboardState(keys);
      }
      // todo: handle Caps Lock -freem
      wchar_t c = ScancodeAndKeysToChar(i.ofs_, keys);
      if (c) {
        return c;
      }
    }
  }

  return InputHandler::DeviceButtonToChar(button, use_current_key_modifiers);
}

/*
 * (c) 2003-2004 Glenn Maynard
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
