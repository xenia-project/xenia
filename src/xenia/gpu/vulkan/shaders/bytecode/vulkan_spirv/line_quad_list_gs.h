// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 23916
; Schema: 0
               OpCapability Geometry
               OpCapability GeometryPointSize
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Geometry %5663 "main" %4930 %5305 %5430 %3302 %4044 %4656 %3736
               OpExecutionMode %5663 InputLinesAdjacency
               OpExecutionMode %5663 Invocations 1
               OpExecutionMode %5663 OutputLineStrip
               OpExecutionMode %5663 OutputVertices 5
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
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_struct_1032 = OpTypeStruct %v4float %float
%_ptr_Output__struct_1032 = OpTypePointer Output %_struct_1032
       %4930 = OpVariable %_ptr_Output__struct_1032 Output
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
%_struct_1033 = OpTypeStruct %v4float %float
       %uint = OpTypeInt 32 0
     %uint_4 = OpConstant %uint 4
%_arr__struct_1033_uint_4 = OpTypeArray %_struct_1033 %uint_4
%_ptr_Input__arr__struct_1033_uint_4 = OpTypePointer Input %_arr__struct_1033_uint_4
       %5305 = OpVariable %_ptr_Input__arr__struct_1033_uint_4 Input
%_ptr_Input_v4float = OpTypePointer Input %v4float
%_ptr_Output_v4float = OpTypePointer Output %v4float
      %int_1 = OpConstant %int 1
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
      %int_2 = OpConstant %int 2
      %int_3 = OpConstant %int 3
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
      %23915 = OpLabel
       %7129 = OpAccessChain %_ptr_Input_v4float %5305 %int_0 %int_0
      %15646 = OpLoad %v4float %7129
      %19981 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %19981 %15646
      %19905 = OpAccessChain %_ptr_Input_float %5305 %int_0 %int_1
       %7391 = OpLoad %float %19905
      %19982 = OpAccessChain %_ptr_Output_float %4930 %int_1
               OpStore %19982 %7391
      %19848 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_0
      %10874 = OpLoad %_arr_v4float_uint_16 %19848
               OpStore %5430 %10874
               OpEmitVertex
      %22812 = OpAccessChain %_ptr_Input_v4float %5305 %int_1 %int_0
      %11398 = OpLoad %v4float %22812
               OpStore %19981 %11398
      %16622 = OpAccessChain %_ptr_Input_float %5305 %int_1 %int_1
       %7967 = OpLoad %float %16622
               OpStore %19982 %7967
      %16623 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_1
      %10875 = OpLoad %_arr_v4float_uint_16 %16623
               OpStore %5430 %10875
               OpEmitVertex
      %22813 = OpAccessChain %_ptr_Input_v4float %5305 %int_2 %int_0
      %11399 = OpLoad %v4float %22813
               OpStore %19981 %11399
      %16624 = OpAccessChain %_ptr_Input_float %5305 %int_2 %int_1
       %7968 = OpLoad %float %16624
               OpStore %19982 %7968
      %16625 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_2
      %10876 = OpLoad %_arr_v4float_uint_16 %16625
               OpStore %5430 %10876
               OpEmitVertex
      %22814 = OpAccessChain %_ptr_Input_v4float %5305 %int_3 %int_0
      %11400 = OpLoad %v4float %22814
               OpStore %19981 %11400
      %16626 = OpAccessChain %_ptr_Input_float %5305 %int_3 %int_1
       %7969 = OpLoad %float %16626
               OpStore %19982 %7969
      %16627 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_3
      %10877 = OpLoad %_arr_v4float_uint_16 %16627
               OpStore %5430 %10877
               OpEmitVertex
               OpStore %19981 %15646
               OpStore %19982 %7391
               OpStore %5430 %10874
               OpEmitVertex
               OpEndPrimitive
               OpReturn
               OpFunctionEnd
#endif

