/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/apps/xmp_app.h>


namespace xe {
namespace kernel {
namespace apps {


X_RESULT XXMPApp::XMPGetStatus(uint32_t unk, uint32_t status_ptr) {
  // Some stupid games will hammer this on a thread - induce a delay
  // here to keep from starving real threads.
  Sleep(1);

  XELOGD("XMPGetStatus(%.8X, %.8X)", unk, status_ptr);

  assert_true(unk == 2);
  poly::store_and_swap<uint32_t>(membase_ + status_ptr, 0);

  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::XMPGetStatusEx(uint32_t unk, uint32_t unk_ptr, uint32_t disabled_ptr) {
  // Some stupid games will hammer this on a thread - induce a delay
  // here to keep from starving real threads.
  Sleep(1);

  XELOGD("XMPGetStatusEx(%.8X, %.8X, %.8X)", unk, unk_ptr, disabled_ptr);

  assert_true(unk == 2);
  poly::store_and_swap<uint32_t>(membase_ + unk_ptr, 0);
  poly::store_and_swap<uint32_t>(membase_ + disabled_ptr, 1);

  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::DispatchMessageSync(uint32_t message, uint32_t arg1, uint32_t arg2) {
  // http://freestyledash.googlecode.com/svn-history/r1/trunk/Freestyle/Scenes/Media/Music/ScnMusic.cpp
  switch (message) {
    case 0x00070009: {
      uint32_t unk =
          poly::load_and_swap<uint32_t>(membase_ + arg1 + 0);  // 0x00000002
      uint32_t status_ptr = poly::load_and_swap<uint32_t>(
          membase_ + arg1 + 4);  // out ptr to 4b - expect 0
      assert_zero(arg2);
      return XMPGetStatus(unk, status_ptr);
    }
    case 0x0007001A: {
      // dcz
      // arg1 = ?
      // arg2 = 0
      break;
    }
    case 0x0007001B: {
      uint32_t unk =
          poly::load_and_swap<uint32_t>(membase_ + arg1 + 0);  // 0x00000002
      uint32_t unk_ptr = poly::load_and_swap<uint32_t>(
          membase_ + arg1 + 4);  // out ptr to 4b - expect 0
      uint32_t disabled_ptr = poly::load_and_swap<uint32_t>(
          membase_ + arg1 + 8);  // out ptr to 4b - expect 1 (to skip)
      assert_zero(arg2);
      return XMPGetStatusEx(unk, unk_ptr, disabled_ptr);
    }
  }
  XELOGE("Unimplemented XMsg message app=%.8X, msg=%.8X, arg1=%.8X, arg2=%.8X",
         app_id(), message, arg1, arg2);
  return X_ERROR_NOT_FOUND;
}

X_RESULT XXMPApp::DispatchMessageAsync(uint32_t message, uint32_t buffer_ptr, size_t buffer_length) {
  switch (message) {
  case 0x00070009: {
    uint32_t unk =
        poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);  // 0x00000002
    uint32_t status_ptr = poly::load_and_swap<uint32_t>(
        membase_ + buffer_ptr + 4);  // out ptr to 4b - expect 0
    assert_true(buffer_length == 8);
    return XMPGetStatus(unk, status_ptr);
  }
  case 0x0007001B: {
    uint32_t unk =
        poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);  // 0x00000002
    uint32_t unk_ptr = poly::load_and_swap<uint32_t>(
        membase_ + buffer_ptr + 4);  // out ptr to 4b - expect 0
    uint32_t disabled_ptr = poly::load_and_swap<uint32_t>(
        membase_ + buffer_ptr + 8);  // out ptr to 4b - expect 1 (to skip)
    assert_true(buffer_length == 0xC);
    return XMPGetStatusEx(unk, unk_ptr, disabled_ptr);
  }
  }
  XELOGE("Unimplemented XMsg message app=%.8X, msg=%.8X, buffer=%.8X, len=%d",
         app_id(), message, buffer_ptr, buffer_length);
  return X_ERROR_NOT_FOUND;
}


}  // namespace apps
}  // namespace kernel
}  // namespace xe
