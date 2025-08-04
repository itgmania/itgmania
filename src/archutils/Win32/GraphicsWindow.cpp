#include "global.h"
#include "GraphicsWindow.h"
#include "ProductInfo.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageDisplay.h"
#include "DisplaySpec.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "archutils/Win32/AppInstance.h"
#include "archutils/Win32/Crash.h"
#include "archutils/Win32/ErrorStrings.h"
#include "archutils/Win32/WindowIcon.h"
#include "archutils/Win32/GetFileInformation.h"
#include "CommandLineActions.h"
#include "DirectXHelpers.h"
#include "PrefsManager.h"

#include <windows.h>
#include <string>
#include <set>

static const RString g_sClassName = PRODUCT_ID;

static HWND g_hWndMain;
static HDC g_HDC;
static VideoModeParams g_CurrentParams;
static bool g_bResolutionChanged = false;
static bool g_bHasFocus = true;
static HICON g_hIcon = nullptr;
static bool m_bWideWindowClass;
static bool g_bD3D = false;

// If we're fullscreen, this is the mode we set.
static DEVMODE g_FullScreenDevMode;
static bool g_bRecreatingVideoMode = false;

static UINT g_iQueryCancelAutoPlayMessage = 0;

static RString GetNewWindow()
{
	HWND h = GetForegroundWindow();
	if( h == nullptr )
		return "(NULL)";

	DWORD iProcessID;
	GetWindowThreadProcessId( h, &iProcessID );

	RString sName;
	GetProcessFileName( iProcessID, sName );

	sName = Basename(sName);

	return sName;
}

