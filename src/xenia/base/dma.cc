#include "dma.h"
#include "logging.h"
#include "mutex.h"
#include "platform_win.h"

XE_NTDLL_IMPORT(NtDelayExecution, cls_NtDelayExecution,
                NtDelayExecutionPointer);
XE_NTDLL_IMPORT(NtAlertThread, cls_NtAlertThread, NtAlertThreadPointer);
XE_NTDLL_IMPORT(NtAlertThreadByThreadId, cls_NtAlertThreadByThreadId,
                NtAlertThreadByThreadId);

template <size_t N, typename... Ts>
static void xedmaloghelper(const char (&fmt)[N], Ts... args) {
  char buffer[1024];
  sprintf_s(buffer, fmt, args...);
  XELOGI("%s", buffer);
}

//#define XEDMALOG(...) XELOGI("XeDma: " __VA_ARGS__)
//#define XEDMALOG(...) xedmaloghelper("XeDma: " __VA_ARGS__)
#define XEDMALOG(...) static_cast<void>(0)
using xe::swcache::CacheLine;
static constexpr unsigned NUM_CACHELINES_IN_PAGE = 4096 / sizeof(CacheLine);
#if defined(__clang__)
XE_FORCEINLINE
static void mvdir64b(void* to, const void* from) {
  __asm__("movdir64b %1, %0" : : "r"(to), "m"(*(char*)from) : "memory");
}
#define _movdir64b mvdir64b
#endif
XE_FORCEINLINE
static void XeCopy16384StreamingAVX(CacheLine* XE_RESTRICT to,
                                    CacheLine* XE_RESTRICT from) {
  uint32_t num_lines_for_8k = 4096 / XE_HOST_CACHE_LINE_SIZE;

  CacheLine* dest1 = to;
  CacheLine* src1 = from;

  CacheLine* dest2 = to + NUM_CACHELINES_IN_PAGE;
  CacheLine* src2 = from + NUM_CACHELINES_IN_PAGE;

  CacheLine* dest3 = to + (NUM_CACHELINES_IN_PAGE * 2);
  CacheLine* src3 = from + (NUM_CACHELINES_IN_PAGE * 2);

  CacheLine* dest4 = to + (NUM_CACHELINES_IN_PAGE * 3);
  CacheLine* src4 = from + (NUM_CACHELINES_IN_PAGE * 3);
#pragma loop(no_vector)
  for (uint32_t i = 0; i < num_lines_for_8k; ++i) {
    xe::swcache::CacheLine line0, line1, line2, line3;

    xe::swcache::ReadLine(&line0, src1 + i);
    xe::swcache::ReadLine(&line1, src2 + i);
    xe::swcache::ReadLine(&line2, src3 + i);
    xe::swcache::ReadLine(&line3, src4 + i);
    XE_MSVC_REORDER_BARRIER();
    xe::swcache::WriteLineNT(dest1 + i, &line0);
    xe::swcache::WriteLineNT(dest2 + i, &line1);

    xe::swcache::WriteLineNT(dest3 + i, &line2);
    xe::swcache::WriteLineNT(dest4 + i, &line3);
  }
  XE_MSVC_REORDER_BARRIER();
}
XE_FORCEINLINE
static void XeCopy16384Movdir64M(CacheLine* XE_RESTRICT to,
                                 CacheLine* XE_RESTRICT from) {
  uint32_t num_lines_for_8k = 4096 / XE_HOST_CACHE_LINE_SIZE;

  CacheLine* dest1 = to;
  CacheLine* src1 = from;

  CacheLine* dest2 = to + NUM_CACHELINES_IN_PAGE;
  CacheLine* src2 = from + NUM_CACHELINES_IN_PAGE;

  CacheLine* dest3 = to + (NUM_CACHELINES_IN_PAGE * 2);
  CacheLine* src3 = from + (NUM_CACHELINES_IN_PAGE * 2);

  CacheLine* dest4 = to + (NUM_CACHELINES_IN_PAGE * 3);
  CacheLine* src4 = from + (NUM_CACHELINES_IN_PAGE * 3);
#pragma loop(no_vector)
  for (uint32_t i = 0; i < num_lines_for_8k; ++i) {
#if 0
    xe::swcache::CacheLine line0, line1, line2, line3;

    xe::swcache::ReadLine(&line0, src1 + i);
    xe::swcache::ReadLine(&line1, src2 + i);
    xe::swcache::ReadLine(&line2, src3 + i);
    xe::swcache::ReadLine(&line3, src4 + i);
    XE_MSVC_REORDER_BARRIER();
    xe::swcache::WriteLineNT(dest1 + i, &line0);
    xe::swcache::WriteLineNT(dest2 + i, &line1);

    xe::swcache::WriteLineNT(dest3 + i, &line2);
    xe::swcache::WriteLineNT(dest4 + i, &line3);
#else
    _movdir64b(dest1 + i, src1 + i);
    _movdir64b(dest2 + i, src2 + i);
    _movdir64b(dest3 + i, src3 + i);
    _movdir64b(dest4 + i, src4 + i);
#endif
  }
  XE_MSVC_REORDER_BARRIER();
}

