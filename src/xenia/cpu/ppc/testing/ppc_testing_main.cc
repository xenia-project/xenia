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
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/raw_module.h"

#if XE_ARCH_AMD64
#include "xenia/cpu/backend/x64/x64_backend.h"
#elif XE_ARCH_ARM64
#include "xenia/cpu/backend/a64/a64_backend.h"
#endif  // XE_ARCH

#if XE_COMPILER_MSVC
#include "xenia/base/platform_win.h"
#endif  // XE_COMPILER_MSVC

DEFINE_path(test_path, "src/xenia/cpu/ppc/testing/",
            "Directory scanned for test files.", "Other");
DEFINE_path(test_bin_path, "src/xenia/cpu/ppc/testing/bin/",
            "Directory with binary outputs of the test files.", "Other");
DEFINE_transient_string(test_name, "", "Test suite name.", "General");

namespace xe {
namespace cpu {
namespace test {

using xe::cpu::ppc::PPCContext;
using namespace xe::literals;

typedef std::vector<std::pair<std::string, std::string>> AnnotationList;

const uint32_t START_ADDRESS = 0x80000000;

struct TestCase {
  TestCase(uint32_t address, const std::string& name)
      : address(address), name(name) {}
  uint32_t address;
  std::string name;
  AnnotationList annotations;
};

class TestSuite {
 public:
  TestSuite(const std::filesystem::path& src_file_path)
      : src_file_path_(src_file_path) {
    auto name = src_file_path.filename();
    name = name.replace_extension();

    name_ = xe::path_to_utf8(name);
    map_file_path_ = cvars::test_bin_path / name.replace_extension(".map");
    bin_file_path_ = cvars::test_bin_path / name.replace_extension(".bin");
  }
    
    // Move constructor
    TestSuite(TestSuite&& other) noexcept
        : name_(std::move(other.name_)),
          src_file_path_(std::move(other.src_file_path_)),
          map_file_path_(std::move(other.map_file_path_)),
          bin_file_path_(std::move(other.bin_file_path_)),
          test_cases_(std::move(other.test_cases_)) {}

    // Move assignment operator
    TestSuite& operator=(TestSuite&& other) noexcept {
      if (this != &other) {
        name_ = std::move(other.name_);
        src_file_path_ = std::move(other.src_file_path_);
        map_file_path_ = std::move(other.map_file_path_);
        bin_file_path_ = std::move(other.bin_file_path_);
        test_cases_ = std::move(other.test_cases_);
      }
      return *this;
    }

    // Delete copy constructor and copy assignment operator
    TestSuite(const TestSuite&) = delete;
    TestSuite& operator=(const TestSuite&) = delete;

  bool Load() {
    if (!ReadMap()) {
      XELOGE("Unable to read map for test {}",
             xe::path_to_utf8(src_file_path_));
      return false;
    }
    if (!ReadAnnotations()) {
      XELOGE("Unable to read annotations for test {}",
             xe::path_to_utf8(src_file_path_));
      return false;
    }
    return true;
  }

  const std::string& name() const { return name_; }
  const std::filesystem::path& src_file_path() const { return src_file_path_; }
  const std::filesystem::path& map_file_path() const { return map_file_path_; }
  const std::filesystem::path& bin_file_path() const { return bin_file_path_; }
  std::vector<TestCase>& test_cases() { return test_cases_; }

 private:
  std::string name_;
  std::filesystem::path src_file_path_;
  std::filesystem::path map_file_path_;
  std::filesystem::path bin_file_path_;
  std::vector<TestCase> test_cases_;

  TestCase* FindTestCase(const std::string_view name) {
    for (auto& test_case : test_cases_) {
      if (test_case.name == name) {
        return &test_case;
      }
    }
    return nullptr;
  }

    bool ReadMap() {
      FILE* f = filesystem::OpenFile(map_file_path_, "r");
      if (!f) {
        return false;
      }
      char line_buffer[BUFSIZ];
      while (fgets(line_buffer, sizeof(line_buffer), f)) {
        if (!strlen(line_buffer)) {
          continue;
        }
        // Remove newline character.
        char* newline = strrchr(line_buffer, '\n');
        if (newline) {
          *newline = 0;
        }
        char* t_test_ = strstr(line_buffer, " t test_");
        if (!t_test_) {
          continue;
        }
        // Extract address.
        size_t address_length = static_cast<size_t>(t_test_ - line_buffer);
        std::string address(line_buffer, address_length);
        // Extract name.
        const char* name_start = t_test_ + strlen(" t test_");
        std::string name(name_start);
        test_cases_.emplace_back(START_ADDRESS + std::stoul(address, nullptr, 16),
                                 name);
      }
      fclose(f);
      return true;
    }

