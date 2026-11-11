#include "RageDisplay_OGL.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Actor.h"
#include "DisplaySpec.h"
#include "EnumHelper.h"
#include "LocalizedString.h"
#include "ModelTypes.h"
#include "RageDisplay.h"
#include "RageDisplay_OGL_Helpers.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTextureRenderTarget.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "Sprite.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"
#include "global.h"

using namespace RageDisplay_Legacy_Helpers;

#if defined(WINDOWS)
#include <GL/wglew.h>
#endif

#if defined(_MSC_VER)
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#endif

#ifdef NO_GL_FLUSH
#define glFlush()
#endif

//
// Globals
//

static bool reverse_packed_pixels_works = true;
static bool color_index_table_works = true;

/* OpenGL system information that generally doesn't change at runtime. */

/* Range and granularity of points and lines: */
static float line_range[2];
static float point_range[2];

/* GLU version * 10: */
static int g_glu_version;

static int max_texture_units = 0;

/* We don't actually use normals (we don't turn on lighting), there's just
 * no GL_T2F_C4F_V3F. */
static const GLenum kRageSpriteVertexFormat = GL_T2F_C4F_N3F_V3F;

/* If we support texture matrix scaling, a handle to the vertex program: */
static GLhandleARB texture_matrix_shader = 0;

static std::map<uintptr_t, RenderTarget*> map_render_targets;
static RenderTarget* current_render_target = nullptr;

static LowLevelWindow* wind;

static bool invert_y = false;

static void InvalidateObjects();

static RageDisplay::RagePixelFormatDesc pixel_format_desc[NUM_RagePixelFormat] =
    {{/* R8G8B8A8 */
      32,
      {0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF}},
     {/* B8G8R8A8 */
      32,
      {0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF}},
     {
         /* R4G4B4A4 */
         16,
         {0xF000, 0x0F00, 0x00F0, 0x000F},
     },
     {
         /* R5G5B5A1 */
         16,
         {0xF800, 0x07C0, 0x003E, 0x0001},
     },
     {
         /* R5G5B5X1 */
         16,
         {0xF800, 0x07C0, 0x003E, 0x0000},
     },
     {/* R8G8B8 */
      24,
      {0xFF0000, 0x00FF00, 0x0000FF, 0x000000}},
     {
         /* Paletted */
         8,
         {0, 0, 0, 0} /* N/A */
     },
     {/* B8G8R8 */
      24,
      {0x0000FF, 0x00FF00, 0xFF0000, 0x000000}},
     {
         /* A1R5G5B5 */
         16,
         {0x7C00, 0x03E0, 0x001F, 0x8000},
     },
     {
         /* X1R5G5B5 */
         16,
         {0x7C00, 0x03E0, 0x001F, 0x0000},
     }};

/* g_GLPixFmtInfo is used for both texture formats and surface formats.  For
 * example, it's fine to ask for a RagePixelFormat_RGB5 texture, but to supply a
 * surface matching RagePixelFormat_RGB8.  OpenGL will simply discard the extra
 * bits.
 *
 * It's possible for a format to be supported as a texture format but not as a
 * surface format.  For example, if packed pixels aren't supported, we can still
 * use GL_RGB5_A1, but we'll have to convert to a supported surface pixel format
 * first.  It's not ideal, since we'll convert to RGBA8 and OGL will convert
 * back, but it works fine.
 */
struct GLPixFmtInfo_t {
  GLenum internalfmt_; /* target format */
  GLenum format_;      /* target format */
  GLenum type_;        /* data format */
} const kGlPixFmtInfo[NUM_RagePixelFormat] = {
    {
        /* R8G8B8A8 */
        GL_RGBA8,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    },
    {
        /* R8G8B8A8 */
        GL_RGBA8,
        GL_BGRA,
        GL_UNSIGNED_BYTE,
    },
    {
        /* B4G4R4A4 */
        GL_RGBA4,
        GL_RGBA,
        GL_UNSIGNED_SHORT_4_4_4_4,
    },
    {
        /* B5G5R5A1 */
        GL_RGB5_A1,
        GL_RGBA,
        GL_UNSIGNED_SHORT_5_5_5_1,
    },
    {
        /* B5G5R5 */
        GL_RGB5,
        GL_RGBA,
        GL_UNSIGNED_SHORT_5_5_5_1,
    },
    {
        /* B8G8R8 */
        GL_RGB8,
        GL_RGB,
        GL_UNSIGNED_BYTE,
    },
    {
        /* Paletted */
        GL_COLOR_INDEX8_EXT,
        GL_COLOR_INDEX,
        GL_UNSIGNED_BYTE,
    },
    {
        /* B8G8R8 */
        GL_RGB8,
        GL_BGR,
        GL_UNSIGNED_BYTE,
    },
    {
        /* A1R5G5B5 (matches D3DFMT_A1R5G5B5) */
        GL_RGB5_A1,
        GL_BGRA,
        GL_UNSIGNED_SHORT_1_5_5_5_REV,
    },
    {
        /* X1R5G5B5 */
        GL_RGB5,
        GL_BGRA,
        GL_UNSIGNED_SHORT_1_5_5_5_REV,
    }};

static void FixLittleEndian() {
  if constexpr (!Endian::kLittle) {
    return;
  }

  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  for (int i = 0; i < NUM_RagePixelFormat; ++i) {
    RageDisplay::RagePixelFormatDesc& pf = pixel_format_desc[i];

    /* OpenGL and RageSurface handle byte formats differently; we need
     * to flip non-paletted masks to make them line up. */
    if (kGlPixFmtInfo[i].type_ != GL_UNSIGNED_BYTE || pf.bpp_ == 8) {
      continue;
    }

    for (int mask = 0; mask < 4; ++mask) {
      int m = pf.masks_[mask];
      switch (pf.bpp_) {
        case 24:
          m = Swap24(m);
          break;
        case 32:
          m = Swap32(m);
          break;
        default:
          FAIL_M(ssprintf("Unsupported BPP value: %i", pf.bpp_));
      }
      pf.masks_[mask] = m;
    }
  }
}

static void TurnOffHardwareVBO() {
  if (GLEW_ARB_vertex_buffer_object) {
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
  }
}

RageDisplay_Legacy::RageDisplay_Legacy() {
  LOG->Trace("RageDisplay_Legacy::RageDisplay_Legacy()");
  LOG->MapLog("renderer", "Current renderer: OpenGL");

  FixLittleEndian();
  RageDisplay_Legacy_Helpers::Init();

  wind = nullptr;
  texture_matrix_shader = 0;
  offscreen_render_target_ = nullptr;
}

std::string GetInfoLog(GLhandleARB h) {
  GLint length;
  glGetObjectParameterivARB(h, GL_OBJECT_INFO_LOG_LENGTH_ARB, &length);
  if (!length) {
    return std::string();
  }

  GLcharARB* info_log = new GLcharARB[length];
  glGetInfoLogARB(h, length, &length, info_log);
  std::string ret = info_log;
  delete[] info_log;
  TrimRight(ret);
  return ret;
}

GLhandleARB CompileShader(
    GLenum shader_type, std::string s_file, std::vector<std::string> defines) {
  /* XXX: This would not be necessary if it wasn't for the special case for Cel.
   */
  if (shader_type == GL_FRAGMENT_SHADER_ARB &&
      !glewIsSupported("GL_VERSION_2_0")) {
    LOG->Warn(
        "Fragment shaders not supported by driver. Some effects will not be "
        "available.");
    return 0;
  }

  std::string buffer;
  {
    RageFile file;
    if (!file.Open(s_file)) {
      LOG->Warn(
          "Error compiling shader %s: %s", s_file.c_str(),
          file.GetError().c_str());
      return 0;
    }

    if (file.Read(buffer, file.GetFileSize()) == -1) {
      LOG->Warn(
          "Error compiling shader %s: %s", s_file.c_str(),
          file.GetError().c_str());
      return 0;
    }
  }

  LOG->Trace("Compiling shader %s", s_file.c_str());
  GLhandleARB shader = glCreateShaderObjectARB(shader_type);
  std::vector<const GLcharARB*> data;
  std::vector<GLint> length;
  for (std::string& s : defines) {
    s = ssprintf("#define %s\n", s.c_str());
    data.push_back(s.data());
    length.push_back(s.size());
  }
  data.push_back("#line 1\n");
  length.push_back(8);

  data.push_back(buffer.data());
  length.push_back(buffer.size());
  glShaderSourceARB(shader, data.size(), &data[0], &length[0]);

  glCompileShaderARB(shader);

  std::string info = GetInfoLog(shader);

  GLint compile_status = GL_FALSE;
  glGetObjectParameterivARB(
      shader, GL_OBJECT_COMPILE_STATUS_ARB, &compile_status);
  if (!compile_status) {
    LOG->Warn("Error compiling shader %s:\n%s", s_file.c_str(), info.c_str());
    glDeleteObjectARB(shader);
    return 0;
  }

  if (!info.empty()) {
    LOG->Trace(
        "Messages compiling shader %s:\n%s", s_file.c_str(), info.c_str());
  }

  return shader;
}

