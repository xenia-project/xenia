test_extsb_1:
  #_ REGISTER_IN r4 0x0F
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F

test_extsb_1_constant:
  li r4, 0x0F
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F

test_extsb_2:
  #_ REGISTER_IN r4 0x7F
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0x7F
  #_ REGISTER_OUT r4 0x7F

test_extsb_2_constant:
  li r4, 0x7F
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0x7F
  #_ REGISTER_OUT r4 0x7F

test_extsb_3:
  #_ REGISTER_IN r4 0x80
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0x80

test_extsb_3_constant:
  li r4, 0x80
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0x80

test_extsb_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFF080
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFF080

test_extsb_4_constant:
  li r4, 0xF7F
  not r4, r4
  extsb r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFF080

test_extsb_cr_1:
  #_ REGISTER_IN r4 0x0F
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F
  #_ REGISTER_OUT r12 0x40000000

test_extsb_cr_1_constant:
  li r4, 0x0F
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F
  #_ REGISTER_OUT r12 0x40000000

test_extsb_cr_2:
  #_ REGISTER_IN r4 0x7F
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7F
  #_ REGISTER_OUT r4 0x7F
  #_ REGISTER_OUT r12 0x40000000

test_extsb_cr_2_constant:
  li r4, 0x7F
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7F
  #_ REGISTER_OUT r4 0x7F
  #_ REGISTER_OUT r12 0x40000000

test_extsb_cr_3:
  #_ REGISTER_IN r4 0x80
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0x80
  #_ REGISTER_OUT r12 0x80000000

test_extsb_cr_3_constant:
  li r4, 0x80
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0x80
  #_ REGISTER_OUT r12 0x80000000

test_extsb_cr_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFF080
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFF080
  #_ REGISTER_OUT r12 0x80000000

test_extsb_cr_4_constant:
  li r4, 0xF7F
  not r4, r4
  extsb. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFF80
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFF080
  #_ REGISTER_OUT r12 0x80000000
