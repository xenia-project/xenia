the_function:
  cmpwi cr6, r3, 3
  bgt .case_3
  lis r11, .the_table@h
  addi r11, r11, .the_table@l
  slwi r3, r3, 2
  lwzx r3, r11, r3
  mtspr ctr, r3
  bctr
.the_table:
  .long .case_0
  .long .case_1
  .long .case_2
  .long .case_3
.case_0:
  li r11, 8
  b .the_divide
.case_1:
  li r11, 16
  b .the_divide
.case_2:
  li r11, 24
  b .the_divide
.case_3:
  li r11, 32
.the_divide:
  extsw r11, r11
  tdllei r11, 0
  divdu r4, r4, r11
  blr

test_jumptable_constants:
  mfspr r12, lr
  li r3, 0
  li r4, 1024
  bl the_function
  li r3, 1
  li r4, 1024
  bl the_function
  li r3, 2
  li r4, 1024
  bl the_function
  li r3, 3
  li r4, 1024
  bl the_function
  mtspr lr, r12
  blr
