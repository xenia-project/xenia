test_lvsl_1:
  #_ REGISTER_IN r4 0x1070
  lvsl v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1070
  #_ REGISTER_OUT v3 [00010203, 04050607, 08090A0B, 0C0D0E0F]

test_lvsl_1_constant:
  li r4, 0x1070
  lvsl v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1070
  #_ REGISTER_OUT v3 [00010203, 04050607, 08090A0B, 0C0D0E0F]

test_lvsl_2:
  #_ REGISTER_IN r4 0x1071
  lvsl v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1071
  #_ REGISTER_OUT v3 [01020304, 05060708, 090A0B0C, 0D0E0F10]

test_lvsl_2_constant:
  li r4, 0x1071
  lvsl v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1071
  #_ REGISTER_OUT v3 [01020304, 05060708, 090A0B0C, 0D0E0F10]

test_lvsl_3:
  #_ REGISTER_IN r4 0x107F
  lvsl v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x107F
  #_ REGISTER_OUT v3 [0F101112, 13141516, 1718191A, 1B1C1D1E]

test_lvsl_3_constant:
  li r4, 0x107F
  lvsl v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x107F
  #_ REGISTER_OUT v3 [0F101112, 13141516, 1718191A, 1B1C1D1E]
