// Perceptual luminance for FXAA.
float XeApplyGammaGetAlpha(float3 color) {
  return dot(color, float3(0.299, 0.587, 0.114));
}
#include "apply_gamma_pwl.hlsli"