namespace xe::dma {
using VastCpyDispatch = void (*)(CacheLine* XE_RESTRICT physaddr,
                                 CacheLine* XE_RESTRICT rdmapping,
                                 uint32_t written_length);
static void vastcpy_impl_avx(CacheLine* XE_RESTRICT physaddr,
                             CacheLine* XE_RESTRICT rdmapping,
                             uint32_t written_length) {
  static constexpr unsigned NUM_LINES_FOR_16K = 16384 / XE_HOST_CACHE_LINE_SIZE;

  while (written_length >= 16384) {
    XeCopy16384StreamingAVX(physaddr, rdmapping);

    physaddr += NUM_LINES_FOR_16K;
    rdmapping += NUM_LINES_FOR_16K;

    written_length -= 16384;
  }

  if (!written_length) {
    return;
  }
  uint32_t num_written_lines = written_length / XE_HOST_CACHE_LINE_SIZE;

  uint32_t i = 0;

  for (; i + 1 < num_written_lines; i += 2) {
    xe::swcache::CacheLine line0, line1;

    xe::swcache::ReadLine(&line0, rdmapping + i);

    xe::swcache::ReadLine(&line1, rdmapping + i + 1);
    XE_MSVC_REORDER_BARRIER();
    xe::swcache::WriteLineNT(physaddr + i, &line0);
    xe::swcache::WriteLineNT(physaddr + i + 1, &line1);
  }

  if (i < num_written_lines) {
    xe::swcache::CacheLine line0;

    xe::swcache::ReadLine(&line0, rdmapping + i);
    xe::swcache::WriteLineNT(physaddr + i, &line0);
  }
}

static void vastcpy_impl_movdir64m(CacheLine* XE_RESTRICT physaddr,
                                   CacheLine* XE_RESTRICT rdmapping,
                                   uint32_t written_length) {
  static constexpr unsigned NUM_LINES_FOR_16K = 16384 / XE_HOST_CACHE_LINE_SIZE;

  while (written_length >= 16384) {
    XeCopy16384Movdir64M(physaddr, rdmapping);

    physaddr += NUM_LINES_FOR_16K;
    rdmapping += NUM_LINES_FOR_16K;

    written_length -= 16384;
  }

  if (!written_length) {
    return;
  }
  uint32_t num_written_lines = written_length / XE_HOST_CACHE_LINE_SIZE;

  uint32_t i = 0;

  for (; i + 1 < num_written_lines; i += 2) {
    _movdir64b(physaddr + i, rdmapping + i);
    _movdir64b(physaddr + i + 1, rdmapping + i + 1);
  }

  if (i < num_written_lines) {
    _movdir64b(physaddr + i, rdmapping + i);
  }
}

XE_COLD
static void first_vastcpy(CacheLine* XE_RESTRICT physaddr,
                          CacheLine* XE_RESTRICT rdmapping,
                          uint32_t written_length);

static VastCpyDispatch vastcpy_dispatch = first_vastcpy;

XE_COLD
static void first_vastcpy(CacheLine* XE_RESTRICT physaddr,
                          CacheLine* XE_RESTRICT rdmapping,
                          uint32_t written_length) {
  VastCpyDispatch dispatch_to_use = nullptr;
  if (amd64::GetFeatureFlags() & amd64::kX64EmitMovdir64M) {
    XELOGI("Selecting MOVDIR64M vastcpy.");
    dispatch_to_use = vastcpy_impl_movdir64m;
  } else {
    XELOGI("Selecting generic AVX vastcpy.");
    dispatch_to_use = vastcpy_impl_avx;
  }

  vastcpy_dispatch =
      dispatch_to_use;  // all future calls will go through our selected path
  return vastcpy_dispatch(physaddr, rdmapping, written_length);
}

XE_NOINLINE
void vastcpy(uint8_t* XE_RESTRICT physaddr, uint8_t* XE_RESTRICT rdmapping,
             uint32_t written_length) {
  return vastcpy_dispatch((CacheLine*)physaddr, (CacheLine*)rdmapping,
                          written_length);
}

#define MAX_INFLIGHT_DMAJOBS 65536
#define INFLICT_DMAJOB_MASK (MAX_INFLIGHT_DMAJOBS - 1)
class XeDMACGeneric : public XeDMAC {
  std::unique_ptr<xe::threading::Thread> thrd_;
  XeDMAJob* jobs_ring_;
  volatile std::atomic<uintptr_t> write_ptr_;

