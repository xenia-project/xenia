// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 25204
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %5663 "main" %gl_GlobalInvocationID
               OpExecutionMode %5663 LocalSize 8 8 1
               OpMemberDecorate %_struct_1017 0 Offset 0
               OpMemberDecorate %_struct_1017 1 Offset 4
               OpMemberDecorate %_struct_1017 2 Offset 8
               OpMemberDecorate %_struct_1017 3 Offset 12
               OpDecorate %_struct_1017 Block
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpMemberDecorate %_struct_1972 0 NonWritable
               OpMemberDecorate %_struct_1972 0 Offset 0
               OpDecorate %_struct_1972 BufferBlock
               OpDecorate %3152 DescriptorSet 1
               OpDecorate %3152 Binding 0
               OpDecorate %_runtimearr_v4uint_0 ArrayStride 16
               OpMemberDecorate %_struct_1973 0 NonReadable
               OpMemberDecorate %_struct_1973 0 Offset 0
               OpDecorate %_struct_1973 BufferBlock
               OpDecorate %5522 DescriptorSet 0
               OpDecorate %5522 Binding 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %bool = OpTypeBool
     %v2bool = OpTypeVector %bool 2
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
     %v4uint = OpTypeVector %uint 4
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %v3int = OpTypeVector %int 3
     %v3uint = OpTypeVector %uint 3
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
       %1837 = OpConstantComposite %v2uint %uint_2 %uint_1
     %uint_0 = OpConstant %uint 0
       %1807 = OpConstantComposite %v2uint %uint_0 %uint_0
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %1816 = OpConstantComposite %v2uint %uint_1 %uint_0
    %uint_80 = OpConstant %uint 80
       %2719 = OpConstantComposite %v2uint %uint_80 %uint_16
      %int_5 = OpConstant %int 5
     %uint_5 = OpConstant %uint 5
     %uint_7 = OpConstant %uint 7
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
%_struct_1017 = OpTypeStruct %uint %uint %uint %uint
%_ptr_PushConstant__struct_1017 = OpTypePointer PushConstant %_struct_1017
       %4495 = OpVariable %_ptr_PushConstant__struct_1017 PushConstant
      %int_0 = OpConstant %int 0
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
  %uint_1023 = OpConstant %uint 1023
    %uint_10 = OpConstant %uint 10
  %uint_4096 = OpConstant %uint 4096
    %uint_13 = OpConstant %uint 13
  %uint_4095 = OpConstant %uint 4095
    %uint_25 = OpConstant %uint 25
    %uint_15 = OpConstant %uint 15
    %uint_29 = OpConstant %uint 29
    %uint_27 = OpConstant %uint 27
       %2398 = OpConstantComposite %v2uint %uint_27 %uint_29
%uint_1073741824 = OpConstant %uint 1073741824
      %false = OpConstantFalse %bool
       %1855 = OpConstantComposite %v2uint %uint_0 %uint_4
       %1856 = OpConstantComposite %v2uint %uint_4 %uint_1
  %uint_2047 = OpConstant %uint 2047
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
    %uint_24 = OpConstant %uint 24
       %2275 = OpConstantComposite %v2uint %uint_20 %uint_24
    %uint_28 = OpConstant %uint 28
   %uint_255 = OpConstant %uint 255
