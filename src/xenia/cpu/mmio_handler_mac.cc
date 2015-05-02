/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/mmio_handler.h"

#include <mach/mach.h>
#include <signal.h>

#include <thread>

#include "xenia/base/logging.h"

// Mach internal function, not defined in any header.
// http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/exc_server.html
extern "C" boolean_t exc_server(mach_msg_header_t* request_msg,
                                mach_msg_header_t* reply_msg);

// Exported for the kernel to call back into.
// http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/catch_exception_raise.html
extern "C" kern_return_t catch_exception_raise(
    mach_port_t exception_port, mach_port_t thread, mach_port_t task,
    exception_type_t exception, exception_data_t code,
    mach_msg_type_number_t code_count);

namespace xe {
namespace cpu {

class MachMMIOHandler : public MMIOHandler {
 public:
  MachMMIOHandler(uint8_t* mapping_base);
  ~MachMMIOHandler() override;

 protected:
  bool Initialize() override;

  uint64_t GetThreadStateRip(void* thread_state_ptr) override;
  void SetThreadStateRip(void* thread_state_ptr, uint64_t rip) override;
  uint64_t* GetThreadStateRegPtr(void* thread_state_ptr,
                                 int32_t be_reg_index) override;

 private:
  void ThreadEntry();

  // Worker thread processing exceptions.
  std::unique_ptr<std::thread> thread_;
  // Port listening for exceptions on the worker thread.
  mach_port_t listen_port_;
};

std::unique_ptr<MMIOHandler> CreateMMIOHandler(uint8_t* mapping_base) {
  return std::make_unique<MachMMIOHandler>(mapping_base);
}

MachMMIOHandler::MachMMIOHandler(uint8_t* mapping_base)
    : MMIOHandler(mapping_base), listen_port_(0) {}

bool MachMMIOHandler::Initialize() {
  // Allocates the port that listens for exceptions.
  // This will be freed in the dtor.
  if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                         &listen_port_) != KERN_SUCCESS) {
    XELOGE("Unable to allocate listen port");
    return false;
  }

  // http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/mach_port_insert_right.html
  if (mach_port_insert_right(mach_task_self(), listen_port_, listen_port_,
                             MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
    XELOGE("Unable to insert listen port right");
    return false;
  }

  // Sets our exception filter so that any BAD_ACCESS exceptions go to it.
  // http://web.mit.edu/darwin/src/modules/xnu/osfmk/man/task_set_exception_ports.html
  if (task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS,
                               listen_port_, EXCEPTION_DEFAULT,
                               MACHINE_THREAD_STATE) != KERN_SUCCESS) {
    XELOGE("Unable to set exception port");
    return false;
  }

  // Spin up the worker thread.
  std::unique_ptr<std::thread> thread(
      new std::thread([this]() { ThreadEntry(); }));
  thread->detach();
  thread_ = std::move(thread);
  return true;
}

MachMMIOHandler::~MachMMIOHandler() {
  task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, 0,
                           EXCEPTION_DEFAULT, 0);
  mach_port_deallocate(mach_task_self(), listen_port_);
}

