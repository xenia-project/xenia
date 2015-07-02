/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/turbo_badger_control.h"

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/debug/ui/turbo_badger_renderer.h"

// TODO(benvanik): remove this.
#include "xenia/debug/ui/application.h"

#include "third_party/turbobadger/src/tb/tb_core.h"
#include "third_party/turbobadger/src/tb/tb_font_renderer.h"
#include "third_party/turbobadger/src/tb/tb_message_window.h"
#include "third_party/turbobadger/src/tb/tb_widget_animation.h"

namespace xe {
namespace debug {
namespace ui {

constexpr bool kContinuousRepaint = false;

// Enables long press behaviors (context menu, etc).
constexpr bool kTouch = false;

constexpr uint64_t kDoubleClickDelayMillis = 600;
constexpr double kDoubleClickDistance = 5;

constexpr int32_t kMouseWheelDetent = 120;

class RootWidget : public tb::Widget {
 public:
  RootWidget(TurboBadgerControl* owner) : owner_(owner) {}
  void OnInvalid() override { owner_->Invalidate(); }

 private:
  TurboBadgerControl* owner_ = nullptr;
};

bool TurboBadgerControl::InitializeTurboBadger(tb::Renderer* renderer) {
  static bool has_initialized = false;
  if (has_initialized) {
    return true;
  }
  has_initialized = true;

  if (!tb::tb_core_init(
          renderer,
          "third_party/turbobadger/resources/language/lng_en.tb.txt")) {
    XELOGE("Failed to initialize turbobadger core");
    return false;
  }

  // Load the default skin, and override skin that contains the graphics
  // specific to the demo.
  if (!tb::g_tb_skin->Load(
          "third_party/turbobadger/resources/default_skin/skin.tb.txt",
          "third_party/turbobadger/Demo/demo01/skin/skin.tb.txt")) {
    XELOGE("Failed to load turbobadger skin");
    return false;
  }

// Register font renderers.
#ifdef TB_FONT_RENDERER_TBBF
  void register_tbbf_font_renderer();
  register_tbbf_font_renderer();
#endif
#ifdef TB_FONT_RENDERER_STB
  void register_stb_font_renderer();
  register_stb_font_renderer();
#endif
#ifdef TB_FONT_RENDERER_FREETYPE
  void register_freetype_font_renderer();
  register_freetype_font_renderer();
#endif

// Add fonts we can use to the font manager.
#if defined(TB_FONT_RENDERER_STB) || defined(TB_FONT_RENDERER_FREETYPE)
  tb::g_font_manager->AddFontInfo("third_party/turbobadger/resources/vera.ttf",
                                  "Default");
#endif
#ifdef TB_FONT_RENDERER_TBBF
  tb::g_font_manager->AddFontInfo(
      "third_party/turbobadger/resources/default_font/"
      "segoe_white_with_shadow.tb.txt",
      "Default");
#endif

  // Set the default font description for widgets to one of the fonts we just
  // added.
  tb::FontDescription fd;
  fd.SetID(TBIDC("Default"));
  fd.SetSize(tb::g_tb_skin->GetDimensionConverter()->DpToPx(14));
  tb::g_font_manager->SetDefaultFontDescription(fd);

  // Create the font now.
  auto font = tb::g_font_manager->CreateFontFace(
      tb::g_font_manager->GetDefaultFontDescription());
  return true;
}

void TurboBadgerControl::ShutdownTurboBadger() { tb::tb_core_shutdown(); }

TurboBadgerControl::TurboBadgerControl(xe::ui::Loop* loop) : super(loop) {}

TurboBadgerControl::~TurboBadgerControl() = default;

bool TurboBadgerControl::Create() {
  if (!super::Create()) {
    return false;
  }

  // TODO(benvanik): setup renderer?
  renderer_ = TBRendererGL4::Create();

  if (!InitializeTurboBadger(renderer_.get())) {
    XELOGE("Unable to initialize turbobadger");
    return false;
  }

  tb::WidgetAnimationManager::Init();

  // TODO(benvanik): setup widgets.
  root_widget_ = std::make_unique<RootWidget>(this);
  root_widget_->SetSkinBg(TBIDC("background"));
  root_widget_->SetRect(tb::Rect(0, 0, 1000, 1000));

  // Block animations during init.
  tb::AnimationBlocker anim_blocker;

  // TODO(benvanik): dummy UI.
  auto message_window = new tb::MessageWindow(root_widget(), TBIDC(""));
  message_window->Show("Title", "Hello!");

  // tb::ShowDebugInfoSettingsWindow(root_widget());

  return true;
}

void TurboBadgerControl::Destroy() {
  tb::WidgetAnimationManager::Shutdown();
  super::Destroy();
}

void TurboBadgerControl::OnLayout(xe::ui::UIEvent& e) {
  super::OnLayout(e);
  if (!root_widget()) {
    return;
  }
  // TODO(benvanik): subregion?
  root_widget()->SetRect(tb::Rect(0, 0, width(), height()));
}

void TurboBadgerControl::OnPaint(xe::ui::UIEvent& e) {
  super::OnPaint(e);
  if (!root_widget()) {
    return;
  }

  ++frame_count_;
  ++fps_frame_count_;
  uint64_t now_ns = xe::Clock::QueryHostSystemTime();
  if (now_ns > fps_update_time_ + 1000 * 10000) {
    fps_ = uint32_t(fps_frame_count_ /
                    (double(now_ns - fps_update_time_) / 10000000.0));
    fps_update_time_ = now_ns;
    fps_frame_count_ = 0;
  }

  // Update TB (run animations, handle deferred input, etc).
  tb::AnimationManager::Update();
  root_widget()->InvokeProcessStates();
  root_widget()->InvokeProcess();

  renderer()->BeginPaint(width(), height());

  // Render entire control hierarchy.
  root_widget()->InvokePaint(tb::Widget::PaintProps());

  // Render debug overlay.
  root_widget()->GetFont()->DrawString(
      5, 5, tb::Color(255, 0, 0),
      tb::format_string("Frame %lld", frame_count_));
  if (kContinuousRepaint) {
    root_widget()->GetFont()->DrawString(5, 20, tb::Color(255, 0, 0),
                                         tb::format_string("FPS: %d", fps_));
  }

  renderer()->EndPaint();

  // If animations are running, reinvalidate immediately.
  if (tb::AnimationManager::HasAnimationsRunning()) {
    root_widget()->Invalidate();
  }
  if (kContinuousRepaint) {
    // Force an immediate repaint, always.
    root_widget()->Invalidate();
  }
}

void TurboBadgerControl::OnGotFocus(xe::ui::UIEvent& e) {
  super::OnGotFocus(e);
}

void TurboBadgerControl::OnLostFocus(xe::ui::UIEvent& e) {
  super::OnLostFocus(e);
  modifier_shift_pressed_ = false;
  modifier_cntrl_pressed_ = false;
  modifier_alt_pressed_ = false;
  modifier_super_pressed_ = false;
  last_click_time_ = 0;
}

tb::ModifierKeys TurboBadgerControl::GetModifierKeys() {
  auto modifiers = tb::ModifierKeys::kNone;
  if (modifier_shift_pressed_) {
    modifiers |= tb::ModifierKeys::kShift;
  }
  if (modifier_cntrl_pressed_) {
    modifiers |= tb::ModifierKeys::kCtrl;
  }
  if (modifier_alt_pressed_) {
    modifiers |= tb::ModifierKeys::kAlt;
  }
  if (modifier_super_pressed_) {
    modifiers |= tb::ModifierKeys::kSuper;
  }
  return modifiers;
}

void TurboBadgerControl::OnKeyPress(xe::ui::KeyEvent& e, bool is_down) {
  if (!root_widget()) {
    return;
  }
  auto special_key = tb::SpecialKey::kUndefined;
  switch (e.key_code()) {
    case 38:
      special_key = tb::SpecialKey::kUp;
      break;
    case 39:
      special_key = tb::SpecialKey::kRight;
      break;
    case 40:
      special_key = tb::SpecialKey::kDown;
      break;
    case 37:
      special_key = tb::SpecialKey::kLeft;
      break;
    case 112:
      special_key = tb::SpecialKey::kF1;
      break;
    case 113:
      special_key = tb::SpecialKey::kF2;
      break;
    case 114:
      special_key = tb::SpecialKey::kF3;
      break;
    case 115:
      special_key = tb::SpecialKey::kF4;
      break;
    case 116:
      special_key = tb::SpecialKey::kF5;
      break;
    case 117:
      special_key = tb::SpecialKey::kF6;
      break;
    case 118:
      special_key = tb::SpecialKey::kF7;
      break;
    case 119:
      special_key = tb::SpecialKey::kF8;
      break;
    case 120:
      special_key = tb::SpecialKey::kF9;
      break;
    case 121:
      special_key = tb::SpecialKey::kF10;
      break;
    case 122:
      special_key = tb::SpecialKey::kF11;
      break;
    case 123:
      special_key = tb::SpecialKey::kF12;
      break;
    case 33:
      special_key = tb::SpecialKey::kPageUp;
      break;
    case 34:
      special_key = tb::SpecialKey::kPageDown;
      break;
    case 36:
      special_key = tb::SpecialKey::kHome;
      break;
    case 35:
      special_key = tb::SpecialKey::kEnd;
      break;
    case 45:
      special_key = tb::SpecialKey::kInsert;
      break;
    case 9:
      special_key = tb::SpecialKey::kTab;
      break;
    case 46:
      special_key = tb::SpecialKey::kDelete;
      break;
    case 8:
      special_key = tb::SpecialKey::kBackspace;
      break;
    case 13:
      special_key = tb::SpecialKey::kEnter;
      break;
    case 27:
      special_key = tb::SpecialKey::kEsc;
      break;
    case 93:
      if (!is_down && tb::Widget::focused_widget) {
        tb::WidgetEvent ev(tb::EventType::kContextMenu);
        ev.modifierkeys = GetModifierKeys();
        tb::Widget::focused_widget->InvokeEvent(ev);
        e.set_handled(true);
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

  if (!CheckShortcutKey(e, special_key, is_down)) {
    e.set_handled(root_widget()->InvokeKey(
        special_key != tb::SpecialKey::kUndefined ? e.key_code() : 0,
        special_key, GetModifierKeys(), is_down));
  }
}

bool TurboBadgerControl::CheckShortcutKey(xe::ui::KeyEvent& e,
                                          tb::SpecialKey special_key,
                                          bool is_down) {
  bool shortcut_key = modifier_cntrl_pressed_;
  if (!tb::Widget::focused_widget || !is_down || !shortcut_key) {
    return false;
  }
  bool reverse_key = modifier_shift_pressed_;
  int upper_key = e.key_code();
  if (upper_key >= 'a' && upper_key <= 'z') {
    upper_key += 'A' - 'a';
  }
  tb::TBID id;
  if (upper_key == 'X') {
    id = TBIDC("cut");
  } else if (upper_key == 'C' || special_key == tb::SpecialKey::kInsert) {
    id = TBIDC("copy");
  } else if (upper_key == 'V' ||
             (special_key == tb::SpecialKey::kInsert && reverse_key)) {
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
  } else if (special_key == tb::SpecialKey::kPageUp) {
    id = TBIDC("prev_doc");
  } else if (special_key == tb::SpecialKey::kPageDown) {
    id = TBIDC("next_doc");
  } else {
    return false;
  }

  tb::WidgetEvent ev(tb::EventType::kShortcut);
  ev.modifierkeys = GetModifierKeys();
  ev.ref_id = id;
  if (!tb::Widget::focused_widget->InvokeEvent(ev)) {
    return false;
  }
  e.set_handled(true);
  return true;
}

void TurboBadgerControl::OnKeyDown(xe::ui::KeyEvent& e) {
  super::OnKeyDown(e);
  OnKeyPress(e, true);
}

void TurboBadgerControl::OnKeyUp(xe::ui::KeyEvent& e) {
  super::OnKeyUp(e);
  OnKeyPress(e, false);
}

void TurboBadgerControl::OnMouseDown(xe::ui::MouseEvent& e) {
  super::OnMouseDown(e);
  if (!root_widget()) {
    return;
  }
  // TODO(benvanik): more button types.
  if (e.button() == xe::ui::MouseEvent::Button::kLeft) {
    // Simulated click count support.
    // TODO(benvanik): move into Control?
    uint64_t now = xe::Clock::QueryHostUptimeMillis();
    if (now < last_click_time_ + kDoubleClickDelayMillis) {
      double distance_moved = std::sqrt(std::pow(e.x() - last_click_x_, 2.0) +
                                        std::pow(e.y() - last_click_y_, 2.0));
      if (distance_moved < kDoubleClickDistance) {
        ++last_click_counter_;
      } else {
        last_click_counter_ = 1;
      }
    } else {
      last_click_counter_ = 1;
    }
    last_click_x_ = e.x();
    last_click_y_ = e.y();
    last_click_time_ = now;

    e.set_handled(root_widget()->InvokePointerDown(
        e.x(), e.y(), last_click_counter_, GetModifierKeys(), kTouch));
  }
}

void TurboBadgerControl::OnMouseMove(xe::ui::MouseEvent& e) {
  super::OnMouseMove(e);
  if (!root_widget()) {
    return;
  }
  root_widget()->InvokePointerMove(e.x(), e.y(), GetModifierKeys(), kTouch);
  e.set_handled(true);
}

void TurboBadgerControl::OnMouseUp(xe::ui::MouseEvent& e) {
  super::OnMouseUp(e);
  if (!root_widget()) {
    return;
  }
  if (e.button() == xe::ui::MouseEvent::Button::kLeft) {
    e.set_handled(root_widget()->InvokePointerUp(e.x(), e.y(),
                                                 GetModifierKeys(), kTouch));
  } else if (e.button() == xe::ui::MouseEvent::Button::kRight) {
    root_widget()->InvokePointerMove(e.x(), e.y(), GetModifierKeys(), kTouch);
    if (tb::Widget::hovered_widget) {
      int x = e.x();
      int y = e.y();
      tb::Widget::hovered_widget->ConvertFromRoot(x, y);
      tb::WidgetEvent ev(tb::EventType::kContextMenu, x, y, kTouch,
                         GetModifierKeys());
      tb::Widget::hovered_widget->InvokeEvent(ev);
    }
    e.set_handled(true);
  }
}

void TurboBadgerControl::OnMouseWheel(xe::ui::MouseEvent& e) {
  super::OnMouseWheel(e);
  if (!root_widget()) {
    return;
  }
  e.set_handled(root_widget()->InvokeWheel(
      e.x(), e.y(), e.dx(), -e.dy() / kMouseWheelDetent, GetModifierKeys()));
}

}  // namespace ui
}  // namespace debug
}  // namespace xe
