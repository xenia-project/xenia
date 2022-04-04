// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 25179
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %5663 "main" %gl_GlobalInvocationID
               OpExecutionMode %5663 LocalSize 4 32 1
               OpMemberDecorate %_struct_1161 0 Offset 0
               OpMemberDecorate %_struct_1161 1 Offset 4
               OpMemberDecorate %_struct_1161 2 Offset 8
               OpMemberDecorate %_struct_1161 3 Offset 12
               OpMemberDecorate %_struct_1161 4 Offset 16
               OpMemberDecorate %_struct_1161 5 Offset 28
               OpMemberDecorate %_struct_1161 6 Offset 32
               OpMemberDecorate %_struct_1161 7 Offset 36
               OpDecorate %_struct_1161 Block
               OpDecorate %5245 DescriptorSet 2
               OpDecorate %5245 Binding 0
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpMemberDecorate %_struct_1972 0 NonWritable
               OpMemberDecorate %_struct_1972 0 Offset 0
               OpDecorate %_struct_1972 BufferBlock
               OpDecorate %4218 DescriptorSet 1
               OpDecorate %4218 Binding 0
               OpDecorate %_runtimearr_v4uint_0 ArrayStride 16
               OpMemberDecorate %_struct_1973 0 NonReadable
               OpMemberDecorate %_struct_1973 0 Offset 0
               OpDecorate %_struct_1973 BufferBlock
               OpDecorate %5134 DescriptorSet 0
               OpDecorate %5134 Binding 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
     %v4uint = OpTypeVector %uint 4
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %v3int = OpTypeVector %int 3
       %bool = OpTypeBool
     %v3uint = OpTypeVector %uint 3
     %uint_9 = OpConstant %uint 9
     %v2bool = OpTypeVector %bool 2
     %uint_0 = OpConstant %uint 0
       %1807 = OpConstantComposite %v2uint %uint_0 %uint_0
   %uint_513 = OpConstant %uint 513
        %536 = OpConstantComposite %v2uint %uint_513 %uint_513
   %uint_512 = OpConstant %uint 512
        %515 = OpConstantComposite %v2uint %uint_512 %uint_512
  %uint_1023 = OpConstant %uint 1023
       %2213 = OpConstantComposite %v2uint %uint_1023 %uint_1023
     %uint_6 = OpConstant %uint 6
     %uint_3 = OpConstant %uint 3
 %uint_65535 = OpConstant %uint 65535
       %2015 = OpConstantComposite %v2uint %uint_65535 %uint_65535
    %uint_10 = OpConstant %uint 10
  %uint_1025 = OpConstant %uint 1025
       %2255 = OpConstantComposite %v2uint %uint_1025 %uint_1025
  %uint_1024 = OpConstant %uint 1024
       %2234 = OpConstantComposite %v2uint %uint_1024 %uint_1024
  %uint_2047 = OpConstant %uint 2047
       %2640 = OpConstantComposite %v2uint %uint_2047 %uint_2047
     %uint_5 = OpConstant %uint 5
    %uint_11 = OpConstant %uint 11
    %uint_16 = OpConstant %uint 16
    %uint_22 = OpConstant %uint 22
%uint_2147418112 = OpConstant %uint 2147418112
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
      %int_5 = OpConstant %int 5
      %int_7 = OpConstant %int 7
     %int_14 = OpConstant %int 14
      %int_2 = OpConstant %int 2
    %int_n16 = OpConstant %int -16
      %int_1 = OpConstant %int 1
     %int_15 = OpConstant %int 15
      %int_4 = OpConstant %int 4
   %int_n512 = OpConstant %int -512
      %int_3 = OpConstant %int 3
     %int_16 = OpConstant %int 16
    %int_448 = OpConstant %int 448
      %int_8 = OpConstant %int 8
      %int_6 = OpConstant %int 6
     %int_63 = OpConstant %int 63
     %uint_4 = OpConstant %uint 4
%int_268435455 = OpConstant %int 268435455
     %int_n2 = OpConstant %int -2
    %uint_32 = OpConstant %uint 32