GLhandleARB LoadShader(
    GLenum shader_type, std::string file, std::vector<std::string> defines) {
  /* Vertex shaders are supported by more hardware than fragment shaders.
   * If this causes any trouble I will have to up the requirement for both
   * of them to at least GL 2.0. Regardless we need basic GLSL support.
   * -Colby */
  if (!glewIsSupported("GL_ARB_shading_language_100 GL_ARB_shader_objects") ||
      (shader_type == GL_FRAGMENT_SHADER_ARB &&
       !glewIsSupported("GL_VERSION_2_0")) ||
      (shader_type == GL_VERTEX_SHADER_ARB &&
       !glewIsSupported("GL_ARB_vertex_shader"))) {
    LOG->Warn(
        "%s shaders not supported by driver. Some effects will not be "
        "available.",
        (shader_type == GL_FRAGMENT_SHADER_ARB) ? "Fragment" : "Vertex");
    return 0;
  }

  // XXX: dumb, but I don't feel like refactoring ragedisplay for this. -Colby
  GLhandleARB secondary_shader = 0;
  if (file == "Data/Shaders/GLSL/Cel.vert") {
    secondary_shader = CompileShader(
        GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Cel.frag", defines);
  } else if (file == "Data/Shaders/GLSL/Shell.vert") {
    secondary_shader = CompileShader(
        GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Shell.frag", defines);
  } else if (file == "Data/Shaders/GLSL/Distance field.vert") {
    secondary_shader = CompileShader(
        GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Distance field.frag",
        defines);
  }

  GLhandleARB shader = CompileShader(shader_type, file, defines);
  if (shader == 0) {
    return 0;
  }

  GLhandleARB program = glCreateProgramObjectARB();
  glAttachObjectARB(program, shader);

  if (secondary_shader) {
    glAttachObjectARB(program, secondary_shader);
    glDeleteObjectARB(secondary_shader);
  }
  glDeleteObjectARB(shader);

  // Link the program.
  glLinkProgramARB(program);
  GLint link_status = false;
  glGetObjectParameterivARB(program, GL_OBJECT_LINK_STATUS_ARB, &link_status);

  if (!link_status) {
    LOG->Warn(
        "Error linking shader %s: %s", file.c_str(),
        GetInfoLog(program).c_str());
    glDeleteObjectARB(program);
    return 0;
  }
  return program;
}

static int attrib_texture_matrix_scale;

static GLhandleARB unpremultiply_shader = 0;
static GLhandleARB color_burn_shader = 0;
static GLhandleARB color_dodge_shader = 0;
static GLhandleARB vivid_light_shader = 0;
static GLhandleARB hard_mix_shader = 0;
static GLhandleARB overlay_shader = 0;
static GLhandleARB screen_shader = 0;
static GLhandleARB yuyv422_shader = 0;
static GLhandleARB shell_shader = 0;
static GLhandleARB cel_shader = 0;
static GLhandleARB distance_field_shader = 0;

void InitShaders() {
  // xxx: replace this with a ShaderManager or something that reads in
  // the shaders and determines shader type by file extension. -aj
  // argh shaders in stepmania are painful -colby
  std::vector<std::string> defines;

  // used for scrolling textures (I think)
  texture_matrix_shader = LoadShader(
      GL_VERTEX_SHADER_ARB, "Data/Shaders/GLSL/Texture matrix scaling.vert",
      defines);

  // these two are for dancing characters and are both actually shader pairs
  shell_shader =
      LoadShader(GL_VERTEX_SHADER_ARB, "Data/Shaders/GLSL/Shell.vert", defines);
  cel_shader =
      LoadShader(GL_VERTEX_SHADER_ARB, "Data/Shaders/GLSL/Cel.vert", defines);
  distance_field_shader = LoadShader(
      GL_VERTEX_SHADER_ARB, "Data/Shaders/GLSL/Distance field.vert", defines);

  // effects
  unpremultiply_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Unpremultiply.frag", defines);
  color_burn_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Color burn.frag", defines);
  color_dodge_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Color dodge.frag", defines);
  vivid_light_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Vivid light.frag", defines);
  hard_mix_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Hard mix.frag", defines);
  overlay_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Overlay.frag", defines);
  screen_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/Screen.frag", defines);
  yuyv422_shader = LoadShader(
      GL_FRAGMENT_SHADER_ARB, "Data/Shaders/GLSL/YUYV422.frag", defines);

  // Bind attributes.
  if (texture_matrix_shader) {
    FlushGLErrors();
    attrib_texture_matrix_scale =
        glGetAttribLocationARB(texture_matrix_shader, "TextureMatrixScale");
    if (attrib_texture_matrix_scale == -1) {
      LOG->Trace(
          "Scaling shader link failed: couldn't bind attribute "
          "\"TextureMatrixScale\"");
      glDeleteObjectARB(texture_matrix_shader);
      texture_matrix_shader = 0;
    } else {
      AssertNoGLError();

      /* Older Catalyst drivers seem to throw GL_INVALID_OPERATION here. */
      glVertexAttrib2fARB(attrib_texture_matrix_scale, 1, 1);
      GLenum i_error = glGetError();
      if (i_error == GL_INVALID_OPERATION) {
        LOG->Trace(
            "Scaling shader failed: glVertexAttrib2fARB returned "
            "GL_INVALID_OPERATION");
        glDeleteObjectARB(texture_matrix_shader);
        texture_matrix_shader = 0;
      } else {
        ASSERT_M(i_error == GL_NO_ERROR, GLToString(i_error));
      }
    }
  }
}

static LocalizedString OBTAIN_AN_UPDATED_VIDEO_DRIVER(
    "RageDisplay_Legacy",
    "Obtain an updated driver from your video card manufacturer.");
static LocalizedString GLDIRECT_IS_NOT_COMPATIBLE(
    "RageDisplay_Legacy",
    "GLDirect was detected.  GLDirect is not compatible with this game and "
    "should be disabled.");
std::string RageDisplay_Legacy::Init(
    const VideoModeParams& p, bool allow_unaccelerated_renderer) {
  wind = LowLevelWindow::Create();

  bool ignore = false;
  std::string error = SetVideoMode(p, ignore);
  if (error != "") {
    return error;
  }

  // Log driver details
  wind->LogDebugInformation();
  LOG->Info(
      "OGL Vendor: %s", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
  LOG->Info(
      "OGL Renderer: %s",
      reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
  LOG->Info(
      "OGL Version: %s",
      reinterpret_cast<const char*>(glGetString(GL_VERSION)));
  LOG->Info("OGL Max texture size: %i", GetMaxTextureSize());
  LOG->Info("OGL Texture units: %i", max_texture_units);

  /* Pretty-print the extension string: */
  LOG->Info("OGL Extensions:");
  {
    const char* extension_string = (const char*)glGetString(GL_EXTENSIONS);
    std::vector<std::string> extensions;
    split(extension_string, " ", extensions);
    sort(extensions.begin(), extensions.end());
    size_t next_to_print = 0;
    while (next_to_print < extensions.size()) {
      size_t last_to_print = next_to_print;
      std::string type;
      for (size_t i = next_to_print; i < extensions.size(); ++i) {
        std::vector<std::string> bits;
        split(extensions[i], "_", bits);
        std::string this_type;
        if (bits.size() > 2) {
          this_type = join("_", bits.begin(), bits.begin() + 2);
        }
        if (i > next_to_print && this_type != type) {
          break;
        }
        type = this_type;
        last_to_print = i;
      }

      if (next_to_print == last_to_print) {
        LOG->Info("  %s", extensions[next_to_print].c_str());
        ++next_to_print;
        continue;
      }

      std::string list = ssprintf("  %s: ", type.c_str());
      while (next_to_print <= last_to_print) {
        std::vector<std::string> bits;
        split(extensions[next_to_print], "_", bits);
        std::string short_ext = join("_", bits.begin() + 2, bits.end());
        list += short_ext;
        if (next_to_print < last_to_print) {
          list += ", ";
        }
        if (next_to_print == last_to_print ||
            list.size() + extensions[next_to_print + 1].size() > 120) {
          LOG->Info("%s", list.c_str());
          list = "    ";
        }
        ++next_to_print;
      }
    }
  }

  if (wind->IsSoftwareRenderer(error)) {
    if (!allow_unaccelerated_renderer) {
      return error + "  " + OBTAIN_AN_UPDATED_VIDEO_DRIVER.GetValue() + "\n\n";
    }
    LOG->Warn("Low-performance OpenGL renderer: %s", error.c_str());
  }

#if defined(_WIN32)
  /* GLDirect is a Direct3D wrapper for OpenGL.  It's rather buggy; and if in
   * any case GLDirect can successfully render us, we should be able to do so
   * too using Direct3D directly.  (If we can't, it's a bug that we can work
   * around--if GLDirect can do it, so can we!) */
  if (!strncmp((const char*)glGetString(GL_RENDERER), "GLDirect", 8)) {
    return GLDIRECT_IS_NOT_COMPATIBLE.GetValue() + "\n";
  }
#endif

  /* Log this, so if people complain that the radar looks bad on their
   * system we can compare them: */
  glGetFloatv(GL_LINE_WIDTH_RANGE, line_range);
  glGetFloatv(GL_POINT_SIZE_RANGE, point_range);

  return std::string();
}

RageDisplay_Legacy::~RageDisplay_Legacy() { delete wind; }

void RageDisplay_Legacy::GetDisplaySpecs(DisplaySpecs& out) const {
  out.clear();
  wind->GetDisplaySpecs(out);
}

static void CheckPalettedTextures() {
  std::string error;
  do {
    if (!GLEW_EXT_paletted_texture) {
      error = "GL_EXT_paletted_texture missing";
      break;
    }

    /* Check to see if paletted textures really work. */
    GLenum gl_tex_format = kGlPixFmtInfo[RagePixelFormat_PAL].internalfmt_;
    GLenum gl_image_format = kGlPixFmtInfo[RagePixelFormat_PAL].format_;
    GLenum gl_image_type = kGlPixFmtInfo[RagePixelFormat_PAL].type_;

    int bits = 8;

    FlushGLErrors();
#define GL_CHECK_ERROR(f)                                               \
  {                                                                     \
    GLenum gl_error = glGetError();                                     \
    if (gl_error != GL_NO_ERROR) {                                      \
      error = ssprintf(f " failed (%s)", GLToString(gl_error).c_str()); \
      break;                                                            \
    }                                                                   \
  }

    glTexImage2D(
        GL_PROXY_TEXTURE_2D, 0, gl_tex_format, 16, 16, 0, gl_image_format,
        gl_image_type, nullptr);
    GL_CHECK_ERROR("glTexImage2D");

    GLuint format = 0;
    glGetTexLevelParameteriv(
        GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INTERNAL_FORMAT),
        (GLint*)&format);
    GL_CHECK_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT)");
    if (format != gl_tex_format) {
      error = ssprintf(
          "Expected format %s, got %s instead",
          GLToString(gl_tex_format).c_str(), GLToString(format).c_str());
      break;
    }

    GLubyte palette[256 * 4];
    memset(palette, 0, sizeof(palette));
    glColorTableEXT(
        GL_PROXY_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, palette);
    GL_CHECK_ERROR("glColorTableEXT");

    GLint size = 0;
    glGetTexLevelParameteriv(
        GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INDEX_SIZE_EXT), &size);
    GL_CHECK_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_INDEX_SIZE_EXT)");
    if (bits > size || size > 8) {
      error = ssprintf(
          "Expected %i-bit palette, got a %i-bit one instead", bits, int(size));
      break;
    }

    GLint real_width = 0;
    glGetColorTableParameterivEXT(
        GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_WIDTH, &real_width);
    GL_CHECK_ERROR("glGetColorTableParameterivEXT(GL_COLOR_TABLE_WIDTH)");
    if (real_width != 1 << bits) {
      error = ssprintf(
          "GL_COLOR_TABLE_WIDTH returned %i instead of %i", int(real_width),
          1 << bits);
      break;
    }

    GLint real_format = 0;
    glGetColorTableParameterivEXT(
        GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_FORMAT, &real_format);
    GL_CHECK_ERROR("glGetColorTableParameterivEXT(GL_COLOR_TABLE_FORMAT)");
    if (real_format != GL_RGBA8) {
      error = ssprintf(
          "GL_COLOR_TABLE_FORMAT returned %s instead of GL_RGBA8",
          GLToString(real_format).c_str());
      break;
    }
  } while (0);
#undef GL_CHECK_ERROR

  if (error == "") {
    return;
  }

  /* If 8-bit palettes don't work, disable them entirely--don't trust 4-bit
   * palettes if it can't even get 8-bit ones right. */
  glColorTableEXT = nullptr;
  glGetColorTableParameterivEXT = nullptr;
  LOG->Info("Paletted textures disabled: %s.", error.c_str());
}

