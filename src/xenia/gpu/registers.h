/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_REGISTERS_H_
#define XENIA_GPU_REGISTERS_H_

#include <cstdint>
#include <cstdlib>

#include "xenia/base/bit_field.h"
#include "xenia/gpu/xenos.h"

// Most registers can be found from:
// https://github.com/UDOOboard/Kernel_Unico/blob/master/drivers/mxc/amd-gpu/include/reg/yamato/14/yamato_registers.h
namespace xe {
namespace gpu {
namespace reg {

/**************************************************
   ___ ___  _  _ _____ ___  ___  _
  / __/ _ \| \| |_   _| _ \/ _ \| |
 | (_| (_) | .` | | | |   / (_) | |__
  \___\___/|_|\_| |_| |_|_\\___/|____|

***************************************************/

union COHER_STATUS_HOST {
  xe::bf<uint32_t, 0, 8> matching_contexts;
  xe::bf<uint32_t, 8, 1> rb_copy_dest_base_ena;
  xe::bf<uint32_t, 9, 1> dest_base_0_ena;
  xe::bf<uint32_t, 10, 1> dest_base_1_ena;
  xe::bf<uint32_t, 11, 1> dest_base_2_ena;
  xe::bf<uint32_t, 12, 1> dest_base_3_ena;
  xe::bf<uint32_t, 13, 1> dest_base_4_ena;
  xe::bf<uint32_t, 14, 1> dest_base_5_ena;
  xe::bf<uint32_t, 15, 1> dest_base_6_ena;
  xe::bf<uint32_t, 16, 1> dest_base_7_ena;

  xe::bf<uint32_t, 24, 1> vc_action_ena;
  xe::bf<uint32_t, 25, 1> tc_action_ena;
  xe::bf<uint32_t, 26, 1> pglb_action_ena;

  xe::bf<uint32_t, 31, 1> status;

  uint32_t value;
};

union WAIT_UNTIL {
  xe::bf<uint32_t, 1, 1> wait_re_vsync;
  xe::bf<uint32_t, 2, 1> wait_fe_vsync;
  xe::bf<uint32_t, 3, 1> wait_vsync;
  xe::bf<uint32_t, 4, 1> wait_dsply_id0;
  xe::bf<uint32_t, 5, 1> wait_dsply_id1;
  xe::bf<uint32_t, 6, 1> wait_dsply_id2;

  xe::bf<uint32_t, 10, 1> wait_cmdfifo;

  xe::bf<uint32_t, 14, 1> wait_2d_idle;
  xe::bf<uint32_t, 15, 1> wait_3d_idle;
  xe::bf<uint32_t, 16, 1> wait_2d_idleclean;
  xe::bf<uint32_t, 17, 1> wait_3d_idleclean;

  xe::bf<uint32_t, 20, 4> cmdfifo_entries;

  uint32_t value;
};

/**************************************************
  ___ ___ ___ __  __ ___ _____ _____   _____
 | _ \ _ \_ _|  \/  |_ _|_   _|_ _\ \ / / __|
 |  _/   /| || |\/| || |  | |  | | \ V /| _|
 |_| |_|_\___|_|  |_|___| |_| |___| \_/ |___|

    _   ___ ___ ___ __  __ ___ _    ___ ___
   /_\ / __/ __| __|  \/  | _ ) |  | __| _ \
  / _ \\__ \__ \ _|| |\/| | _ \ |__| _||   /
 /_/ \_\___/___/___|_|  |_|___/____|___|_|_\

***************************************************/

// Setup Unit / Scanline Converter mode cntl
union PA_SU_SC_MODE_CNTL {
  xe::bf<uint32_t, 0, 1> cull_front;
  xe::bf<uint32_t, 1, 1> cull_back;
  xe::bf<uint32_t, 2, 1> face;
  xe::bf<uint32_t, 3, 2> poly_mode;
  xe::bf<uint32_t, 5, 3> polymode_front_ptype;
  xe::bf<uint32_t, 8, 3> polymode_back_ptype;
  xe::bf<uint32_t, 11, 1> poly_offset_front_enable;
  xe::bf<uint32_t, 12, 1> poly_offset_back_enable;
  xe::bf<uint32_t, 13, 1> poly_offset_para_enable;

