#ifndef LOW_LEVEL_WINDOW_SDL_H
#define LOW_LEVEL_WINDOW_SDL_H

#include <SDL2/SDL.h>

#include "LowLevelWindow.h"
#include "RageDisplay.h"  // VideoModeParams

class LowLevelWindow_SDL : public LowLevelWindow {
 public:
  LowLevelWindow_SDL();
  ~LowLevelWindow_SDL();

  void* GetProcAddress(std::string s);
  std::string TryVideoMode(const VideoModeParams& p, bool& bNewDeviceOut);
  void LogDebugInformation() const;
  bool IsSoftwareRenderer(std::string& sError);
  void SwapBuffers();
  void Update();

  const ActualVideoModeParams GetActualVideoModeParams() const {
    return CurrentParams;
  }

  void GetDisplaySpecs(DisplaySpecs& out) const;

  bool SupportsRenderToTexture() const;
  RenderTarget* CreateRenderTarget();

  bool SupportsFullscreenBorderlessWindow() const;

  bool SupportsThreadedRendering();
  void BeginConcurrentRenderingMainThread();
  void EndConcurrentRenderingMainThread();
  void BeginConcurrentRendering();
  void EndConcurrentRendering();

 private:
  void RestoreOutputConfig();
  bool CreateGLContext();
  void DestroyGLContext();

  SDL_Window* m_window;
  SDL_GLContext m_glContext;
  bool m_bWasWindowed;
  ActualVideoModeParams CurrentParams;

  float m_lastScreensaverInterrupt = 0.0f;
  float m_screensaverInterruptInterval = 60.0f;
};

#ifdef ARCH_LOW_LEVEL_WINDOW
#error "More than one LowLevelWindow selected!"
#endif
#define ARCH_LOW_LEVEL_WINDOW LowLevelWindow_SDL

#endif