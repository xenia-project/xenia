/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HELPER_SDL_SDL_HELPER_H_
#define XENIA_HELPER_SDL_SDL_HELPER_H_

#include "SDL.h"

namespace xe {
namespace helper {
namespace sdl {
// This helper class exists to independently use SDL from different parts of
// xenia.
class SDLHelper {
 public:
  // To configure the SDL library for use in xenia call this function before
  // SDL_InitSubSystem() is called.
  static bool Prepare();
  static bool IsPrepared() { return is_prepared_; }

 private:
  static bool SetHints();
  static bool RedirectLog();

 private:
  static bool is_prepared_;
};
}  // namespace sdl
}  // namespace helper
}  // namespace xe

#endif  // XENIA_HELPER_SDL_SDL_HELPER_H_
