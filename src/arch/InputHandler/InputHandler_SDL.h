/* InputHandler_SDL - SDL-based input handler. */

#ifndef INPUT_HANDLER_SDL_H
#define INPUT_HANDLER_SDL_H

#include "InputHandler.h"

#include <vector>
#include <SDL3/SDL.h>



class InputHandler_SDL: public InputHandler
{
public:
	InputHandler_SDL();
	~InputHandler_SDL() override;
	void Update() override;
	void GetDevicesAndDescriptions( std::vector<InputDeviceInfo>& vDevicesOut ) override;
	RString GetDeviceSpecificInputString( const DeviceInput &di ) override;
	bool DevicesChanged() override;

protected:
	SDL_Window* m_pWindow;
	std::vector<SDL_Joystick*> m_pJoysticks;
	std::vector<SDL_Gamepad*> m_pGamepads;
	std::map<SDL_JoystickID, int> m_connectedDevices;
	bool m_devicesChanged;

	void QueryJoysticks();
	bool ConnectJoystick(SDL_JoystickID joystickId);
	bool DisconnectJoystick(SDL_JoystickID joystickId);



private:
    // timestamp is unsigned long to match X11 Time type
	void RegisterKeyEvent( uint64_t timestamp, bool keyDown, DeviceButton button );
};

#endif
