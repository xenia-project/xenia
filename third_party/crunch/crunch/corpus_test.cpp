// File: corpus_test.cpp
#include "crn_core.h"
#include "corpus_test.h"
#include "crn_find_files.h"
#include "crn_console.h"
#include "crn_image_utils.h"
#include "crn_hash.h"
#include "crn_hash_map.h"
#include "crn_radix_sort.h"
#include "crn_mipmapped_texture.h"

namespace crnlib
{
   corpus_tester::corpus_tester()
   {
      m_bad_block_img.resize(256, 256);
      m_next_bad_block_index = 0;
      m_total_bad_block_files = 0;
   }

   void corpus_tester::print_comparative_metric_stats(const command_line_params& cmd_line_params, const crnlib::vector<image_utils::error_metrics>& stats1, const crnlib::vector<image_utils::error_metrics>& stats2, uint num_blocks_x, uint num_blocks_y)
   {
      num_blocks_y;

      crnlib::vector<uint> better_blocks;
      crnlib::vector<uint> equal_blocks;
      crnlib::vector<uint> worse_blocks;
      crnlib::vector<float> delta_psnr;

      for (uint i = 0; i < stats1.size(); i++)
      {
         //uint bx = i % num_blocks_x;
         //uint by = i / num_blocks_x;

         const image_utils::error_metrics& em1 = stats1[i];
         const image_utils::error_metrics& em2 = stats2[i];

         if (em1.mPeakSNR < em2.mPeakSNR)
         {
            worse_blocks.push_back(i);
            delta_psnr.push_back((float)(em2.mPeakSNR - em1.mPeakSNR));
         }
         else if (fabs(em1.mPeakSNR - em2.mPeakSNR) < .001f)
            equal_blocks.push_back(i);
         else
            better_blocks.push_back(i);
      }  

      console::printf("Num worse blocks: %u, %3.3f%%", worse_blocks.size(), worse_blocks.size() * 100.0f / stats1.size());          
      console::printf("Num equal blocks: %u, %3.3f%%", equal_blocks.size(), equal_blocks.size() * 100.0f / stats1.size());
      console::printf("Num better blocks: %u, %3.3f%%", better_blocks.size(), better_blocks.size() * 100.0f / stats1.size());
      console::printf("Num equal+better blocks: %u, %3.3f%%", equal_blocks.size()+better_blocks.size(), (equal_blocks.size()+better_blocks.size()) * 100.0f / stats1.size());

      if (!cmd_line_params.has_key("nobadblocks"))
      {
         crnlib::vector<uint> indices[2];
         indices[0].resize(worse_blocks.size());
         indices[1].resize(worse_blocks.size());

         uint* pSorted_indices = NULL;
         if (worse_blocks.size())
         {
            pSorted_indices = indirect_radix_sort(worse_blocks.size(), &indices[0][0], &indices[1][0], &delta_psnr[0], 0, sizeof(float), true);

            console::printf("List of worse blocks sorted by delta PSNR:");
            for (uint i = 0; i < worse_blocks.size(); i++)
            {
               uint block_index = worse_blocks[pSorted_indices[i]];
               uint bx = block_index % num_blocks_x;
               uint by = block_index / num_blocks_x;

               console::printf("%u. [%u,%u] %3.3f %3.3f %3.3f", 
                  i,
                  bx, by,
                  stats1[block_index].mPeakSNR,
                  stats2[block_index].mPeakSNR,
                  stats2[block_index].mPeakSNR - stats1[block_index].mPeakSNR);
            }
         }         
      }         
   }

   void corpus_tester::print_metric_stats(const crnlib::vector<image_utils::error_metrics>& stats, uint num_blocks_x, uint num_blocks_y)
   {
      num_blocks_y;

      image_utils::error_metrics best_metrics;
      image_utils::error_metrics worst_metrics;
      worst_metrics.mPeakSNR = 1e+6f;

      vec2I best_loc;
      vec2I worst_loc;
      utils::zero_object(best_loc);
      utils::zero_object(worst_loc);

      double psnr_total = 0.0f;
      double psnr2_total = 0.0f;
      uint num_non_inf = 0;
      uint num_inf = 0;

      for (uint i = 0; i < stats.size(); i++)
      {
         uint bx = i % num_blocks_x;
         uint by = i / num_blocks_x;

         const image_utils::error_metrics& em = stats[i];

         if ((em.mPeakSNR < 200.0f) && (em > best_metrics)) { best_metrics = em; best_loc.set(bx, by); }
         if (em < worst_metrics) { worst_metrics = em; worst_loc.set(bx, by); }

         if (em.mPeakSNR < 200.0f)
         {
            psnr_total += em.mPeakSNR;
            psnr2_total += em.mPeakSNR*em.mPeakSNR;
            num_non_inf++;
         }
         else
         {
            num_inf++;
         }
      }         

      console::printf("Number of infinite PSNR blocks: %u", num_inf);
      console::printf("Number of non-infinite PSNR blocks: %u", num_non_inf);
      if (num_non_inf)
      {
         psnr_total /= num_non_inf;
         psnr2_total /= num_non_inf;

         double psnr_std_dev = sqrt(psnr2_total - psnr_total * psnr_total);

         console::printf("Average Non-Inf PSNR: %3.3f, Std dev: %3.3f", psnr_total, psnr_std_dev);
         console::printf("Worst PSNR: %3.3f, Block Location: %i,%i", worst_metrics.mPeakSNR, worst_loc[0], worst_loc[1]);
         console::printf("Best Non-Inf PSNR: %3.3f, Block Location: %i,%i", best_metrics.mPeakSNR, best_loc[0], best_loc[1]);
      }         
   }

