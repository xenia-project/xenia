// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24950
; Schema: 0
               OpCapability Shader
               OpCapability SampledBuffer
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %gl_FragCoord %3258
               OpExecutionMode %5663 OriginUpperLeft
               OpDecorate %gl_FragCoord BuiltIn FragCoord
               OpDecorate %5759 DescriptorSet 1
               OpDecorate %5759 Binding 0
               OpDecorate %5945 DescriptorSet 0
               OpDecorate %5945 Binding 0
               OpDecorate %3258 Location 0
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
       %5759 = OpVariable %_ptr_UniformConstant_150 UniformConstant
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
      %int_0 = OpConstant %int 0
    %v3float = OpTypeVector %float 3
  %float_255 = OpConstant %float 255
  %float_0_5 = OpConstant %float 0.5
        %154 = OpTypeImage %float Buffer 0 0 0 1 Unknown
%_ptr_UniformConstant_154 = OpTypePointer UniformConstant %154
       %5945 = OpVariable %_ptr_UniformConstant_154 UniformConstant
%float_0_298999995 = OpConstant %float 0.298999995
%float_0_587000012 = OpConstant %float 0.587000012
%float_0_114 = OpConstant %float 0.114
       %1268 = OpConstantComposite %v3float %float_0_298999995 %float_0_587000012 %float_0_114
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %3258 = OpVariable %_ptr_Output_v4float Output
      %10264 = OpUndef %v4float
        %939 = OpConstantComposite %v3float %float_0_5 %float_0_5 %float_0_5
       %5663 = OpFunction %void None %1282
      %24949 = OpLabel
      %18552 = OpLoad %v4float %gl_FragCoord
      %14105 = OpVectorShuffle %v2float %18552 %18552 0 1
       %8667 = OpConvertFToU %v2uint %14105
      %21665 = OpLoad %150 %5759
      %11127 = OpBitcast %v2int %8667
       %6680 = OpImageFetch %v4float %21665 %11127 Lod %int_0
      %16242 = OpVectorShuffle %v3float %6680 %6680 0 1 2
      %13907 = OpVectorTimesScalar %v3float %16242 %float_255
      %16889 = OpFAdd %v3float %13907 %939
      %11099 = OpConvertFToU %v3uint %16889
      %18624 = OpLoad %154 %5945
      %15435 = OpCompositeExtract %uint %11099 0
      %24686 = OpBitcast %int %15435
       %8410 = OpImageFetch %v4float %18624 %24686
       %9324 = OpCompositeExtract %float %8410 2
      %17732 = OpCompositeInsert %v4float %9324 %10264 0
      %12852 = OpCompositeExtract %uint %11099 1
      %12866 = OpBitcast %int %12852
       %8411 = OpImageFetch %v4float %18624 %12866
       %9325 = OpCompositeExtract %float %8411 1
      %17733 = OpCompositeInsert %v4float %9325 %17732 1
      %12853 = OpCompositeExtract %uint %11099 2
      %12867 = OpBitcast %int %12853
       %8412 = OpImageFetch %v4float %18624 %12867
       %9286 = OpCompositeExtract %float %8412 0
      %18534 = OpCompositeInsert %v4float %9286 %17733 2
      %24839 = OpVectorShuffle %v3float %18534 %18534 0 1 2
       %9330 = OpDot %float %24839 %1268
      %24368 = OpCompositeInsert %v4float %9330 %18534 3
               OpStore %3258 %24368
               OpReturn
               OpFunctionEnd
#endif

