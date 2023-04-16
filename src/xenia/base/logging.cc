/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "third_party/disruptorplus/include/disruptorplus/multi_threaded_claim_strategy.hpp"
#include "third_party/disruptorplus/include/disruptorplus/ring_buffer.hpp"
#include "third_party/disruptorplus/include/disruptorplus/sequence_barrier.hpp"
#include "third_party/disruptorplus/include/disruptorplus/spin_wait_strategy.hpp"
#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/console.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/literals.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"
#include "xenia/base/threading.h"

// TODO(benvanik): generic API? logging_win.cc?
#if XE_PLATFORM_ANDROID
#include <android/log.h>
#elif XE_PLATFORM_WIN32
// For MessageBox:
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM

#include "third_party/fmt/include/fmt/format.h"

#if XE_PLATFORM_ANDROID
DEFINE_bool(log_to_logcat, true, "Write log output to Android Logcat.",
            "Logging");
#else
DEFINE_path(log_file, "", "Logs are written to the given file", "Logging");
DEFINE_bool(log_to_stdout, true, "Write log output to stdout", "Logging");
DEFINE_bool(log_to_debugprint, false, "Dump the log to DebugPrint.", "Logging");
#endif  // XE_PLATFORM_ANDROID
DEFINE_bool(flush_log, true, "Flush log file after each log line batch.",
            "Logging");

DEFINE_uint32(log_mask, 0,
              "Disables specific categorizes for more granular debug logging. "
              "Kernel = 1, Apu = 2, Cpu = 4.",
              "Logging");

DEFINE_int32(
    log_level, 2,
    "Maximum level to be logged. (0=error, 1=warning, 2=info, 3=debug)",
    "Logging");

namespace dp = disruptorplus;
using namespace xe::literals;

namespace xe {

class Logger;

Logger* logger_ = nullptr;

struct LogLine {
  size_t buffer_length;
  uint32_t thread_id;
  uint16_t _pad_0;  // (2b) padding
  bool terminate;
  char prefix_char;
};

thread_local char thread_log_buffer_[64_KiB];

FileLogSink::~FileLogSink() {
  if (file_) {
    fflush(file_);
    if (owns_file_) {
      fclose(file_);
    }
  }
}

void FileLogSink::Write(const char* buf, size_t size) {
  if (file_) {
    fwrite(buf, 1, size, file_);
  }
}

void FileLogSink::Flush() {
  if (file_) {
    fflush(file_);
  }
}

void DebugPrintLogSink::Write(const char* buf, size_t size) {
  debugging::DebugPrint("{}", std::string_view(buf, size));
}

#if XE_PLATFORM_ANDROID
class AndroidLogSink final : public LogSink {
 public:
  explicit AndroidLogSink(const std::string_view tag) : tag_(tag) {}

  void Write(const char* buf, size_t size) override;
  void Flush() override;

 private:
  // May be called with an empty line_buffer_ to write an empty log message (if
  // the original message contains a blank line as a separator, for instance).
  void WriteLineBuffer();

