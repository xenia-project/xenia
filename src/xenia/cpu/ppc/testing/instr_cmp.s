test_cmpd_1:
  #_ REGISTER_IN r3 0x0000000100000000
  #_ REGISTER_IN r4 0x0000000200000000
  cmpd r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x80000000

test_cmpd_1_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  cmpd r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x80000000

test_cmpd_2:
  #_ REGISTER_IN r3 0x0000000200000000
  #_ REGISTER_IN r4 0x0000000100000000
  cmpd r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpd_2_constant:
  li r4, 1
  sldi r4, r4, 32
  sldi r3, r4, 1
  cmpd r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpw_1:
  #_ REGISTER_IN r3 0x0000000100000000
  #_ REGISTER_IN r4 0x0000000200000000
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x20000000

test_cmpw_1_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x20000000

test_cmpw_2:
  #_ REGISTER_IN r3 0x0000000200000000
  #_ REGISTER_IN r4 0x0000000100000000
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x20000000

test_cmpw_2_constant:
  li r4, 1
  sldi r4, r4, 32
  sldi r3, r4, 1
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x20000000

test_cmpw_3:
  #_ REGISTER_IN r3 1
  #_ REGISTER_IN r4 2
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x80000000

test_cmpw_3_constant:
  li r3, 1
  li r4, 2
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x80000000

test_cmpw_4:
  #_ REGISTER_IN r3 2
  #_ REGISTER_IN r4 1
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x40000000

test_cmpw_4_constant:
  li r3, 2
  li r4, 1
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x40000000

test_cmpw_5:
  #_ REGISTER_IN r3 0x0000000100000002
  #_ REGISTER_IN r4 0x0000000200000001
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r4 0x0000000200000001
  #_ REGISTER_OUT r12 0x40000000

test_cmpw_5_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  addi r3, r3, 2
  addi r4, r4, 1
  cmpw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r4 0x0000000200000001
  #_ REGISTER_OUT r12 0x40000000

test_cmp_1:
  #_ REGISTER_IN r3 1
  #_ REGISTER_IN r4 2
  cmp 5, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x00000800

test_cmp_1_constant:
  li r3, 1
  li r4, 2
  cmp 5, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x00000800

test_cmp_2:
  #_ REGISTER_IN r3 2
  #_ REGISTER_IN r4 1
  cmp 3, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x00040000

test_cmp_2_constant:
  li r3, 2
  li r4, 1
  cmp 3, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x00040000
