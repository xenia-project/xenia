/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xgi_app.h"

#include "xenia/base/logging.h"
#include "xenia/base/threading.h"

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

X_RESULT XgiApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                     uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x000B0006: {
      assert_true(!buffer_length || buffer_length == 24);
      // dword r3 user index
      // dword (unwritten?)
      // qword 0
      // dword r4 context enum
      // dword r5 value
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t context_id = xe::load_and_swap<uint32_t>(buffer + 16);
      uint32_t context_value = xe::load_and_swap<uint32_t>(buffer + 20);
      XELOGD("XGIUserSetContextEx(%.8X, %.8X, %.8X)", user_index, context_id,
             context_value);
      return X_ERROR_SUCCESS;
    }
    case 0x000B0007: {
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t property_id = xe::load_and_swap<uint32_t>(buffer + 16);
      uint32_t value_size = xe::load_and_swap<uint32_t>(buffer + 20);
      uint32_t value_ptr = xe::load_and_swap<uint32_t>(buffer + 24);
      XELOGD("XGIUserSetPropertyEx(%.8X, %.8X, %d, %.8X)", user_index,
             property_id, value_size, value_ptr);
      return X_ERROR_SUCCESS;
    }
    case 0x000B0008: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t achievement_count = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t achievements_ptr = xe::load_and_swap<uint32_t>(buffer + 4);
      XELOGD("XGIUserWriteAchievements(%.8X, %.8X)", achievement_count,
             achievements_ptr);
      return X_ERROR_SUCCESS;
    }
    case 0x000B0010: {
      assert_true(!buffer_length || buffer_length == 28);
      // Sequence:
      // - XamSessionCreateHandle
      // - XamSessionRefObjByHandle
      // - [this]
      // - CloseHandle
      uint32_t session_ptr = xe::load_and_swap<uint32_t>(buffer + 0x0);
      uint32_t flags = xe::load_and_swap<uint32_t>(buffer + 0x4);
      uint32_t num_slots_public = xe::load_and_swap<uint32_t>(buffer + 0x8);
      uint32_t num_slots_private = xe::load_and_swap<uint32_t>(buffer + 0xC);
      uint32_t user_xuid = xe::load_and_swap<uint32_t>(buffer + 0x10);
      uint32_t session_info_ptr = xe::load_and_swap<uint32_t>(buffer + 0x14);
      uint32_t nonce_ptr = xe::load_and_swap<uint32_t>(buffer + 0x18);

      XELOGD("XGISessionCreateImpl(%.8X, %.8X, %d, %d, %.8X, %.8X, %.8X)",
             session_ptr, flags, num_slots_public, num_slots_private, user_xuid,
             session_info_ptr, nonce_ptr);
      return X_STATUS_SUCCESS;
    }
    case 0x000B0011: {
      // TODO(DrChat): Figure out what this is again
    } break;
    case 0x000B0012: {
      assert_true(buffer_length == 0x14);
      uint32_t session_ptr = xe::load_and_swap<uint32_t>(buffer + 0x0);
      uint32_t user_count = xe::load_and_swap<uint32_t>(buffer + 0x4);
      uint32_t unk_0 = xe::load_and_swap<uint32_t>(buffer + 0x8);
      uint32_t user_index_array = xe::load_and_swap<uint32_t>(buffer + 0xC);
      uint32_t private_slots_array = xe::load_and_swap<uint32_t>(buffer + 0x10);

      assert_zero(unk_0);
      XELOGD("XGISessionJoinLocal(%.8X, %d, %d, %.8X, %.8X)", session_ptr,
             user_count, unk_0, user_index_array, private_slots_array);
      return X_STATUS_SUCCESS;
    }
    case 0x000B0041: {
      assert_true(!buffer_length || buffer_length == 32);
      // 00000000 2789fecc 00000000 00000000 200491e0 00000000 200491f0 20049340
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t context_ptr = xe::load_and_swap<uint32_t>(buffer + 16);
      auto context =
          context_ptr ? memory_->TranslateVirtual(context_ptr) : nullptr;
      uint32_t context_id =
          context ? xe::load_and_swap<uint32_t>(context + 0) : 0;
      XELOGD("XGIUserGetContext(%.8X, %.8X(%.8X))", user_index, context_ptr,
             context_id);
      uint32_t value = 0;
      if (context) {
        xe::store_and_swap<uint32_t>(context + 4, value);
      }
      return X_ERROR_FUNCTION_FAILED;
    }
    case 0x000B0071: {
      XELOGD("XGI 0x000B0071, unimplemented");
      return X_ERROR_SUCCESS;
    }
  }
  XELOGE("Unimplemented XGI message app=%.8X, msg=%.8X, arg1=%.8X, arg2=%.8X",
         app_id(), message, buffer_ptr, buffer_length);
  return X_STATUS_UNSUCCESSFUL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
