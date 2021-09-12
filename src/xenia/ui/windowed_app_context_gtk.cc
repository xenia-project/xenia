/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_gtk.h"

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

namespace xe {
namespace ui {

GTKWindowedAppContext::~GTKWindowedAppContext() {
  // Remove the idle sources as their data pointer (to this context) is now
  // outdated.
  if (quit_idle_pending_) {
    g_source_remove(quit_idle_pending_);
  }
  {
    // Lock the mutex for a pending_functions_idle_pending_ access memory
    // barrier, even though no other threads can access this object anymore.
    std::lock_guard<std::mutex> pending_functions_idle_pending_lock(
        pending_functions_idle_pending_mutex_);
    if (pending_functions_idle_pending_) {
      g_source_remove(pending_functions_idle_pending_);
    }
  }
}

void GTKWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  std::lock_guard<std::mutex> pending_functions_idle_pending_lock(
      pending_functions_idle_pending_mutex_);
  if (!pending_functions_idle_pending_) {
    pending_functions_idle_pending_ =
        gdk_threads_add_idle(PendingFunctionsSourceFunc, this);
  }
}

void GTKWindowedAppContext::PlatformQuitFromUIThread() {
  if (quit_idle_pending_ || !gtk_main_level()) {
    return;
  }
  gtk_main_quit();
  // Quit from all loops in the context, current inner, upcoming inner (this is
  // why the idle function is added even at the main level of 1), until we can
  // return to the tail of RunMainGTKLoop.
  quit_idle_pending_ = gdk_threads_add_idle(QuitSourceFunc, this);
}

void GTKWindowedAppContext::RunMainGTKLoop() {
  // For safety, in case the quit request somehow happened before the loop.
  if (HasQuitFromUIThread()) {
    return;
  }
  assert_zero(gtk_main_level());
  gtk_main();
  // Something else - not QuitFromUIThread - might have done gtk_main_quit for
  // the outer loop. If it has exited for some reason, let the context know, so
  // pending function won't be added pointlessly.
  QuitFromUIThread();
  // Don't interfere with main loops of other contexts that will possibly be
  // executed in this thread later (if this is not the only app that will be
  // executed in this process, for example, or if some WindowedAppContext-less
  // dialog is opened after the windowed app has quit) - cancel the scheduled
  // quit chain as we have quit already.
  if (quit_idle_pending_) {
    g_source_remove(quit_idle_pending_);
    quit_idle_pending_ = 0;
  }
}

gboolean GTKWindowedAppContext::PendingFunctionsSourceFunc(gpointer data) {
  auto& app_context = *static_cast<GTKWindowedAppContext*>(data);
  // Allow new pending function notifications to be made, and stop tracking the
  // lifetime of the idle source that is already being executed and thus
  // removed. This must be done before executing the functions (if done after,
  // notifications may be missed if they happen between the execution of the
  // functions and the reset of pending_functions_idle_pending_).
  {
    std::lock_guard<std::mutex> pending_functions_idle_pending_lock(
        app_context.pending_functions_idle_pending_mutex_);
    app_context.pending_functions_idle_pending_ = 0;
  }
  app_context.ExecutePendingFunctionsFromUIThread();
  return G_SOURCE_REMOVE;
}

gboolean GTKWindowedAppContext::QuitSourceFunc(gpointer data) {
  auto app_context = static_cast<GTKWindowedAppContext*>(data);
  guint main_level = gtk_main_level();
  assert_not_zero(main_level);
  if (!main_level) {
    app_context->quit_idle_pending_ = 0;
    return G_SOURCE_REMOVE;
  }
  gtk_main_quit();
  // Quit from all loops in the context, current inner, upcoming inner (this is
  // why the idle function is added even at the main level of 1), until we can
  // return to the tail of RunMainGTKLoop.
  app_context->quit_idle_pending_ =
      gdk_threads_add_idle(QuitSourceFunc, app_context);
  return G_SOURCE_REMOVE;
}

}  // namespace ui
}  // namespace xe
