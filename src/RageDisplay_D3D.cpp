#include "RageDisplay_D3D.h"

#include <d3d9.h>

#include <algorithm>
#include <string>

#include "DisplaySpec.h"
#include "EnumHelper.h"
#include "LocalizedString.h"
#include "RageDisplay.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/GraphicsWindow.h"
#include "global.h"

// Static libraries
// load Windows D3D9 dynamically
#if defined(_MSC_VER)
#pragma comment(lib, "d3d9.lib")
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

// Globals
HMODULE d3d9_module = nullptr;
LPDIRECT3D9 d3d = nullptr;
LPDIRECT3DDEVICE9 d3d_device = nullptr;
D3DCAPS9 device_caps;
D3DDISPLAYMODE desktop_mode;
D3DPRESENT_PARAMETERS g_d3dpp;
static bool sphere_mapping[NUM_TextureUnit] = {false, false};

// TODO: Instead of defining this here, enumerate the possible formats and
// select whatever one we want to use. This format should be fine for the uses
// of this application though.
const D3DFORMAT kDefaultAdapterFormat = D3DFMT_X8R8G8B8;

/* Direct3D doesn't associate a palette with textures. Instead, we load a
 * palette into a slot. We need to keep track of which texture's palette is
 * stored in what slot. */
std::map<uintptr_t, size_t> tex_resource_to_palette_index;
std::list<size_t> palette_index;
struct TexturePalette {
  PALETTEENTRY p_[256];
};
std::map<uintptr_t, TexturePalette> tex_resource_to_texture_palette;

// Load the palette, if any, for the given texture into a palette slot, and make
// it current.
static void SetPalette(uintptr_t tex_resource) {
  // If the texture isn't paletted, we have nothing to do.
  if (tex_resource_to_texture_palette.find(tex_resource) ==
      tex_resource_to_texture_palette.end()) {
    return;
  }

  // Is the palette already loaded?
  if (tex_resource_to_palette_index.find(tex_resource) ==
      tex_resource_to_palette_index.end()) {
    // It's not. Grab the least recently used slot.
    UINT pal_index = static_cast<UINT>(palette_index.front());

    // If any other texture is currently using this slot, mark that palette
    // unloaded.
    for (std::map<uintptr_t, size_t>::iterator i =
             tex_resource_to_palette_index.begin();
         i != tex_resource_to_palette_index.end(); ++i) {
      if (i->second != pal_index) {
        continue;
      }
      tex_resource_to_palette_index.erase(i);
      break;
    }

    // Load it.
    TexturePalette& pal = tex_resource_to_texture_palette[tex_resource];
    d3d_device->SetPaletteEntries(pal_index, pal.p_);

    tex_resource_to_palette_index[tex_resource] = pal_index;
  }

  const int pal_index = tex_resource_to_palette_index[tex_resource];

  // Find this palette index in the least-recently-used queue and move it to the
  // end.
  for (std::list<size_t>::iterator i = palette_index.begin();
       i != palette_index.end(); ++i) {
    if (*i != pal_index) {
      continue;
    }
    palette_index.erase(i);
    palette_index.push_back(pal_index);
    break;
  }

  d3d_device->SetCurrentTexturePalette(pal_index);
}

#define D3DFVF_RageSpriteVertex \
  (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define D3DFVF_RageModelVertex (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)

static const RageDisplay::RagePixelFormatDesc
    kPixelFormatDesc[NUM_RagePixelFormat] = {
        {/* A8B8G8R8 */
         32,
         {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000}},
        {0, {0, 0, 0, 0}},
        {
            /* A4R4G4B4 */
            16,
            {0x0F00, 0x00F0, 0x000F, 0xF000},
        },
        {
            /* A1B5G5R5 */
            16,
            {0x7C00, 0x03E0, 0x001F, 0x8000},
        },
        {
            /* X1R5G5B5 */
            16,
            {0x7C00, 0x03E0, 0x001F, 0x0000},
        },
        {/* B8G8R8 */
         24,
         {0xFF0000, 0x00FF00, 0x0000FF, 0x000000}},
        {
            /* Paletted */
            8,
            {0, 0, 0, 0} /* N/A */
        },
        {/* BGR (N/A; OpenGL only) */
         0,
         {0, 0, 0, 0}},
        {/* ABGR (N/A; OpenGL only) */
         0,
         {0, 0, 0, 0}},
        {/* X1R5G5B5 */
         0,
         {0, 0, 0, 0}}};

static D3DFORMAT d3d_formats[NUM_RagePixelFormat] = {
    D3DFMT_A8R8G8B8, D3DFMT_UNKNOWN, D3DFMT_A4R4G4B4, D3DFMT_A1R5G5B5,
    D3DFMT_X1R5G5B5, D3DFMT_R8G8B8,  D3DFMT_P8,
    D3DFMT_UNKNOWN,  // no BGR
    D3DFMT_UNKNOWN,  // no ABGR
    D3DFMT_UNKNOWN,  // X1R5G5B5
};

const RageDisplay::RagePixelFormatDesc* RageDisplay_D3D::GetPixelFormatDesc(
    RagePixelFormat pf) const {
  ASSERT(pf < NUM_RagePixelFormat);
  return &kPixelFormatDesc[pf];
}

RageDisplay_D3D::RageDisplay_D3D() {}

static LocalizedString D3D_NOT_INSTALLED(
    "RageDisplay_D3D",
    "DirectX 9.0c or greater is not installed.  You can download it from:");
const std::string kD3dUrl =
    "http://www.microsoft.com/en-us/download/details.aspx?id=8109";
static LocalizedString HARDWARE_ACCELERATION_NOT_AVAILABLE(
    "RageDisplay_D3D",
    "Your system is reporting that Direct3D hardware acceleration is not "
    "available.  Please obtain an updated driver from your video card "
    "manufacturer.");
std::string RageDisplay_D3D::Init(
    const VideoModeParams& p, bool /* bAllowUnacceleratedRenderer */) {
  GraphicsWindow::Initialize(true);

  LOG->Trace("RageDisplay_D3D::RageDisplay_D3D()");
  LOG->MapLog("renderer", "Current renderer: Direct3D");

  d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if (!d3d) {
    LOG->Trace("Direct3DCreate9 failed");
    return D3D_NOT_INSTALLED.GetValue();
  }

  if (FAILED(d3d->GetDeviceCaps(
          D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &device_caps))) {
    return HARDWARE_ACCELERATION_NOT_AVAILABLE.GetValue();
  }

  D3DADAPTER_IDENTIFIER9 identifier;
  d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier);

  LOG->Trace(
      "Driver: %s\n"
      "Description: %s\n"
      "Max texture size: %d\n"
      "Alpha in palette: %s\n",
      identifier.Driver, identifier.Description, device_caps.MaxTextureWidth,
      (device_caps.TextureCaps & D3DPTEXTURECAPS_ALPHAPALETTE) ? "yes" : "no");

  LOG->Trace("This display adaptor supports the following modes:");
  D3DDISPLAYMODE mode;

  UINT mode_count =
      d3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, kDefaultAdapterFormat);

  for (UINT u = 0; u < mode_count; u++) {
    if (SUCCEEDED(d3d->EnumAdapterModes(
            D3DADAPTER_DEFAULT, kDefaultAdapterFormat, u, &mode))) {
      LOG->Trace(
          "  %ux%u %uHz, format %d", mode.Width, mode.Height, mode.RefreshRate,
          mode.Format);
    }
  }

  palette_index.clear();
  for (int i = 0; i < 256; ++i) {
    palette_index.push_back(i);
  }

  // Save the original desktop format.
  d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &desktop_mode);

  /* Up until now, all we've done is set up g_pd3d and do some queries. Now,
   * actually initialize the window. Do this after as many error conditions as
   * possible, because if we have to shut it down again we'll flash a window
   * briefly. */
  bool ignore = false;
  return SetVideoMode(p, ignore);
}

