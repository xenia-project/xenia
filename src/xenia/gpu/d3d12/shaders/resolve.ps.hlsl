cbuffer XeResolveConstants : register(b0) {
  // In samples.
  // Left and top in the lower 16 bits, width and height in the upper.
  uint2 xe_resolve_rect_samples;
  // In samples. Width in the lower 16 bits, height in the upper.
  uint xe_resolve_source_size;
  // 0 - log2(vertical sample count), 0 for 1x AA, 1 for 2x/4x AA.
  // 1 - log2(horizontal sample count), 0 for 1x/2x AA, 1 for 4x AA.
  // 2:3 - vertical sample position:
  //       0 for the upper samples with 2x/4x AA.
  //       1 for 1x AA or to mix samples with 2x/4x AA.
  //       2 for the lower samples with 2x/4x AA.
  // 4:5 - horizontal sample position:
  //       0 for the left samples with 4x AA.
  //       1 for 1x/2x AA or to mix samples with 4x AA.
  //       2 for the right samples with 4x AA.
  // 6 - whether to apply the hack and duplicate the top/left
  //     half-row/half-column to reduce the impact of half-pixel offset with
  //     2x resolution scale.
  // 7:12 - exponent bias.
  uint xe_resolve_info;
};

Texture2D<float4> xe_resolve_source : register(t0);
SamplerState xe_resolve_sampler : register(s0);

float4 main(float4 xe_position : SV_Position) : SV_Target {
  // The source texture dimensions are snapped to 80x16 samples for ease of
  // EDRAM loading, but resolving may be done from different regions within it.
  // The viewport and the quad have the size of the needed region, but the
  // viewport starts at (0,0), so texture sample positions need to be offset.
  // Also because there may be excess texels in the source, clamping needs to be
  // done manually so those pixels won't effect bilinear filtering. Resolving is
  // done without stretching, but the resolve region on the source texture is 2
  // or 4 times bigger than the destination - bilinear filtering is used to mix
  // the samples (if exact samples are needed, the source texture is sampled at
  // texel centers, if AA is resolved, it's sampled between texels).

  // Go to sample coordinates and select the needed samples.
  float2 resolve_position =
      max(xe_position.xy, (float((xe_resolve_info >> 6u) & 1u) + 0.5).xx) *
      float2(((xe_resolve_info.xx >> uint2(1u, 0u)) & 1u) + 1u) +
      (float2((xe_resolve_info.xx >> uint2(4u, 2u)) & 3u) * 0.5 - 0.5);

  // Clamp, offset and normalize the position.
  resolve_position = 
      min(resolve_position, float2(xe_resolve_rect_samples >> 16u) - 0.5) +
      float2(xe_resolve_rect_samples & 0xFFFFu);
  resolve_position /=
      float2((xe_resolve_source_size >> uint2(0u, 16u)) & 0xFFFFu);

  // Resolve the samples.
  float4 pixel = xe_resolve_source.SampleLevel(xe_resolve_sampler,
                                               resolve_position, 0.0);

  // Bias the exponent.
  pixel *= exp2(float(int(xe_resolve_info << (32u - 13u)) >> 26));

  return pixel;
}
