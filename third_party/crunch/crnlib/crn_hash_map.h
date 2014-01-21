// File: crn_hash_map.h
// See Copyright Notice and license at the end of inc/crnlib.h
//
// Notes:
// stl-like hash map/hash set, with predictable performance across platforms/compilers/C run times/etc.
// Hash function ref: http://www.brpreiss.com/books/opus4/html/page215.html
// Compared for performance against VC9's std::hash_map.
// Linear probing, auto resizes on ~50% load factor.
// Uses Knuth's multiplicative method (Fibonacci hashing).
#pragma once
#include "crn_sparse_array.h"
#include "crn_sparse_bit_array.h"
#include "crn_hash.h"

namespace crnlib
{
   template <typename T>
   struct hasher
   {
      inline size_t operator() (const T& key) const { return static_cast<size_t>(key); }
   };

   template <typename T>
   struct bit_hasher
   {
      inline size_t operator() (const T& key) const { return static_cast<size_t>(fast_hash(&key, sizeof(key))); }
   };

   template <typename T>
   struct equal_to
   {
      inline bool operator()(const T& a, const T& b) const { return a == b;  }
   };

   // Important: The Hasher and Equals objects must be bitwise movable!
   template<typename Key, typename Value = empty_type, typename Hasher = hasher<Key>, typename Equals = equal_to<Key> >
   class hash_map
   {
      friend class iterator;
      friend class const_iterator;

      enum state
      {
         cStateInvalid = 0,
         cStateValid = 1
      };

      enum
      {
         cMinHashSize = 4U
      };

   public:
      typedef hash_map<Key, Value, Hasher, Equals> hash_map_type;
      typedef std::pair<Key, Value> value_type;
      typedef Key                   key_type;
      typedef Value                 referent_type;
      typedef Hasher                hasher_type;
      typedef Equals                equals_type;

      hash_map() :
         m_hash_shift(32), m_num_valid(0), m_grow_threshold(0)
      {
      }

      hash_map(const hash_map& other) :
         m_values(other.m_values),
         m_hash_shift(other.m_hash_shift),
         m_hasher(other.m_hasher),
         m_equals(other.m_equals),
         m_num_valid(other.m_num_valid),
         m_grow_threshold(other.m_grow_threshold)
      {
      }

      hash_map& operator= (const hash_map& other)
      {
         if (this == &other)
            return *this;

         clear();

         m_values = other.m_values;
         m_hash_shift = other.m_hash_shift;
         m_num_valid = other.m_num_valid;
         m_grow_threshold = other.m_grow_threshold;
         m_hasher = other.m_hasher;
         m_equals = other.m_equals;

         return *this;
      }

      inline ~hash_map()
      {
         clear();
      }

      const Equals& get_equals() const { return m_equals; }
            Equals& get_equals()       { return m_equals; }

      void set_equals(const Equals& equals) { m_equals = equals; }

      const Hasher& get_hasher() const { return m_hasher; }
            Hasher& get_hasher()       { return m_hasher; }

      void set_hasher(const Hasher& hasher) { m_hasher = hasher; }

      inline void clear()
      {
         if (!m_values.empty())
         {
            if (CRNLIB_HAS_DESTRUCTOR(Key) || CRNLIB_HAS_DESTRUCTOR(Value))
            {
               node* p = &get_node(0);
               node* p_end = p + m_values.size();

               uint num_remaining = m_num_valid;
               while (p != p_end)
               {
                  if (p->state)
                  {
                     destruct_value_type(p);
                     num_remaining--;
                     if (!num_remaining)
                        break;
                  }

                  p++;
               }
            }

            m_values.clear_no_destruction();

            m_hash_shift = 32;
            m_num_valid = 0;
            m_grow_threshold = 0;
         }
      }

      inline void reset()
      {
         if (!m_num_valid)
            return;

         if (CRNLIB_HAS_DESTRUCTOR(Key) || CRNLIB_HAS_DESTRUCTOR(Value))
         {
            node* p = &get_node(0);
            node* p_end = p + m_values.size();

            uint num_remaining = m_num_valid;
            while (p != p_end)
            {
               if (p->state)
               {
                  destruct_value_type(p);
                  p->state = cStateInvalid;

                  num_remaining--;
                  if (!num_remaining)
                     break;
               }

               p++;
            }
         }
         else if (sizeof(node) <= 32)
         {
            memset(&m_values[0], 0, m_values.size_in_bytes());
         }
         else
         {
            node* p = &get_node(0);
            node* p_end = p + m_values.size();

            uint num_remaining = m_num_valid;
            while (p != p_end)
            {
               if (p->state)
               {
                  p->state = cStateInvalid;

                  num_remaining--;
                  if (!num_remaining)
                     break;
               }

               p++;
            }
         }

         m_num_valid = 0;
      }

