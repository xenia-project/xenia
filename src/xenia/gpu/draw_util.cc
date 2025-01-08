/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/draw_util.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/texture_cache.h"
#include "xenia/ui/graphics_util.h"

// Very prominent in 545407F2.
DEFINE_bool(
    resolve_resolution_scale_fill_half_pixel_offset, true,
    "When using resolution scaling, apply the hack that stretches the first "
    "surely covered host pixel in the left and top sides of render target "
    "resolve areas to eliminate the gap caused by the half-pixel offset (this "
    "is necessary for certain games to display the scene graphics).",
    "GPU");

namespace xe {
namespace gpu {
namespace draw_util {

bool IsRasterizationPotentiallyDone(const RegisterFile& regs,
                                    bool primitive_polygonal) {
  // TODO(Triang3l): Investigate ModeControl::kIgnore better, with respect to
  // sample counting. Let's assume sample counting is a part of depth / stencil,
  // thus disabled too.
  xenos::ModeControl edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode != xenos::ModeControl::kColorDepth &&
      edram_mode != xenos::ModeControl::kDepth) {
    return false;
  }
  if (regs.Get<reg::SQ_PROGRAM_CNTL>().vs_export_mode ==
          xenos::VertexShaderExportMode::kMultipass ||
      !regs.Get<reg::RB_SURFACE_INFO>().surface_pitch) {
    return false;
  }
  if (primitive_polygonal) {
    auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    if (pa_su_sc_mode_cntl.cull_front && pa_su_sc_mode_cntl.cull_back) {
      // Both faces are culled.
      return false;
    }
  }
  return true;
}

reg::RB_DEPTHCONTROL GetNormalizedDepthControl(const RegisterFile& regs) {
  xenos::ModeControl edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode != xenos::ModeControl::kColorDepth &&
      edram_mode != xenos::ModeControl::kDepth) {
    // Both depth and stencil disabled (EDRAM depth and stencil ignored).
    reg::RB_DEPTHCONTROL disabled;
    disabled.value = 0;
    return disabled;
  }
  reg::RB_DEPTHCONTROL depthcontrol = regs.Get<reg::RB_DEPTHCONTROL>();
  // For more reliable skipping of depth render target management for draws not
  // requiring depth.
  if (depthcontrol.z_enable && !depthcontrol.z_write_enable &&
      depthcontrol.zfunc == xenos::CompareFunction::kAlways) {
    depthcontrol.z_enable = 0;
  }
  // Stencil is more complex and is expected to be usually enabled explicitly
  // when needed.
  return depthcontrol;
}

// https://docs.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_standard_multisample_quality_levels
const int8_t kD3D10StandardSamplePositions2x[2][2] = {{4, 4}, {-4, -4}};
const int8_t kD3D10StandardSamplePositions4x[4][2] = {
    {-2, -6}, {6, -2}, {-6, 2}, {2, 6}};

void GetPreferredFacePolygonOffset(const RegisterFile& regs,
                                   bool primitive_polygonal, float& scale_out,
                                   float& offset_out) {
  float scale = 0.0f, offset = 0.0f;
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (primitive_polygonal) {
    // Prefer the front polygon offset because in general, front faces are the
    // ones that are rendered (except for shadow volumes).
    if (pa_su_sc_mode_cntl.poly_offset_front_enable &&
        !pa_su_sc_mode_cntl.cull_front) {
      scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
      offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      scale = roundToNearestOrderOfMagnitude(scale);
    }
    if (pa_su_sc_mode_cntl.poly_offset_back_enable &&
        !pa_su_sc_mode_cntl.cull_back && !scale && !offset) {
      scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE);
      offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
    }
  } else {
    // Non-triangle primitives use the front offset, but it's toggled via
    // poly_offset_para_enable.
    if (pa_su_sc_mode_cntl.poly_offset_para_enable) {
      scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
      offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
    }
  }
  scale_out = scale;
  offset_out = offset;
}

bool IsPixelShaderNeededWithRasterization(const Shader& shader,
                                          const RegisterFile& regs) {
  assert_true(shader.type() == xenos::ShaderType::kPixel);
  assert_true(shader.is_ucode_analyzed());

  // See xenos::ModeControl for explanation why the pixel shader is only used
  // when it's kColorDepth here.
  if (regs.Get<reg::RB_MODECONTROL>().edram_mode !=
      xenos::ModeControl::kColorDepth) {
    return false;
  }

  // Discarding (explicitly or through alphatest or alpha to coverage) has side
  // effects on pixel counting.
  //
  // Depth output only really matters if depth test is active, but it's used
  // extremely rarely, and pretty much always intentionally - for simplicity,
  // consider it as always mattering.
  //
  // Memory export is an obvious intentional side effect.
  if (shader.kills_pixels() || shader.writes_depth() ||
      shader.memexport_eM_written() ||
      (shader.writes_color_target(0) &&
       DoesCoverageDependOnAlpha(regs.Get<reg::RB_COLORCONTROL>()))) {
    return true;
  }

  // Check if a color target is actually written.
  uint32_t rb_color_mask = regs[XE_GPU_REG_RB_COLOR_MASK];
  uint32_t rts_remaining = shader.writes_color_targets();
  uint32_t rt_index;
  while (xe::bit_scan_forward(rts_remaining, &rt_index)) {
    rts_remaining &= ~(uint32_t(1) << rt_index);
    uint32_t format_component_count = GetColorRenderTargetFormatComponentCount(
        regs.Get<reg::RB_COLOR_INFO>(
                reg::RB_COLOR_INFO::rt_register_indices[rt_index])
            .color_format);
    if ((rb_color_mask >> (rt_index * 4)) &
        ((uint32_t(1) << format_component_count) - 1)) {
      return true;
    }
  }

  // Only depth / stencil passthrough potentially.
  return false;
}

static float ViewportRecip2_0(float f) {
  float f1 = ArchReciprocalRefined(f);
  return f1 + f1;
}

