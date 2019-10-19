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

enum Register {
#define XE_GPU_REGISTER(index, type, name) XE_GPU_REG_##name = index,
#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER
};

namespace reg {

/*******************************************************************************
   ___ ___  _  _ _____ ___  ___  _
  / __/ _ \| \| |_   _| _ \/ _ \| |
 | (_| (_) | .` | | | |   / (_) | |__
  \___\___/|_|\_| |_| |_|_\\___/|____|

*******************************************************************************/

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
  static constexpr uint32_t register_index = XE_GPU_REG_COHER_STATUS_HOST;
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
  static constexpr uint32_t register_index = XE_GPU_REG_WAIT_UNTIL;
};

/*******************************************************************************
  ___ ___ ___  _   _ ___ _  _  ___ ___ ___
 / __| __/ _ \| | | | __| \| |/ __| __| _ \
 \__ \ _| (_) | |_| | _|| .` | (__| _||   /
 |___/___\__\_\\___/|___|_|\_|\___|___|_|_\

*******************************************************************************/

union SQ_PROGRAM_CNTL {
  // Note from a2xx.xml:
  // Only 0x3F worth of valid register values for VS_NUM_REG and PS_NUM_REG, but
  // high bit is set to indicate "0 registers used".
  xe::bf<uint32_t, 0, 8> vs_num_reg;
  xe::bf<uint32_t, 8, 8> ps_num_reg;
  xe::bf<uint32_t, 16, 1> vs_resource;
  xe::bf<uint32_t, 17, 1> ps_resource;
  xe::bf<uint32_t, 18, 1> param_gen;
  xe::bf<uint32_t, 19, 1> gen_index_pix;
  xe::bf<uint32_t, 20, 4> vs_export_count;
  xe::bf<xenos::VertexShaderExportMode, 24, 3> vs_export_mode;
  xe::bf<uint32_t, 27, 4> ps_export_mode;
  xe::bf<uint32_t, 31, 1> gen_index_vtx;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_SQ_PROGRAM_CNTL;
};

union SQ_CONTEXT_MISC {
  xe::bf<uint32_t, 0, 1> inst_pred_optimize;
  xe::bf<uint32_t, 1, 1> sc_output_screen_xy;
  xe::bf<xenos::SampleControl, 2, 2> sc_sample_cntl;
  xe::bf<uint32_t, 8, 8> param_gen_pos;
  xe::bf<uint32_t, 16, 1> perfcounter_ref;
  xe::bf<uint32_t, 17, 1> yeild_optimize;  // sic
  xe::bf<uint32_t, 18, 1> tx_cache_sel;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_SQ_CONTEXT_MISC;
};

/*******************************************************************************
 __   _____ ___ _____ _____  __
 \ \ / / __| _ \_   _| __\ \/ /
  \ V /| _||   / | | | _| >  <
   \_/ |___|_|_\ |_| |___/_/\_\

   ___ ___  ___  _   _ ___ ___ ___     _   _  _ ___
  / __| _ \/ _ \| | | | _ \ __| _ \   /_\ | \| |   \
 | (_ |   / (_) | |_| |  _/ _||   /  / _ \| .` | |) |
  \___|_|_\\___/ \___/|_| |___|_|_\ /_/ \_\_|\_|___/

  _____ ___ ___ ___ ___ _    _      _ _____ ___  ___
 |_   _| __/ __/ __| __| |  | |    /_\_   _/ _ \| _ \
   | | | _|\__ \__ \ _|| |__| |__ / _ \| || (_) |   /
   |_| |___|___/___/___|____|____/_/ \_\_| \___/|_|_\

*******************************************************************************/

union VGT_OUTPUT_PATH_CNTL {
  xe::bf<xenos::VGTOutputPath, 0, 2> path_select;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_VGT_OUTPUT_PATH_CNTL;
};

union VGT_HOS_CNTL {
  xe::bf<xenos::TessellationMode, 0, 2> tess_mode;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_VGT_HOS_CNTL;
};

