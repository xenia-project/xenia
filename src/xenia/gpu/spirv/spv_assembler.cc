/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv/spv_assembler.h"

#include "third_party/spirv-tools/include/libspirv/libspirv.h"
#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace spirv {

SpvAssembler::Result::Result(spv_binary binary, spv_diagnostic diagnostic)
    : binary_(binary), diagnostic_(diagnostic) {}

SpvAssembler::Result::~Result() {
  if (binary_) {
    spvBinaryDestroy(binary_);
  }
  if (diagnostic_) {
    spvDiagnosticDestroy(diagnostic_);
  }
}

bool SpvAssembler::Result::has_error() const { return !!diagnostic_; }

size_t SpvAssembler::Result::error_source_line() const {
  return diagnostic_ ? diagnostic_->position.line : 0;
}

size_t SpvAssembler::Result::error_source_column() const {
  return diagnostic_ ? diagnostic_->position.column : 0;
}

const char* SpvAssembler::Result::error_string() const {
  return diagnostic_ ? diagnostic_->error : "";
}

const uint32_t* SpvAssembler::Result::words() const {
  return binary_ ? binary_->code : nullptr;
}

size_t SpvAssembler::Result::word_count() const {
  return binary_ ? binary_->wordCount : 0;
}

SpvAssembler::SpvAssembler() : spv_context_(spvContextCreate()) {}

SpvAssembler::~SpvAssembler() { spvContextDestroy(spv_context_); }

std::unique_ptr<SpvAssembler::Result> SpvAssembler::Assemble(
    const char* source_text, size_t source_text_length) {
  spv_binary binary = nullptr;
  spv_diagnostic diagnostic = nullptr;
  auto result_code = spvTextToBinary(spv_context_, source_text,
                                     source_text_length, &binary, &diagnostic);
  std::unique_ptr<Result> result(new Result(binary, diagnostic));
  if (result_code) {
    XELOGE("Failed to assemble spv: %d", result_code);
    if (result->has_error()) {
      return result;
    } else {
      return nullptr;
    }
  }
  return result;
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe
