/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_H_
#define XENIA_UI_WINDOW_H_

#include <memory>
#include <string>

#include "el/element.h"
#include "el/graphics/renderer.h"
#include "xenia/base/delegate.h"
#include "xenia/ui/graphics_context.h"
#include "xenia/ui/loop.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

typedef void* NativePlatformHandle;
typedef void* NativeWindowHandle;

class Window {
 public:
  static std::unique_ptr<Window> Create(Loop* loop, const std::wstring& title);

  virtual ~Window();

  Loop* loop() const { return loop_; }
  virtual NativePlatformHandle native_platform_handle() const = 0;
  virtual NativeWindowHandle native_handle() const = 0;

  MenuItem* main_menu() const { return main_menu_.get(); }
  void set_main_menu(std::unique_ptr<MenuItem> main_menu) {
    main_menu_ = std::move(main_menu);
    OnMainMenuChange();
  }

  const std::wstring& title() const { return title_; }
  virtual bool set_title(const std::wstring& title) {
    if (title == title_) {
      return false;
    }
    title_ = title;
    return true;
  }

  virtual bool is_fullscreen() const { return false; }
  virtual void ToggleFullscreen(bool fullscreen) {}

  virtual bool is_bordered() const { return false; }
  virtual void set_bordered(bool enabled) {}

  bool has_focus() const { return has_focus_; }
  virtual void set_focus(bool value) { has_focus_ = value; }

  bool is_cursor_visible() const { return is_cursor_visible_; }
  virtual void set_cursor_visible(bool value) { is_cursor_visible_ = value; }

  int32_t width() const { return width_; }
  int32_t height() const { return height_; }
  virtual void Resize(int32_t width, int32_t height) = 0;
  virtual void Resize(int32_t left, int32_t top, int32_t right,
                      int32_t bottom) = 0;

  GraphicsContext* context() const { return context_.get(); }
  el::graphics::Renderer* renderer() const { return renderer_.get(); }
  el::Element* root_element() const { return root_element_.get(); }

  virtual bool Initialize() { return true; }
  void set_context(std::unique_ptr<GraphicsContext> context) {
    context_ = std::move(context);
    if (context_) {
      MakeReady();
    }
  }

  bool LoadLanguage(std::string filename);
  bool LoadSkin(std::string filename);

  void Layout();
  virtual void Invalidate();

  virtual void Close() = 0;

 public:
  Delegate<UIEvent*> on_closing;
  Delegate<UIEvent*> on_closed;

  Delegate<int> on_command;

  Delegate<UIEvent*> on_resize;
  Delegate<UIEvent*> on_layout;
  Delegate<UIEvent*> on_painting;
  Delegate<UIEvent*> on_paint;
  Delegate<UIEvent*> on_painted;

  Delegate<UIEvent*> on_visible;
  Delegate<UIEvent*> on_hidden;

  Delegate<UIEvent*> on_got_focus;
  Delegate<UIEvent*> on_lost_focus;

  Delegate<KeyEvent*> on_key_down;
  Delegate<KeyEvent*> on_key_up;
  Delegate<KeyEvent*> on_key_char;

  Delegate<MouseEvent*> on_mouse_down;
  Delegate<MouseEvent*> on_mouse_move;
  Delegate<MouseEvent*> on_mouse_up;
  Delegate<MouseEvent*> on_mouse_wheel;

 protected:
  Window(Loop* loop, const std::wstring& title);

  virtual bool MakeReady();

  virtual bool InitializeElemental(Loop* loop,
                                   el::graphics::Renderer* renderer);

  virtual bool OnCreate();
  virtual void OnMainMenuChange();
  virtual void OnClose();
  virtual void OnDestroy();

  virtual void OnResize(UIEvent* e);
  virtual void OnLayout(UIEvent* e);
  virtual void OnPaint(UIEvent* e);

  virtual void OnVisible(UIEvent* e);
  virtual void OnHidden(UIEvent* e);

  virtual void OnGotFocus(UIEvent* e);
  virtual void OnLostFocus(UIEvent* e);

  virtual void OnKeyDown(KeyEvent* e);
  virtual void OnKeyUp(KeyEvent* e);
  virtual void OnKeyChar(KeyEvent* e);

  virtual void OnMouseDown(MouseEvent* e);
  virtual void OnMouseMove(MouseEvent* e);
  virtual void OnMouseUp(MouseEvent* e);
  virtual void OnMouseWheel(MouseEvent* e);

  el::ModifierKeys GetModifierKeys();
  void OnKeyPress(KeyEvent* e, bool is_down, bool is_char);
  bool CheckShortcutKey(KeyEvent* e, el::SpecialKey special_key, bool is_down);

  Loop* loop_ = nullptr;
  std::unique_ptr<MenuItem> main_menu_;
  std::wstring title_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  bool has_focus_ = true;
  bool is_cursor_visible_ = true;

  std::unique_ptr<GraphicsContext> context_;
  std::unique_ptr<el::graphics::Renderer> renderer_;
  std::unique_ptr<el::Element> root_element_;

  uint32_t frame_count_ = 0;
  uint32_t fps_ = 0;
  uint64_t fps_update_time_ = 0;
  uint64_t fps_frame_count_ = 0;

  bool modifier_shift_pressed_ = false;
  bool modifier_cntrl_pressed_ = false;
  bool modifier_alt_pressed_ = false;
  bool modifier_super_pressed_ = false;
  uint64_t last_click_time_ = 0;
  int last_click_x_ = 0;
  int last_click_y_ = 0;
  int last_click_counter_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_H_
