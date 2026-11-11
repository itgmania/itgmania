#include "RageDisplay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "DisplaySpec.h"
#include "EnumHelper.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "ModelTypes.h"
#include "Platform.h"
#include "Preference.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageSurface.h"
#include "RageSurfaceUtils_Zoom.h"
#include "RageSurface_Save_BMP.h"
#include "RageSurface_Save_JPEG.h"
#include "RageSurface_Save_PNG.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

// Statistics stuff
RageTimer last_check_timer;
int fps, vpf, cfps;

int RageDisplay::GetFPS() const { return fps; }
int RageDisplay::GetVPF() const { return vpf; }
int RageDisplay::GetCumFPS() const { return cfps; }

static int frames_rendered_since_last_check, frames_rendered_since_last_reset,
    verts_rendered_since_last_check, num_checks_since_last_reset;
static RageTimer last_frame_ended_at(kRageZeroTimer);

struct Centering {
  Centering(
      int translate_x = 0, int translate_y = 0, int add_width = 0,
      int add_height = 0)
      : translate_x_(translate_x),
        translate_y_(translate_y),
        add_width_(add_width),
        add_height_(add_height) {}

  int translate_x_, translate_y_, add_width_, add_height_;
};

static std::vector<Centering> centering_stack(1, Centering(0, 0, 0, 0));

RageDisplay* DISPLAY =
    nullptr;  // global and accessible from anywhere in our program

Preference<bool> log_fps("LogFPS", false);
Preference<float> frame_limit_percent("FrameLimitPercent", 0.0f);

static const char* RagePixelFormatNames[] = {
    "RGBA8", "BGRA8", "RGBA4", "RGB5A1", "RGB5",
    "RGB8",  "PAL",   "BGR8",  "A1BGR5", "X1RGB5",
};
XToString(RagePixelFormat);

/* bNeedReloadTextures is set to true if the device was re-created and we need
 * to reload textures.  On failure, an error message is returned.
 * XXX: the renderer itself should probably be the one to try fallback modes */
static LocalizedString SETVIDEOMODE_FAILED(
    "RageDisplay", "SetVideoMode failed:");
std::string RageDisplay::SetVideoMode(
    VideoModeParams p, bool& need_reload_textures) {
  std::string err;
  std::vector<std::string> vs;

  if ((err = this->TryVideoMode(p, need_reload_textures)) == "") {
    return std::string();
  }
  LOG->Trace("TryVideoMode failed: %s", err.c_str());
  vs.push_back(err);

  // fall back to settings that will most likely work
  p.bpp_ = 16;
  if ((err = this->TryVideoMode(p, need_reload_textures)) == "") {
    return std::string();
  }
  vs.push_back(err);

  // "Intel(R) 82810E Graphics Controller" won't accept a 16 bpp surface if
  // the desktop is 32 bpp, so try 32 bpp as well.
  p.bpp_ = 32;
  if ((err = this->TryVideoMode(p, need_reload_textures)) == "") {
    return std::string();
  }
  vs.push_back(err);

  // Fall back on a known resolution good rather than 640 x 480.
  DisplaySpecs dr;
  this->GetDisplaySpecs(dr);
  if (dr.empty()) {
    vs.push_back("No display resolutions");
    return SETVIDEOMODE_FAILED.GetValue() + " " + join(";", vs);
  }

  DisplaySpec d = *dr.begin();
  // Try to find DisplaySpec corresponding to requested display
  for (const auto& candidate : dr) {
    if (candidate.currentMode() != nullptr) {
      d = candidate;
      if (candidate.id() == p.display_id_) {
        break;
      }
    }
  }

  p.display_id_ = d.id();
  const DisplayMode supported = d.currentMode() != nullptr
                                    ? *d.currentMode()
                                    : *d.supportedModes().begin();
  p.width_ = supported.width_;
  p.height_ = supported.height_;
  p.rate_ = std::round(supported.refresh_rate_);
  if ((err = this->TryVideoMode(p, need_reload_textures)) == "") {
    return std::string();
  }
  vs.push_back(err);

  return SETVIDEOMODE_FAILED.GetValue() + " " + join(";", vs);
}

