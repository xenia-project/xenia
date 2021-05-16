#include "xenos_draw.hlsli"

struct XeHSConstantDataOutput {
  float edges[4] : SV_TessFactor;
  float inside[2] : SV_InsideTessFactor;
};

XeHSConstantDataOutput XePatchConstant(
    InputPatch<XeHSControlPointInputAdaptive, 4> xe_input_patch) {
  XeHSConstantDataOutput output = (XeHSConstantDataOutput)0;
  uint i;

  // 1.0 added to the factors according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  // (fractional_even also requires a factor of at least 2.0), to the min/max it
  // has already been added on the CPU.

  // Direct3D 12 (goes in a direction along the perimeter):
  // [0] - between U0V1 and U0V0.
  // [1] - between U0V0 and U1V0.
  // [2] - between U1V0 and U1V1.
  // [3] - between U1V1 and U0V1.
  // Xbox 360 factors go along the perimeter too according to the example of
  // edge factors in Next Generation Graphics Programming on Xbox 360.
  // However, if v0->v1... that seems to be working for triangle patches applies
  // here too, with the swizzle Xenia uses in domain shaders:
  // [0] - between U0V0 and U1V0.
  // [1] - between U1V0 and U1V1.
  // [2] - between U1V1 and U0V1.
  // [3] - between U0V1 and U0V0.
  [unroll] for (i = 0u; i < 4u; ++i) {
    output.edges[i] = xe_input_patch[(i + 3u) & 3u].edge_factor;
  }

  // On the Xbox 360, according to the presentation, the inside factor is the
  // minimum of the factors of the edges along the axis.
  // Direct3D 12:
  // [0] - along U.
  // [1] - along V.
  output.inside[0u] = min(output.edges[1u], output.edges[3u]);
  output.inside[1u] = min(output.edges[0u], output.edges[2u]);

  return output;
}

[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("XePatchConstant")]
XeHSControlPointOutput main(
    InputPatch<XeHSControlPointInputAdaptive, 4> xe_input_patch) {
  XeHSControlPointOutput output;
  // Not used with control point indices.
  output.index = 0.0f;
  return output;
}
