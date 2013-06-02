/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_COMMAND_BUFFER_H_
#define XENIA_GPU_COMMAND_BUFFER_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {


// TODO(benvanik): command packet types.


class CommandBuffer {
public:
  CommandBuffer(xe_memory_ref memory) {
    memory_ = xe_memory_retain(memory);
  }

  virtual ~CommandBuffer() {
    xe_memory_release(memory_);
  }

  xe_memory_ref memory() {
    return memory_;
  }

  // TODO(benvanik): command methods.
  virtual void Foo() = 0;

protected:
  xe_memory_ref   memory_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_COMMAND_BUFFER_H_
