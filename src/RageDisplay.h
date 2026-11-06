/* RageDisplay - Renderer base class. */

#ifndef RAGEDISPLAY_H
#define RAGEDISPLAY_H

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "ModelTypes.h"
#include "RageTypes.h"

class DisplaySpec;
typedef std::set<DisplaySpec> DisplaySpecs;

const int REFRESH_DEFAULT = 0;
struct RageSurface;
enum TextureUnit {
  TextureUnit_1,
  TextureUnit_2,
  TextureUnit_3,
  TextureUnit_4,
  NUM_TextureUnit
};

// RageCompiledGeometry holds vertex data in a format that is most efficient
// for the graphics API.
class RageCompiledGeometry {
 public:
  virtual ~RageCompiledGeometry();

  void Set(const std::vector<msMesh>& vMeshes);

  virtual void Allocate(
      const std::vector<msMesh>& vMeshes) = 0;  // allocate space
  virtual void Change(
      const std::vector<msMesh>& vMeshes) = 0;  // new data must be the same
                                                // size as was passed to Set()
  virtual void Draw(int iMeshIndex) const = 0;

 protected:
  size_t GetTotalVertices() const {
    if (mesh_info_.empty()) {
      return 0;
    }
    return mesh_info_.back().vertex_start_ + mesh_info_.back().vertex_count_;
  }
  size_t GetTotalTriangles() const {
    if (mesh_info_.empty()) {
      return 0;
    }
    return mesh_info_.back().triangle_start_ +
           mesh_info_.back().triangle_count_;
  }

  struct MeshInfo {
    int vertex_start_;
    int vertex_count_;
    int triangle_start_;
    int triangle_count_;
    bool needs_texture_matrix_scale_;
  };
  std::vector<MeshInfo> mesh_info_;
  bool any_needs_texture_matrix_scale_;
};

enum RagePixelFormat {
  RagePixelFormat_RGBA8,
  RagePixelFormat_BGRA8,
  RagePixelFormat_RGBA4,
  RagePixelFormat_RGB5A1,
  RagePixelFormat_RGB5,
  RagePixelFormat_RGB8,
  RagePixelFormat_PAL,
  /* The above formats differ between OpenGL and D3D. These are provided as
   * alternatives for OpenGL that match some format in D3D.  Don't use them
   * directly; they'll be matched automatically by FindPixelFormat. */
  RagePixelFormat_BGR8,
  RagePixelFormat_A1BGR5,
  RagePixelFormat_X1RGB5,
  NUM_RagePixelFormat,
  RagePixelFormat_Invalid
};
const std::string& RagePixelFormatToString(RagePixelFormat i);

/** @brief The parameters used for the present Video Mode. */
class VideoModeParams {
 public:
  // Initialize with a constructor so to guarantee all paramters
  // are filled (in case new params are added).
  VideoModeParams(
      bool windowed_, std::string sDisplayId_, int width_, int height_,
      int bpp_, int rate_, bool vsync_, bool interlaced_, bool bSmoothLines_,
      bool bTrilinearFiltering_, bool bAnisotropicFiltering_,
      bool bWindowIsFullscreenBorderless_, std::string sWindowTitle_,
      std::string sIconFile_, bool PAL_, float fDisplayAspectRatio_)
      : windowed_(windowed_),
        display_id_(sDisplayId_),
        width_(width_),
        height_(height_),
        bpp_(bpp_),
        rate_(rate_),
        vsync_(vsync_),
        interlaced_(interlaced_),
        smooth_lines_(bSmoothLines_),
        trilinear_filtering_(bTrilinearFiltering_),
        anisotropic_filtering_(bAnisotropicFiltering_),
        window_is_fullscreen_borderless_(bWindowIsFullscreenBorderless_),
        window_title_(sWindowTitle_),
        icon_file_(sIconFile_),
        pal_(PAL_),
        display_aspect_ratio_(fDisplayAspectRatio_) {}

  VideoModeParams(const VideoModeParams& other) = default;

  VideoModeParams()
      : windowed_(false),
        width_(0),
        height_(0),
        bpp_(0),
        rate_(0),
        vsync_(false),
        interlaced_(false),
        smooth_lines_(false),
        trilinear_filtering_(false),
        anisotropic_filtering_(false),
        window_is_fullscreen_borderless_(false),
        window_title_(std::string()),
        icon_file_(std::string()),
        pal_(false),
        display_aspect_ratio_(0.0f) {}

  // Subclassing VideoModeParams in ActualVideoModeParams. Make destructor
  // virtual just in case someone tries to delete one of those through a pointer
  // to base...
  virtual ~VideoModeParams() = default;

