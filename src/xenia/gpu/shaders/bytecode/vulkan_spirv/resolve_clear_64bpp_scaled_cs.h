// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 25175
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
               OpMemberName %push_const_block_xe 0 "xe_resolve_clear_value"
               OpMemberName %push_const_block_xe 1 "xe_resolve_edram_info"
               OpMemberName %push_const_block_xe 2 "xe_resolve_coordinate_info"
               OpName %push_consts_xe "push_consts_xe"
               OpName %gl_GlobalInvocationID "gl_GlobalInvocationID"
               OpName %xe_resolve_edram_xe_block "xe_resolve_edram_xe_block"
               OpMemberName %xe_resolve_edram_xe_block 0 "data"
               OpName %xe_resolve_edram "xe_resolve_edram"
               OpDecorate %push_const_block_xe Block
               OpMemberDecorate %push_const_block_xe 0 Offset 0
               OpMemberDecorate %push_const_block_xe 1 Offset 8
               OpMemberDecorate %push_const_block_xe 2 Offset 12
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpDecorate %xe_resolve_edram_xe_block BufferBlock
               OpMemberDecorate %xe_resolve_edram_xe_block 0 NonReadable
               OpMemberDecorate %xe_resolve_edram_xe_block 0 Offset 0
               OpDecorate %xe_resolve_edram NonReadable
               OpDecorate %xe_resolve_edram Binding 0
               OpDecorate %xe_resolve_edram DescriptorSet 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
       %bool = OpTypeBool
     %uint_2 = OpConstant %uint 2
     %uint_1 = OpConstant %uint 1
       %1837 = OpConstantComposite %v2uint %uint_2 %uint_1
     %v2bool = OpTypeVector %bool 2
     %uint_0 = OpConstant %uint 0
       %1807 = OpConstantComposite %v2uint %uint_0 %uint_0
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %1816 = OpConstantComposite %v2uint %uint_1 %uint_0
    %uint_80 = OpConstant %uint 80
    %uint_16 = OpConstant %uint 16
       %2719 = OpConstantComposite %v2uint %uint_80 %uint_16
        %int = OpTypeInt 32 1
  %uint_2048 = OpConstant %uint 2048
%push_const_block_xe = OpTypeStruct %v2uint %uint %uint
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
      %int_1 = OpConstant %int 1
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
      %int_2 = OpConstant %int 2
      %int_0 = OpConstant %int 0
  %uint_1023 = OpConstant %uint 1023
    %uint_10 = OpConstant %uint 10
     %uint_3 = OpConstant %uint 3
    %uint_13 = OpConstant %uint 13
  %uint_2047 = OpConstant %uint 2047
    %uint_15 = OpConstant %uint 15
    %uint_19 = OpConstant %uint 19
       %2179 = OpConstantComposite %v2uint %uint_16 %uint_19
     %uint_7 = OpConstant %uint 7
     %uint_4 = OpConstant %uint 4
       %1855 = OpConstantComposite %v2uint %uint_0 %uint_4
     %uint_5 = OpConstant %uint 5
