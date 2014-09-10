/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/alloy.h>
#include <alloy/backend/ivm/ivm_backend.h>
#include <alloy/backend/x64/x64_backend.h>
#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/runtime/raw_module.h>
#include <poly/main.h>
#include <poly/poly.h>

#if !XE_LIKE_WIN32
#include <dirent.h>
#endif  // !WIN32
#include <gflags/gflags.h>

DEFINE_string(test_path, "src/alloy/frontend/ppc/test/",
              "Directory scanned for test files.");

namespace alloy {
namespace test {

using alloy::frontend::ppc::PPCContext;
using alloy::runtime::Runtime;

typedef std::vector<std::pair<std::string, std::string>> AnnotationList;

const uint32_t START_ADDRESS = 0x100000;

class ThreadState : public alloy::runtime::ThreadState {
 public:
  ThreadState(Runtime* runtime, uint32_t thread_id, uint64_t stack_address,
              size_t stack_size, uint64_t thread_state_address)
      : alloy::runtime::ThreadState(runtime, thread_id),
        stack_address_(stack_address),
        stack_size_(stack_size),
        thread_state_address_(thread_state_address) {
    memset(memory_->Translate(stack_address_), 0, stack_size_);

    // Allocate with 64b alignment.
    context_ = (PPCContext*)calloc(1, sizeof(PPCContext));
    assert_true((reinterpret_cast<uint64_t>(context_) & 0xF) == 0);

    // Stash pointers to common structures that callbacks may need.
    context_->reserve_address = memory_->reserve_address();
    context_->membase = memory_->membase();
    context_->runtime = runtime;
    context_->thread_state = this;

    // Set initial registers.
    context_->r[1] = stack_address_ + stack_size;
    context_->r[13] = thread_state_address_;

    // Pad out stack a bit, as some games seem to overwrite the caller by about
    // 16 to 32b.
    context_->r[1] -= 64;

    raw_context_ = context_;

    runtime_->debugger()->OnThreadCreated(this);
  }
  ~ThreadState() override {
    runtime_->debugger()->OnThreadDestroyed(this);
    free(context_);
  }

  PPCContext* context() const { return context_; }

 private:
  uint64_t stack_address_;
  size_t stack_size_;
  uint64_t thread_state_address_;

  // NOTE: must be 64b aligned for SSE ops.
  PPCContext* context_;
};

bool ReadAnnotations(std::wstring& src_file_path, AnnotationList& annotations) {
  // TODO(benvanik): use PAL instead of this
  FILE* f = fopen(poly::to_string(src_file_path).c_str(), "r");
  char line_buffer[BUFSIZ];
  while (fgets(line_buffer, sizeof(line_buffer), f)) {
    if (strlen(line_buffer) > 3 && line_buffer[0] == '#' &&
        line_buffer[1] == ' ') {
      // Comment - check if formed like an annotation.
      // We don't actually verify anything here.
      char* next_space = strchr(line_buffer + 3, ' ');
      if (next_space) {
        // Looks legit.
        std::string key(line_buffer + 2, next_space);
        std::string value(next_space + 1);
        while (value.find_last_of(" \t\n") == value.size() - 1) {
          value.erase(value.end() - 1);
        }
        annotations.emplace_back(key, value);
      }
    }
  }
  fclose(f);
  return true;
}

class TestRunner {
 public:
  TestRunner() {
    memory_size = 64 * 1024 * 1024;
    memory.reset(new SimpleMemory(memory_size));

    runtime.reset(new Runtime(memory.get()));
    auto frontend =
        std::make_unique<alloy::frontend::ppc::PPCFrontend>(runtime.get());
    runtime->Initialize(std::move(frontend), nullptr);
  }

  ~TestRunner() {
    thread_state.reset();
    runtime.reset();
    memory.reset();
  }

  bool Setup(std::wstring& src_file_path) {
    // test.s -> test.bin
    std::wstring bin_file_path;
    size_t dot = src_file_path.find_last_of(L".s");
    bin_file_path = src_file_path;
    bin_file_path.replace(dot - 1, 2, L".bin");

    // Read annotations so we can setup state/etc.
    if (!ReadAnnotations(src_file_path, annotations)) {
      PLOGE("Unable to read annotations for test %ls", src_file_path.c_str());
      return false;
    }

    // Load the binary module.
    auto module = std::make_unique<alloy::runtime::RawModule>(runtime.get());
    if (module->LoadFile(START_ADDRESS, bin_file_path)) {
      PLOGE("Unable to load test binary %ls", bin_file_path.c_str());
      return false;
    }
    runtime->AddModule(std::move(module));

    // Simulate a thread.
    uint64_t stack_size = 64 * 1024;
    uint64_t stack_address = START_ADDRESS - stack_size;
    uint64_t thread_state_address = stack_address - 0x1000;
    thread_state.reset(new ThreadState(runtime.get(), 0x100, stack_address,
                                       stack_size, thread_state_address));

    return true;
  }

