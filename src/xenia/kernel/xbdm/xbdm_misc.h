/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBDM_XBDM_MSIC_H_
#define XENIA_KERNEL_XBDM_XBDM_MSIC_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam.h"

namespace xe {
namespace kernel {
namespace xbdm {

// chrispy: no idea what a real valid value is for this
static constexpr char DmXboxName[] = "Xbox360Name";

// clang-format off

#define XBDM_SUCCESSFUL         0x02DA0000
#define XBDM_UNSUCCESSFUL       0x82DA0000
#define XBDM_FASTCAPENABLED     0x82DA0007
#define XBDM_ALREADYEXISTS      0x82DA0010
#define XBDM_ENDOFLIST          0x82DA0104
#define XBDM_BUFFER_TOO_SMALL   0x82DA0105

#define MAX_NOTIFY              32
#define DM_NOTIFYMAX            18
#define MAX_ENH                 16
#define MAX_PATH                260
#define MAX_FRAMES_TO_CAPTURE   256

// clang-format on

enum CONSOLE_MEMORY_CONFIG_STATE : uint32_t {
  DM_CONSOLEMEMCONFIG_NOADDITIONALMEM,
  DM_CONSOLEMEMCONFIG_ADDITIONALMEMDISABLED,
  DM_CONSOLEMEMCONFIG_ADDITIONALMEMENABLED
};

struct XBDM_VERSION_INFO {
  xe::be<uint16_t> major;
  xe::be<uint16_t> minor;
  xe::be<uint16_t> build;
  xe::be<uint16_t> qfe;
};
static_assert_size(XBDM_VERSION_INFO, 8);

struct XBDM_SYSTEM_INFO {
  xe::be<uint32_t> size;
  XBDM_VERSION_INFO base_kernel_version;
  XBDM_VERSION_INFO kernel_version;
  XBDM_VERSION_INFO xdk_version;
  xe::be<uint32_t> flags;
};
static_assert_size(XBDM_SYSTEM_INFO, 32);

struct DM_XBE {
  char launch_path[MAX_PATH + 1];
  xe::be<uint32_t> timestamp;
  xe::be<uint32_t> checksum;
  xe::be<uint32_t> stack_size;
};
static_assert_size(DM_XBE, 276);

struct DM_THREAD_INFO_EX {
  xe::be<uint32_t> size;
  xe::be<uint32_t> suspend_count;
  xe::be<uint32_t> priority;
  xe::be<uint32_t> tls_base_ptr;
  xe::be<uint32_t> start_ptr;
  xe::be<uint32_t> stack_base_ptr;
  xe::be<uint32_t> stack_limit_ptr;
  X_FILETIME create_time;
  xe::be<uint32_t> stack_slack_space;
  xe::be<uint32_t> thread_name_ptr;
  xe::be<uint32_t> thread_name_length;
  uint8_t current_processor;
};
static_assert_size(DM_THREAD_INFO_EX, 52);

struct DMN_MODULE_LOAD {
  char name[MAX_PATH];
  xe::be<uint32_t> base_address_ptr;
  xe::be<uint32_t> size;
  xe::be<uint32_t> TimeStamp;
  xe::be<uint32_t> checksum;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> data_address_ptr;
  xe::be<uint32_t> data_size;
  xe::be<uint32_t> thread_id;
};
static_assert_size(DMN_MODULE_LOAD, 292);

struct DM_PDB_SIGNATURE {
  xam::X_GUID guid;
  uint32_t age;
  char path[MAX_PATH];
};
static_assert_size(DM_PDB_SIGNATURE, 280);

uint32_t DmRegisterCommandProcessorEx(char* cmd_handler_name_ptr,
                                      xe::be<uint32_t>* handler_fn,
                                      uint32_t thread);

#define MAKE_DUMMY_STUB_PTR(x)             \
  dword_result_t x##_entry() { return 0; } \
  DECLARE_XBDM_EXPORT1(x, kDebug, kStub)

#define MAKE_DUMMY_STUB_STATUS(x)                                   \
  dword_result_t x##_entry() { return X_STATUS_INVALID_PARAMETER; } \
  DECLARE_XBDM_EXPORT1(x, kDebug, kStub)

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBDM_XBDM_MSIC_H_
