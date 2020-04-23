/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/helper/sdl/sdl_helper.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace helper {
namespace sdl {
bool SDLHelper::is_prepared_ = false;

bool SDLHelper::Prepare() {
  if (is_prepared_) {
    return true;
  }
  is_prepared_ = true;

  is_prepared_ &= SetHints();
  is_prepared_ &= RedirectLog();

  return is_prepared_;
}

bool SDLHelper::SetHints() {
  bool suc = true;

  // SDL calls timeBeginPeriod(1) but xenia sets this to a lower value before
  // using NtSetTimerResolution(). Having that value overwritten causes overall
  // fps drops. Use override priority as timer resolution should always be
  // managed by xenia. https://bugzilla.libsdl.org/show_bug.cgi?id=5104
  suc &= SDL_SetHintWithPriority(SDL_HINT_TIMER_RESOLUTION, "0",
                                 SDL_HINT_OVERRIDE) == SDL_TRUE;

  return suc;
}

bool SDLHelper::RedirectLog() {
  // Redirect SDL_Log* output (internal library stuff) to our log system.
  SDL_LogSetOutputFunction(
      [](void* userdata, int category, SDL_LogPriority priority,
         const char* message) {
        const char* msg_fmt = "SDL: {}";
        switch (priority) {
          case SDL_LOG_PRIORITY_VERBOSE:
          case SDL_LOG_PRIORITY_DEBUG:
            XELOGD(msg_fmt, message);
            break;
          case SDL_LOG_PRIORITY_INFO:
            XELOGI(msg_fmt, message);
            break;
          case SDL_LOG_PRIORITY_WARN:
            XELOGW(msg_fmt, message);
            break;
          case SDL_LOG_PRIORITY_ERROR:
          case SDL_LOG_PRIORITY_CRITICAL:
            XELOGE(msg_fmt, message);
            break;
          default:
            XELOGI(msg_fmt, message);
            assert_always("SDL: Unknown log priority");
            break;
        }
      },
      nullptr);
  // SDL itself isn't that talkative. Additionally to this settings there are
  // hints that can be switched on to output additional internal logging
  // information.
  SDL_LogSetAllPriority(SDL_LogPriority::SDL_LOG_PRIORITY_VERBOSE);
  return true;
}
}  // namespace sdl
}  // namespace helper
}  // namespace xe
