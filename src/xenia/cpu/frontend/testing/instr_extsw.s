test_extsw_1:
  #_ REGISTER_IN r4 0x0F
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F

test_extsw_1_constant:
  li r4, 0x0F
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F

test_extsw_2:
  #_ REGISTER_IN r4 0x7FFFFFFF
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFF
  #_ REGISTER_OUT r4 0x7FFFFFFF

test_extsw_2_constant:
  lis r4, 0x7FFF
  ori r4, r4, 0xFFFF
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFF
  #_ REGISTER_OUT r4 0x7FFFFFFF

test_extsw_3:
  #_ REGISTER_IN r4 0x80000000
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0x80000000

test_extsw_3_constant:
  li r4, 0x80
  sldi r4, r4, 24
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0x80000000

test_extsw_4:
  #_ REGISTER_IN r4 0xFFFFFFF080000000
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0xFFFFFFF080000000

test_extsw_4_constant:
  li r4, 0xF7F
  not r4, r4
  sldi r4, r4, 24
  extsw r3, r4
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0xFFFFFFF080000000

test_extsw_cr_1:
  #_ REGISTER_IN r4 0x0F
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F
  #_ REGISTER_OUT r12 0x40000000

test_extsw_cr_1_constant:
  li r4, 0x0F
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0F
  #_ REGISTER_OUT r4 0x0F
  #_ REGISTER_OUT r12 0x40000000

test_extsw_cr_2:
  #_ REGISTER_IN r4 0x7FFFFFFF
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFF
  #_ REGISTER_OUT r4 0x7FFFFFFF
  #_ REGISTER_OUT r12 0x40000000

test_extsw_cr_2_constant:
  lis r4, 0x7FFF
  ori r4, r4, 0xFFFF
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFF
  #_ REGISTER_OUT r4 0x7FFFFFFF
  #_ REGISTER_OUT r12 0x40000000

test_extsw_cr_3:
  #_ REGISTER_IN r4 0x80000000
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0x80000000
  #_ REGISTER_OUT r12 0x80000000

test_extsw_cr_3_constant:
  li r4, 0x80
  sldi r4, r4, 24
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0x80000000
  #_ REGISTER_OUT r12 0x80000000

test_extsw_cr_4:
  #_ REGISTER_IN r4 0xFFFFFFF080000000
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0xFFFFFFF080000000
  #_ REGISTER_OUT r12 0x80000000

test_extsw_cr_4_constant:
  li r4, 0xF7F
  not r4, r4
  sldi r4, r4, 24
  extsw. r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0xFFFFFFF080000000
  #_ REGISTER_OUT r12 0x80000000