  struct alignas(XE_HOST_CACHE_LINE_SIZE) {
    volatile std::atomic<uintptr_t> read_ptr_;
    xe_mutex push_into_ring_lock_;
  };
  HANDLE gotjob_event;
  void WorkerWait();

 public:
  virtual ~XeDMACGeneric() {}
  void WorkerThreadMain();
  XeDMACGeneric() {
    threading::Thread::CreationParameters crparams;
    crparams.create_suspended = true;
    crparams.initial_priority = threading::ThreadPriority::kNormal;
    crparams.stack_size = 65536;
    gotjob_event = CreateEventA(nullptr, false, false, nullptr);
    thrd_ = std::move(threading::Thread::Create(
        crparams, [this]() { this->WorkerThreadMain(); }));

    jobs_ring_ = (XeDMAJob*)_aligned_malloc(
        MAX_INFLIGHT_DMAJOBS * sizeof(XeDMAJob), XE_HOST_CACHE_LINE_SIZE);

    write_ptr_ = 0;
    read_ptr_ = 0;

    thrd_->Resume();
  }

  virtual DMACJobHandle PushDMAJob(XeDMAJob* job) override {
    // std::unique_lock<xe_mutex> pushlock{push_into_ring_lock_};
    HANDLE dmacevent = CreateEventA(nullptr, true, false, nullptr);
    {
      job->dmac_specific_ = (uintptr_t)dmacevent;

      jobs_ring_[write_ptr_ % MAX_INFLIGHT_DMAJOBS] = *job;
      write_ptr_++;
      SetEvent(gotjob_event);
    }
    return (DMACJobHandle)dmacevent;
  }
  virtual void WaitJobDone(DMACJobHandle handle) override {
    while (WaitForSingleObject((HANDLE)handle, 2) == WAIT_TIMEOUT) {
      // NtAlertThreadByThreadId.invoke<void>(thrd_->system_id());
      //  while (SignalObjectAndWait(gotjob_event, (HANDLE)handle, 2, false) ==
      //           WAIT_TIMEOUT) {
      //    ;
    }
    //}

    // SignalObjectAndWait(gotjob_event, (HANDLE)handle, INFINITE, false);
    CloseHandle((HANDLE)handle);
  }
  virtual void WaitForIdle() override {
    while (write_ptr_ != read_ptr_) {
      threading::MaybeYield();
    }
  }
};
void XeDMACGeneric::WorkerWait() {
  constexpr unsigned NUM_PAUSE_SPINS = 2048;
  constexpr unsigned NUM_YIELD_SPINS = 8;
#if 0

  for (unsigned i = 0; i < NUM_PAUSE_SPINS; ++i) {
    if (write_ptr_ == read_ptr_) {
      _mm_pause();
    } else {
      break;
    }
  }
  for (unsigned i = 0; i < NUM_YIELD_SPINS; ++i) {
    if (write_ptr_ == read_ptr_) {
      threading::MaybeYield();
    } else {
      break;
    }
  }
  LARGE_INTEGER yield_execution_delay{};
  yield_execution_delay.QuadPart =
      -2000;  //-10000 == 1 ms, so -2000 means delay for 0.2 milliseconds
  while (write_ptr_ == read_ptr_) {
    NtDelayExecutionPointer.invoke<void>(0, &yield_execution_delay);
  }
#else
  do {
    if (WaitForSingleObjectEx(gotjob_event, 1, TRUE) == WAIT_OBJECT_0) {
      while (write_ptr_ == read_ptr_) {
        _mm_pause();
      }
    }

  } while (write_ptr_ == read_ptr_);
#endif
}
void XeDMACGeneric::WorkerThreadMain() {
  while (true) {
    this->WorkerWait();

    XeDMAJob current_job = jobs_ring_[read_ptr_ % MAX_INFLIGHT_DMAJOBS];
    swcache::ReadFence();

    if (current_job.precall) {
      current_job.precall(&current_job);
    }

    size_t num_lines = current_job.size / XE_HOST_CACHE_LINE_SIZE;
    size_t line_rounded = num_lines * XE_HOST_CACHE_LINE_SIZE;

    size_t line_rem = current_job.size - line_rounded;

    vastcpy(current_job.destination, current_job.source,
            static_cast<uint32_t>(line_rounded));

    if (line_rem) {
      __movsb(current_job.destination + line_rounded,
              current_job.source + line_rounded, line_rem);
    }

    if (current_job.postcall) {
      current_job.postcall(&current_job);
    }
    read_ptr_++;
    swcache::WriteFence();

    SetEvent((HANDLE)current_job.dmac_specific_);
  }
}

XeDMAC* CreateDMAC() { return new XeDMACGeneric(); }
}  // namespace xe::dma