static LRESULT CALLBACK GraphicsWindow_WndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	CHECKPOINT_M( ssprintf("%p, %u, %08x, %08x", hWnd, msg, wParam, lParam) );

	// Suppress autorun.
	if( msg == g_iQueryCancelAutoPlayMessage )
		return true;

	switch( msg )
	{
		case WM_ACTIVATE:
		{
			const bool bInactive = (LOWORD(wParam) == WA_INACTIVE);
			const bool bMinimized = (HIWORD(wParam) != 0);
			const bool bHadFocus = g_bHasFocus;
			g_bHasFocus = !bInactive && !bMinimized;
			LOG->Trace( "WM_ACTIVATE (%i, %i): %s", bInactive, bMinimized, g_bHasFocus? "has focus":"doesn't have focus" );
			if( !g_bHasFocus )
			{
				RString sName = GetNewWindow();
				static std::set<RString> sLostFocusTo;
				sLostFocusTo.insert( sName );
				RString sStr;
				for( std::set<RString>::const_iterator it = sLostFocusTo.begin(); it != sLostFocusTo.end(); ++it )
					sStr += (sStr.size()?", ":"") + *it;

				LOG->MapLog( "LOST_FOCUS", "Lost focus to: %s", sStr.c_str() );
			}

			if( !g_bD3D && !g_CurrentParams.windowed && !g_bRecreatingVideoMode )
			{
				/* In OpenGL (not D3D), it's our job to unset and reset the
				 * full-screen video mode when we focus changes, and to hide
				 * and show the window. Hiding is done in WM_KILLFOCUS,
				 * because that's where most other apps seem to do it. */
				if( g_bHasFocus && !bHadFocus )
				{
					ChangeDisplaySettingsEx( g_CurrentParams.sDisplayId.c_str(), &g_FullScreenDevMode, nullptr, CDS_FULLSCREEN, nullptr );
					ShowWindow( g_hWndMain, SW_SHOWNORMAL );
					SetWindowPos( g_hWndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
				}
				else if( !g_bHasFocus && bHadFocus )
				{
					ChangeDisplaySettingsEx(g_CurrentParams.sDisplayId.c_str(), nullptr, nullptr, 0, nullptr);
				}
			}

			return 0;
		}
		case WM_KILLFOCUS:
			if( !g_bD3D && !g_CurrentParams.windowed && !g_bRecreatingVideoMode )
				ShowWindow( g_hWndMain, SW_SHOWMINNOACTIVE );
			break;

		/* Is there any reason we should care what size the user resizes
		 * the window to? (who? -aj)
		 * Short answer: yes. -aj */
	//	case WM_GETMINMAXINFO:

		case WM_SETCURSOR:
			if( !g_CurrentParams.windowed )
			{
				// We check if the preference is false because we don't want
				// to show the cursor if the user has set it to false.
				if (!PREFSMAN->m_bShowMouseCursor.Get())
				{
					SetCursor(nullptr);
				}
				return 1;
			}
			break;

		case WM_SYSCOMMAND:
			switch( wParam&0xFFF0 )
			{
				case SC_MONITORPOWER:
				case SC_SCREENSAVE:
					return 0;
			}
			break;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			BeginPaint( hWnd, &ps );
			EndPaint( hWnd, &ps );
			break;
		}

		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_MOUSEWHEEL: // might want to use this for GET_WHEEL_DELTA_WPARAM(wParam) -aj
			// We handle all input ourself, via DirectInput.
			return 0;

		case WM_CLOSE:
			LOG->Trace("WM_CLOSE: shutting down");
			ArchHooks::SetUserQuit();
			return 0;

		case WM_WINDOWPOSCHANGED:
		{
			/* If we're fullscreen and don't have focus, our window is hidden,
			 * so GetClientRect isn't meaningful. */
			if( !g_CurrentParams.windowed && !g_bHasFocus )
				break;

			RECT rect;
			GetClientRect( hWnd, &rect );

			int iWidth = rect.right - rect.left;
			int iHeight = rect.bottom - rect.top;
			if( g_CurrentParams.width != iWidth || g_CurrentParams.height != iHeight )
			{
				g_CurrentParams.width = iWidth;
				g_CurrentParams.height = iHeight;
				g_bResolutionChanged = true;
			}
			break;
		}
		case WM_COPYDATA:
		{
			PCOPYDATASTRUCT pMyCDS = (PCOPYDATASTRUCT) lParam;
			RString sCommandLine( (char*)pMyCDS->lpData, pMyCDS->cbData );
			CommandLineActions::CommandLineArgs args;
			split( sCommandLine, "|", args.argv, false );
			CommandLineActions::ToProcess.push_back( args );
			break;
		}
	}

	CHECKPOINT_M( ssprintf("%p, %u, %08x, %08x", hWnd, msg, wParam, lParam) );

	if( m_bWideWindowClass )
		return DefWindowProcW( hWnd, msg, wParam, lParam );
	else
		return DefWindowProcA( hWnd, msg, wParam, lParam );
}

static void AdjustVideoModeParams( VideoModeParams &p )
{
	DEVMODE dm;
	ZERO( dm );
	dm.dmSize = sizeof(dm);
	if (!EnumDisplaySettings(p.sDisplayId.c_str(), ENUM_CURRENT_SETTINGS, &dm))
	{
		p.rate = 60;
		LOG->Warn( "%s", werr_ssprintf(GetLastError(), "EnumDisplaySettings failed").c_str() );
		return;
	}

	/* On a nForce 2 IGP on Windows 98, dm.dmDisplayFrequency sometimes 
	 * (but not always) is 0.
	 *
	 * MSDN: When you call the EnumDisplaySettings function, the 
	 * dmDisplayFrequency member may return with the value 0 or 1. 
	 * These values represent the display hardware's default refresh rate. 
	 * This default rate is typically set by switches on a display card or 
	 * computer motherboard, or by a configuration program that does not 
	 * use Win32 display functions such as ChangeDisplaySettings. */
	if( !(dm.dmFields & DM_DISPLAYFREQUENCY) ||
		dm.dmDisplayFrequency == 0 ||
		dm.dmDisplayFrequency == 1 )
	{
		p.rate = 60;
		LOG->Warn( "EnumDisplaySettings doesn't know what the refresh rate is. %d %d %d", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel );
	}
	else
	{
		p.rate = dm.dmDisplayFrequency;
	}
}

/* Set the display mode to the given size, bit depth and refresh.
 * The refresh setting may be ignored. */
