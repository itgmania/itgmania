#import "LowLevelWindow_MacOSX.h"
#import "DisplaySpec.h"
#import "RageDisplay_OGL_Helpers.h"
#import "RageThreads.h"
#import "RageUtil.h"
#import "arch/ArchHooks/ArchHooks.h"
#import "global.h"

#include <dlfcn.h>
#include <cstddef>
#include <cstdint>

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

// Bad header!
extern "C" {
#include <IOKit/graphics/IOGraphicsLib.h>
}

static const unsigned int kStyleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
static bool resized;
static int g_width;
static int g_height;
static RageMutex resize_lock("Window resize lock.");

// Simple helper class
class AutoreleasePool {
  AutoreleasePool(const AutoreleasePool&);
  AutoreleasePool& operator=(const AutoreleasePool&);
  NSAutoreleasePool* pool_;

 public:
  AutoreleasePool() { pool_ = [[NSAutoreleasePool alloc] init]; }
  ~AutoreleasePool() { [pool_ release]; }
};

// Window delegate class
@interface SMWindowDelegate : NSObject {
 @public
  NSWindow* window_;
}
- (void)windowDidBecomeKey:(NSNotification*)notification;
- (void)windowDidResignKey:(NSNotification*)notification;
- (void)windowWillClose:(NSNotification*)notification;
- (void)windowDidResize:(NSNotification*)notification;
// XXX maybe use whichever screen contains the window? Hard for me to test though.
//- (void) windowDidChangeScreen:(NSNotification *)aNotification;

// Helper methods to perform actions on the main thread.
- (void)setupWindow;
- (void)closeWindow;
- (void)setParams:(NSValue*)params;
@end

@implementation SMWindowDelegate

- (void)windowDidBecomeKey:(NSNotification*)notification {
  HOOKS->SetHasFocus(true);
}

- (void)windowDidResignKey:(NSNotification*)notification {
  HOOKS->SetHasFocus(false);
}

- (void)windowWillClose:(NSNotification*)notification {
  ArchHooks::SetUserQuit();
}

- (void)windowDidResize:(NSNotification*)notification {
  id window = [notification object];
  NSSize size = [NSWindow contentRectForFrameRect:[window frame] styleMask:kStyleMask].size;

  LockMut(resize_lock);
  resized = true;
  g_width = int(size.width);
  g_height = int(size.height);
}

- (void)setupWindow {
  NSRect rect = NSMakeRect(0, 0, 0, 0);
  window_ = [[NSWindow alloc] initWithContentRect:rect
                                        styleMask:kStyleMask
                                          backing:NSBackingStoreBuffered
                                            defer:YES];

  [window_ setExcludedFromWindowsMenu:YES];
  [window_ setReleasedWhenClosed:NO];
  [window_ setDelegate:static_cast<id<NSWindowDelegate> >(self)];
}

- (void)closeWindow {
  [window_ setDelegate:nil];
  [window_ close];
  [window_ release];
}

- (void)setParams:(NSValue*)params {
  const VideoModeParams& p = *(const VideoModeParams*)[params pointerValue];
  NSRect content_rect = {{0, 0}, {static_cast<CGFloat>(p.width_), static_cast<CGFloat>(p.height_)}};

  [window_ setContentSize:content_rect.size];
  [window_ setTitle:[NSString stringWithUTF8String:p.window_title_.c_str()]];
  [window_ center];
  [window_ makeKeyAndOrderFront:nil];
}
@end

enum GLContextType { WINDOWED, FULL_SCREEN, PIXEL_BUFFER };

