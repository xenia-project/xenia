/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Single compilation unit for Metal IR Converter Runtime implementation
#include "third_party/metal-cpp/Metal/Metal.hpp"

#define IR_RUNTIME_METALCPP
#define IR_PRIVATE_IMPLEMENTATION   // Generate the implementation exactly once

// Use the actual runtime header with absolute path
#include "/usr/local/include/metal_irconverter_runtime/metal_irconverter_runtime.h"