void RageDisplay::ProcessStatsOnFlip() {
  frames_rendered_since_last_check++;
  frames_rendered_since_last_reset++;

  if (last_check_timer.Ago() >= 1.0f)  // update stats every 1 sec.
  {
    float actual_time = last_check_timer.GetDeltaTime();
    num_checks_since_last_reset++;
    fps = std::lrint(frames_rendered_since_last_check / actual_time);
    cfps = frames_rendered_since_last_reset / num_checks_since_last_reset;
    cfps = std::lrint(cfps / actual_time);
    vpf = verts_rendered_since_last_check / frames_rendered_since_last_check;
    frames_rendered_since_last_check = verts_rendered_since_last_check = 0;
    if (log_fps) {
      std::string stats = GetStats();
      Replace(stats, "\n", ", ");
      LOG->Trace("%s", stats.c_str());
    }
  }
}

void RageDisplay::ResetStats() {
  fps = vpf = 0;
  frames_rendered_since_last_check = frames_rendered_since_last_reset = 0;
  num_checks_since_last_reset = 0;
  verts_rendered_since_last_check = 0;
  last_check_timer.GetDeltaTime();
}

std::string RageDisplay::GetStats() const {
  std::string s;
  // If FPS == 0, we don't have stats yet.
  if (!GetFPS()) {
    s = "-- FPS\n-- av FPS\n-- VPF";
  }

  s = ssprintf("%i FPS\n%i av FPS\n%i VPF", GetFPS(), GetCumFPS(), GetVPF());

  //	#if defined(_WIN32)
  s += "\n" + this->GetApiDescription();
  //	#endif

  return s;
}

bool RageDisplay::BeginFrame() {
  this->SetDefaultRenderStates();

  return true;
}

void RageDisplay::EndFrame() { ProcessStatsOnFlip(); }

void RageDisplay::StatsAddVerts(int num_verts_rendered) {
  verts_rendered_since_last_check += num_verts_rendered;
}

/* Draw a line as a quad.  GL_LINES with SmoothLines off can draw line
 * ends at odd angles--they're forced to axis-alignment regardless of the
 * angle of the line. */
void RageDisplay::DrawPolyLine(
    const RageSpriteVertex& p1, const RageSpriteVertex& p2, float line_width) {
  // soh cah toa strikes strikes again!
  float opp = p2.p_.x_ - p1.p_.x_;
  float adj = p2.p_.y_ - p1.p_.y_;
  float hyp = std::pow(opp * opp + adj * adj, 0.5f);

  float lsin = opp / hyp;
  float lcos = adj / hyp;

  RageSpriteVertex v[4];

  v[0] = v[1] = p1;
  v[2] = v[3] = p2;

  float ydist = lsin * line_width / 2;
  float xdist = lcos * line_width / 2;

  v[0].p_.x_ += xdist;
  v[0].p_.y_ -= ydist;
  v[1].p_.x_ -= xdist;
  v[1].p_.y_ += ydist;
  v[2].p_.x_ -= xdist;
  v[2].p_.y_ += ydist;
  v[3].p_.x_ += xdist;
  v[3].p_.y_ -= ydist;

  this->DrawQuad(v);
}

void RageDisplay::DrawLineStripInternal(
    const RageSpriteVertex v[], int num_verts, float line_width) {
  ASSERT(num_verts >= 2);

  /* Draw a line strip with rounded corners using polys. This is used on
   * cards that have strange allergic reactions to antialiased points and
   * lines. */
  for (int i = 0; i < num_verts - 1; ++i) {
    DrawPolyLine(v[i], v[i + 1], line_width);
  }

  // Join the lines with circles so we get rounded corners.
  for (int i = 0; i < num_verts; ++i) {
    DrawCircle(v[i], line_width / 2);
  }
}