%_ptr_PushConstant_v2uint = OpTypePointer PushConstant %v2uint
     %v3uint = OpTypeVector %uint 3
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%_ptr_Input_uint = OpTypePointer Input %uint
       %1834 = OpConstantComposite %v2uint %uint_3 %uint_0
     %v4uint = OpTypeVector %uint 4
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%xe_resolve_edram_xe_block = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform_xe_resolve_edram_xe_block = OpTypePointer Uniform %xe_resolve_edram_xe_block
%xe_resolve_edram = OpVariable %_ptr_Uniform_xe_resolve_edram_xe_block Uniform
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
     %uint_8 = OpConstant %uint 8
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %1954 = OpConstantComposite %v2uint %uint_7 %uint_7
       %1955 = OpConstantComposite %v2uint %uint_15 %uint_1
       %1870 = OpConstantComposite %v2uint %uint_3 %uint_3
       %main = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %19578 None
               OpSwitch %uint_0 %11880
      %11880 = OpLabel
      %22245 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_1
      %15627 = OpLoad %uint %22245
      %22700 = OpAccessChain %_ptr_PushConstant_uint %push_consts_xe %int_2
      %20824 = OpLoad %uint %22700
      %20561 = OpBitwiseAnd %uint %15627 %uint_1023
      %19978 = OpShiftRightLogical %uint %15627 %uint_10
       %8574 = OpBitwiseAnd %uint %19978 %uint_3
      %18836 = OpShiftRightLogical %uint %15627 %uint_13
       %9130 = OpBitwiseAnd %uint %18836 %uint_2047
       %8871 = OpCompositeConstruct %v2uint %20824 %20824
       %9538 = OpShiftRightLogical %v2uint %8871 %2179
      %24998 = OpBitwiseAnd %v2uint %9538 %1954
      %21040 = OpShiftRightLogical %v2uint %8871 %1855
       %6955 = OpBitwiseAnd %v2uint %21040 %1955
      %16207 = OpShiftLeftLogical %v2uint %6955 %1870
      %23019 = OpIMul %v2uint %16207 %24998
      %13123 = OpShiftRightLogical %uint %20824 %uint_5
      %14785 = OpBitwiseAnd %uint %13123 %uint_2047
       %8858 = OpCompositeExtract %uint %24998 0
      %22993 = OpIMul %uint %14785 %8858
      %20321 = OpAccessChain %_ptr_PushConstant_v2uint %push_consts_xe %int_0
      %18180 = OpLoad %v2uint %20321
      %13183 = OpCompositeConstruct %v2uint %8574 %8574
      %21741 = OpUGreaterThanEqual %v2bool %13183 %1837
      %22612 = OpSelect %v2uint %21741 %1828 %1807
      %23890 = OpAccessChain %_ptr_Input_uint %gl_GlobalInvocationID %uint_0
      %19209 = OpLoad %uint %23890
      %20350 = OpCompositeExtract %uint %22612 0
      %15478 = OpShiftLeftLogical %uint %22993 %20350
      %15379 = OpUGreaterThanEqual %bool %19209 %15478
               OpSelectionMerge %17447 DontFlatten
               OpBranchConditional %15379 %21992 %17447
      %21992 = OpLabel
               OpBranch %19578
      %17447 = OpLabel
      %14637 = OpLoad %v3uint %gl_GlobalInvocationID
      %20690 = OpVectorShuffle %v2uint %14637 %14637 0 1
       %9909 = OpShiftLeftLogical %v2uint %20690 %1834
      %23504 = OpShiftLeftLogical %v2uint %23019 %22612
      %10018 = OpIAdd %v2uint %9909 %23504
      %21954 = OpIMul %v2uint %2719 %24998
      %14725 = OpShiftRightLogical %v2uint %21954 %1816
      %19799 = OpUDiv %v2uint %10018 %14725
      %20390 = OpCompositeExtract %uint %19799 1
      %11046 = OpIMul %uint %20390 %20561
      %24665 = OpCompositeExtract %uint %19799 0
      %21536 = OpIAdd %uint %11046 %24665
       %8742 = OpIAdd %uint %9130 %21536
      %22376 = OpIMul %v2uint %19799 %14725
      %20715 = OpISub %v2uint %10018 %22376
       %7303 = OpCompositeExtract %uint %21954 0
      %22882 = OpCompositeExtract %uint %21954 1
      %13170 = OpIMul %uint %7303 %22882
      %14551 = OpIMul %uint %8742 %13170
       %6805 = OpCompositeExtract %uint %20715 1
      %23526 = OpCompositeExtract %uint %14725 0
      %22886 = OpIMul %uint %6805 %23526
       %6886 = OpCompositeExtract %uint %20715 0
       %9696 = OpIAdd %uint %22886 %6886
      %18021 = OpShiftLeftLogical %uint %9696 %uint_1
      %18363 = OpIAdd %uint %14551 %18021
      %13884 = OpIMul %uint %13170 %uint_2048
      %20061 = OpUMod %uint %18363 %13884
      %19460 = OpShiftRightLogical %uint %20061 %uint_2
      %14952 = OpVectorShuffle %v4uint %18180 %18180 0 1 0 1
       %7737 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_edram %int_0 %19460
               OpStore %7737 %14952
      %11457 = OpIAdd %uint %19460 %uint_1
      %22875 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_edram %int_0 %11457
               OpStore %22875 %14952
      %11458 = OpIAdd %uint %19460 %uint_2
      %22876 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_edram %int_0 %11458
               OpStore %22876 %14952
      %11459 = OpIAdd %uint %19460 %uint_3
      %25174 = OpAccessChain %_ptr_Uniform_v4uint %xe_resolve_edram %int_0 %11459
               OpStore %25174 %14952
               OpBranch %19578
      %19578 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t resolve_clear_64bpp_scaled_cs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006257, 0x00000000, 0x00020011,
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
    0x00000000, 0x00070005, 0x000003F6, 0x68737570, 0x6E6F635F, 0x625F7473,
    0x6B636F6C, 0x0065785F, 0x00090006, 0x000003F6, 0x00000000, 0x725F6578,
    0x6C6F7365, 0x635F6576, 0x7261656C, 0x6C61765F, 0x00006575, 0x00090006,
    0x000003F6, 0x00000001, 0x725F6578, 0x6C6F7365, 0x655F6576, 0x6D617264,
    0x666E695F, 0x0000006F, 0x000A0006, 0x000003F6, 0x00000002, 0x725F6578,
    0x6C6F7365, 0x635F6576, 0x64726F6F, 0x74616E69, 0x6E695F65, 0x00006F66,
    0x00060005, 0x00000CE9, 0x68737570, 0x6E6F635F, 0x5F737473, 0x00006578,
    0x00080005, 0x00000F48, 0x475F6C67, 0x61626F6C, 0x766E496C, 0x7461636F,
    0x496E6F69, 0x00000044, 0x00090005, 0x000007B4, 0x725F6578, 0x6C6F7365,
    0x655F6576, 0x6D617264, 0x5F65785F, 0x636F6C62, 0x0000006B, 0x00050006,
    0x000007B4, 0x00000000, 0x61746164, 0x00000000, 0x00070005, 0x00000CC7,
    0x725F6578, 0x6C6F7365, 0x655F6576, 0x6D617264, 0x00000000, 0x00030047,
    0x000003F6, 0x00000002, 0x00050048, 0x000003F6, 0x00000000, 0x00000023,
    0x00000000, 0x00050048, 0x000003F6, 0x00000001, 0x00000023, 0x00000008,
    0x00050048, 0x000003F6, 0x00000002, 0x00000023, 0x0000000C, 0x00040047,
    0x00000F48, 0x0000000B, 0x0000001C, 0x00040047, 0x000007DC, 0x00000006,
    0x00000010, 0x00030047, 0x000007B4, 0x00000003, 0x00040048, 0x000007B4,
    0x00000000, 0x00000019, 0x00050048, 0x000007B4, 0x00000000, 0x00000023,
    0x00000000, 0x00030047, 0x00000CC7, 0x00000019, 0x00040047, 0x00000CC7,
    0x00000021, 0x00000000, 0x00040047, 0x00000CC7, 0x00000022, 0x00000000,
    0x00040047, 0x00000AC7, 0x0000000B, 0x00000019, 0x00020013, 0x00000008,
    0x00030021, 0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020,
    0x00000000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00020014,
    0x00000009, 0x0004002B, 0x0000000B, 0x00000A10, 0x00000002, 0x0004002B,
    0x0000000B, 0x00000A0D, 0x00000001, 0x0005002C, 0x00000011, 0x0000072D,
    0x00000A10, 0x00000A0D, 0x00040017, 0x0000000F, 0x00000009, 0x00000002,
    0x0004002B, 0x0000000B, 0x00000A0A, 0x00000000, 0x0005002C, 0x00000011,
    0x0000070F, 0x00000A0A, 0x00000A0A, 0x0005002C, 0x00000011, 0x00000724,
    0x00000A0D, 0x00000A0D, 0x0005002C, 0x00000011, 0x00000718, 0x00000A0D,
    0x00000A0A, 0x0004002B, 0x0000000B, 0x00000AFA, 0x00000050, 0x0004002B,
    0x0000000B, 0x00000A3A, 0x00000010, 0x0005002C, 0x00000011, 0x00000A9F,
    0x00000AFA, 0x00000A3A, 0x00040015, 0x0000000C, 0x00000020, 0x00000001,
    0x0004002B, 0x0000000B, 0x00000A84, 0x00000800, 0x0005001E, 0x000003F6,
    0x00000011, 0x0000000B, 0x0000000B, 0x00040020, 0x00000673, 0x00000009,
    0x000003F6, 0x0004003B, 0x00000673, 0x00000CE9, 0x00000009, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x00040020, 0x00000288, 0x00000009,
    0x0000000B, 0x0004002B, 0x0000000C, 0x00000A11, 0x00000002, 0x0004002B,
    0x0000000C, 0x00000A0B, 0x00000000, 0x0004002B, 0x0000000B, 0x00000A44,
    0x000003FF, 0x0004002B, 0x0000000B, 0x00000A28, 0x0000000A, 0x0004002B,
    0x0000000B, 0x00000A13, 0x00000003, 0x0004002B, 0x0000000B, 0x00000A31,
    0x0000000D, 0x0004002B, 0x0000000B, 0x00000A81, 0x000007FF, 0x0004002B,
    0x0000000B, 0x00000A37, 0x0000000F, 0x0004002B, 0x0000000B, 0x00000A43,
    0x00000013, 0x0005002C, 0x00000011, 0x00000883, 0x00000A3A, 0x00000A43,
    0x0004002B, 0x0000000B, 0x00000A1F, 0x00000007, 0x0004002B, 0x0000000B,
    0x00000A16, 0x00000004, 0x0005002C, 0x00000011, 0x0000073F, 0x00000A0A,
    0x00000A16, 0x0004002B, 0x0000000B, 0x00000A19, 0x00000005, 0x00040020,
    0x0000028E, 0x00000009, 0x00000011, 0x00040017, 0x00000014, 0x0000000B,
    0x00000003, 0x00040020, 0x00000291, 0x00000001, 0x00000014, 0x0004003B,
    0x00000291, 0x00000F48, 0x00000001, 0x00040020, 0x00000289, 0x00000001,
    0x0000000B, 0x0005002C, 0x00000011, 0x0000072A, 0x00000A13, 0x00000A0A,
    0x00040017, 0x00000017, 0x0000000B, 0x00000004, 0x0003001D, 0x000007DC,
    0x00000017, 0x0003001E, 0x000007B4, 0x000007DC, 0x00040020, 0x00000A32,
    0x00000002, 0x000007B4, 0x0004003B, 0x00000A32, 0x00000CC7, 0x00000002,
    0x00040020, 0x00000294, 0x00000002, 0x00000017, 0x0004002B, 0x0000000B,
    0x00000A22, 0x00000008, 0x0006002C, 0x00000014, 0x00000AC7, 0x00000A22,
    0x00000A22, 0x00000A0D, 0x0005002C, 0x00000011, 0x000007A2, 0x00000A1F,
    0x00000A1F, 0x0005002C, 0x00000011, 0x000007A3, 0x00000A37, 0x00000A0D,
    0x0005002C, 0x00000011, 0x0000074E, 0x00000A13, 0x00000A13, 0x00050036,
    0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00003B06,
    0x000300F7, 0x00004C7A, 0x00000000, 0x000300FB, 0x00000A0A, 0x00002E68,
    0x000200F8, 0x00002E68, 0x00050041, 0x00000288, 0x000056E5, 0x00000CE9,
    0x00000A0E, 0x0004003D, 0x0000000B, 0x00003D0B, 0x000056E5, 0x00050041,
    0x00000288, 0x000058AC, 0x00000CE9, 0x00000A11, 0x0004003D, 0x0000000B,
    0x00005158, 0x000058AC, 0x000500C7, 0x0000000B, 0x00005051, 0x00003D0B,
    0x00000A44, 0x000500C2, 0x0000000B, 0x00004E0A, 0x00003D0B, 0x00000A28,
    0x000500C7, 0x0000000B, 0x0000217E, 0x00004E0A, 0x00000A13, 0x000500C2,
    0x0000000B, 0x00004994, 0x00003D0B, 0x00000A31, 0x000500C7, 0x0000000B,
    0x000023AA, 0x00004994, 0x00000A81, 0x00050050, 0x00000011, 0x000022A7,
    0x00005158, 0x00005158, 0x000500C2, 0x00000011, 0x00002542, 0x000022A7,
    0x00000883, 0x000500C7, 0x00000011, 0x000061A6, 0x00002542, 0x000007A2,
    0x000500C2, 0x00000011, 0x00005230, 0x000022A7, 0x0000073F, 0x000500C7,
    0x00000011, 0x00001B2B, 0x00005230, 0x000007A3, 0x000500C4, 0x00000011,
    0x00003F4F, 0x00001B2B, 0x0000074E, 0x00050084, 0x00000011, 0x000059EB,
    0x00003F4F, 0x000061A6, 0x000500C2, 0x0000000B, 0x00003343, 0x00005158,
    0x00000A19, 0x000500C7, 0x0000000B, 0x000039C1, 0x00003343, 0x00000A81,
    0x00050051, 0x0000000B, 0x0000229A, 0x000061A6, 0x00000000, 0x00050084,
    0x0000000B, 0x000059D1, 0x000039C1, 0x0000229A, 0x00050041, 0x0000028E,
    0x00004F61, 0x00000CE9, 0x00000A0B, 0x0004003D, 0x00000011, 0x00004704,
    0x00004F61, 0x00050050, 0x00000011, 0x0000337F, 0x0000217E, 0x0000217E,
    0x000500AE, 0x0000000F, 0x000054ED, 0x0000337F, 0x0000072D, 0x000600A9,
    0x00000011, 0x00005854, 0x000054ED, 0x00000724, 0x0000070F, 0x00050041,
    0x00000289, 0x00005D52, 0x00000F48, 0x00000A0A, 0x0004003D, 0x0000000B,
    0x00004B09, 0x00005D52, 0x00050051, 0x0000000B, 0x00004F7E, 0x00005854,
    0x00000000, 0x000500C4, 0x0000000B, 0x00003C76, 0x000059D1, 0x00004F7E,
    0x000500AE, 0x00000009, 0x00003C13, 0x00004B09, 0x00003C76, 0x000300F7,
    0x00004427, 0x00000002, 0x000400FA, 0x00003C13, 0x000055E8, 0x00004427,
    0x000200F8, 0x000055E8, 0x000200F9, 0x00004C7A, 0x000200F8, 0x00004427,
    0x0004003D, 0x00000014, 0x0000392D, 0x00000F48, 0x0007004F, 0x00000011,
    0x000050D2, 0x0000392D, 0x0000392D, 0x00000000, 0x00000001, 0x000500C4,
    0x00000011, 0x000026B5, 0x000050D2, 0x0000072A, 0x000500C4, 0x00000011,
    0x00005BD0, 0x000059EB, 0x00005854, 0x00050080, 0x00000011, 0x00002722,
    0x000026B5, 0x00005BD0, 0x00050084, 0x00000011, 0x000055C2, 0x00000A9F,
    0x000061A6, 0x000500C2, 0x00000011, 0x00003985, 0x000055C2, 0x00000718,
    0x00050086, 0x00000011, 0x00004D57, 0x00002722, 0x00003985, 0x00050051,
    0x0000000B, 0x00004FA6, 0x00004D57, 0x00000001, 0x00050084, 0x0000000B,
    0x00002B26, 0x00004FA6, 0x00005051, 0x00050051, 0x0000000B, 0x00006059,
    0x00004D57, 0x00000000, 0x00050080, 0x0000000B, 0x00005420, 0x00002B26,
    0x00006059, 0x00050080, 0x0000000B, 0x00002226, 0x000023AA, 0x00005420,
    0x00050084, 0x00000011, 0x00005768, 0x00004D57, 0x00003985, 0x00050082,
    0x00000011, 0x000050EB, 0x00002722, 0x00005768, 0x00050051, 0x0000000B,
    0x00001C87, 0x000055C2, 0x00000000, 0x00050051, 0x0000000B, 0x00005962,
    0x000055C2, 0x00000001, 0x00050084, 0x0000000B, 0x00003372, 0x00001C87,
    0x00005962, 0x00050084, 0x0000000B, 0x000038D7, 0x00002226, 0x00003372,
    0x00050051, 0x0000000B, 0x00001A95, 0x000050EB, 0x00000001, 0x00050051,
    0x0000000B, 0x00005BE6, 0x00003985, 0x00000000, 0x00050084, 0x0000000B,
    0x00005966, 0x00001A95, 0x00005BE6, 0x00050051, 0x0000000B, 0x00001AE6,
    0x000050EB, 0x00000000, 0x00050080, 0x0000000B, 0x000025E0, 0x00005966,
    0x00001AE6, 0x000500C4, 0x0000000B, 0x00004665, 0x000025E0, 0x00000A0D,
    0x00050080, 0x0000000B, 0x000047BB, 0x000038D7, 0x00004665, 0x00050084,
    0x0000000B, 0x0000363C, 0x00003372, 0x00000A84, 0x00050089, 0x0000000B,
    0x00004E5D, 0x000047BB, 0x0000363C, 0x000500C2, 0x0000000B, 0x00004C04,
    0x00004E5D, 0x00000A10, 0x0009004F, 0x00000017, 0x00003A68, 0x00004704,
    0x00004704, 0x00000000, 0x00000001, 0x00000000, 0x00000001, 0x00060041,
    0x00000294, 0x00001E39, 0x00000CC7, 0x00000A0B, 0x00004C04, 0x0003003E,
    0x00001E39, 0x00003A68, 0x00050080, 0x0000000B, 0x00002CC1, 0x00004C04,
    0x00000A0D, 0x00060041, 0x00000294, 0x0000595B, 0x00000CC7, 0x00000A0B,
    0x00002CC1, 0x0003003E, 0x0000595B, 0x00003A68, 0x00050080, 0x0000000B,
    0x00002CC2, 0x00004C04, 0x00000A10, 0x00060041, 0x00000294, 0x0000595C,
    0x00000CC7, 0x00000A0B, 0x00002CC2, 0x0003003E, 0x0000595C, 0x00003A68,
    0x00050080, 0x0000000B, 0x00002CC3, 0x00004C04, 0x00000A13, 0x00060041,
    0x00000294, 0x00006256, 0x00000CC7, 0x00000A0B, 0x00002CC3, 0x0003003E,
    0x00006256, 0x00003A68, 0x000200F9, 0x00004C7A, 0x000200F8, 0x00004C7A,
    0x000100FD, 0x00010038,
};
