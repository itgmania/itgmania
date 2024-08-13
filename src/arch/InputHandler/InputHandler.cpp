#include "global.h"
#include "InputFilter.h"
#include "RageUtil.h"
#include "InputHandler.h"
#include "RageLog.h"
#include "LocalizedString.h"
#include "arch/arch_default.h"
#include "InputHandler_MonkeyKeyboard.h"
//#include "InputHandler_NSEvent.hpp"

#include <vector>


void InputHandler::UpdateTimer()
{
	m_LastUpdate.Touch();
	m_iInputsSinceUpdate = 0;
}

void InputHandler::ButtonPressed( DeviceInput di )
{
	if( di.ts.IsZero() )
	{
		di.ts = m_LastUpdate.Half();
		++m_iInputsSinceUpdate;
	}

	INPUTFILTER->ButtonPressed( di );

	if( m_iInputsSinceUpdate >= 1000 )
	{
		/*
		 * We haven't received an update in a long time, so warn about it.  We expect to receive
		 * input events before the first UpdateTimer call only on the first update.  Leave
		 * m_iInputsSinceUpdate where it is, so we only warn once.  Only updates that didn't provide
		 * a timestamp are counted; if the driver provides its own timestamps, UpdateTimer is
		 * optional.
		 */
		LOG->Warn("InputHandler::ButtonPressed: Driver sent many updates without calling UpdateTimer. Device ID: %d", di.uniqueID);
		FAIL_M("x");
	}
}

wchar_t InputHandler::DeviceButtonToChar( DeviceButton button, bool bUseCurrentKeyModifiers )
{
	wchar_t c = L'\0';
	switch( button )
	{
	default:
		if( button < 127 )
			c = (wchar_t) button;
		else if( button >= KEY_KP_C0 && button <= KEY_KP_C9 )
			c =(wchar_t) (button - KEY_KP_C0) + '0';
		break;
	case KEY_KP_SLASH:	c = L'/';	break;
	case KEY_KP_ASTERISK:	c = L'*';	break;
	case KEY_KP_HYPHEN:	c = L'-';	break;
	case KEY_KP_PLUS:	c = L'+';	break;
	case KEY_KP_PERIOD:	c = L'.';	break;
	case KEY_KP_EQUAL:	c = L'=';	break;
	}

	// Handle some default US keyboard modifiers for derived InputHandlers that
	// don't implement DeviceButtonToChar.
	if( bUseCurrentKeyModifiers )
	{
		bool bHoldingShift =
			INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
			INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT));

		bool bHoldingCtrl =
			INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL)) ||
			INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL));

		// todo: handle Caps Lock -freem

		if( bHoldingShift && !bHoldingCtrl )
		{
			MakeUpper( &c, 1 );

			switch( c )
			{
			case L'`':	c = L'~';	break;
			case L'1':	c = L'!';	break;
			case L'2':	c = L'@';	break;
			case L'3':	c = L'#';	break;
			case L'4':	c = L'$';	break;
			case L'5':	c = L'%';	break;
			case L'6':	c = L'^';	break;
			case L'7':	c = L'&';	break;
			case L'8':	c = L'*';	break;
			case L'9':	c = L'(';	break;
			case L'0':	c = L')';	break;
			case L'-':	c = L'_';	break;
			case L'=':	c = L'+';	break;
			case L'[':	c = L'{';	break;
			case L']':	c = L'}';	break;
			case L'\'':	c = L'"';	break;
			case L'\\':	c = L'|';	break;
			case L';':	c = L':';	break;
			case L',':	c = L'<';	break;
			case L'.':	c = L'>';	break;
			case L'/':	c = L'?';	break;
			}
		}

	}

	return c;
}

