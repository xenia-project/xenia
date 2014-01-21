// File: crn_vec.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#include "crn_core.h"
#include "crn_rand.h"

namespace crnlib
{
   template<uint N, typename T> class vec : public helpers::rel_ops< vec<N, T> >
   {
   public:
      typedef T scalar_type;
      enum { num_elements = N };

      inline vec() { }

      inline vec(eClear) { clear(); }

      inline vec(const vec& other)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] = other.m_s[i];
      }

      template<uint O, typename U>
      inline vec(const vec<O, U>& other)
      {
         set(other);
      }

      template<uint O, typename U>
      inline vec(const vec<O, U>& other, T w)
      {
         *this = other;
         m_s[N - 1] = w;
      }

      explicit inline vec(T val)
      {
         set(val);
      }

      inline vec(T val0, T val1)
      {
         set(val0, val1);
      }

      inline vec(T val0, T val1, T val2)
      {
         set(val0, val1, val2);
      }

      inline vec(T val0, T val1, T val2, T val3)
      {
         set(val0, val1, val2, val3);
      }

      inline void clear()
      {
         if (N > 4)
            memset(m_s, 0, sizeof(m_s));
         else
         {
            for (uint i = 0; i < N; i++)
               m_s[i] = 0;
         }
      }

      template<uint ON, typename OT> inline vec& set(const vec<ON, OT>& other)
      {
         if ((void*)this == (void*)&other)
            return *this;
         const uint m = math::minimum(N, ON);
         uint i;
         for (i = 0; i < m; i++)
            m_s[i] = static_cast<T>(other[i]);
         for ( ; i < N; i++)
            m_s[i] = 0;
         return *this;
      }

      inline vec& set_component(uint index, T val)
      {
         CRNLIB_ASSERT(index < N);
         m_s[index] = val;
         return *this;
      }

      inline vec& set(T val)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] = val;
         return *this;
      }

      inline vec& set(T val0, T val1)
      {
         m_s[0] = val0;
         if (N >= 2)
         {
            m_s[1] = val1;

            for (uint i = 2; i < N; i++)
               m_s[i] = 0;
         }
         return *this;
      }

      inline vec& set(T val0, T val1, T val2)
      {
         m_s[0] = val0;
         if (N >= 2)
         {
            m_s[1] = val1;

            if (N >= 3)
            {
               m_s[2] = val2;

               for (uint i = 3; i < N; i++)
                  m_s[i] = 0;
            }
         }
         return *this;
      }

      inline vec& set(T val0, T val1, T val2, T val3)
      {
         m_s[0] = val0;
         if (N >= 2)
         {
            m_s[1] = val1;

            if (N >= 3)
            {
               m_s[2] = val2;

               if (N >= 4)
               {
                  m_s[3] = val3;

                  for (uint i = 4; i < N; i++)
                     m_s[i] = 0;
               }
            }
         }
         return *this;
      }

      inline vec& set(const T* pValues)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] = pValues[i];
         return *this;
      }

      template<uint ON, typename OT> inline vec& swizzle_set(const vec<ON, OT>& other, uint i)
      {
         return set(static_cast<T>(other[i]));
      }

      template<uint ON, typename OT> inline vec& swizzle_set(const vec<ON, OT>& other, uint i, uint j)
      {
         return set(static_cast<T>(other[i]), static_cast<T>(other[j]));
      }

      template<uint ON, typename OT> inline vec& swizzle_set(const vec<ON, OT>& other, uint i, uint j, uint k)
      {
         return set(static_cast<T>(other[i]), static_cast<T>(other[j]), static_cast<T>(other[k]));
      }

      template<uint ON, typename OT> inline vec& swizzle_set(const vec<ON, OT>& other, uint i, uint j, uint k, uint l)
      {
         return set(static_cast<T>(other[i]), static_cast<T>(other[j]), static_cast<T>(other[k]), static_cast<T>(other[l]));
      }

      inline vec& operator= (const vec& rhs)
      {
         if (this != &rhs)
         {
            for (uint i = 0; i < N; i++)
               m_s[i] = rhs.m_s[i];
         }
         return *this;
      }

      template<uint O, typename U>
      inline vec& operator= (const vec<O, U>& other)
      {
         if ((void*)this == (void*)&other)
            return *this;

         uint s = math::minimum(N, O);

         uint i;
         for (i = 0; i < s; i++)
            m_s[i] = static_cast<T>(other[i]);

         for ( ; i < N; i++)
            m_s[i] = 0;

         return *this;
      }

      inline bool operator== (const vec& rhs) const
      {
         for (uint i = 0; i < N; i++)
            if (!(m_s[i] == rhs.m_s[i]))
               return false;
         return true;
      }

      inline bool operator< (const vec& rhs) const
      {
         for (uint i = 0; i < N; i++)
         {
            if (m_s[i] < rhs.m_s[i])
               return true;
            else if (!(m_s[i] == rhs.m_s[i]))
               return false;
         }

         return false;
      }

      inline T  operator[] (uint i) const
      {
         CRNLIB_ASSERT(i < N);
         return m_s[i];
      }

      inline T& operator[] (uint i)
      {
         CRNLIB_ASSERT(i < N);
         return m_s[i];
      }

      inline T get_x(void) const { return m_s[0]; }
      inline T get_y(void) const { CRNLIB_ASSUME(N >= 2); return m_s[1]; }
      inline T get_z(void) const { CRNLIB_ASSUME(N >= 3); return m_s[2]; }
      inline T get_w(void) const { CRNLIB_ASSUME(N >= 4); return m_s[3]; }

      inline vec& set_x(T v) { m_s[0] = v; return *this; }
      inline vec& set_y(T v) { CRNLIB_ASSUME(N >= 2); m_s[1] = v; return *this; }
      inline vec& set_z(T v) { CRNLIB_ASSUME(N >= 3); m_s[2] = v; return *this; }
      inline vec& set_w(T v) { CRNLIB_ASSUME(N >= 4); m_s[3] = v; return *this; }

      inline vec as_point() const
      {
         vec result(*this);
         result[N - 1] = 1;
         return result;
      }

      inline vec as_dir() const
      {
         vec result(*this);
         result[N - 1] = 0;
         return result;
      }

      inline vec<2, T> select2(uint i, uint j) const
      {
         CRNLIB_ASSERT((i < N) && (j < N));
         return vec<2, T>(m_s[i], m_s[j]);
      }

      inline vec<3, T> select3(uint i, uint j, uint k) const
      {
         CRNLIB_ASSERT((i < N) && (j < N) && (k < N));
         return vec<3, T>(m_s[i], m_s[j], m_s[k]);
      }

      inline vec<4, T> select4(uint i, uint j, uint k, uint l) const
      {
         CRNLIB_ASSERT((i < N) && (j < N) && (k < N) && (l < N));
         return vec<4, T>(m_s[i], m_s[j], m_s[k], m_s[l]);
      }

      inline bool is_dir() const { return m_s[N - 1] == 0; }
      inline bool is_vector() const { return is_dir(); }
      inline bool is_point() const { return m_s[N - 1] == 1; }

      inline vec project() const
      {
         vec result(*this);
         if (result[N - 1])
            result /= result[N - 1];
         return result;
      }

      inline vec broadcast(unsigned i) const
      {
         return vec((*this)[i]);
      }

      inline vec swizzle(uint i, uint j) const
      {
         return vec((*this)[i], (*this)[j]);
      }

      inline vec swizzle(uint i, uint j, uint k) const
      {
         return vec((*this)[i], (*this)[j], (*this)[k]);
      }

      inline vec swizzle(uint i, uint j, uint k, uint l) const
      {
         return vec((*this)[i], (*this)[j], (*this)[k], (*this)[l]);
      }

      inline vec operator- () const
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = -m_s[i];
         return result;
      }

      inline vec operator+ () const
      {
         return *this;
      }

      inline vec& operator += (const vec& other)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] += other.m_s[i];
         return *this;
      }

      inline vec& operator -= (const vec& other)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] -= other.m_s[i];
         return *this;
      }

      inline vec& operator *= (const vec& other)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] *= other.m_s[i];
         return *this;
      }

      inline vec& operator /= (const vec& other)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] /= other.m_s[i];
         return *this;
      }

      inline vec& operator *= (T s)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] *= s;
         return *this;
      }

      inline vec& operator /= (T s)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] /= s;
         return *this;
      }

      friend inline T operator* (const vec& lhs, const vec& rhs)
      {
         T result = lhs.m_s[0] * rhs.m_s[0];
         for (uint i = 1; i < N; i++)
            result += lhs.m_s[i] * rhs.m_s[i];
         return result;
      }

      friend inline vec operator* (const vec& lhs, T val)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = lhs.m_s[i] * val;
         return result;
      }

      friend inline vec operator* (T val, const vec& lhs)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = lhs.m_s[i] * val;
         return result;
      }

      friend inline vec operator/ (const vec& lhs, const vec& rhs)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = lhs.m_s[i] / rhs.m_s[i];
         return result;
      }

      friend inline vec operator/ (const vec& lhs, T val)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = lhs.m_s[i] / val;
         return result;
      }

      friend inline vec operator+ (const vec& lhs, const vec& rhs)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = lhs.m_s[i] + rhs.m_s[i];
         return result;
      }

      friend inline vec operator- (const vec& lhs, const vec& rhs)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result.m_s[i] = lhs.m_s[i] - rhs.m_s[i];
         return result;
      }

      static inline vec<3, T> cross2(const vec& a, const vec& b)
      {
         CRNLIB_ASSUME(N >= 2);
         return vec<3, T>(0, 0, a[0] * b[1] - a[1] * b[0]);
      }

      static inline vec<3, T> cross3(const vec& a, const vec& b)
      {
         CRNLIB_ASSUME(N >= 3);
         return vec<3, T>(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
      }

      static inline vec<3, T> cross(const vec& a, const vec& b)
      {
         CRNLIB_ASSUME(N >= 2);

         if (N == 2)
            return cross2(a, b);
         else
            return cross3(a, b);
      }

      inline T dot(const vec& rhs) const
      {
         return *this * rhs;
      }

      inline T dot2(const vec& rhs) const
      {
         CRNLIB_ASSUME(N >= 2);
         return m_s[0] * rhs.m_s[0] + m_s[1] * rhs.m_s[1];
      }

      inline T dot3(const vec& rhs) const
      {
         CRNLIB_ASSUME(N >= 3);
         return m_s[0] * rhs.m_s[0] + m_s[1] * rhs.m_s[1] + m_s[2] * rhs.m_s[2];
      }

      inline T norm(void) const
      {
         T sum = m_s[0] * m_s[0];
         for (uint i = 1; i < N; i++)
            sum += m_s[i] * m_s[i];
         return sum;
      }

      inline T length(void) const
      {
         return sqrt(norm());
      }

      inline T squared_distance(const vec& rhs) const
      {
         T dist2 = 0;
         for (uint i = 0; i < N; i++)
         {
            T d = m_s[i] - rhs.m_s[i];
            dist2 += d * d;
         }
         return dist2;
      }

      inline T squared_distance(const vec& rhs, T early_out) const
      {
         T dist2 = 0;
         for (uint i = 0; i < N; i++)
         {
            T d = m_s[i] - rhs.m_s[i];
            dist2 += d * d;
            if (dist2 > early_out)
               break;
         }
         return dist2;
      }

      inline T distance(const vec& rhs) const
      {
         T dist2 = 0;
         for (uint i = 0; i < N; i++)
         {
            T d = m_s[i] - rhs.m_s[i];
            dist2 += d * d;
         }
         return sqrt(dist2);
      }

      inline vec inverse() const
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result[i] = m_s[i] ? (1.0f / m_s[i]) : 0;
         return result;
      }

      inline double normalize(const vec* pDefaultVec = NULL)
      {
         double n = m_s[0] * m_s[0];
         for (uint i = 1; i < N; i++)
            n += m_s[i] * m_s[i];

         if (n != 0)
            *this *= static_cast<T>((1.0f / sqrt(n)));
         else if (pDefaultVec)
            *this = *pDefaultVec;
         return n;
      }

      inline double normalize3(const vec* pDefaultVec = NULL)
      {
         CRNLIB_ASSUME(N >= 3);

         double n = m_s[0] * m_s[0] + m_s[1] * m_s[1] + m_s[2] * m_s[2];

         if (n != 0)
            *this *= static_cast<T>((1.0f / sqrt(n)));
         else if (pDefaultVec)
            *this = *pDefaultVec;
         return n;
      }

      inline vec& normalize_in_place(const vec* pDefaultVec = NULL)
      {
         normalize(pDefaultVec);
         return *this;
      }

      inline vec& normalize3_in_place(const vec* pDefaultVec = NULL)
      {
         normalize3(pDefaultVec);
         return *this;
      }

      inline vec get_normalized(const vec* pDefaultVec = NULL) const
      {
         vec result(*this);
         result.normalize(pDefaultVec);
         return result;
      }

      inline vec get_normalized3(const vec* pDefaultVec = NULL) const
      {
         vec result(*this);
         result.normalize3(pDefaultVec);
         return result;
      }

      inline vec& clamp(T l, T h)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] = static_cast<T>(math::clamp(m_s[i], l, h));
         return *this;
      }

      inline vec& clamp(const vec& l, const vec& h)
      {
         for (uint i = 0; i < N; i++)
            m_s[i] = static_cast<T>(math::clamp(m_s[i], l[i], h[i]));
         return *this;
      }

      inline bool is_within_bounds(const vec& l, const vec& h) const
      {
         for (uint i = 0; i < N; i++)
            if ((m_s[i] < l[i]) || (m_s[i] > h[i]))
               return false;

         return true;
      }

      inline bool is_within_bounds(T l, T h) const
      {
         for (uint i = 0; i < N; i++)
            if ((m_s[i] < l) || (m_s[i] > h))
               return false;

         return true;
      }

      inline uint get_major_axis(void) const
      {
         T m = fabs(m_s[0]);
         uint r = 0;
         for (uint i = 1; i < N; i++)
         {
            const T c = fabs(m_s[i]);
            if (c > m)
            {
               m = c;
               r = i;
            }
         }
         return r;
      }

      inline uint get_minor_axis(void) const
      {
         T m = fabs(m_s[0]);
         uint r = 0;
         for (uint i = 1; i < N; i++)
         {
            const T c = fabs(m_s[i]);
            if (c < m)
            {
               m = c;
               r = i;
            }
         }
         return r;
      }

      inline T get_absolute_minimum(void) const
      {
         T result = fabs(m_s[0]);
         for (uint i = 1; i < N; i++)
            result = math::minimum(result, fabs(m_s[i]));
         return result;
      }

      inline T get_absolute_maximum(void) const
      {
         T result = fabs(m_s[0]);
         for (uint i = 1; i < N; i++)
            result = math::maximum(result, fabs(m_s[i]));
         return result;
      }

      inline T get_minimum(void) const
      {
         T result = m_s[0];
         for (uint i = 1; i < N; i++)
            result = math::minimum(result, m_s[i]);
         return result;
      }

      inline T get_maximum(void) const
      {
         T result = m_s[0];
         for (uint i = 1; i < N; i++)
            result = math::maximum(result, m_s[i]);
         return result;
      }

      inline vec& remove_unit_direction(const vec& dir)
      {
         T p = *this * dir;
         *this -= (p * dir);
         return *this;
      }

      inline bool all_less(const vec& b) const
      {
         for (uint i = 0; i < N; i++)
            if (m_s[i] >= b.m_s[i])
               return false;
         return true;
      }

      inline bool all_less_equal(const vec& b) const
      {
         for (uint i = 0; i < N; i++)
            if (m_s[i] > b.m_s[i])
               return false;
         return true;
      }

      inline bool all_greater(const vec& b) const
      {
         for (uint i = 0; i < N; i++)
            if (m_s[i] <= b.m_s[i])
               return false;
         return true;
      }

      inline bool all_greater_equal(const vec& b) const
      {
         for (uint i = 0; i < N; i++)
            if (m_s[i] < b.m_s[i])
               return false;
         return true;
      }

      inline vec get_negate_xyz() const
      {
         vec ret;

                     ret[0] = -m_s[0];
         if (N >= 2) ret[1] = -m_s[1];
         if (N >= 3) ret[2] = -m_s[2];

         for (uint i = 3; i < N; i++)
            ret[i] = m_s[i];

         return ret;
      }

      inline vec& invert()
      {
         for (uint i = 0; i < N; i++)
            if (m_s[i] != 0.0f)
               m_s[i] = 1.0f / m_s[i];
         return *this;
      }

      static inline vec mul_components(const vec& lhs, const vec& rhs)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result[i] = lhs.m_s[i] * rhs.m_s[i];
         return result;
      }

      static inline vec make_axis(uint i)
      {
         vec result;
         result.clear();
         result[i] = 1;
         return result;
      }

      static inline vec component_max(const vec& a, const vec& b)
      {
         vec ret;
         for (uint i = 0; i < N; i++)
            ret.m_s[i] = math::maximum(a.m_s[i], b.m_s[i]);
         return ret;
      }

      static inline vec component_min(const vec& a, const vec& b)
      {
         vec ret;
         for (uint i = 0; i < N; i++)
            ret.m_s[i] = math::minimum(a.m_s[i], b.m_s[i]);
         return ret;
      }

      static inline vec lerp(const vec& a, const vec& b, float t)
		{
			vec ret;
			for (uint i = 0; i < N; i++)
				ret.m_s[i] = a.m_s[i] + (b.m_s[i] - a.m_s[i]) * t;
			return ret;
		}
      
      static inline vec make_random(random& r, float l, float h)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result[i] = r.frand(l, h);
         return result;
      }

      static inline vec make_random(fast_random& r, float l, float h)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result[i] = r.frand(l, h);
         return result;
      }

      static inline vec make_random(random& r, const vec& l, const vec& h)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result[i] = r.frand(l[i], h[i]);
         return result;
      }

      static inline vec make_random(fast_random& r, const vec& l, const vec& h)
      {
         vec result;
         for (uint i = 0; i < N; i++)
            result[i] = r.frand(l[i], h[i]);
         return result;
      }

   private:
      T m_s[N];
   };

   typedef vec<1, double> vec1D;
   typedef vec<2, double> vec2D;
   typedef vec<3, double> vec3D;
   typedef vec<4, double> vec4D;

   typedef vec<1, float> vec1F;

   typedef vec<2, float> vec2F;
   typedef crnlib::vector<vec2F> vec2F_array;

   typedef vec<3, float> vec3F;
   typedef crnlib::vector<vec3F> vec3F_array;

   typedef vec<4, float> vec4F;
   typedef crnlib::vector<vec4F> vec4F_array;

   typedef vec<2, int> vec2I;
   typedef vec<3, int> vec3I;

   typedef vec<2, int16> vec2I16;
   typedef vec<3, int16> vec3I16;

   template<uint N, typename T>
   struct scalar_type< vec<N, T> >
   {
      enum { cFlag = true };
      static inline void construct(vec<N, T>* p) { }
      static inline void construct(vec<N, T>* p, const vec<N, T>& init) { memcpy(p, &init, sizeof(vec<N, T>)); }
      static inline void construct_array(vec<N, T>* p, uint n) { p, n; }
      static inline void destruct(vec<N, T>* p) { p; }
      static inline void destruct_array(vec<N, T>* p, uint n) { p, n; }
   };

} // namespace crnlib