RageDisplay_D3D::~RageDisplay_D3D() {
  LOG->Trace("RageDisplay_D3D::~RageDisplay()");

  GraphicsWindow::Shutdown();

  if (d3d_device) {
    d3d_device->Release();
    d3d_device = nullptr;
  }

  if (d3d) {
    d3d->Release();
    d3d = nullptr;
  }

  /* Even after we call Release(), D3D may still affect our window. It seems
   * to subclass the window, and never release it. Free the DLL after
   * destroying the window. */
  if (d3d9_module) {
    FreeLibrary(d3d9_module);
    d3d9_module = nullptr;
  }
}

void RageDisplay_D3D::GetDisplaySpecs(DisplaySpecs& out) const {
  out.clear();
  int cnt = d3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, kDefaultAdapterFormat);

  std::set<DisplayMode> modes;
  D3DDISPLAYMODE mode;
  for (int i = 0; i < cnt; ++i) {
    d3d->EnumAdapterModes(D3DADAPTER_DEFAULT, kDefaultAdapterFormat, i, &mode);
    modes.insert(
        {mode.Width, mode.Height, static_cast<double>(mode.RefreshRate)});
  }

  // Get the current display mode
  if (d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode) == D3D_OK) {
    D3DADAPTER_IDENTIFIER9 id;
    d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    DisplayMode active = {
        mode.Width, mode.Height, static_cast<double>(mode.RefreshRate)};
    RectI bounds(0, 0, active.width_, active.height_);
    out.insert(DisplaySpec("", "Fullscreen", modes, active, bounds));
  } else {
    LOG->Warn("Could not find active mode for default D3D adapter");
    if (!modes.empty()) {
      const DisplayMode& m = *modes.begin();
      RectI bounds(0, 0, m.width_, m.height_);
      out.insert(DisplaySpec("", "Fullscreen", modes, m, bounds));
    }
  }
}

D3DFORMAT FindBackBufferType(bool windowed, int bpp) {
  HRESULT hr;

  // If windowed, then bpp is ignored.  Use whatever works.
  std::vector<D3DFORMAT>
      back_buffer_formats;  // throw all possibilities in here

  // When windowed, add all formats; otherwise add only formats that match
  // dwBPP.
  if (bpp == 32 || windowed) {
    back_buffer_formats.push_back(D3DFMT_R8G8B8);
    back_buffer_formats.push_back(D3DFMT_X8R8G8B8);
    back_buffer_formats.push_back(D3DFMT_A8R8G8B8);
  }
  if (bpp == 16 || windowed) {
    back_buffer_formats.push_back(D3DFMT_R5G6B5);
    back_buffer_formats.push_back(D3DFMT_X1R5G5B5);
    back_buffer_formats.push_back(D3DFMT_A1R5G5B5);
  }

  if (!windowed && bpp != 16 && bpp != 32) {
    GraphicsWindow::Shutdown();
    RageException::Throw("Invalid BPP '%i' specified", bpp);
  }

  // Test each back buffer format until we find something that works.
  for (size_t i = 0; i < back_buffer_formats.size(); i++) {
    D3DFORMAT fmt_back_buffer = back_buffer_formats[i];

    D3DFORMAT fmt_display;
    if (windowed) {
      fmt_display = desktop_mode.Format;
    } else {  // Fullscreen
      fmt_display = back_buffer_formats[i];
    }

    LOG->Trace(
        "Testing format: display %d, back buffer %d, windowed %d...",
        fmt_display, fmt_back_buffer, windowed);

    hr = d3d->CheckDeviceType(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, fmt_display, fmt_back_buffer,
        windowed);

    if (FAILED(hr)) {
      continue;  // skip
    }

    // done searching
    LOG->Trace("This will work.");
    return fmt_back_buffer;
  }

  LOG->Trace("Couldn't find an appropriate back buffer format.");
  return D3DFMT_UNKNOWN;
}

std::string SetD3DParams(bool& new_device_out) {
  if (d3d_device == nullptr)  // device is not yet created. We need to create it
  {
    new_device_out = true;
    HRESULT hr = d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GraphicsWindow::GetHwnd(),
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &g_d3dpp,
        &d3d_device);
    if (FAILED(hr)) {
      // Likely D3D_ERR_INVALIDCALL.  The driver probably doesn't support this
      // video mode.
      return ssprintf("CreateDevice failed: '%s'", GetErrorString(hr).c_str());
    }
  } else {
    new_device_out = false;
    // LOG->Warn( "Resetting D3D device" );
    HRESULT hr = d3d_device->Reset(&g_d3dpp);
    if (FAILED(hr)) {
      // Likely D3D_ERR_INVALIDCALL.  The driver probably doesn't support this
      // video mode.
      return ssprintf(
          "g_pd3dDevice->Reset failed: '%s'", GetErrorString(hr).c_str());
    }
  }

  d3d_device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);

  // Palettes were lost by Reset(), so mark them unloaded.
  tex_resource_to_palette_index.clear();

  return std::string();
}