void RageDisplay::DrawCircleInternal(const RageSpriteVertex& p, float radius) {
  const int subdivisions = 32;
  RageSpriteVertex v[subdivisions + 2];
  v[0] = p;

  for (int i = 0; i < subdivisions + 1; ++i) {
    const float rotation = float(i) / subdivisions * 2 * kPi;
    const float x = std::cos(rotation) * radius;
    const float y = -std::sin(rotation) * radius;
    v[1 + i] = v[0];
    v[1 + i].p_.x_ += x;
    v[1 + i].p_.y_ += y;
  }

  this->DrawFan(v, subdivisions + 2);
}

void RageDisplay::SetDefaultRenderStates() {
  SetLighting(false);
  SetCullMode(CULL_NONE);
  SetZWrite(false);
  SetZTestMode(ZTEST_OFF);
  SetAlphaTest(true);
  SetBlendMode(BLEND_NORMAL);
  SetTextureFiltering(TextureUnit_1, true);
  SetZBias(0);
  LoadMenuPerspective(0, 640, 480, 320, 240);  // 0 FOV = ortho
}

// Matrix stuff
class MatrixStack {
  std::vector<RageMatrix> stack_;

 public:
  MatrixStack() : stack_() {
    stack_.resize(1);
    LoadIdentity();
  }

  // Pops the top of the stack.
  void Pop() {
    stack_.pop_back();
    ASSERT(stack_.size() > 0);  // underflow
  }

  // Pushes the stack by one, duplicating the current matrix.
  void Push() {
    stack_.push_back(stack_.back());
    ASSERT(stack_.size() < 100);  // overflow
  }

  // Loads identity in the current matrix.
  void LoadIdentity() { RageMatrixIdentity(&stack_.back()); }

  // Loads the given matrix into the current matrix
  void LoadMatrix(const RageMatrix& m) { stack_.back() = m; }

  // Right-Multiplies the given matrix to the current matrix.
  // (transformation is about the current world origin)
  void MultMatrix(const RageMatrix& m) {
    RageMatrixMultiply(&stack_.back(), &m, &stack_.back());
  }

  // Left-Multiplies the given matrix to the current matrix
  // (transformation is about the local origin of the object)
  void MultMatrixLocal(const RageMatrix& m) {
    RageMatrixMultiply(&stack_.back(), &stack_.back(), &m);
  }

  // Left multiply the current matrix with the computed rotation
  // matrix. All angles are counterclockwise. (rotation is about the
  // local origin of the object)
  void RotateYLocal(float degrees) {
    RageMatrix m;
    RageMatrixRotationY(&m, degrees);
    MultMatrixLocal(m);
  }

  // Right multiply the current matrix with the computed scale
  // matrix. (transformation is about the current world origin)
  void Scale(float x, float y, float z) {
    RageMatrix m;
    RageMatrixScaling(&m, x, y, z);
    MultMatrix(m);
  }

  // Left multiply the current matrix with the computed scale
  // matrix. (transformation is about the local origin of the object)
  void ScaleLocal(float x, float y, float z) {
    RageMatrix m;
    RageMatrixScaling(&m, x, y, z);
    MultMatrixLocal(m);
  }

  // Right multiply the current matrix with the computed translation
  // matrix. (transformation is about the current world origin)
  void Translate(float x, float y, float z) {
    RageMatrix m;
    RageMatrixTranslation(&m, x, y, z);
    MultMatrix(m);
  }

  // Left multiply the current matrix with the computed translation
  // matrix. (transformation is about the local origin of the object)
  void TranslateLocal(float x, float y, float z) {
    RageMatrix m;
    RageMatrixTranslation(&m, x, y, z);
    MultMatrixLocal(m);
  }

  void SkewX(float amount) {
    RageMatrix m;
    RageMatrixSkewX(&m, amount);
    MultMatrixLocal(m);
  }

  void SkewY(float amount) {
    RageMatrix m;
    RageMatrixSkewY(&m, amount);
    MultMatrixLocal(m);
  }

