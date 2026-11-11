#include "LowLevelWindow_X11.h"

#include "DisplaySpec.h"
#include "LocalizedString.h"
#include "PrefsManager.h"  // XXX
#include "RageDisplay.h"   // VideoModeParams
#include "RageDisplay_OGL_Helpers.h"
#include "RageException.h"
#include "RageLog.h"
#include "archutils/Unix/X11Helper.h"
#include "global.h"
using namespace RageDisplay_Legacy_Helpers;
using namespace X11Helper;

#include <GL/glxew.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>  // All sorts of stuff...
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#if defined(HAVE_XINERAMA)
#include <X11/extensions/Xinerama.h>
#endif

#include <dbus/dbus.h>

// Display ID for treating the entire X screen as the display
const std::string kIdXscreen = "XSCREEN_RANDR";

static GLXContext g_context = nullptr;
static bool changed_screen_size = false;
static SizeID old_size = None;
static Rotation old_rotation = RR_Rotate_0;
static XRRScreenConfiguration* screen_config = nullptr;
static RRMode original_randr_mode = None;
static RROutput used_crtc = None;
static int randr_ver_minor = 0;
static int randr_ver_major = 0;
static bool use_xrandr12 = false;
static bool use_xinerama = false;

inline float calcRandRRefresh(
    unsigned long pixel_clock, int h_total, int v_total) {
  // Pixel Clock divided by total pixels in mode,
  // not just those onscreen!
  return (pixel_clock) / (h_total * v_total);
}

bool NetWMSupported(Display* dpy, Atom feature);

static LocalizedString FAILED_CONNECTION_XSERVER(
    "LowLevelWindow_X11", "Failed to establish a connection with the X server");
LowLevelWindow_X11::LowLevelWindow_X11() {
  if (!OpenXConnection()) {
    RageException::Throw("%s", FAILED_CONNECTION_XSERVER.GetValue().c_str());
  }

  if (XRRQueryVersion(g_dpy, &randr_ver_major, &randr_ver_minor) &&
      randr_ver_major >= 1 && randr_ver_minor >= 2) {
    use_xrandr12 = true;
  }
#ifdef HAVE_XINERAMA
  int xinerama_event_base = 0;
  int xinerama_error_base = 0;
  Atom fullscreen_monitors =
      XInternAtom(g_dpy, "_NET_WM_FULLSCREEN_MONITORS", False);
  if (XineramaQueryExtension(
          g_dpy, &xinerama_event_base, &xinerama_error_base) &&
      NetWMSupported(g_dpy, fullscreen_monitors)) {
    use_xinerama = true;
  }
#endif

  const int screen = DefaultScreen(g_dpy);
  int x_server_version = XVendorRelease(g_dpy); /* eg. 40201001 */
  int major = x_server_version / 10000000;
  x_server_version %= 10000000;
  int minor = x_server_version / 100000;
  x_server_version %= 100000;
  int revision = x_server_version / 1000;
  x_server_version %= 1000;
  int patch = x_server_version;

  LOG->Info("Display: %s (screen %i)", DisplayString(g_dpy), screen);
  LOG->Info(
      "X server vendor: %s [%i.%i.%i.%i]", XServerVendor(g_dpy), major, minor,
      revision, patch);
  LOG->Info(
      "Server GLX vendor: %s [%s]",
      glXQueryServerString(g_dpy, screen, GLX_VENDOR),
      glXQueryServerString(g_dpy, screen, GLX_VERSION));
  LOG->Info(
      "Client GLX vendor: %s [%s]", glXGetClientString(g_dpy, GLX_VENDOR),
      glXGetClientString(g_dpy, GLX_VERSION));
  was_windowed_ = true;
  screen_config =
      XRRGetScreenInfo(g_dpy, RootWindow(g_dpy, DefaultScreen(g_dpy)));
}

LowLevelWindow_X11::~LowLevelWindow_X11() {
  UninhibitScreensaver();

  if (fatal_error) {
    return;
  }

  // Reset the display
  if (!was_windowed_) {
    RestoreOutputConfig();
    XUngrabKeyboard(g_dpy, CurrentTime);
  }
  if (g_context) {
    glXDestroyContext(g_dpy, g_context);
    g_context = nullptr;
  }
  XDestroyWindow(g_dpy, g_win);
  g_win = None;
  CloseXConnection();
}

