// File: crn_dxt_hc.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt1.h"
#include "crn_dxt5a.h"
#include "crn_dxt_endpoint_refiner.h"
#include "crn_image.h"
#include "crn_dxt.h"
#include "crn_image.h"
#include "crn_dxt_hc_common.h"
#include "crn_tree_clusterizer.h"
#include "crn_threading.h"

#define CRN_NO_FUNCTION_DEFINITIONS
#include "../inc/crnlib.h"

namespace crnlib
{
   const uint cTotalCompressionPhases = 25;

   class dxt_hc
   {
   public:
      dxt_hc();
      ~dxt_hc();

      struct pixel_chunk
      {
         pixel_chunk() { clear(); }

         dxt_pixel_block m_blocks[cChunkBlockHeight][cChunkBlockWidth];

         const color_quad_u8& operator() (uint cx, uint cy) const
         {
            CRNLIB_ASSERT((cx < cChunkPixelWidth) && (cy < cChunkPixelHeight));

            return m_blocks[cy >> cBlockPixelHeightShift][cx >> cBlockPixelWidthShift].m_pixels
                           [cy & (cBlockPixelHeight - 1)][cx & (cBlockPixelWidth - 1)];
         }

         color_quad_u8& operator() (uint cx, uint cy)
         {
            CRNLIB_ASSERT((cx < cChunkPixelWidth) && (cy < cChunkPixelHeight));

            return m_blocks[cy >> cBlockPixelHeightShift][cx >> cBlockPixelWidthShift].m_pixels
                           [cy & (cBlockPixelHeight - 1)][cx & (cBlockPixelWidth - 1)];
         }

         inline void clear()
         {
            utils::zero_object(*this);
            m_weight = 1.0f;
         }

         float m_weight;
      };

      typedef crnlib::vector<pixel_chunk> pixel_chunk_vec;

      struct params
      {
         params() :
            m_color_endpoint_codebook_size(3072),
            m_color_selector_codebook_size(3072),
            m_alpha_endpoint_codebook_size(3072),
            m_alpha_selector_codebook_size(3072),
            m_adaptive_tile_color_psnr_derating(2.0f), // was 3.4f
            m_adaptive_tile_alpha_psnr_derating(2.0f),
            m_adaptive_tile_color_alpha_weighting_ratio(3.0f),
            m_num_levels(0),
            m_format(cDXT1),
            m_hierarchical(true),
            m_perceptual(true),
            m_debugging(false),
            m_pProgress_func(NULL),
            m_pProgress_func_data(NULL)
         {
            m_alpha_component_indices[0] = 3;
            m_alpha_component_indices[1] = 0;

            for (uint i = 0; i < cCRNMaxLevels; i++)
            {
               m_levels[i].m_first_chunk = 0;
               m_levels[i].m_num_chunks = 0;
            }
         }

         // Valid range for codebook sizes: [32,8192] (non-power of two values are okay)
         uint        m_color_endpoint_codebook_size;
         uint        m_color_selector_codebook_size;

         uint        m_alpha_endpoint_codebook_size;
         uint        m_alpha_selector_codebook_size;

         // Higher values cause fewer 8x4, 4x8, and 4x4 blocks to be utilized less often (lower quality/smaller files).
         // Lower values cause the encoder to use large tiles less often (better quality/larger files).
         // Valid range: [0.0,100.0].
         // A value of 0 will cause the encoder to only use tiles larger than 4x4 if doing so would incur to quality loss.
         float       m_adaptive_tile_color_psnr_derating;

         float       m_adaptive_tile_alpha_psnr_derating;

         float       m_adaptive_tile_color_alpha_weighting_ratio;

         uint        m_alpha_component_indices[2];

         struct miplevel_desc
         {
            uint m_first_chunk;
            uint m_num_chunks;
         };
         // The mip level data is optional!
         miplevel_desc m_levels[cCRNMaxLevels];
         uint        m_num_levels;

         dxt_format  m_format;

         // If m_hierarchical is false, only 4x4 blocks will be used by the encoder (leading to higher quality/larger files).
         bool        m_hierarchical;

         // If m_perceptual is true, perceptual color metrics will be used by the encoder.
         bool        m_perceptual;

         bool        m_debugging;

         crn_progress_callback_func m_pProgress_func;
         void*       m_pProgress_func_data;
      };

      void clear();

      // Main compression function
      bool compress(const params& p, uint num_chunks, const pixel_chunk* pChunks, task_pool& task_pool);

      // Output accessors
      inline uint get_num_chunks() const { return m_num_chunks; }

