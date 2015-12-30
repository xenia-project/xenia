test_divwu_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

test_divwu_1_constant:
  li r4, 1
  li r5, 2
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divwu_2:
#  #_ REGISTER_IN r4 1
#  #_ REGISTER_IN r5 0
#  divwu r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divwu_2_constant:
#  li r4, 1
#  li r5, 0
#  divwu r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

test_divwu_3:
  #_ REGISTER_IN r4 2
  #_ REGISTER_IN r5 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divwu_3_constant:
  li r4, 2
  li r5, 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divwu_4:
  #_ REGISTER_IN r4 35
  #_ REGISTER_IN r5 7
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divwu_4_constant:
  li r4, 35
  li r5, 7
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divwu_5:
  #_ REGISTER_IN r4 0
  #_ REGISTER_IN r5 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divwu_5_constant:
  li r4, 0
  li r5, 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divwu_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divwu_6_constant:
  li r4, -1
  li r5, 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divwu_7:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divwu_7_constant:
  li r4, -1
  li r5, -1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divwu_8:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divwu_8_constant:
  li r4, 1
  li r5, -1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divwu_9:
  #_ REGISTER_IN r4 0x000000007FFFFFFF
  #_ REGISTER_IN r5 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x000000007FFFFFFF
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 1

test_divwu_9_constant:
  li r4, -1
  clrldi r4, r4, 33
  li r5, 1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x000000007FFFFFFF
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 1

test_divwu_10:
  #_ REGISTER_IN r4 0x000000007FFFFFFF
  #_ REGISTER_IN r5 0x000000007FFFFFFF
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divwu_10_constant:
  li r4, -1
  clrldi r4, r4, 33
  mr r5, r4
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0x000000007FFFFFFF
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divwu_11:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0x000000007FFFFFFF
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divwu_11_constant:
  li r4, 1
  li r5, -1
  clrldi r5, r5, 33
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_divwu_12:
  #_ REGISTER_IN r4 0x80000000
  #_ REGISTER_IN r5 -1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x80000000
  #_ REGISTER_OUT r5 -1

test_divwu_12_constant:
  li r4, 1
  sldi r4, r4, 31
  li r5, -1
  divwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x80000000
  #_ REGISTER_OUT r5 -1

test_divwu_13:
  #_ REGISTER_IN r0 0x1
  #_ REGISTER_IN r3 0xFFFFFFFF
  divwu. r0, r3, r0
  blr
  #_ REGISTER_OUT r0 0xFFFFFFFF
  #_ REGISTER_OUT r3 0xFFFFFFFF
  #_ REGISTER_OUT cr 0x0000000080000000

test_divwu_14:
  #_ REGISTER_IN r0 0x1
  #_ REGISTER_IN r3 0xFFFFFFFF
  divwu. r0, r0, r3
  blr
  #_ REGISTER_OUT r0 0
  #_ REGISTER_OUT r3 0xFFFFFFFF
  #_ REGISTER_OUT cr 0x0000000020000000