// chrispy: todo, the int/float divides and the nan-checked mins show up
// relatively high on uprof when i uc to 1.7ghz
void GetHostViewportInfo(GetViewportInfoArgs* XE_RESTRICT args,
                         ViewportInfo& viewport_info_out) {
  assert_not_zero(args->draw_resolution_scale_x);
  assert_not_zero(args->draw_resolution_scale_y);

  // A vertex position goes the following path:
  //
  // = Vertex shader output in clip space, (-w, -w, 0) ... (w, w, w) for
  //   Direct3D or (-w, -w, -w) ... (w, w, w) for OpenGL.
  // > Clipping to the boundaries of the clip space if enabled.
  // > Division by W if not pre-divided.
  // = Normalized device coordinates, (-1, -1, 0) ... (1, 1, 1) for Direct3D or
  //   (-1, -1, -1) ... (1, 1, 1) for OpenGL.
  // > Viewport scaling.
  // > Viewport, window and half-pixel offsetting.
  // = Actual position in render target pixels used for rasterization and depth
  //   buffer coordinates.
  //
  // On modern PC graphics APIs, all drawing is done with clipping enabled (only
  // Z clipping can be replaced with viewport depth range clamping).
  //
  // On the Xbox 360, however, there are two cases:
  //
  // - Clipping is enabled:
  //
  //   Drawing "as normal", primarily for the game world. Draws are clipped to
  //   the (-w, -w, 0) ... (w, w, w) or (-w, -w, -w) ... (w, w, w) clip space.
  //
  //   Ideally all offsets in pixels (window offset, half-pixel offset) are
  //   post-clip, and thus they would need to be applied via the host viewport
  //   (also the Direct3D 11.3 specification defines this as the correct way of
  //   reproducing the original Direct3D 9 half-pixel offset behavior).
  //
  //   However, in reality, only WARP actually truly clips to -W...W, with the
  //   viewport fractional offset actually accurately making samples outside the
  //   fractional rectangle unable to be covered. AMD, Intel and Nvidia, in
  //   Direct3D 12, all don't truly clip even a really huge primitive to -W...W.
  //   Instead, primitives still overflow the fractional rectangle and cover
  //   samples outside of it. The actual viewport scissor is floor(TopLeftX,
  //   TopLeftY) ... floor(TopLeftX + Width, TopLeftY + Height), with flooring
  //   and addition in float32 (with 0x3F7FFFFF TopLeftXY, or 1.0f - ULP, all
  //   the samples in the top row / left column can be covered, while with
  //   0x3F800000, or 1.0f, none of them can be).
  //
  //   We are reproducing the same behavior here - what would happen if we'd be
  //   passing the guest values directly to Direct3D 12. Also, for consistency
  //   across hardware and APIs (especially Vulkan with viewportSubPixelBits
  //   being 0 rather than at least 8 on some devices - Arm Mali, Imagination
  //   PowerVR), and for simplicity of math, and also for exact calculations in
  //   bounds checking in validation layers of the host APIs, we are returning
  //   integer viewport coordinates, handling the fractional offset in the
  //   vertex shaders instead, via ndc_scale and ndc_offset - it shouldn't
  //   significantly affect precision that we will be doing the offsetting in
  //   W-scaled rather than W-divided units, the ratios of exponents involved in
  //   the calculations stay the same, and everything ends up being 16.8 anyway
  //   on most hardware, so small precision differences are very unlikely to
  //   affect coverage.
  //
  // FIXME(Triang3l): Overestimate or more properly round the viewport scissor
  // boundaries if this flooring causes gaps on the bottom / right side in real
  // games if any are found using fractional viewport coordinates. Viewport
  // scissoring is not an inherent result of the viewport scale / offset, these
  // are used merely for transformation of coordinates; rather, it's done by
  // intersecting the viewport and scissor rectangles in the guest driver and
  // writing the common portion to PA_SC_WINDOW_SCISSOR, so how the scissor is
  // computed for a fractional viewport is entirely up to the guest.
  //
  //   Even though Xbox 360 games are designed for Direct3D, with 0...W range of
  //   Z in clip space, the GPU also allows -W...W. Since Xenia is not targeting
  //   OpenGL (where it would be toggled via glClipControl - or, on ES, it would
  //   always be -W...W), this function always remaps it to 0...W, though
  //   numerically not precisely (0 is moved to 0.5, locking the exponent near
  //   what was the truly floating-point 0 originally). It is the guest
  //   viewport's responsibility (haven't checked, but it's logical) to remap
  //   from -1...1 in the NDC to glDepthRange within the 0...1 range. Also -Z
  //   pointing forward in OpenGL doesn't matter here (the -W...W clip space is
  //   symmetric).
  //
  // - Clipping is disabled:
  //
  //   The most common case of drawing without clipping in games is screen-space
  //   draws, most prominently clears, directly in render target coordinates.
  //
  //   In this particular case (though all the general case arithmetic still
  //   applies), the vertex shader returns a position in pixels, pre-divided by
  //   W (though this doesn't matter if W is 1).
  //
  //   Because clipping is disabled, this huge polygon with, for example,
  //   a (1280, 720, 0, 1) vertex, is not clipped to (-w, -w) ... (w, w), so the
  //   vertex becomes (1280, 720) in the NDC as well (even though in regular 3D
  //   draws with clipping, disregarding the guard band for simplicity, it can't
  //   be bigger than (1, 1) after clipping and the division by W).
  //
  //   For these draws, the viewport is also usually disabled (though, again, it
  //   doesn't have to be - an enabled viewport would likely still work as
  //   usual) by disabling PA_CL_VTE_CNTL::VPORT_X/Y/Z_SCALE/OFFSET_ENA - which
  //   equals to having a viewport scale of (1, 1, 1) and offset of (0, 0, 0).
  //   This results in the NDC being treated directly as pixel coordinates.
  //   Normally, with clipping, this would make only a tiny 1x1 area in the
  //   corner of the render target being possible to cover (and 3 unreachable
  //   pixels outside of the render target). The window offset is then applied,
  //   if needed, as well as the half-pixel offset.
  //
  //   It's also possible (though not verified) that without clipping, Z (as a
  //   result of, for instance, polygon offset, or explicit calculations in the
  //   vertex shader) may end up outside the viewport Z range. Direct3D 10
  //   requires clamping to the viewport Z bounds in all cases in the
  //   output-merger according to the Direct3D 11.3 functional specification. A
  //   different behavior is likely on the Xbox 360, however, because while
  //   Direct3D 10-compatible AMD GPUs such as the R600 have
  //   PA_SC_VPORT_ZMIN/ZMAX registers, the Adreno 200 doesn't seem to have any
  //   equivalents, neither in PA nor in RB. This probably also applies to
  //   shader depth output - possibly doesn't need to be clamped as well.
  //
  //   On the PC, we need to emulate disabled clipping by using a viewport at
  //   least as large as the scissor region within the render target, as well as
  //   the full viewport depth range (plus changing Z clipping to Z clamping on
  //   the host if possible), and rescale from the guest clip space to the host
  //   "no clip" clip space, as well as apply the viewport, the window offset,
  //   and the half-pixel offset, in the vertex shader. Ideally, the host
  //   viewport should have a power of 2 size - so scaling doesn't affect
  //   precision, and is merely an exponent bias.
  //
  // NDC XY point towards +XY on the render target - the viewport scale sign
  // handles the remapping from Direct3D 9 -Y towards +U to a generic
  // transformation from the NDC to pixel coordinates.
  //
  // TODO(Triang3l): Investigate the need for clamping of oDepth to 0...1 for
  // D24FS8 as well.

  auto pa_cl_clip_cntl = args->pa_cl_clip_cntl;
  auto pa_cl_vte_cntl = args->pa_cl_vte_cntl;
  auto pa_su_sc_mode_cntl = args->pa_su_sc_mode_cntl;
  auto pa_su_vtx_cntl = args->pa_su_vtx_cntl;

  // Obtain the original viewport values in a normalized way.
  float scale_xy[] = {
      pa_cl_vte_cntl.vport_x_scale_ena ? args->PA_CL_VPORT_XSCALE : 1.0f,
      pa_cl_vte_cntl.vport_y_scale_ena ? args->PA_CL_VPORT_YSCALE : 1.0f,
  };
  float scale_z =
      pa_cl_vte_cntl.vport_z_scale_ena ? args->PA_CL_VPORT_ZSCALE : 1.0f;

  float offset_base_xy[] = {
      pa_cl_vte_cntl.vport_x_offset_ena ? args->PA_CL_VPORT_XOFFSET : 0.0f,
      pa_cl_vte_cntl.vport_y_offset_ena ? args->PA_CL_VPORT_YOFFSET : 0.0f,
  };
  float offset_z =
      pa_cl_vte_cntl.vport_z_offset_ena ? args->PA_CL_VPORT_ZOFFSET : 0.0f;
  // Calculate all the integer.0 or integer.5 offsetting exactly at full
  // precision, separately so it can be used in other integer calculations
  // without double rounding if needed.
  float offset_add_xy[2] = {};
  if (pa_su_sc_mode_cntl.vtx_window_offset_enable) {
    auto pa_sc_window_offset = args->pa_sc_window_offset;
    offset_add_xy[0] += float(pa_sc_window_offset.window_x_offset);
    offset_add_xy[1] += float(pa_sc_window_offset.window_y_offset);
  }
  if (cvars::half_pixel_offset && !pa_su_vtx_cntl.pix_center) {
    offset_add_xy[0] += 0.5f;
    offset_add_xy[1] += 0.5f;
  }

  // The maximum value is at least the maximum host render target size anyway -
  // and a guest pixel is always treated as a whole with resolution scaling.
  // cbrispy: todo, this integer divides show up high on the profiler somehow
  // (it was a very long session, too)
  uint32_t xy_max_unscaled[] = {
      args->draw_resolution_scale_x_divisor.Apply(args->x_max),
      args->draw_resolution_scale_y_divisor.Apply(args->y_max)};
  assert_not_zero(xy_max_unscaled[0]);
  assert_not_zero(xy_max_unscaled[1]);

  float z_min;
  float z_max;
  float ndc_scale[3];
  float ndc_offset[3];

  if (pa_cl_clip_cntl.clip_disable) {
    // Clipping is disabled - use a huge host viewport, perform pixel and depth
    // offsetting in the vertex shader.

    // XY.
    for (uint32_t i = 0; i < 2; ++i) {
      viewport_info_out.xy_offset[i] = 0;
      uint32_t extent_axis_unscaled =
          std::min(xenos::kTexture2DCubeMaxWidthHeight, xy_max_unscaled[i]);
      viewport_info_out.xy_extent[i] =
          extent_axis_unscaled *
          (i ? args->draw_resolution_scale_y : args->draw_resolution_scale_x);
      float extent_axis_unscaled_float = float(extent_axis_unscaled);

      float pixels_to_ndc_axis = ViewportRecip2_0(extent_axis_unscaled_float);

      ndc_scale[i] = scale_xy[i] * pixels_to_ndc_axis;
      ndc_offset[i] = (offset_base_xy[i] - extent_axis_unscaled_float * 0.5f +
                       offset_add_xy[i]) *
                      pixels_to_ndc_axis;
    }

    // Z.
    z_min = 0.0f;
    z_max = 1.0f;
    ndc_scale[2] = scale_z;
    ndc_offset[2] = offset_z;
  } else {
    // Clipping is enabled - perform pixel and depth offsetting via the host
    // viewport.

    // XY.
    for (uint32_t i = 0; i < 2; ++i) {
      // With resolution scaling, do all viewport XY scissoring in guest pixels
      // if fractional and for the half-pixel offset - we treat guest pixels as
      // a whole, and also the half-pixel offset would be irreversible in guest
      // vertices if we did flooring in host pixels. Instead of flooring, also
      // doing truncation for simplicity - since maxing with 0 is done anyway
      // (we only return viewports in the positive quarter-plane).
      uint32_t axis_resolution_scale =
          i ? args->draw_resolution_scale_y : args->draw_resolution_scale_x;
      float offset_axis = offset_base_xy[i] + offset_add_xy[i];
      float scale_axis = scale_xy[i];
      float scale_axis_abs = std::abs(scale_xy[i]);
      float axis_max_unscaled_float = float(xy_max_unscaled[i]);
      uint32_t axis_0_int = uint32_t(xe::clamp_float(
          offset_axis - scale_axis_abs, 0.0f, axis_max_unscaled_float));
      uint32_t axis_1_int = uint32_t(xe::clamp_float(
          offset_axis + scale_axis_abs, 0.0f, axis_max_unscaled_float));
      uint32_t axis_extent_int = axis_1_int - axis_0_int;
      viewport_info_out.xy_offset[i] = axis_0_int * axis_resolution_scale;
      viewport_info_out.xy_extent[i] = axis_extent_int * axis_resolution_scale;
      float ndc_scale_axis;
      float ndc_offset_axis;
      if (axis_extent_int) {
        // Rescale from the old bounds to the new ones, and also apply the sign.
        // If the new bounds are smaller than the old, for instance, we're
        // cropping - the new -W...W clip space is a subregion of the old one -
        // the scale should be > 1 so the area being cut off ends up outside
        // -W...W. If the new region should include more than the original clip
        // space, a region previously outside -W...W should end up within it, so
        // the scale should be < 1.
        float axis_extent_rounded = float(axis_extent_int);
        float inv_axis_extent_rounded =
            ArchReciprocalRefined(axis_extent_rounded);

        ndc_scale_axis = scale_axis * 2.0f * inv_axis_extent_rounded;
        // Move the origin of the snapped coordinates back to the original one.
        ndc_offset_axis = (float(offset_axis) -
                           (float(axis_0_int) + axis_extent_rounded * 0.5f)) *
                          2.0f * inv_axis_extent_rounded;
      } else {
        // Empty viewport (everything outside the viewport scissor).
        ndc_scale_axis = 1.0f;
        ndc_offset_axis = 0.0f;
      }
      ndc_scale[i] = ndc_scale_axis;
      ndc_offset[i] = ndc_offset_axis;
    }

    // Z.
    float host_clip_offset_z;
    float host_clip_scale_z;
    if (pa_cl_clip_cntl.dx_clip_space_def) {
      host_clip_offset_z = offset_z;
      host_clip_scale_z = scale_z;
      ndc_scale[2] = 1.0f;
      ndc_offset[2] = 0.0f;
    } else {
      // Normalizing both Direct3D / Vulkan 0...W and OpenGL -W...W clip spaces
      // to 0...W. We are not targeting OpenGL, but there we could accept the
      // wanted clip space (Direct3D, OpenGL, or any) and return the actual one
      // (Direct3D or OpenGL).
      //
      // If the guest wants to use -W...W clip space (-1...1 NDC) and a 0...1
      // depth range in the end, it's expected to use ZSCALE of 0.5 and ZOFFSET
      // of 0.5.
      //
      // We are providing the near and the far (or offset and offset + scale)
      // plane distances to the host API in a way that the near maps to Z = 0
      // and the far maps to Z = W in clip space (or Z = 1 in NDC).
      //
      // With D3D offset and scale that we want, assuming D3D clip space input,
      // the formula for the depth would be:
      //
      // depth = offset_d3d + scale_d3d * ndc_z_d3d
      //
      // We are remapping the incoming OpenGL Z from -W...W to 0...W by scaling
      // it by 0.5 and adding 0.5 * W to the result. So, our depth formula would
      // be:
      //
      // depth = offset_d3d + scale_d3d * (ndc_z_gl * 0.5 + 0.5)
      //
      // The guest registers, however, contain the offset and the scale for
      // remapping not from 0...W to near...far, but from -W...W to near...far,
      // or:
      //
      // depth = offset_gl + scale_gl * ndc_z_gl
      //
      // Knowing offset_gl, scale_gl and how ndc_z_d3d can be obtained from
      // ndc_z_gl, we need to derive the formulas for the needed offset_d3d and
      // scale_d3d to apply them to the incoming ndc_z_d3d.
      //
      // depth = offset_gl + scale_gl * (ndc_z_d3d * 2 - 1)
      //
      // Expanding:
      //
      // depth = offset_gl + (scale_gl * ndc_z_d3d * 2 - scale_gl)
      //
      // Reordering:
      //
      // depth = (offset_gl - scale_gl) + (scale_gl * 2) * ndc_z_d3d
      // offset_d3d = offset_gl - scale_gl
      // scale_d3d = scale_gl * 2
      host_clip_offset_z = offset_z - scale_z;
      host_clip_scale_z = scale_z * 2.0f;
      // Need to remap -W...W clip space to 0...W via ndc_scale and ndc_offset -
      // by scaling Z by 0.5 and adding 0.5 * W to it.
      ndc_scale[2] = 0.5f;
      ndc_offset[2] = 0.5f;
    }
    if (args->pixel_shader_writes_depth) {
      // Allow the pixel shader to write any depth value since
      // PA_SC_VPORT_ZMIN/ZMAX isn't present on the Adreno 200; guest pixel
      // shaders don't have access to the original Z in the viewport space
      // anyway and likely must write the depth on all execution paths.
      z_min = 0.0f;
      z_max = 1.0f;
    } else {
      // This clamping is not very correct, but just for safety. Direct3D
      // doesn't allow an unrestricted depth range. Vulkan does, as an
      // extension. But cases when this really matters are yet to be found -
      // trying to fix this will result in more correct depth values, but
      // incorrect clipping.
      z_min = xe::saturate(host_clip_offset_z);
      z_max = xe::saturate(host_clip_offset_z + host_clip_scale_z);
      // Direct3D 12 doesn't allow reverse depth range - on some drivers it
      // works, on some drivers it doesn't, actually, but it was never
      // explicitly allowed by the specification.
      if (!args->allow_reverse_z && z_min > z_max) {
        std::swap(z_min, z_max);
        ndc_scale[2] = -ndc_scale[2];
        ndc_offset[2] = 1.0f - ndc_offset[2];
      }
    }
  }

  if (args->normalized_depth_control.z_enable &&
      args->depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    if (args->convert_z_to_float24) {
      // Need to adjust the bounds that the resulting depth values will be
      // clamped to after the pixel shader. Preferring adding some error to
      // interpolated Z instead if conversion can't be done exactly, without
      // modifying clipping bounds by adjusting Z in vertex shaders, as that
      // may cause polygons placed explicitly at Z = 0 or Z = W to be clipped.
      // Rounding the bounds to the nearest even regardless of the depth
      // rounding mode not to add even more error by truncating twice.
      z_min = xenos::Float20e4To32(xenos::Float32To20e4(z_min, true));
      z_max = xenos::Float20e4To32(xenos::Float32To20e4(z_max, true));
    }
    if (args->full_float24_in_0_to_1) {
      // Remap the full [0...2) float24 range to [0...1) support data round-trip
      // during render target ownership transfer of EDRAM tiles through depth
      // input without unrestricted depth range.
      z_min *= 0.5f;
      z_max *= 0.5f;
    }
  }
  viewport_info_out.z_min = z_min;
  viewport_info_out.z_max = z_max;

  if (args->origin_bottom_left) {
    ndc_scale[1] = -ndc_scale[1];
    ndc_offset[1] = -ndc_offset[1];
  }
  for (uint32_t i = 0; i < 3; ++i) {
    viewport_info_out.ndc_scale[i] = ndc_scale[i];
    viewport_info_out.ndc_offset[i] = ndc_offset[i];
  }
}
template <bool clamp_to_surface_pitch>
static inline void GetScissorTmpl(const RegisterFile& XE_RESTRICT regs,
                                  Scissor& XE_RESTRICT scissor_out) {
#if XE_ARCH_AMD64 == 1
  auto pa_sc_window_scissor_tl = regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
  auto pa_sc_window_scissor_br = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
  auto pa_sc_screen_scissor_tl = regs.Get<reg::PA_SC_SCREEN_SCISSOR_TL>();
  auto pa_sc_screen_scissor_br = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
  uint32_t surface_pitch = 0;
  if constexpr (clamp_to_surface_pitch) {
    surface_pitch = regs.Get<reg::RB_SURFACE_INFO>().surface_pitch;
  }
  uint32_t pa_sc_window_scissor_tl_tl_x = pa_sc_window_scissor_tl.tl_x,
           pa_sc_window_scissor_tl_tl_y = pa_sc_window_scissor_tl.tl_y,
           pa_sc_window_scissor_br_br_x = pa_sc_window_scissor_br.br_x,
           pa_sc_window_scissor_br_br_y = pa_sc_window_scissor_br.br_y,
           pa_sc_window_offset_window_x_offset =
               pa_sc_window_offset.window_x_offset,
           pa_sc_window_offset_window_y_offset =
               pa_sc_window_offset.window_y_offset,
           pa_sc_screen_scissor_tl_tl_x = pa_sc_screen_scissor_tl.tl_x,
           pa_sc_screen_scissor_tl_tl_y = pa_sc_screen_scissor_tl.tl_y,
           pa_sc_screen_scissor_br_br_x = pa_sc_screen_scissor_br.br_x,
           pa_sc_screen_scissor_br_br_y = pa_sc_screen_scissor_br.br_y;

  int32_t tl_x = int32_t(pa_sc_window_scissor_tl_tl_x);
  int32_t tl_y = int32_t(pa_sc_window_scissor_tl_tl_y);

  int32_t br_x = int32_t(pa_sc_window_scissor_br_br_x);
  int32_t br_y = int32_t(pa_sc_window_scissor_br_br_y);

  __m128i tmp1 = _mm_setr_epi32(tl_x, tl_y, br_x, br_y);
  __m128i pa_sc_scissor = _mm_setr_epi32(
      pa_sc_screen_scissor_tl_tl_x, pa_sc_screen_scissor_tl_tl_y,
      pa_sc_screen_scissor_br_br_x, pa_sc_screen_scissor_br_br_y);
#if XE_PLATFORM_WIN32
  __m128i xyoffsetadd = _mm_cvtsi64x_si128(
      static_cast<unsigned long long>(pa_sc_window_offset_window_x_offset) |
      (static_cast<unsigned long long>(pa_sc_window_offset_window_y_offset)
       << 32));
#else
  __m128i xyoffsetadd = _mm_cvtsi64_si128(
      static_cast<unsigned long long>(pa_sc_window_offset_window_x_offset) |
      (static_cast<unsigned long long>(pa_sc_window_offset_window_y_offset)
       << 32));
#endif
  xyoffsetadd = _mm_unpacklo_epi64(xyoffsetadd, xyoffsetadd);
  // chrispy: put this here to make it clear that the shift by 31 is extracting
  // this field
  XE_MAYBE_UNUSED
  uint32_t window_offset_disable_reference =
      pa_sc_window_scissor_tl.window_offset_disable;

  __m128i offset_disable_mask = _mm_set1_epi32(pa_sc_window_scissor_tl.value);

  __m128i addend = _mm_blendv_epi8(xyoffsetadd, _mm_setzero_si128(),
                                   _mm_srai_epi32(offset_disable_mask, 31));

  tmp1 = _mm_add_epi32(tmp1, addend);

  //}
  // Screen scissor is not used by Direct3D 9 (always 0, 0 to 8192, 8192), but
  // still handled here for completeness.
  __m128i lomax = _mm_max_epi32(tmp1, pa_sc_scissor);
  __m128i himin = _mm_min_epi32(tmp1, pa_sc_scissor);

  tmp1 = _mm_blend_epi16(lomax, himin, 0b11110000);

  if constexpr (clamp_to_surface_pitch) {
    // Clamp the horizontal scissor to surface_pitch for safety, in case that's
    // not done by the guest for some reason (it's not when doing draws without
    // clipping in Direct3D 9, for instance), to prevent overflow - this is
    // important for host implementations, both based on target-indepedent
    // rasterization without render target width at all (pixel shader
    // interlock-based custom RB implementations) and using conventional render
    // targets, but padded to EDRAM tiles.
    tmp1 = _mm_blend_epi16(
        tmp1, _mm_min_epi32(tmp1, _mm_set1_epi32(surface_pitch)), 0b00110011);
  }

  tmp1 = _mm_max_epi32(tmp1, _mm_setzero_si128());

  __m128i tl_in_high = _mm_unpacklo_epi64(tmp1, tmp1);

  __m128i final_br = _mm_max_epi32(tmp1, tl_in_high);
  final_br = _mm_sub_epi32(final_br, tl_in_high);
  __m128i scissor_res = _mm_blend_epi16(tmp1, final_br, 0b11110000);
  _mm_storeu_si128((__m128i*)&scissor_out, scissor_res);
#else
  auto pa_sc_window_scissor_tl = regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
  auto pa_sc_window_scissor_br = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
  auto pa_sc_screen_scissor_tl = regs.Get<reg::PA_SC_SCREEN_SCISSOR_TL>();
  auto pa_sc_screen_scissor_br = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
  uint32_t surface_pitch = 0;
  if constexpr (clamp_to_surface_pitch) {
    surface_pitch = regs.Get<reg::RB_SURFACE_INFO>().surface_pitch;
  }
  uint32_t pa_sc_window_scissor_tl_tl_x = pa_sc_window_scissor_tl.tl_x,
           pa_sc_window_scissor_tl_tl_y = pa_sc_window_scissor_tl.tl_y,
           pa_sc_window_scissor_br_br_x = pa_sc_window_scissor_br.br_x,
           pa_sc_window_scissor_br_br_y = pa_sc_window_scissor_br.br_y,
           pa_sc_window_offset_window_x_offset =
               pa_sc_window_offset.window_x_offset,
           pa_sc_window_offset_window_y_offset =
               pa_sc_window_offset.window_y_offset,
           pa_sc_screen_scissor_tl_tl_x = pa_sc_screen_scissor_tl.tl_x,
           pa_sc_screen_scissor_tl_tl_y = pa_sc_screen_scissor_tl.tl_y,
           pa_sc_screen_scissor_br_br_x = pa_sc_screen_scissor_br.br_x,
           pa_sc_screen_scissor_br_br_y = pa_sc_screen_scissor_br.br_y;

  int32_t tl_x = int32_t(pa_sc_window_scissor_tl_tl_x);
  int32_t tl_y = int32_t(pa_sc_window_scissor_tl_tl_y);

  int32_t br_x = int32_t(pa_sc_window_scissor_br_br_x);
  int32_t br_y = int32_t(pa_sc_window_scissor_br_br_y);

  // chrispy: put this here to make it clear that the shift by 31 is extracting
  // this field
  XE_MAYBE_UNUSED
  uint32_t window_offset_disable_reference =
      pa_sc_window_scissor_tl.window_offset_disable;
  int32_t window_offset_disable_mask =
      ~(static_cast<int32_t>(pa_sc_window_scissor_tl.value) >> 31);
  // if (!pa_sc_window_scissor_tl.window_offset_disable) {

  tl_x += pa_sc_window_offset_window_x_offset & window_offset_disable_mask;
  tl_y += pa_sc_window_offset_window_y_offset & window_offset_disable_mask;
  br_x += pa_sc_window_offset_window_x_offset & window_offset_disable_mask;
  br_y += pa_sc_window_offset_window_y_offset & window_offset_disable_mask;
  //}
  // Screen scissor is not used by Direct3D 9 (always 0, 0 to 8192, 8192), but
  // still handled here for completeness.

  tl_x = std::max(tl_x, int32_t(pa_sc_screen_scissor_tl_tl_x));
  tl_y = std::max(tl_y, int32_t(pa_sc_screen_scissor_tl_tl_y));

  br_x = std::min(br_x, int32_t(pa_sc_screen_scissor_br_br_x));
  br_y = std::min(br_y, int32_t(pa_sc_screen_scissor_br_br_y));
  if constexpr (clamp_to_surface_pitch) {
    // Clamp the horizontal scissor to surface_pitch for safety, in case that's
    // not done by the guest for some reason (it's not when doing draws without
    // clipping in Direct3D 9, for instance), to prevent overflow - this is
    // important for host implementations, both based on target-indepedent
    // rasterization without render target width at all (pixel shader
    // interlock-based custom RB implementations) and using conventional render
    // targets, but padded to EDRAM tiles.

    tl_x = std::min(tl_x, int32_t(surface_pitch));
    br_x = std::min(br_x, int32_t(surface_pitch));
  }
  // Ensure the rectangle is non-negative, by collapsing it into a 0-sized one
  // (not by reordering the bounds preserving the width / height, which would
  // reveal samples not meant to be covered, unless TL > BR does that on a real
  // console, but no evidence of such has ever been seen), and also drop
  // negative offsets.
  tl_x = std::max(tl_x, int32_t(0));
  tl_y = std::max(tl_y, int32_t(0));
  br_x = std::max(br_x, tl_x);
  br_y = std::max(br_y, tl_y);
  scissor_out.offset[0] = uint32_t(tl_x);
  scissor_out.offset[1] = uint32_t(tl_y);
  scissor_out.extent[0] = uint32_t(br_x - tl_x);
  scissor_out.extent[1] = uint32_t(br_y - tl_y);
#endif
}

