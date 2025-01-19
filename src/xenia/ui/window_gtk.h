/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_GTK_H_
#define XENIA_UI_WINDOW_GTK_H_

#include <memory>
#include <string>

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <xcb/xcb.h>

#include "xenia/base/platform_linux.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

class GTKWindow : public Window {
  using super = Window;

 public:
  GTKWindow(WindowedAppContext& app_context, const std::string_view title,
            uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~GTKWindow() override;

  // Will be null if the window hasn't been successfully opened yet, or has been
  // closed.
  GtkWidget* window() const { return window_; }

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void ApplyNewMainMenu(MenuItem* old_main_menu) override;
  // Mouse capture seems to happen implicitly compared to Windows.
  void FocusImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  void HandleSizeUpdate(WindowDestructionReceiver& destruction_receiver);
  // For updating multiple factors that may influence the window size at once,
  // without handling the configure event multiple times (that may not only
  // result in wasted handling, but also in the state potentially changed to an
  // inconsistent one in the middle of a size update by the listeners).
  void BeginBatchedSizeUpdate();
  void EndBatchedSizeUpdate(WindowDestructionReceiver& destruction_receiver);

  // Translates a gtk virtual key to xenia ui::VirtualKey
  static VirtualKey TranslateVirtualKey(guint keyval);

  // Handling events related to the whole window.
  bool HandleMouse(GdkEvent* event,
                   WindowDestructionReceiver& destruction_receiver);
  bool HandleKeyboard(GdkEventKey* event,
                      WindowDestructionReceiver& destruction_receiver);
  gboolean WindowEventHandler(GdkEvent* event);
  static gboolean WindowEventHandlerThunk(GtkWidget* widget, GdkEvent* event,
                                          gpointer user_data);

  // Handling events related specifically to the drawing (client) area.
  gboolean DrawingAreaEventHandler(GdkEvent* event);
  static gboolean DrawingAreaEventHandlerThunk(GtkWidget* widget,
                                               GdkEvent* event,
                                               gpointer user_data);
  static gboolean DrawHandler(GtkWidget* widget, cairo_t* cr, gpointer data);

  // Non-owning (initially floating) references to the widgets.
  GtkWidget* window_ = nullptr;
  GtkWidget* box_ = nullptr;
  GtkWidget* drawing_area_ = nullptr;

  uint32_t batched_size_update_depth_ = 0;
  bool batched_size_update_contained_configure_ = false;
  bool batched_size_update_contained_draw_ = false;
};

class GTKMenuItem : public MenuItem {
 public:
  GTKMenuItem(Type type, const std::string& text, const std::string& hotkey,
              std::function<void()> callback);
  ~GTKMenuItem() override;

  GtkWidget* handle() const { return menu_; }

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  static void ActivateHandler(GtkWidget* menu_item, gpointer user_data);

  // An owning reference because a menu may be transferred between windows.
  GtkWidget* menu_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_WIN_H_