RString GraphicsWindow::SetScreenMode( const VideoModeParams &p )
{
	const DWORD displaySettingsFlag = p.windowed ? CDS_RESET : CDS_FULLSCREEN;
	
	if( p.windowed )
	{
		// We're going windowed. If we were previously fullscreen, reset.
		ChangeDisplaySettingsEx( p.sDisplayId.c_str(), nullptr, nullptr, displaySettingsFlag, nullptr );

		return RString();
	}

	DEVMODE DevMode;
	ZERO( DevMode );
	DevMode.dmSize = sizeof(DEVMODE);

	// Run DevMode thru EnumDisplaySettings to ensure we only provide modes supported by the display driver.
	if( !EnumDisplaySettings(p.sDisplayId.c_str(), ENUM_CURRENT_SETTINGS, &DevMode) )
	{
		LOG->Warn("EnumDisplaySettings failed for display: %s", p.sDisplayId.c_str());
		return "Failed to retrieve current display settings.";
	}
	
	DevMode.dmPelsWidth = p.width;
	DevMode.dmPelsHeight = p.height;
	DevMode.dmBitsPerPel = p.bpp;
	DevMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

	if( p.rate != REFRESH_DEFAULT )
	{
		DevMode.dmDisplayFrequency = p.rate;
		DevMode.dmFields |= DM_DISPLAYFREQUENCY;
	}

	int ret = ChangeDisplaySettingsEx( p.sDisplayId.c_str(), &DevMode, nullptr, displaySettingsFlag, nullptr );
	if( ret != DISP_CHANGE_SUCCESSFUL && (DevMode.dmFields & DM_DISPLAYFREQUENCY) )
	{
		DevMode.dmFields &= ~DM_DISPLAYFREQUENCY;
		ret = ChangeDisplaySettingsEx( p.sDisplayId.c_str(), &DevMode, nullptr, displaySettingsFlag, nullptr );
	}

	// XXX: append error
	if( ret != DISP_CHANGE_SUCCESSFUL )
		return "Couldn't set screen mode";

	g_FullScreenDevMode = DevMode;
	return RString();
}

static int GetWindowStyle( bool bWindowed , bool bWindowIsFullscreenBorderless)
{
	if( bWindowed && !bWindowIsFullscreenBorderless )
		return WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	else
		return WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
}

/* Set the final window size, set the window text and icon, and then unhide the
 * window. */
