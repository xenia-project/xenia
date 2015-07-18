test_extsh_1:
  #_ REGISTER_IN r4 0x0F
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F

test_extsh_1_constant:
  li r4, 0x0F
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F

test_extsh_2:
  #_ REGISTER_IN r4 0x7FFF
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0x7FFF
  #_ REGISTER_OUT r4 0x7FFF

test_extsh_2_constant:
  li r4, 0x7FFF
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0x7FFF
  #_ REGISTER_OUT r4 0x7FFF

test_extsh_3:
  #_ REGISTER_IN r4 0x8000
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0x8000

test_extsh_3_constant:
  li r4, 0x80
  sldi r4, r4, 8
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0x8000

test_extsh_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFF08000
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0xFFFFFFFFFFF08000

test_extsh_4_constant:
  li r4, 0xF7F
  not r4, r4
  sldi r4, r4, 8
  extsh r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0xFFFFFFFFFFF08000

test_extsh_cr_1:
  #_ REGISTER_IN r4 0x0F
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F
  #_ REGISTER_OUT r12 0x40000000

test_extsh_cr_1_constant:
  li r4, 0x0F
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F
  #_ REGISTER_OUT r12 0x40000000

test_extsh_cr_2:
  #_ REGISTER_IN r4 0x7FFF
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7FFF
  #_ REGISTER_OUT r4 0x7FFF
  #_ REGISTER_OUT r12 0x40000000

test_extsh_cr_2_constant:
  li r4, 0x7FFF
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7FFF
  #_ REGISTER_OUT r4 0x7FFF
  #_ REGISTER_OUT r12 0x40000000

test_extsh_cr_3:
  #_ REGISTER_IN r4 0x8000
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0x8000
  #_ REGISTER_OUT r12 0x80000000

test_extsh_cr_3_constant:
  li r4, 0x80
  sldi r4, r4, 8
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0x8000
  #_ REGISTER_OUT r12 0x80000000

test_extsh_cr_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFF08000
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0xFFFFFFFFFFF08000
  #_ REGISTER_OUT r12 0x80000000

test_extsh_cr_4_constant:
  li r4, 0xF7F
  not r4, r4
  sldi r4, r4, 8
  extsh. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF8000
  #_ REGISTER_OUT r4 0xFFFFFFFFFFF08000
  #_ REGISTER_OUT r12 0x80000000
