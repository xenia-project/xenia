// File: crn_sparse_bit_array.h
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_sparse_bit_array.h"

namespace crnlib
{
   sparse_bit_array::sparse_bit_array() :
      m_num_groups(0), m_ppGroups(NULL)
   {
   }

   sparse_bit_array::sparse_bit_array(uint size) :
      m_num_groups(0), m_ppGroups(NULL)
   {
      resize(size);
   }

   sparse_bit_array::sparse_bit_array(sparse_bit_array& other)
   {
      m_num_groups = other.m_num_groups;
      m_ppGroups = (uint32**)crnlib_malloc(m_num_groups * sizeof(uint32*));
      CRNLIB_VERIFY(m_ppGroups);

      for (uint i = 0; i < m_num_groups; i++)
      {
         if (other.m_ppGroups[i])
         {
            m_ppGroups[i] = alloc_group(false);
            memcpy(m_ppGroups[i], other.m_ppGroups[i], cBytesPerGroup);
         }
         else
            m_ppGroups[i] = NULL;
      }
   }

   sparse_bit_array::~sparse_bit_array()
   {
      clear();
   }

   sparse_bit_array& sparse_bit_array::operator= (sparse_bit_array& other)
   {
      if (this == &other)
         return *this;

      if (m_num_groups != other.m_num_groups)
      {
         clear();

         m_num_groups = other.m_num_groups;
         m_ppGroups = (uint32**)crnlib_calloc(m_num_groups, sizeof(uint32*));
         CRNLIB_VERIFY(m_ppGroups);
      }

      for (uint i = 0; i < m_num_groups; i++)
      {
         if (other.m_ppGroups[i])
         {
            if (!m_ppGroups[i])
               m_ppGroups[i] = alloc_group(false);
            memcpy(m_ppGroups[i], other.m_ppGroups[i], cBytesPerGroup);
         }
         else if (m_ppGroups[i])
         {
            free_group(m_ppGroups[i]);
            m_ppGroups[i] = NULL;
         }
      }

      return *this;
   }

   void sparse_bit_array::clear()
   {
      if (!m_num_groups)
         return;

      for (uint i = 0; i < m_num_groups; i++)
         free_group(m_ppGroups[i]);

      crnlib_free(m_ppGroups);
      m_ppGroups = NULL;

      m_num_groups = 0;
   }

   void sparse_bit_array::swap(sparse_bit_array& other)
   {
      utils::swap(m_ppGroups, other.m_ppGroups);
      utils::swap(m_num_groups, other.m_num_groups);
   }

   void sparse_bit_array::optimize()
   {
      for (uint i = 0; i < m_num_groups; i++)
      {
         uint32* s = m_ppGroups[i];
         if (s)
         {
            uint j;
            for (j = 0; j < cDWORDsPerGroup; j++)
               if (s[j])
                  break;
            if (j == cDWORDsPerGroup)
            {
               free_group(s);
               m_ppGroups[i] = NULL;
            }
         }
      }
   }

   void sparse_bit_array::set_bit_range(uint index, uint num)
   {
      CRNLIB_ASSERT((index + num) <= (m_num_groups << cBitsPerGroupShift));

      if (!num)
         return;
      else if (num == 1)
      {
         set_bit(index);
         return;
      }

      while ((index & cBitsPerGroupMask) || (num <= cBitsPerGroup))
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint32* pGroup = m_ppGroups[group_index];
         if (!pGroup)
         {
            pGroup = alloc_group(true);
            m_ppGroups[group_index] = pGroup;
         }

         const uint group_bit_ofs = index & cBitsPerGroupMask;

         const uint dword_bit_ofs = group_bit_ofs & 31;
         const uint max_bits_to_set = 32 - dword_bit_ofs;

         const uint bits_to_set = math::minimum(max_bits_to_set, num);
         const uint32 msk = (0xFFFFFFFFU >> (32 - bits_to_set));

         pGroup[group_bit_ofs >> 5] |= (msk << dword_bit_ofs);

         num -= bits_to_set;
         if (!num)
            return;

         index += bits_to_set;
      }

      while (num >= cBitsPerGroup)
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint32* pGroup = m_ppGroups[group_index];
         if (!pGroup)
         {
            pGroup = alloc_group(true);
            m_ppGroups[group_index] = pGroup;
         }

         memset(pGroup, 0xFF, sizeof(uint32) * cDWORDsPerGroup);

