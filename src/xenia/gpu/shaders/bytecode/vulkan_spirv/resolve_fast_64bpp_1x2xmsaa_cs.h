// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 25271
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main" %gl_GlobalInvocationID
               OpExecutionMode %main LocalSize 8 8 1
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %push_const_block_xe "push_const_block_xe"
               OpMemberName %push_const_block_xe 0 "xe_resolve_edram_info"
               OpMemberName %push_const_block_xe 1 "xe_resolve_coordinate_info"
               OpMemberName %push_const_block_xe 2 "xe_resolve_dest_info"
               OpMemberName %push_const_block_xe 3 "xe_resolve_dest_coordinate_info"
               OpMemberName %push_const_block_xe 4 "xe_resolve_dest_base"
               OpName %push_consts_xe "push_consts_xe"
               OpName %gl_GlobalInvocationID "gl_GlobalInvocationID"
               OpName %xe_resolve_edram_xe_block "xe_resolve_edram_xe_block"
               OpMemberName %xe_resolve_edram_xe_block 0 "data"
               OpName %xe_resolve_edram "xe_resolve_edram"
               OpName %xe_resolve_dest_xe_block "xe_resolve_dest_xe_block"
               OpMemberName %xe_resolve_dest_xe_block 0 "data"
               OpName %xe_resolve_dest "xe_resolve_dest"
               OpDecorate %push_const_block_xe Block
               OpMemberDecorate %push_const_block_xe 0 Offset 0
               OpMemberDecorate %push_const_block_xe 1 Offset 4
               OpMemberDecorate %push_const_block_xe 2 Offset 8
               OpMemberDecorate %push_const_block_xe 3 Offset 12
               OpMemberDecorate %push_const_block_xe 4 Offset 16
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpDecorate %xe_resolve_edram_xe_block BufferBlock
               OpMemberDecorate %xe_resolve_edram_xe_block 0 NonWritable
               OpMemberDecorate %xe_resolve_edram_xe_block 0 Offset 0
               OpDecorate %xe_resolve_edram NonWritable
               OpDecorate %xe_resolve_edram Binding 0
               OpDecorate %xe_resolve_edram DescriptorSet 0
               OpDecorate %_runtimearr_v4uint_0 ArrayStride 16
               OpDecorate %xe_resolve_dest_xe_block BufferBlock
               OpMemberDecorate %xe_resolve_dest_xe_block 0 NonReadable
               OpMemberDecorate %xe_resolve_dest_xe_block 0 Offset 0
               OpDecorate %xe_resolve_dest NonReadable
               OpDecorate %xe_resolve_dest Binding 0
               OpDecorate %xe_resolve_dest DescriptorSet 1
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
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
     %uint_8 = OpConstant %uint 8
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
    %uint_16 = OpConstant %uint 16
     %uint_4 = OpConstant %uint 4
       %1837 = OpConstantComposite %v2uint %uint_2 %uint_1
     %v2bool = OpTypeVector %bool 2
     %uint_0 = OpConstant %uint 0
       %1807 = OpConstantComposite %v2uint %uint_0 %uint_0
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %1816 = OpConstantComposite %v2uint %uint_1 %uint_0
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
%int_268435455 = OpConstant %int 268435455
     %int_n2 = OpConstant %int -2
%push_const_block_xe = OpTypeStruct %uint %uint %uint %uint %uint
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
      %int_0 = OpConstant %int 0
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
  %uint_1023 = OpConstant %uint 1023
    %uint_10 = OpConstant %uint 10
    %uint_13 = OpConstant %uint 13
  %uint_2047 = OpConstant %uint 2047
    %uint_24 = OpConstant %uint 24
    %uint_15 = OpConstant %uint 15
    %uint_28 = OpConstant %uint 28
       %1855 = OpConstantComposite %v2uint %uint_0 %uint_4
%uint_16777216 = OpConstant %uint 16777216
    %uint_20 = OpConstant %uint 20
       %2275 = OpConstantComposite %v2uint %uint_20 %uint_24
     %v3uint = OpTypeVector %uint 3
%uint_4294901760 = OpConstant %uint 4294901760
 %uint_65535 = OpConstant %uint 65535
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
       %1825 = OpConstantComposite %v2uint %uint_2 %uint_0
      %false = OpConstantFalse %bool
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_resolve_edram_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform_xe_resolve_edram_xe_block = OpTypePointer Uniform %xe_resolve_edram_xe_block
%xe_resolve_edram = OpVariable %_ptr_Uniform_xe_resolve_edram_xe_block Uniform
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
%_runtimearr_v4uint_0 = OpTypeRuntimeArray %v4uint
%xe_resolve_dest_xe_block = OpTypeStruct %_runtimearr_v4uint_0
%_ptr_Uniform_xe_resolve_dest_xe_block = OpTypePointer Uniform %xe_resolve_dest_xe_block
%xe_resolve_dest = OpVariable %_ptr_Uniform_xe_resolve_dest_xe_block Uniform
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %1954 = OpConstantComposite %v2uint %uint_15 %uint_1
       %1870 = OpConstantComposite %v2uint %uint_3 %uint_3
       %2122 = OpConstantComposite %v2uint %uint_15 %uint_15
     %uint_9 = OpConstant %uint 9
       %1877 = OpConstantComposite %v4uint %uint_4294901760 %uint_4294901760 %uint_4294901760 %uint_4294901760
        %850 = OpConstantComposite %v4uint %uint_65535 %uint_65535 %uint_65535 %uint_65535
       %2510 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
        %317 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %1838 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
        %749 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
    %uint_40 = OpConstant %uint 40
       %2359 = OpConstantComposite %v2uint %uint_40 %uint_16
  %uint_1280 = OpConstant %uint 1280