const uint32_t apply_gamma_table_fxaa_luma_ps[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006176, 0x00000000, 0x00020011,
    0x00000001, 0x00020011, 0x0000002E, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0007000F, 0x00000004, 0x0000161F, 0x6E69616D, 0x00000000, 0x00000C93,
    0x00000CBA, 0x00030010, 0x0000161F, 0x00000007, 0x00040047, 0x00000C93,
    0x0000000B, 0x0000000F, 0x00040047, 0x0000167F, 0x00000022, 0x00000001,
    0x00040047, 0x0000167F, 0x00000021, 0x00000000, 0x00040047, 0x00001739,
    0x00000022, 0x00000000, 0x00040047, 0x00001739, 0x00000021, 0x00000000,
    0x00040047, 0x00000CBA, 0x0000001E, 0x00000000, 0x00020013, 0x00000008,
    0x00030021, 0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020,
    0x00000000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004,
    0x00040020, 0x0000029A, 0x00000001, 0x0000001D, 0x0004003B, 0x0000029A,
    0x00000C93, 0x00000001, 0x00040017, 0x00000013, 0x0000000D, 0x00000002,
    0x00040017, 0x00000014, 0x0000000B, 0x00000003, 0x00090019, 0x00000096,
    0x0000000D, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x00000000, 0x00040020, 0x00000313, 0x00000000, 0x00000096, 0x0004003B,
    0x00000313, 0x0000167F, 0x00000000, 0x00040015, 0x0000000C, 0x00000020,
    0x00000001, 0x00040017, 0x00000012, 0x0000000C, 0x00000002, 0x0004002B,
    0x0000000C, 0x00000A0B, 0x00000000, 0x00040017, 0x00000018, 0x0000000D,
    0x00000003, 0x0004002B, 0x0000000D, 0x00000540, 0x437F0000, 0x0004002B,
    0x0000000D, 0x000000FC, 0x3F000000, 0x00090019, 0x0000009A, 0x0000000D,
    0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x00040020, 0x00000317, 0x00000000, 0x0000009A, 0x0004003B, 0x00000317,
    0x00001739, 0x00000000, 0x0004002B, 0x0000000D, 0x00000351, 0x3E991687,
    0x0004002B, 0x0000000D, 0x00000458, 0x3F1645A2, 0x0004002B, 0x0000000D,
    0x000001DC, 0x3DE978D5, 0x0006002C, 0x00000018, 0x000004F4, 0x00000351,
    0x00000458, 0x000001DC, 0x00040020, 0x0000029B, 0x00000003, 0x0000001D,
    0x0004003B, 0x0000029B, 0x00000CBA, 0x00000003, 0x00030001, 0x0000001D,
    0x00002818, 0x0006002C, 0x00000018, 0x000003AB, 0x000000FC, 0x000000FC,
    0x000000FC, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x00006175, 0x0004003D, 0x0000001D, 0x00004878, 0x00000C93,
    0x0007004F, 0x00000013, 0x00003719, 0x00004878, 0x00004878, 0x00000000,
    0x00000001, 0x0004006D, 0x00000011, 0x000021DB, 0x00003719, 0x0004003D,
    0x00000096, 0x000054A1, 0x0000167F, 0x0004007C, 0x00000012, 0x00002B77,
    0x000021DB, 0x0007005F, 0x0000001D, 0x00001A18, 0x000054A1, 0x00002B77,
    0x00000002, 0x00000A0B, 0x0008004F, 0x00000018, 0x00003F72, 0x00001A18,
    0x00001A18, 0x00000000, 0x00000001, 0x00000002, 0x0005008E, 0x00000018,
    0x00003653, 0x00003F72, 0x00000540, 0x00050081, 0x00000018, 0x000041F9,
    0x00003653, 0x000003AB, 0x0004006D, 0x00000014, 0x00002B5B, 0x000041F9,
    0x0004003D, 0x0000009A, 0x000048C0, 0x00001739, 0x00050051, 0x0000000B,
    0x00003C4B, 0x00002B5B, 0x00000000, 0x0004007C, 0x0000000C, 0x0000606E,
    0x00003C4B, 0x0005005F, 0x0000001D, 0x000020DA, 0x000048C0, 0x0000606E,
    0x00050051, 0x0000000D, 0x0000246C, 0x000020DA, 0x00000002, 0x00060052,
    0x0000001D, 0x00004544, 0x0000246C, 0x00002818, 0x00000000, 0x00050051,
    0x0000000B, 0x00003234, 0x00002B5B, 0x00000001, 0x0004007C, 0x0000000C,
    0x00003242, 0x00003234, 0x0005005F, 0x0000001D, 0x000020DB, 0x000048C0,
    0x00003242, 0x00050051, 0x0000000D, 0x0000246D, 0x000020DB, 0x00000001,
    0x00060052, 0x0000001D, 0x00004545, 0x0000246D, 0x00004544, 0x00000001,
    0x00050051, 0x0000000B, 0x00003235, 0x00002B5B, 0x00000002, 0x0004007C,
    0x0000000C, 0x00003243, 0x00003235, 0x0005005F, 0x0000001D, 0x000020DC,
    0x000048C0, 0x00003243, 0x00050051, 0x0000000D, 0x00002446, 0x000020DC,
    0x00000000, 0x00060052, 0x0000001D, 0x00004866, 0x00002446, 0x00004545,
    0x00000002, 0x0008004F, 0x00000018, 0x00006107, 0x00004866, 0x00004866,
    0x00000000, 0x00000001, 0x00000002, 0x00050094, 0x0000000D, 0x00002472,
    0x00006107, 0x000004F4, 0x00060052, 0x0000001D, 0x00005F30, 0x00002472,
    0x00004866, 0x00000003, 0x0003003E, 0x00000CBA, 0x00005F30, 0x000100FD,
    0x00010038,
};
