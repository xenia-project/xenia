// File: crn_ray.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_vec.h"

namespace crnlib
{
   template<typename vector_type>
   class ray
   {
   public:
      typedef vector_type vector_t;
      typedef typename vector_type::scalar_type scalar_type;

      inline ray() { }
      inline ray(eClear) { clear(); }
      inline ray(const vector_type& origin, const vector_type& direction) : m_origin(origin), m_direction(direction) { }

      inline void clear()
      {
         m_origin.clear();
         m_direction.clear();
      }

      inline const vector_type& get_origin(void) const { return m_origin; }
      inline void set_origin(const vector_type& origin) { m_origin = origin; }

      inline const vector_type& get_direction(void) const { return m_direction; }
      inline void set_direction(const vector_type& direction) { m_direction = direction; }

      inline scalar_type set_endpoints(const vector_type& start, const vector_type& end, const vector_type& def)
      {
         m_origin = start;

         m_direction = end - start;
         return m_direction.normalize(&def);
      }

      inline vector_type eval(scalar_type t) const
      {
         return m_origin + m_direction * t;
      }

   private:
      vector_type m_origin;
      vector_type m_direction;
   };

   typedef ray<vec2F> ray2F;
   typedef ray<vec3F> ray3F;

} // namespace crnlib
