// File: corpus_gen.cpp - Block compression corpus generator.
// See Copyright Notice and license at the end of inc/crnlib.h
//
// Example command line:
// -gentest [-deep] [-blockpercentage .035] [-width 4096] [-height 4096] -in c:\temp\*.jpg [-in c:\temp\*.jpeg] [-in @blah.txt]
#include "crn_core.h"
#include "corpus_gen.h"
#include "crn_console.h"

#include "crn_find_files.h"
#include "crn_file_utils.h"
#include "crn_command_line_params.h"

#include "crn_dxt.h"
#include "crn_cfile_stream.h"
#include "crn_texture_conversion.h"
#include "crn_radix_sort.h"

#define CRND_HEADER_FILE_ONLY
#include "crn_decomp.h"

namespace crnlib
{
   struct block
   {
      color_quad_u8 m_c[4*4];

      inline operator size_t() const { return fast_hash(this, sizeof(*this)); }

      inline bool operator== (const block& rhs) const
      {
         return memcmp(this, &rhs, sizeof(*this)) == 0;
      }
   };

   typedef crnlib::hash_map<block, empty_type> block_hash_map;

   corpus_gen::corpus_gen()
   {


   }

   void corpus_gen::sort_blocks(image_u8& img)
   {
      const uint num_blocks_x = img.get_width() / 4;
      const uint num_blocks_y = img.get_height() / 4;
      const uint total_blocks = num_blocks_x * num_blocks_y;

      console::printf("Sorting %u blocks...", total_blocks);

      crnlib::vector<float> block_std_dev(total_blocks);

      for (uint by = 0; by < num_blocks_y; by++)
      {
         for (uint bx = 0; bx < num_blocks_x; bx++)
         {
            color_quad_u8 c[4 * 4];

            for (uint y = 0; y < 4; y++)
               for (uint x = 0; x < 4; x++)
                  c[x+y*4] = img(bx*4+x, by*4+y);

            double std_dev = 0.0f;
            for (uint i = 0; i < 3; i++)
               std_dev += image_utils::compute_std_dev(16, c, i, 1);

            block_std_dev[bx + by * num_blocks_x] = (float)std_dev;
         }
      }

      crnlib::vector<uint> block_indices0(total_blocks);
      crnlib::vector<uint> block_indices1(total_blocks);

      const uint* pIndices = indirect_radix_sort(total_blocks, &block_indices0[0], &block_indices1[0], &block_std_dev[0], 0, sizeof(float), true);

      image_u8 new_img(img.get_width(), img.get_height());

      uint dst_block_index = 0;

      //float prev_std_dev = -999;
      for (uint i = 0; i < total_blocks; i++)
      {
         uint src_block_index = pIndices[i];

         //float std_dev = block_std_dev[src_block_index];
         //crnlib_ASSERT(std_dev >= prev_std_dev);
         //prev_std_dev = std_dev;

         uint src_block_x = src_block_index % num_blocks_x;
         uint src_block_y = src_block_index / num_blocks_x;

         uint dst_block_x = dst_block_index % num_blocks_x;
         uint dst_block_y = dst_block_index / num_blocks_x;

         new_img.unclipped_blit(src_block_x * 4, src_block_y * 4, 4, 4, dst_block_x * 4, dst_block_y * 4, img);

         dst_block_index++;
      }

#if 0      
      //new_img.swap(img);
#else
      crnlib::vector<uint> remaining_blocks(num_blocks_x);

      console::printf("Arranging %u blocks...", total_blocks);

      for (uint by = 0; by < num_blocks_y; by++)
      {
         console::printf("%u of %u", by, num_blocks_y);

         remaining_blocks.resize(num_blocks_x);
         for (uint i = 0; i < num_blocks_x; i++)
            remaining_blocks[i] = i;

         color_quad_u8 match_block[16];
         utils::zero_object(match_block);
         for (uint bx = 0; bx < num_blocks_x; bx++)
         {
            uint best_index = 0;

            uint64 best_error = cUINT64_MAX;

            for (uint i = 0; i < remaining_blocks.size(); i++)
            {
               uint src_block_index = remaining_blocks[i];

               uint64 error = 0;
               for (uint y = 0; y < 4; y++)
               {
                  for (uint x = 0; x < 4; x++)
                  {
                     const color_quad_u8& c = new_img(src_block_index*4+x, by*4+y);
                     error += color::elucidian_distance(c, match_block[x+y*4], false);
                  }
               }

               if (error < best_error)
               {
                  best_error = error;
                  best_index = i;
               }
            }

            uint src_block_index = remaining_blocks[best_index];

            for (uint y = 0; y < 4; y++)
            {
               for (uint x = 0; x < 4; x++)
               {
                  const color_quad_u8& c = new_img(src_block_index*4+x, by*4+y);
                  match_block[x+y*4] = c;

                  img(bx * 4 + x, by * 4 + y) = c;
               }
            }

            remaining_blocks.erase_unordered(best_index);
         }
      }
#endif      
   }