  // Obtain the current matrix at the top of the stack
  const RageMatrix* GetTop() const { return &stack_.back(); }
};

static RageMatrix centering_matrix;
static MatrixStack projection_stack;
static MatrixStack view_stack;
static MatrixStack world_stack;
static MatrixStack texture_stack;

RageDisplay::RageDisplay() {
  RageMatrixIdentity(&centering_matrix);
  projection_stack = MatrixStack();
  view_stack = MatrixStack();
  world_stack = MatrixStack();
  texture_stack = MatrixStack();

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "DISPLAY");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

RageDisplay::~RageDisplay() {
  // Unregister with Lua.
  LUA->UnsetGlobal("DISPLAY");
}

const RageMatrix* RageDisplay::GetCentering() const {
  return &centering_matrix;
}

const RageMatrix* RageDisplay::GetProjectionTop() const {
  return projection_stack.GetTop();
}

const RageMatrix* RageDisplay::GetViewTop() const {
  return view_stack.GetTop();
}

const RageMatrix* RageDisplay::GetWorldTop() const {
  return world_stack.GetTop();
}

const RageMatrix* RageDisplay::GetTextureTop() const {
  return texture_stack.GetTop();
}

void RageDisplay::PushMatrix() { world_stack.Push(); }

void RageDisplay::PopMatrix() { world_stack.Pop(); }

void RageDisplay::TranslateWorld(float x, float y, float z) {
  world_stack.Translate(x, y, z);
}

void RageDisplay::Scale(float x, float y, float z) {
  world_stack.ScaleLocal(x, y, z);
}

void RageDisplay::SkewX(float amount) { world_stack.SkewX(amount); }

void RageDisplay::SkewY(float amount) { world_stack.SkewY(amount); }

void RageDisplay::PostMultMatrix(const RageMatrix& m) {
  world_stack.MultMatrix(m);
}

void RageDisplay::PreMultMatrix(const RageMatrix& m) {
  world_stack.MultMatrixLocal(m);
}

void RageDisplay::LoadIdentity() { world_stack.LoadIdentity(); }

void RageDisplay::TexturePushMatrix() { texture_stack.Push(); }

void RageDisplay::TexturePopMatrix() { texture_stack.Pop(); }

void RageDisplay::TextureTranslate(float x, float y) {
  texture_stack.TranslateLocal(x, y, 0);
}

void RageDisplay::LoadMenuPerspective(
    float fov_degrees, float width, float height, float vanish_point_x,
    float vanish_point_y) {
  // fovDegrees == 0 gives ortho projection.
  if (fov_degrees == 0) {
    float left = 0, right = width, bottom = height, top = 0;
    projection_stack.LoadMatrix(
        GetOrthoMatrix(left, right, bottom, top, -1000, +1000));
    view_stack.LoadIdentity();
  } else {
    rage_clamp(fov_degrees, 0.1f, 179.9f);
    float fov_radians = fov_degrees / 180.f * kPi;
    float theta = fov_radians / 2;
    float dist_camera_from_image = width / 2 / std::tan(theta);

    vanish_point_x = SCALE(vanish_point_x, 0, width, width, 0);
    vanish_point_y = SCALE(vanish_point_y, 0, height, height, 0);

    vanish_point_x -= width / 2;
    vanish_point_y -= height / 2;

    // It's the caller's responsibility to push first.
    projection_stack.LoadMatrix(GetFrustumMatrix(
        (vanish_point_x - width / 2) / dist_camera_from_image,
        (vanish_point_x + width / 2) / dist_camera_from_image,
        (vanish_point_y + height / 2) / dist_camera_from_image,
        (vanish_point_y - height / 2) / dist_camera_from_image, 1,
        dist_camera_from_image + 1000));

    view_stack.LoadMatrix(RageLookAt(
        -vanish_point_x + width / 2, -vanish_point_y + height / 2,
        dist_camera_from_image, -vanish_point_x + width / 2,
        -vanish_point_y + height / 2, 0, 0.0f, 1.0f, 0.0f));
  }
}

