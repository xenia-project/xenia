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
#include <queue>

#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

class AudioDriver;
class XmaDecoder;

class AudioSystem {
 public:
  virtual ~AudioSystem();

  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  XmaDecoder* xma_decoder() const { return xma_decoder_.get(); }

  virtual X_STATUS Setup(kernel::KernelState* kernel_state);
  virtual void Shutdown();

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg,
                          size_t* out_index);
  void UnregisterClient(size_t index);
  void SubmitFrame(size_t index, uint32_t samples_ptr);

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

  bool is_paused() const { return paused_; }
  void Pause();
  void Resume();

 protected:
  explicit AudioSystem(cpu::Processor* processor);

  virtual void Initialize();

  void WorkerThreadMain();

  virtual X_STATUS CreateDriver(size_t index,
                                xe::threading::Semaphore* semaphore,
                                AudioDriver** out_driver) = 0;
  virtual void DestroyDriver(AudioDriver* driver) = 0;

  // TODO(gibbed): respect XAUDIO2_MAX_QUEUED_BUFFERS somehow (ie min(64,
  // XAUDIO2_MAX_QUEUED_BUFFERS))
  static const size_t kMaximumQueuedFrames = 32;

  Memory* memory_ = nullptr;
  cpu::Processor* processor_ = nullptr;
  std::unique_ptr<XmaDecoder> xma_decoder_;

  std::atomic<bool> worker_running_ = {false};
  kernel::object_ref<kernel::XHostThread> worker_thread_;

  xe::global_critical_region global_critical_region_;
  static const size_t kMaximumClientCount = 8;
  struct {
    AudioDriver* driver;
    uint32_t callback;
    uint32_t callback_arg;
    uint32_t wrapped_callback_arg;
    bool in_use;
  } clients_[kMaximumClientCount];

  int FindFreeClient();

  std::unique_ptr<xe::threading::Semaphore>
      client_semaphores_[kMaximumClientCount];
  // Event is always there in case we have no clients.
  std::unique_ptr<xe::threading::Event> shutdown_event_;
  xe::threading::WaitHandle* wait_handles_[kMaximumClientCount + 1];

  bool paused_ = false;
  threading::Fence pause_fence_;
  std::unique_ptr<threading::Event> resume_event_;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_AUDIO_SYSTEM_H_
