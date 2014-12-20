# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'alloy-sandbox',
      'type': 'executable',

      'msvs_settings': {
        'VCLinkerTool': {
          'SubSystem': '1'
        },
      },

      'dependencies': [
        'liballoy',
        'libxenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'alloy-sandbox.cc',
      ],
    },
  ],

  'targets': [
    {
      'target_name': 'alloy-test',
      'type': 'executable',

      'msvs_settings': {
        'VCLinkerTool': {
          'SubSystem': '1'
        },
      },

      'dependencies': [
        'liballoy',
        'libxenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'alloy-test.cc',
        #'test_abs.cc',
        'test_add.cc',
        #'test_add_carry.cc',
        #'test_and.cc',
        #'test_assign.cc',
        #'test_atomic_add.cc',
        #'test_atomic_exchange.cc',
        #'test_atomic_sub.cc',
        #'test_branch.cc',
        'test_byte_swap.cc',
        #'test_cast.cc',
        #'test_cntlz.cc',
        #'test_compare.cc',
        #'test_compare_exchange.cc',
        #'test_convert.cc',
        #'test_did_carry.cc',
        #'test_div.cc',
        #'test_dot_product_3.cc',
        #'test_dot_product_4.cc',
        'test_extract.cc',
        'test_insert.cc',
        #'test_is_true_false.cc',
        #'test_load_clock.cc',
        'test_load_vector_shl_shr.cc',
        #'test_log2.cc',
        #'test_max.cc',
        #'test_min.cc',
        #'test_mul.cc',
        #'test_mul_add.cc',
        #'test_mul_hi.cc',
        #'test_mul_sub.cc',
        #'test_neg.cc',
        #'test_not.cc',
        #'test_or.cc',
        'test_pack.cc',
        'test_permute.cc',
        #'test_pow2.cc',
        #'test_rotate_left.cc',
        #'test_round.cc',
        #'test_rsqrt.cc',
        #'test_select.cc',
        'test_sha.cc',
        'test_shl.cc',
        'test_shr.cc',
        #'test_sign_extend.cc',
        #'test_splat.cc',
        #'test_sqrt.cc',
        #'test_sub.cc',
        'test_swizzle.cc',
        #'test_truncate.cc',
        'test_unpack.cc',
        'test_vector_add.cc',
        #'test_vector_compare.cc',
        #'test_vector_convert.cc',
        'test_vector_max.cc',
        'test_vector_min.cc',
        'test_vector_rotate_left.cc',
        'test_vector_sha.cc',
        'test_vector_shl.cc',
        'test_vector_shr.cc',
        #'test_vector_sub.cc',
        #'test_xor.cc',
        #'test_zero_extend.cc',
        'util.h',
      ],
    },
  ],
}
