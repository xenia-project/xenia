// File: crn_matrix.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#include "crn_vec.h"

namespace crnlib
{
   template<class X, class Y, class Z> Z& matrix_mul_helper(Z& result, const X& lhs, const Y& rhs)
   {
      CRNLIB_ASSUME(Z::num_rows == X::num_rows);
      CRNLIB_ASSUME(Z::num_cols == Y::num_cols);
      CRNLIB_ASSUME(X::num_cols == Y::num_rows);
      CRNLIB_ASSERT((&result != &lhs) && (&result != &rhs));
      for (int r = 0; r < X::num_rows; r++)
         for (int c = 0; c < Y::num_cols; c++)
         {
            typename Z::scalar_type s = lhs(r, 0) * rhs(0, c);
            for (uint i = 1; i < X::num_cols; i++)
               s += lhs(r, i) * rhs(i, c);
            result(r, c) = s;
         }
         return result;
   }

   template<class X, class Y, class Z> Z& matrix_mul_helper_transpose_lhs(Z& result, const X& lhs, const Y& rhs)
   {
      CRNLIB_ASSUME(Z::num_rows == X::num_cols);
      CRNLIB_ASSUME(Z::num_cols == Y::num_cols);
      CRNLIB_ASSUME(X::num_rows == Y::num_rows);
      for (int r = 0; r < X::num_cols; r++)
         for (int c = 0; c < Y::num_cols; c++)
         {
            typename Z::scalar_type s = lhs(0, r) * rhs(0, c);
            for (uint i = 1; i < X::num_rows; i++)
               s += lhs(i, r) * rhs(i, c);
            result(r, c) = s;
         }
         return result;
   }

   template<class X, class Y, class Z> Z& matrix_mul_helper_transpose_rhs(Z& result, const X& lhs, const Y& rhs)
   {
      CRNLIB_ASSUME(Z::num_rows == X::num_rows);
      CRNLIB_ASSUME(Z::num_cols == Y::num_rows);
      CRNLIB_ASSUME(X::num_cols == Y::num_cols);
      for (int r = 0; r < X::num_rows; r++)
         for (int c = 0; c < Y::num_rows; c++)
         {
            typename Z::scalar_type s = lhs(r, 0) * rhs(c, 0);
            for (uint i = 1; i < X::num_cols; i++)
               s += lhs(r, i) * rhs(c, i);
            result(r, c) = s;
         }
         return result;
   }

   template<uint R, uint C, typename T>
   class matrix
   {
   public:
      typedef T scalar_type;
      enum { num_rows = R, num_cols = C };

      typedef vec<R, T> col_vec;
      typedef vec<(R > 1) ? (R - 1) : 0, T> subcol_vec;

      typedef vec<C, T> row_vec;
      typedef vec<(C > 1) ? (C - 1) : 0, T> subrow_vec;

      inline matrix() { }

      inline matrix(eClear) { clear(); }

      inline matrix(const T* p) { set(p); }

      inline matrix(const matrix& other)
      {
         for (uint i = 0; i < R; i++)
            m_rows[i] = other.m_rows[i];
      }

      inline matrix& operator= (const matrix& rhs)
      {
         if (this != &rhs)
            for (uint i = 0; i < R; i++)
               m_rows[i] = rhs.m_rows[i];
         return *this;
      }

      inline matrix(T val00, T val01,
                    T val10, T val11)
      {
         set(val00, val01,  val10, val11);
      }

      inline matrix(T val00, T val01, T val02,
                    T val10, T val11, T val12,
                    T val20, T val21, T val22)
      {
         set(val00, val01, val02,  val10, val11, val12,  val20, val21, val22);
      }

      inline matrix(T val00, T val01, T val02, T val03,
                    T val10, T val11, T val12, T val13,
                    T val20, T val21, T val22, T val23,
                    T val30, T val31, T val32, T val33)
      {
         set(val00, val01, val02, val03, val10, val11, val12, val13, val20, val21, val22, val23, val30, val31, val32, val33);
      }

      inline void set(const float* p)
      {
         for (uint i = 0; i < R; i++)
         {
            m_rows[i].set(p);
            p += C;
         }
      }

      inline void set(T val00, T val01,
                      T val10, T val11)
      {
         m_rows[0].set(val00, val01);
         if (R >= 2)
         {
            m_rows[1].set(val10, val11);

            for (uint i = 2; i < R; i++)
               m_rows[i].clear();
         }
      }

      inline void set(T val00, T val01, T val02,
                      T val10, T val11, T val12,
                      T val20, T val21, T val22)
      {
         m_rows[0].set(val00, val01, val02);
         if (R >= 2)
         {
            m_rows[1].set(val10, val11, val12);
            if (R >= 3)
            {
               m_rows[2].set(val20, val21, val22);

               for (uint i = 3; i < R; i++)
                  m_rows[i].clear();
            }
         }
      }