         num -= cBitsPerGroup;
         index += cBitsPerGroup;
      }

      while (num)
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint32* pGroup = m_ppGroups[group_index];
         if (!pGroup)
         {
            pGroup = alloc_group(true);
            m_ppGroups[group_index] = pGroup;
         }

         uint group_bit_ofs = index & cBitsPerGroupMask;

         uint bits_to_set = math::minimum(32U, num);
         uint32 msk = (0xFFFFFFFFU >> (32 - bits_to_set));

         pGroup[group_bit_ofs >> 5] |= (msk << (group_bit_ofs & 31));

         num -= bits_to_set;
         index += bits_to_set;
      }
   }

   void sparse_bit_array::clear_all_bits()
   {
      for (uint i = 0; i < m_num_groups; i++)
      {
         uint32* pGroup = m_ppGroups[i];
         if (pGroup)
            memset(pGroup, 0, sizeof(uint32) * cDWORDsPerGroup);
      }
   }

   void sparse_bit_array::clear_bit_range(uint index, uint num)
   {
      CRNLIB_ASSERT((index + num) <= (m_num_groups << cBitsPerGroupShift));

      if (!num)
         return;
      else if (num == 1)
      {
         clear_bit(index);
         return;
      }

      while ((index & cBitsPerGroupMask) || (num <= cBitsPerGroup))
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         const uint group_bit_ofs = index & cBitsPerGroupMask;

         const uint dword_bit_ofs = group_bit_ofs & 31;
         const uint max_bits_to_set = 32 - dword_bit_ofs;

         const uint bits_to_set = math::minimum(max_bits_to_set, num);

         uint32* pGroup = m_ppGroups[group_index];
         if (pGroup)
         {
            const uint32 msk = (0xFFFFFFFFU >> (32 - bits_to_set));

            pGroup[group_bit_ofs >> 5] &= (~(msk << dword_bit_ofs));
         }

         num -= bits_to_set;
         if (!num)
            return;

         index += bits_to_set;
      }

      while (num >= cBitsPerGroup)
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint32* pGroup = m_ppGroups[group_index];
         if (pGroup)
         {
            free_group(pGroup);
            m_ppGroups[group_index] = NULL;
         }

         num -= cBitsPerGroup;
         index += cBitsPerGroup;
      }

      while (num)
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint bits_to_set = math::minimum(32u, num);

         uint32* pGroup = m_ppGroups[group_index];
         if (pGroup)
         {
            uint group_bit_ofs = index & cBitsPerGroupMask;

            uint32 msk = (0xFFFFFFFFU >> (32 - bits_to_set));

            pGroup[group_bit_ofs >> 5] &= (~(msk << (group_bit_ofs & 31)));
         }

         num -= bits_to_set;
         index += bits_to_set;
      }
   }

   void sparse_bit_array::resize(uint size)
   {
      uint num_groups = (size + cBitsPerGroup - 1) >> cBitsPerGroupShift;
      if (num_groups == m_num_groups)
         return;

      if (!num_groups)
      {
         clear();
         return;
      }

      sparse_bit_array temp;
      temp.swap(*this);

      m_num_groups = num_groups;
      m_ppGroups = (uint32**)crnlib_calloc(m_num_groups, sizeof(uint32*));
      CRNLIB_VERIFY(m_ppGroups);

      uint n = math::minimum(temp.m_num_groups, m_num_groups);
      for (uint i = 0; i < n; i++)
      {
         uint32* p = temp.m_ppGroups[i];
         if (p)
         {
            m_ppGroups[i] = temp.m_ppGroups[i];
            temp.m_ppGroups[i] = NULL;
         }
      }
   }

   sparse_bit_array& sparse_bit_array::operator&= (const sparse_bit_array& other)
   {
      if (this == &other)
         return *this;

      CRNLIB_VERIFY(other.m_num_groups == m_num_groups);

      for (uint i = 0; i < m_num_groups; i++)
      {
         uint32* d = m_ppGroups[i];
         if (!d)
            continue;
         uint32* s = other.m_ppGroups[i];

         if (!s)
         {
            free_group(d);
            m_ppGroups[i] = NULL;
         }
         else
         {
            uint32 oc = 0;
            for (uint j = 0; j < cDWORDsPerGroup; j++)
            {
               uint32 c = d[j] & s[j];
               d[j] = c;
               oc |= c;
            }
            if (!oc)
            {
               free_group(d);
               m_ppGroups[i] = NULL;
            }
         }
      }

      return *this;
   }

   sparse_bit_array& sparse_bit_array::operator|= (const sparse_bit_array& other)
   {
      if (this == &other)
         return *this;

      CRNLIB_VERIFY(other.m_num_groups == m_num_groups);

      for (uint i = 0; i < m_num_groups; i++)
      {
         uint32* s = other.m_ppGroups[i];
         if (!s)
            continue;

         uint32* d = m_ppGroups[i];
         if (!d)
         {
            d = alloc_group(true);
            m_ppGroups[i] = d;
            memcpy(d, s, cBytesPerGroup);
         }
         else
         {
            uint32 oc = 0;
            for (uint j = 0; j < cDWORDsPerGroup; j++)
            {
               uint32 c = d[j] | s[j];
               d[j] = c;
               oc |= c;
            }
            if (!oc)
            {
               free_group(d);
               m_ppGroups[i] = NULL;
            }
         }
      }

      return *this;
   }

   sparse_bit_array& sparse_bit_array::and_not(const sparse_bit_array& other)
   {
      if (this == &other)
         return *this;

      CRNLIB_VERIFY(other.m_num_groups == m_num_groups);

      for (uint i = 0; i < m_num_groups; i++)
      {
         uint32* d = m_ppGroups[i];
         if (!d)
            continue;
         uint32* s = other.m_ppGroups[i];
         if (!s)
            continue;

         uint32 oc = 0;
         for (uint j = 0; j < cDWORDsPerGroup; j++)
         {
            uint32 c = d[j] & (~s[j]);
            d[j] = c;
            oc |= c;
         }
         if (!oc)
         {
            free_group(d);
            m_ppGroups[i] = NULL;
         }
      }

      return *this;
   }

   int sparse_bit_array::find_first_set_bit(uint index, uint num) const
   {
      CRNLIB_ASSERT((index + num) <= (m_num_groups << cBitsPerGroupShift));

      if (!num)
         return -1;

      while ((index & cBitsPerGroupMask) || (num <= cBitsPerGroup))
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         const uint group_bit_ofs = index & cBitsPerGroupMask;
         const uint dword_bit_ofs = group_bit_ofs & 31;

         const uint max_bits_to_examine = 32 - dword_bit_ofs;
         const uint bits_to_examine = math::minimum(max_bits_to_examine, num);

         uint32* pGroup = m_ppGroups[group_index];
         if (pGroup)
         {
            const uint32 msk = (0xFFFFFFFFU >> (32 - bits_to_examine));

            uint bits = pGroup[group_bit_ofs >> 5] & (msk << dword_bit_ofs);
            if (bits)
            {
               uint num_trailing_zeros = math::count_trailing_zero_bits(bits);
               int set_index = num_trailing_zeros + (index & ~31);
               CRNLIB_ASSERT(get_bit(set_index));
               return set_index;
            }
         }

         num -= bits_to_examine;
         if (!num)
            return -1;

         index += bits_to_examine;
      }

      while (num >= cBitsPerGroup)
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint32* pGroup = m_ppGroups[group_index];
         if (pGroup)
         {
            for (uint i = 0; i < cDWORDsPerGroup; i++)
            {
               uint32 bits = pGroup[i];
               if (bits)
               {
                  uint num_trailing_zeros = math::count_trailing_zero_bits(bits);

                  int set_index = num_trailing_zeros + index + (i << 5);
                  CRNLIB_ASSERT(get_bit(set_index));
                  return set_index;
               }
            }

         }

         num -= cBitsPerGroup;
         index += cBitsPerGroup;
      }

      while (num)
      {
         uint group_index = index >> cBitsPerGroupShift;
         CRNLIB_ASSERT(group_index < m_num_groups);

         uint bits_to_examine = math::minimum(32U, num);

         uint32* pGroup = m_ppGroups[group_index];
         if (pGroup)
         {
            uint group_bit_ofs = index & cBitsPerGroupMask;

            uint32 msk = (0xFFFFFFFFU >> (32 - bits_to_examine));

            uint32 bits = pGroup[group_bit_ofs >> 5] & (msk << (group_bit_ofs & 31));
            if (bits)
            {
               uint num_trailing_zeros = math::count_trailing_zero_bits(bits);

               int set_index = num_trailing_zeros + (index & ~31);
               CRNLIB_ASSERT(get_bit(set_index));
               return set_index;
            }
         }

         num -= bits_to_examine;
         index += bits_to_examine;
      }

      return -1;
   }

} // namespace crnlib












