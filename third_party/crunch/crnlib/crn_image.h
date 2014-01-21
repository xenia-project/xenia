// File: crn_image.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_color.h"
#include "crn_vec.h"
#include "crn_pixel_format.h"
#include "crn_rect.h"

namespace crnlib
{
   template<typename color_type>
   class image
   {
   public:
      typedef color_type color_t;

      typedef crnlib::vector<color_type> pixel_buf_t;

      image() :
         m_width(0),
         m_height(0),
         m_pitch(0),
         m_total(0),
         m_comp_flags(pixel_format_helpers::cDefaultCompFlags),
         m_pPixels(NULL)
      {
      }

      // pitch is in PIXELS, not bytes.
      image(uint width, uint height, uint pitch = UINT_MAX, const color_type& background = color_type::make_black(), uint flags = pixel_format_helpers::cDefaultCompFlags) :
         m_comp_flags(flags)
      {
         CRNLIB_ASSERT((width > 0) && (height > 0));
         if (pitch == UINT_MAX)
            pitch = width;

         m_pixel_buf.resize(pitch * height);

         m_width = width;
         m_height = height;
         m_pitch = pitch;
         m_total = m_pitch * m_height;

         m_pPixels = &m_pixel_buf.front();

         set_all(background);
      }

      // pitch is in PIXELS, not bytes.
      image(color_type* pPixels, uint width, uint height, uint pitch = UINT_MAX, uint flags = pixel_format_helpers::cDefaultCompFlags)
      {
         alias(pPixels, width, height, pitch, flags);
      }

      image& operator= (const image& other)
      {
         if (this == &other)
            return *this;

         if (other.m_pixel_buf.empty())
         {
            // This doesn't look very safe - let's make a new instance.
            //m_pixel_buf.clear();
            //m_pPixels = other.m_pPixels;

            const uint total_pixels = other.m_pitch * other.m_height;
            if ((total_pixels) && (other.m_pPixels))
            {
               m_pixel_buf.resize(total_pixels);
               m_pixel_buf.insert(0, other.m_pPixels, m_pixel_buf.size());
               m_pPixels = &m_pixel_buf.front();
            }
            else
            {
               m_pixel_buf.clear();
               m_pPixels = NULL;
            }
         }
         else
         {
            m_pixel_buf = other.m_pixel_buf;
            m_pPixels = &m_pixel_buf.front();
         }

         m_width = other.m_width;
         m_height = other.m_height;
         m_pitch = other.m_pitch;
         m_total = other.m_total;
         m_comp_flags = other.m_comp_flags;

         return *this;
      }

      image(const image& other) :
         m_width(0), m_height(0), m_pitch(0), m_total(0), m_comp_flags(pixel_format_helpers::cDefaultCompFlags), m_pPixels(NULL)
      {
         *this = other;
      }

      // pitch is in PIXELS, not bytes.
      void alias(color_type* pPixels, uint width, uint height, uint pitch = UINT_MAX, uint flags = pixel_format_helpers::cDefaultCompFlags)
      {
         m_pixel_buf.clear();

         m_pPixels = pPixels;

         m_width = width;
         m_height = height;
         m_pitch = (pitch == UINT_MAX) ? width : pitch;
         m_total = m_pitch * m_height;
         m_comp_flags = flags;
      }

      // pitch is in PIXELS, not bytes.
      bool grant_ownership(color_type* pPixels, uint width, uint height, uint pitch = UINT_MAX, uint flags = pixel_format_helpers::cDefaultCompFlags)
      {
         if (pitch == UINT_MAX)
            pitch = width;

         if ((!pPixels) || (!width) || (!height) || (pitch < width))
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         if (pPixels == get_ptr())
         {
            CRNLIB_ASSERT(0);
            return false;
         }
         
         clear();

         if (!m_pixel_buf.grant_ownership(pPixels, height * pitch, height * pitch))
            return false;
         
         m_pPixels = pPixels;

         m_width = width;
         m_height = height;
         m_pitch = pitch;
         m_total = pitch * height;
         m_comp_flags = flags;
         
         return true;
      }

      void clear()
      {
         m_pPixels = NULL;
         m_pixel_buf.clear();
         m_width = 0;
         m_height = 0;
         m_pitch = 0;
         m_total = 0;
         m_comp_flags = pixel_format_helpers::cDefaultCompFlags;
      }

