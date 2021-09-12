/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/console_app_main.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/string_util.h"

#if XE_COMPILER_MSVC
#include "xenia/base/platform_win.h"
#endif  // XE_COMPILER_MSVC

DEFINE_string(test_path, "src/xenia/cpu/ppc/testing/",
              "Directory scanned for test files.");
DEFINE_string(test_bin_path, "src/xenia/cpu/ppc/testing/bin/",
              "Directory with binary outputs of the test files.");
DEFINE_transient_string(test_name, "", "Test suite name.", "General");

extern "C" void xe_call_native(void* context, void* fn);

namespace xe {
namespace cpu {
namespace test {

struct Context {
  uint64_t r[32];  // 0x000
  double f[32];    // 0x100
  vec128_t v[32];  // 0x200 For now, only support 32 vector registers.
  uint32_t cr;     // 0x400 Condition register
  uint64_t fpscr;  // 0x404 FPSCR
};

typedef std::vector<std::pair<std::string, std::string>> AnnotationList;

const uint32_t START_ADDRESS = 0x00000000;

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
    name = src_file_path.substr(src_file_path.find_last_of(xe::kPathSeparator) +
                                1);
    name = ReplaceExtension(name, L"");
    map_file_path = xe::to_wstring(cvars::test_bin_path) + name + L".map";
    bin_file_path = xe::to_wstring(cvars::test_bin_path) + name + L".bin";
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

  TestCase* FindTestCase(const std::string_view name) {
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
    memory_size_ = 64 * 1024 * 1024;
    // FIXME(Triang3l): If this is ever compiled for a platform without
    // xe::memory::IsWritableExecutableMemorySupported, two memory mappings must
    // be used.
    memory_ = memory::AllocFixed(nullptr, memory_size_,
                                 memory::AllocationType::kReserveCommit,
                                 memory::PageAccess::kExecuteReadWrite);

    context_ = memory::AlignedAlloc<Context>(32);
    std::memset(context_, 0, sizeof(Context));
  }

  ~TestRunner() {
    memory::DeallocFixed(memory_, memory_size_,
                         memory::DeallocationType::kRelease);

    memory::AlignedFree(context_);
  }

  bool Setup(TestSuite& suite) {
    FILE* file = filesystem::OpenFile(suite.bin_file_path, "rb");
    if (!file) {
      XELOGE("Failed to open file %ls!", suite.bin_file_path.c_str());
      return false;
    }

    fseek(file, 0, SEEK_END);
    uint32_t file_length = static_cast<uint32_t>(ftell(file));
    fseek(file, 0, SEEK_SET);

    if (file_length > memory_size_) {
      XELOGE("Bin file %ls is too big!", suite.bin_file_path.c_str());
      return false;
    }

    // Read entire file into our memory.
    fread(memory_, file_length, 1, file);
    fclose(file);

    // Zero out the context
    std::memset(context_, 0, sizeof(Context));
    return true;
  }

  bool Run(TestCase& test_case) {
    // Setup test state from annotations.
    if (!SetupTestState(test_case)) {
      XELOGE("Test setup failed");
      return false;
    }

    // Execute test.
    xe_call_native(reinterpret_cast<void*>(context_),
                   reinterpret_cast<uint8_t*>(memory_) + test_case.address);
    return CheckTestResults(test_case);
  }

  bool CompareRegWithString(const char* name, const char* value,
                            char* out_value, size_t out_value_size,
                            Context* ctx) const {
    int n;
    if (sscanf(name, "r%d", &n) == 1) {
      uint64_t expected = string_util::from_string<uint64_t>(value);
      if (ctx->r[n] != expected) {
        std::snprintf(out_value, out_value_size, "%016" PRIX64, ctx->r[n]);
        return false;
      }
    } else if (sscanf(name, "f%d", &n) == 1) {
      if (std::strstr(value, "0x")) {
        // Special case: Treat float as integer.
        uint64_t expected = string_util::from_string<uint64_t>(value, true);

        union {
          double f;
          uint64_t u;
        } f2u;
        f2u.f = ctx->f[n];

        if (f2u.u != expected) {
          std::snprintf(out_value, out_value_size, "%016" PRIX64, f2u.u);
          return false;
        }
      } else {
        double expected = string_util::from_string<double>(value);

        // TODO(benvanik): epsilon
        if (ctx->f[n] != expected) {
          std::snprintf(out_value, out_value_size, "%f", ctx->f[n]);
          return false;
        }
      }
    } else if (sscanf(name, "v%d", &n) == 1) {
      vec128_t expected = string_util::from_string<vec128_t>(value);
      if (ctx->v[n] != expected) {
        std::snprintf(out_value, out_value_size, "[%.8X, %.8X, %.8X, %.8X]",
                      ctx->v[n].i32[0], ctx->v[n].i32[1], ctx->v[n].i32[2],
                      ctx->v[n].i32[3]);
        return false;
      }
    } else if (std::strcmp(name, "cr") == 0) {
      uint64_t actual = ctx->cr;
      uint64_t expected = string_util::from_string<uint64_t>(value);
      if (actual != expected) {
        std::snprintf(out_value, out_value_size, "%016" PRIX64, actual);
        return false;
      }
    } else {
      assert_always("Unrecognized register name: %s\n", name);
      return false;
    }

    return true;
  }

