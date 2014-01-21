// File: crn_dxt1.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt.h"

namespace crnlib
{
   struct dxt1_solution_coordinates
   {
      inline dxt1_solution_coordinates() : m_low_color(0), m_high_color(0){ }

      inline dxt1_solution_coordinates(uint16 l, uint16 h) : m_low_color(l), m_high_color(h) { }

      inline dxt1_solution_coordinates(const color_quad_u8& l, const color_quad_u8& h, bool scaled = true) :
         m_low_color(dxt1_block::pack_color(l, scaled)),
         m_high_color(dxt1_block::pack_color(h, scaled))
      {
      }

      inline dxt1_solution_coordinates(vec3F nl, vec3F nh)
      {
#if CRNLIB_DXT_ALT_ROUNDING
         // Umm, wtf?
         nl.clamp(0.0f, .999f);
         nh.clamp(0.0f, .999f);
         color_quad_u8 l( (int)floor(nl[0] * 32.0f), (int)floor(nl[1] * 64.0f), (int)floor(nl[2] * 32.0f), 255);
         color_quad_u8 h( (int)floor(nh[0] * 32.0f), (int)floor(nh[1] * 64.0f), (int)floor(nh[2] * 32.0f), 255);
#else
         // Fixes the bins
         color_quad_u8 l( (int)floor(.5f + nl[0] * 31.0f), (int)floor(.5f + nl[1] * 63.0f), (int)floor(.5f + nl[2] * 31.0f), 255);
         color_quad_u8 h( (int)floor(.5f + nh[0] * 31.0f), (int)floor(.5f + nh[1] * 63.0f), (int)floor(.5f + nh[2] * 31.0f), 255);
#endif

         m_low_color = dxt1_block::pack_color(l, false);
         m_high_color = dxt1_block::pack_color(h, false);
      }

      uint16 m_low_color;
      uint16 m_high_color;

      inline void clear()
      {
         m_low_color = 0;
         m_high_color = 0;
      }

      inline dxt1_solution_coordinates& canonicalize()
      {
         if (m_low_color < m_high_color)
            utils::swap(m_low_color, m_high_color);
         return *this;
      }

      inline operator size_t() const { return fast_hash(this, sizeof(*this)); }

      inline bool operator== (const dxt1_solution_coordinates& other) const
      {
         uint16 l0 = math::minimum(m_low_color, m_high_color);
         uint16 h0 = math::maximum(m_low_color, m_high_color);

         uint16 l1 = math::minimum(other.m_low_color, other.m_high_color);
         uint16 h1 = math::maximum(other.m_low_color, other.m_high_color);

         return (l0 == l1) && (h0 == h1);
      }

      inline bool operator!= (const dxt1_solution_coordinates& other) const
      {
         return !(*this == other);
      }

      inline bool operator< (const dxt1_solution_coordinates& other) const
      {
         uint16 l0 = math::minimum(m_low_color, m_high_color);
         uint16 h0 = math::maximum(m_low_color, m_high_color);

         uint16 l1 = math::minimum(other.m_low_color, other.m_high_color);
         uint16 h1 = math::maximum(other.m_low_color, other.m_high_color);

         if (l0 < l1)
            return true;
         else if (l0 == l1)
         {
            if (h0 < h1)
               return true;
         }

         return false;
      }
   };