      inline uint size()
      {
         return m_num_valid;
      }

      inline uint get_table_size()
      {
         return m_values.size();
      }

      inline bool empty()
      {
         return !m_num_valid;
      }

      inline void reserve(uint new_capacity)
      {
         uint new_hash_size = math::maximum(1U, new_capacity);

         new_hash_size = new_hash_size * 2U;

         if (!math::is_power_of_2(new_hash_size))
            new_hash_size = math::next_pow2(new_hash_size);

         new_hash_size = math::maximum<uint>(cMinHashSize, new_hash_size);

         if (new_hash_size > m_values.size())
            rehash(new_hash_size);
      }

      class const_iterator;

      class iterator
      {
         friend class hash_map<Key, Value, Hasher, Equals>;
         friend class hash_map<Key, Value, Hasher, Equals>::const_iterator;

      public:
         inline iterator() : m_pTable(NULL), m_index(0) { }
         inline iterator(hash_map_type& table, uint index) : m_pTable(&table), m_index(index) { }
         inline iterator(const iterator& other) : m_pTable(other.m_pTable), m_index(other.m_index) { }

         inline iterator& operator= (const iterator& other)
         {
            m_pTable = other.m_pTable;
            m_index = other.m_index;
            return *this;
         }

          // post-increment
         inline iterator operator++(int)
         {
            iterator result(*this);
            ++*this;
            return result;
         }

         // pre-increment
         inline iterator& operator++()
         {
            probe();
            return *this;
         }

         inline value_type& operator*() const { return *get_cur(); }
         inline value_type* operator->() const { return get_cur(); }

         inline bool operator == (const iterator& b) const { return (m_pTable == b.m_pTable) && (m_index == b.m_index); }
         inline bool operator != (const iterator& b) const { return !(*this == b); }
         inline bool operator == (const const_iterator& b) const { return (m_pTable == b.m_pTable) && (m_index == b.m_index); }
         inline bool operator != (const const_iterator& b) const { return !(*this == b); }

      private:
         hash_map_type* m_pTable;
         uint m_index;

         inline value_type* get_cur() const
         {
            CRNLIB_ASSERT(m_pTable && (m_index < m_pTable->m_values.size()));
            CRNLIB_ASSERT(m_pTable->get_node_state(m_index) == cStateValid);

            return &m_pTable->get_node(m_index);
         }

         inline void probe()
         {
            CRNLIB_ASSERT(m_pTable);
            m_index = m_pTable->find_next(m_index);
         }
      };

      class const_iterator
      {
         friend class hash_map<Key, Value, Hasher, Equals>;
         friend class hash_map<Key, Value, Hasher, Equals>::iterator;

      public:
         inline const_iterator() : m_pTable(NULL), m_index(0) { }
         inline const_iterator(const hash_map_type& table, uint index) : m_pTable(&table), m_index(index) { }
         inline const_iterator(const iterator& other) : m_pTable(other.m_pTable), m_index(other.m_index) { }
         inline const_iterator(const const_iterator& other) : m_pTable(other.m_pTable), m_index(other.m_index) { }

         inline const_iterator& operator= (const const_iterator& other)
         {
            m_pTable = other.m_pTable;
            m_index = other.m_index;
            return *this;
         }

         inline const_iterator& operator= (const iterator& other)
         {
            m_pTable = other.m_pTable;
            m_index = other.m_index;
            return *this;
         }

         // post-increment
         inline const_iterator operator++(int)
         {
            const_iterator result(*this);
            ++*this;
            return result;
         }

         // pre-increment
         inline const_iterator& operator++()
         {
            probe();
            return *this;
         }

         inline const value_type& operator*() const { return *get_cur(); }
         inline const value_type* operator->() const { return get_cur(); }

