/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xsemaphore.h"

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"

namespace xe {
namespace kernel {

XSemaphore::XSemaphore(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

XSemaphore::~XSemaphore() = default;

bool XSemaphore::Initialize(int32_t initial_count, int32_t maximum_count) {
  assert_false(semaphore_);

  CreateNative(sizeof(X_KSEMAPHORE));

  maximum_count_ = maximum_count;
  semaphore_ = xe::threading::Semaphore::Create(initial_count, maximum_count);
  return !!semaphore_;
}

bool XSemaphore::InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header) {
  assert_false(semaphore_);

  auto semaphore = reinterpret_cast<X_KSEMAPHORE*>(native_ptr);
  maximum_count_ = semaphore->limit;
  semaphore_ = xe::threading::Semaphore::Create(semaphore->header.signal_state,
                                                semaphore->limit);
  return !!semaphore_;
}

int32_t XSemaphore::ReleaseSemaphore(int32_t release_count) {
  int32_t previous_count = 0;
  semaphore_->Release(release_count, &previous_count);
  return previous_count;
}

bool XSemaphore::Save(ByteStream* stream) {
  if (!SaveObject(stream)) {
    return false;
  }

  // Get the free number of slots from the semaphore.
  uint32_t free_count = 0;
  while (
      threading::Wait(semaphore_.get(), false, std::chrono::milliseconds(0)) ==
      threading::WaitResult::kSuccess) {
    free_count++;
  }

  XELOGD("XSemaphore {:08X} (count {}/{})", handle(), free_count,
         maximum_count_);

  // Restore the semaphore back to its previous count.
  semaphore_->Release(free_count, nullptr);

  stream->Write(maximum_count_);
  stream->Write(free_count);

  return true;
}

object_ref<XSemaphore> XSemaphore::Restore(KernelState* kernel_state,
                                           ByteStream* stream) {
  auto sem = new XSemaphore(nullptr);
  sem->kernel_state_ = kernel_state;

  if (!sem->RestoreObject(stream)) {
    return nullptr;
  }

  sem->maximum_count_ = stream->Read<uint32_t>();
  auto free_count = stream->Read<uint32_t>();
  XELOGD("XSemaphore {:08X} (count {}/{})", sem->handle(), free_count,
         sem->maximum_count_);

  sem->semaphore_ =
      threading::Semaphore::Create(free_count, sem->maximum_count_);
  assert_not_null(sem->semaphore_);

  return object_ref<XSemaphore>(sem);
}

}  // namespace kernel
}  // namespace xe
