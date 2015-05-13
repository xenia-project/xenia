test_lvsr_1:
  #_ REGISTER_IN r4 0x1070
  lvsr v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1070
  #_ REGISTER_OUT v3 [10111213, 14151617, 18191A1B, 1C1D1E1F]

test_lvsr_1_constant:
  li r4, 0x1070
  lvsr v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1070
  #_ REGISTER_OUT v3 [10111213, 14151617, 18191A1B, 1C1D1E1F]

test_lvsr_2:
  #_ REGISTER_IN r4 0x1071
  lvsr v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1071
  #_ REGISTER_OUT v3 [0F101112, 13141516, 1718191A, 1B1C1D1E]

test_lvsr_2_constant:
  li r4, 0x1071
  lvsr v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x1071
  #_ REGISTER_OUT v3 [0F101112, 13141516, 1718191A, 1B1C1D1E]

test_lvsr_3:
  #_ REGISTER_IN r4 0x107F
  lvsr v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x107F
  #_ REGISTER_OUT v3 [01020304, 05060708, 090A0B0C, 0D0E0F10]

test_lvsr_3_constant:
  li r4, 0x107F
  lvsr v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x107F
  #_ REGISTER_OUT v3 [01020304, 05060708, 090A0B0C, 0D0E0F10]