static NSOpenGLContext* CreateOGLContext(
    GLContextType type, int color_size, int alpha_size, int depth_size, NSOpenGLContext* share,
    bool& shared) {
  NSOpenGLPixelFormatAttribute attrs[] = {
      NSOpenGLPFANoRecovery,  // so we can share with the full screen context
      NSOpenGLPFAAccelerated, NSOpenGLPFAMinimumPolicy, NSOpenGLPFAColorSize,
      NSOpenGLPixelFormatAttribute(color_size), NSOpenGLPFAAlphaSize,
      NSOpenGLPixelFormatAttribute(alpha_size), NSOpenGLPFADepthSize,
      NSOpenGLPixelFormatAttribute(depth_size),
      /*
                      NSOpenGLPFAMultisample,
                      NSOpenGLPFASampleBuffers, (NSOpenGLPixelFormatAttribute)1,
                      NSOpenGLPFASamples, (NSOpenGLPixelFormatAttribute)4,
      */
      NSOpenGLPixelFormatAttribute(0),  // 9
      NSOpenGLPixelFormatAttribute(0),  // 10
      NSOpenGLPixelFormatAttribute(0),  // 11
      NSOpenGLPixelFormatAttribute(0),  // 12
      NSOpenGLPixelFormatAttribute(0)   // Must be at the end.
  };
  const int n = 9;  // The first noncommon index.

  switch (type) {
    case WINDOWED:
      attrs[n + 0] = NSOpenGLPFAWindow;
      attrs[n + 1] = NSOpenGLPFADoubleBuffer;
      break;
    case FULL_SCREEN:
      attrs[n + 0] = NSOpenGLPFAFullScreen;
      attrs[n + 1] = NSOpenGLPFADoubleBuffer;
      attrs[n + 2] = NSOpenGLPFAScreenMask;
      attrs[n + 3] =
          NSOpenGLPixelFormatAttribute(CGDisplayIDToOpenGLDisplayMask(kCGDirectMainDisplay));
      break;
    case PIXEL_BUFFER:
      attrs[n + 0] = NSOpenGLPFAOffScreen;
      attrs[n + 1] = NSOpenGLPFAPixelBuffer;
      break;
  }

  NSOpenGLPixelFormat* pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];

  if (!pixel_format) {
    return nil;
  }

  NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixel_format
                                                        shareContext:share];

  shared = share && context;
  if (!context) {
    context = [[NSOpenGLContext alloc] initWithFormat:pixel_format shareContext:nil];
  }

  [pixel_format release];

  return context;
}

class RenderTarget_MacOSX : public RenderTarget {
 public:
  RenderTarget_MacOSX(id share_context);
  ~RenderTarget_MacOSX();
  void Create(const RenderTargetParam& param, int& texture_width_out, int& texture_height_out);
  uintptr_t GetTexture() const { return static_cast<uintptr_t>(tex_handle_); }
  void StartRenderingTo();
  void FinishRenderingTo();

 private:
  NSOpenGLContext *share_context_, *old_context_, *p_buffer_context_;
  GLuint tex_handle_;
  int width_, height_;
};

RenderTarget_MacOSX::RenderTarget_MacOSX(id share_context) {
  share_context_ = share_context;
  old_context_ = nil;
  p_buffer_context_ = nil;
  tex_handle_ = 0;
  width_ = 0;
  height_ = 0;
}

RenderTarget_MacOSX::~RenderTarget_MacOSX() {
  AutoreleasePool pool;
  [p_buffer_context_ release];
  if (tex_handle_) {
    glDeleteTextures(1, &tex_handle_);
  }
}

