/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_H_
#define XENIA_UI_WINDOW_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "xenia/base/platform.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/surface.h"
#include "xenia/ui/ui_event.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class Window {
 public:
  // Transitions between these phases is sequential and looped (and >= and <=
  // can be used for openness checks, for closedness checks ! of >= and <= is
  // needed due to looping), with the exception of kDeleting that may be entered
  // from any other state (with an assertion for that not being done during
  // kOpening though as that's extremely dangerous and would require a lot of
  // handling in OpenImpl on all platforms) as the Window object may be deleted
  // externally at any moment, and the Window will have no control anymore when
  // that happens. Another exception is that the window may be closed in the
  // platform during kOpening - in this case, it will skip kOpenBeforeClosing
  // and go directly to kClosing and beyond.
  enum class Phase {
    // The window hasn't been opened yet, or has been fully closed and can be
    // reopened.
    // No native window - external state updates change only the desired state,
    // don't go to the implementation immediately.
    // No actual state.
    // Listeners are not called.
    kClosedOpenable,
    // OpenImpl is being invoked, the implementation is performing initial
    // setup.
    // External state changes are functionally near-impossible as OpenImpl
    // mostly isn't able to communicate with external Xenia code that may have
    // any effect on the Window, to avoid interference during the native window
    // setup which may be pretty complex - and also, this is the phase in which
    // the native window is being created, so there's no guarantee that the
    // native window exists throughout this phase.
    // However, it's still not strictly enforceable that OpenImpl will not cause
    // any interaction with the Window - it may happen, for instance, if
    // OpenImpl causes the OS to execute the pending functions in the
    // WindowedAppContext that might have been, for example, left over from when
    // the window was still open last time. Therefore, for the reason of state
    // consistency within OpenImpl, all external state changes are simply
    // dropped in this phase.
    // However, the desired state may be updated by the implementation during
    // OpenImpl still, but via the Update functions (if the implementation or
    // the OS, for instance, clamps the size of the window during creation, or
    // refuses to enter fullscreen).
    // Actual state is first populated during this phase, and it's readable
    // (with its level of completeness at the specific point in time) for the
    // internal purposes of the implementation.
    // Listeners are not called so the implementation can perform all the setup
    // without outer interference (the common code calls some to let the
    // listeners be aware of the initial state when the window enters kOpen
    // anyway).
    // Note: Closing may occur in the platform during kOpening - skip
    // kOpenBeforeClosing in this case while closing.
    kOpening,
    // Fully interactive.
    // The native window exists, external state changes are applied immediately
    // (or at least immediately requested to be applied shortly after, but the
    // implementation must make sure that, for instance, if SetFullscreen is
    // successfully called, but the window will actually enter fullscreen only
    // at the next platform event loop tick, IsFullscreen will start returning
    // true immediately).
    // The desired state can be updated as feedback from the implementation.
    // Actual state can be retrieved.
    // Listeners are called.
    // The only state in which a non-null Surface can be created (it's destroyed
    // after entering kClosing).
    kOpen,
    // OnBeforeClose is being invoked.
    // The native window still exists, this is mostly like kOpen, but with no
    // way of creating a Surface (it's becoming destroyed in this state so the
    // native window can be destroyed safely in the next phase) or recursively
    // requesting closing - for consistency during closing within the platform,
    // state changes also behave like in kOpen.
    // Listeners are still called as normal, primarily because this is where the
    // OnClosing listener function is invoked, but reopening the window from a
    // listener is not possible for state consistency during closing.
    kOpenBeforeClosing,
    // OnBeforeClose has completed, but OnAfterClose hasn't been called yet for
    // the implementation to confirm that it has finished destroying the native
    // window being closed. This state exists to prevent the situation in which
    // the Window is somehow reopened in the middle of the implementation's
    // internal work in closing.
    // The implementation must detach from the native window and destroy it in
    // this phase.
    // The native window is being destroyed - external state updates change only
    // the desired state, don't go to the implementation immediately.
    // Actual state is still queryable for internal purposes of the
    // implementation.
    // Listeners are not called.
    kClosing,
    // OnBeforeClose has completed, but the close has occurred from a listener,
    // and the call stack of listeners still hasn't been exited.
    // This state exists to prevent the situation in which the Window is being
    // closed and then reopened in the middle of the implementation's internal
    // work in event handlers. For example, let's assume putting a window (HWND)
    // in the fullscreen state requires removing window decorations
    // (SetWindowLong) and resizing the window (SetWindowPos), both being able
    // to invoke the resize handler (WM_SIZE) and thus the resize listener. In
    // this case, consider the following situation:
    // - SetWindowLong called for HWND.
    // - WM_SIZE arrives.
    // - Listener's OnResize called.
    // - Listener's OnResize does RequestClose, destroying the current HWND.
    // - Listener's OnResize does Open, creating a new HWND.
    // - SetWindowPos called for HWND, which is different now.
    // Here, SetWindowLong will be called for one native window, but then it
    // will be replaced, and SetWindowPos will be called for a different one.
    // No native window - external state updates change only the desired state,
    // don't go to the implementation immediately.
    // No actual state.
    // Listeners are not called.
    kClosedLeavingListeners,
    // The destructor has been called - should transition into this before doing
    // anything not only in the common, but in the implementation's destructor
    // as well. The transition must be done regardless of the previous phase as
    // there's no way the Window can stop its destruction from now on. This is
    // mostly similar to kClosedLeavingListeners, except there's no way to leave
    // this phase.
    kDeleting,
  };

  enum class CursorVisibility {
    kVisible,
    // Temporarily revealed, hidden if not interacting with the mouse.
    kAutoHidden,
    kHidden,
  };

  static std::unique_ptr<Window> Create(WindowedAppContext& app_context,
                                        const std::string_view title,
                                        uint32_t desired_logical_width,
                                        uint32_t desired_logical_height);

  virtual ~Window();

  WindowedAppContext& app_context() const { return app_context_; }

  Phase phase() const { return phase_; }
  bool HasActualState() const {
    // kOpening - for internal use by the implementation.
    // kOpen, kOpenBeforeClosing - for both external and internal use.
    // kClosing - for internal use by the implementation.
    return phase_ >= Phase::kOpening && phase_ <= Phase::kClosing;
  }

  void AddListener(WindowListener* listener);
  void RemoveListener(WindowListener* listener);
  void AddInputListener(WindowInputListener* listener, size_t z_order);
  void RemoveInputListener(WindowInputListener* listener);

  // `false` is returned only in case of an error while trying to perform the
  // platform window opening. If the window is already open, or just can't be
  // reopened in the current phase (as in this case it's assumed that the outer
  // will handle this situation properly and won't, for instance, leave a
  // process without windows - it will quit the application in OnBeforeClose of
  // the window closing of which was initiated before or even during opening,
  // for instance), `true` is returned. The functions of WindowListeners will be
  // called only for a newly opened platform window - and the listeners may
  // close or even destroy the window, in which case this function will still
  // return `true` to differentiate from an actual error - if it's really
  // necessary that the platform window is open after the call, check phase()
  // after calling.
  bool Open();
  // The call may or may not close the window immediately, depending on the
  // platform (phase() may still return an open phase, and events may still be
  // sent after the call). Use phase() to check if closing has actually
  // happened immediately.
  void RequestClose() {
    // Don't allow external close requests during opening for state consistency
    // inside OpenImpl (if an internal close happens during OpenImpl, the
    // implementation will be aware of that at least), and don't allow closing
    // twice.
    if (phase_ != Phase::kOpen) {
      return;
    }
    RequestCloseImpl();
    // Must not doing anything else with *this as callbacks might have been
    // triggered during closing (if it has actually even happened), and the
    // Window might have been deleted.
  }

  // The `public` state setters are for calling from outside.
  // The implementation must use the public getters to obtain the desired state
  // while applying, but for updating the actual state, or for overriding the
  // desired state, the `protected` On*Update functions must be used (overall
  // the On* functions are for the implementation's feedback).

  virtual uint32_t GetMediumDpi() const { return 96; }
  uint32_t GetDpi() const {
    uint32_t dpi = GetLatestDpiImpl();
    return dpi ? dpi : GetMediumDpi();
  }

  // Round trips are not guaranteed to return the same results.
  static constexpr uint32_t ConvertSizeDpi(uint32_t size, uint32_t new_dpi,
                                           uint32_t old_dpi) {
    // Always rounding up to prevent zero sizes (unless the input is zero) as
    // well as gaps at the edge.
    return uint32_t((uint64_t(size) * new_dpi + (old_dpi - 1)) / old_dpi);
  }
  uint32_t SizeToLogical(uint32_t size) const {
    return ConvertSizeDpi(size, GetMediumDpi(), GetDpi());
  }
  uint32_t SizeToPhysical(uint32_t size) const {
    return ConvertSizeDpi(size, GetDpi(), GetMediumDpi());
  }
  static constexpr int32_t ConvertPositionDpi(int32_t position,
                                              uint32_t new_dpi,
                                              uint32_t old_dpi) {
    // Rounding to the nearest mostly similar to Windows MulDiv.
    // Plus old_dpi / 2 for positive values, minus old_dpi / 2 for negative
    // values for consistent rounding for both positive and negative values (as
    // the `/` operator rounds towards zero).
    // Example:
    // (-3 - 1) / 3 == -1
    // (-2 - 1) / 3 == -1
    // (-1 - 1) / 3 == 0
    // ---
    // (0 + 1) / 3 == 0
    // (1 + 1) / 3 == 0
    // (2 + 1) / 3 == 1
    return int32_t((int64_t(position) * new_dpi +
                    int32_t(old_dpi >> 1) * (position < 0 ? -1 : 1)) /
                   old_dpi);
  }
  int32_t PositionToLogical(int32_t position) const {
    return ConvertPositionDpi(position, GetMediumDpi(), GetDpi());
  }
  int32_t PositionToPhysical(int32_t position) const {
    return ConvertPositionDpi(position, GetDpi(), GetMediumDpi());
  }

  // The desired logical size of the window when it's not maximized, regardless
  // of the current state of the window (maximized, fullscreen, etc.)
  // The implementation may update it, for instance, to clamp it, or when the
  // user resizes a non-maximized window.
  uint32_t GetDesiredLogicalWidth() const { return desired_logical_width_; }
  uint32_t GetDesiredLogicalHeight() const { return desired_logical_height_; }

  // 0 width or height may be returned even in case of an open window with a
  // valid non-zero-area surface depending on the platform.
  uint32_t GetActualPhysicalWidth() const {
    return HasActualState() ? actual_physical_width_ : 0;
  }
  uint32_t GetActualPhysicalHeight() const {
    return HasActualState() ? actual_physical_height_ : 0;
  }
  uint32_t GetActualLogicalWidth() const {
    return SizeToLogical(GetActualPhysicalWidth());
  }
  uint32_t GetActualLogicalHeight() const {
    return SizeToLogical(GetActualPhysicalHeight());
  }

  // Desired state stored by the common Window, modifiable both externally and
  // by the implementation (including from SetFullscreen itself).
  bool IsFullscreen() const { return fullscreen_; }
  void SetFullscreen(bool new_fullscreen);

  // Desired state stored by the common Window, externally modifiable, read-only
  // in the implementation.
  const std::string& GetTitle() const { return title_; }
  void SetTitle(const std::string_view new_title);

  // Desired state stored in a platform-dependent way in the implementation,
  // externally modifiable, read-only by the implementation unless from the
  // LoadAndApplyIcon implementation. The icon is in Windows .ico format.
  // Provide null buffer and / or zero size to reset the icon.
  void SetIcon(const void* buffer, size_t size);
  void ResetIcon() { SetIcon(nullptr, 0); }

  // Desired state stored by the common Window, externally modifiable, read-only
  // in the implementation.
  void SetMainMenu(std::unique_ptr<MenuItem> new_main_menu);
  MenuItem* GetMainMenu();
  void CompleteMainMenuItemsUpdate();
  void SetMainMenuEnabled(bool enabled);
  void SetMainMenuItemEnabled(int index, bool enabled);

  // Desired state stored by the common Window, externally modifiable, read-only
  // in the implementation.
  bool IsMouseCaptureRequested() const {
    return mouse_capture_request_count_ != 0;
  }
  void CaptureMouse();
  void ReleaseMouse();

  // Desired state stored by the common Window, externally modifiable, read-only
  // in the implementation.
  CursorVisibility GetCursorVisibility() const { return cursor_visibility_; }
  // Setting this to kAutoHidden from any _other_ visibility should hide the
  // cursor immediately - for instance, if the external code wants to auto-hide
  // the cursor in fullscreen, to allow going into the fullscreen mode to hide
  // the cursor instantly.
  void SetCursorVisibility(CursorVisibility new_cursor_visibility);

  bool HasFocus() const { return HasActualState() ? has_focus_ : false; }
  // May be applied in a delayed way or dropped at all, HasFocus will not
  // necessarily be true immediately.
  void Focus();

  // TODO(Triang3l): A resize function, primarily for snapping externally to
  // 1280x720, 1920x1080, and other 1:1 resolutions. It will need to resize the
  // window (to a desired logical size - the actual physical size is entirely
  // the feedback of the implementation) in the normal state, and possibly also
  // un-maximize (and possibly un-fullscreen) it (but this choice will possibly
  // need to be exposed to the caller). Because it's currently not needed, it's
  // not implemented to avoid platform-specific complexities regarding
  // maximization, DPI, etc.

  void SetPresenter(Presenter* presenter);

  // Request repainting of the surface. Can be called from non-UI threads as
  // long as they know the Surface exists and isn't in the middle of being
  // changed to another (the synchronization of this fact between the UI thread
  // and the caller thread must be done externally through OnSurfaceChanged).
  void RequestPaint() {
    if (presenter_surface_) {
      RequestPaintImpl();
    }
  }
  void RequestPresenterUIPaintFromUIThread() {
    if (presenter_) {
      presenter_->RequestUIPaintFromUIThread();
    }
  }

 protected:
  // The receiver, which must never be instantiated in the Window object itself
  // (rather, usually it should be created as a local variable, because only
  // LIFO-ordered creation and deletion of these is supported), that allows
  // deletion of the Window from within an event handler (which may invoke a
  // WindowListener, and window listeners are allowed to destroy windows; also
  // they may execute, for instance, the functions requested to be executed in
  // the UI thread in the WindowedAppContext, which are also allowed to destroy
  // windows - because if the former wasn't allowed, the latter would be
  // required to destroy windows as a result of UI interaction) to be caught by
  // functions inside the Window in order to stop interacting with `*this` and
  // returning after this happens.
  // Note that the receivers are signaled in the *end* of the destruction of the
  // common Window, when truly nothing can be done with it anymore, so it's safe
  // to assume that right after the creation of the WindowDestructionReceiver,
  // it will still be in an unsignaled state even if it's used somewhere in the
  // destructor. The reason is that the users of the WindowDestructionReceiver
  // are expected to stop accessing the Window *immediately* once
  // IsWindowDestroyed becomes `true`, and to leave it in a potentially
  // indeterminate state - but the code executed subsequently in the destructor
  // may still use that state meaningfully.
  class WindowDestructionReceiver {
   public:
    explicit WindowDestructionReceiver(Window* window) : window_(window) {
      if (window_) {
        outer_receiver_ = window_->innermost_destruction_receiver_;
        window_->innermost_destruction_receiver_ = this;
      }
    }

    ~WindowDestructionReceiver() {
      // If the window is not null, removal from the stack must happen
      // regardless of `phase_ == Phase::kDeleting`, because the window
      // destructor iterates the receivers after EnterDestructor(), and if the
      // receiver is not removed in this case, the destructor will do
      // use-after-free.
      if (window_) {
        // Only LIFO order is supported (normally through RAII).
        assert_true(window_->innermost_destruction_receiver_ == this);
        window_->innermost_destruction_receiver_ = outer_receiver_;
      }
    }

    bool IsWindowDestroyed() const { return window_ == nullptr; }

    // Helper functions for common usages of the receiver. Unlike
    // IsWindowDestroyed, these, however, may return false immediately on
    // creation.
    // Primarily for the implementation (most importantly its native event
    // handler), to stop interacting with the native window given that it was
    // possible before the function call it's guarded with.
    bool IsWindowDestroyedOrClosed() const {
      return IsWindowDestroyed() || window_->IsClosed();
    }
    // For guarding Apply* calls if one state setter needs to make multiple of
    // them (or just detecting if it's okay to call Apply*).
    bool IsWindowDestroyedOrStateInapplicable() const {
      return IsWindowDestroyed() || !window_->CanApplyState();
    }
    bool IsWindowDestroyedOrListenersUncallable() const {
      return IsWindowDestroyed() || !window_->CanSendEventsToListeners();
    }

   private:
    // The Window must set window_ to nullptr in its destructor.
    friend Window;
    Window* window_;
    WindowDestructionReceiver* outer_receiver_ = nullptr;
  };

  // Like in the Windows Media Player.
  // A more modern Windows example, Movies & TV in Windows 11 21H2, has 5000,
  // but it's too long especially for highly dynamic games.
  // Implementations may use different values according to the platform's UX
  // conventions.
  static constexpr uint32_t kDefaultCursorAutoHideMilliseconds = 3333;

  Window(WindowedAppContext& app_context, const std::string_view title,
         uint32_t desired_logical_width, uint32_t desired_logical_height);

  // If implementation-specific destruction happens, should be called in the
  // beginning of the implementation's destructor so the implementation can
  // destroy the platform window without doing something unsafe in destruction.
  void EnterDestructor() {
    phase_ = Phase::kDeleting;
    // Disconnect from the surface before destroying the window behind it.
    OnSurfaceChanged(false);
  }

  // For an open window, the implementation should return the current DPI for
  // the window. For a non-open one, it should be the closest approximation,
  // such as the last DPI from an existing window, the system DPI, or just the
  // medium DPI (0 returned from it will also be treated as medium DPI).
  virtual uint32_t GetLatestDpiImpl() const { return GetMediumDpi(); }

  // Deletion of the window may (and must) not happen in OpenImpl, the listeners
  // are deferred, so there's no need to use WindowDestructionReceiver in it.
  // In case of failure, the implementation must not leave itself in an
  // indeterminate state, so another attempt to open the window can be made.
  // The implementation must apply the following desired state if it needs it,
  // directly (not via Set* methods as they will be dropped during OpenImpl
  // since the window is not fully open yet):
  // - Title (GetTitle()).
  // - Icon (from the last LoadAndApplyIcon call).
  // - Main menu (GetMainMenu()) and its enablement.
  // - Desired logical size (GetDesiredLogicalWidth() / Height(), taking into
  //   account that the main menu, during the initial opening, should not be
  //   included in this size - it specifies the client area that painting will
  //   be done to), within the capabilities of the platform (may be clamped by
  //   the OS, for instance - in this case, OnDesiredLogicalSizeUpdate may be
  //   called from within OpenImpl to store the clamped size for later).
  // - Fullscreen (GetFullscreen()) - however, if possible, the calculations
  //   that would normally be done for a non-fullscreen window in OpenImpl
  //   should also be done if entering fullscreen, including the menu-related
  //   ones - first, the usual windowed geometry calculations should be done,
  //   and then fullscreen should be entered; but preferably still entering
  //   fullscreen before actually showing a visible window to the user for a
  //   seamless transition.
  // - Mouse capture (IsMouseCaptureRequested()).
  // - Cursor visibility (GetCursorVisibility()).
  // Also, as a result of the OpenImpl call, these function should be called
  // (immediately from within OpenImpl directly or indirectly through the native
  // event handling, or shortly after during the UI main loop) to provide the
  // initial actual state to the common Window code so it returns the correct
  // values:
  // - OnActualSizeUpdate, at least if the window isn't opened as not yet
  //   visible on screen (otherwise the size will be assumed to be 0x0 until the
  //   next size update caused by something likely requiring user interaction).
  // - OnFocusUpdate, at least if the focus has been obtained (otherwise the
  //   window will be assumed to be not in focus until it goes into the focus
  //   again).
  // Also, if some of the desired state that may be updated by the
  // implementation could not be applied (such as the fullscreen mode), and
  // certain values of the implementation state are either normally
  // differentiated within the implementation or are just meaningful considering
  // the platform's defaults (for instance, the platform inherently supports
  // only fullscreen, possibly doesn't have a concept of windows at all), it's
  // recommended to call the appropriate On*Update functions to update the
  // desired state to the actual one.
  virtual bool OpenImpl() = 0;
  virtual void RequestCloseImpl() = 0;

  // Apply* are called only if CanApplyState() is true (unless the function
  // should do more than just applying, such as also updating the desired state
  // in a platform-dependent way - see each individual function).
  // ApplyNew* means that the value has actually been changed to something
  // different than it was previously.
  virtual void ApplyNewFullscreen() {}
  virtual void ApplyNewTitle() {}
  // can_apply_state_in_current_phase whether the window is in a life cycle
  // phase that would normally accept Apply calls (the native window surely
  // exists), since this function may be called in closed states too to update
  // the desired icon (since it's stored in the implementation) - the
  // implementation may, however, ignore it and use a more granular check of the
  // existence of the native window and the safety of updating the icon for
  // better internal state consistency. The icon is in Windows .ico format. If
  // the buffer is null or the size is 0, the icon should be reset to the
  // default one. Returns whether the icon has been updated successfully.
  virtual void LoadAndApplyIcon(const void* buffer, size_t size,
                                bool can_apply_state_in_current_phase) {}
  MenuItem* GetMainMenu() const { return main_menu_.get(); }
  // May be called to add, replace or remove the main menu.
  virtual void ApplyNewMainMenu(MenuItem* old_main_menu) {}
  // If there's main menu, and state can be applied, will be called to make the
  // implementation's state consistent with the new state of the MenuItems of
  // the main menu after changes have been made to them.
  virtual void CompleteMainMenuItemsUpdateImpl() {}
  // Will be called even if capturing while the mouse is already assumed to be
  // captured, in case something has released it in the OS.
  virtual void ApplyNewMouseCapture() {}
  virtual void ApplyNewMouseRelease() {}
  virtual void ApplyNewCursorVisibility(
      CursorVisibility old_cursor_visibility) {}
  // If state can be applied, this is called to request bringing the window into
  // focus (and once that's done by the OS, update the actual focus state). Does
  // nothing otherwise (focus can't be requested before the window is open, a
  // closed window is always assumed to be not in focus).
  virtual void FocusImpl() {}

  Presenter* presenter() const { return presenter_; }
  bool HasSurface() const { return presenter_surface_ != nullptr; }
  // If new_surface_potentially_exists is false, creation of the new surface for
  // the window won't be updated, and it may be called from the destructor (via
  // EnterDestructor to destroy the surface before destroying what it depends
  // on) as no virtual functions (including CreateSurface) will be called.
  // This function is nonvirtual itself for this reason as well.
  void OnSurfaceChanged(bool new_surface_potentially_exists);
  // Called only for an open window.
  virtual std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) = 0;
  // Called only if the Surface exists.
  virtual void RequestPaintImpl() = 0;

  // Will also disconnect the surface if needed.
  void OnBeforeClose(WindowDestructionReceiver& destruction_receiver);
  void OnAfterClose();

  // These functions may usually also be called as part of the opening process
  // from within OpenImpl (directly or through the platform event handler
  // invoked during it) to actualize the state for the newly createad window,
  // especially if it's different than the desired one.
  void OnDpiChanged(UISetupEvent& e,
                    WindowDestructionReceiver& destruction_receiver);
  void OnMonitorUpdate(MonitorUpdateEvent& e);
  // For calling when the platform changes something in the non-maximized,
  // non-fullscreen size of the window.
  void OnDesiredLogicalSizeUpdate(uint32_t new_desired_logical_width,
                                  uint32_t new_desired_logical_height) {
    desired_logical_width_ = new_desired_logical_width;
    desired_logical_height_ = new_desired_logical_height;
  }
  // If the size of the client area is the same as the currently assumed one
  // (the desired / last size for a newly opened / reopened window, the last
  // actual size for an already open window), does nothing and returns false.
  // Otherwise, updates the size, notifies what depends on the size about the
  // change, and returns true. Not storing the new size in the UISetupEvent
  // because a resize listener may request another resize, in which case it will
  // be outdated - listeners must query the new physical size from the window
  // explicitly.
  bool OnActualSizeUpdate(uint32_t new_physical_width,
                          uint32_t new_physical_height,
                          WindowDestructionReceiver& destruction_receiver);
  void OnDesiredFullscreenUpdate(bool new_fullscreen) {
    fullscreen_ = new_fullscreen;
  }
  void OnFocusUpdate(bool new_has_focus,
                     WindowDestructionReceiver& destruction_receiver);

  // Pass true as force_paint in case the platform can't retain the image from
  // the previous paint so it won't be skipped if there are no content updates.
  void OnPaint(bool force_paint = false);

  void OnFileDrop(FileDropEvent& e,
                  WindowDestructionReceiver& destruction_receiver);

  void OnKeyDown(KeyEvent& e, WindowDestructionReceiver& destruction_receiver);
  void OnKeyUp(KeyEvent& e, WindowDestructionReceiver& destruction_receiver);
  void OnKeyChar(KeyEvent& e, WindowDestructionReceiver& destruction_receiver);

  void OnMouseDown(MouseEvent& e,
                   WindowDestructionReceiver& destruction_receiver);
  void OnMouseMove(MouseEvent& e,
                   WindowDestructionReceiver& destruction_receiver);
  void OnMouseUp(MouseEvent& e,
                 WindowDestructionReceiver& destruction_receiver);
  void OnMouseWheel(MouseEvent& e,
                    WindowDestructionReceiver& destruction_receiver);

  void OnTouchEvent(TouchEvent& e,
                    WindowDestructionReceiver& destruction_receiver);

 private:
  struct ListenerIterationContext {
    explicit ListenerIterationContext(ListenerIterationContext* outer_context,
                                      size_t first_index = 0)
        : outer_context(outer_context), next_index(first_index) {}
    // To support nested listener calls, in case a listener does some
    // interaction with the window that results in more events being triggered
    // (such as calling Windows API functions that return a result from a
    // message handled by a window, rather that simply enqueueing the message).
    ListenerIterationContext* outer_context;
    // Using indices, not iterators, because after the erasure, the adjustment
    // must be done for the vector element indices that would be in the iterator
    // range that would be invalidated.
    size_t next_index;
  };

  struct InputListenerIterationContext {
    explicit InputListenerIterationContext(
        InputListenerIterationContext* outer_context,
        std::multimap<size_t, WindowInputListener*>::const_reverse_iterator
            first_iterator,
        size_t first_z_order = SIZE_MAX)
        : outer_context(outer_context),
          next_iterator(first_iterator),
          current_z_order(first_z_order) {}
    // To support nested listener calls.
    InputListenerIterationContext* outer_context;
    // Reverse iterator because input handlers with a higher Z order index may
    // correspond to what's displayed on top of what has a lower Z order index,
    // so what's higher may consum the event.
    std::multimap<size_t, WindowInputListener*>::const_reverse_iterator
        next_iterator;
    size_t current_z_order;
  };

  // If the window is closed, the platform native window is either being
  // destroyed, or doesn't exist anymore, and thus it's in a non-interactive
  // state.
  bool IsClosed() const {
    return phase_ < Phase::kOpening && phase_ > Phase::kOpenBeforeClosing;
  }

  bool CanApplyState() const {
    // In kOpening, OpenImpl itself pulls the desired state itself and applies
    // it, the Apply* functions can't be called and are unsafe to call because
    // the implementation is an incomplete state, with the platform window
    // potentially not existing. In kOpenBeforeClosing, as the native window
    // still hasn't been destroyed and it can receive new state, still allowing
    // applying new state for more consistency between the desired and the
    // actual state during the final listener invocation.
    return phase_ >= Phase::kOpen && phase_ <= Phase::kOpenBeforeClosing;
  }

  bool CanSendEventsToListeners() const {
    return phase_ >= Phase::kOpen && phase_ <= Phase::kOpenBeforeClosing;
  }
  // The listeners may delete the Window - check the destruction receiver after
  // calling and stop doing anything accessing *this if that happens.
  void SendEventToListeners(std::function<void(WindowListener*)> fn,
                            WindowDestructionReceiver& destruction_receiver);
  void PropagateEventThroughInputListeners(
      std::function<bool(WindowInputListener*)> fn,
      WindowDestructionReceiver& destruction_receiver);

  std::unique_ptr<Surface> CreateSurface(Surface::TypeFlags allowed_types) {
    // If opening, surface creation is deferred until all the initial setup has
    // completed. Destruction of the surface is also a part of the closing
    // process in OnBeforeClose.
    if (phase_ != Phase::kOpen) {
      return nullptr;
    }
    return CreateSurfaceImpl(allowed_types);
  }

  WindowedAppContext& app_context_;

  Phase phase_ = Phase::kClosedOpenable;
  WindowDestructionReceiver* innermost_destruction_receiver_ = nullptr;

  // All currently-attached listeners that get event notifications.
  std::vector<WindowListener*> listeners_;
  // Ordered by the Z order, and then by the time of addition (but executed in
  // reverse order).
  // Note: All the iteration logic involving this Z ordering must be the same as
  // in drawing (in the UI drawers in the Presenter), but in reverse.
  std::multimap<size_t, WindowInputListener*> input_listeners_;
  // Linked list-based stacks of the contexts of the listener iterations
  // currently being done, usually allocated on the stack.
  ListenerIterationContext* innermost_listener_iteration_context_ = nullptr;
  InputListenerIterationContext* innermost_input_listener_iteration_context_ =
      nullptr;

  uint32_t desired_logical_width_ = 0;
  uint32_t desired_logical_height_ = 0;
  // Set by the implementation via OnActualSizeUpdate (from OpenImpl or from the
  // platform resize handler).
  uint32_t actual_physical_width_ = 0;
  uint32_t actual_physical_height_ = 0;

  bool fullscreen_ = false;

  std::string title_;

  std::unique_ptr<MenuItem> main_menu_;

  uint32_t mouse_capture_request_count_ = 0;

  CursorVisibility cursor_visibility_ = CursorVisibility::kVisible;

  bool has_focus_ = false;

  Presenter* presenter_ = nullptr;
  std::unique_ptr<Surface> presenter_surface_;
  // Whether currently in InPaint to prevent recursive painting in case it's
  // triggered somehow from within painting again, because painting is much more
  // complex than just a small state update, and recursive painting is
  // completely unsupported by the Presenter.
  bool is_painting_ = false;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_H_