      inline bool is_valid() const { return m_total > 0; }

      inline pixel_format_helpers::component_flags get_comp_flags() const { return static_cast<pixel_format_helpers::component_flags>(m_comp_flags); }
      inline void set_comp_flags(pixel_format_helpers::component_flags new_flags) { m_comp_flags = new_flags; }
      inline void reset_comp_flags() { m_comp_flags = pixel_format_helpers::cDefaultCompFlags; }

      inline bool is_component_valid(uint index) const { CRNLIB_ASSERT(index < 4U); return utils::is_flag_set(m_comp_flags, index); }
      inline void set_component_valid(uint index, bool state) { CRNLIB_ASSERT(index < 4U); utils::set_flag(m_comp_flags, index, state); }

      inline bool has_rgb() const { return is_component_valid(0) || is_component_valid(1) || is_component_valid(2); }
      inline bool has_alpha() const { return is_component_valid(3); }

      inline bool is_grayscale() const { return utils::is_bit_set(m_comp_flags, pixel_format_helpers::cCompFlagGrayscale); }
      inline void set_grayscale(bool state) { utils::set_bit(m_comp_flags, pixel_format_helpers::cCompFlagGrayscale, state); }

      void set_all(const color_type& c)
      {
         for (uint i = 0; i < m_total; i++)
            m_pPixels[i] = c;
      }

      void flip_x()
      {
         const uint half_width = m_width / 2;
         for (uint y = 0; y < m_height; y++)
         {
            for (uint x = 0; x < half_width; x++)
            {
               color_type c((*this)(x, y));
               (*this)(x, y) = (*this)(m_width - 1 - x, y);
               (*this)(m_width - 1 - x, y) = c;
            }
         }
      }

      void flip_y()
      {
         const uint half_height = m_height / 2;
         for (uint y = 0; y < half_height; y++)
         {
            for (uint x = 0; x < m_width; x++)
            {
               color_type c((*this)(x, y));
               (*this)(x, y) = (*this)(x, m_height - 1 - y);
               (*this)(x, m_height - 1 - y) = c;
            }
         }
      }

      void convert_to_grayscale()
      {
         for (uint y = 0; y < m_height; y++)
            for (uint x = 0; x < m_width; x++)
            {
               color_type c((*this)(x, y));
               typename color_type::component_t l = static_cast< typename color_type::component_t >(c.get_luma());
               c.r = l;
               c.g = l;
               c.b = l;
               (*this)(x, y) = c;
            }

         set_grayscale(true);
      }

      void swizzle(uint r, uint g, uint b, uint a)
      {
         for (uint y = 0; y < m_height; y++)
            for (uint x = 0; x < m_width; x++)
            {
               const color_type& c = (*this)(x, y);

               (*this)(x, y) = color_type(c[r], c[g], c[b], c[a]);
            }
      }

      void set_alpha_to_luma()
      {
         for (uint y = 0; y < m_height; y++)
            for (uint x = 0; x < m_width; x++)
            {
               color_type c((*this)(x, y));
               typename color_type::component_t l = static_cast< typename color_type::component_t >(c.get_luma());
               c.a = l;
               (*this)(x, y) = c;
            }

         set_component_valid(3, true);
      }

      bool extract_block(color_type* pDst, uint x, uint y, uint w, uint h, bool flip_xy = false) const
      {
         if ((x >= m_width) || (y >= m_height))
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         if (flip_xy)
         {
            for (uint y_ofs = 0; y_ofs < h; y_ofs++)
               for (uint x_ofs = 0; x_ofs < w; x_ofs++)
                  pDst[x_ofs * h + y_ofs] = get_clamped(x_ofs + x, y_ofs + y);      // 5/4/12 - this was incorrectly x_ofs * 4
         }
         else if (((x + w) > m_width) || ((y + h) > m_height))
         {
            for (uint y_ofs = 0; y_ofs < h; y_ofs++)
               for (uint x_ofs = 0; x_ofs < w; x_ofs++)
                  *pDst++ = get_clamped(x_ofs + x, y_ofs + y);
         }
         else
         {
            const color_type* pSrc = get_scanline(y) + x;

            for (uint i = h; i; i--)
            {
               memcpy(pDst, pSrc, w * sizeof(color_type));
               pDst += w;

               pSrc += m_pitch;
            }
         }

         return true;
      }

      // No clipping!
      void unclipped_fill_box(uint x, uint y, uint w, uint h, const color_type& c)
      {
         if (((x + w) > m_width) || ((y + h) > m_height))
         {
            CRNLIB_ASSERT(0);
            return;
         }

         color_type* p = get_scanline(y) + x;

         for (uint i = h; i; i--)
         {
            color_type* q = p;
            for (uint j = w; j; j--)
               *q++ = c;
            p += m_pitch;
         }
      }

      void draw_rect(int x, int y, uint width, uint height, const color_type& c)
      {
         draw_line(x, y, x + width - 1, y, c);
         draw_line(x, y, x, y + height - 1, c);
         draw_line(x + width - 1, y, x + width - 1, y + height - 1, c);
         draw_line(x, y + height - 1, x + width - 1, y + height - 1, c);
      }

      // No clipping!
      bool unclipped_blit(uint src_x, uint src_y, uint src_w, uint src_h, uint dst_x, uint dst_y, const image& src)
      {
         if ((!is_valid()) || (!src.is_valid()))
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         if ( ((src_x + src_w) > src.get_width()) || ((src_y + src_h) > src.get_height()) )
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         if ( ((dst_x + src_w) > get_width()) || ((dst_y + src_h) > get_height()) )
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         const color_type* pS = &src(src_x, src_y);
         color_type* pD = &(*this)(dst_x, dst_y);

         const uint bytes_to_copy = src_w * sizeof(color_type);
         for (uint i = src_h; i; i--)
         {
            memcpy(pD, pS, bytes_to_copy);

            pS += src.get_pitch();
            pD += get_pitch();
         }

         return true;
      }

      // With clipping.
      bool blit(int dst_x, int dst_y, const image& src)
      {
         if ((!is_valid()) || (!src.is_valid()))
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         int src_x = 0;
         int src_y = 0;

         if (dst_x < 0)
         {
            src_x = -dst_x;
            if (src_x >= static_cast<int>(src.get_width()))
               return false;
            dst_x = 0;
         }

         if (dst_y < 0)
         {
            src_y = -dst_y;
            if (src_y >= static_cast<int>(src.get_height()))
               return false;
            dst_y = 0;
         }

         if ((dst_x >= (int)m_width) || (dst_y >= (int)m_height))
            return false;

         uint width = math::minimum(m_width - dst_x, src.get_width() - src_x);
         uint height = math::minimum(m_height - dst_y, src.get_height() - src_y);

         bool success = unclipped_blit(src_x, src_y, width, height, dst_x, dst_y, src);
         success;
         CRNLIB_ASSERT(success);

         return true;
      }

      // With clipping.
      bool blit(int src_x, int src_y, int src_w, int src_h, int dst_x, int dst_y, const image& src)
      {
         if ((!is_valid()) || (!src.is_valid()))
         {
            CRNLIB_ASSERT(0);
            return false;
         }

         rect src_rect(src_x, src_y, src_x + src_w, src_y + src_h);
         if (!src_rect.intersect(src.get_bounds()))
            return false;

         rect dst_rect(dst_x, dst_y, dst_x + src_rect.get_width(), dst_y + src_rect.get_height());
         if (!dst_rect.intersect(get_bounds()))
            return false;
         
         bool success = unclipped_blit(
            src_rect.get_left(), src_rect.get_top(), 
            math::minimum(src_rect.get_width(), dst_rect.get_width()), math::minimum(src_rect.get_height(), dst_rect.get_height()),
            dst_rect.get_left(), dst_rect.get_top(), src);
         success;
         CRNLIB_ASSERT(success);

         return true;
      }

      // In-place resize of image dimensions (cropping).
      bool resize(uint new_width, uint new_height, uint new_pitch = UINT_MAX, const color_type background = color_type::make_black())
      {
         if (new_pitch == UINT_MAX)
            new_pitch = new_width;

         if ((new_width == m_width) && (new_height == m_height) && (new_pitch == m_pitch))
            return true;

         if ((!new_width) || (!new_height) || (!new_pitch))
         {
            clear();
            return false;
         }

         pixel_buf_t existing_pixels;
         existing_pixels.swap(m_pixel_buf);

         if (!m_pixel_buf.try_resize(new_height * new_pitch))
         {
            clear();
            return false;
         }

         for (uint y = 0; y < new_height; y++)
         {
            for (uint x = 0; x < new_width; x++)
            {
               if ((x < m_width) && (y < m_height))
                  m_pixel_buf[x + y * new_pitch] = existing_pixels[x + y * m_pitch];
               else
                  m_pixel_buf[x + y * new_pitch] = background;
            }
         }

         m_width = new_width;
         m_height = new_height;
         m_pitch = new_pitch;
         m_total = new_pitch * new_height;
         m_pPixels = &m_pixel_buf.front();

         return true;
      }

