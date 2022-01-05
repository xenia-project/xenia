#include "pixel_formats.hlsli"
#include "xenos_draw.hlsli"

struct XePSInput {
  XeVertexPrePS pre_ps;
  sample float4 position : SV_Position;
};

precise float main(XePSInput xe_input) : SV_DepthLessEqual {
  // Simplified conversion, always less than or equal to the original value -
  // just drop the lower bits.
  // The float32 exponent bias is 127.
  // After saturating, the exponent range is -127...0.
  // The smallest normalized 20e4 exponent is -14 - should drop 3 mantissa bits
  // at -14 or above.
  // The smallest denormalized 20e4 number is -34 - should drop 23 mantissa bits
  // at -34.
  // Anything smaller than 2^-34 becomes 0.
  // Input Z may be outside the viewport range (it's clamped after the shader).
  // Assuming that 0...0.5 on the host corresponds to 0...1 on the guest, to
  // allow for safe reinterpretation of any 24-bit value to and from float24
  // depth using depth output without unrestricted depth range.
  precise uint depth = asuint(saturate(xe_input.position.z * 2.0f));
  // Check if the number is representable as a float24 after truncation - the
  // exponent is at least -34.
  if (depth >= 0x2E800000u) {
    // Extract the biased float32 exponent:
    // 113+ at exponent -14+.
    // 93 at exponent -34.
    uint exponent = (depth >> 23u) & 0xFFu;
    // Convert exponent to the shift amount.
    // 116 - 113 = 3.
    // 116 - 93 = 23.
    uint shift = asuint(max(116 - asint(exponent), 3));
    depth = depth >> shift << shift;
  } else {
    // The number is not representable as float24 after truncation - zero.
    depth = 0u;
  }
  return asfloat(depth) * 0.5f;
}
