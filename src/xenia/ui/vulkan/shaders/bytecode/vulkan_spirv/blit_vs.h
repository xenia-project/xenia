// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 25137
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %5663 "main" %gl_VertexIndex %4930 %5592
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
               OpMemberDecorate %_struct_1080 0 Offset 0
               OpMemberDecorate %_struct_1080 1 Offset 16
               OpDecorate %_struct_1080 Block
               OpMemberDecorate %_struct_1589 0 BuiltIn Position
               OpMemberDecorate %_struct_1589 1 BuiltIn PointSize
               OpMemberDecorate %_struct_1589 2 BuiltIn ClipDistance
               OpMemberDecorate %_struct_1589 3 BuiltIn CullDistance
               OpDecorate %_struct_1589 Block
               OpDecorate %5592 Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v2float = OpTypeVector %float 2
%_ptr_Function_v2float = OpTypePointer Function %v2float
       %uint = OpTypeInt 32 0
     %uint_4 = OpConstant %uint 4
%_arr_v2float_uint_4 = OpTypeArray %v2float %uint_4
    %float_0 = OpConstant %float 0
       %1823 = OpConstantComposite %v2float %float_0 %float_0
    %float_1 = OpConstant %float 1
        %312 = OpConstantComposite %v2float %float_1 %float_0
        %889 = OpConstantComposite %v2float %float_0 %float_1
        %768 = OpConstantComposite %v2float %float_1 %float_1
        %809 = OpConstantComposite %_arr_v2float_uint_4 %1823 %312 %889 %768
        %int = OpTypeInt 32 1
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
%_ptr_Function__arr_v2float_uint_4 = OpTypePointer Function %_arr_v2float_uint_4
    %float_2 = OpConstant %float 2
    %v4float = OpTypeVector %float 4
%_struct_1080 = OpTypeStruct %v4float %v4float
%_ptr_PushConstant__struct_1080 = OpTypePointer PushConstant %_struct_1080
       %3463 = OpVariable %_ptr_PushConstant__struct_1080 PushConstant
      %int_1 = OpConstant %int 1
%_ptr_PushConstant_v4float = OpTypePointer PushConstant %v4float
       %2243 = OpConstantComposite %v4float %float_2 %float_2 %float_2 %float_2
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%_struct_1589 = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1
%_ptr_Output__struct_1589 = OpTypePointer Output %_struct_1589
       %4930 = OpVariable %_ptr_Output__struct_1589 Output
      %int_0 = OpConstant %int 0
%_ptr_Output_v4float = OpTypePointer Output %v4float
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %5592 = OpVariable %_ptr_Output_v2float Output
       %5663 = OpFunction %void None %1282
      %24953 = OpLabel
       %5238 = OpVariable %_ptr_Function__arr_v2float_uint_4 Function
      %24173 = OpLoad %int %gl_VertexIndex
               OpStore %5238 %809
      %16679 = OpAccessChain %_ptr_Function_v2float %5238 %24173
       %7372 = OpLoad %v2float %16679
      %21446 = OpAccessChain %_ptr_PushConstant_v4float %3463 %int_1
      %10986 = OpLoad %v4float %21446
       %7772 = OpFMul %v4float %10986 %2243
      %17065 = OpVectorShuffle %v2float %7772 %7772 0 1
      %22600 = OpFSub %v2float %17065 %768
       %7156 = OpVectorShuffle %v2float %7772 %7772 2 3
      %20491 = OpFMul %v2float %7372 %7156
      %18197 = OpFAdd %v2float %22600 %20491
      %10599 = OpCompositeExtract %float %18197 0
      %13956 = OpCompositeExtract %float %18197 1
      %18260 = OpCompositeConstruct %v4float %10599 %13956 %float_0 %float_1
       %8483 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %8483 %18260
      %20171 = OpAccessChain %_ptr_PushConstant_v4float %3463 %int_0
       %6318 = OpLoad %v4float %20171
       %7688 = OpVectorShuffle %v2float %6318 %6318 2 3
      %18797 = OpFMul %v2float %7372 %7688
      %18691 = OpVectorShuffle %v2float %6318 %6318 0 1
      %25136 = OpFAdd %v2float %18797 %18691
               OpStore %5592 %25136
               OpReturn
               OpFunctionEnd
#endif

const uint32_t blit_vs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006231, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0008000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00001029, 0x00001342, 0x000015D8,
    0x00040047, 0x00001029, 0x0000000B, 0x0000002A, 0x00050048, 0x00000438,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x00000438, 0x00000001,
    0x00000023, 0x00000010, 0x00030047, 0x00000438, 0x00000002, 0x00050048,
    0x00000635, 0x00000000, 0x0000000B, 0x00000000, 0x00050048, 0x00000635,
    0x00000001, 0x0000000B, 0x00000001, 0x00050048, 0x00000635, 0x00000002,
    0x0000000B, 0x00000003, 0x00050048, 0x00000635, 0x00000003, 0x0000000B,
    0x00000004, 0x00030047, 0x00000635, 0x00000002, 0x00040047, 0x000015D8,
    0x0000001E, 0x00000000, 0x00020013, 0x00000008, 0x00030021, 0x00000502,
    0x00000008, 0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x00000013,
    0x0000000D, 0x00000002, 0x00040020, 0x00000290, 0x00000007, 0x00000013,
    0x00040015, 0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B,
    0x00000A16, 0x00000004, 0x0004001C, 0x00000276, 0x00000013, 0x00000A16,
    0x0004002B, 0x0000000D, 0x00000A0C, 0x00000000, 0x0005002C, 0x00000013,
    0x0000071F, 0x00000A0C, 0x00000A0C, 0x0004002B, 0x0000000D, 0x0000008A,
    0x3F800000, 0x0005002C, 0x00000013, 0x00000138, 0x0000008A, 0x00000A0C,
    0x0005002C, 0x00000013, 0x00000379, 0x00000A0C, 0x0000008A, 0x0005002C,
    0x00000013, 0x00000300, 0x0000008A, 0x0000008A, 0x0007002C, 0x00000276,
    0x00000329, 0x0000071F, 0x00000138, 0x00000379, 0x00000300, 0x00040015,
    0x0000000C, 0x00000020, 0x00000001, 0x00040020, 0x00000289, 0x00000001,
    0x0000000C, 0x0004003B, 0x00000289, 0x00001029, 0x00000001, 0x00040020,
    0x000004F3, 0x00000007, 0x00000276, 0x0004002B, 0x0000000D, 0x00000018,
    0x40000000, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x0004001E,
    0x00000438, 0x0000001D, 0x0000001D, 0x00040020, 0x000006B5, 0x00000009,
    0x00000438, 0x0004003B, 0x000006B5, 0x00000D87, 0x00000009, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x00040020, 0x0000029A, 0x00000009,
    0x0000001D, 0x0007002C, 0x0000001D, 0x000008C3, 0x00000018, 0x00000018,
    0x00000018, 0x00000018, 0x0004002B, 0x0000000B, 0x00000A0D, 0x00000001,
    0x0004001C, 0x000002E3, 0x0000000D, 0x00000A0D, 0x0006001E, 0x00000635,
    0x0000001D, 0x0000000D, 0x000002E3, 0x000002E3, 0x00040020, 0x000008B2,
    0x00000003, 0x00000635, 0x0004003B, 0x000008B2, 0x00001342, 0x00000003,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x0000029B,
    0x00000003, 0x0000001D, 0x00040020, 0x00000291, 0x00000003, 0x00000013,
    0x0004003B, 0x00000291, 0x000015D8, 0x00000003, 0x00050036, 0x00000008,
    0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00006179, 0x0004003B,
    0x000004F3, 0x00001476, 0x00000007, 0x0004003D, 0x0000000C, 0x00005E6D,
    0x00001029, 0x0003003E, 0x00001476, 0x00000329, 0x00050041, 0x00000290,
    0x00004127, 0x00001476, 0x00005E6D, 0x0004003D, 0x00000013, 0x00001CCC,
    0x00004127, 0x00050041, 0x0000029A, 0x000053C6, 0x00000D87, 0x00000A0E,
    0x0004003D, 0x0000001D, 0x00002AEA, 0x000053C6, 0x00050085, 0x0000001D,
    0x00001E5C, 0x00002AEA, 0x000008C3, 0x0007004F, 0x00000013, 0x000042A9,
    0x00001E5C, 0x00001E5C, 0x00000000, 0x00000001, 0x00050083, 0x00000013,
    0x00005848, 0x000042A9, 0x00000300, 0x0007004F, 0x00000013, 0x00001BF4,
    0x00001E5C, 0x00001E5C, 0x00000002, 0x00000003, 0x00050085, 0x00000013,
    0x0000500B, 0x00001CCC, 0x00001BF4, 0x00050081, 0x00000013, 0x00004715,
    0x00005848, 0x0000500B, 0x00050051, 0x0000000D, 0x00002967, 0x00004715,
    0x00000000, 0x00050051, 0x0000000D, 0x00003684, 0x00004715, 0x00000001,
    0x00070050, 0x0000001D, 0x00004754, 0x00002967, 0x00003684, 0x00000A0C,
    0x0000008A, 0x00050041, 0x0000029B, 0x00002123, 0x00001342, 0x00000A0B,
    0x0003003E, 0x00002123, 0x00004754, 0x00050041, 0x0000029A, 0x00004ECB,
    0x00000D87, 0x00000A0B, 0x0004003D, 0x0000001D, 0x000018AE, 0x00004ECB,
    0x0007004F, 0x00000013, 0x00001E08, 0x000018AE, 0x000018AE, 0x00000002,
    0x00000003, 0x00050085, 0x00000013, 0x0000496D, 0x00001CCC, 0x00001E08,
    0x0007004F, 0x00000013, 0x00004903, 0x000018AE, 0x000018AE, 0x00000000,
    0x00000001, 0x00050081, 0x00000013, 0x00006230, 0x0000496D, 0x00004903,
    0x0003003E, 0x000015D8, 0x00006230, 0x000100FD, 0x00010038,
};
