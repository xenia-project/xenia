test_srawi_1:
  #_ REGISTER_IN r4 1
  srawi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 0

test_srawi_1_constant:
  li r4, 1
  srawi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 0

test_srawi_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  srawi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 0

test_srawi_2_constant:
  li r4, -1
  srawi r3, r4, 0
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 0

test_srawi_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  srawi r3, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_srawi_3_constant:
  li r4, -1
  srawi r3, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_srawi_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  srawi r3, r4, 30
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_srawi_4_constant:
  li r4, -1
  srawi r3, r4, 30
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_srawi_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  srawi r3, r4, 31
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_srawi_5_constant:
  li r4, -1
  srawi r3, r4, 31
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
