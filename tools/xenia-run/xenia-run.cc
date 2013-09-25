/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>

#include <gflags/gflags.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::gpu;
using namespace xe::dbg;
using namespace xe::kernel;


DEFINE_string(target, "",
    "Specifies the target .xex or .iso to execute.");


class Run {
public:
  Run();
  ~Run();

  int Setup();
  int Launch(const xechar_t* path);

private:
  xe_memory_ref   memory_;
  shared_ptr<Backend>         backend_;
  shared_ptr<GraphicsSystem>  graphics_system_;
  shared_ptr<Processor>       processor_;
  shared_ptr<Runtime>         runtime_;
  shared_ptr<Debugger>        debugger_;
};

Run::Run() {
}

Run::~Run() {
  xe_memory_release(memory_);
}

int Run::Setup() {
  CreationParams params;

  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  XEEXPECTZERO(xe_pal_init(pal_options));

  xe_memory_options_t memory_options;
  xe_zero_struct(&memory_options, sizeof(memory_options));
  memory_ = xe_memory_create(memory_options);
  XEEXPECTNOTNULL(memory_);

  backend_ = shared_ptr<Backend>(new xe::cpu::x64::X64Backend());

  params.memory = memory_;
  graphics_system_ = shared_ptr<GraphicsSystem>(xe::gpu::CreateNop(&params));

  debugger_ = shared_ptr<Debugger>(new Debugger());

  processor_ = shared_ptr<Processor>(new Processor(memory_, backend_));
  processor_->set_graphics_system(graphics_system_);
  graphics_system_->set_processor(processor_);

  XEEXPECTZERO(processor_->Setup());

  runtime_ = shared_ptr<Runtime>(new Runtime(processor_, XT("")));
  processor_->set_export_resolver(runtime_->export_resolver());

  return 0;
XECLEANUP:
  return 1;
}

int Run::Launch(const xechar_t* path) {
  // Normalize the path and make absolute.
  // TODO(benvanik): move this someplace common.
  xechar_t abs_path[XE_MAX_PATH];
  xe_path_get_absolute(path, abs_path, XECOUNT(abs_path));

  // Grab file extension.
  const xechar_t* dot = xestrrchr(abs_path, '.');
  if (!dot) {
    XELOGE("Invalid input path; no extension found");
    return 1;
  }

  // Run the debugger.
  // This may pause waiting for connections.
  if (debugger_->Startup()) {
    XELOGE("Debugger failed to start up");
    return 1;
  }

  // Launch based on file type.
  // This is a silly guess based on file extension.
  // NOTE: the runtime launch routine will wait until the module exits.
  if (xestrcmp(dot, XT(".xex")) == 0) {
    // Treat as a naked xex file.
    return runtime_->LaunchXexFile(abs_path);
  } else {
    // Assume a disc image.
    return runtime_->LaunchDiscImage(abs_path);
  }
}

int xenia_run(int argc, xechar_t** argv) {
  int result_code = 1;

  // Grab path from the flag or unnamed argument.
  if (!FLAGS_target.size() && argc < 2) {
    google::ShowUsageWithFlags("xenia-run");
    return 1;
  }
  const xechar_t* path = NULL;
  if (FLAGS_target.size()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports unicode.
    xechar_t buffer[XE_MAX_PATH];
    XEIGNORE(xestrwiden(buffer, sizeof(buffer), FLAGS_target.c_str()));
    path = buffer;
  } else {
    // Passed as an unnamed argument.
    path = argv[1];
  }

  auto_ptr<Run> run = auto_ptr<Run>(new Run());

  result_code = run->Setup();
  XEEXPECTZERO(result_code);

  run->Launch(path);

  result_code = 0;
XECLEANUP:
  return result_code;
}
XE_MAIN_THUNK(xenia_run, "xenia-run some.xex");
