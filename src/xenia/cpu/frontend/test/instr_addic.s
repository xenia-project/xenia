# constant tests are commented since add_carry isn't supported

test_addic_1:
  #_ REGISTER_IN r4 1
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r6 0

#test_addic_1_constant:
#  li r4, 1
#  addic r4, r4, 1
#  adde r6, r0, r0
#  blr
#  #_ REGISTER_OUT r4 2
#  #_ REGISTER_OUT r6 0

test_addic_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1

#test_addic_2_constant:
#  li r4, -1
#  addic r4, r4, 1
#  adde r6, r0, r0
#  blr
#  #_ REGISTER_OUT r4 0
#  #_ REGISTER_OUT r6 1
