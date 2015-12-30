test_rlwnm_1:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 24
  rlwnm r3, r4, r5, 8, 15
  blr
  #_ REGISTER_OUT r3 0x00120000
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 24

test_rlwnm_1_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 24
  rlwnm r3, r4, r5, 8, 15
  blr
  #_ REGISTER_OUT r3 0x00120000
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 24

test_rlwnm_2:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 4
  rlwnm r3, r4, r5, 0, 27
  blr
  #_ REGISTER_OUT r3 0x23456780
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 4

test_rlwnm_2_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 4
  rlwnm r3, r4, r5, 0, 27
  blr
  #_ REGISTER_OUT r3 0x23456780
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 4

test_rlwnm_3:
  #_ REGISTER_IN r4 0x90003000
  #_ REGISTER_IN r5 2
  rlwnm r3, r4, r5, 0, 0x1D
  blr
  #_ REGISTER_OUT r3 0x4000C000
  #_ REGISTER_OUT r4 0x90003000
  #_ REGISTER_OUT r5 2

test_rlwnm_3_constant:
  lis r4, 0x9000
  ori r4, r4, 0x3000
  clrldi r4, r4, 32
  li r5, 2
  rlwnm r3, r4, r5, 0, 0x1D
  blr
  #_ REGISTER_OUT r3 0x4000C000
  #_ REGISTER_OUT r4 0x90003000
  #_ REGISTER_OUT r5 2

test_rlwnm_4:
  #_ REGISTER_IN r4 0xB0043000
  #_ REGISTER_IN r5 2
  rlwnm. r3, r4, r5, 0, 0x1D
  blr
  #_ REGISTER_OUT r3 0xC010C000
  #_ REGISTER_OUT r4 0xB0043000
  #_ REGISTER_OUT r5 2
  # CRF = 0x8

test_rlwnm_4_constant:
  lis r4, 0xB004
  ori r4, r4, 0x3000
  clrldi r4, r4, 32
  li r5, 2
  rlwnm. r3, r4, r5, 0, 0x1D
  blr
  #_ REGISTER_OUT r3 0xC010C000
  #_ REGISTER_OUT r4 0xB0043000
  #_ REGISTER_OUT r5 2
  # CRF = 0x8

test_rlwnm_5:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 0
  rlwnm r3, r4, r5, 5, 0x1D
  blr
  #_ REGISTER_OUT r3 0x02345678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_5_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 0
  rlwnm r3, r4, r5, 5, 0x1D
  blr
  #_ REGISTER_OUT r3 0x02345678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_6:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 0
  rlwnm r3, r4, r5, 0, 31
  blr
  #_ REGISTER_OUT r3 0x12345678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_6_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 0
  rlwnm r3, r4, r5, 0, 31
  blr
  #_ REGISTER_OUT r3 0x12345678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_7:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 0
  rlwnm r3, r4, r5, 0, 16
  blr
  #_ REGISTER_OUT r3 0x12340000
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_7_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 0
  rlwnm r3, r4, r5, 0, 16
  blr
  #_ REGISTER_OUT r3 0x12340000
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_8:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 0
  rlwnm r3, r4, r5, 16, 31
  blr
  #_ REGISTER_OUT r3 0x00005678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_8_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 0
  rlwnm r3, r4, r5, 16, 31
  blr
  #_ REGISTER_OUT r3 0x00005678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 0

test_rlwnm_9:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 16
  rlwnm r3, r4, r5, 16, 31
  blr
  #_ REGISTER_OUT r3 0x00001234
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 16

test_rlwnm_9_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 16
  rlwnm r3, r4, r5, 16, 31
  blr
  #_ REGISTER_OUT r3 0x00001234
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 16

test_rlwnm_10:
  #_ REGISTER_IN r4 0x12345678
  #_ REGISTER_IN r5 32
  rlwnm r3, r4, r5, 0, 31
  blr
  #_ REGISTER_OUT r3 0x12345678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 32

test_rlwnm_10_constant:
  lis r4, 0x1234
  ori r4, r4, 0x5678
  li r5, 32
  rlwnm r3, r4, r5, 0, 31
  blr
  #_ REGISTER_OUT r3 0x12345678
  #_ REGISTER_OUT r4 0x12345678
  #_ REGISTER_OUT r5 32

test_rlwnm_11:
  #_ REGISTER_IN r0 0x1
  rlwnm r0, r0, r0, 1, 0
  blr
  #_ REGISTER_OUT r0 0x0000000200000002

test_rlwnm_12:
  #_ REGISTER_IN r3 0xFFFFFFFF
  rlwnm r0, r3, r3, 30, 1
  blr
  #_ REGISTER_OUT r0 0xFFFFFFFFC0000003
  #_ REGISTER_OUT r3 0xFFFFFFFF

test_rlwnm_13:
  #_ REGISTER_IN r7 0x01234567
  #_ REGISTER_IN r8 0x0123456789ABCDEFull
  rlwnm r6, r8, r7, 31, 30
  blr
  #_ REGISTER_OUT r6 0xD5E6F7C4D5E6F7C4
  #_ REGISTER_OUT r7 0x01234567
  #_ REGISTER_OUT r8 0x0123456789ABCDEFull
