#include "dma.h"
#include "logging.h"
#include "xbyak/xbyak/xbyak_util.h"

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

static class DMAFeatures {
 public:
  uint32_t has_fast_rep_movsb : 1;
  uint32_t has_movdir64b : 1;

  DMAFeatures() {
    unsigned int data[4];
    memset(data, 0, sizeof(data));
    // intel extended features
    Xbyak::util::Cpu::getCpuidEx(7, 0, data);
    if (data[2] & (1 << 28)) {
      has_movdir64b = 1;
    }
    if (data[1] & (1 << 9)) {
      has_fast_rep_movsb = 1;
    }
  }
} dma_x86_features;
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
  if (dma_x86_features.has_movdir64b) {
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

#define XEDMA_NUM_WORKERS 4
class alignas(256) XeDMACGeneric : public XeDMAC {
  struct alignas(XE_HOST_CACHE_LINE_SIZE) {
    std::atomic<uint64_t> free_job_slots_;
    std::atomic<uint64_t> jobs_submitted_;
    std::atomic<uint64_t> jobs_completed_;
    std::atomic<uint32_t> num_workers_awoken_;
    std::atomic<uint32_t> current_job_serial_;

  } dma_volatile_;

  alignas(XE_HOST_CACHE_LINE_SIZE) XeDMAJob jobs_[64];

  volatile uint32_t jobserials_[64];

  alignas(XE_HOST_CACHE_LINE_SIZE)
      std::unique_ptr<threading::Event> job_done_signals_[64];
  // really dont like using unique pointer for this...
  std::unique_ptr<threading::Event> job_submitted_signal_;
  std::unique_ptr<threading::Event> job_completed_signal_;

  std::unique_ptr<threading::Thread> scheduler_thread_;
  struct WorkSlice {
    uint8_t* destination;
    uint8_t* source;
    size_t numbytes;
  };
  std::unique_ptr<threading::Thread> workers_[XEDMA_NUM_WORKERS];
  std::unique_ptr<threading::Event> worker_has_work_;  //[XEDMA_NUM_WORKERS];
  std::unique_ptr<threading::Event> worker_has_finished_[XEDMA_NUM_WORKERS];

  threading::WaitHandle* worker_has_finished_nosafeptr_[XEDMA_NUM_WORKERS];
  WorkSlice worker_workslice_[XEDMA_NUM_WORKERS];

  // chrispy: this is bad
  static uint32_t find_free_hole_in_dword(uint64_t dw) {
    XEDMALOG("Finding free hole in 0x%llX", dw);

    for (uint32_t i = 0; i < 64; ++i) {
      if (dw & (1ULL << i)) {
        continue;
      }

      return i;
    }
    return ~0U;
  }

  uint32_t allocate_free_dma_slot() {
    XEDMALOG("Allocating free slot");
    uint32_t got_slot = 0;
    uint64_t slots;
    uint64_t allocated_slot;

    do {
      slots = dma_volatile_.free_job_slots_.load();

      got_slot = find_free_hole_in_dword(slots);
      if (!~got_slot) {
        XEDMALOG("Didn't get a slot!");
        return ~0U;
      }
      allocated_slot = slots | (1ULL << got_slot);

    } while (XE_UNLIKELY(!dma_volatile_.free_job_slots_.compare_exchange_strong(
        slots, allocated_slot)));
    XEDMALOG("Allocated slot %d", got_slot);
    return got_slot;
  }
  // chrispy: on x86 this can just be interlockedbittestandreset...
  void free_dma_slot(uint32_t slot) {
    XEDMALOG("Freeing slot %d", slot);
    uint64_t slots;

    uint64_t deallocated_slot;

    do {
      slots = dma_volatile_.free_job_slots_.load();

      deallocated_slot = slots & (~(1ULL << slot));

    } while (XE_UNLIKELY(!dma_volatile_.free_job_slots_.compare_exchange_strong(
        slots, deallocated_slot)));
  }

  void DoDMAJob(uint32_t idx) {
    XeDMAJob& job = jobs_[idx];
    if (job.precall) {
      job.precall(&job);
    }
    // memcpy(job.destination, job.source, job.size);

    size_t job_size = job.size;

    size_t job_num_lines = job_size / XE_HOST_CACHE_LINE_SIZE;

    size_t line_rounded = job_num_lines * XE_HOST_CACHE_LINE_SIZE;

    size_t rem = job_size - line_rounded;

    size_t num_per_worker = line_rounded / XEDMA_NUM_WORKERS;

    XEDMALOG(
        "Distributing %d bytes from %p to %p across %d workers, remainder is "
        "%d",
        line_rounded, job.source, job.destination, XEDMA_NUM_WORKERS, rem);
    if (num_per_worker < 2048) {
      XEDMALOG("not distributing across workers, num_per_worker < 8192");
      // not worth splitting up
      memcpy(job.destination, job.source, job.size);
      job.signal_on_done->Set();
    } else {
      for (uint32_t i = 0; i < XEDMA_NUM_WORKERS; ++i) {
        worker_workslice_[i].destination =
            (i * num_per_worker) + job.destination;
        worker_workslice_[i].source = (i * num_per_worker) + job.source;

        worker_workslice_[i].numbytes = num_per_worker;
      }
      if (rem) {
        __movsb(job.destination + line_rounded, job.source + line_rounded, rem);
      }
      // wake them up
      worker_has_work_->Set();
      XEDMALOG("Starting waitall for job");
      threading::WaitAll(worker_has_finished_nosafeptr_, XEDMA_NUM_WORKERS,
                         false);

      XEDMALOG("Waitall for job completed!");
      job.signal_on_done->Set();
    }
    if (job.postcall) {
      job.postcall(&job);
    }
    ++dma_volatile_.jobs_completed_;
  }

  void WorkerIter(uint32_t worker_index) {
    xenia_assert(worker_index < XEDMA_NUM_WORKERS);
    auto [dest, src, size] = worker_workslice_[worker_index];

    //  if (++dma_volatile_.num_workers_awoken_ == XEDMA_NUM_WORKERS ) {
    worker_has_work_->Reset();
    //}
    xenia_assert(size < (1ULL << 32));
    // memcpy(dest, src, size);
    dma::vastcpy(dest, src, static_cast<uint32_t>(size));
  }
  XE_NOINLINE
  void WorkerMainLoop(uint32_t worker_index) {
    do {
      XEDMALOG("Worker iter for worker %d", worker_index);
      WorkerIter(worker_index);

      XEDMALOG("Worker %d is done\n", worker_index);
      threading::SignalAndWait(worker_has_finished_[worker_index].get(),
                               worker_has_work_.get(), false);
    } while (true);
  }
  void WorkerMain(uint32_t worker_index) {
    XEDMALOG("Entered worker main loop, index %d", worker_index);
    threading::Wait(worker_has_work_.get(), false);
    XEDMALOG("First wait for worker %d completed, first job ever",
             worker_index);
    WorkerMainLoop(worker_index);
  }

  static void WorkerMainForwarder(void* ptr) {
    // we aligned XeDma to 256 bytes and encode extra info in the low 8
    uintptr_t uptr = (uintptr_t)ptr;

    uint32_t worker_index = (uint8_t)uptr;

    uptr &= ~0xFFULL;

    char name_buffer[64];
    sprintf_s(name_buffer, "dma_worker_%d", worker_index);

    xe::threading::set_name(name_buffer);

    reinterpret_cast<XeDMACGeneric*>(uptr)->WorkerMain(worker_index);
  }

  void DMAMain() {
    XEDMALOG("DmaMain");
    do {
      threading::Wait(job_submitted_signal_.get(), false);

      auto slots = dma_volatile_.free_job_slots_.load();

      for (uint32_t i = 0; i < 64; ++i) {
        if (slots & (1ULL << i)) {
          XEDMALOG("Got new job at index %d in DMAMain", i);
          DoDMAJob(i);

          free_dma_slot(i);

          job_completed_signal_->Set();
          //         break;
        }
      }

    } while (true);
  }

  static void DMAMainForwarder(void* ud) {
    xe::threading::set_name("dma_main");
    reinterpret_cast<XeDMACGeneric*>(ud)->DMAMain();
  }

 public:
  virtual DMACJobHandle PushDMAJob(XeDMAJob* job) override {
    XEDMALOG("New job, %p to %p with size %d", job->source, job->destination,
             job->size);
    uint32_t slot;
    do {
      slot = allocate_free_dma_slot();
      if (!~slot) {
        XEDMALOG(
            "Didn't get a free slot, waiting for a job to complete before "
            "resuming.");
        threading::Wait(job_completed_signal_.get(), false);

      } else {
        break;
      }

    } while (true);
    jobs_[slot] = *job;

    jobs_[slot].signal_on_done = job_done_signals_[slot].get();
    jobs_[slot].signal_on_done->Reset();
    XEDMALOG("Setting job submit signal, pushed into slot %d", slot);

    uint32_t new_serial = dma_volatile_.current_job_serial_++;

    jobserials_[slot] = new_serial;

    ++dma_volatile_.jobs_submitted_;
    job_submitted_signal_->Set();
    return (static_cast<uint64_t>(new_serial) << 32) |
           static_cast<uint64_t>(slot);

    // return job_done_signals_[slot].get();
  }

  bool AllJobsDone() {
    return dma_volatile_.jobs_completed_ == dma_volatile_.jobs_submitted_;
  }
  virtual void WaitJobDone(DMACJobHandle handle) override {
    uint32_t serial = static_cast<uint32_t>(handle >> 32);
    uint32_t jobid = static_cast<uint32_t>(handle);
    do {
      if (jobserials_[jobid] != serial) {
        return;  // done, our slot was reused
      }

      auto waitres = threading::Wait(job_done_signals_[jobid].get(), false,
                                     std::chrono::milliseconds{1});

      if (waitres == threading::WaitResult::kTimeout) {
        continue;
      } else {
        return;
      }
    } while (true);
  }
  virtual void WaitForIdle() override {
    while (!AllJobsDone()) {
      threading::MaybeYield();
    }
  }
  XeDMACGeneric() {
    XEDMALOG("Constructing xedma at addr %p", this);
    dma_volatile_.free_job_slots_.store(0ULL);
    dma_volatile_.jobs_submitted_.store(0ULL);
    dma_volatile_.jobs_completed_.store(0ULL);
    dma_volatile_.current_job_serial_.store(
        1ULL);  // so that a jobhandle is never 0
    std::memset(jobs_, 0, sizeof(jobs_));
    job_submitted_signal_ = threading::Event::CreateAutoResetEvent(false);
    job_completed_signal_ = threading::Event::CreateAutoResetEvent(false);
    worker_has_work_ = threading::Event::CreateManualResetEvent(false);
    threading::Thread::CreationParameters worker_params{};
    worker_params.create_suspended = false;
    worker_params.initial_priority = threading::ThreadPriority::kBelowNormal;
    worker_params.stack_size = 65536;  // dont need much stack at all

    for (uint32_t i = 0; i < 64; ++i) {
      job_done_signals_[i] = threading::Event::CreateManualResetEvent(false);
    }
    for (uint32_t i = 0; i < XEDMA_NUM_WORKERS; ++i) {
      // worker_has_work_[i] = threading::Event::CreateAutoResetEvent(false);
      worker_has_finished_[i] = threading::Event::CreateAutoResetEvent(false);
      worker_has_finished_nosafeptr_[i] = worker_has_finished_[i].get();

      uintptr_t encoded = reinterpret_cast<uintptr_t>(this);
      xenia_assert(!(encoded & 0xFFULL));
      xenia_assert(i < 256);

      encoded |= i;

      workers_[i] = threading::Thread::Create(worker_params, [encoded]() {
        XeDMACGeneric::WorkerMainForwarder((void*)encoded);
      });
    }
    threading::Thread::CreationParameters scheduler_params{};
    scheduler_params.create_suspended = false;
    scheduler_params.initial_priority = threading::ThreadPriority::kBelowNormal;
    scheduler_params.stack_size = 65536;
    scheduler_thread_ = threading::Thread::Create(scheduler_params, [this]() {
      XeDMACGeneric::DMAMainForwarder((void*)this);
    });
  }
};
XeDMAC* CreateDMAC() { return new XeDMACGeneric(); }
}  // namespace xe::dma
