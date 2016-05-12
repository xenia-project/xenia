/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_X11_H_
#define XENIA_BASE_PLATFORM_X11_H_

// NOTE: if you're including this file it means you are explicitly depending
// on Linux headers. Including this file outside of linux platform specific
// source code will break portability

#include "xenia/base/platform.h"

// Xlib is used only for GLX interaction, the window management and input
// events are done with gtk/gdk
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

//Used for window management. Gtk is for GUI and wigets, gdk is for lower
//level events like key presses, mouse events, etc
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#endif  // XENIA_BASE_PLATFORM_X11_H_