void GraphicsWindow::CreateGraphicsWindow( const VideoModeParams &p, bool bForceRecreateWindow )
{
	auto resetDeviceMode = [=](DEVMODE& mode)
	{
		ZeroMemory(&mode, sizeof(DEVMODE));
		mode.dmSize = sizeof(DEVMODE);
		mode.dmDriverExtra = 0;
	};

	auto deviceModeIsValid = [=](const DEVMODE& mode)
	{
		return (mode.dmFields & DM_PELSWIDTH) && (mode.dmFields & DM_PELSHEIGHT)
			&& (mode.dmFields & DM_DISPLAYFREQUENCY)
			&& (mode.dmBitsPerPel >= 32 || !(mode.dmFields & DM_BITSPERPEL));
	};

	g_CurrentParams = p;

	// Adjust g_CurrentParams to reflect the actual display settings.
	AdjustVideoModeParams( g_CurrentParams );

	DEVMODE devmode;
	resetDeviceMode(devmode);

	//Top left corner of monitor
	POINTL pos;
	//Set default position to (0,0), which is the primary display device as a fallback
	pos.x = 0;
	pos.y = 0;

	// Look for the preferred display's position.
	if (EnumDisplaySettingsEx(p.sDisplayId.c_str(), ENUM_CURRENT_SETTINGS, &devmode, 0) && deviceModeIsValid(devmode)
		&& (devmode.dmFields & DM_POSITION))
	{
		pos = devmode.dmPosition;
		resetDeviceMode(devmode);
	}

	if( g_hWndMain == nullptr || bForceRecreateWindow )
	{
		int iWindowStyle = GetWindowStyle( p.windowed , p.bWindowIsFullscreenBorderless );

		AppInstance inst;
		HWND hWnd = CreateWindow( g_sClassName.c_str(), "app", iWindowStyle,
						0, 0, 0, 0, nullptr, nullptr, inst, nullptr );
		if( hWnd == nullptr )
			RageException::Throw( "%s", werr_ssprintf( GetLastError(), "CreateWindow" ).c_str() );

		/* If an old window exists, transfer focus to the new window before
		 * deleting it, or some other window may temporarily get focus, which
		 * can cause it to be resized. */
		if( g_hWndMain != nullptr )
		{
			// While we change to the new window, don't do ChangeDisplaySettings in WM_ACTIVATE.
			g_bRecreatingVideoMode = true;
			SetForegroundWindow( hWnd );
			g_bRecreatingVideoMode = false;

			GraphicsWindow::DestroyGraphicsWindow();
		}

		g_hWndMain = hWnd;
		CrashHandler::SetForegroundWindow( g_hWndMain );
		g_HDC = GetDC( g_hWndMain );
	}

	// Update the window title.
	do
	{
		if( m_bWideWindowClass )
		{
			if( SetWindowText( g_hWndMain, ConvertUTF8ToACP(p.sWindowTitle).c_str() ) )
				break;
		}

		SetWindowTextA( g_hWndMain, ConvertUTF8ToACP(p.sWindowTitle).c_str() );
	} while(0);

	// Update the window icon.
	if( g_hIcon != nullptr )
	{
		SetClassLongPtrA( g_hWndMain, GCLP_HICON, reinterpret_cast<LONG_PTR>(LoadIcon(nullptr,IDI_APPLICATION)) );
		DestroyIcon( g_hIcon );
		g_hIcon = nullptr;
	}
	g_hIcon = IconFromFile( p.sIconFile );
	if( g_hIcon != nullptr )
		SetClassLongPtrA( g_hWndMain, GCLP_HICON, reinterpret_cast<LONG_PTR>(g_hIcon) );

	/* The window style may change as a result of switching to or from fullscreen;
	 * apply it. Don't change the WS_VISIBLE bit. */
	int iWindowStyle = GetWindowStyle(p.windowed, p.bWindowIsFullscreenBorderless);
	if( GetWindowLong( g_hWndMain, GWL_STYLE ) & WS_VISIBLE )
		iWindowStyle |= WS_VISIBLE;
	SetWindowLong( g_hWndMain, GWL_STYLE, iWindowStyle );

	// Set rectangle for window based on the preferred display and resolution
	RECT WindowRect;
	SetRect( &WindowRect, pos.x, pos.y, pos.x + p.width, pos.y + p.height );
	AdjustWindowRect( &WindowRect, iWindowStyle, FALSE );

	//LOG->Warn( "w = %d, h = %d", p.width, p.height );

	const int iWidth = WindowRect.right - WindowRect.left;
	const int iHeight = WindowRect.bottom - WindowRect.top;

	int x = pos.x, y = pos.y;
	/* If windowed and not fullscreen borderless, center the window on the primary display.
	 * Otherwise the window will be centered on the last display option that was hovered before selecting Windowed in Options*/
	if( p.windowed && !p.bWindowIsFullscreenBorderless)
	{
		if (EnumDisplaySettingsEx(NULL, ENUM_CURRENT_SETTINGS, &devmode, 0) && deviceModeIsValid(devmode)
			&& (devmode.dmFields & DM_POSITION))
		{
			WindowRect.left = devmode.dmPosition.x;
			WindowRect.top = devmode.dmPosition.y;
			WindowRect.right = devmode.dmPosition.x + devmode.dmPelsWidth;
			WindowRect.bottom = devmode.dmPosition.y + devmode.dmPelsHeight;
		}
		
		x = WindowRect.left + (WindowRect.right - WindowRect.left - iWidth) / 2;
		y = WindowRect.top + (WindowRect.bottom - WindowRect.top - iHeight) / 2;
	}

	resetDeviceMode(devmode);

	/* Move and resize the window. SWP_FRAMECHANGED causes the above
	 * SetWindowLong to take effect. */
	if( !SetWindowPos( g_hWndMain, HWND_NOTOPMOST, x, y, iWidth, iHeight, SWP_FRAMECHANGED|SWP_SHOWWINDOW ) )
		LOG->Warn( "%s", werr_ssprintf( GetLastError(), "SetWindowPos" ).c_str() );

	SetForegroundWindow( g_hWndMain );

	/* Pump messages quickly, to make sure the window is completely set up.
	 * If we don't do this, then starting up in a D3D fullscreen window may
	 * cause all other windows on the system to be resized. */
	MSG msg;
	while( PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE ) )
	{
		GetMessage( &msg, nullptr, 0, 0 );
		DispatchMessage( &msg );
	}
}

