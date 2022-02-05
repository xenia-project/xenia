// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24916
; Schema: 0
               OpCapability Geometry
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Geometry %5663 "main" %5305 %4065 %4930 %5430 %3302 %5753 %5479
               OpExecutionMode %5663 InputPoints
               OpExecutionMode %5663 Invocations 1
               OpExecutionMode %5663 OutputTriangleStrip
               OpExecutionMode %5663 OutputVertices 4
               OpMemberDecorate %_struct_1017 0 BuiltIn Position
               OpDecorate %_struct_1017 Block
               OpMemberDecorate %_struct_1287 0 Offset 0
               OpMemberDecorate %_struct_1287 1 Offset 16
               OpMemberDecorate %_struct_1287 2 Offset 32
               OpMemberDecorate %_struct_1287 3 Offset 48
               OpMemberDecorate %_struct_1287 4 Offset 64
               OpDecorate %_struct_1287 Block
               OpDecorate %4065 Location 17
               OpMemberDecorate %_struct_1018 0 BuiltIn Position
               OpDecorate %_struct_1018 Block
               OpDecorate %5430 Location 0
               OpDecorate %3302 Location 0
               OpDecorate %5753 Location 16
               OpDecorate %5479 Location 16
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_struct_1017 = OpTypeStruct %v4float
       %uint = OpTypeInt 32 0
     %uint_1 = OpConstant %uint 1
%_arr__struct_1017_uint_1 = OpTypeArray %_struct_1017 %uint_1
%_ptr_Input__arr__struct_1017_uint_1 = OpTypePointer Input %_arr__struct_1017_uint_1
       %5305 = OpVariable %_ptr_Input__arr__struct_1017_uint_1 Input
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
%_ptr_Input_v4float = OpTypePointer Input %v4float
    %v2float = OpTypeVector %float 2
%_ptr_Function_v2float = OpTypePointer Function %v2float
%_struct_1287 = OpTypeStruct %v4float %v4float %v4float %v4float %uint
%_ptr_PushConstant__struct_1287 = OpTypePointer PushConstant %_struct_1287
       %3463 = OpVariable %_ptr_PushConstant__struct_1287 PushConstant
      %int_2 = OpConstant %int 2
%_ptr_PushConstant_v4float = OpTypePointer PushConstant %v4float
%_arr_float_uint_1 = OpTypeArray %float %uint_1
%_ptr_Input__arr_float_uint_1 = OpTypePointer Input %_arr_float_uint_1
       %4065 = OpVariable %_ptr_Input__arr_float_uint_1 Input
%_ptr_Input_float = OpTypePointer Input %float
    %float_0 = OpConstant %float 0
       %bool = OpTypeBool
      %int_4 = OpConstant %int 4
%_struct_1018 = OpTypeStruct %v4float
%_ptr_Output__struct_1018 = OpTypePointer Output %_struct_1018
       %4930 = OpVariable %_ptr_Output__struct_1018 Output
     %uint_4 = OpConstant %uint 4
%_arr_v2float_uint_4 = OpTypeArray %v2float %uint_4
   %float_n1 = OpConstant %float -1
    %float_1 = OpConstant %float 1
         %73 = OpConstantComposite %v2float %float_n1 %float_1
        %768 = OpConstantComposite %v2float %float_1 %float_1
         %74 = OpConstantComposite %v2float %float_n1 %float_n1
        %769 = OpConstantComposite %v2float %float_1 %float_n1
       %2941 = OpConstantComposite %_arr_v2float_uint_4 %73 %768 %74 %769
%_ptr_Function__arr_v2float_uint_4 = OpTypePointer Function %_arr_v2float_uint_4
%_ptr_Output_v4float = OpTypePointer Output %v4float
    %uint_16 = OpConstant %uint 16
%_arr_v4float_uint_16 = OpTypeArray %v4float %uint_16
%_ptr_Output__arr_v4float_uint_16 = OpTypePointer Output %_arr_v4float_uint_16
       %5430 = OpVariable %_ptr_Output__arr_v4float_uint_16 Output
%_arr__arr_v4float_uint_16_uint_1 = OpTypeArray %_arr_v4float_uint_16 %uint_1
%_ptr_Input__arr__arr_v4float_uint_16_uint_1 = OpTypePointer Input %_arr__arr_v4float_uint_16_uint_1
       %3302 = OpVariable %_ptr_Input__arr__arr_v4float_uint_16_uint_1 Input
%_ptr_Input__arr_v4float_uint_16 = OpTypePointer Input %_arr_v4float_uint_16
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %5753 = OpVariable %_ptr_Output_v2float Output
       %1823 = OpConstantComposite %v2float %float_0 %float_0
      %int_1 = OpConstant %int 1
