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
               OpEntryPoint Vertex %5663 "main" %4159 %4693 %3080 %5914 %4930 %5474
               OpDecorate %4159 Location 0
               OpDecorate %4693 Location 1
               OpDecorate %3080 Location 1
               OpDecorate %5914 Location 2
               OpMemberDecorate %_struct_419 0 BuiltIn Position
               OpMemberDecorate %_struct_419 1 BuiltIn PointSize
               OpMemberDecorate %_struct_419 2 BuiltIn ClipDistance
               OpMemberDecorate %_struct_419 3 BuiltIn CullDistance
               OpDecorate %_struct_419 Block
               OpDecorate %5474 Location 0
               OpMemberDecorate %_struct_997 0 Offset 0
               OpDecorate %_struct_997 Block
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v2float = OpTypeVector %float 2
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %4159 = OpVariable %_ptr_Output_v2float Output
%_ptr_Input_v2float = OpTypePointer Input %v2float
       %4693 = OpVariable %_ptr_Input_v2float Input
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %3080 = OpVariable %_ptr_Output_v4float Output
%_ptr_Input_v4float = OpTypePointer Input %v4float
       %5914 = OpVariable %_ptr_Input_v4float Input
       %uint = OpTypeInt 32 0
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%_struct_419 = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1
%_ptr_Output__struct_419 = OpTypePointer Output %_struct_419
       %4930 = OpVariable %_ptr_Output__struct_419 Output
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
       %5474 = OpVariable %_ptr_Input_v2float Input
%_struct_997 = OpTypeStruct %v2float
%_ptr_PushConstant__struct_997 = OpTypePointer PushConstant %_struct_997
       %3052 = OpVariable %_ptr_PushConstant__struct_997 PushConstant
%_ptr_PushConstant_v2float = OpTypePointer PushConstant %v2float
    %float_2 = OpConstant %float 2
       %2981 = OpConstantComposite %v2float %float_2 %float_2
    %float_1 = OpConstant %float 1
        %768 = OpConstantComposite %v2float %float_1 %float_1
    %float_0 = OpConstant %float 0
       %5663 = OpFunction %void None %1282
      %24626 = OpLabel
      %20581 = OpLoad %v2float %4693
               OpStore %4159 %20581
      %11060 = OpLoad %v4float %5914
               OpStore %3080 %11060
      %10541 = OpLoad %v2float %5474
      %22255 = OpAccessChain %_ptr_PushConstant_v2float %3052 %int_0
      %12012 = OpLoad %v2float %22255
      %17501 = OpFMul %v2float %10541 %12012
      %13314 = OpFMul %v2float %17501 %2981
       %6620 = OpFSub %v2float %13314 %768
      %22715 = OpCompositeExtract %float %6620 0
      %15569 = OpCompositeExtract %float %6620 1
      %18260 = OpCompositeConstruct %v4float %22715 %15569 %float_0 %float_1
      %12055 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %12055 %18260
               OpReturn
               OpFunctionEnd
#endif

const uint32_t immediate_vs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006033, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x000B000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x0000103F, 0x00001255, 0x00000C08,
    0x0000171A, 0x00001342, 0x00001562, 0x00040047, 0x0000103F, 0x0000001E,
    0x00000000, 0x00040047, 0x00001255, 0x0000001E, 0x00000001, 0x00040047,
    0x00000C08, 0x0000001E, 0x00000001, 0x00040047, 0x0000171A, 0x0000001E,
    0x00000002, 0x00050048, 0x000001A3, 0x00000000, 0x0000000B, 0x00000000,
    0x00050048, 0x000001A3, 0x00000001, 0x0000000B, 0x00000001, 0x00050048,
    0x000001A3, 0x00000002, 0x0000000B, 0x00000003, 0x00050048, 0x000001A3,
    0x00000003, 0x0000000B, 0x00000004, 0x00030047, 0x000001A3, 0x00000002,
    0x00040047, 0x00001562, 0x0000001E, 0x00000000, 0x00050048, 0x000003E5,
    0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x000003E5, 0x00000002,
    0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x00000013, 0x0000000D, 0x00000002,
    0x00040020, 0x00000290, 0x00000003, 0x00000013, 0x0004003B, 0x00000290,
    0x0000103F, 0x00000003, 0x00040020, 0x00000291, 0x00000001, 0x00000013,
    0x0004003B, 0x00000291, 0x00001255, 0x00000001, 0x00040017, 0x0000001D,
    0x0000000D, 0x00000004, 0x00040020, 0x0000029A, 0x00000003, 0x0000001D,
    0x0004003B, 0x0000029A, 0x00000C08, 0x00000003, 0x00040020, 0x0000029B,
    0x00000001, 0x0000001D, 0x0004003B, 0x0000029B, 0x0000171A, 0x00000001,
    0x00040015, 0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B,
    0x00000A0D, 0x00000001, 0x0004001C, 0x00000261, 0x0000000D, 0x00000A0D,
    0x0006001E, 0x000001A3, 0x0000001D, 0x0000000D, 0x00000261, 0x00000261,
    0x00040020, 0x00000420, 0x00000003, 0x000001A3, 0x0004003B, 0x00000420,
    0x00001342, 0x00000003, 0x00040015, 0x0000000C, 0x00000020, 0x00000001,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x0004003B, 0x00000291,
    0x00001562, 0x00000001, 0x0003001E, 0x000003E5, 0x00000013, 0x00040020,
    0x00000662, 0x00000009, 0x000003E5, 0x0004003B, 0x00000662, 0x00000BEC,
    0x00000009, 0x00040020, 0x00000292, 0x00000009, 0x00000013, 0x0004002B,
    0x0000000D, 0x00000018, 0x40000000, 0x0005002C, 0x00000013, 0x00000BA5,
    0x00000018, 0x00000018, 0x0004002B, 0x0000000D, 0x0000008A, 0x3F800000,
    0x0005002C, 0x00000013, 0x00000300, 0x0000008A, 0x0000008A, 0x0004002B,
    0x0000000D, 0x00000A0C, 0x00000000, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x00006032, 0x0004003D, 0x00000013,
    0x00005065, 0x00001255, 0x0003003E, 0x0000103F, 0x00005065, 0x0004003D,
    0x0000001D, 0x00002B34, 0x0000171A, 0x0003003E, 0x00000C08, 0x00002B34,
    0x0004003D, 0x00000013, 0x0000292D, 0x00001562, 0x00050041, 0x00000292,
    0x000056EF, 0x00000BEC, 0x00000A0B, 0x0004003D, 0x00000013, 0x00002EEC,
    0x000056EF, 0x00050085, 0x00000013, 0x0000445D, 0x0000292D, 0x00002EEC,
    0x00050085, 0x00000013, 0x00003402, 0x0000445D, 0x00000BA5, 0x00050083,
    0x00000013, 0x000019DC, 0x00003402, 0x00000300, 0x00050051, 0x0000000D,
    0x000058BB, 0x000019DC, 0x00000000, 0x00050051, 0x0000000D, 0x00003CD1,
    0x000019DC, 0x00000001, 0x00070050, 0x0000001D, 0x00004754, 0x000058BB,
    0x00003CD1, 0x00000A0C, 0x0000008A, 0x00050041, 0x0000029A, 0x00002F17,
    0x00001342, 0x00000A0B, 0x0003003E, 0x00002F17, 0x00004754, 0x000100FD,
    0x00010038,
};
