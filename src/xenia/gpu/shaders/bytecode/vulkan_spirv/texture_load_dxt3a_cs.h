// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 25155
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main" %gl_GlobalInvocationID
               OpExecutionMode %main LocalSize 4 32 1
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %push_const_block_xe "push_const_block_xe"
               OpMemberName %push_const_block_xe 0 "xe_texture_load_is_tiled_3d_endian_scale"
               OpMemberName %push_const_block_xe 1 "xe_texture_load_guest_offset"
               OpMemberName %push_const_block_xe 2 "xe_texture_load_guest_pitch_aligned"
               OpMemberName %push_const_block_xe 3 "xe_texture_load_guest_z_stride_block_rows_aligned"
               OpMemberName %push_const_block_xe 4 "xe_texture_load_size_blocks"
               OpMemberName %push_const_block_xe 5 "xe_texture_load_host_offset"
               OpMemberName %push_const_block_xe 6 "xe_texture_load_host_pitch"
               OpMemberName %push_const_block_xe 7 "xe_texture_load_height_texels"
               OpName %push_consts_xe "push_consts_xe"
               OpName %gl_GlobalInvocationID "gl_GlobalInvocationID"
               OpName %xe_texture_load_source_xe_block "xe_texture_load_source_xe_block"
               OpMemberName %xe_texture_load_source_xe_block 0 "data"
               OpName %xe_texture_load_source "xe_texture_load_source"
               OpName %xe_texture_load_dest_xe_block "xe_texture_load_dest_xe_block"
               OpMemberName %xe_texture_load_dest_xe_block 0 "data"
               OpName %xe_texture_load_dest "xe_texture_load_dest"
               OpDecorate %push_const_block_xe Block
               OpMemberDecorate %push_const_block_xe 0 Offset 0
               OpMemberDecorate %push_const_block_xe 1 Offset 4
               OpMemberDecorate %push_const_block_xe 2 Offset 8
               OpMemberDecorate %push_const_block_xe 3 Offset 12
               OpMemberDecorate %push_const_block_xe 4 Offset 16
               OpMemberDecorate %push_const_block_xe 5 Offset 28
               OpMemberDecorate %push_const_block_xe 6 Offset 32
               OpMemberDecorate %push_const_block_xe 7 Offset 36
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpDecorate %xe_texture_load_source_xe_block BufferBlock
               OpMemberDecorate %xe_texture_load_source_xe_block 0 NonWritable
               OpMemberDecorate %xe_texture_load_source_xe_block 0 Offset 0
               OpDecorate %xe_texture_load_source NonWritable
               OpDecorate %xe_texture_load_source Binding 0
               OpDecorate %xe_texture_load_source DescriptorSet 1
               OpDecorate %_runtimearr_v4uint_0 ArrayStride 16
               OpDecorate %xe_texture_load_dest_xe_block BufferBlock
               OpMemberDecorate %xe_texture_load_dest_xe_block 0 NonReadable
               OpMemberDecorate %xe_texture_load_dest_xe_block 0 Offset 0
               OpDecorate %xe_texture_load_dest NonReadable
               OpDecorate %xe_texture_load_dest Binding 0
               OpDecorate %xe_texture_load_dest DescriptorSet 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v4uint = OpTypeVector %uint 4
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %v3int = OpTypeVector %int 3
       %bool = OpTypeBool
     %v2uint = OpTypeVector %uint 2
     %v3uint = OpTypeVector %uint 3
    %uint_15 = OpConstant %uint 15
   %uint_255 = OpConstant %uint 255
     %uint_4 = OpConstant %uint 4
  %uint_4080 = OpConstant %uint 4080
     %uint_8 = OpConstant %uint 8
 %uint_65280 = OpConstant %uint 65280
    %uint_12 = OpConstant %uint 12
 %uint_61440 = OpConstant %uint 61440
    %uint_16 = OpConstant %uint 16
     %uint_1 = OpConstant %uint 1
     %uint_2 = OpConstant %uint 2
