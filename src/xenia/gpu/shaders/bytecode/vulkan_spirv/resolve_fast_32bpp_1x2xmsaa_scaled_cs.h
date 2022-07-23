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
               OpDecorate %3271 DescriptorSet 0
               OpDecorate %3271 Binding 0
               OpDecorate %_runtimearr_v4uint_0 ArrayStride 16
               OpMemberDecorate %_struct_1973 0 NonReadable
               OpMemberDecorate %_struct_1973 0 Offset 0
               OpDecorate %_struct_1973 BufferBlock
               OpDecorate %5522 DescriptorSet 1
               OpDecorate %5522 Binding 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
     %v4uint = OpTypeVector %uint 4
       %bool = OpTypeBool
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
     %v2bool = OpTypeVector %bool 2
     %uint_0 = OpConstant %uint 0
       %1807 = OpConstantComposite %v2uint %uint_0 %uint_0
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %1816 = OpConstantComposite %v2uint %uint_1 %uint_0
    %uint_80 = OpConstant %uint 80
       %2719 = OpConstantComposite %v2uint %uint_80 %uint_16
  %uint_2048 = OpConstant %uint 2048
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
  %uint_2047 = OpConstant %uint 2047
    %uint_24 = OpConstant %uint 24
    %uint_15 = OpConstant %uint 15
    %uint_28 = OpConstant %uint 28
    %uint_19 = OpConstant %uint 19
       %2179 = OpConstantComposite %v2uint %uint_16 %uint_19
%uint_536870912 = OpConstant %uint 536870912
       %1855 = OpConstantComposite %v2uint %uint_0 %uint_4
       %1856 = OpConstantComposite %v2uint %uint_4 %uint_1
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
       %2275 = OpConstantComposite %v2uint %uint_20 %uint_24
   %uint_255 = OpConstant %uint 255