static void CheckReversePackedPixels() {
  /* Try to create a texture. */
  FlushGLErrors();
  glTexImage2D(
      GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_BGRA,
      GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

  const GLenum gl_error = glGetError();
  if (gl_error == GL_NO_ERROR) {
    reverse_packed_pixels_works = true;
  } else {
    reverse_packed_pixels_works = false;
    LOG->Info(
        "GL_UNSIGNED_SHORT_1_5_5_5_REV failed (%s), disabled",
        GLToString(gl_error).c_str());
  }
}

void SetupExtensions() {
  const float glu_version =
      StringToFloat((const char*)gluGetString(GLU_VERSION));
  g_glu_version = std::lrint(glu_version * 10);

  glewInit();

  max_texture_units = 1;
  if (GLEW_ARB_multitexture) {
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, (GLint*)&max_texture_units);
  }

  CheckPalettedTextures();
  CheckReversePackedPixels();

  {
    GLint max_table_size = 0;
    glGetIntegerv(GL_MAX_PIXEL_MAP_TABLE, &max_table_size);
    if (max_table_size < 256) {
      /* The minimum GL_MAX_PIXEL_MAP_TABLE is 32; if it's not at least 256,
       * we can't fit a palette in it, so we can't send paletted data as input
       * for a non-paletted texture. */
      LOG->Info("GL_MAX_PIXEL_MAP_TABLE is only %d", int(max_table_size));
      color_index_table_works = false;
    } else {
      color_index_table_works = true;
    }
  }
}

bool RageDisplay_Legacy::UseOffscreenRenderTarget() {
  if (!GetActualVideoModeParams().render_offscreen_ || !TEXTUREMAN) {
    return false;
  }

  if (!offscreen_render_target_) {
    RenderTargetParam param;
    param.with_depth_buffer_ = true;
    param.with_alpha_ = true;
    param.float_ = false;
    param.width_ = GetActualVideoModeParams().width_;
    param.height_ = GetActualVideoModeParams().height_;
    RageTextureID id(
        ssprintf("FullscreenTexture%dx%d", param.width_, param.height_));
    // See if we have this texture loaded already
    // (not GC'd yet). If it exists and we try to recreate
    // it, we'll get an error
    if (TEXTUREMAN->IsTextureRegistered(id)) {
      offscreen_render_target_ =
          static_cast<RageTextureRenderTarget*>(TEXTUREMAN->LoadTexture(id));
    } else {
      offscreen_render_target_ = new RageTextureRenderTarget(id, param);
      TEXTUREMAN->RegisterTexture(id, offscreen_render_target_);
    }
  }
  return true;
}

void RageDisplay_Legacy::ResolutionChanged() {
  // LOG->Warn( "RageDisplay_Legacy::ResolutionChanged" );

  /* Clear any junk that's in the framebuffer. */
  if (BeginFrame()) {
    EndFrame();
  }

  RageDisplay::ResolutionChanged();

  if (offscreen_render_target_ && TEXTUREMAN) {
    TEXTUREMAN->UnloadTexture(offscreen_render_target_);
    offscreen_render_target_ = nullptr;
  }
}

// Return true if mode change was successful.
// bNewDeviceOut is set true if a new device was created and textures
// need to be reloaded.
std::string RageDisplay_Legacy::TryVideoMode(
    const VideoModeParams& p, bool& new_device_out) {
  // LOG->Warn( "RageDisplay_Legacy::TryVideoMode( %d, %d, %d, %d, %d, %d )",
  // p.windowed, p.width, p.height, p.bpp, p.rate, p.vsync );

  std::string err;
  err = wind->TryVideoMode(p, new_device_out);
  if (err != "") {
    return err;  // failed to set video mode
  }

  /* Now that we've initialized, we can search for extensions.  Do this before
   * InvalidateObjects, since AllocateBuffers needs it. */
  SetupExtensions();

  if (new_device_out) {
    /* We have a new OpenGL context, so we have to tell our textures that
     * their OpenGL texture number is invalid. */
    if (TEXTUREMAN) {
      TEXTUREMAN->InvalidateTextures();
    }

    /* Delete all render targets.  They may have associated resources other than
     * the texture itself. */
    for (std::pair<const uintptr_t, RenderTarget*>& rt : map_render_targets) {
      delete rt.second;
    }
    map_render_targets.clear();

    /* Recreate all vertex buffers. */
    InvalidateObjects();

    InitShaders();
  }

// I'm not sure this is correct -Colby
#if defined(WINDOWS)
  /* Set vsync the Windows way, if we can.  (What other extensions are there
   * to do this, for other archs?) */
  if (wglewIsSupported("WGL_EXT_swap_control")) {
    wglSwapIntervalEXT(p.vsync_);
  } else {
    return std::string(
        "The WGL_EXT_swap_control extension is not supported on your "
        "computer.");
  }
#endif

  ResolutionChanged();

  return std::string();  // successfully set mode
}

// https://stackoverflow.com/q/6594214
// Since glGetIntegerv won't initialize the variable if it fails, account for
// failure. We initialize it to -1, so we know if it failed. Failure here is
// extremely uncommon, but was first reported on a setup using an AMD RX 6600
// series GPU & late 2024 driver.
int RageDisplay_Legacy::GetMaxTextureSize() const {
  GLint size = -1;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);

  if (size <= 0) {
    LOG->Warn(
        "RageDisplay_Legacy::GetMaxTextureSize: glGetIntegerv %s %d",
        (size == -1) ? "failed" : "returned invalid value", size);
    return 2048;
  }

  return size;
}

bool RageDisplay_Legacy::BeginFrame() {
  /* We do this in here, rather than ResolutionChanged, or we won't update the
   * viewport for the concurrent rendering context. */
  int width = wind->GetActualVideoModeParams().window_width_;
  int height = wind->GetActualVideoModeParams().window_height_;

  glViewport(0, 0, width, height);

  glClearColor(0, 0, 0, 0);
  SetZWrite(true);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  bool begin_frame = RageDisplay::BeginFrame();
  if (begin_frame && UseOffscreenRenderTarget()) {
    offscreen_render_target_->BeginRenderingTo(false);
  }

  return begin_frame;
}

void RageDisplay_Legacy::EndFrame() {
  if (UseOffscreenRenderTarget()) {
    offscreen_render_target_->FinishRenderingTo();
    Sprite fullscreen_sprite;
    // We've got a hold of this, don't want sprite deleting it when
    // it's deleted
    offscreen_render_target_->ref_count_++;
    fullscreen_sprite.SetTexture(offscreen_render_target_);
    fullscreen_sprite.SetHorizAlign(kAlignLeft);
    fullscreen_sprite.SetVertAlign(kAlignTop);
    CameraPushMatrix();
    LoadMenuPerspective(
        0, GetActualVideoModeParams().width_,
        GetActualVideoModeParams().height_,
        static_cast<float>(GetActualVideoModeParams().width_) / 2.f,
        static_cast<float>(GetActualVideoModeParams().height_) / 2.f);
    fullscreen_sprite.Draw();
    CameraPopMatrix();
  }

  FrameLimitBeforeVsync();
  wind->SwapBuffers();
  FrameLimitAfterVsync();

  // Some would advise against glFinish(), ever. Those people don't realize
  // the degree of freedom GL hosts are permitted in queueing commands.
  // If left to its own devices, the host could lag behind several frames' worth
  // of commands.
  // glFlush() only forces the host to not wait to execute all commands
  // sent so far; it does NOT block on those commands until they finish.
  // glFinish() blocks. We WANT to block. Why? This puts the engine state
  // reflected by the next frame as close as possible to the on-screen
  // appearance of that frame.
  glFinish();

  wind->Update();

  RageDisplay::EndFrame();
}

RageSurface* RageDisplay_Legacy::CreateScreenshot() {
  int width = wind->GetActualVideoModeParams().width_;
  int height = wind->GetActualVideoModeParams().height_;

  RageSurface* image = nullptr;
  if (offscreen_render_target_) {
    RageSurface* raw = GetTexture(offscreen_render_target_->GetTexHandle());
    image = CreateSurface(
        offscreen_render_target_->GetImageWidth(),
        offscreen_render_target_->GetImageHeight(), raw->fmt_.bits_per_pixel_,
        raw->fmt_.rmask_, raw->fmt_.gmask_, raw->fmt_.bmask_, raw->fmt_.amask_);
    RageSurfaceUtils::Blit(raw, image);
    delete raw;
  } else {
    const RagePixelFormatDesc& desc = pixel_format_desc[RagePixelFormat_RGBA8];
    image = CreateSurface(
        width, height, desc.bpp_, desc.masks_[0], desc.masks_[1],
        desc.masks_[2], 0);

    DebugFlushGLErrors();

    // TODO: revisit for MacOS, where backbuffer size can be less than window
    // size
    glReadBuffer(GL_FRONT);
    DebugAssertNoGLError();

    glReadPixels(
        0, 0, wind->GetActualVideoModeParams().width_,
        wind->GetActualVideoModeParams().height_, GL_RGBA, GL_UNSIGNED_BYTE,
        image->pixels_);
    DebugAssertNoGLError();

    RageSurfaceUtils::FlipVertically(image);
  }

  return image;
}

RageSurface* RageDisplay_Legacy::GetTexture(uintptr_t texture) {
  if (texture == 0) {
    return nullptr;  // XXX
  }

  FlushGLErrors();

  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
  GLint height, width, alpha_bits;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(
      GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &alpha_bits);
  int format = alpha_bits ? RagePixelFormat_RGBA8 : RagePixelFormat_RGB8;

  const RagePixelFormatDesc& desc = pixel_format_desc[format];
  RageSurface* image = CreateSurface(
      width, height, desc.bpp_, desc.masks_[0], desc.masks_[1], desc.masks_[2],
      desc.masks_[3]);

  glGetTexImage(
      GL_TEXTURE_2D, 0, kGlPixFmtInfo[format].format_, GL_UNSIGNED_BYTE,
      image->pixels_);
  AssertNoGLError();

  return image;
}

ActualVideoModeParams RageDisplay_Legacy::GetActualVideoModeParams() const {
  return wind->GetActualVideoModeParams();
}

static void SetupVertices(const RageSpriteVertex v[], int num_verts) {
  static const bool kInterleaveColor =
      !!GLEW_ARB_vertex_array_bgra || !!GLEW_EXT_vertex_array_bgra;
  if (kInterleaveColor) {
    const GLsizei stride = sizeof(RageSpriteVertex);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, &v[0].p_);

    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, stride, &v[0].c_);

    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, &v[0].t_);

    if (GLEW_ARB_multitexture) {
      glClientActiveTextureARB(GL_TEXTURE1_ARB);
      glEnableClientState(GL_TEXTURE_COORD_ARRAY);
      glTexCoordPointer(2, GL_FLOAT, stride, &v[0].t_);
      glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }

    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, stride, &v[0].n_);
    return;
  }

  // Convert from BGRA -> RGBA for drivers without GL_BGRA vertex arrays.
  static float *vertex, *texture, *normal;
  static GLubyte* color;
  static int size = 0;
  if (num_verts > size) {
    size = num_verts;
    delete[] vertex;
    delete[] color;
    delete[] texture;
    delete[] normal;
    vertex = new float[size * 3];
    color = new GLubyte[size * 4];
    texture = new float[size * 2];
    normal = new float[size * 3];
  }

  for (unsigned i = 0; i < unsigned(num_verts); ++i) {
    vertex[i * 3 + 0] = v[i].p_[0];
    vertex[i * 3 + 1] = v[i].p_[1];
    vertex[i * 3 + 2] = v[i].p_[2];
    color[i * 4 + 0] = v[i].c_.r_;
    color[i * 4 + 1] = v[i].c_.g_;
    color[i * 4 + 2] = v[i].c_.b_;
    color[i * 4 + 3] = v[i].c_.a_;
    texture[i * 2 + 0] = v[i].t_[0];
    texture[i * 2 + 1] = v[i].t_[1];
    normal[i * 3 + 0] = v[i].n_[0];
    normal[i * 3 + 1] = v[i].n_[1];
    normal[i * 3 + 2] = v[i].n_[2];
  }
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3, GL_FLOAT, 0, vertex);

  glEnableClientState(GL_COLOR_ARRAY);
  glColorPointer(4, GL_UNSIGNED_BYTE, 0, color);

  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL_FLOAT, 0, texture);

  if (GLEW_ARB_multitexture) {
    glClientActiveTextureARB(GL_TEXTURE1_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, texture);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
  }

  glEnableClientState(GL_NORMAL_ARRAY);
  glNormalPointer(GL_FLOAT, 0, normal);
}

