#ifndef LOW_LEVEL_WINDOW_SDL_H
#define LOW_LEVEL_WINDOW_SDL_H

// Needed for GetProcAddress
#define SDL_FUNCTION_POINTER_IS_VOID_POINTER 1

#include <set>
#include <SDL3/SDL_video.h>

#include "LowLevelWindow.h"
#include "RageDisplay.h"

class DisplaySpec;
typedef std::set<DisplaySpec> DisplaySpecs;
class VideoModeParams;
class ActualVideoModeParams;
class RenderTarget;
struct RenderTargetParam;

class LowLevelWindow_SDL : public LowLevelWindow
{
public:
	LowLevelWindow_SDL();

	~LowLevelWindow_SDL() override;

	void *GetProcAddress( RString s ) override;

	// Return "" if mode change was successful, otherwise an error message.
	// bNewDeviceOut is set true if a new device was created and textures
	// need to be reloaded.
	RString TryVideoMode( const VideoModeParams &p, bool &bNewDeviceOut ) override;
	void GetDisplaySpecs(DisplaySpecs &out) const override;

	void LogDebugInformation() const override;
	bool IsSoftwareRenderer( RString & /* sError */ ) override { return false; }

	void SwapBuffers() override;
	void Update() override;

	const ActualVideoModeParams GetActualVideoModeParams() const override;

	/**
	* Game only uses Display-based RTT if a commonly available OpenGL extension isn't supported
	* So no real need to implement this!
	*/
	bool SupportsRenderToTexture() const override { return false; }
	RenderTarget *CreateRenderTarget() override { return nullptr; };

	bool SupportsFullscreenBorderlessWindow() const override { return true; };

	/**
	 * Unsure if threaded rendering provides any benefits here
	 */
	bool SupportsThreadedRendering() override { return false; }
	void BeginConcurrentRenderingMainThread() override {};
	void EndConcurrentRenderingMainThread() override {};
	void BeginConcurrentRendering() override {};
	void EndConcurrentRendering() override {};
protected:
	void SetWindowIcon(const RString& filePath) const;

	SDL_Window* m_pWindow;
	SDL_GLContext m_pGlContext;
	ActualVideoModeParams m_CurrentVideoParams;
};

#ifdef ARCH_LOW_LEVEL_WINDOW
#error "More than one LowLevelWindow selected!"
#endif
#define ARCH_LOW_LEVEL_WINDOW LowLevelWindow_SDL

#endif