      inline void set(T val00, T val01, T val02, T val03,
                      T val10, T val11, T val12, T val13,
                      T val20, T val21, T val22, T val23,
                      T val30, T val31, T val32, T val33)
      {
         m_rows[0].set(val00, val01, val02, val03);
         if (R >= 2)
         {
            m_rows[1].set(val10, val11, val12, val13);
            if (R >= 3)
            {
               m_rows[2].set(val20, val21, val22, val23);

               if (R >= 4)
               {
                  m_rows[3].set(val30, val31, val32, val33);

                  for (uint i = 4; i < R; i++)
                     m_rows[i].clear();
               }
            }
         }
      }

      inline T operator() (uint r, uint c) const
      {
         CRNLIB_ASSERT((r < R) && (c < C));
         return m_rows[r][c];
      }

      inline T& operator() (uint r, uint c)
      {
         CRNLIB_ASSERT((r < R) && (c < C));
         return m_rows[r][c];
      }

      inline const row_vec& operator[] (uint r) const
      {
         CRNLIB_ASSERT(r < R);
         return m_rows[r];
      }

      inline row_vec& operator[] (uint r)
      {
         CRNLIB_ASSERT(r < R);
         return m_rows[r];
      }

      inline const row_vec& get_row (uint r) const  { return (*this)[r]; }
      inline       row_vec& get_row (uint r)        { return (*this)[r]; }

      inline col_vec get_col(uint c) const
      {
         CRNLIB_ASSERT(c < C);
         col_vec result;
         for (uint i = 0; i < R; i++)
            result[i] = m_rows[i][c];
         return result;
      }

      inline void set_col(uint c, const col_vec& col)
      {
         CRNLIB_ASSERT(c < C);
         for (uint i = 0; i < R; i++)
            m_rows[i][c] = col[i];
      }

      inline void set_col(uint c, const subcol_vec& col)
      {
         CRNLIB_ASSERT(c < C);
         for (uint i = 0; i < (R - 1); i++)
            m_rows[i][c] = col[i];

         m_rows[R - 1][c] = 0.0f;
      }

      inline const row_vec& get_translate() const
      {
         return m_rows[R - 1];
      }

      inline matrix& set_translate(const row_vec& r)
      {
         m_rows[R - 1] = r;
         return *this;
      }

      inline matrix& set_translate(const subrow_vec& r)
      {
         m_rows[R - 1] = row_vec(r).as_point();
         return *this;
      }

      inline const T* get_ptr() const   { return reinterpret_cast<const T*>(&m_rows[0]); }
      inline       T* get_ptr()         { return reinterpret_cast<      T*>(&m_rows[0]); }

      inline matrix& operator+= (const matrix& other)
      {
         for (uint i = 0; i < R; i++)
            m_rows[i] += other.m_rows[i];
         return *this;
      }

      inline matrix& operator-= (const matrix& other)
      {
         for (uint i = 0; i < R; i++)
            m_rows[i] -= other.m_rows[i];
         return *this;
      }

      inline matrix& operator*= (T val)
      {
         for (uint i = 0; i < R; i++)
            m_rows[i] *= val;
         return *this;
      }

      inline matrix& operator/= (T val)
      {
         for (uint i = 0; i < R; i++)
            m_rows[i] /= val;
         return *this;
      }

      inline matrix& operator*= (const matrix& other)
      {
         matrix result;
         matrix_mul_helper(result, *this, other);
         *this = result;
         return *this;
      }

      friend inline matrix operator+ (const matrix& lhs, const matrix& rhs)
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            result[i] = lhs.m_rows[i] + rhs.m_rows[i];
         return result;
      }

