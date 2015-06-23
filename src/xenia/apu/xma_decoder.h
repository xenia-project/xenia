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

#include "xenia/emulator.h"
#include "xenia/xbox.h"
#include "xenia/apu/xma_context.h"

namespace xe {
namespace kernel {
class XHostThread;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace apu {

struct XMA_CONTEXT_DATA;

class XmaDecoder {
 public:
  XmaDecoder(Emulator* emulator);
  virtual ~XmaDecoder();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup();
  virtual void Shutdown();

  uint32_t context_array_ptr() const { return registers_.context_array_ptr; }

  uint32_t AllocateContext();
  void ReleaseContext(uint32_t guest_ptr);
  bool BlockOnContext(uint32_t guest_ptr, bool poll);

  virtual uint64_t ReadRegister(uint32_t addr);
  virtual void WriteRegister(uint32_t addr, uint64_t value);

 protected:
  int GetContextId(uint32_t guest_ptr);

 private:
  void WorkerThreadMain();

  static uint64_t MMIOReadRegisterThunk(void* ppc_context, XmaDecoder* as,
                                        uint32_t addr) {
    return as->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(void* ppc_context, XmaDecoder* as,
                                     uint32_t addr, uint64_t value) {
    as->WriteRegister(addr, value);
  }

 protected:
  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;

  std::atomic<bool> worker_running_;
  kernel::object_ref<kernel::XHostThread> worker_thread_;
  xe::threading::Fence worker_fence_;

  xe::mutex lock_;

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

  uint32_t context_data_first_ptr_;
  uint32_t context_data_last_ptr_;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_DECODER_H_
