#include "global.h"
#include "InputHandler_Win32_SMX.h"
#include "RageLog.h"

#include <windows.h>

typedef VOID(__stdcall* SMX_Start_t)(
    SMXUpdateCallback UpdateCallback, void *pUser
);
static SMX_Start_t pSMX_Start = nullptr;

typedef uint16_t(__stdcall* SMX_GetInputState_t)(
    int pad
);
static SMX_GetInputState_t pSMX_GetInputState = nullptr;

typedef VOID(__stdcall* SMX_SetLogCallback_t)(
	SMXLogCallback callback
);
static SMX_SetLogCallback_t pSMX_SetLogCallback = nullptr;

typedef VOID(__stdcall* SMX_Stop_t)();
static SMX_Stop_t pSMX_Stop = nullptr;

static HINSTANCE hSMXdll = nullptr;

REGISTER_INPUT_HANDLER_CLASS2(SMX, Win32_SMX);

static bool _smxdll_attempted_load = false;
static bool _smxdll_loaded = false;
static bool __detected_pad = false;
static bool __is_smx_started = false;
static int __p1_pads = 0;
static int __p2_pads = 0;

namespace {
	static void SmxCallback(int pad, SMXUpdateCallbackReason reason, void* pUser) {
		InputHandler_Win32_SMX* inputHandler = static_cast<InputHandler_Win32_SMX*>(pUser);
		inputHandler->ProcessInputEvent(pad);
	};

	static void LogCallback(const char *log) {
		LOG->Info("SMX SDK Log: %s", log);

		//TODO: Find more graceful means of surfacing a warning when multiple pads of the same value are detected.
		// Note: when this happens, to remove this logic:
		// 1) delete all code below this comment.
		// 2) if the static vars `__p1_pads` and `__p2_pads` are no longer used in the new solution, remove them. (if they are reused, note that outside this method, they are reset to 0 in the destructor too.)
		
		// Just return after printing if it's not a device log.
		bool isDeviceInfoLog = strstr(log, "Received device info.  Master version:") != nullptr;
		bool containsP = strstr(log, "P") != nullptr;
		if (!isDeviceInfoLog || !containsP) {
			return;
		}

		// If this is a device log, check which player just connected.
		bool isP2 = strstr(log, "P2") != nullptr;
		if (isP2) {
			__p2_pads += 1;
		} else {
			__p1_pads += 1;
		}

		// If too many of a particular player connected, issue a warning.
		if (__p1_pads > 1 || __p2_pads > 1) {
			LOG->Warn("Both SMX stages are set to the same player ID. Please unplug one of the stages from power and USB, change the jumper position, and then restart the game.");
			MessageBox(nullptr, "Both SMX stages are set to the same player ID. Please unplug one of the stages from power and USB, change the jumper position, and then restart the game.", "Error", MB_OK );
		}
	};
}

int smx_filter(unsigned int, struct _EXCEPTION_POINTERS*)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

bool MapFunctions()
{
	__try
	{
		pSMX_Start = reinterpret_cast<SMX_Start_t>(GetProcAddress(hSMXdll, "SMX_Start"));
		pSMX_GetInputState = reinterpret_cast<SMX_GetInputState_t>(GetProcAddress(hSMXdll, "SMX_GetInputState"));
		pSMX_SetLogCallback = reinterpret_cast<SMX_SetLogCallback_t>(GetProcAddress(hSMXdll, "SMX_SetLogCallback"));
		pSMX_Stop = reinterpret_cast<SMX_Stop_t>(GetProcAddress(hSMXdll, "SMX_Stop"));
	}
	__except (smx_filter(GetExceptionCode(), GetExceptionInformation()))
	{
		LOG->Warn("SMX.dll is not valid. The SMX driver will not be used.");
		FreeLibrary(hSMXdll);
		return false;
	}

	_smxdll_loaded = true;
	return true;
}

// The only way to know for-sure if the DLL is available is to actually load it, so this method attempts to load the DLL the first time it is run.
bool IsSmxDllAvailable()
{
	if (_smxdll_attempted_load) {
		return _smxdll_loaded;
	}
	_smxdll_attempted_load = true;

	hSMXdll = LoadLibrary("SMX.dll");

	if (hSMXdll == nullptr) {
		LOG->Warn("SMX.dll not found. The SMX driver will not be used.");
		_smxdll_loaded = false;
		return false;
	}
	_smxdll_loaded = MapFunctions();
	return _smxdll_loaded;
}

