test_sradi_1:
  #_ REGISTER_IN r4 1
  sradi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 0

test_sradi_1_constant:
  li r4, 1
  sradi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 0

test_sradi_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  sradi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 0

test_sradi_2_constant:
  li r4, -1
  sradi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 0

test_sradi_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  sradi r3, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_sradi_3_constant:
  li r4, -1
  sradi r3, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_sradi_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  sradi r3, r4, 62
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_sradi_4_constant:
  li r4, -1
  sradi r3, r4, 62
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_sradi_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  sradi r3, r4, 63
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_sradi_5_constant:
  li r4, -1
  sradi r3, r4, 63
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