// If the given parameters have failed, try to lower them.
static bool D3DReduceParams(D3DPRESENT_PARAMETERS* pp) {
  D3DDISPLAYMODE current;
  current.Format = pp->BackBufferFormat;
  current.Height = pp->BackBufferHeight;
  current.Width = pp->BackBufferWidth;
  current.RefreshRate = pp->FullScreen_RefreshRateInHz;

  const int cnt =
      d3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, kDefaultAdapterFormat);
  int best = -1;
  int best_score = 0;
  LOG->Trace(
      "cur: %ux%u %uHz, format %i", current.Width, current.Height,
      current.RefreshRate, current.Format);
  for (int i = 0; i < cnt; ++i) {
    D3DDISPLAYMODE mode;
    d3d->EnumAdapterModes(D3DADAPTER_DEFAULT, kDefaultAdapterFormat, i, &mode);

    // Never change the format.
    if (mode.Format != current.Format) {
      continue;
    }
    // Never increase the parameters.
    if (mode.Height > current.Height || mode.Width > current.Width ||
        mode.RefreshRate > current.RefreshRate) {
      continue;
    }

    // Never go below 640x480 unless we already are.
    if ((current.Width >= 640 && current.Height >= 480) &&
        (mode.Width < 640 || mode.Height < 480)) {
      continue;
    }

    // Never go below 60Hz.
    if (mode.RefreshRate && mode.RefreshRate < 60) {
      continue;
    }

    /* If mode.RefreshRate is 0, it means "default". We don't know what
     * that means; assume it's 60Hz. */

    // Higher scores are better.
    int score = 0;
    if (current.RefreshRate >= 70 && mode.RefreshRate < 70) {
      /* Top priority: we really want to avoid dropping to a refresh rate
       * that's below 70Hz. */
      score -= 100000;
    } else if (mode.RefreshRate < current.RefreshRate) {
      /* Low priority: We're lowering the refresh rate, but not too far.
       * current.RefreshRate might be 0, in which case this simply gives
       * points for higher refresh rates. */
      score += (mode.RefreshRate - current.RefreshRate);
    }

    // Medium priority:
    int resolution_diff =
        (current.Height - mode.Height) + (current.Width - mode.Width);
    score -= resolution_diff * 100;

    if (best == -1 || score > best_score) {
      best = i;
      best_score = score;
    }

    LOG->Trace(
        "try: %ux%u %uHz, format %i: score %i", mode.Width, mode.Height,
        mode.RefreshRate, mode.Format, score);
  }

  if (best == -1) {
    return false;
  }

  D3DDISPLAYMODE best_mode;
  d3d->EnumAdapterModes(
      D3DADAPTER_DEFAULT, kDefaultAdapterFormat, best, &best_mode);
  pp->BackBufferHeight = best_mode.Height;
  pp->BackBufferWidth = best_mode.Width;
  pp->FullScreen_RefreshRateInHz = best_mode.RefreshRate;

  return true;
}

static void SetPresentParametersFromVideoModeParams(
    const VideoModeParams& p, D3DPRESENT_PARAMETERS* d3dpp) {
  ZERO(*d3dpp);

  d3dpp->BackBufferWidth = p.width_;
  d3dpp->BackBufferHeight = p.height_;
  d3dpp->BackBufferFormat = FindBackBufferType(p.windowed_, p.bpp_);
  d3dpp->BackBufferCount = 1;
  d3dpp->MultiSampleType = D3DMULTISAMPLE_NONE;
  d3dpp->SwapEffect = D3DSWAPEFFECT_DISCARD;
  d3dpp->hDeviceWindow = GraphicsWindow::GetHwnd();
  d3dpp->Windowed = p.windowed_;
  d3dpp->EnableAutoDepthStencil = TRUE;
  d3dpp->AutoDepthStencilFormat = D3DFMT_D16;

  if (p.windowed_) {
    d3dpp->PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
  } else {
    d3dpp->PresentationInterval =
        p.vsync_ ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
  }

  d3dpp->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
  if (!p.windowed_ && p.rate_ != kRefreshDefault) {
    d3dpp->FullScreen_RefreshRateInHz = p.rate_;
  }

  d3dpp->Flags = 0;

  LOG->Trace(
      "Present Parameters: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
      d3dpp->BackBufferWidth, d3dpp->BackBufferHeight, d3dpp->BackBufferFormat,
      d3dpp->BackBufferCount, d3dpp->MultiSampleType, d3dpp->SwapEffect,
      d3dpp->hDeviceWindow, d3dpp->Windowed, d3dpp->EnableAutoDepthStencil,
      d3dpp->AutoDepthStencilFormat, d3dpp->Flags,
      d3dpp->FullScreen_RefreshRateInHz, d3dpp->PresentationInterval);
}

// Set the video mode.
std::string RageDisplay_D3D::TryVideoMode(
    const VideoModeParams& params, bool& new_device_out) {
  VideoModeParams p = params;
  LOG->Warn(
      "RageDisplay_D3D::TryVideoMode( %d, %d, %d, %d, %d, %d )", p.windowed_,
      p.width_, p.height_, p.bpp_, p.rate_, p.vsync_);

  if (FindBackBufferType(p.windowed_, p.bpp_) ==
      D3DFMT_UNKNOWN) {  // no possible back buffer formats
    return ssprintf(
        "FindBackBufferType(%i,%i) failed", p.windowed_,
        p.bpp_);  // failed to set mode
  }

  /* Set up and display the window before setting up D3D. If we don't do this,
   * then setting up a fullscreen window (when we're not coming from windowed)
   * causes all other windows on the system to be resized to the new resolution.
   */
  GraphicsWindow::CreateGraphicsWindow(p);

  SetPresentParametersFromVideoModeParams(p, &g_d3dpp);

  // Display the window immediately, so we don't display the desktop ...
  while (1) {
    // Try the video mode.
    std::string err = SetD3DParams(new_device_out);
    if (err.empty()) {
      break;
    }

    /* It failed. We're probably selecting a video mode that isn't supported.
     * If we're fullscreen, search the mode list and find the nearest lower
     * mode. */
    if (p.windowed_ || !D3DReduceParams(&g_d3dpp)) {
      return err;
    }

    // Store the new settings we're about to try.
    p.height_ = g_d3dpp.BackBufferHeight;
    p.width_ = g_d3dpp.BackBufferWidth;
    if (g_d3dpp.FullScreen_RefreshRateInHz == D3DPRESENT_RATE_DEFAULT) {
      p.rate_ = kRefreshDefault;
    } else {
      p.rate_ = g_d3dpp.FullScreen_RefreshRateInHz;
    }
  }

  /* Call this again after changing the display mode. If we're going to a window
   * from fullscreen, the first call can't set a larger window than the old
   * fullscreen resolution or set the window position. */
  GraphicsWindow::CreateGraphicsWindow(p);

  ResolutionChanged();

  return std::string();  // mode change successful
}

void RageDisplay_D3D::ResolutionChanged() {
  // LOG->Warn( "RageDisplay_D3D::ResolutionChanged" );

  RageDisplay::ResolutionChanged();
}

int RageDisplay_D3D::GetMaxTextureSize() const {
  return device_caps.MaxTextureWidth;
}

