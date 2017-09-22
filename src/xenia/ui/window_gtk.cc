/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_linux.h"
#include "xenia/ui/window_gtk.h"

namespace xe {
namespace ui {

class FnWrapper {
 public:
  explicit FnWrapper(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Call() { fn_(); }

 private:
  std::function<void()> fn_;
};

std::unique_ptr<Window> Window::Create(Loop* loop, const std::wstring& title) {
  return std::make_unique<GTKWindow>(loop, title);
}

GTKWindow::GTKWindow(Loop* loop, const std::wstring& title)
    : Window(loop, title) {}

GTKWindow::~GTKWindow() {
  OnDestroy();
  if (window_) {
    gtk_widget_destroy(window_);
    window_ = nullptr;
  }
}

bool GTKWindow::Initialize() { return OnCreate(); }

void gtk_event_handler_(GtkWidget* widget, GdkEvent* event, gpointer data) {
  GTKWindow* window = reinterpret_cast<GTKWindow*>(data);
  switch (event->type) {
    case GDK_OWNER_CHANGE:
      window->HandleWindowOwnerChange(&(event->owner_change));
      break;
    case GDK_VISIBILITY_NOTIFY:
      window->HandleWindowVisibility(&(event->visibility));
      break;
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
      window->HandleKeyboard(&(event->key));
      break;
    case GDK_SCROLL:
    case GDK_BUTTON_PRESS:
    case GDK_MOTION_NOTIFY:
      window->HandleMouse(&(event->any));
      break;
    case GDK_FOCUS_CHANGE:
      window->HandleWindowFocus(&(event->focus_change));
      break;
    case GDK_CONFIGURE:
      window->HandleWindowResize(&(event->configure));
    default:
      // Do nothing
      return;
  }
}

void GTKWindow::Create() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), (gchar*)title_.c_str());
  gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 720);
  gtk_widget_show_all(window_);
  g_signal_connect(G_OBJECT(window_), "destroy", G_CALLBACK(gtk_main_quit),
                   NULL);
  g_signal_connect(G_OBJECT(window_), "event", G_CALLBACK(gtk_event_handler_),
                   reinterpret_cast<gpointer>(this));
}

bool GTKWindow::OnCreate() {
  loop()->PostSynchronous([this]() { this->Create(); });
  return super::OnCreate();
}

void GTKWindow::OnDestroy() { super::OnDestroy(); }

void GTKWindow::OnClose() {
  if (!closing_ && window_) {
    closing_ = true;
    gtk_widget_destroy(window_);
    window_ = nullptr;
  }
  super::OnClose();
}

bool GTKWindow::set_title(const std::wstring& title) {
  if (!super::set_title(title)) {
    return false;
  }
  gtk_window_set_title(GTK_WINDOW(window_), (gchar*)title.c_str());
  return true;
}

bool GTKWindow::SetIcon(const void* buffer, size_t size) {
  // TODO(dougvj) Set icon after changin buffer to the correct format. (the
  // call is gtk_window_set_icon)
  return false;
}

bool GTKWindow::is_fullscreen() const { return fullscreen_; }

void GTKWindow::ToggleFullscreen(bool fullscreen) {
  if (fullscreen == is_fullscreen()) {
    return;
  }

  fullscreen_ = fullscreen;

  if (fullscreen) {
    gtk_window_fullscreen(GTK_WINDOW(window_));
  } else {
    gtk_window_unfullscreen(GTK_WINDOW(window_));
  }
}

bool GTKWindow::is_bordered() const {
  return gtk_window_get_decorated(GTK_WINDOW(window_));
}

void GTKWindow::set_bordered(bool enabled) {
  if (is_fullscreen()) {
    // Don't screw with the borders if we're fullscreen.
    return;
  }

  gtk_window_set_decorated(GTK_WINDOW(window_), enabled);
}

void GTKWindow::set_cursor_visible(bool value) {
  if (is_cursor_visible_ == value) {
    return;
  }
  if (value) {
    // TODO(dougvj) Show and hide cursor
  } else {
  }
}

