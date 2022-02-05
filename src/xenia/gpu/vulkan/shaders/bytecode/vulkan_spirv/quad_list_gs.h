// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24789
; Schema: 0
               OpCapability Geometry
               OpCapability GeometryPointSize
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Geometry %5663 "main" %4930 %5305 %5430 %3302 %4044 %4656 %3736
               OpExecutionMode %5663 InputLinesAdjacency
               OpExecutionMode %5663 Invocations 1
               OpExecutionMode %5663 OutputTriangleStrip
               OpExecutionMode %5663 OutputVertices 4
               OpMemberDecorate %_struct_1032 0 BuiltIn Position
               OpMemberDecorate %_struct_1032 1 BuiltIn PointSize
               OpDecorate %_struct_1032 Block
               OpMemberDecorate %_struct_1033 0 BuiltIn Position
               OpMemberDecorate %_struct_1033 1 BuiltIn PointSize
               OpDecorate %_struct_1033 Block
               OpDecorate %5430 Location 0
               OpDecorate %3302 Location 0
               OpDecorate %4044 Location 16
               OpDecorate %4656 Location 17
               OpDecorate %3736 Location 16
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
        %int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
      %int_0 = OpConstant %int 0
      %int_4 = OpConstant %int 4
       %bool = OpTypeBool
       %uint = OpTypeInt 32 0
     %uint_4 = OpConstant %uint 4
%_arr_int_uint_4 = OpTypeArray %int %uint_4
      %int_1 = OpConstant %int 1
      %int_3 = OpConstant %int 3
      %int_2 = OpConstant %int 2
        %566 = OpConstantComposite %_arr_int_uint_4 %int_0 %int_1 %int_3 %int_2
%_ptr_Function__arr_int_uint_4 = OpTypePointer Function %_arr_int_uint_4
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_struct_1032 = OpTypeStruct %v4float %float
%_ptr_Output__struct_1032 = OpTypePointer Output %_struct_1032
       %4930 = OpVariable %_ptr_Output__struct_1032 Output
%_struct_1033 = OpTypeStruct %v4float %float
%_arr__struct_1033_uint_4 = OpTypeArray %_struct_1033 %uint_4
%_ptr_Input__arr__struct_1033_uint_4 = OpTypePointer Input %_arr__struct_1033_uint_4
       %5305 = OpVariable %_ptr_Input__arr__struct_1033_uint_4 Input
%_ptr_Input_v4float = OpTypePointer Input %v4float
%_ptr_Output_v4float = OpTypePointer Output %v4float
%_ptr_Input_float = OpTypePointer Input %float
%_ptr_Output_float = OpTypePointer Output %float
    %uint_16 = OpConstant %uint 16
%_arr_v4float_uint_16 = OpTypeArray %v4float %uint_16
%_ptr_Output__arr_v4float_uint_16 = OpTypePointer Output %_arr_v4float_uint_16
       %5430 = OpVariable %_ptr_Output__arr_v4float_uint_16 Output
%_arr__arr_v4float_uint_16_uint_4 = OpTypeArray %_arr_v4float_uint_16 %uint_4
%_ptr_Input__arr__arr_v4float_uint_16_uint_4 = OpTypePointer Input %_arr__arr_v4float_uint_16_uint_4
       %3302 = OpVariable %_ptr_Input__arr__arr_v4float_uint_16_uint_4 Input
%_ptr_Input__arr_v4float_uint_16 = OpTypePointer Input %_arr_v4float_uint_16
    %v2float = OpTypeVector %float 2
%_arr_v2float_uint_4 = OpTypeArray %v2float %uint_4
%_ptr_Input__arr_v2float_uint_4 = OpTypePointer Input %_arr_v2float_uint_4
       %4044 = OpVariable %_ptr_Input__arr_v2float_uint_4 Input
