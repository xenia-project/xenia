// File: crn_vector2d.h
#pragma once

namespace crnlib
{
   template <typename T>
   class vector2D
   {
   public:
      typedef crnlib::vector<T> vector_type;
      typedef T                 value_type;
      typedef T&                reference;
      typedef const T&          const_reference;
      typedef T*                pointer;
      typedef const T*          const_pointer;
      
      inline vector2D(uint width = 0, uint height = 0, const T& def = T()) :
         m_width(width),
         m_height(height),
         m_vec(width * height),
         m_def(def)
      {
      }
      
      inline vector2D(const vector2D& other) :
         m_width(other.m_width),
         m_height(other.m_height),
         m_vec(other.m_vec),
         m_def(other.m_def)
      {
      }
      
      inline vector2D& operator= (const vector2D& rhs)
      {
         if (this == &rhs)
            return *this;
      
         m_width = rhs.m_width;
         m_height = rhs.m_height;
         m_vec = rhs.m_vec;
         
         return *this;      
      }
                  
      bool try_resize(uint width, uint height, bool preserve = true)
      {
         if ((width == m_width) && (height == m_height))
            return true;

         vector_type new_vec;
         if (!new_vec.try_resize(width * height))
            return false;
            
         if (preserve)
         {
            const uint nx = math::minimum(width, m_width);
            const uint ny = math::minimum(height, m_height);

            for (uint y = 0; y < ny; y++)
            {
               const T* pSrc = &m_vec[y * m_width];
               T* pDst = &new_vec[y * width];
               if (CRNLIB_IS_BITWISE_COPYABLE_OR_MOVABLE(T))
                  memcpy(pDst, pSrc, nx * sizeof(T));
               else
               {
                  for (uint x = 0; x < nx; x++)
                     *pDst++ = *pSrc++;
               }
            }
         }                  

         m_width = width;
         m_height = height;
         m_vec.swap(new_vec);
         
         return true;
      }
      
      void resize(uint width, uint height, bool preserve = true)
      {
         if (!try_resize(width, height, preserve))
         {
            CRNLIB_FAIL("vector2D::resize: Out of memory");
         }
      }
      
      inline void clear()
      {
         m_vec.clear();
         m_width = 0;
         m_height = 0;
      }
      
      inline uint width() const { return m_width; }
      inline uint height() const { return m_height; }
      inline uint size() const { return m_vec.size(); }
      
      inline uint size_in_bytes() const { return m_vec.size() * sizeof(T); }
                                    
      const vector_type& get_vec() const  { return m_vec; }
            vector_type& get_vec()        { return m_vec; }
      
      inline const   T* get_ptr() const   { return m_vec.get_ptr(); }
      inline         T* get_ptr()         { return m_vec.get_ptr(); }
            
      inline const T& operator[] (uint i) const { return m_vec[i]; }
      inline       T& operator[] (uint i)       { return m_vec[i]; }
      
      inline const T& operator() (uint x, uint y) const
      {
         CRNLIB_ASSERT((x < m_width) && (y < m_height));
         return m_vec[x + y * m_width];
      }
      
      inline T& operator() (uint x, uint y) 
      {
         CRNLIB_ASSERT((x < m_width) && (y < m_height));
         return m_vec[x + y * m_width];
      }
      
      inline const T& at (uint x, uint y) const
      {
         if ((x >= m_width) || (y >= m_height))
            return m_def;
         return m_vec[x + y * m_width];
      }

      inline T& at (uint x, uint y) 
      {
         if ((x >= m_width) || (y >= m_height))
            return m_def;
         return m_vec[x + y * m_width];
      }
      
      inline void swap(vector2D& other)
      {
         m_vec.swap(other.m_vec);
         anvil::swap(m_width, other.m_width);
         anvil::swap(m_height, other.m_height);
      }
      
      inline void set_all(const T& x)
      {
         m_vec.set_all(x);
      }
   
   private:
      vector_type m_vec;
      uint        m_width;
      uint        m_height;
      T           m_def;
   };

} // namespace anvil
