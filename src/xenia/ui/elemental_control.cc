/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/elemental_control.h"

#include "el/animation_manager.h"
#include "el/util/debug.h"
#include "el/elemental_forms.h"
#include "el/io/file_manager.h"
#include "el/io/posix_file_system.h"
#include "el/io/win32_res_file_system.h"
#include "el/message_handler.h"
#include "el/text/font_manager.h"
#include "el/util/metrics.h"
#include "el/util/string_table.h"
#include "el/util/timer.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {

constexpr bool kContinuousRepaint = false;

// Enables long press behaviors (context menu, etc).
constexpr bool kTouch = false;

constexpr uint64_t kDoubleClickDelayMillis = 600;
constexpr double kDoubleClickDistance = 5;

constexpr int32_t kMouseWheelDetent = 120;

Loop* elemental_loop_ = nullptr;

class RootElement : public el::Element {
 public:
  RootElement(ElementalControl* owner) : owner_(owner) {}
  void OnInvalid() override { owner_->Invalidate(); }

 private:
  ElementalControl* owner_ = nullptr;
};

bool ElementalControl::InitializeElemental(Loop* loop,
                                           el::graphics::Renderer* renderer) {
  static bool has_initialized = false;
  if (has_initialized) {
    return true;
  }
  has_initialized = true;

  // Need to pass off the Loop we want Elemental to use.
  // TODO(benvanik): give the callback to elemental instead.
  elemental_loop_ = loop;

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
  return true;
}

ElementalControl::ElementalControl(Loop* loop, uint32_t flags)
    : super(flags), loop_(loop) {}

ElementalControl::~ElementalControl() = default;

bool ElementalControl::Create() {
  if (!super::Create()) {
    return false;
  }

  // Create subclass renderer (GL, etc).
  renderer_ = CreateRenderer();

  // Initialize elemental.
  // TODO(benvanik): once? Do we care about multiple controls?
  if (!InitializeElemental(loop_, renderer_.get())) {
    XELOGE("Unable to initialize elemental-forms");
    return false;
  }

  // TODO(benvanik): setup elements.
  root_element_ = std::make_unique<RootElement>(this);
  root_element_->set_background_skin(TBIDC("background"));
  root_element_->set_rect({0, 0, 1000, 1000});

  return true;
}

bool ElementalControl::LoadLanguage(std::string filename) {
  return el::util::StringTable::get()->Load(filename.c_str());
}

bool ElementalControl::LoadSkin(std::string filename) {
  return el::Skin::get()->Load(filename.c_str());
}

void ElementalControl::Destroy() {
  el::Shutdown();
  super::Destroy();
}

void ElementalControl::OnLayout(UIEvent& e) {
  super::OnLayout(e);
  if (!root_element()) {
    return;
  }
  // TODO(benvanik): subregion?
  root_element()->set_rect({0, 0, width(), height()});
}

