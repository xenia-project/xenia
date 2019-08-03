/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_module.h"

#include <vector>

#include "xenia/base/clock.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"

DECLARE_bool(kernel_pix);

namespace xe {
namespace kernel {
namespace xboxkrnl {

enum class DebugMonitorCommand {
  PIXCommandResult = 27,
  SetPIXCallback = 28,
  Unknown66 = 66,
  Unknown89 = 89,
  Unknown94 = 94,
};

void KeDebugMonitorCallback(cpu::ppc::PPCContext* ppc_context,
                            kernel::KernelState* kernel_state) {
  auto id = static_cast<DebugMonitorCommand>(ppc_context->r[3] & 0xFFFFFFFFu);
  auto arg = static_cast<uint32_t>(ppc_context->r[4] & 0xFFFFFFFFu);

  XELOGI("KeDebugMonitorCallback(%u, %08x)", static_cast<uint32_t>(id), arg);

  if (!cvars::kernel_pix) {
    SHIM_SET_RETURN_32(-1);
    return;
  }

  auto xboxkrnl = kernel_state->GetKernelModule<XboxkrnlModule>("xboxkrnl.exe");

  switch (id) {
    case DebugMonitorCommand::PIXCommandResult: {
      auto s = kernel_state->memory()->TranslateVirtual<const char*>(arg);
      debugging::DebugPrint("%s\n", s);
      XELOGD("PIX command result: %s\n", s);
      if (strcmp(s, "PIX!{CaptureFileCreationEnded} 0x00000000") == 0) {
        xboxkrnl->SendPIXCommand("{BeginCapture}");
      }
      SHIM_SET_RETURN_32(0);
      break;
    }
    case DebugMonitorCommand::SetPIXCallback:
      xboxkrnl->set_pix_function(arg);
      xboxkrnl->SendPIXCommand("{LimitCaptureSize} 100");  // in MB
      xboxkrnl->SendPIXCommand("{BeginCaptureFileCreation} scratch:\\test.cap");
      SHIM_SET_RETURN_32(0);
      break;
    case DebugMonitorCommand::Unknown66: {
      struct callback_info {
        xe::be<uint32_t> callback_fn;
        xe::be<uint32_t> callback_arg;  // D3D device object?
      };
      auto cbi = kernel_state->memory()->TranslateVirtual<callback_info*>(arg);
      SHIM_SET_RETURN_32(0);
      break;
    }
    case DebugMonitorCommand::Unknown89:
      // arg = function pointer?
      SHIM_SET_RETURN_32(0);
      break;
    case DebugMonitorCommand::Unknown94:
      // xboxkrnl->SendPIXCommand("{EndCapture}");
      SHIM_SET_RETURN_32(0);
      break;
    default:
      SHIM_SET_RETURN_32(-1);
      break;
  }
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