void RageDisplay_Legacy::SendCurrentMatrices() {
  RageMatrix projection;
  RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());

  if (invert_y) {
    RageMatrix flip;
    RageMatrixScale(&flip, +1, -1, +1);
    RageMatrixMultiply(&projection, &flip, &projection);
  }
  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf((const float*)&projection);

  // OpenGL has just "modelView", whereas D3D has "world" and "view"
  RageMatrix model_view;
  RageMatrixMultiply(&model_view, GetViewTop(), GetWorldTop());
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf((const float*)&model_view);

  glMatrixMode(GL_TEXTURE);
  glLoadMatrixf((const float*)GetTextureTop());
}

class RageCompiledGeometrySWOGL : public RageCompiledGeometry {
 public:
  void Allocate(const std::vector<msMesh>& meshes) {
    /* Always allocate at least 1 entry, so &x[0] is valid. */
    const unsigned int vertices_count =
        std::max<unsigned int>(1u, GetTotalVertices());
    const unsigned int triangles_count =
        std::max<unsigned int>(1u, GetTotalTriangles());

    position_.resize(vertices_count);
    texture_.resize(vertices_count);
    normal_.resize(vertices_count);
    tex_matrix_scale_.resize(vertices_count);
    triangles_.resize(triangles_count);
  }
  void Change(const std::vector<msMesh>& meshes) {
    for (unsigned i = 0; i < meshes.size(); i++) {
      const MeshInfo& mesh_info = mesh_info_[i];
      const msMesh& mesh = meshes[i];
      const std::vector<RageModelVertex>& vertices = mesh.vertices_;
      const std::vector<msTriangle>& triangles = mesh.triangles_;

      for (unsigned j = 0; j < vertices.size(); j++) {
        position_[mesh_info.vertex_start_ + j] = vertices[j].p_;
        texture_[mesh_info.vertex_start_ + j] = vertices[j].t_;
        normal_[mesh_info.vertex_start_ + j] = vertices[j].n_;
        tex_matrix_scale_[mesh_info.vertex_start_ + j] =
            vertices[j].texture_matrix_scale_;
      }

      for (unsigned j = 0; j < triangles.size(); j++) {
        for (unsigned k = 0; k < 3; k++) {
          int vertex_index_in_vbo =
              mesh_info.vertex_start_ + triangles[j].vertex_indices_[k];
          triangles_[mesh_info.triangle_start_ + j].vertex_indices_[k] =
              (uint16_t)vertex_index_in_vbo;
        }
      }
    }
  }
  void Draw(int mesh_index) const {
    TurnOffHardwareVBO();

    const MeshInfo& mesh_info = mesh_info_[mesh_index];

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, &position_[0]);

    glDisableClientState(GL_COLOR_ARRAY);

    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, &texture_[0]);

    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, 0, &normal_[0]);

    if (mesh_info.needs_texture_matrix_scale_) {
      // Kill the texture translation.
      // XXX: Change me to scale the translation by the TextureTranslationScale
      // of the first vertex.
      RageMatrix mat;
      glGetFloatv(GL_TEXTURE_MATRIX, (float*)mat);

      /*
      for( int i=0; i<4; i++ )
      {
              std::string s;
              for( int j=0; j<4; j++ )
                      s += ssprintf( "%f ", mat.m[i][j] );
              LOG->Trace( s );
      }
      */

      mat.m_[3][0] = 0;
      mat.m_[3][1] = 0;
      mat.m_[3][2] = 0;

      glMatrixMode(GL_TEXTURE);
      glLoadMatrixf((const float*)mat);
    }

    glDrawElements(
        GL_TRIANGLES, mesh_info.triangle_count_ * 3, GL_UNSIGNED_SHORT,
        &triangles_[0] + mesh_info.triangle_start_);
  }

 protected:
  std::vector<RageVector3> position_;
  std::vector<RageVector2> texture_;
  std::vector<RageVector3> normal_;
  std::vector<msTriangle> triangles_;
  std::vector<RageVector2> tex_matrix_scale_;
};

class InvalidateObject;
static std::set<InvalidateObject*> invalidate_list;
class InvalidateObject {
 public:
  InvalidateObject() { invalidate_list.insert(this); }
  virtual ~InvalidateObject() { invalidate_list.erase(this); }
  virtual void Invalidate() = 0;
};

static void InvalidateObjects() {
  for (InvalidateObject* it : invalidate_list) {
    it->Invalidate();
  }
}

class RageCompiledGeometryHWOGL : public RageCompiledGeometrySWOGL,
                                  public InvalidateObject {
 protected:
  // vertex buffer object names
  GLuint positions_;
  GLuint texture_coords_;
  GLuint normals_;
  GLuint n_triangles_;
  GLuint texture_matrix_scale_;

  void AllocateBuffers();
  void UploadData();

 public:
  RageCompiledGeometryHWOGL();
  ~RageCompiledGeometryHWOGL();

  /* This is called when our OpenGL context is invalidated. */
  void Invalidate();

  void Allocate(const std::vector<msMesh>& meshes);
  void Change(const std::vector<msMesh>& meshes);
  void Draw(int mesh_index) const;
};

RageCompiledGeometryHWOGL::RageCompiledGeometryHWOGL() {
  positions_ = 0;
  texture_coords_ = 0;
  normals_ = 0;
  n_triangles_ = 0;
  texture_matrix_scale_ = 0;

  AllocateBuffers();
}

RageCompiledGeometryHWOGL::~RageCompiledGeometryHWOGL() {
  DebugFlushGLErrors();

  glDeleteBuffersARB(1, &positions_);
  DebugAssertNoGLError();
  glDeleteBuffersARB(1, &texture_coords_);
  DebugAssertNoGLError();
  glDeleteBuffersARB(1, &normals_);
  DebugAssertNoGLError();
  glDeleteBuffersARB(1, &n_triangles_);
  DebugAssertNoGLError();
  glDeleteBuffersARB(1, &texture_matrix_scale_);
  DebugAssertNoGLError();
}

void RageCompiledGeometryHWOGL::AllocateBuffers() {
  DebugFlushGLErrors();

  if (!positions_) {
    glGenBuffersARB(1, &positions_);
    DebugAssertNoGLError();
  }

  if (!texture_coords_) {
    glGenBuffersARB(1, &texture_coords_);
    DebugAssertNoGLError();
  }

  if (!normals_) {
    glGenBuffersARB(1, &normals_);
    DebugAssertNoGLError();
  }

  if (!n_triangles_) {
    glGenBuffersARB(1, &n_triangles_);
    DebugAssertNoGLError();
  }

  if (!texture_matrix_scale_) {
    glGenBuffersARB(1, &texture_matrix_scale_);
    DebugAssertNoGLError();
  }
}

void RageCompiledGeometryHWOGL::UploadData() {
  DebugFlushGLErrors();

  glBindBufferARB(GL_ARRAY_BUFFER_ARB, positions_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector3),
      &position_[0], GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ARRAY_BUFFER_ARB, texture_coords_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector2),
      &texture_[0], GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector3),
      &normal_[0], GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, n_triangles_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ELEMENT_ARRAY_BUFFER_ARB, GetTotalTriangles() * sizeof(msTriangle),
      &triangles_[0], GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  if (any_needs_texture_matrix_scale_) {
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, texture_matrix_scale_);
    DebugAssertNoGLError();
    glBufferDataARB(
        GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector2),
        &tex_matrix_scale_[0], GL_STATIC_DRAW_ARB);
    DebugAssertNoGLError();
  }
}

void RageCompiledGeometryHWOGL::Invalidate() {
  /* Our vertex buffers no longer exist.  Reallocate and reupload. */
  positions_ = 0;
  texture_coords_ = 0;
  normals_ = 0;
  n_triangles_ = 0;
  texture_matrix_scale_ = 0;
  AllocateBuffers();
  UploadData();
}

void RageCompiledGeometryHWOGL::Allocate(const std::vector<msMesh>& meshes) {
  DebugFlushGLErrors();

  RageCompiledGeometrySWOGL::Allocate(meshes);
  glBindBufferARB(GL_ARRAY_BUFFER_ARB, positions_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector3), nullptr,
      GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ARRAY_BUFFER_ARB, texture_coords_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector2), nullptr,
      GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector3), nullptr,
      GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, n_triangles_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ELEMENT_ARRAY_BUFFER_ARB, GetTotalTriangles() * sizeof(msTriangle),
      nullptr, GL_STATIC_DRAW_ARB);
  DebugAssertNoGLError();

  glBindBufferARB(GL_ARRAY_BUFFER_ARB, texture_matrix_scale_);
  DebugAssertNoGLError();
  glBufferDataARB(
      GL_ARRAY_BUFFER_ARB, GetTotalVertices() * sizeof(RageVector2), nullptr,
      GL_STATIC_DRAW_ARB);
}

void RageCompiledGeometryHWOGL::Change(const std::vector<msMesh>& meshes) {
  RageCompiledGeometrySWOGL::Change(meshes);

  UploadData();
}

