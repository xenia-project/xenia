/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/cpu/cpu.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/frontend/ppc_context.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/raw_module.h"

#if !XE_PLATFORM_WIN32
#include <dirent.h>
#endif  // !WIN32
#include <gflags/gflags.h>

DEFINE_string(test_path, "src/xenia/cpu/frontend/test/",
              "Directory scanned for test files.");
DEFINE_string(test_bin_path, "src/xenia/cpu/frontend/test/bin/",
              "Directory with binary outputs of the test files.");

namespace xe {
namespace cpu {
namespace test {

using xe::cpu::frontend::PPCContext;
using xe::cpu::Runtime;

typedef std::vector<std::pair<std::string, std::string>> AnnotationList;

const uint32_t START_ADDRESS = 0x80000000;

struct TestCase {
  TestCase(uint32_t address, std::string& name)
      : address(address), name(name) {}
  uint32_t address;
  std::string name;
  AnnotationList annotations;
};

class TestSuite {
 public:
  TestSuite(const std::wstring& src_file_path) : src_file_path(src_file_path) {
    name = src_file_path.substr(src_file_path.find_last_of(xe::path_separator) +
                                1);
    name = ReplaceExtension(name, L"");
    map_file_path = xe::to_wstring(FLAGS_test_bin_path) + name + L".map";
    bin_file_path = xe::to_wstring(FLAGS_test_bin_path) + name + L".bin";
  }

  bool Load() {
    if (!ReadMap(map_file_path)) {
      XELOGE("Unable to read map for test %ls", src_file_path.c_str());
      return false;
    }
    if (!ReadAnnotations(src_file_path)) {
      XELOGE("Unable to read annotations for test %ls", src_file_path.c_str());
      return false;
    }
    return true;
  }

  std::wstring name;
  std::wstring src_file_path;
  std::wstring map_file_path;
  std::wstring bin_file_path;
  std::vector<TestCase> test_cases;

 private:
  std::wstring ReplaceExtension(const std::wstring& path,
                                const std::wstring& new_extension) {
    std::wstring result = path;
    auto last_dot = result.find_last_of('.');
    result.replace(result.begin() + last_dot, result.end(), new_extension);
    return result;
  }

  TestCase* FindTestCase(const std::string& name) {
    for (auto& test_case : test_cases) {
      if (test_case.name == name) {
        return &test_case;
      }
    }
    return nullptr;
  }

  bool ReadMap(const std::wstring& map_file_path) {
    FILE* f = fopen(xe::to_string(map_file_path).c_str(), "r");
    if (!f) {
      return false;
    }
    char line_buffer[BUFSIZ];
    while (fgets(line_buffer, sizeof(line_buffer), f)) {
      if (!strlen(line_buffer)) {
        continue;
      }
      // 0000000000000000 t test_add1\n
      char* newline = strrchr(line_buffer, '\n');
      if (newline) {
        *newline = 0;
      }
      char* t_test_ = strstr(line_buffer, " t test_");
      if (!t_test_) {
        continue;
      }
      std::string address(line_buffer, t_test_ - line_buffer);
      std::string name(t_test_ + strlen(" t test_"));
      test_cases.emplace_back(START_ADDRESS + std::stoul(address, 0, 16), name);
    }
    fclose(f);
    return true;
  }

  bool ReadAnnotations(const std::wstring& src_file_path) {
    TestCase* current_test_case = nullptr;
    FILE* f = fopen(xe::to_string(src_file_path).c_str(), "r");
    if (!f) {
      return false;
    }
    char line_buffer[BUFSIZ];
    while (fgets(line_buffer, sizeof(line_buffer), f)) {
      if (!strlen(line_buffer)) {
        continue;
      }
      // Eat leading whitespace.
      char* start = line_buffer;
      while (*start == ' ') {
        ++start;
      }
      if (strncmp(start, "test_", strlen("test_")) == 0) {
        // Global test label.
        std::string label(start + strlen("test_"), strchr(start, ':'));
        current_test_case = FindTestCase(label);
        if (!current_test_case) {
          XELOGE("Test case %s not found in corresponding map for %ls",
                 label.c_str(), src_file_path.c_str());
          return false;
        }
      } else if (strlen(start) > 3 && start[0] == '#' && start[1] == '_') {
        // Annotation.
        // We don't actually verify anything here.
        char* next_space = strchr(start + 3, ' ');
        if (next_space) {
          // Looks legit.
          std::string key(start + 3, next_space);
          std::string value(next_space + 1);
          while (value.find_last_of(" \t\n") == value.size() - 1) {
            value.erase(value.end() - 1);
          }
          if (!current_test_case) {
            XELOGE("Annotation outside of test case in %ls",
                   src_file_path.c_str());
            return false;
          }
          current_test_case->annotations.emplace_back(key, value);
        }
      }
    }
    fclose(f);
    return true;
  }
};

class TestRunner {
 public:
  TestRunner() {
    memory_size = 64 * 1024 * 1024;
    memory.reset(new Memory());
    memory->Initialize();

    processor.reset(new Processor(memory.get(), nullptr, nullptr));
    processor->Setup();
  }

