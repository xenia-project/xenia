/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/spirv/spirv_validator.h"

#include "third_party/spirv-tools/include/spirv-tools/libspirv.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace spirv {

SpirvValidator::Result::Result(spv_text text, spv_diagnostic diagnostic)
    : text_(text), diagnostic_(diagnostic) {}

SpirvValidator::Result::~Result() {
  if (text_) {
    spvTextDestroy(text_);
  }
  if (diagnostic_) {
    spvDiagnosticDestroy(diagnostic_);
  }
}

bool SpirvValidator::Result::has_error() const { return !!diagnostic_; }

size_t SpirvValidator::Result::error_word_index() const {
  return diagnostic_ ? diagnostic_->position.index : 0;
}

const char* SpirvValidator::Result::error_string() const {
  return diagnostic_ ? diagnostic_->error : "";
}

const char* SpirvValidator::Result::text() const {
  return text_ ? text_->str : "";
}

std::string SpirvValidator::Result::to_string() const {
  return text_ ? std::string(text_->str, text_->length) : "";
}

void SpirvValidator::Result::AppendText(StringBuffer* target_buffer) const {
  if (text_) {
    target_buffer->AppendBytes(reinterpret_cast<const uint8_t*>(text_->str),
                               text_->length);
  }
}

SpirvValidator::SpirvValidator()
    : spv_context_(spvContextCreate(SPV_ENV_UNIVERSAL_1_1)) {}
SpirvValidator::~SpirvValidator() { spvContextDestroy(spv_context_); }

std::unique_ptr<SpirvValidator::Result> SpirvValidator::Validate(
    const uint32_t* words, size_t word_count) {
  spv_text text = nullptr;
  spv_diagnostic diagnostic = nullptr;
  spv_const_binary_t binary = {words, word_count};
  auto result_code = spvValidate(spv_context_, &binary, &diagnostic);
  std::unique_ptr<Result> result(new Result(text, diagnostic));
  if (result_code) {
    XELOGE("Failed to validate spv: %d", result_code);
    if (result->has_error()) {
      return result;
    } else {
      return nullptr;
    }
  }
  return result;
}

}  // namespace spirv
}  // namespace ui
}  // namespace xe