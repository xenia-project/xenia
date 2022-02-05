// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24627
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %5663 "main" %3877 %3591 %5049 %5249 %9116 %4372
               OpDecorate %3877 Location 0
               OpDecorate %3591 Location 1
               OpDecorate %5049 RelaxedPrecision
               OpDecorate %5049 Location 1
               OpDecorate %5249 RelaxedPrecision
               OpDecorate %5249 Location 2
               OpDecorate %11060 RelaxedPrecision
               OpMemberDecorate %_struct_1032 0 BuiltIn Position
               OpMemberDecorate %_struct_1032 1 BuiltIn PointSize
               OpDecorate %_struct_1032 Block
               OpDecorate %4372 Location 0
               OpMemberDecorate %_struct_997 0 Offset 0
               OpDecorate %_struct_997 Block
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v2float = OpTypeVector %float 2
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %3877 = OpVariable %_ptr_Output_v2float Output
%_ptr_Input_v2float = OpTypePointer Input %v2float
       %3591 = OpVariable %_ptr_Input_v2float Input
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %5049 = OpVariable %_ptr_Output_v4float Output
%_ptr_Input_v4float = OpTypePointer Input %v4float
       %5249 = OpVariable %_ptr_Input_v4float Input
%_struct_1032 = OpTypeStruct %v4float %float
%_ptr_Output__struct_1032 = OpTypePointer Output %_struct_1032
       %9116 = OpVariable %_ptr_Output__struct_1032 Output
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
       %4372 = OpVariable %_ptr_Input_v2float Input
%_struct_997 = OpTypeStruct %v2float
%_ptr_PushConstant__struct_997 = OpTypePointer PushConstant %_struct_997
       %4930 = OpVariable %_ptr_PushConstant__struct_997 PushConstant
%_ptr_PushConstant_v2float = OpTypePointer PushConstant %v2float
    %float_2 = OpConstant %float 2
    %float_1 = OpConstant %float 1
    %float_0 = OpConstant %float 0
        %768 = OpConstantComposite %v2float %float_1 %float_1
       %5663 = OpFunction %void None %1282
      %24626 = OpLabel
      %20581 = OpLoad %v2float %3591
               OpStore %3877 %20581
      %11060 = OpLoad %v4float %5249
               OpStore %5049 %11060
      %10541 = OpLoad %v2float %4372
      %22255 = OpAccessChain %_ptr_PushConstant_v2float %4930 %int_0
      %12183 = OpLoad %v2float %22255
      %15944 = OpFMul %v2float %10541 %12183
      %15861 = OpVectorTimesScalar %v2float %15944 %float_2
      %10536 = OpFSub %v2float %15861 %768
       %7674 = OpCompositeExtract %float %10536 0
      %15569 = OpCompositeExtract %float %10536 1
      %18260 = OpCompositeConstruct %v4float %7674 %15569 %float_0 %float_1
      %12055 = OpAccessChain %_ptr_Output_v4float %9116 %int_0
               OpStore %12055 %18260
               OpReturn
               OpFunctionEnd
#endif

const uint32_t immediate_vs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006033, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x000B000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F25, 0x00000E07, 0x000013B9,
    0x00001481, 0x0000239C, 0x00001114, 0x00040047, 0x00000F25, 0x0000001E,
    0x00000000, 0x00040047, 0x00000E07, 0x0000001E, 0x00000001, 0x00030047,
    0x000013B9, 0x00000000, 0x00040047, 0x000013B9, 0x0000001E, 0x00000001,
    0x00030047, 0x00001481, 0x00000000, 0x00040047, 0x00001481, 0x0000001E,
    0x00000002, 0x00030047, 0x00002B34, 0x00000000, 0x00050048, 0x00000408,
    0x00000000, 0x0000000B, 0x00000000, 0x00050048, 0x00000408, 0x00000001,
    0x0000000B, 0x00000001, 0x00030047, 0x00000408, 0x00000002, 0x00040047,
    0x00001114, 0x0000001E, 0x00000000, 0x00050048, 0x000003E5, 0x00000000,
    0x00000023, 0x00000000, 0x00030047, 0x000003E5, 0x00000002, 0x00020013,
    0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00030016, 0x0000000D,
    0x00000020, 0x00040017, 0x00000013, 0x0000000D, 0x00000002, 0x00040020,
    0x00000290, 0x00000003, 0x00000013, 0x0004003B, 0x00000290, 0x00000F25,
    0x00000003, 0x00040020, 0x00000291, 0x00000001, 0x00000013, 0x0004003B,
    0x00000291, 0x00000E07, 0x00000001, 0x00040017, 0x0000001D, 0x0000000D,
    0x00000004, 0x00040020, 0x0000029A, 0x00000003, 0x0000001D, 0x0004003B,
    0x0000029A, 0x000013B9, 0x00000003, 0x00040020, 0x0000029B, 0x00000001,
    0x0000001D, 0x0004003B, 0x0000029B, 0x00001481, 0x00000001, 0x0004001E,
    0x00000408, 0x0000001D, 0x0000000D, 0x00040020, 0x00000685, 0x00000003,
    0x00000408, 0x0004003B, 0x00000685, 0x0000239C, 0x00000003, 0x00040015,
    0x0000000C, 0x00000020, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A0B,
    0x00000000, 0x0004003B, 0x00000291, 0x00001114, 0x00000001, 0x0003001E,
    0x000003E5, 0x00000013, 0x00040020, 0x00000662, 0x00000009, 0x000003E5,
    0x0004003B, 0x00000662, 0x00001342, 0x00000009, 0x00040020, 0x00000292,
    0x00000009, 0x00000013, 0x0004002B, 0x0000000D, 0x00000018, 0x40000000,
    0x0004002B, 0x0000000D, 0x0000008A, 0x3F800000, 0x0004002B, 0x0000000D,
    0x00000A0C, 0x00000000, 0x0005002C, 0x00000013, 0x00000300, 0x0000008A,
    0x0000008A, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x00006032, 0x0004003D, 0x00000013, 0x00005065, 0x00000E07,
    0x0003003E, 0x00000F25, 0x00005065, 0x0004003D, 0x0000001D, 0x00002B34,
    0x00001481, 0x0003003E, 0x000013B9, 0x00002B34, 0x0004003D, 0x00000013,
    0x0000292D, 0x00001114, 0x00050041, 0x00000292, 0x000056EF, 0x00001342,
    0x00000A0B, 0x0004003D, 0x00000013, 0x00002F97, 0x000056EF, 0x00050085,
    0x00000013, 0x00003E48, 0x0000292D, 0x00002F97, 0x0005008E, 0x00000013,
    0x00003DF5, 0x00003E48, 0x00000018, 0x00050083, 0x00000013, 0x00002928,
    0x00003DF5, 0x00000300, 0x00050051, 0x0000000D, 0x00001DFA, 0x00002928,
    0x00000000, 0x00050051, 0x0000000D, 0x00003CD1, 0x00002928, 0x00000001,
    0x00070050, 0x0000001D, 0x00004754, 0x00001DFA, 0x00003CD1, 0x00000A0C,
    0x0000008A, 0x00050041, 0x0000029A, 0x00002F17, 0x0000239C, 0x00000A0B,
    0x0003003E, 0x00002F17, 0x00004754, 0x000100FD, 0x00010038,
};
