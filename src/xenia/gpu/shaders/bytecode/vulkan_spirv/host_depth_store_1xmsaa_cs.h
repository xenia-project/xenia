// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 25265
; Schema: 0
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %5663 "main" %gl_GlobalInvocationID
               OpExecutionMode %5663 LocalSize 8 8 1
               OpMemberDecorate %_struct_990 0 Offset 0
               OpMemberDecorate %_struct_990 1 Offset 4
               OpDecorate %_struct_990 Block
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %_runtimearr_v4uint ArrayStride 16
               OpMemberDecorate %_struct_1972 0 NonReadable
               OpMemberDecorate %_struct_1972 0 Offset 0
               OpDecorate %_struct_1972 BufferBlock
               OpDecorate %4790 DescriptorSet 0
               OpDecorate %4790 Binding 0
               OpDecorate %3709 DescriptorSet 1
               OpDecorate %3709 Binding 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
       %bool = OpTypeBool
     %uint_2 = OpConstant %uint 2
     %uint_1 = OpConstant %uint 1
       %1837 = OpConstantComposite %v2uint %uint_2 %uint_1
     %v2bool = OpTypeVector %bool 2
     %uint_0 = OpConstant %uint 0
       %1807 = OpConstantComposite %v2uint %uint_0 %uint_0
       %1828 = OpConstantComposite %v2uint %uint_1 %uint_1
       %1816 = OpConstantComposite %v2uint %uint_1 %uint_0
    %uint_80 = OpConstant %uint 80
    %uint_16 = OpConstant %uint 16
       %2719 = OpConstantComposite %v2uint %uint_80 %uint_16
        %int = OpTypeInt 32 1
%_struct_990 = OpTypeStruct %uint %uint
%_ptr_PushConstant__struct_990 = OpTypePointer PushConstant %_struct_990
       %3052 = OpVariable %_ptr_PushConstant__struct_990 PushConstant
      %int_1 = OpConstant %int 1
%_ptr_PushConstant_uint = OpTypePointer PushConstant %uint
    %uint_10 = OpConstant %uint 10
    %uint_12 = OpConstant %uint 12
       %2041 = OpConstantComposite %v2uint %uint_10 %uint_12
     %uint_3 = OpConstant %uint 3
      %int_0 = OpConstant %int 0
       %1927 = OpConstantComposite %v2uint %uint_0 %uint_10
  %uint_1023 = OpConstant %uint 1023
    %uint_20 = OpConstant %uint 20
     %v3uint = OpTypeVector %uint 3
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%_ptr_Input_uint = OpTypePointer Input %uint
      %v2int = OpTypeVector %int 2
       %1834 = OpConstantComposite %v2uint %uint_3 %uint_0
     %v4uint = OpTypeVector %uint 4
%_runtimearr_v4uint = OpTypeRuntimeArray %v4uint
%_struct_1972 = OpTypeStruct %_runtimearr_v4uint
%_ptr_Uniform__struct_1972 = OpTypePointer Uniform %_struct_1972
       %4790 = OpVariable %_ptr_Uniform__struct_1972 Uniform
      %float = OpTypeFloat 32
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
%_ptr_UniformConstant_150 = OpTypePointer UniformConstant %150
       %3709 = OpVariable %_ptr_UniformConstant_150 UniformConstant
    %v4float = OpTypeVector %float 4
       %1824 = OpConstantComposite %v2int %int_1 %int_0
      %int_2 = OpConstant %int 2
       %1833 = OpConstantComposite %v2int %int_2 %int_0
      %int_3 = OpConstant %int 3
       %1842 = OpConstantComposite %v2int %int_3 %int_0
