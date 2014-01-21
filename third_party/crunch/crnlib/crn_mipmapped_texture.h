// File: crn_mipmapped_texture.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt_image.h"
#include "../inc/dds_defs.h"
#include "crn_pixel_format.h"
#include "crn_image.h"
#include "crn_resampler.h"
#include "crn_data_stream_serializer.h"
#include "crn_qdxt1.h"
#include "crn_qdxt5.h"
#include "crn_texture_file_types.h"
#include "crn_image_utils.h"

namespace crnlib
{
   extern const vec2I g_vertical_cross_image_offsets[6];

   enum orientation_flags_t
   {
      cOrientationFlagXFlipped = 1,
      cOrientationFlagYFlipped = 2,

      cDefaultOrientationFlags = 0
   };

   enum unpack_flags_t
   {
      cUnpackFlagUncook = 1,
      cUnpackFlagUnflip = 2
   };

   class mip_level
   {
      friend class mipmapped_texture;

   public:
      mip_level();
      ~mip_level();

      mip_level(const mip_level& other);
      mip_level& operator= (const mip_level& rhs);

      // Assumes ownership.
      void assign(image_u8* p, pixel_format fmt = PIXEL_FMT_INVALID, orientation_flags_t orient_flags = cDefaultOrientationFlags);
      void assign(dxt_image* p, pixel_format fmt = PIXEL_FMT_INVALID, orientation_flags_t orient_flags = cDefaultOrientationFlags);

      void clear();

      inline uint get_width() const { return m_width; }
      inline uint get_height() const { return m_height; }
      inline uint get_total_pixels() const { return m_width * m_height; }
      
      orientation_flags_t get_orientation_flags() const { return m_orient_flags; }
      void set_orientation_flags(orientation_flags_t flags) { m_orient_flags = flags; }

      inline image_u8* get_image() const { return m_pImage; }
      inline dxt_image* get_dxt_image() const { return m_pDXTImage; }
      
      image_u8* get_unpacked_image(image_u8& tmp, uint unpack_flags) const;

      inline bool is_packed() const { return m_pDXTImage != NULL; }

      inline bool is_valid() const { return (m_pImage != NULL) || (m_pDXTImage != NULL); }

      inline pixel_format_helpers::component_flags get_comp_flags() const { return m_comp_flags; }
      inline void set_comp_flags(pixel_format_helpers::component_flags comp_flags) { m_comp_flags = comp_flags; }

      inline pixel_format get_format() const { return m_format; }
      inline void set_format(pixel_format fmt) { m_format = fmt; }

      bool convert(pixel_format fmt, bool cook, const dxt_image::pack_params& p);

      bool pack_to_dxt(const image_u8& img, pixel_format fmt, bool cook, const dxt_image::pack_params& p, orientation_flags_t orient_flags = cDefaultOrientationFlags);
      bool pack_to_dxt(pixel_format fmt, bool cook, const dxt_image::pack_params& p);

      bool unpack_from_dxt(bool uncook = true);

      // Returns true if flipped on either axis.
      bool is_flipped() const;
      
      bool is_x_flipped() const;
      bool is_y_flipped() const;
      
      bool can_unflip_without_unpacking() const;
      
      // Returns true if unflipped on either axis.
      // Will try to flip packed (DXT/ETC) data in-place, if this isn't possible it'll unpack/uncook the mip level then unflip.
      bool unflip(bool allow_unpacking_to_flip, bool uncook_during_unpack);
      
      bool set_alpha_to_luma();
      bool convert(image_utils::conversion_type conv_type);

      bool flip_x();
      bool flip_y();

   private:
      uint                                   m_width;
      uint                                   m_height;

      pixel_format_helpers::component_flags  m_comp_flags;
      pixel_format                           m_format;

      image_u8*                              m_pImage;
      dxt_image*                             m_pDXTImage;

      orientation_flags_t                    m_orient_flags;

      void cook_image(image_u8& img) const;
      void uncook_image(image_u8& img) const;
   };

   // A face is an array of mip_level ptr's.
   typedef crnlib::vector<mip_level*> mip_ptr_vec;

   // And an array of one, six, or N faces make up a texture.
   typedef crnlib::vector<mip_ptr_vec> face_vec;