void RenderTarget_MacOSX::Create(
    const RenderTargetParam& param, int& texture_width_out, int& texture_height_out) {
  AutoreleasePool pool;
  width_ = param.width_;
  height_ = param.height_;

  // PBuffer needs to be a power of 2.
  int texture_width = power_of_two(param.width_);
  int texture_height = power_of_two(param.height_);

  // Create the PBuffer.
  unsigned long format = param.with_alpha_ ? GL_RGBA : GL_RGB;
  NSOpenGLPixelBuffer* p_buffer =
      [[NSOpenGLPixelBuffer alloc] initWithTextureTarget:GL_TEXTURE_2D
                                   textureInternalFormat:format
                                   textureMaxMipMapLevel:0  // No idea.
                                              pixelsWide:texture_width
                                              pixelsHigh:texture_height];
  DEBUG_ASSERT(p_buffer);

  // Create an OGL context.
  bool shared = false;
  p_buffer_context_ = CreateOGLContext(
      PIXEL_BUFFER, 24, param.with_alpha_ ? 8 : 0, param.with_depth_buffer_ ? 16 : 0,
      share_context_, shared);
  DEBUG_ASSERT(p_buffer_context_);
  DEBUG_ASSERT(shared);
  [p_buffer_context_ setPixelBuffer:p_buffer
                        cubeMapFace:0
                        mipMapLevel:0
               currentVirtualScreen:[share_context_ currentVirtualScreen]];
  [p_buffer release];  // XXX: Hopefully this is retained by the PBufferContext.

  glGenTextures(1, &tex_handle_);
  glBindTexture(GL_TEXTURE_2D, tex_handle_);

  while (glGetError() != GL_NO_ERROR);

  texture_width_out = texture_width;
  texture_height_out = texture_height;

  glTexImage2D(
      GL_TEXTURE_2D, 0, param.with_alpha_ ? GL_RGBA8 : GL_RGB8, texture_width, texture_height, 0,
      param.with_alpha_ ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, nil);
  GLenum error = glGetError();
  ASSERT_M(error == GL_NO_ERROR, RageDisplay_Legacy_Helpers::GLToString(error));

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void RenderTarget_MacOSX::StartRenderingTo() {
  DEBUG_ASSERT(!old_context_);
  old_context_ = [NSOpenGLContext currentContext];
  [p_buffer_context_ makeCurrentContext];
  glViewport(0, 0, width_, height_);
}

void RenderTarget_MacOSX::FinishRenderingTo() {
  DEBUG_ASSERT(old_context_);
  glBindTexture(GL_TEXTURE_2D, tex_handle_);

  while (glGetError() != GL_NO_ERROR);

  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);

  GLenum error = glGetError();
  ASSERT_M(error == GL_NO_ERROR, RageDisplay_Legacy_Helpers::GLToString(error));

  glBindTexture(GL_TEXTURE_2D, 0);

  [old_context_ makeCurrentContext];
  old_context_ = nil;
}

LowLevelWindow_MacOSX::LowLevelWindow_MacOSX()
    : context_(nil), current_display_mode_(nil), display_id_(0) {
  AutoreleasePool pool;
  window_delegate_ = [[SMWindowDelegate alloc] init];
  [window_delegate_ performSelectorOnMainThread:@selector(setupWindow)
                                     withObject:nil
                                  waitUntilDone:YES];

  current_params_.windowed_ = true;  // We are essentially windowed to begin with.
  SetActualParamsFromMode(CGDisplayCurrentMode(kCGDirectMainDisplay));
  dispatch_sync(dispatch_get_main_queue(), ^{
    HOOKS->SetHasFocus([NSApp isActive]);
    ;
  });
}

LowLevelWindow_MacOSX::~LowLevelWindow_MacOSX() {
  AutoreleasePool pool;
  ShutDownFullScreen();

  [context_ clearDrawable];
  [context_ release];
  [window_delegate_ performSelectorOnMainThread:@selector(closeWindow)
                                     withObject:nil
                                  waitUntilDone:YES];
  [window_delegate_ release];
}

void* LowLevelWindow_MacOSX::GetProcAddress(std::string s) {
  // dlsym returns NULL if symbol not found
  static void* gl_handle = nullptr;
  if (!gl_handle) {
    gl_handle =
        dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL", RTLD_LAZY);
  }
  return dlsym(gl_handle, s.c_str());
}

