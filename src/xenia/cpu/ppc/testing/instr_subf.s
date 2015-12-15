test_subf_1:
  #_ REGISTER_IN r10 0x00000000000103BF
  #_ REGISTER_IN r11 0x00000000000103C0
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r11 0x00000000000103C0
  #_ REGISTER_OUT r3 0x1

test_subf_1_constant:
  lis r10, 1
  ori r10, r10, 0x03BF
  lis r11, 1
  ori r11, r11, 0x03C0
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0x00000000000103BF
  #_ REGISTER_OUT r11 0x00000000000103C0
  #_ REGISTER_OUT r3 0x1

test_subf_2:
  #_ REGISTER_IN r10 0
  #_ REGISTER_IN r11 0
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 0

test_subf_2_constant:
  li r10, 0
  li r11, 0
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 0

test_subf_3:
  #_ REGISTER_IN r10 1
  #_ REGISTER_IN r11 0
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 -1

test_subf_3_constant:
  li r10, 1
  li r11, 0
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 1
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r3 -1

test_subf_4:
  #_ REGISTER_IN r10 0
  #_ REGISTER_IN r11 1
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 1
  #_ REGISTER_OUT r3 1

test_subf_4_constant:
  li r10, 0
  li r11, 1
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0
  #_ REGISTER_OUT r11 1
  #_ REGISTER_OUT r3 1

test_subf_5:
  #_ REGISTER_IN r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r11 0xFFFFFFFFFFFFFFFF
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0x0

test_subf_5_constant:
  li r10, -1
  li r11, -1
  subf r3, r10, r11
  blr
  #_ REGISTER_OUT r10 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r3 0x0
