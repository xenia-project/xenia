// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 24608
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %3080 %5914 %4693
               OpExecutionMode %5663 OriginUpperLeft
               OpDecorate %3080 Location 0
               OpDecorate %5914 Location 1
               OpDecorate %5818 Binding 0
               OpDecorate %5818 DescriptorSet 0
               OpDecorate %4693 Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %3080 = OpVariable %_ptr_Output_v4float Output
%_ptr_Input_v4float = OpTypePointer Input %v4float
       %5914 = OpVariable %_ptr_Input_v4float Input
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
        %510 = OpTypeSampledImage %150
%_ptr_UniformConstant_510 = OpTypePointer UniformConstant %510
       %5818 = OpVariable %_ptr_UniformConstant_510 UniformConstant
    %v2float = OpTypeVector %float 2
%_ptr_Input_v2float = OpTypePointer Input %v2float
       %4693 = OpVariable %_ptr_Input_v2float Input
    %float_0 = OpConstant %float 0
       %5663 = OpFunction %void None %1282
      %24607 = OpLabel
      %20754 = OpLoad %v4float %5914
      %24285 = OpLoad %510 %5818
       %8179 = OpLoad %v2float %4693
       %6686 = OpImageSampleExplicitLod %v4float %24285 %8179 Lod %float_0
       %8939 = OpFMul %v4float %20754 %6686
               OpStore %3080 %8939
               OpReturn
               OpFunctionEnd
#endif

const uint32_t immediate_ps[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006020, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0008000F, 0x00000004,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000C08, 0x0000171A, 0x00001255,
    0x00030010, 0x0000161F, 0x00000007, 0x00040047, 0x00000C08, 0x0000001E,
    0x00000000, 0x00040047, 0x0000171A, 0x0000001E, 0x00000001, 0x00040047,
    0x000016BA, 0x00000021, 0x00000000, 0x00040047, 0x000016BA, 0x00000022,
    0x00000000, 0x00040047, 0x00001255, 0x0000001E, 0x00000000, 0x00020013,
    0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00030016, 0x0000000D,
    0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x00040020,
    0x0000029A, 0x00000003, 0x0000001D, 0x0004003B, 0x0000029A, 0x00000C08,
    0x00000003, 0x00040020, 0x0000029B, 0x00000001, 0x0000001D, 0x0004003B,
    0x0000029B, 0x0000171A, 0x00000001, 0x00090019, 0x00000096, 0x0000000D,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x0003001B, 0x000001FE, 0x00000096, 0x00040020, 0x0000047B, 0x00000000,
    0x000001FE, 0x0004003B, 0x0000047B, 0x000016BA, 0x00000000, 0x00040017,
    0x00000013, 0x0000000D, 0x00000002, 0x00040020, 0x00000290, 0x00000001,
    0x00000013, 0x0004003B, 0x00000290, 0x00001255, 0x00000001, 0x0004002B,
    0x0000000D, 0x00000A0C, 0x00000000, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x0000601F, 0x0004003D, 0x0000001D,
    0x00005112, 0x0000171A, 0x0004003D, 0x000001FE, 0x00005EDD, 0x000016BA,
    0x0004003D, 0x00000013, 0x00001FF3, 0x00001255, 0x00070058, 0x0000001D,
    0x00001A1E, 0x00005EDD, 0x00001FF3, 0x00000002, 0x00000A0C, 0x00050085,
    0x0000001D, 0x000022EB, 0x00005112, 0x00001A1E, 0x0003003E, 0x00000C08,
    0x000022EB, 0x000100FD, 0x00010038,
};