void RageDisplay::CameraPushMatrix() {
  projection_stack.Push();
  view_stack.Push();
}

void RageDisplay::CameraPopMatrix() {
  projection_stack.Pop();
  view_stack.Pop();
}

/* gluLookAt. The result is pre-multiplied to the matrix (M = L * M) instead of
 * post-multiplied. */
void RageDisplay::LoadLookAt(
    float fov, const RageVector3& eye, const RageVector3& at,
    const RageVector3& up) {
  float aspect = GetActualVideoModeParams().display_aspect_ratio_;
  projection_stack.LoadMatrix(GetPerspectiveMatrix(fov, aspect, 1, 1000));

  // Flip the Y coordinate, so positive numbers go down.
  projection_stack.Scale(1, -1, 1);

  view_stack.LoadMatrix(RageLookAt(
      eye.x_, eye.y_, eye.z_, at.x_, at.y_, at.z_, up.x_, up.y_, up.z_));
}

RageMatrix RageDisplay::GetPerspectiveMatrix(
    float fovy, float aspect, float z_near, float z_far) {
  float ymax = z_near * std::tan(fovy * kPi / 360.0f);
  float ymin = -ymax;
  float xmin = ymin * aspect;
  float xmax = ymax * aspect;

  return GetFrustumMatrix(xmin, xmax, ymin, ymax, z_near, z_far);
}

RageSurface* RageDisplay::CreateSurfaceFromPixfmt(
    RagePixelFormat pixfmt, void* pixels, int width, int height, int pitch) {
  const RagePixelFormatDesc* tpf = GetPixelFormatDesc(pixfmt);

  RageSurface* surf = CreateSurfaceFrom(
      width, height, tpf->bpp_, tpf->masks_[0], tpf->masks_[1], tpf->masks_[2],
      tpf->masks_[3], (uint8_t*)pixels, pitch);

  return surf;
}

RagePixelFormat RageDisplay::FindPixelFormat(
    int bpp, unsigned rmask, unsigned gmask, unsigned bmask, unsigned amask,
    bool realtime) {
  RagePixelFormatDesc tmp = {bpp, {rmask, gmask, bmask, amask}};

  FOREACH_ENUM(RagePixelFormat, pix_fmt) {
    const RagePixelFormatDesc* pf =
        GetPixelFormatDesc(RagePixelFormat(pix_fmt));
    if (!SupportsTextureFormat(RagePixelFormat(pix_fmt), realtime)) {
      continue;
    }

    if (memcmp(pf, &tmp, sizeof(tmp))) {
      continue;
    }
    return pix_fmt;
  }

  return RagePixelFormat_Invalid;
}

/* These convert to OpenGL's coordinate system: -1,-1 is the bottom-left,
 * +1,+1 is the top-right, and Z goes from -1 (viewer) to +1 (distance).
 * It's a little odd, but very well-defined. */
RageMatrix RageDisplay::GetOrthoMatrix(
    float l, float r, float b, float t, float zn, float zf) {
  RageMatrix m(
      2 / (r - l), 0, 0, 0, 0, 2 / (t - b), 0, 0, 0, 0, -2 / (zf - zn), 0,
      -(r + l) / (r - l), -(t + b) / (t - b), -(zf + zn) / (zf - zn), 1);
  return m;
}

RageMatrix RageDisplay::GetFrustumMatrix(
    float l, float r, float b, float t, float zn, float zf) {
  // glFrustum
  float a = (r + l) / (r - l);
  float B = (t + b) / (t - b);
  float c = -1 * (zf + zn) / (zf - zn);
  float d = -1 * (2 * zf * zn) / (zf - zn);
  RageMatrix m(
      2 * zn / (r - l), 0, 0, 0, 0, 2 * zn / (t - b), 0, 0, a, B, c, -1, 0, 0,
      d, 0);
  return m;
}

void RageDisplay::ResolutionChanged() {
  // The centering matrix depends on the resolution.
  UpdateCentering();
}