bool RageDisplay_D3D::BeginFrame() {
  GraphicsWindow::Update();

  switch (d3d_device->TestCooperativeLevel()) {
    case D3DERR_DEVICELOST:
      return false;
    case D3DERR_DEVICENOTRESET: {
      bool ignore = false;
      std::string error = SetD3DParams(ignore);
      if (!error.empty()) {
        RageException::Throw(error.c_str());
      }

      break;
    }
  }

  d3d_device->Clear(
      0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0),
      1.0f, 0x00000000);
  d3d_device->BeginScene();

  return RageDisplay::BeginFrame();
}

void RageDisplay_D3D::EndFrame() {
  d3d_device->EndScene();

  FrameLimitBeforeVsync();
  d3d_device->Present(0, 0, 0, 0);
  FrameLimitAfterVsync();

  RageDisplay::EndFrame();
}

bool RageDisplay_D3D::SupportsTextureFormat(
    RagePixelFormat pixfmt, bool realtime) {
  // Some cards (Savage) don't support alpha in palettes.
  // Don't allow paletted textures if this is the case.
  if (pixfmt == RagePixelFormat_PAL &&
      !(device_caps.TextureCaps & D3DPTEXTURECAPS_ALPHAPALETTE)) {
    return false;
  }

  if (d3d_formats[pixfmt] == D3DFMT_UNKNOWN) {
    return false;
  }

  D3DFORMAT d3dfmt = d3d_formats[pixfmt];
  HRESULT hr = d3d->CheckDeviceFormat(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_d3dpp.BackBufferFormat, 0,
      D3DRTYPE_TEXTURE, d3dfmt);

  return SUCCEEDED(hr);
}

RageSurface* RageDisplay_D3D::CreateScreenshot() {
  RageSurface* result = nullptr;

  // get the render target
  IDirect3DSurface9* surface_target;
  if (SUCCEEDED(d3d_device->GetRenderTarget(0, &surface_target))) {
    // get the render target surface description
    D3DSURFACE_DESC desc;
    surface_target->GetDesc(&desc);

    // create an offscreen plain surface of the same format in the SYSTEMMEM
    // pool
    IDirect3DSurface9* copy;
    if (SUCCEEDED(d3d_device->CreateOffscreenPlainSurface(
            desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy,
            nullptr))) {
      // copy the data from the render target into the offscreen plain surface
      if (SUCCEEDED(d3d_device->GetRenderTargetData(surface_target, copy))) {
        // Update desc from the copy.
        copy->GetDesc(&desc);

        D3DLOCKED_RECT lr;

        {
          RECT rect;
          rect.left = 0;
          rect.top = 0;
          rect.right = desc.Width;
          rect.bottom = desc.Height;

          copy->LockRect(&lr, &rect, D3DLOCK_READONLY);
        }

        // since we no longer have an easy function to force a conversion to
        // A8R8G8B8, we need to figure out our pixel format. (yes, we could
        // create a couple of surfaces in the default pool, copy the bits into
        // the one matching our source, then use IDirect3DDevice::StretchRect to
        // convert it without stretching into our desired format. This would
        // mean a copy to device memory and a copy back again, though.) possible
        // formats are found in FindBackBufferType
        RagePixelFormat pf;
        switch (desc.Format) {
          default:
            pf = RagePixelFormat_Invalid;
            FAIL_M("Unknown pixel format");
            break;
          case D3DFMT_X8R8G8B8:
            pf = RagePixelFormat_RGBA8;
            break;
          case D3DFMT_A8R8G8B8:
            pf = RagePixelFormat_RGBA8;
            break;
            // 16-bit formats are not here. Does anybody actually use them?
        }

        RageSurface* surface = CreateSurfaceFromPixfmt(
            pf, lr.pBits, desc.Width, desc.Height, lr.Pitch);
        ASSERT(nullptr != surface);

        // We need to make a copy, since lr.pBits will go away when we call
        // UnlockRect().
        result = CreateSurface(
            surface->w_, surface->h_, surface->format_->bits_per_pixel_,
            surface->format_->rmask_, surface->format_->gmask_,
            surface->format_->bmask_, surface->format_->amask_);
        RageSurfaceUtils::CopySurface(surface, result);
        delete surface;

        copy->UnlockRect();
      }

      copy->Release();
    }

    surface_target->Release();
  }

  return result;
}

ActualVideoModeParams RageDisplay_D3D::GetActualVideoModeParams() const {
  return GraphicsWindow::GetParams();
}

