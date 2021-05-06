/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/vfs/file.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

void AddODDContentTest(object_ref<XStaticEnumerator> e, uint32_t content_type) {
  auto root_entry = kernel_state()->file_system()->ResolvePath(
      "game:\\Content\\0000000000000000");
  if (!root_entry) {
    return;
  }

  auto content_type_path = fmt::format("{:08X}", content_type);

  xe::filesystem::WildcardEngine title_find_engine;
  title_find_engine.SetRule("????????");

  xe::filesystem::WildcardEngine content_find_engine;
  content_find_engine.SetRule("????????????????");

  size_t title_find_index = 0;
  vfs::Entry* title_entry;
  for (;;) {
    title_entry =
        root_entry->IterateChildren(title_find_engine, &title_find_index);
    if (!title_entry) {
      break;
    }

    auto title_id =
        string_util::from_string<uint32_t>(title_entry->name(), true);

    auto content_root_entry = title_entry->ResolvePath(content_type_path);
    if (content_root_entry) {
      size_t content_find_index = 0;
      vfs::Entry* content_entry;
      for (;;) {
        content_entry = content_root_entry->IterateChildren(
            content_find_engine, &content_find_index);
        if (!content_entry) {
          break;
        }

        auto item = reinterpret_cast<XCONTENT_AGGREGATE_DATA*>(e->AppendItem());
        assert_not_null(item);
        ContentAggregateData content_aggregate_data = {};
        content_aggregate_data.device_id =
            static_cast<uint32_t>(DummyDeviceId::ODD);
        content_aggregate_data.content_type = content_type;
        content_aggregate_data.display_name = to_utf16(content_entry->name());
        content_aggregate_data.file_name = content_entry->name();
        content_aggregate_data.title_id = title_id;
        content_aggregate_data.Write(item);
      }
    }
  }
}

dword_result_t XamContentAggregateCreateEnumerator(qword_t xuid,
                                                   dword_t device_id,
                                                   dword_t content_type,
                                                   unknown_t unk3,
                                                   lpdword_t handle_out) {
  assert_not_null(handle_out);

  auto device_info = device_id == 0 ? nullptr : GetDummyDeviceInfo(device_id);
  if ((device_id && device_info == nullptr) || !handle_out) {
    return X_E_INVALIDARG;
  }

  auto e = object_ref<XStaticEnumerator>(new XStaticEnumerator(
      kernel_state(), 1, sizeof(XCONTENT_AGGREGATE_DATA)));
  X_KENUMERATOR_CONTENT_AGGREGATE* extra;
  auto result = e->Initialize(0xFF, 0xFE, 0x2000E, 0x20010, 0, &extra);
  if (XFAILED(result)) {
    return result;
  }

  extra->magic = kXObjSignature;
  extra->handle = e->handle();

  if (!device_info || device_info->device_type == DeviceType::HDD) {
    // Get all content data.
    auto content_datas = kernel_state()->content_manager()->ListContent(
        static_cast<uint32_t>(DummyDeviceId::HDD), content_type);
    for (const auto& content_data : content_datas) {
      auto item = reinterpret_cast<XCONTENT_AGGREGATE_DATA*>(e->AppendItem());
      assert_not_null(item);
      ContentAggregateData content_aggregate_data = {};
      content_aggregate_data.device_id = content_data.device_id;
      content_aggregate_data.content_type = content_data.content_type;
      content_aggregate_data.display_name = content_data.display_name;
      content_aggregate_data.file_name = content_data.file_name;
      content_aggregate_data.title_id = kernel_state()->title_id();
      content_aggregate_data.Write(item);
    }
  }

  if (!device_info || device_info->device_type == DeviceType::ODD) {
    AddODDContentTest(e, content_type);
  }

  XELOGD("XamContentAggregateCreateEnumerator: added {} items to enumerator",
         e->item_count());

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentAggregateCreateEnumerator, kContent, kStub);

void RegisterContentAggregateExports(xe::cpu::ExportResolver* export_resolver,
                                     KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