/** @brief Shut down the window, but don't reset the video mode. */
void GraphicsWindow::DestroyGraphicsWindow()
{
	// If we were in fullscreen, release the display mode before destroying the window
	if( g_hWndMain && !g_CurrentParams.windowed )
	{
		ChangeDisplaySettingsEx(g_CurrentParams.sDisplayId, nullptr, nullptr, 0, nullptr);
	}
	
	if( g_HDC != nullptr )
	{
		ReleaseDC( g_hWndMain, g_HDC );
		g_HDC = nullptr;
	}

	if( g_hWndMain != nullptr )
	{
		DestroyWindow( g_hWndMain );
		g_hWndMain = nullptr;
		CrashHandler::SetForegroundWindow( g_hWndMain );
	}

	if( g_hIcon != nullptr )
	{
		DestroyIcon( g_hIcon );
		g_hIcon = nullptr;
	}

	MSG msg;
	while( PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE ) )
	{
		CHECKPOINT;
		GetMessage( &msg, nullptr, 0, 0 );
		CHECKPOINT;
		DispatchMessage( &msg );
	}
}

void GraphicsWindow::Initialize( bool bD3D )
{
	// A few things need to be handled differently for D3D.
	g_bD3D = bD3D;

	AppInstance inst;
	do
	{
		const std::wstring wsClassName = RStringToWstring( g_sClassName );
		WNDCLASSW WindowClassW =
		{
			CS_OWNDC | CS_BYTEALIGNCLIENT,
			GraphicsWindow_WndProc,
			0,				/* cbClsExtra */
			0,				/* cbWndExtra */
			inst,			/* hInstance */
			nullptr,			/* set icon later */
			LoadCursor( nullptr, IDC_ARROW ),	/* default cursor */
			nullptr,			/* hbrBackground */
			nullptr,			/* lpszMenuName */
			wsClassName.c_str()	/* lpszClassName */
		}; 

		m_bWideWindowClass = true;
		if( RegisterClassW( &WindowClassW ) )
			break;

		WNDCLASS WindowClassA =
		{
			CS_OWNDC | CS_BYTEALIGNCLIENT,
			GraphicsWindow_WndProc,
			0,				/* cbClsExtra */
			0,				/* cbWndExtra */
			inst,			/* hInstance */
			nullptr,			/* set icon later */
			LoadCursor( nullptr, IDC_ARROW ),	/* default cursor */
			nullptr,			/* hbrBackground */
			nullptr,			/* lpszMenuName */
			g_sClassName.c_str()	/* lpszClassName */
		}; 

		m_bWideWindowClass = false;
		if( !RegisterClassA( &WindowClassA ) )
			RageException::Throw( "%s", werr_ssprintf( GetLastError(), "RegisterClass" ).c_str() );
	} while(0);

	g_iQueryCancelAutoPlayMessage = RegisterWindowMessage( "QueryCancelAutoPlay" );
}

void GraphicsWindow::Shutdown()
{
	DestroyGraphicsWindow();

	AppInstance inst;
	UnregisterClass( g_sClassName.c_str(), inst );
}

HDC GraphicsWindow::GetHDC()
{
	ASSERT( g_HDC != nullptr );
	return g_HDC;
}

const VideoModeParams &GraphicsWindow::GetParams()
{
	return g_CurrentParams;
}

