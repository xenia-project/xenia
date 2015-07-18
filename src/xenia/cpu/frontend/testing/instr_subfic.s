test_subfic_1:
  #_ REGISTER_IN r10 0x00000000000103BF
  subfic r3, r10, 0x3C0
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xffffffffffff0001
  #_ REGISTER_OUT r4 0

test_subfic_1_constant:
  lis r10, 1
  ori r10, r10, 0x03BF
  subfic r3, r10, 0x3C0
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xffffffffffff0001
  #_ REGISTER_OUT r4 0

test_subfic_2:
  #_ REGISTER_IN r10 0x00000000000103BF
  subfic r3, r10, -234
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xfffffffffffefb57
  #_ REGISTER_OUT r4 1

test_subfic_2_constant:
  lis r10, 1
  ori r10, r10, 0x03BF
  subfic r3, r10, -234
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xfffffffffffefb57
  #_ REGISTER_OUT r4 1

test_subfic_3:
  #_ REGISTER_IN r10 0
  subfic r3, r10, 0
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfic_3_constant:
  li r10, 0
  subfic r3, r10, 0
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfic_4:
  #_ REGISTER_IN r10 1
  subfic r3, r10, 0
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_subfic_4_constant:
  li r10, 1
  subfic r3, r10, 0
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_subfic_5:
  #_ REGISTER_IN r10 0
  subfic r3, r10, 1
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1

test_subfic_5_constant:
  li r10, 0
  subfic r3, r10, 1
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1

test_subfic_6:
  #_ REGISTER_IN r10 0xFFFFFFFFFFFFFFFF
  subfic r3, r10, -1
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfic_6_constant:
  li r10, -1
  subfic r3, r10, -1
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
