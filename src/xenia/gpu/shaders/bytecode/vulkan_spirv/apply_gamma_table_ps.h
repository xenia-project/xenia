// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 24950
; Schema: 0
               OpCapability Shader
               OpCapability SampledBuffer
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %main "main" %gl_FragCoord %xe_apply_gamma_dest
               OpExecutionMode %main OriginUpperLeft
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %gl_FragCoord "gl_FragCoord"
               OpName %xe_apply_gamma_source "xe_apply_gamma_source"
               OpName %xe_apply_gamma_ramp "xe_apply_gamma_ramp"
               OpName %xe_apply_gamma_dest "xe_apply_gamma_dest"
               OpDecorate %gl_FragCoord BuiltIn FragCoord
               OpDecorate %xe_apply_gamma_source Binding 0
               OpDecorate %xe_apply_gamma_source DescriptorSet 1
               OpDecorate %xe_apply_gamma_ramp Binding 0
               OpDecorate %xe_apply_gamma_ramp DescriptorSet 0
               OpDecorate %xe_apply_gamma_dest Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Input_v4float = OpTypePointer Input %v4float
%gl_FragCoord = OpVariable %_ptr_Input_v4float Input
    %v2float = OpTypeVector %float 2
     %v3uint = OpTypeVector %uint 3
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
%_ptr_UniformConstant_150 = OpTypePointer UniformConstant %150
%xe_apply_gamma_source = OpVariable %_ptr_UniformConstant_150 UniformConstant
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %int_0 = OpConstant %int 0
    %v3float = OpTypeVector %float 3
  %float_255 = OpConstant %float 255
  %float_0_5 = OpConstant %float 0.5
        %154 = OpTypeImage %float Buffer 0 0 0 1 Unknown
%_ptr_UniformConstant_154 = OpTypePointer UniformConstant %154
%xe_apply_gamma_ramp = OpVariable %_ptr_UniformConstant_154 UniformConstant
    %float_1 = OpConstant %float 1
%_ptr_Output_v4float = OpTypePointer Output %v4float
%xe_apply_gamma_dest = OpVariable %_ptr_Output_v4float Output
        %939 = OpConstantComposite %v3float %float_0_5 %float_0_5 %float_0_5
       %main = OpFunction %void None %1282
      %24949 = OpLabel
      %18552 = OpLoad %v4float %gl_FragCoord
      %14105 = OpVectorShuffle %v2float %18552 %18552 0 1
       %8667 = OpConvertFToU %v2uint %14105
      %21665 = OpLoad %150 %xe_apply_gamma_source
      %11127 = OpBitcast %v2int %8667
       %6680 = OpImageFetch %v4float %21665 %11127 Lod %int_0
      %16242 = OpVectorShuffle %v3float %6680 %6680 0 1 2
      %13907 = OpVectorTimesScalar %v3float %16242 %float_255
      %16889 = OpFAdd %v3float %13907 %939
      %11099 = OpConvertFToU %v3uint %16889
      %18624 = OpLoad %154 %xe_apply_gamma_ramp
      %15435 = OpCompositeExtract %uint %11099 0
      %24686 = OpBitcast %int %15435
       %8011 = OpImageFetch %v4float %18624 %24686
      %12957 = OpCompositeExtract %float %8011 2
      %18146 = OpLoad %154 %xe_apply_gamma_ramp
      %12224 = OpCompositeExtract %uint %11099 1
      %24687 = OpBitcast %int %12224
       %8012 = OpImageFetch %v4float %18146 %24687
      %12958 = OpCompositeExtract %float %8012 1
      %18147 = OpLoad %154 %xe_apply_gamma_ramp
      %12225 = OpCompositeExtract %uint %11099 2
      %24688 = OpBitcast %int %12225
       %8372 = OpImageFetch %v4float %18147 %24688
       %9309 = OpCompositeExtract %float %8372 0
      %20785 = OpCompositeConstruct %v4float %12957 %12958 %9309 %float_1
               OpStore %xe_apply_gamma_dest %20785
               OpReturn
               OpFunctionEnd
#endif

