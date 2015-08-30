/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include <gflags/gflags.h>

#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <mutex>
#include <vector>

#include "xenia/base/filesystem.h"
#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/threading.h"

// For MessageBox:
// TODO(benvanik): generic API? logging_win.cc?
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_string(log_file, "",
              "Logs are written to the given file instead of the default.");
DEFINE_bool(flush_log, true, "Flush log file after each log line batch.");

namespace xe {

class Logger;

Logger* logger_ = nullptr;
thread_local std::vector<char> log_format_buffer_(64 * 1024);

class Logger {
 public:
  Logger(const std::wstring& app_name)
      : ring_buffer_(buffer_, kBufferSize), running_(true) {
    if (!FLAGS_log_file.empty()) {
      auto file_path = xe::to_wstring(FLAGS_log_file.c_str());
      xe::filesystem::CreateParentFolder(file_path);
      file_ = xe::filesystem::OpenFile(file_path, "wt");
    } else {
      auto file_path = app_name + L".log";
      file_ = xe::filesystem::OpenFile(file_path, "wt");
    }

    flush_event_ = xe::threading::Event::CreateAutoResetEvent(false);
    write_thread_ =
        xe::threading::Thread::Create({}, [this]() { WriteThread(); });
    write_thread_->set_name("xe::FileLogSink Writer");
  }

  ~Logger() {
    running_ = false;
    flush_event_->Set();
    xe::threading::Wait(write_thread_.get(), true);
    fflush(file_);
    fclose(file_);
  }

  void AppendLine(uint32_t thread_id, const char level_char, const char* buffer,
                  size_t buffer_length) {
    LogLine line;
    line.thread_id = thread_id;
    line.level_char = level_char;
    line.buffer_length = buffer_length;
    while (true) {
      mutex_.lock();
      if (ring_buffer_.write_count() < sizeof(line) + buffer_length) {
        // Buffer is full. Stall.
        mutex_.unlock();
        xe::threading::MaybeYield();
        continue;
      }
      ring_buffer_.Write(&line, sizeof(LogLine));
      ring_buffer_.Write(buffer, buffer_length);
      mutex_.unlock();
      break;
    }
    flush_event_->Set();
  }

 private:
  static const size_t kBufferSize = 32 * 1024 * 1024;

  struct LogLine {
    uint32_t thread_id;
    char level_char;
    size_t buffer_length;
  };

  void WriteThread() {
    while (running_) {
      mutex_.lock();
      bool did_write = false;
      while (!ring_buffer_.empty()) {
        did_write = true;
        LogLine line;
        ring_buffer_.Read(&line, sizeof(line));
        ring_buffer_.Read(log_format_buffer_.data(), line.buffer_length);
        char prefix[] = {
            line.level_char,
            '>',
            ' ',
            '0',  // Thread ID gets placed here (8 chars).
            '0',
            '0',
            '0',
            '0',
            '0',
            '0',
            '0',
            ' ',
            0,
        };
        std::snprintf(prefix + 3, sizeof(prefix) - 3, "%08" PRIX32 " ",
                      line.thread_id);
        fwrite(prefix, 1, sizeof(prefix) - 1, file_);
        fwrite(log_format_buffer_.data(), 1, line.buffer_length, file_);
        if (log_format_buffer_[line.buffer_length - 1] != '\n') {
          const char suffix[1] = {'\n'};
          fwrite(suffix, 1, sizeof(suffix), file_);
        }
      }
      mutex_.unlock();
      if (did_write) {
        if (FLAGS_flush_log) {
          fflush(file_);
        }
      }
      xe::threading::Wait(flush_event_.get(), true);
    }
  }

  FILE* file_ = nullptr;
  uint8_t buffer_[kBufferSize];
  RingBuffer ring_buffer_;
  std::mutex mutex_;
  std::atomic<bool> running_;
  std::unique_ptr<xe::threading::Event> flush_event_;
  std::unique_ptr<xe::threading::Thread> write_thread_;
};

void InitializeLogging(const std::wstring& app_name) {
  // We leak this intentionally - lots of cleanup code needs it.
  logger_ = new Logger(app_name);
}

void LogLineFormat(const char level_char, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  size_t chars_written = vsnprintf(log_format_buffer_.data(),
                                   log_format_buffer_.capacity(), fmt, args);
  va_end(args);
  if (chars_written != std::string::npos) {
    logger_->AppendLine(xe::threading::current_thread_id(), level_char,
                        log_format_buffer_.data(), chars_written);
  } else {
    logger_->AppendLine(xe::threading::current_thread_id(), level_char, fmt,
                        std::strlen(fmt));
  }
}

void LogLineVarargs(const char level_char, const char* fmt, va_list args) {
  size_t chars_written = vsnprintf(log_format_buffer_.data(),
                                   log_format_buffer_.capacity(), fmt, args);
  logger_->AppendLine(xe::threading::current_thread_id(), level_char,
                      log_format_buffer_.data(), chars_written);
}

void LogLine(const char level_char, const char* str, size_t str_length) {
  logger_->AppendLine(
      xe::threading::current_thread_id(), level_char, str,
      str_length == std::string::npos ? std::strlen(str) : str_length);
}

void LogLine(const char level_char, const std::string& str) {
  logger_->AppendLine(xe::threading::current_thread_id(), level_char,
                      str.c_str(), str.length());
}

void FatalError(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogLineVarargs('X', fmt, args);
  va_end(args);

#if XE_PLATFORM_WIN32
  if (!xe::has_console_attached()) {
    va_start(args, fmt);
    vsnprintf(log_format_buffer_.data(), log_format_buffer_.capacity(), fmt,
              args);
    va_end(args);
    MessageBoxA(NULL, log_format_buffer_.data(), "Xenia Error",
                MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
  }
#endif  // WIN32

  exit(1);
}

void FatalError(const std::string& str) { FatalError(str.c_str()); }

}  // namespace xe
