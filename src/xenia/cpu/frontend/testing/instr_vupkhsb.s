test_vupkhsb_0:
  # {-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7}
  #_ REGISTER_IN v3 [F8F9FAFB, FCFDFEFF, 00010203, 04050607]
  vupkhsb v3, v3
  blr
  # {-8, -7, -6, -5, -4, -3, -2, -1}
  #_ REGISTER_OUT v3 [FFF8FFF9, FFFAFFFB, FFFCFFFD, FFFEFFFF]
