; ModuleID = 'src/cpu/xethunk/xethunk.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.8.0"

%struct.xe_module_init_options_t = type { i8* }

@xe_memory_base = internal global i8* null, align 8

define i32 @xe_module_init(%struct.xe_module_init_options_t* %options) nounwind uwtable ssp {
  %1 = alloca %struct.xe_module_init_options_t*, align 8
  store %struct.xe_module_init_options_t* %options, %struct.xe_module_init_options_t** %1, align 8
  %2 = load %struct.xe_module_init_options_t** %1, align 8
  %3 = getelementptr inbounds %struct.xe_module_init_options_t* %2, i32 0, i32 0
  %4 = load i8** %3, align 8
  store i8* %4, i8** @xe_memory_base, align 8
  ret i32 0
}

define void @xe_module_uninit() nounwind uwtable ssp {
  ret void
}