void RageDisplay::CenteringPushMatrix() {
  centering_stack.push_back(centering_stack.back());
  ASSERT(centering_stack.size() < 100);  // overflow
}

void RageDisplay::CenteringPopMatrix() {
  centering_stack.pop_back();
  ASSERT(centering_stack.size() > 0);  // underflow
  UpdateCentering();
}

void RageDisplay::ChangeCentering(
    int translate_x, int translate_y, int add_width, int add_height) {
  centering_stack.back() =
      Centering(translate_x, translate_y, add_width, add_height);

  UpdateCentering();
}

RageMatrix RageDisplay::GetCenteringMatrix(
    float translate_x, float translate_y, float add_width,
    float add_height) const {
  // in screen space, left edge = -1, right edge = 1, bottom edge = -1. top edge
  // = 1
  float width = (float)GetActualVideoModeParams().window_width_;
  float height = (float)GetActualVideoModeParams().window_height_;
  float percent_shift_x = SCALE(translate_x, 0, width, 0, +2.0f);
  float percent_shift_y = SCALE(translate_y, 0, height, 0, -2.0f);
  float percent_scale_x = SCALE(add_width, 0, width, 1.0f, 2.0f);
  float percent_scale_y = SCALE(add_height, 0, height, 1.0f, 2.0f);

  RageMatrix m1;
  RageMatrix m2;
  RageMatrixTranslation(&m1, percent_shift_x, percent_shift_y, 0);
  RageMatrixScaling(&m2, percent_scale_x, percent_scale_y, 1);
  RageMatrix out;
  RageMatrixMultiply(&out, &m1, &m2);
  return out;
}

void RageDisplay::UpdateCentering() {
  const Centering& p = centering_stack.back();
  centering_matrix = GetCenteringMatrix(
      (float)p.translate_x_, (float)p.translate_y_, (float)p.add_width_,
      (float)p.add_height_);
}

bool RageDisplay::SaveScreenshot(
    const std::string& path, GraphicsFileFormat format) {
  RageSurface* surface = this->CreateScreenshot();

  if (nullptr == surface) {
    LOG->Warn("SaveScreenshot: failed to create a screenshot surface");
    return false;
  }

  /* Unless we're in lossless, resize the image to 640x480.  If we're saving
   * lossy, there's no sense in saving 1280x960 screenshots, and we don't want
   * to output screenshots in a strange (non-1) sample aspect ratio. */
  if (format != SAVE_LOSSLESS && format != SAVE_LOSSLESS_SENSIBLE) {
    // Maintain the DAR.
    ASSERT(GetActualVideoModeParams().display_aspect_ratio_ > 0);
    int height = 480;
    // This used to be lrint. However, lrint causes odd resolutions like
    // 639x480 (4:3) and 853x480 (16:9). ceil gives correct values. -aj
    int width =
        std::ceil(height * GetActualVideoModeParams().display_aspect_ratio_);
    RageSurfaceUtils::Zoom(surface, width, height);
  }

  RageFile out;
  if (!out.Open(path, RageFile::WRITE)) {
    LOG->Warn(
        "SaveScreenshot: Failed to open %s for writing: %s", path.c_str(),
        out.GetError().c_str());
    RageUtil::SafeDelete(surface);
    return false;
  }

  bool success = false;
  std::string str_error = "";
  switch (format) {
    case SAVE_LOSSLESS:
      success = RageSurfaceUtils::SaveBMP(surface, out);
      break;
    case SAVE_LOSSLESS_SENSIBLE:
      success = RageSurfaceUtils::SavePNG(surface, out, str_error);
      break;
    case SAVE_LOSSY_LOW_QUAL:
      success = RageSurfaceUtils::SaveJPEG(surface, out, false);
      break;
    case SAVE_LOSSY_HIGH_QUAL:
      success = RageSurfaceUtils::SaveJPEG(surface, out, true);
      break;
    default:
      LOG->Warn(
          "SaveScreenshot: Invalid graphics file format requested %d", format);
      success = false;
      break;
  }

  RageUtil::SafeDelete(surface);

  if (!success) {
    LOG->Warn(
        "SaveScreenshot: Failed to save screenshot to %s: %s", path.c_str(),
        out.GetError().c_str());
  }

  return success;
}

