// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 23240
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %5663 "main" %3877 %gl_VertexIndex %4930
               OpDecorate %3877 Location 0
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
               OpMemberDecorate %_struct_1032 0 BuiltIn Position
               OpMemberDecorate %_struct_1032 1 BuiltIn PointSize
               OpDecorate %_struct_1032 Block
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v2float = OpTypeVector %float 2
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %3877 = OpVariable %_ptr_Output_v2float Output
        %int = OpTypeInt 32 1
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
       %uint = OpTypeInt 32 0
     %uint_1 = OpConstant %uint 1
     %v2uint = OpTypeVector %uint 2
     %uint_2 = OpConstant %uint 2
    %v4float = OpTypeVector %float 4
%_struct_1032 = OpTypeStruct %v4float %float
%_ptr_Output__struct_1032 = OpTypePointer Output %_struct_1032
       %4930 = OpVariable %_ptr_Output__struct_1032 Output
      %int_0 = OpConstant %int 0
    %float_2 = OpConstant %float 2
    %float_1 = OpConstant %float 1
    %float_0 = OpConstant %float 0
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %1849 = OpConstantComposite %v2uint %uint_2 %uint_2
        %768 = OpConstantComposite %v2float %float_1 %float_1
       %5663 = OpFunction %void None %1282
       %6733 = OpLabel
      %12420 = OpLoad %int %gl_VertexIndex
      %12986 = OpBitcast %uint %12420
      %21962 = OpShiftLeftLogical %int %12420 %uint_1
      %19941 = OpBitcast %uint %21962
      %15527 = OpCompositeConstruct %v2uint %12986 %19941
       %7198 = OpBitwiseAnd %v2uint %15527 %1849
      %12989 = OpConvertUToF %v2float %7198
               OpStore %3877 %12989
      %23239 = OpLoad %v2float %3877
      %20253 = OpVectorTimesScalar %v2float %23239 %float_2
      %23195 = OpFSub %v2float %20253 %768
       %7674 = OpCompositeExtract %float %23195 0
      %15569 = OpCompositeExtract %float %23195 1
      %18260 = OpCompositeConstruct %v4float %7674 %15569 %float_0 %float_1
      %12055 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %12055 %18260
               OpReturn
               OpFunctionEnd
#endif

const uint32_t fullscreen_tc_vs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00005AC8, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0008000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F25, 0x00001029, 0x00001342,
    0x00040047, 0x00000F25, 0x0000001E, 0x00000000, 0x00040047, 0x00001029,
    0x0000000B, 0x0000002A, 0x00050048, 0x00000408, 0x00000000, 0x0000000B,
    0x00000000, 0x00050048, 0x00000408, 0x00000001, 0x0000000B, 0x00000001,
    0x00030047, 0x00000408, 0x00000002, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00030016, 0x0000000D, 0x00000020, 0x00040017,
    0x00000013, 0x0000000D, 0x00000002, 0x00040020, 0x00000290, 0x00000003,
    0x00000013, 0x0004003B, 0x00000290, 0x00000F25, 0x00000003, 0x00040015,
    0x0000000C, 0x00000020, 0x00000001, 0x00040020, 0x00000289, 0x00000001,
    0x0000000C, 0x0004003B, 0x00000289, 0x00001029, 0x00000001, 0x00040015,
    0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B, 0x00000A0D,
    0x00000001, 0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x0004002B,
    0x0000000B, 0x00000A10, 0x00000002, 0x00040017, 0x0000001D, 0x0000000D,
    0x00000004, 0x0004001E, 0x00000408, 0x0000001D, 0x0000000D, 0x00040020,
    0x00000685, 0x00000003, 0x00000408, 0x0004003B, 0x00000685, 0x00001342,
    0x00000003, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x0004002B,
    0x0000000D, 0x00000018, 0x40000000, 0x0004002B, 0x0000000D, 0x0000008A,
    0x3F800000, 0x0004002B, 0x0000000D, 0x00000A0C, 0x00000000, 0x00040020,
    0x0000029A, 0x00000003, 0x0000001D, 0x0005002C, 0x00000011, 0x00000739,
    0x00000A10, 0x00000A10, 0x0005002C, 0x00000013, 0x00000300, 0x0000008A,
    0x0000008A, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x00001A4D, 0x0004003D, 0x0000000C, 0x00003084, 0x00001029,
    0x0004007C, 0x0000000B, 0x000032BA, 0x00003084, 0x000500C4, 0x0000000C,
    0x000055CA, 0x00003084, 0x00000A0D, 0x0004007C, 0x0000000B, 0x00004DE5,
    0x000055CA, 0x00050050, 0x00000011, 0x00003CA7, 0x000032BA, 0x00004DE5,
    0x000500C7, 0x00000011, 0x00001C1E, 0x00003CA7, 0x00000739, 0x00040070,
    0x00000013, 0x000032BD, 0x00001C1E, 0x0003003E, 0x00000F25, 0x000032BD,
    0x0004003D, 0x00000013, 0x00005AC7, 0x00000F25, 0x0005008E, 0x00000013,
    0x00004F1D, 0x00005AC7, 0x00000018, 0x00050083, 0x00000013, 0x00005A9B,
    0x00004F1D, 0x00000300, 0x00050051, 0x0000000D, 0x00001DFA, 0x00005A9B,
    0x00000000, 0x00050051, 0x0000000D, 0x00003CD1, 0x00005A9B, 0x00000001,
    0x00070050, 0x0000001D, 0x00004754, 0x00001DFA, 0x00003CD1, 0x00000A0C,
    0x0000008A, 0x00050041, 0x0000029A, 0x00002F17, 0x00001342, 0x00000A0B,
    0x0003003E, 0x00002F17, 0x00004754, 0x000100FD, 0x00010038,
};
