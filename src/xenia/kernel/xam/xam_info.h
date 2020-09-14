#pragma once
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {
dword_result_t XamCreateEnumeratorHandle(unknown_t unk1, unknown_t unk2,
                                         unknown_t unk3, unknown_t unk4,
                                         unknown_t unk5, unknown_t unk6,
                                         unknown_t unk7, unknown_t unk8);

dword_result_t XamGetPrivateEnumStructureFromHandle(unknown_t unk1,
                                                    unknown_t unk2);

}  // namespace xam
}  // namespace kernel
}  // namespace xe