%_ptr_Uniform_v4uint = OpTypePointer Uniform %v4uint
      %int_4 = OpConstant %int 4
       %1851 = OpConstantComposite %v2int %int_4 %int_0
      %int_5 = OpConstant %int 5
       %1860 = OpConstantComposite %v2int %int_5 %int_0
      %int_6 = OpConstant %int 6
       %1869 = OpConstantComposite %v2int %int_6 %int_0
      %int_7 = OpConstant %int 7
       %1878 = OpConstantComposite %v2int %int_7 %int_0
     %uint_8 = OpConstant %uint 8
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_8 %uint_8 %uint_1
       %1870 = OpConstantComposite %v2uint %uint_3 %uint_3
       %2213 = OpConstantComposite %v2uint %uint_1023 %uint_1023
       %5663 = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %19578 None
               OpSwitch %uint_0 %11880
      %11880 = OpLabel
      %22245 = OpAccessChain %_ptr_Input_uint %gl_GlobalInvocationID %uint_0
      %15627 = OpLoad %uint %22245
      %22605 = OpAccessChain %_ptr_PushConstant_uint %3052 %int_0
      %21784 = OpLoad %uint %22605
      %21170 = OpShiftRightLogical %uint %21784 %uint_20
      %15618 = OpBitwiseAnd %uint %21170 %uint_1023
      %10265 = OpIAdd %uint %15618 %uint_1
      %19929 = OpAccessChain %_ptr_PushConstant_uint %3052 %int_1
      %15334 = OpLoad %uint %19929
      %10293 = OpCompositeConstruct %v2uint %15334 %15334
      %24634 = OpShiftRightLogical %v2uint %10293 %2041
      %24203 = OpBitwiseAnd %v2uint %24634 %1870
      %10929 = OpCompositeExtract %uint %24203 0
       %7670 = OpIMul %uint %10265 %10929
       %7287 = OpUGreaterThanEqual %bool %15627 %7670
               OpSelectionMerge %16345 DontFlatten
               OpBranchConditional %7287 %21992 %16345
      %21992 = OpLabel
               OpBranch %19578
      %16345 = OpLabel
      %10771 = OpCompositeConstruct %v2uint %21784 %21784
      %13581 = OpShiftRightLogical %v2uint %10771 %1927
      %23379 = OpBitwiseAnd %v2uint %13581 %2213
      %13680 = OpShiftLeftLogical %v2uint %23379 %1870
      %24677 = OpIMul %v2uint %13680 %24203
       %7005 = OpLoad %v3uint %gl_GlobalInvocationID
      %22399 = OpVectorShuffle %v2uint %7005 %7005 0 1
      %21597 = OpShiftLeftLogical %v2uint %22399 %1834
       %9038 = OpIAdd %v2uint %24677 %21597
      %24559 = OpBitcast %v2int %9038
       %8919 = OpBitcast %v2uint %24559
      %18334 = OpBitwiseAnd %uint %15334 %uint_1023
       %7195 = OpUGreaterThanEqual %v2bool %1807 %1837
      %17737 = OpSelect %v2uint %7195 %1828 %1807
      %10430 = OpShiftLeftLogical %v2uint %8919 %17737
      %16475 = OpShiftRightLogical %v2uint %1807 %1816
      %13071 = OpBitwiseAnd %v2uint %16475 %1828
      %20272 = OpIAdd %v2uint %10430 %13071
      %21145 = OpIMul %v2uint %2719 %24203
      %14725 = OpShiftRightLogical %v2uint %21145 %1807
      %19799 = OpUDiv %v2uint %20272 %14725
      %20390 = OpCompositeExtract %uint %19799 1
      %11046 = OpIMul %uint %20390 %18334
      %24741 = OpCompositeExtract %uint %19799 0
      %20806 = OpIAdd %uint %11046 %24741
      %13527 = OpIMul %v2uint %19799 %14725
      %20715 = OpISub %v2uint %20272 %13527
       %7303 = OpCompositeExtract %uint %21145 0
      %22882 = OpCompositeExtract %uint %21145 1
      %13170 = OpIMul %uint %7303 %22882
      %14551 = OpIMul %uint %20806 %13170
       %6805 = OpCompositeExtract %uint %20715 1
      %23526 = OpCompositeExtract %uint %14725 0
      %22886 = OpIMul %uint %6805 %23526
       %6886 = OpCompositeExtract %uint %20715 0
       %9696 = OpIAdd %uint %22886 %6886
      %19199 = OpShiftLeftLogical %uint %9696 %uint_0
      %25264 = OpIAdd %uint %14551 %19199
       %6574 = OpShiftRightLogical %uint %25264 %uint_2
       %7456 = OpLoad %150 %3709
      %17822 = OpImageFetch %v4float %7456 %24559 Lod %int_0
      %11864 = OpCompositeExtract %float %17822 0
      %19035 = OpIAdd %v2int %24559 %1824
      %20902 = OpImageFetch %v4float %7456 %19035 Lod %int_0
      %17472 = OpCompositeExtract %float %20902 0
      %19036 = OpIAdd %v2int %24559 %1833
      %20903 = OpImageFetch %v4float %7456 %19036 Lod %int_0
      %17473 = OpCompositeExtract %float %20903 0
      %19037 = OpIAdd %v2int %24559 %1842
      %19990 = OpImageFetch %v4float %7456 %19037 Lod %int_0
       %7256 = OpCompositeExtract %float %19990 0
       %6487 = OpCompositeConstruct %v4float %11864 %17472 %17473 %7256
      %20366 = OpBitcast %v4uint %6487
      %12860 = OpAccessChain %_ptr_Uniform_v4uint %4790 %int_0 %6574
               OpStore %12860 %20366
      %20256 = OpIAdd %uint %6574 %uint_1
       %8574 = OpIAdd %v2int %24559 %1851
      %10680 = OpImageFetch %v4float %7456 %8574 Lod %int_0
      %17474 = OpCompositeExtract %float %10680 0
      %19038 = OpIAdd %v2int %24559 %1860
      %20904 = OpImageFetch %v4float %7456 %19038 Lod %int_0
      %17475 = OpCompositeExtract %float %20904 0
      %19039 = OpIAdd %v2int %24559 %1869
      %20905 = OpImageFetch %v4float %7456 %19039 Lod %int_0
      %17476 = OpCompositeExtract %float %20905 0
      %19040 = OpIAdd %v2int %24559 %1878
      %19991 = OpImageFetch %v4float %7456 %19040 Lod %int_0
       %7257 = OpCompositeExtract %float %19991 0
       %6488 = OpCompositeConstruct %v4float %17474 %17475 %17476 %7257
      %20367 = OpBitcast %v4uint %6488
      %15159 = OpAccessChain %_ptr_Uniform_v4uint %4790 %int_0 %20256
               OpStore %15159 %20367
               OpBranch %19578
      %19578 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t host_depth_store_1xmsaa_cs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x000062B1, 0x00000000, 0x00020011,
    0x00000001, 0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
    0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000005,
    0x0000161F, 0x6E69616D, 0x00000000, 0x00000F48, 0x00060010, 0x0000161F,
    0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00050048, 0x000003DE,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x000003DE, 0x00000001,
    0x00000023, 0x00000004, 0x00030047, 0x000003DE, 0x00000002, 0x00040047,
    0x00000F48, 0x0000000B, 0x0000001C, 0x00040047, 0x000007DC, 0x00000006,
    0x00000010, 0x00040048, 0x000007B4, 0x00000000, 0x00000019, 0x00050048,
    0x000007B4, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x000007B4,
    0x00000003, 0x00040047, 0x000012B6, 0x00000022, 0x00000000, 0x00040047,
    0x000012B6, 0x00000021, 0x00000000, 0x00040047, 0x00000E7D, 0x00000022,
    0x00000001, 0x00040047, 0x00000E7D, 0x00000021, 0x00000000, 0x00040047,
    0x00000AC7, 0x0000000B, 0x00000019, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00040015, 0x0000000B, 0x00000020, 0x00000000,
    0x00040017, 0x00000011, 0x0000000B, 0x00000002, 0x00020014, 0x00000009,
    0x0004002B, 0x0000000B, 0x00000A10, 0x00000002, 0x0004002B, 0x0000000B,
    0x00000A0D, 0x00000001, 0x0005002C, 0x00000011, 0x0000072D, 0x00000A10,
    0x00000A0D, 0x00040017, 0x0000000F, 0x00000009, 0x00000002, 0x0004002B,
    0x0000000B, 0x00000A0A, 0x00000000, 0x0005002C, 0x00000011, 0x0000070F,
    0x00000A0A, 0x00000A0A, 0x0005002C, 0x00000011, 0x00000724, 0x00000A0D,
    0x00000A0D, 0x0005002C, 0x00000011, 0x00000718, 0x00000A0D, 0x00000A0A,
    0x0004002B, 0x0000000B, 0x00000AFA, 0x00000050, 0x0004002B, 0x0000000B,
    0x00000A3A, 0x00000010, 0x0005002C, 0x00000011, 0x00000A9F, 0x00000AFA,
    0x00000A3A, 0x00040015, 0x0000000C, 0x00000020, 0x00000001, 0x0004001E,
    0x000003DE, 0x0000000B, 0x0000000B, 0x00040020, 0x0000065B, 0x00000009,
    0x000003DE, 0x0004003B, 0x0000065B, 0x00000BEC, 0x00000009, 0x0004002B,
    0x0000000C, 0x00000A0E, 0x00000001, 0x00040020, 0x00000288, 0x00000009,
    0x0000000B, 0x0004002B, 0x0000000B, 0x00000A28, 0x0000000A, 0x0004002B,
    0x0000000B, 0x00000A2E, 0x0000000C, 0x0005002C, 0x00000011, 0x000007F9,
    0x00000A28, 0x00000A2E, 0x0004002B, 0x0000000B, 0x00000A13, 0x00000003,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x0005002C, 0x00000011,
    0x00000787, 0x00000A0A, 0x00000A28, 0x0004002B, 0x0000000B, 0x00000A44,
    0x000003FF, 0x0004002B, 0x0000000B, 0x00000A46, 0x00000014, 0x00040017,
    0x00000014, 0x0000000B, 0x00000003, 0x00040020, 0x00000291, 0x00000001,
    0x00000014, 0x0004003B, 0x00000291, 0x00000F48, 0x00000001, 0x00040020,
    0x00000289, 0x00000001, 0x0000000B, 0x00040017, 0x00000012, 0x0000000C,
    0x00000002, 0x0005002C, 0x00000011, 0x0000072A, 0x00000A13, 0x00000A0A,
    0x00040017, 0x00000017, 0x0000000B, 0x00000004, 0x0003001D, 0x000007DC,
    0x00000017, 0x0003001E, 0x000007B4, 0x000007DC, 0x00040020, 0x00000A31,
    0x00000002, 0x000007B4, 0x0004003B, 0x00000A31, 0x000012B6, 0x00000002,
    0x00030016, 0x0000000D, 0x00000020, 0x00090019, 0x00000096, 0x0000000D,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x00040020, 0x00000313, 0x00000000, 0x00000096, 0x0004003B, 0x00000313,
    0x00000E7D, 0x00000000, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004,
    0x0005002C, 0x00000012, 0x00000720, 0x00000A0E, 0x00000A0B, 0x0004002B,
    0x0000000C, 0x00000A11, 0x00000002, 0x0005002C, 0x00000012, 0x00000729,
    0x00000A11, 0x00000A0B, 0x0004002B, 0x0000000C, 0x00000A14, 0x00000003,
    0x0005002C, 0x00000012, 0x00000732, 0x00000A14, 0x00000A0B, 0x00040020,
    0x00000294, 0x00000002, 0x00000017, 0x0004002B, 0x0000000C, 0x00000A17,
    0x00000004, 0x0005002C, 0x00000012, 0x0000073B, 0x00000A17, 0x00000A0B,
    0x0004002B, 0x0000000C, 0x00000A1A, 0x00000005, 0x0005002C, 0x00000012,
    0x00000744, 0x00000A1A, 0x00000A0B, 0x0004002B, 0x0000000C, 0x00000A1D,
    0x00000006, 0x0005002C, 0x00000012, 0x0000074D, 0x00000A1D, 0x00000A0B,
    0x0004002B, 0x0000000C, 0x00000A20, 0x00000007, 0x0005002C, 0x00000012,
    0x00000756, 0x00000A20, 0x00000A0B, 0x0004002B, 0x0000000B, 0x00000A22,
    0x00000008, 0x0006002C, 0x00000014, 0x00000AC7, 0x00000A22, 0x00000A22,
    0x00000A0D, 0x0005002C, 0x00000011, 0x0000074E, 0x00000A13, 0x00000A13,
    0x0005002C, 0x00000011, 0x000008A5, 0x00000A44, 0x00000A44, 0x00050036,
    0x00000008, 0x0000161F, 0x00000000, 0x00000502, 0x000200F8, 0x00003B06,
    0x000300F7, 0x00004C7A, 0x00000000, 0x000300FB, 0x00000A0A, 0x00002E68,
    0x000200F8, 0x00002E68, 0x00050041, 0x00000289, 0x000056E5, 0x00000F48,
    0x00000A0A, 0x0004003D, 0x0000000B, 0x00003D0B, 0x000056E5, 0x00050041,
    0x00000288, 0x0000584D, 0x00000BEC, 0x00000A0B, 0x0004003D, 0x0000000B,
    0x00005518, 0x0000584D, 0x000500C2, 0x0000000B, 0x000052B2, 0x00005518,
    0x00000A46, 0x000500C7, 0x0000000B, 0x00003D02, 0x000052B2, 0x00000A44,
    0x00050080, 0x0000000B, 0x00002819, 0x00003D02, 0x00000A0D, 0x00050041,
    0x00000288, 0x00004DD9, 0x00000BEC, 0x00000A0E, 0x0004003D, 0x0000000B,
    0x00003BE6, 0x00004DD9, 0x00050050, 0x00000011, 0x00002835, 0x00003BE6,
    0x00003BE6, 0x000500C2, 0x00000011, 0x0000603A, 0x00002835, 0x000007F9,
    0x000500C7, 0x00000011, 0x00005E8B, 0x0000603A, 0x0000074E, 0x00050051,
    0x0000000B, 0x00002AB1, 0x00005E8B, 0x00000000, 0x00050084, 0x0000000B,
    0x00001DF6, 0x00002819, 0x00002AB1, 0x000500AE, 0x00000009, 0x00001C77,
    0x00003D0B, 0x00001DF6, 0x000300F7, 0x00003FD9, 0x00000002, 0x000400FA,
    0x00001C77, 0x000055E8, 0x00003FD9, 0x000200F8, 0x000055E8, 0x000200F9,
    0x00004C7A, 0x000200F8, 0x00003FD9, 0x00050050, 0x00000011, 0x00002A13,
    0x00005518, 0x00005518, 0x000500C2, 0x00000011, 0x0000350D, 0x00002A13,
    0x00000787, 0x000500C7, 0x00000011, 0x00005B53, 0x0000350D, 0x000008A5,
    0x000500C4, 0x00000011, 0x00003570, 0x00005B53, 0x0000074E, 0x00050084,
    0x00000011, 0x00006065, 0x00003570, 0x00005E8B, 0x0004003D, 0x00000014,
    0x00001B5D, 0x00000F48, 0x0007004F, 0x00000011, 0x0000577F, 0x00001B5D,
    0x00001B5D, 0x00000000, 0x00000001, 0x000500C4, 0x00000011, 0x0000545D,
    0x0000577F, 0x0000072A, 0x00050080, 0x00000011, 0x0000234E, 0x00006065,
    0x0000545D, 0x0004007C, 0x00000012, 0x00005FEF, 0x0000234E, 0x0004007C,
    0x00000011, 0x000022D7, 0x00005FEF, 0x000500C7, 0x0000000B, 0x0000479E,
    0x00003BE6, 0x00000A44, 0x000500AE, 0x0000000F, 0x00001C1B, 0x0000070F,
    0x0000072D, 0x000600A9, 0x00000011, 0x00004549, 0x00001C1B, 0x00000724,
    0x0000070F, 0x000500C4, 0x00000011, 0x000028BE, 0x000022D7, 0x00004549,
    0x000500C2, 0x00000011, 0x0000405B, 0x0000070F, 0x00000718, 0x000500C7,
    0x00000011, 0x0000330F, 0x0000405B, 0x00000724, 0x00050080, 0x00000011,
    0x00004F30, 0x000028BE, 0x0000330F, 0x00050084, 0x00000011, 0x00005299,
    0x00000A9F, 0x00005E8B, 0x000500C2, 0x00000011, 0x00003985, 0x00005299,
    0x0000070F, 0x00050086, 0x00000011, 0x00004D57, 0x00004F30, 0x00003985,
    0x00050051, 0x0000000B, 0x00004FA6, 0x00004D57, 0x00000001, 0x00050084,
    0x0000000B, 0x00002B26, 0x00004FA6, 0x0000479E, 0x00050051, 0x0000000B,
    0x000060A5, 0x00004D57, 0x00000000, 0x00050080, 0x0000000B, 0x00005146,
    0x00002B26, 0x000060A5, 0x00050084, 0x00000011, 0x000034D7, 0x00004D57,
    0x00003985, 0x00050082, 0x00000011, 0x000050EB, 0x00004F30, 0x000034D7,
    0x00050051, 0x0000000B, 0x00001C87, 0x00005299, 0x00000000, 0x00050051,
    0x0000000B, 0x00005962, 0x00005299, 0x00000001, 0x00050084, 0x0000000B,
    0x00003372, 0x00001C87, 0x00005962, 0x00050084, 0x0000000B, 0x000038D7,
    0x00005146, 0x00003372, 0x00050051, 0x0000000B, 0x00001A95, 0x000050EB,
    0x00000001, 0x00050051, 0x0000000B, 0x00005BE6, 0x00003985, 0x00000000,
    0x00050084, 0x0000000B, 0x00005966, 0x00001A95, 0x00005BE6, 0x00050051,
    0x0000000B, 0x00001AE6, 0x000050EB, 0x00000000, 0x00050080, 0x0000000B,
    0x000025E0, 0x00005966, 0x00001AE6, 0x000500C4, 0x0000000B, 0x00004AFF,
    0x000025E0, 0x00000A0A, 0x00050080, 0x0000000B, 0x000062B0, 0x000038D7,
    0x00004AFF, 0x000500C2, 0x0000000B, 0x000019AE, 0x000062B0, 0x00000A10,
    0x0004003D, 0x00000096, 0x00001D20, 0x00000E7D, 0x0007005F, 0x0000001D,
    0x0000459E, 0x00001D20, 0x00005FEF, 0x00000002, 0x00000A0B, 0x00050051,
    0x0000000D, 0x00002E58, 0x0000459E, 0x00000000, 0x00050080, 0x00000012,
    0x00004A5B, 0x00005FEF, 0x00000720, 0x0007005F, 0x0000001D, 0x000051A6,
    0x00001D20, 0x00004A5B, 0x00000002, 0x00000A0B, 0x00050051, 0x0000000D,
    0x00004440, 0x000051A6, 0x00000000, 0x00050080, 0x00000012, 0x00004A5C,
    0x00005FEF, 0x00000729, 0x0007005F, 0x0000001D, 0x000051A7, 0x00001D20,
    0x00004A5C, 0x00000002, 0x00000A0B, 0x00050051, 0x0000000D, 0x00004441,
    0x000051A7, 0x00000000, 0x00050080, 0x00000012, 0x00004A5D, 0x00005FEF,
    0x00000732, 0x0007005F, 0x0000001D, 0x00004E16, 0x00001D20, 0x00004A5D,
    0x00000002, 0x00000A0B, 0x00050051, 0x0000000D, 0x00001C58, 0x00004E16,
    0x00000000, 0x00070050, 0x0000001D, 0x00001957, 0x00002E58, 0x00004440,
    0x00004441, 0x00001C58, 0x0004007C, 0x00000017, 0x00004F8E, 0x00001957,
    0x00060041, 0x00000294, 0x0000323C, 0x000012B6, 0x00000A0B, 0x000019AE,
    0x0003003E, 0x0000323C, 0x00004F8E, 0x00050080, 0x0000000B, 0x00004F20,
    0x000019AE, 0x00000A0D, 0x00050080, 0x00000012, 0x0000217E, 0x00005FEF,
    0x0000073B, 0x0007005F, 0x0000001D, 0x000029B8, 0x00001D20, 0x0000217E,
    0x00000002, 0x00000A0B, 0x00050051, 0x0000000D, 0x00004442, 0x000029B8,
    0x00000000, 0x00050080, 0x00000012, 0x00004A5E, 0x00005FEF, 0x00000744,
    0x0007005F, 0x0000001D, 0x000051A8, 0x00001D20, 0x00004A5E, 0x00000002,
    0x00000A0B, 0x00050051, 0x0000000D, 0x00004443, 0x000051A8, 0x00000000,
    0x00050080, 0x00000012, 0x00004A5F, 0x00005FEF, 0x0000074D, 0x0007005F,
    0x0000001D, 0x000051A9, 0x00001D20, 0x00004A5F, 0x00000002, 0x00000A0B,
    0x00050051, 0x0000000D, 0x00004444, 0x000051A9, 0x00000000, 0x00050080,
    0x00000012, 0x00004A60, 0x00005FEF, 0x00000756, 0x0007005F, 0x0000001D,
    0x00004E17, 0x00001D20, 0x00004A60, 0x00000002, 0x00000A0B, 0x00050051,
    0x0000000D, 0x00001C59, 0x00004E17, 0x00000000, 0x00070050, 0x0000001D,
    0x00001958, 0x00004442, 0x00004443, 0x00004444, 0x00001C59, 0x0004007C,
    0x00000017, 0x00004F8F, 0x00001958, 0x00060041, 0x00000294, 0x00003B37,
    0x000012B6, 0x00000A0B, 0x00004F20, 0x0003003E, 0x00003B37, 0x00004F8F,
    0x000200F9, 0x00004C7A, 0x000200F8, 0x00004C7A, 0x000100FD, 0x00010038,
};