namespace {

constexpr const char* kScreenSaverBusName = "org.freedesktop.ScreenSaver";
constexpr const char* kScreenSaverPath = "/org/freedesktop/ScreenSaver";
constexpr const char* kScreenSaverInterface = "org.freedesktop.ScreenSaver";

bool TryInhibitScreensaver(DBusConnection* connection, uint32_t& cookie_out) {
  DBusMessage* message = dbus_message_new_method_call(
      kScreenSaverBusName, kScreenSaverPath, kScreenSaverInterface, "Inhibit");
  if (message == nullptr) {
    return false;
  }

  const char* application = "ITGmania";
  const char* reason = "Playing";
  dbus_message_append_args(
      message, DBUS_TYPE_STRING, &application, DBUS_TYPE_STRING, &reason,
      DBUS_TYPE_INVALID);

  DBusError error;
  dbus_error_init(&error);
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(
      connection, message, 1000, &error);
  dbus_message_unref(message);

  bool success = false;
  if (reply != nullptr) {
    dbus_uint32_t cookie = 0;
    success = dbus_message_get_args(
        reply, &error, DBUS_TYPE_UINT32, &cookie, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    if (success) {
      cookie_out = cookie;
    }
  }
  if (dbus_error_is_set(&error)) {
    LOG->Warn("LowLevelWindow_X11: D-Bus error: %s", error.message);
    dbus_error_free(&error);
  }
  return success;
}

}  // namespace

void LowLevelWindow_X11::InhibitScreensaver() {
  if (dbus_ != nullptr) {
    return;
  }

  dbus_threads_init_default();

  DBusError error;
  dbus_error_init(&error);
  DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
  if (connection == nullptr) {
    LOG->Warn(
        "LowLevelWindow_X11: couldn't connect to the D-Bus session bus: %s",
        dbus_error_is_set(&error) ? error.message : "unknown error");
    if (dbus_error_is_set(&error)) {
      dbus_error_free(&error);
    }
    return;
  }
  dbus_connection_set_exit_on_disconnect(connection, FALSE);

  uint32_t cookie = 0;
  if (TryInhibitScreensaver(connection, cookie)) {
    dbus_ = connection;
    screensaver_cookie_ = cookie;
    LOG->Info("LowLevelWindow_X11: inhibited screensaver");
    return;
  }

  LOG->Warn(
      "LowLevelWindow_X11: failed to inhibit screensaver via %s",
      kScreenSaverBusName);
  dbus_connection_close(connection);
  dbus_connection_unref(connection);
}

void LowLevelWindow_X11::UninhibitScreensaver() {
  if (dbus_ == nullptr) {
    return;
  }

  DBusMessage* message = dbus_message_new_method_call(
      kScreenSaverBusName, kScreenSaverPath, kScreenSaverInterface,
      "UnInhibit");
  if (message != nullptr) {
    dbus_uint32_t cookie = screensaver_cookie_;
    dbus_message_append_args(
        message, DBUS_TYPE_UINT32, &cookie, DBUS_TYPE_INVALID);

    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(dbus_, message, 1000, &error);
    if (reply != nullptr) {
      dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&error)) {
      LOG->Warn(
          "LowLevelWindow_X11: failed to uninhibit screensaver: %s",
          error.message);
      dbus_error_free(&error);
    }
    dbus_message_unref(message);
  }

  screensaver_cookie_ = 0;
  dbus_connection_close(dbus_);
  dbus_connection_unref(dbus_);
  dbus_ = nullptr;
}

/*
 * Restore saved X screen/CRTC configuration
 */
void LowLevelWindow_X11::RestoreOutputConfig() {
  if (changed_screen_size) {
    XRRSetScreenConfig(
        g_dpy, screen_config, RootWindow(g_dpy, DefaultScreen(g_dpy)), old_size,
        old_rotation, CurrentTime);
  }
  if (used_crtc != None) {
    ASSERT(use_xrandr12);
    XRRScreenResources* res = XRRGetScreenResources(g_dpy, g_win);
    XRRCrtcInfo* conf = XRRGetCrtcInfo(g_dpy, res, used_crtc);
    XRRSetCrtcConfig(
        g_dpy, res, used_crtc, conf->timestamp, conf->x, conf->y,
        original_randr_mode, conf->rotation, conf->outputs, conf->noutput);
    XRRFreeScreenResources(res);
    XRRFreeCrtcInfo(conf);
  }
  old_size = None;
  changed_screen_size = false;
  used_crtc = None;
  old_rotation = RR_Rotate_0;
}

void* LowLevelWindow_X11::GetProcAddress(std::string s) {
  // XXX: We should check whether glXGetProcAddress or
  // glXGetProcAddressARB is available/not nullptr, and go by that,
  // instead of assuming like this.
  return (void*)glXGetProcAddressARB((const GLubyte*)s.c_str());
}

