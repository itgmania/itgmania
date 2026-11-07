#include "LoadingWindow_Gtk.h"

#include <gtk/gtk.h>

#include <cstdint>
#include <string>

#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurface_Load.h"
#include "RageUtil.h"
#include "global.h"

static GtkWidget* label;
static GtkWidget* window;
static GtkWidget* splash;
static GtkWidget* progressBar;

LoadingWindow_Gtk::LoadingWindow_Gtk() {}

std::string LoadingWindow_Gtk::Init() {
  // Need to use external library to load this image. Native loader seems broken
  // :/
  const gchar* splash_image_path = "Data/splash.png";
  GtkWidget* vbox;

  gtk_disable_setlocale();
  if (!gtk_init_check(&g_argc, &g_argv)) {
    return "Couldn't initialize gtk (cannot open display)";
  }

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
  gtk_widget_set_size_request(window, -1, -1);
  gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_window_set_role(GTK_WINDOW(window), "sm-startup");
  // gtk_window_set_icon( GTK_WINDOW(window), );
  gtk_widget_realize(window);

  splash = gtk_image_new_from_pixbuf(
      gdk_pixbuf_new_from_file(splash_image_path, nullptr));

  label = gtk_label_new(nullptr);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_label_set_line_wrap(GTK_LABEL(label), FALSE);

  progressBar = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), 0.0);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window), vbox);
  gtk_box_pack_start(GTK_BOX(vbox), splash, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(vbox), progressBar, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(vbox), label, TRUE, TRUE, 0);

  gtk_widget_show_all(window);
  gtk_main_iteration_do(FALSE);
  return "";
}

LoadingWindow_Gtk::~LoadingWindow_Gtk() {
  gtk_widget_hide(window);
  g_signal_emit_by_name(G_OBJECT(window), "destroy");
  while (gtk_events_pending()) {
    gtk_main_iteration_do(FALSE);
  }
}

void LoadingWindow_Gtk::SetText(std::string s) {
  gtk_label_set_text(GTK_LABEL(label), s.c_str());
  gtk_widget_show(label);
  gtk_main_iteration_do(FALSE);
}

static void DeletePixels(guchar* pixels, gpointer data) {
  delete[] (uint8_t*)pixels;
}

static GdkPixbuf* MakePixbuf(const RageSurface* src) {
  RageSurface* surface = CreateSurface(
      src->w_, src->h_, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
  RageSurfaceUtils::Blit(src, surface, -1, -1);

  GdkPixbuf* buf = gdk_pixbuf_new_from_data(
      surface->pixels_, GDK_COLORSPACE_RGB, true, 8, surface->w_, surface->h_,
      surface->pitch_, DeletePixels, nullptr);

  if (buf != nullptr) {
    surface->pixels_owned_ = false;
  }

  delete surface;
  return buf;
}

void LoadingWindow_Gtk::SetIcon(const RageSurface* icon) {
  GdkPixbuf* buf = MakePixbuf(icon);
  if (buf != nullptr) {
    gtk_window_set_icon(GTK_WINDOW(window), buf);
    g_object_unref(buf);
  }
  gtk_main_iteration_do(FALSE);
}

void LoadingWindow_Gtk::SetSplash(const RageSurface* p_splash) {
  GdkPixbuf* buf = MakePixbuf(p_splash);
  if (buf != nullptr) {
    gtk_image_set_from_pixbuf(GTK_IMAGE(splash), buf);
    g_object_unref(buf);
  }
  gtk_main_iteration_do(FALSE);
}

static void UpdateProgress(int progress, int total_work) {
  gdouble fraction = (total_work > 0 ? progress / (gdouble)total_work : 0);
  if (fraction > 1.0) {
    fraction = 1.0;
  }
  if (fraction < 0.0) {
    fraction = 0.0;
  }
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), fraction);
  gtk_main_iteration_do(FALSE);
}

void LoadingWindow_Gtk::SetProgress(const int progress) {
  LoadingWindow::SetProgress(progress);
  UpdateProgress(progress_, total_work_);
}

void LoadingWindow_Gtk::SetTotalWork(const int total_work) {
  LoadingWindow::SetTotalWork(total_work);
  UpdateProgress(progress_, total_work_);
}

void LoadingWindow_Gtk::SetIndeterminate(bool indeterminate) {
  LoadingWindow::SetIndeterminate(indeterminate);

  gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progressBar));
  gtk_main_iteration_do(FALSE);
}

/*
 * (c) 2003-2004 Glenn Maynard, Sean Burke
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
