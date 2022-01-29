// float XeApplyGammaGetAlpha(float3 color) needs to be specified externally.

cbuffer XeApplyGammaRampConstants : register(b0) {
  uint2 xe_apply_gamma_size;
};
RWTexture2D<unorm float4> xe_apply_gamma_dest : register(u0);
Texture2D<float3> xe_apply_gamma_source : register(t0);
Buffer<uint2> xe_apply_gamma_ramp : register(t1);

float XeApplyPWLGamma(uint input, uint component) {
  // TODO(Triang3l): If this is ever used for gamma other than 128 entries for a
  // 10bpc front buffer, handle the increment from DC_LUTA/B_CONTROL. Currently
  // assuming it's 2^3 = 8, or 1024 / 128.
  // output = base + (multiplier * delta) / increment
  // https://developer.amd.com/wordpress/media/2012/10/RRG-216M56-03oOEM.pdf
  // The lower 6 bits of the base and the delta are 0 (though enforcing that in
  // the shader is not necessary).
  // The `(multiplier * delta) / increment` part may result in a nonzero value
  // in the lower 6 bits of the result, however, so doing `* (1.0f / 64.0f)`
  // instead of `>> 6` to preserve them (if the render target is 16bpc rather
  // than 10bpc, for instance).
  uint2 ramp_value = xe_apply_gamma_ramp[(input >> 3u) * 3u + component];
  return saturate((float(ramp_value.x) +
                   float((input & 7u) * ramp_value.y) * (1.0f / 8.0f)) *
                  (1.0f / (64.0f * 1023.0f)));
}

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  [branch] if (any(xe_thread_id.xy >= xe_apply_gamma_size)) {
    return;
  }
  // UNORM conversion according to the Direct3D 10+ rules.
  uint3 input = uint3(xe_apply_gamma_source[xe_thread_id.xy] * 1023.0f + 0.5f);
  // The ramp is BGR, not RGB.
  float3 output = float3(XeApplyPWLGamma(input.r, 2u),
                         XeApplyPWLGamma(input.g, 1u),
                         XeApplyPWLGamma(input.b, 0u));
  xe_apply_gamma_dest[xe_thread_id.xy] =
      float4(output, XeApplyGammaGetAlpha(output));
}
