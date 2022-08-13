/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PROFILING_H_
#define XENIA_BASE_PROFILING_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/ui/ui_drawer.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window_listener.h"

#if XE_PLATFORM_WIN32 && 0
#define XE_OPTION_PROFILING 1
#define XE_OPTION_PROFILING_UI 1
#else
#define XE_OPTION_PROFILING 0
#endif  // XE_PLATFORM_WIN32

#if XE_OPTION_PROFILING
// Pollutes the global namespace. Yuck.
#define MICROPROFILE_MAX_THREADS 256
#include <microprofile/microprofile.h>
#endif  // XE_OPTION_PROFILING

namespace xe {
namespace ui {
class ImmediateDrawer;
class MicroprofileDrawer;
class Presenter;
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {

#if XE_OPTION_PROFILING

// Defines a profiling scope for CPU tasks.
// Use `SCOPE_profile_cpu(name)` to activate the scope.
#define DEFINE_profile_cpu(name, group_name, scope_name) \
  MICROPROFILE_DEFINE(name, group_name, scope_name,      \
                      xe::Profiler::GetColor(scope_name))

// Declares a previously defined profile scope. Use in a translation unit.
#define DECLARE_profile_cpu(name) MICROPROFILE_DECLARE(name)

// Defines a profiling scope for GPU tasks.
// Use `COUNT_profile_gpu(name)` to activate the scope.
#define DEFINE_profile_gpu(name, group_name, scope_name) \
  MICROPROFILE_DEFINE_GPU(name, group_name, scope_name,  \
                          xe::Profiler::GetColor(scope_name))

// Declares a previously defined profile scope. Use in a translation unit.
#define DECLARE_profile_gpu(name) MICROPROFILE_DECLARE_GPU(name)

// Enters a previously defined CPU profiling scope, active for the duration
// of the containing block.
#define SCOPE_profile_cpu(name) MICROPROFILE_SCOPE(name)

// Enters a CPU profiling scope, active for the duration of the containing
// block. No previous definition required.
#define SCOPE_profile_cpu_i(group_name, scope_name) \
  MICROPROFILE_SCOPEI(group_name, scope_name,       \
                      xe::Profiler::GetColor(scope_name))

// Enters a CPU profiling scope by function name, active for the duration of
// the containing block. No previous definition required.
#define SCOPE_profile_cpu_f(group_name)         \
  MICROPROFILE_SCOPEI(group_name, __FUNCTION__, \
                      xe::Profiler::GetColor(__FUNCTION__))

// Enters a previously defined GPU profiling scope, active for the duration
// of the containing block.
#define SCOPE_profile_gpu(name) MICROPROFILE_SCOPEGPU(name)

// Enters a GPU profiling scope, active for the duration of the containing
// block. No previous definition required.
#define SCOPE_profile_gpu_i(group_name, scope_name) \
  MICROPROFILE_SCOPEGPUI(group_name, scope_name,    \
                         xe::Profiler::GetColor(scope_name))

// Enters a GPU profiling scope by function name, active for the duration of
// the containing block. No previous definition required.
#define SCOPE_profile_gpu_f(group_name)            \
  MICROPROFILE_SCOPEGPUI(group_name, __FUNCTION__, \
                         xe::Profiler::GetColor(__FUNCTION__))

// Adds a number to a counter
#define COUNT_profile_add(name, count) MICROPROFILE_COUNTER_ADD(name, count)

// Subtracts a number to a counter
#define COUNT_profile_sub(name, count) MICROPROFILE_COUNTER_SUB(name, count)

// Sets a counter's value
#define COUNT_profile_set(name, count) MICROPROFILE_COUNTER_SET(name, count)

// Tracks a CPU value counter.
#define COUNT_profile_cpu(name, count) MICROPROFILE_META_CPU(name, count)

// Tracks a GPU value counter.
#define COUNT_profile_gpu(name, count) MICROPROFILE_META_GPU(name, count)

#else

#define DEFINE_profile_cpu(name, group_name, scope_name)
#define DEFINE_profile_gpu(name, group_name, scope_name)
#define DECLARE_profile_cpu(name)
#define DECLARE_profile_gpu(name)
#define SCOPE_profile_cpu(name) \
  do {                          \
  } while (false)
#define SCOPE_profile_cpu_f(name) \
  do {                            \
  } while (false)
#define SCOPE_profile_cpu_i(group_name, scope_name) \
  do {                                              \
  } while (false)
#define SCOPE_profile_gpu(name) \
  do {                          \
  } while (false)
#define SCOPE_profile_gpu_f(name) \
  do {                            \
  } while (false)
#define SCOPE_profile_gpu_i(group_name, scope_name) \
  do {                                              \
  } while (false)
#define COUNT_profile_add(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_sub(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_set(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_cpu(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_gpu(name, count) \
  do {                                 \
  } while (false)

#ifndef MICROPROFILE_TEXT_WIDTH
#define MICROPROFILE_TEXT_WIDTH 1
#define MICROPROFILE_TEXT_HEIGHT 1
#endif  // !MICROPROFILE_TEXT_WIDTH

#endif  // XE_OPTION_PROFILING

class Profiler {
 public:
  static bool is_enabled();
  static bool is_visible();

  // Initializes the profiler. Call at startup.
  static void Initialize();
  // Dumps data to stdout.
  static void Dump();
  // Cleans up profiling, releasing all memory.
  static void Shutdown();

  // Computes a color from the given string.
  static uint32_t GetColor(const char* str);

  // Activates the calling thread for profiling.
  // This must be called immediately after launching a thread.
  static void ThreadEnter(const char* name = nullptr);
  // Deactivates the calling thread for profiling.
  static void ThreadExit();

  static void ToggleDisplay();
  static void TogglePause();

  // Initializes input for the given window and drawing for the given presenter
  // and immediate drawer.
  static void SetUserIO(size_t z_order, ui::Window* window,
                        ui::Presenter* presenter,
                        ui::ImmediateDrawer* immediate_drawer);
  // Gets the current drawer, if any.
  static ui::MicroprofileDrawer* drawer() {
#if XE_OPTION_PROFILING_UI
    return drawer_.get();
#else
    return nullptr;
#endif
  }
  // Presents the profiler to the bound display, if any.
  static void Present(ui::UIDrawContext& ui_draw_context);
  // Starts a new frame on the profiler
  static void Flip();

 private:
#if XE_OPTION_PROFILING
  class ProfilerWindowInputListener final : public ui::WindowInputListener {
   public:
    void OnKeyDown(ui::KeyEvent& e) override;
    void OnKeyUp(ui::KeyEvent& e) override;
#if XE_OPTION_PROFILING_UI
    void OnMouseDown(ui::MouseEvent& e) override;
    void OnMouseMove(ui::MouseEvent& e) override;
    void OnMouseUp(ui::MouseEvent& e) override;
    void OnMouseWheel(ui::MouseEvent& e) override;
#endif  // XE_OPTION_PROFILING_UI
  };
  // For now, no need for OnDpiChanged in a WindowListener because redrawing is
  // done continuously.

#if XE_OPTION_PROFILING_UI
  class ProfilerUIDrawer final : public ui::UIDrawer {
   public:
    void Draw(ui::UIDrawContext& context) override;
  };
#endif  // XE_OPTION_PROFILING_UI

#if XE_OPTION_PROFILING_UI
  static void SetMousePosition(int32_t x, int32_t y, int32_t wheel_delta);
#endif  // XE_OPTION_PROFILING_UI
  static void PostInputEvent();

  static ProfilerWindowInputListener input_listener_;
  static size_t z_order_;
  static ui::Window* window_;
#if XE_OPTION_PROFILING_UI
  static ProfilerUIDrawer ui_drawer_;
  static ui::Presenter* presenter_;
  static std::unique_ptr<ui::MicroprofileDrawer> drawer_;
  static bool dpi_scaling_;
#endif  // XE_OPTION_PROFILING_UI
#endif  // XE_OPTION_PROFILING
};

}  // namespace xe

#endif  // XENIA_BASE_PROFILING_H_
