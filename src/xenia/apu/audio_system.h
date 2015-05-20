/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_AUDIO_SYSTEM_H_
#define XENIA_APU_AUDIO_SYSTEM_H_

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

#include "xenia/emulator.h"
#include "xenia/xbox.h"

namespace xe {

namespace kernel { class XHostThread; }

namespace apu {

class AudioDriver;

class AudioSystem {
 public:
  virtual ~AudioSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup();
  virtual void Shutdown();

  uint32_t xma_context_array_ptr() const {
    return registers_.xma_context_array_ptr;
  }
  uint32_t AllocateXmaContext();
  void ReleaseXmaContext(uint32_t guest_ptr);

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg,
                          size_t* out_index);
  void UnregisterClient(size_t index);
  void SubmitFrame(size_t index, uint32_t samples_ptr);

  virtual X_STATUS CreateDriver(size_t index, HANDLE wait_handle,
                                AudioDriver** out_driver) = 0;
  virtual void DestroyDriver(AudioDriver* driver) = 0;

  virtual uint64_t ReadRegister(uint32_t addr);
  virtual void WriteRegister(uint32_t addr, uint64_t value);

 protected:
  virtual void Initialize();

 private:
  void ThreadStart();

  static uint64_t MMIOReadRegisterThunk(AudioSystem* as, uint32_t addr) {
    return as->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(AudioSystem* as, uint32_t addr,
                                     uint64_t value) {
    as->WriteRegister(addr, value);
  }

 protected:
  AudioSystem(Emulator* emulator);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;

  std::unique_ptr<kernel::XHostThread> thread_;
  std::atomic<bool> running_;

  std::mutex lock_;

  // Stored little endian, accessed through 0x7FEA....
  union {
    struct {
      union {
        struct {
          uint8_t ignored0[0x1800];
          // 1800h; points to guest-space physical block of 320 contexts.
          uint32_t xma_context_array_ptr;
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
  std::vector<uint32_t> xma_context_free_list_;

  static const size_t maximum_client_count_ = 8;

  struct {
    AudioDriver* driver;
    uint32_t callback;
    uint32_t callback_arg;
    uint32_t wrapped_callback_arg;
  } clients_[maximum_client_count_];
  // Last handle is always there in case we have no clients.
  HANDLE client_wait_handles_[maximum_client_count_ + 1];
  std::queue<size_t> unused_clients_;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_AUDIO_SYSTEM_H_
