/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_DECODER_H_
#define XENIA_APU_XMA_DECODER_H_

#include <atomic>
#include <mutex>
#include <queue>

#include "xenia/apu/xma_context.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

struct XMA_CONTEXT_DATA;

class XmaDecoder {
 public:
  explicit XmaDecoder(cpu::Processor* processor);
  ~XmaDecoder();

  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  X_STATUS Setup(kernel::KernelState* kernel_state);
  void Shutdown();

  uint32_t context_array_ptr() const { return registers_.context_array_ptr; }

  uint32_t AllocateContext();
  void ReleaseContext(uint32_t guest_ptr);
  bool BlockOnContext(uint32_t guest_ptr, bool poll);

  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);

 protected:
  int GetContextId(uint32_t guest_ptr);

 private:
  void WorkerThreadMain();

  static uint32_t MMIOReadRegisterThunk(void* ppc_context, XmaDecoder* as,
                                        uint32_t addr) {
    return as->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(void* ppc_context, XmaDecoder* as,
                                     uint32_t addr, uint32_t value) {
    as->WriteRegister(addr, value);
  }

 protected:
  Memory* memory_ = nullptr;
  cpu::Processor* processor_ = nullptr;

  std::atomic<bool> worker_running_ = {false};
  kernel::object_ref<kernel::XHostThread> worker_thread_;
  xe::threading::Fence worker_fence_;

  std::mutex lock_;

  // Stored little endian, accessed through 0x7FEA....
  union {
    struct {
      union {
        struct {
          uint8_t ignored0[0x1800];
          // 1800h; points to guest-space physical block of 320 contexts.
          uint32_t context_array_ptr;
        };
        struct {
          uint8_t ignored1[0x1818];
          // 1818h; current context ID.
          uint32_t current_context;
          // 181Ch; next context ID to process.
          uint32_t next_context;
        };
      };
    } registers_;
    uint32_t register_file_[0xFFFF / 4];
  };

  static const uint32_t kContextCount = 320;
  XmaContext contexts_[kContextCount];

  uint32_t context_data_first_ptr_ = 0;
  uint32_t context_data_last_ptr_ = 0;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_DECODER_H_
