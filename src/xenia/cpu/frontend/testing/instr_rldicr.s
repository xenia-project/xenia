.macro make_full_test_constant dest, a, b, c, d
  lis \dest, \a
  ori \dest, \dest, \b
  sldi \dest, \dest, 32
  lis r3, \c
  ori r3, r3, \d
  clrldi r3, r3, 32
  or \dest, \dest, r3
.endm

test_rldicr_1:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 24, 0
  blr
  #_ REGISTER_OUT r3 0x0000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_1_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 24, 0
  blr
  #_ REGISTER_OUT r3 0x0000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_2:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 24, 8
  blr
  #_ REGISTER_OUT r3 0x6780000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_2_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 24, 8
  blr
  #_ REGISTER_OUT r3 0x6780000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_3:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 24, 63
  blr
  #_ REGISTER_OUT r3 0x6789abcdef012345
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_3_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 24, 63
  blr
  #_ REGISTER_OUT r3 0x6789abcdef012345
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_4:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 0, 0
  blr
  #_ REGISTER_OUT r3 0x0000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_4_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 0, 0
  blr
  #_ REGISTER_OUT r3 0x0000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_5:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 0, 63
  blr
  #_ REGISTER_OUT r3 0x0123456789abcdef
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_5_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 0, 63
  blr
  #_ REGISTER_OUT r3 0x0123456789abcdef
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_6:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 0, 8
  blr
  #_ REGISTER_OUT r3 0x0100000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_6_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 0, 8
  blr
  #_ REGISTER_OUT r3 0x0100000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_7:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 63, 0
  blr
  #_ REGISTER_OUT r3 0x8000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_7_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 63, 0
  blr
  #_ REGISTER_OUT r3 0x8000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_8:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 63, 63
  blr
  #_ REGISTER_OUT r3 0x8091a2b3c4d5e6f7
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_8_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 63, 63
  blr
  #_ REGISTER_OUT r3 0x8091a2b3c4d5e6f7
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_9:
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  rldicr r3, r4, 31, 0
  blr
  #_ REGISTER_OUT r3 0x8000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_rldicr_9_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  rldicr r3, r4, 31, 0
  blr
  #_ REGISTER_OUT r3 0x8000000000000000
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_sldi_1:
  #_ REGISTER_IN r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  sldi r3, r3, 0
  sldi r4, r4, 0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_sldi_1_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  li r3, -1
  sldi r3, r3, 0
  sldi r4, r4, 0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0x0123456789ABCDEF

test_sldi_2:
  #_ REGISTER_IN r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  sldi r3, r3, 1
  sldi r4, r4, 1
  blr
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0x02468acf13579bde

test_sldi_2_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  li r3, -1
  sldi r3, r3, 1
  sldi r4, r4, 1
  blr
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0x02468acf13579bde

test_sldi_3:
  #_ REGISTER_IN r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  sldi r3, r3, 32
  sldi r4, r4, 32
  blr
  #_ REGISTER_OUT r3 0xffffffff00000000
  #_ REGISTER_OUT r4 0x89abcdef00000000

test_sldi_3_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  li r3, -1
  sldi r3, r3, 32
  sldi r4, r4, 32
  blr
  #_ REGISTER_OUT r3 0xffffffff00000000
  #_ REGISTER_OUT r4 0x89abcdef00000000

test_sldi_4:
  #_ REGISTER_IN r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r4 0x0123456789ABCDEF
  sldi r3, r3, 63
  sldi r4, r4, 63
  blr
  #_ REGISTER_OUT r3 0x8000000000000000
  #_ REGISTER_OUT r4 0x8000000000000000

test_sldi_4_constant:
  make_full_test_constant r4, 0x0123, 0x4567, 0x89AB, 0xCDEF
  li r3, -1
  sldi r3, r3, 63
  sldi r4, r4, 63
  blr
  #_ REGISTER_OUT r3 0x8000000000000000
  #_ REGISTER_OUT r4 0x8000000000000000