void RageCompiledGeometryHWOGL::Draw(int mesh_index) const {
  DebugFlushGLErrors();

  const MeshInfo& mesh_info = mesh_info_[mesh_index];
  if (!mesh_info.vertex_count_ || !mesh_info.triangle_count_) {
    return;
  }

  glEnableClientState(GL_VERTEX_ARRAY);
  DebugAssertNoGLError();
  glBindBufferARB(GL_ARRAY_BUFFER_ARB, positions_);
  DebugAssertNoGLError();
  glVertexPointer(3, GL_FLOAT, 0, nullptr);
  DebugAssertNoGLError();

  glDisableClientState(GL_COLOR_ARRAY);
  DebugAssertNoGLError();

  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  DebugAssertNoGLError();
  glBindBufferARB(GL_ARRAY_BUFFER_ARB, texture_coords_);
  DebugAssertNoGLError();
  glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
  DebugAssertNoGLError();

  // TRICKY:  Don't bind and send normals if lighting is disabled.  This
  // will save some effort transforming these values.
  // XXX: We should keep track of these ourself and avoid glGet*()
  GLboolean lighting;
  glGetBooleanv(GL_LIGHTING, &lighting);
  GLboolean texture_gen_s;
  glGetBooleanv(GL_TEXTURE_GEN_S, &texture_gen_s);
  GLboolean texture_gen_t;
  glGetBooleanv(GL_TEXTURE_GEN_T, &texture_gen_t);

  if (lighting || texture_gen_s || texture_gen_t) {
    glEnableClientState(GL_NORMAL_ARRAY);
    DebugAssertNoGLError();
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals_);
    DebugAssertNoGLError();
    glNormalPointer(GL_FLOAT, 0, nullptr);
    DebugAssertNoGLError();
  } else {
    glDisableClientState(GL_NORMAL_ARRAY);
    DebugAssertNoGLError();
  }

  if (mesh_info.needs_texture_matrix_scale_) {
    if (texture_matrix_shader != 0) {
      /* If we're using texture matrix scales, set up that buffer, too, and
       * enable the vertex shader.  This shader doesn't support all OpenGL
       * state, so only enable it if we're using it. */
      glEnableVertexAttribArrayARB(attrib_texture_matrix_scale);
      DebugAssertNoGLError();
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, texture_matrix_scale_);
      DebugAssertNoGLError();
      glVertexAttribPointerARB(
          attrib_texture_matrix_scale, 2, GL_FLOAT, false, 0, nullptr);
      DebugAssertNoGLError();

      glUseProgramObjectARB(texture_matrix_shader);
      DebugAssertNoGLError();
    } else {
      // Kill the texture translation.
      // XXX: Change me to scale the translation by the TextureTranslationScale
      // of the first vertex.
      RageMatrix mat;
      glGetFloatv(GL_TEXTURE_MATRIX, (float*)mat);

      /*
      for( int i=0; i<4; i++ )
      {
              std::string s;
              for( int j=0; j<4; j++ )
                      s += ssprintf( "%f ", mat.m[i][j] );
              LOG->Trace( s );
      }
      */

      mat.m_[3][0] = 0;
      mat.m_[3][1] = 0;
      mat.m_[3][2] = 0;

      glMatrixMode(GL_TEXTURE);
      glLoadMatrixf((const float*)mat);
      DebugAssertNoGLError();
    }
  }

  glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, n_triangles_);
  DebugAssertNoGLError();

#define BUFFER_OFFSET(o) ((char*)(o))

  ASSERT(glDrawRangeElements != nullptr);
  glDrawRangeElements(
      GL_TRIANGLES,
      mesh_info.vertex_start_,  // minimum array index contained in indices
      mesh_info.vertex_start_ + mesh_info.vertex_count_ - 1,
      // maximum array index contained in indices
      mesh_info.triangle_count_ * 3,  // number of elements to be rendered
      GL_UNSIGNED_SHORT,
      BUFFER_OFFSET(mesh_info.triangle_start_ * sizeof(msTriangle)));
  DebugAssertNoGLError();

  if (mesh_info.needs_texture_matrix_scale_ && texture_matrix_shader != 0) {
    glDisableVertexAttribArrayARB(attrib_texture_matrix_scale);
    glUseProgramObjectARB(0);
  }
}

RageCompiledGeometry* RageDisplay_Legacy::CreateCompiledGeometry() {
  if (GLEW_ARB_vertex_buffer_object) {
    return new RageCompiledGeometryHWOGL;
  } else {
    return new RageCompiledGeometrySWOGL;
  }
}

void RageDisplay_Legacy::DeleteCompiledGeometry(RageCompiledGeometry* p) {
  delete p;
}

void RageDisplay_Legacy::DrawQuadsInternal(
    const RageSpriteVertex v[], int num_verts) {
  TurnOffHardwareVBO();
  SendCurrentMatrices();

  SetupVertices(v, num_verts);
  glDrawArrays(GL_QUADS, 0, num_verts);
}

void RageDisplay_Legacy::DrawQuadStripInternal(
    const RageSpriteVertex v[], int num_verts) {
  TurnOffHardwareVBO();
  SendCurrentMatrices();

  SetupVertices(v, num_verts);
  glDrawArrays(GL_QUAD_STRIP, 0, num_verts);
}

void RageDisplay_Legacy::DrawSymmetricQuadStripInternal(
    const RageSpriteVertex v[], int num_verts) {
  int num_pieces = (num_verts - 3) / 3;
  int num_triangles = num_pieces * 4;
  int num_indices = num_triangles * 3;

  // make a temporary index buffer
  static std::vector<uint16_t> indices;
  unsigned old_size = indices.size();
  unsigned new_size = std::max(old_size, (unsigned)num_indices);
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

  TurnOffHardwareVBO();
  SendCurrentMatrices();

  SetupVertices(v, num_verts);
  glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, &indices[0]);
}

void RageDisplay_Legacy::DrawFanInternal(
    const RageSpriteVertex v[], int num_verts) {
  TurnOffHardwareVBO();
  SendCurrentMatrices();

  SetupVertices(v, num_verts);
  glDrawArrays(GL_TRIANGLE_FAN, 0, num_verts);
}

void RageDisplay_Legacy::DrawStripInternal(
    const RageSpriteVertex v[], int num_verts) {
  TurnOffHardwareVBO();
  SendCurrentMatrices();

  SetupVertices(v, num_verts);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, num_verts);
}

void RageDisplay_Legacy::DrawTrianglesInternal(
    const RageSpriteVertex v[], int num_verts) {
  TurnOffHardwareVBO();
  SendCurrentMatrices();

  SetupVertices(v, num_verts);
  glDrawArrays(GL_TRIANGLES, 0, num_verts);
}

void RageDisplay_Legacy::DrawCompiledGeometryInternal(
    const RageCompiledGeometry* p, int mesh_index) {
  TurnOffHardwareVBO();
  SendCurrentMatrices();

  p->Draw(mesh_index);
}

void RageDisplay_Legacy::DrawLineStripInternal(
    const RageSpriteVertex v[], int num_verts, float line_width) {
  TurnOffHardwareVBO();

  if (!GetActualVideoModeParams().smooth_lines_) {
    /* Fall back on the generic polygon-based line strip. */
    RageDisplay::DrawLineStripInternal(v, num_verts, line_width);
    return;
  }

  SendCurrentMatrices();

  /* Draw a nice AA'd line loop.  One problem with this is that point and line
   * sizes don't always precisely match, which doesn't look quite right.
   * It's worth it for the AA, though. */
  glEnable(GL_LINE_SMOOTH);

  /* fLineWidth is in units relative to object space, but OpenGL line and point
   * sizes are in raster units (actual pixels).  Scale the line width by the
   * average ratio; if object space is 640x480, and we have a 1280x960 window,
   * we'll double the width. */
  {
    const RageMatrix* p_mat = GetProjectionTop();
    float w = 2 / p_mat->m_[0][0];
    float h = -2 / p_mat->m_[1][1];
    float width_val = float(wind->GetActualVideoModeParams().width_) / w;
    float height_val = float(wind->GetActualVideoModeParams().height_) / h;
    line_width *= (width_val + height_val) / 2;
  }

  /* Clamp the width to the hardware max for both lines and points (whichever
   * is more restrictive). */
  line_width = std::clamp(line_width, line_range[0], line_range[1]);
  line_width = std::clamp(line_width, point_range[0], point_range[1]);

  /* Hmm.  The granularity of lines and points might be different; for example,
   * if lines are .5 and points are .25, we might want to snap the width to the
   * nearest .5, so the hardware doesn't snap them to different sizes.  Does it
   * matter? */
  glLineWidth(line_width);

  /* Draw the line loop: */
  SetupVertices(v, num_verts);
  glDrawArrays(GL_LINE_STRIP, 0, num_verts);
  StatsAddVerts(num_verts);

  glDisable(GL_LINE_SMOOTH);

  /* Round off the corners.  This isn't perfect; the point is sometimes a little
   * larger than the line, causing a small bump on the edge.  Not sure how to
   * fix that. */
  glPointSize(line_width);

  /* Hack: if the points will all be the same, we don't want to draw
   * any points at all, since there's nothing to connect.  That'll happen
   * if both scale factors in the matrix are ~0.  (Actually, I think
   * it's true if two of the three scale factors are ~0, but we don't
   * use this for anything 3d at the moment anyway ...)  This is needed
   * because points aren't scaled like regular polys--a zero-size point
   * will still be drawn. */
  RageMatrix mat;
  glGetFloatv(GL_MODELVIEW_MATRIX, (float*)mat);

  if (mat.m_[0][0] < 1e-5 && mat.m_[1][1] < 1e-5) {
    return;
  }

  glEnable(GL_POINT_SMOOTH);

  SetupVertices(v, num_verts);
  glDrawArrays(GL_POINTS, 0, num_verts);
  StatsAddVerts(num_verts);

  glDisable(GL_POINT_SMOOTH);
}

static bool SetTextureUnit(TextureUnit tu) {
  // If multitexture isn't supported, ignore all textures except for 0.
  if (!GLEW_ARB_multitexture && tu != TextureUnit_1) {
    return false;
  }

  if ((int)tu > max_texture_units) {
    return false;
  }
  glActiveTextureARB(enum_add2(GL_TEXTURE0_ARB, tu));
  return true;
}

void RageDisplay_Legacy::ClearAllTextures() {
  FOREACH_ENUM(TextureUnit, i)
  SetTexture(i, 0);

  // HACK:  Reset the active texture to 0.
  // TODO:  Change all texture functions to take a stage number.
  if (GLEW_ARB_multitexture) {
    glActiveTextureARB(GL_TEXTURE0_ARB);
  }
}

int RageDisplay_Legacy::GetNumTextureUnits() {
  if (GLEW_ARB_multitexture) {
    return 1;
  } else {
    return max_texture_units;
  }
}

void RageDisplay_Legacy::SetTexture(TextureUnit tu, uintptr_t texture) {
  if (!SetTextureUnit(tu)) {
    return;
  }

  if (texture) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
  } else {
    glDisable(GL_TEXTURE_2D);
  }
}

void RageDisplay_Legacy::SetTextureMode(TextureUnit tu, TextureMode tm) {
  if (!SetTextureUnit(tu)) {
    return;
  }

  switch (tm) {
    case TextureMode_Modulate:
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      break;
    case TextureMode_Add:
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
      break;
    case TextureMode_Glow:
      // the below function is glowmode,brighten:
      if (!GLEW_ARB_texture_env_combine && !GLEW_EXT_texture_env_combine) {
        /* This is changing blend state, instead of texture state, which
         * isn't great, but it's better than doing nothing. */
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        return;
      }

      // and this is glowmode,whiten:
      // Source color is the diffuse color only:
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
      glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_COMBINE_RGB_EXT), GL_REPLACE);
      glTexEnvi(
          GL_TEXTURE_ENV, GLenum(GL_SOURCE0_RGB_EXT), GL_PRIMARY_COLOR_EXT);

      // Source alpha is texture alpha * diffuse alpha:
      glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_COMBINE_ALPHA_EXT), GL_MODULATE);
      glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_OPERAND0_ALPHA_EXT), GL_SRC_ALPHA);
      glTexEnvi(
          GL_TEXTURE_ENV, GLenum(GL_SOURCE0_ALPHA_EXT), GL_PRIMARY_COLOR_EXT);
      glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_OPERAND1_ALPHA_EXT), GL_SRC_ALPHA);
      glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE1_ALPHA_EXT), GL_TEXTURE);
      break;
    default:
      break;
  }
}

