// File: crn_hash_map.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_hash_map.h"
#include "crn_rand.h"

namespace crnlib
{
#if 0
   class counted_obj
   {
   public:
      counted_obj(uint v = 0) :
         m_val(v)
      {
         m_count++;
      }

      counted_obj(const counted_obj& obj) :
         m_val(obj.m_val)
      {
         m_count++;
      }

      ~counted_obj()
      {
         CRNLIB_ASSERT(m_count > 0);
         m_count--;
      }

      static uint m_count;

      uint m_val;

      operator size_t() const { return m_val; }

      bool operator== (const counted_obj& rhs) const { return m_val == rhs.m_val; }
      bool operator== (const uint rhs) const { return m_val == rhs; }

   };

   uint counted_obj::m_count;

   void hash_map_test()
   {
      random r0, r1;

      uint seed = 0;
      for ( ; ; )
      {
         seed++;

         typedef crnlib::hash_map<counted_obj, counted_obj> my_hash_map;
         my_hash_map m;

         const uint n = r0.irand(1, 100000);

         printf("%u\n", n);

         r1.seed(seed);

         crnlib::vector<int> q;

         uint count = 0;
         for (uint i = 0; i < n; i++)
         {
            uint v = r1.urand32() & 0x7FFFFFFF;
            my_hash_map::insert_result res = m.insert(counted_obj(v), counted_obj(v ^ 0xdeadbeef));
            if (res.second)
            {
               count++;
               q.push_back(v);
            }
         }

         CRNLIB_VERIFY(m.size() == count);

         r1.seed(seed);

         my_hash_map cm(m);
         m.clear();
         m = cm;
         cm.reset();

         for (uint i = 0; i < n; i++)
         {
            uint v = r1.urand32() & 0x7FFFFFFF;
            my_hash_map::const_iterator it = m.find(counted_obj(v));
            CRNLIB_VERIFY(it != m.end());
            CRNLIB_VERIFY(it->first == v);
            CRNLIB_VERIFY(it->second == (v ^ 0xdeadbeef));
         }

         for (uint t = 0; t < 2; t++)
         {
            const uint nd = r0.irand(1, q.size() + 1);
            for (uint i = 0; i < nd; i++)
            {
               uint p = r0.irand(0, q.size());

               int k = q[p];
               if (k >= 0)
               {
                  q[p] = -k - 1;

                  bool s = m.erase(counted_obj(k));
                  CRNLIB_VERIFY(s);
               }
            }

            typedef crnlib::hash_map<uint, empty_type> uint_hash_set;
            uint_hash_set s;

            for (uint i = 0; i < q.size(); i++)
            {
               int v = q[i];

               if (v >= 0)
               {
                  my_hash_map::const_iterator it = m.find(counted_obj(v));
                  CRNLIB_VERIFY(it != m.end());
                  CRNLIB_VERIFY(it->first == (uint)v);
                  CRNLIB_VERIFY(it->second == ((uint)v ^ 0xdeadbeef));

                  s.insert(v);
               }
               else
               {
                  my_hash_map::const_iterator it = m.find(counted_obj(-v - 1));
                  CRNLIB_VERIFY(it == m.end());
               }
            }

            uint found_count = 0;
            for (my_hash_map::const_iterator it = m.begin(); it != m.end(); ++it)
            {
               CRNLIB_VERIFY(it->second == ((uint)it->first ^ 0xdeadbeef));

               uint_hash_set::const_iterator fit(s.find((uint)it->first));
               CRNLIB_VERIFY(fit != s.end());

               CRNLIB_VERIFY(fit->first == it->first);

               found_count++;
            }

            CRNLIB_VERIFY(found_count == s.size());
         }

         CRNLIB_VERIFY(counted_obj::m_count == m.size() * 2);
      }
   }
#endif

} // namespace crnlib
