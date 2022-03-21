// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24886
; Schema: 0
               OpCapability Geometry
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Geometry %5663 "main" %5305 %3631 %3144 %4930
               OpExecutionMode %5663 Triangles
               OpExecutionMode %5663 Invocations 1
               OpExecutionMode %5663 OutputTriangleStrip
               OpExecutionMode %5663 OutputVertices 6
               OpMemberDecorate %_struct_1017 0 BuiltIn Position
               OpDecorate %_struct_1017 Block
               OpDecorate %3631 Location 0
               OpDecorate %3144 Location 0
               OpMemberDecorate %_struct_1018 0 BuiltIn Position
               OpDecorate %_struct_1018 Block
               OpDecorate %7509 NoContraction
               OpDecorate %15269 NoContraction
               OpDecorate %24885 NoContraction
               OpDecorate %14166 NoContraction
               OpDecorate %7062 NoContraction
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %bool = OpTypeBool
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_struct_1017 = OpTypeStruct %v4float
       %uint = OpTypeInt 32 0
     %uint_3 = OpConstant %uint 3
%_arr__struct_1017_uint_3 = OpTypeArray %_struct_1017 %uint_3
%_ptr_Input__arr__struct_1017_uint_3 = OpTypePointer Input %_arr__struct_1017_uint_3
       %5305 = OpVariable %_ptr_Input__arr__struct_1017_uint_3 Input
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
%_ptr_Input_v4float = OpTypePointer Input %v4float
     %v4bool = OpTypeVector %bool 4
      %int_1 = OpConstant %int 1
      %int_2 = OpConstant %int 2
     %uint_0 = OpConstant %uint 0
    %uint_16 = OpConstant %uint 16
%_arr_v4float_uint_16 = OpTypeArray %v4float %uint_16
%_ptr_Output__arr_v4float_uint_16 = OpTypePointer Output %_arr_v4float_uint_16
       %3631 = OpVariable %_ptr_Output__arr_v4float_uint_16 Output
%_arr__arr_v4float_uint_16_uint_3 = OpTypeArray %_arr_v4float_uint_16 %uint_3
%_ptr_Input__arr__arr_v4float_uint_16_uint_3 = OpTypePointer Input %_arr__arr_v4float_uint_16_uint_3
       %3144 = OpVariable %_ptr_Input__arr__arr_v4float_uint_16_uint_3 Input
%_ptr_Input__arr_v4float_uint_16 = OpTypePointer Input %_arr_v4float_uint_16
%_struct_1018 = OpTypeStruct %v4float
%_ptr_Output__struct_1018 = OpTypePointer Output %_struct_1018
       %4930 = OpVariable %_ptr_Output__struct_1018 Output
