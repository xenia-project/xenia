#include "pixel_formats.hlsli"
#include "xenos_draw.hlsli"

struct XePSInput {
  XeVertexPrePS pre_ps;
  sample float4 position : SV_Position;
};

precise float main(XePSInput xe_input) : SV_Depth {
  // Input Z may be outside the viewport range (it's clamped after the shader).
  // Assuming that 0...0.5 on the host corresponds to 0...1 on the guest, to
  // allow for safe reinterpretation of any 24-bit value to and from float24
  // depth using depth output without unrestricted depth range.
  return asfloat(XeFloat20e4To32(
      XeFloat32To20e4(asuint(saturate(xe_input.position.z * 2.0f))), true));
}