   void corpus_tester::flush_bad_blocks()
   {
      if (!m_next_bad_block_index)
         return;

      dynamic_string filename(cVarArg, "badblocks_%u.tga", m_total_bad_block_files);
      console::printf("Writing bad block image: %s", filename.get_ptr());
      image_utils::write_to_file(filename.get_ptr(), m_bad_block_img, image_utils::cWriteFlagIgnoreAlpha);

      m_bad_block_img.set_all(color_quad_u8::make_black());

      m_total_bad_block_files++;

      m_next_bad_block_index = 0;
   }

   void corpus_tester::add_bad_block(image_u8& block)
   {
      uint num_blocks_x = m_bad_block_img.get_block_width(4);
      uint num_blocks_y = m_bad_block_img.get_block_height(4);
      uint total_blocks = num_blocks_x * num_blocks_y;

      m_bad_block_img.blit((m_next_bad_block_index % num_blocks_x) * 4, (m_next_bad_block_index / num_blocks_x) * 4, block);
      m_next_bad_block_index++;

      if (m_next_bad_block_index == total_blocks)
         flush_bad_blocks();
   }
   
   static bool progress_callback(uint percentage_complete, void* pUser_data_ptr)
   {
      static int s_prev_percentage_complete = -1;
      pUser_data_ptr;
      if (s_prev_percentage_complete != static_cast<int>(percentage_complete))
      {
         console::progress("%u%%", percentage_complete);
         s_prev_percentage_complete = percentage_complete;
      }
      return true;
   }