%_arr_float_uint_4 = OpTypeArray %float %uint_4
%_ptr_Input__arr_float_uint_4 = OpTypePointer Input %_arr_float_uint_4
       %4656 = OpVariable %_ptr_Input__arr_float_uint_4 Input
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %3736 = OpVariable %_ptr_Output_v2float Output
       %5663 = OpFunction %void None %1282
       %9454 = OpLabel
       %5238 = OpVariable %_ptr_Function__arr_int_uint_4 Function
               OpBranch %18173
      %18173 = OpLabel
      %22958 = OpPhi %int %int_0 %9454 %11651 %15146
      %24788 = OpSLessThan %bool %22958 %int_4
               OpLoopMerge %12265 %15146 None
               OpBranchConditional %24788 %15146 %12265
      %15146 = OpLabel
               OpStore %5238 %566
      %22512 = OpAccessChain %_ptr_Function_int %5238 %22958
       %7372 = OpLoad %int %22512
      %20154 = OpAccessChain %_ptr_Input_v4float %5305 %7372 %int_0
      %22427 = OpLoad %v4float %20154
      %19981 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %19981 %22427
      %19905 = OpAccessChain %_ptr_Input_float %5305 %7372 %int_1
       %7391 = OpLoad %float %19905
      %19982 = OpAccessChain %_ptr_Output_float %4930 %int_1
               OpStore %19982 %7391
      %19848 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %7372
      %10874 = OpLoad %_arr_v4float_uint_16 %19848
               OpStore %5430 %10874
               OpEmitVertex
      %11651 = OpIAdd %int %22958 %int_1
               OpBranch %18173
      %12265 = OpLabel
               OpEndPrimitive
               OpReturn
               OpFunctionEnd
#endif