void GetScissor(const RegisterFile& XE_RESTRICT regs,
                Scissor& XE_RESTRICT scissor_out, bool clamp_to_surface_pitch) {
  if (clamp_to_surface_pitch) {
    return GetScissorTmpl<true>(regs, scissor_out);
  } else {
    return GetScissorTmpl<false>(regs, scissor_out);
  }
}

uint32_t GetNormalizedColorMask(const RegisterFile& regs,
                                uint32_t pixel_shader_writes_color_targets) {
  if (regs.Get<reg::RB_MODECONTROL>().edram_mode !=
      xenos::ModeControl::kColorDepth) {
    return 0;
  }
  uint32_t normalized_color_mask = 0;
  uint32_t rb_color_mask = regs[XE_GPU_REG_RB_COLOR_MASK];
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    // Exclude the render targets not statically written to by the pixel shader.
    // If the shader doesn't write to a render target, it shouldn't be written
    // to, and no ownership transfers should happen to it on the host even -
    // otherwise, in 4D5307E6, one render target is being destroyed by a shader
    // not writing anything, and in 58410955, the result of clearing the top
    // tile is being ignored because there are 4 render targets bound with the
    // same EDRAM base (clearly not correct usage), but the shader only clears
    // 1, and then ownership of EDRAM portions by host render targets is
    // conflicting.
    if (!(pixel_shader_writes_color_targets & (uint32_t(1) << i))) {
      continue;
    }
    // Check if any existing component is written to.
    uint32_t format_component_mask =
        (uint32_t(1) << xenos::GetColorRenderTargetFormatComponentCount(
             regs.Get<reg::RB_COLOR_INFO>(
                     reg::RB_COLOR_INFO::rt_register_indices[i])
                 .color_format)) -
        1;
    uint32_t rt_write_mask = (rb_color_mask >> (4 * i)) & format_component_mask;
    if (!rt_write_mask) {
      continue;
    }
    // Mark the non-existent components as written so in the host driver, no
    // slow path (involving reading and merging components) is taken if the
    // driver doesn't perform this check internally, and some components are not
    // included in the mask even though they actually don't exist in the format.
    rt_write_mask |= 0b1111 & ~format_component_mask;
    // Add to the normalized mask.
    normalized_color_mask |= rt_write_mask << (4 * i);
  }
  return normalized_color_mask;
}