   bool corpus_tester::test(const char* pCmd_line)
   {
      console::printf("Command line:\n\"%s\"", pCmd_line);

      static const command_line_params::param_desc param_desc_array[] = 
      {
         { "corpus_test", 0, false },
         { "in", 1, true },
         { "deep", 0, false },
         { "alpha", 0, false },
         { "nomips", 0, false },
         { "perceptual", 0, false },
         { "endpointcaching", 0, false },
         { "multithreaded", 0, false },
         { "writehybrid", 0, false },
         { "nobadblocks", 0, false },
      };

      command_line_params cmd_line_params;
      if (!cmd_line_params.parse(pCmd_line, CRNLIB_ARRAY_SIZE(param_desc_array), param_desc_array, true))
         return false;

      double total_time1 = 0, total_time2 = 0;

      command_line_params::param_map_const_iterator it = cmd_line_params.begin();
      for ( ; it != cmd_line_params.end(); ++it)
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
            if (!file_finder.find(filespec.get_ptr(), find_files::cFlagAllowFiles | (cmd_line_params.has_key("deep") ? find_files::cFlagRecursive : 0)))
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

            image_u8 o(4, 4), a(4, 4), b(4, 4);

            uint first_channel = 0;
            uint num_channels = 3;
            bool perceptual = cmd_line_params.get_value_as_bool("perceptual", false);
            if (perceptual)
            {
               first_channel = 0;
               num_channels = 0;
            }
            console::printf("Perceptual mode: %u", perceptual);

            for (uint file_index = 0; file_index < files.size(); file_index++)
            {
               const find_files::file_desc& file_desc = files[file_index];

               console::printf("-------- Loading image: %s", file_desc.m_fullname.get_ptr());

               image_u8 img;
               if (!image_utils::read_from_file(img, file_desc.m_fullname.get_ptr(), 0))
               {
                  console::warning("Failed loading image file: %s", file_desc.m_fullname.get_ptr());
                  continue;
               }

               if ((!cmd_line_params.has_key("alpha")) && img.is_component_valid(3))
               {
                  for (uint y = 0; y < img.get_height(); y++)
                     for (uint x = 0; x < img.get_width(); x++)
                        img(x, y).a = 255;

                  img.set_component_valid(3, false);
               }

               mipmapped_texture orig_tex;
               orig_tex.assign(crnlib_new<image_u8>(img));

               if (!cmd_line_params.has_key("nomips"))
               {
                  mipmapped_texture::generate_mipmap_params genmip_params;
                  genmip_params.m_srgb = true;

                  console::printf("Generating mipmaps");

                  if (!orig_tex.generate_mipmaps(genmip_params, false))
                  {
                     console::error("Mipmap generation failed!");
                     return false;
                  }
               }               

               console::printf("Compress 1");

               mipmapped_texture tex1(orig_tex);
               dxt_image::pack_params convert_params;
               convert_params.m_endpoint_caching = cmd_line_params.get_value_as_bool("endpointcaching", 0, false);
               convert_params.m_compressor = cCRNDXTCompressorCRN;
               convert_params.m_quality = cCRNDXTQualityNormal;
               convert_params.m_perceptual = perceptual;
               convert_params.m_num_helper_threads = cmd_line_params.get_value_as_bool("multithreaded", 0, true) ? (g_number_of_processors - 1) : 0;
               convert_params.m_pProgress_callback = progress_callback;
               timer t;
               t.start();
               if (!tex1.convert(PIXEL_FMT_ETC1, false, convert_params))
               {
                  console::error("Texture conversion failed!");
                  return false;
               }
               double time1 = t.get_elapsed_secs();
               total_time1 += time1;
               console::printf("Elapsed time: %3.3f", time1);

               console::printf("Compress 2");

               mipmapped_texture tex2(orig_tex);
               convert_params.m_endpoint_caching = false;
               convert_params.m_compressor = cCRNDXTCompressorCRN;
               convert_params.m_quality = cCRNDXTQualitySuperFast;
               t.start();
               if (!tex2.convert(PIXEL_FMT_ETC1, false, convert_params))
               {
                  console::error("Texture conversion failed!");
                  return false;
               }
               double time2 = t.get_elapsed_secs();
               total_time2 += time2;
               console::printf("Elapsed time: %3.3f", time2);

               image_u8 hybrid_img(img.get_width(), img.get_height());

               for (uint l = 0; l < orig_tex.get_num_levels(); l++)
               {
                  image_u8 orig_img, img1, img2;

                  image_u8* pOrig = orig_tex.get_level(0, l)->get_unpacked_image(orig_img, cUnpackFlagUncook | cUnpackFlagUnflip);
                  image_u8* pImg1 = tex1.get_level(0, l)->get_unpacked_image(img1, cUnpackFlagUncook | cUnpackFlagUnflip);
                  image_u8* pImg2 = tex2.get_level(0, l)->get_unpacked_image(img2, cUnpackFlagUncook | cUnpackFlagUnflip);

                  const uint num_blocks_x = pOrig->get_block_width(4);
                  const uint num_blocks_y = pOrig->get_block_height(4);

                  crnlib::vector<image_utils::error_metrics> metrics[2];

                  for (uint by = 0; by < num_blocks_y; by++)
                  {
                     for (uint bx = 0; bx < num_blocks_x; bx++)
                     {
                        pOrig->extract_block(o.get_ptr(), bx * 4, by * 4, 4, 4);
                        pImg1->extract_block(a.get_ptr(), bx * 4, by * 4, 4, 4);
                        pImg2->extract_block(b.get_ptr(), bx * 4, by * 4, 4, 4);

                        image_utils::error_metrics em1;
                        em1.compute(o, a, first_channel, num_channels);

                        image_utils::error_metrics em2;
                        em2.compute(o, b, first_channel, num_channels);

                        metrics[0].push_back(em1);
                        metrics[1].push_back(em2);

                        if (em1.mPeakSNR < em2.mPeakSNR)
                        {
                           add_bad_block(o);

                           hybrid_img.blit(bx * 4, by * 4, b);
                        }
                        else
                        {
                           hybrid_img.blit(bx * 4, by * 4, a);
                        }
                     }
                  }

                  if (cmd_line_params.has_key("writehybrid"))
                     image_utils::write_to_file("hybrid.tga", hybrid_img, image_utils::cWriteFlagIgnoreAlpha);

                  console::printf("---- Mip level: %u, Total blocks: %ux%u, %u", l, num_blocks_x, num_blocks_y, num_blocks_x * num_blocks_y);

                  console::printf("Compressor 1:");
                  print_metric_stats(metrics[0], num_blocks_x, num_blocks_y);

                  console::printf("Compressor 2:");
                  print_metric_stats(metrics[1], num_blocks_x, num_blocks_y);

                  console::printf("Compressor 1 vs. 2:");
                  print_comparative_metric_stats(cmd_line_params, metrics[0], metrics[1], num_blocks_x, num_blocks_y);

                  image_utils::error_metrics em;

                  em.compute(*pOrig, *pImg1, 0, perceptual ? 0 : 3);
                  em.print("Compressor 1: ");

                  em.compute(*pOrig, *pImg2, 0, perceptual ? 0 : 3);
                  em.print("Compressor 2: ");

                  em.compute(*pOrig, hybrid_img, 0, perceptual ? 0 : 3);
                  em.print("Best of Both: ");
               }
            }

         }  // file_index
      } 

      flush_bad_blocks();

      console::printf("Total times: %4.3f vs. %4.3f", total_time1, total_time2);

      return true;
   }

} // namespace crnlib

