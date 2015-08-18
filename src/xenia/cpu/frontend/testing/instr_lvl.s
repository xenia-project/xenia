test_lvl_1:
  #_ MEMORY_IN 10001077 0a 0b 0c 0d 0e 0f 10 13 0c 0d 0e 10 11 12 13 14 ff ff ff ff ff ff
  #_ REGISTER_IN r4 0x10001077
  lvlx v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x10001077
  #_ REGISTER_OUT v3 [0A0B0C0D, 0E0F1013, 0C000000, 00000000]

test_lvl_1_constant:
  #_ MEMORY_IN 10001077 0a 0b 0c 0d 0e 0f 10 13 0c 0d 0e 10 11 12 13 14 ff ff ff ff ff ff
  lis r4, 0x1000
  ori r4, r4, 0x1077
  lvlx v3, r4, r0
  blr
  #_ REGISTER_OUT r4 0x10001077
  #_ REGISTER_OUT v3 [0A0B0C0D, 0E0F1013, 0C000000, 00000000]
