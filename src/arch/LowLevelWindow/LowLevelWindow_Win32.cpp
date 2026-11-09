#include "LowLevelWindow_Win32.h"

#include <GL/glew.h>

#include <cstdint>
#include <string>

#include "LocalizedString.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageDisplay_OGL.h"
#include "RageDisplay_OGL_Helpers.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/ErrorStrings.h"
#include "archutils/Win32/GraphicsWindow.h"
#include "global.h"

static PIXELFORMATDESCRIPTOR current_pixel_format;
static HGLRC hglrc = nullptr;
static HMODULE hgl_module = nullptr;

static void DestroyGraphicsWindowAndOpenGLContext() {
  if (hglrc != nullptr) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc);
    hglrc = nullptr;
  }

  ZERO(current_pixel_format);

  GraphicsWindow::DestroyGraphicsWindow();
}

void* LowLevelWindow_Win32::GetProcAddress(std::string s) {
  void* ret = (void*)wglGetProcAddress(s.c_str());
  if (ret != nullptr) {
    return ret;
  }

  if (hgl_module != nullptr) {
    ret = (void*)::GetProcAddress(hgl_module, s.c_str());

    if (ret != nullptr) {
      return ret;
    }
  }

  return (void*)::GetProcAddress(GetModuleHandle(nullptr), s.c_str());
}

LowLevelWindow_Win32::LowLevelWindow_Win32() {
  ASSERT(hglrc == nullptr);
  ASSERT(hgl_module == nullptr);

  GraphicsWindow::Initialize(false);
}

LowLevelWindow_Win32::~LowLevelWindow_Win32() {
  DestroyGraphicsWindowAndOpenGLContext();
  GraphicsWindow::Shutdown();
}

void LowLevelWindow_Win32::GetDisplaySpecs(DisplaySpecs& out) const {
  GraphicsWindow::GetDisplaySpecs(out);
}

int ChooseWindowPixelFormat(
    const VideoModeParams& p, PIXELFORMATDESCRIPTOR* pixfmt) {
  ASSERT(GraphicsWindow::GetHwnd() != nullptr);
  ASSERT(GraphicsWindow::GetHDC() != nullptr);

  ZERO(*pixfmt);
  pixfmt->nSize = sizeof(PIXELFORMATDESCRIPTOR);
  pixfmt->nVersion = 1;
  pixfmt->dwFlags = PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL;
  pixfmt->iPixelType = PFD_TYPE_RGBA;
  pixfmt->cColorBits = p.bpp_ == 16 ? 16 : 24;
  pixfmt->cDepthBits = 16;

  return ChoosePixelFormat(GraphicsWindow::GetHDC(), pixfmt);
}

void DumpPixelFormat(const PIXELFORMATDESCRIPTOR& pfd) {
  std::string str = ssprintf("Mode: ");
  bool invalid_format = false;

  if (pfd.dwFlags & PFD_GENERIC_FORMAT) {
    if (pfd.dwFlags & PFD_GENERIC_ACCELERATED) {
      str += "MCD ";
    } else {
      str += "software ";
      invalid_format = true;
    }
  } else {
    str += "ICD ";
  }

  if (pfd.iPixelType != PFD_TYPE_RGBA) {
    str += "indexed ";
    invalid_format = true;
  }
  if (!(pfd.dwFlags & PFD_SUPPORT_OPENGL)) {
    str += "!OPENGL ";
    invalid_format = true;
  }
  if (!(pfd.dwFlags & PFD_DRAW_TO_WINDOW)) {
    str += "!window ";
    invalid_format = true;
  }
  if (!(pfd.dwFlags & PFD_DOUBLEBUFFER)) {
    str += "!dbuff ";
    invalid_format = true;
  }

  str += ssprintf(
      "%i (%i%i%i) ", pfd.cColorBits, pfd.cRedBits, pfd.cGreenBits,
      pfd.cBlueBits);
  if (pfd.cAlphaBits) {
    str += ssprintf("%i alpha ", pfd.cAlphaBits);
  }
  if (pfd.cDepthBits) {
    str += ssprintf("%i depth ", pfd.cDepthBits);
  }
  if (pfd.cStencilBits) {
    str += ssprintf("%i stencil ", pfd.cStencilBits);
  }
  if (pfd.cAccumBits) {
    str += ssprintf("%i accum ", pfd.cAccumBits);
  }

  if (invalid_format) {
    LOG->Warn("Invalid format: %s", str.c_str());
  } else {
    LOG->Info("%s", str.c_str());
  }
}

