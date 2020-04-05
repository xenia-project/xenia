#include "xenos_draw.hlsli"

struct XeHSConstantDataOutput {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

struct XeAdaptiveHSControlPointOutput {};

XeHSConstantDataOutput XePatchConstant(
    InputPatch<XeHSControlPointInput, 3> xe_input_patch) {
  XeHSConstantDataOutput output = (XeHSConstantDataOutput)0;
  uint i;

  // Factors for adaptive tessellation are taken from the index buffer.

  // 1.0 added to the factors according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  // (fractional_even also requires a factor of at least 2.0), to the min/max it
  // has already been added on the CPU.

  // Fork phase.
  // UVW are taken with ZYX swizzle (when r1.y is 0) in the vertex (domain)
  // shader. Edge 0 is with U = 0, edge 1 is with V = 0, edge 2 is with W = 0.
  // TODO(Triang3l): Verify this order. There are still cracks.
  [unroll] for (i = 0u; i < 3u; ++i) {
    output.edges[i] =
        clamp(asfloat(xe_input_patch[2u - i].index_or_edge_factor) + 1.0f,
              xe_tessellation_factor_range.x, xe_tessellation_factor_range.y);
  }

  // Join phase. vpc0, vpc1, vpc2 taken as inputs.
  output.inside = min(min(output.edges[0], output.edges[1]), output.edges[2]);

  return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("XePatchConstant")]
XeAdaptiveHSControlPointOutput main(
    InputPatch<XeHSControlPointInput, 3> xe_input_patch) {
  XeAdaptiveHSControlPointOutput output;
  return output;
}
