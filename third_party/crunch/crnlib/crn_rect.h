// File: crn_rect.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_vec.h"
#include "crn_hash.h"

namespace crnlib
{
   class rect
   {
   public:
      inline rect()
      {
      }
      
      inline rect(eClear)
      {
         clear();
      }

      // up to, but not including right/bottom
      inline rect(int left, int top, int right, int bottom)
      {
         set(left, top, right, bottom);
      }
      
      inline rect(const vec2I& lo, const vec2I& hi)
      {
         m_corner[0] = lo;
         m_corner[1] = hi;
      }
      
      inline rect(const vec2I& point)
      {
         m_corner[0] = point;
         m_corner[1].set(point[0] + 1, point[1] + 1);
      }

      inline bool operator== (const rect& r) const
      {
         return (m_corner[0] == r.m_corner[0]) && (m_corner[1] == r.m_corner[1]);
      }

      inline bool operator< (const rect& r) const
      {
         for (uint i = 0; i < 2; i++)
         {
            if (m_corner[i] < r.m_corner[i])
               return true;
            else if (!(m_corner[i] == r.m_corner[i]))
               return false;
         }            

         return false;
      }
      
      inline void clear()
      {
         m_corner[0].clear();
         m_corner[1].clear();  
      }
      
      inline void set(int left, int top, int right, int bottom)
      {
         m_corner[0].set(left, top);
         m_corner[1].set(right, bottom);
      }

      inline void set(const vec2I& lo, const vec2I& hi)
      {
         m_corner[0] = lo;
         m_corner[1] = hi;
      }
      
      inline void set(const vec2I& point)
      {
         m_corner[0] = point;
         m_corner[1].set(point[0] + 1, point[1] + 1);
      }
      
      inline uint get_width() const { return m_corner[1][0] - m_corner[0][0]; }
      inline uint get_height() const { return m_corner[1][1] - m_corner[0][1]; }
            
      inline int get_left() const { return m_corner[0][0]; }
      inline int get_top() const { return m_corner[0][1]; }
      inline int get_right() const { return m_corner[1][0]; }
      inline int get_bottom() const { return m_corner[1][1]; }
      
      inline bool is_empty() const { return (m_corner[1][0] <= m_corner[0][0]) || (m_corner[1][1] <= m_corner[0][1]); }
      
      inline uint get_dimension(uint axis) const { return m_corner[1][axis] - m_corner[0][axis]; }
      inline uint get_area() const { return get_dimension(0) * get_dimension(1); }
      
      inline const vec2I& operator[] (uint i) const { CRNLIB_ASSERT(i < 2); return m_corner[i]; }
      inline       vec2I& operator[] (uint i)       { CRNLIB_ASSERT(i < 2); return m_corner[i]; }

      inline rect& translate(int x_ofs, int y_ofs)
      {
         m_corner[0][0] += x_ofs;  
         m_corner[0][1] += y_ofs;
         m_corner[1][0] += x_ofs;  
         m_corner[1][1] += y_ofs;
         return *this;
      }

      inline rect& init_expand()
      {
         m_corner[0].set(INT_MAX);
         m_corner[1].set(INT_MIN);
         return *this;
      }

      inline rect& expand(int x, int y)
      {
         m_corner[0][0] = math::minimum(m_corner[0][0], x);
         m_corner[0][1] = math::minimum(m_corner[0][1], y);
         m_corner[1][0] = math::maximum(m_corner[1][0], x + 1);
         m_corner[1][1] = math::maximum(m_corner[1][1], y + 1);
         return *this;
      }

      inline rect& expand(const rect& r)
      {
         m_corner[0][0] = math::minimum(m_corner[0][0], r[0][0]);
         m_corner[0][1] = math::minimum(m_corner[0][1], r[0][1]);
         m_corner[1][0] = math::maximum(m_corner[1][0], r[1][0]);
         m_corner[1][1] = math::maximum(m_corner[1][1], r[1][1]);
         return *this;
      }
      
      inline bool touches(const rect& r) const
      {
         for (uint i = 0; i < 2; i++)
         {
            if (r[1][i] <= m_corner[0][i]) 
               return false;
            else if (r[0][i] >= m_corner[1][i]) 
               return false;   
         }

         return true;
      }

      inline bool within(const rect& r) const
      {
         for (uint i = 0; i < 2; i++)
         {
            if (m_corner[0][i] < r[0][i])
               return false;
            else if (m_corner[1][i] > r[1][i])
               return false;   
         }

         return true;
      }

      inline bool intersect(const rect& r) 
      {
         if (!touches(r))
         {
            clear();
            return false;
         }

         for (uint i = 0; i < 2; i++)
         {
            m_corner[0][i] = math::maximum<int>(m_corner[0][i], r[0][i]);
            m_corner[1][i] = math::minimum<int>(m_corner[1][i], r[1][i]);
         }        

         return true;
      }

      inline bool contains(int x, int y) const
      {
         return (x >= m_corner[0][0]) && (x < m_corner[1][0]) && 
                (y >= m_corner[0][1]) && (y < m_corner[1][1]);
      }

      inline bool contains(const vec2I& p) const { return contains(p[0], p[1]); }

   private:
      vec2I m_corner[2];
   };
   
} // namespace crnlib
