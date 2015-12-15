test_equiv_1:
  #_ REGISTER_IN r11 0x9E2A83C1
  lis r9, -0x61D6 # 0x9E2A83C1
  ori r30, r9, 0x83C1 # 0x9E2A83C1
  subf r8, r11, r30
  addic r7, r8, -1
  subfe. r29, r7, r8
  beq equiv_1_good
  li r12, 0
  blr
equiv_1_good:
  li r12, 1
  blr
  #_ REGISTER_OUT r7 0xfffffffeffffffff
  #_ REGISTER_OUT r8 0xffffffff00000000
  #_ REGISTER_OUT r9 0xffffffff9e2a0000
  #_ REGISTER_OUT r30 0xffffffff9e2a83c1
  #_ REGISTER_OUT r29 0
  #_ REGISTER_OUT r11 0x000000009e2a83c1
  #_ REGISTER_OUT r12 1

test_equiv_2:
  #_ REGISTER_IN r11 0xffffffff9e2a83c1
  lis r9, -0x61D6 # 0x9E2A83C1
  ori r30, r9, 0x83C1 # 0x9E2A83C1
  subf r8, r11, r30
  addic r7, r8, -1
  subfe. r29, r7, r8
  beq equiv_2_good
  li r12, 0
  blr
equiv_2_good:
  li r12, 1
  blr
  #_ REGISTER_OUT r7 0xffffffffffffffff
  #_ REGISTER_OUT r8 0
  #_ REGISTER_OUT r9 0xffffffff9e2a0000
  #_ REGISTER_OUT r30 0xffffffff9e2a83c1
  #_ REGISTER_OUT r29 0
  #_ REGISTER_OUT r11 0xffffffff9e2a83c1
  #_ REGISTER_OUT r12 1
