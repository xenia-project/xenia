// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24790
; Schema: 0
               OpCapability Geometry
               OpCapability GeometryPointSize
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Geometry %5663 "main" %5305 %4930 %5430 %3302 %4044 %4656 %3736
               OpExecutionMode %5663 Triangles
               OpExecutionMode %5663 Invocations 1
               OpExecutionMode %5663 OutputTriangleStrip
               OpExecutionMode %5663 OutputVertices 6
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
    %v2float = OpTypeVector %float 2
       %bool = OpTypeBool
     %v2bool = OpTypeVector %bool 2
    %v4float = OpTypeVector %float 4
%_struct_1032 = OpTypeStruct %v4float %float
       %uint = OpTypeInt 32 0
     %uint_3 = OpConstant %uint 3
%_arr__struct_1032_uint_3 = OpTypeArray %_struct_1032 %uint_3
%_ptr_Input__arr__struct_1032_uint_3 = OpTypePointer Input %_arr__struct_1032_uint_3
       %5305 = OpVariable %_ptr_Input__arr__struct_1032_uint_3 Input
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
      %int_2 = OpConstant %int 2
     %uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
      %int_1 = OpConstant %int 1
     %uint_1 = OpConstant %uint 1
%float_0_00100000005 = OpConstant %float 0.00100000005
%_ptr_Input_v4float = OpTypePointer Input %v4float
%_struct_1033 = OpTypeStruct %v4float %float
%_ptr_Output__struct_1033 = OpTypePointer Output %_struct_1033
       %4930 = OpVariable %_ptr_Output__struct_1033 Output
%_ptr_Output_v4float = OpTypePointer Output %v4float
%_ptr_Output_float = OpTypePointer Output %float
    %uint_16 = OpConstant %uint 16
%_arr_v4float_uint_16 = OpTypeArray %v4float %uint_16
%_ptr_Output__arr_v4float_uint_16 = OpTypePointer Output %_arr_v4float_uint_16
       %5430 = OpVariable %_ptr_Output__arr_v4float_uint_16 Output
%_arr__arr_v4float_uint_16_uint_3 = OpTypeArray %_arr_v4float_uint_16 %uint_3
%_ptr_Input__arr__arr_v4float_uint_16_uint_3 = OpTypePointer Input %_arr__arr_v4float_uint_16_uint_3
       %3302 = OpVariable %_ptr_Input__arr__arr_v4float_uint_16_uint_3 Input
%_ptr_Input__arr_v4float_uint_16 = OpTypePointer Input %_arr_v4float_uint_16
     %int_16 = OpConstant %int 16
%_arr_v2float_uint_3 = OpTypeArray %v2float %uint_3
%_ptr_Input__arr_v2float_uint_3 = OpTypePointer Input %_arr_v2float_uint_3
       %4044 = OpVariable %_ptr_Input__arr_v2float_uint_3 Input
%_arr_float_uint_3 = OpTypeArray %float %uint_3
%_ptr_Input__arr_float_uint_3 = OpTypePointer Input %_arr_float_uint_3
       %4656 = OpVariable %_ptr_Input__arr_float_uint_3 Input