void AddMemExportRanges(const RegisterFile& regs, const Shader& shader,
                        std::vector<MemExportRange>& ranges_out) {
  if (!shader.memexport_eM_written()) {
    // The shader has eA writes, but no real exports.
    return;
  }
  uint32_t float_constants_base = shader.type() == xenos::ShaderType::kVertex
                                      ? regs.Get<reg::SQ_VS_CONST>().base
                                      : regs.Get<reg::SQ_PS_CONST>().base;
  for (uint32_t constant_index : shader.memexport_stream_constants()) {
    xenos::xe_gpu_memexport_stream_t stream =
        regs.GetMemExportStream(float_constants_base + constant_index);
    // Safety checks for stream constants potentially not set up if the export
    // isn't done on the control flow path taken by the shader (not checking the
    // Y component because the index is more likely to be constructed
    // arbitrarily).
    // The hardware validates the upper bits of eA according to the
    // IPR2015-00325 sequencer specification.
    if (stream.const_0x1 != 0x1 || stream.const_0x4b0 != 0x4B0 ||
        stream.const_0x96 != 0x96 || !stream.index_count) {
      continue;
    }
    const FormatInfo& format_info =
        *FormatInfo::Get(xenos::TextureFormat(stream.format));
    if (format_info.type != FormatType::kResolvable) {
      XELOGE("Unsupported memexport format {}",
             FormatInfo::GetName(format_info.format));
      // Translated shaders shouldn't be performing exports with an unknown
      // format, the draw can still be performed.
      continue;
    }
    // TODO(Triang3l): Remove the unresearched format logging when it's known
    // how exactly these formats need to be handled (most importantly what
    // components need to be stored and in which order).
    switch (stream.format) {
      case xenos::ColorFormat::k_8_A:
      case xenos::ColorFormat::k_8_B:
      case xenos::ColorFormat::k_8_8_8_8_A:
        XELOGW(
            "Memexport done to an unresearched format {}, report the game to "
            "Xenia developers!",
            FormatInfo::GetName(format_info.format));
        break;
      default:
        break;
    }
    uint32_t stream_size_bytes =
        stream.index_count * (format_info.bits_per_pixel >> 3);
    // Try to reduce the number of shared memory operations when writing
    // different elements into the same buffer through different exports
    // (happens in 4D5307E6).
    bool range_reused = false;
    for (MemExportRange& range : ranges_out) {
      if (range.base_address_dwords == stream.base_address) {
        range.size_bytes = std::max(range.size_bytes, stream_size_bytes);
        range_reused = true;
        break;
      }
    }
    // Add a new range if haven't expanded an existing one.
    if (!range_reused) {
      ranges_out.emplace_back(uint32_t(stream.base_address), stream_size_bytes);
    }
  }
}