%_ptr_Output_v4float = OpTypePointer Output %v4float
    %v3float = OpTypeVector %float 3
   %float_n1 = OpConstant %float -1
    %float_1 = OpConstant %float 1
        %266 = OpConstantComposite %v3float %float_n1 %float_1 %float_1
       %2582 = OpConstantComposite %v3float %float_1 %float_n1 %float_1
        %267 = OpConstantComposite %v3float %float_1 %float_1 %float_n1
     %v3bool = OpTypeVector %bool 3
       %5663 = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %23648 None
               OpSwitch %uint_0 %11880
      %11880 = OpLabel
      %23974 = OpAccessChain %_ptr_Input_v4float %5305 %int_0 %int_0
      %20722 = OpLoad %v4float %23974
      %16842 = OpIsNan %v4bool %20722
       %9783 = OpAny %bool %16842
      %11671 = OpLogicalNot %bool %9783
               OpSelectionMerge %7750 None
               OpBranchConditional %11671 %12129 %7750
      %12129 = OpLabel
      %19939 = OpAccessChain %_ptr_Input_v4float %5305 %int_1 %int_0
      %20723 = OpLoad %v4float %19939
      %18381 = OpIsNan %v4bool %20723
      %14860 = OpAny %bool %18381
               OpBranch %7750
       %7750 = OpLabel
      %24534 = OpPhi %bool %9783 %11880 %14860 %12129
      %22068 = OpLogicalNot %bool %24534
               OpSelectionMerge %9251 None
               OpBranchConditional %22068 %12130 %9251
      %12130 = OpLabel
      %19940 = OpAccessChain %_ptr_Input_v4float %5305 %int_2 %int_0
      %20724 = OpLoad %v4float %19940
      %18382 = OpIsNan %v4bool %20724
      %14861 = OpAny %bool %18382
               OpBranch %9251
       %9251 = OpLabel
      %10924 = OpPhi %bool %24534 %7750 %14861 %12130
               OpSelectionMerge %7205 None
               OpBranchConditional %10924 %21992 %7205
      %21992 = OpLabel
               OpBranch %23648
       %7205 = OpLabel
               OpBranch %6529
       %6529 = OpLabel
      %23131 = OpPhi %uint %uint_0 %7205 %11651 %14551
      %13910 = OpULessThan %bool %23131 %uint_3
               OpLoopMerge %8693 %14551 None
               OpBranchConditional %13910 %14551 %8693
      %14551 = OpLabel
      %18153 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %23131
      %16222 = OpLoad %_arr_v4float_uint_16 %18153
               OpStore %3631 %16222
      %16679 = OpAccessChain %_ptr_Input_v4float %5305 %23131 %int_0
       %7391 = OpLoad %v4float %16679
      %22888 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %22888 %7391
               OpEmitVertex
      %11651 = OpIAdd %uint %23131 %int_1
               OpBranch %6529
       %8693 = OpLabel
               OpEndPrimitive
      %12070 = OpAccessChain %_ptr_Input_v4float %5305 %int_1 %int_0
       %6301 = OpLoad %v4float %12070
      %18018 = OpVectorShuffle %v3float %6301 %6301 0 1 2
      %12374 = OpVectorShuffle %v3float %20722 %20722 0 1 2
      %18845 = OpFSub %v3float %18018 %12374
      %18938 = OpAccessChain %_ptr_Input_v4float %5305 %int_2 %int_0
      %13501 = OpLoad %v4float %18938
       %9022 = OpVectorShuffle %v3float %13501 %13501 0 1 2
       %7477 = OpFSub %v3float %9022 %12374
      %11062 = OpFSub %v3float %9022 %18018
      %14931 = OpDot %float %18845 %18845
      %23734 = OpDot %float %7477 %7477
      %22344 = OpDot %float %11062 %11062
      %24721 = OpFOrdGreaterThan %bool %22344 %14931
               OpSelectionMerge %15688 None
               OpBranchConditional %24721 %13839 %15688
      %13839 = OpLabel
      %21187 = OpFOrdGreaterThan %bool %22344 %23734
               OpBranch %15688
      %15688 = OpLabel
      %10925 = OpPhi %bool %24721 %8693 %21187 %13839
               OpSelectionMerge %11701 None
               OpBranchConditional %10925 %12131 %13261
      %12131 = OpLabel
      %18154 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %int_2
      %16223 = OpLoad %_arr_v4float_uint_16 %18154
               OpStore %3631 %16223
      %19413 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %19413 %13501
               OpEmitVertex
      %22812 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %int_1
      %11341 = OpLoad %_arr_v4float_uint_16 %22812
               OpStore %3631 %11341
               OpStore %19413 %6301
               OpEmitVertex
               OpBranch %11701
      %13261 = OpLabel
      %23993 = OpFOrdGreaterThan %bool %23734 %14931
               OpSelectionMerge %15689 None
               OpBranchConditional %23993 %13840 %15689
      %13840 = OpLabel
      %21188 = OpFOrdGreaterThan %bool %23734 %22344
               OpBranch %15689
      %15689 = OpLabel
      %10926 = OpPhi %bool %23993 %13261 %21188 %13840
               OpSelectionMerge %11046 None
               OpBranchConditional %10926 %12132 %11589
      %12132 = OpLabel
      %18155 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %int_0
      %16224 = OpLoad %_arr_v4float_uint_16 %18155
               OpStore %3631 %16224
      %19414 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %19414 %20722
               OpEmitVertex
      %22813 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %int_2
      %11342 = OpLoad %_arr_v4float_uint_16 %22813
               OpStore %3631 %11342
               OpStore %19414 %13501
               OpEmitVertex
               OpBranch %11046
      %11589 = OpLabel
      %20575 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %int_1
      %16225 = OpLoad %_arr_v4float_uint_16 %20575
               OpStore %3631 %16225
      %19415 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %19415 %6301
               OpEmitVertex
      %22814 = OpAccessChain %_ptr_Input__arr_v4float_uint_16 %3144 %int_0
      %11343 = OpLoad %_arr_v4float_uint_16 %22814
               OpStore %3631 %11343
               OpStore %19415 %20722
               OpEmitVertex
               OpBranch %11046
      %11046 = OpLabel
      %16046 = OpCompositeConstruct %v3bool %10926 %10926 %10926
      %20034 = OpSelect %v3float %16046 %2582 %267
               OpBranch %11701
      %11701 = OpLabel
      %10540 = OpPhi %v3float %266 %12131 %20034 %11046
               OpBranch %19952
      %19952 = OpLabel
      %23132 = OpPhi %uint %uint_0 %11701 %21301 %11859
      %13911 = OpULessThan %bool %23132 %uint_16
               OpLoopMerge %14959 %11859 None
               OpBranchConditional %13911 %11859 %14959
      %11859 = OpLabel
      %19851 = OpCompositeExtract %float %10540 0
      %12487 = OpAccessChain %_ptr_Input_v4float %3144 %int_0 %23132
      %12683 = OpLoad %v4float %12487
       %8719 = OpVectorTimesScalar %v4float %12683 %19851
      %15671 = OpCompositeExtract %float %10540 1
      %17096 = OpAccessChain %_ptr_Input_v4float %3144 %int_1 %23132
      %13595 = OpLoad %v4float %17096
      %19790 = OpVectorTimesScalar %v4float %13595 %15671
      %20206 = OpFAdd %v4float %8719 %19790
      %10579 = OpCompositeExtract %float %10540 2
      %16297 = OpAccessChain %_ptr_Input_v4float %3144 %int_2 %23132
      %13596 = OpLoad %v4float %16297
      %19486 = OpVectorTimesScalar %v4float %13596 %10579
      %22917 = OpFAdd %v4float %20206 %19486
      %16419 = OpAccessChain %_ptr_Output_v4float %3631 %23132
               OpStore %16419 %22917
      %21301 = OpIAdd %uint %23132 %int_1
               OpBranch %19952
      %14959 = OpLabel
       %9332 = OpCompositeExtract %float %10540 0
       %7509 = OpVectorTimesScalar %v4float %20722 %9332
       %6858 = OpCompositeExtract %float %10540 1
      %15269 = OpVectorTimesScalar %v4float %6301 %6858
      %24885 = OpFAdd %v4float %7509 %15269
      %17621 = OpCompositeExtract %float %10540 2
      %14166 = OpVectorTimesScalar %v4float %13501 %17621
       %7062 = OpFAdd %v4float %24885 %14166
      %18129 = OpAccessChain %_ptr_Output_v4float %4930 %int_0
               OpStore %18129 %7062
               OpEmitVertex
               OpEndPrimitive
               OpBranch %23648
      %23648 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t primitive_rectangle_list_gs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00006136, 0x00000000, 0x00020011,
    0x00000002, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0009000F, 0x00000003,
    0x0000161F, 0x6E69616D, 0x00000000, 0x000014B9, 0x00000E2F, 0x00000C48,
    0x00001342, 0x00030010, 0x0000161F, 0x00000016, 0x00040010, 0x0000161F,
    0x00000000, 0x00000001, 0x00030010, 0x0000161F, 0x0000001D, 0x00040010,
    0x0000161F, 0x0000001A, 0x00000006, 0x00050048, 0x000003F9, 0x00000000,
    0x0000000B, 0x00000000, 0x00030047, 0x000003F9, 0x00000002, 0x00040047,
    0x00000E2F, 0x0000001E, 0x00000000, 0x00040047, 0x00000C48, 0x0000001E,
    0x00000000, 0x00050048, 0x000003FA, 0x00000000, 0x0000000B, 0x00000000,
    0x00030047, 0x000003FA, 0x00000002, 0x00030047, 0x00001D55, 0x0000002A,
    0x00030047, 0x00003BA5, 0x0000002A, 0x00030047, 0x00006135, 0x0000002A,
    0x00030047, 0x00003756, 0x0000002A, 0x00030047, 0x00001B96, 0x0000002A,
    0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008, 0x00020014,
    0x00000009, 0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x0000001D,
    0x0000000D, 0x00000004, 0x0003001E, 0x000003F9, 0x0000001D, 0x00040015,
    0x0000000B, 0x00000020, 0x00000000, 0x0004002B, 0x0000000B, 0x00000A13,
    0x00000003, 0x0004001C, 0x00000A0F, 0x000003F9, 0x00000A13, 0x00040020,
    0x000000C9, 0x00000001, 0x00000A0F, 0x0004003B, 0x000000C9, 0x000014B9,
    0x00000001, 0x00040015, 0x0000000C, 0x00000020, 0x00000001, 0x0004002B,
    0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x0000029A, 0x00000001,
    0x0000001D, 0x00040017, 0x00000011, 0x00000009, 0x00000004, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A11,
    0x00000002, 0x0004002B, 0x0000000B, 0x00000A0A, 0x00000000, 0x0004002B,
    0x0000000B, 0x00000A3A, 0x00000010, 0x0004001C, 0x0000066B, 0x0000001D,
    0x00000A3A, 0x00040020, 0x000008E8, 0x00000003, 0x0000066B, 0x0004003B,
    0x000008E8, 0x00000E2F, 0x00000003, 0x0004001C, 0x000001AC, 0x0000066B,
    0x00000A13, 0x00040020, 0x00000429, 0x00000001, 0x000001AC, 0x0004003B,
    0x00000429, 0x00000C48, 0x00000001, 0x00040020, 0x000008E9, 0x00000001,
    0x0000066B, 0x0003001E, 0x000003FA, 0x0000001D, 0x00040020, 0x00000676,
    0x00000003, 0x000003FA, 0x0004003B, 0x00000676, 0x00001342, 0x00000003,
    0x00040020, 0x0000029B, 0x00000003, 0x0000001D, 0x00040017, 0x00000018,
    0x0000000D, 0x00000003, 0x0004002B, 0x0000000D, 0x00000341, 0xBF800000,
    0x0004002B, 0x0000000D, 0x0000008A, 0x3F800000, 0x0006002C, 0x00000018,
    0x0000010A, 0x00000341, 0x0000008A, 0x0000008A, 0x0006002C, 0x00000018,
    0x00000A16, 0x0000008A, 0x00000341, 0x0000008A, 0x0006002C, 0x00000018,
    0x0000010B, 0x0000008A, 0x0000008A, 0x00000341, 0x00040017, 0x00000010,
    0x00000009, 0x00000003, 0x00050036, 0x00000008, 0x0000161F, 0x00000000,
    0x00000502, 0x000200F8, 0x00003B06, 0x000300F7, 0x00005C60, 0x00000000,
    0x000300FB, 0x00000A0A, 0x00002E68, 0x000200F8, 0x00002E68, 0x00060041,
    0x0000029A, 0x00005DA6, 0x000014B9, 0x00000A0B, 0x00000A0B, 0x0004003D,
    0x0000001D, 0x000050F2, 0x00005DA6, 0x0004009C, 0x00000011, 0x000041CA,
    0x000050F2, 0x0004009A, 0x00000009, 0x00002637, 0x000041CA, 0x000400A8,
    0x00000009, 0x00002D97, 0x00002637, 0x000300F7, 0x00001E46, 0x00000000,
    0x000400FA, 0x00002D97, 0x00002F61, 0x00001E46, 0x000200F8, 0x00002F61,
    0x00060041, 0x0000029A, 0x00004DE3, 0x000014B9, 0x00000A0E, 0x00000A0B,
    0x0004003D, 0x0000001D, 0x000050F3, 0x00004DE3, 0x0004009C, 0x00000011,
    0x000047CD, 0x000050F3, 0x0004009A, 0x00000009, 0x00003A0C, 0x000047CD,
    0x000200F9, 0x00001E46, 0x000200F8, 0x00001E46, 0x000700F5, 0x00000009,
    0x00005FD6, 0x00002637, 0x00002E68, 0x00003A0C, 0x00002F61, 0x000400A8,
    0x00000009, 0x00005634, 0x00005FD6, 0x000300F7, 0x00002423, 0x00000000,
    0x000400FA, 0x00005634, 0x00002F62, 0x00002423, 0x000200F8, 0x00002F62,
    0x00060041, 0x0000029A, 0x00004DE4, 0x000014B9, 0x00000A11, 0x00000A0B,
    0x0004003D, 0x0000001D, 0x000050F4, 0x00004DE4, 0x0004009C, 0x00000011,
    0x000047CE, 0x000050F4, 0x0004009A, 0x00000009, 0x00003A0D, 0x000047CE,
    0x000200F9, 0x00002423, 0x000200F8, 0x00002423, 0x000700F5, 0x00000009,
    0x00002AAC, 0x00005FD6, 0x00001E46, 0x00003A0D, 0x00002F62, 0x000300F7,
    0x00001C25, 0x00000000, 0x000400FA, 0x00002AAC, 0x000055E8, 0x00001C25,
    0x000200F8, 0x000055E8, 0x000200F9, 0x00005C60, 0x000200F8, 0x00001C25,
    0x000200F9, 0x00001981, 0x000200F8, 0x00001981, 0x000700F5, 0x0000000B,
    0x00005A5B, 0x00000A0A, 0x00001C25, 0x00002D83, 0x000038D7, 0x000500B0,
    0x00000009, 0x00003656, 0x00005A5B, 0x00000A13, 0x000400F6, 0x000021F5,
    0x000038D7, 0x00000000, 0x000400FA, 0x00003656, 0x000038D7, 0x000021F5,
    0x000200F8, 0x000038D7, 0x00050041, 0x000008E9, 0x000046E9, 0x00000C48,
    0x00005A5B, 0x0004003D, 0x0000066B, 0x00003F5E, 0x000046E9, 0x0003003E,
    0x00000E2F, 0x00003F5E, 0x00060041, 0x0000029A, 0x00004127, 0x000014B9,
    0x00005A5B, 0x00000A0B, 0x0004003D, 0x0000001D, 0x00001CDF, 0x00004127,
    0x00050041, 0x0000029B, 0x00005968, 0x00001342, 0x00000A0B, 0x0003003E,
    0x00005968, 0x00001CDF, 0x000100DA, 0x00050080, 0x0000000B, 0x00002D83,
    0x00005A5B, 0x00000A0E, 0x000200F9, 0x00001981, 0x000200F8, 0x000021F5,
    0x000100DB, 0x00060041, 0x0000029A, 0x00002F26, 0x000014B9, 0x00000A0E,
    0x00000A0B, 0x0004003D, 0x0000001D, 0x0000189D, 0x00002F26, 0x0008004F,
    0x00000018, 0x00004662, 0x0000189D, 0x0000189D, 0x00000000, 0x00000001,
    0x00000002, 0x0008004F, 0x00000018, 0x00003056, 0x000050F2, 0x000050F2,
    0x00000000, 0x00000001, 0x00000002, 0x00050083, 0x00000018, 0x0000499D,
    0x00004662, 0x00003056, 0x00060041, 0x0000029A, 0x000049FA, 0x000014B9,
    0x00000A11, 0x00000A0B, 0x0004003D, 0x0000001D, 0x000034BD, 0x000049FA,
    0x0008004F, 0x00000018, 0x0000233E, 0x000034BD, 0x000034BD, 0x00000000,
    0x00000001, 0x00000002, 0x00050083, 0x00000018, 0x00001D35, 0x0000233E,
    0x00003056, 0x00050083, 0x00000018, 0x00002B36, 0x0000233E, 0x00004662,
    0x00050094, 0x0000000D, 0x00003A53, 0x0000499D, 0x0000499D, 0x00050094,
    0x0000000D, 0x00005CB6, 0x00001D35, 0x00001D35, 0x00050094, 0x0000000D,
    0x00005748, 0x00002B36, 0x00002B36, 0x000500BA, 0x00000009, 0x00006091,
    0x00005748, 0x00003A53, 0x000300F7, 0x00003D48, 0x00000000, 0x000400FA,
    0x00006091, 0x0000360F, 0x00003D48, 0x000200F8, 0x0000360F, 0x000500BA,
    0x00000009, 0x000052C3, 0x00005748, 0x00005CB6, 0x000200F9, 0x00003D48,
    0x000200F8, 0x00003D48, 0x000700F5, 0x00000009, 0x00002AAD, 0x00006091,
    0x000021F5, 0x000052C3, 0x0000360F, 0x000300F7, 0x00002DB5, 0x00000000,
    0x000400FA, 0x00002AAD, 0x00002F63, 0x000033CD, 0x000200F8, 0x00002F63,
    0x00050041, 0x000008E9, 0x000046EA, 0x00000C48, 0x00000A11, 0x0004003D,
    0x0000066B, 0x00003F5F, 0x000046EA, 0x0003003E, 0x00000E2F, 0x00003F5F,
    0x00050041, 0x0000029B, 0x00004BD5, 0x00001342, 0x00000A0B, 0x0003003E,
    0x00004BD5, 0x000034BD, 0x000100DA, 0x00050041, 0x000008E9, 0x0000591C,
    0x00000C48, 0x00000A0E, 0x0004003D, 0x0000066B, 0x00002C4D, 0x0000591C,
    0x0003003E, 0x00000E2F, 0x00002C4D, 0x0003003E, 0x00004BD5, 0x0000189D,
    0x000100DA, 0x000200F9, 0x00002DB5, 0x000200F8, 0x000033CD, 0x000500BA,
    0x00000009, 0x00005DB9, 0x00005CB6, 0x00003A53, 0x000300F7, 0x00003D49,
    0x00000000, 0x000400FA, 0x00005DB9, 0x00003610, 0x00003D49, 0x000200F8,
    0x00003610, 0x000500BA, 0x00000009, 0x000052C4, 0x00005CB6, 0x00005748,
    0x000200F9, 0x00003D49, 0x000200F8, 0x00003D49, 0x000700F5, 0x00000009,
    0x00002AAE, 0x00005DB9, 0x000033CD, 0x000052C4, 0x00003610, 0x000300F7,
    0x00002B26, 0x00000000, 0x000400FA, 0x00002AAE, 0x00002F64, 0x00002D45,
    0x000200F8, 0x00002F64, 0x00050041, 0x000008E9, 0x000046EB, 0x00000C48,
    0x00000A0B, 0x0004003D, 0x0000066B, 0x00003F60, 0x000046EB, 0x0003003E,
    0x00000E2F, 0x00003F60, 0x00050041, 0x0000029B, 0x00004BD6, 0x00001342,
    0x00000A0B, 0x0003003E, 0x00004BD6, 0x000050F2, 0x000100DA, 0x00050041,
    0x000008E9, 0x0000591D, 0x00000C48, 0x00000A11, 0x0004003D, 0x0000066B,
    0x00002C4E, 0x0000591D, 0x0003003E, 0x00000E2F, 0x00002C4E, 0x0003003E,
    0x00004BD6, 0x000034BD, 0x000100DA, 0x000200F9, 0x00002B26, 0x000200F8,
    0x00002D45, 0x00050041, 0x000008E9, 0x0000505F, 0x00000C48, 0x00000A0E,
    0x0004003D, 0x0000066B, 0x00003F61, 0x0000505F, 0x0003003E, 0x00000E2F,
    0x00003F61, 0x00050041, 0x0000029B, 0x00004BD7, 0x00001342, 0x00000A0B,
    0x0003003E, 0x00004BD7, 0x0000189D, 0x000100DA, 0x00050041, 0x000008E9,
    0x0000591E, 0x00000C48, 0x00000A0B, 0x0004003D, 0x0000066B, 0x00002C4F,
    0x0000591E, 0x0003003E, 0x00000E2F, 0x00002C4F, 0x0003003E, 0x00004BD7,
    0x000050F2, 0x000100DA, 0x000200F9, 0x00002B26, 0x000200F8, 0x00002B26,
    0x00060050, 0x00000010, 0x00003EAE, 0x00002AAE, 0x00002AAE, 0x00002AAE,
    0x000600A9, 0x00000018, 0x00004E42, 0x00003EAE, 0x00000A16, 0x0000010B,
    0x000200F9, 0x00002DB5, 0x000200F8, 0x00002DB5, 0x000700F5, 0x00000018,
    0x0000292C, 0x0000010A, 0x00002F63, 0x00004E42, 0x00002B26, 0x000200F9,
    0x00004DF0, 0x000200F8, 0x00004DF0, 0x000700F5, 0x0000000B, 0x00005A5C,
    0x00000A0A, 0x00002DB5, 0x00005335, 0x00002E53, 0x000500B0, 0x00000009,
    0x00003657, 0x00005A5C, 0x00000A3A, 0x000400F6, 0x00003A6F, 0x00002E53,
    0x00000000, 0x000400FA, 0x00003657, 0x00002E53, 0x00003A6F, 0x000200F8,
    0x00002E53, 0x00050051, 0x0000000D, 0x00004D8B, 0x0000292C, 0x00000000,
    0x00060041, 0x0000029A, 0x000030C7, 0x00000C48, 0x00000A0B, 0x00005A5C,
    0x0004003D, 0x0000001D, 0x0000318B, 0x000030C7, 0x0005008E, 0x0000001D,
    0x0000220F, 0x0000318B, 0x00004D8B, 0x00050051, 0x0000000D, 0x00003D37,
    0x0000292C, 0x00000001, 0x00060041, 0x0000029A, 0x000042C8, 0x00000C48,
    0x00000A0E, 0x00005A5C, 0x0004003D, 0x0000001D, 0x0000351B, 0x000042C8,
    0x0005008E, 0x0000001D, 0x00004D4E, 0x0000351B, 0x00003D37, 0x00050081,
    0x0000001D, 0x00004EEE, 0x0000220F, 0x00004D4E, 0x00050051, 0x0000000D,
    0x00002953, 0x0000292C, 0x00000002, 0x00060041, 0x0000029A, 0x00003FA9,
    0x00000C48, 0x00000A11, 0x00005A5C, 0x0004003D, 0x0000001D, 0x0000351C,
    0x00003FA9, 0x0005008E, 0x0000001D, 0x00004C1E, 0x0000351C, 0x00002953,
    0x00050081, 0x0000001D, 0x00005985, 0x00004EEE, 0x00004C1E, 0x00050041,
    0x0000029B, 0x00004023, 0x00000E2F, 0x00005A5C, 0x0003003E, 0x00004023,
    0x00005985, 0x00050080, 0x0000000B, 0x00005335, 0x00005A5C, 0x00000A0E,
    0x000200F9, 0x00004DF0, 0x000200F8, 0x00003A6F, 0x00050051, 0x0000000D,
    0x00002474, 0x0000292C, 0x00000000, 0x0005008E, 0x0000001D, 0x00001D55,
    0x000050F2, 0x00002474, 0x00050051, 0x0000000D, 0x00001ACA, 0x0000292C,
    0x00000001, 0x0005008E, 0x0000001D, 0x00003BA5, 0x0000189D, 0x00001ACA,
    0x00050081, 0x0000001D, 0x00006135, 0x00001D55, 0x00003BA5, 0x00050051,
    0x0000000D, 0x000044D5, 0x0000292C, 0x00000002, 0x0005008E, 0x0000001D,
    0x00003756, 0x000034BD, 0x000044D5, 0x00050081, 0x0000001D, 0x00001B96,
    0x00006135, 0x00003756, 0x00050041, 0x0000029B, 0x000046D1, 0x00001342,
    0x00000A0B, 0x0003003E, 0x000046D1, 0x00001B96, 0x000100DA, 0x000100DB,
    0x000200F9, 0x00005C60, 0x000200F8, 0x00005C60, 0x000100FD, 0x00010038,
};
