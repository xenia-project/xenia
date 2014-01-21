// File: crn_vec_interval.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_vec.h"

namespace crnlib
{
   template<typename T>
   class vec_interval
   {
   public:
      enum { N = T::num_elements };
      typedef typename T::scalar_type scalar_type;

      inline vec_interval(const T& v) { m_bounds[0] = v; m_bounds[1] = v; }
      inline vec_interval(const T& low, const T& high) { m_bounds[0] = low; m_bounds[1] = high; }
      
      inline void clear() { m_bounds[0].clear(); m_bounds[1].clear(); }
      
      inline const T& operator[] (uint i) const { CRNLIB_ASSERT(i < 2); return m_bounds[i]; }
      inline       T& operator[] (uint i)       { CRNLIB_ASSERT(i < 2); return m_bounds[i]; }

   private:
      T m_bounds[2];
   };

   typedef vec_interval<vec1F> vec_interval1F;
   typedef vec_interval<vec2F> vec_interval2F;
   typedef vec_interval<vec3F> vec_interval3F;
   typedef vec_interval<vec4F> vec_interval4F;

   typedef vec_interval2F aabb2F;
   typedef vec_interval3F aabb3F;

} // namespace crnlib