const uint32_t apply_gamma_table_ps[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006176, 0x00000000, 0x00020011,
    0x00000001, 0x00020011, 0x0000002E, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0007000F, 0x00000004, 0x0000161F, 0x6E69616D, 0x00000000, 0x00000C93,
    0x00000CBA, 0x00030010, 0x0000161F, 0x00000007, 0x00030003, 0x00000002,
    0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F,
    0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47,
    0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572,
    0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x0000161F, 0x6E69616D,
    0x00000000, 0x00060005, 0x00000C93, 0x465F6C67, 0x43676172, 0x64726F6F,
    0x00000000, 0x00080005, 0x0000167F, 0x615F6578, 0x796C7070, 0x6D61675F,
    0x735F616D, 0x6372756F, 0x00000065, 0x00070005, 0x00001739, 0x615F6578,
    0x796C7070, 0x6D61675F, 0x725F616D, 0x00706D61, 0x00070005, 0x00000CBA,
    0x615F6578, 0x796C7070, 0x6D61675F, 0x645F616D, 0x00747365, 0x00040047,
    0x00000C93, 0x0000000B, 0x0000000F, 0x00040047, 0x0000167F, 0x00000021,
    0x00000000, 0x00040047, 0x0000167F, 0x00000022, 0x00000001, 0x00040047,
    0x00001739, 0x00000021, 0x00000000, 0x00040047, 0x00001739, 0x00000022,
    0x00000000, 0x00040047, 0x00000CBA, 0x0000001E, 0x00000000, 0x00020013,
    0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00040015, 0x0000000B,
    0x00000020, 0x00000000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002,
    0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D,
    0x00000004, 0x00040020, 0x0000029A, 0x00000001, 0x0000001D, 0x0004003B,
    0x0000029A, 0x00000C93, 0x00000001, 0x00040017, 0x00000013, 0x0000000D,
    0x00000002, 0x00040017, 0x00000014, 0x0000000B, 0x00000003, 0x00090019,
    0x00000096, 0x0000000D, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000000, 0x00040020, 0x00000313, 0x00000000, 0x00000096,
    0x0004003B, 0x00000313, 0x0000167F, 0x00000000, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x00040017, 0x00000012, 0x0000000C, 0x00000002,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040017, 0x00000018,
    0x0000000D, 0x00000003, 0x0004002B, 0x0000000D, 0x00000540, 0x437F0000,
    0x0004002B, 0x0000000D, 0x000000FC, 0x3F000000, 0x00090019, 0x0000009A,
    0x0000000D, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x00000000, 0x00040020, 0x00000317, 0x00000000, 0x0000009A, 0x0004003B,
    0x00000317, 0x00001739, 0x00000000, 0x0004002B, 0x0000000D, 0x0000008A,
    0x3F800000, 0x00040020, 0x0000029B, 0x00000003, 0x0000001D, 0x0004003B,
    0x0000029B, 0x00000CBA, 0x00000003, 0x0006002C, 0x00000018, 0x000003AB,
    0x000000FC, 0x000000FC, 0x000000FC, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x00006175, 0x0004003D, 0x0000001D,
    0x00004878, 0x00000C93, 0x0007004F, 0x00000013, 0x00003719, 0x00004878,
    0x00004878, 0x00000000, 0x00000001, 0x0004006D, 0x00000011, 0x000021DB,
    0x00003719, 0x0004003D, 0x00000096, 0x000054A1, 0x0000167F, 0x0004007C,
    0x00000012, 0x00002B77, 0x000021DB, 0x0007005F, 0x0000001D, 0x00001A18,
    0x000054A1, 0x00002B77, 0x00000002, 0x00000A0B, 0x0008004F, 0x00000018,
    0x00003F72, 0x00001A18, 0x00001A18, 0x00000000, 0x00000001, 0x00000002,
    0x0005008E, 0x00000018, 0x00003653, 0x00003F72, 0x00000540, 0x00050081,
    0x00000018, 0x000041F9, 0x00003653, 0x000003AB, 0x0004006D, 0x00000014,
    0x00002B5B, 0x000041F9, 0x0004003D, 0x0000009A, 0x000048C0, 0x00001739,
    0x00050051, 0x0000000B, 0x00003C4B, 0x00002B5B, 0x00000000, 0x0004007C,
    0x0000000C, 0x0000606E, 0x00003C4B, 0x0005005F, 0x0000001D, 0x00001F4B,
    0x000048C0, 0x0000606E, 0x00050051, 0x0000000D, 0x0000329D, 0x00001F4B,
    0x00000002, 0x0004003D, 0x0000009A, 0x000046E2, 0x00001739, 0x00050051,
    0x0000000B, 0x00002FC0, 0x00002B5B, 0x00000001, 0x0004007C, 0x0000000C,
    0x0000606F, 0x00002FC0, 0x0005005F, 0x0000001D, 0x00001F4C, 0x000046E2,
    0x0000606F, 0x00050051, 0x0000000D, 0x0000329E, 0x00001F4C, 0x00000001,
    0x0004003D, 0x0000009A, 0x000046E3, 0x00001739, 0x00050051, 0x0000000B,
    0x00002FC1, 0x00002B5B, 0x00000002, 0x0004007C, 0x0000000C, 0x00006070,
    0x00002FC1, 0x0005005F, 0x0000001D, 0x000020B4, 0x000046E3, 0x00006070,
    0x00050051, 0x0000000D, 0x0000245D, 0x000020B4, 0x00000000, 0x00070050,
    0x0000001D, 0x00005131, 0x0000329D, 0x0000329E, 0x0000245D, 0x0000008A,
    0x0003003E, 0x00000CBA, 0x00005131, 0x000100FD, 0x00010038,
};