         inline bool operator == (const const_iterator& b) const { return (m_pTable == b.m_pTable) && (m_index == b.m_index); }
         inline bool operator != (const const_iterator& b) const { return !(*this == b); }
         inline bool operator == (const iterator& b) const { return (m_pTable == b.m_pTable) && (m_index == b.m_index); }
         inline bool operator != (const iterator& b) const { return !(*this == b); }

      private:
         const hash_map_type* m_pTable;
         uint m_index;

         inline const value_type* get_cur() const
         {
            CRNLIB_ASSERT(m_pTable && (m_index < m_pTable->m_values.size()));
            CRNLIB_ASSERT(m_pTable->get_node_state(m_index) == cStateValid);

            return &m_pTable->get_node(m_index);
         }

         inline void probe()
         {
            CRNLIB_ASSERT(m_pTable);
            m_index = m_pTable->find_next(m_index);
         }
      };

      inline const_iterator begin() const
      {
         if (!m_num_valid)
            return end();

         return const_iterator(*this, find_next(-1));
      }

      inline const_iterator end() const
      {
         return const_iterator(*this, m_values.size());
      }

      inline iterator begin()
      {
         if (!m_num_valid)
            return end();

         return iterator(*this, find_next(-1));
      }

      inline iterator end()
      {
         return iterator(*this, m_values.size());
      }

      // insert_result.first will always point to inserted key/value (or the already existing key/value).
      // insert_resutt.second will be true if a new key/value was inserted, or false if the key already existed (in which case first will point to the already existing value).
      typedef std::pair<iterator, bool> insert_result;

      inline insert_result insert(const Key& k, const Value& v = Value())
      {
         insert_result result;
         if (!insert_no_grow(result, k, v))
         {
            grow();

            // This must succeed.
            if (!insert_no_grow(result, k, v))
            {
               CRNLIB_FAIL("insert() failed");
            }
         }

         return result;
      }

      inline insert_result insert(const value_type& v)
      {
         return insert(v.first, v.second);
      }

      inline const_iterator find(const Key& k) const
      {
         return const_iterator(*this, find_index(k));
      }

      inline iterator find(const Key& k)
      {
         return iterator(*this, find_index(k));
      }

      inline bool erase(const Key& k)
      {
         int i = find_index(k);

         if (i >= static_cast<int>(m_values.size()))
            return false;

         node* pDst = &get_node(i);
         destruct_value_type(pDst);
         pDst->state = cStateInvalid;

         m_num_valid--;

         for ( ; ; )
         {
            int r, j = i;

            node* pSrc = pDst;

            do
            {
               if (!i)
               {
                  i = m_values.size() - 1;
                  pSrc = &get_node(i);
               }
               else
               {
                  i--;
                  pSrc--;
               }

               if (!pSrc->state)
                  return true;

               r = hash_key(pSrc->first);

            } while ((i <= r && r < j) || (r < j && j < i) || (j < i && i <= r));

            move_node(pDst, pSrc);

            pDst = pSrc;
         }
      }

      inline void swap(hash_map_type& other)
      {
         m_values.swap(other.m_values);
         utils::swap(m_hash_shift,     other.m_hash_shift);
         utils::swap(m_num_valid,      other.m_num_valid);
         utils::swap(m_grow_threshold, other.m_grow_threshold);
         utils::swap(m_hasher,         other.m_hasher);
         utils::swap(m_equals,         other.m_equals);
      }

   private:
      struct node : public value_type
      {
         uint8 state;
      };

      static inline void construct_value_type(value_type* pDst, const Key& k, const Value& v)
      {
         if (CRNLIB_IS_BITWISE_COPYABLE(Key))
            memcpy(&pDst->first, &k, sizeof(Key));
         else
            scalar_type<Key>::construct(&pDst->first, k);

         if (CRNLIB_IS_BITWISE_COPYABLE(Value))
            memcpy(&pDst->second, &v, sizeof(Value));
         else
            scalar_type<Value>::construct(&pDst->second, v);
      }