void RageDisplay::DrawQuads(const RageSpriteVertex v[], int num_verts) {
  ASSERT((num_verts % 4) == 0);

  if (!num_verts) {
    return;
  }

  this->DrawQuadsInternal(v, num_verts);

  StatsAddVerts(num_verts);
}

void RageDisplay::DrawQuadStrip(const RageSpriteVertex v[], int num_verts) {
  ASSERT((num_verts % 2) == 0);

  if (num_verts < 4) {
    return;
  }

  this->DrawQuadStripInternal(v, num_verts);

  StatsAddVerts(num_verts);
}

void RageDisplay::DrawFan(const RageSpriteVertex v[], int num_verts) {
  ASSERT(num_verts >= 3);

  this->DrawFanInternal(v, num_verts);

  StatsAddVerts(num_verts);
}

void RageDisplay::DrawStrip(const RageSpriteVertex v[], int num_verts) {
  ASSERT(num_verts >= 3);

  this->DrawStripInternal(v, num_verts);

  StatsAddVerts(num_verts);
}

void RageDisplay::DrawTriangles(const RageSpriteVertex v[], int num_verts) {
  if (num_verts == 0) {
    return;
  }

  ASSERT(num_verts >= 3);

  this->DrawTrianglesInternal(v, num_verts);

  StatsAddVerts(num_verts);
}

void RageDisplay::DrawCompiledGeometry(
    const RageCompiledGeometry* p, int mesh_index,
    const std::vector<msMesh>& meshes) {
  this->DrawCompiledGeometryInternal(p, mesh_index);

  StatsAddVerts(meshes[mesh_index].triangles_.size());
}

void RageDisplay::DrawLineStrip(
    const RageSpriteVertex v[], int num_verts, float line_width) {
  ASSERT(num_verts >= 2);

  this->DrawLineStripInternal(v, num_verts, line_width);
}

/*
 * Draw a strip of:
 *
 * 0..1..2
 * . /.\ .
 * ./ . \.
 * 3..4..5
 * . /.\ .
 * ./ . \.
 * 6..7..8
 */

void RageDisplay::DrawSymmetricQuadStrip(
    const RageSpriteVertex v[], int num_verts) {
  ASSERT(num_verts >= 3);

  if (num_verts < 6) {
    return;
  }

  this->DrawSymmetricQuadStripInternal(v, num_verts);

  StatsAddVerts(num_verts);
}

void RageDisplay::DrawCircle(const RageSpriteVertex& v, float radius) {
  this->DrawCircleInternal(v, radius);
}

int GetFrameLimitIntervalAsMicroseconds(
    const ActualVideoModeParams& vm) noexcept {
  const bool no_valid_frame_timing_history_or_vsync =
      !vm.vsync_ && vm.rate_ > 0 && frame_limit_percent.Get() > 0.0f &&
      !last_frame_ended_at.IsZero();

  int delay_microseconds = 0;
  if (no_valid_frame_timing_history_or_vsync) {
    float frame_time = last_frame_ended_at.GetDeltaTime();
    float expected_time = 1.0f / vm.rate_;

    /* This is typically used to turn some of the delay that would normally
     * be waiting for vsync and turn it into a usleep, to make sure we give
     * up the CPU.  If we overshoot the sleep, then we'll miss the vsync,
     * so allow tweaking the amount of time we expect a frame to take.
     * Frame limiting is disabled by setting this to 0. */
    expected_time *= frame_limit_percent.Get();
    float extra_time = expected_time - frame_time;

    delay_microseconds = int(extra_time * 1000000);
  }

  if (!HOOKS->AppHasFocus()) {
    delay_microseconds = std::max(
        delay_microseconds,
        10000);  // give some time to other processes and threads
  }

  return delay_microseconds;
}