      inline uint get_width() const { return m_width; }
      inline uint get_height() const { return m_height; }
      inline uint get_total_pixels() const { return m_width * m_height; }

      inline rect get_bounds() const { return rect(0, 0, m_width, m_height); }

      inline uint get_pitch() const { return m_pitch; }
      inline uint get_pitch_in_bytes() const { return m_pitch * sizeof(color_type); }

      // Returns pitch * height, NOT width * height!
      inline uint get_total() const { return m_total; }

      inline uint get_block_width(uint block_size) const { return (m_width + block_size - 1) / block_size; }
      inline uint get_block_height(uint block_size) const { return (m_height + block_size - 1) / block_size; }
      inline uint get_total_blocks(uint block_size) const { return get_block_width(block_size) * get_block_height(block_size); }

      inline uint get_size_in_bytes() const { return sizeof(color_type) * m_total; }

      inline const color_type* get_pixels() const  { return m_pPixels; }
      inline       color_type* get_pixels()        { return m_pPixels; }

      inline const color_type& operator() (uint x, uint y) const
      {
         CRNLIB_ASSERT((x < m_width) && (y < m_height));
         return m_pPixels[x + y * m_pitch];
      }

      inline color_type& operator() (uint x, uint y)
      {
         CRNLIB_ASSERT((x < m_width) && (y < m_height));
         return m_pPixels[x + y * m_pitch];
      }

      inline const color_type& get_unclamped(uint x, uint y) const
      {
         CRNLIB_ASSERT((x < m_width) && (y < m_height));
         return m_pPixels[x + y * m_pitch];
      }

      inline const color_type& get_clamped(int x, int y) const
      {
         x = math::clamp<int>(x, 0, m_width - 1);
         y = math::clamp<int>(y, 0, m_height - 1);
         return m_pPixels[x + y * m_pitch];
      }

      // Sample image with bilinear filtering.
      // (x,y) - Continuous coordinates, where pixel centers are at (.5,.5), valid image coords are [0,width] and [0,height].
      void get_filtered(float x, float y, color_type& result) const
      {
         x -= .5f;
         y -= .5f;

         int ix = (int)floor(x);
         int iy = (int)floor(y);
         float wx = x - ix;
         float wy = y - iy;

         color_type a(get_clamped(ix, iy));
         color_type b(get_clamped(ix + 1, iy));
         color_type c(get_clamped(ix, iy + 1));
         color_type d(get_clamped(ix + 1, iy + 1));

         for (uint i = 0; i < 4; i++)
         {
            double top = math::lerp<double>(a[i], b[i], wx);
            double bot = math::lerp<double>(c[i], d[i], wx);
            double m = math::lerp<double>(top, bot, wy);

            if (!color_type::component_traits::cFloat)
               m += .5f;

            result.set_component(i, static_cast< typename color_type::parameter_t >(m));
         }
      }

      void get_filtered(float x, float y, vec4F& result) const
      {
         x -= .5f;
         y -= .5f;

         int ix = (int)floor(x);
         int iy = (int)floor(y);
         float wx = x - ix;
         float wy = y - iy;

         color_type a(get_clamped(ix, iy));
         color_type b(get_clamped(ix + 1, iy));
         color_type c(get_clamped(ix, iy + 1));
         color_type d(get_clamped(ix + 1, iy + 1));

         for (uint i = 0; i < 4; i++)
         {
            float top = math::lerp<float>(a[i], b[i], wx);
            float bot = math::lerp<float>(c[i], d[i], wx);
            float m = math::lerp<float>(top, bot, wy);

            result[i] = m;
         }
      }

      inline void set_pixel_unclipped(uint x, uint y, const color_type& c)
      {
         CRNLIB_ASSERT((x < m_width) && (y < m_height));
         m_pPixels[x + y * m_pitch] = c;
      }

      inline void set_pixel_clipped(int x, int y, const color_type& c)
      {
         if ((static_cast<uint>(x) >= m_width) || (static_cast<uint>(y) >= m_height))
            return;

         m_pPixels[x + y * m_pitch] = c;
      }