      static inline void construct_value_type(value_type* pDst, const value_type* pSrc)
      {
         if ((CRNLIB_IS_BITWISE_COPYABLE(Key)) && (CRNLIB_IS_BITWISE_COPYABLE(Value)))
         {
            memcpy(pDst, pSrc, sizeof(value_type));
         }
         else
         {
            if (CRNLIB_IS_BITWISE_COPYABLE(Key))
               memcpy(&pDst->first, &pSrc->first, sizeof(Key));
            else
               scalar_type<Key>::construct(&pDst->first, pSrc->first);

            if (CRNLIB_IS_BITWISE_COPYABLE(Value))
               memcpy(&pDst->second, &pSrc->second, sizeof(Value));
            else
               scalar_type<Value>::construct(&pDst->second, pSrc->second);
         }
      }

      static inline void destruct_value_type(value_type* p)
      {
         scalar_type<Key>::destruct(&p->first);
         scalar_type<Value>::destruct(&p->second);
      }

      // Moves *pSrc to *pDst efficiently.
      // pDst should NOT be constructed on entry.
      static inline void move_node(node* pDst, node* pSrc)
      {
         CRNLIB_ASSERT(!pDst->state);

         if (CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(Key) && CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(Value))
         {
            memcpy(pDst, pSrc, sizeof(node));
         }
         else
         {
            if (CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(Key))
               memcpy(&pDst->first, &pSrc->first, sizeof(Key));
            else
            {
               scalar_type<Key>::construct(&pDst->first, pSrc->first);
               scalar_type<Key>::destruct(&pSrc->first);
            }

            if (CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(Value))
               memcpy(&pDst->second, &pSrc->second, sizeof(Value));
            else
            {
               scalar_type<Value>::construct(&pDst->second, pSrc->second);
               scalar_type<Value>::destruct(&pSrc->second);
            }

            pDst->state = cStateValid;
         }

         pSrc->state = cStateInvalid;
      }

      struct raw_node
      {
         inline raw_node()
         {
            node* p = reinterpret_cast<node*>(this);
            p->state = cStateInvalid;
         }

         inline ~raw_node()
         {
            node* p = reinterpret_cast<node*>(this);
            if (p->state)
               hash_map_type::destruct_value_type(p);
         }

         inline raw_node(const raw_node& other)
         {
            node* pDst = reinterpret_cast<node*>(this);
            const node* pSrc = reinterpret_cast<const node*>(&other);

            if (pSrc->state)
            {
               hash_map_type::construct_value_type(pDst, pSrc);
               pDst->state = cStateValid;
            }
            else
               pDst->state = cStateInvalid;
         }

         inline raw_node& operator= (const raw_node& rhs)
         {
            if (this == &rhs)
               return *this;

            node* pDst = reinterpret_cast<node*>(this);
            const node* pSrc = reinterpret_cast<const node*>(&rhs);

            if (pSrc->state)
            {
               if (pDst->state)
               {
                  pDst->first = pSrc->first;
                  pDst->second = pSrc->second;
               }
               else
               {
                  hash_map_type::construct_value_type(pDst, pSrc);
                  pDst->state = cStateValid;
               }
            }
            else if (pDst->state)
            {
               hash_map_type::destruct_value_type(pDst);
               pDst->state = cStateInvalid;
            }

            return *this;
         }

         uint8 m_bits[sizeof(node)];
      };

      typedef crnlib::vector<raw_node> node_vector;

      node_vector       m_values;
      uint              m_hash_shift;

      Hasher            m_hasher;
      Equals            m_equals;

      uint              m_num_valid;

      uint              m_grow_threshold;

      inline int hash_key(const Key& k) const
      {
         CRNLIB_ASSERT((1U << (32U - m_hash_shift)) == m_values.size());

         uint hash = static_cast<uint>(m_hasher(k));

         // Fibonacci hashing
         hash = (2654435769U * hash) >> m_hash_shift;

         CRNLIB_ASSERT(hash < m_values.size());
         return hash;
      }

      inline const node& get_node(uint index) const
      {
         return *reinterpret_cast<const node*>(&m_values[index]);
      }

      inline node& get_node(uint index)
      {
         return *reinterpret_cast<node*>(&m_values[index]);
      }

      inline state get_node_state(uint index) const
      {
         return static_cast<state>(get_node(index).state);
      }

      inline void set_node_state(uint index, bool valid)
      {
         get_node(index).state = valid;
      }

      inline void grow()
      {
         rehash(math::maximum<uint>(cMinHashSize, m_values.size() * 2U));
      }

