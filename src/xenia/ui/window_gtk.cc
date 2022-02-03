/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <string>

#include <X11/Xlib-xcb.h>
#include <gdk/gdkx.h>
#include <xcb/xcb.h>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_linux.h"
#include "xenia/ui/surface_gnulinux.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window_gtk.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<GTKWindow>(app_context, title, desired_logical_width,
                                     desired_logical_height);
}

GTKWindow::GTKWindow(WindowedAppContext& app_context,
                     const std::string_view title,
                     uint32_t desired_logical_width,
                     uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width,
             desired_logical_height) {}

GTKWindow::~GTKWindow() {
  EnterDestructor();
  if (window_) {
    // Set window_ to null to ignore events from now on since this ui::GTKWindow
    // is entering an indeterminate state.
    GtkWidget* window = window_;
    window_ = nullptr;
    // Destroying the top-level window also destroys its children.
    drawing_area_ = nullptr;
    box_ = nullptr;
    gtk_widget_destroy(window);
  }
}

bool GTKWindow::OpenImpl() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(window_), GetTitle().c_str());

  // Create the vertical box container for the main menu and the drawing area.
  box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window_), box_);

  // Add the main menu (even if fullscreen was requested, for the initial layout
  // calculation).
  const GTKMenuItem* main_menu = static_cast<const GTKMenuItem*>(GetMainMenu());
  GtkWidget* main_menu_widget = main_menu ? main_menu->handle() : nullptr;
  if (main_menu_widget) {
    gtk_box_pack_start(GTK_BOX(box_), main_menu_widget, FALSE, FALSE, 0);
  }

  // Create the drawing area for creating the surface for, which will be the
  // client area of the window occupying all the window space not taken by the
  // main menu.
  drawing_area_ = gtk_drawing_area_new();
  gtk_box_pack_end(GTK_BOX(box_), drawing_area_, TRUE, TRUE, 0);
  // The desired size is the client (drawing) area size. Let GTK auto-size the
  // entire window around it (as well as the width of the menu actually if it
  // happens to be bigger - the desired size in the Window will be updated later
  // to reflect that).
  gtk_widget_set_size_request(drawing_area_, GetDesiredLogicalWidth(),
                              GetDesiredLogicalHeight());

  // Attach the event handlers.
  // Keyboard events are processed by the window, mouse events are processed
  // within, and by, the drawing (client) area.
  gtk_widget_set_events(window_, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
                                     GDK_FOCUS_CHANGE_MASK);
  gtk_widget_set_events(drawing_area_,
                        GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK |
                            GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_SCROLL_MASK);
  g_signal_connect(G_OBJECT(window_), "event",
                   G_CALLBACK(WindowEventHandlerThunk),
                   reinterpret_cast<gpointer>(this));
  g_signal_connect(G_OBJECT(drawing_area_), "event",
                   G_CALLBACK(DrawingAreaEventHandlerThunk),
                   reinterpret_cast<gpointer>(this));
  g_signal_connect(G_OBJECT(drawing_area_), "draw", G_CALLBACK(DrawHandler),
                   reinterpret_cast<gpointer>(this));

  // Finally show all the widgets in the window, including the main menu.
  gtk_widget_show_all(window_);

  // Remove the size request after finishing the initial layout because it makes
  // it impossible to make the window smaller.
  gtk_widget_set_size_request(drawing_area_, -1, -1);

  // After setting up the initial layout for non-fullscreen, enter fullscreen if
  // requested.
  if (IsFullscreen()) {
    if (main_menu_widget) {
      gtk_container_remove(GTK_CONTAINER(box_), main_menu_widget);
    }
    gtk_window_fullscreen(GTK_WINDOW(window_));
  }

  // Make sure the initial state after opening is reported to the common Window
  // class no matter how GTK sends the events.
  {
    WindowDestructionReceiver destruction_receiver(this);

    // TODO(Triang3l): Report the desired client area size.

    GtkAllocation drawing_area_allocation;
    gtk_widget_get_allocation(drawing_area_, &drawing_area_allocation);
    OnActualSizeUpdate(uint32_t(drawing_area_allocation.width),
                       uint32_t(drawing_area_allocation.height),
                       destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return true;
    }

    if (gtk_window_has_toplevel_focus(GTK_WINDOW(window_))) {
      OnFocusUpdate(true, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        return true;
      }
    }
  }

  return true;
}

void GTKWindow::RequestCloseImpl() { gtk_window_close(GTK_WINDOW(window_)); }

void GTKWindow::ApplyNewFullscreen() {
  // Various functions here may trigger events that may result in the listeners
  // being invoked, and potentially cause the destruction of the window or
  // fullscreen being toggled from inside this function.
  WindowDestructionReceiver destruction_receiver(this);

  const GTKMenuItem* main_menu = static_cast<const GTKMenuItem*>(GetMainMenu());
  GtkWidget* main_menu_widget = main_menu ? main_menu->handle() : nullptr;

  // Changing the menu and the fullscreen state may change the size of the
  // drawing area too, don't handle the resize multiple times (also potentially
  // with the listeners changing the desired fullscreen if called from the
  // handling of some event like GDK_CONFIGURE).
  BeginBatchedSizeUpdate();

  if (IsFullscreen()) {
    if (main_menu_widget) {
      gtk_container_remove(GTK_CONTAINER(box_), main_menu_widget);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        if (!destruction_receiver.IsWindowDestroyed()) {
          EndBatchedSizeUpdate(destruction_receiver);
        }
        return;
      }
    }
    gtk_window_fullscreen(GTK_WINDOW(window_));
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }
  } else {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }
    if (main_menu_widget) {
      gtk_box_pack_start(GTK_BOX(box_), main_menu_widget, FALSE, FALSE, 0);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        if (!destruction_receiver.IsWindowDestroyed()) {
          EndBatchedSizeUpdate(destruction_receiver);
        }
        return;
      }
      // If the new menu is used for the first time, it will be in the hidden
      // state initially. The menu might have been changed while in fullscreen
      // without having shown it.
      gtk_widget_show_all(main_menu_widget);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        if (!destruction_receiver.IsWindowDestroyed()) {
          EndBatchedSizeUpdate(destruction_receiver);
        }
        return;
      }
    }
  }

  EndBatchedSizeUpdate(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void GTKWindow::ApplyNewTitle() {
  gtk_window_set_title(GTK_WINDOW(window_), GetTitle().c_str());
}

void GTKWindow::ApplyNewMainMenu(MenuItem* old_main_menu) {
  if (IsFullscreen()) {
    // The menu will be set when exiting fullscreen.
    return;
  }
  // The fullscreen state may have been changed by some callback invoked, such
  // as the configure (resize) one, recheck it after making changes also.

  WindowDestructionReceiver destruction_receiver(this);

  // Changing the menu may change the size of the drawing area too, and here the
  // menu may be changed twice (to detach the old one and to attach the new),
  // don't handle the resize multiple times.
  BeginBatchedSizeUpdate();

  if (old_main_menu) {
    const GTKMenuItem& old_gtk_main_menu =
        *static_cast<const GTKMenuItem*>(old_main_menu);
    gtk_container_remove(GTK_CONTAINER(box_), old_gtk_main_menu.handle());
    if (destruction_receiver.IsWindowDestroyedOrClosed() || IsFullscreen()) {
      if (!destruction_receiver.IsWindowDestroyed()) {
        EndBatchedSizeUpdate(destruction_receiver);
      }
      return;
    }
  }

  const GTKMenuItem* new_main_menu =
      static_cast<const GTKMenuItem*>(GetMainMenu());
  if (!new_main_menu) {
    EndBatchedSizeUpdate(destruction_receiver);
    return;
  }
  GtkWidget* new_main_menu_widget = new_main_menu->handle();
  gtk_box_pack_start(GTK_BOX(box_), new_main_menu_widget, FALSE, FALSE, 0);
  if (destruction_receiver.IsWindowDestroyedOrClosed() || IsFullscreen()) {
    if (!destruction_receiver.IsWindowDestroyed()) {
      EndBatchedSizeUpdate(destruction_receiver);
    }
    return;
  }
  // If the new menu is used for the first time, it will be in the hidden state
  // initially.
  gtk_widget_show_all(new_main_menu_widget);
  if (destruction_receiver.IsWindowDestroyedOrClosed() || IsFullscreen()) {
    if (!destruction_receiver.IsWindowDestroyed()) {
      EndBatchedSizeUpdate(destruction_receiver);
    }
    return;
  }

  EndBatchedSizeUpdate(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void GTKWindow::FocusImpl() { gtk_window_activate_focus(GTK_WINDOW(window_)); }

std::unique_ptr<Surface> GTKWindow::CreateSurfaceImpl(
    Surface::TypeFlags allowed_types) {
  GdkDisplay* display = gtk_widget_get_display(window_);
  GdkWindow* drawing_area_window = gtk_widget_get_window(drawing_area_);
  bool type_known = false;
  bool type_supported_by_display = false;
  if (allowed_types & Surface::kTypeFlag_XcbWindow) {
    type_known = true;
    if (GDK_IS_X11_DISPLAY(display)) {
      type_supported_by_display = true;
      return std::make_unique<XcbWindowSurface>(
          XGetXCBConnection(gdk_x11_display_get_xdisplay(display)),
          gdk_x11_window_get_xid(drawing_area_window));
    }
  }
  // TODO(Triang3l): Wayland surface.
  if (type_known && !type_supported_by_display) {
    XELOGE(
        "GTKWindow: The window system of the GTK window is not supported by "
        "Xenia");
  }
  return nullptr;
}

void GTKWindow::RequestPaintImpl() { gtk_widget_queue_draw(drawing_area_); }

void GTKWindow::HandleSizeUpdate(
    WindowDestructionReceiver& destruction_receiver) {
  if (!drawing_area_) {
    // Batched size update ended when the window has already been closed, for
    // instance.
    return;
  }

  // TODO(Triang3l): Report the desired client area size.

  GtkAllocation drawing_area_allocation;
  gtk_widget_get_allocation(drawing_area_, &drawing_area_allocation);
  OnActualSizeUpdate(uint32_t(drawing_area_allocation.width),
                     uint32_t(drawing_area_allocation.height),
                     destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void GTKWindow::BeginBatchedSizeUpdate() {
  // It's okay if batched_size_update_contained_* are not false when beginning
  // a batched update, in case the new batched update was started by a window
  // listener called from within EndBatchedSizeUpdate.
  ++batched_size_update_depth_;
}

void GTKWindow::EndBatchedSizeUpdate(
    WindowDestructionReceiver& destruction_receiver) {
  assert_not_zero(batched_size_update_depth_);
  if (--batched_size_update_depth_) {
    return;
  }
  // Resetting batched_size_update_contained_* in closing, not opening, because
  // a listener may start a new batch, and finish it, and there won't be need to
  // handle the deferred messages twice.
  if (batched_size_update_contained_configure_) {
    batched_size_update_contained_configure_ = false;
    HandleSizeUpdate(destruction_receiver);
    if (destruction_receiver.IsWindowDestroyed()) {
      return;
    }
  }
  if (batched_size_update_contained_draw_) {
    batched_size_update_contained_draw_ = false;
    RequestPaint();
  }
}

bool GTKWindow::HandleMouse(GdkEvent* event,
                            WindowDestructionReceiver& destruction_receiver) {
  MouseEvent::Button button = MouseEvent::Button::kNone;
  int32_t x = 0;
  int32_t y = 0;
  int32_t scroll_x = 0;
  int32_t scroll_y = 0;
  switch (event->type) {
    case GDK_MOTION_NOTIFY: {
      auto motion_event = reinterpret_cast<const GdkEventMotion*>(event);
      x = motion_event->x;
      y = motion_event->y;
    } break;
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE: {
      auto button_event = reinterpret_cast<const GdkEventButton*>(event);
      switch (button_event->button) {
        case 1:
          button = MouseEvent::Button::kLeft;
          break;
        case 2:
          button = MouseEvent::Button::kMiddle;
          break;
        case 3:
          button = MouseEvent::Button::kRight;
          break;
        case 4:
          button = MouseEvent::Button::kX1;
          break;
        case 5:
          button = MouseEvent::Button::kX2;
          break;
        default:
          // Still handle the movement.
          break;
      }
      x = button_event->x;
      y = button_event->y;
    } break;
    case GDK_SCROLL: {
      auto scroll_event = reinterpret_cast<const GdkEventScroll*>(event);
      x = scroll_event->x;
      y = scroll_event->y;
      scroll_x = scroll_event->delta_x * MouseEvent::kScrollPerDetent;
      // In GDK, positive is towards the bottom of the screen, not forward from
      // the user.
      scroll_y = -scroll_event->delta_y * MouseEvent::kScrollPerDetent;
    } break;
    default:
      return false;
  }

  MouseEvent e(this, button, x, y, scroll_x, scroll_y);
  switch (event->type) {
    case GDK_MOTION_NOTIFY:
      OnMouseMove(e, destruction_receiver);
      break;
    case GDK_BUTTON_PRESS:
      OnMouseDown(e, destruction_receiver);
      break;
    case GDK_BUTTON_RELEASE:
      OnMouseUp(e, destruction_receiver);
      break;
    case GDK_SCROLL:
      OnMouseWheel(e, destruction_receiver);
      break;
    default:
      break;
  }
  // Returning immediately anyway - no need to check
  // destruction_receiver.IsWindowDestroyed().
  return e.is_handled();
}

bool GTKWindow::HandleKeyboard(
    GdkEventKey* event, WindowDestructionReceiver& destruction_receiver) {
  unsigned int modifiers = event->state;
  bool shift_pressed = modifiers & GDK_SHIFT_MASK;
  bool ctrl_pressed = modifiers & GDK_CONTROL_MASK;
  bool alt_pressed = modifiers & GDK_META_MASK;
  bool super_pressed = modifiers & GDK_SUPER_MASK;
  uint32_t key_char = gdk_keyval_to_unicode(event->keyval);
  // TODO(Triang3l): event->hardware_keycode to VirtualKey translation.
  KeyEvent e(this, VirtualKey(event->hardware_keycode), 1,
             event->type == GDK_KEY_RELEASE, shift_pressed, ctrl_pressed,
             alt_pressed, super_pressed);
  switch (event->type) {
    case GDK_KEY_PRESS:
      OnKeyDown(e, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        return e.is_handled();
      }
      if (key_char > 0) {
        OnKeyChar(e, destruction_receiver);
      }
      break;
    case GDK_KEY_RELEASE:
      OnKeyUp(e, destruction_receiver);
      break;
    default:
      break;
  }
  // Returning immediately anyway - no need to check
  // destruction_receiver.IsWindowDestroyed().
  return e.is_handled();
}

gboolean GTKWindow::WindowEventHandler(GdkEvent* event) {
  switch (event->type) {
    case GDK_DELETE:
    // In case the widget was somehow forcibly destroyed without GDK_DELETE.
    case GDK_DESTROY: {
      WindowDestructionReceiver destruction_receiver(this);
      OnBeforeClose(destruction_receiver);
      if (destruction_receiver.IsWindowDestroyed()) {
        break;
      }
      // Set window_ to null to ignore events from now on since this
      // ui::GTKWindow is entering an indeterminate state - this should be done
      // at some point in closing anyway.
      GtkWidget* window = window_;
      window_ = nullptr;
      // Destroying the top-level window also destroys its children.
      drawing_area_ = nullptr;
      box_ = nullptr;
      if (event->type != GDK_DESTROY) {
        gtk_widget_destroy(window);
      }
      OnAfterClose();
    } break;

    case GDK_FOCUS_CHANGE: {
      auto focus_event = reinterpret_cast<const GdkEventFocus*>(event);
      WindowDestructionReceiver destruction_receiver(this);
      OnFocusUpdate(bool(focus_event->in), destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        break;
      }
    } break;

    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE: {
      WindowDestructionReceiver destruction_receiver(this);
      HandleKeyboard(reinterpret_cast<GdkEventKey*>(event),
                     destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        break;
      }
    } break;

    default:
      break;
  }

  // The window might have been destroyed by the handlers, don't interact with
  // *this in this function from now on.

  return GDK_EVENT_PROPAGATE;
}

gboolean GTKWindow::WindowEventHandlerThunk(GtkWidget* widget, GdkEvent* event,
                                            gpointer user_data) {
  GTKWindow* window = reinterpret_cast<GTKWindow*>(user_data);
  if (!window || widget != window->window_ ||
      reinterpret_cast<const GdkEventAny*>(event)->window !=
          gtk_widget_get_window(window->window_)) {
    return GDK_EVENT_PROPAGATE;
  }
  return window->WindowEventHandler(event);
}

gboolean GTKWindow::DrawingAreaEventHandler(GdkEvent* event) {
  switch (event->type) {
    case GDK_MOTION_NOTIFY:
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    case GDK_SCROLL: {
      WindowDestructionReceiver destruction_receiver(this);
      HandleMouse(event, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        break;
      }
    } break;

    case GDK_CONFIGURE: {
      if (batched_size_update_depth_) {
        batched_size_update_contained_configure_ = true;
      } else {
        WindowDestructionReceiver destruction_receiver(this);
        HandleSizeUpdate(destruction_receiver);
        if (destruction_receiver.IsWindowDestroyedOrClosed()) {
          break;
        }
      }
    } break;
    default:
      break;
  }

  // The window might have been destroyed by the handlers, don't interact with
  // *this in this function from now on.

  return GDK_EVENT_PROPAGATE;
}

gboolean GTKWindow::DrawingAreaEventHandlerThunk(GtkWidget* widget,
                                                 GdkEvent* event,
                                                 gpointer user_data) {
  GTKWindow* window = reinterpret_cast<GTKWindow*>(user_data);
  if (!window || widget != window->drawing_area_ ||
      reinterpret_cast<const GdkEventAny*>(event)->window !=
          gtk_widget_get_window(window->drawing_area_)) {
    return GDK_EVENT_PROPAGATE;
  }
  return window->DrawingAreaEventHandler(event);
}

gboolean GTKWindow::DrawHandler(GtkWidget* widget, cairo_t* cr,
                                gpointer user_data) {
  GTKWindow* window = reinterpret_cast<GTKWindow*>(user_data);
  if (!window || widget != window->drawing_area_) {
    return FALSE;
  }
  if (window->batched_size_update_depth_) {
    window->batched_size_update_contained_draw_ = true;
  } else {
    window->OnPaint();
  }
  return TRUE;
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::string& text,
                                               const std::string& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<GTKMenuItem>(type, text, hotkey, callback);
}

void GTKMenuItem::ActivateHandler(GtkWidget* menu_item, gpointer user_data) {
  static_cast<GTKMenuItem*>(user_data)->OnSelected();
  // The menu item might have been destroyed by its OnSelected, don't do
  // anything with it here from now on.
}

GTKMenuItem::GTKMenuItem(Type type, const std::string& text,
                         const std::string& hotkey,
                         std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {
  std::string label = text;
  // TODO(dougvj) Would we ever need to escape underscores?
  // Replace & with _ for gtk to see the memonic
  std::replace(label.begin(), label.end(), '&', '_');
  const gchar* gtk_label = reinterpret_cast<const gchar*>(label.c_str());
  switch (type) {
    case MenuItem::Type::kNormal:
    default:
      menu_ = gtk_menu_bar_new();
      break;
    case MenuItem::Type::kPopup:
      menu_ = gtk_menu_item_new_with_mnemonic(gtk_label);
      break;
    case MenuItem::Type::kSeparator:
      menu_ = gtk_separator_menu_item_new();
      break;
    case MenuItem::Type::kString:
      auto full_name = text;
      if (!hotkey.empty()) {
        full_name += "\t" + hotkey;
      }
      menu_ = gtk_menu_item_new_with_mnemonic(gtk_label);
      break;
  }
  if (menu_) {
    // Own the object because it may be detached from and re-attached to a
    // Window.
    g_object_ref_sink(menu_);
    if (GTK_IS_MENU_ITEM(menu_)) {
      g_signal_connect(menu_, "activate", G_CALLBACK(ActivateHandler),
                       reinterpret_cast<gpointer>(this));
    }
  }
}

GTKMenuItem::~GTKMenuItem() {
  if (menu_) {
    g_object_unref(menu_);
  }
}

void GTKMenuItem::OnChildAdded(MenuItem* generic_child_item) {
  auto child_item = static_cast<GTKMenuItem*>(generic_child_item);
  GtkWidget* submenu = nullptr;
  switch (child_item->type()) {
    case MenuItem::Type::kNormal:
      // Nothing special.
      break;
    case MenuItem::Type::kPopup:
      if (GTK_IS_MENU_ITEM(menu_)) {
        submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu_));
        // Get sub menu and if it doesn't exist create it
        if (submenu == nullptr) {
          submenu = gtk_menu_new();
          gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_), submenu);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), child_item->handle());
      } else {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu_), child_item->handle());
      }
      break;
    case MenuItem::Type::kSeparator:
    case MenuItem::Type::kString:
      assert(GTK_IS_MENU_ITEM(menu_));
      // Get sub menu and if it doesn't exist create it
      submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu_));
      if (submenu == nullptr) {
        submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_), submenu);
      }
      gtk_menu_shell_append(GTK_MENU_SHELL(submenu), child_item->handle());
      break;
  }
}

// TODO(dougvj)
void GTKMenuItem::OnChildRemoved(MenuItem* generic_child_item) {
  assert_always();
}

}  // namespace ui

}  // namespace xe
