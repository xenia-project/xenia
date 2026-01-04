// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 24012
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %main "main" %_ %gl_VertexIndex
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %gl_PerVertex "gl_PerVertex"
               OpMemberName %gl_PerVertex 0 "gl_Position"
               OpMemberName %gl_PerVertex 1 "gl_PointSize"
               OpMemberName %gl_PerVertex 2 "gl_ClipDistance"
               OpMemberName %gl_PerVertex 3 "gl_CullDistance"
               OpName %_ ""
               OpName %push_const_block_xe "push_const_block_xe"
               OpMemberName %push_const_block_xe 0 "xe_triangle_strip_rect_offset"
               OpMemberName %push_const_block_xe 1 "xe_triangle_strip_rect_size"
               OpName %push_consts_xe "push_consts_xe"
               OpName %gl_VertexIndex "gl_VertexIndex"
               OpDecorate %gl_PerVertex Block
               OpMemberDecorate %gl_PerVertex 0 BuiltIn Position
               OpMemberDecorate %gl_PerVertex 1 BuiltIn PointSize
               OpMemberDecorate %gl_PerVertex 2 BuiltIn ClipDistance
               OpMemberDecorate %gl_PerVertex 3 BuiltIn CullDistance
               OpDecorate %push_const_block_xe Block
               OpMemberDecorate %push_const_block_xe 0 Offset 0
               OpMemberDecorate %push_const_block_xe 1 Offset 8
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%gl_PerVertex = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1
%_ptr_Output_gl_PerVertex = OpTypePointer Output %gl_PerVertex
          %_ = OpVariable %_ptr_Output_gl_PerVertex Output
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
    %v2float = OpTypeVector %float 2
%push_const_block_xe = OpTypeStruct %v2float %v2float
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
%_ptr_PushConstant_v2float = OpTypePointer PushConstant %v2float
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
     %uint_0 = OpConstant %uint 0
       %1819 = OpConstantComposite %v2uint %uint_0 %uint_1
      %int_1 = OpConstant %int 1
    %float_0 = OpConstant %float 0
    %float_1 = OpConstant %float 1
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %main = OpFunction %void None %1282
      %23915 = OpLabel
       %7053 = OpAccessChain %_ptr_PushConstant_v2float %push_consts_xe %int_0
      %17516 = OpLoad %v2float %7053
      %23241 = OpLoad %int %gl_VertexIndex
       %9480 = OpBitcast %uint %23241
      %15408 = OpCompositeConstruct %v2uint %9480 %9480
      %14991 = OpShiftRightLogical %v2uint %15408 %1819
      %17567 = OpBitwiseAnd %v2uint %14991 %1828
       %7856 = OpConvertUToF %v2float %17567
      %12606 = OpAccessChain %_ptr_PushConstant_v2float %push_consts_xe %int_1
      %24011 = OpLoad %v2float %12606
      %17243 = OpFMul %v2float %7856 %24011
      %16594 = OpFAdd %v2float %17516 %17243
      %10599 = OpCompositeExtract %float %16594 0
      %13956 = OpCompositeExtract %float %16594 1
      %18260 = OpCompositeConstruct %v4float %10599 %13956 %float_0 %float_1
      %12055 = OpAccessChain %_ptr_Output_v4float %_ %int_0
               OpStore %12055 %18260
               OpReturn
               OpFunctionEnd
#endif