/* This function does not reset the video mode if it fails, because we might be
 * trying yet another video mode, so we'd just thrash the display.  On fatal
 * error, LowLevelWindow_Win32::~LowLevelWindow_Win32 will call
 * GraphicsWindow::Shutdown(). */
std::string LowLevelWindow_Win32::TryVideoMode(
    const VideoModeParams& p, bool& new_device_out) {
  // LOG->Warn( "LowLevelWindow_Win32::TryVideoMode" );

  ASSERT_M(p.bpp_ == 16 || p.bpp_ == 32, ssprintf("%i", p.bpp_));

  new_device_out = false;

  /* We're only allowed to change the pixel format of a window exactly once. */
  bool can_set_pixel_format = true;

  /* Do we have an old window? */
  if (GraphicsWindow::GetHwnd() == nullptr) {
    /* No.  Always create and show the window before changing the video mode.
     * Otherwise, some other window may have focus, and changing the video mode
     * will cause that window to be resized. */
    new_device_out = true;
    GraphicsWindow::CreateGraphicsWindow(p);
  } else {
    /* We already have a window.  Assume that its pixel format has already
     * been set. */
    can_set_pixel_format = false;
  }

  ASSERT(GraphicsWindow::GetHwnd() != nullptr);

  /* Set the display mode: switch to a fullscreen mode or revert to windowed
   * mode. */
  LOG->Trace("SetScreenMode ...");
  std::string err = GraphicsWindow::SetScreenMode(p);
  if (!err.empty()) {
    return err;
  }

  PIXELFORMATDESCRIPTOR pixfmt;
  int pixel_format = ChooseWindowPixelFormat(p, &pixfmt);
  if (pixel_format == 0) {
    /* Destroy the window. */
    DestroyGraphicsWindowAndOpenGLContext();
    return "Pixel format not found";
  }

  bool need_to_set_pixel_format = false;
  {
    /* We'll need to recreate it if the pixel format is going to change.  We
     * aren't allowed to change the pixel format twice. */
    PIXELFORMATDESCRIPTOR dest_pixel_format;
    ZERO(dest_pixel_format);
    DescribePixelFormat(
        GraphicsWindow::GetHDC(), pixel_format, sizeof(PIXELFORMATDESCRIPTOR),
        &dest_pixel_format);
    if (memcmp(
            &dest_pixel_format, &current_pixel_format,
            sizeof(PIXELFORMATDESCRIPTOR))) {
      LOG->Trace("Reset: pixel format changing");
      need_to_set_pixel_format = true;
    }
  }

  if (need_to_set_pixel_format && !can_set_pixel_format) {
    /*
     * The screen mode has changed, so we need to set the pixel format.  If
     * we're not allowed to do so, destroy the window and make a new one.
     *
     * For some reason, if we destroy the old window before creating the new
     * one, the "maximized apps go under the taskbar" glitch will happen when we
     * quit. We have to create the new window first.
     */
    LOG->Trace(
        "Mode requires new pixel format, and we've already set one; resetting "
        "OpenGL context");
    if (hglrc != nullptr) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(hglrc);
      hglrc = nullptr;
    }

    new_device_out = true;
  }

  /* If we deleted the OpenGL context above, also recreate the window.
   * Otherwise, just reconfigure it. */
  GraphicsWindow::CreateGraphicsWindow(p, new_device_out);

  if (need_to_set_pixel_format) {
    /* Set the pixel format. */
    if (!SetPixelFormat(GraphicsWindow::GetHDC(), pixel_format, &pixfmt)) {
      DWORD last_error = GetLastError();
      /* Destroy the window. */
      DestroyGraphicsWindowAndOpenGLContext();

      return werr_ssprintf(last_error, "Pixel format failed");
    }

    DescribePixelFormat(
        GraphicsWindow::GetHDC(), pixel_format, sizeof(current_pixel_format),
        &current_pixel_format);

    DumpPixelFormat(current_pixel_format);
  }

  if (hglrc == nullptr) {
    hgl_module = LoadLibraryA("opengl32.dll");

    hglrc = wglCreateContext(GraphicsWindow::GetHDC());
    if (hglrc == nullptr) {
      DWORD last_error = GetLastError();
      DestroyGraphicsWindowAndOpenGLContext();
      return hr_ssprintf(last_error, "wglCreateContext");
    }

    if (!wglMakeCurrent(GraphicsWindow::GetHDC(), hglrc)) {
      DWORD last_error = GetLastError();
      DestroyGraphicsWindowAndOpenGLContext();
      return hr_ssprintf(last_error, "wglCreateContext");
    }
  }
  return std::string();  // we set the video mode successfully
}

