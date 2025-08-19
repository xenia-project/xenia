// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 24950
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %gl_FragCoord %5312
               OpExecutionMode %5663 OriginUpperLeft
               OpDecorate %gl_FragCoord BuiltIn FragCoord
               OpDecorate %_struct_1028 Block
               OpMemberDecorate %_struct_1028 0 Offset 16
               OpMemberDecorate %_struct_1028 1 Offset 24
               OpDecorate %4448 Binding 0
               OpDecorate %4448 DescriptorSet 0
               OpDecorate %4927 Binding 1
               OpDecorate %4927 DescriptorSet 0
               OpDecorate %5312 Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Input_v4float = OpTypePointer Input %v4float
%gl_FragCoord = OpVariable %_ptr_Input_v4float Input
    %v2float = OpTypeVector %float 2
        %int = OpTypeInt 32 1
      %v2int = OpTypeVector %int 2
%_struct_1028 = OpTypeStruct %v2int %v2float
%_ptr_PushConstant__struct_1028 = OpTypePointer PushConstant %_struct_1028
       %3305 = OpVariable %_ptr_PushConstant__struct_1028 PushConstant
      %int_0 = OpConstant %int 0
%_ptr_PushConstant_v2int = OpTypePointer PushConstant %v2int
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
%_ptr_UniformConstant_150 = OpTypePointer UniformConstant %150
       %4448 = OpVariable %_ptr_UniformConstant_150 UniformConstant
        %508 = OpTypeSampler
%_ptr_UniformConstant_508 = OpTypePointer UniformConstant %508
       %4927 = OpVariable %_ptr_UniformConstant_508 UniformConstant
        %510 = OpTypeSampledImage %150
  %float_0_5 = OpConstant %float 0.5
      %int_1 = OpConstant %int 1
%_ptr_PushConstant_v2float = OpTypePointer PushConstant %v2float
    %float_0 = OpConstant %float 0
    %float_1 = OpConstant %float 1
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %5312 = OpVariable %_ptr_Output_v4float Output
       %1566 = OpConstantComposite %v2float %float_0_5 %float_0_5
       %5663 = OpFunction %void None %1282
      %24949 = OpLabel
      %18571 = OpLoad %v4float %gl_FragCoord
      %14008 = OpVectorShuffle %v2float %18571 %18571 0 1
      %17656 = OpConvertFToS %v2int %14008
      %19279 = OpAccessChain %_ptr_PushConstant_v2int %3305 %int_0
      %22822 = OpLoad %v2int %19279
      %23236 = OpISub %v2int %17656 %22822
      %10630 = OpBitcast %v2uint %23236
      %14905 = OpLoad %150 %4448
      %16965 = OpLoad %508 %4927
       %8907 = OpSampledImage %510 %14905 %16965
      %13759 = OpConvertUToF %v2float %10630
      %15917 = OpFAdd %v2float %13759 %1566
      %11863 = OpAccessChain %_ptr_PushConstant_v2float %3305 %int_1
      %20800 = OpLoad %v2float %11863
      %24336 = OpFMul %v2float %15917 %20800
       %9229 = OpImageSampleExplicitLod %v4float %8907 %24336 Lod %float_0
      %21727 = OpCompositeExtract %float %9229 0
      %22672 = OpCompositeExtract %float %9229 1
       %7472 = OpCompositeExtract %float %9229 2
      %22408 = OpCompositeConstruct %v4float %21727 %22672 %7472 %float_1
               OpStore %5312 %22408
               OpReturn
               OpFunctionEnd
#endif