std::string LowLevelWindow_X11::TryVideoMode(
    const VideoModeParams& p, bool& new_device_out) {
  // We're going to be interested in MapNotify/ConfigureNotify events in this
  // routine, so ensure our event mask includes these, restore it on exit
  XWindowAttributes win_attrib;
  auto restore = [&](XWindowAttributes* attr) {
    XSelectInput(g_dpy, g_win, attr->your_event_mask);
  };
  auto restore_attrib = std::unique_ptr<XWindowAttributes, decltype(restore)>(
      &win_attrib, restore);

  // These might change if we're rendering at different resolution than window
  int window_width = p.width_;
  int window_height = p.height_;
  bool render_offscreen = false;

  if (g_context == nullptr || p.bpp_ != current_params_.bpp_ ||
      was_windowed_ != p.windowed_) {
    bool first_run = g_context == nullptr;
    // Different depth, or we didn't make a window before. New context.
    new_device_out = true;

    int vis_attribs[32];
    int i = 0;
    ASSERT(p.bpp_ == 16 || p.bpp_ == 32);

    if (p.bpp_ == 32) {
      vis_attribs[i++] = GLX_RED_SIZE;
      vis_attribs[i++] = 8;
      vis_attribs[i++] = GLX_GREEN_SIZE;
      vis_attribs[i++] = 8;
      vis_attribs[i++] = GLX_BLUE_SIZE;
      vis_attribs[i++] = 8;
    } else {
      vis_attribs[i++] = GLX_RED_SIZE;
      vis_attribs[i++] = 5;
      vis_attribs[i++] = GLX_GREEN_SIZE;
      vis_attribs[i++] = 6;
      vis_attribs[i++] = GLX_BLUE_SIZE;
      vis_attribs[i++] = 5;
    }

    vis_attribs[i++] = GLX_DEPTH_SIZE;
    vis_attribs[i++] = 16;
    vis_attribs[i++] = GLX_RGBA;
    vis_attribs[i++] = GLX_DOUBLEBUFFER;

    vis_attribs[i++] = None;

    XVisualInfo* xvi =
        glXChooseVisual(g_dpy, DefaultScreen(g_dpy), vis_attribs);
    if (xvi == nullptr) {
      return "No visual available for that depth.";
    }

    // I get strange behavior if I add override redirect after creating the
    // window. So, let's recreate the window when changing that state.
    if (!MakeWindow(
            g_win, xvi->screen, xvi->depth, xvi->visual, p.width_, p.height_,
            !p.windowed_)) {
      return "Failed to create the window.";
    }

    char* window_title = const_cast<char*>(p.window_title_.c_str());
    XChangeProperty(
        g_dpy, g_win, XA_WM_NAME, XA_STRING, 8, PropModeReplace,
        reinterpret_cast<unsigned char*>(window_title), strlen(window_title));

    if (g_context) {
      glXDestroyContext(g_dpy, g_context);
    }
    g_context = glXCreateContext(g_dpy, xvi, nullptr, True);

    glXMakeCurrent(g_dpy, g_win, g_context);

    XGetWindowAttributes(g_dpy, g_win, &win_attrib);
    XSelectInput(
        g_dpy, g_win,
        win_attrib.your_event_mask | StructureNotifyMask | PropertyChangeMask);

    XMapWindow(g_dpy, g_win);

    XEvent ev;
    do {
      XWindowEvent(g_dpy, g_win, StructureNotifyMask, &ev);
    } while (ev.type != MapNotify);

    // I can't find official docs saying what happens if you re-init GLEW.
    // I'll just assume the behavior is undefined.
    if (first_run) {
      GLenum err = glewInit();
      ASSERT(err == GLEW_OK);
    }
  } else {
    // We're remodeling the existing window, and not touching the context.
    new_device_out = false;

    XGetWindowAttributes(g_dpy, g_win, &win_attrib);
    XSelectInput(
        g_dpy, g_win,
        win_attrib.your_event_mask | StructureNotifyMask | PropertyChangeMask);

    if (!p.windowed_) {
      // X11 is an asynchronous beast. If we're resizing an existing
      // window directly (i.e. override-redirect as opposed to asking the
      // WM to do it) and don't wait for the window to actually be
      // resized, we'll get unexpected results from glViewport() etc. I
      // don't know why, or why it *doesn't* break in the slower process
      // of waiting for the WM to resize the window.

      // So, set the event mask so we're notified when the window is resized...
      // Send the resize command...
      XResizeWindow(
          g_dpy, g_win, static_cast<unsigned int>(p.width_),
          static_cast<unsigned int>(p.height_));

      // We'll wait for the notification once we've done everything else,
      // to save time.
    }
  }

  float rate = 60;  // Will be unchanged if windowed. Not sure I care.

  if (!p.windowed_) {
    RestoreOutputConfig();

    if (p.display_id_ == kIdXscreen || p.display_id_.empty()) {
      // If the user changed the resolution while StepMania was windowed we
      // overwrite the resolution to restore with it at exit.
      old_size = XRRConfigCurrentConfiguration(screen_config, &old_rotation);
      was_windowed_ = false;

      // Find a matching mode.
      int sizes_xct;
      XRRScreenSize* sizes_x =
          XRRSizes(g_dpy, DefaultScreen(g_dpy), &sizes_xct);
      ASSERT_M(sizes_xct != 0, "Couldn't get resolution list from X server");

      int size_match = -1;

      for (int i = 0; i < sizes_xct; ++i) {
        if (sizes_x[i].width == p.width_ && sizes_x[i].height == p.height_) {
          size_match = i;
          break;
        }
      }
      if (size_match != old_size) {
        changed_screen_size = true;
      }

      // Set this mode.
      // XXX: This doesn't handle if the config has changed since we queried it
      // (see man Xrandr)
      Status s = XRRSetScreenConfig(
          g_dpy, screen_config, RootWindow(g_dpy, DefaultScreen(g_dpy)),
          size_match, 1, CurrentTime);
      if (s) {
        return "Failed to set screen config";
      }

      XMoveWindow(g_dpy, g_win, 0, 0);

      XRaiseWindow(g_dpy, g_win);

      // We want to prevent the WM from catching anything that comes from the
      // keyboard. We should do this every time on fullscreen and not only we
      // entering from windowed mode because we could lose focus at resolution
      // change and that will leave the user input locked.
      while (XGrabKeyboard(
          g_dpy, g_win, True, GrabModeAsync, GrabModeAsync, CurrentTime));

    } else {
      ASSERT(use_xrandr12);
      /* === Configuring a specific CRTC === */
      // Arcane and undocumented but PROPER XRandR 1.2 method.
      // What we do is directly reconfigure the CRTC of the primary display,
      // Which prevents the (RandR) screen itself from resizing, and therefore
      // leaving user's desktop unmolested.
      LOG->Info("LowLevelWindow_X11: Using XRandR");

      XRRScreenResources* scr_res = XRRGetScreenResources(g_dpy, g_win);
      ASSERT(scr_res != nullptr);
      ASSERT(scr_res->ncrtc > 0);
      ASSERT(scr_res->noutput > 0);
      ASSERT(scr_res->nmode > 0);

      // If an output name has been specified, search for it
      RROutput target_out = None;
      if (p.display_id_.length() > 0) {
        for (unsigned int i = 0;
             i < static_cast<uint32_t>(scr_res->noutput) && target_out == None;
             ++i) {
          XRROutputInfo* out_info =
              XRRGetOutputInfo(g_dpy, scr_res, scr_res->outputs[i]);
          std::string out_name = std::string(
              out_info->name, static_cast<unsigned int>(out_info->nameLen));
          if (p.display_id_ == out_name) {
            target_out = scr_res->outputs[i];
          }
          XRRFreeOutputInfo(out_info);
        }
      }
      if (target_out == None) {
        LOG->Info(
            "Did not find display output %s, trying another",
            p.display_id_.c_str());
        // didn't find named output, pick primary/or at least one that works
        if (randr_ver_major >= 1 && randr_ver_minor >= 3) {
          // RandR 1.3 can tell us what the primary display is.
          target_out = XRRGetOutputPrimary(g_dpy, g_win);
        } else {
          // Only RandR 1.2. We'll look for a "Connected" output, or if we can't
          // find that, (it is possible the connection state could be unknown),
          // we'll at least look for an output with a CRTC driving it
          RROutput connected = None, has_crtc = None;
          for (unsigned int i = 0; i < static_cast<uint32_t>(scr_res->noutput);
               ++i) {
            XRROutputInfo* out_info =
                XRRGetOutputInfo(g_dpy, scr_res, scr_res->outputs[i]);
            if (out_info->connection ==
                RR_Connected) {  // Check for CONNECTED state: Connected == 0
              connected = scr_res->outputs[i];
            }
            if (out_info->crtc != None) {
              has_crtc = out_info->crtc;
            }
            XRRFreeOutputInfo(out_info);
          }
          target_out = connected != None ? connected : has_crtc;
          ASSERT(target_out != None);
        }
      }

      // if the target output is not currently being driven by a crtc,
      // find an unused crtc that can be connected to it
      XRROutputInfo* tgt_out_info =
          XRRGetOutputInfo(g_dpy, scr_res, target_out);
      if (tgt_out_info == nullptr) {
        XRRFreeScreenResources(scr_res);
        return "Failed to find XRROutput";
      }

      RRCrtc tgt_out_crtc = tgt_out_info->crtc;
      if (tgt_out_crtc == None) {
        for (unsigned int i = 0; i < static_cast<uint32_t>(tgt_out_info->ncrtc);
             ++i) {
          XRRCrtcInfo* crtc_info =
              XRRGetCrtcInfo(g_dpy, scr_res, tgt_out_info->crtcs[i]);
          if (crtc_info->mode == None) {
            tgt_out_crtc = tgt_out_info->crtcs[i];
          }
          XRRFreeCrtcInfo(crtc_info);
        }
      }
      ASSERT(tgt_out_crtc != None);

      XRRCrtcInfo* old_conf = XRRGetCrtcInfo(g_dpy, scr_res, tgt_out_crtc);

      float refresh_diff = 99999;
      float refresh_rate = 0;
      RRMode mode = None;
      // A quirk of XRandR is that the width and height are as the display
      // controller ("CRTC") sees it, which means height and width are
      // flipped if there's rotation going on.
      const bool portrait =
          (old_conf->rotation & (RR_Rotate_90 | RR_Rotate_270)) != 0;
      // Find a mode that matches our exact wanted resolution,
      // with as close to our desired refresh rate as possible.
      for (int i = 0; i < scr_res->nmode; i++) {
        const XRRModeInfo& this_mi = scr_res->modes[i];
        const unsigned int mode_width =
            portrait ? this_mi.height : this_mi.width;
        const unsigned int mode_height =
            portrait ? this_mi.width : this_mi.height;
        if (p.width_ >= 0 && p.height_ >= 0 &&
            mode_width == static_cast<uint32_t>(p.width_) &&
            mode_height == static_cast<uint32_t>(p.height_)) {
          float temp_refresh = calcRandRRefresh(
              this_mi.dotClock, this_mi.hTotal, this_mi.vTotal);
          float temp_diff = std::abs(p.rate_ - temp_refresh);
          if ((p.rate_ != kRefreshDefault && temp_diff < refresh_diff) ||
              (p.rate_ == kRefreshDefault && temp_refresh > refresh_rate)) {
            int j;
            // Ensure that the output supports the mode
            for (j = 0; j < tgt_out_info->nmode; j++) {
              if (tgt_out_info->modes[j] == scr_res->modes[i].id) {
                mode = tgt_out_info->modes[j];
                break;
              }
            }

            if (j < tgt_out_info->nmode) {
              refresh_rate = temp_refresh;
              refresh_diff = temp_diff;
            }
          }
        }
      }
      rate = std::round(refresh_rate);

      used_crtc = tgt_out_crtc;
      original_randr_mode = old_conf->mode;

      const std::string tgt_out_name = std::string(
          tgt_out_info->name, static_cast<unsigned int>(tgt_out_info->nameLen));
      LOG->Info(
          "XRandR output config using CRTC %lu in mode %lu, driving output %s",
          used_crtc, mode, tgt_out_name.c_str());
      // and FIRE!
      Status s = XRRSetCrtcConfig(
          g_dpy, scr_res, used_crtc, old_conf->timestamp, old_conf->x,
          old_conf->y, mode, old_conf->rotation, old_conf->outputs,
          old_conf->noutput);
      if (s) {
        XRRFreeCrtcInfo(old_conf);
        XRRFreeOutputInfo(tgt_out_info);
        XRRFreeScreenResources(scr_res);
        return "Failed to set CRTC config";
      }

      // We don't move to absolute 0,0 because that may be in the area of a
      // different output. Instead we preserved the corner of our CRTC; go to
      // that.
      XMoveWindow(g_dpy, g_win, old_conf->x, old_conf->y);

      // Final cleanup
      XRRFreeCrtcInfo(old_conf);
      XRRFreeOutputInfo(tgt_out_info);
      XRRFreeScreenResources(scr_res);
    }
    was_windowed_ = false;

    XRaiseWindow(g_dpy, g_win);

    // We want to prevent the WM from catching anything that comes from the
    // keyboard. We should do this every time on fullscreen and not only we
    // entering from windowed mode because we could lose focus at resolution
    // change and that will leave the user input locked.
    while (XGrabKeyboard(
        g_dpy, g_win, True, GrabModeAsync, GrabModeAsync, CurrentTime));
  } else  // if(p.windowed)
  {
    if (!was_windowed_) {
      // Return the display to the mode it was in before we fullscreened.
      RestoreOutputConfig();
      XUngrabKeyboard(g_dpy, CurrentTime);
      was_windowed_ = true;
    }

    Atom net_wm_state = XInternAtom(g_dpy, "_NET_WM_STATE", False);
    Atom fullscreen_state =
        XInternAtom(g_dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom maximized_vert =
        XInternAtom(g_dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom maximized_horz =
        XInternAtom(g_dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    // if FSBW, find matching monitor, move window to its origin,
    // then set fullscreen hint, and set the CurrentParams.outWidth,
    // CurrentParams.outHeight to the values of that display otherwise set the
    // size hints and disable MAXIMIZED_*
    if (p.window_is_fullscreen_borderless_) {
      auto specs = DisplaySpecs{};
      GetDisplaySpecs(specs);
      auto target = std::find_if(
          specs.begin(), specs.end(), [&](const DisplaySpec& spec) {
            return p.display_id_ == spec.id() && spec.currentMode() != nullptr;
          });
      // If we didn't find a matching DisplaySpec for the requested ID, pick the
      // first one with a current mode
      if (target == specs.end()) {
        target = std::find_if(
            specs.begin(), specs.end(), [&](const DisplaySpec& spec) {
              return spec.currentMode() != nullptr;
            });
      }
      // If we _still_ haven't found anything (unlikely), then just give up
      if (target == specs.end()) {
        return "Unable to find destination monitor for fullscreen borderless";
      }

      window_width = target->currentMode()->width_;
      window_height = target->currentMode()->height_;

      if (window_width != p.width_ || window_height != p.height_) {
        render_offscreen = true;
      }

      // Reset anything that might've been set previously:
      // (1) Undo Min/Max size bounds
      // (2) Remove FULLSCREEN/MAXIMIZED_{HORIZ,VERT} hints
      // Without doing this, WM may not let us move/resize window to new display
      // Give Window manager the chance to react to changes (otherwise, Mutter
      // had problems properly reacting to moving a _NET_WM_STATE_FULLSCREEN
      // window to a different output
      //   and fullscreen resetting FULLSCREEN hint.
      XSizeHints hints;
      hints.flags = 0;
      XSetWMNormalHints(g_dpy, g_win, &hints);
#if defined(HAVE_XINERAMA)
      if (!use_xinerama || !SetWMFullscreenMonitors(*target))
#endif
      {
        SetWMState(win_attrib.root, g_win, 0, maximized_horz);
        SetWMState(win_attrib.root, g_win, 0, maximized_vert);
        SetWMState(win_attrib.root, g_win, 0, fullscreen_state);

        XFlush(g_dpy);
        XResizeWindow(
            g_dpy, g_win, static_cast<unsigned int>(window_width),
            static_cast<unsigned int>(window_height));
        XMoveWindow(
            g_dpy, g_win, target->currentBounds().left_,
            target->currentBounds().top_);
        XRaiseWindow(g_dpy, g_win);

        SetWMState(win_attrib.root, g_win, 1, fullscreen_state);
        SetWMState(win_attrib.root, g_win, 1, maximized_horz);
        SetWMState(win_attrib.root, g_win, 1, maximized_vert);
      }
    } else {
      window_width = p.width_;
      window_height = p.height_;

      SetWMState(win_attrib.root, g_win, 0, fullscreen_state);
      // Make a window fixed size, don't let resize it or maximize it.
      // Do this before resizing the window so that pane-style WMs (Ion,
      // ratpoison) don't resize us back inappropriately.
      {
        XSizeHints hints;

        hints.flags = PMinSize | PMaxSize | PWinGravity;
        hints.min_width = hints.max_width = window_width;
        hints.min_height = hints.max_height = window_height;
        hints.win_gravity = CenterGravity;

        XSetWMNormalHints(g_dpy, g_win, &hints);
      }
      /* Workaround for metacity and compiz: if the window have the same
       * resolution or higher than the screen, it gets automaximized even
       * when the window is set to not let it happen. This happens when
       * changing from fullscreen to window mode and our screen resolution
       * is bigger. */
      {
        SetWMState(win_attrib.root, g_win, 1, maximized_vert);
        SetWMState(win_attrib.root, g_win, 1, maximized_horz);

        // This one is needed for compiz, if the window reaches out of bounds of
        // the screen it becames destroyed, only the window, the program is left
        // running. Commented out per the patch at
        // http://ssc.ajworld.net/sm-ssc/bugtracker/view.php?id=398
        // XMoveWindow( Dpy, Win, 0, 0 );
      }
    }
  }

  current_params_ = p;
  current_params_.window_width_ = window_width;
  current_params_.window_height_ = window_height;
  current_params_.render_offscreen_ = render_offscreen;
  ASSERT(rate > 0);
  current_params_.rate_ = std::round(rate);

  if (!p.windowed_) {
    // Set our V-sync hint.
    if (GLXEW_EXT_swap_control) {  // I haven't seen this actually implemented
                                   // yet, but why not.
      glXSwapIntervalEXT(g_dpy, g_win, current_params_.vsync_ ? 1 : 0);
    }
    // XXX: These two might be server-global. I should look into whether
    // to try to preserve the original value on exit.
#ifdef GLXEW_MESA_swap_control  // Added in 1.7. 1.6 is still common out there
                                // apparently.
    else if (GLXEW_MESA_swap_control) {  // Haven't seen this NOT implemented
                                         // yet
      glXSwapIntervalMESA(current_params_.vsync_ ? 1 : 0);
    }
#endif
    else if (GLXEW_SGI_swap_control) {  // But old GLEW.
      glXSwapIntervalSGI(current_params_.vsync_ ? 1 : 0);
    } else {
      current_params_.vsync_ = false;  // Assuming it's not on
    }
  }

  if (PREFSMAN->disable_screen_saver_) {
    InhibitScreensaver();
  }

  return "";  // Success
}

void LowLevelWindow_X11::LogDebugInformation() const {
  LOG->Info(
      "Direct rendering: %s",
      glXIsDirect(g_dpy, glXGetCurrentContext()) ? "yes" : "no");
}

bool LowLevelWindow_X11::IsSoftwareRenderer(std::string& error) {
  if (glXIsDirect(g_dpy, glXGetCurrentContext())) {
    return false;
  }

  error = "Direct rendering is not available.";
  return true;
}

void LowLevelWindow_X11::SwapBuffers() { glXSwapBuffers(g_dpy, g_win); }

void LowLevelWindow_X11::GetDisplaySpecs(DisplaySpecs& out) const {
  int screen_num = DefaultScreen(g_dpy);
  Screen* screen = ScreenOfDisplay(g_dpy, screen_num);

  XWindowAttributes win_attr = XWindowAttributes();
  if (XGetWindowAttributes(g_dpy, g_win, &win_attr)) {
    screen = win_attr.screen;
    screen_num = XScreenNumberOfScreen(screen);
  }

  // Create a display spec for the entire X screen itself
  // First get current config
  Rotation cur_rotation;
  XRRScreenConfiguration* screen_conf = XRRGetScreenInfo(g_dpy, g_win);
  const short cur_rate = XRRConfigCurrentRate(screen_conf);
  SizeID cur_size_id =
      XRRConfigCurrentConfiguration(screen_conf, &cur_rotation);
  // curRotation does not factor into how we report supported XScreen sizes:
  // XRR reports the supported *screen* sizes with height/width swapped
  // appropriately for currently configured rotation. Supported sizes for
  // *output* modes (below) DO NOT account for screen rotation

  std::set<DisplayMode> screen_modes;
  int nsizes = 0;
  XRRScreenSize* screen_sizes = XRRSizes(g_dpy, screen_num, &nsizes);
  DisplayMode screen_cur_mode{};
  for (unsigned int idx = 0, mode_idx = 0;
       nsizes >= 0 && idx < static_cast<uint32_t>(nsizes); ++idx) {
    XRRScreenSize& size = screen_sizes[idx];
    int nrates = 0;
    short* rates = XRRRates(g_dpy, screen_num, idx, &nrates);
    for (unsigned int r_idx = 0;
         nrates >= 0 && r_idx < static_cast<uint32_t>(nrates);
         ++r_idx, ++mode_idx) {
      DisplayMode m = {
          static_cast<unsigned int>(size.width),
          static_cast<unsigned int>(size.height),
          static_cast<double>(rates[r_idx])};
      screen_modes.insert(m);
      if (rates[r_idx] == cur_rate && idx == cur_size_id) {
        screen_cur_mode = m;
      }
    }
  }
  const RectI screen_bounds(
      0, 0, screen_sizes[cur_size_id].width, screen_sizes[cur_size_id].height);
  const DisplaySpec screen_spec(
      kIdXscreen, "X Screen", screen_modes, screen_cur_mode, screen_bounds,
      true);
  out.insert(screen_spec);
  // XRRScreenSize array from XRRSizes does *not* have to be returned (valgrind
  // said XFree was an invalid free in a small test program, there is no
  // XRRFreeScreenSize, etc)
  XRRFreeScreenConfigInfo(screen_conf);

  if (use_xrandr12) {
    // Build per-output DisplaySpecs

    // First, get the list of resolutions that'll be referenced (by RRMode) in
    // each OutputInfo
    XRRScreenResources* scr_res = XRRGetScreenResources(g_dpy, g_win);
    std::map<RRMode, DisplayMode> output_modes;
    for (int i = 0; i < scr_res->nmode; ++i) {
      const XRRModeInfo& mode = scr_res->modes[i];
      DisplayMode m = {
          mode.width, mode.height,
          calcRandRRefresh(mode.dotClock, mode.hTotal, mode.vTotal)};
      output_modes[mode.id] = m;
    }

    // Now, for each output, build a corresponding DisplaySpec
    for (unsigned int out_idx = 0;
         out_idx < static_cast<uint32_t>(scr_res->noutput); ++out_idx) {
      XRROutputInfo* out_info =
          XRRGetOutputInfo(g_dpy, scr_res, scr_res->outputs[out_idx]);
      if (out_info->nmode > 0) {
        // Get the current configuration of the Output, if it's being driven by
        // a crtc
        RRMode cur_rr_mode = None;
        bool portrait = false;
        int crtc_x = 0, crtc_y = 0;
        if (out_info->crtc != None) {
          XRRCrtcInfo* conf = XRRGetCrtcInfo(g_dpy, scr_res, out_info->crtc);
          cur_rr_mode = conf->mode;
          portrait = (conf->rotation & (RR_Rotate_90 | RR_Rotate_270)) != 0;
          crtc_x = conf->x;
          crtc_y = conf->y;
          XRRFreeCrtcInfo(conf);
        }
        // Get all supported modes, noting which one, if any, is currently
        // active
        std::set<DisplayMode> output_supported;
        DisplayMode output_cur_mode{};
        RectI out_bounds;
        for (unsigned int modeIdx = 0;
             modeIdx < static_cast<uint32_t>(out_info->nmode); ++modeIdx) {
          DisplayMode mode = output_modes[out_info->modes[modeIdx]];
          unsigned int mode_width = portrait ? mode.height_ : mode.width_;
          unsigned int mode_height = portrait ? mode.width_ : mode.height_;
          DisplayMode m = {mode_width, mode_height, mode.refresh_rate_};
          output_supported.insert(m);
          if (cur_rr_mode != None && out_info->modes[modeIdx] == cur_rr_mode) {
            output_cur_mode = m;
            out_bounds = RectI(
                crtc_x, crtc_y, crtc_x + mode_width, crtc_y + mode_height);
          }
        }
        const std::string out_id(
            out_info->name, static_cast<unsigned int>(out_info->nameLen));
        const std::string out_name(out_id);
        if (cur_rr_mode != None) {
          out.insert(DisplaySpec(
              out_id, out_name, output_supported, output_cur_mode, out_bounds));
        } else {
          out.insert(DisplaySpec(out_id, out_name, output_supported));
        }
      }
      XRRFreeOutputInfo(out_info);
    }
    XRRFreeScreenResources(scr_res);
  }
}

class RenderTarget_X11 : public RenderTarget {
 public:
  RenderTarget_X11(LowLevelWindow_X11* wind);
  ~RenderTarget_X11();

  void Create(
      const RenderTargetParam& param, int& texture_width_out,
      int& texture_height_out);
  uintptr_t GetTexture() const { return static_cast<uintptr_t>(tex_handle_); }
  void StartRenderingTo();
  void FinishRenderingTo();

  // Copying from the Pbuffer to the texture flips Y.
  virtual bool InvertY() const { return true; }

 private:
  int width_, height_;
  LowLevelWindow_X11* wind_;
  GLXPbuffer pbuffer_;
  GLXContext pbuffer_context_;
  GLuint tex_handle_;

  GLXContext old_context_;
  GLXDrawable old_drawable_;
};

RenderTarget_X11::RenderTarget_X11(LowLevelWindow_X11* wind) {
  wind_ = wind;
  pbuffer_ = 0;
  pbuffer_context_ = nullptr;
  tex_handle_ = 0;
  old_context_ = nullptr;
  old_drawable_ = 0;
}

RenderTarget_X11::~RenderTarget_X11() {
  if (pbuffer_context_) {
    glXDestroyContext(g_dpy, pbuffer_context_);
  }
  if (pbuffer_) {
    glXDestroyPbuffer(g_dpy, pbuffer_);
  }
  if (tex_handle_) {
    glDeleteTextures(1, reinterpret_cast<GLuint*>(&tex_handle_));
  }
}

/* Note that although the texture size may need to be a power of 2,
 * the Pbuffer does not. */
void RenderTarget_X11::Create(
    const RenderTargetParam& param, int& texture_width_out,
    int& texture_height_out) {
  // ASSERT( param.iWidth == power_of_two(param.iWidth) && param.iHeight ==
  // power_of_two(param.iHeight) );

  width_ = param.width_;
  height_ = param.height_;

  /* NOTE: int casts on GLX_DONT_CARE are for -Werror=narrowing */
  int config_attribs[] = {
      GLX_DRAWABLE_TYPE,
      GLX_PBUFFER_BIT,
      GLX_RENDER_TYPE,
      GLX_RGBA_BIT,

      GLX_RED_SIZE,
      8,
      GLX_GREEN_SIZE,
      8,
      GLX_BLUE_SIZE,
      8,
      GLX_ALPHA_SIZE,
      param.with_alpha_ ? 8 : (int)GLX_DONT_CARE,

      GLX_DOUBLEBUFFER,
      False,
      GLX_DEPTH_SIZE,
      param.with_depth_buffer_ ? 16 : (int)GLX_DONT_CARE,
      None};
  int i_configs;
  GLXFBConfig* p_configs = glXChooseFBConfig(
      g_dpy, DefaultScreen(g_dpy), config_attribs, &i_configs);
  ASSERT(p_configs);

  const int pbuffer_attribs[] = {
      GLX_PBUFFER_WIDTH, param.width_, GLX_PBUFFER_HEIGHT, param.height_, None};

  for (int i = 0; i < i_configs; ++i) {
    pbuffer_ = glXCreatePbuffer(g_dpy, p_configs[i], pbuffer_attribs);
    if (pbuffer_ == 0) {
      continue;
    }

    XVisualInfo* visual = glXGetVisualFromFBConfig(g_dpy, p_configs[i]);
    pbuffer_context_ = glXCreateContext(g_dpy, visual, g_context, True);
    ASSERT(pbuffer_context_);
    XFree(visual);
    break;
  }

  ASSERT(pbuffer_);

  // allocate OpenGL texture resource
  glGenTextures(1, reinterpret_cast<GLuint*>(&tex_handle_));
  glBindTexture(GL_TEXTURE_2D, tex_handle_);

  LOG->Trace("n %i, %ix%i", tex_handle_, param.width_, param.height_);
  while (glGetError() != GL_NO_ERROR);

  int texture_width = power_of_two(param.width_);
  int texture_height = power_of_two(param.height_);
  texture_width_out = texture_width;
  texture_height_out = texture_height;

  glTexImage2D(
      GL_TEXTURE_2D, 0, param.with_alpha_ ? GL_RGBA8 : GL_RGB8, texture_width,
      texture_height, 0, param.with_alpha_ ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE,
      nullptr);
  GLenum error = glGetError();
  ASSERT_M(error == GL_NO_ERROR, GLToString(error));

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void RenderTarget_X11::StartRenderingTo() {
  old_context_ = glXGetCurrentContext();
  old_drawable_ = glXGetCurrentDrawable();
  glXMakeCurrent(g_dpy, pbuffer_, pbuffer_context_);

  glViewport(0, 0, width_, height_);
}

void RenderTarget_X11::FinishRenderingTo() {
  glFlush();

  glBindTexture(GL_TEXTURE_2D, tex_handle_);

  while (glGetError() != GL_NO_ERROR);

  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);

  GLenum error = glGetError();
  ASSERT_M(error == GL_NO_ERROR, GLToString(error));

  glBindTexture(GL_TEXTURE_2D, 0);

  glXMakeCurrent(g_dpy, old_drawable_, old_context_);
  old_context_ = nullptr;
  old_drawable_ = 0;
}

bool LowLevelWindow_X11::SupportsRenderToTexture() const {
  // Server must support pbuffers:
  const int screen = DefaultScreen(g_dpy);
  float version =
      strtof(glXQueryServerString(g_dpy, screen, GLX_VERSION), nullptr);
  if (version < 1.3f) {
    return false;
  }

  return true;
}

bool NetWMSupported(Display* dpy, Atom feature) {
  Atom net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
  Atom actual_type_return = BadAtom;
  int actual_format_return = 0;
  unsigned long nitems_return = 0;
  unsigned long bytes_after_return = 0;
  Atom* prop_return;
  Status status = XGetWindowProperty(
      dpy, RootWindow(dpy, DefaultScreen(dpy)), net_supported, 0, 8192, False,
      XA_ATOM, &actual_type_return, &actual_format_return, &nitems_return,
      &bytes_after_return, reinterpret_cast<unsigned char**>(&prop_return));
  if (status != Success) {
    return false;
  }

  auto supported =
      std::find(prop_return, prop_return + nitems_return, feature) !=
      prop_return + nitems_return;
  XFree(prop_return);
  return supported;
}

bool LowLevelWindow_X11::SupportsFullscreenBorderlessWindow() const {
  Atom fullscreen = XInternAtom(g_dpy, "_NET_WM_STATE_FULLSCREEN", False);
  return NetWMSupported(g_dpy, fullscreen);
}

RenderTarget* LowLevelWindow_X11::CreateRenderTarget() {
  return new RenderTarget_X11(this);
}

/*
 * (c) 2005 Ben Anderson
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
