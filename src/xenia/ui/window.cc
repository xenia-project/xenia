/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window.h"

#include "third_party/imgui/imgui.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/presenter.h"

namespace xe {
namespace ui {

Window::Window(WindowedAppContext& app_context, const std::string_view title,
               uint32_t desired_logical_width, uint32_t desired_logical_height)
    : app_context_(app_context),
      title_(title),
      desired_logical_width_(desired_logical_width),
      desired_logical_height_(desired_logical_height) {}

Window::~Window() {
  // In case the implementation didn't need to call EnterDestructor. Though
  // that was likely a mistake, so placing an assertion.
  assert_true(phase_ == Phase::kDeleting);
  EnterDestructor();

  if (presenter_) {
    Presenter* old_presenter = presenter_;
    // Null the pointer to prevent an infinite loop between
    // SetWindowSurfaceFromUIThread and SetPresenter calling each other.
    presenter_ = nullptr;
    old_presenter->SetWindowSurfaceFromUIThread(nullptr, nullptr);
    presenter_surface_.reset();
  }

  // Right before destruction has finished, after which no interaction with
  // *this can be done, notify the destruction receivers that the window is
  // being destroyed and that *this is not accessible anymore.
  while (innermost_destruction_receiver_) {
    innermost_destruction_receiver_->window_ = nullptr;
    innermost_destruction_receiver_ =
        innermost_destruction_receiver_->outer_receiver_;
  }
}

void Window::AddListener(WindowListener* listener) {
  assert_not_null(listener);
  // Check if already added.
  if (std::find(listeners_.cbegin(), listeners_.cend(), listener) !=
      listeners_.cend()) {
    return;
  }
  listeners_.push_back(listener);
}

void Window::RemoveListener(WindowListener* listener) {
  assert_not_null(listener);
  auto it = std::find(listeners_.cbegin(), listeners_.cend(), listener);
  if (it == listeners_.cend()) {
    return;
  }
  // Actualize the next listener indices after the erasure from the vector.
  ListenerIterationContext* iteration_context =
      innermost_listener_iteration_context_;
  if (iteration_context) {
    size_t existing_index = size_t(std::distance(listeners_.cbegin(), it));
    while (iteration_context) {
      if (iteration_context->next_index > existing_index) {
        --iteration_context->next_index;
      }
      iteration_context = iteration_context->outer_context;
    }
  }
  listeners_.erase(it);
}

void Window::AddInputListener(WindowInputListener* listener, size_t z_order) {
  assert_not_null(listener);
  // Check if already added.
  for (auto it_existing = input_listeners_.rbegin();
       it_existing != input_listeners_.rend(); ++it_existing) {
    if (it_existing->second != listener) {
      continue;
    }
    if (it_existing->first == z_order) {
      return;
    }
    // If removing the listener that is the next in a current listener loop,
    // skip it (in a multimap, only one element iterator is invalidated).
    InputListenerIterationContext* iteration_context =
        innermost_input_listener_iteration_context_;
    while (iteration_context) {
      if (iteration_context->next_iterator == it_existing) {
        ++iteration_context->next_iterator;
      }
      iteration_context = iteration_context->outer_context;
    }
    input_listeners_.erase(std::prev(it_existing.base()));
    // FIXME(Triang3l): Changing the Z order of an existing listener while
    // already executing the listeners may cause listeners to be called twice if
    // the Z order is lowered from one that has already been processed to one
    // below the current one. Because nested listener calls are supported, a
    // single last call index can't be associated with a listener to skip it if
    // calling twice for the same event (a "not equal to" comparison of the call
    // indices will result in the skipping being cancelled in the outer loop if
    // an inner one is done, a "greater than" comparison will cause the inner
    // loop to effectively terminate all outer ones).
  }
  auto it_new = std::prev(
      std::make_reverse_iterator(input_listeners_.emplace(z_order, listener)));
  // If adding to layers in between the currently being processed ones (from
  // highest to lowest) and the previously next, make sure the new listener is
  // executed too. Execution within one layer, however, happens in the reverse
  // order of addition, so if adding to the Z layer currently being processed,
  // the new listener must not be executed within the loop. But, if adding to
  // the next Z layer after the current one, it must be executed immediately.
  {
    InputListenerIterationContext* iteration_context =
        innermost_input_listener_iteration_context_;
    while (iteration_context) {
      if (z_order < iteration_context->current_z_order &&
          (iteration_context->next_iterator == input_listeners_.crend() ||
           z_order >= iteration_context->next_iterator->first)) {
        iteration_context->next_iterator = it_new;
      }
      iteration_context = iteration_context->outer_context;
    }
  }
}

void Window::RemoveInputListener(WindowInputListener* listener) {
  assert_not_null(listener);
  for (auto it_existing = input_listeners_.rbegin();
       it_existing != input_listeners_.rend(); ++it_existing) {
    if (it_existing->second != listener) {
      continue;
    }
    // If removing the listener that is the next in a current listener loop,
    // skip it (in a multimap, only one element iterator is invalidated).
    InputListenerIterationContext* iteration_context =
        innermost_input_listener_iteration_context_;
    while (iteration_context) {
      if (iteration_context->next_iterator == it_existing) {
        ++iteration_context->next_iterator;
      }
      iteration_context = iteration_context->outer_context;
    }
    input_listeners_.erase(std::prev(it_existing.base()));
    return;
  }
}

bool Window::Open() {
  if (phase_ != Phase::kClosedOpenable) {
    return true;
  }

  // For consistency of the behavior of OpenImpl and the initial On*Update
  // that should be called as a result of it, reset the actual state to its
  // defaults for a closed window.
  // Note that this is performed in Open, not after closing, because there's
  // only one entry point for opening a window, while closing may be done
  // different ways - by actually closing, by destroying, or by failing to call
  // OpenImpl - instead of performing this reset in every possible close case,
  // just returning these defaults from the actual state getters if
  // HasActualState is false.
  actual_physical_width_ = 0;
  actual_physical_height_ = 0;
  has_focus_ = false;
  phase_ = Phase::kOpening;
  bool platform_open_result = OpenImpl();
  if (!platform_open_result) {
    phase_ = Phase::kClosedOpenable;
    return false;
  }
  if (phase_ != Phase::kOpening) {
    // The window was closed mid-opening.
    return true;
  }
  phase_ = Phase::kOpen;

  // Call the listeners (OnOpened with all the new state so the listeners are
  // aware that they can start interacting with the open Window, and after that,
  // in case certain listeners don't handle OnOpened, but rather, only need the
  // more granular callbacks, make sure those callbacks receive the new state
  // too) for the actual state of the new window (that may be different than the
  // desired, depending on how the platform has interpreted the desired state).
  {
    MonitorUpdateEvent e(this, true, true);
    OnMonitorUpdate(e);
  }
  {
    UISetupEvent e(this, true);
    WindowDestructionReceiver destruction_receiver(this);
    SendEventToListeners([&e](auto listener) { listener->OnOpened(e); },
                         destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrListenersUncallable()) {
      return true;
    }
    SendEventToListeners([&e](auto listener) { listener->OnDpiChanged(e); },
                         destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrListenersUncallable()) {
      return true;
    }
    SendEventToListeners([&e](auto listener) { listener->OnResize(e); },
                         destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrListenersUncallable()) {
      return true;
    }
    if (HasFocus()) {
      SendEventToListeners([&e](auto listener) { listener->OnGotFocus(e); },
                           destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrListenersUncallable()) {
        return true;
      }
    }
  }

  // May now try to create a valid surface (though the window may be in a
  // minimized state without the possibility of creating a surface, but that
  // will be resolved by the implementation), after OnDpiChanged and OnResize so
  // nothing related to painting will be making wrong assumptions about the
  // size.
  OnSurfaceChanged(true);

  return true;
}

void Window::SetFullscreen(bool new_fullscreen) {
  if (fullscreen_ == new_fullscreen) {
    return;
  }
  fullscreen_ = new_fullscreen;
  if (!CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ApplyNewFullscreen();
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::SetTitle(const std::string_view new_title) {
  if (title_ == new_title) {
    return;
  }
  title_ = new_title;
  if (!CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ApplyNewTitle();
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::SetIcon(const void* buffer, size_t size) {
  bool reset = !buffer || !size;
  WindowDestructionReceiver destruction_receiver(this);
  LoadAndApplyIcon(reset ? nullptr : buffer, reset ? 0 : size, CanApplyState());
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::SetMainMenu(std::unique_ptr<MenuItem> new_main_menu) {
  // The primary reason for this comparison (of two unique pointers) is
  // nullptr == nullptr.
  if (main_menu_ == new_main_menu) {
    return;
  }
  // Keep the old menu object existing while it's still being detached from
  // the platform window.
  std::unique_ptr<MenuItem> old_main_menu = std::move(main_menu_);
  main_menu_ = std::move(new_main_menu);
  if (!CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ApplyNewMainMenu(old_main_menu.get());
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::CompleteMainMenuItemsUpdate() {
  if (!main_menu_ || !CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  CompleteMainMenuItemsUpdateImpl();
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::SetMainMenuEnabled(bool enabled) {
  if (!main_menu_) {
    return;
  }
  // Not comparing to the actual enabled state, it's a part of the MenuItem, not
  // the Window.
  // In case enabling (or even disabling) causes menu-related events (like
  // pressing) that may execute callbacks potentially destroying the Window via
  // the outer architecture.
  WindowDestructionReceiver destruction_receiver(this);
  main_menu_->SetEnabled(enabled);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
  // Modifying the state of the items, notify the implementation so it makes the
  // displaying of the main menu consistent.
  CompleteMainMenuItemsUpdate();
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::CaptureMouse() {
  ++mouse_capture_request_count_;
  if (!CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  // Call even if capturing while the mouse is already assumed to be captured,
  // in case something has released it in the OS.
  ApplyNewMouseCapture();
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::ReleaseMouse() {
  assert_not_zero(mouse_capture_request_count_);
  if (!mouse_capture_request_count_) {
    return;
  }
  if (--mouse_capture_request_count_) {
    return;
  }
  if (!CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ApplyNewMouseRelease();
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::SetCursorVisibility(CursorVisibility new_cursor_visibility) {
  if (cursor_visibility_ == new_cursor_visibility) {
    return;
  }
  CursorVisibility old_cursor_visibility = cursor_visibility_;
  cursor_visibility_ = new_cursor_visibility;
  if (!CanApplyState()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ApplyNewCursorVisibility(old_cursor_visibility);
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::Focus() {
  if (!CanApplyState() || has_focus_) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  FocusImpl();
  if (destruction_receiver.IsWindowDestroyedOrStateInapplicable()) {
    return;
  }
}

void Window::SetPresenter(Presenter* presenter) {
  if (presenter_ == presenter) {
    return;
  }
  if (presenter_) {
    presenter_->SetWindowSurfaceFromUIThread(nullptr, nullptr);
    presenter_surface_.reset();
  }
  presenter_ = presenter;
  if (presenter_) {
    presenter_surface_ = CreateSurface(presenter_->GetSupportedSurfaceTypes());
    presenter_->SetWindowSurfaceFromUIThread(this, presenter_surface_.get());
  }
}

void Window::OnSurfaceChanged(bool new_surface_potentially_exists) {
  if (!presenter_) {
    return;
  }

  // Detach the presenter from the old surface before attaching to the new one.
  if (presenter_surface_) {
    presenter_->SetWindowSurfaceFromUIThread(this, nullptr);
    presenter_surface_.reset();
  }

  if (!new_surface_potentially_exists) {
    return;
  }

  presenter_surface_ = CreateSurface(presenter_->GetSupportedSurfaceTypes());
  if (presenter_surface_) {
    presenter_->SetWindowSurfaceFromUIThread(this, presenter_surface_.get());
  }
}

void Window::OnBeforeClose(WindowDestructionReceiver& destruction_receiver) {
  // Because events are not sent from closed windows, and to make sure the
  // window isn't closed while its surface is still attached to the presenter,
  // this must be called before doing what constitutes closing in the platform
  // implementation, not after.

  bool was_open = phase_ == Phase::kOpen;
  bool was_open_or_opening = was_open || phase_ == Phase::kOpening;
  assert_true(was_open_or_opening);
  if (!was_open_or_opening) {
    return;
  }

  if (was_open) {
    phase_ = Phase::kOpenBeforeClosing;

    // If the window was focused, notify the listeners that focus is being lost
    // because the window is being closed (the implementation usually wouldn't
    // be sending any events to the listeners after OnClosing even if the OS
    // actually sends the focus loss event as part of the closing process).
    OnFocusUpdate(false, destruction_receiver);
    if (destruction_receiver.IsWindowDestroyed()) {
      return;
    }

    {
      UIEvent e(this);
      SendEventToListeners([&e](auto listener) { listener->OnClosing(e); },
                           destruction_receiver);
      if (destruction_receiver.IsWindowDestroyed()) {
        return;
      }
    }

    // Disconnect from the surface without connecting to the new one (after the
    // listeners so they don't try to reconnect afterwards).
    OnSurfaceChanged(false);
  }

  phase_ = Phase::kClosing;
}

void Window::OnAfterClose() {
  assert_true(phase_ == Phase::kClosing);
  if (phase_ != Phase::kClosing) {
    return;
  }
  phase_ = (innermost_listener_iteration_context_ ||
            innermost_input_listener_iteration_context_)
               ? Phase::kClosedLeavingListeners
               : Phase::kClosedOpenable;
}

void Window::OnDpiChanged(UISetupEvent& e,
                          WindowDestructionReceiver& destruction_receiver) {
  SendEventToListeners([&e](auto listener) { listener->OnDpiChanged(e); },
                       destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnMonitorUpdate(MonitorUpdateEvent& e) {
  if (presenter_surface_) {
    presenter_->OnSurfaceMonitorUpdateFromUIThread(
        e.old_monitor_potentially_disconnected());
  }
}

bool Window::OnActualSizeUpdate(
    uint32_t new_physical_width, uint32_t new_physical_height,
    WindowDestructionReceiver& destruction_receiver) {
  if (actual_physical_width_ == new_physical_width &&
      actual_physical_height_ == new_physical_height) {
    return false;
  }
  actual_physical_width_ = new_physical_width;
  actual_physical_height_ = new_physical_height;
  // The listeners may reference the presenter, update the presenter first.
  if (presenter_surface_) {
    presenter_->OnSurfaceResizeFromUIThread();
  }
  UISetupEvent e(this);
  SendEventToListeners([&e](auto listener) { listener->OnResize(e); },
                       destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return true;
  }
  return true;
}

void Window::OnFocusUpdate(bool new_has_focus,
                           WindowDestructionReceiver& destruction_receiver) {
  if (has_focus_ == new_has_focus) {
    return;
  }
  has_focus_ = new_has_focus;
  UISetupEvent e(this);
  if (has_focus_) {
    SendEventToListeners([&e](auto listener) { listener->OnGotFocus(e); },
                         destruction_receiver);
  } else {
    SendEventToListeners([&e](auto listener) { listener->OnLostFocus(e); },
                         destruction_receiver);
  }
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnPaint(bool force_paint) {
  if (is_painting_) {
    return;
  }
  is_painting_ = true;
  if (presenter_surface_) {
    presenter_->PaintFromUIThread(force_paint);
  }
  is_painting_ = false;
}

void Window::OnFileDrop(FileDropEvent& e,
                        WindowDestructionReceiver& destruction_receiver) {
  SendEventToListeners([&e](auto listener) { listener->OnFileDrop(e); },
                       destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnKeyDown(KeyEvent& e,
                       WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnKeyDown(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnKeyUp(KeyEvent& e,
                     WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnKeyUp(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnKeyChar(KeyEvent& e,
                       WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnKeyChar(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnMouseDown(MouseEvent& e,
                         WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnMouseDown(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnMouseMove(MouseEvent& e,
                         WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnMouseMove(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnMouseUp(MouseEvent& e,
                       WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnMouseUp(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnMouseWheel(MouseEvent& e,
                          WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnMouseWheel(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::OnTouchEvent(TouchEvent& e,
                          WindowDestructionReceiver& destruction_receiver) {
  PropagateEventThroughInputListeners(
      [&e](auto listener) {
        listener->OnTouchEvent(e);
        return e.is_handled();
      },
      destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
}

void Window::SendEventToListeners(
    std::function<void(WindowListener*)> fn,
    WindowDestructionReceiver& destruction_receiver) {
  if (!CanSendEventsToListeners()) {
    return;
  }
  ListenerIterationContext iteration_context(
      innermost_listener_iteration_context_);
  innermost_listener_iteration_context_ = &iteration_context;
  while (iteration_context.next_index < listeners_.size()) {
    // iteration_context.next_index may be changed during the execution of the
    // listener if the list of the listeners is modified by it - don't assume
    // that after the call iteration_context.next_index will be the same as
    // iteration_context.next_index + 1 before it.
    fn(listeners_[iteration_context.next_index++]);
    if (destruction_receiver.IsWindowDestroyed()) {
      // The window was destroyed by the listener, can't access anything in
      // *this anymore, including innermost_listener_iteration_context_ which
      // has to be left in an indeterminate state.
      return;
    }
    if (!CanSendEventsToListeners()) {
      // The listener has put the window in a state in which the window can't
      // send events anymore.
      break;
    }
  }
  assert_true(innermost_listener_iteration_context_ == &iteration_context);
  innermost_listener_iteration_context_ =
      innermost_listener_iteration_context_->outer_context;
  if (phase_ == Phase::kClosedLeavingListeners &&
      !innermost_listener_iteration_context_ &&
      !innermost_input_listener_iteration_context_) {
    phase_ = Phase::kClosedOpenable;
  }
}

void Window::PropagateEventThroughInputListeners(
    std::function<bool(WindowInputListener*)> fn,
    WindowDestructionReceiver& destruction_receiver) {
  if (!CanSendEventsToListeners()) {
    return;
  }
  InputListenerIterationContext iteration_context(
      innermost_input_listener_iteration_context_, input_listeners_.crbegin());
  innermost_input_listener_iteration_context_ = &iteration_context;
  while (iteration_context.next_iterator != input_listeners_.crend()) {
    // The current iterator may be invalidated, and
    // iteration_context.next_iterator may be changed, during the execution of
    // the listener if the list of the listeners is modified by it - don't
    // assume that after the call iteration_context.next_iterator will be the
    // same as std::next(iteration_context.next_iterator) before it.
    iteration_context.current_z_order = iteration_context.next_iterator->first;
    bool event_handled = fn((iteration_context.next_iterator++)->second);
    if (destruction_receiver.IsWindowDestroyed()) {
      // The window was destroyed by the listener, can't access anything in
      // *this anymore, including innermost_listener_iteration_context_ which
      // has to be left in an indeterminate state.
      return;
    }
    if (event_handled) {
      break;
    }
    if (!CanSendEventsToListeners()) {
      // The listener has put the window in a state in which the window can't
      // send events anymore.
      break;
    }
  }
  assert_true(innermost_input_listener_iteration_context_ ==
              &iteration_context);
  innermost_input_listener_iteration_context_ =
      innermost_input_listener_iteration_context_->outer_context;
  if (phase_ == Phase::kClosedLeavingListeners &&
      !innermost_listener_iteration_context_ &&
      !innermost_input_listener_iteration_context_) {
    phase_ = Phase::kClosedOpenable;
  }
}

}  // namespace ui
}  // namespace xe