  bool windowed_;
  std::string display_id_;
  int width_;
  int height_;
  int bpp_;
  int rate_;
  bool vsync_;
  bool interlaced_;
  bool smooth_lines_;
  bool trilinear_filtering_;
  bool anisotropic_filtering_;
  bool window_is_fullscreen_borderless_;
  std::string window_title_;
  std::string icon_file_;
  bool pal_;
  float display_aspect_ratio_;
};

/**
 * @brief The _actual_ VideoModeParams determined by the LowLevelWindow
 * implementation. Contains all the attributes of VideoModeParams, plus the
 * actual window width/height determined by LLW
 */
class ActualVideoModeParams : public VideoModeParams {
 public:
  ActualVideoModeParams()
      : VideoModeParams(),
        window_width_(0),
        window_height_(0),
        render_offscreen_(false) {}
  ActualVideoModeParams(const VideoModeParams& params)
      : VideoModeParams(params),
        window_width_(params.width_),
        window_height_(params.height_),
        render_offscreen_(false) {}
  ActualVideoModeParams(
      const VideoModeParams& params, int windowWidth, int windowHeight,
      bool renderOffscreen)
      : VideoModeParams(params),
        window_width_(windowWidth),
        window_height_(windowHeight),
        render_offscreen_(renderOffscreen) {}
  ActualVideoModeParams(const ActualVideoModeParams& other) = default;
  ActualVideoModeParams& operator=(const ActualVideoModeParams& other) =
      default;

  // If bWindowIsFullscreenBorderless is true,
  // then these properties will differ from width/height (which describe the
  // render size)
  int window_width_;
  int window_height_;
  bool render_offscreen_;
};

struct RenderTargetParam {
  RenderTargetParam()
      : width_(0),
        height_(0),
        with_depth_buffer_(false),
        with_alpha_(false),
        float_(false) {}

  // The dimensions of the actual render target, analogous to a window size:
  int width_, height_;

  bool with_depth_buffer_;
  bool with_alpha_;
  bool float_;
};

struct RageTextureLock {
  virtual ~RageTextureLock() {}

  /* Given a surface with a format and no pixel data, lock the texture into the
   * surface. The data is write-only. */
  virtual void Lock(uintptr_t iTexHandle, RageSurface* pSurface) = 0;

  /* Unlock and update the texture. If bChanged is false, the texture update
   * may be omitted. */
  virtual void Unlock(RageSurface* pSurface, bool bChanged = true) = 0;
};

class RageDisplay {
  friend class RageTexture;

 public:
  struct RagePixelFormatDesc {
    int bpp_;
    unsigned int masks_[4];
  };

  virtual const RagePixelFormatDesc* GetPixelFormatDesc(
      RagePixelFormat pf) const = 0;

  RageDisplay();
  virtual ~RageDisplay();

  virtual std::string Init(
      const VideoModeParams& p, bool bAllowUnacceleratedRenderer) = 0;

  virtual std::string GetApiDescription() const = 0;
  virtual void GetDisplaySpecs(DisplaySpecs& out) const = 0;

  // Don't override this.  Override TryVideoMode() instead.
  // This will set the video mode to be as close as possible to params.
  // Return true if device was re-created and we need to reload textures.
  std::string SetVideoMode(VideoModeParams p, bool& bNeedReloadTextures);

  // Call this when the resolution has been changed externally:
  virtual void ResolutionChanged();

  virtual bool BeginFrame();
  virtual void EndFrame();
  virtual ActualVideoModeParams GetActualVideoModeParams() const = 0;

  virtual void SetBlendMode(BlendMode mode) = 0;

  virtual bool SupportsTextureFormat(
      RagePixelFormat pixfmt, bool realtime = false) = 0;
  virtual bool SupportsPerVertexMatrixScale() = 0;

  /* return 0 if failed or internal texture resource handle
   * (unsigned in OpenGL, texture pointer in D3D) */
  virtual uintptr_t CreateTexture(
      RagePixelFormat pixfmt,  // format of img and of texture in video mem
      RageSurface* img,        // must be in pixfmt
      bool bGenerateMipMaps) = 0;
  virtual void UpdateTexture(
      uintptr_t iTexHandle, RageSurface* img, int xoffset, int yoffset,
      int width, int height) = 0;
  virtual void DeleteTexture(uintptr_t iTexHandle) = 0;
  /* Return an object to lock pixels for streaming. If not supported, returns
   * nullptr. Delete the object normally. */
  virtual RageTextureLock* CreateTextureLock() { return nullptr; }
  virtual void ClearAllTextures() = 0;
  virtual int GetNumTextureUnits() = 0;
  virtual void SetTexture(TextureUnit, uintptr_t /* iTexture */) = 0;
  virtual void SetTextureMode(TextureUnit, TextureMode) = 0;
  virtual void SetTextureWrapping(TextureUnit, bool) = 0;
  virtual int GetMaxTextureSize() const = 0;
  virtual void SetTextureFiltering(TextureUnit, bool) = 0;
  virtual void SetEffectMode(EffectMode) {}
  virtual bool IsEffectModeSupported(EffectMode effect) {
    return effect == EffectMode_Normal;
  }

