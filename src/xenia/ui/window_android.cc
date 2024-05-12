/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window_android.h"

#include <android/input.h>
#include <jni.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/surface_android.h"
#include "xenia/ui/windowed_app_context_android.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<AndroidWindow>(
      app_context, title, desired_logical_width, desired_logical_height);
}

AndroidWindow::~AndroidWindow() {
  EnterDestructor();
  auto& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  if (android_app_context.GetActivityWindow() == this) {
    android_app_context.SetActivityWindow(nullptr);
  }
}

void AndroidWindow::OnActivitySurfaceLayoutChange() {
  auto& android_app_context =
      static_cast<const AndroidWindowedAppContext&>(app_context());
  assert_true(android_app_context.GetActivityWindow() == this);
  uint32_t physical_width =
      uint32_t(android_app_context.window_surface_layout_right() -
               android_app_context.window_surface_layout_left());
  uint32_t physical_height =
      uint32_t(android_app_context.window_surface_layout_bottom() -
               android_app_context.window_surface_layout_top());
  OnDesiredLogicalSizeUpdate(SizeToLogical(physical_width),
                             SizeToLogical(physical_height));
  WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(physical_width, physical_height, destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

bool AndroidWindow::OnActivitySurfaceMotionEvent(jobject event) {
  auto& android_app_context =
      static_cast<const AndroidWindowedAppContext&>(app_context());
  JNIEnv* jni_env = android_app_context.ui_thread_jni_env();
  const AndroidWindowedAppContext::JniIDs& jni_ids =
      android_app_context.jni_ids();

  int32_t source =
      jni_env->CallIntMethod(event, jni_ids.motion_event_get_source);

  switch (source) {
    case AINPUT_SOURCE_TOUCHSCREEN: {
      // Returning true for all touch events regardless of whether they have
      // been handled to keep receiving touch events for the pointers in the
      // event (if returning false for a down event, no more events will be sent
      // for the pointers in it), and also because there are multiple pointers
      // in a single event, and different handler invocations may result in
      // different is_handled.
      WindowDestructionReceiver destruction_receiver(this);
      int32_t action_and_pointer_index =
          jni_env->CallIntMethod(event, jni_ids.motion_event_get_action);
      int32_t action = action_and_pointer_index & AMOTION_EVENT_ACTION_MASK;
      // For pointer ACTION_POINTER_DOWN and ACTION_POINTER_UP.
      int32_t pointer_index = (action_and_pointer_index &
                               AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                              AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
      if (action == AMOTION_EVENT_ACTION_POINTER_DOWN ||
          action == AMOTION_EVENT_ACTION_POINTER_UP) {
        int32_t touch_pointer_index =
            (action_and_pointer_index &
             AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        TouchEvent e(
            this,
            jni_env->CallIntMethod(event, jni_ids.motion_event_get_pointer_id,
                                   touch_pointer_index),
            (action == AMOTION_EVENT_ACTION_POINTER_DOWN)
                ? TouchEvent::Action::kDown
                : TouchEvent::Action::kUp,
            jni_env->CallFloatMethod(event, jni_ids.motion_event_get_x,
                                     pointer_index),
            jni_env->CallFloatMethod(event, jni_ids.motion_event_get_y,
                                     pointer_index));
        OnTouchEvent(e, destruction_receiver);
        if (destruction_receiver.IsWindowDestroyed()) {
          return true;
        }
      } else {
        TouchEvent::Action touch_event_action;
        switch (action) {
          case AMOTION_EVENT_ACTION_DOWN:
            touch_event_action = TouchEvent::Action::kDown;
            break;
          case AMOTION_EVENT_ACTION_UP:
            touch_event_action = TouchEvent::Action::kUp;
            break;
          case AMOTION_EVENT_ACTION_MOVE:
            touch_event_action = TouchEvent::Action::kMove;
            break;
          case AMOTION_EVENT_ACTION_CANCEL:
            touch_event_action = TouchEvent::Action::kCancel;
            break;
          default:
            return true;
        }
        int32_t touch_pointer_count = jni_env->CallIntMethod(
            event, jni_ids.motion_event_get_pointer_count);
        for (int32_t i = 0; i < touch_pointer_count; ++i) {
          TouchEvent e(
              this,
              jni_env->CallIntMethod(event, jni_ids.motion_event_get_pointer_id,
                                     i),
              touch_event_action,
              jni_env->CallFloatMethod(event, jni_ids.motion_event_get_x, i),
              jni_env->CallFloatMethod(event, jni_ids.motion_event_get_y, i));
          OnTouchEvent(e, destruction_receiver);
          if (destruction_receiver.IsWindowDestroyed()) {
            return true;
          }
        }
      }
      return true;
    } break;

    case AINPUT_SOURCE_MOUSE: {
      WindowDestructionReceiver destruction_receiver(this);
      // X and Y can be outside the View (have negative coordinates, or beyond
      // the size of the element), and not only for ACTION_HOVER_EXIT (it's
      // predeced by ACTION_HOVER_MOVE, at least on Android API level 30, also
      // with out-of-bounds coordinates), when moving the mouse outside the
      // View, or when starting moving the mouse when the pointer was previously
      // outside the View in some cases.
      int32_t mouse_x =
          int32_t(xe::clamp_float(jni_env->CallFloatMethod(
                                      event, jni_ids.motion_event_get_x, 0),
                                  0.0f, float(GetActualPhysicalWidth())) +
                  0.5f);
      int32_t mouse_y =
          int32_t(xe::clamp_float(jni_env->CallFloatMethod(
                                      event, jni_ids.motion_event_get_y, 0),
                                  0.0f, float(GetActualPhysicalHeight())) +
                  0.5f);
      static const MouseEvent::Button kMouseEventButtons[] = {
          MouseEvent::Button::kLeft,   MouseEvent::Button::kRight,
          MouseEvent::Button::kMiddle, MouseEvent::Button::kX1,
          MouseEvent::Button::kX2,
      };
      static constexpr uint32_t kUsedMouseButtonMask =
          (UINT32_C(1) << xe::countof(kMouseEventButtons)) - 1;
      uint32_t new_mouse_button_state = uint32_t(
          jni_env->CallIntMethod(event, jni_ids.motion_event_get_button_state));
      // OnMouseUp.
      uint32_t mouse_buttons_remaining =
          mouse_button_state_ & ~new_mouse_button_state & kUsedMouseButtonMask;
      uint32_t mouse_button_index;
      while (
          xe::bit_scan_forward(mouse_buttons_remaining, &mouse_button_index)) {
        mouse_buttons_remaining &= ~(UINT32_C(1) << mouse_button_index);
        MouseEvent e(this, kMouseEventButtons[mouse_button_index], mouse_x,
                     mouse_y);
        OnMouseUp(e, destruction_receiver);
        if (destruction_receiver.IsWindowDestroyed()) {
          return true;
        }
      }
      // Generic OnMouseMove regardless of the action since any event can
      // provide new coordinates.
      {
        MouseEvent e(this, MouseEvent::Button::kNone, mouse_x, mouse_y);
        OnMouseMove(e, destruction_receiver);
        if (destruction_receiver.IsWindowDestroyed()) {
          return true;
        }
      }
      // OnMouseWheel.
      // The axis value may be outside -1...1 if multiple scrolls have occurred
      // quickly.
      int32_t scroll_x = int32_t(
          jni_env->CallFloatMethod(event, jni_ids.motion_event_get_axis_value,
                                   AMOTION_EVENT_AXIS_HSCROLL, 0) *
          float(MouseEvent::kScrollPerDetent));
      int32_t scroll_y = int32_t(
          jni_env->CallFloatMethod(event, jni_ids.motion_event_get_axis_value,
                                   AMOTION_EVENT_AXIS_VSCROLL, 0) *
          float(MouseEvent::kScrollPerDetent));
      if (scroll_x || scroll_y) {
        MouseEvent e(this, MouseEvent::Button::kNone, mouse_x, mouse_y,
                     scroll_x, scroll_y);
        OnMouseWheel(e, destruction_receiver);
        if (destruction_receiver.IsWindowDestroyed()) {
          return true;
        }
      }
      // OnMouseDown.
      mouse_buttons_remaining =
          new_mouse_button_state & ~mouse_button_state_ & kUsedMouseButtonMask;
      while (
          xe::bit_scan_forward(mouse_buttons_remaining, &mouse_button_index)) {
        mouse_buttons_remaining &= ~(UINT32_C(1) << mouse_button_index);
        MouseEvent e(this, kMouseEventButtons[mouse_button_index], mouse_x,
                     mouse_y);
        OnMouseDown(e, destruction_receiver);
        if (destruction_receiver.IsWindowDestroyed()) {
          return true;
        }
      }
      // Update the button state for state differences.
      mouse_button_state_ = new_mouse_button_state;
      return true;
    } break;
  }

  return false;
}

uint32_t AndroidWindow::GetLatestDpiImpl() const {
  auto& android_app_context =
      static_cast<const AndroidWindowedAppContext&>(app_context());
  return android_app_context.GetPixelDensity();
}

bool AndroidWindow::OpenImpl() {
  // Reset the input.
  mouse_button_state_ = 0;

  // The window is a proxy between the main activity and Xenia, so there can be
  // only one open window for an activity.
  auto& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  AndroidWindow* previous_activity_window =
      android_app_context.GetActivityWindow();
  assert_null(previous_activity_window);
  if (previous_activity_window) {
    // Don't detach the old window as it's assuming it's still attached while
    // it's in an open phase.
    return false;
  }
  android_app_context.SetActivityWindow(this);

  // Report the initial layout.
  OnActivitySurfaceLayoutChange();

  return true;
}

void AndroidWindow::RequestCloseImpl() {
  // Finishing the Activity would cause the entire WindowedAppContext to quit,
  // which is not the same behavior as on other platforms - the
  // WindowedAppContext quit is done explicitly by the listeners during
  // OnClosing if needed (the main Window is potentially being changed to a
  // different one, for instance). Therefore, only detach the window from the
  // app context.

  WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
  OnAfterClose();

  auto& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  if (android_app_context.GetActivityWindow() == this) {
    android_app_context.SetActivityWindow(nullptr);
  }
}

std::unique_ptr<Surface> AndroidWindow::CreateSurfaceImpl(
    Surface::TypeFlags allowed_types) {
  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow) {
    auto& android_app_context =
        static_cast<const AndroidWindowedAppContext&>(app_context());
    assert_true(android_app_context.GetActivityWindow() == this);
    ANativeWindow* activity_window_surface =
        android_app_context.GetWindowSurface();
    if (activity_window_surface) {
      return std::make_unique<AndroidNativeWindowSurface>(
          activity_window_surface);
    }
  }
  return nullptr;
}

void AndroidWindow::RequestPaintImpl() {
  auto& android_app_context =
      static_cast<AndroidWindowedAppContext&>(app_context());
  assert_true(android_app_context.GetActivityWindow() == this);
  android_app_context.PostInvalidateWindowSurface();
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::string& text,
                                               const std::string& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<AndroidMenuItem>(type, text, hotkey, callback);
}

}  // namespace ui
}  // namespace xe
