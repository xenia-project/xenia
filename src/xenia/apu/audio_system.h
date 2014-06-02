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

#include <xenia/core.h>
#include <xenia/xbox.h>

#include <queue>

XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, cpu, Processor);
XEDECLARECLASS2(xe, cpu, XenonThreadState);
XEDECLARECLASS2(xe, apu, AudioDriver);

namespace xe {
namespace apu {


class AudioSystem {
public:
  virtual ~AudioSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup();
  virtual void Shutdown();

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg, size_t* out_index);
  void UnregisterClient(size_t index);
  void SubmitFrame(size_t index, uint32_t samples_ptr);

  virtual X_STATUS CreateDriver(size_t index, HANDLE wait_handle, AudioDriver** out_driver) = 0;
  virtual void DestroyDriver(AudioDriver* driver) = 0;

  virtual uint64_t ReadRegister(uint64_t addr);
  virtual void WriteRegister(uint64_t addr, uint64_t value);

protected:
  virtual void Initialize();

private:
  static void ThreadStartThunk(AudioSystem* this_ptr) {
    this_ptr->ThreadStart();
  }
  void ThreadStart();

  static uint64_t MMIOReadRegisterThunk(AudioSystem* as, uint64_t addr) {
    return as->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(AudioSystem* as, uint64_t addr,
                                     uint64_t value) {
    as->WriteRegister(addr, value);
  }

protected:
  AudioSystem(Emulator* emulator);

  Emulator*         emulator_;
  Memory*           memory_;
  cpu::Processor*   processor_;

  xe_thread_ref     thread_;
  cpu::XenonThreadState* thread_state_;
  uint32_t          thread_block_;
  bool              running_;

  xe_mutex_t*       lock_;

  static const size_t maximum_client_count_ = 8;

  struct {
    AudioDriver* driver;
    uint32_t callback;
    uint32_t callback_arg;
    uint32_t wrapped_callback_arg;
  } clients_[maximum_client_count_];
  HANDLE client_wait_handles_[maximum_client_count_];
  std::queue<size_t> unused_clients_;
};


}  // namespace apu
}  // namespace xe


#endif  // XENIA_APU_AUDIO_SYSTEM_H_
