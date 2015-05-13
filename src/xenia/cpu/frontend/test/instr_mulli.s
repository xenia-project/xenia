test_mulli_1:
  #_ REGISTER_IN r4 1
  mulli r3, r4, 0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_mulli_1_constant:
  li r4, 1
  mulli r3, r4, 0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1

test_mulli_2:
  #_ REGISTER_IN r4 1
  mulli r3, r4, 1
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1

test_mulli_2_constant:
  li r4, 1
  mulli r3, r4, 1
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1

test_mulli_3:
  #_ REGISTER_IN r4 1
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 1

test_mulli_3_constant:
  li r4, 1
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 1

test_mulli_4:
  #_ REGISTER_IN r4 123
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 -123
  #_ REGISTER_OUT r4 123

test_mulli_4_constant:
  li r4, 123
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 -123
  #_ REGISTER_OUT r4 123

test_mulli_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  mulli r3, r4, 1
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF

test_mulli_5_constant:
  li r4, -1
  mulli r3, r4, 1
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF

test_mulli_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  mulli r3, r4, 2
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF

test_mulli_6_constant:
  li r4, -1
  mulli r3, r4, 2
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF

test_mulli_7:
  #_ REGISTER_IN r4 1
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1

test_mulli_7_constant:
  li r4, 1
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1

test_mulli_8:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF

test_mulli_8_constant:
  li r4, -1
  mulli r3, r4, -1
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
