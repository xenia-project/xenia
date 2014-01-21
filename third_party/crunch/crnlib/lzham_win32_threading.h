// File: lzham_task_pool_win32.h
// See Copyright Notice and license at the end of include/lzham.h
#pragma once

#if LZHAM_USE_WIN32_API

#if LZHAM_NO_ATOMICS
#error No atomic operations defined in lzham_platform.h!
#endif

namespace lzham
{
   class semaphore
   {
      LZHAM_NO_COPY_OR_ASSIGNMENT_OP(semaphore);

   public:
      semaphore(long initialCount = 0, long maximumCount = 1, const char* pName = NULL)
      {
         m_handle = CreateSemaphoreA(NULL, initialCount, maximumCount, pName);
         if (NULL == m_handle)
         {
            LZHAM_FAIL("semaphore: CreateSemaphore() failed");
         }
      }

      ~semaphore()
      {
         if (m_handle)
         {
            CloseHandle(m_handle);
            m_handle = NULL;
         }
      }

      inline HANDLE get_handle(void) const { return m_handle; }

      void release(long releaseCount = 1)
      {
         if (0 == ReleaseSemaphore(m_handle, releaseCount, NULL))
         {
            LZHAM_FAIL("semaphore: ReleaseSemaphore() failed");
         }
      }

      bool wait(uint32 milliseconds = UINT32_MAX)
      {
         LZHAM_ASSUME(INFINITE == UINT32_MAX);

         DWORD result = WaitForSingleObject(m_handle, milliseconds);

         if (WAIT_FAILED == result)
         {
            LZHAM_FAIL("semaphore: WaitForSingleObject() failed");
         }

         return WAIT_OBJECT_0 == result;
      }

   private:
      HANDLE m_handle;
   };

   template<typename T>
   class tsstack
   {
   public:
      inline tsstack(bool use_freelist = true) :
         m_use_freelist(use_freelist)
      {
         LZHAM_VERIFY(((ptr_bits_t)this & (LZHAM_GET_ALIGNMENT(tsstack) - 1)) == 0);
         InitializeSListHead(&m_stack_head);
         InitializeSListHead(&m_freelist_head);
      }

      inline ~tsstack()
      {
         clear();
      }

      inline void clear()
      {
         for ( ; ; )
         {
            node* pNode = (node*)InterlockedPopEntrySList(&m_stack_head);
            if (!pNode)
               break;

            LZHAM_MEMORY_IMPORT_BARRIER

            helpers::destruct(&pNode->m_obj);

            lzham_free(pNode);
         }

         flush_freelist();
      }

      inline void flush_freelist()
      {
         if (!m_use_freelist)
            return;

         for ( ; ; )
         {
            node* pNode = (node*)InterlockedPopEntrySList(&m_freelist_head);
            if (!pNode)
               break;

            LZHAM_MEMORY_IMPORT_BARRIER

            lzham_free(pNode);
         }
      }

      inline bool try_push(const T& obj)
      {
         node* pNode = alloc_node();
         if (!pNode)
            return false;

         helpers::construct(&pNode->m_obj, obj);

         LZHAM_MEMORY_EXPORT_BARRIER

         InterlockedPushEntrySList(&m_stack_head, &pNode->m_slist_entry);

         return true;
      }

      inline bool pop(T& obj)
      {
         node* pNode = (node*)InterlockedPopEntrySList(&m_stack_head);
         if (!pNode)
            return false;

         LZHAM_MEMORY_IMPORT_BARRIER

         obj = pNode->m_obj;

         helpers::destruct(&pNode->m_obj);

         free_node(pNode);

         return true;
      }

   private:
      SLIST_HEADER m_stack_head;
      SLIST_HEADER m_freelist_head;

      struct node
      {
         SLIST_ENTRY m_slist_entry;
         T m_obj;
      };

      bool m_use_freelist;

      inline node* alloc_node()
      {
         node* pNode = m_use_freelist ? (node*)InterlockedPopEntrySList(&m_freelist_head) : NULL;

         if (!pNode)
            pNode = (node*)lzham_malloc(sizeof(node));

         return pNode;
      }

      inline void free_node(node* pNode)
      {
         if (m_use_freelist)
            InterlockedPushEntrySList(&m_freelist_head, &pNode->m_slist_entry);
         else
            lzham_free(pNode);
      }
   };

   class task_pool
   {
   public:
      task_pool();
      task_pool(uint num_threads);
      ~task_pool();

      enum { cMaxThreads = 16 };
      bool init(uint num_threads);
      void deinit();

      inline uint get_num_threads() const { return m_num_threads; }
      inline uint get_num_outstanding_tasks() const { return m_num_outstanding_tasks; }