std::string LowLevelWindow_MacOSX::TryVideoMode(const VideoModeParams& p, bool& new_device_out) {
  // Always set these params.
  current_params_.smooth_lines_ = p.smooth_lines_;
  current_params_.trilinear_filtering_ = p.trilinear_filtering_;
  current_params_.anisotropic_filtering_ = p.anisotropic_filtering_;
  current_params_.interlaced_ = p.interlaced_;
  current_params_.pal_ = p.pal_;
  current_params_.display_aspect_ratio_ = p.display_aspect_ratio_;

#define X(x) p.x != current_params_.x
  const bool rebuild_context = X(bpp_) || X(windowed_) || !context_;
  const bool change_mode = X(width_) || X(height_) || X(rate_) || rebuild_context;
  const bool change_vsync = X(vsync_) || rebuild_context;
#undef X

  if (!change_mode && !change_vsync) {
    return std::string();
  }

  AutoreleasePool pool;
  new_device_out = false;

  ASSERT(p.bpp_ == 16 || p.bpp_ == 32);

  // If we don't have focus, we cannot be full screen.
  if (p.windowed_ || !HOOKS->AppHasFocus()) {
    if (rebuild_context) {
      bool shared;
      NSOpenGLContext* new_context = CreateOGLContext(
          WINDOWED, p.bpp_ == 16 ? 16 : 24, p.bpp_ == 16 ? 1 : 8, 16, context_, shared);

      if (!new_context) {
        return "Failed to create OGL context.";
      }
      ShutDownFullScreen();
      [context_ release];
      context_ = new_context;
      new_device_out = !shared;
      current_params_.bpp_ = p.bpp_;
    }

    [window_delegate_ performSelectorOnMainThread:@selector(setParams:)
                                       withObject:[NSValue valueWithPointer:&p]
                                    waitUntilDone:YES];
    dispatch_sync(dispatch_get_main_queue(), ^{
      [context_ setView:[((SMWindowDelegate*)window_delegate_)->window_ contentView]];
      [context_ update];
    });
    [context_ makeCurrentContext];
    current_params_.windowed_ = true;
    SetActualParamsFromMode(CGDisplayCurrentMode(kCGDirectMainDisplay));

    if (change_vsync) {
      GLint swap = p.vsync_ ? 1 : 0;
      CGLSetParameter([context_ CGLContextObj], kCGLCPSwapInterval, &swap);
      current_params_.vsync_ = p.vsync_;
    }

    return std::string();
  }
  if (change_mode) {
    int result = ChangeDisplayMode(p);

    if (result) {
      return ssprintf(
          "Failed to switch to full screen:%d x %d @ %d. Error %d.", p.width_, p.height_, p.rate_,
          result);
    }
  }

  if (rebuild_context) {
    bool shared;
    NSOpenGLContext* new_context = CreateOGLContext(
        FULL_SCREEN, p.bpp_ == 16 ? 16 : 24, p.bpp_ == 16 ? 1 : 8, 16, context_, shared);

    if (!new_context) {
      return "Failed to create full screen OGL context.";
    }
    [context_ clearDrawable];
    [context_ release];
    context_ = new_context;
    new_device_out = !shared;
    current_params_.bpp_ = p.bpp_;
  }
  dispatch_sync(dispatch_get_main_queue(), ^{
    [context_ setFullScreen];
    [context_ update];
  });
  [context_ makeCurrentContext];

  if (change_vsync) {
    GLint swap = p.vsync_ ? 1 : 0;
    CGLSetParameter([context_ CGLContextObj], kCGLCPSwapInterval, &swap);
    current_params_.vsync_ = p.vsync_;
  }

  return std::string();
}