   bool corpus_gen::generate(const char* pCmd_line)
   {
      static const command_line_params::param_desc param_desc_array[] = 
      {
         { "corpus_gen", 0, false },
         { "in", 1, true },
         { "deep", 0, false },
         { "blockpercentage", 1, false },
         { "width", 1, false },
         { "height", 1, false },
         { "alpha", 0, false },
      };

      command_line_params params;
      if (!params.parse(pCmd_line, CRNLIB_ARRAY_SIZE(param_desc_array), param_desc_array, true))
         return false;

      if (!params.has_key("in"))
      {
         console::error("Must specify one or more input files using the /in option!");
         return false;
      }

      uint num_dst_blocks_x = params.get_value_as_int("width", 0, 4096, 128, 4096);
      num_dst_blocks_x = (num_dst_blocks_x + 3) / 4;
      uint num_dst_blocks_y = params.get_value_as_int("height", 0, 4096, 128, 4096);
      num_dst_blocks_y = (num_dst_blocks_y + 3) / 4;

      const uint total_dst_blocks = num_dst_blocks_x * num_dst_blocks_y;
      image_u8 dst_img(num_dst_blocks_x * 4, num_dst_blocks_y * 4);
      uint next_dst_block = 0;
      uint total_dst_images = 0;

      random rm;

      block_hash_map block_hash;
      block_hash.reserve(total_dst_blocks);

      uint total_images_loaded = 0;
      uint total_blocks_written = 0;

      command_line_params::param_map_const_iterator it = params.begin();
      for ( ; it != params.end(); ++it)
      {
         if (it->first != "in")
            continue;
         if (it->second.m_values.empty())
         {
            console::error("Must follow /in parameter with a filename!\n");
            return false;
         }
         
         for (uint in_value_index = 0; in_value_index < it->second.m_values.size(); in_value_index++)
         {
            const dynamic_string& filespec = it->second.m_values[in_value_index];

            find_files file_finder;
            if (!file_finder.find(filespec.get_ptr(), find_files::cFlagAllowFiles | (params.has_key("deep") ? find_files::cFlagRecursive : 0)))
            {
               console::warning("Failed finding files: %s", filespec.get_ptr());
               continue;
            }

            if (file_finder.get_files().empty())
            {
               console::warning("No files found: %s", filespec.get_ptr());
               return false;
            }

            const find_files::file_desc_vec& files = file_finder.get_files();

            for (uint file_index = 0; file_index < files.size(); file_index++)
            {
               const find_files::file_desc& file_desc = files[file_index];

               console::printf("Loading image: %s", file_desc.m_fullname.get_ptr());

               image_u8 img;
               if (!image_utils::read_from_file(img, file_desc.m_fullname.get_ptr(), 0))
               {
                  console::warning("Failed loading image file: %s", file_desc.m_fullname.get_ptr());
                  continue;
               }

               if (!params.has_key("alpha"))
               {
                  for (uint y = 0; y < img.get_height(); y++)
                     for (uint x = 0; x < img.get_width(); x++)
                        img(x, y).a = 255;
               }

               total_images_loaded++;

               uint width = img.get_width();
               uint height = img.get_height();

               uint num_blocks_x = (width + 3) / 4;
               uint num_blocks_y = (height + 3) / 4;
               uint total_blocks = num_blocks_x * num_blocks_y;

               float percentage = params.get_value_as_float("blockpercentage", 0, .1f, .001f, 1.0f);
               uint total_rand_blocks = math::maximum<uint>(1U, (uint)(total_blocks * percentage));

               crnlib::vector<uint> remaining_blocks(total_blocks);
               for (uint i = 0; i < total_blocks; i++)
                  remaining_blocks[i] = i;

               uint num_blocks_remaining = total_rand_blocks;
               while (num_blocks_remaining)
               {
                  if (remaining_blocks.empty())
                     break;

                  uint rand_block_index = rm.irand(0, remaining_blocks.size());
                  uint block_index = remaining_blocks[rand_block_index];
                  remaining_blocks.erase_unordered(rand_block_index);

                  uint block_y = block_index / num_blocks_x;
                  uint block_x = block_index % num_blocks_x;

                  block b;

                  for (uint y = 0; y < 4; y++)
                  {
                     for (uint x = 0; x < 4; x++)
                     {
                        b.m_c[x+y*4] = img.get_clamped(block_x*4+x, block_y*4+y);
                     }
                  }

                  if (!block_hash.insert(b).second)
                     continue;
                  if (block_hash.size() == total_dst_blocks)
                  {
                     block_hash.clear();
                     block_hash.reserve(total_dst_blocks);
                  }

                  uint dst_block_x = next_dst_block % num_dst_blocks_x;
                  uint dst_block_y = next_dst_block / num_dst_blocks_x;
                  for (uint y = 0; y < 4; y++)
                  {
                     for (uint x = 0; x < 4; x++)
                     {
                        dst_img(dst_block_x * 4 + x, dst_block_y * 4 + y) = b.m_c[x + y*4];
                     }
                  }        

                  next_dst_block++;
                  if (total_dst_blocks == next_dst_block)
                  {
                     sort_blocks(dst_img);

                     dynamic_string dst_filename(cVarArg, "test_%u.tga", total_dst_images);
                     console::printf("Writing image: %s", dst_filename.get_ptr());
                     image_utils::write_to_file(dst_filename.get_ptr(), dst_img, 0);

                     dst_img.set_all(color_quad_u8::make_black());

                     next_dst_block = 0;

                     total_dst_images++;
                  }

                  total_blocks_written++;

                  num_blocks_remaining--;
               }

            }  // file_index

         } // in_value_index

      }         

      if (next_dst_block)
      {
         sort_blocks(dst_img);

         dynamic_string dst_filename(cVarArg, "test_%u.tga", total_dst_images);
         console::printf("Writing image: %s", dst_filename.get_ptr());
         image_utils::write_to_file(dst_filename.get_ptr(), dst_img, 0);

         next_dst_block = 0;

         total_dst_images++;
      }

      console::printf("Found %u input images, %u output images, %u total blocks", total_images_loaded, total_dst_images, total_blocks_written);

      return true;   
   }

} // namespace crnlib