  virtual bool SupportsRenderToTexture() const { return false; }
  virtual bool SupportsFullscreenBorderlessWindow() const { return false; }

  /* Create a render target, returning a texture handle. In addition to normal
   * texture functions, this can be passed to SetRenderTarget. Delete with
   * DeleteTexture. (UpdateTexture is not permitted.) Returns 0 if render-to-
   * texture is unsupported.
   */
  virtual uintptr_t CreateRenderTarget(
      const RenderTargetParam&, int& /* iTextureWidthOut */,
      int& /* iTextureHeightOut */) {
    return 0;
  }

  virtual uintptr_t GetRenderTarget() { return 0; }

  /* Set the render target, or 0 to resume rendering to the framebuffer. An
   * active render target may not be used as a texture. If bPreserveTexture is
   * true, the contents of the texture will be preserved from the previous call;
   * otherwise, cleared.  If bPreserveTexture is true the first time a render
   * target is used, behave as if bPreserveTexture was false.
   */
  virtual void SetRenderTarget(
      uintptr_t /* iHandle */, bool /* bPreserveTexture */ = true) {}

  virtual bool IsZWriteEnabled() const = 0;
  virtual void SetZWrite(bool) = 0;
  virtual void SetZTestMode(ZTestMode) = 0;
  virtual void SetZBias(float) = 0;
  virtual void ClearZBuffer() = 0;

  virtual void SetCullMode(CullMode mode) = 0;

  virtual void SetAlphaTest(bool b) = 0;

  virtual void SetMaterial(
      const RageColor& emissive, const RageColor& ambient,
      const RageColor& diffuse, const RageColor& specular, float shininess) = 0;

  virtual void SetLighting(bool b) = 0;
  virtual void SetLightOff(int index) = 0;
  virtual void SetLightDirectional(
      int index, const RageColor& ambient, const RageColor& diffuse,
      const RageColor& specular, const RageVector3& dir) = 0;

  virtual void SetSphereEnvironmentMapping(TextureUnit tu, bool b) = 0;
  virtual void SetCelShaded(int stage) = 0;

  virtual RageCompiledGeometry* CreateCompiledGeometry() = 0;
  virtual void DeleteCompiledGeometry(RageCompiledGeometry* p) = 0;

  void DrawQuads(const RageSpriteVertex v[], int iNumVerts);
  void DrawQuadStrip(const RageSpriteVertex v[], int iNumVerts);
  void DrawFan(const RageSpriteVertex v[], int iNumVerts);
  void DrawStrip(const RageSpriteVertex v[], int iNumVerts);
  void DrawTriangles(const RageSpriteVertex v[], int iNumVerts);
  void DrawCompiledGeometry(
      const RageCompiledGeometry* p, int iMeshIndex,
      const std::vector<msMesh>& vMeshes);
  void DrawLineStrip(
      const RageSpriteVertex v[], int iNumVerts, float LineWidth);
  void DrawSymmetricQuadStrip(const RageSpriteVertex v[], int iNumVerts);
  void DrawCircle(const RageSpriteVertex& v, float radius);

  void DrawQuad(const RageSpriteVertex v[]) {
    DrawQuads(v, 4);
  } /* alias. upper-left, upper-right, lower-left, lower-right */

  // hacks for cell-shaded models
  virtual void SetLineWidth(float) {}

  enum GraphicsFileFormat {
    SAVE_LOSSLESS,           // bmp
    SAVE_LOSSLESS_SENSIBLE,  // png
    SAVE_LOSSY_LOW_QUAL,     // jpg
    SAVE_LOSSY_HIGH_QUAL     // jpg
  };
  bool SaveScreenshot(const std::string& sPath, GraphicsFileFormat format);

  virtual RageSurface*
  CreateScreenshot() = 0;  // allocates a surface.  Caller must delete it.
  virtual RageSurface* GetTexture(uintptr_t /* iTexture */) {
    return nullptr;
  }  // allocates a surface.  Caller must delete it.