      struct chunk_encoding
      {
         chunk_encoding() { utils::zero_object(*this); };

         // Index into g_chunk_encodings.
         uint8 m_encoding_index;

         // Number of tiles, endpoint indices.
         uint8 m_num_tiles;

         // Color, alpha0, alpha1
         enum { cColorIndex = 0, cAlpha0Index = 1, cAlpha1Index = 2 };
         uint16 m_endpoint_indices[3][cChunkMaxTiles];
         uint16 m_selector_indices[3][cChunkBlockHeight][cChunkBlockWidth];   // [block_y][block_x]
      };

      typedef crnlib::vector<chunk_encoding> chunk_encoding_vec;

      inline const chunk_encoding& get_chunk_encoding(uint chunk_index) const { return m_chunk_encoding[chunk_index]; }
      inline const chunk_encoding_vec& get_chunk_encoding_vec() const { return m_chunk_encoding; }

      struct selectors
      {
         selectors() { utils::zero_object(*this); }

         uint8 m_selectors[cBlockPixelHeight][cBlockPixelWidth];

         uint8 get_by_index(uint i) const { CRNLIB_ASSERT(i < (cBlockPixelWidth * cBlockPixelHeight)); const uint8* p = (const uint8*)m_selectors; return *(p + i); }
         void set_by_index(uint i, uint v) { CRNLIB_ASSERT(i < (cBlockPixelWidth * cBlockPixelHeight)); uint8* p = (uint8*)m_selectors; *(p + i) = static_cast<uint8>(v); }
      };
      typedef crnlib::vector<selectors> selectors_vec;

      // Color endpoints
      inline uint get_color_endpoint_codebook_size() const { return m_color_endpoints.size(); }
      inline uint get_color_endpoint(uint codebook_index) const { return m_color_endpoints[codebook_index]; }
      const crnlib::vector<uint>& get_color_endpoint_vec() const { return m_color_endpoints; }

      // Color selectors
      uint get_color_selector_codebook_size() const { return m_color_selectors.size(); }
      const selectors& get_color_selectors(uint codebook_index) const { return m_color_selectors[codebook_index]; }
      const crnlib::vector<selectors>& get_color_selectors_vec() const { return m_color_selectors; }

      // Alpha endpoints
      inline uint get_alpha_endpoint_codebook_size() const { return m_alpha_endpoints.size(); }
      inline uint get_alpha_endpoint(uint codebook_index) const { return m_alpha_endpoints[codebook_index]; }
      const crnlib::vector<uint>& get_alpha_endpoint_vec() const { return m_alpha_endpoints; }

      // Alpha selectors
      uint get_alpha_selector_codebook_size() const { return m_alpha_selectors.size(); }
      const selectors& get_alpha_selectors(uint codebook_index) const { return m_alpha_selectors[codebook_index]; }
      const crnlib::vector<selectors>& get_alpha_selectors_vec() const { return m_alpha_selectors; }