  const std::string tag_;
  android_LogPriority current_priority_ = ANDROID_LOG_INFO;
  bool is_line_continuation_ = false;
  size_t line_buffer_used_ = 0;
  // LOGGER_ENTRY_MAX_PAYLOAD is defined as 4076 on older Android versions and
  // as 4068 on newer ones. An attempt to write more than that amount to the
  // kernel logger will result in a truncated log entry. 4000 is the commonly
  // used limit considered safe.
  // However, "Log message text may be truncated to less than an
  // implementation-specific limit (1023 bytes)" - android/log.h.
  char line_buffer_[1023 + 1];
};

void AndroidLogSink::Write(const char* buf, size_t size) {
  // A null character, if appears, is fine - it will just truncate a single
  // line, but since every message ends with a newline, and WriteLineBuffer is
  // done after every line, it won't have lasting effects.
  // Using memchr and memcpy as they are vectorized in Bionic.
  while (size) {
    const void* newline = std::memchr(buf, '\n', size);
    size_t line_remaining =
        newline ? size_t(static_cast<const char*>(newline) - buf) : size;
    while (line_remaining) {
      assert_true(line_buffer_used_ < xe::countof(line_buffer_));
      size_t append_count =
          std::min(line_remaining,
                   xe::countof(line_buffer_) - size_t(1) - line_buffer_used_);
      std::memcpy(line_buffer_ + line_buffer_used_, buf, append_count);
      buf += append_count;
      size -= append_count;
      line_remaining -= append_count;
      line_buffer_used_ += append_count;
      if (line_buffer_used_ >= xe::countof(line_buffer_) - size_t(1)) {
        WriteLineBuffer();
        // The next WriteLineBuffer belongs to the same line.
        is_line_continuation_ = true;
      }
    }
    if (newline) {
      // If the end of the buffer was reached right before a newline character,
      // so line_buffer_used_ is 0, and is_line_continuation_ has been set, the
      // line break has already been done by Android itself - don't write an
      // empty message. However, if the message intentionally contains blank
      // lines, write them.
      if (line_buffer_used_ || !is_line_continuation_) {
        WriteLineBuffer();
      }
      is_line_continuation_ = false;
      ++buf;
      --size;
    }
  }
}

void AndroidLogSink::Flush() {
  if (line_buffer_used_) {
    WriteLineBuffer();
  }
}

void AndroidLogSink::WriteLineBuffer() {
  // If this is a new line, check if it's a new log message, and if it is,
  // update the priority based on the prefix.
  if (!is_line_continuation_ && line_buffer_used_ >= 3 &&
      line_buffer_[1] == '>' && line_buffer_[2] == ' ') {
    switch (line_buffer_[0]) {
      case 'd':
        current_priority_ = ANDROID_LOG_DEBUG;
        break;
      case 'w':
        current_priority_ = ANDROID_LOG_WARN;
        break;
      case '!':
        current_priority_ = ANDROID_LOG_ERROR;
        break;
      case 'x':
        current_priority_ = ANDROID_LOG_FATAL;
        break;
      default:
        current_priority_ = ANDROID_LOG_INFO;
        break;
    }
  }
  // Android skips blank lines, but if writing a blank line was requested
  // explicitly for formatting, write a non-newline character.
  if (!line_buffer_used_) {
    line_buffer_[line_buffer_used_++] = ' ';
  }
  // Terminate the text.
  assert_true(line_buffer_used_ < xe::countof(line_buffer_));
  line_buffer_[line_buffer_used_] = '\0';
  // Write.
  __android_log_write(current_priority_, tag_.c_str(), line_buffer_);
  line_buffer_used_ = 0;
}
#endif  // XE_PLATFORM_ANDROID

class Logger {
 public:
  explicit Logger(const std::string_view app_name)
      : wait_strategy_(),
        claim_strategy_(kBlockCount, wait_strategy_),
        consumed_(wait_strategy_) {
    claim_strategy_.add_claim_barrier(consumed_);

    write_thread_ =
        xe::threading::Thread::Create({}, [this]() { WriteThread(); });
    assert_not_null(write_thread_);
    write_thread_->set_name("Logging Writer");
  }

  ~Logger() {
    AppendLine(0, '\0', nullptr, 0, true);  // append a terminator
    xe::threading::Wait(write_thread_.get(), true);
  }

  void AddLogSink(std::unique_ptr<LogSink>&& sink) {
    sinks_.push_back(std::move(sink));
  }

 private:
  static const size_t kBufferSize = 8_MiB;
  uint8_t buffer_[kBufferSize];

  static const size_t kBlockSize = 256;
  static const size_t kBlockCount = kBufferSize / kBlockSize;
  static const size_t kBlockIndexMask = kBlockCount - 1;

  static const size_t kClaimStrategyFootprint =
      sizeof(std::atomic<dp::sequence_t>[kBlockCount]);

  static size_t BlockOffset(dp::sequence_t sequence) {
    return (sequence & kBlockIndexMask) * kBlockSize;
  }

