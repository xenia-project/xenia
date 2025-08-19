// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 22213
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %5663 "main" %4930 %gl_VertexIndex
               OpDecorate %_struct_374 Block
               OpMemberDecorate %_struct_374 0 BuiltIn Position
               OpMemberDecorate %_struct_374 1 BuiltIn PointSize
               OpMemberDecorate %_struct_374 2 BuiltIn ClipDistance
               OpMemberDecorate %_struct_374 3 BuiltIn CullDistance
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%_struct_374 = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1
%_ptr_Output__struct_374 = OpTypePointer Output %_struct_374
       %4930 = OpVariable %_ptr_Output__struct_374 Output
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
     %uint_0 = OpConstant %uint 0
       %1819 = OpConstantComposite %v2uint %uint_0 %uint_1
    %v2float = OpTypeVector %float 2
    %float_4 = OpConstant %float 4
       %2183 = OpConstantComposite %v2float %float_4 %float_4
   %float_n1 = OpConstant %float -1
         %73 = OpConstantComposite %v2float %float_n1 %float_n1
    %float_0 = OpConstant %float 0
    %float_1 = OpConstant %float 1
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %5663 = OpFunction %void None %1282
       %6733 = OpLabel
      %10216 = OpLoad %int %gl_VertexIndex
      %15313 = OpBitcast %uint %10216
      %15408 = OpCompositeConstruct %v2uint %15313 %15313
      %14991 = OpShiftRightLogical %v2uint %15408 %1819
      %18859 = OpBitwiseAnd %v2uint %14991 %1828
      %16455 = OpConvertUToF %v2float %18859
       %8403 = OpFMul %v2float %16455 %2183
      %22212 = OpFAdd %v2float %8403 %73
      %10599 = OpCompositeExtract %float %22212 0
      %13956 = OpCompositeExtract %float %22212 1
      %18260 = OpCompositeConstruct %v4float %10599 %13956 %float_0 %float_1
      %12055 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %12055 %18260
               OpReturn
               OpFunctionEnd
#endif

const uint32_t fullscreen_cw_vs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x000056C5, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00001342, 0x00001029, 0x00030047,
    0x00000176, 0x00000002, 0x00050048, 0x00000176, 0x00000000, 0x0000000B,
    0x00000000, 0x00050048, 0x00000176, 0x00000001, 0x0000000B, 0x00000001,
    0x00050048, 0x00000176, 0x00000002, 0x0000000B, 0x00000003, 0x00050048,
    0x00000176, 0x00000003, 0x0000000B, 0x00000004, 0x00040047, 0x00001029,
    0x0000000B, 0x0000002A, 0x00020013, 0x00000008, 0x00030021, 0x00000502,
    0x00000008, 0x00040015, 0x0000000B, 0x00000020, 0x00000000, 0x00040017,
    0x00000011, 0x0000000B, 0x00000002, 0x00030016, 0x0000000D, 0x00000020,
    0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x0004002B, 0x0000000B,
    0x00000A0D, 0x00000001, 0x0004001C, 0x0000025C, 0x0000000D, 0x00000A0D,
    0x0006001E, 0x00000176, 0x0000001D, 0x0000000D, 0x0000025C, 0x0000025C,
    0x00040020, 0x000003F3, 0x00000003, 0x00000176, 0x0004003B, 0x000003F3,
    0x00001342, 0x00000003, 0x00040015, 0x0000000C, 0x00000020, 0x00000001,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x00000289,
    0x00000001, 0x0000000C, 0x0004003B, 0x00000289, 0x00001029, 0x00000001,
    0x0004002B, 0x0000000B, 0x00000A0A, 0x00000000, 0x0005002C, 0x00000011,
    0x0000071B, 0x00000A0A, 0x00000A0D, 0x00040017, 0x00000013, 0x0000000D,
    0x00000002, 0x0004002B, 0x0000000D, 0x00000B69, 0x40800000, 0x0005002C,
    0x00000013, 0x00000887, 0x00000B69, 0x00000B69, 0x0004002B, 0x0000000D,
    0x00000341, 0xBF800000, 0x0005002C, 0x00000013, 0x00000049, 0x00000341,
    0x00000341, 0x0004002B, 0x0000000D, 0x00000A0C, 0x00000000, 0x0004002B,
    0x0000000D, 0x0000008A, 0x3F800000, 0x00040020, 0x0000029A, 0x00000003,
    0x0000001D, 0x0005002C, 0x00000011, 0x00000724, 0x00000A0D, 0x00000A0D,
    0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8,
    0x00001A4D, 0x0004003D, 0x0000000C, 0x000027E8, 0x00001029, 0x0004007C,
    0x0000000B, 0x00003BD1, 0x000027E8, 0x00050050, 0x00000011, 0x00003C30,
    0x00003BD1, 0x00003BD1, 0x000500C2, 0x00000011, 0x00003A8F, 0x00003C30,
    0x0000071B, 0x000500C7, 0x00000011, 0x000049AB, 0x00003A8F, 0x00000724,
    0x00040070, 0x00000013, 0x00004047, 0x000049AB, 0x00050085, 0x00000013,
    0x000020D3, 0x00004047, 0x00000887, 0x00050081, 0x00000013, 0x000056C4,
    0x000020D3, 0x00000049, 0x00050051, 0x0000000D, 0x00002967, 0x000056C4,
    0x00000000, 0x00050051, 0x0000000D, 0x00003684, 0x000056C4, 0x00000001,
    0x00070050, 0x0000001D, 0x00004754, 0x00002967, 0x00003684, 0x00000A0C,
    0x0000008A, 0x00050041, 0x0000029A, 0x00002F17, 0x00001342, 0x00000A0B,
    0x0003003E, 0x00002F17, 0x00004754, 0x000100FD, 0x00010038,
};
