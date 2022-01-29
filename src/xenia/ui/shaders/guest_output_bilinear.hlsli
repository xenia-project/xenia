#if XE_GUEST_OUTPUT_DITHER
  #include "dither_8bpc.xesli"
#endif  // XE_GUEST_OUTPUT_DITHER

cbuffer XeBilinearConstants : register(b0) {
  int2 xe_bilinear_output_offset;
  float2 xe_bilinear_output_size_inv;
};

Texture2D<float3> xe_texture : register(t0);
SamplerState xe_sampler_linear_clamp : register(s0);

float4 main(float4 xe_position : SV_Position) : SV_Target {
  uint2 pixel_coord = uint2(int2(xe_position.xy) - xe_bilinear_output_offset);
  // + 0.5 so the origin is at the pixel center, and at 1:1 the original pixel
  // is taken.
  // Interpolating the four colors in the perceptual space because doing it in
  // linear space causes, in particular, bright text on a dark background to
  // become too thick, and aliasing of bright parts on top of dark areas to be
  // too apparent (4D5307E6 HUD, for example, mainly the edges of the
  // multiplayer score bars).
  float3 color = xe_texture.SampleLevel(
      xe_sampler_linear_clamp,
      (float2(pixel_coord) + 0.5f) * xe_bilinear_output_size_inv,
      0.0f);
  #if XE_GUEST_OUTPUT_DITHER
    // Not clamping because a normalized format is explicitly requested on DXGI.
    color += XeDitherOffset8bpc(pixel_coord);
  #endif  // XE_GUEST_OUTPUT_DITHER
  return float4(color, 1.0f);
}
