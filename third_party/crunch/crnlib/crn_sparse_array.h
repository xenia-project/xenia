// File: crn_sparse_array.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   template<typename T, uint Log2N>
   class sparse_array_traits
   {
   public:
      static inline void* alloc_space(uint size)
      {
         return crnlib_malloc(size);
      }

      static inline void free_space(void* p)
      {
         crnlib_free(p);
      }

      static inline void construct_group(T* p)
      {
         scalar_type<T>::construct_array(p, 1U << Log2N);
      }

      static inline void destruct_group(T* p)
      {
         scalar_type<T>::destruct_array(p, 1U << Log2N);
      }

      static inline void construct_element(T* p)
      {
         scalar_type<T>::construct(p);
      }

      static inline void destruct_element(T* p)
      {
         scalar_type<T>::destruct(p);
      }

      static inline void copy_group(T* pDst, const T* pSrc)
      {
         for (uint j = 0; j < (1U << Log2N); j++)
            pDst[j] = pSrc[j];
      }
   };

   template<typename T, uint Log2N = 5, template <typename, uint> class Traits = sparse_array_traits>
   class sparse_array : public Traits<T, Log2N>
   {
   public:
      enum { N = 1U << Log2N };

      inline sparse_array() : m_size(0), m_num_active_groups(0)
      {
         init_default();
      }

      inline sparse_array(uint size) : m_size(0), m_num_active_groups(0)
      {
         init_default();

         resize(size);
      }

      inline sparse_array(const sparse_array& other) : m_size(0), m_num_active_groups(0)
      {
         init_default();

         *this = other;
      }

      inline ~sparse_array()
      {
         for (uint i = 0; (i < m_groups.size()) && m_num_active_groups; i++)
            free_group(m_groups[i]);

         deinit_default();
      }

      bool assign(const sparse_array& other)
      {
         if (this == &other)
            return true;

         if (!try_resize(other.size()))
            return false;

         for (uint i = 0; i < other.m_groups.size(); i++)
         {
            const T* p = other.m_groups[i];

            T* q = m_groups[i];

            if (p)
            {
               if (!q)
               {
                  q = alloc_group(true);
                  if (!q)
                     return false;

                  m_groups[i] = q;
               }

               copy_group(q, p);
            }
            else if (q)
            {
               free_group(q);
               m_groups[i] = NULL;
            }
         }

         return true;
      }

      sparse_array& operator= (const sparse_array& other)
      {
         if (!assign(other))
         {
            CRNLIB_FAIL("Out of memory");
         }

         return *this;
      }

      bool operator== (const sparse_array& other) const
      {
         if (m_size != other.m_size)
            return false;

         for (uint i = 0; i < m_size; i++)
            if (!((*this)[i] == other[i]))
               return false;

         return true;
      }

      bool operator< (const sparse_array& rhs) const
      {
         const uint min_size = math::minimum(m_size, rhs.m_size);

         uint i;
         for (i = 0; i < min_size; i++)
            if (!((*this)[i] == rhs[i]))
               break;

         if (i < min_size)
            return (*this)[i] < rhs[i];

         return m_size < rhs.m_size;
      }

      void clear()
      {
         if (m_groups.size())
         {
            for (uint i = 0; (i < m_groups.size()) && m_num_active_groups; i++)
               free_group(m_groups[i]);

            m_groups.clear();
         }

         m_size = 0;

         CRNLIB_ASSERT(!m_num_active_groups);
      }

      bool try_resize(uint size)
      {
         if (m_size == size)
            return true;

         const uint new_num_groups = (size + N - 1) >> Log2N;
         if (new_num_groups != m_groups.size())
         {
            for (uint i = new_num_groups; i < m_groups.size(); i++)
               free_group(m_groups[i]);

            if (!m_groups.try_resize(new_num_groups))
               return false;
         }

         m_size = size;
         return true;
      }

      void resize(uint size)
      {
         if (!try_resize(size))
         {
            CRNLIB_FAIL("Out of memory");
         }
      }

      inline uint size() const { return m_size; }
      inline bool empty() const { return 0 == m_size; }

      inline uint capacity() const { return m_groups.size(); }

      inline const T& operator[] (uint i) const
      {
         CRNLIB_ASSERT(i < m_size);
         const T* p = m_groups[i >> Log2N];
         const void *t = m_default;
         return p ? p[i & (N - 1)] : *reinterpret_cast<const T*>(t);
      }

      inline const T* get(uint i) const
      {
         CRNLIB_ASSERT(i < m_size);
         const T* p = m_groups[i >> Log2N];
         return p ? &p[i & (N - 1)] : NULL;
      }

      inline T* get(uint i)
      {
         CRNLIB_ASSERT(i < m_size);
         T* p = m_groups[i >> Log2N];
         return p ? &p[i & (N - 1)] : NULL;
      }

      inline bool is_present(uint i) const
      {
         CRNLIB_ASSERT(i < m_size);
         return m_groups[i >> Log2N] != NULL;
      }

      inline uint get_num_groups() const { return m_groups.size(); }

      inline const T* get_group(uint group_index) const
      {
         return m_groups[group_index];
      }

      inline T* get_group(uint group_index)
      {
         return m_groups[group_index];
      }

      inline uint get_group_size() const
      {
         return N;
      }

      inline T* ensure_valid(uint index)
      {
         CRNLIB_ASSERT(index <= m_size);

         const uint group_index = index >> Log2N;

         if (group_index >= m_groups.size())
         {
            T* p = alloc_group(true);
            if (!p)
               return NULL;

            if (!m_groups.try_push_back(p))
            {
               free_group(p);
               return NULL;
            }
         }

         T* p = m_groups[group_index];
         if (!p)
         {
            p = alloc_group(true);
            if (!p)
               return NULL;

            m_groups[group_index] = p;
         }

         m_size = math::maximum(index + 1, m_size);

         return p + (index & (N - 1));
      }

      inline bool set(uint index, const T& obj)
      {
         T* p = ensure_valid(index);
         if (!p)
            return false;

         *p = obj;

         return true;
      }

      inline void push_back(const T& obj)
      {
         if (!set(m_size, obj))
         {
            CRNLIB_FAIL("Out of memory");
         }
      }

      inline bool try_push_back(const T& obj)
      {
         return set(m_size, obj);
      }

      inline void pop_back()
      {
         CRNLIB_ASSERT(m_size);
         if (m_size)
            resize(m_size - 1);
      }

      inline void unset_range(uint start, uint num)
      {
         if (!num)
            return;

         CRNLIB_ASSERT((start + num) <= capacity());

         const uint num_to_skip = math::minimum(math::get_align_up_value_delta(start, N), num);
         num -= num_to_skip;

         const uint first_group = (start + num_to_skip) >> Log2N;
         const uint num_groups = num >> Log2N;

         for (uint i = 0; i < num_groups; i++)
         {
            T* p = m_groups[first_group + i];
            if (p)
            {
               free_group(p);
               m_groups[i] = NULL;
            }
         }
      }

      inline void unset_all()
      {
         unset_range(0, m_groups.size() << Log2N);
      }

      inline void swap(sparse_array& other)
      {
         utils::swap(m_size, other.m_size);
         m_groups.swap(other.m_groups);
         utils::swap(m_num_active_groups, other.m_num_active_groups);
      }

   private:
      uint              m_size;
      uint              m_num_active_groups;

      crnlib::vector<T*> m_groups;

      uint64            m_default[(sizeof(T) + sizeof(uint64) - 1) / sizeof(uint64)];

      inline T* alloc_group(bool nofail = false)
      {
         T* p = static_cast<T*>(alloc_space(N * sizeof(T)));

         if (!p)
         {
            if (nofail)
               return NULL;

            CRNLIB_FAIL("Out of memory");
         }

         construct_group(p);

         m_num_active_groups++;

         return p;
      }

      inline void free_group(T* p)
      {
         if (p)
         {
            CRNLIB_ASSERT(m_num_active_groups);
            m_num_active_groups--;

            destruct_group(p);

            free_space(p);
         }
      }

      inline void init_default()
      {
         construct_element(reinterpret_cast<T*>(m_default));
      }

      inline void deinit_default()
      {
         destruct_element(reinterpret_cast<T*>(m_default));
      }
   };

} // namespace crnlib
