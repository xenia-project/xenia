test_neg_1:
  #_ REGISTER_IN r3 0x0000000080000000
  neg r3, r3
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000

test_neg_1_constant:
  li r3, 1
  sldi r3, r3, 31
  neg r3, r3
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000

test_neg_2:
  #_ REGISTER_IN r3 0x8000000000000000
  neg r3, r3
  blr
  #_ REGISTER_OUT r3 0x8000000000000000

test_neg_2_constant:
  li r3, 1
  sldi r3, r3, 63
  neg r3, r3
  blr
  #_ REGISTER_OUT r3 0x8000000000000000

test_neg_3:
  #_ REGISTER_IN r3 0x0000000000000005
  neg r3, r3
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFB

test_neg_3_constant:
  li r3, 5
  neg r3, r3
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFB
