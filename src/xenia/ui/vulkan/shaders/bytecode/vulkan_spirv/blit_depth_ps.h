// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24608
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %gl_FragDepth %5592 %4841
               OpExecutionMode %5663 OriginUpperLeft
               OpExecutionMode %5663 DepthReplacing
               OpDecorate %gl_FragDepth BuiltIn FragDepth
               OpDecorate %5164 DescriptorSet 0
               OpDecorate %5164 Binding 0
               OpDecorate %5592 Location 0
               OpDecorate %4841 Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
%_ptr_Output_float = OpTypePointer Output %float
%gl_FragDepth = OpVariable %_ptr_Output_float Output
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
        %510 = OpTypeSampledImage %150
%_ptr_UniformConstant_510 = OpTypePointer UniformConstant %510
       %5164 = OpVariable %_ptr_UniformConstant_510 UniformConstant
    %v2float = OpTypeVector %float 2
%_ptr_Input_v2float = OpTypePointer Input %v2float
       %5592 = OpVariable %_ptr_Input_v2float Input
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %4841 = OpVariable %_ptr_Output_v4float Output
       %5663 = OpFunction %void None %1282
      %24607 = OpLabel
      %21248 = OpLoad %510 %5164
      %19654 = OpLoad %v2float %5592
      %23875 = OpImageSampleImplicitLod %v4float %21248 %19654
      %15662 = OpCompositeExtract %float %23875 0
               OpStore %gl_FragDepth %15662
               OpReturn
               OpFunctionEnd
#endif

const uint32_t blit_depth_ps[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006020, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0008000F, 0x00000004,
    0x0000161F, 0x6E69616D, 0x00000000, 0x000011F3, 0x000015D8, 0x000012E9,
    0x00030010, 0x0000161F, 0x00000007, 0x00030010, 0x0000161F, 0x0000000C,
    0x00040047, 0x000011F3, 0x0000000B, 0x00000016, 0x00040047, 0x0000142C,
    0x00000022, 0x00000000, 0x00040047, 0x0000142C, 0x00000021, 0x00000000,
    0x00040047, 0x000015D8, 0x0000001E, 0x00000000, 0x00040047, 0x000012E9,
    0x0000001E, 0x00000000, 0x00020013, 0x00000008, 0x00030021, 0x00000502,
    0x00000008, 0x00030016, 0x0000000D, 0x00000020, 0x00040020, 0x0000028A,
    0x00000003, 0x0000000D, 0x0004003B, 0x0000028A, 0x000011F3, 0x00000003,
    0x00090019, 0x00000096, 0x0000000D, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000001, 0x00000000, 0x0003001B, 0x000001FE, 0x00000096,
    0x00040020, 0x0000047B, 0x00000000, 0x000001FE, 0x0004003B, 0x0000047B,
    0x0000142C, 0x00000000, 0x00040017, 0x00000013, 0x0000000D, 0x00000002,
    0x00040020, 0x00000290, 0x00000001, 0x00000013, 0x0004003B, 0x00000290,
    0x000015D8, 0x00000001, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004,
    0x00040020, 0x0000029A, 0x00000003, 0x0000001D, 0x0004003B, 0x0000029A,
    0x000012E9, 0x00000003, 0x00050036, 0x00000008, 0x0000161F, 0x00000000,
    0x00000502, 0x000200F8, 0x0000601F, 0x0004003D, 0x000001FE, 0x00005300,
    0x0000142C, 0x0004003D, 0x00000013, 0x00004CC6, 0x000015D8, 0x00050057,
    0x0000001D, 0x00005D43, 0x00005300, 0x00004CC6, 0x00050051, 0x0000000D,
    0x00003D2E, 0x00005D43, 0x00000000, 0x0003003E, 0x000011F3, 0x00003D2E,
    0x000100FD, 0x00010038,
};
