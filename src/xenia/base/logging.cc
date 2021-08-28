/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include <atomic>
#include <mutex>
#include <vector>

#include "third_party/disruptorplus/include/disruptorplus/multi_threaded_claim_strategy.hpp"
#include "third_party/disruptorplus/include/disruptorplus/ring_buffer.hpp"
#include "third_party/disruptorplus/include/disruptorplus/sequence_barrier.hpp"
#include "third_party/disruptorplus/include/disruptorplus/spin_wait_strategy.hpp"
#include "xenia/base/atomic.h"
#include "xenia/base/console.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"
#include "xenia/base/threading.h"

// For MessageBox:
// TODO(benvanik): generic API? logging_win.cc?
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

#include "third_party/fmt/include/fmt/format.h"

DEFINE_path(log_file, "", "Logs are written to the given file", "Logging");
DEFINE_bool(log_to_stdout, true, "Write log output to stdout", "Logging");
DEFINE_bool(log_to_debugprint, false, "Dump the log to DebugPrint.", "Logging");
DEFINE_bool(flush_log, true, "Flush log file after each log line batch.",
            "Logging");
DEFINE_int32(
    log_level, 2,
    "Maximum level to be logged. (0=error, 1=warning, 2=info, 3=debug)",
    "Logging");

namespace dp = disruptorplus;

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

thread_local char thread_log_buffer_[64 * 1024];

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

class Logger {
 public:
  explicit Logger(const std::string_view app_name)
      : wait_strategy_(),
        claim_strategy_(kBlockCount, wait_strategy_),
        consumed_(wait_strategy_) {
    claim_strategy_.add_claim_barrier(consumed_);

    write_thread_ =
        xe::threading::Thread::Create({}, [this]() { WriteThread(); });
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
  static const size_t kBufferSize = 8 * 1024 * 1024;
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
    if (cvars::log_to_debugprint) {
      debugging::DebugPrint("{}", std::string_view(buf, size));
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

      auto available_sequence = claim_strategy_.wait_until_published(
          next_range.last(), last_sequence);

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

  FILE* log_file = nullptr;

  if (cvars::log_file.empty()) {
    // Default to app name.
    auto file_name = fmt::format("{}.log", app_name);
    auto file_path = std::filesystem::path(file_name);
    xe::filesystem::CreateParentFolder(file_path);

    log_file = xe::filesystem::OpenFile(file_path, "wt");
  } else {
    xe::filesystem::CreateParentFolder(cvars::log_file);
    log_file = xe::filesystem::OpenFile(cvars::log_file, "wt");
  }
  auto sink = std::make_unique<FileLogSink>(log_file);
  logger_->AddLogSink(std::move(sink));

  if (cvars::log_to_stdout) {
    auto stdout_sink = std::make_unique<FileLogSink>(stdout);
    logger_->AddLogSink(std::move(stdout_sink));
  }
}

void ShutdownLogging() {
  Logger* logger = logger_;
  logger_ = nullptr;

  logger->~Logger();
  memory::AlignedFree(logger);
}

bool logging::internal::ShouldLog(LogLevel log_level) {
  return logger_ != nullptr &&
         static_cast<int32_t>(log_level) <= cvars::log_level;
}

std::pair<char*, size_t> logging::internal::GetThreadBuffer() {
  return {thread_log_buffer_, sizeof(thread_log_buffer_)};
}

void logging::internal::AppendLogLine(LogLevel log_level,
                                      const char prefix_char, size_t written) {
  if (!ShouldLog(log_level) || !written) {
    return;
  }
  logger_->AppendLine(xe::threading::current_thread_id(), prefix_char,
                      thread_log_buffer_, written);
}

void logging::AppendLogLine(LogLevel log_level, const char prefix_char,
                            const std::string_view str) {
  if (!internal::ShouldLog(log_level) || !str.size()) {
    return;
  }
  logger_->AppendLine(xe::threading::current_thread_id(), prefix_char,
                      str.data(), str.size());
}

void FatalError(const std::string_view str) {
  logging::AppendLogLine(LogLevel::Error, 'X', str);

  if (!xe::has_console_attached()) {
    ShowSimpleMessageBox(SimpleMessageBoxType::Error, str);
  }

  ShutdownLogging();
  std::exit(1);
}

}  // namespace xe