      friend inline matrix operator- (const matrix& lhs, const matrix& rhs)
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            result[i] = lhs.m_rows[i] - rhs.m_rows[i];
         return result;
      }

      friend inline matrix operator* (const matrix& lhs, T val)
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            result[i] = lhs.m_rows[i] * val;
         return result;
      }

      friend inline matrix operator/ (const matrix& lhs, T val)
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            result[i] = lhs.m_rows[i] / val;
         return result;
      }

      friend inline matrix operator* (T val, const matrix& rhs)
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            result[i] = val * rhs.m_rows[i];
         return result;
      }

      friend inline matrix operator* (const matrix& lhs, const matrix& rhs)
      {
         matrix result;
         return matrix_mul_helper(result, lhs, rhs);
      }

      friend inline row_vec operator* (const col_vec& a, const matrix& b)
      {
         return transform(a, b);
      }

      inline matrix operator+ () const
      {
         return *this;
      }

      inline matrix operator- () const
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            result[i] = -m_rows[i];
         return result;
      }

      inline void clear(void)
      {
         for (uint i = 0; i < R; i++)
            m_rows[i].clear();
      }

      inline void set_zero_matrix()
      {
         clear();
      }

      inline void set_identity_matrix()
      {
         for (uint i = 0; i < R; i++)
         {
            m_rows[i].clear();
            m_rows[i][i] = 1.0f;
         }
      }

      inline matrix& set_scale_matrix(float s)
      {
         clear();
         for (int i = 0; i < (R - 1); i++)
            m_rows[i][i] = s;
         m_rows[R - 1][C - 1] = 1.0f;
         return *this;
      }

      inline matrix& set_scale_matrix(const row_vec& s)
      {
         clear();
         for (uint i = 0; i < R; i++)
            m_rows[i][i] = s[i];
         return *this;
      }

      inline matrix& set_translate_matrix(const row_vec& s)
      {
         set_identity_matrix();
         set_translate(s);
         return *this;
      }

      inline matrix& set_translate_matrix(float x, float y)
      {
         set_identity_matrix();
         set_translate(row_vec(x, y).as_point());
         return *this;
      }

      inline matrix& set_translate_matrix(float x, float y, float z)
      {
         set_identity_matrix();
         set_translate(row_vec(x, y, z).as_point());
         return *this;
      }

      inline matrix get_transposed(void) const
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            for (uint j = 0; j < C; j++)
               result.m_rows[i][j] = m_rows[j][i];
         return result;
      }

      inline matrix& transpose_in_place(void)
      {
         matrix result;
         for (uint i = 0; i < R; i++)
            for (uint j = 0; j < C; j++)
               result.m_rows[i][j] = m_rows[j][i];
         *this = result;
         return *this;
      }
                  
      // This method transforms a column vec by a matrix (D3D-style).
      static inline row_vec transform(const col_vec& a, const matrix& b)
      {
         row_vec result(b[0] * a[0]);
         for (uint r = 1; r < R; r++)
            result += b[r] * a[r];
         return result;
      }

      // This method transforms a column vec by a matrix. Last component of vec is assumed to be 1.
      static inline row_vec transform_point(const col_vec& a, const matrix& b)
      {
         row_vec result(0);
         for (int r = 0; r < (R - 1); r++)
            result += b[r] * a[r];
         result += b[R - 1];
         return result;
      }

      // This method transforms a column vec by a matrix. Last component of vec is assumed to be 0.
      static inline row_vec transform_vector(const col_vec& a, const matrix& b)
      {
         row_vec result(0);
         for (int r = 0; r < (R - 1); r++)
            result += b[r] * a[r];
         return result;
      }

      static inline subcol_vec transform_point(const subcol_vec& a, const matrix& b)
      {
         subcol_vec result(0);
         for (int r = 0; r < R; r++)
         {
            const T s = (r < subcol_vec::num_elements) ? a[r] : 1.0f;
            for (int c = 0; c < (C - 1); c++)
               result[c] += b[r][c] * s;
         }
         return result;
      }

      static inline subcol_vec transform_vector(const subcol_vec& a, const matrix& b)
      {
         subcol_vec result(0);
         for (int r = 0; r < (R - 1); r++)
         {
            const T s = a[r];
            for (int c = 0; c < (C - 1); c++)
               result[c] += b[r][c] * s;
         }
         return result;
      }

      // This method transforms a column vec by the transpose of a matrix.
      static inline col_vec transform_transposed(const matrix& b, const col_vec& a)
      {
         CRNLIB_ASSUME(R == C);
         col_vec result;
         for (uint r = 0; r < R; r++)
            result[r] = b[r] * a;
         return result;
      }

      // This method transforms a column vec by the transpose of a matrix. Last component of vec is assumed to be 0.
      static inline col_vec transform_vector_transposed(const matrix& b, const col_vec& a)
      {
         CRNLIB_ASSUME(R == C);
         col_vec result;
         for (uint r = 0; r < R; r++)
         {
            T s = 0;
            for (uint c = 0; c < (C - 1); c++)
               s += b[r][c] * a[c];

            result[r] = s;
         }
         return result;
      }

      // This method transforms a matrix by a row vector (OGL style).
      static inline col_vec transform(const matrix& b, const row_vec& a)
      {
         col_vec result;
         for (int r = 0; r < R; r++)
            result[r] = b[r] * a;
         return result;
      }

      static inline matrix& multiply(matrix& result, const matrix& lhs, const matrix& rhs)
      {
         return matrix_mul_helper(result, lhs, rhs);
      }

      static inline matrix make_scale_matrix(float s)
      {
         return matrix().set_scale_matrix(s);
      }

      static inline matrix make_scale_matrix(const row_vec& s)
      {
         return matrix().set_scale_matrix(s);
      }

      static inline matrix make_scale_matrix(float x, float y)
      {
         CRNLIB_ASSUME(R >= 3 && C >= 3);
         matrix result;
         result.clear();
         result.m_rows[0][0] = x;
         result.m_rows[1][1] = y;
         result.m_rows[2][2] = 1.0f;
         return result;
      }

      static inline matrix make_scale_matrix(float x, float y, float z)
      {
         CRNLIB_ASSUME(R >= 4 && C >= 4);
         matrix result;
         result.clear();
         result.m_rows[0][0] = x;
         result.m_rows[1][1] = y;
         result.m_rows[2][2] = z;
         result.m_rows[3][3] = 1.0f;
         return result;
      }

   private:
      row_vec m_rows[R];
   };

   typedef matrix<2, 2, float> matrix22F;
   typedef matrix<2, 2, double> matrix22D;

   typedef matrix<3, 3, float> matrix33F;
   typedef matrix<3, 3, double> matrix33D;

   typedef matrix<4, 4, float> matrix44F;
   typedef matrix<4, 4, double> matrix44D;

   typedef matrix<8, 8, float> matrix88F;
   
} // namespace crnlib