      inline void rehash(uint new_hash_size)
      {
         CRNLIB_ASSERT(new_hash_size >= m_num_valid);
         CRNLIB_ASSERT(math::is_power_of_2(new_hash_size));

         if ((new_hash_size < m_num_valid) || (new_hash_size == m_values.size()))
            return;

         hash_map new_map;
         new_map.m_values.resize(new_hash_size);
         new_map.m_hash_shift = 32U - math::floor_log2i(new_hash_size);
         CRNLIB_ASSERT(new_hash_size == (1U << (32U - new_map.m_hash_shift)));
         new_map.m_grow_threshold = UINT_MAX;

         node* pNode = reinterpret_cast<node*>(m_values.begin());
         node* pNode_end = pNode + m_values.size();

         while (pNode != pNode_end)
         {
            if (pNode->state)
            {
               new_map.move_into(pNode);

               if (new_map.m_num_valid == m_num_valid)
                  break;
            }

            pNode++;
         }

         new_map.m_grow_threshold = (new_hash_size + 1U) >> 1U;

         m_values.clear_no_destruction();
         m_hash_shift = 32;

         swap(new_map);
      }

      inline uint find_next(int index) const
      {
         index++;

         if (index >= static_cast<int>(m_values.size()))
            return index;

         const node* pNode = &get_node(index);

         for ( ; ; )
         {
            if (pNode->state)
               break;

            if (++index >= static_cast<int>(m_values.size()))
               break;

            pNode++;
         }

         return index;
      }

      inline uint find_index(const Key& k) const
      {
         if (m_num_valid)
         {
            int index = hash_key(k);
            const node* pNode = &get_node(index);

            if (pNode->state)
            {
               if (m_equals(pNode->first, k))
                  return index;

               const int orig_index = index;

               for ( ; ; )
               {
                  if (!index)
                  {
                     index = m_values.size() - 1;
                     pNode = &get_node(index);
                  }
                  else
                  {
                     index--;
                     pNode--;
                  }

                  if (index == orig_index)
                     break;

                  if (!pNode->state)
                     break;

                  if (m_equals(pNode->first, k))
                     return index;
               }
            }
         }

         return m_values.size();
      }

      inline bool insert_no_grow(insert_result& result, const Key& k, const Value& v = Value())
      {
         if (!m_values.size())
            return false;

         int index = hash_key(k);
         node* pNode = &get_node(index);

         if (pNode->state)
         {
            if (m_equals(pNode->first, k))
            {
               result.first = iterator(*this, index);
               result.second = false;
               return true;
            }

            const int orig_index = index;

            for ( ; ; )
            {
               if (!index)
               {
                  index = m_values.size() - 1;
                  pNode = &get_node(index);
               }
               else
               {
                  index--;
                  pNode--;
               }

               if (orig_index == index)
                  return false;

               if (!pNode->state)
                  break;

               if (m_equals(pNode->first, k))
               {
                  result.first = iterator(*this, index);
                  result.second = false;
                  return true;
               }
            }
         }

         if (m_num_valid >= m_grow_threshold)
            return false;

         construct_value_type(pNode, k, v);

         pNode->state = cStateValid;

         m_num_valid++;
         CRNLIB_ASSERT(m_num_valid <= m_values.size());

         result.first = iterator(*this, index);
         result.second = true;

         return true;
      }

      inline void move_into(node* pNode)
      {
         int index = hash_key(pNode->first);
         node* pDst_node = &get_node(index);

         if (pDst_node->state)
         {
            const int orig_index = index;

            for ( ; ; )
            {
               if (!index)
               {
                  index = m_values.size() - 1;
                  pDst_node = &get_node(index);
               }
               else
               {
                  index--;
                  pDst_node--;
               }

               if (index == orig_index)
               {
                  CRNLIB_ASSERT(false);
                  return;
               }

               if (!pDst_node->state)
                  break;
            }
         }

         move_node(pDst_node, pNode);

         m_num_valid++;
      }
   };

   template<typename Key, typename Value, typename Hasher, typename Equals>
   struct bitwise_movable< hash_map<Key, Value, Hasher, Equals> > { enum { cFlag = true }; };

   template<typename Key, typename Value, typename Hasher, typename Equals>
   inline void swap(hash_map<Key, Value, Hasher, Equals>& a, hash_map<Key, Value, Hasher, Equals>& b)
   {
      a.swap(b);
   }

   extern void hash_map_test();

} // namespace crnlib
