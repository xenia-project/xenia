cbuffer XeClearConstants : register(b0) {
  uint2 xe_clear_value;
};

uint2 main() : SV_Target {
  return xe_clear_value;
}