  xe::bf<uint32_t, 15, 1> msaa_enable;
  xe::bf<uint32_t, 16, 1> vtx_window_offset_enable;

  xe::bf<uint32_t, 18, 1> line_stipple_enable;
  xe::bf<uint32_t, 19, 1> provoking_vtx_last;
  xe::bf<uint32_t, 20, 1> persp_corr_dis;
  xe::bf<uint32_t, 21, 1> multi_prim_ib_ena;

  xe::bf<uint32_t, 23, 1> quad_order_enable;

  xe::bf<uint32_t, 25, 1> wait_rb_idle_all_tri;
  xe::bf<uint32_t, 26, 1> wait_rb_idle_first_tri_new_state;

  uint32_t value;
};

// Setup Unit Vertex Control
union PA_SU_VTX_CNTL {
  xe::bf<uint32_t, 0, 1> pix_center;  // 1 = half pixel offset
  xe::bf<uint32_t, 1, 2> round_mode;
  xe::bf<uint32_t, 3, 3> quant_mode;

  uint32_t value;
};

union PA_SC_MPASS_PS_CNTL {
  xe::bf<uint32_t, 0, 20> mpass_pix_vec_per_pass;
  xe::bf<uint32_t, 31, 1> mpass_ps_ena;

  uint32_t value;
};

// Scanline converter viz query
union PA_SC_VIZ_QUERY {
  xe::bf<uint32_t, 0, 1> viz_query_ena;
  xe::bf<uint32_t, 1, 6> viz_query_id;
  xe::bf<uint32_t, 7, 1> kill_pix_post_early_z;

  uint32_t value;
};

// Clipper clip control
// TODO(DrChat): This seem to differ. Need to examine this.
// https://github.com/decaf-emu/decaf-emu/blob/c017a9ff8128852fb9a5da19466778a171cea6e1/src/libdecaf/src/gpu/latte_registers_pa.h#L11
union PA_CL_CLIP_CNTL {
  xe::bf<uint32_t, 0, 1> ucp_ena_0;
  xe::bf<uint32_t, 1, 1> ucp_ena_1;
  xe::bf<uint32_t, 2, 1> ucp_ena_2;
  xe::bf<uint32_t, 3, 1> ucp_ena_3;
  xe::bf<uint32_t, 4, 1> ucp_ena_4;
  xe::bf<uint32_t, 5, 1> ucp_ena_5;

  xe::bf<uint32_t, 14, 2> ps_ucp_mode;
  xe::bf<uint32_t, 16, 1> clip_disable;
  xe::bf<uint32_t, 17, 1> ucp_cull_only_ena;
  xe::bf<uint32_t, 18, 1> boundary_edge_flag_ena;
  xe::bf<uint32_t, 19, 1> dx_clip_space_def;
  xe::bf<uint32_t, 20, 1> dis_clip_err_detect;
  xe::bf<uint32_t, 21, 1> vtx_kill_or;
  xe::bf<uint32_t, 22, 1> xy_nan_retain;
  xe::bf<uint32_t, 23, 1> z_nan_retain;
  xe::bf<uint32_t, 24, 1> w_nan_retain;

  uint32_t value;
};

// Viewport transform engine control
union PA_CL_VTE_CNTL {
  xe::bf<uint32_t, 0, 1> vport_x_scale_ena;
  xe::bf<uint32_t, 1, 1> vport_x_offset_ena;
  xe::bf<uint32_t, 2, 1> vport_y_scale_ena;
  xe::bf<uint32_t, 3, 1> vport_y_offset_ena;
  xe::bf<uint32_t, 4, 1> vport_z_scale_ena;
  xe::bf<uint32_t, 5, 1> vport_z_offset_ena;