void ElementalControl::OnPaint(UIEvent& e) {
  super::OnPaint(e);
  if (!root_element()) {
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
  el::AnimationManager::Update();
  root_element()->InvokeProcessStates();
  root_element()->InvokeProcess();

  renderer()->BeginPaint(width(), height());

  // Render entire control hierarchy.
  root_element()->InvokePaint(el::Element::PaintProps());

  // Render debug overlay.
  root_element()->computed_font()->DrawString(
      5, 5, el::Color(255, 0, 0),
      el::format_string("Frame %lld", frame_count_));
  if (kContinuousRepaint) {
    root_element()->computed_font()->DrawString(
        5, 20, el::Color(255, 0, 0), el::format_string("FPS: %d", fps_));
  }

  renderer()->EndPaint();

  // If animations are running, reinvalidate immediately.
  if (el::AnimationManager::has_running_animations()) {
    root_element()->Invalidate();
  }
  if (kContinuousRepaint) {
    // Force an immediate repaint, always.
    root_element()->Invalidate();
  }
}

void ElementalControl::OnGotFocus(UIEvent& e) { super::OnGotFocus(e); }

void ElementalControl::OnLostFocus(UIEvent& e) {
  super::OnLostFocus(e);
  modifier_shift_pressed_ = false;
  modifier_cntrl_pressed_ = false;
  modifier_alt_pressed_ = false;
  modifier_super_pressed_ = false;
  last_click_time_ = 0;
}

el::ModifierKeys ElementalControl::GetModifierKeys() {
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

void ElementalControl::OnKeyPress(KeyEvent& e, bool is_down, bool is_char) {
  if (!root_element()) {
    return;
  }
  auto special_key = el::SpecialKey::kUndefined;
  if (!is_char) {
    switch (e.key_code()) {
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
          el::Element::focused_element->InvokeEvent(ev);
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
  }

  if (!CheckShortcutKey(e, special_key, is_down)) {
    int key_code = 0;
    if (is_char) {
      key_code = e.key_code();
      if (key_code < 32 || (key_code > 126 && key_code < 160)) {
        key_code = 0;
      }
    }
    e.set_handled(root_element()->InvokeKey(key_code, special_key,
                                            GetModifierKeys(), is_down));
  }
}

bool ElementalControl::CheckShortcutKey(KeyEvent& e, el::SpecialKey special_key,
                                        bool is_down) {
  bool shortcut_key = modifier_cntrl_pressed_;
  if (!el::Element::focused_element || !is_down || !shortcut_key) {
    return false;
  }
  bool reverse_key = modifier_shift_pressed_;
  int upper_key = e.key_code();
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
  if (!el::Element::focused_element->InvokeEvent(ev)) {
    return false;
  }
  e.set_handled(true);
  return true;
}

void ElementalControl::OnKeyDown(KeyEvent& e) {
  super::OnKeyDown(e);
  OnKeyPress(e, true, false);
}

void ElementalControl::OnKeyUp(KeyEvent& e) {
  super::OnKeyUp(e);
  OnKeyPress(e, false, false);
}

void ElementalControl::OnKeyChar(KeyEvent& e) {
  super::OnKeyChar(e);
  OnKeyPress(e, true, true);
  OnKeyPress(e, false, true);
}

void ElementalControl::OnMouseDown(MouseEvent& e) {
  super::OnMouseDown(e);
  if (!root_element()) {
    return;
  }
  // TODO(benvanik): more button types.
  if (e.button() == MouseEvent::Button::kLeft) {
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

    e.set_handled(root_element()->InvokePointerDown(
        e.x(), e.y(), last_click_counter_, GetModifierKeys(), kTouch));
  }
}

void ElementalControl::OnMouseMove(MouseEvent& e) {
  super::OnMouseMove(e);
  if (!root_element()) {
    return;
  }
  root_element()->InvokePointerMove(e.x(), e.y(), GetModifierKeys(), kTouch);
  e.set_handled(true);
}

void ElementalControl::OnMouseUp(MouseEvent& e) {
  super::OnMouseUp(e);
  if (!root_element()) {
    return;
  }
  if (e.button() == MouseEvent::Button::kLeft) {
    e.set_handled(root_element()->InvokePointerUp(e.x(), e.y(),
                                                  GetModifierKeys(), kTouch));
  } else if (e.button() == MouseEvent::Button::kRight) {
    root_element()->InvokePointerMove(e.x(), e.y(), GetModifierKeys(), kTouch);
    if (el::Element::hovered_element) {
      int x = e.x();
      int y = e.y();
      el::Element::hovered_element->ConvertFromRoot(x, y);
      el::Event ev(el::EventType::kContextMenu, x, y, kTouch,
                   GetModifierKeys());
      el::Element::hovered_element->InvokeEvent(ev);
    }
    e.set_handled(true);
  }
}

void ElementalControl::OnMouseWheel(MouseEvent& e) {
  super::OnMouseWheel(e);
  if (!root_element()) {
    return;
  }
  e.set_handled(root_element()->InvokeWheel(
      e.x(), e.y(), e.dx(), -e.dy() / kMouseWheelDetent, GetModifierKeys()));
}

}  // namespace ui
}  // namespace xe

// This doesn't really belong here (it belongs in tb_system_[linux/windows].cpp.
// This is here since the proper implementations has not yet been done.
void el::util::RescheduleTimer(uint64_t fire_time) {
  if (fire_time == el::MessageHandler::kNotSoon) {
    return;
  }

  uint64_t now = el::util::GetTimeMS();
  uint64_t delay_millis = fire_time >= now ? fire_time - now : 0;
  xe::ui::elemental_loop_->PostDelayed([]() {
    uint64_t next_fire_time = el::MessageHandler::GetNextMessageFireTime();
    uint64_t now = el::util::GetTimeMS();
    if (now < next_fire_time) {
      // We timed out *before* we were supposed to (the OS is not playing nice).
      // Calling ProcessMessages now won't achieve a thing so force a reschedule
      // of the platform timer again with the same time.
      // ReschedulePlatformTimer(next_fire_time, true);
      return;
    }

    el::MessageHandler::ProcessMessages();

    // If we still have things to do (because we didn't process all messages,
    // or because there are new messages), we need to rescedule, so call
    // RescheduleTimer.
    el::util::RescheduleTimer(el::MessageHandler::GetNextMessageFireTime());
  }, delay_millis);
}
