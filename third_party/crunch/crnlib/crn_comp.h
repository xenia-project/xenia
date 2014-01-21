// File: crn_comp.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#define CRND_HEADER_FILE_ONLY
#include "../inc/crn_decomp.h"
#undef CRND_HEADER_FILE_ONLY

#include "../inc/crnlib.h"
#include "crn_symbol_codec.h"
#include "crn_dxt_hc.h"
#include "crn_image.h"
#include "crn_image_utils.h"
#include "crn_texture_comp.h"

namespace crnlib
{
   class crn_comp : public itexture_comp
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(crn_comp);

   public:
      crn_comp();
      virtual ~crn_comp();

      virtual const char *get_ext() const { return "CRN"; }

      virtual bool compress_init(const crn_comp_params& params);
      virtual bool compress_pass(const crn_comp_params& params, float *pEffective_bitrate);
      virtual void compress_deinit();

      virtual const crnlib::vector<uint8>& get_comp_data() const  { return m_comp_data; }
      virtual       crnlib::vector<uint8>& get_comp_data()        { return m_comp_data; }

      uint get_comp_data_size() const { return m_comp_data.size(); }
      const uint8* get_comp_data_ptr() const { return m_comp_data.size() ? &m_comp_data[0] : NULL; }

   private:
      task_pool                  m_task_pool;
      const crn_comp_params* m_pParams;

      image_u8 m_images[cCRNMaxFaces][cCRNMaxLevels];

      struct level_tag
      {
         uint m_width, m_height;
         uint m_chunk_width, m_chunk_height;
         uint m_group_index;
         uint m_num_chunks;
         uint m_first_chunk;
         uint m_group_first_chunk;
      } m_levels[cCRNMaxLevels];

      struct mip_group
      {
         mip_group() : m_first_chunk(0), m_num_chunks(0) { }

         uint m_first_chunk;
         uint m_num_chunks;
      };
      crnlib::vector<mip_group> m_mip_groups;

      enum comp
      {
         cColor,
         cAlpha0,
         cAlpha1,
         cNumComps
      };

      bool m_has_comp[cNumComps];

      struct chunk_detail
      {
         chunk_detail() { utils::zero_object(*this); }

         uint m_first_endpoint_index;
         uint m_first_selector_index;
      };
      typedef crnlib::vector<chunk_detail> chunk_detail_vec;
      chunk_detail_vec              m_chunk_details;

      crnlib::vector<uint>          m_endpoint_indices[cNumComps];
      crnlib::vector<uint>          m_selector_indices[cNumComps];

      uint                          m_total_chunks;
      dxt_hc::pixel_chunk_vec       m_chunks;

      crnd::crn_header              m_crn_header;
      crnlib::vector<uint8>         m_comp_data;

      dxt_hc                        m_hvq;

      symbol_histogram              m_chunk_encoding_hist;
      static_huffman_data_model     m_chunk_encoding_dm;

      symbol_histogram              m_endpoint_index_hist[2];
      static_huffman_data_model     m_endpoint_index_dm[2]; // color, alpha

      symbol_histogram              m_selector_index_hist[2];
      static_huffman_data_model     m_selector_index_dm[2]; // color, alpha

      crnlib::vector<uint8>         m_packed_chunks[cCRNMaxLevels];
      crnlib::vector<uint8>         m_packed_data_models;
      crnlib::vector<uint8>         m_packed_color_endpoints;
      crnlib::vector<uint8>         m_packed_color_selectors;
      crnlib::vector<uint8>         m_packed_alpha_endpoints;
      crnlib::vector<uint8>         m_packed_alpha_selectors;

      void clear();

      void append_chunks(const image_u8& img, uint num_chunks_x, uint num_chunks_y, dxt_hc::pixel_chunk_vec& chunks, float weight);

      static float color_endpoint_similarity_func(uint index_a, uint index_b, void* pContext);
      static float alpha_endpoint_similarity_func(uint index_a, uint index_b, void* pContext);
      void sort_color_endpoint_codebook(crnlib::vector<uint>& remapping, const crnlib::vector<uint>& endpoints);
      void sort_alpha_endpoint_codebook(crnlib::vector<uint>& remapping, const crnlib::vector<uint>& endpoints);

      bool pack_color_endpoints(crnlib::vector<uint8>& data, const crnlib::vector<uint>& remapping, const crnlib::vector<uint>& endpoint_indices, uint trial_index);
      bool pack_alpha_endpoints(crnlib::vector<uint8>& data, const crnlib::vector<uint>& remapping, const crnlib::vector<uint>& endpoint_indices, uint trial_index);

      static float color_selector_similarity_func(uint index_a, uint index_b, void* pContext);
      static float alpha_selector_similarity_func(uint index_a, uint index_b, void* pContext);
      void sort_selector_codebook(crnlib::vector<uint>& remapping, const crnlib::vector<dxt_hc::selectors>& selectors, const uint8* pTo_linear);

      bool pack_selectors(
         crnlib::vector<uint8>& packed_data,
         const crnlib::vector<uint>& selector_indices,
         const crnlib::vector<dxt_hc::selectors>& selectors,
         const crnlib::vector<uint>& remapping,
         uint max_selector_value,
         const uint8* pTo_linear,
         uint trial_index);

      bool alias_images();
      void create_chunks();
      bool quantize_chunks();
      void create_chunk_indices();

      bool pack_chunks(
         uint first_chunk, uint num_chunks,
         bool clear_histograms,
         symbol_codec* pCodec,
         const crnlib::vector<uint>* pColor_endpoint_remap,
         const crnlib::vector<uint>* pColor_selector_remap,
         const crnlib::vector<uint>* pAlpha_endpoint_remap,
         const crnlib::vector<uint>* pAlpha_selector_remap);

      bool pack_chunks_simulation(
         uint first_chunk, uint num_chunks,
         uint& total_bits,
         const crnlib::vector<uint>* pColor_endpoint_remap,
         const crnlib::vector<uint>* pColor_selector_remap,
         const crnlib::vector<uint>* pAlpha_endpoint_remap,
         const crnlib::vector<uint>* pAlpha_selector_remap);

      void optimize_color_endpoint_codebook_task(uint64 data, void* pData_ptr);
      bool optimize_color_endpoint_codebook(crnlib::vector<uint>& remapping);

      void optimize_color_selector_codebook_task(uint64 data, void* pData_ptr);
      bool optimize_color_selector_codebook(crnlib::vector<uint>& remapping);

      void optimize_alpha_endpoint_codebook_task(uint64 data, void* pData_ptr);
      bool optimize_alpha_endpoint_codebook(crnlib::vector<uint>& remapping);

      void optimize_alpha_selector_codebook_task(uint64 data, void* pData_ptr);
      bool optimize_alpha_selector_codebook(crnlib::vector<uint>& remapping);

      bool create_comp_data();

      bool pack_data_models();

      bool update_progress(uint phase_index, uint subphase_index, uint subphase_total);

      bool compress_internal();

      static void append_vec(crnlib::vector<uint8>& a, const void* p, uint size);
      static void append_vec(crnlib::vector<uint8>& a, const crnlib::vector<uint8>& b);
   };

} // namespace crnlib