  xe::bf<uint32_t, 8, 1> vtx_xy_fmt;
  xe::bf<uint32_t, 9, 1> vtx_z_fmt;
  xe::bf<uint32_t, 10, 1> vtx_w0_fmt;
  xe::bf<uint32_t, 11, 1> perfcounter_ref;

  uint32_t value;
};

union PA_SC_WINDOW_OFFSET {
  xe::bf<int32_t, 0, 15> window_x_offset;
  xe::bf<int32_t, 16, 15> window_y_offset;

  uint32_t value;
};

union PA_SC_WINDOW_SCISSOR_TL {
  xe::bf<uint32_t, 0, 14> tl_x;
  xe::bf<uint32_t, 16, 14> tl_y;
  xe::bf<uint32_t, 31, 1> window_offset_disable;

  uint32_t value;
};

union PA_SC_WINDOW_SCISSOR_BR {
  xe::bf<uint32_t, 0, 14> br_x;
  xe::bf<uint32_t, 16, 14> br_y;

  uint32_t value;
};

/**************************************************
  ___ ___
 | _ \ _ )
 |   / _ \
 |_|_\___/

***************************************************/

union RB_MODECONTROL {
  xe::bf<xenos::ModeControl, 0, 3> edram_mode;

  uint32_t value;
};

union RB_SURFACE_INFO {
  xe::bf<uint32_t, 0, 14> surface_pitch;
  xe::bf<MsaaSamples, 16, 2> msaa_samples;
  xe::bf<uint32_t, 18, 14> hiz_pitch;

  uint32_t value;
};

union RB_COLORCONTROL {
  xe::bf<uint32_t, 0, 3> alpha_func;
  xe::bf<uint32_t, 3, 1> alpha_test_enable;
  xe::bf<uint32_t, 4, 1> alpha_to_mask_enable;

  xe::bf<uint32_t, 24, 2> alpha_to_mask_offset0;
  xe::bf<uint32_t, 26, 2> alpha_to_mask_offset1;
  xe::bf<uint32_t, 28, 2> alpha_to_mask_offset2;
  xe::bf<uint32_t, 30, 2> alpha_to_mask_offset3;

  uint32_t value;
};

union RB_COLOR_INFO {
  xe::bf<uint32_t, 0, 12> color_base;
  xe::bf<ColorRenderTargetFormat, 16, 4> color_format;
  xe::bf<uint32_t, 20, 6> color_exp_bias;

  uint32_t value;
};

union RB_DEPTH_INFO {
  xe::bf<uint32_t, 0, 12> depth_base;
  xe::bf<DepthRenderTargetFormat, 16, 1> depth_format;

  uint32_t value;
};

union RB_COPY_CONTROL {
  xe::bf<uint32_t, 0, 3> copy_src_select;
  xe::bf<uint32_t, 4, 3> copy_sample_select;
  xe::bf<uint32_t, 8, 1> color_clear_enable;
  xe::bf<uint32_t, 9, 1> depth_clear_enable;

  xe::bf<xenos::CopyCommand, 20, 2> copy_command;

  uint32_t value;
};

union RB_COPY_DEST_INFO {
  xe::bf<Endian128, 0, 3> copy_dest_endian;
  xe::bf<uint32_t, 3, 1> copy_dest_array;
  xe::bf<uint32_t, 4, 3> copy_dest_slice;
  xe::bf<ColorFormat, 7, 6> copy_dest_format;
  xe::bf<uint32_t, 13, 3> copy_dest_number;
  xe::bf<uint32_t, 16, 6> copy_dest_exp_bias;
  xe::bf<uint32_t, 24, 1> copy_dest_swap;

  uint32_t value;
};

union RB_COPY_DEST_PITCH {
  xe::bf<uint32_t, 0, 14> copy_dest_pitch;
  xe::bf<uint32_t, 16, 14> copy_dest_height;

  uint32_t value;
};

}  // namespace reg
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_REGISTERS_H_