const uint32_t guest_output_triangle_strip_rect_vs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00005DCC, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00001342, 0x00001029, 0x00030003,
    0x00000002, 0x000001CC, 0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F,
    0x665F6C6F, 0x5F776F6C, 0x72747461, 0x74756269, 0x00007365, 0x000B0004,
    0x455F4C47, 0x735F5458, 0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865,
    0x665F6572, 0x74636E75, 0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47,
    0x4C474F4F, 0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F,
    0x69746365, 0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45,
    0x64756C63, 0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x0000161F,
    0x6E69616D, 0x00000000, 0x00060005, 0x00000176, 0x505F6C67, 0x65567265,
    0x78657472, 0x00000000, 0x00060006, 0x00000176, 0x00000000, 0x505F6C67,
    0x7469736F, 0x006E6F69, 0x00070006, 0x00000176, 0x00000001, 0x505F6C67,
    0x746E696F, 0x657A6953, 0x00000000, 0x00070006, 0x00000176, 0x00000002,
    0x435F6C67, 0x4470696C, 0x61747369, 0x0065636E, 0x00070006, 0x00000176,
    0x00000003, 0x435F6C67, 0x446C6C75, 0x61747369, 0x0065636E, 0x00030005,
    0x00001342, 0x00000000, 0x00070005, 0x00000406, 0x68737570, 0x6E6F635F,
    0x625F7473, 0x6B636F6C, 0x0065785F, 0x000B0006, 0x00000406, 0x00000000,
    0x745F6578, 0x6E616972, 0x5F656C67, 0x69727473, 0x65725F70, 0x6F5F7463,
    0x65736666, 0x00000074, 0x000A0006, 0x00000406, 0x00000001, 0x745F6578,
    0x6E616972, 0x5F656C67, 0x69727473, 0x65725F70, 0x735F7463, 0x00657A69,
    0x00060005, 0x00000CE9, 0x68737570, 0x6E6F635F, 0x5F737473, 0x00006578,
    0x00060005, 0x00001029, 0x565F6C67, 0x65747265, 0x646E4978, 0x00007865,
    0x00030047, 0x00000176, 0x00000002, 0x00050048, 0x00000176, 0x00000000,
    0x0000000B, 0x00000000, 0x00050048, 0x00000176, 0x00000001, 0x0000000B,
    0x00000001, 0x00050048, 0x00000176, 0x00000002, 0x0000000B, 0x00000003,
    0x00050048, 0x00000176, 0x00000003, 0x0000000B, 0x00000004, 0x00030047,
    0x00000406, 0x00000002, 0x00050048, 0x00000406, 0x00000000, 0x00000023,
    0x00000000, 0x00050048, 0x00000406, 0x00000001, 0x00000023, 0x00000008,
    0x00040047, 0x00001029, 0x0000000B, 0x0000002A, 0x00020013, 0x00000008,
    0x00030021, 0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020,
    0x00000000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004,
    0x0004002B, 0x0000000B, 0x00000A0D, 0x00000001, 0x0004001C, 0x0000025C,
    0x0000000D, 0x00000A0D, 0x0006001E, 0x00000176, 0x0000001D, 0x0000000D,
    0x0000025C, 0x0000025C, 0x00040020, 0x000003F3, 0x00000003, 0x00000176,
    0x0004003B, 0x000003F3, 0x00001342, 0x00000003, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000,
    0x00040017, 0x00000013, 0x0000000D, 0x00000002, 0x0004001E, 0x00000406,
    0x00000013, 0x00000013, 0x00040020, 0x00000683, 0x00000009, 0x00000406,
    0x0004003B, 0x00000683, 0x00000CE9, 0x00000009, 0x00040020, 0x00000290,
    0x00000009, 0x00000013, 0x00040020, 0x00000289, 0x00000001, 0x0000000C,
    0x0004003B, 0x00000289, 0x00001029, 0x00000001, 0x0004002B, 0x0000000B,
    0x00000A0A, 0x00000000, 0x0005002C, 0x00000011, 0x0000071B, 0x00000A0A,
    0x00000A0D, 0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001, 0x0004002B,
    0x0000000D, 0x00000A0C, 0x00000000, 0x0004002B, 0x0000000D, 0x0000008A,
    0x3F800000, 0x00040020, 0x0000029A, 0x00000003, 0x0000001D, 0x0005002C,
    0x00000011, 0x00000724, 0x00000A0D, 0x00000A0D, 0x00050036, 0x00000008,
    0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00005D6B, 0x00050041,
    0x00000290, 0x00001B8D, 0x00000CE9, 0x00000A0B, 0x0004003D, 0x00000013,
    0x0000446C, 0x00001B8D, 0x0004003D, 0x0000000C, 0x00005AC9, 0x00001029,
    0x0004007C, 0x0000000B, 0x00002508, 0x00005AC9, 0x00050050, 0x00000011,
    0x00003C30, 0x00002508, 0x00002508, 0x000500C2, 0x00000011, 0x00003A8F,
    0x00003C30, 0x0000071B, 0x000500C7, 0x00000011, 0x0000449F, 0x00003A8F,
    0x00000724, 0x00040070, 0x00000013, 0x00001EB0, 0x0000449F, 0x00050041,
    0x00000290, 0x0000313E, 0x00000CE9, 0x00000A0E, 0x0004003D, 0x00000013,
    0x00005DCB, 0x0000313E, 0x00050085, 0x00000013, 0x0000435B, 0x00001EB0,
    0x00005DCB, 0x00050081, 0x00000013, 0x000040D2, 0x0000446C, 0x0000435B,
    0x00050051, 0x0000000D, 0x00002967, 0x000040D2, 0x00000000, 0x00050051,
    0x0000000D, 0x00003684, 0x000040D2, 0x00000001, 0x00070050, 0x0000001D,
    0x00004754, 0x00002967, 0x00003684, 0x00000A0C, 0x0000008A, 0x00050041,
    0x0000029A, 0x00002F17, 0x00001342, 0x00000A0B, 0x0003003E, 0x00002F17,
    0x00004754, 0x000100FD, 0x00010038,
};
