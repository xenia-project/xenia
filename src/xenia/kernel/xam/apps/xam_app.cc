/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xam_app.h"

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xenumerator.h"

/* Notes:
   - Messages ids that start with 0x00021xxx are UI calls
   - Messages ids that start with 0x00023xxx are used for the user profile
   - Messages ids that start with 0x0002Bxxx are used by the Kinect device
   usually for camera related functions
   - Messages ids that start with 0x0002Cxxx are used by the XamNuiIdentity
   functions
*/

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

XamApp::XamApp(KernelState* kernel_state) : App(kernel_state, 0xFE) {}

X_HRESULT XamApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x0002000E: {
      struct message_data {
        xe::be<uint32_t> user_index;
        xe::be<uint32_t> unk_04;
        xe::be<uint32_t> extra_ptr;
        xe::be<uint32_t> buffer_ptr;
        xe::be<uint32_t> buffer_size;
        xe::be<uint32_t> unk_14;
        xe::be<uint32_t> length_ptr;
        xe::be<uint32_t> unk_1C;
      }* data = reinterpret_cast<message_data*>(buffer);
      XELOGD(
          "XamAppEnumerateContentAggregate({}, {:08X}, {:08X}, {:08X}, {}, "
          "{:08X}, {:08X}, {:08X})",
          (uint32_t)data->user_index, (uint32_t)data->unk_04,
          (uint32_t)data->extra_ptr, (uint32_t)data->buffer_ptr,
          (uint32_t)data->buffer_size, (uint32_t)data->unk_14,
          (uint32_t)data->length_ptr, (uint32_t)data->unk_1C);
      if (!data->buffer_ptr || !data->extra_ptr) {
        return X_E_INVALIDARG;
      }

      auto extra = memory_->TranslateVirtual<X_KENUMERATOR_CONTENT_AGGREGATE*>(
          data->extra_ptr);
      auto buffer = memory_->TranslateVirtual(data->buffer_ptr);
      auto e = kernel_state_->object_table()->LookupObject<XEnumerator>(
          extra->handle);
      if (!e) {
        return X_E_INVALIDARG;
      }
      assert_true(extra->magic == kXObjSignature);
      if (data->buffer_size) {
        std::memset(buffer, 0, data->buffer_size);
      }
      uint32_t item_count = 0;
      auto result = e->WriteItems(data->buffer_ptr, buffer, &item_count);

      if (result == X_ERROR_SUCCESS && item_count >= 1) {
        if (data->length_ptr) {
          auto length_ptr =
              memory_->TranslateVirtual<be<uint32_t>*>(data->length_ptr);
          *length_ptr = 1;
        }
        return X_E_SUCCESS;
      }
      return X_E_NO_MORE_FILES;
    }
    case 0x00020021: {
      struct XContentQueryVolumeDeviceType {
        char root_name[64];
        xe::be<uint32_t> is_title_process;
        xe::be<DeviceType> device_type_ptr;
        xe::be<uint32_t> overlapped_ptr;
      }* data = reinterpret_cast<XContentQueryVolumeDeviceType*>(buffer);
      assert_true(buffer_length == sizeof(XContentQueryVolumeDeviceType));

      xe::be<DeviceType>* device_type_ptr =
          memory_->TranslateVirtual<xe::be<DeviceType>*>(
              static_cast<uint32_t>(data->device_type_ptr.get()));

      switch (kernel_state_->deployment_type_) {
        case XDeploymentType::kDownload:
        case XDeploymentType::kInstalledToHDD: {
          *device_type_ptr = DeviceType::HDD;
        } break;
        case XDeploymentType::kOpticalDisc: {
          *device_type_ptr = DeviceType::ODD;
        } break;
        default: {
          *device_type_ptr = DeviceType::Invalid;
        } break;
      }

      XELOGD("XContentQueryVolumeDeviceType('{}', {:08X}, {:08X}, {:08X})",
             data->root_name,
             static_cast<uint32_t>(data->is_title_process.get()),
             static_cast<uint32_t>(data->device_type_ptr.get()),
             static_cast<uint32_t>(data->overlapped_ptr.get()));

      return X_E_SUCCESS;
    }
    case 0x00021012: {
      uint32_t enabled = xe::load_and_swap<uint32_t>(buffer);
      XELOGD("XEnableGuestSignin: {}", enabled ? "true" : "false");
      return X_E_SUCCESS;
    }
    case 0x00022005: {
      struct XTITLE_GET_DEPLOYMENT_TYPE {
        xe::be<uint32_t> deployment_type_ptr;
        xe::be<uint32_t> overlapped_ptr;
      }* data = reinterpret_cast<XTITLE_GET_DEPLOYMENT_TYPE*>(buffer);
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XTITLE_GET_DEPLOYMENT_TYPE));
      auto deployment_type =
          memory_->TranslateVirtual<uint32_t*>(data->deployment_type_ptr);
      *deployment_type = static_cast<uint32_t>(kernel_state_->deployment_type_);
      XELOGD("XTitleGetDeploymentType({:08X}, {:08X}",
             data->deployment_type_ptr.get(), data->overlapped_ptr.get());
      return X_E_SUCCESS;
    }
    case 0x0002B003: {
      // Games used in:
      // 4D5309C9
      // It only receives buffer
      struct {
        xe::be<uint64_t> unk1;
        xe::be<uint64_t> unk2;
        xe::be<uint64_t> unk3;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);

      XELOGD("XamUnk2B003({:016X}, {:016X}, {:016X}), unimplemented",
             args->unk1.get(), args->unk2.get(), args->unk3.get());
      return X_E_SUCCESS;
    }
  }
  XELOGE(
      "Unimplemented XAM message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