  ~TestRunner() {
    thread_state.reset();
    processor.reset();
    memory.reset();
  }

  bool Setup(TestSuite& suite) {
    // Load the binary module.
    auto module = std::make_unique<xe::cpu::RawModule>(processor.get());
    if (!module->LoadFile(START_ADDRESS, suite.bin_file_path)) {
      XELOGE("Unable to load test binary %ls", suite.bin_file_path.c_str());
      return false;
    }
    processor->AddModule(std::move(module));

    processor->backend()->CommitExecutableRange(START_ADDRESS,
                                                START_ADDRESS + 1024 * 1024);

    // Simulate a thread.
    uint32_t stack_size = 64 * 1024;
    uint32_t stack_address = START_ADDRESS - stack_size;
    uint32_t pcr_address = stack_address - 0x1000;
    thread_state.reset(new ThreadState(processor.get(), 0x100,
                                       ThreadStackType::kUserStack,
                                       stack_address, stack_size, pcr_address));

    return true;
  }

  bool Run(TestCase& test_case) {
    // Setup test state from annotations.
    if (!SetupTestState(test_case)) {
      XELOGE("Test setup failed");
      return false;
    }

    // Execute test.
    xe::cpu::Function* fn = nullptr;
    if (!processor->ResolveFunction(test_case.address, &fn)) {
      XELOGE("Entry function not found");
      return false;
    }

    auto ctx = thread_state->context();
    ctx->lr = 0xBCBCBCBC;
    fn->Call(thread_state.get(), uint32_t(ctx->lr));

    // Assert test state expectations.
    bool result = CheckTestResults(test_case);

    return result;
  }

  bool SetupTestState(TestCase& test_case) {
    auto ppc_state = thread_state->context();
    for (auto& it : test_case.annotations) {
      if (it.first == "REGISTER_IN") {
        size_t space_pos = it.second.find(" ");
        auto reg_name = it.second.substr(0, space_pos);
        auto reg_value = it.second.substr(space_pos + 1);
        ppc_state->SetRegFromString(reg_name.c_str(), reg_value.c_str());
      } else if (it.first == "MEMORY_IN") {
        size_t space_pos = it.second.find(" ");
        auto address_str = it.second.substr(0, space_pos);
        auto bytes_str = it.second.substr(space_pos + 1);
        uint32_t address = std::strtoul(address_str.c_str(), nullptr, 16);
        auto p = memory->TranslateVirtual(address);
        const char* c = bytes_str.c_str();
        while (*c) {
          while (*c == ' ') ++c;
          if (!*c) {
            break;
          }
          char ccs[3] = {c[0], c[1], 0};
          c += 2;
          uint32_t b = std::strtoul(ccs, nullptr, 16);
          *p = static_cast<uint8_t>(b);
          ++p;
        }
      }
    }
    return true;
  }

