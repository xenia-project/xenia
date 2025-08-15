/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

 #ifndef XENIA_UI_METAL_METAL_API_H_
 #define XENIA_UI_METAL_METAL_API_H_

// IMPORTANT: Define this BEFORE including any Metal Shader Converter (MSC) headers
// This tells MSC to use metal-cpp types instead of Objective-C
#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "third_party/metal-cpp/Metal/MTLDevice.hpp"
#define IR_RUNTIME_METALCPP
// Include metal-cpp (without implementation - that's in metal_api.cc)
#include "third_party/metal-shader-converter/include/metal_irconverter/metal_irconverter.h"
// Don't include the runtime header here - that needs IR_PRIVATE_IMPLEMENTATION
// which should only be in ir_runtime_impl.cc

 #endif