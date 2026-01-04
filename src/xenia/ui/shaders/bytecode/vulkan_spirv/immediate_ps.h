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
               OpEntryPoint Fragment %main "main" %xe_out_color %xe_in_color %xe_in_texcoord
               OpExecutionMode %main OriginUpperLeft
               OpSource GLSL 460
               OpSourceExtension "GL_EXT_control_flow_attributes"
               OpSourceExtension "GL_EXT_samplerless_texture_functions"
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %xe_out_color "xe_out_color"
               OpName %xe_in_color "xe_in_color"
               OpName %xe_immediate_texture "xe_immediate_texture"
               OpName %xe_in_texcoord "xe_in_texcoord"
               OpDecorate %xe_out_color Location 0
               OpDecorate %xe_in_color Location 1
               OpDecorate %xe_immediate_texture Binding 0
               OpDecorate %xe_immediate_texture DescriptorSet 0
               OpDecorate %xe_in_texcoord Location 0
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
%xe_out_color = OpVariable %_ptr_Output_v4float Output
%_ptr_Input_v4float = OpTypePointer Input %v4float
%xe_in_color = OpVariable %_ptr_Input_v4float Input
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
        %510 = OpTypeSampledImage %150
%_ptr_UniformConstant_510 = OpTypePointer UniformConstant %510
%xe_immediate_texture = OpVariable %_ptr_UniformConstant_510 UniformConstant
    %v2float = OpTypeVector %float 2
%_ptr_Input_v2float = OpTypePointer Input %v2float
%xe_in_texcoord = OpVariable %_ptr_Input_v2float Input
    %float_0 = OpConstant %float 0
       %main = OpFunction %void None %1282
      %24607 = OpLabel
      %20754 = OpLoad %v4float %xe_in_color
      %24285 = OpLoad %510 %xe_immediate_texture
       %8179 = OpLoad %v2float %xe_in_texcoord
       %6686 = OpImageSampleExplicitLod %v4float %24285 %8179 Lod %float_0
       %8939 = OpFMul %v4float %20754 %6686
               OpStore %xe_out_color %8939
               OpReturn
               OpFunctionEnd
#endif

const uint32_t immediate_ps[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00006020, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0008000F, 0x00000004,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000C08, 0x0000171A, 0x00001255,
    0x00030010, 0x0000161F, 0x00000007, 0x00030003, 0x00000002, 0x000001CC,
    0x00090004, 0x455F4C47, 0x635F5458, 0x72746E6F, 0x665F6C6F, 0x5F776F6C,
    0x72747461, 0x74756269, 0x00007365, 0x000B0004, 0x455F4C47, 0x735F5458,
    0x6C706D61, 0x656C7265, 0x745F7373, 0x75747865, 0x665F6572, 0x74636E75,
    0x736E6F69, 0x00000000, 0x000A0004, 0x475F4C47, 0x4C474F4F, 0x70635F45,
    0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365, 0x00006576,
    0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63, 0x69645F65,
    0x74636572, 0x00657669, 0x00040005, 0x0000161F, 0x6E69616D, 0x00000000,
    0x00060005, 0x00000C08, 0x6F5F6578, 0x635F7475, 0x726F6C6F, 0x00000000,
    0x00050005, 0x0000171A, 0x695F6578, 0x6F635F6E, 0x00726F6C, 0x00080005,
    0x000016BA, 0x695F6578, 0x64656D6D, 0x65746169, 0x7865745F, 0x65727574,
    0x00000000, 0x00060005, 0x00001255, 0x695F6578, 0x65745F6E, 0x6F6F6378,
    0x00006472, 0x00040047, 0x00000C08, 0x0000001E, 0x00000000, 0x00040047,
    0x0000171A, 0x0000001E, 0x00000001, 0x00040047, 0x000016BA, 0x00000021,
    0x00000000, 0x00040047, 0x000016BA, 0x00000022, 0x00000000, 0x00040047,
    0x00001255, 0x0000001E, 0x00000000, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00030016, 0x0000000D, 0x00000020, 0x00040017,
    0x0000001D, 0x0000000D, 0x00000004, 0x00040020, 0x0000029A, 0x00000003,
    0x0000001D, 0x0004003B, 0x0000029A, 0x00000C08, 0x00000003, 0x00040020,
    0x0000029B, 0x00000001, 0x0000001D, 0x0004003B, 0x0000029B, 0x0000171A,
    0x00000001, 0x00090019, 0x00000096, 0x0000000D, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001B, 0x000001FE,
    0x00000096, 0x00040020, 0x0000047B, 0x00000000, 0x000001FE, 0x0004003B,
    0x0000047B, 0x000016BA, 0x00000000, 0x00040017, 0x00000013, 0x0000000D,
    0x00000002, 0x00040020, 0x00000290, 0x00000001, 0x00000013, 0x0004003B,
    0x00000290, 0x00001255, 0x00000001, 0x0004002B, 0x0000000D, 0x00000A0C,
    0x00000000, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x0000601F, 0x0004003D, 0x0000001D, 0x00005112, 0x0000171A,
    0x0004003D, 0x000001FE, 0x00005EDD, 0x000016BA, 0x0004003D, 0x00000013,
    0x00001FF3, 0x00001255, 0x00070058, 0x0000001D, 0x00001A1E, 0x00005EDD,
    0x00001FF3, 0x00000002, 0x00000A0C, 0x00050085, 0x0000001D, 0x000022EB,
    0x00005112, 0x00001A1E, 0x0003003E, 0x00000C08, 0x000022EB, 0x000100FD,
    0x00010038,
};