void RageDisplay_D3D::SendCurrentMatrices() {
  RageMatrix m;
  RageMatrixMultiply(&m, GetCentering(), GetProjectionTop());

  // Convert to OpenGL-style "pixel-centered" coords
  RageMatrix m2 = GetCenteringMatrix(-0.5f, -0.5f, 0, 0);
  RageMatrix projection;
  RageMatrixMultiply(&projection, &m2, &m);
  d3d_device->SetTransform(D3DTS_PROJECTION, (D3DMATRIX*)&projection);

  d3d_device->SetTransform(D3DTS_VIEW, (D3DMATRIX*)GetViewTop());
  d3d_device->SetTransform(D3DTS_WORLD, (D3DMATRIX*)GetWorldTop());

  FOREACH_ENUM(TextureUnit, tu) {
    // Optimization opportunity: Turn off texture transform if not using texture
    // coords.
    d3d_device->SetTextureStageState(
        tu, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

    // If no texture is set for this texture unit, don't bother setting it up.
    IDirect3DBaseTexture9* texture = nullptr;
    d3d_device->GetTexture(tu, &texture);
    if (texture == nullptr) {
      continue;
    }
    texture->Release();

    if (sphere_mapping[tu]) {
      static const RageMatrix kTex = RageMatrix(
          0.5f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.5f, -0.5f, 0.0f, 1.0f);
      d3d_device->SetTransform(
          (D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + Enum::to_integral(tu)),
          (D3DMATRIX*)&kTex);

      // Tell D3D to use transformed reflection vectors as texture co-ordinate 0
      // and then transform this coordinate by the specified texture matrix.
      d3d_device->SetTextureStageState(
          tu, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
    } else {
      /* Direct3D is expecting a 3x3 matrix loaded into the 4x4 in order
       * to transform the 2-component texture coordinates. We currently
       * only use translate and scale, and ignore the z component entirely,
       * so convert the texture matrix from 4x4 to 3x3 by dropping z. */

      const RageMatrix& tex1 = *GetTextureTop();
      const RageMatrix tex2 = RageMatrix(
          tex1.m_[0][0], tex1.m_[0][1], tex1.m_[0][3], 0, tex1.m_[1][0],
          tex1.m_[1][1], tex1.m_[1][3], 0, tex1.m_[3][0], tex1.m_[3][1],
          tex1.m_[3][3], 0, 0, 0, 0, 0);
      d3d_device->SetTransform(
          D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0 + Enum::to_integral(tu)),
          (D3DMATRIX*)&tex2);

      d3d_device->SetTextureStageState(
          tu, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);
    }
  }
}

class RageCompiledGeometrySWD3D : public RageCompiledGeometry {
 public:
  void Allocate(const std::vector<msMesh>& meshes) {
    vertex_.resize(std::max<unsigned int>(1u, GetTotalVertices()));
    triangles_.resize(std::max<unsigned int>(1u, GetTotalTriangles()));
  }
  void Change(const std::vector<msMesh>& meshes) {
    for (size_t i = 0; i < meshes.size(); i++) {
      const MeshInfo& mesh_info = mesh_info_[i];
      const msMesh& mesh = meshes[i];
      const std::vector<RageModelVertex>& vertices = mesh.vertices_;
      const std::vector<msTriangle>& triangles = mesh.triangles_;

      for (size_t j = 0; j < vertices.size(); j++) {
        vertex_[mesh_info.vertex_start_ + j] = vertices[j];
      }

      for (size_t j = 0; j < triangles.size(); j++) {
        for (size_t k = 0; k < 3; k++) {
          triangles_[mesh_info.triangle_start_ + j].vertex_indices_[k] =
              (uint16_t)mesh_info.vertex_start_ +
              triangles[j].vertex_indices_[k];
        }
      }
    }
  }
  void Draw(int mesh_index) const {
    const MeshInfo& mesh_info = mesh_info_[mesh_index];

    if (mesh_info.needs_texture_matrix_scale_) {
      // Kill the texture translation.
      // XXX: Change me to scale the translation by the TextureTranslationScale
      // of the first vertex.
      RageMatrix m;
      d3d_device->GetTransform(D3DTS_TEXTURE0, (D3DMATRIX*)&m);

      m.m_[2][0] = 0;
      m.m_[2][1] = 0;

      d3d_device->SetTransform(D3DTS_TEXTURE0, (D3DMATRIX*)&m);
    }

    d3d_device->SetFVF(D3DFVF_RageModelVertex);
    d3d_device->DrawIndexedPrimitiveUP(
        D3DPT_TRIANGLELIST,                          // PrimitiveType
        mesh_info.vertex_start_,                     // MinIndex
        mesh_info.vertex_count_,                     // NumVertices
        mesh_info.triangle_count_,                   // PrimitiveCount,
        &triangles_[0] + mesh_info.triangle_start_,  // pIndexData,
        D3DFMT_INDEX16,                              // IndexDataFormat,
        &vertex_[0],                                 // pVertexStreamZeroData,
        sizeof(vertex_[0])                           // VertexStreamZeroStride
    );
  }

 protected:
  std::vector<RageModelVertex> vertex_;
  std::vector<msTriangle> triangles_;
};

RageCompiledGeometry* RageDisplay_D3D::CreateCompiledGeometry() {
  return new RageCompiledGeometrySWD3D;
}

void RageDisplay_D3D::DeleteCompiledGeometry(RageCompiledGeometry* p) {
  delete p;
}

void RageDisplay_D3D::DrawQuadsInternal(
    const RageSpriteVertex v[], int num_verts) {
  // there isn't a quad primitive in D3D, so we have to fake it with indexed
  // triangles
  int num_quads = num_verts / 4;
  int num_triangles = num_quads * 2;
  int num_indices = num_triangles * 3;

  // make a temporary index buffer
  static std::vector<uint16_t> indices;
  size_t old_size = indices.size();
  size_t new_size = std::max(old_size, static_cast<size_t>(num_indices));
  indices.resize(new_size);
  for (uint16_t i = (uint16_t)old_size / 6; i < (uint16_t)num_quads; i++) {
    indices[i * 6 + 0] = i * 4 + 0;
    indices[i * 6 + 1] = i * 4 + 1;
    indices[i * 6 + 2] = i * 4 + 2;
    indices[i * 6 + 3] = i * 4 + 2;
    indices[i * 6 + 4] = i * 4 + 3;
    indices[i * 6 + 5] = i * 4 + 0;
  }

  d3d_device->SetFVF(D3DFVF_RageSpriteVertex);
  SendCurrentMatrices();
  d3d_device->DrawIndexedPrimitiveUP(
      D3DPT_TRIANGLELIST,       // PrimitiveType
      0,                        // MinIndex
      num_verts,                // NumVertices
      num_triangles,            // PrimitiveCount,
      &indices[0],              // pIndexData,
      D3DFMT_INDEX16,           // IndexDataFormat,
      v,                        // pVertexStreamZeroData,
      sizeof(RageSpriteVertex)  // VertexStreamZeroStride
  );
}

void RageDisplay_D3D::DrawQuadStripInternal(
    const RageSpriteVertex v[], int num_verts) {
  // there isn't a quad strip primitive in D3D, so we have to fake it with
  // indexed triangles
  int num_quads = (num_verts - 2) / 2;
  int num_triangles = num_quads * 2;
  int num_indices = num_triangles * 3;

  // make a temporary index buffer
  static std::vector<uint16_t> indices;
  size_t old_size = indices.size();
  size_t new_size = std::max(old_size, static_cast<size_t>(num_indices));
  indices.resize(new_size);
  for (uint16_t i = (uint16_t)old_size / 6; i < (uint16_t)num_quads; i++) {
    indices[i * 6 + 0] = i * 2 + 0;
    indices[i * 6 + 1] = i * 2 + 1;
    indices[i * 6 + 2] = i * 2 + 2;
    indices[i * 6 + 3] = i * 2 + 1;
    indices[i * 6 + 4] = i * 2 + 2;
    indices[i * 6 + 5] = i * 2 + 3;
  }

  d3d_device->SetFVF(D3DFVF_RageSpriteVertex);
  SendCurrentMatrices();
  d3d_device->DrawIndexedPrimitiveUP(
      D3DPT_TRIANGLELIST,       // PrimitiveType
      0,                        // MinIndex
      num_verts,                // NumVertices
      num_triangles,            // PrimitiveCount,
      &indices[0],              // pIndexData,
      D3DFMT_INDEX16,           // IndexDataFormat,
      v,                        // pVertexStreamZeroData,
      sizeof(RageSpriteVertex)  // VertexStreamZeroStride
  );
}

void RageDisplay_D3D::DrawSymmetricQuadStripInternal(
    const RageSpriteVertex v[], int num_verts) {
  int num_pieces = (num_verts - 3) / 3;
  int num_triangles = num_pieces * 4;
  int num_indices = num_triangles * 3;

  // make a temporary index buffer
  static std::vector<uint16_t> indices;
  size_t old_size = indices.size();
  size_t new_size = std::max(old_size, static_cast<size_t>(num_indices));
  indices.resize(new_size);
  for (uint16_t i = (uint16_t)old_size / 12; i < (uint16_t)num_pieces; i++) {
    // { 1, 3, 0 } { 1, 4, 3 } { 1, 5, 4 } { 1, 2, 5 }
    indices[i * 12 + 0] = i * 3 + 1;
    indices[i * 12 + 1] = i * 3 + 3;
    indices[i * 12 + 2] = i * 3 + 0;
    indices[i * 12 + 3] = i * 3 + 1;
    indices[i * 12 + 4] = i * 3 + 4;
    indices[i * 12 + 5] = i * 3 + 3;
    indices[i * 12 + 6] = i * 3 + 1;
    indices[i * 12 + 7] = i * 3 + 5;
    indices[i * 12 + 8] = i * 3 + 4;
    indices[i * 12 + 9] = i * 3 + 1;
    indices[i * 12 + 10] = i * 3 + 2;
    indices[i * 12 + 11] = i * 3 + 5;
  }

  d3d_device->SetFVF(D3DFVF_RageSpriteVertex);
  SendCurrentMatrices();
  d3d_device->DrawIndexedPrimitiveUP(
      D3DPT_TRIANGLELIST,       // PrimitiveType
      0,                        // MinIndex
      num_verts,                // NumVertices
      num_triangles,            // PrimitiveCount,
      &indices[0],              // pIndexData,
      D3DFMT_INDEX16,           // IndexDataFormat,
      v,                        // pVertexStreamZeroData,
      sizeof(RageSpriteVertex)  // VertexStreamZeroStride
  );
}

void RageDisplay_D3D::DrawFanInternal(
    const RageSpriteVertex v[], int num_verts) {
  d3d_device->SetFVF(D3DFVF_RageSpriteVertex);
  SendCurrentMatrices();
  d3d_device->DrawPrimitiveUP(
      D3DPT_TRIANGLEFAN,  // PrimitiveType
      num_verts - 2,      // PrimitiveCount,
      v,                  // pVertexStreamZeroData,
      sizeof(RageSpriteVertex));
}

void RageDisplay_D3D::DrawStripInternal(
    const RageSpriteVertex v[], int num_verts) {
  d3d_device->SetFVF(D3DFVF_RageSpriteVertex);
  SendCurrentMatrices();
  d3d_device->DrawPrimitiveUP(
      D3DPT_TRIANGLESTRIP,  // PrimitiveType
      num_verts - 2,        // PrimitiveCount,
      v,                    // pVertexStreamZeroData,
      sizeof(RageSpriteVertex));
}

void RageDisplay_D3D::DrawTrianglesInternal(
    const RageSpriteVertex v[], int num_verts) {
  d3d_device->SetFVF(D3DFVF_RageSpriteVertex);
  SendCurrentMatrices();
  d3d_device->DrawPrimitiveUP(
      D3DPT_TRIANGLELIST,  // PrimitiveType
      num_verts / 3,       // PrimitiveCount,
      v,                   // pVertexStreamZeroData,
      sizeof(RageSpriteVertex));
}

void RageDisplay_D3D::DrawCompiledGeometryInternal(
    const RageCompiledGeometry* p, int mesh_index) {
  SendCurrentMatrices();

  /* If lighting is off, then the current material will have no effect. We
   * want to still be able to color models with lighting off, so shove the
   * material color in texture factor and modify the texture stage to use it
   * instead of the vertex color (our models don't have vertex coloring anyway).
   */
  DWORD lighting;
  d3d_device->GetRenderState(D3DRS_LIGHTING, &lighting);

  if (!lighting) {
    d3d_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
    d3d_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
  }

  p->Draw(mesh_index);

  if (!lighting) {
    d3d_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    d3d_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
  }
}

/* Use the default poly-based implementation.  D3D lines apparently don't
 * support AA with greater-than-one widths. */
/*
void RageDisplay_D3D::DrawLineStrip( const RageSpriteVertex v[], int iNumVerts,
float LineWidth )
{
        ASSERT( iNumVerts >= 2 );
        g_pd3dDevice->SetRenderState( D3DRS_POINTSIZE, *((DWORD*)&LineWidth) );
// funky cast.  See D3DRENDERSTATETYPE doc g_pd3dDevice->SetVertexShader(
D3DFVF_RageSpriteVertex ); SendCurrentMatrices(); g_pd3dDevice->DrawPrimitiveUP(
                D3DPT_LINESTRIP, // PrimitiveType
                iNumVerts-1, // PrimitiveCount,
                v, // pVertexStreamZeroData,
                sizeof(RageSpriteVertex)
        );
        StatsAddVerts( iNumVerts );
}
*/

void RageDisplay_D3D::ClearAllTextures() {
  FOREACH_ENUM(TextureUnit, i)
  SetTexture(i, 0);
}

int RageDisplay_D3D::GetNumTextureUnits() {
  return device_caps.MaxSimultaneousTextures;
}

void RageDisplay_D3D::SetTexture(TextureUnit tu, uintptr_t texture) {
  //	g_DeviceCaps.MaxSimultaneousTextures = 1;
  if (tu >= (int)device_caps.MaxSimultaneousTextures) {  // not supported
    return;
  }

  if (texture == 0) {
    d3d_device->SetTexture(tu, nullptr);

    /* Intentionally commented out. Don't mess with texture stage state
     * when just setting the texture. Model sets its texture modes before
     * setting the final texture. */
    // g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP, D3DTOP_DISABLE );
  } else {
    IDirect3DTexture9* tex = reinterpret_cast<IDirect3DTexture9*>(texture);
    d3d_device->SetTexture(tu, tex);

    /* Intentionally commented out. Don't mess with texture stage state
     * when just setting the texture. Model sets its texture modes before
     * setting the final texture. */
    // g_pd3dDevice->SetTextureStageState( tu, D3DTSS_COLOROP, D3DTOP_MODULATE
    // );

    // Set palette (if any)
    SetPalette(texture);
  }
}

void RageDisplay_D3D::SetTextureMode(TextureUnit tu, TextureMode tm) {
  if (tu >= (int)device_caps.MaxSimultaneousTextures) {  // not supported
    return;
  }

  switch (tm) {
    case TextureMode_Modulate:
      // Use D3DTA_CURRENT instead of diffuse so that multitexturing works
      // properly.  For stage 0, D3DTA_CURRENT is the diffuse color.

      d3d_device->SetTextureStageState(tu, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      d3d_device->SetTextureStageState(tu, D3DTSS_COLORARG2, D3DTA_CURRENT);
      d3d_device->SetTextureStageState(tu, D3DTSS_COLOROP, D3DTOP_MODULATE);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
      break;
    case TextureMode_Add:
      d3d_device->SetTextureStageState(tu, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      d3d_device->SetTextureStageState(tu, D3DTSS_COLORARG2, D3DTA_CURRENT);
      d3d_device->SetTextureStageState(tu, D3DTSS_COLOROP, D3DTOP_ADD);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
      break;
    case TextureMode_Glow:
      d3d_device->SetTextureStageState(tu, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      d3d_device->SetTextureStageState(tu, D3DTSS_COLORARG2, D3DTA_CURRENT);
      d3d_device->SetTextureStageState(tu, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
      d3d_device->SetTextureStageState(tu, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
      break;
  }
}

void RageDisplay_D3D::SetTextureFiltering(TextureUnit tu, bool b) {
  if (tu >= (int)device_caps.MaxSimultaneousTextures) {  // not supported
    return;
  }

  d3d_device->SetSamplerState(
      tu, D3DSAMP_MINFILTER, b ? D3DTEXF_LINEAR : D3DTEXF_POINT);
  d3d_device->SetSamplerState(
      tu, D3DSAMP_MAGFILTER, b ? D3DTEXF_LINEAR : D3DTEXF_POINT);
}

void RageDisplay_D3D::SetBlendMode(BlendMode mode) {
  d3d_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);

  if (mode == BLEND_INVERT_DEST) {
    d3d_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_SUBTRACT);
  } else {
    d3d_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  }

  switch (mode) {
    case BLEND_NORMAL:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
      break;
    case BLEND_ADD:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      break;
    // This is not the right way to do BLEND_SUBTRACT.  This code is only here
    // to prevent crashing when someone tries to use it. -Kyz
    case BLEND_SUBTRACT:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
      break;
    case BLEND_MODULATE:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
      break;
    case BLEND_COPY_SRC:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
      break;
    /* Effects currently missing in D3D: BLEND_ALPHA_MASK, BLEND_ALPHA_KNOCK_OUT
     * These two may require DirectX9 since D3DRS_SRCALPHA and D3DRS_DESTALPHA
     * don't seem to exist in DX8. -aj */
    case BLEND_ALPHA_MASK:
      // RGB: iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      // Alpha: iSourceAlpha = GL_ZERO; iDestAlpha = GL_SRC_ALPHA;
      /*
      g_pd3dDevice->SetRenderState( D3DRS_SRCALPHA,  D3DBLEND_ZERO );
      g_pd3dDevice->SetRenderState( D3DRS_DESTALPHA, D3DBLEND_SRCALPHA );
      */
      break;
    case BLEND_ALPHA_KNOCK_OUT:
      // RGB: iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      // Alpha: iSourceAlpha = GL_ZERO; iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
      /*
      g_pd3dDevice->SetRenderState( D3DRS_SRCBLENDALPHA,  D3DBLEND_ZERO );
      g_pd3dDevice->SetRenderState( D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA
      );
      */
      break;
    case BLEND_ALPHA_MULTIPLY:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
      break;
    case BLEND_WEIGHTED_MULTIPLY:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
      break;
    case BLEND_INVERT_DEST:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      break;
    case BLEND_NO_EFFECT:
      d3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
      d3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      break;
    default:
      FAIL_M(ssprintf("Invalid BlendMode: %i", mode));
  }
}

bool RageDisplay_D3D::IsZWriteEnabled() const {
  DWORD b;
  d3d_device->GetRenderState(D3DRS_ZWRITEENABLE, &b);
  return b != 0;
}

void RageDisplay_D3D::SetZBias(float f) {
  D3DVIEWPORT9 view_data;
  d3d_device->GetViewport(&view_data);
  view_data.MinZ = SCALE(f, 0.0f, 1.0f, 0.05f, 0.0f);
  view_data.MaxZ = SCALE(f, 0.0f, 1.0f, 1.0f, 0.95f);
  d3d_device->SetViewport(&view_data);
}

void RageDisplay_D3D::SetZWrite(bool b) {
  d3d_device->SetRenderState(D3DRS_ZWRITEENABLE, b);
}

void RageDisplay_D3D::SetZTestMode(ZTestMode mode) {
  d3d_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
  DWORD dw;
  switch (mode) {
    case ZTEST_OFF:
      dw = D3DCMP_ALWAYS;
      break;
    case ZTEST_WRITE_ON_PASS:
      dw = D3DCMP_LESSEQUAL;
      break;
    case ZTEST_WRITE_ON_FAIL:
      dw = D3DCMP_GREATER;
      break;
    default:
      dw = D3DCMP_NEVER;
      FAIL_M(ssprintf("Invalid ZTestMode: %i", mode));
  }
  d3d_device->SetRenderState(D3DRS_ZFUNC, dw);
}

void RageDisplay_D3D::ClearZBuffer() {
  d3d_device->Clear(
      0, nullptr, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0x00000000);
}

void RageDisplay_D3D::SetTextureWrapping(TextureUnit tu, bool b) {
  if (tu >= (int)device_caps.MaxSimultaneousTextures) {  // not supported
    return;
  }

  int mode = b ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
  d3d_device->SetSamplerState(tu, D3DSAMP_ADDRESSU, mode);
  d3d_device->SetSamplerState(tu, D3DSAMP_ADDRESSV, mode);
}

void RageDisplay_D3D::SetMaterial(
    const RageColor& emissive, const RageColor& ambient,
    const RageColor& diffuse, const RageColor& specular, float shininess) {
  /* If lighting is off, then the current material will have no effect.
   * We want to still be able to color models with lighting off, so shove the
   * material color in texture factor and modify the texture stage to use it
   * instead of the vertex color (our models don't have vertex coloring anyway).
   */
  DWORD lighting;
  d3d_device->GetRenderState(D3DRS_LIGHTING, &lighting);

  if (lighting) {
    D3DMATERIAL9 mat;
    memcpy(&mat.Diffuse, diffuse, sizeof(float) * 4);
    memcpy(&mat.Ambient, ambient, sizeof(float) * 4);
    memcpy(&mat.Specular, specular, sizeof(float) * 4);
    memcpy(&mat.Emissive, emissive, sizeof(float) * 4);
    mat.Power = shininess;
    d3d_device->SetMaterial(&mat);
  } else {
    RageColor c = diffuse;
    c.r_ += emissive.r_ + ambient.r_;
    c.g_ += emissive.g_ + ambient.g_;
    c.b_ += emissive.b_ + ambient.b_;
    RageVColor c2 = c;
    DWORD c3 = *(DWORD*)&c2;
    d3d_device->SetRenderState(D3DRS_TEXTUREFACTOR, c3);
  }
}

void RageDisplay_D3D::SetLighting(bool b) {
  d3d_device->SetRenderState(D3DRS_LIGHTING, b);
}

void RageDisplay_D3D::SetLightOff(int index) {
  d3d_device->LightEnable(index, false);
}
void RageDisplay_D3D::SetLightDirectional(
    int index, const RageColor& ambient, const RageColor& diffuse,
    const RageColor& specular, const RageVector3& dir) {
  d3d_device->LightEnable(index, true);

  D3DLIGHT9 light;
  ZERO(light);
  light.Type = D3DLIGHT_DIRECTIONAL;

  /* Z for lighting is flipped for D3D compared to OpenGL.
   * XXX: figure out exactly why this is needed. Our transforms are probably
   * goofed up, but the Z test is the same for both API's, so I'm not sure
   * why we don't see other weirdness. -Chris */
  float position[] = {dir.x_, dir.y_, -dir.z_};
  memcpy(&light.Direction, position, sizeof(position));
  memcpy(&light.Diffuse, diffuse, sizeof(diffuse));
  memcpy(&light.Ambient, ambient, sizeof(ambient));
  memcpy(&light.Specular, specular, sizeof(specular));

  // Same as OpenGL defaults.  Not used in directional lights.
  //	light.Attenuation0 = 1;
  //	light.Attenuation1 = 0;
  //	light.Attenuation2 = 0;

  d3d_device->SetLight(index, &light);
}

void RageDisplay_D3D::SetCullMode(CullMode mode) {
  switch (mode) {
    case CULL_BACK:
      d3d_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
      break;
    case CULL_FRONT:
      d3d_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
      break;
    case CULL_NONE:
      d3d_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
      break;
    default:
      FAIL_M(ssprintf("Invalid CullMode: %i", mode));
  }
}

void RageDisplay_D3D::DeleteTexture(uintptr_t tex_handle) {
  if (tex_handle == 0) {
    return;
  }

  IDirect3DTexture9* tex = reinterpret_cast<IDirect3DTexture9*>(tex_handle);
  tex->Release();

  // Delete palette (if any)
  if (tex_resource_to_palette_index.find(tex_handle) !=
      tex_resource_to_palette_index.end()) {
    tex_resource_to_palette_index.erase(
        tex_resource_to_palette_index.find(tex_handle));
  }
  if (tex_resource_to_texture_palette.find(tex_handle) !=
      tex_resource_to_texture_palette.end()) {
    tex_resource_to_texture_palette.erase(
        tex_resource_to_texture_palette.find(tex_handle));
  }
}

uintptr_t RageDisplay_D3D::CreateTexture(
    RagePixelFormat pixfmt, RageSurface* img, bool generate_mip_maps) {
  HRESULT hr;
  IDirect3DTexture9* tex;
  hr = d3d_device->CreateTexture(
      power_of_two(img->w_), power_of_two(img->h_), 1, 0, d3d_formats[pixfmt],
      D3DPOOL_MANAGED, &tex, nullptr);

  if (FAILED(hr)) {
    RageException::Throw(
        "CreateTexture(%i,%i,%s) failed: %s", img->w_, img->h_,
        RagePixelFormatToString(pixfmt).c_str(), GetErrorString(hr).c_str());
  }

  uintptr_t tex_handle = reinterpret_cast<uintptr_t>(tex);

  if (pixfmt == RagePixelFormat_PAL) {
    // Save palette
    TexturePalette pal;
    memset(pal.p_, 0, sizeof(pal.p_));
    for (int i = 0; i < img->format_->palette_->ncolors_; i++) {
      RageSurfaceColor& c = img->format_->palette_->colors_[i];
      pal.p_[i].peRed = c.r_;
      pal.p_[i].peGreen = c.g_;
      pal.p_[i].peBlue = c.b_;
      pal.p_[i].peFlags = c.a_;
    }

    ASSERT(
        tex_resource_to_texture_palette.find(tex_handle) ==
        tex_resource_to_texture_palette.end());
    tex_resource_to_texture_palette[tex_handle] = pal;
  }

  UpdateTexture(tex_handle, img, 0, 0, img->w_, img->h_);

  return tex_handle;
}

void RageDisplay_D3D::UpdateTexture(
    uintptr_t tex_handle, RageSurface* img, int xoffset, int yoffset, int width,
    int height) {
  IDirect3DTexture9* tex = reinterpret_cast<IDirect3DTexture9*>(tex_handle);
  ASSERT(tex != nullptr);

  RECT rect;
  rect.left = xoffset;
  rect.top = yoffset;
  rect.right = width - xoffset;
  rect.bottom = height - yoffset;

  D3DLOCKED_RECT lr;
  tex->LockRect(0, &lr, &rect, 0);

  D3DSURFACE_DESC desc;
  tex->GetLevelDesc(0, &desc);
  ASSERT(xoffset + width <= int(desc.Width));
  ASSERT(yoffset + height <= int(desc.Height));

  // Copy bits
  int texpixfmt;
  for (texpixfmt = 0; texpixfmt < NUM_RagePixelFormat; ++texpixfmt) {
    if (d3d_formats[texpixfmt] == desc.Format) {
      break;
    }
  }
  ASSERT(texpixfmt != NUM_RagePixelFormat);

  RageSurface* texture = CreateSurfaceFromPixfmt(
      RagePixelFormat(texpixfmt), lr.pBits, width, height, lr.Pitch);
  ASSERT(texture != nullptr);
  RageSurfaceUtils::Blit(img, texture, width, height);

  delete texture;

  tex->UnlockRect(0);
}

void RageDisplay_D3D::SetAlphaTest(bool b) {
  d3d_device->SetRenderState(D3DRS_ALPHATESTENABLE, b);
  d3d_device->SetRenderState(D3DRS_ALPHAREF, 0);
  d3d_device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
}

RageMatrix RageDisplay_D3D::GetOrthoMatrix(
    float l, float r, float b, float t, float zn, float zf) {
  RageMatrix m = RageDisplay::GetOrthoMatrix(l, r, b, t, zn, zf);

  // Convert from OpenGL's [-1,+1] Z values to D3D's [0,+1].
  RageMatrix tmp;
  RageMatrixScaling(&tmp, 1, 1, 0.5f);
  RageMatrixMultiply(&m, &tmp, &m);

  RageMatrixTranslation(&tmp, 0, 0, 0.5f);
  RageMatrixMultiply(&m, &tmp, &m);

  return m;
}

void RageDisplay_D3D::SetSphereEnvironmentMapping(TextureUnit tu, bool b) {
  sphere_mapping[tu] = b;
}

void RageDisplay_D3D::SetCelShaded(int stage) {
  // todo: implement me!
}

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
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
