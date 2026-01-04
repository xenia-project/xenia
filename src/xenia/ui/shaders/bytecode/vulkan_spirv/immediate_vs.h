// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 24627
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Vertex %main "main" %xe_out_texcoord %xe_in_texcoord %xe_out_color %xe_in_color %_ %xe_in_position
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %xe_out_texcoord "xe_out_texcoord"
               OpName %xe_in_texcoord "xe_in_texcoord"
               OpName %xe_out_color "xe_out_color"
               OpName %xe_in_color "xe_in_color"
               OpName %gl_PerVertex "gl_PerVertex"
               OpMemberName %gl_PerVertex 0 "gl_Position"
               OpMemberName %gl_PerVertex 1 "gl_PointSize"
               OpMemberName %gl_PerVertex 2 "gl_ClipDistance"
               OpMemberName %gl_PerVertex 3 "gl_CullDistance"
               OpName %_ ""
               OpName %xe_in_position "xe_in_position"
               OpName %push_const_block_xe "push_const_block_xe"
               OpMemberName %push_const_block_xe 0 "xe_coordinate_space_size_inv"
               OpName %push_consts_xe "push_consts_xe"
               OpDecorate %xe_out_texcoord Location 0
               OpDecorate %xe_in_texcoord Location 1
               OpDecorate %xe_out_color Location 1
               OpDecorate %xe_in_color Location 2
               OpDecorate %gl_PerVertex Block
               OpMemberDecorate %gl_PerVertex 0 BuiltIn Position
               OpMemberDecorate %gl_PerVertex 1 BuiltIn PointSize
               OpMemberDecorate %gl_PerVertex 2 BuiltIn ClipDistance
               OpMemberDecorate %gl_PerVertex 3 BuiltIn CullDistance
               OpDecorate %xe_in_position Location 0
               OpDecorate %push_const_block_xe Block
               OpMemberDecorate %push_const_block_xe 0 Offset 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v2float = OpTypeVector %float 2
%_ptr_Output_v2float = OpTypePointer Output %v2float
%xe_out_texcoord = OpVariable %_ptr_Output_v2float Output
%_ptr_Input_v2float = OpTypePointer Input %v2float
%xe_in_texcoord = OpVariable %_ptr_Input_v2float Input
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
%xe_out_color = OpVariable %_ptr_Output_v4float Output
%_ptr_Input_v4float = OpTypePointer Input %v4float
%xe_in_color = OpVariable %_ptr_Input_v4float Input
       %uint = OpTypeInt 32 0
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%gl_PerVertex = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1
%_ptr_Output_gl_PerVertex = OpTypePointer Output %gl_PerVertex
          %_ = OpVariable %_ptr_Output_gl_PerVertex Output
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
%xe_in_position = OpVariable %_ptr_Input_v2float Input
%push_const_block_xe = OpTypeStruct %v2float
%_ptr_PushConstant_push_const_block_xe = OpTypePointer PushConstant %push_const_block_xe
%push_consts_xe = OpVariable %_ptr_PushConstant_push_const_block_xe PushConstant
%_ptr_PushConstant_v2float = OpTypePointer PushConstant %v2float
    %float_2 = OpConstant %float 2
       %2981 = OpConstantComposite %v2float %float_2 %float_2
    %float_1 = OpConstant %float 1
        %768 = OpConstantComposite %v2float %float_1 %float_1
    %float_0 = OpConstant %float 0
       %main = OpFunction %void None %1282
      %24626 = OpLabel
      %20581 = OpLoad %v2float %xe_in_texcoord
               OpStore %xe_out_texcoord %20581
      %11060 = OpLoad %v4float %xe_in_color
               OpStore %xe_out_color %11060
      %10541 = OpLoad %v2float %xe_in_position
      %22255 = OpAccessChain %_ptr_PushConstant_v2float %push_consts_xe %int_0
      %12012 = OpLoad %v2float %22255
      %17501 = OpFMul %v2float %10541 %12012
      %13314 = OpFMul %v2float %17501 %2981
       %6620 = OpFSub %v2float %13314 %768
      %22715 = OpCompositeExtract %float %6620 0
      %15569 = OpCompositeExtract %float %6620 1
      %18260 = OpCompositeConstruct %v4float %22715 %15569 %float_0 %float_1
      %12055 = OpAccessChain %_ptr_Output_v4float %_ %int_0
               OpStore %12055 %18260
               OpReturn
               OpFunctionEnd
#endif