bool InputHandler_Win32_SMX_Register_Pad() {
	if (__detected_pad) {
		return true;
	}

	if (!IsSmxDllAvailable()) {
		return false;
	}

	__detected_pad = true;
	return true;
}

InputHandler_Win32_SMX::InputHandler_Win32_SMX() {
	std::fill(std::begin(m_padInputStates), std::end(m_padInputStates), 0);
	SMX_SetLogCallback();
}

InputHandler_Win32_SMX::~InputHandler_Win32_SMX() {
	SMX_Stop();

	// Only one InputHandler should be active at a time.
	// If we reset `__detected_pad` here, it allows us to stop reporting the device in `GetDevicesAndDescriptions` when a pad is disconnected. 
	// If (a) pad(s) is still connected, it'll re-register before the next constructor runs.
	__detected_pad = false;
	__p1_pads = 0;
	__p2_pads = 0;
}

void InputHandler_Win32_SMX::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut)
{
	// Ensure that the SMX device is not registered unless a pad is connected.
	if (__detected_pad) {
		vDevicesOut.push_back(InputDeviceInfo(InputDevice(DEVICE_SMX), "SMX"));

		// Start the SMX SDK if needed when a pad is detected. (It's okay to call this repeatedly)
		SMX_Start();
	}
}

void InputHandler_Win32_SMX::ProcessInputEvent(int pad) {
	/*
	The SMX SDK always sends callback events over the same thread, so thread safety is not a worry.
	*/

	// Fast-fail if somehow the pad is outside of range
    if (pad < 0 || pad >= SMX_PAD_COUNT) {
        return;
    }

	// Get new input state and compare with the old
    uint16_t newInputState = SMX_GetInputState(pad);
    uint16_t changedInputs = newInputState ^ m_padInputStates[pad];

	// If inputs weren't updated, report the end of an empty poll and return
    if (changedInputs == 0) {
        InputHandler::UpdateTimer();
        return;
    }

	// SMX events come in for multiple pads, but get translated into one device from SM's POV.
	// Ensure P2's buttons begin from the correct `JOY_BUTTON`.
    DeviceButton beginButton = enum_add2(JOY_BUTTON_1, pad * SMX_PANEL_COUNT);

	// Process inputs
    for (int i = 0; i < SMX_PANEL_COUNT; i++) {
		bool didButtonStateChange = (changedInputs & (1 << i)) != 0;
        if (didButtonStateChange) {
            bool pressed = newInputState & (1 << i); // Get pressed state
            DeviceInput di(DEVICE_SMX, enum_add2(beginButton, i), pressed); // Make input event with pressed state for this button
			di.ts.Touch(); // Touch the timestamp timer to indicate the input happened *now*
            ButtonPressed(di); // Report the input event
        }
    }

	// Save the new input state for later, and report the end of a poll.
    m_padInputStates[pad] = newInputState;
    InputHandler::UpdateTimer();
}

RString InputHandler_Win32_SMX::GetDeviceSpecificInputString(const DeviceInput &di)
{
    int zeroIndexedButton = di.button - JOY_BUTTON_1;
    int pad = (zeroIndexedButton / SMX_PANEL_COUNT) + 1;
    int padRemovedButton = zeroIndexedButton % SMX_PANEL_COUNT;

    static const char* buttonStrings[SMX_PANEL_COUNT] =
    {
        "UpLeft", "Up", "UpRight", "Left", "Center",
        "Right", "DownLeft", "Down", "DownRight"
    };

    const char* buttonString = (padRemovedButton >= 0 && padRemovedButton < SMX_PANEL_COUNT) ? buttonStrings[padRemovedButton] : "unknown";

    return ssprintf("SMX P%d %s", pad, buttonString);
}

void InputHandler_Win32_SMX::SMX_Start() {
	if (__is_smx_started) {
		return;
	}

    if (pSMX_Start != nullptr) {
        pSMX_Start(&SmxCallback, this);
		__is_smx_started = true;
    }
}

uint16_t InputHandler_Win32_SMX::SMX_GetInputState(int pad) {
    if (__is_smx_started && pSMX_GetInputState != nullptr) {
        return pSMX_GetInputState(pad);
    }
	
	return 0;
}

void InputHandler_Win32_SMX::SMX_SetLogCallback() {
	if (pSMX_SetLogCallback != nullptr) {
		pSMX_SetLogCallback(&LogCallback);
	}
}

void InputHandler_Win32_SMX::SMX_Stop() {
	if (!__is_smx_started) {
		return;
	}

	if (pSMX_Stop != nullptr) {
		pSMX_Stop();
		__is_smx_started = false;
	}
}