    bool ReadAnnotations() {
      TestCase* current_test_case = nullptr;
      FILE* f = filesystem::OpenFile(src_file_path_, "r");
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
          const char* label_start = start + strlen("test_");
          char* label_end = strchr(start, ':');
          if (!label_end) {
            XELOGE("Malformed test label in {}", xe::path_to_utf8(src_file_path_));
            fclose(f);
            return false;
          }
          size_t label_length = static_cast<size_t>(label_end - label_start);
          std::string label(label_start, label_length);
          current_test_case = FindTestCase(label);
          if (!current_test_case) {
            XELOGE("Test case {} not found in corresponding map for {}", label,
                   xe::path_to_utf8(src_file_path_));
            fclose(f);
            return false;
          }
        } else if (strlen(start) > 3 && start[0] == '#' && start[1] == '_') {
          // Annotation.
          char* next_space = strchr(start + 3, ' ');
          if (next_space) {
            // Extract key and value.
            const char* key_start = start + 3;
            size_t key_length = static_cast<size_t>(next_space - key_start);
            std::string key(key_start, key_length);
            const char* value_start = next_space + 1;
            std::string value(value_start);
            // Trim trailing whitespace.
            value.erase(value.find_last_not_of(" \t\n\r") + 1);
            if (!current_test_case) {
              XELOGE("Annotation outside of test case in {}",
                     xe::path_to_utf8(src_file_path_));
              fclose(f);
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
  TestRunner() : memory_size_(64_MiB) {
    memory_.reset(new Memory());
    memory_->Initialize();
  }

  ~TestRunner() {
    thread_state_.reset();
    processor_.reset();
    memory_.reset();
  }

  bool Setup(TestSuite& suite) {
    // Reset memory.
    memory_->Reset();

    std::unique_ptr<xe::cpu::backend::Backend> backend;
    if (!backend) {
#if XE_ARCH_AMD64
      if (cvars::cpu == "x64") {
        backend.reset(new xe::cpu::backend::x64::X64Backend());
      }
#elif XE_ARCH_ARM64
      if (cvars::cpu == "a64") {
        backend.reset(new xe::cpu::backend::a64::A64Backend());
      }
#endif  // XE_ARCH
      if (cvars::cpu == "any") {
        if (!backend) {
#if XE_ARCH_AMD64
          backend.reset(new xe::cpu::backend::x64::X64Backend());
#elif XE_ARCH_ARM64
          backend.reset(new xe::cpu::backend::a64::A64Backend());
#endif  // XE_ARCH
        }
      }
    }

    // Setup a fresh processor.
    processor_.reset(new Processor(memory_.get(), nullptr));
    processor_->Setup(std::move(backend));
    processor_->set_debug_info_flags(DebugInfoFlags::kDebugInfoAll);

    // Load the binary module.
    auto module = std::make_unique<xe::cpu::RawModule>(processor_.get());
    if (!module->LoadFile(START_ADDRESS, suite.bin_file_path())) {
      XELOGE("Unable to load test binary {}",
             xe::path_to_utf8(suite.bin_file_path()));
      return false;
    }
    processor_->AddModule(std::move(module));

    processor_->backend()->CommitExecutableRange(START_ADDRESS,
                                                 START_ADDRESS + 1024 * 1024);

    // Add dummy space for memory.
    processor_->memory()->LookupHeap(0)->AllocFixed(
        0x10001000, 0xEFFF, 0,
        kMemoryAllocationReserve | kMemoryAllocationCommit,
        kMemoryProtectRead | kMemoryProtectWrite);

    // Simulate a thread.
    uint32_t stack_size = 64 * 1024;
    uint32_t stack_address = START_ADDRESS - stack_size;
    uint32_t pcr_address = stack_address - 0x1000;
    thread_state_.reset(
        new ThreadState(processor_.get(), 0x100, stack_address, pcr_address));

    return true;
  }

  bool Run(TestCase& test_case) {
    // Setup test state from annotations.
    if (!SetupTestState(test_case)) {
      XELOGE("Test setup failed");
      return false;
    }

    // Execute test.
    auto fn = processor_->ResolveFunction(test_case.address);
    if (!fn) {
      XELOGE("Entry function not found");
      return false;
    }

    auto ctx = thread_state_->context();
    ctx->lr = 0xBCBCBCBC;
    fn->Call(thread_state_.get(), uint32_t(ctx->lr));

    // Assert test state expectations.
    bool result = CheckTestResults(test_case);
    if (!result) {
      // Also dump all disasm/etc.
      if (fn->is_guest()) {
        static_cast<xe::cpu::GuestFunction*>(fn)->debug_info()->Dump();
      }
    }

    return result;
  }

  bool SetupTestState(TestCase& test_case) {
    auto ppc_context = thread_state_->context();
    for (auto& it : test_case.annotations) {
      if (it.first == "REGISTER_IN") {
        size_t space_pos = it.second.find(" ");
        auto reg_name = it.second.substr(0, space_pos);
        auto reg_value = it.second.substr(space_pos + 1);
        ppc_context->SetRegFromString(reg_name.c_str(), reg_value.c_str());
      } else if (it.first == "MEMORY_IN") {
        size_t space_pos = it.second.find(" ");
        auto address_str = it.second.substr(0, space_pos);
        auto bytes_str = it.second.substr(space_pos + 1);
        uint32_t address = std::strtoul(address_str.c_str(), nullptr, 16);
        auto p = memory_->TranslateVirtual(address);
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
    auto ppc_context = thread_state_->context();

    bool any_failed = false;
    for (auto& it : test_case.annotations) {
      if (it.first == "REGISTER_OUT") {
        size_t space_pos = it.second.find(" ");
        auto reg_name = it.second.substr(0, space_pos);
        auto reg_value = it.second.substr(space_pos + 1);
        std::string actual_value;
        if (!ppc_context->CompareRegWithString(
                reg_name.c_str(), reg_value.c_str(), actual_value)) {
          any_failed = true;
          XELOGE("Register {} assert failed:\n", reg_name);
          XELOGE("  Expected: {} == {}\n", reg_name, reg_value);
          XELOGE("    Actual: {} == {}\n", reg_name, actual_value);
        }
      } else if (it.first == "MEMORY_OUT") {
        size_t space_pos = it.second.find(" ");
        auto address_str = it.second.substr(0, space_pos);
        auto bytes_str = it.second.substr(space_pos + 1);
        uint32_t address = std::strtoul(address_str.c_str(), nullptr, 16);
        auto base_address = memory_->TranslateVirtual(address);
        auto p = base_address;
        const char* c = bytes_str.c_str();
        bool failed = false;
        size_t count = 0;
        StringBuffer expecteds;
        StringBuffer actuals;
        while (*c) {
          while (*c == ' ') ++c;
          if (!*c) {
            break;
          }
          char ccs[3] = {c[0], c[1], 0};
          c += 2;
          count++;
          uint32_t current_address =
              address + static_cast<uint32_t>(p - base_address);
          uint32_t expected = std::strtoul(ccs, nullptr, 16);
          uint8_t actual = *p;

          expecteds.AppendFormat(" {:02X}", expected);
          actuals.AppendFormat(" {:02X}", actual);

          if (expected != actual) {
            any_failed = true;
            failed = true;
          }
          ++p;
        }
        if (failed) {
          XELOGE("Memory {} assert failed:\n", address_str);
          XELOGE("  Expected:{}\n", expecteds.to_string());
          XELOGE("    Actual:{}\n", actuals.to_string());
        }
      }
    }
    return !any_failed;
  }

  size_t memory_size_;
  std::unique_ptr<Memory> memory_;
  std::unique_ptr<Processor> processor_;
  std::unique_ptr<ThreadState> thread_state_;
};

bool DiscoverTests(const std::filesystem::path& test_path,
                   std::vector<std::filesystem::path>& test_files) {
  auto file_infos = xe::filesystem::ListFiles(test_path);
  for (auto& file_info : file_infos) {
    if (file_info.name.extension() == ".s") {
      test_files.push_back(test_path / file_info.name);
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

bool RunTests(const std::string_view test_name) {
    int result_code = 1;
    int failed_count = 0;
    int passed_count = 0;

#if XE_ARCH_AMD64
    XELOGI("Instruction feature mask {}.", cvars::x64_extension_mask);
#endif  // XE_ARCH_AMD64

    auto test_path_root = cvars::test_path;
    std::cout << std::filesystem::current_path() << std::endl;
    std::vector<std::filesystem::path> test_files;
    if (!DiscoverTests(test_path_root, test_files)) {
        return false;
    }
    if (!test_files.size()) {
        XELOGE("No tests discovered - invalid path?");
        return false;
    }
    XELOGI("{} tests discovered.", test_files.size());
    XELOGI("");

    std::vector<std::unique_ptr<TestSuite>> test_suites;
    bool load_failed = false;
    for (auto& test_path : test_files) {
        auto test_suite = std::make_unique<TestSuite>(test_path);
        if (!test_name.empty() && test_suite->name() != test_name) {
            continue;
        }
        if (!test_suite->Load()) {
            XELOGE("TEST SUITE {} FAILED TO LOAD", xe::path_to_utf8(test_path));
            load_failed = true;
            continue;
        }
        test_suites.push_back(std::move(test_suite));
    }
    if (load_failed) {
        XELOGE("One or more test suites failed to load.");
    }

    XELOGI("{} tests loaded.", test_suites.size());
    TestRunner runner;
    for (auto& test_suite_ptr : test_suites) {
        auto& test_suite = *test_suite_ptr;
        XELOGI("{}.s:", test_suite.name());

        for (auto& test_case : test_suite.test_cases()) {
            XELOGI("  - {}", test_case.name);
            ProtectedRunTest(test_suite, runner, test_case, failed_count,
                             passed_count);
        }

        XELOGI("");
    }

    XELOGI("");
    XELOGI("Total tests: {}", failed_count + passed_count);
    XELOGI("Passed: {}", passed_count);
    XELOGI("Failed: {}", failed_count);

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