static LocalizedString OPENGL_NOT_AVAILABLE(
    "LowLevelWindow_Win32", "OpenGL hardware acceleration is not available.");
bool LowLevelWindow_Win32::IsSoftwareRenderer(std::string& error) {
  std::string vendor = (const char*)glGetString(GL_VENDOR);
  std::string renderer = (const char*)glGetString(GL_RENDERER);

  LOG->Trace(
      "LowLevelWindow_Win32::IsSoftwareRenderer '%s', '%s'", vendor.c_str(),
      renderer.c_str());

  if (vendor == "Microsoft Corporation" && renderer == "GDI Generic") {
    error = OPENGL_NOT_AVAILABLE;
    return true;
  }

  return false;
}

void LowLevelWindow_Win32::SwapBuffers() {
  ::SwapBuffers(GraphicsWindow::GetHDC());
}

void LowLevelWindow_Win32::Update() {
  ::ShowCursor(PREFSMAN->show_mouse_cursor_);
  GraphicsWindow::Update();
}

const ActualVideoModeParams LowLevelWindow_Win32::GetActualVideoModeParams()
    const {
  return GraphicsWindow::GetParams();
}

class RenderTarget_Win32 : public RenderTarget {
 public:
  RenderTarget_Win32(LowLevelWindow_Win32* wind);
  virtual ~RenderTarget_Win32();

  void Create(
      const RenderTargetParam& param, int& texture_width_out,
      int& texture_height_out);
  uintptr_t GetTexture() const { return static_cast<uintptr_t>(tex_handle_); }
  void StartRenderingTo();
  void FinishRenderingTo();

  virtual bool InvertY() const { return true; }

 private:
  LowLevelWindow_Win32* wind_;
  int width_;
  int height_;
  GLuint tex_handle_;
  HDC old_device_context_;
  HGLRC old_render_context_;
};

RenderTarget_Win32::RenderTarget_Win32(LowLevelWindow_Win32* wind) {
  wind_ = wind;
  tex_handle_ = 0;
  old_device_context_ = nullptr;
  old_render_context_ = nullptr;
}

RenderTarget_Win32::~RenderTarget_Win32() {
  glDeleteTextures(
      1, &tex_handle_);  // deleting a 0 texture is safe and ignored
}

void RenderTarget_Win32::Create(
    const RenderTargetParam& param, int& texture_width_out,
    int& texture_height_out) {
  param_ = param;
  width_ = param.width_;
  height_ = param.height_;

  FlushGLErrors();

  glGenTextures(1, &tex_handle_);
  ASSERT(tex_handle_ > 0);
  glBindTexture(GL_TEXTURE_2D, tex_handle_);

  int texture_width = power_of_two(param.width_);
  int texture_height = power_of_two(param.height_);
  texture_width_out = texture_width;
  texture_height_out = texture_height;

  GLenum internalformat;
  GLenum type = param.with_alpha_ ? GL_RGBA : GL_RGB;
  if (param.float_ && GLEW_ARB_texture_float) {
    internalformat = param.with_alpha_ ? GL_RGBA16F_ARB : GL_RGB16F_ARB;
  } else {
    internalformat = param.with_alpha_ ? GL_RGBA8 : GL_RGB8;
  }

  glTexImage2D(
      GL_TEXTURE_2D, 0, internalformat, texture_width, texture_height, 0, type,
      GL_UNSIGNED_BYTE, nullptr);

  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  AssertNoGLError();
}

void RenderTarget_Win32::StartRenderingTo() {
  old_device_context_ = wglGetCurrentDC();
  old_render_context_ = wglGetCurrentContext();

  BOOL successful = wglMakeCurrent(GraphicsWindow::GetHDC(), hglrc);
  ASSERT_M(
      successful == TRUE,
      "wglMakeCurrent failed in RenderTarget_Win32::StartRenderingTo()");

  FlushGLErrors();
  glBindTexture(GL_TEXTURE_2D, tex_handle_);
  AssertNoGLError();
}

void RenderTarget_Win32::FinishRenderingTo() {
  FlushGLErrors();

  glBindTexture(GL_TEXTURE_2D, tex_handle_);
  AssertNoGLError();

  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);

  glBindTexture(GL_TEXTURE_2D, 0);

  AssertNoGLError();

  BOOL successful = wglMakeCurrent(old_device_context_, old_render_context_);
  ASSERT_M(
      successful == TRUE,
      "wglMakeCurrent failed in RenderTarget_Win32::FinishRenderingTo()");

  old_device_context_ = 0;
  old_render_context_ = 0;
}

RenderTarget* LowLevelWindow_Win32::CreateRenderTarget() {
  return new RenderTarget_Win32(this);
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