void LowLevelWindow_MacOSX::ShutDownFullScreen() {
  if (current_params_.windowed_) {
    return;
  }
  ASSERT(current_display_mode_);

  // Clear the front and back framebuffers before switching out of FullScreen mode.
  // (This is not strictly necessary, but avoids an untidy flash of garbage.)
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  [context_ flushBuffer];
  glClear(GL_COLOR_BUFFER_BIT);
  [context_ flushBuffer];

  [NSOpenGLContext clearCurrentContext];
  [context_ clearDrawable];

  CGDisplayErr err = CGDisplaySwitchToMode(kCGDirectMainDisplay, current_display_mode_);

  ASSERT(err == kCGErrorSuccess);
  CGDisplayShowCursor(kCGDirectMainDisplay);
  err = CGDisplayRelease(display_id_);
  ASSERT(err == kCGErrorSuccess);
  SetActualParamsFromMode(current_display_mode_);
  // We don't own this so we cannot release it.
  current_display_mode_ = nil;
  current_params_.windowed_ = true;
}

int LowLevelWindow_MacOSX::ChangeDisplayMode(const VideoModeParams& p) {
  CFDictionaryRef mode = nil;
  CFDictionaryRef new_mode;
  CGDisplayErr err;

  if (!current_display_mode_) {
    display_id_ = CGMainDisplayID();
    if ((err = CGDisplayCapture(display_id_)) != kCGErrorSuccess) {
      return err;
    }
    // Only hide the first time we go to full screen.
    CGDisplayHideCursor(kCGDirectMainDisplay);
    mode = CGDisplayCurrentMode(kCGDirectMainDisplay);
  }

  if (p.rate_ == kRefreshDefault) {
    new_mode =
        CGDisplayBestModeForParameters(kCGDirectMainDisplay, p.bpp_, p.width_, p.height_, nil);
  } else {
    new_mode = CGDisplayBestModeForParametersAndRefreshRate(
        kCGDirectMainDisplay, p.bpp_, p.width_, p.height_, p.rate_, nil);
  }

  err = CGDisplaySwitchToMode(kCGDirectMainDisplay, new_mode);

  if (err != kCGErrorSuccess) {
    return err;  // We don't own mode, don't release it.
  }

  if (!current_display_mode_) {
    current_display_mode_ = mode;
  }
  current_params_.windowed_ = false;
  SetActualParamsFromMode(new_mode);

  return 0;
}

// http://lukassen.wordpress.com/2010/01/18/taming-snow-leopard-cgdisplaybitsperpixel-deprication/
static size_t GetDisplayBitsPerPixel(CGDirectDisplayID display_id) {
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id);
  size_t depth = 0;

  CFStringRef pix_enc = CGDisplayModeCopyPixelEncoding(mode);
  if (CFStringCompare(pix_enc, CFSTR(IO32BitDirectPixels), kCFCompareCaseInsensitive) ==
      kCFCompareEqualTo) {
    depth = 32;
  } else if (
      CFStringCompare(pix_enc, CFSTR(IO16BitDirectPixels), kCFCompareCaseInsensitive) ==
      kCFCompareEqualTo) {
    depth = 16;
  } else if (
      CFStringCompare(pix_enc, CFSTR(IO8BitIndexedPixels), kCFCompareCaseInsensitive) ==
      kCFCompareEqualTo) {
    depth = 8;
  }

  CFRelease(pix_enc);
  CGDisplayModeRelease(mode);

  return depth;
}

void LowLevelWindow_MacOSX::SetActualParamsFromMode(CFDictionaryRef mode) {
  SInt32 rate;
  bool ret = CFNumberGetValue(
      (CFNumberRef)CFDictionaryGetValue(mode, CFSTR("RefreshRate")), kCFNumberSInt32Type, &rate);

  if (!ret || rate == 0) {
    rate = 60;
  }
  current_params_.rate_ = rate;

  if (!current_params_.windowed_) {
    GLint swap;
    current_params_.width_ = CGDisplayPixelsWide(kCGDirectMainDisplay);
    current_params_.height_ = CGDisplayPixelsHigh(kCGDirectMainDisplay);
    CGLGetParameter(CGLGetCurrentContext(), kCGLCPSwapInterval, &swap);
    current_params_.vsync_ = swap != 0;
  } else {
    __block NSSize size;
    dispatch_sync(dispatch_get_main_queue(), ^{
      size = [[((SMWindowDelegate*)window_delegate_)->window_ contentView] frame].size;
    });

    current_params_.width_ = int(size.width);
    current_params_.height_ = int(size.height);
  }

  current_params_.bpp_ = GetDisplayBitsPerPixel(kCGDirectMainDisplay);
}

