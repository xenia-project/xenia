#include "byte_swap.hlsli"
#include "xenos_draw.hlsli"
ByteAddressBuffer xe_shared_memory_srv : register(t0);
RWByteAddressBuffer xe_shared_memory_uav : register(u0);

struct XeHSControlPointOutput {};

struct XeHSConstantDataOutput {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

XeHSConstantDataOutput XePatchConstant(uint xe_patch_id : SV_PrimitiveID) {
  XeHSConstantDataOutput output = (XeHSConstantDataOutput)0;

  // 1.0 added to the factors according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  // (fractional_even also requires a factor of at least 2.0), to the min/max it
  // has already been added on the CPU.

  // Due to usage of SV_PrimitiveID, and because the inside factor (join phase)
  // depends only on the edge factors (fork phase), there will be no
  // dcl_hs_fork_phase_instance_count set (all 3 edge factors will be set in the
  // same hs_fork_phase instance), so it's okay to have data calculated only
  // once for all edge factors.

  uint edge_factor_address =
      XeGetTessellationFactorAddress(xe_patch_id * 3u, 3u);
  uint3 edge_factor_data;
  [branch] if (xe_flags & XeSysFlag_SharedMemoryIsUAV) {
    edge_factor_data = xe_shared_memory_uav.Load3(edge_factor_address);
  } else {
    edge_factor_data = xe_shared_memory_srv.Load3(edge_factor_address);
  }
  edge_factor_data =
      XeByteSwap(edge_factor_data, xe_vertex_index_endian_and_edge_factors);
  float3 edge_factors = clamp(asfloat(edge_factor_data) + 1.0,
                              xe_tessellation_factor_range.x,
                              xe_tessellation_factor_range.y);
  // UVW are taken with ZYX swizzle (when r1.y is 0) in the vertex (domain)
  // shader. Edge 0 is with U = 0, edge 1 is with V = 0, edge 2 is with W = 0.
  // TODO(Triang3l): Verify this order. There are still cracks.
  output.edges[0] = edge_factors.z;
  output.edges[1] = edge_factors.y;
  output.edges[2] = edge_factors.x;
  // vpc0, vpc1, vpc2 taken as inputs to the join phase.
  output.inside = min(min(output.edges[0], output.edges[1]), output.edges[2]);

  return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("XePatchConstant")]
void main(InputPatch<XeHSControlPointOutput, 3> input_patch) {}
