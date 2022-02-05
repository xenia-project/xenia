// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24608
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %5663 "main" %4841 %5592
               OpExecutionMode %5663 OriginUpperLeft
               OpDecorate %4841 Location 0
               OpDecorate %5164 DescriptorSet 0
               OpDecorate %5164 Binding 0
               OpDecorate %5592 Location 0
               OpMemberDecorate %_struct_1019 0 Offset 32
               OpMemberDecorate %_struct_1019 1 Offset 44
               OpDecorate %_struct_1019 Block
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
       %4841 = OpVariable %_ptr_Output_v4float Output
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
        %510 = OpTypeSampledImage %150
%_ptr_UniformConstant_510 = OpTypePointer UniformConstant %510
       %5164 = OpVariable %_ptr_UniformConstant_510 UniformConstant
    %v2float = OpTypeVector %float 2
%_ptr_Input_v2float = OpTypePointer Input %v2float
       %5592 = OpVariable %_ptr_Input_v2float Input
    %v3float = OpTypeVector %float 3
        %int = OpTypeInt 32 1
%_struct_1019 = OpTypeStruct %v3float %int
%_ptr_PushConstant__struct_1019 = OpTypePointer PushConstant %_struct_1019
       %3463 = OpVariable %_ptr_PushConstant__struct_1019 PushConstant
      %int_1 = OpConstant %int 1
%_ptr_PushConstant_int = OpTypePointer PushConstant %int
      %int_0 = OpConstant %int 0
       %bool = OpTypeBool
       %5663 = OpFunction %void None %1282
      %24607 = OpLabel
      %21248 = OpLoad %510 %5164
      %19293 = OpLoad %v2float %5592
       %8148 = OpImageSampleImplicitLod %v4float %21248 %19293
               OpStore %4841 %8148
      %20291 = OpAccessChain %_ptr_PushConstant_int %3463 %int_1
      %11639 = OpLoad %int %20291
      %12913 = OpINotEqual %bool %11639 %int_0
               OpSelectionMerge %19578 None
               OpBranchConditional %12913 %13163 %19578
      %13163 = OpLabel
       %9669 = OpLoad %v4float %4841
       %6737 = OpVectorShuffle %v4float %9669 %9669 2 1 0 3
               OpStore %4841 %6737
               OpBranch %19578
      %19578 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t blit_color_ps[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006020, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0007000F, 0x00000004,
    0x0000161F, 0x6E69616D, 0x00000000, 0x000012E9, 0x000015D8, 0x00030010,
    0x0000161F, 0x00000007, 0x00040047, 0x000012E9, 0x0000001E, 0x00000000,
    0x00040047, 0x0000142C, 0x00000022, 0x00000000, 0x00040047, 0x0000142C,
    0x00000021, 0x00000000, 0x00040047, 0x000015D8, 0x0000001E, 0x00000000,
    0x00050048, 0x000003FB, 0x00000000, 0x00000023, 0x00000020, 0x00050048,
    0x000003FB, 0x00000001, 0x00000023, 0x0000002C, 0x00030047, 0x000003FB,
    0x00000002, 0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008,
    0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D,
    0x00000004, 0x00040020, 0x0000029A, 0x00000003, 0x0000001D, 0x0004003B,
    0x0000029A, 0x000012E9, 0x00000003, 0x00090019, 0x00000096, 0x0000000D,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x0003001B, 0x000001FE, 0x00000096, 0x00040020, 0x0000047B, 0x00000000,
    0x000001FE, 0x0004003B, 0x0000047B, 0x0000142C, 0x00000000, 0x00040017,
    0x00000013, 0x0000000D, 0x00000002, 0x00040020, 0x00000290, 0x00000001,
    0x00000013, 0x0004003B, 0x00000290, 0x000015D8, 0x00000001, 0x00040017,
    0x00000018, 0x0000000D, 0x00000003, 0x00040015, 0x0000000C, 0x00000020,
    0x00000001, 0x0004001E, 0x000003FB, 0x00000018, 0x0000000C, 0x00040020,
    0x00000678, 0x00000009, 0x000003FB, 0x0004003B, 0x00000678, 0x00000D87,
    0x00000009, 0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001, 0x00040020,
    0x00000289, 0x00000009, 0x0000000C, 0x0004002B, 0x0000000C, 0x00000A0B,
    0x00000000, 0x00020014, 0x00000009, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x0000601F, 0x0004003D, 0x000001FE,
    0x00005300, 0x0000142C, 0x0004003D, 0x00000013, 0x00004B5D, 0x000015D8,
    0x00050057, 0x0000001D, 0x00001FD4, 0x00005300, 0x00004B5D, 0x0003003E,
    0x000012E9, 0x00001FD4, 0x00050041, 0x00000289, 0x00004F43, 0x00000D87,
    0x00000A0E, 0x0004003D, 0x0000000C, 0x00002D77, 0x00004F43, 0x000500AB,
    0x00000009, 0x00003271, 0x00002D77, 0x00000A0B, 0x000300F7, 0x00004C7A,
    0x00000000, 0x000400FA, 0x00003271, 0x0000336B, 0x00004C7A, 0x000200F8,
    0x0000336B, 0x0004003D, 0x0000001D, 0x000025C5, 0x000012E9, 0x0009004F,
    0x0000001D, 0x00001A51, 0x000025C5, 0x000025C5, 0x00000002, 0x00000001,
    0x00000000, 0x00000003, 0x0003003E, 0x000012E9, 0x00001A51, 0x000200F9,
    0x00004C7A, 0x000200F8, 0x00004C7A, 0x000100FD, 0x00010038,
};