 protected:
  virtual void DrawQuadsInternal(const RageSpriteVertex v[], int iNumVerts) = 0;
  virtual void DrawQuadStripInternal(
      const RageSpriteVertex v[], int iNumVerts) = 0;
  virtual void DrawFanInternal(const RageSpriteVertex v[], int iNumVerts) = 0;
  virtual void DrawStripInternal(const RageSpriteVertex v[], int iNumVerts) = 0;
  virtual void DrawTrianglesInternal(
      const RageSpriteVertex v[], int iNumVerts) = 0;
  virtual void DrawCompiledGeometryInternal(
      const RageCompiledGeometry* p, int iMeshIndex) = 0;
  virtual void DrawLineStripInternal(
      const RageSpriteVertex v[], int iNumVerts, float LineWidth);
  virtual void DrawSymmetricQuadStripInternal(
      const RageSpriteVertex v[], int iNumVerts) = 0;
  virtual void DrawCircleInternal(const RageSpriteVertex& v, float radius);

  // return std::string() if mode change was successful, an error message
  // otherwise. bNewDeviceOut is set true if a new device was created and
  // textures need to be reloaded.
  virtual std::string TryVideoMode(
      const VideoModeParams& p, bool& bNewDeviceOut) = 0;

  void DrawPolyLine(
      const RageSpriteVertex& p1, const RageSpriteVertex& p2, float LineWidth);

  // Stuff in RageDisplay.cpp
  void SetDefaultRenderStates();

 public:
  // Statistics
  int GetFPS() const;
  int GetVPF() const;
  int GetCumFPS() const;  // average FPS since last reset
  virtual void ResetStats();
  virtual void ProcessStatsOnFlip();
  virtual std::string GetStats() const;
  void StatsAddVerts(int iNumVertsRendered);

  // World matrix stack functions.
  void PushMatrix();
  void PopMatrix();
  void TranslateWorld(float x, float y, float z);
  void Scale(float x, float y, float z);
  void SkewX(float fAmount);
  void SkewY(float fAmount);
  void MultMatrix(const RageMatrix& f) { this->PostMultMatrix(f); } /* alias */
  void PostMultMatrix(const RageMatrix& f);
  void PreMultMatrix(const RageMatrix& f);
  void LoadIdentity();

  // Texture matrix functions
  void TexturePushMatrix();
  void TexturePopMatrix();
  void TextureTranslate(float x, float y);
  void TextureTranslate(const RageVector2& v) {
    this->TextureTranslate(v.x_, v.y_);
  }

  // Projection and View matrix stack functions.
  void CameraPushMatrix();
  void CameraPopMatrix();
  void LoadMenuPerspective(
      float fFOVDegrees, float fWidth, float fHeight, float fVanishPointX,
      float fVanishPointY);
  void LoadLookAt(
      float fov, const RageVector3& Eye, const RageVector3& At,
      const RageVector3& Up);

  // Centering matrix
  void CenteringPushMatrix();
  void CenteringPopMatrix();
  void ChangeCentering(int trans_x, int trans_y, int add_width, int add_height);

  RageSurface* CreateSurfaceFromPixfmt(
      RagePixelFormat pixfmt, void* pixels, int width, int height, int pitch);
  RagePixelFormat FindPixelFormat(
      int bpp, unsigned Rmask, unsigned Gmask, unsigned Bmask, unsigned Amask,
      bool realtime = false);

  // Lua
  void PushSelf(lua_State* L);

 protected:
  RageMatrix GetPerspectiveMatrix(
      float fovy, float aspect, float zNear, float zFar);

  // Different for D3D and OpenGL. Not sure why they're not compatible. -Chris
  virtual RageMatrix GetOrthoMatrix(
      float l, float r, float b, float t, float zn, float zf);
  virtual RageMatrix GetFrustumMatrix(
      float l, float r, float b, float t, float zn, float zf);

  // Matrix that adjusts position and scale of image on the screen
  RageMatrix GetCenteringMatrix(
      float fTranslateX, float fTranslateY, float fAddWidth,
      float fAddHeight) const;
  void UpdateCentering();

  // Called by the RageDisplay derivitives
  const RageMatrix* GetCentering() const;
  const RageMatrix* GetProjectionTop() const;
  const RageMatrix* GetViewTop() const;
  const RageMatrix* GetWorldTop() const;
  const RageMatrix* GetTextureTop() const;

  // Call immediately before and after the backend-specific present routine.
  // When vsync is enabled, the present call itself provides the pacing.
  // When it is disabled, these helpers apply any manual frame limiting.
  void FrameLimitBeforeVsync();
  void FrameLimitAfterVsync();
};

extern RageDisplay*
    DISPLAY;  // global and accessible from anywhere in our program

#endif
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
