test_vrfin_1:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9 -> 1.0, 2.0, 1.0, 2.0
  vrfin v3, v3
  blr
  #_ REGISTER_OUT v3 [3f800000, 40000000, 3f800000, 40000000]

test_vrfin_2:
  #_ REGISTER_IN v3 [bf800000, bfc00000, bf8ccccd, bff33333]
  # -1.0, -1.5, -1.1, -1.9 -> -1.0, -2.0, -1.0, -2.0
  vrfin v3, v3
  blr
  #_ REGISTER_OUT v3 [bf800000, c0000000, bf800000, c0000000]
