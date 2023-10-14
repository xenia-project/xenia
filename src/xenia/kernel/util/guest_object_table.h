/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Xenia Canary. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_GUEST_OBJECT_TABLE_H_
#define XENIA_KERNEL_UTIL_GUEST_OBJECT_TABLE_H_

#include "xenia/kernel/kernel_state.h"
#include "xenia/xbox.h"
namespace xe {
namespace kernel {
namespace util {
// use this to make it clearer in the code whether a uint32_t is a handle or not
using guest_handle_t = uint32_t;
// not normally api visible, but used so we can accurately recreate how the 360
// os allocated handles
struct X_HANDLE_TABLE {
  xe::be<uint32_t> num_handles;
  xe::be<guest_handle_t> free_offset;
  xe::be<uint32_t> highest_allocated_offset;
  xe::be<uint32_t> table_dynamic_buckets;
  xe::be<uint32_t> table_static_buckets[8];
  X_KSPINLOCK table_lock;
  //used as unknown arg 3 to pool allocations
  uint8_t unk_pool_arg_34;
  uint8_t handle_high_byte;
  uint8_t unk_36;
  uint8_t unk_38;
};

static_assert_size(X_HANDLE_TABLE, 0x38);

bool GrowHandleTable(uint32_t table_ptr, cpu::ppc::PPCContext* context);
uint32_t NewObjectHandle(uint32_t table_guest, uint32_t object_guest,
                         cpu::ppc::PPCContext* context);
uint32_t DestroyObjectHandle(uint32_t table_guest, guest_handle_t handle,
                             cpu::ppc::PPCContext* context);
uint32_t LookupHandleUnlocked(X_HANDLE_TABLE* table, guest_handle_t handle,
                              bool reference_object,
                              cpu::ppc::PPCContext* context);
uint32_t LookupHandle(uint32_t table, guest_handle_t handle,
                      uint32_t reference_object, cpu::ppc::PPCContext* context);
}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_GUEST_OBJECT_TABLE_H_
