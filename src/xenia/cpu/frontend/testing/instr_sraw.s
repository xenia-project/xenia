test_sraw_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0

test_sraw_1_constant:
  li r4, 1
  li r5, 0
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0

test_sraw_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0

test_sraw_2_constant:
  li r4, -1
  li r5, 0
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0

test_sraw_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 1

test_sraw_3_constant:
  li r4, -1
  li r5, 1
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 1

test_sraw_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 63
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 63
  #_ REGISTER_OUT r6 1

test_sraw_4_constant:
  li r4, -1
  li r5, 63
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 63
  #_ REGISTER_OUT r6 1

test_sraw_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 64
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 64
  #_ REGISTER_OUT r6 0

test_sraw_5_constant:
  li r4, -1
  li r5, 64
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 64
  #_ REGISTER_OUT r6 0

test_sraw_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 100
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 100
  #_ REGISTER_OUT r6 1

test_sraw_6_constant:
  li r4, -1
  li r5, 100
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 100
  #_ REGISTER_OUT r6 1

test_sraw_7:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 30
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 30
  #_ REGISTER_OUT r6 1

test_sraw_7_constant:
  li r4, -1
  li r5, 30
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 30
  #_ REGISTER_OUT r6 1

test_sraw_8:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 31
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 31
  #_ REGISTER_OUT r6 1

test_sraw_8_constant:
  li r4, -1
  li r5, 31
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 31
  #_ REGISTER_OUT r6 1

test_sraw_9:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 32
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 32
  #_ REGISTER_OUT r6 1

test_sraw_9_constant:
  li r4, -1
  li r5, 32
  sraw r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xffffffffffffffff
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 32
  #_ REGISTER_OUT r6 1