      // C-style task callback
      typedef void (*task_callback_func)(uint64 data, void* pData_ptr);
      bool queue_task(task_callback_func pFunc, uint64 data = 0, void* pData_ptr = NULL);

      class executable_task
      {
      public:
         virtual void execute_task(uint64 data, void* pData_ptr) = 0;
      };

      // It's the caller's responsibility to delete pObj within the execute_task() method, if needed!
      bool queue_task(executable_task* pObj, uint64 data = 0, void* pData_ptr = NULL);

      template<typename S, typename T>
      inline bool queue_object_task(S* pObject, T pObject_method, uint64 data = 0, void* pData_ptr = NULL);

      template<typename S, typename T>
      inline bool queue_multiple_object_tasks(S* pObject, T pObject_method, uint64 first_data, uint num_tasks, void* pData_ptr = NULL);

      void join();

   private:
      struct task
      {
         //inline task() : m_data(0), m_pData_ptr(NULL), m_pObj(NULL), m_flags(0) { }

         uint64 m_data;
         void* m_pData_ptr;

         union
         {
            task_callback_func m_callback;
            executable_task* m_pObj;
         };

         uint m_flags;
      };

      tsstack<task> m_task_stack;

      uint m_num_threads;
      HANDLE m_threads[cMaxThreads];

      semaphore m_tasks_available;

      enum task_flags
      {
         cTaskFlagObject = 1
      };

      volatile atomic32_t m_num_outstanding_tasks;
      volatile atomic32_t m_exit_flag;

      void process_task(task& tsk);

      static unsigned __stdcall thread_func(void* pContext);
   };

   enum object_task_flags
   {
      cObjectTaskFlagDefault = 0,
      cObjectTaskFlagDeleteAfterExecution = 1
   };

   template<typename T>
   class object_task : public task_pool::executable_task
   {
   public:
      object_task(uint flags = cObjectTaskFlagDefault) :
         m_pObject(NULL),
         m_pMethod(NULL),
         m_flags(flags)
      {
      }

      typedef void (T::*object_method_ptr)(uint64 data, void* pData_ptr);

      object_task(T* pObject, object_method_ptr pMethod, uint flags = cObjectTaskFlagDefault) :
         m_pObject(pObject),
         m_pMethod(pMethod),
         m_flags(flags)
      {
         LZHAM_ASSERT(pObject && pMethod);
      }

      void init(T* pObject, object_method_ptr pMethod, uint flags = cObjectTaskFlagDefault)
      {
         LZHAM_ASSERT(pObject && pMethod);

         m_pObject = pObject;
         m_pMethod = pMethod;
         m_flags = flags;
      }

      T* get_object() const { return m_pObject; }
      object_method_ptr get_method() const { return m_pMethod; }

      virtual void execute_task(uint64 data, void* pData_ptr)
      {
         (m_pObject->*m_pMethod)(data, pData_ptr);

         if (m_flags & cObjectTaskFlagDeleteAfterExecution)
            lzham_delete(this);
      }

   protected:
      T* m_pObject;

      object_method_ptr m_pMethod;

      uint m_flags;
   };

   template<typename S, typename T>
   inline bool task_pool::queue_object_task(S* pObject, T pObject_method, uint64 data, void* pData_ptr)
   {
      object_task<S> *pTask = lzham_new< object_task<S> >(pObject, pObject_method, cObjectTaskFlagDeleteAfterExecution);
      if (!pTask)
         return false;
      return queue_task(pTask, data, pData_ptr);
   }

   template<typename S, typename T>
   inline bool task_pool::queue_multiple_object_tasks(S* pObject, T pObject_method, uint64 first_data, uint num_tasks, void* pData_ptr)
   {
      LZHAM_ASSERT(m_num_threads);
      LZHAM_ASSERT(pObject);
      LZHAM_ASSERT(num_tasks);
      if (!num_tasks)
         return true;

      bool status = true;

      uint i;
      for (i = 0; i < num_tasks; i++)
      {
         task tsk;

         tsk.m_pObj = lzham_new< object_task<S> >(pObject, pObject_method, cObjectTaskFlagDeleteAfterExecution);
         if (!tsk.m_pObj)
         {
            status = false;
            break;
         }

         tsk.m_data = first_data + i;
         tsk.m_pData_ptr = pData_ptr;
         tsk.m_flags = cTaskFlagObject;

         if (!m_task_stack.try_push(tsk))
         {
            status = false;
            break;
         }
      }

      if (i)
      {
         atomic_add32(&m_num_outstanding_tasks, i);

         m_tasks_available.release(i);
      }

      return status;
   }

   inline void lzham_sleep(unsigned int milliseconds)
   {
      Sleep(milliseconds);
   }

   uint lzham_get_max_helper_threads();

} // namespace lzham

#endif // LZHAM_USE_WIN32_API
