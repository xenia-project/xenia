/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SPIRV_SPIRV_ASSEMBLER_H_
#define XENIA_UI_SPIRV_SPIRV_ASSEMBLER_H_

#include <memory>
#include <string>

#include "xenia/ui/spirv/spirv_util.h"

namespace xe {
namespace ui {
namespace spirv {

class SpirvAssembler {
 public:
  class Result {
   public:
    Result(spv_binary binary, spv_diagnostic diagnostic);
    ~Result();

    // True if the result has an error associated with it.
    bool has_error() const;
    // Line of the error in the provided source text.
    size_t error_source_line() const;
    // Column of the error in the provided source text.
    size_t error_source_column() const;
    // Human-readable description of the error.
    const char* error_string() const;

    // Assembled SPIRV binary.
    // Returned pointer lifetime is tied to this Result instance.
    const uint32_t* words() const;
    // Size of the SPIRV binary, in words.
    size_t word_count() const;

   private:
    spv_binary binary_ = nullptr;
    spv_diagnostic diagnostic_ = nullptr;
  };

  SpirvAssembler();
  ~SpirvAssembler();

  // Assembles the given source text into a SPIRV binary.
  // The return will be nullptr if assembly fails due to a library error.
  // The return may have an error set on it if the source text is malformed.
  std::unique_ptr<Result> Assemble(const char* source_text,
                                   size_t source_text_length);
  std::unique_ptr<Result> Assemble(const std::string& source_text) {
    return Assemble(source_text.c_str(), source_text.size());
  }

 private:
  spv_context spv_context_ = nullptr;
};

}  // namespace spirv
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SPIRV_SPIRV_ASSEMBLER_H_