%_arr_v2float_uint_1 = OpTypeArray %v2float %uint_1
%_ptr_Input__arr_v2float_uint_1 = OpTypePointer Input %_arr_v2float_uint_1
       %5479 = OpVariable %_ptr_Input__arr_v2float_uint_1 Input
       %5663 = OpFunction %void None %1282
      %24915 = OpLabel
      %18491 = OpVariable %_ptr_Function__arr_v2float_uint_4 Function
       %5238 = OpVariable %_ptr_Function__arr_v2float_uint_4 Function
      %22270 = OpAccessChain %_ptr_Input_v4float %5305 %int_0 %int_0
       %8181 = OpLoad %v4float %22270
      %20420 = OpAccessChain %_ptr_PushConstant_v4float %3463 %int_2
      %20062 = OpLoad %v4float %20420
      %19110 = OpVectorShuffle %v2float %20062 %20062 0 1
       %7988 = OpAccessChain %_ptr_Input_float %4065 %int_0
      %13069 = OpLoad %float %7988
      %23515 = OpFOrdGreaterThan %bool %13069 %float_0
               OpSelectionMerge %16839 None
               OpBranchConditional %23515 %13106 %16839
      %13106 = OpLabel
      %18836 = OpCompositeConstruct %v2float %13069 %13069
               OpBranch %16839
      %16839 = OpLabel
      %19748 = OpPhi %v2float %19110 %24915 %18836 %13106
      %24067 = OpAccessChain %_ptr_PushConstant_v4float %3463 %int_0
      %15439 = OpLoad %v4float %24067
      %10399 = OpVectorShuffle %v2float %15439 %15439 2 3
      %24282 = OpFDiv %v2float %19748 %10399
               OpBranch %6318
       %6318 = OpLabel
      %22958 = OpPhi %int %int_0 %16839 %11651 %12148
      %24788 = OpSLessThan %bool %22958 %int_4
               OpLoopMerge %12265 %12148 None
               OpBranchConditional %24788 %12148 %12265
      %12148 = OpLabel
      %17761 = OpVectorShuffle %v2float %8181 %8181 0 1
               OpStore %18491 %2941
      %19574 = OpAccessChain %_ptr_Function_v2float %18491 %22958
      %15971 = OpLoad %v2float %19574
      %17243 = OpFMul %v2float %15971 %24282
      %16594 = OpFAdd %v2float %17761 %17243
      %10618 = OpCompositeExtract %float %16594 0
      %14087 = OpCompositeExtract %float %16594 1
       %7641 = OpCompositeExtract %float %8181 2
       %7529 = OpCompositeExtract %float %8181 3
      %18260 = OpCompositeConstruct %v4float %10618 %14087 %7641 %7529
       %8483 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %8483 %18260
      %19848 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_0
       %7910 = OpLoad %_arr_v4float_uint_16 %19848
               OpStore %5430 %7910
               OpStore %5238 %2941
      %13290 = OpAccessChain %_ptr_Function_v2float %5238 %22958
      %19207 = OpLoad %v2float %13290
       %8973 = OpExtInst %v2float %1 FMax %19207 %1823
               OpStore %5753 %8973
               OpEmitVertex
      %11651 = OpIAdd %int %22958 %int_1
               OpBranch %6318
      %12265 = OpLabel
               OpEndPrimitive
               OpReturn
               OpFunctionEnd
#endif

