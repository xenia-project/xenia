#ifndef XENIA_CPU_BACKEND_A64_MAC_UNWIND_INFO_H_
#define XENIA_CPU_BACKEND_A64_MAC_UNWIND_INFO_H_

#include <cstdint>

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {
namespace unwind_info {

// See "OS X ABI Function Call Guide" > "Stack Unwinding"
// https://developer.apple.com/library/mac/documentation/DeveloperTools/Conceptual/LowLevelABI/1.0/LowLevelABI.pdf

// Compact Unwind Format
// A 32-bit value, stored in the __unwind_info section of the binary.
// It describes how to unwind a function, including information about the
// function's prolog, what registers are saved, and the size of the stack
// frame.

// Common-sub-expressions for building the compact unwind encoding.
namespace compact_unwind_encoding {

// Mode (bits 30-31)
constexpr uint32_t kModeMask = 0xC0000000;
constexpr uint32_t kModePreindexed = 0x40000000;  // Pre-indexed addressing

// Stack size (bits 16-23)
constexpr uint32_t kStackSizeMask = 0x00FF0000;
constexpr uint32_t kStackSizeShift = 16;

// Registers saved (bits 0-15)
constexpr uint32_t kSavedRegistersMask = 0x0000FFFF;

}  // namespace compact_unwind_encoding

// Represents the compact unwind information for a single function.
union CompactUnwindInfo {
  uint32_t encoding;
  struct {
    // Whether the function saves the frame pointer (FP) and link register (LR).
    bool saves_fp_lr : 1;
    // The size of the function's prolog in bytes.
    uint32_t preamble_size_bytes : 7;
    // The size of the function's stack frame in bytes.
    uint32_t stack_size_bytes : 24;
  } prolog;
};

static_assert(sizeof(CompactUnwindInfo) == 4, "CompactUnwindInfo size must be 4 bytes");

}  // namespace unwind_info
}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_MAC_UNWIND_INFO_H_