%uint_2621440 = OpConstant %uint 2621440
       %main = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %19578 None
               OpSwitch %uint_0 %11880
      %11880 = OpLabel
      %22245 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_0
      %15627 = OpLoad %uint %22245
      %22700 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
      %20824 = OpLoad %uint %22700
      %20561 = OpBitwiseAnd %uint %15627 %uint_1023
      %19978 = OpShiftRightLogical %uint %15627 %uint_10
       %8574 = OpBitwiseAnd %uint %19978 %uint_3
      %21002 = OpShiftRightLogical %uint %15627 %uint_13
       %8575 = OpBitwiseAnd %uint %21002 %uint_2047
      %18836 = OpShiftRightLogical %uint %15627 %uint_24
       %9130 = OpBitwiseAnd %uint %18836 %uint_15
       %8871 = OpCompositeConstruct %v2uint %20824 %20824
       %9576 = OpShiftRightLogical %v2uint %8871 %1855
      %23379 = OpBitwiseAnd %v2uint %9576 %1954
      %16207 = OpShiftLeftLogical %v2uint %23379 %1870
      %23019 = OpIMul %v2uint %16207 %1828
      %12819 = OpShiftRightLogical %uint %20824 %uint_5
      %16204 = OpBitwiseAnd %uint %12819 %uint_2047
      %18732 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
      %24236 = OpLoad %uint %18732
      %22701 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_3
      %20919 = OpLoad %uint %22701
      %19164 = OpBitwiseAnd %uint %24236 %uint_7
      %21999 = OpBitwiseAnd %uint %24236 %uint_8
      %20495 = OpINotEqual %bool %21999 %uint_0
      %10402 = OpShiftRightLogical %uint %24236 %uint_4
      %23037 = OpBitwiseAnd %uint %10402 %uint_7
      %23118 = OpBitwiseAnd %uint %24236 %uint_16777216
      %19573 = OpINotEqual %bool %23118 %uint_0
       %8003 = OpBitwiseAnd %uint %20919 %uint_1023
      %15783 = OpShiftLeftLogical %uint %8003 %uint_5
      %22591 = OpShiftRightLogical %uint %20919 %uint_10
      %19390 = OpBitwiseAnd %uint %22591 %uint_1023
      %25203 = OpShiftLeftLogical %uint %19390 %uint_5
      %10422 = OpCompositeConstruct %v2uint %20919 %20919
      %10385 = OpShiftRightLogical %v2uint %10422 %2275
      %23380 = OpBitwiseAnd %v2uint %10385 %2122
      %16208 = OpShiftLeftLogical %v2uint %23380 %1870
      %23020 = OpIMul %v2uint %16208 %1828
      %12820 = OpShiftRightLogical %uint %20919 %uint_28
      %16205 = OpBitwiseAnd %uint %12820 %uint_7
      %18656 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_4
      %25270 = OpLoad %uint %18656
      %14159 = OpLoad %v3uint %gl_GlobalInvocationID
      %12672 = OpVectorShuffle %v2uint %14159 %14159 0 1
      %12025 = OpShiftLeftLogical %v2uint %12672 %1825
       %7640 = OpCompositeExtract %uint %12025 0
      %11658 = OpShiftLeftLogical %uint %16204 %uint_3
      %15379 = OpUGreaterThanEqual %bool %7640 %11658
               OpSelectionMerge %12755 DontFlatten
               OpBranchConditional %15379 %21992 %12755
      %21992 = OpLabel
               OpBranch %19578
      %12755 = OpLabel
       %7340 = OpCompositeExtract %uint %12025 1
       %7992 = OpExtInst %uint %1 UMax %7340 %uint_0
      %20975 = OpCompositeConstruct %v2uint %7640 %7992
      %21036 = OpIAdd %v2uint %20975 %23019
      %16075 = OpULessThanEqual %bool %16205 %uint_3
               OpSelectionMerge %23776 None
               OpBranchConditional %16075 %10990 %15087
      %15087 = OpLabel
      %13566 = OpIEqual %bool %16205 %uint_5
       %8438 = OpSelect %uint %13566 %uint_2 %uint_0
               OpBranch %23776
      %10990 = OpLabel
               OpBranch %23776
      %23776 = OpLabel
      %19300 = OpPhi %uint %16205 %10990 %8438 %15087
      %16830 = OpCompositeConstruct %v2uint %8574 %8574
      %11801 = OpUGreaterThanEqual %v2bool %16830 %1837
      %19381 = OpSelect %v2uint %11801 %1828 %1807
      %10986 = OpShiftLeftLogical %v2uint %21036 %19381
      %24669 = OpCompositeConstruct %v2uint %19300 %19300
       %9093 = OpShiftRightLogical %v2uint %24669 %1816
      %16110 = OpBitwiseAnd %v2uint %9093 %1828
      %17779 = OpIAdd %v2uint %10986 %16110
      %24270 = OpUDiv %v2uint %17779 %2359
      %12360 = OpCompositeExtract %uint %24270 1
      %11046 = OpIMul %uint %12360 %20561
      %24665 = OpCompositeExtract %uint %24270 0
      %21536 = OpIAdd %uint %11046 %24665
       %8742 = OpIAdd %uint %8575 %21536
      %23345 = OpIMul %v2uint %24270 %2359
      %11892 = OpISub %v2uint %17779 %23345
       %9022 = OpIMul %uint %8742 %uint_1280
      %14471 = OpCompositeExtract %uint %11892 1
      %15890 = OpIMul %uint %14471 %uint_40
       %6886 = OpCompositeExtract %uint %11892 0
       %9696 = OpIAdd %uint %15890 %6886
      %18116 = OpShiftLeftLogical %uint %9696 %uint_1
      %18581 = OpIAdd %uint %9022 %18116
      %17820 = OpUMod %uint %18581 %uint_2621440
      %18580 = OpShiftRightLogical %uint %17820 %uint_2
      %17174 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_edram %int_0 %18580
      %12609 = OpLoad %v4uint %17174
      %11687 = OpIAdd %uint %18580 %uint_1
       %7197 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_edram %int_0 %11687
      %18360 = OpLoad %v4uint %7197
      %15472 = OpIEqual %bool %7640 %uint_0
      %15603 = OpSelect %bool %15472 %false %15472
               OpSelectionMerge %21910 DontFlatten
               OpBranchConditional %15603 %9760 %21910
       %9760 = OpLabel
      %17290 = OpCompositeExtract %uint %12609 2
      %21174 = OpCompositeInsert %v4uint %17290 %12609 0
      %23044 = OpCompositeExtract %uint %12609 3
       %9296 = OpCompositeInsert %v4uint %23044 %21174 1
               OpBranch %21910
      %21910 = OpLabel
      %10924 = OpPhi %v4uint %12609 %23776 %9296 %9760
               OpSelectionMerge %21263 DontFlatten
               OpBranchConditional %19573 %15068 %21263
      %15068 = OpLabel
      %13701 = OpIEqual %bool %9130 %uint_5
      %17015 = OpLogicalNot %bool %13701
               OpSelectionMerge %15698 None
               OpBranchConditional %17015 %16607 %15698
      %16607 = OpLabel
      %18778 = OpIEqual %bool %9130 %uint_7
               OpBranch %15698
      %15698 = OpLabel
      %10925 = OpPhi %bool %13701 %15068 %18778 %16607
               OpSelectionMerge %14836 DontFlatten
               OpBranchConditional %10925 %8360 %14836
       %8360 = OpLabel
      %19441 = OpBitwiseAnd %v4uint %10924 %1877
      %20970 = OpVectorShuffle %v4uint %10924 %10924 1 0 3 2
       %7405 = OpBitwiseAnd %v4uint %20970 %850
      %13888 = OpBitwiseOr %v4uint %19441 %7405
      %21265 = OpBitwiseAnd %v4uint %18360 %1877
      %15352 = OpVectorShuffle %v4uint %18360 %18360 1 0 3 2
       %8355 = OpBitwiseAnd %v4uint %15352 %850
       %8449 = OpBitwiseOr %v4uint %21265 %8355
               OpBranch %14836
      %14836 = OpLabel
      %11251 = OpPhi %v4uint %18360 %15698 %8449 %8360
      %13709 = OpPhi %v4uint %10924 %15698 %13888 %8360
               OpBranch %21263
      %21263 = OpLabel
       %8952 = OpPhi %v4uint %18360 %21910 %11251 %14836
      %22009 = OpPhi %v4uint %10924 %21910 %13709 %14836
       %7319 = OpIAdd %v2uint %12025 %23020
               OpSelectionMerge %21237 DontFlatten
               OpBranchConditional %20495 %10574 %21373
      %21373 = OpLabel
      %10608 = OpBitcast %v2int %7319
      %17090 = OpCompositeExtract %int %10608 0
       %9469 = OpShiftRightArithmetic %int %17090 %int_5
      %10055 = OpCompositeExtract %int %10608 1
      %16476 = OpShiftRightArithmetic %int %10055 %int_5
      %23373 = OpShiftRightLogical %uint %15783 %uint_5
       %6314 = OpBitcast %int %23373
      %21319 = OpIMul %int %16476 %6314
      %16222 = OpIAdd %int %9469 %21319
      %19086 = OpShiftLeftLogical %int %16222 %uint_10
      %10934 = OpBitwiseAnd %int %17090 %int_7
      %12600 = OpBitwiseAnd %int %10055 %int_14
      %17741 = OpShiftLeftLogical %int %12600 %int_2
      %17303 = OpIAdd %int %10934 %17741
       %6375 = OpShiftLeftLogical %int %17303 %uint_3
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
       %7916 = OpShiftRightArithmetic %int %17090 %int_3
      %13750 = OpIAdd %int %16831 %7916
      %21587 = OpBitwiseAnd %int %13750 %int_3
      %21580 = OpShiftLeftLogical %int %21587 %int_6
      %15436 = OpIAdd %int %16708 %21580
      %11782 = OpBitwiseAnd %int %16727 %int_63
      %14671 = OpIAdd %int %15436 %11782
      %22127 = OpBitcast %uint %14671
               OpBranch %21237
      %10574 = OpLabel
      %19866 = OpCompositeExtract %uint %7319 0
      %11267 = OpCompositeExtract %uint %7319 1
       %8414 = OpCompositeConstruct %v3uint %19866 %11267 %23037
      %20125 = OpBitcast %v3int %8414
      %10438 = OpCompositeExtract %int %20125 1
       %9470 = OpShiftRightArithmetic %int %10438 %int_4
      %10056 = OpCompositeExtract %int %20125 2
      %16477 = OpShiftRightArithmetic %int %10056 %int_2
      %23374 = OpShiftRightLogical %uint %25203 %uint_4
       %6315 = OpBitcast %int %23374
      %21281 = OpIMul %int %16477 %6315
      %15143 = OpIAdd %int %9470 %21281
       %9032 = OpShiftRightLogical %uint %15783 %uint_5
      %12427 = OpBitcast %int %9032
      %10360 = OpIMul %int %15143 %12427
      %25154 = OpCompositeExtract %int %20125 0
      %20423 = OpShiftRightArithmetic %int %25154 %int_5
      %18940 = OpIAdd %int %20423 %10360
       %8797 = OpShiftLeftLogical %int %18940 %uint_9
      %11510 = OpBitwiseAnd %int %8797 %int_268435455
      %18938 = OpShiftLeftLogical %int %11510 %int_1
      %19768 = OpBitwiseAnd %int %25154 %int_7
      %12601 = OpBitwiseAnd %int %10438 %int_6
      %17742 = OpShiftLeftLogical %int %12601 %int_2
      %17227 = OpIAdd %int %19768 %17742
       %7048 = OpShiftLeftLogical %int %17227 %uint_9
      %24035 = OpShiftRightArithmetic %int %7048 %int_6
       %8725 = OpShiftRightArithmetic %int %10438 %int_3
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
      %23346 = OpShiftLeftLogical %int %20336 %int_1
      %23274 = OpBitwiseAnd %int %24035 %int_15
      %10332 = OpIAdd %int %23346 %23274
      %18357 = OpBitwiseAnd %int %10056 %int_3
      %21582 = OpShiftLeftLogical %int %18357 %uint_9
      %16729 = OpIAdd %int %10332 %21582
      %19167 = OpBitwiseAnd %int %10438 %int_1
      %21583 = OpShiftLeftLogical %int %19167 %int_4
      %16730 = OpIAdd %int %16729 %21583
      %20438 = OpBitwiseAnd %int %15437 %int_1
       %9987 = OpShiftLeftLogical %int %20438 %int_3
      %13106 = OpShiftRightArithmetic %int %16730 %int_6
      %14038 = OpBitwiseAnd %int %13106 %int_7
      %13330 = OpIAdd %int %9987 %14038
      %23347 = OpShiftLeftLogical %int %13330 %int_3
      %23217 = OpBitwiseAnd %int %15437 %int_n2
      %10908 = OpIAdd %int %23347 %23217
      %23348 = OpShiftLeftLogical %int %10908 %int_2
      %23218 = OpBitwiseAnd %int %16730 %int_n512
      %10909 = OpIAdd %int %23348 %23218
      %23349 = OpShiftLeftLogical %int %10909 %int_3
      %21849 = OpBitwiseAnd %int %16730 %int_63
      %24314 = OpIAdd %int %23349 %21849
      %22128 = OpBitcast %uint %24314
               OpBranch %21237
      %21237 = OpLabel
      %11376 = OpPhi %uint %22128 %10574 %22127 %21373
      %20616 = OpIAdd %uint %11376 %25270
      %20138 = OpShiftRightLogical %uint %20616 %uint_4
      %19356 = OpIEqual %bool %19164 %uint_4
               OpSelectionMerge %14780 None
               OpBranchConditional %19356 %13279 %14780
      %13279 = OpLabel
       %7958 = OpVectorShuffle %v4uint %22009 %22009 1 0 3 2
               OpBranch %14780
      %14780 = OpLabel
      %22898 = OpPhi %v4uint %22009 %21237 %7958 %13279
       %6605 = OpSelect %uint %19356 %uint_2 %19164
      %13412 = OpIEqual %bool %6605 %uint_1
      %18370 = OpIEqual %bool %6605 %uint_2
      %22150 = OpLogicalOr %bool %13412 %18370
               OpSelectionMerge %13411 None
               OpBranchConditional %22150 %10583 %13411
      %10583 = OpLabel
      %18271 = OpBitwiseAnd %v4uint %22898 %2510
       %9425 = OpShiftLeftLogical %v4uint %18271 %317
      %20652 = OpBitwiseAnd %v4uint %22898 %1838
      %17549 = OpShiftRightLogical %v4uint %20652 %317
      %16376 = OpBitwiseOr %v4uint %9425 %17549
               OpBranch %13411
      %13411 = OpLabel
      %22649 = OpPhi %v4uint %22898 %14780 %16376 %10583
      %19638 = OpIEqual %bool %6605 %uint_3
      %15139 = OpLogicalOr %bool %18370 %19638
               OpSelectionMerge %11416 None
               OpBranchConditional %15139 %11064 %11416
      %11064 = OpLabel
      %24087 = OpShiftLeftLogical %v4uint %22649 %749
      %15335 = OpShiftRightLogical %v4uint %22649 %749
      %10728 = OpBitwiseOr %v4uint %24087 %15335
               OpBranch %11416
      %11416 = OpLabel
      %19767 = OpPhi %v4uint %22649 %13411 %10728 %11064
      %24825 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_dest %int_0 %20138
               OpStore %24825 %19767
      %21685 = OpIAdd %uint %20138 %uint_2
               OpSelectionMerge %16262 None
               OpBranchConditional %19356 %13280 %16262
      %13280 = OpLabel
       %7959 = OpVectorShuffle %v4uint %8952 %8952 1 0 3 2
               OpBranch %16262
      %16262 = OpLabel
      %10926 = OpPhi %v4uint %8952 %11416 %7959 %13280
               OpSelectionMerge %14874 None
               OpBranchConditional %22150 %10584 %14874
      %10584 = OpLabel
      %18272 = OpBitwiseAnd %v4uint %10926 %2510
       %9426 = OpShiftLeftLogical %v4uint %18272 %317
      %20653 = OpBitwiseAnd %v4uint %10926 %1838
      %17550 = OpShiftRightLogical %v4uint %20653 %317
      %16377 = OpBitwiseOr %v4uint %9426 %17550
               OpBranch %14874
      %14874 = OpLabel
      %10927 = OpPhi %v4uint %10926 %16262 %16377 %10584
               OpSelectionMerge %11417 None
               OpBranchConditional %15139 %11065 %11417
      %11065 = OpLabel
      %24088 = OpShiftLeftLogical %v4uint %10927 %749
      %15336 = OpShiftRightLogical %v4uint %10927 %749
      %10729 = OpBitwiseOr %v4uint %24088 %15336
               OpBranch %11417
      %11417 = OpLabel
      %19769 = OpPhi %v4uint %10927 %14874 %10729 %11065
       %8053 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_dest %int_0 %21685
               OpStore %8053 %19769
               OpBranch %19578
      %19578 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_fast_64bpp_1x2xmsaa_cs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x000062B7, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F48, 0x00060010, 0x0000161F,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x0000161F, 0x6E69616D,
    0x00000000, 0x00070005, 0x0000040C, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x00090006, 0x0000040C, 0x00000000, 0x725F6578,
    0x6C6F7365, 0x655F6576, 0x6D617264, 0x666E695F, 0x0000006F, 0x000A0006,
    0x0000040C, 0x00000001, 0x725F6578, 0x6C6F7365, 0x635F6576, 0x64726F6F,
    0x74616E69, 0x6E695F65, 0x00006F66, 0x00090006, 0x0000040C, 0x00000002,
    0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365, 0x6F666E69, 0x00000000,
    0x000B0006, 0x0000040C, 0x00000003, 0x725F6578, 0x6C6F7365, 0x645F6576,
    0x5F747365, 0x726F6F63, 0x616E6964, 0x695F6574, 0x006F666E, 0x00090006,
    0x0000040C, 0x00000004, 0x725F6578, 0x6C6F7365, 0x645F6576, 0x5F747365,
    0x65736162, 0x00000000, 0x00060005, 0x00000CE9, 0x68737570, 0x6E6F635F,
    0x5F737473, 0x00006578, 0x00080005, 0x00000F48, 0x475F6C67, 0x61626F6C,
    0x766E496C, 0x7461636F, 0x496E6F69, 0x00000044, 0x00090005, 0x000007B4,
    0x725F6578, 0x6C6F7365, 0x655F6576, 0x6D617264, 0x5F65785F, 0x636F6C62,
    0x0000006B, 0x00050006, 0x000007B4, 0x00000000, 0x61746164, 0x00000000,
    0x00070005, 0x00000CC7, 0x725F6578, 0x6C6F7365, 0x655F6576, 0x6D617264,
    0x00000000, 0x00090005, 0x000007B5, 0x725F6578, 0x6C6F7365, 0x645F6576,
    0x5F747365, 0x625F6578, 0x6B636F6C, 0x00000000, 0x00050006, 0x000007B5,
    0x00000000, 0x61746164, 0x00000000, 0x00060005, 0x00001592, 0x725F6578,
    0x6C6F7365, 0x645F6576, 0x00747365, 0x00030047, 0x0000040C, 0x00000002,
    0x00050048, 0x0000040C, 0x00000000, 0x00000023, 0x00000000, 0x00050048,
    0x0000040C, 0x00000001, 0x00000023, 0x00000004, 0x00050048, 0x0000040C,
    0x00000002, 0x00000023, 0x00000008, 0x00050048, 0x0000040C, 0x00000003,
    0x00000023, 0x0000000C, 0x00050048, 0x0000040C, 0x00000004, 0x00000023,
    0x00000010, 0x00040047, 0x00000F48, 0x0000000B, 0x0000001C, 0x00040047,
    0x000007DC, 0x00000006, 0x00000010, 0x00030047, 0x000007B4, 0x00000003,
    0x00040048, 0x000007B4, 0x00000000, 0x00000018, 0x00050048, 0x000007B4,
    0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000CC7, 0x00000018,
    0x00040047, 0x00000CC7, 0x00000021, 0x00000000, 0x00040047, 0x00000CC7,
    0x00000022, 0x00000000, 0x00040047, 0x000007DD, 0x00000006, 0x00000010,
    0x00030047, 0x000007B5, 0x00000003, 0x00040048, 0x000007B5, 0x00000000,
    0x00000019, 0x00050048, 0x000007B5, 0x00000000, 0x00000023, 0x00000000,
    0x00030047, 0x00001592, 0x00000019, 0x00040047, 0x00001592, 0x00000021,
    0x00000000, 0x00040047, 0x00001592, 0x00000022, 0x00000001, 0x00040047,
    0x00000AC7, 0x0000000B, 0x00000019, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00040017, 0x00000017,
    0x0000000B, 0x00000004, 0x00020014, 0x00000009, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x00040017, 0x00000012, 0x0000000C, 0x00000002,
    0x00040017, 0x00000016, 0x0000000C, 0x00000003, 0x0004002B, 0x0000000B,
    0x00000A0D, 0x00000001, 0x0004002B, 0x0000000B, 0x00000A10, 0x00000002,
    0x0004002B, 0x0000000B, 0x000008A6, 0x00FF00FF, 0x0004002B, 0x0000000B,
    0x00000A22, 0x00000008, 0x0004002B, 0x0000000B, 0x000005FD, 0xFF00FF00,
    0x0004002B, 0x0000000B, 0x00000A13, 0x00000003, 0x0004002B, 0x0000000B,
    0x00000A3A, 0x00000010, 0x0004002B, 0x0000000B, 0x00000A16, 0x00000004,
    0x0005002C, 0x00000011, 0x0000072D, 0x00000A10, 0x00000A0D, 0x00040017,
    0x0000000F, 0x00000009, 0x00000002, 0x0004002B, 0x0000000B, 0x00000A0A,
    0x00000000, 0x0005002C, 0x00000011, 0x0000070F, 0x00000A0A, 0x00000A0A,
    0x0005002C, 0x00000011, 0x00000724, 0x00000A0D, 0x00000A0D, 0x0005002C,
    0x00000011, 0x00000718, 0x00000A0D, 0x00000A0A, 0x0004002B, 0x0000000C,
    0x00000A1A, 0x00000005, 0x0004002B, 0x0000000B, 0x00000A19, 0x00000005,
    0x0004002B, 0x0000000B, 0x00000A1F, 0x00000007, 0x0004002B, 0x0000000C,
    0x00000A20, 0x00000007, 0x0004002B, 0x0000000C, 0x00000A35, 0x0000000E,
    0x0004002B, 0x0000000C, 0x00000A11, 0x00000002, 0x0004002B, 0x0000000C,
    0x000009DB, 0xFFFFFFF0, 0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001,
    0x0004002B, 0x0000000C, 0x00000A38, 0x0000000F, 0x0004002B, 0x0000000C,
    0x00000A17, 0x00000004, 0x0004002B, 0x0000000C, 0x0000040B, 0xFFFFFE00,
    0x0004002B, 0x0000000C, 0x00000A14, 0x00000003, 0x0004002B, 0x0000000C,
    0x00000A3B, 0x00000010, 0x0004002B, 0x0000000C, 0x00000388, 0x000001C0,
    0x0004002B, 0x0000000C, 0x00000A23, 0x00000008, 0x0004002B, 0x0000000C,
    0x00000A1D, 0x00000006, 0x0004002B, 0x0000000C, 0x00000AC8, 0x0000003F,
    0x0004002B, 0x0000000C, 0x0000078B, 0x0FFFFFFF, 0x0004002B, 0x0000000C,
    0x00000A05, 0xFFFFFFFE, 0x0007001E, 0x0000040C, 0x0000000B, 0x0000000B,
    0x0000000B, 0x0000000B, 0x0000000B, 0x00040020, 0x00000688, 0x00000009,
    0x0000040C, 0x0004003B, 0x00000688, 0x00000CE9, 0x00000009, 0x0004002B,
    0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x00000288, 0x00000009,
    0x0000000B, 0x0004002B, 0x0000000B, 0x00000A44, 0x000003FF, 0x0004002B,
    0x0000000B, 0x00000A28, 0x0000000A, 0x0004002B, 0x0000000B, 0x00000A31,
    0x0000000D, 0x0004002B, 0x0000000B, 0x00000A81, 0x000007FF, 0x0004002B,
    0x0000000B, 0x00000A52, 0x00000018, 0x0004002B, 0x0000000B, 0x00000A37,
    0x0000000F, 0x0004002B, 0x0000000B, 0x00000A5E, 0x0000001C, 0x0005002C,
    0x00000011, 0x0000073F, 0x00000A0A, 0x00000A16, 0x0004002B, 0x0000000B,
    0x00000926, 0x01000000, 0x0004002B, 0x0000000B, 0x00000A46, 0x00000014,
    0x0005002C, 0x00000011, 0x000008E3, 0x00000A46, 0x00000A52, 0x00040017,
    0x00000014, 0x0000000B, 0x00000003, 0x0004002B, 0x0000000B, 0x0000068D,
    0xFFFF0000, 0x0004002B, 0x0000000B, 0x000001C1, 0x0000FFFF, 0x00040020,
    0x00000291, 0x00000001, 0x00000014, 0x0004003B, 0x00000291, 0x00000F48,
    0x00000001, 0x0005002C, 0x00000011, 0x00000721, 0x00000A10, 0x00000A0A,
    0x0003002A, 0x00000009, 0x00000787, 0x0003001D, 0x000007DC, 0x00000017,
    0x0003001E, 0x000007B4, 0x000007DC, 0x00040020, 0x00000A32, 0x00000002,
    0x000007B4, 0x0004003B, 0x00000A32, 0x00000CC7, 0x00000002, 0x00040020,
    0x00000294, 0x00000002, 0x00000017, 0x0003001D, 0x000007DD, 0x00000017,
    0x0003001E, 0x000007B5, 0x000007DD, 0x00040020, 0x00000A33, 0x00000002,
    0x000007B5, 0x0004003B, 0x00000A33, 0x00001592, 0x00000002, 0x0006002C,
    0x00000014, 0x00000AC7, 0x00000A22, 0x00000A22, 0x00000A0D, 0x0005002C,
    0x00000011, 0x000007A2, 0x00000A37, 0x00000A0D, 0x0005002C, 0x00000011,
    0x0000074E, 0x00000A13, 0x00000A13, 0x0005002C, 0x00000011, 0x0000084A,
    0x00000A37, 0x00000A37, 0x0004002B, 0x0000000B, 0x00000A25, 0x00000009,
    0x0007002C, 0x00000017, 0x00000755, 0x0000068D, 0x0000068D, 0x0000068D,
    0x0000068D, 0x0007002C, 0x00000017, 0x00000352, 0x000001C1, 0x000001C1,
    0x000001C1, 0x000001C1, 0x0007002C, 0x00000017, 0x000009CE, 0x000008A6,
    0x000008A6, 0x000008A6, 0x000008A6, 0x0007002C, 0x00000017, 0x0000013D,
    0x00000A22, 0x00000A22, 0x00000A22, 0x00000A22, 0x0007002C, 0x00000017,
    0x0000072E, 0x000005FD, 0x000005FD, 0x000005FD, 0x000005FD, 0x0007002C,
    0x00000017, 0x000002ED, 0x00000A3A, 0x00000A3A, 0x00000A3A, 0x00000A3A,
    0x0004002B, 0x0000000B, 0x00000A82, 0x00000028, 0x0005002C, 0x00000011,
    0x00000937, 0x00000A82, 0x00000A3A, 0x0004002B, 0x0000000B, 0x00000184,
    0x00000500, 0x0004002B, 0x0000000B, 0x0000086E, 0x00280000, 0x00050036,
    0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00003B06,
    0x000300F7, 0x00004C7A, 0x00000000, 0x000300FB, 0x00000A0A, 0x00002E68,
    0x000200F8, 0x00002E68, 0x00050041, 0x00000288, 0x000056E5, 0x00000CE9,
    0x00000A0B, 0x0004003D, 0x0000000B, 0x00003D0B, 0x000056E5, 0x00050041,
    0x00000288, 0x000058AC, 0x00000CE9, 0x00000A0E, 0x0004003D, 0x0000000B,
    0x00005158, 0x000058AC, 0x000500C7, 0x0000000B, 0x00005051, 0x00003D0B,
    0x00000A44, 0x000500C2, 0x0000000B, 0x00004E0A, 0x00003D0B, 0x00000A28,
    0x000500C7, 0x0000000B, 0x0000217E, 0x00004E0A, 0x00000A13, 0x000500C2,
    0x0000000B, 0x0000520A, 0x00003D0B, 0x00000A31, 0x000500C7, 0x0000000B,
    0x0000217F, 0x0000520A, 0x00000A81, 0x000500C2, 0x0000000B, 0x00004994,
    0x00003D0B, 0x00000A52, 0x000500C7, 0x0000000B, 0x000023AA, 0x00004994,
    0x00000A37, 0x00050050, 0x00000011, 0x000022A7, 0x00005158, 0x00005158,
    0x000500C2, 0x00000011, 0x00002568, 0x000022A7, 0x0000073F, 0x000500C7,
    0x00000011, 0x00005B53, 0x00002568, 0x000007A2, 0x000500C4, 0x00000011,
    0x00003F4F, 0x00005B53, 0x0000074E, 0x00050084, 0x00000011, 0x000059EB,
    0x00003F4F, 0x00000724, 0x000500C2, 0x0000000B, 0x00003213, 0x00005158,
    0x00000A19, 0x000500C7, 0x0000000B, 0x00003F4C, 0x00003213, 0x00000A81,
    0x00050041, 0x00000288, 0x0000492C, 0x00000CE9, 0x00000A11, 0x0004003D,
    0x0000000B, 0x00005EAC, 0x0000492C, 0x00050041, 0x00000288, 0x000058AD,
    0x00000CE9, 0x00000A14, 0x0004003D, 0x0000000B, 0x000051B7, 0x000058AD,
    0x000500C7, 0x0000000B, 0x00004ADC, 0x00005EAC, 0x00000A1F, 0x000500C7,
    0x0000000B, 0x000055EF, 0x00005EAC, 0x00000A22, 0x000500AB, 0x00000009,
    0x0000500F, 0x000055EF, 0x00000A0A, 0x000500C2, 0x0000000B, 0x000028A2,
    0x00005EAC, 0x00000A16, 0x000500C7, 0x0000000B, 0x000059FD, 0x000028A2,
    0x00000A1F, 0x000500C7, 0x0000000B, 0x00005A4E, 0x00005EAC, 0x00000926,
    0x000500AB, 0x00000009, 0x00004C75, 0x00005A4E, 0x00000A0A, 0x000500C7,
    0x0000000B, 0x00001F43, 0x000051B7, 0x00000A44, 0x000500C4, 0x0000000B,
    0x00003DA7, 0x00001F43, 0x00000A19, 0x000500C2, 0x0000000B, 0x0000583F,
    0x000051B7, 0x00000A28, 0x000500C7, 0x0000000B, 0x00004BBE, 0x0000583F,
    0x00000A44, 0x000500C4, 0x0000000B, 0x00006273, 0x00004BBE, 0x00000A19,
    0x00050050, 0x00000011, 0x000028B6, 0x000051B7, 0x000051B7, 0x000500C2,
    0x00000011, 0x00002891, 0x000028B6, 0x000008E3, 0x000500C7, 0x00000011,
    0x00005B54, 0x00002891, 0x0000084A, 0x000500C4, 0x00000011, 0x00003F50,
    0x00005B54, 0x0000074E, 0x00050084, 0x00000011, 0x000059EC, 0x00003F50,
    0x00000724, 0x000500C2, 0x0000000B, 0x00003214, 0x000051B7, 0x00000A5E,
    0x000500C7, 0x0000000B, 0x00003F4D, 0x00003214, 0x00000A1F, 0x00050041,
    0x00000288, 0x000048E0, 0x00000CE9, 0x00000A17, 0x0004003D, 0x0000000B,
    0x000062B6, 0x000048E0, 0x0004003D, 0x00000014, 0x0000374F, 0x00000F48,
    0x0007004F, 0x00000011, 0x00003180, 0x0000374F, 0x0000374F, 0x00000000,
    0x00000001, 0x000500C4, 0x00000011, 0x00002EF9, 0x00003180, 0x00000721,
    0x00050051, 0x0000000B, 0x00001DD8, 0x00002EF9, 0x00000000, 0x000500C4,
    0x0000000B, 0x00002D8A, 0x00003F4C, 0x00000A13, 0x000500AE, 0x00000009,
    0x00003C13, 0x00001DD8, 0x00002D8A, 0x000300F7, 0x000031D3, 0x00000002,
    0x000400FA, 0x00003C13, 0x000055E8, 0x000031D3, 0x000200F8, 0x000055E8,
    0x000200F9, 0x00004C7A, 0x000200F8, 0x000031D3, 0x00050051, 0x0000000B,
    0x00001CAC, 0x00002EF9, 0x00000001, 0x0007000C, 0x0000000B, 0x00001F38,
    0x00000001, 0x00000029, 0x00001CAC, 0x00000A0A, 0x00050050, 0x00000011,
    0x000051EF, 0x00001DD8, 0x00001F38, 0x00050080, 0x00000011, 0x0000522C,
    0x000051EF, 0x000059EB, 0x000500B2, 0x00000009, 0x00003ECB, 0x00003F4D,
    0x00000A13, 0x000300F7, 0x00005CE0, 0x00000000, 0x000400FA, 0x00003ECB,
    0x00002AEE, 0x00003AEF, 0x000200F8, 0x00003AEF, 0x000500AA, 0x00000009,
    0x000034FE, 0x00003F4D, 0x00000A19, 0x000600A9, 0x0000000B, 0x000020F6,
    0x000034FE, 0x00000A10, 0x00000A0A, 0x000200F9, 0x00005CE0, 0x000200F8,
    0x00002AEE, 0x000200F9, 0x00005CE0, 0x000200F8, 0x00005CE0, 0x000700F5,
    0x0000000B, 0x00004B64, 0x00003F4D, 0x00002AEE, 0x000020F6, 0x00003AEF,
    0x00050050, 0x00000011, 0x000041BE, 0x0000217E, 0x0000217E, 0x000500AE,
    0x0000000F, 0x00002E19, 0x000041BE, 0x0000072D, 0x000600A9, 0x00000011,
    0x00004BB5, 0x00002E19, 0x00000724, 0x0000070F, 0x000500C4, 0x00000011,
    0x00002AEA, 0x0000522C, 0x00004BB5, 0x00050050, 0x00000011, 0x0000605D,
    0x00004B64, 0x00004B64, 0x000500C2, 0x00000011, 0x00002385, 0x0000605D,
    0x00000718, 0x000500C7, 0x00000011, 0x00003EEE, 0x00002385, 0x00000724,
    0x00050080, 0x00000011, 0x00004573, 0x00002AEA, 0x00003EEE, 0x00050086,
    0x00000011, 0x00005ECE, 0x00004573, 0x00000937, 0x00050051, 0x0000000B,
    0x00003048, 0x00005ECE, 0x00000001, 0x00050084, 0x0000000B, 0x00002B26,
    0x00003048, 0x00005051, 0x00050051, 0x0000000B, 0x00006059, 0x00005ECE,
    0x00000000, 0x00050080, 0x0000000B, 0x00005420, 0x00002B26, 0x00006059,
    0x00050080, 0x0000000B, 0x00002226, 0x0000217F, 0x00005420, 0x00050084,
    0x00000011, 0x00005B31, 0x00005ECE, 0x00000937, 0x00050082, 0x00000011,
    0x00002E74, 0x00004573, 0x00005B31, 0x00050084, 0x0000000B, 0x0000233E,
    0x00002226, 0x00000184, 0x00050051, 0x0000000B, 0x00003887, 0x00002E74,
    0x00000001, 0x00050084, 0x0000000B, 0x00003E12, 0x00003887, 0x00000A82,
    0x00050051, 0x0000000B, 0x00001AE6, 0x00002E74, 0x00000000, 0x00050080,
    0x0000000B, 0x000025E0, 0x00003E12, 0x00001AE6, 0x000500C4, 0x0000000B,
    0x000046C4, 0x000025E0, 0x00000A0D, 0x00050080, 0x0000000B, 0x00004895,
    0x0000233E, 0x000046C4, 0x00050089, 0x0000000B, 0x0000459C, 0x00004895,
    0x0000086E, 0x000500C2, 0x0000000B, 0x00004894, 0x0000459C, 0x00000A10,
    0x00060041, 0x00000294, 0x00004316, 0x00000CC7, 0x00000A0B, 0x00004894,
    0x0004003D, 0x00000017, 0x00003141, 0x00004316, 0x00050080, 0x0000000B,
    0x00002DA7, 0x00004894, 0x00000A0D, 0x00060041, 0x00000294, 0x00001C1D,
    0x00000CC7, 0x00000A0B, 0x00002DA7, 0x0004003D, 0x00000017, 0x000047B8,
    0x00001C1D, 0x000500AA, 0x00000009, 0x00003C70, 0x00001DD8, 0x00000A0A,
    0x000600A9, 0x00000009, 0x00003CF3, 0x00003C70, 0x00000787, 0x00003C70,
    0x000300F7, 0x00005596, 0x00000002, 0x000400FA, 0x00003CF3, 0x00002620,
    0x00005596, 0x000200F8, 0x00002620, 0x00050051, 0x0000000B, 0x0000438A,
    0x00003141, 0x00000002, 0x00060052, 0x00000017, 0x000052B6, 0x0000438A,
    0x00003141, 0x00000000, 0x00050051, 0x0000000B, 0x00005A04, 0x00003141,
    0x00000003, 0x00060052, 0x00000017, 0x00002450, 0x00005A04, 0x000052B6,
    0x00000001, 0x000200F9, 0x00005596, 0x000200F8, 0x00005596, 0x000700F5,
    0x00000017, 0x00002AAC, 0x00003141, 0x00005CE0, 0x00002450, 0x00002620,
    0x000300F7, 0x0000530F, 0x00000002, 0x000400FA, 0x00004C75, 0x00003ADC,
    0x0000530F, 0x000200F8, 0x00003ADC, 0x000500AA, 0x00000009, 0x00003585,
    0x000023AA, 0x00000A19, 0x000400A8, 0x00000009, 0x00004277, 0x00003585,
    0x000300F7, 0x00003D52, 0x00000000, 0x000400FA, 0x00004277, 0x000040DF,
    0x00003D52, 0x000200F8, 0x000040DF, 0x000500AA, 0x00000009, 0x0000495A,
    0x000023AA, 0x00000A1F, 0x000200F9, 0x00003D52, 0x000200F8, 0x00003D52,
    0x000700F5, 0x00000009, 0x00002AAD, 0x00003585, 0x00003ADC, 0x0000495A,
    0x000040DF, 0x000300F7, 0x000039F4, 0x00000002, 0x000400FA, 0x00002AAD,
    0x000020A8, 0x000039F4, 0x000200F8, 0x000020A8, 0x000500C7, 0x00000017,
    0x00004BF1, 0x00002AAC, 0x00000755, 0x0009004F, 0x00000017, 0x000051EA,
    0x00002AAC, 0x00002AAC, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
    0x000500C7, 0x00000017, 0x00001CED, 0x000051EA, 0x00000352, 0x000500C5,
    0x00000017, 0x00003640, 0x00004BF1, 0x00001CED, 0x000500C7, 0x00000017,
    0x00005311, 0x000047B8, 0x00000755, 0x0009004F, 0x00000017, 0x00003BF8,
    0x000047B8, 0x000047B8, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
    0x000500C7, 0x00000017, 0x000020A3, 0x00003BF8, 0x00000352, 0x000500C5,
    0x00000017, 0x00002101, 0x00005311, 0x000020A3, 0x000200F9, 0x000039F4,
    0x000200F8, 0x000039F4, 0x000700F5, 0x00000017, 0x00002BF3, 0x000047B8,
    0x00003D52, 0x00002101, 0x000020A8, 0x000700F5, 0x00000017, 0x0000358D,
    0x00002AAC, 0x00003D52, 0x00003640, 0x000020A8, 0x000200F9, 0x0000530F,
    0x000200F8, 0x0000530F, 0x000700F5, 0x00000017, 0x000022F8, 0x000047B8,
    0x00005596, 0x00002BF3, 0x000039F4, 0x000700F5, 0x00000017, 0x000055F9,
    0x00002AAC, 0x00005596, 0x0000358D, 0x000039F4, 0x00050080, 0x00000011,
    0x00001C97, 0x00002EF9, 0x000059EC, 0x000300F7, 0x000052F5, 0x00000002,
    0x000400FA, 0x0000500F, 0x0000294E, 0x0000537D, 0x000200F8, 0x0000537D,
    0x0004007C, 0x00000012, 0x00002970, 0x00001C97, 0x00050051, 0x0000000C,
    0x000042C2, 0x00002970, 0x00000000, 0x000500C3, 0x0000000C, 0x000024FD,
    0x000042C2, 0x00000A1A, 0x00050051, 0x0000000C, 0x00002747, 0x00002970,
    0x00000001, 0x000500C3, 0x0000000C, 0x0000405C, 0x00002747, 0x00000A1A,
    0x000500C2, 0x0000000B, 0x00005B4D, 0x00003DA7, 0x00000A19, 0x0004007C,
    0x0000000C, 0x000018AA, 0x00005B4D, 0x00050084, 0x0000000C, 0x00005347,
    0x0000405C, 0x000018AA, 0x00050080, 0x0000000C, 0x00003F5E, 0x000024FD,
    0x00005347, 0x000500C4, 0x0000000C, 0x00004A8E, 0x00003F5E, 0x00000A28,
    0x000500C7, 0x0000000C, 0x00002AB6, 0x000042C2, 0x00000A20, 0x000500C7,
    0x0000000C, 0x00003138, 0x00002747, 0x00000A35, 0x000500C4, 0x0000000C,
    0x0000454D, 0x00003138, 0x00000A11, 0x00050080, 0x0000000C, 0x00004397,
    0x00002AB6, 0x0000454D, 0x000500C4, 0x0000000C, 0x000018E7, 0x00004397,
    0x00000A13, 0x000500C7, 0x0000000C, 0x000027B1, 0x000018E7, 0x000009DB,
    0x000500C4, 0x0000000C, 0x00002F76, 0x000027B1, 0x00000A0E, 0x00050080,
    0x0000000C, 0x00003C4B, 0x00004A8E, 0x00002F76, 0x000500C7, 0x0000000C,
    0x00003397, 0x000018E7, 0x00000A38, 0x00050080, 0x0000000C, 0x00004D30,
    0x00003C4B, 0x00003397, 0x000500C7, 0x0000000C, 0x000047B4, 0x00002747,
    0x00000A0E, 0x000500C4, 0x0000000C, 0x0000544A, 0x000047B4, 0x00000A17,
    0x00050080, 0x0000000C, 0x00004157, 0x00004D30, 0x0000544A, 0x000500C7,
    0x0000000C, 0x00005022, 0x00004157, 0x0000040B, 0x000500C4, 0x0000000C,
    0x00002416, 0x00005022, 0x00000A14, 0x000500C7, 0x0000000C, 0x00004A33,
    0x00002747, 0x00000A3B, 0x000500C4, 0x0000000C, 0x00002F77, 0x00004A33,
    0x00000A20, 0x00050080, 0x0000000C, 0x00004158, 0x00002416, 0x00002F77,
    0x000500C7, 0x0000000C, 0x00004ADD, 0x00004157, 0x00000388, 0x000500C4,
    0x0000000C, 0x0000544B, 0x00004ADD, 0x00000A11, 0x00050080, 0x0000000C,
    0x00004144, 0x00004158, 0x0000544B, 0x000500C7, 0x0000000C, 0x00005083,
    0x00002747, 0x00000A23, 0x000500C3, 0x0000000C, 0x000041BF, 0x00005083,
    0x00000A11, 0x000500C3, 0x0000000C, 0x00001EEC, 0x000042C2, 0x00000A14,
    0x00050080, 0x0000000C, 0x000035B6, 0x000041BF, 0x00001EEC, 0x000500C7,
    0x0000000C, 0x00005453, 0x000035B6, 0x00000A14, 0x000500C4, 0x0000000C,
    0x0000544C, 0x00005453, 0x00000A1D, 0x00050080, 0x0000000C, 0x00003C4C,
    0x00004144, 0x0000544C, 0x000500C7, 0x0000000C, 0x00002E06, 0x00004157,
    0x00000AC8, 0x00050080, 0x0000000C, 0x0000394F, 0x00003C4C, 0x00002E06,
    0x0004007C, 0x0000000B, 0x0000566F, 0x0000394F, 0x000200F9, 0x000052F5,
    0x000200F8, 0x0000294E, 0x00050051, 0x0000000B, 0x00004D9A, 0x00001C97,
    0x00000000, 0x00050051, 0x0000000B, 0x00002C03, 0x00001C97, 0x00000001,
    0x00060050, 0x00000014, 0x000020DE, 0x00004D9A, 0x00002C03, 0x000059FD,
    0x0004007C, 0x00000016, 0x00004E9D, 0x000020DE, 0x00050051, 0x0000000C,
    0x000028C6, 0x00004E9D, 0x00000001, 0x000500C3, 0x0000000C, 0x000024FE,
    0x000028C6, 0x00000A17, 0x00050051, 0x0000000C, 0x00002748, 0x00004E9D,
    0x00000002, 0x000500C3, 0x0000000C, 0x0000405D, 0x00002748, 0x00000A11,
    0x000500C2, 0x0000000B, 0x00005B4E, 0x00006273, 0x00000A16, 0x0004007C,
    0x0000000C, 0x000018AB, 0x00005B4E, 0x00050084, 0x0000000C, 0x00005321,
    0x0000405D, 0x000018AB, 0x00050080, 0x0000000C, 0x00003B27, 0x000024FE,
    0x00005321, 0x000500C2, 0x0000000B, 0x00002348, 0x00003DA7, 0x00000A19,
    0x0004007C, 0x0000000C, 0x0000308B, 0x00002348, 0x00050084, 0x0000000C,
    0x00002878, 0x00003B27, 0x0000308B, 0x00050051, 0x0000000C, 0x00006242,
    0x00004E9D, 0x00000000, 0x000500C3, 0x0000000C, 0x00004FC7, 0x00006242,
    0x00000A1A, 0x00050080, 0x0000000C, 0x000049FC, 0x00004FC7, 0x00002878,
    0x000500C4, 0x0000000C, 0x0000225D, 0x000049FC, 0x00000A25, 0x000500C7,
    0x0000000C, 0x00002CF6, 0x0000225D, 0x0000078B, 0x000500C4, 0x0000000C,
    0x000049FA, 0x00002CF6, 0x00000A0E, 0x000500C7, 0x0000000C, 0x00004D38,
    0x00006242, 0x00000A20, 0x000500C7, 0x0000000C, 0x00003139, 0x000028C6,
    0x00000A1D, 0x000500C4, 0x0000000C, 0x0000454E, 0x00003139, 0x00000A11,
    0x00050080, 0x0000000C, 0x0000434B, 0x00004D38, 0x0000454E, 0x000500C4,
    0x0000000C, 0x00001B88, 0x0000434B, 0x00000A25, 0x000500C3, 0x0000000C,
    0x00005DE3, 0x00001B88, 0x00000A1D, 0x000500C3, 0x0000000C, 0x00002215,
    0x000028C6, 0x00000A14, 0x00050080, 0x0000000C, 0x000035A3, 0x00002215,
    0x0000405D, 0x000500C7, 0x0000000C, 0x00005A0C, 0x000035A3, 0x00000A0E,
    0x000500C3, 0x0000000C, 0x00004112, 0x00006242, 0x00000A14, 0x000500C4,
    0x0000000C, 0x0000496A, 0x00005A0C, 0x00000A0E, 0x00050080, 0x0000000C,
    0x000034BD, 0x00004112, 0x0000496A, 0x000500C7, 0x0000000C, 0x00004ADE,
    0x000034BD, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544D, 0x00004ADE,
    0x00000A0E, 0x00050080, 0x0000000C, 0x00003C4D, 0x00005A0C, 0x0000544D,
    0x000500C7, 0x0000000C, 0x0000335E, 0x00005DE3, 0x000009DB, 0x00050080,
    0x0000000C, 0x00004F70, 0x000049FA, 0x0000335E, 0x000500C4, 0x0000000C,
    0x00005B32, 0x00004F70, 0x00000A0E, 0x000500C7, 0x0000000C, 0x00005AEA,
    0x00005DE3, 0x00000A38, 0x00050080, 0x0000000C, 0x0000285C, 0x00005B32,
    0x00005AEA, 0x000500C7, 0x0000000C, 0x000047B5, 0x00002748, 0x00000A14,
    0x000500C4, 0x0000000C, 0x0000544E, 0x000047B5, 0x00000A25, 0x00050080,
    0x0000000C, 0x00004159, 0x0000285C, 0x0000544E, 0x000500C7, 0x0000000C,
    0x00004ADF, 0x000028C6, 0x00000A0E, 0x000500C4, 0x0000000C, 0x0000544F,
    0x00004ADF, 0x00000A17, 0x00050080, 0x0000000C, 0x0000415A, 0x00004159,
    0x0000544F, 0x000500C7, 0x0000000C, 0x00004FD6, 0x00003C4D, 0x00000A0E,
    0x000500C4, 0x0000000C, 0x00002703, 0x00004FD6, 0x00000A14, 0x000500C3,
    0x0000000C, 0x00003332, 0x0000415A, 0x00000A1D, 0x000500C7, 0x0000000C,
    0x000036D6, 0x00003332, 0x00000A20, 0x00050080, 0x0000000C, 0x00003412,
    0x00002703, 0x000036D6, 0x000500C4, 0x0000000C, 0x00005B33, 0x00003412,
    0x00000A14, 0x000500C7, 0x0000000C, 0x00005AB1, 0x00003C4D, 0x00000A05,
    0x00050080, 0x0000000C, 0x00002A9C, 0x00005B33, 0x00005AB1, 0x000500C4,
    0x0000000C, 0x00005B34, 0x00002A9C, 0x00000A11, 0x000500C7, 0x0000000C,
    0x00005AB2, 0x0000415A, 0x0000040B, 0x00050080, 0x0000000C, 0x00002A9D,
    0x00005B34, 0x00005AB2, 0x000500C4, 0x0000000C, 0x00005B35, 0x00002A9D,
    0x00000A14, 0x000500C7, 0x0000000C, 0x00005559, 0x0000415A, 0x00000AC8,
    0x00050080, 0x0000000C, 0x00005EFA, 0x00005B35, 0x00005559, 0x0004007C,
    0x0000000B, 0x00005670, 0x00005EFA, 0x000200F9, 0x000052F5, 0x000200F8,
    0x000052F5, 0x000700F5, 0x0000000B, 0x00002C70, 0x00005670, 0x0000294E,
    0x0000566F, 0x0000537D, 0x00050080, 0x0000000B, 0x00005088, 0x00002C70,
    0x000062B6, 0x000500C2, 0x0000000B, 0x00004EAA, 0x00005088, 0x00000A16,
    0x000500AA, 0x00000009, 0x00004B9C, 0x00004ADC, 0x00000A16, 0x000300F7,
    0x000039BC, 0x00000000, 0x000400FA, 0x00004B9C, 0x000033DF, 0x000039BC,
    0x000200F8, 0x000033DF, 0x0009004F, 0x00000017, 0x00001F16, 0x000055F9,
    0x000055F9, 0x00000001, 0x00000000, 0x00000003, 0x00000002, 0x000200F9,
    0x000039BC, 0x000200F8, 0x000039BC, 0x000700F5, 0x00000017, 0x00005972,
    0x000055F9, 0x000052F5, 0x00001F16, 0x000033DF, 0x000600A9, 0x0000000B,
    0x000019CD, 0x00004B9C, 0x00000A10, 0x00004ADC, 0x000500AA, 0x00000009,
    0x00003464, 0x000019CD, 0x00000A0D, 0x000500AA, 0x00000009, 0x000047C2,
    0x000019CD, 0x00000A10, 0x000500A6, 0x00000009, 0x00005686, 0x00003464,
    0x000047C2, 0x000300F7, 0x00003463, 0x00000000, 0x000400FA, 0x00005686,
    0x00002957, 0x00003463, 0x000200F8, 0x00002957, 0x000500C7, 0x00000017,
    0x0000475F, 0x00005972, 0x000009CE, 0x000500C4, 0x00000017, 0x000024D1,
    0x0000475F, 0x0000013D, 0x000500C7, 0x00000017, 0x000050AC, 0x00005972,
    0x0000072E, 0x000500C2, 0x00000017, 0x0000448D, 0x000050AC, 0x0000013D,
    0x000500C5, 0x00000017, 0x00003FF8, 0x000024D1, 0x0000448D, 0x000200F9,
    0x00003463, 0x000200F8, 0x00003463, 0x000700F5, 0x00000017, 0x00005879,
    0x00005972, 0x000039BC, 0x00003FF8, 0x00002957, 0x000500AA, 0x00000009,
    0x00004CB6, 0x000019CD, 0x00000A13, 0x000500A6, 0x00000009, 0x00003B23,
    0x000047C2, 0x00004CB6, 0x000300F7, 0x00002C98, 0x00000000, 0x000400FA,
    0x00003B23, 0x00002B38, 0x00002C98, 0x000200F8, 0x00002B38, 0x000500C4,
    0x00000017, 0x00005E17, 0x00005879, 0x000002ED, 0x000500C2, 0x00000017,
    0x00003BE7, 0x00005879, 0x000002ED, 0x000500C5, 0x00000017, 0x000029E8,
    0x00005E17, 0x00003BE7, 0x000200F9, 0x00002C98, 0x000200F8, 0x00002C98,
    0x000700F5, 0x00000017, 0x00004D37, 0x00005879, 0x00003463, 0x000029E8,
    0x00002B38, 0x00060041, 0x00000294, 0x000060F9, 0x00001592, 0x00000A0B,
    0x00004EAA, 0x0003003E, 0x000060F9, 0x00004D37, 0x00050080, 0x0000000B,
    0x000054B5, 0x00004EAA, 0x00000A10, 0x000300F7, 0x00003F86, 0x00000000,
    0x000400FA, 0x00004B9C, 0x000033E0, 0x00003F86, 0x000200F8, 0x000033E0,
    0x0009004F, 0x00000017, 0x00001F17, 0x000022F8, 0x000022F8, 0x00000001,
    0x00000000, 0x00000003, 0x00000002, 0x000200F9, 0x00003F86, 0x000200F8,
    0x00003F86, 0x000700F5, 0x00000017, 0x00002AAE, 0x000022F8, 0x00002C98,
    0x00001F17, 0x000033E0, 0x000300F7, 0x00003A1A, 0x00000000, 0x000400FA,
    0x00005686, 0x00002958, 0x00003A1A, 0x000200F8, 0x00002958, 0x000500C7,
    0x00000017, 0x00004760, 0x00002AAE, 0x000009CE, 0x000500C4, 0x00000017,
    0x000024D2, 0x00004760, 0x0000013D, 0x000500C7, 0x00000017, 0x000050AD,
    0x00002AAE, 0x0000072E, 0x000500C2, 0x00000017, 0x0000448E, 0x000050AD,
    0x0000013D, 0x000500C5, 0x00000017, 0x00003FF9, 0x000024D2, 0x0000448E,
    0x000200F9, 0x00003A1A, 0x000200F8, 0x00003A1A, 0x000700F5, 0x00000017,
    0x00002AAF, 0x00002AAE, 0x00003F86, 0x00003FF9, 0x00002958, 0x000300F7,
    0x00002C99, 0x00000000, 0x000400FA, 0x00003B23, 0x00002B39, 0x00002C99,
    0x000200F8, 0x00002B39, 0x000500C4, 0x00000017, 0x00005E18, 0x00002AAF,
    0x000002ED, 0x000500C2, 0x00000017, 0x00003BE8, 0x00002AAF, 0x000002ED,
    0x000500C5, 0x00000017, 0x000029E9, 0x00005E18, 0x00003BE8, 0x000200F9,
    0x00002C99, 0x000200F8, 0x00002C99, 0x000700F5, 0x00000017, 0x00004D39,
    0x00002AAF, 0x00003A1A, 0x000029E9, 0x00002B39, 0x00060041, 0x00000294,
    0x00001F75, 0x00001592, 0x00000A0B, 0x000054B5, 0x0003003E, 0x00001F75,
    0x00004D39, 0x000200F9, 0x00004C7A, 0x000200F8, 0x00004C7A, 0x000100FD,
    0x00010038,
};