void GraphicsWindow::Update()
{
	MSG msg;
	while( PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE ) )
	{
		GetMessage( &msg, nullptr, 0, 0 );
		DispatchMessage( &msg );
	}

	HOOKS->SetHasFocus( g_bHasFocus );

	if( g_bResolutionChanged && DISPLAY != nullptr )
	{
		//LOG->Warn( "Changing resolution" );

		/* Let DISPLAY know that our resolution has changed. (Note that ResolutionChanged()
		 * can come back here, so reset g_bResolutionChanged first.) */
		g_bResolutionChanged = false;
		DISPLAY->ResolutionChanged();
	}
}

HWND GraphicsWindow::GetHwnd()
{
	return g_hWndMain;
}

void GraphicsWindow::GetDisplaySpecs( DisplaySpecs &out )
{
	auto resetDeviceMode = [=]( DEVMODE& mode ) {
		ZeroMemory( &mode, sizeof( DEVMODE ) );
		mode.dmSize = sizeof(DEVMODE);
		mode.dmDriverExtra = 0;
	};

	auto deviceModeIsValid = [=]( const DEVMODE& mode ) {
		return (mode.dmFields & DM_PELSWIDTH) && (mode.dmFields & DM_PELSHEIGHT)
			&& (mode.dmFields & DM_DISPLAYFREQUENCY)
			&& (mode.dmBitsPerPel >= 32 || !(mode.dmFields & DM_BITSPERPEL));
	};

	DEVMODE devmode;
	resetDeviceMode(devmode);

	DWORD deviceIter = 0;
	DISPLAY_DEVICE dd;
	ZeroMemory(&dd, sizeof(dd));
	dd.cb = sizeof(dd);

	// Loop through all displays and their display settings to create a DisplaySpec for each display.
	while (EnumDisplayDevices(NULL, deviceIter++, &dd, 0))
	{
		int mode = 0;
		std::set<DisplayMode> dispModes;

		while (EnumDisplaySettingsEx(dd.DeviceName, mode++, &devmode, 0))
		{
			if (deviceModeIsValid(devmode))
			{
				DisplayMode m = { devmode.dmPelsWidth, devmode.dmPelsHeight, static_cast<double> (devmode.dmDisplayFrequency) };
				dispModes.insert(m);
			}
			resetDeviceMode(devmode);
		}

		//Clean up the device name to store as the DisplaySpec ID
		RString sDeviceName;
		sDeviceName = dd.DeviceName;
		TrimRight(sDeviceName);

		//Make the DisplaySpec Name more friendly by removing the "\\.\" prefix on most Windows display devices
		RString modeName;
		modeName = sDeviceName;
		TrimLeft(modeName, "\\");
		TrimLeft(modeName, ".");
		TrimLeft(modeName, "\\");

		// Get the current display mode
		// Set the ENUM_CURRENT_SETTINGS flag so that we only store DisplaySpecs for valid displays
		if (EnumDisplaySettingsEx(dd.DeviceName, ENUM_CURRENT_SETTINGS, &devmode, 0) && deviceModeIsValid(devmode)
			&& (devmode.dmFields & DM_POSITION))
		{
			DisplayMode m = { devmode.dmPelsWidth, devmode.dmPelsHeight, static_cast<double> (devmode.dmDisplayFrequency) };
			RectI bounds = { devmode.dmPosition.x, devmode.dmPosition.y, static_cast<int> (m.width), static_cast<int> (m.height) };
			out.insert(DisplaySpec(sDeviceName, modeName, dispModes, m, bounds));
		}
		else if (!dispModes.empty())
		{
			LOG->Warn("Could not retrieve valid current display mode");
			out.insert(DisplaySpec(sDeviceName, modeName, *dispModes.begin()));
		}
	}
	if (out.size() == 0)
		LOG->Warn("Could not retrieve *any* DisplaySpec's!");
	ZeroMemory(&dd, sizeof(dd));
	dd.cb = sizeof(dd);
	resetDeviceMode(devmode);
}

/*
 * (c) 2004 Glenn Maynard
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
