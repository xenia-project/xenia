/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/registers.h"

namespace xe {
namespace gpu {
namespace reg {

constexpr Register RB_COLOR_INFO::rt_register_indices[4] = {
    XE_GPU_REG_RB_COLOR_INFO,
    XE_GPU_REG_RB_COLOR1_INFO,
    XE_GPU_REG_RB_COLOR2_INFO,
    XE_GPU_REG_RB_COLOR3_INFO,
};

constexpr Register RB_BLENDCONTROL::rt_register_indices[4] = {
    XE_GPU_REG_RB_BLENDCONTROL0,
    XE_GPU_REG_RB_BLENDCONTROL1,
    XE_GPU_REG_RB_BLENDCONTROL2,
    XE_GPU_REG_RB_BLENDCONTROL3,
};

}  //  namespace reg
}  //  namespace gpu
}  //  namespace xe
