/* RageDisplay_Legacy: OpenGL renderer. */

#ifndef RAGE_DISPLAY_OGL_H
#define RAGE_DISPLAY_OGL_H

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION 1
#endif

#include <cstdint>
#include <string>

#include "RageDisplay.h"
#include "RageTextureRenderTarget.h"
#include "RageTypes.h"
#include "global.h"

/* Making an OpenGL call doesn't also flush the error state; if we happen
 * to have an error from a previous call, then the assert below will fail.
 * Flush it. */
#define FlushGLErrors() \
  do {                  \
  } while (glGetError() != GL_NO_ERROR)
#define AssertNoGLError()                                                     \
  {                                                                           \
    GLenum error = glGetError();                                              \
    ASSERT_M(                                                                 \
        error == GL_NO_ERROR, RageDisplay_Legacy_Helpers::GLToString(error)); \
  }

#if defined(DEBUG) || !defined(GL_GET_ERROR_IS_SLOW)
#define DebugFlushGLErrors() FlushGLErrors()
#define DebugAssertNoGLError() AssertNoGLError()
#else
#define DebugFlushGLErrors()
#define DebugAssertNoGLError()
#endif

class RageDisplay_Legacy : public RageDisplay {
 public:
  RageDisplay_Legacy();
  virtual ~RageDisplay_Legacy();
  virtual std::string Init(
      const VideoModeParams& p, bool allow_unaccelerated_renderer);

  virtual std::string GetApiDescription() const { return "OpenGL"; }
  virtual void GetDisplaySpecs(DisplaySpecs& out) const;
  void ResolutionChanged();
  const RagePixelFormatDesc* GetPixelFormatDesc(RagePixelFormat pf) const;

  bool BeginFrame();
  void EndFrame();
  ActualVideoModeParams GetActualVideoModeParams() const;
  void SetBlendMode(BlendMode mode);
  bool SupportsTextureFormat(RagePixelFormat pixfmt, bool realtime = false);
  bool SupportsPerVertexMatrixScale();
  uintptr_t CreateTexture(
      RagePixelFormat pixfmt, RageSurface* img, bool generate_mip_maps);
  void UpdateTexture(
      uintptr_t tex_handle, RageSurface* img, int xoffset, int yoffset,
      int width, int height);
  void DeleteTexture(uintptr_t tex_handle);
  bool UseOffscreenRenderTarget();
  RageSurface* GetTexture(uintptr_t texture);
  RageTextureLock* CreateTextureLock();

  void ClearAllTextures();
  int GetNumTextureUnits();
  void SetTexture(TextureUnit tu, uintptr_t texture);
  void SetTextureMode(TextureUnit tu, TextureMode tm);
  void SetTextureWrapping(TextureUnit tu, bool b);
  int GetMaxTextureSize() const;
  void SetTextureFiltering(TextureUnit tu, bool b);
  void SetEffectMode(EffectMode effect);
  bool IsEffectModeSupported(EffectMode effect);
  bool SupportsRenderToTexture() const;
  bool SupportsFullscreenBorderlessWindow() const;
  uintptr_t CreateRenderTarget(
      const RenderTargetParam& param, int& texture_width_out,
      int& texture_height_out);
  uintptr_t GetRenderTarget();
  void SetRenderTarget(uintptr_t handle, bool preserve_texture);
  bool IsZWriteEnabled() const;
  void SetZWrite(bool b);
  void SetZBias(float f);
  void SetZTestMode(ZTestMode mode);
  void ClearZBuffer();
  void SetCullMode(CullMode mode);
  void SetAlphaTest(bool b);
  void SetMaterial(
      const RageColor& emissive, const RageColor& ambient,
      const RageColor& diffuse, const RageColor& specular, float shininess);
  void SetLighting(bool b);
  void SetLightOff(int index);
  void SetLightDirectional(
      int index, const RageColor& ambient, const RageColor& diffuse,
      const RageColor& specular, const RageVector3& dir);

  void SetSphereEnvironmentMapping(TextureUnit tu, bool b);
  void SetCelShaded(int stage);

  RageCompiledGeometry* CreateCompiledGeometry();
  void DeleteCompiledGeometry(RageCompiledGeometry* p);

  // hacks for cell-shaded models
  virtual void SetLineWidth(float width);

 protected:
  void DrawQuadsInternal(const RageSpriteVertex v[], int num_verts);
  void DrawQuadStripInternal(const RageSpriteVertex v[], int num_verts);
  void DrawFanInternal(const RageSpriteVertex v[], int num_verts);
  void DrawStripInternal(const RageSpriteVertex v[], int num_verts);
  void DrawTrianglesInternal(const RageSpriteVertex v[], int num_verts);
  void DrawCompiledGeometryInternal(
      const RageCompiledGeometry* p, int mesh_index);
  void DrawLineStripInternal(
      const RageSpriteVertex v[], int num_verts, float line_width);
  void DrawSymmetricQuadStripInternal(
      const RageSpriteVertex v[], int num_verts);

  std::string TryVideoMode(const VideoModeParams& p, bool& new_device_out);
  RageSurface* CreateScreenshot();
  RagePixelFormat GetImgPixelFormat(
      RageSurface*& img, bool& free_img, int width, int height,
      bool paletted_texture);
  bool SupportsSurfaceFormat(RagePixelFormat pixfmt);

  void SendCurrentMatrices();

 private:
  RageTextureRenderTarget* offscreen_render_target_;
};

#endif

/*
 * Copyright (c) 2001-2011 Chris Danford, Glenn Maynard, Colby Klein
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
