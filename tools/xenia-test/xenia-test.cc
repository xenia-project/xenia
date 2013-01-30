/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>

#include <dirent.h>
#include <gflags/gflags.h>


using namespace std;
using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;


DEFINE_string(test_path, "test/codegen/",
    "Directory scanned for test files.");


typedef vector<pair<string, string> > annotations_list_t;


int read_annotations(xe_pal_ref pal, string& src_file_path,
                     annotations_list_t& annotations) {
  // TODO(benvanik): use PAL instead of this
  FILE* f = fopen(src_file_path.c_str(), "r");
  char line_buffer[BUFSIZ];
  while (fgets(line_buffer, sizeof(line_buffer), f)) {
    if (strlen(line_buffer) > 3 &&
        line_buffer[0] == '#' &&
        line_buffer[1] == ' ') {
      // Comment - check if formed like an annotation.
      // We don't actually verify anything here.
      char* next_space = strchr(line_buffer + 3, ' ');
      if (next_space) {
        // Looks legit.
        string key = string(line_buffer + 2, next_space);
        string value = string(next_space + 1);
        while (value.find_last_of(" \t\n") == value.size() - 1) {
          value.erase(value.end() - 1);
        }
        annotations.push_back(pair<string, string>(key, value));
      }
    }
  }
  fclose(f);

  return 0;
}

int setup_test_state(xe_memory_ref memory, Processor* processor,
                     ThreadState* thread_state,
                     annotations_list_t& annotations) {
  xe_ppc_state_t* ppc_state = thread_state->ppc_state();

  for (annotations_list_t::iterator it = annotations.begin();
       it != annotations.end(); ++it) {
    if (it->first == "REGISTER_IN") {
      size_t space_pos = it->second.find(" ");
      string reg_name = it->second.substr(0, space_pos);
      string reg_value = it->second.substr(space_pos + 1);
      ppc_state->SetRegFromString(reg_name.c_str(), reg_value.c_str());
    }
  }
  return 0;
}

int check_test_results(xe_memory_ref memory, Processor* processor,
                       ThreadState* thread_state,
                       annotations_list_t& annotations) {
  xe_ppc_state_t* ppc_state = thread_state->ppc_state();

  char actual_value[2048];

  bool any_failed = false;
  for (annotations_list_t::iterator it = annotations.begin();
       it != annotations.end(); ++it) {
    if (it->first == "REGISTER_OUT") {
      size_t space_pos = it->second.find(" ");
      string reg_name = it->second.substr(0, space_pos);
      string reg_value = it->second.substr(space_pos + 1);
      if (!ppc_state->CompareRegWithString(
          reg_name.c_str(), reg_value.c_str(),
          actual_value, XECOUNT(actual_value))) {
        any_failed = true;
        printf("Register %s assert failed:\n", reg_name.c_str());
        printf("  Expected: %s == %s\n", reg_name.c_str(), reg_value.c_str());
        printf("    Actual: %s == %s\n", reg_name.c_str(), actual_value);
      }
    }
  }
  return any_failed;
}

int run_test(xe_pal_ref pal, string& src_file_path) {
  int result_code = 1;

  // test.s -> test.bin
  string bin_file_path;
  size_t dot = src_file_path.find_last_of(".s");
  bin_file_path = src_file_path;
  bin_file_path.replace(dot - 1, 2, ".bin");

  xe_memory_ref memory = NULL;
  shared_ptr<Processor> processor;
  shared_ptr<Runtime> runtime;
  annotations_list_t annotations;
  ThreadState* thread_state = NULL;

  XEEXPECTZERO(read_annotations(pal, src_file_path, annotations));

  xe_memory_options_t memory_options;
  xe_zero_struct(&memory_options, sizeof(memory_options));
  memory = xe_memory_create(pal, memory_options);
  XEEXPECTNOTNULL(memory);

  processor = shared_ptr<Processor>(new Processor(pal, memory));
  XEEXPECTZERO(processor->Setup());

  runtime = shared_ptr<Runtime>(new Runtime(pal, processor, XT("")));

  // Load the binary module.
  XEEXPECTZERO(runtime->LoadBinaryModule(bin_file_path.c_str(), 0x82010000));

  // Simulate a thread.
  thread_state = processor->AllocThread(0x80000000, 256 * 1024 * 1024);

  // Setup test state from annotations.
  XEEXPECTZERO(setup_test_state(memory, processor.get(), thread_state,
                                annotations));

  // Execute test.
  XEEXPECTZERO(processor->Execute(thread_state, 0x82010000));

  // Assert test state expectations.
  XEEXPECTZERO(check_test_results(memory, processor.get(), thread_state,
                                  annotations));

  result_code = 0;
XECLEANUP:
  if (processor && thread_state) {
    processor->DeallocThread(thread_state);
  }
  runtime.reset();
  processor.reset();
  xe_memory_release(memory);
  return result_code;
}

int discover_tests(string& test_path,
                   vector<string>& test_files) {
  // TODO(benvanik): use PAL instead of this
  DIR* d = opendir(test_path.c_str());
  struct dirent* dir;
  while ((dir = readdir(d))) {
    if (dir->d_type == DT_REG) {
      // Only return .s files.
      string file_name = string(dir->d_name);
      if (file_name.rfind(".s") != string::npos) {
        string file_path = test_path;
        if (*(test_path.end() - 1) != '/') {
          file_path += "/";
        }
        file_path += file_name;
        test_files.push_back(file_path);
      }
    }
  }
  closedir(d);
  return 0;
}

int run_tests(const xechar_t* test_name) {
  int result_code = 1;
  int failed_count = 0;
  int passed_count = 0;

  xe_pal_ref pal = NULL;
  vector<string> test_files;

  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  pal = xe_pal_create(pal_options);
  XEEXPECTNOTNULL(pal);

  XEEXPECTZERO(discover_tests(FLAGS_test_path, test_files));
  if (!test_files.size()) {
    printf("No tests discovered - invalid path?\n");
    XEFAIL();
  }
  printf("%d tests discovered.\n", (int)test_files.size());
  printf("\n");

  for (vector<string>::iterator it = test_files.begin();
       it != test_files.end(); ++it) {
    if (test_name && *it != test_name) {
      continue;
    }

    printf("Running %s...\n", (*it).c_str());
    if (run_test(pal, *it)) {
      printf("TEST FAILED\n");
      failed_count++;
    } else {
      printf("Passed\n");
      passed_count++;
    }
  }

  printf("\n");
  printf("Total tests: %d\n", failed_count + passed_count);
  printf("Passed: %d\n", passed_count);
  printf("Failed: %d\n", failed_count);

  result_code = failed_count ? 1 : 0;
XECLEANUP:
  xe_pal_release(pal);
  return result_code;
}

int xenia_test(int argc, xechar_t **argv) {
  string usage = "usage: ";
  usage += "xenia-test some.xex";
  google::SetUsageMessage(usage);
  google::SetVersionString("1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);

  int result_code = 1;

  // Grab test name, if present.
  const xechar_t* test_name = NULL;
  if (argc >= 2) {
    test_name = argv[1];
  }

  result_code = run_tests(test_name);

  google::ShutDownCommandLineFlags();
  return result_code;
}
XE_MAIN_THUNK(xenia_test);
