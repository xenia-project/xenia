test_vupkhsb_1:
  # {-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7}
  #_ REGISTER_IN v3 [F8F9FAFB, FCFDFEFF, 00010203, 04050607]
  vupkhsb v3, v3
  blr
  # {-8, -7, -6, -5, -4, -3, -2, -1}
  #_ REGISTER_OUT v3 [FFF8FFF9, FFFAFFFB, FFFCFFFD, FFFEFFFF]

test_vupkhsb_2:
  # {0, 255, 255, 0, 0, 0, 255, 0, 255, 0, 0, 255, 255, 255, 0, 255}
  #_ REGISTER_IN v3 [00FFFF00, 0000FF00, FF0000FF, FFFF00FF]
  vupkhsb v3, v3
  blr
  # {0, 65535, 65535, 0, 0, 0, 65535, 0}
  #_ REGISTER_OUT v3 [0000FFFF, FFFF0000, 00000000, FFFF0000]