void MachMMIOHandler::ThreadEntry() {
  while (true) {
    struct {
      mach_msg_header_t head;
      mach_msg_body_t msgh_body;
      char data[1024];
    } msg;
    struct {
      mach_msg_header_t head;
      char data[1024];
    } reply;

    // Wait for a message on the exception port.
    mach_msg_return_t ret =
        mach_msg(&msg.head, MACH_RCV_MSG | MACH_RCV_LARGE, 0, sizeof(msg),
                 listen_port_, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (ret != MACH_MSG_SUCCESS) {
      XELOGE("mach_msg receive failed with %d %s", ret, mach_error_string(ret));
      xe::debugging::Break();
      break;
    }

    // Call exc_server, which will dispatch the catch_exception_raise.
    exc_server(&msg.head, &reply.head);

    // Send the reply.
    if (mach_msg(&reply.head, MACH_SEND_MSG, reply.head.msgh_size, 0,
                 MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
                 MACH_PORT_NULL) != MACH_MSG_SUCCESS) {
      XELOGE("mach_msg reply send failed");
      xe::debugging::Break();
      break;
    }
  }
}

// Kills the app when a bad access exception is unhandled.
void FailBadAccess() {
  raise(SIGSEGV);
  abort();
}

kern_return_t CatchExceptionRaise(mach_port_t thread) {
  auto state_count = x86_EXCEPTION_STATE64_COUNT;
  x86_exception_state64_t exc_state;
  if (thread_get_state(thread, x86_EXCEPTION_STATE64,
                       reinterpret_cast<thread_state_t>(&exc_state),
                       &state_count) != KERN_SUCCESS) {
    XELOGE("thread_get_state failed to get exception state");
    return KERN_FAILURE;
  }
  state_count = x86_THREAD_STATE64_COUNT;
  x86_thread_state64_t thread_state;
  if (thread_get_state(thread, x86_THREAD_STATE64,
                       reinterpret_cast<thread_state_t>(&thread_state),
                       &state_count) != KERN_SUCCESS) {
    XELOGE("thread_get_state failed to get thread state");
    return KERN_FAILURE;
  }

  auto fault_address = exc_state.__faultvaddr;
  auto mmio_handler =
      static_cast<MachMMIOHandler*>(MMIOHandler::global_handler());
  bool handled = mmio_handler->HandleAccessFault(&thread_state, fault_address);
  if (!handled) {
    // Unhandled - raise to the system.
    XELOGE("MMIO unhandled bad access for %llx, bubbling", fault_address);
    // TODO(benvanik): manipulate stack so that we can rip = break_handler or
    // something and have the stack trace be valid.
    xe::debugging::Break();

    // When the thread resumes, kill it.
    thread_state.__rip = reinterpret_cast<uint64_t>(FailBadAccess);

    // Mach docs say we can return this to continue searching for handlers, but
    // OSX doesn't seem to have it.
    // return MIG_DESTROY_REQUEST;
  }

  // Set the thread state - as we've likely changed it.
  if (thread_set_state(thread, x86_THREAD_STATE64,
                       reinterpret_cast<thread_state_t>(&thread_state),
                       state_count) != KERN_SUCCESS) {
    XELOGE("thread_set_state failed to set thread state for continue");
    return KERN_FAILURE;
  }
  return KERN_SUCCESS;
}

uint64_t MachMMIOHandler::GetThreadStateRip(void* thread_state_ptr) {
  auto thread_state = reinterpret_cast<x86_thread_state64_t*>(thread_state_ptr);
  return thread_state->__rip;
}

void MachMMIOHandler::SetThreadStateRip(void* thread_state_ptr, uint64_t rip) {
  auto thread_state = reinterpret_cast<x86_thread_state64_t*>(thread_state_ptr);
  thread_state->__rip = rip;
}

uint64_t* MachMMIOHandler::GetThreadStateRegPtr(void* thread_state_ptr,
                                                int32_t be_reg_index) {
  // Map from BeaEngine register order to x86_thread_state64 order.
  static const uint32_t mapping[] = {
      0,   // REG0 / RAX -> 0
      2,   // REG1 / RCX -> 2
      3,   // REG2 / RDX -> 3
      1,   // REG3 / RBX -> 1
      7,   // REG4 / RSP -> 7
      6,   // REG5 / RBP -> 6
      5,   // REG6 / RSI -> 5
      4,   // REG7 / RDI -> 4
      8,   // REG8 / R8 -> 8
      9,   // REG9 / R9 -> 9
      10,  // REG10 / R10 -> 10
      11,  // REG11 / R11 -> 11
      12,  // REG12 / R12 -> 12
      13,  // REG13 / R13 -> 13
      14,  // REG14 / R14 -> 14
      15,  // REG15 / R15 -> 15
  };
  auto thread_state = reinterpret_cast<x86_thread_state64_t*>(thread_state_ptr);
  return &thread_state->__rax + mapping[be_reg_index];
}

}  // namespace cpu
}  // namespace xe

// Exported and called by exc_server.
extern "C" kern_return_t catch_exception_raise(
    mach_port_t exception_port, mach_port_t thread, mach_port_t task,
    exception_type_t exception, exception_data_t code,
    mach_msg_type_number_t code_count) {
  // We get/set the states manually instead of using catch_exception_raise_state
  // variant because that truncates everything to 32bit.
  return xe::cpu::CatchExceptionRaise(thread);
}