static LocalizedString HOME	( "DeviceButton", "Home" );
static LocalizedString END		( "DeviceButton", "End" );
static LocalizedString UP		( "DeviceButton", "Up" );
static LocalizedString DOWN	( "DeviceButton", "Down" );
static LocalizedString SPACE	( "DeviceButton", "Space" );
static LocalizedString SHIFT	( "DeviceButton", "Shift" );
static LocalizedString CTRL	( "DeviceButton", "Ctrl" );
static LocalizedString ALT		( "DeviceButton", "Alt" );
static LocalizedString INSERT	( "DeviceButton", "Insert" );
static LocalizedString DEL		( "DeviceButton", "Delete" );
static LocalizedString PGUP	( "DeviceButton", "PgUp" );
static LocalizedString PGDN	( "DeviceButton", "PgDn" );
static LocalizedString BACKSLASH	( "DeviceButton", "Backslash" );

RString InputHandler::GetDeviceSpecificInputString( const DeviceInput &di )
{
	if( di.device == InputDevice_Invalid )
		return RString();

    RString deviceString = InputDeviceToString(di.device) + " (ID: " + std::to_string(di.uniqueID) + ")";

	if( di.device == DEVICE_KEYBOARD )
	{
		if( di.button >= KEY_KP_C0 && di.button <= KEY_KP_ENTER )
			return DeviceButtonToString( di.button );

		wchar_t c = DeviceButtonToChar( di.button, false );
		if( c && c != L' ' ) // Don't show "Key  " for space.
			return InputDeviceToString( di.device ) + " " + Capitalize( WStringToRString(std::wstring()+c) );
	}

	RString s = DeviceButtonToString( di.button );
	if( di.device != DEVICE_KEYBOARD )
		s = InputDeviceToString( di.device ) + " " + s;
	return s;
}

RString InputHandler::GetLocalizedInputString(const DeviceInput &di)
{
	RString localizedString;

	switch (di.button)
	{
	case KEY_HOME: localizedString = HOME.GetValue(); break;
	case KEY_END: localizedString = END.GetValue(); break;
	case KEY_UP: localizedString = UP.GetValue(); break;
	case KEY_DOWN: localizedString = DOWN.GetValue(); break;
	case KEY_SPACE: localizedString = SPACE.GetValue(); break;
	case KEY_LSHIFT: case KEY_RSHIFT: localizedString = SHIFT.GetValue(); break;
	case KEY_LCTRL: case KEY_RCTRL: localizedString = CTRL.GetValue(); break;
	case KEY_LALT: case KEY_RALT: localizedString = ALT.GetValue(); break;
	case KEY_INSERT: localizedString = INSERT.GetValue(); break;
	case KEY_DEL: localizedString = DEL.GetValue(); break;
	case KEY_PGUP: localizedString = PGUP.GetValue(); break;
	case KEY_PGDN: localizedString = PGDN.GetValue(); break;
	case KEY_BACKSLASH: localizedString = BACKSLASH.GetValue(); break;
	default:
		wchar_t c = DeviceButtonToChar( di.button, false );
		if( c && c != L' ' ) // Don't show "Key  " for space.
			localizedString = Capitalize(WStringToRString(std::wstring() + c));
		else
			localizedString = DeviceButtonToString(di.button);
		break;
	}

	return localizedString + " (ID: " + std::to_string(di.uniqueID) + ")";
}

DriverList InputHandler::m_pDriverList;

static LocalizedString INPUT_HANDLERS_EMPTY( "Arch", "Input Handlers cannot be empty." );
void InputHandler::Create( const RString &drivers_, std::vector<InputHandler *> &Add )
{
	const RString drivers = drivers_.empty()? RString(DEFAULT_INPUT_DRIVER_LIST):drivers_;
	std::vector<RString> DriversToTry;
	split( drivers, ",", DriversToTry, true );

	if( DriversToTry.empty() )
		RageException::Throw( "%s", INPUT_HANDLERS_EMPTY.GetValue().c_str() );

	for (RString const &s : DriversToTry)
	{
		RageDriver *pDriver = InputHandler::m_pDriverList.Create( s );
		if( pDriver == nullptr )
		{
			LOG->Trace( "Unknown Input Handler name: %s", s.c_str() );
			continue;
		}

		InputHandler *ret = dynamic_cast<InputHandler *>( pDriver );
		DEBUG_ASSERT( ret );
		Add.push_back( ret );
	}

	// Always add
	Add.push_back( new InputHandler_MonkeyKeyboard );
//    Add.push_back( new InputHandler_NSEvent );
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