      // Debug images
      const pixel_chunk_vec& get_compressed_chunk_pixels() const { return m_dbg_chunk_pixels; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_tile_vis() const { return m_dbg_chunk_pixels_tile_vis; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_color_quantized() const { return m_dbg_chunk_pixels_color_quantized; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_alpha_quantized() const { return m_dbg_chunk_pixels_alpha_quantized; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_final() const { return m_dbg_chunk_pixels_final; }

      const pixel_chunk_vec& get_compressed_chunk_pixels_orig_color_selectors() const { return m_dbg_chunk_pixels_orig_color_selectors; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_quantized_color_selectors() const { return m_dbg_chunk_pixels_quantized_color_selectors; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_final_color_selectors() const { return m_dbg_chunk_pixels_final_color_selectors; }

      const pixel_chunk_vec& get_compressed_chunk_pixels_orig_alpha_selectors() const { return m_dbg_chunk_pixels_orig_alpha_selectors; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_quantized_alpha_selectors() const { return m_dbg_chunk_pixels_quantized_alpha_selectors; }
      const pixel_chunk_vec& get_compressed_chunk_pixels_final_alpha_selectors() const { return m_dbg_chunk_pixels_final_alpha_selectors; }

      static void create_debug_image_from_chunks(uint num_chunks_x, uint num_chunks_y, const pixel_chunk_vec& chunks, const chunk_encoding_vec *pChunk_encodings, image_u8& img, bool serpentine_scan, int comp_index = -1);

   private:
      params               m_params;

      uint                 m_num_chunks;
      const pixel_chunk*   m_pChunks;

      chunk_encoding_vec   m_chunk_encoding;

      uint                 m_num_alpha_blocks; // 0, 1, or 2
      bool                 m_has_color_blocks;
      bool                 m_has_alpha0_blocks;
      bool                 m_has_alpha1_blocks;

      struct compressed_tile
      {
         uint m_endpoint_cluster_index;
         uint m_first_endpoint;
         uint m_second_endpoint;

         uint8 m_selectors[cChunkPixelWidth * cChunkPixelHeight];

         void set_selector(uint x, uint y, uint s)
         {
            CRNLIB_ASSERT((x < m_pixel_width) && (y < m_pixel_height));
            m_selectors[x + y * m_pixel_width] = static_cast<uint8>(s);
         }

         uint get_selector(uint x, uint y) const
         {
            CRNLIB_ASSERT((x < m_pixel_width) && (y < m_pixel_height));
            return m_selectors[x + y * m_pixel_width];
         }

         uint8 m_pixel_width;
         uint8 m_pixel_height;

         uint8 m_layout_index;

         bool m_alpha_encoding;
      };

      struct compressed_chunk
      {
         compressed_chunk() { utils::zero_object(*this); }

         uint8 m_encoding_index;

         uint8 m_num_tiles;

         compressed_tile m_tiles[cChunkMaxTiles];
         compressed_tile m_quantized_tiles[cChunkMaxTiles];

         uint16 m_endpoint_cluster_index[cChunkMaxTiles];
         uint16 m_selector_cluster_index[cChunkBlockHeight][cChunkBlockWidth];
      };

      typedef crnlib::vector<compressed_chunk> compressed_chunk_vec;
      enum
      {
         cColorChunks = 0,
         cAlpha0Chunks = 1,
         cAlpha1Chunks = 2,

         cNumCompressedChunkVecs = 3
      };
      compressed_chunk_vec m_compressed_chunks[cNumCompressedChunkVecs];

      volatile atomic32_t m_encoding_hist[cNumChunkEncodings];

      atomic32_t m_total_tiles;

      void compress_dxt1_block(
         dxt1_endpoint_optimizer::results& results,
         uint chunk_index, const image_u8& chunk, uint x_ofs, uint y_ofs, uint width, uint height,
         uint8* pSelectors);

      void compress_dxt5_block(
         dxt5_endpoint_optimizer::results& results,
         uint chunk_index, const image_u8& chunk, uint x_ofs, uint y_ofs, uint width, uint height, uint component_index,
         uint8* pAlpha_selectors);

      void determine_compressed_chunks_task(uint64 data, void* pData_ptr);
      bool determine_compressed_chunks();

      struct tile_cluster
      {
         tile_cluster() : m_first_endpoint(0), m_second_endpoint(0), m_error(0), m_alpha_encoding(false) { }

         // first = chunk, second = tile
         // if an alpha tile, second's upper 16 bits contains the alpha index (0 or 1)
         crnlib::vector< std::pair<uint, uint> > m_tiles;

         uint m_first_endpoint;
         uint m_second_endpoint;
         uint64 m_error;

         bool m_alpha_encoding;
      };

      typedef crnlib::vector<tile_cluster> tile_cluster_vec;

      tile_cluster_vec m_color_clusters;
      tile_cluster_vec m_alpha_clusters;

      selectors_vec m_color_selectors;
      selectors_vec m_alpha_selectors;

      // For each selector, this array indicates every chunk/tile/tile block that use this color selector.
      struct block_id
      {
         block_id() { utils::zero_object(*this); }

         block_id(uint chunk_index, uint alpha_index, uint tile_index, uint block_x, uint block_y) :
            m_chunk_index(chunk_index), m_alpha_index((uint8)alpha_index), m_tile_index((uint8)tile_index), m_block_x((uint8)block_x), m_block_y((uint8)block_y) { }

         uint m_chunk_index;
         uint8 m_alpha_index;
         uint8 m_tile_index;
         uint8 m_block_x;
         uint8 m_block_y;
      };

      typedef crnlib::vector< crnlib::vector< block_id > > chunk_blocks_using_selectors_vec;
      chunk_blocks_using_selectors_vec m_chunk_blocks_using_color_selectors;
      chunk_blocks_using_selectors_vec m_chunk_blocks_using_alpha_selectors; // second's upper 16 bits contain alpha index!

      crnlib::vector<uint> m_color_endpoints;   // not valid until end, only for user access
      crnlib::vector<uint> m_alpha_endpoints;   // not valid until end, only for user access

      // Debugging
      pixel_chunk_vec m_dbg_chunk_pixels;
      pixel_chunk_vec m_dbg_chunk_pixels_tile_vis;
      pixel_chunk_vec m_dbg_chunk_pixels_color_quantized;
      pixel_chunk_vec m_dbg_chunk_pixels_alpha_quantized;

      pixel_chunk_vec m_dbg_chunk_pixels_orig_color_selectors;
      pixel_chunk_vec m_dbg_chunk_pixels_quantized_color_selectors;
      pixel_chunk_vec m_dbg_chunk_pixels_final_color_selectors;

      pixel_chunk_vec m_dbg_chunk_pixels_orig_alpha_selectors;
      pixel_chunk_vec m_dbg_chunk_pixels_quantized_alpha_selectors;
      pixel_chunk_vec m_dbg_chunk_pixels_final_alpha_selectors;

      pixel_chunk_vec m_dbg_chunk_pixels_final;

      crn_thread_id_t m_main_thread_id;
      bool m_canceled;
      task_pool* m_pTask_pool;

      int m_prev_phase_index;
      int m_prev_percentage_complete;

      typedef vec<6, float> vec6F;
      typedef vec<16, float> vec16F;
      typedef tree_clusterizer<vec2F> vec2F_tree_vq;
      typedef tree_clusterizer<vec6F> vec6F_tree_vq;
      typedef tree_clusterizer<vec16F> vec16F_tree_vq;

      struct assign_color_endpoint_clusters_state
      {
         CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(assign_color_endpoint_clusters_state);

         assign_color_endpoint_clusters_state(vec6F_tree_vq& vq, crnlib::vector< crnlib::vector<vec6F> >& training_vecs) :
            m_vq(vq), m_training_vecs(training_vecs) { }

         vec6F_tree_vq& m_vq;
         crnlib::vector< crnlib::vector<vec6F> >& m_training_vecs;
      };

      struct create_selector_codebook_state
      {
         CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(create_selector_codebook_state);

         create_selector_codebook_state(dxt_hc& hc, bool alpha_blocks, uint comp_index_start, uint comp_index_end, vec16F_tree_vq& selector_vq, chunk_blocks_using_selectors_vec& chunk_blocks_using_selectors, selectors_vec& selectors_cb) :
            m_hc(hc),
            m_alpha_blocks(alpha_blocks),
            m_comp_index_start(comp_index_start),
            m_comp_index_end(comp_index_end),
            m_selector_vq(selector_vq),
            m_chunk_blocks_using_selectors(chunk_blocks_using_selectors),
            m_selectors_cb(selectors_cb)
         {
         }

         dxt_hc&                             m_hc;
         bool                                m_alpha_blocks;
         uint                                m_comp_index_start;
         uint                                m_comp_index_end;
         vec16F_tree_vq&                     m_selector_vq;
         chunk_blocks_using_selectors_vec&   m_chunk_blocks_using_selectors;
         selectors_vec&                      m_selectors_cb;

         mutable spinlock                    m_chunk_blocks_using_selectors_lock;
      };

      void assign_color_endpoint_clusters_task(uint64 data, void* pData_ptr);
      bool determine_color_endpoint_clusters();

      struct determine_alpha_endpoint_clusters_state
      {
         vec2F_tree_vq m_vq;
         crnlib::vector< crnlib::vector<vec2F> > m_training_vecs[2];
      };

      void determine_alpha_endpoint_clusters_task(uint64 data, void* pData_ptr);
      bool determine_alpha_endpoint_clusters();

      void determine_color_endpoint_codebook_task(uint64 data, void* pData_ptr);
      bool determine_color_endpoint_codebook();

      void determine_alpha_endpoint_codebook_task(uint64 data, void* pData_ptr);
      bool determine_alpha_endpoint_codebook();

      void create_quantized_debug_images();

      void create_selector_codebook_task(uint64 data, void* pData_ptr);
      bool create_selector_codebook(bool alpha_blocks);

      bool refine_quantized_color_endpoints();
      bool refine_quantized_color_selectors();
      bool refine_quantized_alpha_endpoints();
      bool refine_quantized_alpha_selectors();
      void create_final_debug_image();
      bool create_chunk_encodings();
      bool update_progress(uint phase_index, uint subphase_index, uint subphase_total);
      bool compress_internal(const params& p, uint num_chunks, const pixel_chunk* pChunks);
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt_hc::pixel_chunk);
   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt_hc::chunk_encoding);
   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt_hc::selectors);

} // namespace crnlib
