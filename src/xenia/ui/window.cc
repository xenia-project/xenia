/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window.h"

#include "el/animation_manager.h"
#include "el/elemental_forms.h"
#include "el/io/file_manager.h"
#include "el/io/posix_file_system.h"
#include "el/io/win32_res_file_system_win.h"
#include "el/message_handler.h"
#include "el/text/font_manager.h"
#include "el/util/debug.h"
#include "el/util/metrics.h"
#include "el/util/string_table.h"
#include "el/util/timer.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/ui/elemental_drawer.h"

namespace xe {
namespace ui {

constexpr bool kContinuousRepaint = false;
constexpr bool kShowPresentFps = kContinuousRepaint;

// Enables long press behaviors (context menu, etc).
constexpr bool kTouch = false;

constexpr uint64_t kDoubleClickDelayMillis = 600;
constexpr double kDoubleClickDistance = 5;

constexpr int32_t kMouseWheelDetent = 120;

// Ref count of elemental initializations.
int32_t elemental_initialization_count_ = 0;

class RootElement : public el::Element {
 public:
  explicit RootElement(Window* owner) : owner_(owner) {}
  void OnInvalid() override { owner_->Invalidate(); }

 private:
  Window* owner_ = nullptr;
};

Window::Window(Loop* loop, const std::wstring& title)
    : loop_(loop), title_(title) {}

Window::~Window() {
  // Context must have been cleaned up already in OnDestroy.
  assert_null(context_.get());
}

bool Window::InitializeElemental(Loop* loop, el::graphics::Renderer* renderer) {
  GraphicsContextLock context_lock(context_.get());

  if (++elemental_initialization_count_ > 1) {
    return true;
  }

  // Need to pass off the Loop we want Elemental to use.
  // TODO(benvanik): give the callback to elemental instead.
  Loop::set_elemental_loop(loop);

  if (!el::Initialize(renderer)) {
    XELOGE("Failed to initialize elemental core");
    return false;
  }

  el::io::FileManager::RegisterFileSystem(
      std::make_unique<el::io::Win32ResFileSystem>("IDR_default_resources_"));
  el::io::FileManager::RegisterFileSystem(
      std::make_unique<el::io::PosixFileSystem>(
          "third_party/elemental-forms/testbed/resources/"));

  // Load default translations.
  el::util::StringTable::get()->Load("default_language/language_en.tb.txt");

  // Load the default skin. Hosting controls may load additional skins later.
  if (!LoadSkin("default_skin/skin.tb.txt")) {
    XELOGE("Failed to load default skin");
    return false;
  }

// Register font renderers.
#ifdef EL_FONT_RENDERER_TBBF
  void register_tbbf_font_renderer();
  register_tbbf_font_renderer();
#endif
#ifdef EL_FONT_RENDERER_STB
  void register_stb_font_renderer();
  register_stb_font_renderer();
#endif
#ifdef EL_FONT_RENDERER_FREETYPE
  void register_freetype_font_renderer();
  register_freetype_font_renderer();
#endif
  auto font_manager = el::text::FontManager::get();

// Add fonts we can use to the font manager.
#if defined(EL_FONT_RENDERER_STB) || defined(EL_FONT_RENDERER_FREETYPE)
  font_manager->AddFontInfo("fonts/vera.ttf", "Default");
#endif
#ifdef EL_FONT_RENDERER_TBBF
  font_manager->AddFontInfo("fonts/segoe_white_with_shadow.tb.txt", "Default");
#endif

  // Set the default font description for elements to one of the fonts we just
  // added.
  el::FontDescription fd;
  fd.set_id(TBIDC("Default"));
  fd.set_size(el::Skin::get()->dimension_converter()->DpToPx(14));
  font_manager->set_default_font_description(fd);

  // Create the font now.
  auto font =
      font_manager->CreateFontFace(font_manager->default_font_description());
  assert(font != nullptr);
  return true;
}

bool Window::OnCreate() { return true; }

bool Window::MakeReady() {
  renderer_ = std::make_unique<ElementalDrawer>(this);

  // Initialize elemental.
  // TODO(benvanik): once? Do we care about multiple controls?
  if (!InitializeElemental(loop_, renderer_.get())) {
    XELOGE("Unable to initialize elemental-forms");
    return false;
  }

  // TODO(benvanik): setup elements.
  root_element_ = std::make_unique<RootElement>(this);
  root_element_->set_background_skin(TBIDC("background"));
  root_element_->set_rect({0, 0, width(), height()});

  // el::util::ShowDebugInfoSettingsForm(root_element_.get());

  return true;
}

void Window::OnMainMenuChange() {}

void Window::OnClose() {
  UIEvent e(this);
  on_closing(&e);
  on_closed(&e);
}

void Window::OnDestroy() {
  if (!context_) {
    return;
  }

  if (--elemental_initialization_count_ == 0) {
    el::Shutdown();
  }

  // Context must go last.
  root_element_.reset();
  renderer_.reset();
  context_.reset();
}

bool Window::LoadLanguage(std::string filename) {
  return el::util::StringTable::get()->Load(filename.c_str());
}

bool Window::LoadSkin(std::string filename) {
  return el::Skin::get()->Load(filename.c_str());
}

void Window::Layout() {
  UIEvent e(this);
  OnLayout(&e);
}

void Window::Invalidate() {}

void Window::OnResize(UIEvent* e) { on_resize(e); }

void Window::OnLayout(UIEvent* e) {
  on_layout(e);
  if (!root_element()) {
    return;
  }
  // TODO(benvanik): subregion?
  root_element()->set_rect({0, 0, width(), height()});
}

void Window::OnPaint(UIEvent* e) {
  if (!renderer()) {
    return;
  }

  ++frame_count_;
  ++fps_frame_count_;
  uint64_t now_ns = xe::Clock::QueryHostSystemTime();
  if (now_ns > fps_update_time_ + 1000 * 10000) {
    fps_ = static_cast<uint32_t>(
        fps_frame_count_ /
        (static_cast<double>(now_ns - fps_update_time_) / 10000000.0));
    fps_update_time_ = now_ns;
    fps_frame_count_ = 0;
  }

  // Update TB (run animations, handle deferred input, etc).
  el::AnimationManager::Update();
  if (root_element()) {
    root_element()->InvokeProcessStates();
    root_element()->InvokeProcess();
  }

  GraphicsContextLock context_lock(context_.get());

  context_->BeginSwap();

  on_painting(e);

  renderer()->BeginPaint(width(), height());

  // Render entire control hierarchy.
  if (root_element()) {
    root_element()->InvokePaint(el::Element::PaintProps());
  }

  on_paint(e);

  if (root_element() && kShowPresentFps) {
    // Render debug overlay.
    root_element()->computed_font()->DrawString(
        5, 5, el::Color(255, 0, 0),
        el::format_string("Frame %lld", frame_count_));
    root_element()->computed_font()->DrawString(
        5, 20, el::Color(255, 0, 0),
        el::format_string("Present FPS: %d", fps_));
  }

  renderer()->EndPaint();

  on_painted(e);

  context_->EndSwap();

  // If animations are running, reinvalidate immediately.
  if (root_element()) {
    if (el::AnimationManager::has_running_animations()) {
      root_element()->Invalidate();
    }
    if (kContinuousRepaint) {
      // Force an immediate repaint, always.
      root_element()->Invalidate();
    }
  } else {
    if (kContinuousRepaint) {
      Invalidate();
    }
  }
}

void Window::OnVisible(UIEvent* e) { on_visible(e); }

void Window::OnHidden(UIEvent* e) { on_hidden(e); }

void Window::OnGotFocus(UIEvent* e) { on_got_focus(e); }

void Window::OnLostFocus(UIEvent* e) {
  modifier_shift_pressed_ = false;
  modifier_cntrl_pressed_ = false;
  modifier_alt_pressed_ = false;
  modifier_super_pressed_ = false;
  last_click_time_ = 0;
  on_lost_focus(e);
}

el::ModifierKeys Window::GetModifierKeys() {
  auto modifiers = el::ModifierKeys::kNone;
  if (modifier_shift_pressed_) {
    modifiers |= el::ModifierKeys::kShift;
  }
  if (modifier_cntrl_pressed_) {
    modifiers |= el::ModifierKeys::kCtrl;
  }
  if (modifier_alt_pressed_) {
    modifiers |= el::ModifierKeys::kAlt;
  }
  if (modifier_super_pressed_) {
    modifiers |= el::ModifierKeys::kSuper;
  }
  return modifiers;
}

void Window::OnKeyPress(KeyEvent* e, bool is_down, bool is_char) {
  if (!root_element()) {
    return;
  }
  auto special_key = el::SpecialKey::kUndefined;
  if (!is_char) {
    switch (e->key_code()) {
      case 38:
        special_key = el::SpecialKey::kUp;
        break;
      case 39:
        special_key = el::SpecialKey::kRight;
        break;
      case 40:
        special_key = el::SpecialKey::kDown;
        break;
      case 37:
        special_key = el::SpecialKey::kLeft;
        break;
      case 112:
        special_key = el::SpecialKey::kF1;
        break;
      case 113:
        special_key = el::SpecialKey::kF2;
        break;
      case 114:
        special_key = el::SpecialKey::kF3;
        break;
      case 115:
        special_key = el::SpecialKey::kF4;
        break;
      case 116:
        special_key = el::SpecialKey::kF5;
        break;
      case 117:
        special_key = el::SpecialKey::kF6;
        break;
      case 118:
        special_key = el::SpecialKey::kF7;
        break;
      case 119:
        special_key = el::SpecialKey::kF8;
        break;
      case 120:
        special_key = el::SpecialKey::kF9;
        break;
      case 121:
        special_key = el::SpecialKey::kF10;
        break;
      case 122:
        special_key = el::SpecialKey::kF11;
        break;
      case 123:
        special_key = el::SpecialKey::kF12;
        break;
      case 33:
        special_key = el::SpecialKey::kPageUp;
        break;
      case 34:
        special_key = el::SpecialKey::kPageDown;
        break;
      case 36:
        special_key = el::SpecialKey::kHome;
        break;
      case 35:
        special_key = el::SpecialKey::kEnd;
        break;
      case 45:
        special_key = el::SpecialKey::kInsert;
        break;
      case 9:
        special_key = el::SpecialKey::kTab;
        break;
      case 46:
        special_key = el::SpecialKey::kDelete;
        break;
      case 8:
        special_key = el::SpecialKey::kBackspace;
        break;
      case 13:
        special_key = el::SpecialKey::kEnter;
        break;
      case 27:
        special_key = el::SpecialKey::kEsc;
        break;
      case 93:
        if (!is_down && el::Element::focused_element) {
          el::Event ev(el::EventType::kContextMenu);
          ev.modifierkeys = GetModifierKeys();
          el::Element::focused_element->InvokeEvent(std::move(ev));
          e->set_handled(true);
          return;
        }
        break;
      case 16:
        modifier_shift_pressed_ = is_down;
        break;
      case 17:
        modifier_cntrl_pressed_ = is_down;
        break;
      // case xx:
      //  // alt ??
      //  modifier_alt_pressed_ = is_down;
      //  break;
      case 91:
        modifier_super_pressed_ = is_down;
        break;
    }
  }

  if (!CheckShortcutKey(e, special_key, is_down)) {
    int key_code = 0;
    if (is_char) {
      key_code = e->key_code();
      if (key_code < 32 || (key_code > 126 && key_code < 160)) {
        key_code = 0;
      }
    }
    e->set_handled(root_element()->InvokeKey(key_code, special_key,
                                             GetModifierKeys(), is_down));
  }
}

bool Window::CheckShortcutKey(KeyEvent* e, el::SpecialKey special_key,
                              bool is_down) {
  bool shortcut_key = modifier_cntrl_pressed_;
  if (!el::Element::focused_element || !is_down || !shortcut_key) {
    return false;
  }
  bool reverse_key = modifier_shift_pressed_;
  int upper_key = e->key_code();
  if (upper_key >= 'a' && upper_key <= 'z') {
    upper_key += 'A' - 'a';
  }
  el::TBID id;
  if (upper_key == 'X') {
    id = TBIDC("cut");
  } else if (upper_key == 'C' || special_key == el::SpecialKey::kInsert) {
    id = TBIDC("copy");
  } else if (upper_key == 'V' ||
             (special_key == el::SpecialKey::kInsert && reverse_key)) {
    id = TBIDC("paste");
  } else if (upper_key == 'A') {
    id = TBIDC("selectall");
  } else if (upper_key == 'Z' || upper_key == 'Y') {
    bool undo = upper_key == 'Z';
    if (reverse_key) {
      undo = !undo;
    }
    id = undo ? TBIDC("undo") : TBIDC("redo");
  } else if (upper_key == 'N') {
    id = TBIDC("new");
  } else if (upper_key == 'O') {
    id = TBIDC("open");
  } else if (upper_key == 'S') {
    id = TBIDC("save");
  } else if (upper_key == 'W') {
    id = TBIDC("close");
  } else if (special_key == el::SpecialKey::kPageUp) {
    id = TBIDC("prev_doc");
  } else if (special_key == el::SpecialKey::kPageDown) {
    id = TBIDC("next_doc");
  } else {
    return false;
  }

  el::Event ev(el::EventType::kShortcut);
  ev.modifierkeys = GetModifierKeys();
  ev.ref_id = id;
  if (!el::Element::focused_element->InvokeEvent(std::move(ev))) {
    return false;
  }
  e->set_handled(true);
  return true;
}

void Window::OnKeyDown(KeyEvent* e) {
  on_key_down(e);
  if (e->is_handled()) {
    return;
  }
  OnKeyPress(e, true, false);
}

void Window::OnKeyUp(KeyEvent* e) {
  on_key_up(e);
  if (e->is_handled()) {
    return;
  }
  OnKeyPress(e, false, false);
}

void Window::OnKeyChar(KeyEvent* e) {
  OnKeyPress(e, true, true);
  on_key_char(e);
  OnKeyPress(e, false, true);
}

void Window::OnMouseDown(MouseEvent* e) {
  on_mouse_down(e);
  if (e->is_handled()) {
    return;
  }
  if (!root_element()) {
    return;
  }
  // TODO(benvanik): more button types.
  if (e->button() == MouseEvent::Button::kLeft) {
    // Simulated click count support.
    // TODO(benvanik): move into Control?
    uint64_t now = xe::Clock::QueryHostUptimeMillis();
    if (now < last_click_time_ + kDoubleClickDelayMillis) {
      double distance_moved = std::sqrt(std::pow(e->x() - last_click_x_, 2.0) +
                                        std::pow(e->y() - last_click_y_, 2.0));
      if (distance_moved < kDoubleClickDistance) {
        ++last_click_counter_;
      } else {
        last_click_counter_ = 1;
      }
    } else {
      last_click_counter_ = 1;
    }
    last_click_x_ = e->x();
    last_click_y_ = e->y();
    last_click_time_ = now;

    e->set_handled(root_element()->InvokePointerDown(
        e->x(), e->y(), last_click_counter_, GetModifierKeys(), kTouch));
  }
}

void Window::OnMouseMove(MouseEvent* e) {
  on_mouse_move(e);
  if (e->is_handled()) {
    return;
  }
  if (!root_element()) {
    return;
  }
  root_element()->InvokePointerMove(e->x(), e->y(), GetModifierKeys(), kTouch);
  e->set_handled(true);
}

void Window::OnMouseUp(MouseEvent* e) {
  on_mouse_up(e);
  if (e->is_handled()) {
    return;
  }
  if (!root_element()) {
    return;
  }
  if (e->button() == MouseEvent::Button::kLeft) {
    e->set_handled(root_element()->InvokePointerUp(e->x(), e->y(),
                                                   GetModifierKeys(), kTouch));
  } else if (e->button() == MouseEvent::Button::kRight) {
    root_element()->InvokePointerMove(e->x(), e->y(), GetModifierKeys(),
                                      kTouch);
    if (el::Element::hovered_element) {
      int x = e->x();
      int y = e->y();
      el::Element::hovered_element->ConvertFromRoot(&x, &y);
      el::Event ev(el::EventType::kContextMenu, x, y, kTouch,
                   GetModifierKeys());
      el::Element::hovered_element->InvokeEvent(std::move(ev));
    }
    e->set_handled(true);
  }
}

void Window::OnMouseWheel(MouseEvent* e) {
  on_mouse_wheel(e);
  if (e->is_handled()) {
    return;
  }
  if (!root_element()) {
    return;
  }
  e->set_handled(root_element()->InvokeWheel(e->x(), e->y(), e->dx(),
                                             -e->dy() / kMouseWheelDetent,
                                             GetModifierKeys()));
}

}  // namespace ui
}  // namespace xe
