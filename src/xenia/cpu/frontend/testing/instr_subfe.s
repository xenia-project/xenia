test_subfe_1:
  #_ REGISTER_IN r10 0x00000000000103BF
  #_ REGISTER_IN r11 0x00000000000103C0
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r11 0x00000000000103C0
  #_ REGISTER_OUT r3 0x0
  #_ REGISTER_OUT r4 1

test_subfe_1_constant:
  lis r10, 1
  ori r10, r10, 0x03BF
  lis r11, 1
  ori r11, r11, 0x03C0
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r11 0x00000000000103C0
  #_ REGISTER_OUT r3 0x0
  #_ REGISTER_OUT r4 1

test_subfe_2:
  #_ REGISTER_IN r10 0
  #_ REGISTER_IN r11 0
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0

test_subfe_2_constant:
  li r10, 0
  li r11, 0
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0

test_subfe_3:
  #_ REGISTER_IN r10 1
  #_ REGISTER_IN r11 0
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0

test_subfe_3_constant:
  li r10, 1
  li r11, 0
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0

test_subfe_4:
  #_ REGISTER_IN r10 0
  #_ REGISTER_IN r11 1
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 1
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfe_4_constant:
  li r10, 0
  li r11, 1
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 1
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfe_5:
  #_ REGISTER_IN r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r11 0xFFFFFFFFFFFFFFFF
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0

test_subfe_5_constant:
  li r10, -1
  li r11, -1
  subfe r3, r10, r11
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0
