// File: lzham_task_pool_win32.cpp
// See Copyright Notice and license at the end of include/lzham.h
#include "lzham_core.h"
#include "lzham_win32_threading.h"
#include "lzham_timer.h"
#include <process.h>

#if LZHAM_USE_WIN32_API

namespace lzham
{
   task_pool::task_pool() :
      m_num_threads(0),
      m_tasks_available(0, 32767),
      m_num_outstanding_tasks(0),
      m_exit_flag(false)
   {
      utils::zero_object(m_threads);
   }

   task_pool::task_pool(uint num_threads) :
      m_num_threads(0),
      m_tasks_available(0, 32767),
      m_num_outstanding_tasks(0),
      m_exit_flag(false)
   {
      utils::zero_object(m_threads);

      bool status = init(num_threads);
      LZHAM_VERIFY(status);
   }

   task_pool::~task_pool()
   {
      deinit();
   }

   bool task_pool::init(uint num_threads)
   {
      LZHAM_ASSERT(num_threads <= cMaxThreads);
      num_threads = math::minimum<uint>(num_threads, cMaxThreads);

      deinit();

      bool succeeded = true;

      m_num_threads = 0;
      while (m_num_threads < num_threads)
      {
         m_threads[m_num_threads] = (HANDLE)_beginthreadex(NULL, 32768, thread_func, this, 0, NULL);
         LZHAM_ASSERT(m_threads[m_num_threads] != 0);

         if (!m_threads[m_num_threads])
         {
            succeeded = false;
            break;
         }

         m_num_threads++;
      }

      if (!succeeded)
      {
         deinit();
         return false;
      }

      return true;
   }

   void task_pool::deinit()
   {
      if (m_num_threads)
      {
         join();

         atomic_exchange32(&m_exit_flag, true);

         m_tasks_available.release(m_num_threads);

         for (uint i = 0; i < m_num_threads; i++)
         {
            if (m_threads[i])
            {
               for ( ; ; )
               {
                  DWORD result = WaitForSingleObject(m_threads[i], 30000);
                  if ((result == WAIT_OBJECT_0) || (result == WAIT_ABANDONED))
                     break;
               }

               CloseHandle(m_threads[i]);
               m_threads[i] = NULL;
            }
         }

         m_num_threads = 0;

         atomic_exchange32(&m_exit_flag, false);
      }

      m_task_stack.clear();
      m_num_outstanding_tasks = 0;
   }

   bool task_pool::queue_task(task_callback_func pFunc, uint64 data, void* pData_ptr)
   {
      LZHAM_ASSERT(m_num_threads);
      LZHAM_ASSERT(pFunc);

      task tsk;
      tsk.m_callback = pFunc;
      tsk.m_data = data;
      tsk.m_pData_ptr = pData_ptr;
      tsk.m_flags = 0;

      if (!m_task_stack.try_push(tsk))
         return false;

      atomic_increment32(&m_num_outstanding_tasks);

      m_tasks_available.release(1);

      return true;
   }

   // It's the object's responsibility to delete pObj within the execute_task() method, if needed!
   bool task_pool::queue_task(executable_task* pObj, uint64 data, void* pData_ptr)
   {
      LZHAM_ASSERT(m_num_threads);
      LZHAM_ASSERT(pObj);

      task tsk;
      tsk.m_pObj = pObj;
      tsk.m_data = data;
      tsk.m_pData_ptr = pData_ptr;
      tsk.m_flags = cTaskFlagObject;

      if (!m_task_stack.try_push(tsk))
         return false;

      atomic_increment32(&m_num_outstanding_tasks);

      m_tasks_available.release(1);

      return true;
   }

   void task_pool::process_task(task& tsk)
   {
      if (tsk.m_flags & cTaskFlagObject)
         tsk.m_pObj->execute_task(tsk.m_data, tsk.m_pData_ptr);
      else
         tsk.m_callback(tsk.m_data, tsk.m_pData_ptr);

      atomic_decrement32(&m_num_outstanding_tasks);
   }

   void task_pool::join()
   {
      while (atomic_add32(&m_num_outstanding_tasks, 0) > 0)
      {
         task tsk;
         if (m_task_stack.pop(tsk))
         {
            process_task(tsk);
         }
         else
         {
            lzham_sleep(1);
         }
      }
   }

   unsigned __stdcall task_pool::thread_func(void* pContext)
   {
      task_pool* pPool = static_cast<task_pool*>(pContext);

      for ( ; ; )
      {
         if (!pPool->m_tasks_available.wait())
            break;

         if (pPool->m_exit_flag)
            break;

         task tsk;
         if (pPool->m_task_stack.pop(tsk))
         {
            pPool->process_task(tsk);
         }
      }

      _endthreadex(0);
      return 0;
   }

   static uint g_num_processors;

   uint lzham_get_max_helper_threads()
   {
      if (!g_num_processors)
      {
         SYSTEM_INFO system_info;
         GetSystemInfo(&system_info);
         g_num_processors = system_info.dwNumberOfProcessors;
      }

      if (g_num_processors > 1)
      {
         // use all CPU's
         return LZHAM_MIN(task_pool::cMaxThreads, g_num_processors - 1);
      }

      return 0;
   }

} // namespace lzham

#endif // LZHAM_USE_WIN32_API