%uint_16711935 = OpConstant %uint 16711935
%uint_4278255360 = OpConstant %uint 4278255360
     %uint_3 = OpConstant %uint 3
     %uint_0 = OpConstant %uint 0
      %int_5 = OpConstant %int 5
     %uint_5 = OpConstant %uint 5
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
%push_const_block_xe = OpTypeStruct %uint %uint %uint %uint %v3uint %uint %uint %uint
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
      %int_0 = OpConstant %int 0
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
%_ptr_PushConstant_v3uint = OpTypePointer PushConstant %v3uint
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
       %2596 = OpConstantComposite %v3uint %uint_2 %uint_0 %uint_0
     %v2bool = OpTypeVector %bool 2
       %2620 = OpConstantComposite %v3uint %uint_2 %uint_2 %uint_0
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_texture_load_source_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform_xe_texture_load_source_xe_block = OpTypePointer Uniform %xe_texture_load_source_xe_block
%xe_texture_load_source = OpVariable %_ptr_Uniform_xe_texture_load_source_xe_block Uniform
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
%_runtimearr_v4uint_0 = OpTypeRuntimeArray %v4uint
%xe_texture_load_dest_xe_block = OpTypeStruct %_runtimearr_v4uint_0
%_ptr_Uniform_xe_texture_load_dest_xe_block = OpTypePointer Uniform %xe_texture_load_dest_xe_block
%xe_texture_load_dest = OpVariable %_ptr_Uniform_xe_texture_load_dest_xe_block Uniform
    %uint_32 = OpConstant %uint 32
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_4 %uint_32 %uint_1
     %uint_9 = OpConstant %uint 9
    %uint_10 = OpConstant %uint 10
       %2510 = OpConstantComposite %v4uint %uint_16711935 %uint_16711935 %uint_16711935 %uint_16711935
        %317 = OpConstantComposite %v4uint %uint_8 %uint_8 %uint_8 %uint_8
       %1838 = OpConstantComposite %v4uint %uint_4278255360 %uint_4278255360 %uint_4278255360 %uint_4278255360
        %749 = OpConstantComposite %v4uint %uint_16 %uint_16 %uint_16 %uint_16
        %695 = OpConstantComposite %v4uint %uint_15 %uint_15 %uint_15 %uint_15
       %1611 = OpConstantComposite %v4uint %uint_255 %uint_255 %uint_255 %uint_255
        %101 = OpConstantComposite %v4uint %uint_4 %uint_4 %uint_4 %uint_4
        %402 = OpConstantComposite %v4uint %uint_4080 %uint_4080 %uint_4080 %uint_4080
       %2135 = OpConstantComposite %v4uint %uint_65280 %uint_65280 %uint_65280 %uint_65280
        %533 = OpConstantComposite %v4uint %uint_12 %uint_12 %uint_12 %uint_12
       %2534 = OpConstantComposite %v4uint %uint_61440 %uint_61440 %uint_61440 %uint_61440
       %main = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %14903 None
               OpSwitch %uint_0 %11880
      %11880 = OpLabel
      %24791 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_0
      %13606 = OpLoad %uint %24791
      %24540 = OpBitwiseAnd %uint %13606 %uint_1
      %17270 = OpINotEqual %bool %24540 %uint_0
      %12328 = OpBitwiseAnd %uint %13606 %uint_2
      %17284 = OpINotEqual %bool %12328 %uint_0
       %7856 = OpShiftRightLogical %uint %13606 %uint_2
      %25058 = OpBitwiseAnd %uint %7856 %uint_3
      %18732 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
      %24236 = OpLoad %uint %18732
      %20154 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
      %22408 = OpLoad %uint %20154
      %20155 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_3
      %22409 = OpLoad %uint %20155
      %20156 = OpAccessChain %_ptr_PushConstant_v3uint %push_consts_xe %int_4
      %22410 = OpLoad %v3uint %20156
      %20157 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_5
      %22411 = OpLoad %uint %20157
      %20158 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_6
      %22412 = OpLoad %uint %20158
      %20078 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_7
       %6594 = OpLoad %uint %20078
      %10766 = OpLoad %v3uint %gl_GlobalInvocationID
      %21387 = OpShiftLeftLogical %v3uint %10766 %2596
      %17136 = OpVectorShuffle %v2uint %21387 %21387 0 1
       %9263 = OpVectorShuffle %v2uint %22410 %22410 0 1
      %17032 = OpUGreaterThanEqual %v2bool %17136 %9263
      %24679 = OpAny %bool %17032
               OpSelectionMerge %14018 DontFlatten
               OpBranchConditional %24679 %21992 %14018
      %21992 = OpLabel
               OpBranch %14903
      %14018 = OpLabel
      %17344 = OpShiftLeftLogical %v3uint %21387 %2620
      %14520 = OpBitcast %v3int %17344
       %8905 = OpCompositeExtract %int %14520 0
       %6813 = OpCompositeExtract %int %14520 2
      %21501 = OpBitcast %int %6594
      %11279 = OpIMul %int %6813 %21501
      %17598 = OpCompositeExtract %int %14520 1
      %22228 = OpIAdd %int %11279 %17598
      %22405 = OpBitcast %int %22412
      %24535 = OpIMul %int %22228 %22405
       %8258 = OpIAdd %int %8905 %24535
      %10898 = OpBitcast %uint %8258
       %9077 = OpIAdd %uint %10898 %22411
      %11726 = OpShiftRightLogical %uint %9077 %uint_4
       %6977 = OpShiftRightLogical %uint %22412 %uint_4
               OpSelectionMerge %24387 DontFlatten
               OpBranchConditional %17270 %22376 %20009
      %22376 = OpLabel
               OpSelectionMerge %7691 DontFlatten
               OpBranchConditional %17284 %21373 %6361
      %21373 = OpLabel
      %10608 = OpBitcast %v3int %21387
      %17090 = OpCompositeExtract %int %10608 1
       %9469 = OpShiftRightArithmetic %int %17090 %int_4
      %10055 = OpCompositeExtract %int %10608 2
      %16476 = OpShiftRightArithmetic %int %10055 %int_2
      %23373 = OpShiftRightLogical %uint %22409 %uint_4
       %6314 = OpBitcast %int %23373
      %21281 = OpIMul %int %16476 %6314
      %15143 = OpIAdd %int %9469 %21281
       %9032 = OpShiftRightLogical %uint %22408 %uint_5
      %12427 = OpBitcast %int %9032
      %10360 = OpIMul %int %15143 %12427
      %25154 = OpCompositeExtract %int %10608 0
      %20423 = OpShiftRightArithmetic %int %25154 %int_5
      %18940 = OpIAdd %int %20423 %10360
       %8797 = OpShiftLeftLogical %int %18940 %uint_9
      %11510 = OpBitwiseAnd %int %8797 %int_268435455
      %18938 = OpShiftLeftLogical %int %11510 %int_1
      %19768 = OpBitwiseAnd %int %25154 %int_7
      %12600 = OpBitwiseAnd %int %17090 %int_6
      %17741 = OpShiftLeftLogical %int %12600 %int_2
      %17227 = OpIAdd %int %19768 %17741
       %7048 = OpShiftLeftLogical %int %17227 %uint_9
      %24035 = OpShiftRightArithmetic %int %7048 %int_6
       %8725 = OpShiftRightArithmetic %int %17090 %int_3
      %13731 = OpIAdd %int %8725 %16476
      %23052 = OpBitwiseAnd %int %13731 %int_1
      %16658 = OpShiftRightArithmetic %int %25154 %int_3
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
      %18356 = OpBitwiseAnd %int %10055 %int_3
      %21579 = OpShiftLeftLogical %int %18356 %uint_9
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
      %24224 = OpBitwiseAnd %int %16728 %int_63
      %21741 = OpIAdd %int %23348 %24224
               OpBranch %7691
       %6361 = OpLabel
       %6573 = OpBitcast %v2int %17136
      %17091 = OpCompositeExtract %int %6573 0
       %9470 = OpShiftRightArithmetic %int %17091 %int_5
      %10056 = OpCompositeExtract %int %6573 1
      %16477 = OpShiftRightArithmetic %int %10056 %int_5
      %23374 = OpShiftRightLogical %uint %22408 %uint_5
       %6315 = OpBitcast %int %23374
      %21319 = OpIMul %int %16477 %6315
      %16222 = OpIAdd %int %9470 %21319
      %19086 = OpShiftLeftLogical %int %16222 %uint_10
      %10934 = OpBitwiseAnd %int %17091 %int_7
      %12601 = OpBitwiseAnd %int %10056 %int_14
      %17742 = OpShiftLeftLogical %int %12601 %int_2
      %17303 = OpIAdd %int %10934 %17742
       %6375 = OpShiftLeftLogical %int %17303 %uint_3
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
       %7916 = OpShiftRightArithmetic %int %17091 %int_3
      %13750 = OpIAdd %int %16831 %7916
      %21587 = OpBitwiseAnd %int %13750 %int_3
      %21583 = OpShiftLeftLogical %int %21587 %int_6
      %15437 = OpIAdd %int %16708 %21583
      %14157 = OpBitwiseAnd %int %16729 %int_63
      %12098 = OpIAdd %int %15437 %14157
               OpBranch %7691
       %7691 = OpLabel
      %10540 = OpPhi %int %21741 %21373 %12098 %6361
               OpBranch %24387
      %20009 = OpLabel
      %24447 = OpBitcast %v3int %21387
       %8918 = OpCompositeExtract %int %24447 0
       %9362 = OpIMul %int %8918 %int_8
       %6362 = OpCompositeExtract %int %24447 2
      %14505 = OpBitcast %int %22409
      %11280 = OpIMul %int %6362 %14505
      %17599 = OpCompositeExtract %int %24447 1
      %22229 = OpIAdd %int %11280 %17599
      %22406 = OpBitcast %int %22408
       %7839 = OpIMul %int %22229 %22406
       %7984 = OpIAdd %int %9362 %7839
               OpBranch %24387
      %24387 = OpLabel
      %10814 = OpPhi %int %10540 %7691 %7984 %20009
       %6719 = OpBitcast %int %24236
      %22221 = OpIAdd %int %6719 %10814
      %16105 = OpBitcast %uint %22221
      %22117 = OpShiftRightLogical %uint %16105 %uint_4
      %17173 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_source %int_0 %22117
       %7338 = OpLoad %v4uint %17173
      %13760 = OpIEqual %bool %25058 %uint_1
      %21366 = OpIEqual %bool %25058 %uint_2
      %22150 = OpLogicalOr %bool %13760 %21366
               OpSelectionMerge %13411 None
               OpBranchConditional %22150 %10583 %13411
      %10583 = OpLabel
      %18271 = OpBitwiseAnd %v4uint %7338 %2510
       %9425 = OpShiftLeftLogical %v4uint %18271 %317
      %20652 = OpBitwiseAnd %v4uint %7338 %1838
      %17549 = OpShiftRightLogical %v4uint %20652 %317
      %16376 = OpBitwiseOr %v4uint %9425 %17549
               OpBranch %13411
      %13411 = OpLabel
      %22649 = OpPhi %v4uint %7338 %24387 %16376 %10583
      %19638 = OpIEqual %bool %25058 %uint_3
      %15139 = OpLogicalOr %bool %21366 %19638
               OpSelectionMerge %13392 None
               OpBranchConditional %15139 %11064 %13392
      %11064 = OpLabel
      %24087 = OpShiftLeftLogical %v4uint %22649 %749
      %15335 = OpShiftRightLogical %v4uint %22649 %749
      %10728 = OpBitwiseOr %v4uint %24087 %15335
               OpBranch %13392
      %13392 = OpLabel
      %22100 = OpPhi %v4uint %22649 %13411 %10728 %11064
      %11876 = OpSelect %uint %17270 %uint_2 %uint_1
      %11339 = OpIAdd %uint %22117 %11876
      %18278 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_source %int_0 %11339
       %6578 = OpLoad %v4uint %18278
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
      %10924 = OpPhi %v4uint %6578 %13392 %16377 %10584
               OpSelectionMerge %11720 None
               OpBranchConditional %15139 %11065 %11720
      %11065 = OpLabel
      %24088 = OpShiftLeftLogical %v4uint %10924 %749
      %15336 = OpShiftRightLogical %v4uint %10924 %749
      %10729 = OpBitwiseOr %v4uint %24088 %15336
               OpBranch %11720
      %11720 = OpLabel
      %17360 = OpPhi %v4uint %10924 %14874 %10729 %11065
      %23054 = OpCompositeExtract %uint %22100 0
      %11086 = OpCompositeExtract %uint %22100 2
       %7641 = OpCompositeExtract %uint %17360 0
      %10075 = OpCompositeExtract %uint %17360 2
      %16752 = OpCompositeConstruct %v4uint %23054 %11086 %7641 %10075
       %8141 = OpBitwiseAnd %v4uint %16752 %695
      %16912 = OpBitwiseAnd %v4uint %16752 %1611
      %24932 = OpShiftLeftLogical %v4uint %16912 %101
      %17083 = OpBitwiseOr %v4uint %8141 %24932
      %20866 = OpBitwiseAnd %v4uint %16752 %402
      %23319 = OpShiftLeftLogical %v4uint %20866 %317
      %17084 = OpBitwiseOr %v4uint %17083 %23319
      %20867 = OpBitwiseAnd %v4uint %16752 %2135
      %23320 = OpShiftLeftLogical %v4uint %20867 %533
      %17085 = OpBitwiseOr %v4uint %17084 %23320
      %20868 = OpBitwiseAnd %v4uint %16752 %2534
      %20773 = OpShiftLeftLogical %v4uint %20868 %749
      %18648 = OpBitwiseOr %v4uint %17085 %20773
      %20974 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_dest %int_0 %11726
               OpStore %20974 %18648
      %12832 = OpCompositeExtract %uint %17344 1
      %23232 = OpIAdd %uint %12832 %int_1
      %17425 = OpULessThan %bool %23232 %6594
               OpSelectionMerge %7206 DontFlatten
               OpBranchConditional %17425 %22828 %7206
      %22828 = OpLabel
      %15576 = OpIAdd %uint %11726 %6977
      %13400 = OpShiftRightLogical %v4uint %16752 %749
      %11432 = OpBitwiseAnd %v4uint %13400 %695
      %18755 = OpBitwiseAnd %v4uint %13400 %1611
      %24933 = OpShiftLeftLogical %v4uint %18755 %101
      %17086 = OpBitwiseOr %v4uint %11432 %24933
      %20869 = OpBitwiseAnd %v4uint %13400 %402
      %23321 = OpShiftLeftLogical %v4uint %20869 %317
      %17087 = OpBitwiseOr %v4uint %17086 %23321
      %20870 = OpBitwiseAnd %v4uint %13400 %2135
      %23322 = OpShiftLeftLogical %v4uint %20870 %533
      %17088 = OpBitwiseOr %v4uint %17087 %23322
      %20871 = OpBitwiseAnd %v4uint %13400 %2534
      %20774 = OpShiftLeftLogical %v4uint %20871 %749
      %18649 = OpBitwiseOr %v4uint %17088 %20774
      %21867 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_dest %int_0 %15576
               OpStore %21867 %18649
      %14840 = OpIAdd %uint %12832 %int_2
      %11787 = OpULessThan %bool %14840 %6594
               OpSelectionMerge %7205 DontFlatten
               OpBranchConditional %11787 %20681 %7205
      %20681 = OpLabel
      %13812 = OpIAdd %uint %15576 %6977
      %10288 = OpCompositeExtract %uint %22100 1
      %10052 = OpCompositeExtract %uint %22100 3
       %7642 = OpCompositeExtract %uint %17360 1
      %10076 = OpCompositeExtract %uint %17360 3
      %16753 = OpCompositeConstruct %v4uint %10288 %10052 %7642 %10076
       %8142 = OpBitwiseAnd %v4uint %16753 %695
      %16913 = OpBitwiseAnd %v4uint %16753 %1611
      %24934 = OpShiftLeftLogical %v4uint %16913 %101
      %17089 = OpBitwiseOr %v4uint %8142 %24934
      %20872 = OpBitwiseAnd %v4uint %16753 %402
      %23323 = OpShiftLeftLogical %v4uint %20872 %317
      %17092 = OpBitwiseOr %v4uint %17089 %23323
      %20873 = OpBitwiseAnd %v4uint %16753 %2135
      %23324 = OpShiftLeftLogical %v4uint %20873 %533
      %17093 = OpBitwiseOr %v4uint %17092 %23324
      %20874 = OpBitwiseAnd %v4uint %16753 %2534
      %20775 = OpShiftLeftLogical %v4uint %20874 %749
      %18650 = OpBitwiseOr %v4uint %17093 %20775
      %21868 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_dest %int_0 %13812
               OpStore %21868 %18650
      %14841 = OpIAdd %uint %12832 %int_3
      %11788 = OpULessThan %bool %14841 %6594
               OpSelectionMerge %18021 DontFlatten
               OpBranchConditional %11788 %22829 %18021
      %22829 = OpLabel
      %15577 = OpIAdd %uint %13812 %6977
      %13401 = OpShiftRightLogical %v4uint %16753 %749
      %11433 = OpBitwiseAnd %v4uint %13401 %695
      %18756 = OpBitwiseAnd %v4uint %13401 %1611
      %24935 = OpShiftLeftLogical %v4uint %18756 %101
      %17094 = OpBitwiseOr %v4uint %11433 %24935
      %20875 = OpBitwiseAnd %v4uint %13401 %402
      %23325 = OpShiftLeftLogical %v4uint %20875 %317
      %17095 = OpBitwiseOr %v4uint %17094 %23325
      %20876 = OpBitwiseAnd %v4uint %13401 %2135
      %23326 = OpShiftLeftLogical %v4uint %20876 %533
      %17096 = OpBitwiseOr %v4uint %17095 %23326
      %20877 = OpBitwiseAnd %v4uint %13401 %2534
      %20776 = OpShiftLeftLogical %v4uint %20877 %749
      %18651 = OpBitwiseOr %v4uint %17096 %20776
      %24166 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_dest %int_0 %15577
               OpStore %24166 %18651
               OpBranch %18021
      %18021 = OpLabel
               OpBranch %7205
       %7205 = OpLabel
               OpBranch %7206
       %7206 = OpLabel
               OpBranch %14903
      %14903 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t texture_load_dxt3a_cs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006243, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F48, 0x00060010, 0x0000161F,
    0x00000011, 0x00000004, 0x00000020, 0x00000001, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x0000161F, 0x6E69616D,
    0x00000000, 0x00070005, 0x00000489, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x000E0006, 0x00000489, 0x00000000, 0x745F6578,
    0x75747865, 0x6C5F6572, 0x5F64616F, 0x745F7369, 0x64656C69, 0x5F64335F,
    0x69646E65, 0x735F6E61, 0x656C6163, 0x00000000, 0x000B0006, 0x00000489,
    0x00000001, 0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F, 0x73657567,
    0x666F5F74, 0x74657366, 0x00000000, 0x000C0006, 0x00000489, 0x00000002,
    0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F, 0x73657567, 0x69705F74,
    0x5F686374, 0x67696C61, 0x0064656E, 0x00100006, 0x00000489, 0x00000003,
    0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F, 0x73657567, 0x5F7A5F74,
    0x69727473, 0x625F6564, 0x6B636F6C, 0x776F725F, 0x6C615F73, 0x656E6769,
    0x00000064, 0x000A0006, 0x00000489, 0x00000004, 0x745F6578, 0x75747865,
    0x6C5F6572, 0x5F64616F, 0x657A6973, 0x6F6C625F, 0x00736B63, 0x000A0006,
    0x00000489, 0x00000005, 0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F,
    0x74736F68, 0x66666F5F, 0x00746573, 0x000A0006, 0x00000489, 0x00000006,
    0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F, 0x74736F68, 0x7469705F,
    0x00006863, 0x000B0006, 0x00000489, 0x00000007, 0x745F6578, 0x75747865,
    0x6C5F6572, 0x5F64616F, 0x67696568, 0x745F7468, 0x6C657865, 0x00000073,
    0x00060005, 0x00000CE9, 0x68737570, 0x6E6F635F, 0x5F737473, 0x00006578,
    0x00080005, 0x00000F48, 0x475F6C67, 0x61626F6C, 0x766E496C, 0x7461636F,
    0x496E6F69, 0x00000044, 0x000A0005, 0x000007B4, 0x745F6578, 0x75747865,
    0x6C5F6572, 0x5F64616F, 0x72756F73, 0x785F6563, 0x6C625F65, 0x006B636F,
    0x00050006, 0x000007B4, 0x00000000, 0x61746164, 0x00000000, 0x00080005,
    0x0000107A, 0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F, 0x72756F73,
    0x00006563, 0x000A0005, 0x000007B5, 0x745F6578, 0x75747865, 0x6C5F6572,
    0x5F64616F, 0x74736564, 0x5F65785F, 0x636F6C62, 0x0000006B, 0x00050006,
    0x000007B5, 0x00000000, 0x61746164, 0x00000000, 0x00080005, 0x0000140E,
    0x745F6578, 0x75747865, 0x6C5F6572, 0x5F64616F, 0x74736564, 0x00000000,
    0x00030047, 0x00000489, 0x00000002, 0x00050048, 0x00000489, 0x00000000,
    0x00000023, 0x00000000, 0x00050048, 0x00000489, 0x00000001, 0x00000023,
    0x00000004, 0x00050048, 0x00000489, 0x00000002, 0x00000023, 0x00000008,
    0x00050048, 0x00000489, 0x00000003, 0x00000023, 0x0000000C, 0x00050048,
    0x00000489, 0x00000004, 0x00000023, 0x00000010, 0x00050048, 0x00000489,
    0x00000005, 0x00000023, 0x0000001C, 0x00050048, 0x00000489, 0x00000006,
    0x00000023, 0x00000020, 0x00050048, 0x00000489, 0x00000007, 0x00000023,
    0x00000024, 0x00040047, 0x00000F48, 0x0000000B, 0x0000001C, 0x00040047,
    0x000007DC, 0x00000006, 0x00000010, 0x00030047, 0x000007B4, 0x00000003,
    0x00040048, 0x000007B4, 0x00000000, 0x00000018, 0x00050048, 0x000007B4,
    0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x0000107A, 0x00000018,
    0x00040047, 0x0000107A, 0x00000021, 0x00000000, 0x00040047, 0x0000107A,
    0x00000022, 0x00000001, 0x00040047, 0x000007DD, 0x00000006, 0x00000010,
    0x00030047, 0x000007B5, 0x00000003, 0x00040048, 0x000007B5, 0x00000000,
    0x00000019, 0x00050048, 0x000007B5, 0x00000000, 0x00000023, 0x00000000,
    0x00030047, 0x0000140E, 0x00000019, 0x00040047, 0x0000140E, 0x00000021,
    0x00000000, 0x00040047, 0x0000140E, 0x00000022, 0x00000000, 0x00040047,
    0x00000BC3, 0x0000000B, 0x00000019, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x00040017, 0x00000017, 0x0000000B, 0x00000004, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x00040017, 0x00000012, 0x0000000C, 0x00000002,
    0x00040017, 0x00000016, 0x0000000C, 0x00000003, 0x00020014, 0x00000009,
    0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00040017, 0x00000014,
    0x0000000B, 0x00000003, 0x0004002B, 0x0000000B, 0x00000A37, 0x0000000F,
    0x0004002B, 0x0000000B, 0x00000144, 0x000000FF, 0x0004002B, 0x0000000B,
    0x00000A16, 0x00000004, 0x0004002B, 0x0000000B, 0x00000ACE, 0x00000FF0,
    0x0004002B, 0x0000000B, 0x00000A22, 0x00000008, 0x0004002B, 0x0000000B,
    0x00000A87, 0x0000FF00, 0x0004002B, 0x0000000B, 0x00000A2E, 0x0000000C,
    0x0004002B, 0x0000000B, 0x000000D0, 0x0000F000, 0x0004002B, 0x0000000B,
    0x00000A3A, 0x00000010, 0x0004002B, 0x0000000B, 0x00000A0D, 0x00000001,
    0x0004002B, 0x0000000B, 0x00000A10, 0x00000002, 0x0004002B, 0x0000000B,
    0x000008A6, 0x00FF00FF, 0x0004002B, 0x0000000B, 0x000005FD, 0xFF00FF00,
    0x0004002B, 0x0000000B, 0x00000A13, 0x00000003, 0x0004002B, 0x0000000B,
    0x00000A0A, 0x00000000, 0x0004002B, 0x0000000C, 0x00000A1A, 0x00000005,
    0x0004002B, 0x0000000B, 0x00000A19, 0x00000005, 0x0004002B, 0x0000000C,
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
    0x00000A05, 0xFFFFFFFE, 0x000A001E, 0x00000489, 0x0000000B, 0x0000000B,
    0x0000000B, 0x0000000B, 0x00000014, 0x0000000B, 0x0000000B, 0x0000000B,
    0x00040020, 0x00000706, 0x00000009, 0x00000489, 0x0004003B, 0x00000706,
    0x00000CE9, 0x00000009, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000,
    0x00040020, 0x00000288, 0x00000009, 0x0000000B, 0x00040020, 0x00000291,
    0x00000009, 0x00000014, 0x00040020, 0x00000292, 0x00000001, 0x00000014,
    0x0004003B, 0x00000292, 0x00000F48, 0x00000001, 0x0006002C, 0x00000014,
    0x00000A24, 0x00000A10, 0x00000A0A, 0x00000A0A, 0x00040017, 0x0000000F,
    0x00000009, 0x00000002, 0x0006002C, 0x00000014, 0x00000A3C, 0x00000A10,
    0x00000A10, 0x00000A0A, 0x0003001D, 0x000007DC, 0x00000017, 0x0003001E,
    0x000007B4, 0x000007DC, 0x00040020, 0x00000A31, 0x00000002, 0x000007B4,
    0x0004003B, 0x00000A31, 0x0000107A, 0x00000002, 0x00040020, 0x00000294,
    0x00000002, 0x00000017, 0x0003001D, 0x000007DD, 0x00000017, 0x0003001E,
    0x000007B5, 0x000007DD, 0x00040020, 0x00000A32, 0x00000002, 0x000007B5,
    0x0004003B, 0x00000A32, 0x0000140E, 0x00000002, 0x0004002B, 0x0000000B,
    0x00000A6A, 0x00000020, 0x0006002C, 0x00000014, 0x00000BC3, 0x00000A16,
    0x00000A6A, 0x00000A0D, 0x0004002B, 0x0000000B, 0x00000A25, 0x00000009,
    0x0004002B, 0x0000000B, 0x00000A28, 0x0000000A, 0x0007002C, 0x00000017,
    0x000009CE, 0x000008A6, 0x000008A6, 0x000008A6, 0x000008A6, 0x0007002C,
    0x00000017, 0x0000013D, 0x00000A22, 0x00000A22, 0x00000A22, 0x00000A22,
    0x0007002C, 0x00000017, 0x0000072E, 0x000005FD, 0x000005FD, 0x000005FD,
    0x000005FD, 0x0007002C, 0x00000017, 0x000002ED, 0x00000A3A, 0x00000A3A,
    0x00000A3A, 0x00000A3A, 0x0007002C, 0x00000017, 0x000002B7, 0x00000A37,
    0x00000A37, 0x00000A37, 0x00000A37, 0x0007002C, 0x00000017, 0x0000064B,
    0x00000144, 0x00000144, 0x00000144, 0x00000144, 0x0007002C, 0x00000017,
    0x00000065, 0x00000A16, 0x00000A16, 0x00000A16, 0x00000A16, 0x0007002C,
    0x00000017, 0x00000192, 0x00000ACE, 0x00000ACE, 0x00000ACE, 0x00000ACE,
    0x0007002C, 0x00000017, 0x00000857, 0x00000A87, 0x00000A87, 0x00000A87,
    0x00000A87, 0x0007002C, 0x00000017, 0x00000215, 0x00000A2E, 0x00000A2E,
    0x00000A2E, 0x00000A2E, 0x0007002C, 0x00000017, 0x000009E6, 0x000000D0,
    0x000000D0, 0x000000D0, 0x000000D0, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x00003B06, 0x000300F7, 0x00003A37,
    0x00000000, 0x000300FB, 0x00000A0A, 0x00002E68, 0x000200F8, 0x00002E68,
    0x00050041, 0x00000288, 0x000060D7, 0x00000CE9, 0x00000A0B, 0x0004003D,
    0x0000000B, 0x00003526, 0x000060D7, 0x000500C7, 0x0000000B, 0x00005FDC,
    0x00003526, 0x00000A0D, 0x000500AB, 0x00000009, 0x00004376, 0x00005FDC,
    0x00000A0A, 0x000500C7, 0x0000000B, 0x00003028, 0x00003526, 0x00000A10,
    0x000500AB, 0x00000009, 0x00004384, 0x00003028, 0x00000A0A, 0x000500C2,
    0x0000000B, 0x00001EB0, 0x00003526, 0x00000A10, 0x000500C7, 0x0000000B,
    0x000061E2, 0x00001EB0, 0x00000A13, 0x00050041, 0x00000288, 0x0000492C,
    0x00000CE9, 0x00000A0E, 0x0004003D, 0x0000000B, 0x00005EAC, 0x0000492C,
    0x00050041, 0x00000288, 0x00004EBA, 0x00000CE9, 0x00000A11, 0x0004003D,
    0x0000000B, 0x00005788, 0x00004EBA, 0x00050041, 0x00000288, 0x00004EBB,
    0x00000CE9, 0x00000A14, 0x0004003D, 0x0000000B, 0x00005789, 0x00004EBB,
    0x00050041, 0x00000291, 0x00004EBC, 0x00000CE9, 0x00000A17, 0x0004003D,
    0x00000014, 0x0000578A, 0x00004EBC, 0x00050041, 0x00000288, 0x00004EBD,
    0x00000CE9, 0x00000A1A, 0x0004003D, 0x0000000B, 0x0000578B, 0x00004EBD,
    0x00050041, 0x00000288, 0x00004EBE, 0x00000CE9, 0x00000A1D, 0x0004003D,
    0x0000000B, 0x0000578C, 0x00004EBE, 0x00050041, 0x00000288, 0x00004E6E,
    0x00000CE9, 0x00000A20, 0x0004003D, 0x0000000B, 0x000019C2, 0x00004E6E,
    0x0004003D, 0x00000014, 0x00002A0E, 0x00000F48, 0x000500C4, 0x00000014,
    0x0000538B, 0x00002A0E, 0x00000A24, 0x0007004F, 0x00000011, 0x000042F0,
    0x0000538B, 0x0000538B, 0x00000000, 0x00000001, 0x0007004F, 0x00000011,
    0x0000242F, 0x0000578A, 0x0000578A, 0x00000000, 0x00000001, 0x000500AE,
    0x0000000F, 0x00004288, 0x000042F0, 0x0000242F, 0x0004009A, 0x00000009,
    0x00006067, 0x00004288, 0x000300F7, 0x000036C2, 0x00000002, 0x000400FA,
    0x00006067, 0x000055E8, 0x000036C2, 0x000200F8, 0x000055E8, 0x000200F9,
    0x00003A37, 0x000200F8, 0x000036C2, 0x000500C4, 0x00000014, 0x000043C0,
    0x0000538B, 0x00000A3C, 0x0004007C, 0x00000016, 0x000038B8, 0x000043C0,
    0x00050051, 0x0000000C, 0x000022C9, 0x000038B8, 0x00000000, 0x00050051,
    0x0000000C, 0x00001A9D, 0x000038B8, 0x00000002, 0x0004007C, 0x0000000C,
    0x000053FD, 0x000019C2, 0x00050084, 0x0000000C, 0x00002C0F, 0x00001A9D,
    0x000053FD, 0x00050051, 0x0000000C, 0x000044BE, 0x000038B8, 0x00000001,
    0x00050080, 0x0000000C, 0x000056D4, 0x00002C0F, 0x000044BE, 0x0004007C,
    0x0000000C, 0x00005785, 0x0000578C, 0x00050084, 0x0000000C, 0x00005FD7,
    0x000056D4, 0x00005785, 0x00050080, 0x0000000C, 0x00002042, 0x000022C9,
    0x00005FD7, 0x0004007C, 0x0000000B, 0x00002A92, 0x00002042, 0x00050080,
    0x0000000B, 0x00002375, 0x00002A92, 0x0000578B, 0x000500C2, 0x0000000B,
    0x00002DCE, 0x00002375, 0x00000A16, 0x000500C2, 0x0000000B, 0x00001B41,
    0x0000578C, 0x00000A16, 0x000300F7, 0x00005F43, 0x00000002, 0x000400FA,
    0x00004376, 0x00005768, 0x00004E29, 0x000200F8, 0x00005768, 0x000300F7,
    0x00001E0B, 0x00000002, 0x000400FA, 0x00004384, 0x0000537D, 0x000018D9,
    0x000200F8, 0x0000537D, 0x0004007C, 0x00000016, 0x00002970, 0x0000538B,
    0x00050051, 0x0000000C, 0x000042C2, 0x00002970, 0x00000001, 0x000500C3,
    0x0000000C, 0x000024FD, 0x000042C2, 0x00000A17, 0x00050051, 0x0000000C,
    0x00002747, 0x00002970, 0x00000002, 0x000500C3, 0x0000000C, 0x0000405C,
    0x00002747, 0x00000A11, 0x000500C2, 0x0000000B, 0x00005B4D, 0x00005789,
    0x00000A16, 0x0004007C, 0x0000000C, 0x000018AA, 0x00005B4D, 0x00050084,
    0x0000000C, 0x00005321, 0x0000405C, 0x000018AA, 0x00050080, 0x0000000C,
    0x00003B27, 0x000024FD, 0x00005321, 0x000500C2, 0x0000000B, 0x00002348,
    0x00005788, 0x00000A19, 0x0004007C, 0x0000000C, 0x0000308B, 0x00002348,
    0x00050084, 0x0000000C, 0x00002878, 0x00003B27, 0x0000308B, 0x00050051,
    0x0000000C, 0x00006242, 0x00002970, 0x00000000, 0x000500C3, 0x0000000C,
    0x00004FC7, 0x00006242, 0x00000A1A, 0x00050080, 0x0000000C, 0x000049FC,
    0x00004FC7, 0x00002878, 0x000500C4, 0x0000000C, 0x0000225D, 0x000049FC,
    0x00000A25, 0x000500C7, 0x0000000C, 0x00002CF6, 0x0000225D, 0x0000078B,
    0x000500C4, 0x0000000C, 0x000049FA, 0x00002CF6, 0x00000A0E, 0x000500C7,
    0x0000000C, 0x00004D38, 0x00006242, 0x00000A20, 0x000500C7, 0x0000000C,
    0x00003138, 0x000042C2, 0x00000A1D, 0x000500C4, 0x0000000C, 0x0000454D,
    0x00003138, 0x00000A11, 0x00050080, 0x0000000C, 0x0000434B, 0x00004D38,
    0x0000454D, 0x000500C4, 0x0000000C, 0x00001B88, 0x0000434B, 0x00000A25,
    0x000500C3, 0x0000000C, 0x00005DE3, 0x00001B88, 0x00000A1D, 0x000500C3,
    0x0000000C, 0x00002215, 0x000042C2, 0x00000A14, 0x00050080, 0x0000000C,
    0x000035A3, 0x00002215, 0x0000405C, 0x000500C7, 0x0000000C, 0x00005A0C,
    0x000035A3, 0x00000A0E, 0x000500C3, 0x0000000C, 0x00004112, 0x00006242,
    0x00000A14, 0x000500C4, 0x0000000C, 0x0000496A, 0x00005A0C, 0x00000A0E,
    0x00050080, 0x0000000C, 0x000034BD, 0x00004112, 0x0000496A, 0x000500C7,
    0x0000000C, 0x00004ADD, 0x000034BD, 0x00000A14, 0x000500C4, 0x0000000C,
    0x0000544A, 0x00004ADD, 0x00000A0E, 0x00050080, 0x0000000C, 0x00003C4B,
    0x00005A0C, 0x0000544A, 0x000500C7, 0x0000000C, 0x0000335E, 0x00005DE3,
    0x000009DB, 0x00050080, 0x0000000C, 0x00004F70, 0x000049FA, 0x0000335E,
    0x000500C4, 0x0000000C, 0x00005B31, 0x00004F70, 0x00000A0E, 0x000500C7,
    0x0000000C, 0x00005AEA, 0x00005DE3, 0x00000A38, 0x00050080, 0x0000000C,
    0x0000285C, 0x00005B31, 0x00005AEA, 0x000500C7, 0x0000000C, 0x000047B4,
    0x00002747, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544B, 0x000047B4,
    0x00000A25, 0x00050080, 0x0000000C, 0x00004157, 0x0000285C, 0x0000544B,
    0x000500C7, 0x0000000C, 0x00004ADE, 0x000042C2, 0x00000A0E, 0x000500C4,
    0x0000000C, 0x0000544C, 0x00004ADE, 0x00000A17, 0x00050080, 0x0000000C,
    0x00004158, 0x00004157, 0x0000544C, 0x000500C7, 0x0000000C, 0x00004FD6,
    0x00003C4B, 0x00000A0E, 0x000500C4, 0x0000000C, 0x00002703, 0x00004FD6,
    0x00000A14, 0x000500C3, 0x0000000C, 0x00003332, 0x00004158, 0x00000A1D,
    0x000500C7, 0x0000000C, 0x000036D6, 0x00003332, 0x00000A20, 0x00050080,
    0x0000000C, 0x00003412, 0x00002703, 0x000036D6, 0x000500C4, 0x0000000C,
    0x00005B32, 0x00003412, 0x00000A14, 0x000500C7, 0x0000000C, 0x00005AB1,
    0x00003C4B, 0x00000A05, 0x00050080, 0x0000000C, 0x00002A9C, 0x00005B32,
    0x00005AB1, 0x000500C4, 0x0000000C, 0x00005B33, 0x00002A9C, 0x00000A11,
    0x000500C7, 0x0000000C, 0x00005AB2, 0x00004158, 0x0000040B, 0x00050080,
    0x0000000C, 0x00002A9D, 0x00005B33, 0x00005AB2, 0x000500C4, 0x0000000C,
    0x00005B34, 0x00002A9D, 0x00000A14, 0x000500C7, 0x0000000C, 0x00005EA0,
    0x00004158, 0x00000AC8, 0x00050080, 0x0000000C, 0x000054ED, 0x00005B34,
    0x00005EA0, 0x000200F9, 0x00001E0B, 0x000200F8, 0x000018D9, 0x0004007C,
    0x00000012, 0x000019AD, 0x000042F0, 0x00050051, 0x0000000C, 0x000042C3,
    0x000019AD, 0x00000000, 0x000500C3, 0x0000000C, 0x000024FE, 0x000042C3,
    0x00000A1A, 0x00050051, 0x0000000C, 0x00002748, 0x000019AD, 0x00000001,
    0x000500C3, 0x0000000C, 0x0000405D, 0x00002748, 0x00000A1A, 0x000500C2,
    0x0000000B, 0x00005B4E, 0x00005788, 0x00000A19, 0x0004007C, 0x0000000C,
    0x000018AB, 0x00005B4E, 0x00050084, 0x0000000C, 0x00005347, 0x0000405D,
    0x000018AB, 0x00050080, 0x0000000C, 0x00003F5E, 0x000024FE, 0x00005347,
    0x000500C4, 0x0000000C, 0x00004A8E, 0x00003F5E, 0x00000A28, 0x000500C7,
    0x0000000C, 0x00002AB6, 0x000042C3, 0x00000A20, 0x000500C7, 0x0000000C,
    0x00003139, 0x00002748, 0x00000A35, 0x000500C4, 0x0000000C, 0x0000454E,
    0x00003139, 0x00000A11, 0x00050080, 0x0000000C, 0x00004397, 0x00002AB6,
    0x0000454E, 0x000500C4, 0x0000000C, 0x000018E7, 0x00004397, 0x00000A13,
    0x000500C7, 0x0000000C, 0x000027B1, 0x000018E7, 0x000009DB, 0x000500C4,
    0x0000000C, 0x00002F76, 0x000027B1, 0x00000A0E, 0x00050080, 0x0000000C,
    0x00003C4C, 0x00004A8E, 0x00002F76, 0x000500C7, 0x0000000C, 0x00003397,
    0x000018E7, 0x00000A38, 0x00050080, 0x0000000C, 0x00004D30, 0x00003C4C,
    0x00003397, 0x000500C7, 0x0000000C, 0x000047B5, 0x00002748, 0x00000A0E,
    0x000500C4, 0x0000000C, 0x0000544D, 0x000047B5, 0x00000A17, 0x00050080,
    0x0000000C, 0x00004159, 0x00004D30, 0x0000544D, 0x000500C7, 0x0000000C,
    0x00005022, 0x00004159, 0x0000040B, 0x000500C4, 0x0000000C, 0x00002416,
    0x00005022, 0x00000A14, 0x000500C7, 0x0000000C, 0x00004A33, 0x00002748,
    0x00000A3B, 0x000500C4, 0x0000000C, 0x00002F77, 0x00004A33, 0x00000A20,
    0x00050080, 0x0000000C, 0x0000415A, 0x00002416, 0x00002F77, 0x000500C7,
    0x0000000C, 0x00004ADF, 0x00004159, 0x00000388, 0x000500C4, 0x0000000C,
    0x0000544E, 0x00004ADF, 0x00000A11, 0x00050080, 0x0000000C, 0x00004144,
    0x0000415A, 0x0000544E, 0x000500C7, 0x0000000C, 0x00005083, 0x00002748,
    0x00000A23, 0x000500C3, 0x0000000C, 0x000041BF, 0x00005083, 0x00000A11,
    0x000500C3, 0x0000000C, 0x00001EEC, 0x000042C3, 0x00000A14, 0x00050080,
    0x0000000C, 0x000035B6, 0x000041BF, 0x00001EEC, 0x000500C7, 0x0000000C,
    0x00005453, 0x000035B6, 0x00000A14, 0x000500C4, 0x0000000C, 0x0000544F,
    0x00005453, 0x00000A1D, 0x00050080, 0x0000000C, 0x00003C4D, 0x00004144,
    0x0000544F, 0x000500C7, 0x0000000C, 0x0000374D, 0x00004159, 0x00000AC8,
    0x00050080, 0x0000000C, 0x00002F42, 0x00003C4D, 0x0000374D, 0x000200F9,
    0x00001E0B, 0x000200F8, 0x00001E0B, 0x000700F5, 0x0000000C, 0x0000292C,
    0x000054ED, 0x0000537D, 0x00002F42, 0x000018D9, 0x000200F9, 0x00005F43,
    0x000200F8, 0x00004E29, 0x0004007C, 0x00000016, 0x00005F7F, 0x0000538B,
    0x00050051, 0x0000000C, 0x000022D6, 0x00005F7F, 0x00000000, 0x00050084,
    0x0000000C, 0x00002492, 0x000022D6, 0x00000A23, 0x00050051, 0x0000000C,
    0x000018DA, 0x00005F7F, 0x00000002, 0x0004007C, 0x0000000C, 0x000038A9,
    0x00005789, 0x00050084, 0x0000000C, 0x00002C10, 0x000018DA, 0x000038A9,
    0x00050051, 0x0000000C, 0x000044BF, 0x00005F7F, 0x00000001, 0x00050080,
    0x0000000C, 0x000056D5, 0x00002C10, 0x000044BF, 0x0004007C, 0x0000000C,
    0x00005786, 0x00005788, 0x00050084, 0x0000000C, 0x00001E9F, 0x000056D5,
    0x00005786, 0x00050080, 0x0000000C, 0x00001F30, 0x00002492, 0x00001E9F,
    0x000200F9, 0x00005F43, 0x000200F8, 0x00005F43, 0x000700F5, 0x0000000C,
    0x00002A3E, 0x0000292C, 0x00001E0B, 0x00001F30, 0x00004E29, 0x0004007C,
    0x0000000C, 0x00001A3F, 0x00005EAC, 0x00050080, 0x0000000C, 0x000056CD,
    0x00001A3F, 0x00002A3E, 0x0004007C, 0x0000000B, 0x00003EE9, 0x000056CD,
    0x000500C2, 0x0000000B, 0x00005665, 0x00003EE9, 0x00000A16, 0x00060041,
    0x00000294, 0x00004315, 0x0000107A, 0x00000A0B, 0x00005665, 0x0004003D,
    0x00000017, 0x00001CAA, 0x00004315, 0x000500AA, 0x00000009, 0x000035C0,
    0x000061E2, 0x00000A0D, 0x000500AA, 0x00000009, 0x00005376, 0x000061E2,
    0x00000A10, 0x000500A6, 0x00000009, 0x00005686, 0x000035C0, 0x00005376,
    0x000300F7, 0x00003463, 0x00000000, 0x000400FA, 0x00005686, 0x00002957,
    0x00003463, 0x000200F8, 0x00002957, 0x000500C7, 0x00000017, 0x0000475F,
    0x00001CAA, 0x000009CE, 0x000500C4, 0x00000017, 0x000024D1, 0x0000475F,
    0x0000013D, 0x000500C7, 0x00000017, 0x000050AC, 0x00001CAA, 0x0000072E,
    0x000500C2, 0x00000017, 0x0000448D, 0x000050AC, 0x0000013D, 0x000500C5,
    0x00000017, 0x00003FF8, 0x000024D1, 0x0000448D, 0x000200F9, 0x00003463,
    0x000200F8, 0x00003463, 0x000700F5, 0x00000017, 0x00005879, 0x00001CAA,
    0x00005F43, 0x00003FF8, 0x00002957, 0x000500AA, 0x00000009, 0x00004CB6,
    0x000061E2, 0x00000A13, 0x000500A6, 0x00000009, 0x00003B23, 0x00005376,
    0x00004CB6, 0x000300F7, 0x00003450, 0x00000000, 0x000400FA, 0x00003B23,
    0x00002B38, 0x00003450, 0x000200F8, 0x00002B38, 0x000500C4, 0x00000017,
    0x00005E17, 0x00005879, 0x000002ED, 0x000500C2, 0x00000017, 0x00003BE7,
    0x00005879, 0x000002ED, 0x000500C5, 0x00000017, 0x000029E8, 0x00005E17,
    0x00003BE7, 0x000200F9, 0x00003450, 0x000200F8, 0x00003450, 0x000700F5,
    0x00000017, 0x00005654, 0x00005879, 0x00003463, 0x000029E8, 0x00002B38,
    0x000600A9, 0x0000000B, 0x00002E64, 0x00004376, 0x00000A10, 0x00000A0D,
    0x00050080, 0x0000000B, 0x00002C4B, 0x00005665, 0x00002E64, 0x00060041,
    0x00000294, 0x00004766, 0x0000107A, 0x00000A0B, 0x00002C4B, 0x0004003D,
    0x00000017, 0x000019B2, 0x00004766, 0x000300F7, 0x00003A1A, 0x00000000,
    0x000400FA, 0x00005686, 0x00002958, 0x00003A1A, 0x000200F8, 0x00002958,
    0x000500C7, 0x00000017, 0x00004760, 0x000019B2, 0x000009CE, 0x000500C4,
    0x00000017, 0x000024D2, 0x00004760, 0x0000013D, 0x000500C7, 0x00000017,
    0x000050AD, 0x000019B2, 0x0000072E, 0x000500C2, 0x00000017, 0x0000448E,
    0x000050AD, 0x0000013D, 0x000500C5, 0x00000017, 0x00003FF9, 0x000024D2,
    0x0000448E, 0x000200F9, 0x00003A1A, 0x000200F8, 0x00003A1A, 0x000700F5,
    0x00000017, 0x00002AAC, 0x000019B2, 0x00003450, 0x00003FF9, 0x00002958,
    0x000300F7, 0x00002DC8, 0x00000000, 0x000400FA, 0x00003B23, 0x00002B39,
    0x00002DC8, 0x000200F8, 0x00002B39, 0x000500C4, 0x00000017, 0x00005E18,
    0x00002AAC, 0x000002ED, 0x000500C2, 0x00000017, 0x00003BE8, 0x00002AAC,
    0x000002ED, 0x000500C5, 0x00000017, 0x000029E9, 0x00005E18, 0x00003BE8,
    0x000200F9, 0x00002DC8, 0x000200F8, 0x00002DC8, 0x000700F5, 0x00000017,
    0x000043D0, 0x00002AAC, 0x00003A1A, 0x000029E9, 0x00002B39, 0x00050051,
    0x0000000B, 0x00005A0E, 0x00005654, 0x00000000, 0x00050051, 0x0000000B,
    0x00002B4E, 0x00005654, 0x00000002, 0x00050051, 0x0000000B, 0x00001DD9,
    0x000043D0, 0x00000000, 0x00050051, 0x0000000B, 0x0000275B, 0x000043D0,
    0x00000002, 0x00070050, 0x00000017, 0x00004170, 0x00005A0E, 0x00002B4E,
    0x00001DD9, 0x0000275B, 0x000500C7, 0x00000017, 0x00001FCD, 0x00004170,
    0x000002B7, 0x000500C7, 0x00000017, 0x00004210, 0x00004170, 0x0000064B,
    0x000500C4, 0x00000017, 0x00006164, 0x00004210, 0x00000065, 0x000500C5,
    0x00000017, 0x000042BB, 0x00001FCD, 0x00006164, 0x000500C7, 0x00000017,
    0x00005182, 0x00004170, 0x00000192, 0x000500C4, 0x00000017, 0x00005B17,
    0x00005182, 0x0000013D, 0x000500C5, 0x00000017, 0x000042BC, 0x000042BB,
    0x00005B17, 0x000500C7, 0x00000017, 0x00005183, 0x00004170, 0x00000857,
    0x000500C4, 0x00000017, 0x00005B18, 0x00005183, 0x00000215, 0x000500C5,
    0x00000017, 0x000042BD, 0x000042BC, 0x00005B18, 0x000500C7, 0x00000017,
    0x00005184, 0x00004170, 0x000009E6, 0x000500C4, 0x00000017, 0x00005125,
    0x00005184, 0x000002ED, 0x000500C5, 0x00000017, 0x000048D8, 0x000042BD,
    0x00005125, 0x00060041, 0x00000294, 0x000051EE, 0x0000140E, 0x00000A0B,
    0x00002DCE, 0x0003003E, 0x000051EE, 0x000048D8, 0x00050051, 0x0000000B,
    0x00003220, 0x000043C0, 0x00000001, 0x00050080, 0x0000000B, 0x00005AC0,
    0x00003220, 0x00000A0E, 0x000500B0, 0x00000009, 0x00004411, 0x00005AC0,
    0x000019C2, 0x000300F7, 0x00001C26, 0x00000002, 0x000400FA, 0x00004411,
    0x0000592C, 0x00001C26, 0x000200F8, 0x0000592C, 0x00050080, 0x0000000B,
    0x00003CD8, 0x00002DCE, 0x00001B41, 0x000500C2, 0x00000017, 0x00003458,
    0x00004170, 0x000002ED, 0x000500C7, 0x00000017, 0x00002CA8, 0x00003458,
    0x000002B7, 0x000500C7, 0x00000017, 0x00004943, 0x00003458, 0x0000064B,
    0x000500C4, 0x00000017, 0x00006165, 0x00004943, 0x00000065, 0x000500C5,
    0x00000017, 0x000042BE, 0x00002CA8, 0x00006165, 0x000500C7, 0x00000017,
    0x00005185, 0x00003458, 0x00000192, 0x000500C4, 0x00000017, 0x00005B19,
    0x00005185, 0x0000013D, 0x000500C5, 0x00000017, 0x000042BF, 0x000042BE,
    0x00005B19, 0x000500C7, 0x00000017, 0x00005186, 0x00003458, 0x00000857,
    0x000500C4, 0x00000017, 0x00005B1A, 0x00005186, 0x00000215, 0x000500C5,
    0x00000017, 0x000042C0, 0x000042BF, 0x00005B1A, 0x000500C7, 0x00000017,
    0x00005187, 0x00003458, 0x000009E6, 0x000500C4, 0x00000017, 0x00005126,
    0x00005187, 0x000002ED, 0x000500C5, 0x00000017, 0x000048D9, 0x000042C0,
    0x00005126, 0x00060041, 0x00000294, 0x0000556B, 0x0000140E, 0x00000A0B,
    0x00003CD8, 0x0003003E, 0x0000556B, 0x000048D9, 0x00050080, 0x0000000B,
    0x000039F8, 0x00003220, 0x00000A11, 0x000500B0, 0x00000009, 0x00002E0B,
    0x000039F8, 0x000019C2, 0x000300F7, 0x00001C25, 0x00000002, 0x000400FA,
    0x00002E0B, 0x000050C9, 0x00001C25, 0x000200F8, 0x000050C9, 0x00050080,
    0x0000000B, 0x000035F4, 0x00003CD8, 0x00001B41, 0x00050051, 0x0000000B,
    0x00002830, 0x00005654, 0x00000001, 0x00050051, 0x0000000B, 0x00002744,
    0x00005654, 0x00000003, 0x00050051, 0x0000000B, 0x00001DDA, 0x000043D0,
    0x00000001, 0x00050051, 0x0000000B, 0x0000275C, 0x000043D0, 0x00000003,
    0x00070050, 0x00000017, 0x00004171, 0x00002830, 0x00002744, 0x00001DDA,
    0x0000275C, 0x000500C7, 0x00000017, 0x00001FCE, 0x00004171, 0x000002B7,
    0x000500C7, 0x00000017, 0x00004211, 0x00004171, 0x0000064B, 0x000500C4,
    0x00000017, 0x00006166, 0x00004211, 0x00000065, 0x000500C5, 0x00000017,
    0x000042C1, 0x00001FCE, 0x00006166, 0x000500C7, 0x00000017, 0x00005188,
    0x00004171, 0x00000192, 0x000500C4, 0x00000017, 0x00005B1B, 0x00005188,
    0x0000013D, 0x000500C5, 0x00000017, 0x000042C4, 0x000042C1, 0x00005B1B,
    0x000500C7, 0x00000017, 0x00005189, 0x00004171, 0x00000857, 0x000500C4,
    0x00000017, 0x00005B1C, 0x00005189, 0x00000215, 0x000500C5, 0x00000017,
    0x000042C5, 0x000042C4, 0x00005B1C, 0x000500C7, 0x00000017, 0x0000518A,
    0x00004171, 0x000009E6, 0x000500C4, 0x00000017, 0x00005127, 0x0000518A,
    0x000002ED, 0x000500C5, 0x00000017, 0x000048DA, 0x000042C5, 0x00005127,
    0x00060041, 0x00000294, 0x0000556C, 0x0000140E, 0x00000A0B, 0x000035F4,
    0x0003003E, 0x0000556C, 0x000048DA, 0x00050080, 0x0000000B, 0x000039F9,
    0x00003220, 0x00000A14, 0x000500B0, 0x00000009, 0x00002E0C, 0x000039F9,
    0x000019C2, 0x000300F7, 0x00004665, 0x00000002, 0x000400FA, 0x00002E0C,
    0x0000592D, 0x00004665, 0x000200F8, 0x0000592D, 0x00050080, 0x0000000B,
    0x00003CD9, 0x000035F4, 0x00001B41, 0x000500C2, 0x00000017, 0x00003459,
    0x00004171, 0x000002ED, 0x000500C7, 0x00000017, 0x00002CA9, 0x00003459,
    0x000002B7, 0x000500C7, 0x00000017, 0x00004944, 0x00003459, 0x0000064B,
    0x000500C4, 0x00000017, 0x00006167, 0x00004944, 0x00000065, 0x000500C5,
    0x00000017, 0x000042C6, 0x00002CA9, 0x00006167, 0x000500C7, 0x00000017,
    0x0000518B, 0x00003459, 0x00000192, 0x000500C4, 0x00000017, 0x00005B1D,
    0x0000518B, 0x0000013D, 0x000500C5, 0x00000017, 0x000042C7, 0x000042C6,
    0x00005B1D, 0x000500C7, 0x00000017, 0x0000518C, 0x00003459, 0x00000857,
    0x000500C4, 0x00000017, 0x00005B1E, 0x0000518C, 0x00000215, 0x000500C5,
    0x00000017, 0x000042C8, 0x000042C7, 0x00005B1E, 0x000500C7, 0x00000017,
    0x0000518D, 0x00003459, 0x000009E6, 0x000500C4, 0x00000017, 0x00005128,
    0x0000518D, 0x000002ED, 0x000500C5, 0x00000017, 0x000048DB, 0x000042C8,
    0x00005128, 0x00060041, 0x00000294, 0x00005E66, 0x0000140E, 0x00000A0B,
    0x00003CD9, 0x0003003E, 0x00005E66, 0x000048DB, 0x000200F9, 0x00004665,
    0x000200F8, 0x00004665, 0x000200F9, 0x00001C25, 0x000200F8, 0x00001C25,
    0x000200F9, 0x00001C26, 0x000200F8, 0x00001C26, 0x000200F9, 0x00003A37,
    0x000200F8, 0x00003A37, 0x000100FD, 0x00010038,
};