%_ptr_Output_v2float = OpTypePointer Output %v2float
       %3736 = OpVariable %_ptr_Output_v2float Output
       %1759 = OpConstantComposite %v2float %float_0_00100000005 %float_0_00100000005
       %5663 = OpFunction %void None %1282
      %23915 = OpLabel
       %7129 = OpAccessChain %_ptr_Input_float %5305 %int_2 %int_0 %uint_0
      %15627 = OpLoad %float %7129
      %20439 = OpAccessChain %_ptr_Input_float %5305 %int_1 %int_0 %uint_1
      %19889 = OpLoad %float %20439
      %10917 = OpCompositeConstruct %v2float %15627 %19889
      %24777 = OpAccessChain %_ptr_Input_v4float %5305 %int_0 %int_0
       %7883 = OpLoad %v4float %24777
       %6765 = OpVectorShuffle %v2float %7883 %7883 0 1
      %15739 = OpFSub %v2float %6765 %10917
       %7757 = OpExtInst %v2float %1 FAbs %15739
      %19021 = OpFOrdLessThanEqual %v2bool %7757 %1759
      %15711 = OpAll %bool %19021
      %11402 = OpLogicalNot %bool %15711
               OpSelectionMerge %13286 None
               OpBranchConditional %11402 %12129 %13286
      %12129 = OpLabel
      %18210 = OpAccessChain %_ptr_Input_float %5305 %int_1 %int_0 %uint_0
      %15628 = OpLoad %float %18210
      %20440 = OpAccessChain %_ptr_Input_float %5305 %int_2 %int_0 %uint_1
      %21143 = OpLoad %float %20440
      %17643 = OpCompositeConstruct %v2float %15628 %21143
      %15490 = OpFSub %v2float %6765 %17643
      %24406 = OpExtInst %v2float %1 FAbs %15490
      %20560 = OpFOrdLessThanEqual %v2bool %24406 %1759
      %20788 = OpAll %bool %20560
               OpBranch %13286
      %13286 = OpLabel
      %10924 = OpPhi %bool %15711 %23915 %20788 %12129
               OpSelectionMerge %23648 None
               OpBranchConditional %10924 %12148 %9186
      %12148 = OpLabel
      %18037 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %18037 %7883
      %19905 = OpAccessChain %_ptr_Input_float %5305 %int_0 %int_1
       %7391 = OpLoad %float %19905
      %19981 = OpAccessChain %_ptr_Output_float %4930 %int_1
               OpStore %19981 %7391
      %19848 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_0
      %10874 = OpLoad %_arr_v4float_uint_16 %19848
               OpStore %5430 %10874
               OpEmitVertex
      %22812 = OpAccessChain %_ptr_Input_v4float %5305 %int_1 %int_0
      %11398 = OpLoad %v4float %22812
               OpStore %18037 %11398
      %16622 = OpAccessChain %_ptr_Input_float %5305 %int_1 %int_1
       %7967 = OpLoad %float %16622
               OpStore %19981 %7967
      %16623 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_1
      %10875 = OpLoad %_arr_v4float_uint_16 %16623
               OpStore %5430 %10875
               OpEmitVertex
      %22813 = OpAccessChain %_ptr_Input_v4float %5305 %int_2 %int_0
      %11399 = OpLoad %v4float %22813
               OpStore %18037 %11399
      %16624 = OpAccessChain %_ptr_Input_float %5305 %int_2 %int_1
       %7968 = OpLoad %float %16624
               OpStore %19981 %7968
      %16625 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_2
      %10876 = OpLoad %_arr_v4float_uint_16 %16625
               OpStore %5430 %10876
               OpEmitVertex
               OpEndPrimitive
               OpStore %18037 %11399
               OpStore %19981 %7968
               OpStore %5430 %10876
               OpEmitVertex
               OpStore %18037 %11398
               OpStore %19981 %7967
               OpStore %5430 %10875
               OpEmitVertex
       %8851 = OpFNegate %v2float %6765
      %13757 = OpVectorShuffle %v2float %11398 %11398 0 1
      %21457 = OpFAdd %v2float %8851 %13757
       %7434 = OpVectorShuffle %v2float %11399 %11399 0 1
      %21812 = OpFAdd %v2float %21457 %7434
      %18423 = OpCompositeExtract %float %21812 0
      %14087 = OpCompositeExtract %float %21812 1
       %7641 = OpCompositeExtract %float %11399 2
       %7472 = OpCompositeExtract %float %11399 3
      %18779 = OpCompositeConstruct %v4float %18423 %14087 %7641 %7472
               OpStore %18037 %18779
               OpStore %19981 %7968
               OpBranch %17364
      %17364 = OpLabel
      %22958 = OpPhi %int %int_0 %12148 %21301 %14551
      %24788 = OpSLessThan %bool %22958 %int_16
               OpLoopMerge %11792 %14551 None
               OpBranchConditional %24788 %14551 %11792
      %14551 = OpLabel
      %19388 = OpAccessChain %_ptr_Input_v4float %3302 %int_0 %22958
      %24048 = OpLoad %v4float %19388
      %19880 = OpFNegate %v4float %24048
       %6667 = OpAccessChain %_ptr_Input_v4float %3302 %int_1 %22958
       %6828 = OpLoad %v4float %6667
      %22565 = OpFAdd %v4float %19880 %6828
      %18783 = OpAccessChain %_ptr_Input_v4float %3302 %int_2 %22958
      %21055 = OpLoad %v4float %18783
      %22584 = OpFAdd %v4float %22565 %21055
      %18591 = OpAccessChain %_ptr_Output_v4float %5430 %22958
               OpStore %18591 %22584
      %21301 = OpIAdd %int %22958 %int_1
               OpBranch %17364
      %11792 = OpLabel
               OpEmitVertex
               OpEndPrimitive
               OpBranch %23648
       %9186 = OpLabel
      %20459 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %20459 %7883
      %19906 = OpAccessChain %_ptr_Input_float %5305 %int_0 %int_1
       %7392 = OpLoad %float %19906
      %19982 = OpAccessChain %_ptr_Output_float %4930 %int_1
               OpStore %19982 %7392
      %19849 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_0
      %10877 = OpLoad %_arr_v4float_uint_16 %19849
               OpStore %5430 %10877
               OpEmitVertex
      %22814 = OpAccessChain %_ptr_Input_v4float %5305 %int_1 %int_0
      %11400 = OpLoad %v4float %22814
               OpStore %20459 %11400
      %16626 = OpAccessChain %_ptr_Input_float %5305 %int_1 %int_1
       %7969 = OpLoad %float %16626
               OpStore %19982 %7969
      %16627 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_1
      %10878 = OpLoad %_arr_v4float_uint_16 %16627
               OpStore %5430 %10878
               OpEmitVertex
      %22815 = OpAccessChain %_ptr_Input_v4float %5305 %int_2 %int_0
      %11401 = OpLoad %v4float %22815
               OpStore %20459 %11401
      %16628 = OpAccessChain %_ptr_Input_float %5305 %int_2 %int_1
       %7970 = OpLoad %float %16628
               OpStore %19982 %7970
      %16629 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3302 %int_2
      %10879 = OpLoad %_arr_v4float_uint_16 %16629
               OpStore %5430 %10879
               OpEmitVertex
               OpEndPrimitive
               OpStore %20459 %7883
               OpStore %19982 %7392
               OpStore %5430 %10877
               OpEmitVertex
               OpStore %20459 %11401
               OpStore %19982 %7970
               OpStore %5430 %10879
               OpEmitVertex
      %12391 = OpVectorShuffle %v2float %11400 %11400 0 1
      %21222 = OpFNegate %v2float %12391
       %8335 = OpFAdd %v2float %6765 %21222
      %13861 = OpVectorShuffle %v2float %11401 %11401 0 1
      %21813 = OpFAdd %v2float %8335 %13861
      %18424 = OpCompositeExtract %float %21813 0
      %14088 = OpCompositeExtract %float %21813 1
       %7642 = OpCompositeExtract %float %11401 2
       %7473 = OpCompositeExtract %float %11401 3
      %18780 = OpCompositeConstruct %v4float %18424 %14088 %7642 %7473
               OpStore %20459 %18780
               OpStore %19982 %7970
               OpBranch %17365
      %17365 = OpLabel
      %22959 = OpPhi %int %int_0 %9186 %21302 %14552
      %24789 = OpSLessThan %bool %22959 %int_16
               OpLoopMerge %11793 %14552 None
               OpBranchConditional %24789 %14552 %11793
      %14552 = OpLabel
      %18211 = OpAccessChain %_ptr_Input_v4float %3302 %int_0 %22959
      %15629 = OpLoad %v4float %18211
      %21332 = OpAccessChain %_ptr_Input_v4float %3302 %int_1 %22959
      %12974 = OpLoad %v4float %21332
       %8884 = OpFNegate %v4float %12974
       %7862 = OpFAdd %v4float %15629 %8884
      %14199 = OpAccessChain %_ptr_Input_v4float %3302 %int_2 %22959
      %21056 = OpLoad %v4float %14199
      %22585 = OpFAdd %v4float %7862 %21056
      %18592 = OpAccessChain %_ptr_Output_v4float %5430 %22959
               OpStore %18592 %22585
      %21302 = OpIAdd %int %22959 %int_1
               OpBranch %17365
      %11793 = OpLabel
               OpEmitVertex
               OpEndPrimitive
               OpBranch %23648
      %23648 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t rect_list_gs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x000060D6, 0x00000000, 0x00020011,
    0x00000002, 0x00020011, 0x00000018, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x000C000F, 0x00000003, 0x0000161F, 0x6E69616D, 0x00000000, 0x000014B9,
    0x00001342, 0x00001536, 0x00000CE6, 0x00000FCC, 0x00001230, 0x00000E98,
    0x00030010, 0x0000161F, 0x00000016, 0x00040010, 0x0000161F, 0x00000000,
    0x00000001, 0x00030010, 0x0000161F, 0x0000001D, 0x00040010, 0x0000161F,
    0x0000001A, 0x00000006, 0x00050048, 0x00000408, 0x00000000, 0x0000000B,
    0x00000000, 0x00050048, 0x00000408, 0x00000001, 0x0000000B, 0x00000001,
    0x00030047, 0x00000408, 0x00000002, 0x00050048, 0x00000409, 0x00000000,
    0x0000000B, 0x00000000, 0x00050048, 0x00000409, 0x00000001, 0x0000000B,
    0x00000001, 0x00030047, 0x00000409, 0x00000002, 0x00040047, 0x00001536,
    0x0000001E, 0x00000000, 0x00040047, 0x00000CE6, 0x0000001E, 0x00000000,
    0x00040047, 0x00000FCC, 0x0000001E, 0x00000010, 0x00040047, 0x00001230,
    0x0000001E, 0x00000011, 0x00040047, 0x00000E98, 0x0000001E, 0x00000010,
    0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00030016,
    0x0000000D, 0x00000020, 0x00040017, 0x00000013, 0x0000000D, 0x00000002,
    0x00020014, 0x00000009, 0x00040017, 0x0000000F, 0x00000009, 0x00000002,
    0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x0004001E, 0x00000408,
    0x0000001D, 0x0000000D, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x0004002B, 0x0000000B, 0x00000A13, 0x00000003, 0x0004001C, 0x0000085F,
    0x00000408, 0x00000A13, 0x00040020, 0x00000ADC, 0x00000001, 0x0000085F,
    0x0004003B, 0x00000ADC, 0x000014B9, 0x00000001, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000,
    0x0004002B, 0x0000000C, 0x00000A11, 0x00000002, 0x0004002B, 0x0000000B,
    0x00000A0A, 0x00000000, 0x00040020, 0x0000028A, 0x00000001, 0x0000000D,
    0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001, 0x0004002B, 0x0000000B,
    0x00000A0D, 0x00000001, 0x0004002B, 0x0000000D, 0x00000030, 0x3A83126F,
    0x00040020, 0x0000029A, 0x00000001, 0x0000001D, 0x0004001E, 0x00000409,
    0x0000001D, 0x0000000D, 0x00040020, 0x00000685, 0x00000003, 0x00000409,
    0x0004003B, 0x00000685, 0x00001342, 0x00000003, 0x00040020, 0x0000029B,
    0x00000003, 0x0000001D, 0x00040020, 0x0000028B, 0x00000003, 0x0000000D,
    0x0004002B, 0x0000000B, 0x00000A3A, 0x00000010, 0x0004001C, 0x000008F6,
    0x0000001D, 0x00000A3A, 0x00040020, 0x00000B73, 0x00000003, 0x000008F6,
    0x0004003B, 0x00000B73, 0x00001536, 0x00000003, 0x0004001C, 0x0000084A,
    0x000008F6, 0x00000A13, 0x00040020, 0x00000AC7, 0x00000001, 0x0000084A,
    0x0004003B, 0x00000AC7, 0x00000CE6, 0x00000001, 0x00040020, 0x00000B74,
    0x00000001, 0x000008F6, 0x0004002B, 0x0000000C, 0x00000A3B, 0x00000010,
    0x0004001C, 0x00000352, 0x00000013, 0x00000A13, 0x00040020, 0x000005CF,
    0x00000001, 0x00000352, 0x0004003B, 0x000005CF, 0x00000FCC, 0x00000001,
    0x0004001C, 0x00000298, 0x0000000D, 0x00000A13, 0x00040020, 0x00000515,
    0x00000001, 0x00000298, 0x0004003B, 0x00000515, 0x00001230, 0x00000001,
    0x00040020, 0x00000290, 0x00000003, 0x00000013, 0x0004003B, 0x00000290,
    0x00000E98, 0x00000003, 0x0005002C, 0x00000013, 0x000006DF, 0x00000030,
    0x00000030, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x00005D6B, 0x00070041, 0x0000028A, 0x00001BD9, 0x000014B9,
    0x00000A11, 0x00000A0B, 0x00000A0A, 0x0004003D, 0x0000000D, 0x00003D0B,
    0x00001BD9, 0x00070041, 0x0000028A, 0x00004FD7, 0x000014B9, 0x00000A0E,
    0x00000A0B, 0x00000A0D, 0x0004003D, 0x0000000D, 0x00004DB1, 0x00004FD7,
    0x00050050, 0x00000013, 0x00002AA5, 0x00003D0B, 0x00004DB1, 0x00060041,
    0x0000029A, 0x000060C9, 0x000014B9, 0x00000A0B, 0x00000A0B, 0x0004003D,
    0x0000001D, 0x00001ECB, 0x000060C9, 0x0007004F, 0x00000013, 0x00001A6D,
    0x00001ECB, 0x00001ECB, 0x00000000, 0x00000001, 0x00050083, 0x00000013,
    0x00003D7B, 0x00001A6D, 0x00002AA5, 0x0006000C, 0x00000013, 0x00001E4D,
    0x00000001, 0x00000004, 0x00003D7B, 0x000500BC, 0x0000000F, 0x00004A4D,
    0x00001E4D, 0x000006DF, 0x0004009B, 0x00000009, 0x00003D5F, 0x00004A4D,
    0x000400A8, 0x00000009, 0x00002C8A, 0x00003D5F, 0x000300F7, 0x000033E6,
    0x00000000, 0x000400FA, 0x00002C8A, 0x00002F61, 0x000033E6, 0x000200F8,
    0x00002F61, 0x00070041, 0x0000028A, 0x00004722, 0x000014B9, 0x00000A0E,
    0x00000A0B, 0x00000A0A, 0x0004003D, 0x0000000D, 0x00003D0C, 0x00004722,
    0x00070041, 0x0000028A, 0x00004FD8, 0x000014B9, 0x00000A11, 0x00000A0B,
    0x00000A0D, 0x0004003D, 0x0000000D, 0x00005297, 0x00004FD8, 0x00050050,
    0x00000013, 0x000044EB, 0x00003D0C, 0x00005297, 0x00050083, 0x00000013,
    0x00003C82, 0x00001A6D, 0x000044EB, 0x0006000C, 0x00000013, 0x00005F56,
    0x00000001, 0x00000004, 0x00003C82, 0x000500BC, 0x0000000F, 0x00005050,
    0x00005F56, 0x000006DF, 0x0004009B, 0x00000009, 0x00005134, 0x00005050,
    0x000200F9, 0x000033E6, 0x000200F8, 0x000033E6, 0x000700F5, 0x00000009,
    0x00002AAC, 0x00003D5F, 0x00005D6B, 0x00005134, 0x00002F61, 0x000300F7,
    0x00005C60, 0x00000000, 0x000400FA, 0x00002AAC, 0x00002F74, 0x000023E2,
    0x000200F8, 0x00002F74, 0x00050041, 0x0000029B, 0x00004675, 0x00001342,
    0x00000A0B, 0x0003003E, 0x00004675, 0x00001ECB, 0x00060041, 0x0000028A,
    0x00004DC1, 0x000014B9, 0x00000A0B, 0x00000A0E, 0x0004003D, 0x0000000D,
    0x00001CDF, 0x00004DC1, 0x00050041, 0x0000028B, 0x00004E0D, 0x00001342,
    0x00000A0E, 0x0003003E, 0x00004E0D, 0x00001CDF, 0x00050041, 0x00000B74,
    0x00004D88, 0x00000CE6, 0x00000A0B, 0x0004003D, 0x000008F6, 0x00002A7A,
    0x00004D88, 0x0003003E, 0x00001536, 0x00002A7A, 0x000100DA, 0x00060041,
    0x0000029A, 0x0000591C, 0x000014B9, 0x00000A0E, 0x00000A0B, 0x0004003D,
    0x0000001D, 0x00002C86, 0x0000591C, 0x0003003E, 0x00004675, 0x00002C86,
    0x00060041, 0x0000028A, 0x000040EE, 0x000014B9, 0x00000A0E, 0x00000A0E,
    0x0004003D, 0x0000000D, 0x00001F1F, 0x000040EE, 0x0003003E, 0x00004E0D,
    0x00001F1F, 0x00050041, 0x00000B74, 0x000040EF, 0x00000CE6, 0x00000A0E,
    0x0004003D, 0x000008F6, 0x00002A7B, 0x000040EF, 0x0003003E, 0x00001536,
    0x00002A7B, 0x000100DA, 0x00060041, 0x0000029A, 0x0000591D, 0x000014B9,
    0x00000A11, 0x00000A0B, 0x0004003D, 0x0000001D, 0x00002C87, 0x0000591D,
    0x0003003E, 0x00004675, 0x00002C87, 0x00060041, 0x0000028A, 0x000040F0,
    0x000014B9, 0x00000A11, 0x00000A0E, 0x0004003D, 0x0000000D, 0x00001F20,
    0x000040F0, 0x0003003E, 0x00004E0D, 0x00001F20, 0x00050041, 0x00000B74,
    0x000040F1, 0x00000CE6, 0x00000A11, 0x0004003D, 0x000008F6, 0x00002A7C,
    0x000040F1, 0x0003003E, 0x00001536, 0x00002A7C, 0x000100DA, 0x000100DB,
    0x0003003E, 0x00004675, 0x00002C87, 0x0003003E, 0x00004E0D, 0x00001F20,
    0x0003003E, 0x00001536, 0x00002A7C, 0x000100DA, 0x0003003E, 0x00004675,
    0x00002C86, 0x0003003E, 0x00004E0D, 0x00001F1F, 0x0003003E, 0x00001536,
    0x00002A7B, 0x000100DA, 0x0004007F, 0x00000013, 0x00002293, 0x00001A6D,
    0x0007004F, 0x00000013, 0x000035BD, 0x00002C86, 0x00002C86, 0x00000000,
    0x00000001, 0x00050081, 0x00000013, 0x000053D1, 0x00002293, 0x000035BD,
    0x0007004F, 0x00000013, 0x00001D0A, 0x00002C87, 0x00002C87, 0x00000000,
    0x00000001, 0x00050081, 0x00000013, 0x00005534, 0x000053D1, 0x00001D0A,
    0x00050051, 0x0000000D, 0x000047F7, 0x00005534, 0x00000000, 0x00050051,
    0x0000000D, 0x00003707, 0x00005534, 0x00000001, 0x00050051, 0x0000000D,
    0x00001DD9, 0x00002C87, 0x00000002, 0x00050051, 0x0000000D, 0x00001D30,
    0x00002C87, 0x00000003, 0x00070050, 0x0000001D, 0x0000495B, 0x000047F7,
    0x00003707, 0x00001DD9, 0x00001D30, 0x0003003E, 0x00004675, 0x0000495B,
    0x0003003E, 0x00004E0D, 0x00001F20, 0x000200F9, 0x000043D4, 0x000200F8,
    0x000043D4, 0x000700F5, 0x0000000C, 0x000059AE, 0x00000A0B, 0x00002F74,
    0x00005335, 0x000038D7, 0x000500B1, 0x00000009, 0x000060D4, 0x000059AE,
    0x00000A3B, 0x000400F6, 0x00002E10, 0x000038D7, 0x00000000, 0x000400FA,
    0x000060D4, 0x000038D7, 0x00002E10, 0x000200F8, 0x000038D7, 0x00060041,
    0x0000029A, 0x00004BBC, 0x00000CE6, 0x00000A0B, 0x000059AE, 0x0004003D,
    0x0000001D, 0x00005DF0, 0x00004BBC, 0x0004007F, 0x0000001D, 0x00004DA8,
    0x00005DF0, 0x00060041, 0x0000029A, 0x00001A0B, 0x00000CE6, 0x00000A0E,
    0x000059AE, 0x0004003D, 0x0000001D, 0x00001AAC, 0x00001A0B, 0x00050081,
    0x0000001D, 0x00005825, 0x00004DA8, 0x00001AAC, 0x00060041, 0x0000029A,
    0x0000495F, 0x00000CE6, 0x00000A11, 0x000059AE, 0x0004003D, 0x0000001D,
    0x0000523F, 0x0000495F, 0x00050081, 0x0000001D, 0x00005838, 0x00005825,
    0x0000523F, 0x00050041, 0x0000029B, 0x0000489F, 0x00001536, 0x000059AE,
    0x0003003E, 0x0000489F, 0x00005838, 0x00050080, 0x0000000C, 0x00005335,
    0x000059AE, 0x00000A0E, 0x000200F9, 0x000043D4, 0x000200F8, 0x00002E10,
    0x000100DA, 0x000100DB, 0x000200F9, 0x00005C60, 0x000200F8, 0x000023E2,
    0x00050041, 0x0000029B, 0x00004FEB, 0x00001342, 0x00000A0B, 0x0003003E,
    0x00004FEB, 0x00001ECB, 0x00060041, 0x0000028A, 0x00004DC2, 0x000014B9,
    0x00000A0B, 0x00000A0E, 0x0004003D, 0x0000000D, 0x00001CE0, 0x00004DC2,
    0x00050041, 0x0000028B, 0x00004E0E, 0x00001342, 0x00000A0E, 0x0003003E,
    0x00004E0E, 0x00001CE0, 0x00050041, 0x00000B74, 0x00004D89, 0x00000CE6,
    0x00000A0B, 0x0004003D, 0x000008F6, 0x00002A7D, 0x00004D89, 0x0003003E,
    0x00001536, 0x00002A7D, 0x000100DA, 0x00060041, 0x0000029A, 0x0000591E,
    0x000014B9, 0x00000A0E, 0x00000A0B, 0x0004003D, 0x0000001D, 0x00002C88,
    0x0000591E, 0x0003003E, 0x00004FEB, 0x00002C88, 0x00060041, 0x0000028A,
    0x000040F2, 0x000014B9, 0x00000A0E, 0x00000A0E, 0x0004003D, 0x0000000D,
    0x00001F21, 0x000040F2, 0x0003003E, 0x00004E0E, 0x00001F21, 0x00050041,
    0x00000B74, 0x000040F3, 0x00000CE6, 0x00000A0E, 0x0004003D, 0x000008F6,
    0x00002A7E, 0x000040F3, 0x0003003E, 0x00001536, 0x00002A7E, 0x000100DA,
    0x00060041, 0x0000029A, 0x0000591F, 0x000014B9, 0x00000A11, 0x00000A0B,
    0x0004003D, 0x0000001D, 0x00002C89, 0x0000591F, 0x0003003E, 0x00004FEB,
    0x00002C89, 0x00060041, 0x0000028A, 0x000040F4, 0x000014B9, 0x00000A11,
    0x00000A0E, 0x0004003D, 0x0000000D, 0x00001F22, 0x000040F4, 0x0003003E,
    0x00004E0E, 0x00001F22, 0x00050041, 0x00000B74, 0x000040F5, 0x00000CE6,
    0x00000A11, 0x0004003D, 0x000008F6, 0x00002A7F, 0x000040F5, 0x0003003E,
    0x00001536, 0x00002A7F, 0x000100DA, 0x000100DB, 0x0003003E, 0x00004FEB,
    0x00001ECB, 0x0003003E, 0x00004E0E, 0x00001CE0, 0x0003003E, 0x00001536,
    0x00002A7D, 0x000100DA, 0x0003003E, 0x00004FEB, 0x00002C89, 0x0003003E,
    0x00004E0E, 0x00001F22, 0x0003003E, 0x00001536, 0x00002A7F, 0x000100DA,
    0x0007004F, 0x00000013, 0x00003067, 0x00002C88, 0x00002C88, 0x00000000,
    0x00000001, 0x0004007F, 0x00000013, 0x000052E6, 0x00003067, 0x00050081,
    0x00000013, 0x0000208F, 0x00001A6D, 0x000052E6, 0x0007004F, 0x00000013,
    0x00003625, 0x00002C89, 0x00002C89, 0x00000000, 0x00000001, 0x00050081,
    0x00000013, 0x00005535, 0x0000208F, 0x00003625, 0x00050051, 0x0000000D,
    0x000047F8, 0x00005535, 0x00000000, 0x00050051, 0x0000000D, 0x00003708,
    0x00005535, 0x00000001, 0x00050051, 0x0000000D, 0x00001DDA, 0x00002C89,
    0x00000002, 0x00050051, 0x0000000D, 0x00001D31, 0x00002C89, 0x00000003,
    0x00070050, 0x0000001D, 0x0000495C, 0x000047F8, 0x00003708, 0x00001DDA,
    0x00001D31, 0x0003003E, 0x00004FEB, 0x0000495C, 0x0003003E, 0x00004E0E,
    0x00001F22, 0x000200F9, 0x000043D5, 0x000200F8, 0x000043D5, 0x000700F5,
    0x0000000C, 0x000059AF, 0x00000A0B, 0x000023E2, 0x00005336, 0x000038D8,
    0x000500B1, 0x00000009, 0x000060D5, 0x000059AF, 0x00000A3B, 0x000400F6,
    0x00002E11, 0x000038D8, 0x00000000, 0x000400FA, 0x000060D5, 0x000038D8,
    0x00002E11, 0x000200F8, 0x000038D8, 0x00060041, 0x0000029A, 0x00004723,
    0x00000CE6, 0x00000A0B, 0x000059AF, 0x0004003D, 0x0000001D, 0x00003D0D,
    0x00004723, 0x00060041, 0x0000029A, 0x00005354, 0x00000CE6, 0x00000A0E,
    0x000059AF, 0x0004003D, 0x0000001D, 0x000032AE, 0x00005354, 0x0004007F,
    0x0000001D, 0x000022B4, 0x000032AE, 0x00050081, 0x0000001D, 0x00001EB6,
    0x00003D0D, 0x000022B4, 0x00060041, 0x0000029A, 0x00003777, 0x00000CE6,
    0x00000A11, 0x000059AF, 0x0004003D, 0x0000001D, 0x00005240, 0x00003777,
    0x00050081, 0x0000001D, 0x00005839, 0x00001EB6, 0x00005240, 0x00050041,
    0x0000029B, 0x000048A0, 0x00001536, 0x000059AF, 0x0003003E, 0x000048A0,
    0x00005839, 0x00050080, 0x0000000C, 0x00005336, 0x000059AF, 0x00000A0E,
    0x000200F9, 0x000043D5, 0x000200F8, 0x00002E11, 0x000100DA, 0x000100DB,
    0x000200F9, 0x00005C60, 0x000200F8, 0x00005C60, 0x000100FD, 0x00010038,
};
