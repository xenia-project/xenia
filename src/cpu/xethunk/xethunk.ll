; ModuleID = 'src/cpu/xethunk/xethunk.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.8.0"

define i32 @xe_module_init() nounwind uwtable ssp {
  ret i32 0
}

define void @xe_module_uninit() nounwind uwtable ssp {
  ret void
}
