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

constexpr uint32_t COHER_STATUS_HOST::register_index;
constexpr uint32_t WAIT_UNTIL::register_index;

constexpr uint32_t SQ_PROGRAM_CNTL::register_index;
constexpr uint32_t SQ_CONTEXT_MISC::register_index;

constexpr uint32_t VGT_OUTPUT_PATH_CNTL::register_index;
constexpr uint32_t VGT_HOS_CNTL::register_index;

constexpr uint32_t PA_SU_POINT_MINMAX::register_index;
constexpr uint32_t PA_SU_POINT_SIZE::register_index;
constexpr uint32_t PA_SU_SC_MODE_CNTL::register_index;
constexpr uint32_t PA_SU_VTX_CNTL::register_index;
constexpr uint32_t PA_SC_MPASS_PS_CNTL::register_index;
constexpr uint32_t PA_SC_VIZ_QUERY::register_index;
constexpr uint32_t PA_CL_CLIP_CNTL::register_index;
constexpr uint32_t PA_CL_VTE_CNTL::register_index;
constexpr uint32_t PA_SC_WINDOW_OFFSET::register_index;
constexpr uint32_t PA_SC_WINDOW_SCISSOR_TL::register_index;
constexpr uint32_t PA_SC_WINDOW_SCISSOR_BR::register_index;

constexpr uint32_t RB_MODECONTROL::register_index;
constexpr uint32_t RB_SURFACE_INFO::register_index;
constexpr uint32_t RB_COLORCONTROL::register_index;
constexpr uint32_t RB_COLOR_INFO::register_index;
constexpr uint32_t RB_COLOR_MASK::register_index;
constexpr uint32_t RB_DEPTHCONTROL::register_index;
constexpr uint32_t RB_STENCILREFMASK::register_index;
constexpr uint32_t RB_DEPTH_INFO::register_index;
constexpr uint32_t RB_COPY_CONTROL::register_index;
constexpr uint32_t RB_COPY_DEST_INFO::register_index;
constexpr uint32_t RB_COPY_DEST_PITCH::register_index;

}  //  namespace reg
}  //  namespace gpu
}  //  namespace xe