XE_NOINLINE
XE_NOALIAS

xenos::CopySampleSelect SanitizeCopySampleSelect(
    xenos::CopySampleSelect copy_sample_select, xenos::MsaaSamples msaa_samples,
    bool is_depth) {
  // Depth can't be averaged.
  if (msaa_samples >= xenos::MsaaSamples::k4X) {
    if (copy_sample_select > xenos::CopySampleSelect::k0123) {
      copy_sample_select = xenos::CopySampleSelect::k0123;
    }
    if (is_depth) {
      switch (copy_sample_select) {
        case xenos::CopySampleSelect::k01:
        case xenos::CopySampleSelect::k0123:
          copy_sample_select = xenos::CopySampleSelect::k0;
          break;
        case xenos::CopySampleSelect::k23:
          copy_sample_select = xenos::CopySampleSelect::k2;
          break;
        default:
          break;
      }
    }
  } else if (msaa_samples >= xenos::MsaaSamples::k2X) {
    switch (copy_sample_select) {
      case xenos::CopySampleSelect::k2:
        copy_sample_select = xenos::CopySampleSelect::k0;
        break;
      case xenos::CopySampleSelect::k3:
        copy_sample_select = xenos::CopySampleSelect::k1;
        break;
      default:
        if (copy_sample_select > xenos::CopySampleSelect::k01) {
          copy_sample_select = xenos::CopySampleSelect::k01;
        }
    }
    if (is_depth && copy_sample_select == xenos::CopySampleSelect::k01) {
      copy_sample_select = xenos::CopySampleSelect::k0;
    }
  } else {
    copy_sample_select = xenos::CopySampleSelect::k0;
  }
  return copy_sample_select;
}

