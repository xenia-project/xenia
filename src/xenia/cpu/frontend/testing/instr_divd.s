test_divd_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

test_divd_1_constant:
  li r4, 1
  li r5, 2
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divd_2:
#  #_ REGISTER_IN r4 1
#  #_ REGISTER_IN r5 0
#  divd r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divd_2_constant:
#  li r4, 1
#  li r5, 0
#  divd r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

test_divd_3:
  #_ REGISTER_IN r4 2
  #_ REGISTER_IN r5 1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divd_3_constant:
  li r4, 2
  li r5, 1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divd_4:
  #_ REGISTER_IN r4 35
  #_ REGISTER_IN r5 7
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divd_4_constant:
  li r4, 35
  li r5, 7
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divd_5:
  #_ REGISTER_IN r4 0
  #_ REGISTER_IN r5 1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divd_5_constant:
  li r4, 0
  li r5, 1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divd_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divd_6_constant:
  li r4, -1
  li r5, 1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divd_7:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divd_7_constant:
  li r4, -1
  li r5, -1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divd_8:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divd_8_constant:
  li r4, 1
  li r5, -1
  divd r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

# TODO(benvanik): integer overflow (=0)
#test_divd_9:
#  #_ REGISTER_IN r4 0x8000000000000000
#  #_ REGISTER_IN r5 -1
#  divd r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 0x8000000000000000
#  #_ REGISTER_OUT r5 -1

# TODO(benvanik): integer overflow (=0)
#test_divd_9_constant:
#  li r4, 1
#  sldi r4, r4, 63
#  li r5, -1
#  divd r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 0x8000000000000000
#  #_ REGISTER_OUT r5 -1
