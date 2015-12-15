test_mulld_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mulld_1_constant:
  li r4, 1
  li r5, 0
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0

test_mulld_2:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 1

test_mulld_2_constant:
  li r4, 1
  li r5, 1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 1

test_mulld_3:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mulld_3_constant:
  li r4, 1
  li r5, -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mulld_4:
  #_ REGISTER_IN r4 123
  #_ REGISTER_IN r5 -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -123
  #_ REGISTER_OUT r4 123
  #_ REGISTER_OUT r5 -1

test_mulld_4_constant:
  li r4, 123
  li r5, -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 -123
  #_ REGISTER_OUT r4 123
  #_ REGISTER_OUT r5 -1

test_mulld_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulld_5_constant:
  li r4, -1
  li r5, 1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_mulld_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 2
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2

test_mulld_6_constant:
  li r4, -1
  li r5, 2
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2

test_mulld_7:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mulld_7_constant:
  li r4, 1
  li r5, -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 -1

test_mulld_8:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 -1

test_mulld_8_constant:
  li r4, -1
  li r5, -1
  mulld r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 -1