void GTKWindow::set_focus(bool value) {
  if (has_focus_ == value) {
    return;
  }
  if (window_) {
    if (value) {
      gtk_window_activate_focus(GTK_WINDOW(window_));
    } else {
      // TODO(dougvj) Check to see if we need to do something here to unset
      // the focus.
    }
  } else {
    has_focus_ = value;
  }
}

void GTKWindow::Resize(int32_t width, int32_t height) {
  gtk_window_resize(GTK_WINDOW(window_), width, height);
}

void GTKWindow::Resize(int32_t left, int32_t top, int32_t right,
                       int32_t bottom) {
  // TODO(dougvj) Verify that this is the desired behavior from this call
  gtk_window_move(GTK_WINDOW(window_), left, top);
  gtk_window_resize(GTK_WINDOW(window_), left - right, top - bottom);
}

void GTKWindow::OnResize(UIEvent* e) {
  int32_t width;
  int32_t height;
  gtk_window_get_size(GTK_WINDOW(window_), &width, &height);
  if (width != width_ || height != height_) {
    width_ = width;
    height_ = height;
    Layout();
  }
  super::OnResize(e);
}

void GTKWindow::Invalidate() {
  super::Invalidate();
  // TODO(dougvj) I am not sure what this is supposed to do
}

void GTKWindow::Close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  Close();
  OnClose();
}

void GTKWindow::OnMainMenuChange() {
  // We need to store the old handle for detachment
  static GtkWidget* box = nullptr;
  auto main_menu = reinterpret_cast<GTKMenuItem*>(main_menu_.get());
  if (main_menu && !is_fullscreen()) {
    if (box) gtk_widget_destroy(box);
    GtkWidget* menu = main_menu->handle();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(box), menu, FALSE, FALSE, 3);
    gtk_container_add(GTK_CONTAINER(window_), box);
  }
}

bool GTKWindow::HandleWindowOwnerChange(GdkEventOwnerChange* event) {
  if (event->type == GDK_OWNER_CHANGE) {
    if (event->reason == GDK_OWNER_CHANGE_DESTROY) {
      OnDestroy();
    } else if (event->reason == GDK_OWNER_CHANGE_CLOSE) {
      closing_ = true;
      Close();
      OnClose();
    }
    return true;
  }
  return false;
}

bool GTKWindow::HandleWindowResize(GdkEventConfigure* event) {
  if (event->type == GDK_CONFIGURE) {
    auto e = UIEvent(this);
    OnResize(&e);
    return true;
  }
  return false;
}

bool GTKWindow::HandleWindowVisibility(GdkEventVisibility* event) {
  // TODO(dougvj) The gdk docs say that this is deprecated because modern window
  // managers composite everything and nothing is truly hidden.
  if (event->type == GDK_VISIBILITY_NOTIFY) {
    if (event->state == GDK_VISIBILITY_UNOBSCURED) {
      auto e = UIEvent(this);
      OnVisible(&e);
    } else {
      auto e = UIEvent(this);
      OnHidden(&e);
    }
    return true;
  }
  return false;
}

bool GTKWindow::HandleWindowFocus(GdkEventFocus* event) {
  if (event->type == GDK_FOCUS_CHANGE) {
    if (!event->in) {
      has_focus_ = false;
      auto e = UIEvent(this);
      OnLostFocus(&e);
    } else {
      has_focus_ = true;
      auto e = UIEvent(this);
      OnGotFocus(&e);
    }
    return true;
  }
  return false;
}