  static size_t BlockCount(size_t byte_size) {
    return (byte_size + (kBlockSize - 1)) / kBlockSize;
  }

  dp::spin_wait_strategy wait_strategy_;
  dp::multi_threaded_claim_strategy<dp::spin_wait_strategy> claim_strategy_;
  dp::sequence_barrier<dp::spin_wait_strategy> consumed_;

  std::vector<std::unique_ptr<LogSink>> sinks_;

  std::unique_ptr<xe::threading::Thread> write_thread_;

  void Write(const char* buf, size_t size) {
    for (const auto& sink : sinks_) {
      sink->Write(buf, size);
    }
  }

  void WriteThread() {
    RingBuffer rb(buffer_, kBufferSize);

    size_t idle_loops = 0;

    dp::sequence_t next_sequence = 0;
    dp::sequence_t last_sequence = -1;

    size_t desired_count = 1;
    while (true) {
      // We want one block to find out how many blocks we need or we know how
      // many blocks needed for at least one log line.
      auto next_range = dp::sequence_range(next_sequence, desired_count);

      claim_strategy_.wait_until_published(next_range.last(), last_sequence);

      size_t read_count = 0;
      auto available_range = next_range;
      auto available_count = available_range.size();

      rb.set_write_offset(BlockOffset(available_range.end()));

      bool terminate = false;
      for (size_t i = available_range.first(); i != available_range.end();) {
        rb.set_read_offset(BlockOffset(i));

        LogLine line;
        rb.Read(&line, sizeof(line));

        auto needed_count = BlockCount(sizeof(LogLine) + line.buffer_length);
        if (read_count + needed_count > available_count) {
          // More blocks are needed for a complete line.
          desired_count = needed_count;
          break;
        } else {
          // Enough blocks to read this log line, advance by that many.
          read_count += needed_count;
          i += needed_count;

          if (line.prefix_char) {
            char prefix[] = {
                line.prefix_char,
                '>',
                ' ',
                '?',  // Thread ID gets placed here (8 chars).
                '?',
                '?',
                '?',
                '?',
                '?',
                '?',
                '?',
                ' ',
                0,
            };
            fmt::format_to_n(prefix + 3, sizeof(prefix) - 3, "{:08X}",
                             line.thread_id);
            Write(prefix, sizeof(prefix) - 1);
          }

          if (line.buffer_length) {
            // Get access to the line data - which may be split in the ring
            // buffer - and write it out in parts.
            auto line_range = rb.BeginRead(line.buffer_length);
            Write(reinterpret_cast<const char*>(line_range.first),
                  line_range.first_length);
            if (line_range.second_length) {
              Write(reinterpret_cast<const char*>(line_range.second),
                    line_range.second_length);
            }

            // Always ensure there is a newline.
            char last_char =
                line_range.second
                    ? line_range.second[line_range.second_length - 1]
                    : line_range.first[line_range.first_length - 1];
            if (last_char != '\n') {
              const char suffix[1] = {'\n'};
              Write(suffix, 1);
            }

            rb.EndRead(std::move(line_range));
          } else {
            // Always ensure there is a newline.
            const char suffix[1] = {'\n'};
            Write(suffix, 1);
          }

          if (line.terminate) {
            terminate = true;
            break;
          }
        }
      }

      if (terminate) {
        break;
      }

      if (read_count) {
        // Advance by the number of blocks we read.
        auto read_range = dp::sequence_range(next_sequence, read_count);
        next_sequence = read_range.end();
        last_sequence = read_range.last();
        consumed_.publish(last_sequence);

        desired_count = 1;

        if (cvars::flush_log) {
          for (const auto& sink : sinks_) {
            sink->Flush();
          }
        }

        idle_loops = 0;
      } else {
        if (idle_loops >= 1000) {
          // Introduce a waiting period.
          xe::threading::Sleep(std::chrono::milliseconds(50));
        } else {
          idle_loops++;
        }
      }
    }
  }

