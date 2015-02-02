/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/postmortem_cursor.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <xdb/postmortem_debug_target.h>
#include <xdb/protocol.h>

namespace xdb {

using xdb::protocol::EventType;

struct Command {
  enum class Type {
    EXIT,
    CONTINUE,
  };
  Type type;

 protected:
  Command(Type type) : type(type) {}
};

struct ExitCommand : public Command {
  ExitCommand() : Command(Type::EXIT) {}
};

struct ContinueCommand : public Command {
  ContinueCommand() : Command(Type::CONTINUE), thread(nullptr) {}
  Thread* thread;
};

class PostmortemCursor::Executor {
 public:
  Executor(PostmortemCursor* cursor) : cursor_(cursor), running_(true) {
    trace_ptr_ = cursor->target_->trace_base();

    // Must be last, as it'll spin up here.
    thread_ = std::thread(std::bind(&Executor::ExecutionThread, this));
  }
  ~Executor() {
    running_ = false;
    IssueCommand(std::make_unique<ExitCommand>());
    thread_.join();
  }

  bool eof() const { return eof_; }

  void IssueCommand(std::unique_ptr<Command> command) {
    {
      std::unique_lock<std::mutex> lock(queue_lock_);
      queue_.push(std::move(command));
      pending_commands_ = true;
    }
    queue_cond_.notify_one();
  }

 private:
  void ExecutionThread() {
    while (running_) {
      auto command = AwaitCommand();
      if (!running_ || !command || command->type == Command::Type::EXIT) {
        break;
      }

      switch (command->type) {
        case Command::Type::CONTINUE: {
          auto cmd = static_cast<ContinueCommand*>(command.get());
          cmd->thread;
          break;
        }
      }

      //
      Process(pending_commands_);
    }
  }

  void Process(std::atomic<bool>& exit_signal) {
    if (eof_) {
      return;
    }
    while (!exit_signal) {
      auto event_type = poly::load<EventType>(trace_ptr_);
      switch (event_type) {
        case EventType::END_OF_STREAM: {
          eof_= true;
          cursor_->end_of_stream();
          return;
        }
        case EventType::MODULE_LOAD: {
          auto ev = protocol::ModuleLoadEvent::Get(trace_ptr_);
          break;
        }
        case EventType::MODULE_UNLOAD: {
          auto ev = protocol::ModuleLoadEvent::Get(trace_ptr_);
          break;
        }
        case EventType::THREAD_CREATE: {
          auto ev = protocol::ThreadCreateEvent::Get(trace_ptr_);
          break;
        }
        case EventType::THREAD_INFO: {
          auto ev = protocol::ThreadInfoEvent::Get(trace_ptr_);
          break;
        }
        case EventType::THREAD_EXIT: {
          auto ev = protocol::ThreadExitEvent::Get(trace_ptr_);
          break;
        }
      }
      trace_ptr_ += protocol::kEventSizes[static_cast<uint8_t>(event_type)];
    }
  }

  std::unique_ptr<Command> AwaitCommand() {
    std::unique_lock<std::mutex> lock(queue_lock_);
    while (queue_.empty()) {
      queue_cond_.wait(lock);
    }
    auto command = std::move(queue_.front());
    queue_.pop();
    pending_commands_ = !queue_.empty();
    return command;
  }

  PostmortemCursor* cursor_;
  std::atomic<bool> running_;
  const uint8_t* trace_ptr_;
  std::atomic<bool> eof_;
  std::thread thread_;

  std::queue<std::unique_ptr<Command>> queue_;
  std::mutex queue_lock_;
  std::condition_variable queue_cond_;
  std::atomic<bool> pending_commands_;
};

PostmortemCursor::PostmortemCursor(PostmortemDebugTarget* target)
    : target_(target) {
  executor_.reset(new Executor(this));
}

PostmortemCursor::~PostmortemCursor() { executor_.reset(); }

bool PostmortemCursor::can_step() const {
  // TODO(benvanik): check trace flags.
  return true;
}

void PostmortemCursor::Continue(Thread* thread) {
  auto cmd = std::make_unique<ContinueCommand>();
  cmd->thread = thread;
  executor_->IssueCommand(std::move(cmd));
}

void PostmortemCursor::ContinueTo(uint32_t address, Thread* thread) {}

void PostmortemCursor::ContinueToNextCall(Thread* thread) {}

void PostmortemCursor::ContinueToReturn(Thread* thread) {}

void PostmortemCursor::ContinueUntil(
    const std::function<bool(protocol::EventType, uint8_t*)> predicate,
    Thread* thread) {}

void PostmortemCursor::Break() {}

}  // namespace xdb