   class mipmapped_texture
   {
   public:
      // Construction/destruction
      mipmapped_texture();
      ~mipmapped_texture();

      mipmapped_texture(const mipmapped_texture& other);
      mipmapped_texture& operator= (const mipmapped_texture& rhs);

      void clear();

      void init(uint width, uint height, uint levels, uint faces, pixel_format fmt, const char* pName, orientation_flags_t orient_flags);

      // Assumes ownership.
      void assign(face_vec& faces);
      void assign(mip_level* pLevel);
      void assign(image_u8* p, pixel_format fmt = PIXEL_FMT_INVALID, orientation_flags_t orient_flags = cDefaultOrientationFlags);
      void assign(dxt_image* p, pixel_format fmt = PIXEL_FMT_INVALID, orientation_flags_t orient_flags = cDefaultOrientationFlags);

      void set(texture_file_types::format source_file_type, const mipmapped_texture& mipmapped_texture);

      // Accessors
      image_u8* get_level_image(uint face, uint level, image_u8& img, uint unpack_flags = cUnpackFlagUncook | cUnpackFlagUnflip) const;

      inline bool is_valid() const { return m_faces.size() > 0; }

      const dynamic_string& get_name() const { return m_name; }
      void set_name(const dynamic_string& name) { m_name = name; }

      const dynamic_string& get_source_filename() const { return get_name(); }
      texture_file_types::format get_source_file_type() const { return m_source_file_type; }

      inline uint get_width() const { return m_width; }
      inline uint get_height() const { return m_height; }
      inline uint get_total_pixels() const { return m_width * m_height; }
      uint get_total_pixels_in_all_faces_and_mips() const;

      inline uint get_num_faces() const { return m_faces.size(); }
      inline uint get_num_levels() const { if (m_faces.empty()) return 0; else return m_faces[0].size(); }

      inline pixel_format_helpers::component_flags get_comp_flags() const { return m_comp_flags; }
      inline pixel_format get_format() const { return m_format; }

      inline bool is_unpacked() const { if (get_num_faces()) { return get_level(0, 0)->get_image() != NULL; } return false; }

      inline const   mip_ptr_vec& get_face(uint face) const { return m_faces[face]; }
      inline         mip_ptr_vec& get_face(uint face)       { return m_faces[face]; }

      inline const   mip_level* get_level(uint face, uint mip) const { return m_faces[face][mip]; }
      inline         mip_level* get_level(uint face, uint mip)       { return m_faces[face][mip]; }

      bool has_alpha() const;
      bool is_normal_map() const;
      bool is_vertical_cross() const;
      bool is_packed() const;
      texture_type determine_texture_type() const;

      const dynamic_string& get_last_error() const { return m_last_error; }
      void clear_last_error() { m_last_error.clear(); }

      // Reading/writing
      bool read_dds(data_stream_serializer& serializer);
      bool write_dds(data_stream_serializer& serializer) const;

      bool read_ktx(data_stream_serializer& serializer);
      bool write_ktx(data_stream_serializer& serializer) const;
 
      bool read_crn(data_stream_serializer& serializer);
      bool read_crn_from_memory(const void *pData, uint data_size, const char* pFilename);
      
      // If file_format is texture_file_types::cFormatInvalid, the format will be determined from the filename's extension.
      bool read_from_file(const char* pFilename, texture_file_types::format file_format = texture_file_types::cFormatInvalid);
      bool read_from_stream(data_stream_serializer& serializer, texture_file_types::format file_format = texture_file_types::cFormatInvalid);

      bool write_to_file(
         const char* pFilename,
         texture_file_types::format file_format = texture_file_types::cFormatInvalid,
         crn_comp_params* pComp_params = NULL,
         uint32* pActual_quality_level = NULL, float* pActual_bitrate = NULL,
         uint32 image_write_flags = 0);

      // Conversion
      bool convert(pixel_format fmt, bool cook, const dxt_image::pack_params& p);
      bool convert(pixel_format fmt, const dxt_image::pack_params& p);
      bool convert(pixel_format fmt, bool cook, const dxt_image::pack_params& p, int qdxt_quality, bool hierarchical = true);
      bool convert(image_utils::conversion_type conv_type);

      bool unpack_from_dxt(bool uncook = true);