const uint32_t guest_output_bilinear_ps[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006176, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000004,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000C93, 0x000014C0, 0x00030010,
    0x0000161F, 0x00000007, 0x00040047, 0x00000C93, 0x0000000B, 0x0000000F,
    0x00030047, 0x00000404, 0x00000002, 0x00050048, 0x00000404, 0x00000000,
    0x00000023, 0x00000010, 0x00050048, 0x00000404, 0x00000001, 0x00000023,
    0x00000018, 0x00040047, 0x00001160, 0x00000021, 0x00000000, 0x00040047,
    0x00001160, 0x00000022, 0x00000000, 0x00040047, 0x0000133F, 0x00000021,
    0x00000001, 0x00040047, 0x0000133F, 0x00000022, 0x00000000, 0x00040047,
    0x000014C0, 0x0000001E, 0x00000000, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00030016, 0x0000000D,
    0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x00040020,
    0x0000029A, 0x00000001, 0x0000001D, 0x0004003B, 0x0000029A, 0x00000C93,
    0x00000001, 0x00040017, 0x00000013, 0x0000000D, 0x00000002, 0x00040015,
    0x0000000C, 0x00000020, 0x00000001, 0x00040017, 0x00000012, 0x0000000C,
    0x00000002, 0x0004001E, 0x00000404, 0x00000012, 0x00000013, 0x00040020,
    0x00000681, 0x00000009, 0x00000404, 0x0004003B, 0x00000681, 0x00000CE9,
    0x00000009, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040020,
    0x0000028F, 0x00000009, 0x00000012, 0x00090019, 0x00000096, 0x0000000D,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x00040020, 0x00000313, 0x00000000, 0x00000096, 0x0004003B, 0x00000313,
    0x00001160, 0x00000000, 0x0002001A, 0x000001FC, 0x00040020, 0x00000479,
    0x00000000, 0x000001FC, 0x0004003B, 0x00000479, 0x0000133F, 0x00000000,
    0x0003001B, 0x000001FE, 0x00000096, 0x0004002B, 0x0000000D, 0x000000FC,
    0x3F000000, 0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001, 0x00040020,
    0x00000290, 0x00000009, 0x00000013, 0x0004002B, 0x0000000D, 0x00000A0C,
    0x00000000, 0x0004002B, 0x0000000D, 0x0000008A, 0x3F800000, 0x00040020,
    0x0000029B, 0x00000003, 0x0000001D, 0x0004003B, 0x0000029B, 0x000014C0,
    0x00000003, 0x0005002C, 0x00000013, 0x0000061E, 0x000000FC, 0x000000FC,
    0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8,
    0x00006175, 0x0004003D, 0x0000001D, 0x0000488B, 0x00000C93, 0x0007004F,
    0x00000013, 0x000036B8, 0x0000488B, 0x0000488B, 0x00000000, 0x00000001,
    0x0004006E, 0x00000012, 0x000044F8, 0x000036B8, 0x00050041, 0x0000028F,
    0x00004B4F, 0x00000CE9, 0x00000A0B, 0x0004003D, 0x00000012, 0x00005926,
    0x00004B4F, 0x00050082, 0x00000012, 0x00005AC4, 0x000044F8, 0x00005926,
    0x0004007C, 0x00000011, 0x00002986, 0x00005AC4, 0x0004003D, 0x00000096,
    0x00003A39, 0x00001160, 0x0004003D, 0x000001FC, 0x00004245, 0x0000133F,
    0x00050056, 0x000001FE, 0x000022CB, 0x00003A39, 0x00004245, 0x00040070,
    0x00000013, 0x000035BF, 0x00002986, 0x00050081, 0x00000013, 0x00003E2D,
    0x000035BF, 0x0000061E, 0x00050041, 0x00000290, 0x00002E57, 0x00000CE9,
    0x00000A0E, 0x0004003D, 0x00000013, 0x00005140, 0x00002E57, 0x00050085,
    0x00000013, 0x00005F10, 0x00003E2D, 0x00005140, 0x00070058, 0x0000001D,
    0x0000240D, 0x000022CB, 0x00005F10, 0x00000002, 0x00000A0C, 0x00050051,
    0x0000000D, 0x000054DF, 0x0000240D, 0x00000000, 0x00050051, 0x0000000D,
    0x00005890, 0x0000240D, 0x00000001, 0x00050051, 0x0000000D, 0x00001D30,
    0x0000240D, 0x00000002, 0x00070050, 0x0000001D, 0x00005788, 0x000054DF,
    0x00005890, 0x00001D30, 0x0000008A, 0x0003003E, 0x000014C0, 0x00005788,
    0x000100FD, 0x00010038,
};
