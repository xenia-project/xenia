.macro make_full_test_constant dest, a, b, c, d
  lis \dest, \a
  ori \dest, \dest, \b
  sldi \dest, \dest, 32
  lis r3, \c
  ori r3, r3, \d
  clrldi r3, r3, 32
  or \dest, \dest, r3
.endm

test_rlwimi:
  #_ REGISTER_IN r4 0xCAFEBABE90003000
  #_ REGISTER_IN r6 0xDEADBEEF00000003
  rlwimi r6, r4, 2, 0, 0x1D
  blr
  #_ REGISTER_OUT r4 0xCAFEBABE90003000
  #_ REGISTER_OUT r6 0xDEADBEEF4000C003

test_rlwimi_constant:
  make_full_test_constant r4, 0xCAFE, 0xBABE, 0x9000, 0x3000
  make_full_test_constant r6, 0xDEAD, 0xBEEF, 0x0000, 0x0003
  rlwimi r6, r4, 2, 0, 0x1D
  blr
  #_ REGISTER_OUT r4 0xCAFEBABE90003000
  #_ REGISTER_OUT r6 0xDEADBEEF4000C003
