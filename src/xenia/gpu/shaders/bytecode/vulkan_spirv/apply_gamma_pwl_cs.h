// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 10
; Bound: 24005
; Schema: 0
               OpCapability Shader
               OpCapability SampledBuffer
               OpCapability StorageImageExtendedFormats
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %5663 "main" %gl_GlobalInvocationID
               OpExecutionMode %5663 LocalSize 16 8 1
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpMemberDecorate %_struct_993 0 Offset 0
               OpDecorate %_struct_993 Block
               OpDecorate %5759 DescriptorSet 1
               OpDecorate %5759 Binding 0
               OpDecorate %5945 DescriptorSet 0
               OpDecorate %5945 Binding 0
               OpDecorate %3258 DescriptorSet 2
               OpDecorate %3258 Binding 0
               OpDecorate %3258 NonReadable
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
    %float_0 = OpConstant %float 0
    %float_1 = OpConstant %float 1
     %uint_0 = OpConstant %uint 0
     %uint_7 = OpConstant %uint 7
     %uint_1 = OpConstant %uint 1
%float_0_125 = OpConstant %float 0.125
%float_1_52737048en05 = OpConstant %float 1.52737048e-05
     %v3uint = OpTypeVector %uint 3
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%_struct_993 = OpTypeStruct %v2uint
%_ptr_PushConstant__struct_993 = OpTypePointer PushConstant %_struct_993
       %4495 = OpVariable %_ptr_PushConstant__struct_993 PushConstant
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
%_ptr_PushConstant_v2uint = OpTypePointer PushConstant %v2uint
       %bool = OpTypeBool
     %v2bool = OpTypeVector %bool 2
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
%_ptr_UniformConstant_150 = OpTypePointer UniformConstant %150
       %5759 = OpVariable %_ptr_UniformConstant_150 UniformConstant
      %v2int = OpTypeVector %int 2
    %v4float = OpTypeVector %float 4
    %v3float = OpTypeVector %float 3
 %float_1023 = OpConstant %float 1023
  %float_0_5 = OpConstant %float 0.5
        %152 = OpTypeImage %uint Buffer 0 0 0 1 Unknown
%_ptr_UniformConstant_152 = OpTypePointer UniformConstant %152
       %5945 = OpVariable %_ptr_UniformConstant_152 UniformConstant
     %uint_3 = OpConstant %uint 3
     %v4uint = OpTypeVector %uint 4
     %uint_2 = OpConstant %uint 2
        %166 = OpTypeImage %float 2D 0 0 0 2 Rgb10A2
