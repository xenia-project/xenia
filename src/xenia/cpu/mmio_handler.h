/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_MMIO_HANDLER_H_
#define XENIA_CPU_MMIO_HANDLER_H_

#include <memory>
#include <mutex>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/base/platform.h"

namespace xe {
class Exception;
class HostThreadContext;
}  // namespace xe

namespace xe {
namespace cpu {

typedef uint32_t (*MMIOReadCallback)(void* ppc_context, void* callback_context,
                                     uint32_t addr);
typedef void (*MMIOWriteCallback)(void* ppc_context, void* callback_context,
                                  uint32_t addr, uint32_t value);
typedef void (*MmioAccessRecordCallback)(void* context,
                                         void* host_insn_address);
struct MMIORange {
  uint32_t address;
  uint32_t mask;
  uint32_t size;
  void* callback_context;
  MMIOReadCallback read;
  MMIOWriteCallback write;
};

// NOTE: only one can exist at a time!
class MMIOHandler {
 public:
  virtual ~MMIOHandler();

  typedef uint32_t (*HostToGuestVirtual)(const void* context,
                                         const void* host_address);
  typedef bool (*AccessViolationCallback)(
      global_unique_lock_type global_lock_locked_once, //not passed by reference with const like the others?
      void* context, void* host_address, bool is_write);

  // access_violation_callback is called with global_critical_region locked once
  // on the thread, so if multiple threads trigger an access violation in the
  // same page, the callback will be called only once.
  static std::unique_ptr<MMIOHandler> Install(
      uint8_t* virtual_membase, uint8_t* physical_membase, uint8_t* membase_end,
      HostToGuestVirtual host_to_guest_virtual,
      const void* host_to_guest_virtual_context,
      AccessViolationCallback access_violation_callback,
      void* access_violation_callback_context,
      MmioAccessRecordCallback record_mmio_callback, void* record_mmio_context);
  static MMIOHandler* global_handler() { return global_handler_; }

  bool RegisterRange(uint32_t virtual_address, uint32_t mask, uint32_t size,
                     void* context, MMIOReadCallback read_callback,
                     MMIOWriteCallback write_callback);
  MMIORange* LookupRange(uint32_t virtual_address);

  bool CheckLoad(uint32_t virtual_address, uint32_t* out_value);
  bool CheckStore(uint32_t virtual_address, uint32_t value);
  void SetMMIOExceptionRecordingCallback(MmioAccessRecordCallback callback,
                                         void* context) {
    record_mmio_context_ = context;
    record_mmio_callback_ = callback;
  }

 protected:
  MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase,
              uint8_t* membase_end, HostToGuestVirtual host_to_guest_virtual,
              const void* host_to_guest_virtual_context,
              AccessViolationCallback access_violation_callback,
              void* access_violation_callback_context,
              MmioAccessRecordCallback record_mmio_callback,
              void* record_mmio_context);

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;
  uint8_t* memory_end_;

  std::vector<MMIORange> mapped_ranges_;

  HostToGuestVirtual host_to_guest_virtual_;
  const void* host_to_guest_virtual_context_;

  AccessViolationCallback access_violation_callback_;
  void* access_violation_callback_context_;
  MmioAccessRecordCallback record_mmio_callback_;

  void* record_mmio_context_;
  static MMIOHandler* global_handler_;

  xe::global_critical_region global_critical_region_;

 private:
  struct DecodedLoadStore {
    // Matches the Xn/Wn register number for 0 reads and ignored writes in many
    // usage cases.
    static constexpr uint8_t kArm64RegZero = 31;

    // Matches the actual register number encoding for an SP base in AArch64
    // load and store instructions.
    static constexpr uint8_t kArm64MemBaseRegSp = kArm64RegZero;

    static constexpr uint8_t kArm64ValueRegX0 = 0;
    static constexpr uint8_t kArm64ValueRegZero =
        kArm64ValueRegX0 + kArm64RegZero;
    static constexpr uint8_t kArm64ValueRegV0 = 32;

    size_t length;
    // Inidicates this is a load (or conversely a store).
    bool is_load;
    // Indicates the memory must be swapped.
    bool byte_swap;
    // Source (for store) or target (for load) register.
    // For x86-64:
    // AX  CX  DX  BX  SP  BP  SI  DI   // REX.R=0
    // R8  R9  R10 R11 R12 R13 R14 R15  // REX.R=1
    // For AArch64:
    // - kArm64ValueRegX0 + [0...30]: Xn (Wn for 32 bits - upper 32 bits of Xn
    //   are zeroed on Wn write).
    // - kArm64ValueRegZero: Zero constant for register read, ignored register
    //   write (though memory must still be accessed - a MMIO load may have side
    //   effects even if the result is discarded).
    // - kArm64ValueRegV0 + [0...31]: Vn (Sn for 32 bits).
    uint8_t value_reg;
    // [base + (index * scale) + displacement]
    bool mem_has_base;
    // On AArch64, if mem_base_reg is kArm64MemBaseRegSp, the base register is
    // SP, not Xn.
    uint8_t mem_base_reg;
    // For AArch64 pre- and post-indexing. In case of a load, the base register
    // is written back after the loaded data is written to the register,
    // overwriting the value register if it's the same.
    bool mem_base_writeback;
    int32_t mem_base_writeback_offset;
    bool mem_has_index;
    uint8_t mem_index_reg;
    uint8_t mem_index_size;
    bool mem_index_sign_extend;
    uint8_t mem_scale;
    ptrdiff_t mem_displacement;
    bool is_constant;
    int32_t constant;
  };

  static bool TryDecodeLoadStore(const uint8_t* p,
                                 DecodedLoadStore& decoded_out);
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MMIO_HANDLER_H_
