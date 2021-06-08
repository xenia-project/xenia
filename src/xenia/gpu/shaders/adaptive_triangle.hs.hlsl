#include "xenos_draw.hlsli"

struct XeHSConstantDataOutput {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

XeHSConstantDataOutput XePatchConstant(
    InputPatch<XeHSControlPointInputAdaptive, 3> xe_input_patch) {
  XeHSConstantDataOutput output = (XeHSConstantDataOutput)0;
  uint i;

  // Factors for adaptive tessellation are taken from the index buffer.

  // 1.0 added to the factors according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  // (fractional_even also requires a factor of at least 2.0), to the min/max it
  // has already been added on the CPU.

  // Fork phase.
  // It appears that on the Xbox 360:
  // - [0] is the factor for the v0->v1 edge.
  // - [1] is the factor for the v1->v2 edge.
  // - [2] is the factor for the v2->v0 edge.
  // Where v0 is the U1V0W0 vertex, v1 is the U0V1W0 vertex, and v2 is the
  // U0V0W1 vertex.
  // The hint at the order was provided in the Code Listing 15 of:
  // http://www.uraldev.ru/files/download/21/Real-Time_Tessellation_on_GPU.pdf
  // In Direct3D 12:
  // - [0] is the factor for the U0 edge (v1->v2).
  // - [1] is the factor for the V0 edge (v2->v0),
  // - [2] is the factor for the W0 edge (v0->v1).
  // Direct3D 12 provides barycentrics as X for v0, Y for v1, Z for v2.
  // In Xenia's domain shaders, the barycentric coordinates are handled as:
  // 1) vDomain.xyz -> r0.zyx by Xenia.
  // 2) r0.zyx -> r0.zyx by the guest (because r1.y is set to 0 by Xenia, which
  //    apparently means identity swizzle to games).
  // 3) r0.z * v0 + r0.y * v1 + r0.x * v2 by the guest.
  // With this order, there are no cracks in Halo 3 water.
  [unroll] for (i = 0u; i < 3u; ++i) {
    output.edges[i] = xe_input_patch[(i + 1u) % 3u].edge_factor;
  }

  // Join phase. vpc0, vpc1, vpc2 taken as inputs.
  output.inside =
      min(min(output.edges[0u], output.edges[1u]), output.edges[2u]);

  return output;
}

[domain("tri")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("XePatchConstant")]
XeHSControlPointOutput main(
    InputPatch<XeHSControlPointInputAdaptive, 3> xe_input_patch) {
  XeHSControlPointOutput output;
  // Not used with control point indices.
  output.index = 0.0f;
  return output;
}