  bool Run() {
    // Setup test state from annotations.
    if (!SetupTestState()) {
      PLOGE("Test setup failed");
      return false;
    }

    // Execute test.
    alloy::runtime::Function* fn;
    runtime->ResolveFunction(START_ADDRESS, &fn);
    if (!fn) {
      PLOGE("Entry function not found");
      return false;
    }

    auto ctx = thread_state->context();
    ctx->lr = 0xBEBEBEBE;
    fn->Call(thread_state.get(), ctx->lr);

    // Assert test state expectations.
    bool result = CheckTestResults();

    return result;
  }

  bool SetupTestState() {
    auto ppc_state = thread_state->context();

    for (AnnotationList::iterator it = annotations.begin();
         it != annotations.end(); ++it) {
      if (it->first == "REGISTER_IN") {
        size_t space_pos = it->second.find(" ");
        auto reg_name = it->second.substr(0, space_pos);
        auto reg_value = it->second.substr(space_pos + 1);
        ppc_state->SetRegFromString(reg_name.c_str(), reg_value.c_str());
      }
    }
    return true;
  }

  bool CheckTestResults() {
    auto ppc_state = thread_state->context();

    char actual_value[2048];

    bool any_failed = false;
    for (AnnotationList::iterator it = annotations.begin();
         it != annotations.end(); ++it) {
      if (it->first == "REGISTER_OUT") {
        size_t space_pos = it->second.find(" ");
        auto reg_name = it->second.substr(0, space_pos);
        auto reg_value = it->second.substr(space_pos + 1);
        if (!ppc_state->CompareRegWithString(reg_name.c_str(),
                                             reg_value.c_str(), actual_value,
                                             poly::countof(actual_value))) {
          any_failed = true;
          printf("Register %s assert failed:\n", reg_name.c_str());
          printf("  Expected: %s == %s\n", reg_name.c_str(), reg_value.c_str());
          printf("    Actual: %s == %s\n", reg_name.c_str(), actual_value);
        }
      }
    }
    return !any_failed;
  }

  size_t memory_size;
  std::unique_ptr<Memory> memory;
  std::unique_ptr<Runtime> runtime;
  std::unique_ptr<ThreadState> thread_state;
  AnnotationList annotations;
};

bool DiscoverTests(std::wstring& test_path,
                   std::vector<std::wstring>& test_files) {
// TODO(benvanik): use PAL instead of this.
#if XE_LIKE_WIN32
  std::wstring search_path = test_path;
  search_path.append(L"\\*.s");
  WIN32_FIND_DATA ffd;
  HANDLE hFind = FindFirstFile(search_path.c_str(), &ffd);
  if (hFind == INVALID_HANDLE_VALUE) {
    PLOGE("Unable to find test path %s", test_path.c_str());
    return false;
  }
  do {
    if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      std::wstring file_name(ffd.cFileName);
      std::wstring file_path = test_path;
      if (*(test_path.end() - 1) != '\\') {
        file_path += '\\';
      }
      file_path += file_name;
      test_files.push_back(file_path);
    }
  } while (FindNextFile(hFind, &ffd));
  FindClose(hFind);
#else
  DIR* d = opendir(test_path.c_str());
  if (!d) {
    PLOGE("Unable to find test path %s", test_path.c_str());
    return false;
  }
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
#endif  // WIN32
  return true;
}

bool RunTests(const std::wstring& test_name) {
  int result_code = 1;
  int failed_count = 0;
  int passed_count = 0;

  auto test_path = poly::fix_path_separators(poly::to_wstring(FLAGS_test_path));
  std::vector<std::wstring> test_files;
  if (!DiscoverTests(test_path, test_files)) {
    return false;
  }
  if (!test_files.size()) {
    PLOGE("No tests discovered - invalid path?");
    return false;
  }
  PLOGI("%d tests discovered.", (int)test_files.size());
  PLOGI("");

  for (auto& test_path : test_files) {
    if (!test_name.empty() && test_path != test_name) {
      continue;
    }

    PLOGI("Running %ls...", test_path.c_str());
    TestRunner runner;
    if (!runner.Setup(test_path)) {
      PLOGE("TEST FAILED SETUP");
      ++failed_count;
    }
    if (runner.Run()) {
      PLOGI("Passed");
      ++passed_count;
    } else {
      PLOGE("TEST FAILED");
      ++failed_count;
    }
  }

  PLOGI("");
  PLOGI("Total tests: %d", failed_count + passed_count);
  PLOGI("Passed: %d", passed_count);
  PLOGI("Failed: %d", failed_count);

  return failed_count ? false : true;
}

int main(std::vector<std::wstring>& args) {
  // Grab test name, if present.
  std::wstring test_name;
  if (args.size() >= 2) {
    test_name = args[1];
  }

  return RunTests(test_name) ? 0 : 1;
}

}  // namespace test
}  // namespace alloy

DEFINE_ENTRY_POINT(L"alloy-ppc-test", L"alloy-ppc-test [test name]",
                   alloy::test::main);