const uint32_t line_quad_list_gs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00005D6C, 0x00000000, 0x00020011,
    0x00000002, 0x00020011, 0x00000018, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x000C000F, 0x00000003, 0x0000161F, 0x6E69616D, 0x00000000, 0x00001342,
    0x000014B9, 0x00001536, 0x00000CE6, 0x00000FCC, 0x00001230, 0x00000E98,
    0x00030010, 0x0000161F, 0x00000015, 0x00040010, 0x0000161F, 0x00000000,
    0x00000001, 0x00030010, 0x0000161F, 0x0000001C, 0x00040010, 0x0000161F,
    0x0000001A, 0x00000005, 0x00050048, 0x00000408, 0x00000000, 0x0000000B,
    0x00000000, 0x00050048, 0x00000408, 0x00000001, 0x0000000B, 0x00000001,
    0x00030047, 0x00000408, 0x00000002, 0x00050048, 0x00000409, 0x00000000,
    0x0000000B, 0x00000000, 0x00050048, 0x00000409, 0x00000001, 0x0000000B,
    0x00000001, 0x00030047, 0x00000409, 0x00000002, 0x00040047, 0x00001536,
    0x0000001E, 0x00000000, 0x00040047, 0x00000CE6, 0x0000001E, 0x00000000,
    0x00040047, 0x00000FCC, 0x0000001E, 0x00000010, 0x00040047, 0x00001230,
    0x0000001E, 0x00000011, 0x00040047, 0x00000E98, 0x0000001E, 0x00000010,
    0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004,
    0x0004001E, 0x00000408, 0x0000001D, 0x0000000D, 0x00040020, 0x00000685,
    0x00000003, 0x00000408, 0x0004003B, 0x00000685, 0x00001342, 0x00000003,
    0x00040015, 0x0000000C, 0x00000020, 0x00000001, 0x0004002B, 0x0000000C,
    0x00000A0B, 0x00000000, 0x0004001E, 0x00000409, 0x0000001D, 0x0000000D,
    0x00040015, 0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B,
    0x00000A16, 0x00000004, 0x0004001C, 0x0000032E, 0x00000409, 0x00000A16,
    0x00040020, 0x000005AB, 0x00000001, 0x0000032E, 0x0004003B, 0x000005AB,
    0x000014B9, 0x00000001, 0x00040020, 0x0000029A, 0x00000001, 0x0000001D,
    0x00040020, 0x0000029B, 0x00000003, 0x0000001D, 0x0004002B, 0x0000000C,
    0x00000A0E, 0x00000001, 0x00040020, 0x0000028A, 0x00000001, 0x0000000D,
    0x00040020, 0x0000028B, 0x00000003, 0x0000000D, 0x0004002B, 0x0000000B,
    0x00000A3A, 0x00000010, 0x0004001C, 0x00000473, 0x0000001D, 0x00000A3A,
    0x00040020, 0x000006F0, 0x00000003, 0x00000473, 0x0004003B, 0x000006F0,
    0x00001536, 0x00000003, 0x0004001C, 0x00000973, 0x00000473, 0x00000A16,
    0x00040020, 0x0000002D, 0x00000001, 0x00000973, 0x0004003B, 0x0000002D,
    0x00000CE6, 0x00000001, 0x00040020, 0x000006F1, 0x00000001, 0x00000473,
    0x0004002B, 0x0000000C, 0x00000A11, 0x00000002, 0x0004002B, 0x0000000C,
    0x00000A14, 0x00000003, 0x00040017, 0x00000013, 0x0000000D, 0x00000002,
    0x0004001C, 0x000002A2, 0x00000013, 0x00000A16, 0x00040020, 0x0000051F,
    0x00000001, 0x000002A2, 0x0004003B, 0x0000051F, 0x00000FCC, 0x00000001,
    0x0004001C, 0x00000248, 0x0000000D, 0x00000A16, 0x00040020, 0x000004C5,
    0x00000001, 0x00000248, 0x0004003B, 0x000004C5, 0x00001230, 0x00000001,
    0x00040020, 0x00000290, 0x00000003, 0x00000013, 0x0004003B, 0x00000290,
    0x00000E98, 0x00000003, 0x00050036, 0x00000008, 0x0000161F, 0x00000000,
    0x00000502, 0x000200F8, 0x00005D6B, 0x00060041, 0x0000029A, 0x00001BD9,
    0x000014B9, 0x00000A0B, 0x00000A0B, 0x0004003D, 0x0000001D, 0x00003D1E,
    0x00001BD9, 0x00050041, 0x0000029B, 0x00004E0D, 0x00001342, 0x00000A0B,
    0x0003003E, 0x00004E0D, 0x00003D1E, 0x00060041, 0x0000028A, 0x00004DC1,
    0x000014B9, 0x00000A0B, 0x00000A0E, 0x0004003D, 0x0000000D, 0x00001CDF,
    0x00004DC1, 0x00050041, 0x0000028B, 0x00004E0E, 0x00001342, 0x00000A0E,
    0x0003003E, 0x00004E0E, 0x00001CDF, 0x00050041, 0x000006F1, 0x00004D88,
    0x00000CE6, 0x00000A0B, 0x0004003D, 0x00000473, 0x00002A7A, 0x00004D88,
    0x0003003E, 0x00001536, 0x00002A7A, 0x000100DA, 0x00060041, 0x0000029A,
    0x0000591C, 0x000014B9, 0x00000A0E, 0x00000A0B, 0x0004003D, 0x0000001D,
    0x00002C86, 0x0000591C, 0x0003003E, 0x00004E0D, 0x00002C86, 0x00060041,
    0x0000028A, 0x000040EE, 0x000014B9, 0x00000A0E, 0x00000A0E, 0x0004003D,
    0x0000000D, 0x00001F1F, 0x000040EE, 0x0003003E, 0x00004E0E, 0x00001F1F,
    0x00050041, 0x000006F1, 0x000040EF, 0x00000CE6, 0x00000A0E, 0x0004003D,
    0x00000473, 0x00002A7B, 0x000040EF, 0x0003003E, 0x00001536, 0x00002A7B,
    0x000100DA, 0x00060041, 0x0000029A, 0x0000591D, 0x000014B9, 0x00000A11,
    0x00000A0B, 0x0004003D, 0x0000001D, 0x00002C87, 0x0000591D, 0x0003003E,
    0x00004E0D, 0x00002C87, 0x00060041, 0x0000028A, 0x000040F0, 0x000014B9,
    0x00000A11, 0x00000A0E, 0x0004003D, 0x0000000D, 0x00001F20, 0x000040F0,
    0x0003003E, 0x00004E0E, 0x00001F20, 0x00050041, 0x000006F1, 0x000040F1,
    0x00000CE6, 0x00000A11, 0x0004003D, 0x00000473, 0x00002A7C, 0x000040F1,
    0x0003003E, 0x00001536, 0x00002A7C, 0x000100DA, 0x00060041, 0x0000029A,
    0x0000591E, 0x000014B9, 0x00000A14, 0x00000A0B, 0x0004003D, 0x0000001D,
    0x00002C88, 0x0000591E, 0x0003003E, 0x00004E0D, 0x00002C88, 0x00060041,
    0x0000028A, 0x000040F2, 0x000014B9, 0x00000A14, 0x00000A0E, 0x0004003D,
    0x0000000D, 0x00001F21, 0x000040F2, 0x0003003E, 0x00004E0E, 0x00001F21,
    0x00050041, 0x000006F1, 0x000040F3, 0x00000CE6, 0x00000A14, 0x0004003D,
    0x00000473, 0x00002A7D, 0x000040F3, 0x0003003E, 0x00001536, 0x00002A7D,
    0x000100DA, 0x0003003E, 0x00004E0D, 0x00003D1E, 0x0003003E, 0x00004E0E,
    0x00001CDF, 0x0003003E, 0x00001536, 0x00002A7A, 0x000100DA, 0x000100DB,
    0x000100FD, 0x00010038,
};