      bool set_alpha_to_luma();

      void discard_mipmaps();

      void discard_mips();

      struct resample_params
      {
         resample_params() :
            m_pFilter("kaiser"),
            m_wrapping(false),
            m_srgb(false),
            m_renormalize(false),
            m_filter_scale(.9f),
            m_gamma(1.75f),    // or 2.2f
            m_multithreaded(true)
         {
         }

         const char* m_pFilter;
         bool        m_wrapping;
         bool        m_srgb;
         bool        m_renormalize;
         float       m_filter_scale;
         float       m_gamma;
         bool        m_multithreaded;
      };

      bool resize(uint new_width, uint new_height, const resample_params& params);

      struct generate_mipmap_params : public resample_params
      {
         generate_mipmap_params() :
            resample_params(),
            m_min_mip_size(1),
            m_max_mips(0)
         {
         }

         uint        m_min_mip_size;
         uint        m_max_mips; // actually the max # of total levels
      };

      bool generate_mipmaps(const generate_mipmap_params& params, bool force);

      bool crop(uint x, uint y, uint width, uint height);

      bool vertical_cross_to_cubemap();

      // Low-level clustered DXT (QDXT) compression
      struct qdxt_state
      {
         qdxt_state(task_pool& tp) : m_fmt(PIXEL_FMT_INVALID), m_qdxt1(tp), m_qdxt5a(tp), m_qdxt5b(tp)
         {
         }

         pixel_format                        m_fmt;
         qdxt1                               m_qdxt1;
         qdxt5                               m_qdxt5a;
         qdxt5                               m_qdxt5b;
         crnlib::vector<dxt_pixel_block>     m_pixel_blocks;

         qdxt1_params                        m_qdxt1_params;
         qdxt5_params                        m_qdxt5_params[2];
         bool                                m_has_blocks[3];

         void clear()
         {
            m_fmt = PIXEL_FMT_INVALID;
            m_qdxt1.clear();
            m_qdxt5a.clear();
            m_qdxt5b.clear();
            m_pixel_blocks.clear();
            m_qdxt1_params.clear();
            m_qdxt5_params[0].clear();
            m_qdxt5_params[1].clear();
            utils::zero_object(m_has_blocks);
         }
      };
      bool qdxt_pack_init(qdxt_state& state, mipmapped_texture& dst_tex, const qdxt1_params& dxt1_params, const qdxt5_params& dxt5_params, pixel_format fmt, bool cook);
      bool qdxt_pack(qdxt_state& state, mipmapped_texture& dst_tex, const qdxt1_params& dxt1_params, const qdxt5_params& dxt5_params);

      void swap(mipmapped_texture& img);

      bool check() const;

      void set_orientation_flags(orientation_flags_t flags);

      // Returns true if any face/miplevel is flipped.
      bool is_flipped() const;
      bool is_x_flipped() const;
      bool is_y_flipped() const;
      bool can_unflip_without_unpacking() const;
      bool unflip(bool allow_unpacking_to_flip, bool uncook_if_necessary_to_unpack);
      
      bool flip_y(bool update_orientation_flags);

   private:
      dynamic_string                         m_name;

      uint                                   m_width;
      uint                                   m_height;

      pixel_format_helpers::component_flags  m_comp_flags;
      pixel_format                           m_format;

      face_vec                               m_faces;

      texture_file_types::format             m_source_file_type;

      mutable dynamic_string                 m_last_error;

      inline void clear_last_error() const { m_last_error.clear(); }
      inline void set_last_error(const char* p) const { m_last_error = p; }

      void free_all_mips();
      bool read_regular_image(data_stream_serializer &serializer, texture_file_types::format file_format);
      bool write_regular_image(const char* pFilename, uint32 image_write_flags);
      bool read_dds_internal(data_stream_serializer& serializer);
      void print_crn_comp_params(const crn_comp_params& p);
      bool write_comp_texture(const char* pFilename, const crn_comp_params &comp_params, uint32 *pActual_quality_level, float *pActual_bitrate);
      void change_dxt1_to_dxt1a();
      bool flip_y_helper();
   };

   inline void swap(mipmapped_texture& a, mipmapped_texture& b)
   {
      a.swap(b);
   }

} // namespace crnlib
