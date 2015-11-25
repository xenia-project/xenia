/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/spirv/spirv_disassembler.h"

#include "third_party/spirv-tools/include/libspirv/libspirv.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace spirv {

SpirvDisassembler::Result::Result(spv_text text, spv_diagnostic diagnostic)
    : text_(text), diagnostic_(diagnostic) {}

SpirvDisassembler::Result::~Result() {
  if (text_) {
    spvTextDestroy(text_);
  }
  if (diagnostic_) {
    spvDiagnosticDestroy(diagnostic_);
  }
}

bool SpirvDisassembler::Result::has_error() const { return !!diagnostic_; }

size_t SpirvDisassembler::Result::error_word_index() const {
  return diagnostic_ ? diagnostic_->position.index : 0;
}

const char* SpirvDisassembler::Result::error_string() const {
  return diagnostic_ ? diagnostic_->error : "";
}

const char* SpirvDisassembler::Result::text() const {
  return text_ ? text_->str : "";
}

std::string SpirvDisassembler::Result::to_string() const {
  return text_ ? std::string(text_->str, text_->length) : "";
}

void SpirvDisassembler::Result::AppendText(StringBuffer* target_buffer) const {
  if (text_) {
    target_buffer->AppendBytes(reinterpret_cast<const uint8_t*>(text_->str),
                               text_->length);
  }
}

SpirvDisassembler::SpirvDisassembler() : spv_context_(spvContextCreate()) {}

SpirvDisassembler::~SpirvDisassembler() { spvContextDestroy(spv_context_); }

std::unique_ptr<SpirvDisassembler::Result> SpirvDisassembler::Disassemble(
    const uint32_t* words, size_t word_count) {
  spv_text text = nullptr;
  spv_diagnostic diagnostic = nullptr;
  auto result_code =
      spvBinaryToText(spv_context_, words, word_count,
                      SPV_BINARY_TO_TEXT_OPTION_INDENT, &text, &diagnostic);
  std::unique_ptr<Result> result(new Result(text, diagnostic));
  if (result_code) {
    XELOGE("Failed to disassemble spv: %d", result_code);
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