void RageDisplay_Legacy::SetTextureFiltering(TextureUnit tu, bool b) {
  glTexParameteri(
      GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, b ? GL_LINEAR : GL_NEAREST);

  GLint min_filter;
  if (b) {
    GLint width1 = -1;
    GLint width2 = -1;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width1);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &width2);
    if (width1 > 1 && width2 != 0) {
      /* Mipmaps are enabled. */
      if (wind->GetActualVideoModeParams().trilinear_filtering_) {
        min_filter = GL_LINEAR_MIPMAP_LINEAR;
      } else {
        min_filter = GL_LINEAR_MIPMAP_NEAREST;
      }
    } else {
      min_filter = GL_LINEAR;
    }
  } else {
    min_filter = GL_NEAREST;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
}

void RageDisplay_Legacy::SetEffectMode(EffectMode effect) {
  if (!GLEW_ARB_fragment_program || !GLEW_ARB_shading_language_100 ||
      !GLEW_ARB_shader_objects) {
    return;
  }

  GLhandleARB shader = 0;
  switch (effect) {
    case EffectMode_Normal:
      shader = 0;
      break;
    case EffectMode_Unpremultiply:
      shader = unpremultiply_shader;
      break;
    case EffectMode_ColorBurn:
      shader = color_burn_shader;
      break;
    case EffectMode_ColorDodge:
      shader = color_dodge_shader;
      break;
    case EffectMode_VividLight:
      shader = vivid_light_shader;
      break;
    case EffectMode_HardMix:
      shader = hard_mix_shader;
      break;
    case EffectMode_Overlay:
      shader = overlay_shader;
      break;
    case EffectMode_Screen:
      shader = screen_shader;
      break;
    case EffectMode_YUYV422:
      shader = yuyv422_shader;
      break;
    case EffectMode_DistanceField:
      shader = distance_field_shader;
    default:
      break;
  }

  DebugFlushGLErrors();
  glUseProgramObjectARB(shader);
  if (shader == 0) {
    return;
  }
  GLint texture1 = glGetUniformLocationARB(shader, "Texture1");
  GLint texture2 = glGetUniformLocationARB(shader, "Texture2");
  glUniform1iARB(texture1, 0);
  glUniform1iARB(texture2, 1);

  if (effect == EffectMode_YUYV422) {
    GLint texture_width_uniform =
        glGetUniformLocationARB(shader, "TextureWidth");
    GLint width;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glUniform1iARB(texture_width_uniform, width);
  }

  DebugAssertNoGLError();
}

bool RageDisplay_Legacy::IsEffectModeSupported(EffectMode effect) {
  switch (effect) {
    case EffectMode_Normal:
      return true;
    case EffectMode_Unpremultiply:
      return unpremultiply_shader != 0;
    case EffectMode_ColorBurn:
      return color_burn_shader != 0;
    case EffectMode_ColorDodge:
      return color_dodge_shader != 0;
    case EffectMode_VividLight:
      return vivid_light_shader != 0;
    case EffectMode_HardMix:
      return hard_mix_shader != 0;
    case EffectMode_Overlay:
      return overlay_shader != 0;
    case EffectMode_Screen:
      return screen_shader != 0;
    case EffectMode_YUYV422:
      return yuyv422_shader != 0;
    case EffectMode_DistanceField:
      return distance_field_shader != 0;
    default:
      return false;
  }
}

void RageDisplay_Legacy::SetBlendMode(BlendMode mode) {
  glEnable(GL_BLEND);

  if (glBlendEquation != nullptr) {
    if (mode == BLEND_INVERT_DEST) {
      glBlendEquation(GL_FUNC_SUBTRACT);
    } else if (mode == BLEND_SUBTRACT) {
      glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    } else {
      glBlendEquation(GL_FUNC_ADD);
    }
  }

  int source_rgb, dest_rgb;
  int source_alpha = GL_ONE, dest_alpha = GL_ONE_MINUS_SRC_ALPHA;
  switch (mode) {
    case BLEND_NORMAL:
      source_rgb = GL_SRC_ALPHA;
      dest_rgb = GL_ONE_MINUS_SRC_ALPHA;
      break;
    case BLEND_ADD:
      source_rgb = GL_SRC_ALPHA;
      dest_rgb = GL_ONE;
      break;
    case BLEND_SUBTRACT:
      source_rgb = GL_SRC_ALPHA;
      dest_rgb = GL_ONE_MINUS_SRC_ALPHA;
      break;
    case BLEND_MODULATE:
      source_rgb = GL_ZERO;
      dest_rgb = GL_SRC_COLOR;
      break;
    case BLEND_COPY_SRC:
      source_rgb = GL_ONE;
      dest_rgb = GL_ZERO;
      source_alpha = GL_ONE;
      dest_alpha = GL_ZERO;
      break;
    case BLEND_ALPHA_MASK:
      source_rgb = GL_ZERO;
      dest_rgb = GL_ONE;
      source_alpha = GL_ZERO;
      dest_alpha = GL_SRC_ALPHA;
      break;
    case BLEND_ALPHA_KNOCK_OUT:
      source_rgb = GL_ZERO;
      dest_rgb = GL_ONE;
      source_alpha = GL_ZERO;
      dest_alpha = GL_ONE_MINUS_SRC_ALPHA;
      break;
    case BLEND_ALPHA_MULTIPLY:
      source_rgb = GL_SRC_ALPHA;
      dest_rgb = GL_ZERO;
      break;
    case BLEND_WEIGHTED_MULTIPLY:
      /* output = 2*(dst*src).  0.5,0.5,0.5 is identity; darker colors darken
       * the image, and brighter colors lighten the image. */
      source_rgb = GL_DST_COLOR;
      dest_rgb = GL_SRC_COLOR;
      break;
    case BLEND_INVERT_DEST:
      /* out = src - dst.  The source color should almost always be #FFFFFF, to
       * make it "1 - dst". */
      source_rgb = GL_ONE;
      dest_rgb = GL_ONE;
      break;
    case BLEND_NO_EFFECT:
      source_rgb = GL_ZERO;
      dest_rgb = GL_ONE;
      source_alpha = GL_ZERO;
      dest_alpha = GL_ONE;
      break;
      DEFAULT_FAIL(mode);
  }

  if (GLEW_EXT_blend_equation_separate) {
    glBlendFuncSeparateEXT(source_rgb, dest_rgb, source_alpha, dest_alpha);
  } else {
    glBlendFunc(source_rgb, dest_rgb);
  }
}

bool RageDisplay_Legacy::IsZWriteEnabled() const {
  bool a;
  glGetBooleanv(GL_DEPTH_WRITEMASK, (unsigned char*)&a);
  return a;
}

void RageDisplay_Legacy::ClearZBuffer() {
  bool write = IsZWriteEnabled();
  SetZWrite(true);
  glClear(GL_DEPTH_BUFFER_BIT);
  SetZWrite(write);
}

void RageDisplay_Legacy::SetZWrite(bool b) { glDepthMask(b); }

void RageDisplay_Legacy::SetZBias(float f) {
  float near_val = SCALE(f, 0.0f, 1.0f, 0.05f, 0.0f);
  float far_val = SCALE(f, 0.0f, 1.0f, 1.0f, 0.95f);

  glDepthRange(near_val, far_val);
}

void RageDisplay_Legacy::SetZTestMode(ZTestMode mode) {
  glEnable(GL_DEPTH_TEST);
  switch (mode) {
    case ZTEST_OFF:
      glDepthFunc(GL_ALWAYS);
      break;
    case ZTEST_WRITE_ON_PASS:
      glDepthFunc(GL_LEQUAL);
      break;
    case ZTEST_WRITE_ON_FAIL:
      glDepthFunc(GL_GREATER);
      break;
    default:
      FAIL_M(ssprintf("Invalid ZTestMode: %i", mode));
  }
}

void RageDisplay_Legacy::SetTextureWrapping(TextureUnit tu, bool b) {
  /* This should be per-texture-unit state, but it's per-texture state in
   * OpenGl, so we'll behave incorrectly if the same texture is used in more
   * than one texture unit simultaneously with different wrapping. */
  SetTextureUnit(tu);

  GLenum mode = b ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
}

void RageDisplay_Legacy::SetMaterial(
    const RageColor& emissive, const RageColor& ambient,
    const RageColor& diffuse, const RageColor& specular, float shininess) {
  // TRICKY:  If lighting is off, then setting the material
  // will have no effect.  Even if lighting is off, we still
  // want Models to have basic color and transparency.
  // We can do this fake lighting by setting the vertex color.
  // XXX: unintended: SetLighting must be called before SetMaterial
  GLboolean lighting;
  glGetBooleanv(GL_LIGHTING, &lighting);

  if (lighting) {
    glMaterialfv(GL_FRONT, GL_EMISSION, emissive);
    glMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT, GL_SHININESS, shininess);
  } else {
    RageColor c = diffuse;
    c.r_ += emissive.r_ + ambient.r_;
    c.g_ += emissive.g_ + ambient.g_;
    c.b_ += emissive.b_ + ambient.b_;
    glColor4fv(c);
  }
}

void RageDisplay_Legacy::SetLighting(bool b) {
  if (b) {
    glEnable(GL_LIGHTING);
  } else {
    glDisable(GL_LIGHTING);
  }
}

void RageDisplay_Legacy::SetLightOff(int index) {
  glDisable(GL_LIGHT0 + index);
}

void RageDisplay_Legacy::SetLightDirectional(
    int index, const RageColor& ambient, const RageColor& diffuse,
    const RageColor& specular, const RageVector3& dir) {
  // Light coordinates are transformed by the modelview matrix, but
  // we are being passed in world-space coords.
  glPushMatrix();
  glLoadIdentity();

  glEnable(GL_LIGHT0 + index);
  glLightfv(GL_LIGHT0 + index, GL_AMBIENT, ambient);
  glLightfv(GL_LIGHT0 + index, GL_DIFFUSE, diffuse);
  glLightfv(GL_LIGHT0 + index, GL_SPECULAR, specular);
  float position[4] = {dir.x_, dir.y_, dir.z_, 0};
  glLightfv(GL_LIGHT0 + index, GL_POSITION, position);

  glPopMatrix();
}

void RageDisplay_Legacy::SetCullMode(CullMode mode) {
  if (mode != CULL_NONE) {
    glEnable(GL_CULL_FACE);
  }
  switch (mode) {
    case CULL_BACK:
      glCullFace(GL_BACK);
      break;
    case CULL_FRONT:
      glCullFace(GL_FRONT);
      break;
    case CULL_NONE:
      glDisable(GL_CULL_FACE);
      break;
    default:
      FAIL_M(ssprintf("Invalid CullMode: %i", mode));
  }
}

const RageDisplay::RagePixelFormatDesc* RageDisplay_Legacy::GetPixelFormatDesc(
    RagePixelFormat pf) const {
  ASSERT(pf < NUM_RagePixelFormat);
  return &pixel_format_desc[pf];
}