%uint_3222273024 = OpConstant %uint 3222273024
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%_ptr_Input_uint = OpTypePointer Input %uint
       %1834 = OpConstantComposite %v2uint %uint_3 %uint_0
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%_struct_1972 = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform__struct_1972 = OpTypePointer Uniform %_struct_1972
       %3152 = OpVariable %_ptr_Uniform__struct_1972 Uniform
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
%_runtimearr_v4uint_0 = OpTypeRuntimeArray %v4uint
%_struct_1973 = OpTypeStruct %_runtimearr_v4uint_0
%_ptr_Uniform__struct_1973 = OpTypePointer Uniform %_struct_1973
       %5522 = OpVariable %_ptr_Uniform__struct_1973 Uniform
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %1870 = OpConstantComposite %v2uint %uint_3 %uint_3
        %315 = OpConstantComposite %v2bool %false %false
       %2122 = OpConstantComposite %v2uint %uint_15 %uint_15
       %1838 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
       %1611 = OpConstantComposite %v4uint %uint_255 %uint_255 %uint_255 %uint_255
        %749 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
       %2352 = OpConstantComposite %v4uint %uint_3222273024 %uint_3222273024 %uint_3222273024 %uint_3222273024
        %929 = OpConstantComposite %v4uint %uint_1023 %uint_1023 %uint_1023 %uint_1023
        %965 = OpConstantComposite %v4uint %uint_20 %uint_20 %uint_20 %uint_20
     %uint_9 = OpConstant %uint 9
       %2510 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
        %317 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %5663 = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %19578 None
               OpSwitch %uint_0 %11880
      %11880 = OpLabel
      %22245 = OpAccessChain %_ptr_PushConstant_uint %4495 %int_0
      %15627 = OpLoad %uint %22245
      %22700 = OpAccessChain %_ptr_PushConstant_uint %4495 %int_1
      %20824 = OpLoad %uint %22700
      %20561 = OpBitwiseAnd %uint %15627 %uint_1023
      %20073 = OpShiftRightLogical %uint %15627 %uint_10
       %7177 = OpBitwiseAnd %uint %20073 %uint_3
      %23023 = OpBitwiseAnd %uint %15627 %uint_4096
      %20495 = OpINotEqual %bool %23023 %uint_0
      %10307 = OpShiftRightLogical %uint %15627 %uint_13
      %24434 = OpBitwiseAnd %uint %10307 %uint_4095
      %18836 = OpShiftRightLogical %uint %15627 %uint_25
       %9130 = OpBitwiseAnd %uint %18836 %uint_15
       %8871 = OpCompositeConstruct %v2uint %20824 %20824
       %9633 = OpShiftRightLogical %v2uint %8871 %2398
      %23601 = OpBitwiseAnd %v2uint %9633 %1870
      %24030 = OpBitwiseAnd %uint %15627 %uint_1073741824
      %12295 = OpINotEqual %bool %24030 %uint_0
               OpSelectionMerge %6871 None
               OpBranchConditional %12295 %16261 %10181
      %16261 = OpLabel
      %21463 = OpUGreaterThan %v2bool %23601 %1828
               OpBranch %6871
      %10181 = OpLabel
               OpBranch %6871
       %6871 = OpLabel
      %19067 = OpPhi %v2bool %21463 %16261 %315 %10181
       %6303 = OpShiftRightLogical %v2uint %8871 %1855
      %10897 = OpShiftLeftLogical %v2uint %1828 %1856
      %18608 = OpISub %v2uint %10897 %1828
      %18743 = OpBitwiseAnd %v2uint %6303 %18608
      %22404 = OpShiftLeftLogical %v2uint %18743 %1870
      %23019 = OpIMul %v2uint %22404 %23601
      %13123 = OpShiftRightLogical %uint %20824 %uint_5
      %14785 = OpBitwiseAnd %uint %13123 %uint_2047
       %8858 = OpCompositeExtract %uint %23601 0
      %22993 = OpIMul %uint %14785 %8858
      %20036 = OpAccessChain %_ptr_PushConstant_uint %4495 %int_2
      %18628 = OpLoad %uint %20036
      %22701 = OpAccessChain %_ptr_PushConstant_uint %4495 %int_3
      %20919 = OpLoad %uint %22701
      %19164 = OpBitwiseAnd %uint %18628 %uint_7
      %21999 = OpBitwiseAnd %uint %18628 %uint_8
      %20496 = OpINotEqual %bool %21999 %uint_0
      %10402 = OpShiftRightLogical %uint %18628 %uint_4
      %23037 = OpBitwiseAnd %uint %10402 %uint_7
      %23118 = OpBitwiseAnd %uint %18628 %uint_16777216
      %19573 = OpINotEqual %bool %23118 %uint_0
       %8003 = OpBitwiseAnd %uint %20919 %uint_1023
      %15783 = OpShiftLeftLogical %uint %8003 %uint_5
      %22591 = OpShiftRightLogical %uint %20919 %uint_10
      %19390 = OpBitwiseAnd %uint %22591 %uint_1023
      %25203 = OpShiftLeftLogical %uint %19390 %uint_5
      %10422 = OpCompositeConstruct %v2uint %20919 %20919
      %10385 = OpShiftRightLogical %v2uint %10422 %2275
      %23379 = OpBitwiseAnd %v2uint %10385 %2122
      %16207 = OpShiftLeftLogical %v2uint %23379 %1870
      %23020 = OpIMul %v2uint %16207 %23601
      %12819 = OpShiftRightLogical %uint %20919 %uint_28
      %16204 = OpBitwiseAnd %uint %12819 %uint_7
      %20803 = OpAccessChain %_ptr_Input_uint %gl_GlobalInvocationID %uint_0
       %8913 = OpLoad %uint %20803
       %7405 = OpUGreaterThanEqual %bool %8913 %22993
               OpSelectionMerge %17447 DontFlatten
               OpBranchConditional %7405 %21992 %17447
      %21992 = OpLabel
               OpBranch %19578
      %17447 = OpLabel
      %14637 = OpLoad %v3uint %gl_GlobalInvocationID
      %18505 = OpVectorShuffle %v2uint %14637 %14637 0 1
       %9840 = OpShiftLeftLogical %v2uint %18505 %1834
       %6697 = OpCompositeExtract %uint %9840 0
      %21367 = OpCompositeExtract %uint %9840 1
      %10237 = OpCompositeExtract %bool %19067 1
      %11443 = OpSelect %uint %10237 %uint_1 %uint_0
       %6891 = OpExtInst %uint %1 UMax %21367 %11443
      %14763 = OpCompositeConstruct %v2uint %6697 %6891
      %21036 = OpIAdd %v2uint %14763 %23019
      %16075 = OpULessThanEqual %bool %16204 %uint_3
               OpSelectionMerge %8490 None
               OpBranchConditional %16075 %21993 %19371
      %21993 = OpLabel
               OpBranch %8490
      %19371 = OpLabel
      %15988 = OpIEqual %bool %16204 %uint_5
       %8438 = OpSelect %uint %15988 %uint_2 %uint_0
               OpBranch %8490
       %8490 = OpLabel
      %19300 = OpPhi %uint %16204 %21993 %8438 %19371
      %16830 = OpCompositeConstruct %v2uint %7177 %7177
      %11801 = OpUGreaterThanEqual %v2bool %16830 %1837
      %19381 = OpSelect %v2uint %11801 %1828 %1807
      %10986 = OpShiftLeftLogical %v2uint %21036 %19381
      %24669 = OpCompositeConstruct %v2uint %19300 %19300
       %9093 = OpShiftRightLogical %v2uint %24669 %1816
      %16072 = OpBitwiseAnd %v2uint %9093 %1828
      %20272 = OpIAdd %v2uint %10986 %16072
      %21145 = OpIMul %v2uint %2719 %23601
      %14725 = OpShiftRightLogical %v2uint %21145 %1807
      %19799 = OpUDiv %v2uint %20272 %14725
      %20390 = OpCompositeExtract %uint %19799 1
      %11046 = OpIMul %uint %20390 %20561
      %24665 = OpCompositeExtract %uint %19799 0
      %21536 = OpIAdd %uint %11046 %24665
       %8742 = OpIAdd %uint %24434 %21536
       %6459 = OpIMul %v2uint %19799 %14725
      %14279 = OpISub %v2uint %20272 %6459
               OpSelectionMerge %18756 None
               OpBranchConditional %20495 %11888 %18756
      %11888 = OpLabel
      %16985 = OpCompositeExtract %uint %14725 0
      %13307 = OpShiftRightLogical %uint %16985 %uint_1
      %22207 = OpCompositeExtract %uint %14279 0
      %15197 = OpBitcast %int %22207
      %15736 = OpUGreaterThanEqual %bool %22207 %13307
               OpSelectionMerge %21237 None
               OpBranchConditional %15736 %22228 %7940
      %22228 = OpLabel
      %22920 = OpBitcast %int %13307
      %17199 = OpSNegate %int %22920
               OpBranch %21237
       %7940 = OpLabel
      %16658 = OpBitcast %int %13307
               OpBranch %21237
      %21237 = OpLabel
      %10046 = OpPhi %int %17199 %22228 %16658 %7940
      %11983 = OpIAdd %int %15197 %10046
      %17709 = OpBitcast %uint %11983
      %21574 = OpCompositeInsert %v2uint %17709 %14279 0
               OpBranch %18756
      %18756 = OpLabel
      %17360 = OpPhi %v2uint %14279 %8490 %21574 %21237
      %24023 = OpCompositeExtract %uint %21145 0
      %22303 = OpCompositeExtract %uint %21145 1
      %13170 = OpIMul %uint %24023 %22303
      %14551 = OpIMul %uint %8742 %13170
       %6805 = OpCompositeExtract %uint %17360 1
      %23526 = OpCompositeExtract %uint %14725 0
      %22886 = OpIMul %uint %6805 %23526
       %6886 = OpCompositeExtract %uint %17360 0
       %9696 = OpIAdd %uint %22886 %6886
      %19199 = OpShiftLeftLogical %uint %9696 %uint_0
       %6269 = OpIAdd %uint %14551 %19199
      %24307 = OpShiftRightLogical %uint %6269 %uint_2
      %19601 = OpAccessChain %_ptr_Uniform_v4uint %3152 %int_0 %24307
      %12609 = OpLoad %v4uint %19601
      %11687 = OpIAdd %uint %24307 %uint_1
      %24577 = OpAccessChain %_ptr_Uniform_v4uint %3152 %int_0 %11687
      %16168 = OpLoad %v4uint %24577
      %12971 = OpCompositeExtract %bool %19067 0
               OpSelectionMerge %15698 None
               OpBranchConditional %12971 %16607 %15698
      %16607 = OpLabel
      %18778 = OpIEqual %bool %6697 %uint_0
               OpBranch %15698
      %15698 = OpLabel
      %10924 = OpPhi %bool %12971 %18756 %18778 %16607
               OpSelectionMerge %21910 None
               OpBranchConditional %10924 %9760 %21910
       %9760 = OpLabel
      %20482 = OpCompositeExtract %uint %12609 1
      %14335 = OpCompositeInsert %v4uint %20482 %12609 0
               OpBranch %21910
      %21910 = OpLabel
      %10925 = OpPhi %v4uint %12609 %15698 %14335 %9760
               OpSelectionMerge %21263 DontFlatten
               OpBranchConditional %19573 %22395 %21263
      %22395 = OpLabel
               OpSelectionMerge %14836 None
               OpSwitch %9130 %14836 0 %10391 1 %10391 2 %21920 3 %21920 10 %21920 12 %21920
      %10391 = OpLabel
      %15273 = OpBitwiseAnd %v4uint %10925 %1838
      %23564 = OpBitwiseAnd %v4uint %10925 %1611
      %24837 = OpShiftLeftLogical %v4uint %23564 %749
      %18005 = OpBitwiseOr %v4uint %15273 %24837
      %23170 = OpShiftRightLogical %v4uint %10925 %749
       %6442 = OpBitwiseAnd %v4uint %23170 %1611
      %15589 = OpBitwiseOr %v4uint %18005 %6442
      %19519 = OpBitwiseAnd %v4uint %16168 %1838
      %17946 = OpBitwiseAnd %v4uint %16168 %1611
      %24838 = OpShiftLeftLogical %v4uint %17946 %749
      %18006 = OpBitwiseOr %v4uint %19519 %24838
      %23171 = OpShiftRightLogical %v4uint %16168 %749
       %7392 = OpBitwiseAnd %v4uint %23171 %1611
       %7870 = OpBitwiseOr %v4uint %18006 %7392
               OpBranch %14836
      %21920 = OpLabel
      %20117 = OpBitwiseAnd %v4uint %10925 %2352
      %23565 = OpBitwiseAnd %v4uint %10925 %929
      %24839 = OpShiftLeftLogical %v4uint %23565 %965
      %18007 = OpBitwiseOr %v4uint %20117 %24839
      %23172 = OpShiftRightLogical %v4uint %10925 %965
       %6443 = OpBitwiseAnd %v4uint %23172 %929
      %15590 = OpBitwiseOr %v4uint %18007 %6443
      %19520 = OpBitwiseAnd %v4uint %16168 %2352
      %17947 = OpBitwiseAnd %v4uint %16168 %929
      %24840 = OpShiftLeftLogical %v4uint %17947 %965
      %18008 = OpBitwiseOr %v4uint %19520 %24840
      %23173 = OpShiftRightLogical %v4uint %16168 %965
       %7393 = OpBitwiseAnd %v4uint %23173 %929
       %7871 = OpBitwiseOr %v4uint %18008 %7393
               OpBranch %14836
      %14836 = OpLabel
      %11251 = OpPhi %v4uint %16168 %22395 %7870 %10391 %7871 %21920
      %13709 = OpPhi %v4uint %10925 %22395 %15589 %10391 %15590 %21920
               OpBranch %21263
      %21263 = OpLabel
       %8952 = OpPhi %v4uint %16168 %21910 %11251 %14836
      %18855 = OpPhi %v4uint %10925 %21910 %13709 %14836
      %13755 = OpIAdd %v2uint %9840 %23020
      %13244 = OpCompositeExtract %uint %13755 0
       %9555 = OpCompositeExtract %uint %13755 1
      %11053 = OpShiftRightLogical %uint %13244 %uint_2
       %7832 = OpCompositeConstruct %v2uint %11053 %9555
      %24920 = OpUDiv %v2uint %7832 %23601
      %13932 = OpCompositeExtract %uint %24920 0
      %19770 = OpShiftLeftLogical %uint %13932 %uint_2
      %24251 = OpCompositeExtract %uint %24920 1
      %21452 = OpCompositeConstruct %v3uint %19770 %24251 %23037
               OpSelectionMerge %21313 DontFlatten
               OpBranchConditional %20496 %21373 %11737
      %21373 = OpLabel
      %10608 = OpBitcast %v3int %21452
      %17090 = OpCompositeExtract %int %10608 1
       %9469 = OpShiftRightArithmetic %int %17090 %int_4
      %10055 = OpCompositeExtract %int %10608 2
      %16476 = OpShiftRightArithmetic %int %10055 %int_2
      %23373 = OpShiftRightLogical %uint %25203 %uint_4
       %6314 = OpBitcast %int %23373
      %21281 = OpIMul %int %16476 %6314
      %15143 = OpIAdd %int %9469 %21281
       %9032 = OpShiftRightLogical %uint %15783 %uint_5
      %12427 = OpBitcast %int %9032
      %10360 = OpIMul %int %15143 %12427
      %25154 = OpCompositeExtract %int %10608 0
      %20423 = OpShiftRightArithmetic %int %25154 %int_5
      %18940 = OpIAdd %int %20423 %10360
       %8797 = OpShiftLeftLogical %int %18940 %uint_8
      %11510 = OpBitwiseAnd %int %8797 %int_268435455
      %18938 = OpShiftLeftLogical %int %11510 %int_1
      %19768 = OpBitwiseAnd %int %25154 %int_7
      %12600 = OpBitwiseAnd %int %17090 %int_6
      %17741 = OpShiftLeftLogical %int %12600 %int_2
      %17227 = OpIAdd %int %19768 %17741
       %7048 = OpShiftLeftLogical %int %17227 %uint_8
      %24035 = OpShiftRightArithmetic %int %7048 %int_6
       %8725 = OpShiftRightArithmetic %int %17090 %int_3
      %13731 = OpIAdd %int %8725 %16476
      %23052 = OpBitwiseAnd %int %13731 %int_1
      %16659 = OpShiftRightArithmetic %int %25154 %int_3
      %18794 = OpShiftLeftLogical %int %23052 %int_1
      %13501 = OpIAdd %int %16659 %18794
      %19165 = OpBitwiseAnd %int %13501 %int_3
      %21578 = OpShiftLeftLogical %int %19165 %int_1
      %15435 = OpIAdd %int %23052 %21578
      %13150 = OpBitwiseAnd %int %24035 %int_n16
      %20336 = OpIAdd %int %18938 %13150
      %23345 = OpShiftLeftLogical %int %20336 %int_1
      %23274 = OpBitwiseAnd %int %24035 %int_15
      %10332 = OpIAdd %int %23345 %23274
      %18356 = OpBitwiseAnd %int %10055 %int_3
      %21579 = OpShiftLeftLogical %int %18356 %uint_8
      %16727 = OpIAdd %int %10332 %21579
      %19166 = OpBitwiseAnd %int %17090 %int_1
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
      %21849 = OpBitwiseAnd %int %16728 %int_63
      %24314 = OpIAdd %int %23348 %21849
      %22127 = OpBitcast %uint %24314
               OpBranch %21313
      %11737 = OpLabel
       %9761 = OpVectorShuffle %v2uint %21452 %21452 0 1
      %22991 = OpBitcast %v2int %9761
       %6403 = OpCompositeExtract %int %22991 0
       %9470 = OpShiftRightArithmetic %int %6403 %int_5
      %10056 = OpCompositeExtract %int %22991 1
      %16477 = OpShiftRightArithmetic %int %10056 %int_5
      %23374 = OpShiftRightLogical %uint %15783 %uint_5
       %6315 = OpBitcast %int %23374
      %21319 = OpIMul %int %16477 %6315
      %16222 = OpIAdd %int %9470 %21319
      %19086 = OpShiftLeftLogical %int %16222 %uint_9
      %10934 = OpBitwiseAnd %int %6403 %int_7
      %12601 = OpBitwiseAnd %int %10056 %int_14
      %17742 = OpShiftLeftLogical %int %12601 %int_2
      %17303 = OpIAdd %int %10934 %17742
       %6375 = OpShiftLeftLogical %int %17303 %uint_2
      %10161 = OpBitwiseAnd %int %6375 %int_n16
      %12150 = OpShiftLeftLogical %int %10161 %int_1
      %15436 = OpIAdd %int %19086 %12150
      %13207 = OpBitwiseAnd %int %6375 %int_15
      %19760 = OpIAdd %int %15436 %13207
      %18357 = OpBitwiseAnd %int %10056 %int_1
      %21581 = OpShiftLeftLogical %int %18357 %int_4
      %16729 = OpIAdd %int %19760 %21581
      %20514 = OpBitwiseAnd %int %16729 %int_n512
       %9238 = OpShiftLeftLogical %int %20514 %int_3
      %18995 = OpBitwiseAnd %int %10056 %int_16
      %12151 = OpShiftLeftLogical %int %18995 %int_7
      %16730 = OpIAdd %int %9238 %12151
      %19167 = OpBitwiseAnd %int %16729 %int_448
      %21582 = OpShiftLeftLogical %int %19167 %int_2
      %16708 = OpIAdd %int %16730 %21582
      %20611 = OpBitwiseAnd %int %10056 %int_8
      %16831 = OpShiftRightArithmetic %int %20611 %int_2
       %7916 = OpShiftRightArithmetic %int %6403 %int_3
      %13750 = OpIAdd %int %16831 %7916
      %21587 = OpBitwiseAnd %int %13750 %int_3
      %21583 = OpShiftLeftLogical %int %21587 %int_6
      %15437 = OpIAdd %int %16708 %21583
      %11782 = OpBitwiseAnd %int %16729 %int_63
      %14671 = OpIAdd %int %15437 %11782
      %22128 = OpBitcast %uint %14671
               OpBranch %21313
      %21313 = OpLabel
       %9468 = OpPhi %uint %22127 %21373 %22128 %11737
      %16296 = OpIMul %v2uint %24920 %23601
      %16262 = OpISub %v2uint %7832 %16296
      %17551 = OpCompositeExtract %uint %23601 1
      %23632 = OpIMul %uint %8858 %17551
      %15520 = OpIMul %uint %9468 %23632
      %16084 = OpCompositeExtract %uint %16262 0
      %15890 = OpIMul %uint %16084 %17551
       %6887 = OpCompositeExtract %uint %16262 1
      %11045 = OpIAdd %uint %15890 %6887
      %24733 = OpShiftLeftLogical %uint %11045 %uint_2
      %23219 = OpBitwiseAnd %uint %13244 %uint_3
       %9559 = OpIAdd %uint %24733 %23219
      %17811 = OpShiftLeftLogical %uint %9559 %uint_2
       %8264 = OpIAdd %uint %15520 %17811
       %8213 = OpShiftRightLogical %uint %8264 %uint_4
      %12010 = OpIEqual %bool %19164 %uint_1
      %22390 = OpIEqual %bool %19164 %uint_2
      %22150 = OpLogicalOr %bool %12010 %22390
               OpSelectionMerge %13411 None
               OpBranchConditional %22150 %10583 %13411
      %10583 = OpLabel
      %18271 = OpBitwiseAnd %v4uint %18855 %2510
       %9425 = OpShiftLeftLogical %v4uint %18271 %317
      %20652 = OpBitwiseAnd %v4uint %18855 %1838
      %17549 = OpShiftRightLogical %v4uint %20652 %317
      %16376 = OpBitwiseOr %v4uint %9425 %17549
               OpBranch %13411
      %13411 = OpLabel
      %22649 = OpPhi %v4uint %18855 %21313 %16376 %10583
      %19638 = OpIEqual %bool %19164 %uint_3
      %15139 = OpLogicalOr %bool %22390 %19638
               OpSelectionMerge %11416 None
               OpBranchConditional %15139 %11064 %11416
      %11064 = OpLabel
      %24087 = OpShiftLeftLogical %v4uint %22649 %749
      %15335 = OpShiftRightLogical %v4uint %22649 %749
      %10728 = OpBitwiseOr %v4uint %24087 %15335
               OpBranch %11416
      %11416 = OpLabel
      %19767 = OpPhi %v4uint %22649 %13411 %10728 %11064
       %6590 = OpAccessChain %_ptr_Uniform_v4uint %5522 %int_0 %8213
               OpStore %6590 %19767
      %23542 = OpUGreaterThan %bool %8858 %uint_1
               OpSelectionMerge %24764 DontFlatten
               OpBranchConditional %23542 %10270 %20628
      %10270 = OpLabel
      %11476 = OpShiftRightLogical %uint %6697 %uint_2
       %7937 = OpUDiv %uint %11476 %8858
      %16891 = OpIMul %uint %7937 %8858
      %12657 = OpISub %uint %11476 %16891
       %9511 = OpIAdd %uint %12657 %uint_1
      %13375 = OpIEqual %bool %9511 %8858
               OpSelectionMerge %7917 None
               OpBranchConditional %13375 %22174 %8593
      %22174 = OpLabel
      %19289 = OpIMul %uint %uint_32 %8858
      %21519 = OpShiftLeftLogical %uint %12657 %uint_4
      %18757 = OpISub %uint %19289 %21519
               OpBranch %7917
       %8593 = OpLabel
               OpBranch %7917
       %7917 = OpLabel
      %10540 = OpPhi %uint %18757 %22174 %uint_16 %8593
               OpBranch %24764
      %20628 = OpLabel
               OpBranch %24764
      %24764 = OpLabel
      %10684 = OpPhi %uint %10540 %7917 %uint_32 %20628
      %18731 = OpIMul %uint %10684 %17551
      %19951 = OpShiftRightLogical %uint %18731 %uint_4
      %23410 = OpIAdd %uint %8213 %19951
               OpSelectionMerge %14874 None
               OpBranchConditional %22150 %10584 %14874
      %10584 = OpLabel
      %18272 = OpBitwiseAnd %v4uint %8952 %2510
       %9426 = OpShiftLeftLogical %v4uint %18272 %317
      %20653 = OpBitwiseAnd %v4uint %8952 %1838
      %17550 = OpShiftRightLogical %v4uint %20653 %317
      %16377 = OpBitwiseOr %v4uint %9426 %17550
               OpBranch %14874
      %14874 = OpLabel
      %10926 = OpPhi %v4uint %8952 %24764 %16377 %10584
               OpSelectionMerge %11417 None
               OpBranchConditional %15139 %11065 %11417
      %11065 = OpLabel
      %24088 = OpShiftLeftLogical %v4uint %10926 %749
      %15336 = OpShiftRightLogical %v4uint %10926 %749
      %10729 = OpBitwiseOr %v4uint %24088 %15336
               OpBranch %11417
      %11417 = OpLabel
      %19769 = OpPhi %v4uint %10926 %14874 %10729 %11065
       %8053 = OpAccessChain %_ptr_Uniform_v4uint %5522 %int_0 %23410
               OpStore %8053 %19769
               OpBranch %19578
      %19578 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_fast_32bpp_1x2xmsaa_scaled_cs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006274, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F48, 0x00060010, 0x0000161F,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00050048, 0x000003F9,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x000003F9, 0x00000001,
    0x00000023, 0x00000004, 0x00050048, 0x000003F9, 0x00000002, 0x00000023,
    0x00000008, 0x00050048, 0x000003F9, 0x00000003, 0x00000023, 0x0000000C,
    0x00030047, 0x000003F9, 0x00000002, 0x00040047, 0x00000F48, 0x0000000B,
    0x0000001C, 0x00040047, 0x000007DC, 0x00000006, 0x00000010, 0x00040048,
    0x000007B4, 0x00000000, 0x00000018, 0x00050048, 0x000007B4, 0x00000000,
    0x00000023, 0x00000000, 0x00030047, 0x000007B4, 0x00000003, 0x00040047,
    0x00000C50, 0x00000022, 0x00000001, 0x00040047, 0x00000C50, 0x00000021,
    0x00000000, 0x00040047, 0x000007DD, 0x00000006, 0x00000010, 0x00040048,
    0x000007B5, 0x00000000, 0x00000019, 0x00050048, 0x000007B5, 0x00000000,
    0x00000023, 0x00000000, 0x00030047, 0x000007B5, 0x00000003, 0x00040047,
    0x00001592, 0x00000022, 0x00000000, 0x00040047, 0x00001592, 0x00000021,
    0x00000000, 0x00040047, 0x00000AC7, 0x0000000B, 0x00000019, 0x00020013,
    0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00020014, 0x00000009,
    0x00040017, 0x0000000F, 0x00000009, 0x00000002, 0x00040015, 0x0000000B,
    0x00000020, 0x00000000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002,
    0x00040017, 0x00000017, 0x0000000B, 0x00000004, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x00040017, 0x00000012, 0x0000000C, 0x00000002,
    0x00040017, 0x00000016, 0x0000000C, 0x00000003, 0x00040017, 0x00000014,
    0x0000000B, 0x00000003, 0x0004002B, 0x0000000B, 0x00000A0D, 0x00000001,
    0x0004002B, 0x0000000B, 0x00000A10, 0x00000002, 0x0004002B, 0x0000000B,
    0x000008A6, 0x00FF00FF, 0x0004002B, 0x0000000B, 0x00000A22, 0x00000008,
    0x0004002B, 0x0000000B, 0x000005FD, 0xFF00FF00, 0x0004002B, 0x0000000B,
    0x00000A13, 0x00000003, 0x0004002B, 0x0000000B, 0x00000A3A, 0x00000010,
    0x0005002C, 0x00000011, 0x0000072D, 0x00000A10, 0x00000A0D, 0x0004002B,
    0x0000000B, 0x00000A0A, 0x00000000, 0x0005002C, 0x00000011, 0x0000070F,
    0x00000A0A, 0x00000A0A, 0x0005002C, 0x00000011, 0x00000724, 0x00000A0D,
    0x00000A0D, 0x0005002C, 0x00000011, 0x00000718, 0x00000A0D, 0x00000A0A,
    0x0004002B, 0x0000000B, 0x00000AFA, 0x00000050, 0x0005002C, 0x00000011,
    0x00000A9F, 0x00000AFA, 0x00000A3A, 0x0004002B, 0x0000000C, 0x00000A1A,
    0x00000005, 0x0004002B, 0x0000000B, 0x00000A19, 0x00000005, 0x0004002B,
    0x0000000B, 0x00000A1F, 0x00000007, 0x0004002B, 0x0000000C, 0x00000A20,
    0x00000007, 0x0004002B, 0x0000000C, 0x00000A35, 0x0000000E, 0x0004002B,
    0x0000000C, 0x00000A11, 0x00000002, 0x0004002B, 0x0000000C, 0x000009DB,
    0xFFFFFFF0, 0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001, 0x0004002B,
    0x0000000C, 0x00000A38, 0x0000000F, 0x0004002B, 0x0000000C, 0x00000A17,
    0x00000004, 0x0004002B, 0x0000000C, 0x0000040B, 0xFFFFFE00, 0x0004002B,
    0x0000000C, 0x00000A14, 0x00000003, 0x0004002B, 0x0000000C, 0x00000A3B,
    0x00000010, 0x0004002B, 0x0000000C, 0x00000388, 0x000001C0, 0x0004002B,
    0x0000000C, 0x00000A23, 0x00000008, 0x0004002B, 0x0000000C, 0x00000A1D,
    0x00000006, 0x0004002B, 0x0000000C, 0x00000AC8, 0x0000003F, 0x0004002B,
    0x0000000B, 0x00000A16, 0x00000004, 0x0004002B, 0x0000000C, 0x0000078B,
    0x0FFFFFFF, 0x0004002B, 0x0000000C, 0x00000A05, 0xFFFFFFFE, 0x0004002B,
    0x0000000B, 0x00000A6A, 0x00000020, 0x0006001E, 0x000003F9, 0x0000000B,
    0x0000000B, 0x0000000B, 0x0000000B, 0x00040020, 0x00000676, 0x00000009,
    0x000003F9, 0x0004003B, 0x00000676, 0x0000118F, 0x00000009, 0x0004002B,
    0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x00000288, 0x00000009,
    0x0000000B, 0x0004002B, 0x0000000B, 0x00000A44, 0x000003FF, 0x0004002B,
    0x0000000B, 0x00000A28, 0x0000000A, 0x0004002B, 0x0000000B, 0x00000AFE,
    0x00001000, 0x0004002B, 0x0000000B, 0x00000A31, 0x0000000D, 0x0004002B,
    0x0000000B, 0x00000AFB, 0x00000FFF, 0x0004002B, 0x0000000B, 0x00000A55,
    0x00000019, 0x0004002B, 0x0000000B, 0x00000A37, 0x0000000F, 0x0004002B,
    0x0000000B, 0x00000A61, 0x0000001D, 0x0004002B, 0x0000000B, 0x00000A5B,
    0x0000001B, 0x0005002C, 0x00000011, 0x0000095E, 0x00000A5B, 0x00000A61,
    0x0004002B, 0x0000000B, 0x00000018, 0x40000000, 0x0003002A, 0x00000009,
    0x00000787, 0x0005002C, 0x00000011, 0x0000073F, 0x00000A0A, 0x00000A16,
    0x0005002C, 0x00000011, 0x00000740, 0x00000A16, 0x00000A0D, 0x0004002B,
    0x0000000B, 0x00000A81, 0x000007FF, 0x0004002B, 0x0000000B, 0x00000926,
    0x01000000, 0x0004002B, 0x0000000B, 0x00000A46, 0x00000014, 0x0004002B,
    0x0000000B, 0x00000A52, 0x00000018, 0x0005002C, 0x00000011, 0x000008E3,
    0x00000A46, 0x00000A52, 0x0004002B, 0x0000000B, 0x00000A5E, 0x0000001C,
    0x0004002B, 0x0000000B, 0x00000144, 0x000000FF, 0x0004002B, 0x0000000B,
    0x00000B54, 0xC00FFC00, 0x00040020, 0x00000291, 0x00000001, 0x00000014,
    0x0004003B, 0x00000291, 0x00000F48, 0x00000001, 0x00040020, 0x00000289,
    0x00000001, 0x0000000B, 0x0005002C, 0x00000011, 0x0000072A, 0x00000A13,
    0x00000A0A, 0x0003001D, 0x000007DC, 0x00000017, 0x0003001E, 0x000007B4,
    0x000007DC, 0x00040020, 0x00000A32, 0x00000002, 0x000007B4, 0x0004003B,
    0x00000A32, 0x00000C50, 0x00000002, 0x00040020, 0x00000294, 0x00000002,
    0x00000017, 0x0003001D, 0x000007DD, 0x00000017, 0x0003001E, 0x000007B5,
    0x000007DD, 0x00040020, 0x00000A33, 0x00000002, 0x000007B5, 0x0004003B,
    0x00000A33, 0x00001592, 0x00000002, 0x0006002C, 0x00000014, 0x00000AC7,
    0x00000A22, 0x00000A22, 0x00000A0D, 0x0005002C, 0x00000011, 0x0000074E,
    0x00000A13, 0x00000A13, 0x0005002C, 0x0000000F, 0x0000013B, 0x00000787,
    0x00000787, 0x0005002C, 0x00000011, 0x0000084A, 0x00000A37, 0x00000A37,
    0x0007002C, 0x00000017, 0x0000072E, 0x000005FD, 0x000005FD, 0x000005FD,
    0x000005FD, 0x0007002C, 0x00000017, 0x0000064B, 0x00000144, 0x00000144,
    0x00000144, 0x00000144, 0x0007002C, 0x00000017, 0x000002ED, 0x00000A3A,
    0x00000A3A, 0x00000A3A, 0x00000A3A, 0x0007002C, 0x00000017, 0x00000930,
    0x00000B54, 0x00000B54, 0x00000B54, 0x00000B54, 0x0007002C, 0x00000017,
    0x000003A1, 0x00000A44, 0x00000A44, 0x00000A44, 0x00000A44, 0x0007002C,
    0x00000017, 0x000003C5, 0x00000A46, 0x00000A46, 0x00000A46, 0x00000A46,
    0x0004002B, 0x0000000B, 0x00000A25, 0x00000009, 0x0007002C, 0x00000017,
    0x000009CE, 0x000008A6, 0x000008A6, 0x000008A6, 0x000008A6, 0x0007002C,
    0x00000017, 0x0000013D, 0x00000A22, 0x00000A22, 0x00000A22, 0x00000A22,
    0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8,
    0x00003B06, 0x000300F7, 0x00004C7A, 0x00000000, 0x000300FB, 0x00000A0A,
    0x00002E68, 0x000200F8, 0x00002E68, 0x00050041, 0x00000288, 0x000056E5,
    0x0000118F, 0x00000A0B, 0x0004003D, 0x0000000B, 0x00003D0B, 0x000056E5,
    0x00050041, 0x00000288, 0x000058AC, 0x0000118F, 0x00000A0E, 0x0004003D,
    0x0000000B, 0x00005158, 0x000058AC, 0x000500C7, 0x0000000B, 0x00005051,
    0x00003D0B, 0x00000A44, 0x000500C2, 0x0000000B, 0x00004E69, 0x00003D0B,
    0x00000A28, 0x000500C7, 0x0000000B, 0x00001C09, 0x00004E69, 0x00000A13,
    0x000500C7, 0x0000000B, 0x000059EF, 0x00003D0B, 0x00000AFE, 0x000500AB,
    0x00000009, 0x0000500F, 0x000059EF, 0x00000A0A, 0x000500C2, 0x0000000B,
    0x00002843, 0x00003D0B, 0x00000A31, 0x000500C7, 0x0000000B, 0x00005F72,
    0x00002843, 0x00000AFB, 0x000500C2, 0x0000000B, 0x00004994, 0x00003D0B,
    0x00000A55, 0x000500C7, 0x0000000B, 0x000023AA, 0x00004994, 0x00000A37,
    0x00050050, 0x00000011, 0x000022A7, 0x00005158, 0x00005158, 0x000500C2,
    0x00000011, 0x000025A1, 0x000022A7, 0x0000095E, 0x000500C7, 0x00000011,
    0x00005C31, 0x000025A1, 0x0000074E, 0x000500C7, 0x0000000B, 0x00005DDE,
    0x00003D0B, 0x00000018, 0x000500AB, 0x00000009, 0x00003007, 0x00005DDE,
    0x00000A0A, 0x000300F7, 0x00001AD7, 0x00000000, 0x000400FA, 0x00003007,
    0x00003F85, 0x000027C5, 0x000200F8, 0x00003F85, 0x000500AC, 0x0000000F,
    0x000053D7, 0x00005C31, 0x00000724, 0x000200F9, 0x00001AD7, 0x000200F8,
    0x000027C5, 0x000200F9, 0x00001AD7, 0x000200F8, 0x00001AD7, 0x000700F5,
    0x0000000F, 0x00004A7B, 0x000053D7, 0x00003F85, 0x0000013B, 0x000027C5,
    0x000500C2, 0x00000011, 0x0000189F, 0x000022A7, 0x0000073F, 0x000500C4,
    0x00000011, 0x00002A91, 0x00000724, 0x00000740, 0x00050082, 0x00000011,
    0x000048B0, 0x00002A91, 0x00000724, 0x000500C7, 0x00000011, 0x00004937,
    0x0000189F, 0x000048B0, 0x000500C4, 0x00000011, 0x00005784, 0x00004937,
    0x0000074E, 0x00050084, 0x00000011, 0x000059EB, 0x00005784, 0x00005C31,
    0x000500C2, 0x0000000B, 0x00003343, 0x00005158, 0x00000A19, 0x000500C7,
    0x0000000B, 0x000039C1, 0x00003343, 0x00000A81, 0x00050051, 0x0000000B,
    0x0000229A, 0x00005C31, 0x00000000, 0x00050084, 0x0000000B, 0x000059D1,
    0x000039C1, 0x0000229A, 0x00050041, 0x00000288, 0x00004E44, 0x0000118F,
    0x00000A11, 0x0004003D, 0x0000000B, 0x000048C4, 0x00004E44, 0x00050041,
    0x00000288, 0x000058AD, 0x0000118F, 0x00000A14, 0x0004003D, 0x0000000B,
    0x000051B7, 0x000058AD, 0x000500C7, 0x0000000B, 0x00004ADC, 0x000048C4,
    0x00000A1F, 0x000500C7, 0x0000000B, 0x000055EF, 0x000048C4, 0x00000A22,
    0x000500AB, 0x00000009, 0x00005010, 0x000055EF, 0x00000A0A, 0x000500C2,
    0x0000000B, 0x000028A2, 0x000048C4, 0x00000A16, 0x000500C7, 0x0000000B,
    0x000059FD, 0x000028A2, 0x00000A1F, 0x000500C7, 0x0000000B, 0x00005A4E,
    0x000048C4, 0x00000926, 0x000500AB, 0x00000009, 0x00004C75, 0x00005A4E,
    0x00000A0A, 0x000500C7, 0x0000000B, 0x00001F43, 0x000051B7, 0x00000A44,
    0x000500C4, 0x0000000B, 0x00003DA7, 0x00001F43, 0x00000A19, 0x000500C2,
    0x0000000B, 0x0000583F, 0x000051B7, 0x00000A28, 0x000500C7, 0x0000000B,
    0x00004BBE, 0x0000583F, 0x00000A44, 0x000500C4, 0x0000000B, 0x00006273,
    0x00004BBE, 0x00000A19, 0x00050050, 0x00000011, 0x000028B6, 0x000051B7,
    0x000051B7, 0x000500C2, 0x00000011, 0x00002891, 0x000028B6, 0x000008E3,
    0x000500C7, 0x00000011, 0x00005B53, 0x00002891, 0x0000084A, 0x000500C4,
    0x00000011, 0x00003F4F, 0x00005B53, 0x0000074E, 0x00050084, 0x00000011,
    0x000059EC, 0x00003F4F, 0x00005C31, 0x000500C2, 0x0000000B, 0x00003213,
    0x000051B7, 0x00000A5E, 0x000500C7, 0x0000000B, 0x00003F4C, 0x00003213,
    0x00000A1F, 0x00050041, 0x00000289, 0x00005143, 0x00000F48, 0x00000A0A,
    0x0004003D, 0x0000000B, 0x000022D1, 0x00005143, 0x000500AE, 0x00000009,
    0x00001CED, 0x000022D1, 0x000059D1, 0x000300F7, 0x00004427, 0x00000002,
    0x000400FA, 0x00001CED, 0x000055E8, 0x00004427, 0x000200F8, 0x000055E8,
    0x000200F9, 0x00004C7A, 0x000200F8, 0x00004427, 0x0004003D, 0x00000014,
    0x0000392D, 0x00000F48, 0x0007004F, 0x00000011, 0x00004849, 0x0000392D,
    0x0000392D, 0x00000000, 0x00000001, 0x000500C4, 0x00000011, 0x00002670,
    0x00004849, 0x0000072A, 0x00050051, 0x0000000B, 0x00001A29, 0x00002670,
    0x00000000, 0x00050051, 0x0000000B, 0x00005377, 0x00002670, 0x00000001,
    0x00050051, 0x00000009, 0x000027FD, 0x00004A7B, 0x00000001, 0x000600A9,
    0x0000000B, 0x00002CB3, 0x000027FD, 0x00000A0D, 0x00000A0A, 0x0007000C,
    0x0000000B, 0x00001AEB, 0x00000001, 0x00000029, 0x00005377, 0x00002CB3,
    0x00050050, 0x00000011, 0x000039AB, 0x00001A29, 0x00001AEB, 0x00050080,
    0x00000011, 0x0000522C, 0x000039AB, 0x000059EB, 0x000500B2, 0x00000009,
    0x00003ECB, 0x00003F4C, 0x00000A13, 0x000300F7, 0x0000212A, 0x00000000,
    0x000400FA, 0x00003ECB, 0x000055E9, 0x00004BAB, 0x000200F8, 0x000055E9,
    0x000200F9, 0x0000212A, 0x000200F8, 0x00004BAB, 0x000500AA, 0x00000009,
    0x00003E74, 0x00003F4C, 0x00000A19, 0x000600A9, 0x0000000B, 0x000020F6,
    0x00003E74, 0x00000A10, 0x00000A0A, 0x000200F9, 0x0000212A, 0x000200F8,
    0x0000212A, 0x000700F5, 0x0000000B, 0x00004B64, 0x00003F4C, 0x000055E9,
    0x000020F6, 0x00004BAB, 0x00050050, 0x00000011, 0x000041BE, 0x00001C09,
    0x00001C09, 0x000500AE, 0x0000000F, 0x00002E19, 0x000041BE, 0x0000072D,
    0x000600A9, 0x00000011, 0x00004BB5, 0x00002E19, 0x00000724, 0x0000070F,
    0x000500C4, 0x00000011, 0x00002AEA, 0x0000522C, 0x00004BB5, 0x00050050,
    0x00000011, 0x0000605D, 0x00004B64, 0x00004B64, 0x000500C2, 0x00000011,
    0x00002385, 0x0000605D, 0x00000718, 0x000500C7, 0x00000011, 0x00003EC8,
    0x00002385, 0x00000724, 0x00050080, 0x00000011, 0x00004F30, 0x00002AEA,
    0x00003EC8, 0x00050084, 0x00000011, 0x00005299, 0x00000A9F, 0x00005C31,
    0x000500C2, 0x00000011, 0x00003985, 0x00005299, 0x0000070F, 0x00050086,
    0x00000011, 0x00004D57, 0x00004F30, 0x00003985, 0x00050051, 0x0000000B,
    0x00004FA6, 0x00004D57, 0x00000001, 0x00050084, 0x0000000B, 0x00002B26,
    0x00004FA6, 0x00005051, 0x00050051, 0x0000000B, 0x00006059, 0x00004D57,
    0x00000000, 0x00050080, 0x0000000B, 0x00005420, 0x00002B26, 0x00006059,
    0x00050080, 0x0000000B, 0x00002226, 0x00005F72, 0x00005420, 0x00050084,
    0x00000011, 0x0000193B, 0x00004D57, 0x00003985, 0x00050082, 0x00000011,
    0x000037C7, 0x00004F30, 0x0000193B, 0x000300F7, 0x00004944, 0x00000000,
    0x000400FA, 0x0000500F, 0x00002E70, 0x00004944, 0x000200F8, 0x00002E70,
    0x00050051, 0x0000000B, 0x00004259, 0x00003985, 0x00000000, 0x000500C2,
    0x0000000B, 0x000033FB, 0x00004259, 0x00000A0D, 0x00050051, 0x0000000B,
    0x000056BF, 0x000037C7, 0x00000000, 0x0004007C, 0x0000000C, 0x00003B5D,
    0x000056BF, 0x000500AE, 0x00000009, 0x00003D78, 0x000056BF, 0x000033FB,
    0x000300F7, 0x000052F5, 0x00000000, 0x000400FA, 0x00003D78, 0x000056D4,
    0x00001F04, 0x000200F8, 0x000056D4, 0x0004007C, 0x0000000C, 0x00005988,
    0x000033FB, 0x0004007E, 0x0000000C, 0x0000432F, 0x00005988, 0x000200F9,
    0x000052F5, 0x000200F8, 0x00001F04, 0x0004007C, 0x0000000C, 0x00004112,
    0x000033FB, 0x000200F9, 0x000052F5, 0x000200F8, 0x000052F5, 0x000700F5,
    0x0000000C, 0x0000273E, 0x0000432F, 0x000056D4, 0x00004112, 0x00001F04,
    0x00050080, 0x0000000C, 0x00002ECF, 0x00003B5D, 0x0000273E, 0x0004007C,
    0x0000000B, 0x0000452D, 0x00002ECF, 0x00060052, 0x00000011, 0x00005446,
    0x0000452D, 0x000037C7, 0x00000000, 0x000200F9, 0x00004944, 0x000200F8,
    0x00004944, 0x000700F5, 0x00000011, 0x000043D0, 0x000037C7, 0x0000212A,
    0x00005446, 0x000052F5, 0x00050051, 0x0000000B, 0x00005DD7, 0x00005299,
    0x00000000, 0x00050051, 0x0000000B, 0x0000571F, 0x00005299, 0x00000001,
    0x00050084, 0x0000000B, 0x00003372, 0x00005DD7, 0x0000571F, 0x00050084,
    0x0000000B, 0x000038D7, 0x00002226, 0x00003372, 0x00050051, 0x0000000B,
    0x00001A95, 0x000043D0, 0x00000001, 0x00050051, 0x0000000B, 0x00005BE6,
    0x00003985, 0x00000000, 0x00050084, 0x0000000B, 0x00005966, 0x00001A95,
    0x00005BE6, 0x00050051, 0x0000000B, 0x00001AE6, 0x000043D0, 0x00000000,
    0x00050080, 0x0000000B, 0x000025E0, 0x00005966, 0x00001AE6, 0x000500C4,
    0x0000000B, 0x00004AFF, 0x000025E0, 0x00000A0A, 0x00050080, 0x0000000B,
    0x0000187D, 0x000038D7, 0x00004AFF, 0x000500C2, 0x0000000B, 0x00005EF3,
    0x0000187D, 0x00000A10, 0x00060041, 0x00000294, 0x00004C91, 0x00000C50,
    0x00000A0B, 0x00005EF3, 0x0004003D, 0x00000017, 0x00003141, 0x00004C91,
    0x00050080, 0x0000000B, 0x00002DA7, 0x00005EF3, 0x00000A0D, 0x00060041,
    0x00000294, 0x00006001, 0x00000C50, 0x00000A0B, 0x00002DA7, 0x0004003D,
    0x00000017, 0x00003F28, 0x00006001, 0x00050051, 0x00000009, 0x000032AB,
    0x00004A7B, 0x00000000, 0x000300F7, 0x00003D52, 0x00000000, 0x000400FA,
    0x000032AB, 0x000040DF, 0x00003D52, 0x000200F8, 0x000040DF, 0x000500AA,
    0x00000009, 0x0000495A, 0x00001A29, 0x00000A0A, 0x000200F9, 0x00003D52,
    0x000200F8, 0x00003D52, 0x000700F5, 0x00000009, 0x00002AAC, 0x000032AB,
    0x00004944, 0x0000495A, 0x000040DF, 0x000300F7, 0x00005596, 0x00000000,
    0x000400FA, 0x00002AAC, 0x00002620, 0x00005596, 0x000200F8, 0x00002620,
    0x00050051, 0x0000000B, 0x00005002, 0x00003141, 0x00000001, 0x00060052,
    0x00000017, 0x000037FF, 0x00005002, 0x00003141, 0x00000000, 0x000200F9,
    0x00005596, 0x000200F8, 0x00005596, 0x000700F5, 0x00000017, 0x00002AAD,
    0x00003141, 0x00003D52, 0x000037FF, 0x00002620, 0x000300F7, 0x0000530F,
    0x00000002, 0x000400FA, 0x00004C75, 0x0000577B, 0x0000530F, 0x000200F8,
    0x0000577B, 0x000300F7, 0x000039F4, 0x00000000, 0x000F00FB, 0x000023AA,
    0x000039F4, 0x00000000, 0x00002897, 0x00000001, 0x00002897, 0x00000002,
    0x000055A0, 0x00000003, 0x000055A0, 0x0000000A, 0x000055A0, 0x0000000C,
    0x000055A0, 0x000200F8, 0x00002897, 0x000500C7, 0x00000017, 0x00003BA9,
    0x00002AAD, 0x0000072E, 0x000500C7, 0x00000017, 0x00005C0C, 0x00002AAD,
    0x0000064B, 0x000500C4, 0x00000017, 0x00006105, 0x00005C0C, 0x000002ED,
    0x000500C5, 0x00000017, 0x00004655, 0x00003BA9, 0x00006105, 0x000500C2,
    0x00000017, 0x00005A82, 0x00002AAD, 0x000002ED, 0x000500C7, 0x00000017,
    0x0000192A, 0x00005A82, 0x0000064B, 0x000500C5, 0x00000017, 0x00003CE5,
    0x00004655, 0x0000192A, 0x000500C7, 0x00000017, 0x00004C3F, 0x00003F28,
    0x0000072E, 0x000500C7, 0x00000017, 0x0000461A, 0x00003F28, 0x0000064B,
    0x000500C4, 0x00000017, 0x00006106, 0x0000461A, 0x000002ED, 0x000500C5,
    0x00000017, 0x00004656, 0x00004C3F, 0x00006106, 0x000500C2, 0x00000017,
    0x00005A83, 0x00003F28, 0x000002ED, 0x000500C7, 0x00000017, 0x00001CE0,
    0x00005A83, 0x0000064B, 0x000500C5, 0x00000017, 0x00001EBE, 0x00004656,
    0x00001CE0, 0x000200F9, 0x000039F4, 0x000200F8, 0x000055A0, 0x000500C7,
    0x00000017, 0x00004E95, 0x00002AAD, 0x00000930, 0x000500C7, 0x00000017,
    0x00005C0D, 0x00002AAD, 0x000003A1, 0x000500C4, 0x00000017, 0x00006107,
    0x00005C0D, 0x000003C5, 0x000500C5, 0x00000017, 0x00004657, 0x00004E95,
    0x00006107, 0x000500C2, 0x00000017, 0x00005A84, 0x00002AAD, 0x000003C5,
    0x000500C7, 0x00000017, 0x0000192B, 0x00005A84, 0x000003A1, 0x000500C5,
    0x00000017, 0x00003CE6, 0x00004657, 0x0000192B, 0x000500C7, 0x00000017,
    0x00004C40, 0x00003F28, 0x00000930, 0x000500C7, 0x00000017, 0x0000461B,
    0x00003F28, 0x000003A1, 0x000500C4, 0x00000017, 0x00006108, 0x0000461B,
    0x000003C5, 0x000500C5, 0x00000017, 0x00004658, 0x00004C40, 0x00006108,
    0x000500C2, 0x00000017, 0x00005A85, 0x00003F28, 0x000003C5, 0x000500C7,
    0x00000017, 0x00001CE1, 0x00005A85, 0x000003A1, 0x000500C5, 0x00000017,
    0x00001EBF, 0x00004658, 0x00001CE1, 0x000200F9, 0x000039F4, 0x000200F8,
    0x000039F4, 0x000900F5, 0x00000017, 0x00002BF3, 0x00003F28, 0x0000577B,
    0x00001EBE, 0x00002897, 0x00001EBF, 0x000055A0, 0x000900F5, 0x00000017,
    0x0000358D, 0x00002AAD, 0x0000577B, 0x00003CE5, 0x00002897, 0x00003CE6,
    0x000055A0, 0x000200F9, 0x0000530F, 0x000200F8, 0x0000530F, 0x000700F5,
    0x00000017, 0x000022F8, 0x00003F28, 0x00005596, 0x00002BF3, 0x000039F4,
    0x000700F5, 0x00000017, 0x000049A7, 0x00002AAD, 0x00005596, 0x0000358D,
    0x000039F4, 0x00050080, 0x00000011, 0x000035BB, 0x00002670, 0x000059EC,
    0x00050051, 0x0000000B, 0x000033BC, 0x000035BB, 0x00000000, 0x00050051,
    0x0000000B, 0x00002553, 0x000035BB, 0x00000001, 0x000500C2, 0x0000000B,
    0x00002B2D, 0x000033BC, 0x00000A10, 0x00050050, 0x00000011, 0x00001E98,
    0x00002B2D, 0x00002553, 0x00050086, 0x00000011, 0x00006158, 0x00001E98,
    0x00005C31, 0x00050051, 0x0000000B, 0x0000366C, 0x00006158, 0x00000000,
    0x000500C4, 0x0000000B, 0x00004D3A, 0x0000366C, 0x00000A10, 0x00050051,
    0x0000000B, 0x00005EBB, 0x00006158, 0x00000001, 0x00060050, 0x00000014,
    0x000053CC, 0x00004D3A, 0x00005EBB, 0x000059FD, 0x000300F7, 0x00005341,
    0x00000002, 0x000400FA, 0x00005010, 0x0000537D, 0x00002DD9, 0x000200F8,
    0x0000537D, 0x0004007C, 0x00000016, 0x00002970, 0x000053CC, 0x00050051,
    0x0000000C, 0x000042C2, 0x00002970, 0x00000001, 0x000500C3, 0x0000000C,
    0x000024FD, 0x000042C2, 0x00000A17, 0x00050051, 0x0000000C, 0x00002747,
    0x00002970, 0x00000002, 0x000500C3, 0x0000000C, 0x0000405C, 0x00002747,
    0x00000A11, 0x000500C2, 0x0000000B, 0x00005B4D, 0x00006273, 0x00000A16,
    0x0004007C, 0x0000000C, 0x000018AA, 0x00005B4D, 0x00050084, 0x0000000C,
    0x00005321, 0x0000405C, 0x000018AA, 0x00050080, 0x0000000C, 0x00003B27,
    0x000024FD, 0x00005321, 0x000500C2, 0x0000000B, 0x00002348, 0x00003DA7,
    0x00000A19, 0x0004007C, 0x0000000C, 0x0000308B, 0x00002348, 0x00050084,
    0x0000000C, 0x00002878, 0x00003B27, 0x0000308B, 0x00050051, 0x0000000C,
    0x00006242, 0x00002970, 0x00000000, 0x000500C3, 0x0000000C, 0x00004FC7,
    0x00006242, 0x00000A1A, 0x00050080, 0x0000000C, 0x000049FC, 0x00004FC7,
    0x00002878, 0x000500C4, 0x0000000C, 0x0000225D, 0x000049FC, 0x00000A22,
    0x000500C7, 0x0000000C, 0x00002CF6, 0x0000225D, 0x0000078B, 0x000500C4,
    0x0000000C, 0x000049FA, 0x00002CF6, 0x00000A0E, 0x000500C7, 0x0000000C,
    0x00004D38, 0x00006242, 0x00000A20, 0x000500C7, 0x0000000C, 0x00003138,
    0x000042C2, 0x00000A1D, 0x000500C4, 0x0000000C, 0x0000454D, 0x00003138,
    0x00000A11, 0x00050080, 0x0000000C, 0x0000434B, 0x00004D38, 0x0000454D,
    0x000500C4, 0x0000000C, 0x00001B88, 0x0000434B, 0x00000A22, 0x000500C3,
    0x0000000C, 0x00005DE3, 0x00001B88, 0x00000A1D, 0x000500C3, 0x0000000C,
    0x00002215, 0x000042C2, 0x00000A14, 0x00050080, 0x0000000C, 0x000035A3,
    0x00002215, 0x0000405C, 0x000500C7, 0x0000000C, 0x00005A0C, 0x000035A3,
    0x00000A0E, 0x000500C3, 0x0000000C, 0x00004113, 0x00006242, 0x00000A14,
    0x000500C4, 0x0000000C, 0x0000496A, 0x00005A0C, 0x00000A0E, 0x00050080,
    0x0000000C, 0x000034BD, 0x00004113, 0x0000496A, 0x000500C7, 0x0000000C,
    0x00004ADD, 0x000034BD, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544A,
    0x00004ADD, 0x00000A0E, 0x00050080, 0x0000000C, 0x00003C4B, 0x00005A0C,
    0x0000544A, 0x000500C7, 0x0000000C, 0x0000335E, 0x00005DE3, 0x000009DB,
    0x00050080, 0x0000000C, 0x00004F70, 0x000049FA, 0x0000335E, 0x000500C4,
    0x0000000C, 0x00005B31, 0x00004F70, 0x00000A0E, 0x000500C7, 0x0000000C,
    0x00005AEA, 0x00005DE3, 0x00000A38, 0x00050080, 0x0000000C, 0x0000285C,
    0x00005B31, 0x00005AEA, 0x000500C7, 0x0000000C, 0x000047B4, 0x00002747,
    0x00000A14, 0x000500C4, 0x0000000C, 0x0000544B, 0x000047B4, 0x00000A22,
    0x00050080, 0x0000000C, 0x00004157, 0x0000285C, 0x0000544B, 0x000500C7,
    0x0000000C, 0x00004ADE, 0x000042C2, 0x00000A0E, 0x000500C4, 0x0000000C,
    0x0000544C, 0x00004ADE, 0x00000A17, 0x00050080, 0x0000000C, 0x00004158,
    0x00004157, 0x0000544C, 0x000500C7, 0x0000000C, 0x00004FD6, 0x00003C4B,
    0x00000A0E, 0x000500C4, 0x0000000C, 0x00002703, 0x00004FD6, 0x00000A14,
    0x000500C3, 0x0000000C, 0x00003332, 0x00004158, 0x00000A1D, 0x000500C7,
    0x0000000C, 0x000036D6, 0x00003332, 0x00000A20, 0x00050080, 0x0000000C,
    0x00003412, 0x00002703, 0x000036D6, 0x000500C4, 0x0000000C, 0x00005B32,
    0x00003412, 0x00000A14, 0x000500C7, 0x0000000C, 0x00005AB1, 0x00003C4B,
    0x00000A05, 0x00050080, 0x0000000C, 0x00002A9C, 0x00005B32, 0x00005AB1,
    0x000500C4, 0x0000000C, 0x00005B33, 0x00002A9C, 0x00000A11, 0x000500C7,
    0x0000000C, 0x00005AB2, 0x00004158, 0x0000040B, 0x00050080, 0x0000000C,
    0x00002A9D, 0x00005B33, 0x00005AB2, 0x000500C4, 0x0000000C, 0x00005B34,
    0x00002A9D, 0x00000A14, 0x000500C7, 0x0000000C, 0x00005559, 0x00004158,
    0x00000AC8, 0x00050080, 0x0000000C, 0x00005EFA, 0x00005B34, 0x00005559,
    0x0004007C, 0x0000000B, 0x0000566F, 0x00005EFA, 0x000200F9, 0x00005341,
    0x000200F8, 0x00002DD9, 0x0007004F, 0x00000011, 0x00002621, 0x000053CC,
    0x000053CC, 0x00000000, 0x00000001, 0x0004007C, 0x00000012, 0x000059CF,
    0x00002621, 0x00050051, 0x0000000C, 0x00001903, 0x000059CF, 0x00000000,
    0x000500C3, 0x0000000C, 0x000024FE, 0x00001903, 0x00000A1A, 0x00050051,
    0x0000000C, 0x00002748, 0x000059CF, 0x00000001, 0x000500C3, 0x0000000C,
    0x0000405D, 0x00002748, 0x00000A1A, 0x000500C2, 0x0000000B, 0x00005B4E,
    0x00003DA7, 0x00000A19, 0x0004007C, 0x0000000C, 0x000018AB, 0x00005B4E,
    0x00050084, 0x0000000C, 0x00005347, 0x0000405D, 0x000018AB, 0x00050080,
    0x0000000C, 0x00003F5E, 0x000024FE, 0x00005347, 0x000500C4, 0x0000000C,
    0x00004A8E, 0x00003F5E, 0x00000A25, 0x000500C7, 0x0000000C, 0x00002AB6,
    0x00001903, 0x00000A20, 0x000500C7, 0x0000000C, 0x00003139, 0x00002748,
    0x00000A35, 0x000500C4, 0x0000000C, 0x0000454E, 0x00003139, 0x00000A11,
    0x00050080, 0x0000000C, 0x00004397, 0x00002AB6, 0x0000454E, 0x000500C4,
    0x0000000C, 0x000018E7, 0x00004397, 0x00000A10, 0x000500C7, 0x0000000C,
    0x000027B1, 0x000018E7, 0x000009DB, 0x000500C4, 0x0000000C, 0x00002F76,
    0x000027B1, 0x00000A0E, 0x00050080, 0x0000000C, 0x00003C4C, 0x00004A8E,
    0x00002F76, 0x000500C7, 0x0000000C, 0x00003397, 0x000018E7, 0x00000A38,
    0x00050080, 0x0000000C, 0x00004D30, 0x00003C4C, 0x00003397, 0x000500C7,
    0x0000000C, 0x000047B5, 0x00002748, 0x00000A0E, 0x000500C4, 0x0000000C,
    0x0000544D, 0x000047B5, 0x00000A17, 0x00050080, 0x0000000C, 0x00004159,
    0x00004D30, 0x0000544D, 0x000500C7, 0x0000000C, 0x00005022, 0x00004159,
    0x0000040B, 0x000500C4, 0x0000000C, 0x00002416, 0x00005022, 0x00000A14,
    0x000500C7, 0x0000000C, 0x00004A33, 0x00002748, 0x00000A3B, 0x000500C4,
    0x0000000C, 0x00002F77, 0x00004A33, 0x00000A20, 0x00050080, 0x0000000C,
    0x0000415A, 0x00002416, 0x00002F77, 0x000500C7, 0x0000000C, 0x00004ADF,
    0x00004159, 0x00000388, 0x000500C4, 0x0000000C, 0x0000544E, 0x00004ADF,
    0x00000A11, 0x00050080, 0x0000000C, 0x00004144, 0x0000415A, 0x0000544E,
    0x000500C7, 0x0000000C, 0x00005083, 0x00002748, 0x00000A23, 0x000500C3,
    0x0000000C, 0x000041BF, 0x00005083, 0x00000A11, 0x000500C3, 0x0000000C,
    0x00001EEC, 0x00001903, 0x00000A14, 0x00050080, 0x0000000C, 0x000035B6,
    0x000041BF, 0x00001EEC, 0x000500C7, 0x0000000C, 0x00005453, 0x000035B6,
    0x00000A14, 0x000500C4, 0x0000000C, 0x0000544F, 0x00005453, 0x00000A1D,
    0x00050080, 0x0000000C, 0x00003C4D, 0x00004144, 0x0000544F, 0x000500C7,
    0x0000000C, 0x00002E06, 0x00004159, 0x00000AC8, 0x00050080, 0x0000000C,
    0x0000394F, 0x00003C4D, 0x00002E06, 0x0004007C, 0x0000000B, 0x00005670,
    0x0000394F, 0x000200F9, 0x00005341, 0x000200F8, 0x00005341, 0x000700F5,
    0x0000000B, 0x000024FC, 0x0000566F, 0x0000537D, 0x00005670, 0x00002DD9,
    0x00050084, 0x00000011, 0x00003FA8, 0x00006158, 0x00005C31, 0x00050082,
    0x00000011, 0x00003F86, 0x00001E98, 0x00003FA8, 0x00050051, 0x0000000B,
    0x0000448F, 0x00005C31, 0x00000001, 0x00050084, 0x0000000B, 0x00005C50,
    0x0000229A, 0x0000448F, 0x00050084, 0x0000000B, 0x00003CA0, 0x000024FC,
    0x00005C50, 0x00050051, 0x0000000B, 0x00003ED4, 0x00003F86, 0x00000000,
    0x00050084, 0x0000000B, 0x00003E12, 0x00003ED4, 0x0000448F, 0x00050051,
    0x0000000B, 0x00001AE7, 0x00003F86, 0x00000001, 0x00050080, 0x0000000B,
    0x00002B25, 0x00003E12, 0x00001AE7, 0x000500C4, 0x0000000B, 0x0000609D,
    0x00002B25, 0x00000A10, 0x000500C7, 0x0000000B, 0x00005AB3, 0x000033BC,
    0x00000A13, 0x00050080, 0x0000000B, 0x00002557, 0x0000609D, 0x00005AB3,
    0x000500C4, 0x0000000B, 0x00004593, 0x00002557, 0x00000A10, 0x00050080,
    0x0000000B, 0x00002048, 0x00003CA0, 0x00004593, 0x000500C2, 0x0000000B,
    0x00002015, 0x00002048, 0x00000A16, 0x000500AA, 0x00000009, 0x00002EEA,
    0x00004ADC, 0x00000A0D, 0x000500AA, 0x00000009, 0x00005776, 0x00004ADC,
    0x00000A10, 0x000500A6, 0x00000009, 0x00005686, 0x00002EEA, 0x00005776,
    0x000300F7, 0x00003463, 0x00000000, 0x000400FA, 0x00005686, 0x00002957,
    0x00003463, 0x000200F8, 0x00002957, 0x000500C7, 0x00000017, 0x0000475F,
    0x000049A7, 0x000009CE, 0x000500C4, 0x00000017, 0x000024D1, 0x0000475F,
    0x0000013D, 0x000500C7, 0x00000017, 0x000050AC, 0x000049A7, 0x0000072E,
    0x000500C2, 0x00000017, 0x0000448D, 0x000050AC, 0x0000013D, 0x000500C5,
    0x00000017, 0x00003FF8, 0x000024D1, 0x0000448D, 0x000200F9, 0x00003463,
    0x000200F8, 0x00003463, 0x000700F5, 0x00000017, 0x00005879, 0x000049A7,
    0x00005341, 0x00003FF8, 0x00002957, 0x000500AA, 0x00000009, 0x00004CB6,
    0x00004ADC, 0x00000A13, 0x000500A6, 0x00000009, 0x00003B23, 0x00005776,
    0x00004CB6, 0x000300F7, 0x00002C98, 0x00000000, 0x000400FA, 0x00003B23,
    0x00002B38, 0x00002C98, 0x000200F8, 0x00002B38, 0x000500C4, 0x00000017,
    0x00005E17, 0x00005879, 0x000002ED, 0x000500C2, 0x00000017, 0x00003BE7,
    0x00005879, 0x000002ED, 0x000500C5, 0x00000017, 0x000029E8, 0x00005E17,
    0x00003BE7, 0x000200F9, 0x00002C98, 0x000200F8, 0x00002C98, 0x000700F5,
    0x00000017, 0x00004D37, 0x00005879, 0x00003463, 0x000029E8, 0x00002B38,
    0x00060041, 0x00000294, 0x000019BE, 0x00001592, 0x00000A0B, 0x00002015,
    0x0003003E, 0x000019BE, 0x00004D37, 0x000500AC, 0x00000009, 0x00005BF6,
    0x0000229A, 0x00000A0D, 0x000300F7, 0x000060BC, 0x00000002, 0x000400FA,
    0x00005BF6, 0x0000281E, 0x00005094, 0x000200F8, 0x0000281E, 0x000500C2,
    0x0000000B, 0x00002CD4, 0x00001A29, 0x00000A10, 0x00050086, 0x0000000B,
    0x00001F01, 0x00002CD4, 0x0000229A, 0x00050084, 0x0000000B, 0x000041FB,
    0x00001F01, 0x0000229A, 0x00050082, 0x0000000B, 0x00003171, 0x00002CD4,
    0x000041FB, 0x00050080, 0x0000000B, 0x00002527, 0x00003171, 0x00000A0D,
    0x000500AA, 0x00000009, 0x0000343F, 0x00002527, 0x0000229A, 0x000300F7,
    0x00001EED, 0x00000000, 0x000400FA, 0x0000343F, 0x0000569E, 0x00002191,
    0x000200F8, 0x0000569E, 0x00050084, 0x0000000B, 0x00004B59, 0x00000A6A,
    0x0000229A, 0x000500C4, 0x0000000B, 0x0000540F, 0x00003171, 0x00000A16,
    0x00050082, 0x0000000B, 0x00004945, 0x00004B59, 0x0000540F, 0x000200F9,
    0x00001EED, 0x000200F8, 0x00002191, 0x000200F9, 0x00001EED, 0x000200F8,
    0x00001EED, 0x000700F5, 0x0000000B, 0x0000292C, 0x00004945, 0x0000569E,
    0x00000A3A, 0x00002191, 0x000200F9, 0x000060BC, 0x000200F8, 0x00005094,
    0x000200F9, 0x000060BC, 0x000200F8, 0x000060BC, 0x000700F5, 0x0000000B,
    0x000029BC, 0x0000292C, 0x00001EED, 0x00000A6A, 0x00005094, 0x00050084,
    0x0000000B, 0x0000492B, 0x000029BC, 0x0000448F, 0x000500C2, 0x0000000B,
    0x00004DEF, 0x0000492B, 0x00000A16, 0x00050080, 0x0000000B, 0x00005B72,
    0x00002015, 0x00004DEF, 0x000300F7, 0x00003A1A, 0x00000000, 0x000400FA,
    0x00005686, 0x00002958, 0x00003A1A, 0x000200F8, 0x00002958, 0x000500C7,
    0x00000017, 0x00004760, 0x000022F8, 0x000009CE, 0x000500C4, 0x00000017,
    0x000024D2, 0x00004760, 0x0000013D, 0x000500C7, 0x00000017, 0x000050AD,
    0x000022F8, 0x0000072E, 0x000500C2, 0x00000017, 0x0000448E, 0x000050AD,
    0x0000013D, 0x000500C5, 0x00000017, 0x00003FF9, 0x000024D2, 0x0000448E,
    0x000200F9, 0x00003A1A, 0x000200F8, 0x00003A1A, 0x000700F5, 0x00000017,
    0x00002AAE, 0x000022F8, 0x000060BC, 0x00003FF9, 0x00002958, 0x000300F7,
    0x00002C99, 0x00000000, 0x000400FA, 0x00003B23, 0x00002B39, 0x00002C99,
    0x000200F8, 0x00002B39, 0x000500C4, 0x00000017, 0x00005E18, 0x00002AAE,
    0x000002ED, 0x000500C2, 0x00000017, 0x00003BE8, 0x00002AAE, 0x000002ED,
    0x000500C5, 0x00000017, 0x000029E9, 0x00005E18, 0x00003BE8, 0x000200F9,
    0x00002C99, 0x000200F8, 0x00002C99, 0x000700F5, 0x00000017, 0x00004D39,
    0x00002AAE, 0x00003A1A, 0x000029E9, 0x00002B39, 0x00060041, 0x00000294,
    0x00001F75, 0x00001592, 0x00000A0B, 0x00005B72, 0x0003003E, 0x00001F75,
    0x00004D39, 0x000200F9, 0x00004C7A, 0x000200F8, 0x00004C7A, 0x000100FD,
    0x00010038,
};
