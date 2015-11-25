/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SPIRV_SPIRV_DISASSEMBLER_H_
#define XENIA_UI_SPIRV_SPIRV_DISASSEMBLER_H_

#include <memory>
#include <string>

#include "xenia/base/string_buffer.h"
#include "xenia/ui/spirv/spirv_util.h"

namespace xe {
namespace ui {
namespace spirv {

class SpirvDisassembler {
 public:
  class Result {
   public:
    Result(spv_text text, spv_diagnostic diagnostic);
    ~Result();

    // True if the result has an error associated with it.
    bool has_error() const;
    // Index of the error in the provided binary word data.
    size_t error_word_index() const;
    // Human-readable description of the error.
    const char* error_string() const;

    // Disassembled source text.
    // Returned pointer lifetime is tied to this Result instance.
    const char* text() const;
    // Converts the disassembled source text to a string.
    std::string to_string() const;
    // Appends the disassembled source text to the given buffer.
    void AppendText(StringBuffer* target_buffer) const;

   private:
    spv_text text_ = nullptr;
    spv_diagnostic diagnostic_ = nullptr;
  };

  SpirvDisassembler();
  ~SpirvDisassembler();

  // Disassembles the given SPIRV binary.
  // The return will be nullptr if disassembly fails due to a library error.
  // The return may have an error set on it if the SPIRV binary is malformed.
  std::unique_ptr<Result> Disassemble(const uint32_t* words, size_t word_count);

 private:
  spv_context spv_context_ = nullptr;
};

}  // namespace spirv
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SPIRV_SPIRV_DISASSEMBLER_H_
