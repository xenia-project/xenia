test_mulhdu_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mulhdu_1_constant:
  li r4, 1
  li r5, 0
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mulhdu_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhdu_2_constant:
  li r4, -1
  li r5, 1
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhdu_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 2
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2

test_mulhdu_3_constant:
  li r4, -1
  li r5, 2
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2

test_mulhdu_4:
  #_ REGISTER_IN r4 0x8000000000000000
  #_ REGISTER_IN r5 1
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x8000000000000000
  #_ REGISTER_OUT r5 1

test_mulhdu_4_constant:
  li r5, 1
  sldi r4, r5, 63
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x8000000000000000
  #_ REGISTER_OUT r5 1

test_mulhdu_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_mulhdu_5_constant:
  li r4, -1
  li r5, -1
  mulhdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