bool GTKWindow::HandleMouse(GdkEventAny* event) {
  MouseEvent::Button button = MouseEvent::Button::kNone;
  int32_t dx = 0;
  int32_t dy = 0;
  int32_t x = 0;
  int32_t y = 0;
  switch (event->type) {
    default:
      // Double click/etc?
      return true;
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE: {
      GdkEventButton* e = reinterpret_cast<GdkEventButton*>(event);
      switch (e->button) {
        case 1:
          button = MouseEvent::Button::kLeft;
          break;
        case 3:
          button = MouseEvent::Button::kRight;
          break;
        case 2:
          button = MouseEvent::Button::kMiddle;
          break;
        case 4:
          button = MouseEvent::Button::kX1;
          break;
        case 5:
          button = MouseEvent::Button::kX2;
          break;
      }
      x = e->x;
      y = e->y;
      break;
    }
    case GDK_MOTION_NOTIFY: {
      GdkEventMotion* e = reinterpret_cast<GdkEventMotion*>(event);
      x = e->x;
      y = e->y;
      break;
    }
    case GDK_SCROLL: {
      GdkEventScroll* e = reinterpret_cast<GdkEventScroll*>(event);
      x = e->x;
      y = e->y;
      dx = e->delta_x;
      dy = e->delta_y;
      break;
    }
  }

  auto e = MouseEvent(this, button, x, y, dx, dy);
  switch (event->type) {
    case GDK_BUTTON_PRESS:
      OnMouseDown(&e);
      break;
    case GDK_BUTTON_RELEASE:
      OnMouseUp(&e);
      break;
    case GDK_MOTION_NOTIFY:
      OnMouseMove(&e);
      break;
    case GDK_SCROLL:
      OnMouseWheel(&e);
      break;
    default:
      return false;
  }
  return e.is_handled();
}

bool GTKWindow::HandleKeyboard(GdkEventKey* event) {
  unsigned int modifiers = event->state;
  bool shift_pressed = modifiers & GDK_SHIFT_MASK;
  bool ctrl_pressed = modifiers & GDK_CONTROL_MASK;
  bool alt_pressed = modifiers & GDK_META_MASK;
  bool super_pressed = modifiers & GDK_SUPER_MASK;
  auto e =
      KeyEvent(this, event->hardware_keycode, 1, event->type == GDK_KEY_RELEASE,
               shift_pressed, ctrl_pressed, alt_pressed, super_pressed);
  switch (event->type) {
    case GDK_KEY_PRESS:
      OnKeyDown(&e);
      break;
    case GDK_KEY_RELEASE:
      OnKeyUp(&e);
      break;
    // TODO(dougvj) GDK doesn't have a KEY CHAR event, so we will have to
    // figure out its equivalent here to call  OnKeyChar(&e);
    default:
      return false;
  }
  return e.is_handled();
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::wstring& text,
                                               const std::wstring& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<GTKMenuItem>(type, text, hotkey, callback);
}

static void _menu_activate_callback(GtkWidget* menu, gpointer data) {
  auto fn = reinterpret_cast<FnWrapper*>(data);
  fn->Call();
  delete fn;
}

GTKMenuItem::GTKMenuItem(Type type, const std::wstring& text,
                         const std::wstring& hotkey,
                         std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {
  switch (type) {
    case MenuItem::Type::kNormal:
    default:
      menu_ = gtk_menu_bar_new();
      break;
    case MenuItem::Type::kPopup:
      menu_ = gtk_menu_item_new_with_label((gchar*)xe::to_string(text).c_str());
      break;
    case MenuItem::Type::kSeparator:
      menu_ = gtk_separator_menu_item_new();
      break;
    case MenuItem::Type::kString:
      auto full_name = text;
      if (!hotkey.empty()) {
        full_name += L"\t" + hotkey;
      }
      menu_ = gtk_menu_item_new_with_label(
          (gchar*)xe::to_string(full_name).c_str());
      break;
  }
  if (GTK_IS_MENU_ITEM(menu_))
    g_signal_connect(menu_, "activate", G_CALLBACK(_menu_activate_callback),
                     (gpointer) new FnWrapper(callback));
}

GTKMenuItem::~GTKMenuItem() {
  if (menu_) {
  }
}

void GTKMenuItem::OnChildAdded(MenuItem* generic_child_item) {
  auto child_item = static_cast<GTKMenuItem*>(generic_child_item);
  switch (child_item->type()) {
    case MenuItem::Type::kNormal:
      // Nothing special.
      break;
    case MenuItem::Type::kPopup:
      if (GTK_IS_MENU_ITEM(menu_)) {
        assert(gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu_)) == nullptr);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_), child_item->handle());
      } else {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu_), child_item->handle());
      }
      break;
    case MenuItem::Type::kSeparator:
    case MenuItem::Type::kString:
      assert(GTK_IS_MENU_ITEM(menu_));
      // Get sub menu and if it doesn't exist create it
      GtkWidget* submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu_));
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
