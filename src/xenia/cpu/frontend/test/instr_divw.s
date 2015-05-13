test_divw_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

test_divw_1_constant:
  li r4, 1
  li r5, 2
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divw_2:
#  #_ REGISTER_IN r4 1
#  #_ REGISTER_IN r5 0
#  divw r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divw_2_constant:
#  li r4, 1
#  li r5, 0
#  divw r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

test_divw_3:
  #_ REGISTER_IN r4 2
  #_ REGISTER_IN r5 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divw_3_constant:
  li r4, 2
  li r5, 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divw_4:
  #_ REGISTER_IN r4 35
  #_ REGISTER_IN r5 7
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divw_4_constant:
  li r4, 35
  li r5, 7
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divw_5:
  #_ REGISTER_IN r4 0
  #_ REGISTER_IN r5 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divw_5_constant:
  li r4, 0
  li r5, 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divw_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divw_6_constant:
  li r4, -1
  li r5, 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divw_7:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divw_7_constant:
  li r4, -1
  li r5, -1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divw_8:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divw_8_constant:
  li r4, 1
  li r5, -1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divw_9:
  #_ REGISTER_IN r4 0x000000007FFFFFFF
  #_ REGISTER_IN r5 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x000000007FFFFFFF
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 1

test_divw_9_constant:
  li r4, -1
  clrldi r4, r4, 33
  li r5, 1
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x000000007FFFFFFF
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 1

test_divw_10:
  #_ REGISTER_IN r4 0x000000007FFFFFFF
  #_ REGISTER_IN r5 0x000000007FFFFFFF
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divw_10_constant:
  li r4, -1
  clrldi r4, r4, 33
  mr r5, r4
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divw_11:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0x000000007FFFFFFF
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divw_11_constant:
  li r4, 1
  li r5, -1
  clrldi r5, r5, 33
  divw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

# TODO(benvanik): integer overflow (=0)
#test_divw_12:
#  #_ REGISTER_IN r4 0x80000000
#  #_ REGISTER_IN r5 -1
#  divw r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 0x80000000
#  #_ REGISTER_OUT r5 -1

# TODO(benvanik): integer overflow (=0)
#test_divw_12_constant:
#  li r4, 1
#  srdi r4, 31
#  li r5, -1
#  divw r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 0x80000000
#  #_ REGISTER_OUT r5 -1