      inline const color_type* get_scanline(uint y) const
      {
         CRNLIB_ASSERT(y < m_height);
         return &m_pPixels[y * m_pitch];
      }

      inline color_type* get_scanline(uint y)
      {
         CRNLIB_ASSERT(y < m_height);
         return &m_pPixels[y * m_pitch];
      }

      inline const color_type* get_ptr() const
      {
         return m_pPixels;
      }

      inline color_type* get_ptr()
      {
         return m_pPixels;
      }

      inline void swap(image& other)
      {
         utils::swap(m_width, other.m_width);
         utils::swap(m_height, other.m_height);
         utils::swap(m_pitch, other.m_pitch);
         utils::swap(m_total, other.m_total);
         utils::swap(m_comp_flags, other.m_comp_flags);
         utils::swap(m_pPixels, other.m_pPixels);
         m_pixel_buf.swap(other.m_pixel_buf);
      }

      void draw_line(int xs, int ys, int xe, int ye, const color_type& color)
      {
         if (xs > xe)
         {
            utils::swap(xs, xe);
            utils::swap(ys, ye);
         }

         int dx = xe - xs, dy = ye - ys;
         if (!dx)
         {
            if (ys > ye)
               utils::swap(ys, ye);
            for (int i = ys ; i <= ye ; i++)
               set_pixel_clipped(xs, i, color);
         }
         else if (!dy)
         {
            for (int i = xs ; i < xe ; i++)
               set_pixel_clipped(i, ys, color);
         }
         else if (dy > 0)
         {
            if (dy <= dx)
            {
               int e = 2 * dy - dx, e_no_inc = 2 * dy, e_inc = 2 * (dy - dx);
               rasterize_line(xs, ys, xe, ye, 0, 1, e, e_inc, e_no_inc, color);
            }
            else
            {
               int e = 2 * dx - dy, e_no_inc = 2 * dx, e_inc = 2 * (dx - dy);
               rasterize_line(xs, ys, xe, ye, 1, 1, e, e_inc, e_no_inc, color);
            }
         }
         else
         {
            dy = -dy;
            if (dy <= dx)
            {
               int e = 2 * dy - dx, e_no_inc = 2 * dy, e_inc = 2 * (dy - dx);
               rasterize_line(xs, ys, xe, ye, 0, -1, e, e_inc, e_no_inc, color);
            }
            else
            {
               int e = 2 * dx - dy, e_no_inc = (2 * dx), e_inc = 2 * (dx - dy);
               rasterize_line(xe, ye, xs, ys, 1, -1, e, e_inc, e_no_inc, color);
            }
         }
      }

      const pixel_buf_t& get_pixel_buf() const  { return m_pixel_buf; }
            pixel_buf_t& get_pixel_buf()        { return m_pixel_buf; }

   private:
      uint                    m_width;
      uint                    m_height;
      uint                    m_pitch;
      uint                    m_total;
      uint                    m_comp_flags;

      color_type*             m_pPixels;

      pixel_buf_t             m_pixel_buf;

      void rasterize_line(int xs, int ys, int xe, int ye, int pred, int inc_dec, int e, int e_inc, int e_no_inc, const color_type& color)
      {
         int start, end, var;

         if (pred)
         {
            start = ys; end = ye; var = xs;
            for (int i = start; i <= end; i++)
            {
               set_pixel_clipped(var, i, color);
               if (e < 0)
                  e += e_no_inc;
               else
               {
                  var += inc_dec;
                  e += e_inc;
               }
            }
         }
         else
         {
            start = xs; end = xe; var = ys;
            for (int i = start; i <= end; i++)
            {
               set_pixel_clipped(i, var, color);
               if (e < 0)
                  e += e_no_inc;
               else
               {
                  var += inc_dec;
                  e += e_inc;
               }
            }
         }
      }
   };

   typedef image<color_quad_u8>  image_u8;
   typedef image<color_quad_i16> image_i16;
   typedef image<color_quad_u16> image_u16;
   typedef image<color_quad_i32> image_i32;
   typedef image<color_quad_u32> image_u32;
   typedef image<color_quad_f>   image_f;

   template<typename color_type>
   inline void swap(image<color_type>& a, image<color_type>& b)
   {
      a.swap(b);
   }

} // namespace crnlib
