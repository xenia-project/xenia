test_andi_cr_1:
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  andi. r11, r5, 0xCAFE
  mfcr r12
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0x0000CAFE
  #_ REGISTER_OUT r12 0x40000000

test_andi_cr_1_constant:
  li r5, -1
  andi. r11, r5, 0xCAFE
  mfcr r12
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0x0000CAFE
  #_ REGISTER_OUT r12 0x40000000

test_andi_cr_2:
 #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
 andi. r11, r5, 0
 mfcr r12
 blr
 #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
 #_ REGISTER_OUT r11 0
 #_ REGISTER_OUT r12 0x20000000

test_andi_cr_2_constant:
 #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
 andi. r11, r5, 0
 mfcr r12
 blr
 #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
 #_ REGISTER_OUT r11 0
 #_ REGISTER_OUT r12 0x20000000

test_andi_cr_3:
  #_ REGISTER_IN r5 0
  andi. r11, r5, 0xFFFF
  mfcr r12
  blr
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r12 0x20000000

test_andi_cr_3_constant:
  li r5, 0
  andi. r11, r5, 0xFFFF
  mfcr r12
  blr
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r11 0
  #_ REGISTER_OUT r12 0x20000000

test_andi_cr_4:
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  andi. r11, r5, 0xCAFE
  mfcr r12
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0x0000CAFE
  #_ REGISTER_OUT r12 0x40000000

test_andi_cr_4_constant:
  li r5, -1
  andi. r11, r5, 0xCAFE
  mfcr r12
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r11 0x0000CAFE
  #_ REGISTER_OUT r12 0x40000000

test_andi_cr_5:
  #_ REGISTER_IN r0 0x100000FF
  andi. r11, r0, 0xFFFF
  mfcr r12
  blr
  #_ REGISTER_OUT r0 0x100000FF
  #_ REGISTER_OUT r11 0x000000FF
  #_ REGISTER_OUT r12 0x40000000

test_andi_cr_5_constant:
  lis r0, 0x1000
  ori r0, r0, 0xFF
  andi. r11, r0, 0xFFFF
  mfcr r12
  blr
  #_ REGISTER_OUT r0 0x100000FF
  #_ REGISTER_OUT r11 0x000000FF
  #_ REGISTER_OUT r12 0x40000000