%_struct_1161 = OpTypeStruct %uint %uint %uint %uint %v3uint %uint %uint %uint
%_ptr_Uniform__struct_1161 = OpTypePointer Uniform %_struct_1161
       %5245 = OpVariable %_ptr_Uniform__struct_1161 Uniform
      %int_0 = OpConstant %int 0
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
%_ptr_Uniform_v3uint = OpTypePointer Uniform %v3uint
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
       %2604 = OpConstantComposite %v3uint %uint_3 %uint_0 %uint_0
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%_struct_1972 = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform__struct_1972 = OpTypePointer Uniform %_struct_1972
       %4218 = OpVariable %_ptr_Uniform__struct_1972 Uniform
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
%_runtimearr_v4uint_0 = OpTypeRuntimeArray %v4uint
%_struct_1973 = OpTypeStruct %_runtimearr_v4uint_0
%_ptr_Uniform__struct_1973 = OpTypePointer Uniform %_struct_1973
       %5134 = OpVariable %_ptr_Uniform__struct_1973 Uniform
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_4 %uint_32 %uint_1
       %2510 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
        %317 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %1838 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
        %749 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
       %2017 = OpConstantComposite %v2uint %uint_10 %uint_10
       %1912 = OpConstantComposite %v2uint %uint_5 %uint_5
       %2038 = OpConstantComposite %v2uint %uint_11 %uint_11
       %2143 = OpConstantComposite %v2uint %uint_16 %uint_16
       %2269 = OpConstantComposite %v2uint %uint_22 %uint_22
       %1996 = OpConstantComposite %v2uint %uint_9 %uint_9
       %1933 = OpConstantComposite %v2uint %uint_6 %uint_6
       %1870 = OpConstantComposite %v2uint %uint_3 %uint_3
        %883 = OpConstantComposite %v2uint %uint_2147418112 %uint_2147418112
       %5663 = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %19578 None
               OpSwitch %uint_0 %15137
      %15137 = OpLabel
      %12591 = OpLoad %v3uint %gl_GlobalInvocationID
      %10229 = OpShiftLeftLogical %v3uint %12591 %2604
      %25178 = OpAccessChain %_ptr_Uniform_v3uint %5245 %int_4
      %22965 = OpLoad %v3uint %25178
      %18835 = OpVectorShuffle %v2uint %10229 %10229 0 1
       %6626 = OpVectorShuffle %v2uint %22965 %22965 0 1
      %17032 = OpUGreaterThanEqual %v2bool %18835 %6626
      %24679 = OpAny %bool %17032
               OpSelectionMerge %6282 DontFlatten
               OpBranchConditional %24679 %21992 %6282
      %21992 = OpLabel
               OpBranch %19578
       %6282 = OpLabel
       %6795 = OpBitcast %v3int %10229
      %18792 = OpAccessChain %_ptr_Uniform_uint %5245 %int_6
       %9788 = OpLoad %uint %18792
      %20376 = OpCompositeExtract %uint %22965 1
      %14692 = OpCompositeExtract %int %6795 0
      %22810 = OpIMul %int %14692 %int_8
       %6362 = OpCompositeExtract %int %6795 2
      %14505 = OpBitcast %int %20376
      %11279 = OpIMul %int %6362 %14505
      %17598 = OpCompositeExtract %int %6795 1
      %22228 = OpIAdd %int %11279 %17598
      %22405 = OpBitcast %int %9788
      %24535 = OpIMul %int %22228 %22405
       %7061 = OpIAdd %int %22810 %24535
      %19270 = OpBitcast %uint %7061
      %19460 = OpAccessChain %_ptr_Uniform_uint %5245 %int_5
      %22875 = OpLoad %uint %19460
       %8517 = OpIAdd %uint %19270 %22875
      %21670 = OpShiftRightLogical %uint %8517 %uint_4
      %20950 = OpAccessChain %_ptr_Uniform_uint %5245 %int_0
      %21411 = OpLoad %uint %20950
       %6381 = OpBitwiseAnd %uint %21411 %uint_1
      %10467 = OpINotEqual %bool %6381 %uint_0
               OpSelectionMerge %23266 DontFlatten
               OpBranchConditional %10467 %10108 %10765
      %10108 = OpLabel
      %23508 = OpBitwiseAnd %uint %21411 %uint_2
      %16300 = OpINotEqual %bool %23508 %uint_0
               OpSelectionMerge %7691 DontFlatten
               OpBranchConditional %16300 %12129 %25128
      %12129 = OpLabel
      %18210 = OpAccessChain %_ptr_Uniform_uint %5245 %int_2
      %15627 = OpLoad %uint %18210
      %22624 = OpAccessChain %_ptr_Uniform_uint %5245 %int_3
      %21535 = OpLoad %uint %22624
      %14923 = OpShiftRightArithmetic %int %17598 %int_4
      %18773 = OpShiftRightArithmetic %int %6362 %int_2
      %18759 = OpShiftRightLogical %uint %21535 %uint_4
       %6314 = OpBitcast %int %18759
      %21281 = OpIMul %int %18773 %6314
      %15143 = OpIAdd %int %14923 %21281
       %9032 = OpShiftRightLogical %uint %15627 %uint_5
      %14593 = OpBitcast %int %9032
       %8436 = OpIMul %int %15143 %14593
      %12986 = OpShiftRightArithmetic %int %14692 %int_5
      %24558 = OpIAdd %int %12986 %8436
       %8797 = OpShiftLeftLogical %int %24558 %uint_8
      %11510 = OpBitwiseAnd %int %8797 %int_268435455
      %18938 = OpShiftLeftLogical %int %11510 %int_1
      %19768 = OpBitwiseAnd %int %14692 %int_7
      %12600 = OpBitwiseAnd %int %17598 %int_6
      %17741 = OpShiftLeftLogical %int %12600 %int_2
      %17227 = OpIAdd %int %19768 %17741
       %7048 = OpShiftLeftLogical %int %17227 %uint_8
      %24035 = OpShiftRightArithmetic %int %7048 %int_6
       %8725 = OpShiftRightArithmetic %int %17598 %int_3
      %13731 = OpIAdd %int %8725 %18773
      %23052 = OpBitwiseAnd %int %13731 %int_1
      %16658 = OpShiftRightArithmetic %int %14692 %int_3
      %18794 = OpShiftLeftLogical %int %23052 %int_1
      %13501 = OpIAdd %int %16658 %18794
      %19165 = OpBitwiseAnd %int %13501 %int_3
      %21578 = OpShiftLeftLogical %int %19165 %int_1
      %15435 = OpIAdd %int %23052 %21578
      %13150 = OpBitwiseAnd %int %24035 %int_n16
      %20336 = OpIAdd %int %18938 %13150
      %23345 = OpShiftLeftLogical %int %20336 %int_1
      %23274 = OpBitwiseAnd %int %24035 %int_15
      %10332 = OpIAdd %int %23345 %23274
      %18356 = OpBitwiseAnd %int %6362 %int_3
      %21579 = OpShiftLeftLogical %int %18356 %uint_8
      %16727 = OpIAdd %int %10332 %21579
      %19166 = OpBitwiseAnd %int %17598 %int_1
      %21580 = OpShiftLeftLogical %int %19166 %int_4
      %16728 = OpIAdd %int %16727 %21580
      %20438 = OpBitwiseAnd %int %15435 %int_1
       %9987 = OpShiftLeftLogical %int %20438 %int_3
      %13106 = OpShiftRightArithmetic %int %16728 %int_6
      %14038 = OpBitwiseAnd %int %13106 %int_7
      %13330 = OpIAdd %int %9987 %14038
      %23346 = OpShiftLeftLogical %int %13330 %int_3
      %23217 = OpBitwiseAnd %int %15435 %int_n2
      %10908 = OpIAdd %int %23346 %23217
      %23347 = OpShiftLeftLogical %int %10908 %int_2
      %23218 = OpBitwiseAnd %int %16728 %int_n512
      %10909 = OpIAdd %int %23347 %23218
      %23348 = OpShiftLeftLogical %int %10909 %int_3
      %24224 = OpBitwiseAnd %int %16728 %int_63
      %21741 = OpIAdd %int %23348 %24224
               OpBranch %7691
      %25128 = OpLabel
       %6796 = OpBitcast %v2int %18835
      %18793 = OpAccessChain %_ptr_Uniform_uint %5245 %int_2
      %11954 = OpLoad %uint %18793
      %18756 = OpCompositeExtract %int %6796 0
      %19701 = OpShiftRightArithmetic %int %18756 %int_5
      %10055 = OpCompositeExtract %int %6796 1
      %16476 = OpShiftRightArithmetic %int %10055 %int_5
      %23373 = OpShiftRightLogical %uint %11954 %uint_5
       %6315 = OpBitcast %int %23373
      %21319 = OpIMul %int %16476 %6315
      %16222 = OpIAdd %int %19701 %21319
      %19086 = OpShiftLeftLogical %int %16222 %uint_9
      %10934 = OpBitwiseAnd %int %18756 %int_7
      %12601 = OpBitwiseAnd %int %10055 %int_14
      %17742 = OpShiftLeftLogical %int %12601 %int_2
      %17303 = OpIAdd %int %10934 %17742
       %6375 = OpShiftLeftLogical %int %17303 %uint_2
      %10161 = OpBitwiseAnd %int %6375 %int_n16
      %12150 = OpShiftLeftLogical %int %10161 %int_1
      %15436 = OpIAdd %int %19086 %12150
      %13207 = OpBitwiseAnd %int %6375 %int_15
      %19760 = OpIAdd %int %15436 %13207
      %18357 = OpBitwiseAnd %int %10055 %int_1
      %21581 = OpShiftLeftLogical %int %18357 %int_4
      %16729 = OpIAdd %int %19760 %21581
      %20514 = OpBitwiseAnd %int %16729 %int_n512
       %9238 = OpShiftLeftLogical %int %20514 %int_3
      %18995 = OpBitwiseAnd %int %10055 %int_16
      %12151 = OpShiftLeftLogical %int %18995 %int_7
      %16730 = OpIAdd %int %9238 %12151
      %19167 = OpBitwiseAnd %int %16729 %int_448
      %21582 = OpShiftLeftLogical %int %19167 %int_2
      %16708 = OpIAdd %int %16730 %21582
      %20611 = OpBitwiseAnd %int %10055 %int_8
      %16831 = OpShiftRightArithmetic %int %20611 %int_2
       %7916 = OpShiftRightArithmetic %int %18756 %int_3
      %13750 = OpIAdd %int %16831 %7916
      %21587 = OpBitwiseAnd %int %13750 %int_3
      %21583 = OpShiftLeftLogical %int %21587 %int_6
      %15437 = OpIAdd %int %16708 %21583
      %14157 = OpBitwiseAnd %int %16729 %int_63
      %12098 = OpIAdd %int %15437 %14157
               OpBranch %7691
       %7691 = OpLabel
      %10540 = OpPhi %int %21741 %12129 %12098 %25128
               OpBranch %23266
      %10765 = OpLabel
      %20632 = OpAccessChain %_ptr_Uniform_uint %5245 %int_2
      %15628 = OpLoad %uint %20632
      %21427 = OpAccessChain %_ptr_Uniform_uint %5245 %int_3
      %12014 = OpLoad %uint %21427
       %8199 = OpIMul %int %14692 %int_4
      %11736 = OpBitcast %int %12014
       %8690 = OpIMul %int %6362 %11736
       %8334 = OpIAdd %int %8690 %17598
       %8952 = OpBitcast %int %15628
       %7839 = OpIMul %int %8334 %8952
       %7984 = OpIAdd %int %8199 %7839
               OpBranch %23266
      %23266 = OpLabel
      %19748 = OpPhi %int %10540 %7691 %7984 %10765
      %24922 = OpAccessChain %_ptr_Uniform_uint %5245 %int_1
       %7502 = OpLoad %uint %24922
      %15686 = OpBitcast %int %7502
      %15579 = OpIAdd %int %15686 %19748
      %18556 = OpBitcast %uint %15579
      %21493 = OpShiftRightLogical %uint %18556 %uint_4
      %14997 = OpShiftRightLogical %uint %21411 %uint_2
       %8394 = OpBitwiseAnd %uint %14997 %uint_3
      %20727 = OpAccessChain %_ptr_Uniform_v4uint %4218 %int_0 %21493
       %8142 = OpLoad %v4uint %20727
      %13760 = OpIEqual %bool %8394 %uint_1
      %21366 = OpIEqual %bool %8394 %uint_2
      %22150 = OpLogicalOr %bool %13760 %21366
               OpSelectionMerge %13411 None
               OpBranchConditional %22150 %10583 %13411
      %10583 = OpLabel
      %18271 = OpBitwiseAnd %v4uint %8142 %2510
       %9425 = OpShiftLeftLogical %v4uint %18271 %317
      %20652 = OpBitwiseAnd %v4uint %8142 %1838
      %17549 = OpShiftRightLogical %v4uint %20652 %317
      %16376 = OpBitwiseOr %v4uint %9425 %17549
               OpBranch %13411
      %13411 = OpLabel
      %22649 = OpPhi %v4uint %8142 %23266 %16376 %10583
      %19638 = OpIEqual %bool %8394 %uint_3
      %15139 = OpLogicalOr %bool %21366 %19638
               OpSelectionMerge %11682 None
               OpBranchConditional %15139 %11064 %11682
      %11064 = OpLabel
      %24087 = OpShiftLeftLogical %v4uint %22649 %749
      %15335 = OpShiftRightLogical %v4uint %22649 %749
      %10728 = OpBitwiseOr %v4uint %24087 %15335
               OpBranch %11682
      %11682 = OpLabel
      %19948 = OpPhi %v4uint %22649 %13411 %10728 %11064
      %21173 = OpVectorShuffle %v2uint %19948 %19948 0 1
      %12206 = OpBitwiseAnd %v2uint %21173 %2640
       %6898 = OpShiftRightLogical %v2uint %12206 %2017
      %12708 = OpINotEqual %v2bool %6898 %1807
      %20140 = OpIEqual %v2bool %12206 %2234
       %7655 = OpSelect %v2uint %20140 %2255 %12206
      %23050 = OpSelect %v2uint %12708 %2640 %1807
      %23672 = OpBitwiseXor %v2uint %7655 %23050
      %24500 = OpIAdd %v2uint %23672 %6898
       %8872 = OpShiftLeftLogical %v2uint %24500 %1912
      %13590 = OpShiftRightLogical %v2uint %24500 %1912
      %23618 = OpBitwiseOr %v2uint %8872 %13590
      %15463 = OpSelect %v2uint %12708 %2015 %1807
       %7774 = OpBitwiseXor %v2uint %23618 %15463
      %24941 = OpIAdd %v2uint %7774 %6898
      %24336 = OpShiftRightLogical %v2uint %21173 %2038
      %11822 = OpBitwiseAnd %v2uint %24336 %2640
       %6319 = OpShiftRightLogical %v2uint %11822 %2017
      %12709 = OpINotEqual %v2bool %6319 %1807
      %20141 = OpIEqual %v2bool %11822 %2234
       %7656 = OpSelect %v2uint %20141 %2255 %11822
      %23051 = OpSelect %v2uint %12709 %2640 %1807
      %23673 = OpBitwiseXor %v2uint %7656 %23051
      %24501 = OpIAdd %v2uint %23673 %6319
       %8873 = OpShiftLeftLogical %v2uint %24501 %1912
      %13591 = OpShiftRightLogical %v2uint %24501 %1912
      %23619 = OpBitwiseOr %v2uint %8873 %13591
      %15464 = OpSelect %v2uint %12709 %2015 %1807
       %7812 = OpBitwiseXor %v2uint %23619 %15464
      %24557 = OpIAdd %v2uint %7812 %6319
       %8296 = OpShiftLeftLogical %v2uint %24557 %2143
       %9076 = OpBitwiseOr %v2uint %24941 %8296
      %23541 = OpShiftRightLogical %v2uint %21173 %2269
      %13212 = OpShiftRightLogical %v2uint %23541 %1996
      %11904 = OpINotEqual %v2bool %13212 %1807
      %20142 = OpIEqual %v2bool %23541 %515
       %7657 = OpSelect %v2uint %20142 %536 %23541
      %23053 = OpSelect %v2uint %11904 %2213 %1807
      %23674 = OpBitwiseXor %v2uint %7657 %23053
      %24502 = OpIAdd %v2uint %23674 %13212
       %8874 = OpShiftLeftLogical %v2uint %24502 %1933
      %13592 = OpShiftRightLogical %v2uint %24502 %1870
      %23620 = OpBitwiseOr %v2uint %8874 %13592
      %15465 = OpSelect %v2uint %11904 %2015 %1807
       %7831 = OpBitwiseXor %v2uint %23620 %15465
      %22180 = OpIAdd %v2uint %7831 %13212
      %18024 = OpBitwiseOr %v2uint %22180 %883
      %10453 = OpCompositeExtract %uint %9076 0
      %23730 = OpCompositeExtract %uint %9076 1
       %7641 = OpCompositeExtract %uint %18024 0
       %7795 = OpCompositeExtract %uint %18024 1
      %16161 = OpCompositeConstruct %v4uint %10453 %23730 %7641 %7795
       %7813 = OpVectorShuffle %v4uint %16161 %16161 0 2 1 3
       %8699 = OpVectorShuffle %v2uint %19948 %19948 2 3
       %7167 = OpBitwiseAnd %v2uint %8699 %2640
       %6899 = OpShiftRightLogical %v2uint %7167 %2017
      %12710 = OpINotEqual %v2bool %6899 %1807
      %20143 = OpIEqual %v2bool %7167 %2234
       %7658 = OpSelect %v2uint %20143 %2255 %7167
      %23054 = OpSelect %v2uint %12710 %2640 %1807
      %23675 = OpBitwiseXor %v2uint %7658 %23054
      %24503 = OpIAdd %v2uint %23675 %6899
       %8875 = OpShiftLeftLogical %v2uint %24503 %1912
      %13593 = OpShiftRightLogical %v2uint %24503 %1912
      %23621 = OpBitwiseOr %v2uint %8875 %13593
      %15466 = OpSelect %v2uint %12710 %2015 %1807
       %7775 = OpBitwiseXor %v2uint %23621 %15466
      %24942 = OpIAdd %v2uint %7775 %6899
      %24337 = OpShiftRightLogical %v2uint %8699 %2038
      %11823 = OpBitwiseAnd %v2uint %24337 %2640
       %6320 = OpShiftRightLogical %v2uint %11823 %2017
      %12711 = OpINotEqual %v2bool %6320 %1807
      %20144 = OpIEqual %v2bool %11823 %2234
       %7659 = OpSelect %v2uint %20144 %2255 %11823
      %23055 = OpSelect %v2uint %12711 %2640 %1807
      %23676 = OpBitwiseXor %v2uint %7659 %23055
      %24504 = OpIAdd %v2uint %23676 %6320
       %8876 = OpShiftLeftLogical %v2uint %24504 %1912
      %13594 = OpShiftRightLogical %v2uint %24504 %1912
      %23622 = OpBitwiseOr %v2uint %8876 %13594
      %15467 = OpSelect %v2uint %12711 %2015 %1807
       %7814 = OpBitwiseXor %v2uint %23622 %15467
      %24559 = OpIAdd %v2uint %7814 %6320
       %8297 = OpShiftLeftLogical %v2uint %24559 %2143
       %9077 = OpBitwiseOr %v2uint %24942 %8297
      %23542 = OpShiftRightLogical %v2uint %8699 %2269
      %13213 = OpShiftRightLogical %v2uint %23542 %1996
      %11905 = OpINotEqual %v2bool %13213 %1807
      %20145 = OpIEqual %v2bool %23542 %515
       %7660 = OpSelect %v2uint %20145 %536 %23542
      %23056 = OpSelect %v2uint %11905 %2213 %1807
      %23677 = OpBitwiseXor %v2uint %7660 %23056
      %24505 = OpIAdd %v2uint %23677 %13213
       %8877 = OpShiftLeftLogical %v2uint %24505 %1933
      %13595 = OpShiftRightLogical %v2uint %24505 %1870
      %23623 = OpBitwiseOr %v2uint %8877 %13595
      %15468 = OpSelect %v2uint %11905 %2015 %1807
       %7832 = OpBitwiseXor %v2uint %23623 %15468
      %22181 = OpIAdd %v2uint %7832 %13213
      %18025 = OpBitwiseOr %v2uint %22181 %883
      %10454 = OpCompositeExtract %uint %9077 0
      %23731 = OpCompositeExtract %uint %9077 1
       %7642 = OpCompositeExtract %uint %18025 0
       %7796 = OpCompositeExtract %uint %18025 1
      %15895 = OpCompositeConstruct %v4uint %10454 %23731 %7642 %7796
       %7631 = OpVectorShuffle %v4uint %15895 %15895 0 2 1 3
      %12351 = OpAccessChain %_ptr_Uniform_v4uint %5134 %int_0 %21670
               OpStore %12351 %7813
      %11457 = OpIAdd %uint %21670 %uint_1
      %23654 = OpAccessChain %_ptr_Uniform_v4uint %5134 %int_0 %11457
               OpStore %23654 %7631
      %16830 = OpSelect %uint %10467 %uint_32 %uint_16
      %22844 = OpShiftRightLogical %uint %16830 %uint_4
      %13947 = OpIAdd %uint %21493 %22844
      %22298 = OpAccessChain %_ptr_Uniform_v4uint %4218 %int_0 %13947
       %6578 = OpLoad %v4uint %22298
               OpSelectionMerge %14874 None
               OpBranchConditional %22150 %10584 %14874
      %10584 = OpLabel
      %18272 = OpBitwiseAnd %v4uint %6578 %2510
       %9426 = OpShiftLeftLogical %v4uint %18272 %317
      %20653 = OpBitwiseAnd %v4uint %6578 %1838
      %17550 = OpShiftRightLogical %v4uint %20653 %317
      %16377 = OpBitwiseOr %v4uint %9426 %17550
               OpBranch %14874
      %14874 = OpLabel
      %10924 = OpPhi %v4uint %6578 %11682 %16377 %10584
               OpSelectionMerge %11683 None
               OpBranchConditional %15139 %11065 %11683
      %11065 = OpLabel
      %24088 = OpShiftLeftLogical %v4uint %10924 %749
      %15336 = OpShiftRightLogical %v4uint %10924 %749
      %10729 = OpBitwiseOr %v4uint %24088 %15336
               OpBranch %11683
      %11683 = OpLabel
      %19949 = OpPhi %v4uint %10924 %14874 %10729 %11065
      %21174 = OpVectorShuffle %v2uint %19949 %19949 0 1
      %12207 = OpBitwiseAnd %v2uint %21174 %2640
       %6900 = OpShiftRightLogical %v2uint %12207 %2017
      %12712 = OpINotEqual %v2bool %6900 %1807
      %20146 = OpIEqual %v2bool %12207 %2234
       %7661 = OpSelect %v2uint %20146 %2255 %12207
      %23057 = OpSelect %v2uint %12712 %2640 %1807
      %23678 = OpBitwiseXor %v2uint %7661 %23057
      %24506 = OpIAdd %v2uint %23678 %6900
       %8878 = OpShiftLeftLogical %v2uint %24506 %1912
      %13596 = OpShiftRightLogical %v2uint %24506 %1912
      %23624 = OpBitwiseOr %v2uint %8878 %13596
      %15469 = OpSelect %v2uint %12712 %2015 %1807
       %7776 = OpBitwiseXor %v2uint %23624 %15469
      %24943 = OpIAdd %v2uint %7776 %6900
      %24338 = OpShiftRightLogical %v2uint %21174 %2038
      %11824 = OpBitwiseAnd %v2uint %24338 %2640
       %6321 = OpShiftRightLogical %v2uint %11824 %2017
      %12713 = OpINotEqual %v2bool %6321 %1807
      %20147 = OpIEqual %v2bool %11824 %2234
       %7662 = OpSelect %v2uint %20147 %2255 %11824
      %23058 = OpSelect %v2uint %12713 %2640 %1807
      %23679 = OpBitwiseXor %v2uint %7662 %23058
      %24507 = OpIAdd %v2uint %23679 %6321
       %8879 = OpShiftLeftLogical %v2uint %24507 %1912
      %13597 = OpShiftRightLogical %v2uint %24507 %1912
      %23625 = OpBitwiseOr %v2uint %8879 %13597
      %15470 = OpSelect %v2uint %12713 %2015 %1807
       %7815 = OpBitwiseXor %v2uint %23625 %15470
      %24560 = OpIAdd %v2uint %7815 %6321
       %8298 = OpShiftLeftLogical %v2uint %24560 %2143
       %9078 = OpBitwiseOr %v2uint %24943 %8298
      %23543 = OpShiftRightLogical %v2uint %21174 %2269
      %13214 = OpShiftRightLogical %v2uint %23543 %1996
      %11906 = OpINotEqual %v2bool %13214 %1807
      %20148 = OpIEqual %v2bool %23543 %515
       %7663 = OpSelect %v2uint %20148 %536 %23543
      %23059 = OpSelect %v2uint %11906 %2213 %1807
      %23680 = OpBitwiseXor %v2uint %7663 %23059
      %24508 = OpIAdd %v2uint %23680 %13214
       %8880 = OpShiftLeftLogical %v2uint %24508 %1933
      %13598 = OpShiftRightLogical %v2uint %24508 %1870
      %23626 = OpBitwiseOr %v2uint %8880 %13598
      %15471 = OpSelect %v2uint %11906 %2015 %1807
       %7833 = OpBitwiseXor %v2uint %23626 %15471
      %22182 = OpIAdd %v2uint %7833 %13214
      %18026 = OpBitwiseOr %v2uint %22182 %883
      %10455 = OpCompositeExtract %uint %9078 0
      %23732 = OpCompositeExtract %uint %9078 1
       %7643 = OpCompositeExtract %uint %18026 0
       %7797 = OpCompositeExtract %uint %18026 1
      %16162 = OpCompositeConstruct %v4uint %10455 %23732 %7643 %7797
       %7816 = OpVectorShuffle %v4uint %16162 %16162 0 2 1 3
       %8700 = OpVectorShuffle %v2uint %19949 %19949 2 3
       %7168 = OpBitwiseAnd %v2uint %8700 %2640
       %6901 = OpShiftRightLogical %v2uint %7168 %2017
      %12714 = OpINotEqual %v2bool %6901 %1807
      %20149 = OpIEqual %v2bool %7168 %2234
       %7664 = OpSelect %v2uint %20149 %2255 %7168
      %23060 = OpSelect %v2uint %12714 %2640 %1807
      %23681 = OpBitwiseXor %v2uint %7664 %23060
      %24509 = OpIAdd %v2uint %23681 %6901
       %8881 = OpShiftLeftLogical %v2uint %24509 %1912
      %13599 = OpShiftRightLogical %v2uint %24509 %1912
      %23627 = OpBitwiseOr %v2uint %8881 %13599
      %15472 = OpSelect %v2uint %12714 %2015 %1807
       %7777 = OpBitwiseXor %v2uint %23627 %15472
      %24944 = OpIAdd %v2uint %7777 %6901
      %24339 = OpShiftRightLogical %v2uint %8700 %2038
      %11825 = OpBitwiseAnd %v2uint %24339 %2640
       %6322 = OpShiftRightLogical %v2uint %11825 %2017
      %12715 = OpINotEqual %v2bool %6322 %1807
      %20150 = OpIEqual %v2bool %11825 %2234
       %7665 = OpSelect %v2uint %20150 %2255 %11825
      %23061 = OpSelect %v2uint %12715 %2640 %1807
      %23682 = OpBitwiseXor %v2uint %7665 %23061
      %24510 = OpIAdd %v2uint %23682 %6322
       %8882 = OpShiftLeftLogical %v2uint %24510 %1912
      %13600 = OpShiftRightLogical %v2uint %24510 %1912
      %23628 = OpBitwiseOr %v2uint %8882 %13600
      %15473 = OpSelect %v2uint %12715 %2015 %1807
       %7817 = OpBitwiseXor %v2uint %23628 %15473
      %24561 = OpIAdd %v2uint %7817 %6322
       %8299 = OpShiftLeftLogical %v2uint %24561 %2143
       %9079 = OpBitwiseOr %v2uint %24944 %8299
      %23544 = OpShiftRightLogical %v2uint %8700 %2269
      %13215 = OpShiftRightLogical %v2uint %23544 %1996
      %11907 = OpINotEqual %v2bool %13215 %1807
      %20151 = OpIEqual %v2bool %23544 %515
       %7666 = OpSelect %v2uint %20151 %536 %23544
      %23062 = OpSelect %v2uint %11907 %2213 %1807
      %23683 = OpBitwiseXor %v2uint %7666 %23062
      %24511 = OpIAdd %v2uint %23683 %13215
       %8883 = OpShiftLeftLogical %v2uint %24511 %1933
      %13601 = OpShiftRightLogical %v2uint %24511 %1870
      %23629 = OpBitwiseOr %v2uint %8883 %13601
      %15474 = OpSelect %v2uint %11907 %2015 %1807
       %7834 = OpBitwiseXor %v2uint %23629 %15474
      %22183 = OpIAdd %v2uint %7834 %13215
      %18027 = OpBitwiseOr %v2uint %22183 %883
      %10456 = OpCompositeExtract %uint %9079 0
      %23733 = OpCompositeExtract %uint %9079 1
       %7644 = OpCompositeExtract %uint %18027 0
       %7798 = OpCompositeExtract %uint %18027 1
      %17092 = OpCompositeConstruct %v4uint %10456 %23733 %7644 %7798
      %15860 = OpVectorShuffle %v4uint %17092 %17092 0 2 1 3
      %21950 = OpIAdd %uint %21670 %uint_2
       %7829 = OpAccessChain %_ptr_Uniform_v4uint %5134 %int_0 %21950
               OpStore %7829 %7816
      %11458 = OpIAdd %uint %21670 %uint_3
      %25174 = OpAccessChain %_ptr_Uniform_v4uint %5134 %int_0 %11458
               OpStore %25174 %15860
               OpBranch %19578
      %19578 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t texture_load_r11g11b10_rgba16_snorm_cs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x0000625B, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F48, 0x00060010, 0x0000161F,
    0x00000011, 0x00000004, 0x00000020, 0x00000001, 0x00050048, 0x00000489,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x00000489, 0x00000001,
    0x00000023, 0x00000004, 0x00050048, 0x00000489, 0x00000002, 0x00000023,
    0x00000008, 0x00050048, 0x00000489, 0x00000003, 0x00000023, 0x0000000C,
    0x00050048, 0x00000489, 0x00000004, 0x00000023, 0x00000010, 0x00050048,
    0x00000489, 0x00000005, 0x00000023, 0x0000001C, 0x00050048, 0x00000489,
    0x00000006, 0x00000023, 0x00000020, 0x00050048, 0x00000489, 0x00000007,
    0x00000023, 0x00000024, 0x00030047, 0x00000489, 0x00000002, 0x00040047,
    0x0000147D, 0x00000022, 0x00000002, 0x00040047, 0x0000147D, 0x00000021,
    0x00000000, 0x00040047, 0x00000F48, 0x0000000B, 0x0000001C, 0x00040047,
    0x000007DC, 0x00000006, 0x00000010, 0x00040048, 0x000007B4, 0x00000000,
    0x00000018, 0x00050048, 0x000007B4, 0x00000000, 0x00000023, 0x00000000,
    0x00030047, 0x000007B4, 0x00000003, 0x00040047, 0x0000107A, 0x00000022,
    0x00000001, 0x00040047, 0x0000107A, 0x00000021, 0x00000000, 0x00040047,
    0x000007DD, 0x00000006, 0x00000010, 0x00040048, 0x000007B5, 0x00000000,
    0x00000019, 0x00050048, 0x000007B5, 0x00000000, 0x00000023, 0x00000000,
    0x00030047, 0x000007B5, 0x00000003, 0x00040047, 0x0000140E, 0x00000022,
    0x00000000, 0x00040047, 0x0000140E, 0x00000021, 0x00000000, 0x00040047,
    0x00000BC3, 0x0000000B, 0x00000019, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00040017, 0x00000017,
    0x0000000B, 0x00000004, 0x00040015, 0x0000000C, 0x00000020, 0x00000001,
    0x00040017, 0x00000012, 0x0000000C, 0x00000002, 0x00040017, 0x00000016,
    0x0000000C, 0x00000003, 0x00020014, 0x00000009, 0x00040017, 0x00000014,
    0x0000000B, 0x00000003, 0x0004002B, 0x0000000B, 0x00000A25, 0x00000009,
    0x00040017, 0x0000000F, 0x00000009, 0x00000002, 0x0004002B, 0x0000000B,
    0x00000A0A, 0x00000000, 0x0005002C, 0x00000011, 0x0000070F, 0x00000A0A,
    0x00000A0A, 0x0004002B, 0x0000000B, 0x0000044A, 0x00000201, 0x0005002C,
    0x00000011, 0x00000218, 0x0000044A, 0x0000044A, 0x0004002B, 0x0000000B,
    0x00000447, 0x00000200, 0x0005002C, 0x00000011, 0x00000203, 0x00000447,
    0x00000447, 0x0004002B, 0x0000000B, 0x00000A44, 0x000003FF, 0x0005002C,
    0x00000011, 0x000008A5, 0x00000A44, 0x00000A44, 0x0004002B, 0x0000000B,
    0x00000A1C, 0x00000006, 0x0004002B, 0x0000000B, 0x00000A13, 0x00000003,
    0x0004002B, 0x0000000B, 0x000001C1, 0x0000FFFF, 0x0005002C, 0x00000011,
    0x000007DF, 0x000001C1, 0x000001C1, 0x0004002B, 0x0000000B, 0x00000A28,
    0x0000000A, 0x0004002B, 0x0000000B, 0x00000A4A, 0x00000401, 0x0005002C,
    0x00000011, 0x000008CF, 0x00000A4A, 0x00000A4A, 0x0004002B, 0x0000000B,
    0x00000A47, 0x00000400, 0x0005002C, 0x00000011, 0x000008BA, 0x00000A47,
    0x00000A47, 0x0004002B, 0x0000000B, 0x00000A81, 0x000007FF, 0x0005002C,
    0x00000011, 0x00000A50, 0x00000A81, 0x00000A81, 0x0004002B, 0x0000000B,
    0x00000A19, 0x00000005, 0x0004002B, 0x0000000B, 0x00000A2B, 0x0000000B,
    0x0004002B, 0x0000000B, 0x00000A3A, 0x00000010, 0x0004002B, 0x0000000B,
    0x00000A4C, 0x00000016, 0x0004002B, 0x0000000B, 0x000003D6, 0x7FFF0000,
    0x0004002B, 0x0000000B, 0x00000A0D, 0x00000001, 0x0004002B, 0x0000000B,
    0x00000A10, 0x00000002, 0x0004002B, 0x0000000B, 0x000008A6, 0x00FF00FF,
    0x0004002B, 0x0000000B, 0x00000A22, 0x00000008, 0x0004002B, 0x0000000B,
    0x000005FD, 0xFF00FF00, 0x0004002B, 0x0000000C, 0x00000A1A, 0x00000005,
    0x0004002B, 0x0000000C, 0x00000A20, 0x00000007, 0x0004002B, 0x0000000C,
    0x00000A35, 0x0000000E, 0x0004002B, 0x0000000C, 0x00000A11, 0x00000002,
    0x0004002B, 0x0000000C, 0x000009DB, 0xFFFFFFF0, 0x0004002B, 0x0000000C,
    0x00000A0E, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A38, 0x0000000F,
    0x0004002B, 0x0000000C, 0x00000A17, 0x00000004, 0x0004002B, 0x0000000C,
    0x0000040B, 0xFFFFFE00, 0x0004002B, 0x0000000C, 0x00000A14, 0x00000003,
    0x0004002B, 0x0000000C, 0x00000A3B, 0x00000010, 0x0004002B, 0x0000000C,
    0x00000388, 0x000001C0, 0x0004002B, 0x0000000C, 0x00000A23, 0x00000008,
    0x0004002B, 0x0000000C, 0x00000A1D, 0x00000006, 0x0004002B, 0x0000000C,
    0x00000AC8, 0x0000003F, 0x0004002B, 0x0000000B, 0x00000A16, 0x00000004,
    0x0004002B, 0x0000000C, 0x0000078B, 0x0FFFFFFF, 0x0004002B, 0x0000000C,
    0x00000A05, 0xFFFFFFFE, 0x0004002B, 0x0000000B, 0x00000A6A, 0x00000020,
    0x000A001E, 0x00000489, 0x0000000B, 0x0000000B, 0x0000000B, 0x0000000B,
    0x00000014, 0x0000000B, 0x0000000B, 0x0000000B, 0x00040020, 0x00000706,
    0x00000002, 0x00000489, 0x0004003B, 0x00000706, 0x0000147D, 0x00000002,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x00000288,
    0x00000002, 0x0000000B, 0x00040020, 0x00000291, 0x00000002, 0x00000014,
    0x00040020, 0x00000292, 0x00000001, 0x00000014, 0x0004003B, 0x00000292,
    0x00000F48, 0x00000001, 0x0006002C, 0x00000014, 0x00000A2C, 0x00000A13,
    0x00000A0A, 0x00000A0A, 0x0003001D, 0x000007DC, 0x00000017, 0x0003001E,
    0x000007B4, 0x000007DC, 0x00040020, 0x00000A31, 0x00000002, 0x000007B4,
    0x0004003B, 0x00000A31, 0x0000107A, 0x00000002, 0x00040020, 0x00000294,
    0x00000002, 0x00000017, 0x0003001D, 0x000007DD, 0x00000017, 0x0003001E,
    0x000007B5, 0x000007DD, 0x00040020, 0x00000A32, 0x00000002, 0x000007B5,
    0x0004003B, 0x00000A32, 0x0000140E, 0x00000002, 0x0006002C, 0x00000014,
    0x00000BC3, 0x00000A16, 0x00000A6A, 0x00000A0D, 0x0007002C, 0x00000017,
    0x000009CE, 0x000008A6, 0x000008A6, 0x000008A6, 0x000008A6, 0x0007002C,
    0x00000017, 0x0000013D, 0x00000A22, 0x00000A22, 0x00000A22, 0x00000A22,
    0x0007002C, 0x00000017, 0x0000072E, 0x000005FD, 0x000005FD, 0x000005FD,
    0x000005FD, 0x0007002C, 0x00000017, 0x000002ED, 0x00000A3A, 0x00000A3A,
    0x00000A3A, 0x00000A3A, 0x0005002C, 0x00000011, 0x000007E1, 0x00000A28,
    0x00000A28, 0x0005002C, 0x00000011, 0x00000778, 0x00000A19, 0x00000A19,
    0x0005002C, 0x00000011, 0x000007F6, 0x00000A2B, 0x00000A2B, 0x0005002C,
    0x00000011, 0x0000085F, 0x00000A3A, 0x00000A3A, 0x0005002C, 0x00000011,
    0x000008DD, 0x00000A4C, 0x00000A4C, 0x0005002C, 0x00000011, 0x000007CC,
    0x00000A25, 0x00000A25, 0x0005002C, 0x00000011, 0x0000078D, 0x00000A1C,
    0x00000A1C, 0x0005002C, 0x00000011, 0x0000074E, 0x00000A13, 0x00000A13,
    0x0005002C, 0x00000011, 0x00000373, 0x000003D6, 0x000003D6, 0x00050036,
    0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00003B06,
    0x000300F7, 0x00004C7A, 0x00000000, 0x000300FB, 0x00000A0A, 0x00003B21,
    0x000200F8, 0x00003B21, 0x0004003D, 0x00000014, 0x0000312F, 0x00000F48,
    0x000500C4, 0x00000014, 0x000027F5, 0x0000312F, 0x00000A2C, 0x00050041,
    0x00000291, 0x0000625A, 0x0000147D, 0x00000A17, 0x0004003D, 0x00000014,
    0x000059B5, 0x0000625A, 0x0007004F, 0x00000011, 0x00004993, 0x000027F5,
    0x000027F5, 0x00000000, 0x00000001, 0x0007004F, 0x00000011, 0x000019E2,
    0x000059B5, 0x000059B5, 0x00000000, 0x00000001, 0x000500AE, 0x0000000F,
    0x00004288, 0x00004993, 0x000019E2, 0x0004009A, 0x00000009, 0x00006067,
    0x00004288, 0x000300F7, 0x0000188A, 0x00000002, 0x000400FA, 0x00006067,
    0x000055E8, 0x0000188A, 0x000200F8, 0x000055E8, 0x000200F9, 0x00004C7A,
    0x000200F8, 0x0000188A, 0x0004007C, 0x00000016, 0x00001A8B, 0x000027F5,
    0x00050041, 0x00000288, 0x00004968, 0x0000147D, 0x00000A1D, 0x0004003D,
    0x0000000B, 0x0000263C, 0x00004968, 0x00050051, 0x0000000B, 0x00004F98,
    0x000059B5, 0x00000001, 0x00050051, 0x0000000C, 0x00003964, 0x00001A8B,
    0x00000000, 0x00050084, 0x0000000C, 0x0000591A, 0x00003964, 0x00000A23,
    0x00050051, 0x0000000C, 0x000018DA, 0x00001A8B, 0x00000002, 0x0004007C,
    0x0000000C, 0x000038A9, 0x00004F98, 0x00050084, 0x0000000C, 0x00002C0F,
    0x000018DA, 0x000038A9, 0x00050051, 0x0000000C, 0x000044BE, 0x00001A8B,
    0x00000001, 0x00050080, 0x0000000C, 0x000056D4, 0x00002C0F, 0x000044BE,
    0x0004007C, 0x0000000C, 0x00005785, 0x0000263C, 0x00050084, 0x0000000C,
    0x00005FD7, 0x000056D4, 0x00005785, 0x00050080, 0x0000000C, 0x00001B95,
    0x0000591A, 0x00005FD7, 0x0004007C, 0x0000000B, 0x00004B46, 0x00001B95,
    0x00050041, 0x00000288, 0x00004C04, 0x0000147D, 0x00000A1A, 0x0004003D,
    0x0000000B, 0x0000595B, 0x00004C04, 0x00050080, 0x0000000B, 0x00002145,
    0x00004B46, 0x0000595B, 0x000500C2, 0x0000000B, 0x000054A6, 0x00002145,
    0x00000A16, 0x00050041, 0x00000288, 0x000051D6, 0x0000147D, 0x00000A0B,
    0x0004003D, 0x0000000B, 0x000053A3, 0x000051D6, 0x000500C7, 0x0000000B,
    0x000018ED, 0x000053A3, 0x00000A0D, 0x000500AB, 0x00000009, 0x000028E3,
    0x000018ED, 0x00000A0A, 0x000300F7, 0x00005AE2, 0x00000002, 0x000400FA,
    0x000028E3, 0x0000277C, 0x00002A0D, 0x000200F8, 0x0000277C, 0x000500C7,
    0x0000000B, 0x00005BD4, 0x000053A3, 0x00000A10, 0x000500AB, 0x00000009,
    0x00003FAC, 0x00005BD4, 0x00000A0A, 0x000300F7, 0x00001E0B, 0x00000002,
    0x000400FA, 0x00003FAC, 0x00002F61, 0x00006228, 0x000200F8, 0x00002F61,
    0x00050041, 0x00000288, 0x00004722, 0x0000147D, 0x00000A11, 0x0004003D,
    0x0000000B, 0x00003D0B, 0x00004722, 0x00050041, 0x00000288, 0x00005860,
    0x0000147D, 0x00000A14, 0x0004003D, 0x0000000B, 0x0000541F, 0x00005860,
    0x000500C3, 0x0000000C, 0x00003A4B, 0x000044BE, 0x00000A17, 0x000500C3,
    0x0000000C, 0x00004955, 0x000018DA, 0x00000A11, 0x000500C2, 0x0000000B,
    0x00004947, 0x0000541F, 0x00000A16, 0x0004007C, 0x0000000C, 0x000018AA,
    0x00004947, 0x00050084, 0x0000000C, 0x00005321, 0x00004955, 0x000018AA,
    0x00050080, 0x0000000C, 0x00003B27, 0x00003A4B, 0x00005321, 0x000500C2,
    0x0000000B, 0x00002348, 0x00003D0B, 0x00000A19, 0x0004007C, 0x0000000C,
    0x00003901, 0x00002348, 0x00050084, 0x0000000C, 0x000020F4, 0x00003B27,
    0x00003901, 0x000500C3, 0x0000000C, 0x000032BA, 0x00003964, 0x00000A1A,
    0x00050080, 0x0000000C, 0x00005FEE, 0x000032BA, 0x000020F4, 0x000500C4,
    0x0000000C, 0x0000225D, 0x00005FEE, 0x00000A22, 0x000500C7, 0x0000000C,
    0x00002CF6, 0x0000225D, 0x0000078B, 0x000500C4, 0x0000000C, 0x000049FA,
    0x00002CF6, 0x00000A0E, 0x000500C7, 0x0000000C, 0x00004D38, 0x00003964,
    0x00000A20, 0x000500C7, 0x0000000C, 0x00003138, 0x000044BE, 0x00000A1D,
    0x000500C4, 0x0000000C, 0x0000454D, 0x00003138, 0x00000A11, 0x00050080,
    0x0000000C, 0x0000434B, 0x00004D38, 0x0000454D, 0x000500C4, 0x0000000C,
    0x00001B88, 0x0000434B, 0x00000A22, 0x000500C3, 0x0000000C, 0x00005DE3,
    0x00001B88, 0x00000A1D, 0x000500C3, 0x0000000C, 0x00002215, 0x000044BE,
    0x00000A14, 0x00050080, 0x0000000C, 0x000035A3, 0x00002215, 0x00004955,
    0x000500C7, 0x0000000C, 0x00005A0C, 0x000035A3, 0x00000A0E, 0x000500C3,
    0x0000000C, 0x00004112, 0x00003964, 0x00000A14, 0x000500C4, 0x0000000C,
    0x0000496A, 0x00005A0C, 0x00000A0E, 0x00050080, 0x0000000C, 0x000034BD,
    0x00004112, 0x0000496A, 0x000500C7, 0x0000000C, 0x00004ADD, 0x000034BD,
    0x00000A14, 0x000500C4, 0x0000000C, 0x0000544A, 0x00004ADD, 0x00000A0E,
    0x00050080, 0x0000000C, 0x00003C4B, 0x00005A0C, 0x0000544A, 0x000500C7,
    0x0000000C, 0x0000335E, 0x00005DE3, 0x000009DB, 0x00050080, 0x0000000C,
    0x00004F70, 0x000049FA, 0x0000335E, 0x000500C4, 0x0000000C, 0x00005B31,
    0x00004F70, 0x00000A0E, 0x000500C7, 0x0000000C, 0x00005AEA, 0x00005DE3,
    0x00000A38, 0x00050080, 0x0000000C, 0x0000285C, 0x00005B31, 0x00005AEA,
    0x000500C7, 0x0000000C, 0x000047B4, 0x000018DA, 0x00000A14, 0x000500C4,
    0x0000000C, 0x0000544B, 0x000047B4, 0x00000A22, 0x00050080, 0x0000000C,
    0x00004157, 0x0000285C, 0x0000544B, 0x000500C7, 0x0000000C, 0x00004ADE,
    0x000044BE, 0x00000A0E, 0x000500C4, 0x0000000C, 0x0000544C, 0x00004ADE,
    0x00000A17, 0x00050080, 0x0000000C, 0x00004158, 0x00004157, 0x0000544C,
    0x000500C7, 0x0000000C, 0x00004FD6, 0x00003C4B, 0x00000A0E, 0x000500C4,
    0x0000000C, 0x00002703, 0x00004FD6, 0x00000A14, 0x000500C3, 0x0000000C,
    0x00003332, 0x00004158, 0x00000A1D, 0x000500C7, 0x0000000C, 0x000036D6,
    0x00003332, 0x00000A20, 0x00050080, 0x0000000C, 0x00003412, 0x00002703,
    0x000036D6, 0x000500C4, 0x0000000C, 0x00005B32, 0x00003412, 0x00000A14,
    0x000500C7, 0x0000000C, 0x00005AB1, 0x00003C4B, 0x00000A05, 0x00050080,
    0x0000000C, 0x00002A9C, 0x00005B32, 0x00005AB1, 0x000500C4, 0x0000000C,
    0x00005B33, 0x00002A9C, 0x00000A11, 0x000500C7, 0x0000000C, 0x00005AB2,
    0x00004158, 0x0000040B, 0x00050080, 0x0000000C, 0x00002A9D, 0x00005B33,
    0x00005AB2, 0x000500C4, 0x0000000C, 0x00005B34, 0x00002A9D, 0x00000A14,
    0x000500C7, 0x0000000C, 0x00005EA0, 0x00004158, 0x00000AC8, 0x00050080,
    0x0000000C, 0x000054ED, 0x00005B34, 0x00005EA0, 0x000200F9, 0x00001E0B,
    0x000200F8, 0x00006228, 0x0004007C, 0x00000012, 0x00001A8C, 0x00004993,
    0x00050041, 0x00000288, 0x00004969, 0x0000147D, 0x00000A11, 0x0004003D,
    0x0000000B, 0x00002EB2, 0x00004969, 0x00050051, 0x0000000C, 0x00004944,
    0x00001A8C, 0x00000000, 0x000500C3, 0x0000000C, 0x00004CF5, 0x00004944,
    0x00000A1A, 0x00050051, 0x0000000C, 0x00002747, 0x00001A8C, 0x00000001,
    0x000500C3, 0x0000000C, 0x0000405C, 0x00002747, 0x00000A1A, 0x000500C2,
    0x0000000B, 0x00005B4D, 0x00002EB2, 0x00000A19, 0x0004007C, 0x0000000C,
    0x000018AB, 0x00005B4D, 0x00050084, 0x0000000C, 0x00005347, 0x0000405C,
    0x000018AB, 0x00050080, 0x0000000C, 0x00003F5E, 0x00004CF5, 0x00005347,
    0x000500C4, 0x0000000C, 0x00004A8E, 0x00003F5E, 0x00000A25, 0x000500C7,
    0x0000000C, 0x00002AB6, 0x00004944, 0x00000A20, 0x000500C7, 0x0000000C,
    0x00003139, 0x00002747, 0x00000A35, 0x000500C4, 0x0000000C, 0x0000454E,
    0x00003139, 0x00000A11, 0x00050080, 0x0000000C, 0x00004397, 0x00002AB6,
    0x0000454E, 0x000500C4, 0x0000000C, 0x000018E7, 0x00004397, 0x00000A10,
    0x000500C7, 0x0000000C, 0x000027B1, 0x000018E7, 0x000009DB, 0x000500C4,
    0x0000000C, 0x00002F76, 0x000027B1, 0x00000A0E, 0x00050080, 0x0000000C,
    0x00003C4C, 0x00004A8E, 0x00002F76, 0x000500C7, 0x0000000C, 0x00003397,
    0x000018E7, 0x00000A38, 0x00050080, 0x0000000C, 0x00004D30, 0x00003C4C,
    0x00003397, 0x000500C7, 0x0000000C, 0x000047B5, 0x00002747, 0x00000A0E,
    0x000500C4, 0x0000000C, 0x0000544D, 0x000047B5, 0x00000A17, 0x00050080,
    0x0000000C, 0x00004159, 0x00004D30, 0x0000544D, 0x000500C7, 0x0000000C,
    0x00005022, 0x00004159, 0x0000040B, 0x000500C4, 0x0000000C, 0x00002416,
    0x00005022, 0x00000A14, 0x000500C7, 0x0000000C, 0x00004A33, 0x00002747,
    0x00000A3B, 0x000500C4, 0x0000000C, 0x00002F77, 0x00004A33, 0x00000A20,
    0x00050080, 0x0000000C, 0x0000415A, 0x00002416, 0x00002F77, 0x000500C7,
    0x0000000C, 0x00004ADF, 0x00004159, 0x00000388, 0x000500C4, 0x0000000C,
    0x0000544E, 0x00004ADF, 0x00000A11, 0x00050080, 0x0000000C, 0x00004144,
    0x0000415A, 0x0000544E, 0x000500C7, 0x0000000C, 0x00005083, 0x00002747,
    0x00000A23, 0x000500C3, 0x0000000C, 0x000041BF, 0x00005083, 0x00000A11,
    0x000500C3, 0x0000000C, 0x00001EEC, 0x00004944, 0x00000A14, 0x00050080,
    0x0000000C, 0x000035B6, 0x000041BF, 0x00001EEC, 0x000500C7, 0x0000000C,
    0x00005453, 0x000035B6, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544F,
    0x00005453, 0x00000A1D, 0x00050080, 0x0000000C, 0x00003C4D, 0x00004144,
    0x0000544F, 0x000500C7, 0x0000000C, 0x0000374D, 0x00004159, 0x00000AC8,
    0x00050080, 0x0000000C, 0x00002F42, 0x00003C4D, 0x0000374D, 0x000200F9,
    0x00001E0B, 0x000200F8, 0x00001E0B, 0x000700F5, 0x0000000C, 0x0000292C,
    0x000054ED, 0x00002F61, 0x00002F42, 0x00006228, 0x000200F9, 0x00005AE2,
    0x000200F8, 0x00002A0D, 0x00050041, 0x00000288, 0x00005098, 0x0000147D,
    0x00000A11, 0x0004003D, 0x0000000B, 0x00003D0C, 0x00005098, 0x00050041,
    0x00000288, 0x000053B3, 0x0000147D, 0x00000A14, 0x0004003D, 0x0000000B,
    0x00002EEE, 0x000053B3, 0x00050084, 0x0000000C, 0x00002007, 0x00003964,
    0x00000A17, 0x0004007C, 0x0000000C, 0x00002DD8, 0x00002EEE, 0x00050084,
    0x0000000C, 0x000021F2, 0x000018DA, 0x00002DD8, 0x00050080, 0x0000000C,
    0x0000208E, 0x000021F2, 0x000044BE, 0x0004007C, 0x0000000C, 0x000022F8,
    0x00003D0C, 0x00050084, 0x0000000C, 0x00001E9F, 0x0000208E, 0x000022F8,
    0x00050080, 0x0000000C, 0x00001F30, 0x00002007, 0x00001E9F, 0x000200F9,
    0x00005AE2, 0x000200F8, 0x00005AE2, 0x000700F5, 0x0000000C, 0x00004D24,
    0x0000292C, 0x00001E0B, 0x00001F30, 0x00002A0D, 0x00050041, 0x00000288,
    0x0000615A, 0x0000147D, 0x00000A0E, 0x0004003D, 0x0000000B, 0x00001D4E,
    0x0000615A, 0x0004007C, 0x0000000C, 0x00003D46, 0x00001D4E, 0x00050080,
    0x0000000C, 0x00003CDB, 0x00003D46, 0x00004D24, 0x0004007C, 0x0000000B,
    0x0000487C, 0x00003CDB, 0x000500C2, 0x0000000B, 0x000053F5, 0x0000487C,
    0x00000A16, 0x000500C2, 0x0000000B, 0x00003A95, 0x000053A3, 0x00000A10,
    0x000500C7, 0x0000000B, 0x000020CA, 0x00003A95, 0x00000A13, 0x00060041,
    0x00000294, 0x000050F7, 0x0000107A, 0x00000A0B, 0x000053F5, 0x0004003D,
    0x00000017, 0x00001FCE, 0x000050F7, 0x000500AA, 0x00000009, 0x000035C0,
    0x000020CA, 0x00000A0D, 0x000500AA, 0x00000009, 0x00005376, 0x000020CA,
    0x00000A10, 0x000500A6, 0x00000009, 0x00005686, 0x000035C0, 0x00005376,
    0x000300F7, 0x00003463, 0x00000000, 0x000400FA, 0x00005686, 0x00002957,
    0x00003463, 0x000200F8, 0x00002957, 0x000500C7, 0x00000017, 0x0000475F,
    0x00001FCE, 0x000009CE, 0x000500C4, 0x00000017, 0x000024D1, 0x0000475F,
    0x0000013D, 0x000500C7, 0x00000017, 0x000050AC, 0x00001FCE, 0x0000072E,
    0x000500C2, 0x00000017, 0x0000448D, 0x000050AC, 0x0000013D, 0x000500C5,
    0x00000017, 0x00003FF8, 0x000024D1, 0x0000448D, 0x000200F9, 0x00003463,
    0x000200F8, 0x00003463, 0x000700F5, 0x00000017, 0x00005879, 0x00001FCE,
    0x00005AE2, 0x00003FF8, 0x00002957, 0x000500AA, 0x00000009, 0x00004CB6,
    0x000020CA, 0x00000A13, 0x000500A6, 0x00000009, 0x00003B23, 0x00005376,
    0x00004CB6, 0x000300F7, 0x00002DA2, 0x00000000, 0x000400FA, 0x00003B23,
    0x00002B38, 0x00002DA2, 0x000200F8, 0x00002B38, 0x000500C4, 0x00000017,
    0x00005E17, 0x00005879, 0x000002ED, 0x000500C2, 0x00000017, 0x00003BE7,
    0x00005879, 0x000002ED, 0x000500C5, 0x00000017, 0x000029E8, 0x00005E17,
    0x00003BE7, 0x000200F9, 0x00002DA2, 0x000200F8, 0x00002DA2, 0x000700F5,
    0x00000017, 0x00004DEC, 0x00005879, 0x00003463, 0x000029E8, 0x00002B38,
    0x0007004F, 0x00000011, 0x000052B5, 0x00004DEC, 0x00004DEC, 0x00000000,
    0x00000001, 0x000500C7, 0x00000011, 0x00002FAE, 0x000052B5, 0x00000A50,
    0x000500C2, 0x00000011, 0x00001AF2, 0x00002FAE, 0x000007E1, 0x000500AB,
    0x0000000F, 0x000031A4, 0x00001AF2, 0x0000070F, 0x000500AA, 0x0000000F,
    0x00004EAC, 0x00002FAE, 0x000008BA, 0x000600A9, 0x00000011, 0x00001DE7,
    0x00004EAC, 0x000008CF, 0x00002FAE, 0x000600A9, 0x00000011, 0x00005A0A,
    0x000031A4, 0x00000A50, 0x0000070F, 0x000500C6, 0x00000011, 0x00005C78,
    0x00001DE7, 0x00005A0A, 0x00050080, 0x00000011, 0x00005FB4, 0x00005C78,
    0x00001AF2, 0x000500C4, 0x00000011, 0x000022A8, 0x00005FB4, 0x00000778,
    0x000500C2, 0x00000011, 0x00003516, 0x00005FB4, 0x00000778, 0x000500C5,
    0x00000011, 0x00005C42, 0x000022A8, 0x00003516, 0x000600A9, 0x00000011,
    0x00003C67, 0x000031A4, 0x000007DF, 0x0000070F, 0x000500C6, 0x00000011,
    0x00001E5E, 0x00005C42, 0x00003C67, 0x00050080, 0x00000011, 0x0000616D,
    0x00001E5E, 0x00001AF2, 0x000500C2, 0x00000011, 0x00005F10, 0x000052B5,
    0x000007F6, 0x000500C7, 0x00000011, 0x00002E2E, 0x00005F10, 0x00000A50,
    0x000500C2, 0x00000011, 0x000018AF, 0x00002E2E, 0x000007E1, 0x000500AB,
    0x0000000F, 0x000031A5, 0x000018AF, 0x0000070F, 0x000500AA, 0x0000000F,
    0x00004EAD, 0x00002E2E, 0x000008BA, 0x000600A9, 0x00000011, 0x00001DE8,
    0x00004EAD, 0x000008CF, 0x00002E2E, 0x000600A9, 0x00000011, 0x00005A0B,
    0x000031A5, 0x00000A50, 0x0000070F, 0x000500C6, 0x00000011, 0x00005C79,
    0x00001DE8, 0x00005A0B, 0x00050080, 0x00000011, 0x00005FB5, 0x00005C79,
    0x000018AF, 0x000500C4, 0x00000011, 0x000022A9, 0x00005FB5, 0x00000778,
    0x000500C2, 0x00000011, 0x00003517, 0x00005FB5, 0x00000778, 0x000500C5,
    0x00000011, 0x00005C43, 0x000022A9, 0x00003517, 0x000600A9, 0x00000011,
    0x00003C68, 0x000031A5, 0x000007DF, 0x0000070F, 0x000500C6, 0x00000011,
    0x00001E84, 0x00005C43, 0x00003C68, 0x00050080, 0x00000011, 0x00005FED,
    0x00001E84, 0x000018AF, 0x000500C4, 0x00000011, 0x00002068, 0x00005FED,
    0x0000085F, 0x000500C5, 0x00000011, 0x00002374, 0x0000616D, 0x00002068,
    0x000500C2, 0x00000011, 0x00005BF5, 0x000052B5, 0x000008DD, 0x000500C2,
    0x00000011, 0x0000339C, 0x00005BF5, 0x000007CC, 0x000500AB, 0x0000000F,
    0x00002E80, 0x0000339C, 0x0000070F, 0x000500AA, 0x0000000F, 0x00004EAE,
    0x00005BF5, 0x00000203, 0x000600A9, 0x00000011, 0x00001DE9, 0x00004EAE,
    0x00000218, 0x00005BF5, 0x000600A9, 0x00000011, 0x00005A0D, 0x00002E80,
    0x000008A5, 0x0000070F, 0x000500C6, 0x00000011, 0x00005C7A, 0x00001DE9,
    0x00005A0D, 0x00050080, 0x00000011, 0x00005FB6, 0x00005C7A, 0x0000339C,
    0x000500C4, 0x00000011, 0x000022AA, 0x00005FB6, 0x0000078D, 0x000500C2,
    0x00000011, 0x00003518, 0x00005FB6, 0x0000074E, 0x000500C5, 0x00000011,
    0x00005C44, 0x000022AA, 0x00003518, 0x000600A9, 0x00000011, 0x00003C69,
    0x00002E80, 0x000007DF, 0x0000070F, 0x000500C6, 0x00000011, 0x00001E97,
    0x00005C44, 0x00003C69, 0x00050080, 0x00000011, 0x000056A4, 0x00001E97,
    0x0000339C, 0x000500C5, 0x00000011, 0x00004668, 0x000056A4, 0x00000373,
    0x00050051, 0x0000000B, 0x000028D5, 0x00002374, 0x00000000, 0x00050051,
    0x0000000B, 0x00005CB2, 0x00002374, 0x00000001, 0x00050051, 0x0000000B,
    0x00001DD9, 0x00004668, 0x00000000, 0x00050051, 0x0000000B, 0x00001E73,
    0x00004668, 0x00000001, 0x00070050, 0x00000017, 0x00003F21, 0x000028D5,
    0x00005CB2, 0x00001DD9, 0x00001E73, 0x0009004F, 0x00000017, 0x00001E85,
    0x00003F21, 0x00003F21, 0x00000000, 0x00000002, 0x00000001, 0x00000003,
    0x0007004F, 0x00000011, 0x000021FB, 0x00004DEC, 0x00004DEC, 0x00000002,
    0x00000003, 0x000500C7, 0x00000011, 0x00001BFF, 0x000021FB, 0x00000A50,
    0x000500C2, 0x00000011, 0x00001AF3, 0x00001BFF, 0x000007E1, 0x000500AB,
    0x0000000F, 0x000031A6, 0x00001AF3, 0x0000070F, 0x000500AA, 0x0000000F,
    0x00004EAF, 0x00001BFF, 0x000008BA, 0x000600A9, 0x00000011, 0x00001DEA,
    0x00004EAF, 0x000008CF, 0x00001BFF, 0x000600A9, 0x00000011, 0x00005A0E,
    0x000031A6, 0x00000A50, 0x0000070F, 0x000500C6, 0x00000011, 0x00005C7B,
    0x00001DEA, 0x00005A0E, 0x00050080, 0x00000011, 0x00005FB7, 0x00005C7B,
    0x00001AF3, 0x000500C4, 0x00000011, 0x000022AB, 0x00005FB7, 0x00000778,
    0x000500C2, 0x00000011, 0x00003519, 0x00005FB7, 0x00000778, 0x000500C5,
    0x00000011, 0x00005C45, 0x000022AB, 0x00003519, 0x000600A9, 0x00000011,
    0x00003C6A, 0x000031A6, 0x000007DF, 0x0000070F, 0x000500C6, 0x00000011,
    0x00001E5F, 0x00005C45, 0x00003C6A, 0x00050080, 0x00000011, 0x0000616E,
    0x00001E5F, 0x00001AF3, 0x000500C2, 0x00000011, 0x00005F11, 0x000021FB,
    0x000007F6, 0x000500C7, 0x00000011, 0x00002E2F, 0x00005F11, 0x00000A50,
    0x000500C2, 0x00000011, 0x000018B0, 0x00002E2F, 0x000007E1, 0x000500AB,
    0x0000000F, 0x000031A7, 0x000018B0, 0x0000070F, 0x000500AA, 0x0000000F,
    0x00004EB0, 0x00002E2F, 0x000008BA, 0x000600A9, 0x00000011, 0x00001DEB,
    0x00004EB0, 0x000008CF, 0x00002E2F, 0x000600A9, 0x00000011, 0x00005A0F,
    0x000031A7, 0x00000A50, 0x0000070F, 0x000500C6, 0x00000011, 0x00005C7C,
    0x00001DEB, 0x00005A0F, 0x00050080, 0x00000011, 0x00005FB8, 0x00005C7C,
    0x000018B0, 0x000500C4, 0x00000011, 0x000022AC, 0x00005FB8, 0x00000778,
    0x000500C2, 0x00000011, 0x0000351A, 0x00005FB8, 0x00000778, 0x000500C5,
    0x00000011, 0x00005C46, 0x000022AC, 0x0000351A, 0x000600A9, 0x00000011,
    0x00003C6B, 0x000031A7, 0x000007DF, 0x0000070F, 0x000500C6, 0x00000011,
    0x00001E86, 0x00005C46, 0x00003C6B, 0x00050080, 0x00000011, 0x00005FEF,
    0x00001E86, 0x000018B0, 0x000500C4, 0x00000011, 0x00002069, 0x00005FEF,
    0x0000085F, 0x000500C5, 0x00000011, 0x00002375, 0x0000616E, 0x00002069,
    0x000500C2, 0x00000011, 0x00005BF6, 0x000021FB, 0x000008DD, 0x000500C2,
    0x00000011, 0x0000339D, 0x00005BF6, 0x000007CC, 0x000500AB, 0x0000000F,
    0x00002E81, 0x0000339D, 0x0000070F, 0x000500AA, 0x0000000F, 0x00004EB1,
    0x00005BF6, 0x00000203, 0x000600A9, 0x00000011, 0x00001DEC, 0x00004EB1,
    0x00000218, 0x00005BF6, 0x000600A9, 0x00000011, 0x00005A10, 0x00002E81,
    0x000008A5, 0x0000070F, 0x000500C6, 0x00000011, 0x00005C7D, 0x00001DEC,
    0x00005A10, 0x00050080, 0x00000011, 0x00005FB9, 0x00005C7D, 0x0000339D,
    0x000500C4, 0x00000011, 0x000022AD, 0x00005FB9, 0x0000078D, 0x000500C2,
    0x00000011, 0x0000351B, 0x00005FB9, 0x0000074E, 0x000500C5, 0x00000011,
    0x00005C47, 0x000022AD, 0x0000351B, 0x000600A9, 0x00000011, 0x00003C6C,
    0x00002E81, 0x000007DF, 0x0000070F, 0x000500C6, 0x00000011, 0x00001E98,
    0x00005C47, 0x00003C6C, 0x00050080, 0x00000011, 0x000056A5, 0x00001E98,
    0x0000339D, 0x000500C5, 0x00000011, 0x00004669, 0x000056A5, 0x00000373,
    0x00050051, 0x0000000B, 0x000028D6, 0x00002375, 0x00000000, 0x00050051,
    0x0000000B, 0x00005CB3, 0x00002375, 0x00000001, 0x00050051, 0x0000000B,
    0x00001DDA, 0x00004669, 0x00000000, 0x00050051, 0x0000000B, 0x00001E74,
    0x00004669, 0x00000001, 0x00070050, 0x00000017, 0x00003E17, 0x000028D6,
    0x00005CB3, 0x00001DDA, 0x00001E74, 0x0009004F, 0x00000017, 0x00001DCF,
    0x00003E17, 0x00003E17, 0x00000000, 0x00000002, 0x00000001, 0x00000003,
    0x00060041, 0x00000294, 0x0000303F, 0x0000140E, 0x00000A0B, 0x000054A6,
    0x0003003E, 0x0000303F, 0x00001E85, 0x00050080, 0x0000000B, 0x00002CC1,
    0x000054A6, 0x00000A0D, 0x00060041, 0x00000294, 0x00005C66, 0x0000140E,
    0x00000A0B, 0x00002CC1, 0x0003003E, 0x00005C66, 0x00001DCF, 0x000600A9,
    0x0000000B, 0x000041BE, 0x000028E3, 0x00000A6A, 0x00000A3A, 0x000500C2,
    0x0000000B, 0x0000593C, 0x000041BE, 0x00000A16, 0x00050080, 0x0000000B,
    0x0000367B, 0x000053F5, 0x0000593C, 0x00060041, 0x00000294, 0x0000571A,
    0x0000107A, 0x00000A0B, 0x0000367B, 0x0004003D, 0x00000017, 0x000019B2,
    0x0000571A, 0x000300F7, 0x00003A1A, 0x00000000, 0x000400FA, 0x00005686,
    0x00002958, 0x00003A1A, 0x000200F8, 0x00002958, 0x000500C7, 0x00000017,
    0x00004760, 0x000019B2, 0x000009CE, 0x000500C4, 0x00000017, 0x000024D2,
    0x00004760, 0x0000013D, 0x000500C7, 0x00000017, 0x000050AD, 0x000019B2,
    0x0000072E, 0x000500C2, 0x00000017, 0x0000448E, 0x000050AD, 0x0000013D,
    0x000500C5, 0x00000017, 0x00003FF9, 0x000024D2, 0x0000448E, 0x000200F9,
    0x00003A1A, 0x000200F8, 0x00003A1A, 0x000700F5, 0x00000017, 0x00002AAC,
    0x000019B2, 0x00002DA2, 0x00003FF9, 0x00002958, 0x000300F7, 0x00002DA3,
    0x00000000, 0x000400FA, 0x00003B23, 0x00002B39, 0x00002DA3, 0x000200F8,
    0x00002B39, 0x000500C4, 0x00000017, 0x00005E18, 0x00002AAC, 0x000002ED,
    0x000500C2, 0x00000017, 0x00003BE8, 0x00002AAC, 0x000002ED, 0x000500C5,
    0x00000017, 0x000029E9, 0x00005E18, 0x00003BE8, 0x000200F9, 0x00002DA3,
    0x000200F8, 0x00002DA3, 0x000700F5, 0x00000017, 0x00004DED, 0x00002AAC,
    0x00003A1A, 0x000029E9, 0x00002B39, 0x0007004F, 0x00000011, 0x000052B6,
    0x00004DED, 0x00004DED, 0x00000000, 0x00000001, 0x000500C7, 0x00000011,
    0x00002FAF, 0x000052B6, 0x00000A50, 0x000500C2, 0x00000011, 0x00001AF4,
    0x00002FAF, 0x000007E1, 0x000500AB, 0x0000000F, 0x000031A8, 0x00001AF4,
    0x0000070F, 0x000500AA, 0x0000000F, 0x00004EB2, 0x00002FAF, 0x000008BA,
    0x000600A9, 0x00000011, 0x00001DED, 0x00004EB2, 0x000008CF, 0x00002FAF,
    0x000600A9, 0x00000011, 0x00005A11, 0x000031A8, 0x00000A50, 0x0000070F,
    0x000500C6, 0x00000011, 0x00005C7E, 0x00001DED, 0x00005A11, 0x00050080,
    0x00000011, 0x00005FBA, 0x00005C7E, 0x00001AF4, 0x000500C4, 0x00000011,
    0x000022AE, 0x00005FBA, 0x00000778, 0x000500C2, 0x00000011, 0x0000351C,
    0x00005FBA, 0x00000778, 0x000500C5, 0x00000011, 0x00005C48, 0x000022AE,
    0x0000351C, 0x000600A9, 0x00000011, 0x00003C6D, 0x000031A8, 0x000007DF,
    0x0000070F, 0x000500C6, 0x00000011, 0x00001E60, 0x00005C48, 0x00003C6D,
    0x00050080, 0x00000011, 0x0000616F, 0x00001E60, 0x00001AF4, 0x000500C2,
    0x00000011, 0x00005F12, 0x000052B6, 0x000007F6, 0x000500C7, 0x00000011,
    0x00002E30, 0x00005F12, 0x00000A50, 0x000500C2, 0x00000011, 0x000018B1,
    0x00002E30, 0x000007E1, 0x000500AB, 0x0000000F, 0x000031A9, 0x000018B1,
    0x0000070F, 0x000500AA, 0x0000000F, 0x00004EB3, 0x00002E30, 0x000008BA,
    0x000600A9, 0x00000011, 0x00001DEE, 0x00004EB3, 0x000008CF, 0x00002E30,
    0x000600A9, 0x00000011, 0x00005A12, 0x000031A9, 0x00000A50, 0x0000070F,
    0x000500C6, 0x00000011, 0x00005C7F, 0x00001DEE, 0x00005A12, 0x00050080,
    0x00000011, 0x00005FBB, 0x00005C7F, 0x000018B1, 0x000500C4, 0x00000011,
    0x000022AF, 0x00005FBB, 0x00000778, 0x000500C2, 0x00000011, 0x0000351D,
    0x00005FBB, 0x00000778, 0x000500C5, 0x00000011, 0x00005C49, 0x000022AF,
    0x0000351D, 0x000600A9, 0x00000011, 0x00003C6E, 0x000031A9, 0x000007DF,
    0x0000070F, 0x000500C6, 0x00000011, 0x00001E87, 0x00005C49, 0x00003C6E,
    0x00050080, 0x00000011, 0x00005FF0, 0x00001E87, 0x000018B1, 0x000500C4,
    0x00000011, 0x0000206A, 0x00005FF0, 0x0000085F, 0x000500C5, 0x00000011,
    0x00002376, 0x0000616F, 0x0000206A, 0x000500C2, 0x00000011, 0x00005BF7,
    0x000052B6, 0x000008DD, 0x000500C2, 0x00000011, 0x0000339E, 0x00005BF7,
    0x000007CC, 0x000500AB, 0x0000000F, 0x00002E82, 0x0000339E, 0x0000070F,
    0x000500AA, 0x0000000F, 0x00004EB4, 0x00005BF7, 0x00000203, 0x000600A9,
    0x00000011, 0x00001DEF, 0x00004EB4, 0x00000218, 0x00005BF7, 0x000600A9,
    0x00000011, 0x00005A13, 0x00002E82, 0x000008A5, 0x0000070F, 0x000500C6,
    0x00000011, 0x00005C80, 0x00001DEF, 0x00005A13, 0x00050080, 0x00000011,
    0x00005FBC, 0x00005C80, 0x0000339E, 0x000500C4, 0x00000011, 0x000022B0,
    0x00005FBC, 0x0000078D, 0x000500C2, 0x00000011, 0x0000351E, 0x00005FBC,
    0x0000074E, 0x000500C5, 0x00000011, 0x00005C4A, 0x000022B0, 0x0000351E,
    0x000600A9, 0x00000011, 0x00003C6F, 0x00002E82, 0x000007DF, 0x0000070F,
    0x000500C6, 0x00000011, 0x00001E99, 0x00005C4A, 0x00003C6F, 0x00050080,
    0x00000011, 0x000056A6, 0x00001E99, 0x0000339E, 0x000500C5, 0x00000011,
    0x0000466A, 0x000056A6, 0x00000373, 0x00050051, 0x0000000B, 0x000028D7,
    0x00002376, 0x00000000, 0x00050051, 0x0000000B, 0x00005CB4, 0x00002376,
    0x00000001, 0x00050051, 0x0000000B, 0x00001DDB, 0x0000466A, 0x00000000,
    0x00050051, 0x0000000B, 0x00001E75, 0x0000466A, 0x00000001, 0x00070050,
    0x00000017, 0x00003F22, 0x000028D7, 0x00005CB4, 0x00001DDB, 0x00001E75,
    0x0009004F, 0x00000017, 0x00001E88, 0x00003F22, 0x00003F22, 0x00000000,
    0x00000002, 0x00000001, 0x00000003, 0x0007004F, 0x00000011, 0x000021FC,
    0x00004DED, 0x00004DED, 0x00000002, 0x00000003, 0x000500C7, 0x00000011,
    0x00001C00, 0x000021FC, 0x00000A50, 0x000500C2, 0x00000011, 0x00001AF5,
    0x00001C00, 0x000007E1, 0x000500AB, 0x0000000F, 0x000031AA, 0x00001AF5,
    0x0000070F, 0x000500AA, 0x0000000F, 0x00004EB5, 0x00001C00, 0x000008BA,
    0x000600A9, 0x00000011, 0x00001DF0, 0x00004EB5, 0x000008CF, 0x00001C00,
    0x000600A9, 0x00000011, 0x00005A14, 0x000031AA, 0x00000A50, 0x0000070F,
    0x000500C6, 0x00000011, 0x00005C81, 0x00001DF0, 0x00005A14, 0x00050080,
    0x00000011, 0x00005FBD, 0x00005C81, 0x00001AF5, 0x000500C4, 0x00000011,
    0x000022B1, 0x00005FBD, 0x00000778, 0x000500C2, 0x00000011, 0x0000351F,
    0x00005FBD, 0x00000778, 0x000500C5, 0x00000011, 0x00005C4B, 0x000022B1,
    0x0000351F, 0x000600A9, 0x00000011, 0x00003C70, 0x000031AA, 0x000007DF,
    0x0000070F, 0x000500C6, 0x00000011, 0x00001E61, 0x00005C4B, 0x00003C70,
    0x00050080, 0x00000011, 0x00006170, 0x00001E61, 0x00001AF5, 0x000500C2,
    0x00000011, 0x00005F13, 0x000021FC, 0x000007F6, 0x000500C7, 0x00000011,
    0x00002E31, 0x00005F13, 0x00000A50, 0x000500C2, 0x00000011, 0x000018B2,
    0x00002E31, 0x000007E1, 0x000500AB, 0x0000000F, 0x000031AB, 0x000018B2,
    0x0000070F, 0x000500AA, 0x0000000F, 0x00004EB6, 0x00002E31, 0x000008BA,
    0x000600A9, 0x00000011, 0x00001DF1, 0x00004EB6, 0x000008CF, 0x00002E31,
    0x000600A9, 0x00000011, 0x00005A15, 0x000031AB, 0x00000A50, 0x0000070F,
    0x000500C6, 0x00000011, 0x00005C82, 0x00001DF1, 0x00005A15, 0x00050080,
    0x00000011, 0x00005FBE, 0x00005C82, 0x000018B2, 0x000500C4, 0x00000011,
    0x000022B2, 0x00005FBE, 0x00000778, 0x000500C2, 0x00000011, 0x00003520,
    0x00005FBE, 0x00000778, 0x000500C5, 0x00000011, 0x00005C4C, 0x000022B2,
    0x00003520, 0x000600A9, 0x00000011, 0x00003C71, 0x000031AB, 0x000007DF,
    0x0000070F, 0x000500C6, 0x00000011, 0x00001E89, 0x00005C4C, 0x00003C71,
    0x00050080, 0x00000011, 0x00005FF1, 0x00001E89, 0x000018B2, 0x000500C4,
    0x00000011, 0x0000206B, 0x00005FF1, 0x0000085F, 0x000500C5, 0x00000011,
    0x00002377, 0x00006170, 0x0000206B, 0x000500C2, 0x00000011, 0x00005BF8,
    0x000021FC, 0x000008DD, 0x000500C2, 0x00000011, 0x0000339F, 0x00005BF8,
    0x000007CC, 0x000500AB, 0x0000000F, 0x00002E83, 0x0000339F, 0x0000070F,
    0x000500AA, 0x0000000F, 0x00004EB7, 0x00005BF8, 0x00000203, 0x000600A9,
    0x00000011, 0x00001DF2, 0x00004EB7, 0x00000218, 0x00005BF8, 0x000600A9,
    0x00000011, 0x00005A16, 0x00002E83, 0x000008A5, 0x0000070F, 0x000500C6,
    0x00000011, 0x00005C83, 0x00001DF2, 0x00005A16, 0x00050080, 0x00000011,
    0x00005FBF, 0x00005C83, 0x0000339F, 0x000500C4, 0x00000011, 0x000022B3,
    0x00005FBF, 0x0000078D, 0x000500C2, 0x00000011, 0x00003521, 0x00005FBF,
    0x0000074E, 0x000500C5, 0x00000011, 0x00005C4D, 0x000022B3, 0x00003521,
    0x000600A9, 0x00000011, 0x00003C72, 0x00002E83, 0x000007DF, 0x0000070F,
    0x000500C6, 0x00000011, 0x00001E9A, 0x00005C4D, 0x00003C72, 0x00050080,
    0x00000011, 0x000056A7, 0x00001E9A, 0x0000339F, 0x000500C5, 0x00000011,
    0x0000466B, 0x000056A7, 0x00000373, 0x00050051, 0x0000000B, 0x000028D8,
    0x00002377, 0x00000000, 0x00050051, 0x0000000B, 0x00005CB5, 0x00002377,
    0x00000001, 0x00050051, 0x0000000B, 0x00001DDC, 0x0000466B, 0x00000000,
    0x00050051, 0x0000000B, 0x00001E76, 0x0000466B, 0x00000001, 0x00070050,
    0x00000017, 0x000042C4, 0x000028D8, 0x00005CB5, 0x00001DDC, 0x00001E76,
    0x0009004F, 0x00000017, 0x00003DF4, 0x000042C4, 0x000042C4, 0x00000000,
    0x00000002, 0x00000001, 0x00000003, 0x00050080, 0x0000000B, 0x000055BE,
    0x000054A6, 0x00000A10, 0x00060041, 0x00000294, 0x00001E95, 0x0000140E,
    0x00000A0B, 0x000055BE, 0x0003003E, 0x00001E95, 0x00001E88, 0x00050080,
    0x0000000B, 0x00002CC2, 0x000054A6, 0x00000A13, 0x00060041, 0x00000294,
    0x00006256, 0x0000140E, 0x00000A0B, 0x00002CC2, 0x0003003E, 0x00006256,
    0x00003DF4, 0x000200F9, 0x00004C7A, 0x000200F8, 0x00004C7A, 0x000100FD,
    0x00010038,
};