%_ptr_UniformConstant_166 = OpTypePointer UniformConstant %166
       %3258 = OpVariable %_ptr_UniformConstant_166 UniformConstant
    %uint_16 = OpConstant %uint 16
     %uint_8 = OpConstant %uint 8
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_16 %uint_8 %uint_1
      %10264 = OpUndef %v4float
        %939 = OpConstantComposite %v3float %float_0_5 %float_0_5 %float_0_5
       %5663 = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %21573 None
               OpSwitch %uint_0 %12914
      %12914 = OpLabel
      %13761 = OpLoad %v3uint %gl_GlobalInvocationID
      %21717 = OpVectorShuffle %v2uint %13761 %13761 0 1
       %7760 = OpAccessChain %_ptr_PushConstant_v2uint %4495 %int_0
      %13378 = OpLoad %v2uint %7760
      %23437 = OpUGreaterThanEqual %v2bool %21717 %13378
      %23076 = OpAny %bool %23437
               OpSelectionMerge %18302 DontFlatten
               OpBranchConditional %23076 %21992 %18302
      %21992 = OpLabel
               OpBranch %21573
      %18302 = OpLabel
      %24004 = OpLoad %150 %5759
      %10533 = OpBitcast %v2int %21717
       %6680 = OpImageFetch %v4float %24004 %10533 Lod %int_0
      %16242 = OpVectorShuffle %v3float %6680 %6680 0 1 2
      %13907 = OpVectorTimesScalar %v3float %16242 %float_1023
      %16889 = OpFAdd %v3float %13907 %939
      %11099 = OpConvertFToU %v3uint %16889
      %19954 = OpLoad %152 %5945
      %23099 = OpCompositeExtract %uint %11099 0
      %17722 = OpShiftRightLogical %uint %23099 %uint_3
      %15968 = OpIMul %uint %17722 %uint_3
      %18268 = OpBitcast %int %15968
      %14598 = OpImageFetch %v4uint %19954 %18268
       %6376 = OpCompositeExtract %uint %14598 0
      %17705 = OpConvertUToF %float %6376
      %12314 = OpBitwiseAnd %uint %23099 %uint_7
      %14345 = OpCompositeExtract %uint %14598 1
      %16230 = OpIMul %uint %12314 %14345
      %17759 = OpConvertUToF %float %16230
      %22854 = OpFMul %float %17759 %float_0_125
      %11948 = OpFAdd %float %17705 %22854
       %7244 = OpFMul %float %11948 %float_1_52737048en05
      %22361 = OpExtInst %float %1 FClamp %7244 %float_0 %float_1
       %7500 = OpCompositeInsert %v4float %22361 %10264 0
      %19969 = OpCompositeExtract %uint %11099 1
      %18592 = OpShiftRightLogical %uint %19969 %uint_3
      %15827 = OpIMul %uint %18592 %uint_3
      %18887 = OpIAdd %uint %15827 %uint_1
      %14460 = OpBitcast %int %18887
      %17829 = OpImageFetch %v4uint %19954 %14460
       %6377 = OpCompositeExtract %uint %17829 0
      %17706 = OpConvertUToF %float %6377
      %12315 = OpBitwiseAnd %uint %19969 %uint_7
      %14346 = OpCompositeExtract %uint %17829 1
      %16231 = OpIMul %uint %12315 %14346
      %17760 = OpConvertUToF %float %16231
      %22855 = OpFMul %float %17760 %float_0_125
      %11949 = OpFAdd %float %17706 %22855
       %7245 = OpFMul %float %11949 %float_1_52737048en05
      %22362 = OpExtInst %float %1 FClamp %7245 %float_0 %float_1
       %7501 = OpCompositeInsert %v4float %22362 %7500 1
      %19970 = OpCompositeExtract %uint %11099 2
      %18593 = OpShiftRightLogical %uint %19970 %uint_3
      %15828 = OpIMul %uint %18593 %uint_3
      %18888 = OpIAdd %uint %15828 %uint_2
      %14461 = OpBitcast %int %18888
      %17830 = OpImageFetch %v4uint %19954 %14461
       %6378 = OpCompositeExtract %uint %17830 0
      %17707 = OpConvertUToF %float %6378
      %12316 = OpBitwiseAnd %uint %19970 %uint_7
      %14347 = OpCompositeExtract %uint %17830 1
      %16232 = OpIMul %uint %12316 %14347
      %17761 = OpConvertUToF %float %16232
      %22856 = OpFMul %float %17761 %float_0_125
      %11950 = OpFAdd %float %17707 %22856
       %7246 = OpFMul %float %11950 %float_1_52737048en05
      %22380 = OpExtInst %float %1 FClamp %7246 %float_0 %float_1
      %23871 = OpCompositeInsert %v4float %22380 %7501 2
      %15087 = OpCompositeInsert %v4float %float_1 %23871 3
      %16359 = OpLoad %166 %3258
               OpImageWrite %16359 %10533 %15087
               OpBranch %21573
      %21573 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t apply_gamma_pwl_cs[] = {
    0x07230203, 0x00010000, 0x0008000A, 0x00005DC5, 0x00000000, 0x00020011,
    0x00000001, 0x00020011, 0x0000002E, 0x00020011, 0x00000031, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E,
    0x00000000, 0x00000001, 0x0006000F, 0x00000005, 0x0000161F, 0x6E69616D,
    0x00000000, 0x00000F48, 0x00060010, 0x0000161F, 0x00000011, 0x00000010,
    0x00000008, 0x00000001, 0x00040047, 0x00000F48, 0x0000000B, 0x0000001C,
    0x00050048, 0x000003E1, 0x00000000, 0x00000023, 0x00000000, 0x00030047,
    0x000003E1, 0x00000002, 0x00040047, 0x0000167F, 0x00000022, 0x00000001,
    0x00040047, 0x0000167F, 0x00000021, 0x00000000, 0x00040047, 0x00001739,
    0x00000022, 0x00000000, 0x00040047, 0x00001739, 0x00000021, 0x00000000,
    0x00040047, 0x00000CBA, 0x00000022, 0x00000002, 0x00040047, 0x00000CBA,
    0x00000021, 0x00000000, 0x00030047, 0x00000CBA, 0x00000019, 0x00040047,
    0x00000B0F, 0x0000000B, 0x00000019, 0x00020013, 0x00000008, 0x00030021,
    0x00000502, 0x00000008, 0x00030016, 0x0000000D, 0x00000020, 0x00040015,
    0x0000000B, 0x00000020, 0x00000000, 0x00040017, 0x00000011, 0x0000000B,
    0x00000002, 0x0004002B, 0x0000000D, 0x00000A0C, 0x00000000, 0x0004002B,
    0x0000000D, 0x0000008A, 0x3F800000, 0x0004002B, 0x0000000B, 0x00000A0A,
    0x00000000, 0x0004002B, 0x0000000B, 0x00000A1F, 0x00000007, 0x0004002B,
    0x0000000B, 0x00000A0D, 0x00000001, 0x0004002B, 0x0000000D, 0x000001E0,
    0x3E000000, 0x0004002B, 0x0000000D, 0x000009AA, 0x37802008, 0x00040017,
    0x00000014, 0x0000000B, 0x00000003, 0x00040020, 0x00000291, 0x00000001,
    0x00000014, 0x0004003B, 0x00000291, 0x00000F48, 0x00000001, 0x0003001E,
    0x000003E1, 0x00000011, 0x00040020, 0x0000065E, 0x00000009, 0x000003E1,
    0x0004003B, 0x0000065E, 0x0000118F, 0x00000009, 0x00040015, 0x0000000C,
    0x00000020, 0x00000001, 0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000,
    0x00040020, 0x0000028E, 0x00000009, 0x00000011, 0x00020014, 0x00000009,
    0x00040017, 0x0000000F, 0x00000009, 0x00000002, 0x00090019, 0x00000096,
    0x0000000D, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x00000000, 0x00040020, 0x00000313, 0x00000000, 0x00000096, 0x0004003B,
    0x00000313, 0x0000167F, 0x00000000, 0x00040017, 0x00000012, 0x0000000C,
    0x00000002, 0x00040017, 0x0000001D, 0x0000000D, 0x00000004, 0x00040017,
    0x00000018, 0x0000000D, 0x00000003, 0x0004002B, 0x0000000D, 0x00000409,
    0x447FC000, 0x0004002B, 0x0000000D, 0x000000FC, 0x3F000000, 0x00090019,
    0x00000098, 0x0000000B, 0x00000005, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000000, 0x00040020, 0x00000315, 0x00000000, 0x00000098,
    0x0004003B, 0x00000315, 0x00001739, 0x00000000, 0x0004002B, 0x0000000B,
    0x00000A13, 0x00000003, 0x00040017, 0x00000017, 0x0000000B, 0x00000004,
    0x0004002B, 0x0000000B, 0x00000A10, 0x00000002, 0x00090019, 0x000000A6,
    0x0000000D, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000002,
    0x0000000B, 0x00040020, 0x00000323, 0x00000000, 0x000000A6, 0x0004003B,
    0x00000323, 0x00000CBA, 0x00000000, 0x0004002B, 0x0000000B, 0x00000A3A,
    0x00000010, 0x0004002B, 0x0000000B, 0x00000A22, 0x00000008, 0x0006002C,
    0x00000014, 0x00000B0F, 0x00000A3A, 0x00000A22, 0x00000A0D, 0x00030001,
    0x0000001D, 0x00002818, 0x0006002C, 0x00000018, 0x000003AB, 0x000000FC,
    0x000000FC, 0x000000FC, 0x00050036, 0x00000008, 0x0000161F, 0x00000000,
    0x00000502, 0x000200F8, 0x00003B06, 0x000300F7, 0x00005445, 0x00000000,
    0x000300FB, 0x00000A0A, 0x00003272, 0x000200F8, 0x00003272, 0x0004003D,
    0x00000014, 0x000035C1, 0x00000F48, 0x0007004F, 0x00000011, 0x000054D5,
    0x000035C1, 0x000035C1, 0x00000000, 0x00000001, 0x00050041, 0x0000028E,
    0x00001E50, 0x0000118F, 0x00000A0B, 0x0004003D, 0x00000011, 0x00003442,
    0x00001E50, 0x000500AE, 0x0000000F, 0x00005B8D, 0x000054D5, 0x00003442,
    0x0004009A, 0x00000009, 0x00005A24, 0x00005B8D, 0x000300F7, 0x0000477E,
    0x00000002, 0x000400FA, 0x00005A24, 0x000055E8, 0x0000477E, 0x000200F8,
    0x000055E8, 0x000200F9, 0x00005445, 0x000200F8, 0x0000477E, 0x0004003D,
    0x00000096, 0x00005DC4, 0x0000167F, 0x0004007C, 0x00000012, 0x00002925,
    0x000054D5, 0x0007005F, 0x0000001D, 0x00001A18, 0x00005DC4, 0x00002925,
    0x00000002, 0x00000A0B, 0x0008004F, 0x00000018, 0x00003F72, 0x00001A18,
    0x00001A18, 0x00000000, 0x00000001, 0x00000002, 0x0005008E, 0x00000018,
    0x00003653, 0x00003F72, 0x00000409, 0x00050081, 0x00000018, 0x000041F9,
    0x00003653, 0x000003AB, 0x0004006D, 0x00000014, 0x00002B5B, 0x000041F9,
    0x0004003D, 0x00000098, 0x00004DF2, 0x00001739, 0x00050051, 0x0000000B,
    0x00005A3B, 0x00002B5B, 0x00000000, 0x000500C2, 0x0000000B, 0x0000453A,
    0x00005A3B, 0x00000A13, 0x00050084, 0x0000000B, 0x00003E60, 0x0000453A,
    0x00000A13, 0x0004007C, 0x0000000C, 0x0000475C, 0x00003E60, 0x0005005F,
    0x00000017, 0x00003906, 0x00004DF2, 0x0000475C, 0x00050051, 0x0000000B,
    0x000018E8, 0x00003906, 0x00000000, 0x00040070, 0x0000000D, 0x00004529,
    0x000018E8, 0x000500C7, 0x0000000B, 0x0000301A, 0x00005A3B, 0x00000A1F,
    0x00050051, 0x0000000B, 0x00003809, 0x00003906, 0x00000001, 0x00050084,
    0x0000000B, 0x00003F66, 0x0000301A, 0x00003809, 0x00040070, 0x0000000D,
    0x0000455F, 0x00003F66, 0x00050085, 0x0000000D, 0x00005946, 0x0000455F,
    0x000001E0, 0x00050081, 0x0000000D, 0x00002EAC, 0x00004529, 0x00005946,
    0x00050085, 0x0000000D, 0x00001C4C, 0x00002EAC, 0x000009AA, 0x0008000C,
    0x0000000D, 0x00005759, 0x00000001, 0x0000002B, 0x00001C4C, 0x00000A0C,
    0x0000008A, 0x00060052, 0x0000001D, 0x00001D4C, 0x00005759, 0x00002818,
    0x00000000, 0x00050051, 0x0000000B, 0x00004E01, 0x00002B5B, 0x00000001,
    0x000500C2, 0x0000000B, 0x000048A0, 0x00004E01, 0x00000A13, 0x00050084,
    0x0000000B, 0x00003DD3, 0x000048A0, 0x00000A13, 0x00050080, 0x0000000B,
    0x000049C7, 0x00003DD3, 0x00000A0D, 0x0004007C, 0x0000000C, 0x0000387C,
    0x000049C7, 0x0005005F, 0x00000017, 0x000045A5, 0x00004DF2, 0x0000387C,
    0x00050051, 0x0000000B, 0x000018E9, 0x000045A5, 0x00000000, 0x00040070,
    0x0000000D, 0x0000452A, 0x000018E9, 0x000500C7, 0x0000000B, 0x0000301B,
    0x00004E01, 0x00000A1F, 0x00050051, 0x0000000B, 0x0000380A, 0x000045A5,
    0x00000001, 0x00050084, 0x0000000B, 0x00003F67, 0x0000301B, 0x0000380A,
    0x00040070, 0x0000000D, 0x00004560, 0x00003F67, 0x00050085, 0x0000000D,
    0x00005947, 0x00004560, 0x000001E0, 0x00050081, 0x0000000D, 0x00002EAD,
    0x0000452A, 0x00005947, 0x00050085, 0x0000000D, 0x00001C4D, 0x00002EAD,
    0x000009AA, 0x0008000C, 0x0000000D, 0x0000575A, 0x00000001, 0x0000002B,
    0x00001C4D, 0x00000A0C, 0x0000008A, 0x00060052, 0x0000001D, 0x00001D4D,
    0x0000575A, 0x00001D4C, 0x00000001, 0x00050051, 0x0000000B, 0x00004E02,
    0x00002B5B, 0x00000002, 0x000500C2, 0x0000000B, 0x000048A1, 0x00004E02,
    0x00000A13, 0x00050084, 0x0000000B, 0x00003DD4, 0x000048A1, 0x00000A13,
    0x00050080, 0x0000000B, 0x000049C8, 0x00003DD4, 0x00000A10, 0x0004007C,
    0x0000000C, 0x0000387D, 0x000049C8, 0x0005005F, 0x00000017, 0x000045A6,
    0x00004DF2, 0x0000387D, 0x00050051, 0x0000000B, 0x000018EA, 0x000045A6,
    0x00000000, 0x00040070, 0x0000000D, 0x0000452B, 0x000018EA, 0x000500C7,
    0x0000000B, 0x0000301C, 0x00004E02, 0x00000A1F, 0x00050051, 0x0000000B,
    0x0000380B, 0x000045A6, 0x00000001, 0x00050084, 0x0000000B, 0x00003F68,
    0x0000301C, 0x0000380B, 0x00040070, 0x0000000D, 0x00004561, 0x00003F68,
    0x00050085, 0x0000000D, 0x00005948, 0x00004561, 0x000001E0, 0x00050081,
    0x0000000D, 0x00002EAE, 0x0000452B, 0x00005948, 0x00050085, 0x0000000D,
    0x00001C4E, 0x00002EAE, 0x000009AA, 0x0008000C, 0x0000000D, 0x0000576C,
    0x00000001, 0x0000002B, 0x00001C4E, 0x00000A0C, 0x0000008A, 0x00060052,
    0x0000001D, 0x00005D3F, 0x0000576C, 0x00001D4D, 0x00000002, 0x00060052,
    0x0000001D, 0x00003AEF, 0x0000008A, 0x00005D3F, 0x00000003, 0x0004003D,
    0x000000A6, 0x00003FE7, 0x00000CBA, 0x00040063, 0x00003FE7, 0x00002925,
    0x00003AEF, 0x000200F9, 0x00005445, 0x000200F8, 0x00005445, 0x000100FD,
    0x00010038,
};