 public:
  void AppendLine(uint32_t thread_id, const char prefix_char,
                  const char* buffer_data, size_t buffer_length,
                  bool terminate = false) {
    size_t count = BlockCount(sizeof(LogLine) + buffer_length);

    auto range = claim_strategy_.claim(count);
    assert_true(range.size() == count);

    RingBuffer rb(buffer_, kBufferSize);
    rb.set_write_offset(BlockOffset(range.first()));
    rb.set_read_offset(BlockOffset(range.end()));

    LogLine line = {};
    line.buffer_length = buffer_length;
    line.thread_id = thread_id;
    line.prefix_char = prefix_char;
    line.terminate = terminate;

    rb.Write(&line, sizeof(LogLine));
    if (buffer_length) {
      rb.Write(buffer_data, buffer_length);
    }

    claim_strategy_.publish(range);
  }
};

void InitializeLogging(const std::string_view app_name) {
  auto mem = memory::AlignedAlloc<Logger>(0x10);
  logger_ = new (mem) Logger(app_name);

#if XE_PLATFORM_ANDROID
  // TODO(Triang3l): Enable file logging, but not by default as logs may be
  // huge.
  if (cvars::log_to_logcat) {
    logger_->AddLogSink(std::make_unique<AndroidLogSink>(app_name));
  }
#else
  FILE* log_file = nullptr;
  if (cvars::log_file.empty()) {
    // Default to app name.
    auto file_name = fmt::format("{}.log", app_name);
    auto file_path = xe::filesystem::GetExecutableFolder() / file_name;
    log_file = xe::filesystem::OpenFile(file_path, "wt");
  } else {
    xe::filesystem::CreateParentFolder(cvars::log_file);
    log_file = xe::filesystem::OpenFile(cvars::log_file, "wt");
  }
  logger_->AddLogSink(std::make_unique<FileLogSink>(log_file, true));

  if (cvars::log_to_stdout) {
    logger_->AddLogSink(std::make_unique<FileLogSink>(stdout, false));
  }

  if (cvars::log_to_debugprint) {
    logger_->AddLogSink(std::make_unique<DebugPrintLogSink>());
  }
#endif  // XE_PLATFORM_ANDROID
}

void ShutdownLogging() {
  Logger* logger = logger_;
  logger_ = nullptr;

  logger->~Logger();
  memory::AlignedFree(logger);
}

static int g_saved_loglevel = static_cast<int>(LogLevel::Disabled);
void logging::internal::ToggleLogLevel() {
  auto swap = g_saved_loglevel;

  g_saved_loglevel = cvars::log_level;
  cvars::log_level = swap;
}
bool logging::internal::ShouldLog(LogLevel log_level, uint32_t log_mask) {
  return static_cast<int32_t>(log_level) <= cvars::log_level &&
         (log_mask & cvars::log_mask) == 0;
}

std::pair<char*, size_t> logging::internal::GetThreadBuffer() {
  return {thread_log_buffer_, sizeof(thread_log_buffer_)};
}
XE_NOALIAS
void logging::internal::AppendLogLine(LogLevel log_level,
                                      const char prefix_char, size_t written) {
  if (!logger_ || !ShouldLog(log_level) || !written) {
    return;
  }
  logger_->AppendLine(xe::threading::current_thread_id(), prefix_char,
                      thread_log_buffer_, written);
}

void logging::AppendLogLine(LogLevel log_level, const char prefix_char,
                            const std::string_view str, uint32_t log_mask) {
  if (!internal::ShouldLog(log_level, log_mask) || !str.size()) {
    return;
  }
  logger_->AppendLine(xe::threading::current_thread_id(), prefix_char,
                      str.data(), str.size());
}

void FatalError(const std::string_view str) {
  logging::AppendLogLine(LogLevel::Error, 'x', str);

  if (!xe::has_console_attached()) {
    ShowSimpleMessageBox(SimpleMessageBoxType::Error, str);
  }

  ShutdownLogging();

#if XE_PLATFORM_ANDROID
  // Throw an error that can be reported to the developers via the store.
  std::abort();
#else
  std::exit(EXIT_FAILURE);
#endif  // XE_PLATFORM_ANDROID
}

}  // namespace xe