/*******************************************************************************
  ___ ___ ___ __  __ ___ _____ _____   _____
 | _ \ _ \_ _|  \/  |_ _|_   _|_ _\ \ / / __|
 |  _/   /| || |\/| || |  | |  | | \ V /| _|
 |_| |_|_\___|_|  |_|___| |_| |___| \_/ |___|

    _   ___ ___ ___ __  __ ___ _    ___ ___
   /_\ / __/ __| __|  \/  | _ ) |  | __| _ \
  / _ \\__ \__ \ _|| |\/| | _ \ |__| _||   /
 /_/ \_\___/___/___|_|  |_|___/____|___|_|_\

*******************************************************************************/

union PA_SU_POINT_MINMAX {
  // Radius, 12.4 fixed point.
  xe::bf<uint32_t, 0, 16> min_size;
  xe::bf<uint32_t, 16, 16> max_size;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SU_POINT_MINMAX;
};

union PA_SU_POINT_SIZE {
  // 1/2 width or height, 12.4 fixed point.
  xe::bf<uint32_t, 0, 16> height;
  xe::bf<uint32_t, 16, 16> width;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SU_POINT_SIZE;
};

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
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SU_SC_MODE_CNTL;
};

// Setup Unit Vertex Control
union PA_SU_VTX_CNTL {
  xe::bf<uint32_t, 0, 1> pix_center;  // 1 = half pixel offset
  xe::bf<uint32_t, 1, 2> round_mode;
  xe::bf<uint32_t, 3, 3> quant_mode;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SU_VTX_CNTL;
};

union PA_SC_MPASS_PS_CNTL {
  xe::bf<uint32_t, 0, 20> mpass_pix_vec_per_pass;
  xe::bf<uint32_t, 31, 1> mpass_ps_ena;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SC_MPASS_PS_CNTL;
};

// Scanline converter viz query
union PA_SC_VIZ_QUERY {
  xe::bf<uint32_t, 0, 1> viz_query_ena;
  xe::bf<uint32_t, 1, 6> viz_query_id;
  xe::bf<uint32_t, 7, 1> kill_pix_post_early_z;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SC_VIZ_QUERY;
};

// Clipper clip control
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
  static constexpr uint32_t register_index = XE_GPU_REG_PA_CL_CLIP_CNTL;
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
  static constexpr uint32_t register_index = XE_GPU_REG_PA_CL_VTE_CNTL;
};

union PA_SC_WINDOW_OFFSET {
  xe::bf<int32_t, 0, 15> window_x_offset;
  xe::bf<int32_t, 16, 15> window_y_offset;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SC_WINDOW_OFFSET;
};

union PA_SC_WINDOW_SCISSOR_TL {
  xe::bf<uint32_t, 0, 14> tl_x;
  xe::bf<uint32_t, 16, 14> tl_y;
  xe::bf<uint32_t, 31, 1> window_offset_disable;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL;
};

union PA_SC_WINDOW_SCISSOR_BR {
  xe::bf<uint32_t, 0, 14> br_x;
  xe::bf<uint32_t, 16, 14> br_y;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR;
};

/*******************************************************************************
  ___ ___
 | _ \ _ )
 |   / _ \
 |_|_\___/

*******************************************************************************/

union RB_MODECONTROL {
  xe::bf<xenos::ModeControl, 0, 3> edram_mode;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_MODECONTROL;
};

union RB_SURFACE_INFO {
  xe::bf<uint32_t, 0, 14> surface_pitch;
  xe::bf<MsaaSamples, 16, 2> msaa_samples;
  xe::bf<uint32_t, 18, 14> hiz_pitch;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_SURFACE_INFO;
};

union RB_COLORCONTROL {
  xe::bf<CompareFunction, 0, 3> alpha_func;
  xe::bf<uint32_t, 3, 1> alpha_test_enable;
  xe::bf<uint32_t, 4, 1> alpha_to_mask_enable;
  // Everything in between was added on Adreno, not in game PDBs and never set.
  xe::bf<uint32_t, 24, 2> alpha_to_mask_offset0;
  xe::bf<uint32_t, 26, 2> alpha_to_mask_offset1;
  xe::bf<uint32_t, 28, 2> alpha_to_mask_offset2;
  xe::bf<uint32_t, 30, 2> alpha_to_mask_offset3;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_COLORCONTROL;
};

union RB_COLOR_INFO {
  xe::bf<uint32_t, 0, 12> color_base;
  xe::bf<ColorRenderTargetFormat, 16, 4> color_format;
  xe::bf<int32_t, 20, 6> color_exp_bias;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_COLOR_INFO;
  // RB_COLOR[1-3]_INFO also use this format.
};