void RageDisplay::FrameLimitBeforeVsync() {
  const int delay_microseconds =
      GetFrameLimitIntervalAsMicroseconds(GetActualVideoModeParams());
  if (delay_microseconds > 0) {
    usleep(delay_microseconds);
  }
}

void RageDisplay::FrameLimitAfterVsync() { last_frame_ended_at.Touch(); }

RageCompiledGeometry::~RageCompiledGeometry() {}

void RageCompiledGeometry::Set(const std::vector<msMesh>& meshes) {
  size_t total_verts = 0;
  size_t total_triangles = 0;

  any_needs_texture_matrix_scale_ = false;

  mesh_info_.resize(meshes.size());
  for (unsigned i = 0; i < meshes.size(); i++) {
    const msMesh& mesh = meshes[i];
    const std::vector<RageModelVertex>& vertices = mesh.vertices_;
    const std::vector<msTriangle>& triangles = mesh.triangles_;

    MeshInfo& mesh_info = mesh_info_[i];
    mesh_info.needs_texture_matrix_scale_ = false;

    mesh_info.vertex_start_ = total_verts;
    mesh_info.vertex_count_ = vertices.size();
    mesh_info.triangle_start_ = total_triangles;
    mesh_info.triangle_count_ = triangles.size();

    total_verts += vertices.size();
    total_triangles += triangles.size();

    for (unsigned j = 0; j < vertices.size(); ++j) {
      if (vertices[j].texture_matrix_scale_.x_ != 1.0f ||
          vertices[j].texture_matrix_scale_.y_ != 1.0f) {
        mesh_info.needs_texture_matrix_scale_ = true;
        any_needs_texture_matrix_scale_ = true;
      }
    }
  }

  this->Allocate(meshes);

  Change(meshes);
}

// lua start
#include "LuaBinding.h"

// Register with Lua.
static void register_REFRESH_DEFAULT(lua_State* L) {
  lua_pushstring(L, "REFRESH_DEFAULT");
  lua_pushinteger(L, kRefreshDefault);
  lua_settable(L, LUA_GLOBALSINDEX);
}
REGISTER_WITH_LUA_FUNCTION(register_REFRESH_DEFAULT);

/** @brief Allow Lua to have access to the RageDisplay. */
class LunaRageDisplay : public Luna<RageDisplay> {
 public:
  static int GetDisplayWidth(T* p, lua_State* L) {
    VideoModeParams params = p->GetActualVideoModeParams();
    LuaHelpers::Push(L, params.width_);
    return 1;
  }

  static int GetDisplayHeight(T* p, lua_State* L) {
    VideoModeParams params = p->GetActualVideoModeParams();
    LuaHelpers::Push(L, params.height_);
    return 1;
  }

  static int GetFPS(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetFPS());
    return 1;
  }

  static int GetVPF(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetVPF());
    return 1;
  }

  static int GetCumFPS(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetCumFPS());
    return 1;
  }

  static int GetDisplaySpecs(T* p, lua_State* L) {
    DisplaySpecs s;
    p->GetDisplaySpecs(s);
    pushDisplaySpecs(L, s);
    return 1;
  }

  static int SupportsRenderToTexture(T* p, lua_State* L) {
    lua_pushboolean(L, p->SupportsRenderToTexture());
    return 1;
  }

  static int SupportsFullscreenBorderlessWindow(T* p, lua_State* L) {
    lua_pushboolean(L, p->SupportsFullscreenBorderlessWindow());
    return 1;
  }

  LunaRageDisplay() {
    ADD_METHOD(GetDisplayWidth);
    ADD_METHOD(GetDisplayHeight);
    ADD_METHOD(GetFPS);
    ADD_METHOD(GetVPF);
    ADD_METHOD(GetCumFPS);
    ADD_METHOD(GetDisplaySpecs);
    ADD_METHOD(SupportsRenderToTexture);
    ADD_METHOD(SupportsFullscreenBorderlessWindow);
  }
};

LUA_REGISTER_CLASS(RageDisplay)
// lua end

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
