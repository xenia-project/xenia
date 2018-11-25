#include "xenia/base/logging.h"

extern "C" void xenia_log(const char* fmt, ...) {
  char buffer[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  XELOGW("mspack: %s", buffer);
}
