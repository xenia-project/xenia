// File: crn_vector.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   struct elemental_vector
   {
      void* m_p;
      uint m_size;
      uint m_capacity;

      typedef void (*object_mover)(void* pDst, void* pSrc, uint num);

      bool increase_capacity(uint min_new_capacity, bool grow_hint, uint element_size, object_mover pRelocate, bool nofail);
   };

   template<typename T>
   class vector : public helpers::rel_ops< vector<T> >
   {
   public:
      typedef T*              iterator;
      typedef const T*        const_iterator;
      typedef T               value_type;
      typedef T&              reference;
      typedef const T&        const_reference;
      typedef T*              pointer;
      typedef const T*        const_pointer;

      inline vector() :
         m_p(NULL),
         m_size(0),
         m_capacity(0)
      {
      }

      inline vector(uint n, const T& init) :
         m_p(NULL),
         m_size(0),
         m_capacity(0)
      {
         increase_capacity(n, false);
         helpers::construct_array(m_p, n, init);
         m_size = n;
      }

      inline vector(const vector& other) :
         m_p(NULL),
         m_size(0),
         m_capacity(0)
      {
         increase_capacity(other.m_size, false);

         m_size = other.m_size;

         if (CRNLIB_IS_BITWISE_COPYABLE(T))
            memcpy(m_p, other.m_p, m_size * sizeof(T));
         else
         {
            T* pDst = m_p;
            const T* pSrc = other.m_p;
            for (uint i = m_size; i > 0; i--)
               helpers::construct(pDst++, *pSrc++);
         }
      }

      inline explicit vector(uint size) :
         m_p(NULL),
         m_size(0),
         m_capacity(0)
      {
         resize(size);
      }

      inline ~vector()
      {
         if (m_p)
         {
            scalar_type<T>::destruct_array(m_p, m_size);
            crnlib_free(m_p);
         }
      }

      inline vector& operator= (const vector& other)
      {
         if (this == &other)
            return *this;

         if (m_capacity >= other.m_size)
            resize(0);
         else
         {
            clear();
            increase_capacity(other.m_size, false);
         }

         if (CRNLIB_IS_BITWISE_COPYABLE(T))
            memcpy(m_p, other.m_p, other.m_size * sizeof(T));
         else
         {
            T* pDst = m_p;
            const T* pSrc = other.m_p;
            for (uint i = other.m_size; i > 0; i--)
               helpers::construct(pDst++, *pSrc++);
         }

         m_size = other.m_size;

         return *this;
      }

      inline const   T* begin() const  { return m_p; }
                     T* begin()        { return m_p; }

      inline const   T* end() const  { return m_p + m_size; }
                     T* end()        { return m_p + m_size; }

      inline bool empty() const { return !m_size; }
      inline uint size() const { return m_size; }
      inline uint size_in_bytes() const { return m_size * sizeof(T); }
      inline uint capacity() const { return m_capacity; }

      // operator[] will assert on out of range indices, but in final builds there is (and will never be) any range checking on this method.
      inline const T& operator[] (uint i) const  { CRNLIB_ASSERT(i < m_size); return m_p[i]; }
      inline       T& operator[] (uint i)        { CRNLIB_ASSERT(i < m_size); return m_p[i]; }

      // at() always includes range checking, even in final builds, unlike operator [].
      // The first element is returned if the index is out of range.
      inline const T& at(uint i) const  { CRNLIB_ASSERT(i < m_size); return (i >= m_size) ? m_p[0] : m_p[i]; }
      inline       T& at(uint i)        { CRNLIB_ASSERT(i < m_size); return (i >= m_size) ? m_p[0] : m_p[i]; }

      inline const T& front() const  { CRNLIB_ASSERT(m_size); return m_p[0]; }
      inline       T& front()        { CRNLIB_ASSERT(m_size); return m_p[0]; }

      inline const T& back() const  { CRNLIB_ASSERT(m_size); return m_p[m_size - 1]; }
      inline       T& back()        { CRNLIB_ASSERT(m_size); return m_p[m_size - 1]; }

      inline const   T* get_ptr() const   { return m_p; }
      inline         T* get_ptr()         { return m_p; }

      // clear() sets the container to empty, then frees the allocated block.
      inline void clear()
      {
         if (m_p)
         {
            scalar_type<T>::destruct_array(m_p, m_size);
            crnlib_free(m_p);
            m_p = NULL;
            m_size = 0;
            m_capacity = 0;
         }
      }
      
      inline void clear_no_destruction()
      {
         if (m_p)
         {
            crnlib_free(m_p);
            m_p = NULL;
            m_size = 0;
            m_capacity = 0;
         }
      }

      inline void reserve(uint new_capacity)
      {
         if (new_capacity > m_capacity)
            increase_capacity(new_capacity, false);
         else if (new_capacity < m_capacity)
         {
            // Must work around the lack of a "decrease_capacity()" method.
            // This case is rare enough in practice that it's probably not worth implementing an optimized in-place resize.
            vector tmp;
            tmp.increase_capacity(math::maximum(m_size, new_capacity), false);
            tmp = *this;
            swap(tmp);
         }
      }

      inline bool try_reserve(uint new_capacity)
      {
         return increase_capacity(new_capacity, true, true);
      }

      // resize(0) sets the container to empty, but does not free the allocated block.
      inline void resize(uint new_size, bool grow_hint = false)
      {
         if (m_size != new_size)
         {
            if (new_size < m_size)
               scalar_type<T>::destruct_array(m_p + new_size, m_size - new_size);
            else
            {
               if (new_size > m_capacity)
                  increase_capacity(new_size, (new_size == (m_size + 1)) || grow_hint);

               scalar_type<T>::construct_array(m_p + m_size, new_size - m_size);
            }

            m_size = new_size;
         }
      }

      inline bool try_resize(uint new_size, bool grow_hint = false)
      {
         if (m_size != new_size)
         {
            if (new_size < m_size)
               scalar_type<T>::destruct_array(m_p + new_size, m_size - new_size);
            else
            {
               if (new_size > m_capacity)
               {
                  if (!increase_capacity(new_size, (new_size == (m_size + 1)) || grow_hint, true))
                     return false;
               }

               scalar_type<T>::construct_array(m_p + m_size, new_size - m_size);
            }

            m_size = new_size;
         }

         return true;
      }

      // If size >= capacity/2, reset() sets the container's size to 0 but doesn't free the allocated block (because the container may be similarly loaded in the future).
      // Otherwise it blows away the allocated block. See http://www.codercorner.com/blog/?p=494
      inline void reset()
      {
         if (m_size >= (m_capacity >> 1))
            resize(0);
         else
            clear();
      }

      inline T* enlarge(uint i)
      {
         uint cur_size = m_size;
         resize(cur_size + i, true);
         return get_ptr() + cur_size;
      }

      inline T* try_enlarge(uint i)
      {
         uint cur_size = m_size;
         if (!try_resize(cur_size + i, true))
            return NULL;
         return get_ptr() + cur_size;
      }

      inline void push_back(const T& obj)
      {
         CRNLIB_ASSERT(!m_p || (&obj < m_p) || (&obj >= (m_p + m_size)));

         if (m_size >= m_capacity)
            increase_capacity(m_size + 1, true);

         scalar_type<T>::construct(m_p + m_size, obj);
         m_size++;
      }

      inline bool try_push_back(const T& obj)
      {
         CRNLIB_ASSERT(!m_p || (&obj < m_p) || (&obj >= (m_p + m_size)));

         if (m_size >= m_capacity)
         {
            if (!increase_capacity(m_size + 1, true, true))
               return false;
         }

         scalar_type<T>::construct(m_p + m_size, obj);
         m_size++;

         return true;
      }

      inline void push_back_value(T obj)
      {
         if (m_size >= m_capacity)
            increase_capacity(m_size + 1, true);

         scalar_type<T>::construct(m_p + m_size, obj);
         m_size++;
      }

      inline void pop_back()
      {
         CRNLIB_ASSERT(m_size);

         if (m_size)
         {
            m_size--;
            scalar_type<T>::destruct(&m_p[m_size]);
         }
      }

      inline void insert(uint index, const T* p, uint n)
      {
         CRNLIB_ASSERT(index <= m_size);
         if (!n)
            return;

         const uint orig_size = m_size;
         resize(m_size + n, true);

         const uint num_to_move = orig_size - index;

         if (CRNLIB_IS_BITWISE_COPYABLE(T))
         {
            // This overwrites the destination object bits, but bitwise copyable means we don't need to worry about destruction.
            memmove(m_p + index + n, m_p + index, sizeof(T) * num_to_move);
         }
         else
         {
            const T* pSrc = m_p + orig_size - 1;
            T* pDst = const_cast<T*>(pSrc) + n;

            for (uint i = 0; i < num_to_move; i++)
            {
               CRNLIB_ASSERT((pDst - m_p) < (int)m_size);
               *pDst-- = *pSrc--;
            }
         }

         T* pDst = m_p + index;

         if (CRNLIB_IS_BITWISE_COPYABLE(T))
         {
            // This copies in the new bits, overwriting the existing objects, which is OK for copyable types that don't need destruction.
            memcpy(pDst, p, sizeof(T) * n);
         }
         else
         {
            for (uint i = 0; i < n; i++)
            {
               CRNLIB_ASSERT((pDst - m_p) < (int)m_size);
               *pDst++ = *p++;
            }
         }
      }

      // push_front() isn't going to be very fast - it's only here for usability.
      inline void push_front(const T& obj)
      {
         insert(0, &obj, 1);
      }

      vector& append(const vector& other)
      {
         if (other.m_size)
            insert(m_size, &other[0], other.m_size);
         return *this;
      }

      vector& append(const T* p, uint n)
      {
         if (n)
            insert(m_size, p, n);
         return *this;
      }

      inline void erase(uint start, uint n)
      {
         CRNLIB_ASSERT((start + n) <= m_size);
         if ((start + n) > m_size)
            return;

         if (!n)
            return;

         const uint num_to_move = m_size - (start + n);

         T* pDst = m_p + start;
         
         const T* pSrc = m_p + start + n;

         if (CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(T))
         {
            // This test is overly cautious.
            if ((!CRNLIB_IS_BITWISE_COPYABLE(T)) || (CRNLIB_HAS_DESTRUCTOR(T)))
            {
               // Type has been marked explictly as bitwise movable, which means we can move them around but they may need to be destructed.
               // First destroy the erased objects.
               scalar_type<T>::destruct_array(pDst, n);
            }

            // Copy "down" the objects to preserve, filling in the empty slots.
            memmove(pDst, pSrc, num_to_move * sizeof(T));
         }
         else
         {
            // Type is not bitwise copyable or movable. 
            // Move them down one at a time by using the equals operator, and destroying anything that's left over at the end.
            T* pDst_end = pDst + num_to_move;
            while (pDst != pDst_end)
               *pDst++ = *pSrc++;

            scalar_type<T>::destruct_array(pDst_end, n);
         }

         m_size -= n;
      }

      inline void erase(uint index)
      {
         erase(index, 1);
      }

      inline void erase(T* p)
      {
         CRNLIB_ASSERT((p >= m_p) && (p < (m_p + m_size)));
         erase(static_cast<uint>(p - m_p));
      }

      void erase_unordered(uint index)
      {
         CRNLIB_ASSERT(index < m_size);

         if ((index + 1) < m_size)
            (*this)[index] = back();

         pop_back();
      }

      inline bool operator== (const vector& rhs) const
      {
         if (m_size != rhs.m_size)
            return false;
         else if (m_size)
         {
            if (scalar_type<T>::cFlag)
               return memcmp(m_p, rhs.m_p, sizeof(T) * m_size) == 0;
            else
            {
               const T* pSrc = m_p;
               const T* pDst = rhs.m_p;
               for (uint i = m_size; i; i--)
                  if (!(*pSrc++ == *pDst++))
                     return false;
            }
         }

         return true;
      }

      inline bool operator< (const vector& rhs) const
      {
         const uint min_size = math::minimum(m_size, rhs.m_size);

         const T* pSrc = m_p;
         const T* pSrc_end = m_p + min_size;
         const T* pDst = rhs.m_p;

         while ((pSrc < pSrc_end) && (*pSrc == *pDst))
         {
            pSrc++;
            pDst++;
         }

         if (pSrc < pSrc_end)
            return *pSrc < *pDst;

         return m_size < rhs.m_size;
      }

      inline void swap(vector& other)
      {
         utils::swap(m_p, other.m_p);
         utils::swap(m_size, other.m_size);
         utils::swap(m_capacity, other.m_capacity);
      }

      inline void sort()
      {
         std::sort(begin(), end());
      }

      inline void unique()
      {
         if (!empty())
         {
            sort();

            resize(std::unique(begin(), end()) - begin());
         }
      }

      inline void reverse()
      {
         uint j = m_size >> 1;
         for (uint i = 0; i < j; i++)
            utils::swap(m_p[i], m_p[m_size - 1 - i]);
      }

      inline int find(const T& key) const
      {
         const T* p = m_p;
         const T* p_end = m_p + m_size;

         uint index = 0;

         while (p != p_end)
         {
            if (key == *p)
               return index;

            p++;
            index++;
         }

         return cInvalidIndex;
      }

      inline int find_sorted(const T& key) const
      {
         if (m_size)
         {
            // Uniform binary search - Knuth Algorithm 6.2.1 U, unrolled twice.
            int i = ((m_size + 1) >> 1) - 1;
            int m = m_size;

            for ( ; ; )
            {
               CRNLIB_ASSERT_OPEN_RANGE(i, 0, (int)m_size);
               const T* pKey_i = m_p + i;
               int cmp = key < *pKey_i;
               if ((!cmp) && (key == *pKey_i)) return i;
               m >>= 1;
               if (!m) break;
               cmp = -cmp;
               i += (((m + 1) >> 1) ^ cmp) - cmp;

               CRNLIB_ASSERT_OPEN_RANGE(i, 0, (int)m_size);
               pKey_i = m_p + i;
               cmp = key < *pKey_i;
               if ((!cmp) && (key == *pKey_i)) return i;
               m >>= 1;
               if (!m) break;
               cmp = -cmp;
               i += (((m + 1) >> 1) ^ cmp) - cmp;
            }
         }

         return cInvalidIndex;
      }

      template<typename Q>
      inline int find_sorted(const T& key, Q less_than) const
      {
         if (m_size)
         {
            // Uniform binary search - Knuth Algorithm 6.2.1 U, unrolled twice.
            int i = ((m_size + 1) >> 1) - 1;
            int m = m_size;

            for ( ; ; )
            {
               CRNLIB_ASSERT_OPEN_RANGE(i, 0, (int)m_size);
               const T* pKey_i = m_p + i;
               int cmp = less_than(key, *pKey_i);
               if ((!cmp) && (!less_than(*pKey_i, key))) return i;
               m >>= 1;
               if (!m) break;
               cmp = -cmp;
               i += (((m + 1) >> 1) ^ cmp) - cmp;

               CRNLIB_ASSERT_OPEN_RANGE(i, 0, (int)m_size);
               pKey_i = m_p + i;
               cmp = less_than(key, *pKey_i);
               if ((!cmp) && (!less_than(*pKey_i, key))) return i;
               m >>= 1;
               if (!m) break;
               cmp = -cmp;
               i += (((m + 1) >> 1) ^ cmp) - cmp;
            }
         }

         return cInvalidIndex;
      }

      inline uint count_occurences(const T& key) const
      {
         uint c = 0;

         const T* p = m_p;
         const T* p_end = m_p + m_size;

         while (p != p_end)
         {
            if (key == *p)
               c++;

            p++;
         }

         return c;
      }

      inline void set_all(const T& o)
      {
         if ((sizeof(T) == 1) && (scalar_type<T>::cFlag))
            memset(m_p, *reinterpret_cast<const uint8*>(&o), m_size);
         else
         {
            T* pDst = m_p;
            T* pDst_end = pDst + m_size;
            while (pDst != pDst_end)
               *pDst++ = o;
         }
      }

      // Caller assumes ownership of the heap block associated with the container. Container is cleared.
      inline void *assume_ownership()
      {
         T* p = m_p;
         m_p = NULL;
         m_size = 0;
         m_capacity = 0;
         return p;
      }

      // Caller is granting ownership of the indicated heap block.
      // Block must have size constructed elements, and have enough room for capacity elements.
      inline bool grant_ownership(T* p, uint size, uint capacity)
      {
         // To to prevent the caller from obviously shooting themselves in the foot.
         if (((p + capacity) > m_p) && (p < (m_p + m_capacity)))
         {
            // Can grant ownership of a block inside the container itself!
            CRNLIB_ASSERT(0);
            return false;
         }
         
         if (size > capacity)
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         if (!p)
         {
            if (capacity)
            {
               CRNLIB_ASSERT(0);
               return false;
            }
         }
         else if (!capacity)
         {
            CRNLIB_ASSERT(0);
            return false;
         }
         
         clear();
         m_p = p;
         m_size = size;
         m_capacity = capacity;
         return true;
      }

   private:
      T*       m_p;
      uint     m_size;
      uint     m_capacity;

      template<typename Q> struct is_vector { enum { cFlag = false }; };
      template<typename Q> struct is_vector< vector<Q> > { enum { cFlag = true }; };

      static void object_mover(void* pDst_void, void* pSrc_void, uint num)
      {
         T* pSrc = static_cast<T*>(pSrc_void);
         T* const pSrc_end = pSrc + num;
         T* pDst = static_cast<T*>(pDst_void);

         while (pSrc != pSrc_end)
         {
            // placement new
            new (static_cast<void*>(pDst)) T(*pSrc);
            pSrc->~T();
            ++pSrc;
            ++pDst;
         }
      }
      
      inline bool increase_capacity(uint min_new_capacity, bool grow_hint, bool nofail = false)
      {
         return reinterpret_cast<elemental_vector*>(this)->increase_capacity(
            min_new_capacity, grow_hint, sizeof(T),
            (CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(T) || (is_vector<T>::cFlag)) ? NULL : object_mover, nofail);
      }
   };

   typedef crnlib::vector<uint8> uint8_vec;

   template<typename T> struct bitwise_movable< vector<T> > { enum { cFlag = true }; };

   extern void vector_test();

   template<typename T>
   inline void swap(vector<T>& a, vector<T>& b)
   {
      a.swap(b);
   }

} // namespace crnlib

