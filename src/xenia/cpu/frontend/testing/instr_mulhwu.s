test_mulhwu_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mulhwu_1_constant:
  li r4, 1
  li r5, 0
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mulhwu_2:
  #_ REGISTER_IN r4 0x00000000FFFFFFFF
  #_ REGISTER_IN r5 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x00000000FFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_2_constant:
  li r4, -1
  clrldi r4, r4, 32
  li r5, 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x00000000FFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_3:
  #_ REGISTER_IN r4 0x00000001FFFFFFFF
  #_ REGISTER_IN r5 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x00000001FFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_3_constant:
  li r4, -1
  clrldi r4, r4, 31
  li r5, 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x00000001FFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_4:
  #_ REGISTER_IN r4 0x800000007FFFFFFF
  #_ REGISTER_IN r5 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x800000007FFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_4_constant:
  li r4, -1
  clrldi r4, r4, 33
  li r5, 1
  sldi r5, r5, 63
  or r4, r4, r5
  li r5, 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x800000007FFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_5_constant:
  li r4, -1
  li r5, 1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulhwu_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_mulhwu_6_constant:
  li r4, -1
  li r5, -1
  mulhwu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x00000000FFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
