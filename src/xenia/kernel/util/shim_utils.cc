/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/shim_utils.h"

DEFINE_bool(log_high_frequency_kernel_calls, false,
            "Log kernel calls with the kHighFrequency tag.");

namespace xe {
namespace kernel {
namespace shim {

thread_local StringBuffer string_buffer_;

StringBuffer* thread_local_string_buffer() { return &string_buffer_; }

}  // namespace shim
}  // namespace kernel
}  // namespace xe
