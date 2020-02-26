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

constexpr Register COHER_STATUS_HOST::register_index;
constexpr Register WAIT_UNTIL::register_index;

constexpr Register SQ_PROGRAM_CNTL::register_index;
constexpr Register SQ_CONTEXT_MISC::register_index;

constexpr Register VGT_DRAW_INITIATOR::register_index;
constexpr Register VGT_OUTPUT_PATH_CNTL::register_index;
constexpr Register VGT_HOS_CNTL::register_index;

constexpr Register PA_SU_POINT_MINMAX::register_index;
constexpr Register PA_SU_POINT_SIZE::register_index;
constexpr Register PA_SU_SC_MODE_CNTL::register_index;
constexpr Register PA_SU_VTX_CNTL::register_index;
constexpr Register PA_SC_MPASS_PS_CNTL::register_index;
constexpr Register PA_SC_VIZ_QUERY::register_index;
constexpr Register PA_CL_CLIP_CNTL::register_index;
constexpr Register PA_CL_VTE_CNTL::register_index;
constexpr Register PA_SC_WINDOW_OFFSET::register_index;
constexpr Register PA_SC_WINDOW_SCISSOR_TL::register_index;
constexpr Register PA_SC_WINDOW_SCISSOR_BR::register_index;

constexpr Register RB_MODECONTROL::register_index;
constexpr Register RB_SURFACE_INFO::register_index;
constexpr Register RB_COLORCONTROL::register_index;
constexpr Register RB_COLOR_INFO::register_index;
const Register RB_COLOR_INFO::rt_register_indices[4] = {
    XE_GPU_REG_RB_COLOR_INFO,
    XE_GPU_REG_RB_COLOR1_INFO,
    XE_GPU_REG_RB_COLOR2_INFO,
    XE_GPU_REG_RB_COLOR3_INFO,
};
constexpr Register RB_COLOR_MASK::register_index;
constexpr Register RB_BLENDCONTROL::register_index;
const Register RB_BLENDCONTROL::rt_register_indices[4] = {
    XE_GPU_REG_RB_BLENDCONTROL0,
    XE_GPU_REG_RB_BLENDCONTROL1,
    XE_GPU_REG_RB_BLENDCONTROL2,
    XE_GPU_REG_RB_BLENDCONTROL3,
};
constexpr Register RB_DEPTHCONTROL::register_index;
constexpr Register RB_STENCILREFMASK::register_index;
constexpr Register RB_DEPTH_INFO::register_index;
constexpr Register RB_COPY_CONTROL::register_index;
constexpr Register RB_COPY_DEST_INFO::register_index;
constexpr Register RB_COPY_DEST_PITCH::register_index;

}  //  namespace reg
}  //  namespace gpu
}  //  namespace xe
