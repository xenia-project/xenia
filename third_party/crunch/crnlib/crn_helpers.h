// File: crn_helpers.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#define CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(c) c(const c&); c& operator= (const c&);
#define CRNLIB_NO_HEAP_ALLOC() private: static void* operator new(size_t); static void* operator new[](size_t);

namespace crnlib
{
   namespace helpers
   {
      template<typename T> struct rel_ops 
      {
         friend bool operator!=(const T& x, const T& y) { return (!(x == y)); }
         friend bool operator> (const T& x, const T& y) { return (y < x); }
         friend bool operator<=(const T& x, const T& y) { return (!(y < x)); }
         friend bool operator>=(const T& x, const T& y) { return (!(x < y)); }
      };
      
      template <typename T> 
      inline T* construct(T* p) 
      { 
         return new (static_cast<void*>(p)) T; 
      }

      template <typename T, typename U> 
      inline T* construct(T* p, const U& init) 
      { 
         return new (static_cast<void*>(p)) T(init); 
      }

      template <typename T> 
      inline void construct_array(T* p, uint n) 
      { 
         T* q = p + n;
         for ( ; p != q; ++p)
            new (static_cast<void*>(p)) T; 
      }
      
      template <typename T, typename U> 
      inline void construct_array(T* p, uint n, const U& init) 
      { 
         T* q = p + n;
         for ( ; p != q; ++p)
            new (static_cast<void*>(p)) T(init); 
      }

      template <typename T> 
      inline void destruct(T* p) 
      {	   
         p;
         p->~T(); 
      } 
      
      template <typename T> inline void destruct_array(T* p, uint n) 
      {	
         T* q = p + n;
         for ( ; p != q; ++p)
            p->~T();
      } 
                  
   }  // namespace helpers    
   
}  // namespace crnlib
