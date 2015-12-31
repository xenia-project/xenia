test_divdu_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

test_divdu_1_constant:
  li r4, 1
  li r5, 2
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divdu_2:
#  #_ REGISTER_IN r4 1
#  #_ REGISTER_IN r5 0
#  divdu r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divdu_2_constant:
#  li r4, 1
#  li r5, 0
#  divdu r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

test_divdu_3:
  #_ REGISTER_IN r4 2
  #_ REGISTER_IN r5 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divdu_3_constant:
  li r4, 2
  li r5, 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divdu_4:
  #_ REGISTER_IN r4 35
  #_ REGISTER_IN r5 7
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divdu_4_constant:
  li r4, 35
  li r5, 7
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divdu_5:
  #_ REGISTER_IN r4 0
  #_ REGISTER_IN r5 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divdu_5_constant:
  li r4, 0
  li r5, 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divdu_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divdu_6_constant:
  li r4, -1
  li r5, 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divdu_7:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divdu_7_constant:
  li r4, -1
  li r5, -1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divdu_8:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divdu_8_constant:
  li r4, 1
  li r5, -1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divdu_9:
  #_ REGISTER_IN r4 0x8000000000000000
  #_ REGISTER_IN r5 -1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x8000000000000000
  #_ REGISTER_OUT r5 -1

# TODO(benvanik): integer overflow (=0)
test_divdu_9_constant:
  li r4, 1
  sldi r4, r4, 63
  li r5, -1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x8000000000000000
  #_ REGISTER_OUT r5 -1

test_divdu_10:
  #_ REGISTER_IN r0 0x1
  #_ REGISTER_IN r3 0xFFFFFFFF
  divdu. r0, r3, r0
  blr
  #_ REGISTER_OUT r0 0xFFFFFFFF
  #_ REGISTER_OUT r3 0xFFFFFFFF
  #_ REGISTER_OUT cr 0x0000000080000000

test_divdu_11:
  #_ REGISTER_IN r0 0
  #_ REGISTER_IN r3 0xFFFFFFFF
  divdu. r0, r0, r3
  blr
  #_ REGISTER_OUT r0 0
  #_ REGISTER_OUT r3 0xFFFFFFFF
  #_ REGISTER_OUT cr 0x0000000020000000
