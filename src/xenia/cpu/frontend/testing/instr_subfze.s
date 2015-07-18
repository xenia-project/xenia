test_subfze_one_ca_1:
  #_ REGISTER_IN r10 0x00000000000103BF
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xfffffffffffefc41
  #_ REGISTER_OUT r4 0

test_subfze_one_ca_1_constant:
  lis r10, 1
  ori r10, r10, 0x03BF
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xfffffffffffefc41
  #_ REGISTER_OUT r4 0

test_subfze_one_ca_2:
  #_ REGISTER_IN r10 0
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfze_one_ca_2_constant:
  li r10, 0
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_subfze_one_ca_3:
  #_ REGISTER_IN r10 1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_subfze_one_ca_3_constant:
  li r10, 1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_subfze_one_ca_4:
  #_ REGISTER_IN r10 0xFFFFFFFFFFFFFFFF
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0x1
  #_ REGISTER_OUT r4 0

test_subfze_one_ca_4_constant:
  li r10, -1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0x1
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_1:
  #_ REGISTER_IN r10 0x00000000000103BF
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xfffffffffffefc40
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_1_constant:
  lis r10, 1
  ori r10, r10, 0x03BF
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r3 0xfffffffffffefc40
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_2:
  #_ REGISTER_IN r10 0
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_2_constant:
  li r10, 0
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_3:
  #_ REGISTER_IN r10 1
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_3_constant:
  li r10, 1
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_4:
  #_ REGISTER_IN r10 0xFFFFFFFFFFFFFFFF
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0

test_subfze_zero_ca_4_constant:
  li r10, -1
  xor r3, r3, r3
  addic r3, r3, 1
  subfze r3, r10
  adde r4, r0, r0
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
