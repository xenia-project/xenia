test_vctsxs_1:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vctsxs v3, v3, 0
  blr
  #_ REGISTER_OUT v3 [00000001, 00000001, 00000001, 00000001]

test_vctsxs_2:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vctsxs v3, v3, 1
  blr
  #_ REGISTER_OUT v3 [00000002, 00000003, 00000002, 00000003]

test_vctsxs_3:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vctsxs v3, v3, 2
  blr
  #_ REGISTER_OUT v3 [00000004, 00000006, 00000004, 00000007]

test_vctsxs_4:
  #_ REGISTER_IN v3 [42c83333, 43480000, 449a4000, c49a4000]
  vctsxs v3, v3, 0
  blr
  #_ REGISTER_OUT v3 [00000064, 000000c8, 000004d2, fffffb2e]

test_vctsxs_5:
  #_ REGISTER_IN v3 [42c83333, 43480000, 449a4000, c49a4000]
  vctsxs v3, v3, 1
  blr
  #_ REGISTER_OUT v3 [000000c8, 00000190, 000009a4, fffff65c]