void GetResolveEdramTileSpan(ResolveEdramInfo edram_info,
                             ResolveCoordinateInfo coordinate_info,
                             uint32_t height_div_8, uint32_t& base_out,
                             uint32_t& row_length_used_out,
                             uint32_t& rows_out) {
  // Due to 64bpp, and also not to make an assumption that the offsets are
  // limited to (80 - 8, 8 - 8) with 2x MSAA, and (40 - 8, 8 - 8) with 4x MSAA,
  // still taking the offset into account.
  uint32_t x_scale_log2 =
      3 + uint32_t(edram_info.msaa_samples >= xenos::MsaaSamples::k4X) +
      edram_info.format_is_64bpp;
  uint32_t x0 = (coordinate_info.edram_offset_x_div_8 << x_scale_log2) /
                xenos::kEdramTileWidthSamples;
  uint32_t x1 =
      (((coordinate_info.edram_offset_x_div_8 + coordinate_info.width_div_8)
        << x_scale_log2) +
       (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  uint32_t y_scale_log2 =
      3 + uint32_t(edram_info.msaa_samples >= xenos::MsaaSamples::k2X);
  uint32_t y0 = (coordinate_info.edram_offset_y_div_8 << y_scale_log2) /
                xenos::kEdramTileHeightSamples;
  uint32_t y1 =
      (((coordinate_info.edram_offset_y_div_8 + height_div_8) << y_scale_log2) +
       (xenos::kEdramTileHeightSamples - 1)) /
      xenos::kEdramTileHeightSamples;
  base_out = edram_info.base_tiles + y0 * edram_info.pitch_tiles + x0;
  row_length_used_out = x1 - x0;
  rows_out = y1 - y0;
}

const ResolveCopyShaderInfo
    resolve_copy_shader_info[size_t(ResolveCopyShaderIndex::kCount)] = {
        {"Resolve Copy Fast 32bpp 1x/2xMSAA", false, 4, 4, 6, 3},
        {"Resolve Copy Fast 32bpp 4xMSAA", false, 4, 4, 6, 3},
        {"Resolve Copy Fast 64bpp 1x/2xMSAA", false, 4, 4, 5, 3},
        {"Resolve Copy Fast 64bpp 4xMSAA", false, 3, 4, 5, 3},
        {"Resolve Copy Full 8bpp", true, 2, 3, 6, 3},
        {"Resolve Copy Full 16bpp", true, 2, 3, 5, 3},
        {"Resolve Copy Full 32bpp", true, 2, 4, 5, 3},
        {"Resolve Copy Full 64bpp", true, 2, 4, 5, 3},
        {"Resolve Copy Full 128bpp", true, 2, 4, 4, 3},
};
XE_MSVC_OPTIMIZE_SMALL()
bool GetResolveInfo(const RegisterFile& regs, const Memory& memory,
                    TraceWriter& trace_writer, uint32_t draw_resolution_scale_x,
                    uint32_t draw_resolution_scale_y,
                    bool fixed_rg16_truncated_to_minus_1_to_1,
                    bool fixed_rgba16_truncated_to_minus_1_to_1,
                    ResolveInfo& info_out) {
  // Don't pass uninitialized values to shaders, not to leak data to frame
  // captures. Also initialize an invalid resolve to empty.
  info_out.coordinate_info.packed = 0;
  info_out.height_div_8 = 0;

  auto rb_copy_control = regs.Get<reg::RB_COPY_CONTROL>();
  info_out.rb_copy_control = rb_copy_control;

  if (rb_copy_control.copy_command != xenos::CopyCommand::kRaw &&
      rb_copy_control.copy_command != xenos::CopyCommand::kConvert) {
    XELOGE(
        "Unsupported resolve copy command {}. Report the game to Xenia "
        "developers",
        uint32_t(rb_copy_control.copy_command));
    assert_always();
    return false;
  }

  // Get the extent of pixels covered by the resolve rectangle, according to the
  // top-left rasterization rule.
  // D3D9 HACK: Vertices to use are always in vf0, and are written by the CPU.
  xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(0);
  if (fetch.type != xenos::FetchConstantType::kVertex || fetch.size != 3 * 2) {
    XELOGE("Unsupported resolve vertex buffer format");
    assert_always();
    return false;
  }
  trace_writer.WriteMemoryRead(fetch.address * sizeof(uint32_t),
                               fetch.size * sizeof(uint32_t));
  const float* vertices_guest = reinterpret_cast<const float*>(
      memory.TranslatePhysical(fetch.address * sizeof(uint32_t)));
  // Most vertices have a negative half-pixel offset applied, which we reverse.
  float half_pixel_offset =
      regs.Get<reg::PA_SU_VTX_CNTL>().pix_center ? 0.0f : 0.5f;
  int32_t vertices_fixed[6];
  for (size_t i = 0; i < xe::countof(vertices_fixed); ++i) {
    vertices_fixed[i] = ui::FloatToD3D11Fixed16p8(
        xenos::GpuSwap(vertices_guest[i], fetch.endian) + half_pixel_offset);
  }
  // Inclusive.
  int32_t x0 = std::min(std::min(vertices_fixed[0], vertices_fixed[2]),
                        vertices_fixed[4]);
  int32_t y0 = std::min(std::min(vertices_fixed[1], vertices_fixed[3]),
                        vertices_fixed[5]);
  // Exclusive.
  int32_t x1 = std::max(std::max(vertices_fixed[0], vertices_fixed[2]),
                        vertices_fixed[4]);
  int32_t y1 = std::max(std::max(vertices_fixed[1], vertices_fixed[3]),
                        vertices_fixed[5]);
  // Top-left - include .5 (0.128 treated as 0 covered, 0.129 as 0 not covered).
  x0 = (x0 + 127) >> 8;
  y0 = (y0 + 127) >> 8;
  // Bottom-right - exclude .5.
  x1 = (x1 + 127) >> 8;
  y1 = (y1 + 127) >> 8;

  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();

  // Apply the window offset to the vertices.
  if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
    x0 += pa_sc_window_offset.window_x_offset;
    y0 += pa_sc_window_offset.window_y_offset;
    x1 += pa_sc_window_offset.window_x_offset;
    y1 += pa_sc_window_offset.window_y_offset;
  }

  // Apply the scissor and prevent negative origin (behind the EDRAM base).
  Scissor scissor;
  // False because clamping to the surface pitch will be done later (it will be
  // aligned to the resolve alignment here, for resolving from render targets
  // with a pitch that is not a multiple of 8).
  GetScissor(regs, scissor, false);
  int32_t scissor_right = int32_t(scissor.offset[0] + scissor.extent[0]);
  int32_t scissor_bottom = int32_t(scissor.offset[1] + scissor.extent[1]);
  x0 = std::clamp(x0, int32_t(scissor.offset[0]), scissor_right);
  y0 = std::clamp(y0, int32_t(scissor.offset[1]), scissor_bottom);
  x1 = std::clamp(x1, int32_t(scissor.offset[0]), scissor_right);
  y1 = std::clamp(y1, int32_t(scissor.offset[1]), scissor_bottom);

  assert_true(x0 <= x1 && y0 <= y1);

  // Direct3D 9's D3DDevice_Resolve internally rounds the right/bottom of the
  // rectangle internally to 8. While all the alignment should have already been
  // done by Direct3D 9, just for safety of host implementation of resolve,
  // force-align the rectangle by expanding (D3D9 expands to the right/bottom
  // for some reason, haven't found how left/top is rounded, but logically it
  // would make sense to expand to the left/top too).
  x0 &= ~int32_t(xenos::kResolveAlignmentPixels - 1);
  y0 &= ~int32_t(xenos::kResolveAlignmentPixels - 1);
  x1 = xe::align(x1, int32_t(xenos::kResolveAlignmentPixels));
  y1 = xe::align(y1, int32_t(xenos::kResolveAlignmentPixels));

  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  if (rb_surface_info.msaa_samples > xenos::MsaaSamples::k4X) {
    // Safety check because a lot of code assumes up to 4x.
    assert_always();
    XELOGE(
        "{}x MSAA requested by the guest in a resolve, Xenos only supports up "
        "to 4x",
        uint32_t(1) << uint32_t(rb_surface_info.msaa_samples));
    return false;
  }

  // Clamp to the EDRAM surface pitch (maximum possible surface pitch is also
  // assumed to be the largest resolvable size).
  int32_t surface_pitch_aligned =
      int32_t(rb_surface_info.surface_pitch &
              ~uint32_t(xenos::kResolveAlignmentPixels - 1));
  if (x1 > surface_pitch_aligned) {
    XELOGE("Resolve region {} <= x < {} is outside the surface pitch {}", x0,
           x1, surface_pitch_aligned);
    x0 = std::min(x0, surface_pitch_aligned);
    x1 = std::min(x1, surface_pitch_aligned);
  }
  assert_true(x1 - x0 <= int32_t(xenos::kMaxResolveSize));

  // Clamp the height to a sane value (to make sure it can fit in the packed
  // shader constant).
  if (y1 - y0 > int32_t(xenos::kMaxResolveSize)) {
    XELOGE("Resolve region {} <= y < {} is taller than {}", y0, y1,
           xenos::kMaxResolveSize);
    y1 = y0 + int32_t(xenos::kMaxResolveSize);
  }
  // fails in forza horizon 1
  // x0 is 0, x1 is 0x100, y0 is 0x100, y1 is 0x100
  assert_true(x0 <= x1 && y0 <= y1);
  if (x0 >= x1 || y0 >= y1) {
    XELOGE("Resolve region is empty");
    return false;
  }

  info_out.coordinate_info.width_div_8 =
      uint32_t(x1 - x0) >> xenos::kResolveAlignmentPixelsLog2;
  info_out.height_div_8 =
      uint32_t(y1 - y0) >> xenos::kResolveAlignmentPixelsLog2;
  // 3 bits for each.
  assert_true(draw_resolution_scale_x <= 7);
  assert_true(draw_resolution_scale_y <= 7);
  info_out.coordinate_info.draw_resolution_scale_x = draw_resolution_scale_x;
  info_out.coordinate_info.draw_resolution_scale_y = draw_resolution_scale_y;

  // Handle the destination.
  bool is_depth =
      rb_copy_control.copy_src_select >= xenos::kMaxColorRenderTargets;
  // Get the sample selection to safely pass to the shader.
  xenos::CopySampleSelect sample_select =
      SanitizeCopySampleSelect(rb_copy_control.copy_sample_select,
                               rb_surface_info.msaa_samples, is_depth);
  if (rb_copy_control.copy_sample_select != sample_select) {
    XELOGW(
        "Incorrect resolve sample selected for {}-sample {}: {}, treating like "
        "{}",
        1 << uint32_t(rb_surface_info.msaa_samples),
        is_depth ? "depth" : "color",
        static_cast<uint32_t>(rb_copy_control.copy_sample_select),
        static_cast<uint32_t>(sample_select));
  }
  info_out.copy_dest_coordinate_info.copy_sample_select = sample_select;
  // Get the format to pass to the shader in a unified way - for depth (for
  // which Direct3D 9 specifies the k_8_8_8_8 uint destination format), make
  // sure the shader won't try to do conversion - pass proper k_24_8 or
  // k_24_8_FLOAT.
  auto rb_copy_dest_info = regs.Get<reg::RB_COPY_DEST_INFO>();
  xenos::TextureFormat dest_format;
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  if (is_depth) {
    dest_format = DepthRenderTargetToTextureFormat(rb_depth_info.depth_format);
  } else {
    dest_format = xenos::TextureFormat(rb_copy_dest_info.copy_dest_format);
    // For development feedback - not much known about these formats currently.
    xenos::TextureFormat dest_closest_format;
    switch (dest_format) {
      case xenos::TextureFormat::k_8_A:
      case xenos::TextureFormat::k_8_B:
        dest_closest_format = xenos::TextureFormat::k_8;
        break;
      case xenos::TextureFormat::k_8_8_8_8_A:
        dest_closest_format = xenos::TextureFormat::k_8_8_8_8;
        break;
      default:
        dest_closest_format = dest_format;
    }
    if (dest_format != dest_closest_format) {
      XELOGW(
          "Resolving to format {}, which is untested - treating like {}. "
          "Report the game to Xenia developers!",
          FormatInfo::GetName(dest_format),
          FormatInfo::GetName(dest_closest_format));
    }
  }

  // Calculate the destination memory extent.
  uint32_t rb_copy_dest_base = regs[XE_GPU_REG_RB_COPY_DEST_BASE];
  uint32_t copy_dest_base_adjusted = rb_copy_dest_base;
  uint32_t copy_dest_extent_start, copy_dest_extent_end;
  auto rb_copy_dest_pitch = regs.Get<reg::RB_COPY_DEST_PITCH>();
  uint32_t copy_dest_pitch_aligned_div_32 =
      (rb_copy_dest_pitch.copy_dest_pitch +
       (xenos::kTextureTileWidthHeight - 1)) >>
      xenos::kTextureTileWidthHeightLog2;
  info_out.copy_dest_coordinate_info.pitch_aligned_div_32 =
      copy_dest_pitch_aligned_div_32;
  info_out.copy_dest_coordinate_info.height_aligned_div_32 =
      (rb_copy_dest_pitch.copy_dest_height +
       (xenos::kTextureTileWidthHeight - 1)) >>
      xenos::kTextureTileWidthHeightLog2;
  const FormatInfo& dest_format_info = *FormatInfo::Get(dest_format);
  if (is_depth || dest_format_info.type == FormatType::kResolvable) {
    uint32_t bpp_log2 = xe::log2_floor(dest_format_info.bits_per_pixel >> 3);
    uint32_t dest_base_relative_x_mask =
        (UINT32_C(1) << xenos::GetTextureTiledXBaseGranularityLog2(
             bool(rb_copy_dest_info.copy_dest_array), bpp_log2)) -
        1;
    uint32_t dest_base_relative_y_mask =
        (UINT32_C(1) << xenos::GetTextureTiledYBaseGranularityLog2(
             bool(rb_copy_dest_info.copy_dest_array), bpp_log2)) -
        1;
    info_out.copy_dest_coordinate_info.offset_x_div_8 =
        (uint32_t(x0) & dest_base_relative_x_mask) >>
        xenos::kResolveAlignmentPixelsLog2;
    info_out.copy_dest_coordinate_info.offset_y_div_8 =
        (uint32_t(y0) & dest_base_relative_y_mask) >>
        xenos::kResolveAlignmentPixelsLog2;
    uint32_t dest_base_x = uint32_t(x0) & ~dest_base_relative_x_mask;
    uint32_t dest_base_y = uint32_t(y0) & ~dest_base_relative_y_mask;
    if (rb_copy_dest_info.copy_dest_array) {
      // The base pointer is already adjusted to the Z / 8 (copy_dest_slice is
      // 3-bit).
      copy_dest_base_adjusted += texture_util::GetTiledOffset3D(
          int32_t(dest_base_x), int32_t(dest_base_y), 0,
          rb_copy_dest_pitch.copy_dest_pitch,
          rb_copy_dest_pitch.copy_dest_height, bpp_log2);
      copy_dest_extent_start =
          rb_copy_dest_base +
          texture_util::GetTiledAddressLowerBound3D(
              uint32_t(x0), uint32_t(y0), rb_copy_dest_info.copy_dest_slice,
              rb_copy_dest_pitch.copy_dest_pitch,
              rb_copy_dest_pitch.copy_dest_height, bpp_log2);
      copy_dest_extent_end =
          rb_copy_dest_base +
          texture_util::GetTiledAddressUpperBound3D(
              uint32_t(x1), uint32_t(y1), rb_copy_dest_info.copy_dest_slice + 1,
              rb_copy_dest_pitch.copy_dest_pitch,
              rb_copy_dest_pitch.copy_dest_height, bpp_log2);
    } else {
      copy_dest_base_adjusted += texture_util::GetTiledOffset2D(
          int32_t(dest_base_x), int32_t(dest_base_y),
          rb_copy_dest_pitch.copy_dest_pitch, bpp_log2);
      copy_dest_extent_start =
          rb_copy_dest_base + texture_util::GetTiledAddressLowerBound2D(
                                  uint32_t(x0), uint32_t(y0),
                                  rb_copy_dest_pitch.copy_dest_pitch, bpp_log2);
      copy_dest_extent_end =
          rb_copy_dest_base + texture_util::GetTiledAddressUpperBound2D(
                                  uint32_t(x1), uint32_t(y1),
                                  rb_copy_dest_pitch.copy_dest_pitch, bpp_log2);
    }
  } else {
    XELOGE("Tried to resolve to format {}, which is not a ColorFormat",
           FormatInfo::GetName(dest_format));
    copy_dest_extent_start = copy_dest_base_adjusted;
    copy_dest_extent_end = copy_dest_base_adjusted;
  }
  assert_true(copy_dest_extent_start >= copy_dest_base_adjusted);
  assert_true(copy_dest_extent_end >= copy_dest_base_adjusted);
  assert_true(copy_dest_extent_end >= copy_dest_extent_start);
  info_out.copy_dest_base = copy_dest_base_adjusted;
  info_out.copy_dest_extent_start = copy_dest_extent_start;
  info_out.copy_dest_extent_length =
      copy_dest_extent_end - copy_dest_extent_start;

  // Offset relative to the beginning of the tile to put it in fewer bits.
  uint32_t sample_count_log2_x =
      uint32_t(rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t sample_count_log2_y =
      uint32_t(rb_surface_info.msaa_samples >= xenos::MsaaSamples::k2X);
  uint32_t x0_samples = uint32_t(x0) << sample_count_log2_x;
  uint32_t y0_samples = uint32_t(y0) << sample_count_log2_y;
  uint32_t base_offset_x_tiles = x0_samples / xenos::kEdramTileWidthSamples;
  uint32_t base_offset_y_tiles = y0_samples / xenos::kEdramTileHeightSamples;
  info_out.coordinate_info.edram_offset_x_div_8 =
      (x0_samples % xenos::kEdramTileWidthSamples) >> (sample_count_log2_x + 3);
  info_out.coordinate_info.edram_offset_y_div_8 =
      (y0_samples % xenos::kEdramTileHeightSamples) >>
      (sample_count_log2_y + 3);
  uint32_t surface_pitch_tiles = xenos::GetSurfacePitchTiles(
      rb_surface_info.surface_pitch, rb_surface_info.msaa_samples, false);
  uint32_t edram_base_offset_tiles =
      base_offset_y_tiles * surface_pitch_tiles + base_offset_x_tiles;

  // Write the color/depth EDRAM info.
  bool fill_half_pixel_offset =
      (draw_resolution_scale_x > 1 || draw_resolution_scale_y > 1) &&
      cvars::resolve_resolution_scale_fill_half_pixel_offset &&
      cvars::half_pixel_offset && !regs.Get<reg::PA_SU_VTX_CNTL>().pix_center;
  int32_t exp_bias = is_depth ? 0 : rb_copy_dest_info.copy_dest_exp_bias;
  ResolveEdramInfo depth_edram_info;
  depth_edram_info.packed = 0;
  if (is_depth || rb_copy_control.depth_clear_enable) {
    depth_edram_info.pitch_tiles = surface_pitch_tiles;
    depth_edram_info.msaa_samples = rb_surface_info.msaa_samples;
    depth_edram_info.is_depth = 1;
    // If wrapping happens, it's fine, it doesn't matter how many times and
    // where modulo xenos::kEdramTileCount is applied in this context.
    depth_edram_info.base_tiles =
        rb_depth_info.depth_base + edram_base_offset_tiles;
    depth_edram_info.format = uint32_t(rb_depth_info.depth_format);
    depth_edram_info.format_is_64bpp = 0;
    depth_edram_info.fill_half_pixel_offset = uint32_t(fill_half_pixel_offset);
    info_out.depth_original_base = rb_depth_info.depth_base;
  } else {
    info_out.depth_original_base = 0;
  }
  info_out.depth_edram_info = depth_edram_info;
  ResolveEdramInfo color_edram_info;
  color_edram_info.packed = 0;
  if (!is_depth) {
    // Color.
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[rb_copy_control
                                                    .copy_src_select]);
    uint32_t is_64bpp = uint32_t(
        xenos::IsColorRenderTargetFormat64bpp(color_info.color_format));
    color_edram_info.pitch_tiles = surface_pitch_tiles << is_64bpp;
    color_edram_info.msaa_samples = rb_surface_info.msaa_samples;
    color_edram_info.is_depth = 0;
    // If wrapping happens, it's fine, it doesn't matter how many times and
    // where modulo xenos::kEdramTileCount is applied in this context.
    color_edram_info.base_tiles =
        color_info.color_base + (edram_base_offset_tiles << is_64bpp);
    color_edram_info.format = uint32_t(color_info.color_format);
    color_edram_info.format_is_64bpp = is_64bpp;
    color_edram_info.fill_half_pixel_offset = uint32_t(fill_half_pixel_offset);
    if ((fixed_rg16_truncated_to_minus_1_to_1 &&
         color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16) ||
        (fixed_rgba16_truncated_to_minus_1_to_1 &&
         color_info.color_format ==
             xenos::ColorRenderTargetFormat::k_16_16_16_16)) {
      // The texture expects 0x8001 = -32, 0x7FFF = 32, but the hack making
      // 0x8001 = -1, 0x7FFF = 1 is used - revert (this won't be correct if the
      // requested exponent bias is 27 or above, but it's a hack anyway, no need
      // to create a new copy info structure with one more bit just for this).
      exp_bias = std::min(exp_bias + int32_t(5), int32_t(31));
    }
    info_out.color_original_base = color_info.color_base;
  } else {
    info_out.color_original_base = 0;
  }
  info_out.color_edram_info = color_edram_info;

  // Patch and write RB_COPY_DEST_INFO.
  info_out.copy_dest_info = rb_copy_dest_info;
  // Override with the depth format to make sure the shader doesn't have any
  // reason to try to do k_8_8_8_8 packing.
  info_out.copy_dest_info.copy_dest_format = xenos::ColorFormat(dest_format);
  // Handle k_16_16 and k_16_16_16_16 range.
  info_out.copy_dest_info.copy_dest_exp_bias = exp_bias;
  if (is_depth) {
    // Single component, nothing to swap.
    info_out.copy_dest_info.copy_dest_swap = false;
  }

  info_out.rb_depth_clear = regs[XE_GPU_REG_RB_DEPTH_CLEAR];
  info_out.rb_color_clear = regs[XE_GPU_REG_RB_COLOR_CLEAR];
  info_out.rb_color_clear_lo = regs[XE_GPU_REG_RB_COLOR_CLEAR_LO];

#if 0
  XELOGD(
      "Resolve: {},{} <= x,y < {},{}, {} -> {} at 0x{:08X} (potentially "
      "modified memory range 0x{:08X} to 0x{:08X})",
      x0, y0, x1, y1,
      is_depth ? xenos::GetDepthRenderTargetFormatName(
                     xenos::DepthRenderTargetFormat(depth_edram_info.format))
               : xenos::GetColorRenderTargetFormatName(
                     xenos::ColorRenderTargetFormat(color_edram_info.format)),
      FormatInfo::GetName(dest_format), rb_copy_dest_base, copy_dest_extent_start,
      copy_dest_extent_end);
#endif
  return true;
}
XE_MSVC_OPTIMIZE_REVERT()
ResolveCopyShaderIndex ResolveInfo::GetCopyShader(
    uint32_t draw_resolution_scale_x, uint32_t draw_resolution_scale_y,
    ResolveCopyShaderConstants& constants_out, uint32_t& group_count_x_out,
    uint32_t& group_count_y_out) const {
  ResolveCopyShaderIndex shader = ResolveCopyShaderIndex::kUnknown;
  bool is_depth = IsCopyingDepth();
  ResolveEdramInfo edram_info = is_depth ? depth_edram_info : color_edram_info;
  bool source_is_64bpp = !is_depth && color_edram_info.format_is_64bpp != 0;
  if (is_depth || (!copy_dest_info.copy_dest_exp_bias &&
                   xenos::IsSingleCopySampleSelected(
                       copy_dest_coordinate_info.copy_sample_select) &&
                   xenos::IsColorResolveFormatBitwiseEquivalent(
                       xenos::ColorRenderTargetFormat(color_edram_info.format),
                       xenos::ColorFormat(copy_dest_info.copy_dest_format)))) {
    if (edram_info.msaa_samples >= xenos::MsaaSamples::k4X) {
      shader = source_is_64bpp ? ResolveCopyShaderIndex::kFast64bpp4xMSAA
                               : ResolveCopyShaderIndex::kFast32bpp4xMSAA;
    } else {
      shader = source_is_64bpp ? ResolveCopyShaderIndex::kFast64bpp1x2xMSAA
                               : ResolveCopyShaderIndex::kFast32bpp1x2xMSAA;
    }
  } else {
    const FormatInfo& dest_format_info =
        *FormatInfo::Get(xenos::TextureFormat(copy_dest_info.copy_dest_format));
    switch (dest_format_info.bits_per_pixel) {
      case 8:
        shader = ResolveCopyShaderIndex::kFull8bpp;
        break;
      case 16:
        shader = ResolveCopyShaderIndex::kFull16bpp;
        break;
      case 32:
        shader = ResolveCopyShaderIndex::kFull32bpp;
        break;
      case 64:
        shader = ResolveCopyShaderIndex::kFull64bpp;
        break;
      case 128:
        shader = ResolveCopyShaderIndex::kFull128bpp;
        break;
      default:
        assert_unhandled_case(dest_format_info.bits_per_pixel);
    }
  }

  constants_out.dest_relative.edram_info = edram_info;
  constants_out.dest_relative.coordinate_info = coordinate_info;
  constants_out.dest_relative.dest_info = copy_dest_info;
  constants_out.dest_relative.dest_coordinate_info = copy_dest_coordinate_info;
  constants_out.dest_base = copy_dest_base;

  if (shader != ResolveCopyShaderIndex::kUnknown) {
    uint32_t width =
        (coordinate_info.width_div_8 << xenos::kResolveAlignmentPixelsLog2) *
        draw_resolution_scale_x;
    uint32_t height = (height_div_8 << xenos::kResolveAlignmentPixelsLog2) *
                      draw_resolution_scale_y;
    const ResolveCopyShaderInfo& shader_info =
        resolve_copy_shader_info[size_t(shader)];
    group_count_x_out = (width + ((1 << shader_info.group_size_x_log2) - 1)) >>
                        shader_info.group_size_x_log2;
    group_count_y_out = (height + ((1 << shader_info.group_size_y_log2) - 1)) >>
                        shader_info.group_size_y_log2;
  } else {
    XELOGE("No resolve copy compute shader for the provided configuration");
    assert_always();
    group_count_x_out = 0;
    group_count_y_out = 0;
  }

  return shader;
}

}  // namespace draw_util
}  // namespace gpu
}  // namespace xe