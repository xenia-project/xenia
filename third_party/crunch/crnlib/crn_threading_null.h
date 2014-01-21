// File: crn_threading_null.h
// See Copyright Notice and license at the end of include/crnlib.h
#pragma once

#include "crn_atomics.h"

namespace crnlib
{
   const uint g_number_of_processors = 1;
   
   inline void crn_threading_init()
   {
   }

   typedef uint64 crn_thread_id_t;
   inline crn_thread_id_t crn_get_current_thread_id()
   {
      return 0;
   }

   inline void crn_sleep(unsigned int milliseconds)
   {
      milliseconds;
   }

   inline uint crn_get_max_helper_threads()
   {
      return 0;
   }

   class mutex
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(mutex);

   public:
      inline mutex(unsigned int spin_count = 0)
      {
         spin_count;
      }

      inline ~mutex()
      {
      }

      inline void lock()
      {
      }

      inline void unlock()
      {
      }

      inline void set_spin_count(unsigned int count)
      {
         count;
      }
   };

   class scoped_mutex
   {
      scoped_mutex(const scoped_mutex&);
      scoped_mutex& operator= (const scoped_mutex&);

   public:
      inline scoped_mutex(mutex& lock) : m_lock(lock) { m_lock.lock(); }
      inline ~scoped_mutex() { m_lock.unlock(); }

   private:
      mutex& m_lock;
   };

    // Simple non-recursive spinlock.
   class spinlock
   {
   public:
      inline spinlock()
      {
      }

      inline void lock(uint32 max_spins = 4096, bool yielding = true, bool memoryBarrier = true)
      {
         max_spins, yielding, memoryBarrier;
      }

      inline void lock_no_barrier(uint32 max_spins = 4096, bool yielding = true)
      {
         max_spins, yielding;
      }

      inline void unlock()
      {
      }

      inline void unlock_no_barrier()
      {
      }
   };

   class scoped_spinlock
   {
      scoped_spinlock(const scoped_spinlock&);
      scoped_spinlock& operator= (const scoped_spinlock&);

   public:
      inline scoped_spinlock(spinlock& lock) : m_lock(lock) { m_lock.lock(); }
      inline ~scoped_spinlock() { m_lock.unlock(); }

   private:
      spinlock& m_lock;
   };

   class semaphore
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(semaphore);

   public:
      inline semaphore(long initialCount = 0, long maximumCount = 1, const char* pName = NULL)
      {
         initialCount, maximumCount, pName;
      }

      inline ~semaphore()
      {
      }

      inline void release(long releaseCount = 1, long *pPreviousCount = NULL)
      {
         releaseCount, pPreviousCount;
      }

      inline bool wait(uint32 milliseconds = cUINT32_MAX)
      {
         milliseconds;
         return true;
      }
   };

   class task_pool
   {
   public:
      inline task_pool() { }
      inline task_pool(uint num_threads) { num_threads; }
      inline ~task_pool() { }

      inline bool init(uint num_threads) { num_threads; return true; }
      inline void deinit() { }

      inline uint get_num_threads() const { return 0; }
      inline uint get_num_outstanding_tasks() const { return 0; }

      // C-style task callback
      typedef void (*task_callback_func)(uint64 data, void* pData_ptr);
      inline bool queue_task(task_callback_func pFunc, uint64 data = 0, void* pData_ptr = NULL)
      {
         pFunc(data, pData_ptr);
         return true;
      }

      class executable_task
      {
      public:
         virtual void execute_task(uint64 data, void* pData_ptr) = 0;
      };

      // It's the caller's responsibility to delete pObj within the execute_task() method, if needed!
      inline bool queue_task(executable_task* pObj, uint64 data = 0, void* pData_ptr = NULL)
      {
         pObj->execute_task(data, pData_ptr);
         return true;
      }

      template<typename S, typename T>
      inline bool queue_object_task(S* pObject, T pObject_method, uint64 data = 0, void* pData_ptr = NULL)
      {
         (pObject->*pObject_method)(data, pData_ptr);
         return true;
      }

      template<typename S, typename T>
      inline bool queue_multiple_object_tasks(S* pObject, T pObject_method, uint64 first_data, uint num_tasks, void* pData_ptr = NULL)
      {
         for (uint i = 0; i < num_tasks; i++)
         {
            (pObject->*pObject_method)(first_data + i, pData_ptr);
         }
         return true;
      }

      inline void join() { }
   };

} // namespace crnlib