static int GetIntValue(CFTypeRef r) {
  int ret;

  if (!r || CFGetTypeID(r) != CFNumberGetTypeID() ||
      !CFNumberGetValue(CFNumberRef(r), kCFNumberIntType, &ret)) {
    return 0;
  }
  return ret;
}

static bool GetBoolValue(CFTypeRef r) {
  return r && CFGetTypeID(r) == CFBooleanGetTypeID() && CFBooleanGetValue(CFBooleanRef(r));
}

static double GetDoubleValue(CFTypeRef r) {
  double ret;

  if (!r || CFGetTypeID(r) != CFNumberGetTypeID() ||
      !CFNumberGetValue(CFNumberRef(r), kCFNumberDoubleType, &ret)) {
    return 0;
  }
  return ret;
}

static DisplayMode ConvertDisplayMode(CFDictionaryRef dict) {
  int width = GetIntValue(CFDictionaryGetValue(dict, kCGDisplayWidth));
  int height = GetIntValue(CFDictionaryGetValue(dict, kCGDisplayHeight));
  double rate = GetDoubleValue(CFDictionaryGetValue(dict, kCGDisplayRefreshRate));

  return {static_cast<unsigned int>(width), static_cast<unsigned int>(height), rate};
}

void LowLevelWindow_MacOSX::GetDisplaySpecs(DisplaySpecs& specs) const {
  CFArrayRef modes = CGDisplayAvailableModes(kCGDirectMainDisplay);
  ASSERT(modes);
  const CFIndex count = CFArrayGetCount(modes);

  std::set<DisplayMode> available;
  CFDictionaryRef current_mode_dict = CGDisplayCurrentMode(kCGDirectMainDisplay);
  DisplayMode current = ConvertDisplayMode(current_mode_dict);

  for (CFIndex i = 0; i < count; ++i) {
    CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(modes, i);
    CFTypeRef safe = CFDictionaryGetValue(dict, kCGDisplayModeIsSafeForHardware);
    DisplayMode mode = ConvertDisplayMode(dict);

    if (!mode.width_ || !mode.height_) {
      continue;
    }
    if (safe && !GetBoolValue(safe)) {
      continue;
    }
    available.insert(mode);
  }
  // Do not release modes! We don't own them here.
  RectI bounds(0, 0, current.width_, current.height_);
  DisplaySpec s("", "Fullscreen", available, current, bounds);
  specs.insert(s);
}

void LowLevelWindow_MacOSX::SwapBuffers() { CGLFlushDrawable(CGLGetCurrentContext()); }

void LowLevelWindow_MacOSX::Update() {
  // Keep the system from sleeping or the screen saver from activating.
  UpdateSystemActivity(IdleActivity);

  LockMutex lock(resize_lock);
  if (likely(!resized)) {
    return;
  }
  resized = false;
  if (current_params_.width_ == g_width && current_params_.height_ == g_height) {
    return;
  }
  current_params_.width_ = g_width;
  current_params_.height_ = g_height;
  lock.Unlock();  // Unlock before calling ResolutionChanged().
  dispatch_sync(dispatch_get_main_queue(), ^{
    [context_ update];
  });

  DISPLAY->ResolutionChanged();
}

RenderTarget* LowLevelWindow_MacOSX::CreateRenderTarget() {
  return new RenderTarget_MacOSX(context_);
}

/*
 * (c) 2005-2006, 2008 Steve Checkoway
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
