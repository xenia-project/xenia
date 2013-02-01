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
using namespace xe::kernel;


DEFINE_bool(abort_before_entry, false,
    "Abort execution right before launching the module.");


class Run {
public:
  Run();
  ~Run();

  int Setup();
  int Launch(const xechar_t* path);

private:
  xe_pal_ref      pal_;
  xe_memory_ref   memory_;
  shared_ptr<Processor> processor_;
  shared_ptr<Runtime>   runtime_;
};

Run::Run() {
}

Run::~Run() {
  xe_memory_release(memory_);
  xe_pal_release(pal_);
}

int Run::Setup() {
  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  pal_ = xe_pal_create(pal_options);
  XEEXPECTNOTNULL(pal_);

  xe_memory_options_t memory_options;
  xe_zero_struct(&memory_options, sizeof(memory_options));
  memory_ = xe_memory_create(pal_, memory_options);
  XEEXPECTNOTNULL(memory_);

  processor_ = shared_ptr<Processor>(new Processor(pal_, memory_));
  XEEXPECTZERO(processor_->Setup());

  runtime_ = shared_ptr<Runtime>(new Runtime(pal_, processor_, XT("")));

  return 0;
XECLEANUP:
  return 1;
}

int Run::Launch(const xechar_t* path) {
  if (FLAGS_abort_before_entry) {
    return 0;
  }

  // Normalize the path and make absolute.
  // TODO(benvanik): move this someplace common.
  xechar_t abs_path[XE_MAX_PATH];
#if XE_PLATFORM(WIN32)
  _wfullpath(abs_path, path, XECOUNT(abs_path));
#else
  realpath(path, abs_path);
#endif  // WIN32

  // Grab file extension.
  const xechar_t* dot = xestrrchr(abs_path, '.');
  if (!dot) {
    XELOGE(XT("Invalid input path; no extension found"));
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

int xenia_run(int argc, xechar_t **argv) {
  std::string usage = "usage: ";
  usage += "xenia-run some.xex";
  google::SetUsageMessage(usage);
  google::SetVersionString("1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);

  // Dummy call to keep the GPU code linking in to ensure it's working.
  do_gpu_stuff();

  int result_code = 1;

  // Grab path.
  if (argc < 2) {
    google::ShowUsageWithFlags("xenia-run");
    return 1;
  }
  const xechar_t *path = argv[1];

  auto_ptr<Run> run = auto_ptr<Run>(new Run());

  result_code = run->Setup();
  XEEXPECTZERO(result_code);

  //xe_module_dump(run->module);

  run->Launch(path);

  result_code = 0;
XECLEANUP:

  google::ShutDownCommandLineFlags();
  return result_code;
}
XE_MAIN_THUNK(xenia_run);