%uint_3222273024 = OpConstant %uint 3222273024
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%_ptr_Input_uint = OpTypePointer Input %uint
       %1834 = OpConstantComposite %v2uint %uint_3 %uint_0
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%_struct_1972 = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform__struct_1972 = OpTypePointer Uniform %_struct_1972
       %3271 = OpVariable %_ptr_Uniform__struct_1972 Uniform
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
%_runtimearr_v4uint_0 = OpTypeRuntimeArray %v4uint
%_struct_1973 = OpTypeStruct %_runtimearr_v4uint_0
%_ptr_Uniform__struct_1973 = OpTypePointer Uniform %_struct_1973
       %5522 = OpVariable %_ptr_Uniform__struct_1973 Uniform
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %1954 = OpConstantComposite %v2uint %uint_7 %uint_7
       %1870 = OpConstantComposite %v2uint %uint_3 %uint_3
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
      %24434 = OpBitwiseAnd %uint %10307 %uint_2047
      %18836 = OpShiftRightLogical %uint %15627 %uint_24
       %9130 = OpBitwiseAnd %uint %18836 %uint_15
       %8871 = OpCompositeConstruct %v2uint %20824 %20824
       %9633 = OpShiftRightLogical %v2uint %8871 %2179
      %23601 = OpBitwiseAnd %v2uint %9633 %1954
      %24030 = OpBitwiseAnd %uint %15627 %uint_536870912
      %12295 = OpINotEqual %bool %24030 %uint_0
               OpSelectionMerge %14676 None
               OpBranchConditional %12295 %16739 %21992
      %21992 = OpLabel
               OpBranch %14676
      %16739 = OpLabel
      %15278 = OpShiftRightLogical %v2uint %23601 %1828
               OpBranch %14676
      %14676 = OpLabel
      %19067 = OpPhi %v2uint %15278 %16739 %1807 %21992
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
               OpBranchConditional %7405 %21993 %17447
      %21993 = OpLabel
               OpBranch %19578
      %17447 = OpLabel
      %14637 = OpLoad %v3uint %gl_GlobalInvocationID
      %18505 = OpVectorShuffle %v2uint %14637 %14637 0 1
       %9840 = OpShiftLeftLogical %v2uint %18505 %1834
       %6697 = OpCompositeExtract %uint %9840 0
      %18425 = OpCompositeExtract %uint %9840 1
      %14186 = OpCompositeExtract %uint %19067 1
      %24446 = OpExtInst %uint %1 UMax %18425 %14186
      %20975 = OpCompositeConstruct %v2uint %6697 %24446
      %21036 = OpIAdd %v2uint %20975 %23019
      %16075 = OpULessThanEqual %bool %16204 %uint_3
               OpSelectionMerge %23776 None
               OpBranchConditional %16075 %10990 %15087
      %15087 = OpLabel
      %13566 = OpIEqual %bool %16204 %uint_5
       %8438 = OpSelect %uint %13566 %uint_2 %uint_0
               OpBranch %23776
      %10990 = OpLabel
               OpBranch %23776
      %23776 = OpLabel
      %19300 = OpPhi %uint %16204 %10990 %8438 %15087
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
               OpSelectionMerge %22850 None
               OpBranchConditional %15736 %23061 %24565
      %24565 = OpLabel
      %20693 = OpBitcast %int %13307
               OpBranch %22850
      %23061 = OpLabel
      %18885 = OpBitcast %int %13307
      %17199 = OpSNegate %int %18885
               OpBranch %22850
      %22850 = OpLabel
      %10046 = OpPhi %int %17199 %23061 %20693 %24565
      %11983 = OpIAdd %int %15197 %10046
      %17709 = OpBitcast %uint %11983
      %21574 = OpCompositeInsert %v2uint %17709 %14279 0
               OpBranch %18756
      %18756 = OpLabel
      %17360 = OpPhi %v2uint %14279 %23776 %21574 %22850
      %24023 = OpCompositeExtract %uint %21145 0
      %22303 = OpCompositeExtract %uint %21145 1
      %13170 = OpIMul %uint %24023 %22303
      %14551 = OpIMul %uint %8742 %13170
       %6805 = OpCompositeExtract %uint %17360 1
      %23526 = OpCompositeExtract %uint %14725 0
      %22886 = OpIMul %uint %6805 %23526
       %6886 = OpCompositeExtract %uint %17360 0
       %9696 = OpIAdd %uint %22886 %6886
      %18021 = OpShiftLeftLogical %uint %9696 %uint_0
      %18363 = OpIAdd %uint %14551 %18021
      %13884 = OpIMul %uint %13170 %uint_2048
      %19795 = OpUMod %uint %18363 %13884
      %21806 = OpShiftRightLogical %uint %19795 %uint_2
      %17174 = OpAccessChain %_ptr_Uniform_v4uint %3271 %int_0 %21806
      %12609 = OpLoad %v4uint %17174
      %11687 = OpIAdd %uint %21806 %uint_1
       %7197 = OpAccessChain %_ptr_Uniform_v4uint %3271 %int_0 %11687
      %19842 = OpLoad %v4uint %7197
      %21106 = OpIEqual %bool %6697 %uint_0
               OpSelectionMerge %13276 None
               OpBranchConditional %21106 %11451 %13276
      %11451 = OpLabel
      %24156 = OpCompositeExtract %uint %19067 0
      %22470 = OpINotEqual %bool %24156 %uint_0
               OpBranch %13276
      %13276 = OpLabel
      %10924 = OpPhi %bool %21106 %18756 %22470 %11451
               OpSelectionMerge %21910 DontFlatten
               OpBranchConditional %10924 %11508 %21910
      %11508 = OpLabel
      %23599 = OpCompositeExtract %uint %19067 0
      %17346 = OpUGreaterThanEqual %bool %23599 %uint_2
               OpSelectionMerge %18758 None
               OpBranchConditional %17346 %15877 %18758
      %15877 = OpLabel
      %24532 = OpUGreaterThanEqual %bool %23599 %uint_3
               OpSelectionMerge %18757 None
               OpBranchConditional %24532 %9760 %18757
       %9760 = OpLabel
      %20482 = OpCompositeExtract %uint %12609 3
      %14335 = OpCompositeInsert %v4uint %20482 %12609 2
               OpBranch %18757
      %18757 = OpLabel
      %17379 = OpPhi %v4uint %12609 %15877 %14335 %9760
       %7002 = OpCompositeExtract %uint %17379 2
      %15144 = OpCompositeInsert %v4uint %7002 %17379 1
               OpBranch %18758
      %18758 = OpLabel
      %17380 = OpPhi %v4uint %12609 %11508 %15144 %18757
       %7003 = OpCompositeExtract %uint %17380 1
      %15145 = OpCompositeInsert %v4uint %7003 %17380 0
               OpBranch %21910
      %21910 = OpLabel
      %10925 = OpPhi %v4uint %12609 %13276 %15145 %18758
               OpSelectionMerge %21263 DontFlatten
               OpBranchConditional %19573 %22395 %21263
      %22395 = OpLabel
               OpSelectionMerge %14836 None
               OpSwitch %9130 %14836 0 %21920 1 %21920 2 %10391 3 %10391 10 %10391 12 %10391
      %10391 = OpLabel
      %15273 = OpBitwiseAnd %v4uint %10925 %2352
      %23564 = OpBitwiseAnd %v4uint %10925 %929
      %24837 = OpShiftLeftLogical %v4uint %23564 %965
      %18005 = OpBitwiseOr %v4uint %15273 %24837
      %23170 = OpShiftRightLogical %v4uint %10925 %965
       %6442 = OpBitwiseAnd %v4uint %23170 %929
      %15589 = OpBitwiseOr %v4uint %18005 %6442
      %19519 = OpBitwiseAnd %v4uint %19842 %2352
      %17946 = OpBitwiseAnd %v4uint %19842 %929
      %24838 = OpShiftLeftLogical %v4uint %17946 %965
      %18006 = OpBitwiseOr %v4uint %19519 %24838
      %23171 = OpShiftRightLogical %v4uint %19842 %965
       %7392 = OpBitwiseAnd %v4uint %23171 %929
       %7870 = OpBitwiseOr %v4uint %18006 %7392
               OpBranch %14836
      %21920 = OpLabel
      %20117 = OpBitwiseAnd %v4uint %10925 %1838
      %23565 = OpBitwiseAnd %v4uint %10925 %1611
      %24839 = OpShiftLeftLogical %v4uint %23565 %749
      %18007 = OpBitwiseOr %v4uint %20117 %24839
      %23172 = OpShiftRightLogical %v4uint %10925 %749
       %6443 = OpBitwiseAnd %v4uint %23172 %1611
      %15590 = OpBitwiseOr %v4uint %18007 %6443
      %19520 = OpBitwiseAnd %v4uint %19842 %1838
      %17947 = OpBitwiseAnd %v4uint %19842 %1611
      %24840 = OpShiftLeftLogical %v4uint %17947 %749
      %18008 = OpBitwiseOr %v4uint %19520 %24840
      %23173 = OpShiftRightLogical %v4uint %19842 %749
       %7393 = OpBitwiseAnd %v4uint %23173 %1611
       %7871 = OpBitwiseOr %v4uint %18008 %7393
               OpBranch %14836
      %14836 = OpLabel
      %11251 = OpPhi %v4uint %19842 %22395 %7871 %21920 %7870 %10391
      %13709 = OpPhi %v4uint %10925 %22395 %15590 %21920 %15589 %10391
               OpBranch %21263
      %21263 = OpLabel
       %8952 = OpPhi %v4uint %19842 %21910 %11251 %14836
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
               OpBranchConditional %20496 %22206 %10904
      %10904 = OpLabel
       %7339 = OpVectorShuffle %v2uint %21452 %21452 0 1
      %22991 = OpBitcast %v2int %7339
       %6403 = OpCompositeExtract %int %22991 0
       %9469 = OpShiftRightArithmetic %int %6403 %int_5
      %10055 = OpCompositeExtract %int %22991 1
      %16476 = OpShiftRightArithmetic %int %10055 %int_5
      %23373 = OpShiftRightLogical %uint %15783 %uint_5
       %6314 = OpBitcast %int %23373
      %21319 = OpIMul %int %16476 %6314
      %16222 = OpIAdd %int %9469 %21319
      %19086 = OpShiftLeftLogical %int %16222 %uint_9
      %10934 = OpBitwiseAnd %int %6403 %int_7
      %12600 = OpBitwiseAnd %int %10055 %int_14
      %17741 = OpShiftLeftLogical %int %12600 %int_2
      %17303 = OpIAdd %int %10934 %17741
       %6375 = OpShiftLeftLogical %int %17303 %uint_2
      %10161 = OpBitwiseAnd %int %6375 %int_n16
      %12150 = OpShiftLeftLogical %int %10161 %int_1
      %15435 = OpIAdd %int %19086 %12150
      %13207 = OpBitwiseAnd %int %6375 %int_15
      %19760 = OpIAdd %int %15435 %13207
      %18356 = OpBitwiseAnd %int %10055 %int_1
      %21578 = OpShiftLeftLogical %int %18356 %int_4
      %16727 = OpIAdd %int %19760 %21578
      %20514 = OpBitwiseAnd %int %16727 %int_n512
       %9238 = OpShiftLeftLogical %int %20514 %int_3
      %18995 = OpBitwiseAnd %int %10055 %int_16
      %12151 = OpShiftLeftLogical %int %18995 %int_7
      %16728 = OpIAdd %int %9238 %12151
      %19165 = OpBitwiseAnd %int %16727 %int_448
      %21579 = OpShiftLeftLogical %int %19165 %int_2
      %16708 = OpIAdd %int %16728 %21579
      %20611 = OpBitwiseAnd %int %10055 %int_8
      %16831 = OpShiftRightArithmetic %int %20611 %int_2
       %7916 = OpShiftRightArithmetic %int %6403 %int_3
      %13750 = OpIAdd %int %16831 %7916
      %21587 = OpBitwiseAnd %int %13750 %int_3
      %21580 = OpShiftLeftLogical %int %21587 %int_6
      %15436 = OpIAdd %int %16708 %21580
      %11782 = OpBitwiseAnd %int %16727 %int_63
      %14671 = OpIAdd %int %15436 %11782
      %22127 = OpBitcast %uint %14671
               OpBranch %21313
      %22206 = OpLabel
       %6573 = OpBitcast %v3int %21452
      %17090 = OpCompositeExtract %int %6573 1
       %9470 = OpShiftRightArithmetic %int %17090 %int_4
      %10056 = OpCompositeExtract %int %6573 2
      %16477 = OpShiftRightArithmetic %int %10056 %int_2
      %23374 = OpShiftRightLogical %uint %25203 %uint_4
       %6315 = OpBitcast %int %23374
      %21281 = OpIMul %int %16477 %6315
      %15143 = OpIAdd %int %9470 %21281
       %9032 = OpShiftRightLogical %uint %15783 %uint_5
      %12427 = OpBitcast %int %9032
      %10360 = OpIMul %int %15143 %12427
      %25154 = OpCompositeExtract %int %6573 0
      %20423 = OpShiftRightArithmetic %int %25154 %int_5
      %18940 = OpIAdd %int %20423 %10360
       %8797 = OpShiftLeftLogical %int %18940 %uint_8
      %11510 = OpBitwiseAnd %int %8797 %int_268435455
      %18938 = OpShiftLeftLogical %int %11510 %int_1
      %19768 = OpBitwiseAnd %int %25154 %int_7
      %12601 = OpBitwiseAnd %int %17090 %int_6
      %17742 = OpShiftLeftLogical %int %12601 %int_2
      %17227 = OpIAdd %int %19768 %17742
       %7048 = OpShiftLeftLogical %int %17227 %uint_8
      %24035 = OpShiftRightArithmetic %int %7048 %int_6
       %8725 = OpShiftRightArithmetic %int %17090 %int_3
      %13731 = OpIAdd %int %8725 %16477
      %23052 = OpBitwiseAnd %int %13731 %int_1
      %16658 = OpShiftRightArithmetic %int %25154 %int_3
      %18794 = OpShiftLeftLogical %int %23052 %int_1
      %13501 = OpIAdd %int %16658 %18794
      %19166 = OpBitwiseAnd %int %13501 %int_3
      %21581 = OpShiftLeftLogical %int %19166 %int_1
      %15437 = OpIAdd %int %23052 %21581
      %13150 = OpBitwiseAnd %int %24035 %int_n16
      %20336 = OpIAdd %int %18938 %13150
      %23345 = OpShiftLeftLogical %int %20336 %int_1
      %23274 = OpBitwiseAnd %int %24035 %int_15
      %10332 = OpIAdd %int %23345 %23274
      %18357 = OpBitwiseAnd %int %10056 %int_3
      %21582 = OpShiftLeftLogical %int %18357 %uint_8
      %16729 = OpIAdd %int %10332 %21582
      %19167 = OpBitwiseAnd %int %17090 %int_1
      %21583 = OpShiftLeftLogical %int %19167 %int_4
      %16730 = OpIAdd %int %16729 %21583
      %20438 = OpBitwiseAnd %int %15437 %int_1
       %9987 = OpShiftLeftLogical %int %20438 %int_3
      %13106 = OpShiftRightArithmetic %int %16730 %int_6
      %14038 = OpBitwiseAnd %int %13106 %int_7
      %13330 = OpIAdd %int %9987 %14038
      %23346 = OpShiftLeftLogical %int %13330 %int_3
      %23217 = OpBitwiseAnd %int %15437 %int_n2
      %10908 = OpIAdd %int %23346 %23217
      %23347 = OpShiftLeftLogical %int %10908 %int_2
      %23218 = OpBitwiseAnd %int %16730 %int_n512
      %10909 = OpIAdd %int %23347 %23218
      %23348 = OpShiftLeftLogical %int %10909 %int_3
      %21849 = OpBitwiseAnd %int %16730 %int_63
      %24314 = OpIAdd %int %23348 %21849
      %22128 = OpBitcast %uint %24314
               OpBranch %21313
      %21313 = OpLabel
       %9468 = OpPhi %uint %22128 %22206 %22127 %10904
      %16296 = OpIMul %v2uint %24920 %23601
      %16261 = OpISub %v2uint %7832 %16296
      %17551 = OpCompositeExtract %uint %23601 1
      %23632 = OpIMul %uint %8858 %17551
      %15520 = OpIMul %uint %9468 %23632
      %16084 = OpCompositeExtract %uint %16261 0
      %15890 = OpIMul %uint %16084 %17551
       %6887 = OpCompositeExtract %uint %16261 1
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
               OpSelectionMerge %19116 DontFlatten
               OpBranchConditional %23542 %14554 %21994
      %21994 = OpLabel
               OpBranch %19116
      %14554 = OpLabel
      %13898 = OpShiftRightLogical %uint %6697 %uint_2
       %7937 = OpUDiv %uint %13898 %8858
      %16891 = OpIMul %uint %7937 %8858
      %12657 = OpISub %uint %13898 %16891
       %9511 = OpIAdd %uint %12657 %uint_1
      %13375 = OpIEqual %bool %9511 %8858
               OpSelectionMerge %9304 None
               OpBranchConditional %13375 %7387 %21995
      %21995 = OpLabel
               OpBranch %9304
       %7387 = OpLabel
      %15254 = OpIMul %uint %uint_32 %8858
      %21519 = OpShiftLeftLogical %uint %12657 %uint_4
      %18759 = OpISub %uint %15254 %21519
               OpBranch %9304
       %9304 = OpLabel
      %10540 = OpPhi %uint %18759 %7387 %uint_16 %21995
               OpBranch %19116
      %19116 = OpLabel
      %10684 = OpPhi %uint %10540 %9304 %uint_32 %21994
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
      %10926 = OpPhi %v4uint %8952 %19116 %16377 %10584
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
    0x00000CC7, 0x00000022, 0x00000000, 0x00040047, 0x00000CC7, 0x00000021,
    0x00000000, 0x00040047, 0x000007DD, 0x00000006, 0x00000010, 0x00040048,
    0x000007B5, 0x00000000, 0x00000019, 0x00050048, 0x000007B5, 0x00000000,
    0x00000023, 0x00000000, 0x00030047, 0x000007B5, 0x00000003, 0x00040047,
    0x00001592, 0x00000022, 0x00000001, 0x00040047, 0x00001592, 0x00000021,
    0x00000000, 0x00040047, 0x00000AC7, 0x0000000B, 0x00000019, 0x00020013,
    0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00040015, 0x0000000B,
    0x00000020, 0x00000000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002,
    0x00040017, 0x00000017, 0x0000000B, 0x00000004, 0x00020014, 0x00000009,
    0x00040015, 0x0000000C, 0x00000020, 0x00000001, 0x00040017, 0x00000012,
    0x0000000C, 0x00000002, 0x00040017, 0x00000016, 0x0000000C, 0x00000003,
    0x00040017, 0x00000014, 0x0000000B, 0x00000003, 0x0004002B, 0x0000000B,
    0x00000A0D, 0x00000001, 0x0004002B, 0x0000000B, 0x00000A10, 0x00000002,
    0x0004002B, 0x0000000B, 0x000008A6, 0x00FF00FF, 0x0004002B, 0x0000000B,
    0x00000A22, 0x00000008, 0x0004002B, 0x0000000B, 0x000005FD, 0xFF00FF00,
    0x0004002B, 0x0000000B, 0x00000A13, 0x00000003, 0x0004002B, 0x0000000B,
    0x00000A3A, 0x00000010, 0x0005002C, 0x00000011, 0x0000072D, 0x00000A10,
    0x00000A0D, 0x00040017, 0x0000000F, 0x00000009, 0x00000002, 0x0004002B,
    0x0000000B, 0x00000A0A, 0x00000000, 0x0005002C, 0x00000011, 0x0000070F,
    0x00000A0A, 0x00000A0A, 0x0005002C, 0x00000011, 0x00000724, 0x00000A0D,
    0x00000A0D, 0x0005002C, 0x00000011, 0x00000718, 0x00000A0D, 0x00000A0A,
    0x0004002B, 0x0000000B, 0x00000AFA, 0x00000050, 0x0005002C, 0x00000011,
    0x00000A9F, 0x00000AFA, 0x00000A3A, 0x0004002B, 0x0000000B, 0x00000A84,
    0x00000800, 0x0004002B, 0x0000000C, 0x00000A1A, 0x00000005, 0x0004002B,
    0x0000000B, 0x00000A19, 0x00000005, 0x0004002B, 0x0000000B, 0x00000A1F,
    0x00000007, 0x0004002B, 0x0000000C, 0x00000A20, 0x00000007, 0x0004002B,
    0x0000000C, 0x00000A35, 0x0000000E, 0x0004002B, 0x0000000C, 0x00000A11,
    0x00000002, 0x0004002B, 0x0000000C, 0x000009DB, 0xFFFFFFF0, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A38,
    0x0000000F, 0x0004002B, 0x0000000C, 0x00000A17, 0x00000004, 0x0004002B,
    0x0000000C, 0x0000040B, 0xFFFFFE00, 0x0004002B, 0x0000000C, 0x00000A14,
    0x00000003, 0x0004002B, 0x0000000C, 0x00000A3B, 0x00000010, 0x0004002B,
    0x0000000C, 0x00000388, 0x000001C0, 0x0004002B, 0x0000000C, 0x00000A23,
    0x00000008, 0x0004002B, 0x0000000C, 0x00000A1D, 0x00000006, 0x0004002B,
    0x0000000C, 0x00000AC8, 0x0000003F, 0x0004002B, 0x0000000B, 0x00000A16,
    0x00000004, 0x0004002B, 0x0000000C, 0x0000078B, 0x0FFFFFFF, 0x0004002B,
    0x0000000C, 0x00000A05, 0xFFFFFFFE, 0x0004002B, 0x0000000B, 0x00000A6A,
    0x00000020, 0x0006001E, 0x000003F9, 0x0000000B, 0x0000000B, 0x0000000B,
    0x0000000B, 0x00040020, 0x00000676, 0x00000009, 0x000003F9, 0x0004003B,
    0x00000676, 0x0000118F, 0x00000009, 0x0004002B, 0x0000000C, 0x00000A0B,
    0x00000000, 0x00040020, 0x00000288, 0x00000009, 0x0000000B, 0x0004002B,
    0x0000000B, 0x00000A44, 0x000003FF, 0x0004002B, 0x0000000B, 0x00000A28,
    0x0000000A, 0x0004002B, 0x0000000B, 0x00000AFE, 0x00001000, 0x0004002B,
    0x0000000B, 0x00000A31, 0x0000000D, 0x0004002B, 0x0000000B, 0x00000A81,
    0x000007FF, 0x0004002B, 0x0000000B, 0x00000A52, 0x00000018, 0x0004002B,
    0x0000000B, 0x00000A37, 0x0000000F, 0x0004002B, 0x0000000B, 0x00000A5E,
    0x0000001C, 0x0004002B, 0x0000000B, 0x00000A43, 0x00000013, 0x0005002C,
    0x00000011, 0x00000883, 0x00000A3A, 0x00000A43, 0x0004002B, 0x0000000B,
    0x00000510, 0x20000000, 0x0005002C, 0x00000011, 0x0000073F, 0x00000A0A,
    0x00000A16, 0x0005002C, 0x00000011, 0x00000740, 0x00000A16, 0x00000A0D,
    0x0004002B, 0x0000000B, 0x00000926, 0x01000000, 0x0004002B, 0x0000000B,
    0x00000A46, 0x00000014, 0x0005002C, 0x00000011, 0x000008E3, 0x00000A46,
    0x00000A52, 0x0004002B, 0x0000000B, 0x00000144, 0x000000FF, 0x0004002B,
    0x0000000B, 0x00000B54, 0xC00FFC00, 0x00040020, 0x00000291, 0x00000001,
    0x00000014, 0x0004003B, 0x00000291, 0x00000F48, 0x00000001, 0x00040020,
    0x00000289, 0x00000001, 0x0000000B, 0x0005002C, 0x00000011, 0x0000072A,
    0x00000A13, 0x00000A0A, 0x0003001D, 0x000007DC, 0x00000017, 0x0003001E,
    0x000007B4, 0x000007DC, 0x00040020, 0x00000A32, 0x00000002, 0x000007B4,
    0x0004003B, 0x00000A32, 0x00000CC7, 0x00000002, 0x00040020, 0x00000294,
    0x00000002, 0x00000017, 0x0003001D, 0x000007DD, 0x00000017, 0x0003001E,
    0x000007B5, 0x000007DD, 0x00040020, 0x00000A33, 0x00000002, 0x000007B5,
    0x0004003B, 0x00000A33, 0x00001592, 0x00000002, 0x0006002C, 0x00000014,
    0x00000AC7, 0x00000A22, 0x00000A22, 0x00000A0D, 0x0005002C, 0x00000011,
    0x000007A2, 0x00000A1F, 0x00000A1F, 0x0005002C, 0x00000011, 0x0000074E,
    0x00000A13, 0x00000A13, 0x0005002C, 0x00000011, 0x0000084A, 0x00000A37,
    0x00000A37, 0x0007002C, 0x00000017, 0x0000072E, 0x000005FD, 0x000005FD,
    0x000005FD, 0x000005FD, 0x0007002C, 0x00000017, 0x0000064B, 0x00000144,
    0x00000144, 0x00000144, 0x00000144, 0x0007002C, 0x00000017, 0x000002ED,
    0x00000A3A, 0x00000A3A, 0x00000A3A, 0x00000A3A, 0x0007002C, 0x00000017,
    0x00000930, 0x00000B54, 0x00000B54, 0x00000B54, 0x00000B54, 0x0007002C,
    0x00000017, 0x000003A1, 0x00000A44, 0x00000A44, 0x00000A44, 0x00000A44,
    0x0007002C, 0x00000017, 0x000003C5, 0x00000A46, 0x00000A46, 0x00000A46,
    0x00000A46, 0x0004002B, 0x0000000B, 0x00000A25, 0x00000009, 0x0007002C,
    0x00000017, 0x000009CE, 0x000008A6, 0x000008A6, 0x000008A6, 0x000008A6,
    0x0007002C, 0x00000017, 0x0000013D, 0x00000A22, 0x00000A22, 0x00000A22,
    0x00000A22, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x00003B06, 0x000300F7, 0x00004C7A, 0x00000000, 0x000300FB,
    0x00000A0A, 0x00002E68, 0x000200F8, 0x00002E68, 0x00050041, 0x00000288,
    0x000056E5, 0x0000118F, 0x00000A0B, 0x0004003D, 0x0000000B, 0x00003D0B,
    0x000056E5, 0x00050041, 0x00000288, 0x000058AC, 0x0000118F, 0x00000A0E,
    0x0004003D, 0x0000000B, 0x00005158, 0x000058AC, 0x000500C7, 0x0000000B,
    0x00005051, 0x00003D0B, 0x00000A44, 0x000500C2, 0x0000000B, 0x00004E69,
    0x00003D0B, 0x00000A28, 0x000500C7, 0x0000000B, 0x00001C09, 0x00004E69,
    0x00000A13, 0x000500C7, 0x0000000B, 0x000059EF, 0x00003D0B, 0x00000AFE,
    0x000500AB, 0x00000009, 0x0000500F, 0x000059EF, 0x00000A0A, 0x000500C2,
    0x0000000B, 0x00002843, 0x00003D0B, 0x00000A31, 0x000500C7, 0x0000000B,
    0x00005F72, 0x00002843, 0x00000A81, 0x000500C2, 0x0000000B, 0x00004994,
    0x00003D0B, 0x00000A52, 0x000500C7, 0x0000000B, 0x000023AA, 0x00004994,
    0x00000A37, 0x00050050, 0x00000011, 0x000022A7, 0x00005158, 0x00005158,
    0x000500C2, 0x00000011, 0x000025A1, 0x000022A7, 0x00000883, 0x000500C7,
    0x00000011, 0x00005C31, 0x000025A1, 0x000007A2, 0x000500C7, 0x0000000B,
    0x00005DDE, 0x00003D0B, 0x00000510, 0x000500AB, 0x00000009, 0x00003007,
    0x00005DDE, 0x00000A0A, 0x000300F7, 0x00003954, 0x00000000, 0x000400FA,
    0x00003007, 0x00004163, 0x000055E8, 0x000200F8, 0x000055E8, 0x000200F9,
    0x00003954, 0x000200F8, 0x00004163, 0x000500C2, 0x00000011, 0x00003BAE,
    0x00005C31, 0x00000724, 0x000200F9, 0x00003954, 0x000200F8, 0x00003954,
    0x000700F5, 0x00000011, 0x00004A7B, 0x00003BAE, 0x00004163, 0x0000070F,
    0x000055E8, 0x000500C2, 0x00000011, 0x0000189F, 0x000022A7, 0x0000073F,
    0x000500C4, 0x00000011, 0x00002A91, 0x00000724, 0x00000740, 0x00050082,
    0x00000011, 0x000048B0, 0x00002A91, 0x00000724, 0x000500C7, 0x00000011,
    0x00004937, 0x0000189F, 0x000048B0, 0x000500C4, 0x00000011, 0x00005784,
    0x00004937, 0x0000074E, 0x00050084, 0x00000011, 0x000059EB, 0x00005784,
    0x00005C31, 0x000500C2, 0x0000000B, 0x00003343, 0x00005158, 0x00000A19,
    0x000500C7, 0x0000000B, 0x000039C1, 0x00003343, 0x00000A81, 0x00050051,
    0x0000000B, 0x0000229A, 0x00005C31, 0x00000000, 0x00050084, 0x0000000B,
    0x000059D1, 0x000039C1, 0x0000229A, 0x00050041, 0x00000288, 0x00004E44,
    0x0000118F, 0x00000A11, 0x0004003D, 0x0000000B, 0x000048C4, 0x00004E44,
    0x00050041, 0x00000288, 0x000058AD, 0x0000118F, 0x00000A14, 0x0004003D,
    0x0000000B, 0x000051B7, 0x000058AD, 0x000500C7, 0x0000000B, 0x00004ADC,
    0x000048C4, 0x00000A1F, 0x000500C7, 0x0000000B, 0x000055EF, 0x000048C4,
    0x00000A22, 0x000500AB, 0x00000009, 0x00005010, 0x000055EF, 0x00000A0A,
    0x000500C2, 0x0000000B, 0x000028A2, 0x000048C4, 0x00000A16, 0x000500C7,
    0x0000000B, 0x000059FD, 0x000028A2, 0x00000A1F, 0x000500C7, 0x0000000B,
    0x00005A4E, 0x000048C4, 0x00000926, 0x000500AB, 0x00000009, 0x00004C75,
    0x00005A4E, 0x00000A0A, 0x000500C7, 0x0000000B, 0x00001F43, 0x000051B7,
    0x00000A44, 0x000500C4, 0x0000000B, 0x00003DA7, 0x00001F43, 0x00000A19,
    0x000500C2, 0x0000000B, 0x0000583F, 0x000051B7, 0x00000A28, 0x000500C7,
    0x0000000B, 0x00004BBE, 0x0000583F, 0x00000A44, 0x000500C4, 0x0000000B,
    0x00006273, 0x00004BBE, 0x00000A19, 0x00050050, 0x00000011, 0x000028B6,
    0x000051B7, 0x000051B7, 0x000500C2, 0x00000011, 0x00002891, 0x000028B6,
    0x000008E3, 0x000500C7, 0x00000011, 0x00005B53, 0x00002891, 0x0000084A,
    0x000500C4, 0x00000011, 0x00003F4F, 0x00005B53, 0x0000074E, 0x00050084,
    0x00000011, 0x000059EC, 0x00003F4F, 0x00005C31, 0x000500C2, 0x0000000B,
    0x00003213, 0x000051B7, 0x00000A5E, 0x000500C7, 0x0000000B, 0x00003F4C,
    0x00003213, 0x00000A1F, 0x00050041, 0x00000289, 0x00005143, 0x00000F48,
    0x00000A0A, 0x0004003D, 0x0000000B, 0x000022D1, 0x00005143, 0x000500AE,
    0x00000009, 0x00001CED, 0x000022D1, 0x000059D1, 0x000300F7, 0x00004427,
    0x00000002, 0x000400FA, 0x00001CED, 0x000055E9, 0x00004427, 0x000200F8,
    0x000055E9, 0x000200F9, 0x00004C7A, 0x000200F8, 0x00004427, 0x0004003D,
    0x00000014, 0x0000392D, 0x00000F48, 0x0007004F, 0x00000011, 0x00004849,
    0x0000392D, 0x0000392D, 0x00000000, 0x00000001, 0x000500C4, 0x00000011,
    0x00002670, 0x00004849, 0x0000072A, 0x00050051, 0x0000000B, 0x00001A29,
    0x00002670, 0x00000000, 0x00050051, 0x0000000B, 0x000047F9, 0x00002670,
    0x00000001, 0x00050051, 0x0000000B, 0x0000376A, 0x00004A7B, 0x00000001,
    0x0007000C, 0x0000000B, 0x00005F7E, 0x00000001, 0x00000029, 0x000047F9,
    0x0000376A, 0x00050050, 0x00000011, 0x000051EF, 0x00001A29, 0x00005F7E,
    0x00050080, 0x00000011, 0x0000522C, 0x000051EF, 0x000059EB, 0x000500B2,
    0x00000009, 0x00003ECB, 0x00003F4C, 0x00000A13, 0x000300F7, 0x00005CE0,
    0x00000000, 0x000400FA, 0x00003ECB, 0x00002AEE, 0x00003AEF, 0x000200F8,
    0x00003AEF, 0x000500AA, 0x00000009, 0x000034FE, 0x00003F4C, 0x00000A19,
    0x000600A9, 0x0000000B, 0x000020F6, 0x000034FE, 0x00000A10, 0x00000A0A,
    0x000200F9, 0x00005CE0, 0x000200F8, 0x00002AEE, 0x000200F9, 0x00005CE0,
    0x000200F8, 0x00005CE0, 0x000700F5, 0x0000000B, 0x00004B64, 0x00003F4C,
    0x00002AEE, 0x000020F6, 0x00003AEF, 0x00050050, 0x00000011, 0x000041BE,
    0x00001C09, 0x00001C09, 0x000500AE, 0x0000000F, 0x00002E19, 0x000041BE,
    0x0000072D, 0x000600A9, 0x00000011, 0x00004BB5, 0x00002E19, 0x00000724,
    0x0000070F, 0x000500C4, 0x00000011, 0x00002AEA, 0x0000522C, 0x00004BB5,
    0x00050050, 0x00000011, 0x0000605D, 0x00004B64, 0x00004B64, 0x000500C2,
    0x00000011, 0x00002385, 0x0000605D, 0x00000718, 0x000500C7, 0x00000011,
    0x00003EC8, 0x00002385, 0x00000724, 0x00050080, 0x00000011, 0x00004F30,
    0x00002AEA, 0x00003EC8, 0x00050084, 0x00000011, 0x00005299, 0x00000A9F,
    0x00005C31, 0x000500C2, 0x00000011, 0x00003985, 0x00005299, 0x0000070F,
    0x00050086, 0x00000011, 0x00004D57, 0x00004F30, 0x00003985, 0x00050051,
    0x0000000B, 0x00004FA6, 0x00004D57, 0x00000001, 0x00050084, 0x0000000B,
    0x00002B26, 0x00004FA6, 0x00005051, 0x00050051, 0x0000000B, 0x00006059,
    0x00004D57, 0x00000000, 0x00050080, 0x0000000B, 0x00005420, 0x00002B26,
    0x00006059, 0x00050080, 0x0000000B, 0x00002226, 0x00005F72, 0x00005420,
    0x00050084, 0x00000011, 0x0000193B, 0x00004D57, 0x00003985, 0x00050082,
    0x00000011, 0x000037C7, 0x00004F30, 0x0000193B, 0x000300F7, 0x00004944,
    0x00000000, 0x000400FA, 0x0000500F, 0x00002E70, 0x00004944, 0x000200F8,
    0x00002E70, 0x00050051, 0x0000000B, 0x00004259, 0x00003985, 0x00000000,
    0x000500C2, 0x0000000B, 0x000033FB, 0x00004259, 0x00000A0D, 0x00050051,
    0x0000000B, 0x000056BF, 0x000037C7, 0x00000000, 0x0004007C, 0x0000000C,
    0x00003B5D, 0x000056BF, 0x000500AE, 0x00000009, 0x00003D78, 0x000056BF,
    0x000033FB, 0x000300F7, 0x00005942, 0x00000000, 0x000400FA, 0x00003D78,
    0x00005A15, 0x00005FF5, 0x000200F8, 0x00005FF5, 0x0004007C, 0x0000000C,
    0x000050D5, 0x000033FB, 0x000200F9, 0x00005942, 0x000200F8, 0x00005A15,
    0x0004007C, 0x0000000C, 0x000049C5, 0x000033FB, 0x0004007E, 0x0000000C,
    0x0000432F, 0x000049C5, 0x000200F9, 0x00005942, 0x000200F8, 0x00005942,
    0x000700F5, 0x0000000C, 0x0000273E, 0x0000432F, 0x00005A15, 0x000050D5,
    0x00005FF5, 0x00050080, 0x0000000C, 0x00002ECF, 0x00003B5D, 0x0000273E,
    0x0004007C, 0x0000000B, 0x0000452D, 0x00002ECF, 0x00060052, 0x00000011,
    0x00005446, 0x0000452D, 0x000037C7, 0x00000000, 0x000200F9, 0x00004944,
    0x000200F8, 0x00004944, 0x000700F5, 0x00000011, 0x000043D0, 0x000037C7,
    0x00005CE0, 0x00005446, 0x00005942, 0x00050051, 0x0000000B, 0x00005DD7,
    0x00005299, 0x00000000, 0x00050051, 0x0000000B, 0x0000571F, 0x00005299,
    0x00000001, 0x00050084, 0x0000000B, 0x00003372, 0x00005DD7, 0x0000571F,
    0x00050084, 0x0000000B, 0x000038D7, 0x00002226, 0x00003372, 0x00050051,
    0x0000000B, 0x00001A95, 0x000043D0, 0x00000001, 0x00050051, 0x0000000B,
    0x00005BE6, 0x00003985, 0x00000000, 0x00050084, 0x0000000B, 0x00005966,
    0x00001A95, 0x00005BE6, 0x00050051, 0x0000000B, 0x00001AE6, 0x000043D0,
    0x00000000, 0x00050080, 0x0000000B, 0x000025E0, 0x00005966, 0x00001AE6,
    0x000500C4, 0x0000000B, 0x00004665, 0x000025E0, 0x00000A0A, 0x00050080,
    0x0000000B, 0x000047BB, 0x000038D7, 0x00004665, 0x00050084, 0x0000000B,
    0x0000363C, 0x00003372, 0x00000A84, 0x00050089, 0x0000000B, 0x00004D53,
    0x000047BB, 0x0000363C, 0x000500C2, 0x0000000B, 0x0000552E, 0x00004D53,
    0x00000A10, 0x00060041, 0x00000294, 0x00004316, 0x00000CC7, 0x00000A0B,
    0x0000552E, 0x0004003D, 0x00000017, 0x00003141, 0x00004316, 0x00050080,
    0x0000000B, 0x00002DA7, 0x0000552E, 0x00000A0D, 0x00060041, 0x00000294,
    0x00001C1D, 0x00000CC7, 0x00000A0B, 0x00002DA7, 0x0004003D, 0x00000017,
    0x00004D82, 0x00001C1D, 0x000500AA, 0x00000009, 0x00005272, 0x00001A29,
    0x00000A0A, 0x000300F7, 0x000033DC, 0x00000000, 0x000400FA, 0x00005272,
    0x00002CBB, 0x000033DC, 0x000200F8, 0x00002CBB, 0x00050051, 0x0000000B,
    0x00005E5C, 0x00004A7B, 0x00000000, 0x000500AB, 0x00000009, 0x000057C6,
    0x00005E5C, 0x00000A0A, 0x000200F9, 0x000033DC, 0x000200F8, 0x000033DC,
    0x000700F5, 0x00000009, 0x00002AAC, 0x00005272, 0x00004944, 0x000057C6,
    0x00002CBB, 0x000300F7, 0x00005596, 0x00000002, 0x000400FA, 0x00002AAC,
    0x00002CF4, 0x00005596, 0x000200F8, 0x00002CF4, 0x00050051, 0x0000000B,
    0x00005C2F, 0x00004A7B, 0x00000000, 0x000500AE, 0x00000009, 0x000043C2,
    0x00005C2F, 0x00000A10, 0x000300F7, 0x00004946, 0x00000000, 0x000400FA,
    0x000043C2, 0x00003E05, 0x00004946, 0x000200F8, 0x00003E05, 0x000500AE,
    0x00000009, 0x00005FD4, 0x00005C2F, 0x00000A13, 0x000300F7, 0x00004945,
    0x00000000, 0x000400FA, 0x00005FD4, 0x00002620, 0x00004945, 0x000200F8,
    0x00002620, 0x00050051, 0x0000000B, 0x00005002, 0x00003141, 0x00000003,
    0x00060052, 0x00000017, 0x000037FF, 0x00005002, 0x00003141, 0x00000002,
    0x000200F9, 0x00004945, 0x000200F8, 0x00004945, 0x000700F5, 0x00000017,
    0x000043E3, 0x00003141, 0x00003E05, 0x000037FF, 0x00002620, 0x00050051,
    0x0000000B, 0x00001B5A, 0x000043E3, 0x00000002, 0x00060052, 0x00000017,
    0x00003B28, 0x00001B5A, 0x000043E3, 0x00000001, 0x000200F9, 0x00004946,
    0x000200F8, 0x00004946, 0x000700F5, 0x00000017, 0x000043E4, 0x00003141,
    0x00002CF4, 0x00003B28, 0x00004945, 0x00050051, 0x0000000B, 0x00001B5B,
    0x000043E4, 0x00000001, 0x00060052, 0x00000017, 0x00003B29, 0x00001B5B,
    0x000043E4, 0x00000000, 0x000200F9, 0x00005596, 0x000200F8, 0x00005596,
    0x000700F5, 0x00000017, 0x00002AAD, 0x00003141, 0x000033DC, 0x00003B29,
    0x00004946, 0x000300F7, 0x0000530F, 0x00000002, 0x000400FA, 0x00004C75,
    0x0000577B, 0x0000530F, 0x000200F8, 0x0000577B, 0x000300F7, 0x000039F4,
    0x00000000, 0x000F00FB, 0x000023AA, 0x000039F4, 0x00000000, 0x000055A0,
    0x00000001, 0x000055A0, 0x00000002, 0x00002897, 0x00000003, 0x00002897,
    0x0000000A, 0x00002897, 0x0000000C, 0x00002897, 0x000200F8, 0x00002897,
    0x000500C7, 0x00000017, 0x00003BA9, 0x00002AAD, 0x00000930, 0x000500C7,
    0x00000017, 0x00005C0C, 0x00002AAD, 0x000003A1, 0x000500C4, 0x00000017,
    0x00006105, 0x00005C0C, 0x000003C5, 0x000500C5, 0x00000017, 0x00004655,
    0x00003BA9, 0x00006105, 0x000500C2, 0x00000017, 0x00005A82, 0x00002AAD,
    0x000003C5, 0x000500C7, 0x00000017, 0x0000192A, 0x00005A82, 0x000003A1,
    0x000500C5, 0x00000017, 0x00003CE5, 0x00004655, 0x0000192A, 0x000500C7,
    0x00000017, 0x00004C3F, 0x00004D82, 0x00000930, 0x000500C7, 0x00000017,
    0x0000461A, 0x00004D82, 0x000003A1, 0x000500C4, 0x00000017, 0x00006106,
    0x0000461A, 0x000003C5, 0x000500C5, 0x00000017, 0x00004656, 0x00004C3F,
    0x00006106, 0x000500C2, 0x00000017, 0x00005A83, 0x00004D82, 0x000003C5,
    0x000500C7, 0x00000017, 0x00001CE0, 0x00005A83, 0x000003A1, 0x000500C5,
    0x00000017, 0x00001EBE, 0x00004656, 0x00001CE0, 0x000200F9, 0x000039F4,
    0x000200F8, 0x000055A0, 0x000500C7, 0x00000017, 0x00004E95, 0x00002AAD,
    0x0000072E, 0x000500C7, 0x00000017, 0x00005C0D, 0x00002AAD, 0x0000064B,
    0x000500C4, 0x00000017, 0x00006107, 0x00005C0D, 0x000002ED, 0x000500C5,
    0x00000017, 0x00004657, 0x00004E95, 0x00006107, 0x000500C2, 0x00000017,
    0x00005A84, 0x00002AAD, 0x000002ED, 0x000500C7, 0x00000017, 0x0000192B,
    0x00005A84, 0x0000064B, 0x000500C5, 0x00000017, 0x00003CE6, 0x00004657,
    0x0000192B, 0x000500C7, 0x00000017, 0x00004C40, 0x00004D82, 0x0000072E,
    0x000500C7, 0x00000017, 0x0000461B, 0x00004D82, 0x0000064B, 0x000500C4,
    0x00000017, 0x00006108, 0x0000461B, 0x000002ED, 0x000500C5, 0x00000017,
    0x00004658, 0x00004C40, 0x00006108, 0x000500C2, 0x00000017, 0x00005A85,
    0x00004D82, 0x000002ED, 0x000500C7, 0x00000017, 0x00001CE1, 0x00005A85,
    0x0000064B, 0x000500C5, 0x00000017, 0x00001EBF, 0x00004658, 0x00001CE1,
    0x000200F9, 0x000039F4, 0x000200F8, 0x000039F4, 0x000900F5, 0x00000017,
    0x00002BF3, 0x00004D82, 0x0000577B, 0x00001EBF, 0x000055A0, 0x00001EBE,
    0x00002897, 0x000900F5, 0x00000017, 0x0000358D, 0x00002AAD, 0x0000577B,
    0x00003CE6, 0x000055A0, 0x00003CE5, 0x00002897, 0x000200F9, 0x0000530F,
    0x000200F8, 0x0000530F, 0x000700F5, 0x00000017, 0x000022F8, 0x00004D82,
    0x00005596, 0x00002BF3, 0x000039F4, 0x000700F5, 0x00000017, 0x000049A7,
    0x00002AAD, 0x00005596, 0x0000358D, 0x000039F4, 0x00050080, 0x00000011,
    0x000035BB, 0x00002670, 0x000059EC, 0x00050051, 0x0000000B, 0x000033BC,
    0x000035BB, 0x00000000, 0x00050051, 0x0000000B, 0x00002553, 0x000035BB,
    0x00000001, 0x000500C2, 0x0000000B, 0x00002B2D, 0x000033BC, 0x00000A10,
    0x00050050, 0x00000011, 0x00001E98, 0x00002B2D, 0x00002553, 0x00050086,
    0x00000011, 0x00006158, 0x00001E98, 0x00005C31, 0x00050051, 0x0000000B,
    0x0000366C, 0x00006158, 0x00000000, 0x000500C4, 0x0000000B, 0x00004D3A,
    0x0000366C, 0x00000A10, 0x00050051, 0x0000000B, 0x00005EBB, 0x00006158,
    0x00000001, 0x00060050, 0x00000014, 0x000053CC, 0x00004D3A, 0x00005EBB,
    0x000059FD, 0x000300F7, 0x00005341, 0x00000002, 0x000400FA, 0x00005010,
    0x000056BE, 0x00002A98, 0x000200F8, 0x00002A98, 0x0007004F, 0x00000011,
    0x00001CAB, 0x000053CC, 0x000053CC, 0x00000000, 0x00000001, 0x0004007C,
    0x00000012, 0x000059CF, 0x00001CAB, 0x00050051, 0x0000000C, 0x00001903,
    0x000059CF, 0x00000000, 0x000500C3, 0x0000000C, 0x000024FD, 0x00001903,
    0x00000A1A, 0x00050051, 0x0000000C, 0x00002747, 0x000059CF, 0x00000001,
    0x000500C3, 0x0000000C, 0x0000405C, 0x00002747, 0x00000A1A, 0x000500C2,
    0x0000000B, 0x00005B4D, 0x00003DA7, 0x00000A19, 0x0004007C, 0x0000000C,
    0x000018AA, 0x00005B4D, 0x00050084, 0x0000000C, 0x00005347, 0x0000405C,
    0x000018AA, 0x00050080, 0x0000000C, 0x00003F5E, 0x000024FD, 0x00005347,
    0x000500C4, 0x0000000C, 0x00004A8E, 0x00003F5E, 0x00000A25, 0x000500C7,
    0x0000000C, 0x00002AB6, 0x00001903, 0x00000A20, 0x000500C7, 0x0000000C,
    0x00003138, 0x00002747, 0x00000A35, 0x000500C4, 0x0000000C, 0x0000454D,
    0x00003138, 0x00000A11, 0x00050080, 0x0000000C, 0x00004397, 0x00002AB6,
    0x0000454D, 0x000500C4, 0x0000000C, 0x000018E7, 0x00004397, 0x00000A10,
    0x000500C7, 0x0000000C, 0x000027B1, 0x000018E7, 0x000009DB, 0x000500C4,
    0x0000000C, 0x00002F76, 0x000027B1, 0x00000A0E, 0x00050080, 0x0000000C,
    0x00003C4B, 0x00004A8E, 0x00002F76, 0x000500C7, 0x0000000C, 0x00003397,
    0x000018E7, 0x00000A38, 0x00050080, 0x0000000C, 0x00004D30, 0x00003C4B,
    0x00003397, 0x000500C7, 0x0000000C, 0x000047B4, 0x00002747, 0x00000A0E,
    0x000500C4, 0x0000000C, 0x0000544A, 0x000047B4, 0x00000A17, 0x00050080,
    0x0000000C, 0x00004157, 0x00004D30, 0x0000544A, 0x000500C7, 0x0000000C,
    0x00005022, 0x00004157, 0x0000040B, 0x000500C4, 0x0000000C, 0x00002416,
    0x00005022, 0x00000A14, 0x000500C7, 0x0000000C, 0x00004A33, 0x00002747,
    0x00000A3B, 0x000500C4, 0x0000000C, 0x00002F77, 0x00004A33, 0x00000A20,
    0x00050080, 0x0000000C, 0x00004158, 0x00002416, 0x00002F77, 0x000500C7,
    0x0000000C, 0x00004ADD, 0x00004157, 0x00000388, 0x000500C4, 0x0000000C,
    0x0000544B, 0x00004ADD, 0x00000A11, 0x00050080, 0x0000000C, 0x00004144,
    0x00004158, 0x0000544B, 0x000500C7, 0x0000000C, 0x00005083, 0x00002747,
    0x00000A23, 0x000500C3, 0x0000000C, 0x000041BF, 0x00005083, 0x00000A11,
    0x000500C3, 0x0000000C, 0x00001EEC, 0x00001903, 0x00000A14, 0x00050080,
    0x0000000C, 0x000035B6, 0x000041BF, 0x00001EEC, 0x000500C7, 0x0000000C,
    0x00005453, 0x000035B6, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544C,
    0x00005453, 0x00000A1D, 0x00050080, 0x0000000C, 0x00003C4C, 0x00004144,
    0x0000544C, 0x000500C7, 0x0000000C, 0x00002E06, 0x00004157, 0x00000AC8,
    0x00050080, 0x0000000C, 0x0000394F, 0x00003C4C, 0x00002E06, 0x0004007C,
    0x0000000B, 0x0000566F, 0x0000394F, 0x000200F9, 0x00005341, 0x000200F8,
    0x000056BE, 0x0004007C, 0x00000016, 0x000019AD, 0x000053CC, 0x00050051,
    0x0000000C, 0x000042C2, 0x000019AD, 0x00000001, 0x000500C3, 0x0000000C,
    0x000024FE, 0x000042C2, 0x00000A17, 0x00050051, 0x0000000C, 0x00002748,
    0x000019AD, 0x00000002, 0x000500C3, 0x0000000C, 0x0000405D, 0x00002748,
    0x00000A11, 0x000500C2, 0x0000000B, 0x00005B4E, 0x00006273, 0x00000A16,
    0x0004007C, 0x0000000C, 0x000018AB, 0x00005B4E, 0x00050084, 0x0000000C,
    0x00005321, 0x0000405D, 0x000018AB, 0x00050080, 0x0000000C, 0x00003B27,
    0x000024FE, 0x00005321, 0x000500C2, 0x0000000B, 0x00002348, 0x00003DA7,
    0x00000A19, 0x0004007C, 0x0000000C, 0x0000308B, 0x00002348, 0x00050084,
    0x0000000C, 0x00002878, 0x00003B27, 0x0000308B, 0x00050051, 0x0000000C,
    0x00006242, 0x000019AD, 0x00000000, 0x000500C3, 0x0000000C, 0x00004FC7,
    0x00006242, 0x00000A1A, 0x00050080, 0x0000000C, 0x000049FC, 0x00004FC7,
    0x00002878, 0x000500C4, 0x0000000C, 0x0000225D, 0x000049FC, 0x00000A22,
    0x000500C7, 0x0000000C, 0x00002CF6, 0x0000225D, 0x0000078B, 0x000500C4,
    0x0000000C, 0x000049FA, 0x00002CF6, 0x00000A0E, 0x000500C7, 0x0000000C,
    0x00004D38, 0x00006242, 0x00000A20, 0x000500C7, 0x0000000C, 0x00003139,
    0x000042C2, 0x00000A1D, 0x000500C4, 0x0000000C, 0x0000454E, 0x00003139,
    0x00000A11, 0x00050080, 0x0000000C, 0x0000434B, 0x00004D38, 0x0000454E,
    0x000500C4, 0x0000000C, 0x00001B88, 0x0000434B, 0x00000A22, 0x000500C3,
    0x0000000C, 0x00005DE3, 0x00001B88, 0x00000A1D, 0x000500C3, 0x0000000C,
    0x00002215, 0x000042C2, 0x00000A14, 0x00050080, 0x0000000C, 0x000035A3,
    0x00002215, 0x0000405D, 0x000500C7, 0x0000000C, 0x00005A0C, 0x000035A3,
    0x00000A0E, 0x000500C3, 0x0000000C, 0x00004112, 0x00006242, 0x00000A14,
    0x000500C4, 0x0000000C, 0x0000496A, 0x00005A0C, 0x00000A0E, 0x00050080,
    0x0000000C, 0x000034BD, 0x00004112, 0x0000496A, 0x000500C7, 0x0000000C,
    0x00004ADE, 0x000034BD, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544D,
    0x00004ADE, 0x00000A0E, 0x00050080, 0x0000000C, 0x00003C4D, 0x00005A0C,
    0x0000544D, 0x000500C7, 0x0000000C, 0x0000335E, 0x00005DE3, 0x000009DB,
    0x00050080, 0x0000000C, 0x00004F70, 0x000049FA, 0x0000335E, 0x000500C4,
    0x0000000C, 0x00005B31, 0x00004F70, 0x00000A0E, 0x000500C7, 0x0000000C,
    0x00005AEA, 0x00005DE3, 0x00000A38, 0x00050080, 0x0000000C, 0x0000285C,
    0x00005B31, 0x00005AEA, 0x000500C7, 0x0000000C, 0x000047B5, 0x00002748,
    0x00000A14, 0x000500C4, 0x0000000C, 0x0000544E, 0x000047B5, 0x00000A22,
    0x00050080, 0x0000000C, 0x00004159, 0x0000285C, 0x0000544E, 0x000500C7,
    0x0000000C, 0x00004ADF, 0x000042C2, 0x00000A0E, 0x000500C4, 0x0000000C,
    0x0000544F, 0x00004ADF, 0x00000A17, 0x00050080, 0x0000000C, 0x0000415A,
    0x00004159, 0x0000544F, 0x000500C7, 0x0000000C, 0x00004FD6, 0x00003C4D,
    0x00000A0E, 0x000500C4, 0x0000000C, 0x00002703, 0x00004FD6, 0x00000A14,
    0x000500C3, 0x0000000C, 0x00003332, 0x0000415A, 0x00000A1D, 0x000500C7,
    0x0000000C, 0x000036D6, 0x00003332, 0x00000A20, 0x00050080, 0x0000000C,
    0x00003412, 0x00002703, 0x000036D6, 0x000500C4, 0x0000000C, 0x00005B32,
    0x00003412, 0x00000A14, 0x000500C7, 0x0000000C, 0x00005AB1, 0x00003C4D,
    0x00000A05, 0x00050080, 0x0000000C, 0x00002A9C, 0x00005B32, 0x00005AB1,
    0x000500C4, 0x0000000C, 0x00005B33, 0x00002A9C, 0x00000A11, 0x000500C7,
    0x0000000C, 0x00005AB2, 0x0000415A, 0x0000040B, 0x00050080, 0x0000000C,
    0x00002A9D, 0x00005B33, 0x00005AB2, 0x000500C4, 0x0000000C, 0x00005B34,
    0x00002A9D, 0x00000A14, 0x000500C7, 0x0000000C, 0x00005559, 0x0000415A,
    0x00000AC8, 0x00050080, 0x0000000C, 0x00005EFA, 0x00005B34, 0x00005559,
    0x0004007C, 0x0000000B, 0x00005670, 0x00005EFA, 0x000200F9, 0x00005341,
    0x000200F8, 0x00005341, 0x000700F5, 0x0000000B, 0x000024FC, 0x00005670,
    0x000056BE, 0x0000566F, 0x00002A98, 0x00050084, 0x00000011, 0x00003FA8,
    0x00006158, 0x00005C31, 0x00050082, 0x00000011, 0x00003F85, 0x00001E98,
    0x00003FA8, 0x00050051, 0x0000000B, 0x0000448F, 0x00005C31, 0x00000001,
    0x00050084, 0x0000000B, 0x00005C50, 0x0000229A, 0x0000448F, 0x00050084,
    0x0000000B, 0x00003CA0, 0x000024FC, 0x00005C50, 0x00050051, 0x0000000B,
    0x00003ED4, 0x00003F85, 0x00000000, 0x00050084, 0x0000000B, 0x00003E12,
    0x00003ED4, 0x0000448F, 0x00050051, 0x0000000B, 0x00001AE7, 0x00003F85,
    0x00000001, 0x00050080, 0x0000000B, 0x00002B25, 0x00003E12, 0x00001AE7,
    0x000500C4, 0x0000000B, 0x0000609D, 0x00002B25, 0x00000A10, 0x000500C7,
    0x0000000B, 0x00005AB3, 0x000033BC, 0x00000A13, 0x00050080, 0x0000000B,
    0x00002557, 0x0000609D, 0x00005AB3, 0x000500C4, 0x0000000B, 0x00004593,
    0x00002557, 0x00000A10, 0x00050080, 0x0000000B, 0x00002048, 0x00003CA0,
    0x00004593, 0x000500C2, 0x0000000B, 0x00002015, 0x00002048, 0x00000A16,
    0x000500AA, 0x00000009, 0x00002EEA, 0x00004ADC, 0x00000A0D, 0x000500AA,
    0x00000009, 0x00005776, 0x00004ADC, 0x00000A10, 0x000500A6, 0x00000009,
    0x00005686, 0x00002EEA, 0x00005776, 0x000300F7, 0x00003463, 0x00000000,
    0x000400FA, 0x00005686, 0x00002957, 0x00003463, 0x000200F8, 0x00002957,
    0x000500C7, 0x00000017, 0x0000475F, 0x000049A7, 0x000009CE, 0x000500C4,
    0x00000017, 0x000024D1, 0x0000475F, 0x0000013D, 0x000500C7, 0x00000017,
    0x000050AC, 0x000049A7, 0x0000072E, 0x000500C2, 0x00000017, 0x0000448D,
    0x000050AC, 0x0000013D, 0x000500C5, 0x00000017, 0x00003FF8, 0x000024D1,
    0x0000448D, 0x000200F9, 0x00003463, 0x000200F8, 0x00003463, 0x000700F5,
    0x00000017, 0x00005879, 0x000049A7, 0x00005341, 0x00003FF8, 0x00002957,
    0x000500AA, 0x00000009, 0x00004CB6, 0x00004ADC, 0x00000A13, 0x000500A6,
    0x00000009, 0x00003B23, 0x00005776, 0x00004CB6, 0x000300F7, 0x00002C98,
    0x00000000, 0x000400FA, 0x00003B23, 0x00002B38, 0x00002C98, 0x000200F8,
    0x00002B38, 0x000500C4, 0x00000017, 0x00005E17, 0x00005879, 0x000002ED,
    0x000500C2, 0x00000017, 0x00003BE7, 0x00005879, 0x000002ED, 0x000500C5,
    0x00000017, 0x000029E8, 0x00005E17, 0x00003BE7, 0x000200F9, 0x00002C98,
    0x000200F8, 0x00002C98, 0x000700F5, 0x00000017, 0x00004D37, 0x00005879,
    0x00003463, 0x000029E8, 0x00002B38, 0x00060041, 0x00000294, 0x000019BE,
    0x00001592, 0x00000A0B, 0x00002015, 0x0003003E, 0x000019BE, 0x00004D37,
    0x000500AC, 0x00000009, 0x00005BF6, 0x0000229A, 0x00000A0D, 0x000300F7,
    0x00004AAC, 0x00000002, 0x000400FA, 0x00005BF6, 0x000038DA, 0x000055EA,
    0x000200F8, 0x000055EA, 0x000200F9, 0x00004AAC, 0x000200F8, 0x000038DA,
    0x000500C2, 0x0000000B, 0x0000364A, 0x00001A29, 0x00000A10, 0x00050086,
    0x0000000B, 0x00001F01, 0x0000364A, 0x0000229A, 0x00050084, 0x0000000B,
    0x000041FB, 0x00001F01, 0x0000229A, 0x00050082, 0x0000000B, 0x00003171,
    0x0000364A, 0x000041FB, 0x00050080, 0x0000000B, 0x00002527, 0x00003171,
    0x00000A0D, 0x000500AA, 0x00000009, 0x0000343F, 0x00002527, 0x0000229A,
    0x000300F7, 0x00002458, 0x00000000, 0x000400FA, 0x0000343F, 0x00001CDB,
    0x000055EB, 0x000200F8, 0x000055EB, 0x000200F9, 0x00002458, 0x000200F8,
    0x00001CDB, 0x00050084, 0x0000000B, 0x00003B96, 0x00000A6A, 0x0000229A,
    0x000500C4, 0x0000000B, 0x0000540F, 0x00003171, 0x00000A16, 0x00050082,
    0x0000000B, 0x00004947, 0x00003B96, 0x0000540F, 0x000200F9, 0x00002458,
    0x000200F8, 0x00002458, 0x000700F5, 0x0000000B, 0x0000292C, 0x00004947,
    0x00001CDB, 0x00000A3A, 0x000055EB, 0x000200F9, 0x00004AAC, 0x000200F8,
    0x00004AAC, 0x000700F5, 0x0000000B, 0x000029BC, 0x0000292C, 0x00002458,
    0x00000A6A, 0x000055EA, 0x00050084, 0x0000000B, 0x0000492B, 0x000029BC,
    0x0000448F, 0x000500C2, 0x0000000B, 0x00004DEF, 0x0000492B, 0x00000A16,
    0x00050080, 0x0000000B, 0x00005B72, 0x00002015, 0x00004DEF, 0x000300F7,
    0x00003A1A, 0x00000000, 0x000400FA, 0x00005686, 0x00002958, 0x00003A1A,
    0x000200F8, 0x00002958, 0x000500C7, 0x00000017, 0x00004760, 0x000022F8,
    0x000009CE, 0x000500C4, 0x00000017, 0x000024D2, 0x00004760, 0x0000013D,
    0x000500C7, 0x00000017, 0x000050AD, 0x000022F8, 0x0000072E, 0x000500C2,
    0x00000017, 0x0000448E, 0x000050AD, 0x0000013D, 0x000500C5, 0x00000017,
    0x00003FF9, 0x000024D2, 0x0000448E, 0x000200F9, 0x00003A1A, 0x000200F8,
    0x00003A1A, 0x000700F5, 0x00000017, 0x00002AAE, 0x000022F8, 0x00004AAC,
    0x00003FF9, 0x00002958, 0x000300F7, 0x00002C99, 0x00000000, 0x000400FA,
    0x00003B23, 0x00002B39, 0x00002C99, 0x000200F8, 0x00002B39, 0x000500C4,
    0x00000017, 0x00005E18, 0x00002AAE, 0x000002ED, 0x000500C2, 0x00000017,
    0x00003BE8, 0x00002AAE, 0x000002ED, 0x000500C5, 0x00000017, 0x000029E9,
    0x00005E18, 0x00003BE8, 0x000200F9, 0x00002C99, 0x000200F8, 0x00002C99,
    0x000700F5, 0x00000017, 0x00004D39, 0x00002AAE, 0x00003A1A, 0x000029E9,
    0x00002B39, 0x00060041, 0x00000294, 0x00001F75, 0x00001592, 0x00000A0B,
    0x00005B72, 0x0003003E, 0x00001F75, 0x00004D39, 0x000200F9, 0x00004C7A,
    0x000200F8, 0x00004C7A, 0x000100FD, 0x00010038,
};