const uint32_t immediate_vs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006033, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x000B000F, 0x00000000,
    0x0000161F, 0x6E69616D, 0x00000000, 0x0000103F, 0x00001255, 0x00000C08,
    0x0000171A, 0x00001342, 0x00001562, 0x00030003, 0x00000002, 0x000001CC,
    0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F, 0x5F776F6C,
    0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47, 0x735F5458,
    0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572, 0x74636E75,
    0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F, 0x70635F45,
    0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365, 0x00006576,
    0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63, 0x69645F65,
    0x74636572, 0x00657669, 0x00040005, 0x0000161F, 0x6E69616D, 0x00000000,
    0x00060005, 0x0000103F, 0x6F5F6578, 0x745F7475, 0x6F637865, 0x0064726F,
    0x00060005, 0x00001255, 0x695F6578, 0x65745F6E, 0x6F6F6378, 0x00006472,
    0x00060005, 0x00000C08, 0x6F5F6578, 0x635F7475, 0x726F6C6F, 0x00000000,
    0x00050005, 0x0000171A, 0x695F6578, 0x6F635F6E, 0x00726F6C, 0x00060005,
    0x000001A3, 0x505F6C67, 0x65567265, 0x78657472, 0x00000000, 0x00060006,
    0x000001A3, 0x00000000, 0x505F6C67, 0x7469736F, 0x006E6F69, 0x00070006,
    0x000001A3, 0x00000001, 0x505F6C67, 0x746E696F, 0x657A6953, 0x00000000,
    0x00070006, 0x000001A3, 0x00000002, 0x435F6C67, 0x4470696C, 0x61747369,
    0x0065636E, 0x00070006, 0x000001A3, 0x00000003, 0x435F6C67, 0x446C6C75,
    0x61747369, 0x0065636E, 0x00030005, 0x00001342, 0x00000000, 0x00060005,
    0x00001562, 0x695F6578, 0x6F705F6E, 0x69746973, 0x00006E6F, 0x00070005,
    0x000003E5, 0x68737570, 0x6E6F635F, 0x625F7473, 0x6B636F6C, 0x0065785F,
    0x000B0006, 0x000003E5, 0x00000000, 0x635F6578, 0x64726F6F, 0x74616E69,
    0x70735F65, 0x5F656361, 0x657A6973, 0x766E695F, 0x00000000, 0x00060005,
    0x00000CE9, 0x68737570, 0x6E6F635F, 0x5F737473, 0x00006578, 0x00040047,
    0x0000103F, 0x0000001E, 0x00000000, 0x00040047, 0x00001255, 0x0000001E,
    0x00000001, 0x00040047, 0x00000C08, 0x0000001E, 0x00000001, 0x00040047,
    0x0000171A, 0x0000001E, 0x00000002, 0x00030047, 0x000001A3, 0x00000002,
    0x00050048, 0x000001A3, 0x00000000, 0x0000000B, 0x00000000, 0x00050048,
    0x000001A3, 0x00000001, 0x0000000B, 0x00000001, 0x00050048, 0x000001A3,
    0x00000002, 0x0000000B, 0x00000003, 0x00050048, 0x000001A3, 0x00000003,
    0x0000000B, 0x00000004, 0x00040047, 0x00001562, 0x0000001E, 0x00000000,
    0x00030047, 0x000003E5, 0x00000002, 0x00050048, 0x000003E5, 0x00000000,
    0x00000023, 0x00000000, 0x00020013, 0x00000008, 0x00030021, 0x00000502,
    0x00000008, 0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x00000013,
    0x0000000D, 0x00000002, 0x00040020, 0x00000290, 0x00000003, 0x00000013,
    0x0004003B, 0x00000290, 0x0000103F, 0x00000003, 0x00040020, 0x00000291,
    0x00000001, 0x00000013, 0x0004003B, 0x00000291, 0x00001255, 0x00000001,
    0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x00040020, 0x0000029A,
    0x00000003, 0x0000001D, 0x0004003B, 0x0000029A, 0x00000C08, 0x00000003,
    0x00040020, 0x0000029B, 0x00000001, 0x0000001D, 0x0004003B, 0x0000029B,
    0x0000171A, 0x00000001, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x0004002B, 0x0000000B, 0x00000A0D, 0x00000001, 0x0004001C, 0x00000261,
    0x0000000D, 0x00000A0D, 0x0006001E, 0x000001A3, 0x0000001D, 0x0000000D,
    0x00000261, 0x00000261, 0x00040020, 0x00000420, 0x00000003, 0x000001A3,
    0x0004003B, 0x00000420, 0x00001342, 0x00000003, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000,
    0x0004003B, 0x00000291, 0x00001562, 0x00000001, 0x0003001E, 0x000003E5,
    0x00000013, 0x00040020, 0x00000662, 0x00000009, 0x000003E5, 0x0004003B,
    0x00000662, 0x00000CE9, 0x00000009, 0x00040020, 0x00000292, 0x00000009,
    0x00000013, 0x0004002B, 0x0000000D, 0x00000018, 0x40000000, 0x0005002C,
    0x00000013, 0x00000BA5, 0x00000018, 0x00000018, 0x0004002B, 0x0000000D,
    0x0000008A, 0x3F800000, 0x0005002C, 0x00000013, 0x00000300, 0x0000008A,
    0x0000008A, 0x0004002B, 0x0000000D, 0x00000A0C, 0x00000000, 0x00050036,
    0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00006032,
    0x0004003D, 0x00000013, 0x00005065, 0x00001255, 0x0003003E, 0x0000103F,
    0x00005065, 0x0004003D, 0x0000001D, 0x00002B34, 0x0000171A, 0x0003003E,
    0x00000C08, 0x00002B34, 0x0004003D, 0x00000013, 0x0000292D, 0x00001562,
    0x00050041, 0x00000292, 0x000056EF, 0x00000CE9, 0x00000A0B, 0x0004003D,
    0x00000013, 0x00002EEC, 0x000056EF, 0x00050085, 0x00000013, 0x0000445D,
    0x0000292D, 0x00002EEC, 0x00050085, 0x00000013, 0x00003402, 0x0000445D,
    0x00000BA5, 0x00050083, 0x00000013, 0x000019DC, 0x00003402, 0x00000300,
    0x00050051, 0x0000000D, 0x000058BB, 0x000019DC, 0x00000000, 0x00050051,
    0x0000000D, 0x00003CD1, 0x000019DC, 0x00000001, 0x00070050, 0x0000001D,
    0x00004754, 0x000058BB, 0x00003CD1, 0x00000A0C, 0x0000008A, 0x00050041,
    0x0000029A, 0x00002F17, 0x00001342, 0x00000A0B, 0x0003003E, 0x00002F17,
    0x00004754, 0x000100FD, 0x00010038,
};
