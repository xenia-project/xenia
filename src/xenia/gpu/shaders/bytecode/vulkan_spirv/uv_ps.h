// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24988
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %5120 %3877
               OpExecutionMode %5663 OriginUpperLeft
               OpDecorate %5120 RelaxedPrecision
               OpDecorate %5120 Location 0
               OpDecorate %3877 Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %5120 = OpVariable %_ptr_Output_v4float Output
    %v2float = OpTypeVector %float 2
%_ptr_Input_v2float = OpTypePointer Input %v2float
       %3877 = OpVariable %_ptr_Input_v2float Input
    %float_0 = OpConstant %float 0
    %float_1 = OpConstant %float 1
       %5663 = OpFunction %void None %1282
      %24987 = OpLabel
      %17674 = OpLoad %v2float %3877
      %21995 = OpCompositeExtract %float %17674 0
      %23327 = OpCompositeExtract %float %17674 1
      %22408 = OpCompositeConstruct %v4float %21995 %23327 %float_0 %float_1
               OpStore %5120 %22408
               OpReturn
               OpFunctionEnd
#endif

const uint32_t uv_ps[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x0000619C, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000004,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00001400, 0x00000F25, 0x00030010,
    0x0000161F, 0x00000007, 0x00030047, 0x00001400, 0x00000000, 0x00040047,
    0x00001400, 0x0000001E, 0x00000000, 0x00040047, 0x00000F25, 0x0000001E,
    0x00000000, 0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008,
    0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D,
    0x00000004, 0x00040020, 0x0000029A, 0x00000003, 0x0000001D, 0x0004003B,
    0x0000029A, 0x00001400, 0x00000003, 0x00040017, 0x00000013, 0x0000000D,
    0x00000002, 0x00040020, 0x00000290, 0x00000001, 0x00000013, 0x0004003B,
    0x00000290, 0x00000F25, 0x00000001, 0x0004002B, 0x0000000D, 0x00000A0C,
    0x00000000, 0x0004002B, 0x0000000D, 0x0000008A, 0x3F800000, 0x00050036,
    0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x0000619B,
    0x0004003D, 0x00000013, 0x0000450A, 0x00000F25, 0x00050051, 0x0000000D,
    0x000055EB, 0x0000450A, 0x00000000, 0x00050051, 0x0000000D, 0x00005B1F,
    0x0000450A, 0x00000001, 0x00070050, 0x0000001D, 0x00005788, 0x000055EB,
    0x00005B1F, 0x00000A0C, 0x0000008A, 0x0003003E, 0x00001400, 0x00005788,
    0x000100FD, 0x00010038,
};