const uint32_t point_list_gs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006154, 0x00000000, 0x00020011,
    0x00000002, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x000C000F, 0x00000003,
    0x0000161F, 0x6E69616D, 0x00000000, 0x000014B9, 0x00000FE1, 0x00001342,
    0x00001536, 0x00000CE6, 0x00001679, 0x00001567, 0x00030010, 0x0000161F,
    0x00000013, 0x00040010, 0x0000161F, 0x00000000, 0x00000001, 0x00030010,
    0x0000161F, 0x0000001D, 0x00040010, 0x0000161F, 0x0000001A, 0x00000004,
    0x00050048, 0x000003F9, 0x00000000, 0x0000000B, 0x00000000, 0x00030047,
    0x000003F9, 0x00000002, 0x00050048, 0x00000507, 0x00000000, 0x00000023,
    0x00000000, 0x00050048, 0x00000507, 0x00000001, 0x00000023, 0x00000010,
    0x00050048, 0x00000507, 0x00000002, 0x00000023, 0x00000020, 0x00050048,
    0x00000507, 0x00000003, 0x00000023, 0x00000030, 0x00050048, 0x00000507,
    0x00000004, 0x00000023, 0x00000040, 0x00030047, 0x00000507, 0x00000002,
    0x00040047, 0x00000FE1, 0x0000001E, 0x00000011, 0x00050048, 0x000003FA,
    0x00000000, 0x0000000B, 0x00000000, 0x00030047, 0x000003FA, 0x00000002,
    0x00040047, 0x00001536, 0x0000001E, 0x00000000, 0x00040047, 0x00000CE6,
    0x0000001E, 0x00000000, 0x00040047, 0x00001679, 0x0000001E, 0x00000010,
    0x00040047, 0x00001567, 0x0000001E, 0x00000010, 0x00020013, 0x00000008,
    0x00030021, 0x00000502, 0x00000008, 0x00030016, 0x0000000D, 0x00000020,
    0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x0003001E, 0x000003F9,
    0x0000001D, 0x00040015, 0x0000000B, 0x00000020, 0x00000000, 0x0004002B,
    0x0000000B, 0x00000A0D, 0x00000001, 0x0004001C, 0x0000023D, 0x000003F9,
    0x00000A0D, 0x00040020, 0x000004BA, 0x00000001, 0x0000023D, 0x0004003B,
    0x000004BA, 0x000014B9, 0x00000001, 0x00040015, 0x0000000C, 0x00000020,
    0x00000001, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040020,
    0x0000029A, 0x00000001, 0x0000001D, 0x00040017, 0x00000013, 0x0000000D,
    0x00000002, 0x00040020, 0x00000290, 0x00000007, 0x00000013, 0x0007001E,
    0x00000507, 0x0000001D, 0x0000001D, 0x0000001D, 0x0000001D, 0x0000000B,
    0x00040020, 0x00000784, 0x00000009, 0x00000507, 0x0004003B, 0x00000784,
    0x00000D87, 0x00000009, 0x0004002B, 0x0000000C, 0x00000A11, 0x00000002,
    0x00040020, 0x0000029B, 0x00000009, 0x0000001D, 0x0004001C, 0x00000239,
    0x0000000D, 0x00000A0D, 0x00040020, 0x000004B6, 0x00000001, 0x00000239,
    0x0004003B, 0x000004B6, 0x00000FE1, 0x00000001, 0x00040020, 0x0000028A,
    0x00000001, 0x0000000D, 0x0004002B, 0x0000000D, 0x00000A0C, 0x00000000,
    0x00020014, 0x00000009, 0x0004002B, 0x0000000C, 0x00000A17, 0x00000004,
    0x0003001E, 0x000003FA, 0x0000001D, 0x00040020, 0x00000676, 0x00000003,
    0x000003FA, 0x0004003B, 0x00000676, 0x00001342, 0x00000003, 0x0004002B,
    0x0000000B, 0x00000A16, 0x00000004, 0x0004001C, 0x000004D3, 0x00000013,
    0x00000A16, 0x0004002B, 0x0000000D, 0x00000341, 0xBF800000, 0x0004002B,
    0x0000000D, 0x0000008A, 0x3F800000, 0x0005002C, 0x00000013, 0x00000049,
    0x00000341, 0x0000008A, 0x0005002C, 0x00000013, 0x00000300, 0x0000008A,
    0x0000008A, 0x0005002C, 0x00000013, 0x0000004A, 0x00000341, 0x00000341,
    0x0005002C, 0x00000013, 0x00000301, 0x0000008A, 0x00000341, 0x0007002C,
    0x000004D3, 0x00000B7D, 0x00000049, 0x00000300, 0x0000004A, 0x00000301,
    0x00040020, 0x00000750, 0x00000007, 0x000004D3, 0x00040020, 0x0000029C,
    0x00000003, 0x0000001D, 0x0004002B, 0x0000000B, 0x00000A3A, 0x00000010,
    0x0004001C, 0x00000989, 0x0000001D, 0x00000A3A, 0x00040020, 0x00000043,
    0x00000003, 0x00000989, 0x0004003B, 0x00000043, 0x00001536, 0x00000003,
    0x0004001C, 0x00000A2E, 0x00000989, 0x00000A0D, 0x00040020, 0x000000E8,
    0x00000001, 0x00000A2E, 0x0004003B, 0x000000E8, 0x00000CE6, 0x00000001,
    0x00040020, 0x00000044, 0x00000001, 0x00000989, 0x00040020, 0x00000291,
    0x00000003, 0x00000013, 0x0004003B, 0x00000291, 0x00001679, 0x00000003,
    0x0005002C, 0x00000013, 0x0000071F, 0x00000A0C, 0x00000A0C, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x0004001C, 0x00000281, 0x00000013,
    0x00000A0D, 0x00040020, 0x000004FE, 0x00000001, 0x00000281, 0x0004003B,
    0x000004FE, 0x00001567, 0x00000001, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x00006153, 0x0004003B, 0x00000750,
    0x0000483B, 0x00000007, 0x0004003B, 0x00000750, 0x00001476, 0x00000007,
    0x00060041, 0x0000029A, 0x000056FE, 0x000014B9, 0x00000A0B, 0x00000A0B,
    0x0004003D, 0x0000001D, 0x00001FF5, 0x000056FE, 0x00050041, 0x0000029B,
    0x00004FC4, 0x00000D87, 0x00000A11, 0x0004003D, 0x0000001D, 0x00004E5E,
    0x00004FC4, 0x0007004F, 0x00000013, 0x00004AA6, 0x00004E5E, 0x00004E5E,
    0x00000000, 0x00000001, 0x00050041, 0x0000028A, 0x00001F34, 0x00000FE1,
    0x00000A0B, 0x0004003D, 0x0000000D, 0x0000330D, 0x00001F34, 0x000500BA,
    0x00000009, 0x00005BDB, 0x0000330D, 0x00000A0C, 0x000300F7, 0x000041C7,
    0x00000000, 0x000400FA, 0x00005BDB, 0x00003332, 0x000041C7, 0x000200F8,
    0x00003332, 0x00050050, 0x00000013, 0x00004994, 0x0000330D, 0x0000330D,
    0x000200F9, 0x000041C7, 0x000200F8, 0x000041C7, 0x000700F5, 0x00000013,
    0x00004D24, 0x00004AA6, 0x00006153, 0x00004994, 0x00003332, 0x00050041,
    0x0000029B, 0x00005E03, 0x00000D87, 0x00000A0B, 0x0004003D, 0x0000001D,
    0x00003C4F, 0x00005E03, 0x0007004F, 0x00000013, 0x0000289F, 0x00003C4F,
    0x00003C4F, 0x00000002, 0x00000003, 0x00050088, 0x00000013, 0x00005EDA,
    0x00004D24, 0x0000289F, 0x000200F9, 0x000018AE, 0x000200F8, 0x000018AE,
    0x000700F5, 0x0000000C, 0x000059AE, 0x00000A0B, 0x000041C7, 0x00002D83,
    0x00002F74, 0x000500B1, 0x00000009, 0x000060D4, 0x000059AE, 0x00000A17,
    0x000400F6, 0x00002FE9, 0x00002F74, 0x00000000, 0x000400FA, 0x000060D4,
    0x00002F74, 0x00002FE9, 0x000200F8, 0x00002F74, 0x0007004F, 0x00000013,
    0x00004561, 0x00001FF5, 0x00001FF5, 0x00000000, 0x00000001, 0x0003003E,
    0x0000483B, 0x00000B7D, 0x00050041, 0x00000290, 0x00004C76, 0x0000483B,
    0x000059AE, 0x0004003D, 0x00000013, 0x00003E63, 0x00004C76, 0x00050085,
    0x00000013, 0x0000435B, 0x00003E63, 0x00005EDA, 0x00050081, 0x00000013,
    0x000040D2, 0x00004561, 0x0000435B, 0x00050051, 0x0000000D, 0x0000297A,
    0x000040D2, 0x00000000, 0x00050051, 0x0000000D, 0x00003707, 0x000040D2,
    0x00000001, 0x00050051, 0x0000000D, 0x00001DD9, 0x00001FF5, 0x00000002,
    0x00050051, 0x0000000D, 0x00001D69, 0x00001FF5, 0x00000003, 0x00070050,
    0x0000001D, 0x00004754, 0x0000297A, 0x00003707, 0x00001DD9, 0x00001D69,
    0x00050041, 0x0000029C, 0x00002123, 0x00001342, 0x00000A0B, 0x0003003E,
    0x00002123, 0x00004754, 0x00050041, 0x00000044, 0x00004D88, 0x00000CE6,
    0x00000A0B, 0x0004003D, 0x00000989, 0x00001EE6, 0x00004D88, 0x0003003E,
    0x00001536, 0x00001EE6, 0x0003003E, 0x00001476, 0x00000B7D, 0x00050041,
    0x00000290, 0x000033EA, 0x00001476, 0x000059AE, 0x0004003D, 0x00000013,
    0x00004B07, 0x000033EA, 0x0007000C, 0x00000013, 0x0000230D, 0x00000001,
    0x00000028, 0x00004B07, 0x0000071F, 0x0003003E, 0x00001679, 0x0000230D,
    0x000100DA, 0x00050080, 0x0000000C, 0x00002D83, 0x000059AE, 0x00000A0E,
    0x000200F9, 0x000018AE, 0x000200F8, 0x00002FE9, 0x000100DB, 0x000100FD,
    0x00010038,
};