  bool SetRegFromString(const char* name, const char* value, Context* ctx) {
    int n;
    if (sscanf(name, "r%d", &n) == 1) {
      ctx->r[n] = string_util::from_string<uint64_t>(value);
    } else if (sscanf(name, "f%d", &n) == 1) {
      ctx->f[n] = string_util::from_string<double>(value);
    } else if (sscanf(name, "v%d", &n) == 1) {
      ctx->v[n] = string_util::from_string<vec128_t>(value);
    } else if (std::strcmp(name, "cr") == 0) {
      ctx->cr = uint32_t(string_util::from_string<uint64_t>(value));
    } else {
      printf("Unrecognized register name: %s\n", name);
      return false;
    }

    return true;
  }

  bool SetupTestState(TestCase& test_case) {
    for (auto& it : test_case.annotations) {
      if (it.first == "REGISTER_IN") {
        size_t space_pos = it.second.find(" ");
        auto reg_name = it.second.substr(0, space_pos);
        auto reg_value = it.second.substr(space_pos + 1);
        if (!SetRegFromString(reg_name.c_str(), reg_value.c_str(), context_)) {
          return false;
        }
      } else if (it.first == "MEMORY_IN") {
        XELOGW("Warning: MEMORY_IN unimplemented");
        return false;
        /*
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
        */
      }
    }
    return true;
  }

  bool CheckTestResults(TestCase& test_case) {
    char actual_value[2048];

    bool any_failed = false;
    for (auto& it : test_case.annotations) {
      if (it.first == "REGISTER_OUT") {
        size_t space_pos = it.second.find(" ");
        auto reg_name = it.second.substr(0, space_pos);
        auto reg_value = it.second.substr(space_pos + 1);
        if (!CompareRegWithString(reg_name.c_str(), reg_value.c_str(),
                                  actual_value, xe::countof(actual_value),
                                  context_)) {
          any_failed = true;
          XELOGE("Register %s assert failed:\n", reg_name.c_str());
          XELOGE("  Expected: %s == %s\n", reg_name.c_str(), reg_value.c_str());
          XELOGE("    Actual: %s == %s\n", reg_name.c_str(), actual_value);
        }
      } else if (it.first == "MEMORY_OUT") {
        XELOGW("Warning: MEMORY_OUT unimplemented");
        any_failed = true;
        /*
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
            XELOGE("Memory %s assert failed:\n", address_str.c_str());
            XELOGE("  Expected: %.8X %.2X\n", current_address, expected);
            XELOGE("    Actual: %.8X %.2X\n", current_address, actual);
          }
          ++p;
        }
        */
      }
    }
    return !any_failed;
  }

  void* memory_;
  size_t memory_size_;
  Context* context_;
};

bool DiscoverTests(std::wstring& test_path,
                   std::vector<std::wstring>& test_files) {
  auto file_infos = xe::filesystem::ListFiles(test_path);
  for (auto& file_info : file_infos) {
    if (file_info.name != L"." && file_info.name != L".." &&
        file_info.name.rfind(L".s") == file_info.name.size() - 2) {
      test_files.push_back(xe::join_paths(test_path, file_info.name));
    }
  }
  return true;
}

#if XE_COMPILER_MSVC
int filter(unsigned int code) {
  if (code == EXCEPTION_ILLEGAL_INSTRUCTION) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif  // XE_COMPILER_MSVC

void ProtectedRunTest(TestSuite& test_suite, TestRunner& runner,
                      TestCase& test_case, int& failed_count,
                      int& passed_count) {
#if XE_COMPILER_MSVC
  __try {
#endif  // XE_COMPILER_MSVC

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

#if XE_COMPILER_MSVC
  } __except (filter(GetExceptionCode())) {
    XELOGE("    TEST FAILED (UNSUPPORTED INSTRUCTION)");
    ++failed_count;
  }
#endif  // XE_COMPILER_MSVC
}

bool RunTests(const std::wstring& test_name) {
  int result_code = 1;
  int failed_count = 0;
  int passed_count = 0;

  auto test_path_root =
      xe::fix_path_separators(xe::to_wstring(cvars::test_path));
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

  XELOGI("%d tests loaded successfully.", (int)test_suites.size());

  TestRunner runner;
  for (auto& test_suite : test_suites) {
    XELOGI("%ls.s:", test_suite.name.c_str());

    for (auto& test_case : test_suite.test_cases) {
      XELOGI("  - %s", test_case.name.c_str());
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

int main(const std::vector<std::string>& args) {
  return RunTests(cvars::test_name) ? 0 : 1;
}

}  // namespace test
}  // namespace cpu
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-cpu-ppc-test", xe::cpu::test::main, "[test name]",
                      "test_name");
