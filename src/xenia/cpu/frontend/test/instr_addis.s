test_addis_1:
  #_ REGISTER_IN r0 1234
  #_ REGISTER_IN r4 1
  addis r3, r0, 1
  blr
  #_ REGISTER_OUT r0 1234
  #_ REGISTER_OUT r3 0x10000
  #_ REGISTER_OUT r4 1

test_addis_1_constant:
  li r0, 1234
  li r4, 1
  addis r3, r0, 1
  blr
  #_ REGISTER_OUT r0 1234
  #_ REGISTER_OUT r3 0x10000
  #_ REGISTER_OUT r4 1

test_addis_2:
  #_ REGISTER_IN r4 1234
  #_ REGISTER_IN r5 1
  addis r3, r4, 1
  blr
  #_ REGISTER_OUT r3 0x104D2
  #_ REGISTER_OUT r4 1234

test_addis_2_constant:
  li r4, 1234
  li r5, 1
  addis r3, r4, 1
  blr
  #_ REGISTER_OUT r3 0x104D2
  #_ REGISTER_OUT r4 1234

test_lis_1:
  #_ REGISTER_IN r0 1234
  lis r3, 1
  blr
  #_ REGISTER_OUT r0 1234
  #_ REGISTER_OUT r3 0x10000

test_lis_1_constant:
  li r0, 1234
  lis r3, 1
  blr
  #_ REGISTER_OUT r0 1234
  #_ REGISTER_OUT r3 0x10000

test_lis_2:
  #_ REGISTER_IN r0 1234
  lis r3, -1
  blr
  #_ REGISTER_OUT r0 1234
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF0000

test_lis_2_constant:
  li r0, 1234
  lis r3, -1
  blr
  #_ REGISTER_OUT r0 1234
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF0000

test_subis_1:
  #_ REGISTER_IN r4 1234
  #_ REGISTER_IN r5 1
  subis r3, r4, 1
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF04D2
  #_ REGISTER_OUT r4 1234

test_subis_1_constant:
  li r4, 1234
  li r5, 1
  subis r3, r4, 1
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFF04D2
  #_ REGISTER_OUT r4 1234
