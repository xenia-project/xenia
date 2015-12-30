test_mullw_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mullw_1_constant:
  li r4, 1
  li r5, 0
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mullw_2:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 1

test_mullw_2_constant:
  li r4, 1
  li r5, 1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 1

test_mullw_3:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mullw_3_constant:
  li r4, 1
  li r5, -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mullw_4:
  #_ REGISTER_IN r4 123
  #_ REGISTER_IN r5 -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -123
  #_ REGISTER_OUT r4 123
  #_ REGISTER_OUT r5 -1

test_mullw_4_constant:
  li r4, 123
  li r5, -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -123
  #_ REGISTER_OUT r4 123
  #_ REGISTER_OUT r5 -1

test_mullw_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mullw_5_constant:
  li r4, -1
  li r5, 1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mullw_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 2
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2

test_mullw_6_constant:
  li r4, -1
  li r5, 2
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2

test_mullw_7:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mullw_7_constant:
  li r4, 1
  li r5, -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mullw_8:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 -1

test_mullw_8_constant:
  li r4, -1
  li r5, -1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 -1

test_mullw_9:
  #_ REGISTER_IN r4 0xFFFFFFFF00000000
  #_ REGISTER_IN r5 1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFF00000000
  #_ REGISTER_OUT r5 1

test_mullw_9_constant:
  li r4, -1
  sldi r4, r4, 32
  li r5, 1
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFF00000000
  #_ REGISTER_OUT r5 1

test_mullw_10:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0xFFFFFFFF00000000
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFF00000000

test_mullw_10_constant:
  li r4, 1
  li r5, -1
  sldi r5, r5, 32
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFF00000000

test_mullw_11:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0x000000007FFFFFFF
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x000000007FFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_mullw_11_constant:
  li r4, 1
  li r5, -1
  clrldi r5, r5, 33
  mullw r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x000000007FFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0x000000007FFFFFFF

test_mullw_12:
  #_ REGISTER_IN r0 0x1
  #_ REGISTER_IN r8 0x0123456789ABCDEF
  mullw r0, r8, r8
  blr
  #_ REGISTER_OUT r0 0x36B1B9D890F2A521
  #_ REGISTER_OUT r8 0x0123456789ABCDEF
  blr