void RageDisplay_Legacy::DeleteTexture(uintptr_t texture) {
  if (texture == 0) {
    return;
  }

  if (map_render_targets.find(texture) != map_render_targets.end()) {
    delete map_render_targets[texture];
    map_render_targets.erase(texture);
    return;
  }

  DebugFlushGLErrors();
  glDeleteTextures(1, reinterpret_cast<GLuint*>(&texture));
  DebugAssertNoGLError();
}

RagePixelFormat RageDisplay_Legacy::GetImgPixelFormat(
    RageSurface*& img, bool& free_img, int width, int height,
    bool paletted_texture) {
  RagePixelFormat pixfmt = FindPixelFormat(
      img->format_->bits_per_pixel_, img->format_->rmask_, img->format_->gmask_,
      img->format_->bmask_, img->format_->amask_);

  /* If img is paletted, we're setting up a non-paletted texture, and color
   * indexes are too small, depalettize. */
  bool supported = true;
  if (!paletted_texture && img->fmt_.bytes_per_pixel_ == 1 &&
      !color_index_table_works) {
    supported = false;
  }

  if (pixfmt == RagePixelFormat_Invalid || !SupportsSurfaceFormat(pixfmt)) {
    supported = false;
  }

  if (!supported) {
    /* The source isn't in a supported, known pixel format.  We need to convert
     * it ourself.  Just convert it to RGBA8, and let OpenGL convert it back
     * down to whatever the actual pixel format is.  This is a very slow code
     * path, which should almost never be used. */
    pixfmt = RagePixelFormat_RGBA8;
    ASSERT(SupportsSurfaceFormat(pixfmt));

    const RagePixelFormatDesc* pfd = DISPLAY->GetPixelFormatDesc(pixfmt);

    RageSurface* imgconv = CreateSurface(
        img->w_, img->h_, pfd->bpp_, pfd->masks_[0], pfd->masks_[1],
        pfd->masks_[2], pfd->masks_[3]);
    RageSurfaceUtils::Blit(img, imgconv, width, height);
    img = imgconv;
    free_img = true;
  } else {
    free_img = false;
  }

  return pixfmt;
}

/* If we're sending a paletted surface to a non-paletted texture, set the
 * palette. */
void SetPixelMapForSurface(
    int gl_image_format, int gl_tex_format, const RageSurfacePalette* palette) {
  if (gl_image_format != GL_COLOR_INDEX ||
      gl_tex_format == GL_COLOR_INDEX8_EXT) {
    glPixelTransferi(GL_MAP_COLOR, false);
    return;
  }

  GLushort buf[4][256];
  memset(buf, 0, sizeof(buf));

  for (int i = 0; i < palette->ncolors_; ++i) {
    buf[0][i] = SCALE(palette->colors_[i].r_, 0, 255, 0, 65535);
    buf[1][i] = SCALE(palette->colors_[i].g_, 0, 255, 0, 65535);
    buf[2][i] = SCALE(palette->colors_[i].b_, 0, 255, 0, 65535);
    buf[3][i] = SCALE(palette->colors_[i].a_, 0, 255, 0, 65535);
  }

  DebugFlushGLErrors();
  glPixelMapusv(GL_PIXEL_MAP_I_TO_R, 256, buf[0]);
  glPixelMapusv(GL_PIXEL_MAP_I_TO_G, 256, buf[1]);
  glPixelMapusv(GL_PIXEL_MAP_I_TO_B, 256, buf[2]);
  glPixelMapusv(GL_PIXEL_MAP_I_TO_A, 256, buf[3]);
  glPixelTransferi(GL_MAP_COLOR, true);
  DebugAssertNoGLError();
}

uintptr_t RageDisplay_Legacy::CreateTexture(
    RagePixelFormat pixfmt, RageSurface* img, bool generate_mip_maps) {
  ASSERT(pixfmt < NUM_RagePixelFormat);

  /* Find the pixel format of the surface we've been given. */
  bool free_img;
  RagePixelFormat surface_pix_fmt = GetImgPixelFormat(
      img, free_img, img->w_, img->h_, pixfmt == RagePixelFormat_PAL);
  ASSERT(surface_pix_fmt != RagePixelFormat_Invalid);

  GLenum gl_tex_format = kGlPixFmtInfo[pixfmt].internalfmt_;
  GLenum gl_image_format = kGlPixFmtInfo[surface_pix_fmt].format_;
  GLenum gl_image_type = kGlPixFmtInfo[surface_pix_fmt].type_;

  /* If the image is paletted, but we're not sending it to a paletted image,
   * set up glPixelMap. */
  SetPixelMapForSurface(
      gl_image_format, gl_tex_format, img->format_->palette_.get());

  // HACK:  OpenGL 1.2 types aren't available in GLU 1.3.  Don't call GLU for
  // mip mapping if we're using an OGL 1.2 type and don't have >= GLU 1.3.
  // http://pyopengl.sourceforge.net/documentation/manual/gluBuild2DMipmaps.3G.html
  if (generate_mip_maps && g_glu_version < 13) {
    switch (pixfmt) {
      // OpenGL 1.1 types
      case RagePixelFormat_RGBA8:
      case RagePixelFormat_RGB8:
      case RagePixelFormat_PAL:
      case RagePixelFormat_BGR8:
        break;
      // OpenGL 1.2 types
      default:
        LOG->Trace(
            "Can't generate mipmaps for type %s because GLU version %.1f is "
            "too old.",
            GLToString(gl_image_type).c_str(), g_glu_version / 10.f);
        generate_mip_maps = false;
        break;
    }
  }

  SetTextureUnit(TextureUnit_1);

  // allocate OpenGL texture resource
  uintptr_t tex_handle;
  glGenTextures(1, reinterpret_cast<GLuint*>(&tex_handle));
  ASSERT(tex_handle != 0);

  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex_handle));

  if (wind->GetActualVideoModeParams().anisotropic_filtering_ &&
      GLEW_EXT_texture_filter_anisotropic) {
    GLfloat largest_supported_anisotropy;
    glGetFloatv(
        GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
    glTexParameterf(
        GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
        largest_supported_anisotropy);
  }

  SetTextureFiltering(TextureUnit_1, true);
  SetTextureWrapping(TextureUnit_1, false);

  glPixelStorei(
      GL_UNPACK_ROW_LENGTH, img->pitch_ / img->format_->bytes_per_pixel_);

  if (pixfmt == RagePixelFormat_PAL) {
    /* The texture is paletted; set the texture palette. */
    GLubyte palette[256 * 4];
    memset(palette, 0, sizeof(palette));
    int p = 0;
    /* Copy the palette to the format OpenGL expects. */
    for (int i = 0; i < img->format_->palette_->ncolors_; ++i) {
      palette[p++] = img->format_->palette_->colors_[i].r_;
      palette[p++] = img->format_->palette_->colors_[i].g_;
      palette[p++] = img->format_->palette_->colors_[i].b_;
      palette[p++] = img->format_->palette_->colors_[i].a_;
    }

    /* Set the palette. */
    glColorTableEXT(
        GL_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, palette);

    GLint real_format = 0;
    glGetColorTableParameterivEXT(
        GL_TEXTURE_2D, GL_COLOR_TABLE_FORMAT, &real_format);
    ASSERT(real_format == GL_RGBA8);
  }

  LOG->Trace(
      "%s (format %s, %ix%i, format %s, type %s, pixfmt %i, imgpixfmt %i)",
      "glTexImage2D", GLToString(gl_tex_format).c_str(), img->w_, img->h_,
      GLToString(gl_image_format).c_str(), GLToString(gl_image_type).c_str(),
      pixfmt, surface_pix_fmt);

  DebugFlushGLErrors();

  glTexImage2D(
      GL_TEXTURE_2D, 0, gl_tex_format, power_of_two(img->w_),
      power_of_two(img->h_), 0, gl_image_format, gl_image_type, nullptr);
  if (img->pixels_) {
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, img->w_, img->h_, gl_image_format,
        gl_image_type, img->pixels_);
  }

  if (generate_mip_maps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  DebugAssertNoGLError();

  /* Sanity check: */
  if (pixfmt == RagePixelFormat_PAL) {
    GLint size = 0;
    glGetTexLevelParameteriv(
        GL_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INDEX_SIZE_EXT), &size);
    if (size != 8) {
      RageException::Throw("Thought paletted textures worked, but they don't.");
    }
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glFlush();

  if (free_img) {
    delete img;
  }
  return tex_handle;
}

struct RageTextureLock_OGL : public RageTextureLock, public InvalidateObject {
 public:
  RageTextureLock_OGL() {
    tex_handle_ = 0;
    buffer_ = 0;

    CreateObject();
  }

  ~RageTextureLock_OGL() {
    ASSERT(tex_handle_ == 0);  // locked!
    glDeleteBuffersARB(1, &buffer_);
  }

  /* This is called when our OpenGL context is invalidated. */
  void Invalidate() { tex_handle_ = 0; }

  void Lock(uintptr_t tex_handle, RageSurface* surface) {
    ASSERT(tex_handle_ == 0);
    ASSERT(surface->pixels_ == nullptr);

    CreateObject();

    tex_handle_ = tex_handle;
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, buffer_);

    int size = surface->h_ * surface->pitch_;
    glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, size, nullptr, GL_STREAM_DRAW);

    void* surface_memory =
        glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY);
    surface->pixels_ = (uint8_t*)surface_memory;
    surface->pixels_owned_ = false;
  }

  void Unlock(RageSurface* surface, bool changed) {
    glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);

    surface->pixels_ = (uint8_t*)BUFFER_OFFSET(0);

    if (changed) {
      DISPLAY->UpdateTexture(
          tex_handle_, surface, 0, 0, surface->w_, surface->h_);
    }

    surface->pixels_ = nullptr;

    tex_handle_ = 0;
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

 private:
  void CreateObject() {
    if (buffer_ != 0) {
      return;
    }

    DebugFlushGLErrors();
    glGenBuffersARB(1, &buffer_);
    DebugAssertNoGLError();
  }

  GLuint buffer_;

  uintptr_t tex_handle_;
};

RageTextureLock* RageDisplay_Legacy::CreateTextureLock() {
  if (!GLEW_ARB_pixel_buffer_object) {
    return nullptr;
  }

  return new RageTextureLock_OGL;
}

void RageDisplay_Legacy::UpdateTexture(
    uintptr_t tex_handle, RageSurface* img, int x_offset, int y_offset,
    int width, int height) {
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex_handle));

  bool free_img;
  RagePixelFormat surface_pix_fmt =
      GetImgPixelFormat(img, free_img, width, height, false);

  glPixelStorei(
      GL_UNPACK_ROW_LENGTH, img->pitch_ / img->format_->bytes_per_pixel_);

  GLenum gl_image_format = kGlPixFmtInfo[surface_pix_fmt].format_;
  GLenum gl_image_type = kGlPixFmtInfo[surface_pix_fmt].type_;

  /* If the image is paletted, but we're not sending it to a paletted image,
   * set up glPixelMap. */
  if (img->format_->palette_) {
    GLenum gl_tex_format = 0;
    glGetTexLevelParameteriv(
        GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INTERNAL_FORMAT),
        (GLint*)&gl_tex_format);
    SetPixelMapForSurface(
        gl_image_format, gl_tex_format, img->format_->palette_.get());
  }

  glTexSubImage2D(
      GL_TEXTURE_2D, 0, x_offset, y_offset, width, height, gl_image_format,
      gl_image_type, img->pixels_);

  /* Must unset PixelStore when we're done! */
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glFlush();

  if (free_img) {
    delete img;
  }
}