   typedef crnlib::vector<dxt1_solution_coordinates> dxt1_solution_coordinates_vec;

   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt1_solution_coordinates);

   struct unique_color
   {
      inline unique_color() { }
      inline unique_color(const color_quad_u8& color, uint weight) : m_color(color), m_weight(weight) { }

      color_quad_u8  m_color;
      uint           m_weight;

      inline bool operator< (const unique_color& c) const
      {
         return *reinterpret_cast<const uint32*>(&m_color) < *reinterpret_cast<const uint32*>(&c.m_color);
      }

      inline bool operator== (const unique_color& c) const
      {
         return *reinterpret_cast<const uint32*>(&m_color) == *reinterpret_cast<const uint32*>(&c.m_color);
      }
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(unique_color);

   class dxt1_endpoint_optimizer
   {
   public:
      dxt1_endpoint_optimizer();

      struct params
      {
         params() :
            m_block_index(0),
            m_pPixels(NULL),
            m_num_pixels(0),
            m_dxt1a_alpha_threshold(128U),
            m_quality(cCRNDXTQualityUber),
            m_pixels_have_alpha(false),
            m_use_alpha_blocks(true),
            m_perceptual(true),
            m_grayscale_sampling(false),
            m_endpoint_caching(true),
            m_use_transparent_indices_for_black(false),
            m_force_alpha_blocks(false)
         {
            m_color_weights[0] = 1;
            m_color_weights[1] = 1;
            m_color_weights[2] = 1;
         }

         uint                 m_block_index;

         const color_quad_u8* m_pPixels;
         uint                 m_num_pixels;
         uint                 m_dxt1a_alpha_threshold;

         crn_dxt_quality      m_quality;

         bool                 m_pixels_have_alpha;
         bool                 m_use_alpha_blocks;
         bool                 m_perceptual;
         bool                 m_grayscale_sampling;
         bool                 m_endpoint_caching;
         bool                 m_use_transparent_indices_for_black;
         bool                 m_force_alpha_blocks;
         int                  m_color_weights[3];
      };

      struct results
      {
         inline results() : m_pSelectors(NULL) { }

         uint64         m_error;

         uint16         m_low_color;
         uint16         m_high_color;

         uint8*         m_pSelectors;
         bool           m_alpha_block;
      };

      struct solution
      {
         solution() { }

         solution(const solution& other)
         {
            m_results = other.m_results;
            m_selectors = other.m_selectors;
            m_results.m_pSelectors = m_selectors.begin();
         }

         solution& operator= (const solution& rhs)
         {
            if (this == &rhs)
               return *this;

            m_results = rhs.m_results;
            m_selectors = rhs.m_selectors;
            m_results.m_pSelectors = m_selectors.begin();

            return *this;
         }

         results              m_results;
         crnlib::vector<uint8> m_selectors;

         inline bool operator< (const solution& other) const
         {
            return m_results.m_error < other.m_results.m_error;
         }
         static inline bool coords_equal(const solution& lhs, const solution& rhs)
         {
            return (lhs.m_results.m_low_color == rhs.m_results.m_low_color) && (lhs.m_results.m_high_color == rhs.m_results.m_high_color);
         }
      };
      typedef crnlib::vector<solution> solution_vec;

      bool compute(const params& p, results& r, solution_vec* pSolutions = NULL);

   private:
      const params*     m_pParams;
      results*          m_pResults;
      solution_vec*     m_pSolutions;

      bool              m_perceptual;
      bool              m_has_color_weighting;

      typedef crnlib::vector<unique_color> unique_color_vec;

      //typedef crnlib::hash_map<uint32, uint32, bit_hasher<uint32> > unique_color_hash_map;
      typedef crnlib::hash_map<uint32, uint32> unique_color_hash_map;
      unique_color_hash_map m_unique_color_hash_map;

      unique_color_vec  m_unique_colors;                 // excludes transparent colors!
      unique_color_vec  m_temp_unique_colors;

      uint              m_total_unique_color_weight;

      bool              m_has_transparent_pixels;

      vec3F_array       m_norm_unique_colors;
      vec3F             m_mean_norm_color;

      vec3F_array       m_norm_unique_colors_weighted;
      vec3F             m_mean_norm_color_weighted;

      vec3F             m_principle_axis;

      bool              m_all_pixels_grayscale;

      crnlib::vector<uint16> m_unique_packed_colors;
      crnlib::vector<uint8> m_trial_selectors;

      crnlib::vector<vec3F> m_low_coords;
      crnlib::vector<vec3F> m_high_coords;

      enum { cMaxPrevResults = 4 };
      dxt1_solution_coordinates m_prev_results[cMaxPrevResults];
      uint m_num_prev_results;

      crnlib::vector<vec3I> m_lo_cells;
      crnlib::vector<vec3I> m_hi_cells;

      uint m_total_evals;

      struct potential_solution
      {
         potential_solution() : m_coords(), m_error(cUINT64_MAX), m_alpha_block(false), m_valid(false)
         {
         }

         dxt1_solution_coordinates  m_coords;
         crnlib::vector<uint8>      m_selectors;
         uint64                     m_error;
         bool                       m_alpha_block;
         bool                       m_valid;

         void clear()
         {
            m_coords.clear();
            m_selectors.resize(0);
            m_error = cUINT64_MAX;
            m_alpha_block = false;
            m_valid = false;
         }

         bool are_selectors_all_equal() const
         {
            if (m_selectors.empty())
               return false;
            const uint s = m_selectors[0];
            for (uint i = 1; i < m_selectors.size(); i++)
               if (m_selectors[i] != s)
                  return false;
            return true;
         }
      };

      potential_solution m_trial_solution;
      potential_solution m_best_solution;

      typedef crnlib::hash_map<uint, empty_type> solution_hash_map;
      solution_hash_map m_solutions_tried;
      
      bool refine_solution(int refinement_level = 0);

      bool evaluate_solution(
         const dxt1_solution_coordinates& coords,
         bool early_out,
         potential_solution* pBest_solution,
         bool alternate_rounding = false);

      bool evaluate_solution_uber(
         potential_solution& solution,
         const dxt1_solution_coordinates& coords,
         bool early_out,
         potential_solution* pBest_solution,
         bool alternate_rounding = false);

      bool evaluate_solution_fast(
         potential_solution& solution,
         const dxt1_solution_coordinates& coords,
         bool early_out,
         potential_solution* pBest_solution,
         bool alternate_rounding = false);

      void clear();
      void find_unique_colors();
      bool handle_all_transparent_block();
      bool handle_solid_block();
      bool handle_multicolor_block();
      bool handle_grayscale_block();
      void compute_pca(vec3F& axis, const vec3F_array& norm_colors, const vec3F& def);
      void compute_vectors(const vec3F& perceptual_weights);
      void return_solution(results& results, const potential_solution& solution);
      void try_combinatorial_encoding();
      void optimize_endpoint_comps();
      bool optimize_endpoints(vec3F& low_color, vec3F& high_color);
      bool try_alpha_as_black_optimization();
      bool try_average_block_as_solid();
      bool try_median4(const vec3F& low_color, const vec3F& high_color);

      bool compute_internal(const params& p, results& r, solution_vec* pSolutions);
      
      unique_color lerp_color(const color_quad_u8& a, const color_quad_u8& b, float f, int rounding = 1);

      inline uint color_distance(bool perceptual, const color_quad_u8& e1, const color_quad_u8& e2, bool alpha);

      static inline vec3F unpack_to_vec3F_raw(uint16 packed_color);
      static inline vec3F unpack_to_vec3F(uint16 packed_color);
   };

   inline void swap(dxt1_endpoint_optimizer::solution& a, dxt1_endpoint_optimizer::solution& b)
   {
      std::swap(a.m_results, b.m_results);
      a.m_selectors.swap(b.m_selectors);
   }

} // namespace crnlib
