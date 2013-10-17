/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/objects/xfile.h>

#include <xenia/kernel/modules/xboxkrnl/async_request.h>
#include <xenia/kernel/modules/xboxkrnl/fs/entry.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xevent.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


XFile::XFile(KernelState* kernel_state, FileEntry* entry) :
    entry_(entry),
    position_(0),
    XObject(kernel_state, kTypeFile) {
  async_event_ = new XEvent(kernel_state);
  async_event_->Initialize(false, false);
}

XFile::~XFile() {
  // TODO(benvanik): signal that the file is closing?
  async_event_->Set(0, false);
  async_event_->Delete();
}

X_STATUS XFile::Wait(uint32_t wait_reason, uint32_t processor_mode,
                     uint32_t alertable, uint64_t* opt_timeout) {
  // Wait until some async operation completes.
  return async_event_->Wait(
      wait_reason, processor_mode, alertable, opt_timeout);
}

X_STATUS XFile::Read(void* buffer, size_t buffer_length, size_t byte_offset,
                     size_t* out_bytes_read) {
  if (byte_offset == -1) {
    // Read from current position.
  }
  X_STATUS result = entry_->Read(buffer, buffer_length, byte_offset, out_bytes_read);
  if (XSUCCEEDED(result)) {
    position_ += *out_bytes_read;
  }
  return result;
}

X_STATUS XFile::Read(void* buffer, size_t buffer_length, size_t byte_offset,
                     XAsyncRequest* request) {
  // Also tack on our event so that any waiters wake.
  request->AddWaitEvent(async_event_);
  position_ = byte_offset;
  return entry_->Read(buffer, buffer_length, byte_offset, request);
}