const uint32_t quad_list_gs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x000060D5, 0x00000000, 0x00020011,
    0x00000002, 0x00020011, 0x00000018, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x000C000F, 0x00000003, 0x0000161F, 0x6E69616D, 0x00000000, 0x00001342,
    0x000014B9, 0x00001536, 0x00000CE6, 0x00000FCC, 0x00001230, 0x00000E98,
    0x00030010, 0x0000161F, 0x00000015, 0x00040010, 0x0000161F, 0x00000000,
    0x00000001, 0x00030010, 0x0000161F, 0x0000001D, 0x00040010, 0x0000161F,
    0x0000001A, 0x00000004, 0x00050048, 0x00000408, 0x00000000, 0x0000000B,
    0x00000000, 0x00050048, 0x00000408, 0x00000001, 0x0000000B, 0x00000001,
    0x00030047, 0x00000408, 0x00000002, 0x00050048, 0x00000409, 0x00000000,
    0x0000000B, 0x00000000, 0x00050048, 0x00000409, 0x00000001, 0x0000000B,
    0x00000001, 0x00030047, 0x00000409, 0x00000002, 0x00040047, 0x00001536,
    0x0000001E, 0x00000000, 0x00040047, 0x00000CE6, 0x0000001E, 0x00000000,
    0x00040047, 0x00000FCC, 0x0000001E, 0x00000010, 0x00040047, 0x00001230,
    0x0000001E, 0x00000011, 0x00040047, 0x00000E98, 0x0000001E, 0x00000010,
    0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00040015,
    0x0000000C, 0x00000020, 0x00000001, 0x00040020, 0x00000289, 0x00000007,
    0x0000000C, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x0004002B,
    0x0000000C, 0x00000A17, 0x00000004, 0x00020014, 0x00000009, 0x00040015,
    0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B, 0x00000A16,
    0x00000004, 0x0004001C, 0x00000251, 0x0000000C, 0x00000A16, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A14,
    0x00000003, 0x0004002B, 0x0000000C, 0x00000A11, 0x00000002, 0x0007002C,
    0x00000251, 0x00000236, 0x00000A0B, 0x00000A0E, 0x00000A14, 0x00000A11,
    0x00040020, 0x000004CE, 0x00000007, 0x00000251, 0x00030016, 0x0000000D,
    0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x0004001E,
    0x00000408, 0x0000001D, 0x0000000D, 0x00040020, 0x00000685, 0x00000003,
    0x00000408, 0x0004003B, 0x00000685, 0x00001342, 0x00000003, 0x0004001E,
    0x00000409, 0x0000001D, 0x0000000D, 0x0004001C, 0x000003A8, 0x00000409,
    0x00000A16, 0x00040020, 0x00000625, 0x00000001, 0x000003A8, 0x0004003B,
    0x00000625, 0x000014B9, 0x00000001, 0x00040020, 0x0000029A, 0x00000001,
    0x0000001D, 0x00040020, 0x0000029B, 0x00000003, 0x0000001D, 0x00040020,
    0x0000028A, 0x00000001, 0x0000000D, 0x00040020, 0x0000028B, 0x00000003,
    0x0000000D, 0x0004002B, 0x0000000B, 0x00000A3A, 0x00000010, 0x0004001C,
    0x00000656, 0x0000001D, 0x00000A3A, 0x00040020, 0x000008D3, 0x00000003,
    0x00000656, 0x0004003B, 0x000008D3, 0x00001536, 0x00000003, 0x0004001C,
    0x00000503, 0x00000656, 0x00000A16, 0x00040020, 0x0000077F, 0x00000001,
    0x00000503, 0x0004003B, 0x0000077F, 0x00000CE6, 0x00000001, 0x00040020,
    0x000008D4, 0x00000001, 0x00000656, 0x00040017, 0x00000013, 0x0000000D,
    0x00000002, 0x0004001C, 0x000002E4, 0x00000013, 0x00000A16, 0x00040020,
    0x00000561, 0x00000001, 0x000002E4, 0x0004003B, 0x00000561, 0x00000FCC,
    0x00000001, 0x0004001C, 0x00000266, 0x0000000D, 0x00000A16, 0x00040020,
    0x000004E3, 0x00000001, 0x00000266, 0x0004003B, 0x000004E3, 0x00001230,
    0x00000001, 0x00040020, 0x00000290, 0x00000003, 0x00000013, 0x0004003B,
    0x00000290, 0x00000E98, 0x00000003, 0x00050036, 0x00000008, 0x0000161F,
    0x00000000, 0x00000502, 0x000200F8, 0x000024EE, 0x0004003B, 0x000004CE,
    0x00001476, 0x00000007, 0x000200F9, 0x000046FD, 0x000200F8, 0x000046FD,
    0x000700F5, 0x0000000C, 0x000059AE, 0x00000A0B, 0x000024EE, 0x00002D83,
    0x00003B2A, 0x000500B1, 0x00000009, 0x000060D4, 0x000059AE, 0x00000A17,
    0x000400F6, 0x00002FE9, 0x00003B2A, 0x00000000, 0x000400FA, 0x000060D4,
    0x00003B2A, 0x00002FE9, 0x000200F8, 0x00003B2A, 0x0003003E, 0x00001476,
    0x00000236, 0x00050041, 0x00000289, 0x000057F0, 0x00001476, 0x000059AE,
    0x0004003D, 0x0000000C, 0x00001CCC, 0x000057F0, 0x00060041, 0x0000029A,
    0x00004EBA, 0x000014B9, 0x00001CCC, 0x00000A0B, 0x0004003D, 0x0000001D,
    0x0000579B, 0x00004EBA, 0x00050041, 0x0000029B, 0x00004E0D, 0x00001342,
    0x00000A0B, 0x0003003E, 0x00004E0D, 0x0000579B, 0x00060041, 0x0000028A,
    0x00004DC1, 0x000014B9, 0x00001CCC, 0x00000A0E, 0x0004003D, 0x0000000D,
    0x00001CDF, 0x00004DC1, 0x00050041, 0x0000028B, 0x00004E0E, 0x00001342,
    0x00000A0E, 0x0003003E, 0x00004E0E, 0x00001CDF, 0x00050041, 0x000008D4,
    0x00004D88, 0x00000CE6, 0x00001CCC, 0x0004003D, 0x00000656, 0x00002A7A,
    0x00004D88, 0x0003003E, 0x00001536, 0x00002A7A, 0x000100DA, 0x00050080,
    0x0000000C, 0x00002D83, 0x000059AE, 0x00000A0E, 0x000200F9, 0x000046FD,
    0x000200F8, 0x00002FE9, 0x000100DB, 0x000100FD, 0x00010038,
};
