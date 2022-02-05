// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 16104
; Schema: 0
               OpCapability Shader
               OpCapability Sampled1D
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %3302 %4841
               OpExecutionMode %5663 OriginUpperLeft
               OpDecorate %3302 Location 0
               OpDecorate %4841 Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
       %uint = OpTypeInt 32 0
    %uint_16 = OpConstant %uint 16
%_arr_v4float_uint_16 = OpTypeArray %v4float %uint_16
%_ptr_Input__arr_v4float_uint_16 = OpTypePointer Input %_arr_v4float_uint_16
       %3302 = OpVariable %_ptr_Input__arr_v4float_uint_16 Input
     %uint_4 = OpConstant %uint 4
%_arr_v4float_uint_4 = OpTypeArray %v4float %uint_4
%_ptr_Output__arr_v4float_uint_4 = OpTypePointer Output %_arr_v4float_uint_4
       %4841 = OpVariable %_ptr_Output__arr_v4float_uint_4 Output
       %5663 = OpFunction %void None %1282
      %16103 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t dummy_ps[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00003EE8, 0x00000000, 0x00020011,
    0x00000001, 0x00020011, 0x0000002B, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0007000F, 0x00000004, 0x0000161F, 0x6E69616D, 0x00000000, 0x00000CE6,
    0x000012E9, 0x00030010, 0x0000161F, 0x00000007, 0x00040047, 0x00000CE6,
    0x0000001E, 0x00000000, 0x00040047, 0x000012E9, 0x0000001E, 0x00000000,
    0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004,
    0x00040015, 0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B,
    0x00000A3A, 0x00000010, 0x0004001C, 0x0000056F, 0x0000001D, 0x00000A3A,
    0x00040020, 0x000007EC, 0x00000001, 0x0000056F, 0x0004003B, 0x000007EC,
    0x00000CE6, 0x00000001, 0x0004002B, 0x0000000B, 0x00000A16, 0x00000004,
    0x0004001C, 0x000005C3, 0x0000001D, 0x00000A16, 0x00040020, 0x00000840,
    0x00000003, 0x000005C3, 0x0004003B, 0x00000840, 0x000012E9, 0x00000003,
    0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8,
    0x00003EE7, 0x000100FD, 0x00010038,
};
