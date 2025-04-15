#ifndef INPUT_HANDLER_WIN32_SMX_H
#define INPUT_HANDLER_WIN32_SMX_H

#include "InputHandler.h"

constexpr int SMX_PAD_COUNT = 2;
constexpr int SMX_PANEL_COUNT = 9;

bool InputHandler_Win32_SMX_Register_Pad();

enum SMXUpdateCallbackReason {
	SMXUpdateCallback_Updated,
	SMXUpdateCallback_FactoryResetCommandComplete
};
typedef void SMXUpdateCallback(int pad, SMXUpdateCallbackReason reason, void* pUser);
struct SMXInfo {
    bool m_bConnected = false;
	char m_Serial[33];
	uint16_t m_iFirmwareVersion;
};
typedef void SMXLogCallback(const char *log);

class InputHandler_Win32_SMX: public InputHandler
{
public:
	InputHandler_Win32_SMX();
	~InputHandler_Win32_SMX();
	void GetDevicesAndDescriptions( std::vector<InputDeviceInfo>& vDevicesOut );
	RString GetDeviceSpecificInputString( const DeviceInput &di );
	void ProcessInputEvent( int pad );

	void SMX_Start();

private:
	uint16_t m_padInputStates[SMX_PAD_COUNT];

    uint16_t SMX_GetInputState( int pad );
	void SMX_SetLogCallback();
	void SMX_Stop();
};

#endif
