// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 25157
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
      %int_4 = OpConstant %int 4
      %int_6 = OpConstant %int 6
     %int_11 = OpConstant %int 11
     %int_15 = OpConstant %int 15
      %int_1 = OpConstant %int 1
      %int_5 = OpConstant %int 5
      %int_7 = OpConstant %int 7
      %int_8 = OpConstant %int 8
     %int_12 = OpConstant %int 12
     %uint_0 = OpConstant %uint 0
      %int_3 = OpConstant %int 3
      %int_2 = OpConstant %int 2
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
       %2619 = OpConstantComposite %v3uint %uint_2 %uint_2 %uint_0
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
      %17344 = OpShiftLeftLogical %v3uint %21387 %2619
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
      %10225 = OpShiftRightLogical %uint %9077 %uint_4
       %7973 = OpShiftRightLogical %uint %22412 %uint_4
      %24701 = OpLogicalNot %bool %17270
               OpSelectionMerge %19040 DontFlatten
               OpBranchConditional %24701 %9741 %17007
       %9741 = OpLabel
      %17463 = OpCompositeExtract %uint %21387 0
      %11246 = OpCompositeExtract %uint %21387 1
      %18801 = OpCompositeExtract %uint %21387 2
      %14831 = OpIMul %uint %22409 %18801
      %20322 = OpIAdd %uint %11246 %14831
      %21676 = OpIMul %uint %22408 %20322
      %20398 = OpIAdd %uint %17463 %21676
      %11367 = OpShiftLeftLogical %uint %20398 %uint_3
               OpBranch %19040
      %17007 = OpLabel
               OpSelectionMerge %23536 DontFlatten
               OpBranchConditional %17284 %23520 %24353
      %23520 = OpLabel
      %10111 = OpBitcast %v3int %21387
      %19476 = OpShiftRightLogical %uint %22408 %int_5
      %18810 = OpShiftRightLogical %uint %22409 %int_4
       %6782 = OpCompositeExtract %int %10111 2
      %12089 = OpShiftRightArithmetic %int %6782 %int_2
      %22400 = OpBitcast %int %18810
       %7938 = OpIMul %int %12089 %22400
      %25154 = OpCompositeExtract %int %10111 1
      %19055 = OpShiftRightArithmetic %int %25154 %int_4
      %11052 = OpIAdd %int %7938 %19055
      %16898 = OpBitcast %int %19476
      %14944 = OpIMul %int %11052 %16898
      %25155 = OpCompositeExtract %int %10111 0
      %20423 = OpShiftRightArithmetic %int %25155 %int_5
      %18940 = OpIAdd %int %14944 %20423
       %8797 = OpShiftLeftLogical %int %18940 %int_7
      %11434 = OpBitwiseAnd %int %6782 %int_3
      %19630 = OpShiftLeftLogical %int %11434 %int_5
      %14398 = OpShiftRightArithmetic %int %25154 %int_1
      %21364 = OpBitwiseAnd %int %14398 %int_3
      %21706 = OpShiftLeftLogical %int %21364 %int_3
      %17102 = OpBitwiseOr %int %19630 %21706
      %20693 = OpBitwiseAnd %int %25155 %int_7
      %15069 = OpBitwiseOr %int %17102 %20693
      %17334 = OpBitwiseOr %int %8797 %15069
      %24144 = OpShiftLeftLogical %int %17334 %uint_3
      %13015 = OpShiftRightArithmetic %int %25154 %int_3
       %9929 = OpBitwiseXor %int %13015 %12089
      %16793 = OpBitwiseAnd %int %9929 %int_1
       %9616 = OpShiftRightArithmetic %int %25155 %int_3
      %20574 = OpBitwiseAnd %int %9616 %int_3
      %21533 = OpShiftLeftLogical %int %16793 %int_1
       %8890 = OpBitwiseXor %int %20574 %21533
      %20598 = OpBitwiseAnd %int %25154 %int_1
      %21032 = OpShiftLeftLogical %int %20598 %int_4
       %6551 = OpShiftLeftLogical %int %8890 %int_6
      %18430 = OpBitwiseOr %int %21032 %6551
       %7168 = OpShiftLeftLogical %int %16793 %int_11
      %15489 = OpBitwiseOr %int %18430 %7168
      %20655 = OpBitwiseAnd %int %24144 %int_15
      %15472 = OpBitwiseOr %int %15489 %20655
      %14149 = OpShiftRightArithmetic %int %24144 %int_4
       %6328 = OpBitwiseAnd %int %14149 %int_1
      %21630 = OpShiftLeftLogical %int %6328 %int_5
      %17832 = OpBitwiseOr %int %15472 %21630
      %14958 = OpShiftRightArithmetic %int %24144 %int_5
       %6329 = OpBitwiseAnd %int %14958 %int_7
      %21631 = OpShiftLeftLogical %int %6329 %int_8
      %17775 = OpBitwiseOr %int %17832 %21631
      %15496 = OpShiftRightArithmetic %int %24144 %int_8
      %10276 = OpShiftLeftLogical %int %15496 %int_12
      %15225 = OpBitwiseOr %int %17775 %10276
      %16869 = OpBitcast %uint %15225
               OpBranch %23536
      %24353 = OpLabel
      %23000 = OpBitcast %v2int %17136
      %22120 = OpShiftRightLogical %uint %22408 %int_5
      %14597 = OpCompositeExtract %int %23000 1
      %12090 = OpShiftRightArithmetic %int %14597 %int_5
      %22401 = OpBitcast %int %22120
       %7939 = OpIMul %int %12090 %22401
      %25156 = OpCompositeExtract %int %23000 0
      %20424 = OpShiftRightArithmetic %int %25156 %int_5
      %18864 = OpIAdd %int %7939 %20424
       %9546 = OpShiftLeftLogical %int %18864 %int_6
      %24635 = OpShiftRightArithmetic %int %14597 %int_1
      %21402 = OpBitwiseAnd %int %24635 %int_7
      %21322 = OpShiftLeftLogical %int %21402 %int_3
      %20133 = OpBitwiseAnd %int %25156 %int_7
      %11034 = OpBitwiseOr %int %21322 %20133
      %17335 = OpBitwiseOr %int %9546 %11034
      %24163 = OpShiftLeftLogical %int %17335 %uint_3
      %12766 = OpShiftRightArithmetic %int %14597 %int_4
      %21575 = OpBitwiseAnd %int %12766 %int_1
      %10406 = OpShiftRightArithmetic %int %25156 %int_3
      %20766 = OpBitwiseAnd %int %10406 %int_3
      %10425 = OpShiftRightArithmetic %int %14597 %int_3
      %20575 = OpBitwiseAnd %int %10425 %int_1
      %21534 = OpShiftLeftLogical %int %20575 %int_1
       %8891 = OpBitwiseXor %int %20766 %21534
      %20599 = OpBitwiseAnd %int %14597 %int_1
      %21033 = OpShiftLeftLogical %int %20599 %int_4
       %6552 = OpShiftLeftLogical %int %8891 %int_6
      %18431 = OpBitwiseOr %int %21033 %6552
       %7169 = OpShiftLeftLogical %int %21575 %int_11
      %15490 = OpBitwiseOr %int %18431 %7169
      %20656 = OpBitwiseAnd %int %24163 %int_15
      %15473 = OpBitwiseOr %int %15490 %20656
      %14150 = OpShiftRightArithmetic %int %24163 %int_4
       %6330 = OpBitwiseAnd %int %14150 %int_1
      %21632 = OpShiftLeftLogical %int %6330 %int_5
      %17833 = OpBitwiseOr %int %15473 %21632
      %14959 = OpShiftRightArithmetic %int %24163 %int_5
       %6331 = OpBitwiseAnd %int %14959 %int_7
      %21633 = OpShiftLeftLogical %int %6331 %int_8
      %17776 = OpBitwiseOr %int %17833 %21633
      %15497 = OpShiftRightArithmetic %int %24163 %int_8
      %10277 = OpShiftLeftLogical %int %15497 %int_12
      %15226 = OpBitwiseOr %int %17776 %10277
      %16870 = OpBitcast %uint %15226
               OpBranch %23536
      %23536 = OpLabel
      %10540 = OpPhi %uint %16869 %23520 %16870 %24353
               OpBranch %19040
      %19040 = OpLabel
      %11376 = OpPhi %uint %11367 %9741 %10540 %23536
      %18621 = OpIAdd %uint %11376 %24236
      %15698 = OpShiftRightLogical %uint %18621 %uint_4
      %20399 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_source %int_0 %15698
       %7338 = OpLoad %v4uint %20399
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
      %22649 = OpPhi %v4uint %7338 %19040 %16376 %10583
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
      %11339 = OpIAdd %uint %15698 %11876
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
      %20974 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_dest %int_0 %10225
               OpStore %20974 %18648
      %12832 = OpCompositeExtract %uint %17344 1
      %23232 = OpIAdd %uint %12832 %int_1
      %17425 = OpULessThan %bool %23232 %6594
               OpSelectionMerge %7206 DontFlatten
               OpBranchConditional %17425 %22828 %7206
      %22828 = OpLabel
      %15576 = OpIAdd %uint %10225 %7973
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
      %13812 = OpIAdd %uint %15576 %7973
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
      %17090 = OpBitwiseOr %v4uint %17089 %23323
      %20873 = OpBitwiseAnd %v4uint %16753 %2135
      %23324 = OpShiftLeftLogical %v4uint %20873 %533
      %17091 = OpBitwiseOr %v4uint %17090 %23324
      %20874 = OpBitwiseAnd %v4uint %16753 %2534
      %20775 = OpShiftLeftLogical %v4uint %20874 %749
      %18650 = OpBitwiseOr %v4uint %17091 %20775
      %21868 = OpAccessChain %_ptr_Uniform_v4uint %xe_texture_load_dest %int_0 %13812
               OpStore %21868 %18650
      %14841 = OpIAdd %uint %12832 %int_3
      %11788 = OpULessThan %bool %14841 %6594
               OpSelectionMerge %18021 DontFlatten
               OpBranchConditional %11788 %22829 %18021
      %22829 = OpLabel
      %15577 = OpIAdd %uint %13812 %7973
      %13401 = OpShiftRightLogical %v4uint %16753 %749
      %11433 = OpBitwiseAnd %v4uint %13401 %695
      %18756 = OpBitwiseAnd %v4uint %13401 %1611
      %24935 = OpShiftLeftLogical %v4uint %18756 %101
      %17092 = OpBitwiseOr %v4uint %11433 %24935
      %20875 = OpBitwiseAnd %v4uint %13401 %402
      %23325 = OpShiftLeftLogical %v4uint %20875 %317
      %17093 = OpBitwiseOr %v4uint %17092 %23325
      %20876 = OpBitwiseAnd %v4uint %13401 %2135
      %23326 = OpShiftLeftLogical %v4uint %20876 %533
      %17094 = OpBitwiseOr %v4uint %17093 %23326
      %20877 = OpBitwiseAnd %v4uint %13401 %2534
      %20776 = OpShiftLeftLogical %v4uint %20877 %749
      %18651 = OpBitwiseOr %v4uint %17094 %20776
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
    0x07230203, 0x00010000, 0x0008000B, 0x00006245, 0x00000000, 0x00020011,
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
    0x0004002B, 0x0000000B, 0x00000A13, 0x00000003, 0x0004002B, 0x0000000C,
    0x00000A17, 0x00000004, 0x0004002B, 0x0000000C, 0x00000A1D, 0x00000006,
    0x0004002B, 0x0000000C, 0x00000A2C, 0x0000000B, 0x0004002B, 0x0000000C,
    0x00000A38, 0x0000000F, 0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001,
    0x0004002B, 0x0000000C, 0x00000A1A, 0x00000005, 0x0004002B, 0x0000000C,
    0x00000A20, 0x00000007, 0x0004002B, 0x0000000C, 0x00000A23, 0x00000008,
    0x0004002B, 0x0000000C, 0x00000A2F, 0x0000000C, 0x0004002B, 0x0000000B,
    0x00000A0A, 0x00000000, 0x0004002B, 0x0000000C, 0x00000A14, 0x00000003,
    0x0004002B, 0x0000000C, 0x00000A11, 0x00000002, 0x000A001E, 0x00000489,
    0x0000000B, 0x0000000B, 0x0000000B, 0x0000000B, 0x00000014, 0x0000000B,
    0x0000000B, 0x0000000B, 0x00040020, 0x00000706, 0x00000009, 0x00000489,
    0x0004003B, 0x00000706, 0x00000CE9, 0x00000009, 0x0004002B, 0x0000000C,
    0x00000A0B, 0x00000000, 0x00040020, 0x00000288, 0x00000009, 0x0000000B,
    0x00040020, 0x00000291, 0x00000009, 0x00000014, 0x00040020, 0x00000292,
    0x00000001, 0x00000014, 0x0004003B, 0x00000292, 0x00000F48, 0x00000001,
    0x0006002C, 0x00000014, 0x00000A24, 0x00000A10, 0x00000A0A, 0x00000A0A,
    0x00040017, 0x0000000F, 0x00000009, 0x00000002, 0x0006002C, 0x00000014,
    0x00000A3B, 0x00000A10, 0x00000A10, 0x00000A0A, 0x0003001D, 0x000007DC,
    0x00000017, 0x0003001E, 0x000007B4, 0x000007DC, 0x00040020, 0x00000A31,
    0x00000002, 0x000007B4, 0x0004003B, 0x00000A31, 0x0000107A, 0x00000002,
    0x00040020, 0x00000294, 0x00000002, 0x00000017, 0x0003001D, 0x000007DD,
    0x00000017, 0x0003001E, 0x000007B5, 0x000007DD, 0x00040020, 0x00000A32,
    0x00000002, 0x000007B5, 0x0004003B, 0x00000A32, 0x0000140E, 0x00000002,
    0x0004002B, 0x0000000B, 0x00000A6A, 0x00000020, 0x0006002C, 0x00000014,
    0x00000BC3, 0x00000A16, 0x00000A6A, 0x00000A0D, 0x0007002C, 0x00000017,
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
    0x0000538B, 0x00000A3B, 0x0004007C, 0x00000016, 0x000038B8, 0x000043C0,
    0x00050051, 0x0000000C, 0x000022C9, 0x000038B8, 0x00000000, 0x00050051,
    0x0000000C, 0x00001A9D, 0x000038B8, 0x00000002, 0x0004007C, 0x0000000C,
    0x000053FD, 0x000019C2, 0x00050084, 0x0000000C, 0x00002C0F, 0x00001A9D,
    0x000053FD, 0x00050051, 0x0000000C, 0x000044BE, 0x000038B8, 0x00000001,
    0x00050080, 0x0000000C, 0x000056D4, 0x00002C0F, 0x000044BE, 0x0004007C,
    0x0000000C, 0x00005785, 0x0000578C, 0x00050084, 0x0000000C, 0x00005FD7,
    0x000056D4, 0x00005785, 0x00050080, 0x0000000C, 0x00002042, 0x000022C9,
    0x00005FD7, 0x0004007C, 0x0000000B, 0x00002A92, 0x00002042, 0x00050080,
    0x0000000B, 0x00002375, 0x00002A92, 0x0000578B, 0x000500C2, 0x0000000B,
    0x000027F1, 0x00002375, 0x00000A16, 0x000500C2, 0x0000000B, 0x00001F25,
    0x0000578C, 0x00000A16, 0x000400A8, 0x00000009, 0x0000607D, 0x00004376,
    0x000300F7, 0x00004A60, 0x00000002, 0x000400FA, 0x0000607D, 0x0000260D,
    0x0000426F, 0x000200F8, 0x0000260D, 0x00050051, 0x0000000B, 0x00004437,
    0x0000538B, 0x00000000, 0x00050051, 0x0000000B, 0x00002BEE, 0x0000538B,
    0x00000001, 0x00050051, 0x0000000B, 0x00004971, 0x0000538B, 0x00000002,
    0x00050084, 0x0000000B, 0x000039EF, 0x00005789, 0x00004971, 0x00050080,
    0x0000000B, 0x00004F62, 0x00002BEE, 0x000039EF, 0x00050084, 0x0000000B,
    0x000054AC, 0x00005788, 0x00004F62, 0x00050080, 0x0000000B, 0x00004FAE,
    0x00004437, 0x000054AC, 0x000500C4, 0x0000000B, 0x00002C67, 0x00004FAE,
    0x00000A13, 0x000200F9, 0x00004A60, 0x000200F8, 0x0000426F, 0x000300F7,
    0x00005BF0, 0x00000002, 0x000400FA, 0x00004384, 0x00005BE0, 0x00005F21,
    0x000200F8, 0x00005BE0, 0x0004007C, 0x00000016, 0x0000277F, 0x0000538B,
    0x000500C2, 0x0000000B, 0x00004C14, 0x00005788, 0x00000A1A, 0x000500C2,
    0x0000000B, 0x0000497A, 0x00005789, 0x00000A17, 0x00050051, 0x0000000C,
    0x00001A7E, 0x0000277F, 0x00000002, 0x000500C3, 0x0000000C, 0x00002F39,
    0x00001A7E, 0x00000A11, 0x0004007C, 0x0000000C, 0x00005780, 0x0000497A,
    0x00050084, 0x0000000C, 0x00001F02, 0x00002F39, 0x00005780, 0x00050051,
    0x0000000C, 0x00006242, 0x0000277F, 0x00000001, 0x000500C3, 0x0000000C,
    0x00004A6F, 0x00006242, 0x00000A17, 0x00050080, 0x0000000C, 0x00002B2C,
    0x00001F02, 0x00004A6F, 0x0004007C, 0x0000000C, 0x00004202, 0x00004C14,
    0x00050084, 0x0000000C, 0x00003A60, 0x00002B2C, 0x00004202, 0x00050051,
    0x0000000C, 0x00006243, 0x0000277F, 0x00000000, 0x000500C3, 0x0000000C,
    0x00004FC7, 0x00006243, 0x00000A1A, 0x00050080, 0x0000000C, 0x000049FC,
    0x00003A60, 0x00004FC7, 0x000500C4, 0x0000000C, 0x0000225D, 0x000049FC,
    0x00000A20, 0x000500C7, 0x0000000C, 0x00002CAA, 0x00001A7E, 0x00000A14,
    0x000500C4, 0x0000000C, 0x00004CAE, 0x00002CAA, 0x00000A1A, 0x000500C3,
    0x0000000C, 0x0000383E, 0x00006242, 0x00000A0E, 0x000500C7, 0x0000000C,
    0x00005374, 0x0000383E, 0x00000A14, 0x000500C4, 0x0000000C, 0x000054CA,
    0x00005374, 0x00000A14, 0x000500C5, 0x0000000C, 0x000042CE, 0x00004CAE,
    0x000054CA, 0x000500C7, 0x0000000C, 0x000050D5, 0x00006243, 0x00000A20,
    0x000500C5, 0x0000000C, 0x00003ADD, 0x000042CE, 0x000050D5, 0x000500C5,
    0x0000000C, 0x000043B6, 0x0000225D, 0x00003ADD, 0x000500C4, 0x0000000C,
    0x00005E50, 0x000043B6, 0x00000A13, 0x000500C3, 0x0000000C, 0x000032D7,
    0x00006242, 0x00000A14, 0x000500C6, 0x0000000C, 0x000026C9, 0x000032D7,
    0x00002F39, 0x000500C7, 0x0000000C, 0x00004199, 0x000026C9, 0x00000A0E,
    0x000500C3, 0x0000000C, 0x00002590, 0x00006243, 0x00000A14, 0x000500C7,
    0x0000000C, 0x0000505E, 0x00002590, 0x00000A14, 0x000500C4, 0x0000000C,
    0x0000541D, 0x00004199, 0x00000A0E, 0x000500C6, 0x0000000C, 0x000022BA,
    0x0000505E, 0x0000541D, 0x000500C7, 0x0000000C, 0x00005076, 0x00006242,
    0x00000A0E, 0x000500C4, 0x0000000C, 0x00005228, 0x00005076, 0x00000A17,
    0x000500C4, 0x0000000C, 0x00001997, 0x000022BA, 0x00000A1D, 0x000500C5,
    0x0000000C, 0x000047FE, 0x00005228, 0x00001997, 0x000500C4, 0x0000000C,
    0x00001C00, 0x00004199, 0x00000A2C, 0x000500C5, 0x0000000C, 0x00003C81,
    0x000047FE, 0x00001C00, 0x000500C7, 0x0000000C, 0x000050AF, 0x00005E50,
    0x00000A38, 0x000500C5, 0x0000000C, 0x00003C70, 0x00003C81, 0x000050AF,
    0x000500C3, 0x0000000C, 0x00003745, 0x00005E50, 0x00000A17, 0x000500C7,
    0x0000000C, 0x000018B8, 0x00003745, 0x00000A0E, 0x000500C4, 0x0000000C,
    0x0000547E, 0x000018B8, 0x00000A1A, 0x000500C5, 0x0000000C, 0x000045A8,
    0x00003C70, 0x0000547E, 0x000500C3, 0x0000000C, 0x00003A6E, 0x00005E50,
    0x00000A1A, 0x000500C7, 0x0000000C, 0x000018B9, 0x00003A6E, 0x00000A20,
    0x000500C4, 0x0000000C, 0x0000547F, 0x000018B9, 0x00000A23, 0x000500C5,
    0x0000000C, 0x0000456F, 0x000045A8, 0x0000547F, 0x000500C3, 0x0000000C,
    0x00003C88, 0x00005E50, 0x00000A23, 0x000500C4, 0x0000000C, 0x00002824,
    0x00003C88, 0x00000A2F, 0x000500C5, 0x0000000C, 0x00003B79, 0x0000456F,
    0x00002824, 0x0004007C, 0x0000000B, 0x000041E5, 0x00003B79, 0x000200F9,
    0x00005BF0, 0x000200F8, 0x00005F21, 0x0004007C, 0x00000012, 0x000059D8,
    0x000042F0, 0x000500C2, 0x0000000B, 0x00005668, 0x00005788, 0x00000A1A,
    0x00050051, 0x0000000C, 0x00003905, 0x000059D8, 0x00000001, 0x000500C3,
    0x0000000C, 0x00002F3A, 0x00003905, 0x00000A1A, 0x0004007C, 0x0000000C,
    0x00005781, 0x00005668, 0x00050084, 0x0000000C, 0x00001F03, 0x00002F3A,
    0x00005781, 0x00050051, 0x0000000C, 0x00006244, 0x000059D8, 0x00000000,
    0x000500C3, 0x0000000C, 0x00004FC8, 0x00006244, 0x00000A1A, 0x00050080,
    0x0000000C, 0x000049B0, 0x00001F03, 0x00004FC8, 0x000500C4, 0x0000000C,
    0x0000254A, 0x000049B0, 0x00000A1D, 0x000500C3, 0x0000000C, 0x0000603B,
    0x00003905, 0x00000A0E, 0x000500C7, 0x0000000C, 0x0000539A, 0x0000603B,
    0x00000A20, 0x000500C4, 0x0000000C, 0x0000534A, 0x0000539A, 0x00000A14,
    0x000500C7, 0x0000000C, 0x00004EA5, 0x00006244, 0x00000A20, 0x000500C5,
    0x0000000C, 0x00002B1A, 0x0000534A, 0x00004EA5, 0x000500C5, 0x0000000C,
    0x000043B7, 0x0000254A, 0x00002B1A, 0x000500C4, 0x0000000C, 0x00005E63,
    0x000043B7, 0x00000A13, 0x000500C3, 0x0000000C, 0x000031DE, 0x00003905,
    0x00000A17, 0x000500C7, 0x0000000C, 0x00005447, 0x000031DE, 0x00000A0E,
    0x000500C3, 0x0000000C, 0x000028A6, 0x00006244, 0x00000A14, 0x000500C7,
    0x0000000C, 0x0000511E, 0x000028A6, 0x00000A14, 0x000500C3, 0x0000000C,
    0x000028B9, 0x00003905, 0x00000A14, 0x000500C7, 0x0000000C, 0x0000505F,
    0x000028B9, 0x00000A0E, 0x000500C4, 0x0000000C, 0x0000541E, 0x0000505F,
    0x00000A0E, 0x000500C6, 0x0000000C, 0x000022BB, 0x0000511E, 0x0000541E,
    0x000500C7, 0x0000000C, 0x00005077, 0x00003905, 0x00000A0E, 0x000500C4,
    0x0000000C, 0x00005229, 0x00005077, 0x00000A17, 0x000500C4, 0x0000000C,
    0x00001998, 0x000022BB, 0x00000A1D, 0x000500C5, 0x0000000C, 0x000047FF,
    0x00005229, 0x00001998, 0x000500C4, 0x0000000C, 0x00001C01, 0x00005447,
    0x00000A2C, 0x000500C5, 0x0000000C, 0x00003C82, 0x000047FF, 0x00001C01,
    0x000500C7, 0x0000000C, 0x000050B0, 0x00005E63, 0x00000A38, 0x000500C5,
    0x0000000C, 0x00003C71, 0x00003C82, 0x000050B0, 0x000500C3, 0x0000000C,
    0x00003746, 0x00005E63, 0x00000A17, 0x000500C7, 0x0000000C, 0x000018BA,
    0x00003746, 0x00000A0E, 0x000500C4, 0x0000000C, 0x00005480, 0x000018BA,
    0x00000A1A, 0x000500C5, 0x0000000C, 0x000045A9, 0x00003C71, 0x00005480,
    0x000500C3, 0x0000000C, 0x00003A6F, 0x00005E63, 0x00000A1A, 0x000500C7,
    0x0000000C, 0x000018BB, 0x00003A6F, 0x00000A20, 0x000500C4, 0x0000000C,
    0x00005481, 0x000018BB, 0x00000A23, 0x000500C5, 0x0000000C, 0x00004570,
    0x000045A9, 0x00005481, 0x000500C3, 0x0000000C, 0x00003C89, 0x00005E63,
    0x00000A23, 0x000500C4, 0x0000000C, 0x00002825, 0x00003C89, 0x00000A2F,
    0x000500C5, 0x0000000C, 0x00003B7A, 0x00004570, 0x00002825, 0x0004007C,
    0x0000000B, 0x000041E6, 0x00003B7A, 0x000200F9, 0x00005BF0, 0x000200F8,
    0x00005BF0, 0x000700F5, 0x0000000B, 0x0000292C, 0x000041E5, 0x00005BE0,
    0x000041E6, 0x00005F21, 0x000200F9, 0x00004A60, 0x000200F8, 0x00004A60,
    0x000700F5, 0x0000000B, 0x00002C70, 0x00002C67, 0x0000260D, 0x0000292C,
    0x00005BF0, 0x00050080, 0x0000000B, 0x000048BD, 0x00002C70, 0x00005EAC,
    0x000500C2, 0x0000000B, 0x00003D52, 0x000048BD, 0x00000A16, 0x00060041,
    0x00000294, 0x00004FAF, 0x0000107A, 0x00000A0B, 0x00003D52, 0x0004003D,
    0x00000017, 0x00001CAA, 0x00004FAF, 0x000500AA, 0x00000009, 0x000035C0,
    0x000061E2, 0x00000A0D, 0x000500AA, 0x00000009, 0x00005376, 0x000061E2,
    0x00000A10, 0x000500A6, 0x00000009, 0x00005686, 0x000035C0, 0x00005376,
    0x000300F7, 0x00003463, 0x00000000, 0x000400FA, 0x00005686, 0x00002957,
    0x00003463, 0x000200F8, 0x00002957, 0x000500C7, 0x00000017, 0x0000475F,
    0x00001CAA, 0x000009CE, 0x000500C4, 0x00000017, 0x000024D1, 0x0000475F,
    0x0000013D, 0x000500C7, 0x00000017, 0x000050AC, 0x00001CAA, 0x0000072E,
    0x000500C2, 0x00000017, 0x0000448D, 0x000050AC, 0x0000013D, 0x000500C5,
    0x00000017, 0x00003FF8, 0x000024D1, 0x0000448D, 0x000200F9, 0x00003463,
    0x000200F8, 0x00003463, 0x000700F5, 0x00000017, 0x00005879, 0x00001CAA,
    0x00004A60, 0x00003FF8, 0x00002957, 0x000500AA, 0x00000009, 0x00004CB6,
    0x000061E2, 0x00000A13, 0x000500A6, 0x00000009, 0x00003B23, 0x00005376,
    0x00004CB6, 0x000300F7, 0x00003450, 0x00000000, 0x000400FA, 0x00003B23,
    0x00002B38, 0x00003450, 0x000200F8, 0x00002B38, 0x000500C4, 0x00000017,
    0x00005E17, 0x00005879, 0x000002ED, 0x000500C2, 0x00000017, 0x00003BE7,
    0x00005879, 0x000002ED, 0x000500C5, 0x00000017, 0x000029E8, 0x00005E17,
    0x00003BE7, 0x000200F9, 0x00003450, 0x000200F8, 0x00003450, 0x000700F5,
    0x00000017, 0x00005654, 0x00005879, 0x00003463, 0x000029E8, 0x00002B38,
    0x000600A9, 0x0000000B, 0x00002E64, 0x00004376, 0x00000A10, 0x00000A0D,
    0x00050080, 0x0000000B, 0x00002C4B, 0x00003D52, 0x00002E64, 0x00060041,
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
    0x000027F1, 0x0003003E, 0x000051EE, 0x000048D8, 0x00050051, 0x0000000B,
    0x00003220, 0x000043C0, 0x00000001, 0x00050080, 0x0000000B, 0x00005AC0,
    0x00003220, 0x00000A0E, 0x000500B0, 0x00000009, 0x00004411, 0x00005AC0,
    0x000019C2, 0x000300F7, 0x00001C26, 0x00000002, 0x000400FA, 0x00004411,
    0x0000592C, 0x00001C26, 0x000200F8, 0x0000592C, 0x00050080, 0x0000000B,
    0x00003CD8, 0x000027F1, 0x00001F25, 0x000500C2, 0x00000017, 0x00003458,
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
    0x0000000B, 0x000035F4, 0x00003CD8, 0x00001F25, 0x00050051, 0x0000000B,
    0x00002830, 0x00005654, 0x00000001, 0x00050051, 0x0000000B, 0x00002744,
    0x00005654, 0x00000003, 0x00050051, 0x0000000B, 0x00001DDA, 0x000043D0,
    0x00000001, 0x00050051, 0x0000000B, 0x0000275C, 0x000043D0, 0x00000003,
    0x00070050, 0x00000017, 0x00004171, 0x00002830, 0x00002744, 0x00001DDA,
    0x0000275C, 0x000500C7, 0x00000017, 0x00001FCE, 0x00004171, 0x000002B7,
    0x000500C7, 0x00000017, 0x00004211, 0x00004171, 0x0000064B, 0x000500C4,
    0x00000017, 0x00006166, 0x00004211, 0x00000065, 0x000500C5, 0x00000017,
    0x000042C1, 0x00001FCE, 0x00006166, 0x000500C7, 0x00000017, 0x00005188,
    0x00004171, 0x00000192, 0x000500C4, 0x00000017, 0x00005B1B, 0x00005188,
    0x0000013D, 0x000500C5, 0x00000017, 0x000042C2, 0x000042C1, 0x00005B1B,
    0x000500C7, 0x00000017, 0x00005189, 0x00004171, 0x00000857, 0x000500C4,
    0x00000017, 0x00005B1C, 0x00005189, 0x00000215, 0x000500C5, 0x00000017,
    0x000042C3, 0x000042C2, 0x00005B1C, 0x000500C7, 0x00000017, 0x0000518A,
    0x00004171, 0x000009E6, 0x000500C4, 0x00000017, 0x00005127, 0x0000518A,
    0x000002ED, 0x000500C5, 0x00000017, 0x000048DA, 0x000042C3, 0x00005127,
    0x00060041, 0x00000294, 0x0000556C, 0x0000140E, 0x00000A0B, 0x000035F4,
    0x0003003E, 0x0000556C, 0x000048DA, 0x00050080, 0x0000000B, 0x000039F9,
    0x00003220, 0x00000A14, 0x000500B0, 0x00000009, 0x00002E0C, 0x000039F9,
    0x000019C2, 0x000300F7, 0x00004665, 0x00000002, 0x000400FA, 0x00002E0C,
    0x0000592D, 0x00004665, 0x000200F8, 0x0000592D, 0x00050080, 0x0000000B,
    0x00003CD9, 0x000035F4, 0x00001F25, 0x000500C2, 0x00000017, 0x00003459,
    0x00004171, 0x000002ED, 0x000500C7, 0x00000017, 0x00002CA9, 0x00003459,
    0x000002B7, 0x000500C7, 0x00000017, 0x00004944, 0x00003459, 0x0000064B,
    0x000500C4, 0x00000017, 0x00006167, 0x00004944, 0x00000065, 0x000500C5,
    0x00000017, 0x000042C4, 0x00002CA9, 0x00006167, 0x000500C7, 0x00000017,
    0x0000518B, 0x00003459, 0x00000192, 0x000500C4, 0x00000017, 0x00005B1D,
    0x0000518B, 0x0000013D, 0x000500C5, 0x00000017, 0x000042C5, 0x000042C4,
    0x00005B1D, 0x000500C7, 0x00000017, 0x0000518C, 0x00003459, 0x00000857,
    0x000500C4, 0x00000017, 0x00005B1E, 0x0000518C, 0x00000215, 0x000500C5,
    0x00000017, 0x000042C6, 0x000042C5, 0x00005B1E, 0x000500C7, 0x00000017,
    0x0000518D, 0x00003459, 0x000009E6, 0x000500C4, 0x00000017, 0x00005128,
    0x0000518D, 0x000002ED, 0x000500C5, 0x00000017, 0x000048DB, 0x000042C6,
    0x00005128, 0x00060041, 0x00000294, 0x00005E66, 0x0000140E, 0x00000A0B,
    0x00003CD9, 0x0003003E, 0x00005E66, 0x000048DB, 0x000200F9, 0x00004665,
    0x000200F8, 0x00004665, 0x000200F9, 0x00001C25, 0x000200F8, 0x00001C25,
    0x000200F9, 0x00001C26, 0x000200F8, 0x00001C26, 0x000200F9, 0x00003A37,
    0x000200F8, 0x00003A37, 0x000100FD, 0x00010038,
};