union RB_COLOR_MASK {
  xe::bf<uint32_t, 0, 1> write_red0;
  xe::bf<uint32_t, 1, 1> write_green0;
  xe::bf<uint32_t, 2, 1> write_blue0;
  xe::bf<uint32_t, 3, 1> write_alpha0;
  xe::bf<uint32_t, 4, 1> write_red1;
  xe::bf<uint32_t, 5, 1> write_green1;
  xe::bf<uint32_t, 6, 1> write_blue1;
  xe::bf<uint32_t, 7, 1> write_alpha1;
  xe::bf<uint32_t, 8, 1> write_red2;
  xe::bf<uint32_t, 9, 1> write_green2;
  xe::bf<uint32_t, 10, 1> write_blue2;
  xe::bf<uint32_t, 11, 1> write_alpha2;
  xe::bf<uint32_t, 12, 1> write_red3;
  xe::bf<uint32_t, 13, 1> write_green3;
  xe::bf<uint32_t, 14, 1> write_blue3;
  xe::bf<uint32_t, 15, 1> write_alpha3;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_COLOR_MASK;
};

union RB_DEPTHCONTROL {
  xe::bf<uint32_t, 0, 1> stencil_enable;
  xe::bf<uint32_t, 1, 1> z_enable;
  xe::bf<uint32_t, 2, 1> z_write_enable;
  // EARLY_Z_ENABLE was added on Adreno.
  xe::bf<CompareFunction, 4, 3> zfunc;
  xe::bf<uint32_t, 7, 1> backface_enable;
  xe::bf<CompareFunction, 8, 3> stencilfunc;
  xe::bf<StencilOp, 11, 3> stencilfail;
  xe::bf<StencilOp, 14, 3> stencilzpass;
  xe::bf<StencilOp, 17, 3> stencilzfail;
  xe::bf<CompareFunction, 20, 3> stencilfunc_bf;
  xe::bf<StencilOp, 23, 3> stencilfail_bf;
  xe::bf<StencilOp, 26, 3> stencilzpass_bf;
  xe::bf<StencilOp, 29, 3> stencilzfail_bf;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_DEPTHCONTROL;
};

union RB_STENCILREFMASK {
  xe::bf<uint32_t, 0, 8> stencilref;
  xe::bf<uint32_t, 8, 8> stencilmask;
  xe::bf<uint32_t, 16, 8> stencilwritemask;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_STENCILREFMASK;
  // RB_STENCILREFMASK_BF also uses this format.
};

union RB_DEPTH_INFO {
  xe::bf<uint32_t, 0, 12> depth_base;
  xe::bf<DepthRenderTargetFormat, 16, 1> depth_format;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_DEPTH_INFO;
};

union RB_COPY_CONTROL {
  xe::bf<uint32_t, 0, 3> copy_src_select;
  xe::bf<xenos::CopySampleSelect, 4, 3> copy_sample_select;
  xe::bf<uint32_t, 8, 1> color_clear_enable;
  xe::bf<uint32_t, 9, 1> depth_clear_enable;

  xe::bf<xenos::CopyCommand, 20, 2> copy_command;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_COPY_CONTROL;
};

union RB_COPY_DEST_INFO {
  xe::bf<Endian128, 0, 3> copy_dest_endian;
  xe::bf<uint32_t, 3, 1> copy_dest_array;
  xe::bf<uint32_t, 4, 3> copy_dest_slice;
  xe::bf<ColorFormat, 7, 6> copy_dest_format;
  xe::bf<uint32_t, 13, 3> copy_dest_number;
  xe::bf<int32_t, 16, 6> copy_dest_exp_bias;
  xe::bf<uint32_t, 24, 1> copy_dest_swap;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_COPY_DEST_INFO;
};

union RB_COPY_DEST_PITCH {
  xe::bf<uint32_t, 0, 14> copy_dest_pitch;
  xe::bf<uint32_t, 16, 14> copy_dest_height;

  uint32_t value;
  static constexpr uint32_t register_index = XE_GPU_REG_RB_COPY_DEST_PITCH;
};

}  // namespace reg

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_REGISTERS_H_
