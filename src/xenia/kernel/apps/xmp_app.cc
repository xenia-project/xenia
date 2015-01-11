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

XXMPApp::XXMPApp(KernelState* kernel_state)
    : XApp(kernel_state, 0xFA),
      status_(Status::kStopped),
      disabled_(0),
      unknown_state1_(0),
      unknown_state2_(0),
      unknown_flags_(0),
      unknown_float_(0.0f) {}

X_RESULT XXMPApp::XMPGetStatus(uint32_t status_ptr) {
  // Some stupid games will hammer this on a thread - induce a delay
  // here to keep from starving real threads.
  Sleep(1);

  XELOGD("XMPGetStatus(%.8X)", status_ptr);
  poly::store_and_swap<uint32_t>(membase_ + status_ptr,
                                 static_cast<uint32_t>(status_));
  OnStatusChanged();
  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::XMPContinue() {
  XELOGD("XMPContinue()");
  if (status_ == Status::kPaused) {
    status_ = Status::kPlaying;
  }
  OnStatusChanged();
  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::XMPStop(uint32_t unk) {
  assert_zero(unk);
  XELOGD("XMPStop(%.8X)", unk);
  status_ = Status::kStopped;
  OnStatusChanged();
  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::XMPPause() {
  XELOGD("XMPPause()");
  if (status_ == Status::kPlaying) {
    status_ = Status::kPaused;
  }
  OnStatusChanged();
  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::XMPNext() {
  XELOGD("XMPNext()");
  status_ = Status::kPlaying;
  OnStatusChanged();
  return X_ERROR_SUCCESS;
}

X_RESULT XXMPApp::XMPPrevious() {
  XELOGD("XMPPrevious()");
  status_ = Status::kPlaying;
  OnStatusChanged();
  return X_ERROR_SUCCESS;
}

void XXMPApp::OnStatusChanged() {
  kernel_state_->BroadcastNotification(kMsgStatusChanged,
                                       static_cast<uint32_t>(status_));
}

X_RESULT XXMPApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  // http://freestyledash.googlecode.com/svn-history/r1/trunk/Freestyle/Scenes/Media/Music/ScnMusic.cpp
  switch (message) {
    case 0x00070003: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPContinue();
    }
    case 0x00070004: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t unk = poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 4);
      assert_true(xmp_client == 0x00000002);
      return XMPStop(unk);
    }
    case 0x00070005: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPPause();
    }
    case 0x00070006: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPNext();
    }
    case 0x00070007: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPPrevious();
    }
    case 0x00070008: {
      assert_true(!buffer_length || buffer_length == 16);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t unk1 = poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 4);
      uint32_t unk2 = poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 8);
      uint32_t flags =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 12);
      assert_true(xmp_client == 0x00000002);
      XELOGD("XMPSetState?(%.8X, %.8X, %.8X)", unk1, unk2, flags);
      unknown_state1_ = unk1;
      unknown_state2_ = unk2;
      unknown_flags_ = flags;
      kernel_state_->BroadcastNotification(kMsgStateChanged, 0);
      return X_ERROR_SUCCESS;
    }
    case 0x00070009: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t status_ptr = poly::load_and_swap<uint32_t>(
          membase_ + buffer_ptr + 4);  // out ptr to 4b - expect 0
      assert_true(xmp_client == 0x00000002);
      return XMPGetStatus(status_ptr);
    }
    case 0x0007000B: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t float_ptr = poly::load_and_swap<uint32_t>(
          membase_ + buffer_ptr + 4);  // out ptr to 4b - floating point
      assert_true(xmp_client == 0x00000002);
      XELOGD("XMPGetFloat?(%.8X)", float_ptr);
      poly::store_and_swap<float>(membase_ + float_ptr, unknown_float_);
      return X_ERROR_SUCCESS;
    }
    case 0x0007000C: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      float float_value = poly::load_and_swap<float>(membase_ + buffer_ptr + 4);
      assert_true(xmp_client == 0x00000002);
      XELOGD("XMPSetFloat?(%g)", float_value);
      unknown_float_ = float_value;
      return X_ERROR_SUCCESS;
    }
    case 0x0007001A: {
      assert_true(!buffer_length || buffer_length == 12);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t unk1 = poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 4);
      uint32_t disabled =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 8);
      assert_true(xmp_client == 0x00000002);
      assert_zero(unk1);
      XELOGD("XMPSetDisabled(%.8X, %.8X)", unk1, disabled);
      disabled_ = disabled;
      kernel_state_->BroadcastNotification(kMsgDisableChanged, disabled_);
      return X_ERROR_SUCCESS;
    }
    case 0x0007001B: {
      assert_true(!buffer_length || buffer_length == 12);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t unk_ptr = poly::load_and_swap<uint32_t>(
          membase_ + buffer_ptr + 4);  // out ptr to 4b - expect 0
      uint32_t disabled_ptr = poly::load_and_swap<uint32_t>(
          membase_ + buffer_ptr + 8);  // out ptr to 4b - expect 1 (to skip)
      assert_true(xmp_client == 0x00000002);
      XELOGD("XMPGetDisabled(%.8X, %.8X)", unk_ptr, disabled_ptr);
      poly::store_and_swap<uint32_t>(membase_ + unk_ptr, 0);
      poly::store_and_swap<uint32_t>(membase_ + disabled_ptr, disabled_);
      return X_ERROR_SUCCESS;
    }
    case 0x00070029: {
      assert_true(!buffer_length || buffer_length == 16);
      uint32_t xmp_client =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 0);
      uint32_t unk1_ptr =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 4);
      uint32_t unk2_ptr =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 8);
      uint32_t unk3_ptr =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 12);
      assert_true(xmp_client == 0x00000002);
      XELOGD("XMPGetState?(%.8X, %.8X, %.8X)", unk1_ptr, unk2_ptr, unk3_ptr);
      poly::store_and_swap<uint32_t>(membase_ + unk1_ptr, unknown_state1_);
      poly::store_and_swap<uint32_t>(membase_ + unk2_ptr, unknown_state2_);
      poly::store_and_swap<uint32_t>(membase_ + unk3_ptr, unknown_flags_);
      return X_ERROR_SUCCESS;
    }
    case 0x0007002E: {
      assert_true(!buffer_length || buffer_length == 12);
      // 00000002 00000003 20049ce0
      uint32_t xmp_client = poly::load_and_swap<uint32_t>(
          membase_ + buffer_ptr + 0);  // 0x00000002
      uint32_t unk1 = poly::load_and_swap<uint32_t>(membase_ + buffer_ptr +
                                                    4);  // 0x00000003
      uint32_t unk_ptr =
          poly::load_and_swap<uint32_t>(membase_ + buffer_ptr + 8);
      assert_true(xmp_client == 0x00000002);
      //
      break;
    }
  }
  XELOGE("Unimplemented XMsg message app=%.8X, msg=%.8X, arg1=%.8X, arg2=%.8X",
         app_id(), message, buffer_ptr, buffer_length);
  return X_ERROR_NOT_FOUND;
}

}  // namespace apps
}  // namespace kernel
}  // namespace xe