class RenderTarget_FramebufferObject : public RenderTarget {
 public:
  RenderTarget_FramebufferObject();
  ~RenderTarget_FramebufferObject();
  void Create(
      const RenderTargetParam& param, int& texture_width_out,
      int& texture_height_out);
  uintptr_t GetTexture() const { return tex_handle_; }
  void StartRenderingTo();
  void FinishRenderingTo();

  virtual bool InvertY() const { return true; }

 private:
  uintptr_t frame_buffer_handle_;
  uintptr_t tex_handle_;
  uintptr_t depth_buffer_handle_;
};

RenderTarget_FramebufferObject::RenderTarget_FramebufferObject() {
  frame_buffer_handle_ = 0;
  tex_handle_ = 0;
  depth_buffer_handle_ = 0;
}

RenderTarget_FramebufferObject::~RenderTarget_FramebufferObject() {
  if (depth_buffer_handle_) {
    glDeleteRenderbuffersEXT(
        1, reinterpret_cast<GLuint*>(&depth_buffer_handle_));
  }
  if (frame_buffer_handle_) {
    glDeleteFramebuffersEXT(
        1, reinterpret_cast<GLuint*>(&frame_buffer_handle_));
  }
  if (tex_handle_) {
    glDeleteTextures(1, reinterpret_cast<GLuint*>(&tex_handle_));
  }
}

void RenderTarget_FramebufferObject::Create(
    const RenderTargetParam& param, int& texture_width_out,
    int& texture_height_out) {
  param_ = param;

  DebugFlushGLErrors();

  // Allocate OpenGL texture resource
  glGenTextures(1, reinterpret_cast<GLuint*>(&tex_handle_));
  ASSERT(tex_handle_ != 0);

  int texture_width = power_of_two(param.width_);
  int texture_height = power_of_two(param.height_);

  texture_width_out = texture_width;
  texture_height_out = texture_height;

  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex_handle_));
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
  DebugAssertNoGLError();

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  /* Create the framebuffer object. */
  glGenFramebuffersEXT(1, reinterpret_cast<GLuint*>(&frame_buffer_handle_));
  ASSERT(frame_buffer_handle_ != 0);

  /* Attach the texture to it. */
  glBindFramebufferEXT(
      GL_FRAMEBUFFER_EXT, static_cast<GLuint>(frame_buffer_handle_));
  glFramebufferTexture2DEXT(
      GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D,
      static_cast<GLuint>(tex_handle_), 0);
  DebugAssertNoGLError();

  /* Attach a depth buffer, if requested. */
  if (param.with_depth_buffer_) {
    glGenRenderbuffersEXT(1, reinterpret_cast<GLuint*>(&depth_buffer_handle_));
    ASSERT(depth_buffer_handle_ != 0);

    glBindRenderbufferEXT(
        GL_RENDERBUFFER, static_cast<GLuint>(depth_buffer_handle_));
    glRenderbufferStorageEXT(
        GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16, texture_width,
        texture_height);
    glFramebufferRenderbufferEXT(
        GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
        static_cast<GLuint>(depth_buffer_handle_));
  }

  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  switch (status) {
    case GL_FRAMEBUFFER_COMPLETE_EXT:
      break;
    case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
      FAIL_M("GL_FRAMEBUFFER_UNSUPPORTED_EXT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
      FAIL_M("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
      FAIL_M("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
      FAIL_M("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
      FAIL_M("GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
      FAIL_M("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
      FAIL_M("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT");
      break;
    default:
      FAIL_M(ssprintf("Unexpected GL framebuffer status: %i", status));
  }

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void RenderTarget_FramebufferObject::StartRenderingTo() {
  glBindFramebufferEXT(
      GL_FRAMEBUFFER_EXT, static_cast<GLuint>(frame_buffer_handle_));
}

void RenderTarget_FramebufferObject::FinishRenderingTo() {
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

bool RageDisplay_Legacy::SupportsRenderToTexture() const {
  return GLEW_EXT_framebuffer_object || wind->SupportsRenderToTexture();
}

bool RageDisplay_Legacy::SupportsFullscreenBorderlessWindow() const {
  // In order to support FSBW, we're going to need the LowLevelWindow
  // implementation to support creating a fullscreen borderless window, and
  // we're going to need RenderToTexture support in order to render in
  // alternative resolutions
  return wind->SupportsFullscreenBorderlessWindow() &&
         SupportsRenderToTexture();
}

/*
 * Render-to-texture can be implemented in several ways: the generic
 * GL_ARB_pixel_buffer_object, or platform-specifically.  PBO is not available
 * on all hardware that supports RTT, particularly GeForce 2, but is simpler and
 * faster when available.
 */

uintptr_t RageDisplay_Legacy::CreateRenderTarget(
    const RenderTargetParam& param, int& texture_width_out,
    int& texture_height_out) {
  RenderTarget* target;
  if (GLEW_EXT_framebuffer_object) {
    target = new RenderTarget_FramebufferObject;
  } else {
    target = wind->CreateRenderTarget();
  }

  target->Create(param, texture_width_out, texture_height_out);

  uintptr_t texture = target->GetTexture();

  ASSERT(map_render_targets.find(texture) == map_render_targets.end());
  map_render_targets[texture] = target;
  return texture;
}

uintptr_t RageDisplay_Legacy::GetRenderTarget() {
  for (std::map<uintptr_t, RenderTarget*>::const_iterator it =
           map_render_targets.begin();
       it != map_render_targets.end(); ++it) {
    if (it->second == current_render_target) {
      return it->first;
    }
  }
  return 0;
}

void RageDisplay_Legacy::SetRenderTarget(
    uintptr_t texture, bool preserve_texture) {
  if (texture == 0) {
    invert_y = false;
    glFrontFace(GL_CCW);

    /* Pop matrixes affected by SetDefaultRenderStates. */
    DISPLAY->CameraPopMatrix();

    /* Reset the viewport. */
    int width = wind->GetActualVideoModeParams().window_width_;
    int height = wind->GetActualVideoModeParams().window_height_;
    glViewport(0, 0, width, height);

    if (current_render_target) {
      current_render_target->FinishRenderingTo();
    }
    current_render_target = nullptr;
    return;
  }

  /* If we already had a render target, disable it. */
  if (current_render_target != nullptr) {
    SetRenderTarget(0, true);
  }

  /* Enable the new render target. */
  ASSERT(map_render_targets.find(texture) != map_render_targets.end());
  RenderTarget* target = map_render_targets[texture];
  target->StartRenderingTo();
  current_render_target = target;

  /* Set the viewport to the size of the render target. */
  glViewport(0, 0, target->GetParam().width_, target->GetParam().height_);

  /* If this render target implementation flips Y, compensate.   Inverting will
   * switch the winding order. */
  invert_y = target->InvertY();
  if (invert_y) {
    glFrontFace(GL_CW);
  }

  /* The render target may be in a different OpenGL context, so re-send
   * state.  Push matrixes affected by SetDefaultRenderStates. */
  DISPLAY->CameraPushMatrix();
  SetDefaultRenderStates();

  /* Clear the texture, if requested.  Always set the associated state, for
   * consistency. */
  glClearColor(0, 0, 0, 0);
  SetZWrite(true);

  /* If bPreserveTexture is false, clear the render target.  Only clear the
   * depth buffer if the target has one; otherwise we're clearing the real depth
   * buffer. */
  if (!preserve_texture) {
    int bit = GL_COLOR_BUFFER_BIT;
    if (target->GetParam().with_depth_buffer_) {
      bit |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(bit);
  }
}

void RageDisplay_Legacy::SetLineWidth(float width) { glLineWidth(width); }

/*
 * XXX: Things like this only have to be set once per context - making
 * SetDefault call These kinds of functions is wasteful. -Colby
 */
void RageDisplay_Legacy::SetAlphaTest(bool b) {
  // Previously this was 0.01, rather than 0x01.
  glAlphaFunc(GL_GREATER, 0.00390625 /* 1/256 */);
  if (b) {
    glEnable(GL_ALPHA_TEST);
  } else {
    glDisable(GL_ALPHA_TEST);
  }
}

/*
 * Although we pair texture formats (eg. GL_RGB8) and surface formats
 * (pairs of eg. GL_RGB8,GL_UNSIGNED_SHORT_5_5_5_1), it's possible for
 * a format to be supported for a texture format but not a surface
 * format.  This is abstracted, so you don't need to know about this
 * as a user calling CreateTexture.
 *
 * One case of this is if packed pixels aren't supported.  We can still
 * use 16-bit color modes, but we have to send it in 32-bit.  Almost
 * everything supports packed pixels.
 *
 * Another case of this is incomplete packed pixels support.  Some
 * implementations neglect GL_UNSIGNED_SHORT_*_REV.
 */
bool RageDisplay_Legacy::SupportsSurfaceFormat(RagePixelFormat pixfmt) {
  switch (kGlPixFmtInfo[pixfmt].type_) {
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      return GLEW_EXT_bgra && reverse_packed_pixels_works;
    default:
      return true;
  }
}

bool RageDisplay_Legacy::SupportsTextureFormat(
    RagePixelFormat pixfmt, bool realtime) {
  /* If we support a pixfmt for texture formats but not for surface formats,
   * then we'll have to convert the texture to a supported surface format before
   * uploading. This is too slow for dynamic textures. */
  if (realtime && !SupportsSurfaceFormat(pixfmt)) {
    return false;
  }

  switch (kGlPixFmtInfo[pixfmt].format_) {
    case GL_COLOR_INDEX:
      return glColorTableEXT && glGetColorTableParameterivEXT;
    case GL_BGR:
    case GL_BGRA:
      return !!GLEW_EXT_bgra;
    default:
      return true;
  }
}

bool RageDisplay_Legacy::SupportsPerVertexMatrixScale() {
  // Intel i915 on OSX 10.4.4 supports vertex programs but not hardware vertex
  // buffers. Our software vertex rendering doesn't support vertex programs.
  return glGenBuffersARB && texture_matrix_shader != 0;
}

void RageDisplay_Legacy::SetSphereEnvironmentMapping(TextureUnit tu, bool b) {
  if (!SetTextureUnit(tu)) {
    return;
  }

  if (b) {
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
  } else {
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
  }
}

void RageDisplay_Legacy::SetCelShaded(int stage) {
  if (!GLEW_ARB_fragment_program && !GL_ARB_shading_language_100) {
    return;  // not supported
  }

  switch (stage) {
    case 1:
      glUseProgramObjectARB(shell_shader);
      break;
    case 2:
      glUseProgramObjectARB(cel_shader);
      break;
    default:
      glUseProgramObjectARB(0);
      break;
  }
}

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
