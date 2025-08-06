#ifndef METAL_DEBUG_H_
#define METAL_DEBUG_H_

#include "xenia/base/logging.h"

// Debug macro to track Metal object lifecycle
#define METAL_DEBUG 1

#if METAL_DEBUG
#define METAL_LOG_CREATE(obj, type, name) \
  XELOGI("METAL_DEBUG: CREATE {} {} at {}", type, name, static_cast<void*>(obj))
  
#define METAL_LOG_RELEASE(obj, type, name) \
  XELOGI("METAL_DEBUG: RELEASE {} {} at {}", type, name, static_cast<void*>(obj))
  
#define METAL_LOG_AUTORELEASE(obj, type, name) \
  XELOGI("METAL_DEBUG: AUTORELEASE {} {} at {}", type, name, static_cast<void*>(obj))
#else
#define METAL_LOG_CREATE(obj, type, name)
#define METAL_LOG_RELEASE(obj, type, name)
#define METAL_LOG_AUTORELEASE(obj, type, name)
#endif

#endif  // METAL_DEBUG_H_