  bool CheckTestResults(TestCase& test_case) {
    auto ppc_state = thread_state->context();

    char actual_value[2048];

    bool any_failed = false;
    for (auto& it : test_case.annotations) {
      if (it.first == "REGISTER_OUT") {
        size_t space_pos = it.second.find(" ");
        auto reg_name = it.second.substr(0, space_pos);
        auto reg_value = it.second.substr(space_pos + 1);
        if (!ppc_state->CompareRegWithString(reg_name.c_str(),
                                             reg_value.c_str(), actual_value,
                                             xe::countof(actual_value))) {
          any_failed = true;
          printf("Register %s assert failed:\n", reg_name.c_str());
          printf("  Expected: %s == %s\n", reg_name.c_str(), reg_value.c_str());
          printf("    Actual: %s == %s\n", reg_name.c_str(), actual_value);
        }
      } else if (it.first == "MEMORY_OUT") {
        size_t space_pos = it.second.find(" ");
        auto address_str = it.second.substr(0, space_pos);
        auto bytes_str = it.second.substr(space_pos + 1);
        uint32_t address = std::strtoul(address_str.c_str(), nullptr, 16);
        auto base_address = memory->TranslateVirtual(address);
        auto p = base_address;
        const char* c = bytes_str.c_str();
        while (*c) {
          while (*c == ' ') ++c;
          if (!*c) {
            break;
          }
          char ccs[3] = {c[0], c[1], 0};
          c += 2;
          uint32_t current_address =
              address + static_cast<uint32_t>(p - base_address);
          uint32_t expected = std::strtoul(ccs, nullptr, 16);
          uint8_t actual = *p;
          if (expected != actual) {
            any_failed = true;
            printf("Memory %s assert failed:\n", address_str.c_str());
            printf("  Expected: %.8X %.2X\n", current_address, expected);
            printf("    Actual: %.8X %.2X\n", current_address, actual);
          }
          ++p;
        }
      }
    }
    return !any_failed;
  }

  size_t memory_size;
  std::unique_ptr<Memory> memory;
  std::unique_ptr<Processor> processor;
  std::unique_ptr<ThreadState> thread_state;
};

bool DiscoverTests(std::wstring& test_path,
                   std::vector<std::wstring>& test_files) {
// TODO(benvanik): use PAL instead of this.
#if XE_PLATFORM_WIN32
  std::wstring search_path = test_path;
  search_path.append(L"\\*.s");
  WIN32_FIND_DATA ffd;
  HANDLE hFind = FindFirstFile(search_path.c_str(), &ffd);
  if (hFind == INVALID_HANDLE_VALUE) {
    XELOGE("Unable to find test path %ls", test_path.c_str());
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
    XELOGE("Unable to find test path %ls", test_path.c_str());
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

#ifdef _MSC_VER
int filter(unsigned int code) {
  if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void ProtectedRunTest(TestSuite& test_suite, TestRunner& runner,
                      TestCase& test_case, int& failed_count,
                      int& passed_count) {
#ifdef _MSC_VER
  __try {
#endif

    if (!runner.Setup(test_suite)) {
      XELOGE("    TEST FAILED SETUP");
      ++failed_count;
    }
    if (runner.Run(test_case)) {
      ++passed_count;
    } else {
      XELOGE("    TEST FAILED");
      ++failed_count;
    }

#ifdef _MSC_VER
  }
  __except(filter(GetExceptionCode())) {
    XELOGE("    TEST FAILED (UNSUPPORTED INSTRUCTION)");
    ++failed_count;
  }
#endif
}

bool RunTests(const std::wstring& test_name) {
  int result_code = 1;
  int failed_count = 0;
  int passed_count = 0;

  auto test_path_root =
      xe::fix_path_separators(xe::to_wstring(FLAGS_test_path));
  std::vector<std::wstring> test_files;
  if (!DiscoverTests(test_path_root, test_files)) {
    return false;
  }
  if (!test_files.size()) {
    XELOGE("No tests discovered - invalid path?");
    return false;
  }
  XELOGI("%d tests discovered.", (int)test_files.size());
  XELOGI("");

  std::vector<TestSuite> test_suites;
  bool load_failed = false;
  for (auto& test_path : test_files) {
    TestSuite test_suite(test_path);
    if (!test_name.empty() && test_suite.name != test_name) {
      continue;
    }
    if (!test_suite.Load()) {
      XELOGE("TEST SUITE %ls FAILED TO LOAD", test_path.c_str());
      load_failed = true;
      continue;
    }
    test_suites.push_back(std::move(test_suite));
  }
  if (load_failed) {
    return false;
  }

  for (auto& test_suite : test_suites) {
    XELOGI("%ls.s:", test_suite.name.c_str());

    for (auto& test_case : test_suite.test_cases) {
      XELOGI("  - %s", test_case.name.c_str());
      TestRunner runner;
      ProtectedRunTest(test_suite, runner, test_case, failed_count,
                       passed_count);
    }

    XELOGI("");
  }

  XELOGI("");
  XELOGI("Total tests: %d", failed_count + passed_count);
  XELOGI("Passed: %d", passed_count);
  XELOGI("Failed: %d", failed_count);

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
}  // namespace cpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xe-cpu-ppc-test", L"xe-cpu-ppc-test [test name]",
                   xe::cpu::test::main);
