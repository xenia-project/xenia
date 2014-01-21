// File: crunch.cpp - Command line tool for DDS/CRN texture compression/decompression.
// This tool exposes all of crnlib's functionality. It also uses a bunch of internal crlib
// classes that aren't directly exposed in the main crnlib.h header. The actual tool is
// implemented as a single class "crunch" which in theory is reusable. Most of the heavy
// lifting is actually done by functions in the crnlib::texture_conversion namespace,
// which are mostly wrappers over the public crnlib.h functions.
// See Copyright Notice and license at the end of inc/crnlib.h
//
// Important: If compiling with gcc, be sure strict aliasing is disabled: -fno-strict-aliasing
#include "crn_core.h"
#include "crn_console.h"

#include "crn_colorized_console.h"

#include "crn_find_files.h"
#include "crn_file_utils.h"
#include "crn_command_line_params.h"

#include "crn_dxt.h"
#include "crn_cfile_stream.h"
#include "crn_texture_conversion.h"

#define CRND_HEADER_FILE_ONLY
#include "crn_decomp.h"

#include "corpus_gen.h"
#include "corpus_test.h"

using namespace crnlib;

const int cDefaultCRNQualityLevel = 128;

class crunch
{
   CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(crunch);

   cfile_stream m_log_stream;

   uint32 m_num_processed;
   uint32 m_num_failed;
   uint32 m_num_succeeded;
   uint32 m_num_skipped;

public:
   crunch() :
      m_num_processed(0),
      m_num_failed(0),
      m_num_succeeded(0),
      m_num_skipped(0)
   {
   }

   ~crunch()
   {
   }

   enum convert_status
   {
      cCSFailed,
      cCSSucceeded,
      cCSSkipped,
      cCSBadParam,
   };

   inline uint32 get_num_processed() const { return m_num_processed; }
   inline uint32 get_num_failed() const { return m_num_failed; }
   inline uint32 get_num_succeeded() const { return m_num_succeeded; }
   inline uint32 get_num_skipped() const { return m_num_skipped; }

   static void print_usage()
   {
      //                -------------------------------------------------------------------------------
      console::message("\nCommand line usage:");
      console::printf("crunch [options] -file filename");
      console::printf("-file filename - Required input filename, wildcards, multiple -file params OK.");
      console::printf("-file @list.txt - List of files to convert.");
      console::printf("Supported source file formats: dds,ktx,crn,tga,bmp,png,jpg/jpeg,psd");
      console::printf("Note: Some file format variants are unsupported.");
      console::printf("See the docs for stb_image.c: http://www.nothings.org/stb_image.c");
      console::printf("Progressive JPEG files are supported, see: http://code.google.com/p/jpeg-compressor/");

      console::message("\nPath/file related parameters:");
      console::printf("-out filename - Output filename");
      console::printf("-outdir dir - Output directory");
      console::printf("-outsamedir - Write output file to input directory");
      console::printf("-deep - Recurse subdirectories, default=false");
      console::printf("-nooverwrite - Don't overwrite existing files");
      console::printf("-timestamp - Update only changed files");
      console::printf("-forcewrite - Overwrite read-only files");
      console::printf("-recreate - Recreate directory structure");
      console::printf("-fileformat [dds,ktx,crn,tga,bmp,png] - Output file format, default=crn or dds");

      console::message("\nModes:");
      console::printf("-compare - Compare input and output files (no output files are written).");
      console::printf("-info - Only display input file statistics (no output files are written).");

      console::message("\nMisc. options:");
      console::printf("-helperThreads # - Set number of helper threads, 0-16, default=(# of CPU's)-1");
      console::printf("-noprogress - Disable progress output");
      console::printf("-quiet - Disable all console output");
      console::printf("-ignoreerrors - Continue processing files after errors. Note: The default");
      console::printf("                behavior is to immediately exit whenever an error occurs.");
      console::printf("-logfile filename - Append output to log file");
      console::printf("-pause - Wait for keypress on error");
      console::printf("-window <left> <top> <right> <bottom> - Crop window before processing");
      console::printf("-clamp <width> <height> - Crop image if larger than width/height");
      console::printf("-clampscale <width> <height> - Scale image if larger than width/height");
      console::printf("-nostats - Disable all output file statistics (faster)");
      console::printf("-imagestats - Print various image qualilty statistics");
      console::printf("-mipstats - Print statistics for each mipmap, not just the top mip");
      console::printf("-lzmastats - Print size of output file compressed with LZMA codec");
      console::printf("-split - Write faces/mip levels to multiple separate output PNG files");
      console::printf("-yflip - Always flip texture on Y axis before processing");
      console::printf("-unflip - Unflip texture if read from source file as flipped");

      console::message("\nImage rescaling (mutually exclusive options)");
      console::printf("-rescale <int> <int> - Rescale image to specified resolution");
      console::printf("-relscale <float> <float> - Rescale image to specified relative resolution");
      console::printf("-rescalemode <nearest | hi | lo> - Auto-rescale non-power of two images");
      console::printf(" nearest - Use nearest power of 2, hi - Use next, lo - Use previous");

      console::message("\nDDS/CRN compression quality control:");
      console::printf("-quality # (or /q #) - Set Clustered DDS/CRN quality factor [0-255] 255=best");
      console::printf("       DDS default quality is best possible.");
      console::printf("       CRN default quality is %u.", cDefaultCRNQualityLevel);
      console::printf("-bitrate # - Set the desired output bitrate of DDS or CRN output files.");
      console::printf("             This option causes crunch to find the quality factor");
      console::printf("             closest to the desired bitrate using a binary search.");

      console::message("\nLow-level CRN specific options:");
      console::printf("-c # - Color endpoint palette size, 32-8192, default=3072");
      console::printf("-s # - Color selector palette size, 32-8192, default=3072");
      console::printf("-ca # - Alpha endpoint palette size, 32-8192, default=3072");
      console::printf("-sa # - Alpha selector palette size, 32-8192, default=3072");

      //                -------------------------------------------------------------------------------
      console::message("\nMipmap filtering options:");
      console::printf("-mipMode [UseSourceOrGenerate,UseSource,Generate,None]");
      console::printf("         Default mipMode is UseSourceOrGenerate");
      console::printf(" UseSourceOrGenerate: Use source mipmaps if possible, or create new mipmaps.");
      console::printf(" UseSource: Always use source mipmaps, if any (never generate new mipmaps)");
      console::printf(" Generate: Always generate a new mipmap chain (ignore source mipmaps)");
      console::printf(" None: Do not output any mipmaps");
      console::printf("-mipFilter [box,tent,lanczos4,mitchell,kaiser], default=kaiser");
      console::printf("-gamma # - Mipmap gamma correction value, default=2.2, use 1.0 for linear");
      console::printf("-blurriness # - Scale filter kernel, >1=blur, <1=sharpen, .01-8, default=.9");
      console::printf("-wrap - Assume texture is tiled when filtering, default=clamping");
      console::printf("-renormalize - Renormalize filtered normal map texels, default=disabled");
      console::printf("-maxmips # - Limit number of generated texture mipmap levels, 1-16, default=16");
      console::printf("-minmipsize # - Smallest allowable mipmap resolution, default=1");

      console::message("\nCompression options:");
      console::printf("-alphaThreshold # - Set DXT1A alpha threshold, 0-255, default=128");
      console::printf(" Note: -alphaThreshold also changes the compressor's behavior to");
      console::printf(" prefer DXT1A over DXT5 for images with alpha channels (.DDS only).");
      console::printf("-uniformMetrics - Use uniform color metrics, default=use perceptual metrics");
      console::printf("-noAdaptiveBlocks - Disable adaptive block sizes (i.e. disable macroblocks).");
#ifdef CRNLIB_SUPPORT_ATI_COMPRESS
      console::printf("-compressor [CRN,CRNF,RYG,ATI] - Set DXTn compressor, default=CRN");
#else
      console::printf("-compressor [CRN,CRNF,RYG] - Set DXTn compressor, default=CRN");
#endif
      console::printf("-dxtQuality [superfast,fast,normal,better,uber] - Endpoint optimizer speed.");
      console::printf("            Sets endpoint optimizer's max iteration depth. Default=uber.");
      console::printf("-noendpointcaching - Don't try reusing previous DXT endpoint solutions.");
      console::printf("-grayscalsampling - Assume shader will convert fetched results to luma (Y).");
      console::printf("-forceprimaryencoding - Only use DXT1 color4 and DXT5 alpha8 block encodings.");
      console::printf("-usetransparentindicesforblack - Try DXT1 transparent indices for dark pixels.");

      console::message("\nOuptut pixel format options:");
      console::printf("-usesourceformat - Use input file's format for output format (when possible).");
      console::message("\nAll supported texture formats (Note: .CRN only supports DXTn pixel formats):");
      for (uint32 i = 0; i < pixel_format_helpers::get_num_formats(); i++)
      {
         pixel_format fmt = pixel_format_helpers::get_pixel_format_by_index(i);
         console::printf("-%s", pixel_format_helpers::get_pixel_format_string(fmt));
      }

      console::printf("\nFor bugs, support, or feedback: richgel99@gmail.com");
   }

   bool convert(const char* pCommand_line)
   {
      m_num_processed = 0;
      m_num_failed = 0;
      m_num_succeeded = 0;
      m_num_skipped = 0;

      command_line_params::param_desc std_params[] =
      {
         { "file", 1, true },

         { "out", 1, false },
         { "outdir", 1, false },
         { "outsamedir", 0, false },
         { "deep", 0, false },
         { "fileformat", 1, false },

         { "helperThreads", 1, false },
         { "noprogress", 0, false },
         { "quiet", 0, false },
         { "ignoreerrors", 0, false },
         { "logfile", 1, false },

         { "q", 1, false },
         { "quality", 1, false },

         { "c", 1, false },
         { "s", 1, false },
         { "ca", 1, false },
         { "sa", 1, false },

         { "mipMode", 1, false },
         { "mipFilter", 1, false },
         { "gamma", 1, false },
         { "blurriness", 1, false },
         { "wrap", 0, false },
         { "renormalize", 0, false },
         { "noprogress", 0, false },
         { "paramdebug", 0, false },
         { "debug", 0, false },
         { "quick", 0, false },
         { "imagestats", 0, false },
         { "nostats", 0, false },
         { "mipstats", 0, false },

         { "alphaThreshold", 1, false },
         { "uniformMetrics", 0, false },
         { "noAdaptiveBlocks", 0, false },
         { "compressor", 1, false },
         { "dxtQuality", 1, false },
         { "noendpointcaching", 0, false },
         { "grayscalesampling", 0, false  },
         { "converttoluma", 0, false  },
         { "setalphatoluma", 0, false  },
         { "pause", 0, false  },
         { "timestamp", 0, false  },
         { "nooverwrite", 0, false  },
         { "forcewrite", 0, false  },
         { "recreate", 0, false  },
         { "compare", 0, false  },
         { "info", 0, false  },
         { "forceprimaryencoding", 0, false },
         { "usetransparentindicesforblack", 0, false  },
         { "usesourceformat", 0, false  },

         { "rescalemode", 1, false },
         { "rescale", 2, false },
         { "relrescale", 2, false },
         { "clamp", 2, false },
         { "clampScale", 2, false },
         { "window", 4, false },

         { "maxmips", 1, false },
         { "minmipsize", 1, false },

         { "bitrate", 1, false },

         { "lzmastats", 0, false },
         { "split", 0, false },
         { "csvfile", 1, false },

         { "yflip", 0, false },
         { "unflip", 0, false },
      };

      crnlib::vector<command_line_params::param_desc> params;
      params.append(std_params, sizeof(std_params) / sizeof(std_params[0]));

      for (uint32 i = 0; i < pixel_format_helpers::get_num_formats(); i++)
      {
         pixel_format fmt = pixel_format_helpers::get_pixel_format_by_index(i);

         command_line_params::param_desc desc;
         desc.m_pName = pixel_format_helpers::get_pixel_format_string(fmt);
         desc.m_num_values = 0;
         desc.m_support_listing_file = false;
         params.push_back(desc);
      }

      if (!m_params.parse(pCommand_line, params.size(), params.get_ptr(), true))
      {
         return false;
      }

      if (!m_params.get_num_params())
      {
         console::error("No command line parameters specified!");

         print_usage();

         return false;
      }

#if 0
      if (m_params.get_count(""))
      {
         console::error("Unrecognized command line parameter: \"%s\"", m_params.get_value_as_string_or_empty("", 0).get_ptr());

         return false;
      }
#endif

      if (m_params.get_value_as_bool("debug"))
      {
         console::debug("Command line parameters:");
         for (command_line_params::param_map_const_iterator it = m_params.begin(); it != m_params.end(); ++it)
         {
            console::disable_crlf();
            console::debug("Key:\"%s\" Values (%u): ", it->first.get_ptr(), it->second.m_values.size());
            for (uint32 i = 0; i < it->second.m_values.size(); i++)
               console::debug("\"%s\" ", it->second.m_values[i].get_ptr());
            console::debug("\n");
            console::enable_crlf();
         }
      }

      dynamic_string log_filename;
      if (m_params.get_value_as_string("logfile", 0, log_filename))
      {
         if (!m_log_stream.open(log_filename.get_ptr(), cDataStreamWritable | cDataStreamSeekable, true))
         {
            console::error("Unable to open log file: \"%s\"", log_filename.get_ptr());
            return false;
         }

         console::printf("Appending output to log file \"%s\"", log_filename.get_ptr());

         console::set_log_stream(&m_log_stream);
      }

      bool status = convert();

      if (m_log_stream.is_opened())
      {
         console::set_log_stream(NULL);

         m_log_stream.close();
      }

      return status;
   }

private:
   command_line_params m_params;

   bool convert()
   {
      find_files::file_desc_vec files;

      uint32 total_input_specs = 0;

      for (uint32 phase = 0; phase < 2; phase++)
      {
         command_line_params::param_map_const_iterator begin, end;
         m_params.find(phase ? "" : "file", begin, end);
         for (command_line_params::param_map_const_iterator it = begin; it != end; ++it)
         {
            total_input_specs++;

            const dynamic_string_array& strings = it->second.m_values;
            for (uint32 i = 0; i < strings.size(); i++)
            {
               if (!process_input_spec(files, strings[i]))
               {
                  if (!m_params.get_value_as_bool("ignoreerrors"))
                     return false;
               }
            }
         }
      }

      if (!total_input_specs)
      {
         console::error("No input files specified!");
         return false;
      }

      if (files.empty())
      {
         console::error("No files found to process!");
         return false;
      }

      std::sort(files.begin(), files.end());
      files.resize((uint32)(std::unique(files.begin(), files.end()) - files.begin()));

      timer tm;
      tm.start();

      if (!process_files(files))
      {
         if (!m_params.get_value_as_bool("ignoreerrors"))
            return false;
      }

      double total_time = tm.get_elapsed_secs();

      console::printf("Total time: %3.3fs", total_time);

      console::printf(
         ((m_num_skipped) || (m_num_failed)) ? cWarningConsoleMessage : cInfoConsoleMessage,
         "%u total file(s) successfully processed, %u file(s) skipped, %u file(s) failed.", m_num_succeeded, m_num_skipped, m_num_failed);

      return true;
   }

   bool process_input_spec(find_files::file_desc_vec& files, const dynamic_string& input_spec)
   {
      dynamic_string find_name(input_spec);

      if ((find_name.get_len()) && (file_utils::does_dir_exist(find_name.get_ptr())))
      {
         file_utils::combine_path(find_name, find_name.get_ptr(), "*");
      }

      if ((find_name.is_empty()) || (!file_utils::full_path(find_name)))
      {
         console::error("Invalid input filename: %s", find_name.get_ptr());
         return false;
      }

      const bool deep_flag = m_params.get_value_as_bool("deep");

      dynamic_string find_drive, find_path, find_fname, find_ext;
      file_utils::split_path(find_name.get_ptr(), &find_drive, &find_path, &find_fname, &find_ext);

      dynamic_string find_pathname;
      file_utils::combine_path(find_pathname, find_drive.get_ptr(), find_path.get_ptr());
      dynamic_string find_filename;
      find_filename = find_fname + find_ext;

      find_files file_finder;
      bool success = file_finder.find(find_pathname.get_ptr(), find_filename.get_ptr(), find_files::cFlagAllowFiles | (deep_flag ? find_files::cFlagRecursive : 0));
      if (!success)
      {
         console::error("Failed finding files: %s", find_name.get_ptr());
         return false;
      }

      if (file_finder.get_files().empty())
      {
         console::warning("No files found: %s", find_name.get_ptr());
         return true;
      }

      files.append(file_finder.get_files());

      return true;
   }

   bool read_only_file_check(const char* pDst_filename)
   {
      if (!file_utils::is_read_only(pDst_filename))
         return true;

      if (m_params.get_value_as_bool("forcewrite"))
      {
         if (file_utils::disable_read_only(pDst_filename))
         {
            console::warning("Setting read-only file \"%s\" to writable", pDst_filename);
            return true;
         }
         else
         {
            console::error("Failed setting read-only file \"%s\" to writable!", pDst_filename);
            return false;
         }
      }

      console::error("Output file \"%s\" is read-only!", pDst_filename);

      return false;
   }

   bool process_files(find_files::file_desc_vec& files)
   {
      const bool compare_mode = m_params.get_value_as_bool("compare");
      const bool info_mode = m_params.get_value_as_bool("info");

      for (uint32 file_index = 0; file_index < files.size(); file_index++)
      {
         const find_files::file_desc& file_desc = files[file_index];
         const dynamic_string& in_filename = file_desc.m_fullname;

         dynamic_string in_drive, in_path, in_fname, in_ext;
         file_utils::split_path(in_filename.get_ptr(), &in_drive, &in_path, &in_fname, &in_ext);

         texture_file_types::format out_file_type = texture_file_types::cFormatCRN;
         dynamic_string fmt;
         if (m_params.get_value_as_string("fileformat", 0, fmt))
         {
            if (fmt == "tga")
               out_file_type = texture_file_types::cFormatTGA;
            else if (fmt == "bmp")
               out_file_type = texture_file_types::cFormatBMP;
            else if (fmt == "dds")
               out_file_type = texture_file_types::cFormatDDS;
            else if (fmt == "ktx")
               out_file_type = texture_file_types::cFormatKTX;
            else if (fmt == "crn")
               out_file_type = texture_file_types::cFormatCRN;
            else if (fmt == "png")
               out_file_type = texture_file_types::cFormatPNG;
            else
            {
               console::error("Unsupported output file type: %s", fmt.get_ptr());
               return false;
            }
         }

         // No explicit output format has been specified - try to determine something doable.
         if (!m_params.has_key("fileformat"))
         {
            if (m_params.has_key("split"))
            {
               out_file_type = texture_file_types::cFormatPNG;
            }
            else
            {
               texture_file_types::format input_file_type = texture_file_types::determine_file_format(in_filename.get_ptr());
               if (input_file_type == texture_file_types::cFormatCRN)
               {
                  // Automatically transcode CRN->DXTc and write to DDS files, unless the user specifies either the /fileformat or /split options.
                  out_file_type = texture_file_types::cFormatDDS;
               }
               else if (input_file_type == texture_file_types::cFormatKTX)
               {
                  // Default to converting KTX files to PNG
                  out_file_type = texture_file_types::cFormatPNG;
               }
            }
         }

         dynamic_string out_filename;
         if (m_params.get_value_as_bool("outsamedir"))
            out_filename.format("%s%s%s.%s", in_drive.get_ptr(), in_path.get_ptr(), in_fname.get_ptr(), texture_file_types::get_extension(out_file_type));
         else if (m_params.has_key("out"))
         {
            out_filename = m_params.get_value_as_string_or_empty("out");

            if (files.size() > 1)
            {
               dynamic_string out_drive, out_dir, out_name, out_ext;
               file_utils::split_path(out_filename.get_ptr(), &out_drive, &out_dir, &out_name, &out_ext);

               out_name.format("%s_%u", out_name.get_ptr(), file_index);

               out_filename.format("%s%s%s%s", out_drive.get_ptr(), out_dir.get_ptr(), out_name.get_ptr(), out_ext.get_ptr());
            }

            if (!m_params.has_key("fileformat"))
               out_file_type = texture_file_types::determine_file_format(out_filename.get_ptr());
         }
         else
         {
            dynamic_string out_dir(m_params.get_value_as_string_or_empty("outdir"));

            if (m_params.get_value_as_bool("recreate") && file_desc.m_rel.get_len())
            {
               file_utils::combine_path(out_dir, out_dir.get_ptr(), file_desc.m_rel.get_ptr());
            }

            if (out_dir.get_len())
            {
               if (file_utils::is_path_separator(out_dir.back()))
                  out_filename.format("%s%s.%s", out_dir.get_ptr(), in_fname.get_ptr(), texture_file_types::get_extension(out_file_type));
               else
                  out_filename.format("%s\\%s.%s", out_dir.get_ptr(), in_fname.get_ptr(), texture_file_types::get_extension(out_file_type));
            }
            else
            {
               out_filename.format("%s.%s", in_fname.get_ptr(), texture_file_types::get_extension(out_file_type));
            }

            if (m_params.get_value_as_bool("recreate"))
            {
               if (file_utils::full_path(out_filename))
               {
                  if ((!compare_mode) && (!info_mode))
                  {
                     dynamic_string out_drive, out_path;
                     file_utils::split_path(out_filename.get_ptr(), &out_drive, &out_path, NULL, NULL);
                     out_drive += out_path;
                     file_utils::create_path(out_drive.get_ptr());
                  }
               }
            }
         }

         if ((!compare_mode) && (!info_mode))
         {
            if (file_utils::does_file_exist(out_filename.get_ptr()))
            {
               if (m_params.get_value_as_bool("nooverwrite"))
               {
                  console::warning("Skipping already existing file: %s\n", out_filename.get_ptr());
                  m_num_skipped++;
                  continue;
               }

               if (m_params.get_value_as_bool("timestamp"))
               {
                  if (file_utils::is_older_than(in_filename.get_ptr(), out_filename.get_ptr()))
                  {
                     console::warning("Skipping up to date file: %s\n", out_filename.get_ptr());
                     m_num_skipped++;
                     continue;
                  }
               }
            }
         }

         convert_status status = cCSFailed;

         if (info_mode)
            status = display_file_info(file_index, files.size(), in_filename.get_ptr());
         else if (compare_mode)
            status = compare_file(file_index, files.size(), in_filename.get_ptr(), out_filename.get_ptr(), out_file_type);
         else if (read_only_file_check(out_filename.get_ptr()))
            status = convert_file(file_index, files.size(), in_filename.get_ptr(), out_filename.get_ptr(), out_file_type);

         m_num_processed++;

         switch (status)
         {
            case cCSSucceeded:
            {
               console::info("");
               m_num_succeeded++;
               break;
            }
            case cCSSkipped:
            {
               console::info("Skipping file.\n");
               m_num_skipped++;
               break;
            }
            case cCSBadParam:
            {
               return false;
            }
            default:
            {
               if (!m_params.get_value_as_bool("ignoreerrors"))
                  return false;

               console::info("");

               m_num_failed++;
               break;
            }
         }
      }

      return true;
   }

   void print_texture_info(const char* pTex_desc, texture_conversion::convert_params& params, mipmapped_texture& tex)
   {
      console::info("%s: %ux%u, Levels: %u, Faces: %u, Format: %s",
         pTex_desc,
         tex.get_width(),
         tex.get_height(),
         tex.get_num_levels(),
         tex.get_num_faces(),
         pixel_format_helpers::get_pixel_format_string(tex.get_format()));

      console::disable_crlf();
      console::info("Apparent type: %s, ", get_texture_type_desc(params.m_texture_type));

      console::info("Flags: ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagRValid) console::info("R ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagGValid) console::info("G ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagBValid) console::info("B ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagAValid) console::info("A ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagGrayscale) console::info("Grayscale ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagNormalMap) console::info("NormalMap ");
      if (tex.get_comp_flags() & pixel_format_helpers::cCompFlagLumaChroma) console::info("LumaChroma ");
      if (tex.is_flipped()) console::info("Flipped "); else console::info("Non-Flipped ");
      console::info("\n");
      console::enable_crlf();
   }

   static bool progress_callback_func(uint32 percentage_complete, void* pUser_data_ptr)
   {
      pUser_data_ptr;

      console::disable_crlf();

      char buf[8];
      for (uint32 i = 0; i < 7; i++)
         buf[i] = 8;
      buf[7] = '\0';

      for (uint32 i = 0; i < 130/8; i++)
         console::progress(buf);

      console::progress("Processing: %u%%", percentage_complete);

      for (uint32 i = 0; i < 7; i++)
         buf[i] = ' ';
      console::progress(buf);
      console::progress(buf);

      for (uint32 i = 0; i < 7; i++)
         buf[i] = 8;
      console::progress(buf);
      console::progress(buf);

      console::enable_crlf();

      return true;
   }

   bool parse_mipmap_params(crn_mipmap_params& mip_params)
   {
      dynamic_string val;

      if (m_params.get_value_as_string("mipMode", 0, val))
      {
         uint32 i;
         for (i = 0; i < cCRNMipModeTotal; i++)
         {
            if (val == crn_get_mip_mode_name( static_cast<crn_mip_mode>(i) ))
            {
               mip_params.m_mode = static_cast<crn_mip_mode>(i);
               break;
            }
         }
         if (i == cCRNMipModeTotal)
         {
            console::error("Invalid MipMode: \"%s\"", val.get_ptr());
            return false;
         }
      }

      if (m_params.get_value_as_string("mipFilter", 0, val))
      {
         uint32 i;
         for (i = 0; i < cCRNMipFilterTotal; i++)
         {
            if (val == dynamic_string(crn_get_mip_filter_name( static_cast<crn_mip_filter>(i) )) )
            {
               mip_params.m_filter = static_cast<crn_mip_filter>(i);
               break;
            }
         }

         if (i == cCRNMipFilterTotal)
         {
            console::error("Invalid MipFilter: \"%s\"", val.get_ptr());
            return false;
         }

         if (i == cCRNMipFilterBox)
            mip_params.m_blurriness = 1.0f;
      }

      mip_params.m_gamma = m_params.get_value_as_float("gamma", 0, mip_params.m_gamma, .1f, 8.0f);
      mip_params.m_gamma_filtering = (mip_params.m_gamma != 1.0f);

      mip_params.m_blurriness = m_params.get_value_as_float("blurriness", 0, mip_params.m_blurriness, .01f, 8.0f);

      mip_params.m_renormalize = m_params.get_value_as_bool("renormalize", 0, mip_params.m_renormalize != 0);
      mip_params.m_tiled = m_params.get_value_as_bool("wrap");

      mip_params.m_max_levels = m_params.get_value_as_int("maxmips", 0, cCRNMaxLevels, 1, cCRNMaxLevels);
      mip_params.m_min_mip_size = m_params.get_value_as_int("minmipsize", 0, 1, 1, cCRNMaxLevelResolution);

      return true;
   }

   bool parse_scale_params(crn_mipmap_params &mipmap_params)
   {
      if (m_params.has_key("rescale"))
      {
         int w = m_params.get_value_as_int("rescale", 0, -1, 1, cCRNMaxLevelResolution, 0);
         int h = m_params.get_value_as_int("rescale", 0, -1, 1, cCRNMaxLevelResolution, 1);

         mipmap_params.m_scale_mode = cCRNSMAbsolute;
         mipmap_params.m_scale_x = (float)w;
         mipmap_params.m_scale_y = (float)h;
      }
      else if (m_params.has_key("relrescale"))
      {
         float w = m_params.get_value_as_float("relrescale", 0, 1, 1, 256, 0);
         float h = m_params.get_value_as_float("relrescale", 0, 1, 1, 256, 1);

         mipmap_params.m_scale_mode = cCRNSMRelative;
         mipmap_params.m_scale_x = w;
         mipmap_params.m_scale_y = h;
      }
      else if (m_params.has_key("rescalemode"))
      {
         // nearest | hi | lo

         dynamic_string mode_str(m_params.get_value_as_string_or_empty("rescalemode"));
         if (mode_str == "nearest")
            mipmap_params.m_scale_mode = cCRNSMNearestPow2;
         else if (mode_str == "hi")
            mipmap_params.m_scale_mode = cCRNSMNextPow2;
         else if (mode_str == "lo")
            mipmap_params.m_scale_mode = cCRNSMLowerPow2;
         else
         {
            console::error("Invalid rescale mode: \"%s\"", mode_str.get_ptr());
            return false;
         }
      }

      if (m_params.has_key("clamp"))
      {
         uint32 w = m_params.get_value_as_int("clamp", 0, 1, 1, cCRNMaxLevelResolution, 0);
         uint32 h = m_params.get_value_as_int("clamp", 0, 1, 1, cCRNMaxLevelResolution, 1);

         mipmap_params.m_clamp_scale = false;
         mipmap_params.m_clamp_width = w;
         mipmap_params.m_clamp_height = h;
      }
      else if (m_params.has_key("clampScale"))
      {
         uint32 w = m_params.get_value_as_int("clampscale", 0, 1, 1, cCRNMaxLevelResolution, 0);
         uint32 h = m_params.get_value_as_int("clampscale", 0, 1, 1, cCRNMaxLevelResolution, 1);

         mipmap_params.m_clamp_scale = true;
         mipmap_params.m_clamp_width = w;
         mipmap_params.m_clamp_height = h;
      }

      if (m_params.has_key("window"))
      {
         uint32 xl = m_params.get_value_as_int("window", 0, 0, 0, cCRNMaxLevelResolution, 0);
         uint32 yl = m_params.get_value_as_int("window", 0, 0, 0, cCRNMaxLevelResolution, 1);
         uint32 xh = m_params.get_value_as_int("window", 0, 0, 0, cCRNMaxLevelResolution, 2);
         uint32 yh = m_params.get_value_as_int("window", 0, 0, 0, cCRNMaxLevelResolution, 3);

         mipmap_params.m_window_left = math::minimum(xl, xh);
         mipmap_params.m_window_top = math::minimum(yl, yh);
         mipmap_params.m_window_right = math::maximum(xl, xh);
         mipmap_params.m_window_bottom = math::maximum(yl, yh);
      }

      return true;
   }

   bool parse_comp_params(texture_file_types::format dst_file_format, crn_comp_params &comp_params)
   {
      if (dst_file_format == texture_file_types::cFormatCRN)
         comp_params.m_quality_level = cDefaultCRNQualityLevel;

      if (m_params.has_key("q") || m_params.has_key("quality"))
      {
         const char *pKeyName = m_params.has_key("q") ? "q" : "quality";

         if ((dst_file_format == texture_file_types::cFormatDDS) || (dst_file_format == texture_file_types::cFormatCRN) || (dst_file_format == texture_file_types::cFormatKTX))
         {
            uint32 i = m_params.get_value_as_int(pKeyName, 0, cDefaultCRNQualityLevel, 0, cCRNMaxQualityLevel);

            comp_params.m_quality_level = i;
         }
         else
         {
            console::error("/quality or /q option is only invalid when writing DDS, KTX or CRN files!");
            return false;
         }
      }
      else
      {
         float desired_bitrate = m_params.get_value_as_float("bitrate", 0, 0.0f, .1f, 30.0f);
         if (desired_bitrate > 0.0f)
         {
            comp_params.m_target_bitrate = desired_bitrate;
         }
      }

      int color_endpoints = m_params.get_value_as_int("c", 0, 0, cCRNMinPaletteSize, cCRNMaxPaletteSize);
      int color_selectors = m_params.get_value_as_int("s", 0, 0, cCRNMinPaletteSize, cCRNMaxPaletteSize);
      int alpha_endpoints = m_params.get_value_as_int("ca", 0, 0, cCRNMinPaletteSize, cCRNMaxPaletteSize);
      int alpha_selectors = m_params.get_value_as_int("sa", 0, 0, cCRNMinPaletteSize, cCRNMaxPaletteSize);
      if ( ((color_endpoints > 0) && (color_selectors > 0)) ||
           ((alpha_endpoints > 0) && (alpha_selectors > 0)) )
      {
         comp_params.set_flag(cCRNCompFlagManualPaletteSizes, true);
         comp_params.m_crn_color_endpoint_palette_size = color_endpoints;
         comp_params.m_crn_color_selector_palette_size = color_selectors;
         comp_params.m_crn_alpha_endpoint_palette_size = alpha_endpoints;
         comp_params.m_crn_alpha_selector_palette_size = alpha_selectors;
      }

      if (m_params.has_key("alphaThreshold"))
      {
         int dxt1a_alpha_threshold = m_params.get_value_as_int("alphaThreshold", 0, 128, 0, 255);
         comp_params.m_dxt1a_alpha_threshold = dxt1a_alpha_threshold;
         if (dxt1a_alpha_threshold > 0)
         {
            comp_params.set_flag(cCRNCompFlagDXT1AForTransparency, true);
         }
      }

      comp_params.set_flag(cCRNCompFlagPerceptual, !m_params.get_value_as_bool("uniformMetrics"));
      comp_params.set_flag(cCRNCompFlagHierarchical, !m_params.get_value_as_bool("noAdaptiveBlocks"));

      if (m_params.has_key("helperThreads"))
         comp_params.m_num_helper_threads = m_params.get_value_as_int("helperThreads", 0, cCRNMaxHelperThreads, 0, cCRNMaxHelperThreads);
      else if (g_number_of_processors > 1)
         comp_params.m_num_helper_threads = g_number_of_processors - 1;

      dynamic_string comp_name;
      if (m_params.get_value_as_string("compressor", 0, comp_name))
      {
         uint32 i;
         for (i = 0; i < cCRNTotalDXTCompressors; i++)
         {
            if (comp_name == get_dxt_compressor_name(static_cast<crn_dxt_compressor_type>(i)))
            {
               comp_params.m_dxt_compressor_type = static_cast<crn_dxt_compressor_type>(i);
               break;
            }
         }
         if (i == cCRNTotalDXTCompressors)
         {
            console::error("Invalid compressor: \"%s\"", comp_name.get_ptr());
            return false;
         }
      }

      dynamic_string dxt_quality_str;
      if (m_params.get_value_as_string("dxtquality", 0, dxt_quality_str))
      {
         uint32 i;
         for (i = 0; i < cCRNDXTQualityTotal; i++)
         {
            if (dxt_quality_str == crn_get_dxt_quality_string(static_cast<crn_dxt_quality>(i)))
            {
               comp_params.m_dxt_quality = static_cast<crn_dxt_quality>(i);
               break;
            }
         }
         if (i == cCRNDXTQualityTotal)
         {
            console::error("Invalid DXT quality: \"%s\"", dxt_quality_str.get_ptr());
            return false;
         }
      }
      else
      {
         comp_params.m_dxt_quality = cCRNDXTQualityUber;
      }

      comp_params.set_flag(cCRNCompFlagDisableEndpointCaching, m_params.get_value_as_bool("noendpointcaching"));
      comp_params.set_flag(cCRNCompFlagGrayscaleSampling, m_params.get_value_as_bool("grayscalesampling"));
      comp_params.set_flag(cCRNCompFlagUseBothBlockTypes, !m_params.get_value_as_bool("forceprimaryencoding"));
      if (comp_params.get_flag(cCRNCompFlagUseBothBlockTypes))
         comp_params.set_flag(cCRNCompFlagUseTransparentIndicesForBlack, m_params.get_value_as_bool("usetransparentindicesforblack"));
      else
         comp_params.set_flag(cCRNCompFlagUseTransparentIndicesForBlack, false);

      return true;
   }

   convert_status display_file_info(uint32 file_index, uint32 num_files, const char* pSrc_filename)
   {
      if (num_files > 1)
         console::message("[%u/%u] Source texture: \"%s\"", file_index + 1, num_files, pSrc_filename);
      else
         console::message("Source texture: \"%s\"", pSrc_filename);

      texture_file_types::format src_file_format = texture_file_types::determine_file_format(pSrc_filename);
      if (src_file_format == texture_file_types::cFormatInvalid)
      {
         console::error("Unrecognized file type: %s", pSrc_filename);
         return cCSFailed;
      }

      mipmapped_texture src_tex;
      if (!src_tex.read_from_file(pSrc_filename, src_file_format))
      {
         if (src_tex.get_last_error().is_empty())
            console::error("Failed reading source file: \"%s\"", pSrc_filename);
         else
            console::error("%s", src_tex.get_last_error().get_ptr());

         return cCSFailed;
      }

      uint64 input_file_size;
      file_utils::get_file_size(pSrc_filename, input_file_size);

      uint32 total_in_pixels = 0;
      for (uint32 i = 0; i < src_tex.get_num_levels(); i++)
      {
         uint32 width = math::maximum<uint32>(1, src_tex.get_width() >> i);
         uint32 height = math::maximum<uint32>(1, src_tex.get_height() >> i);
         total_in_pixels += width*height*src_tex.get_num_faces();
      }

      vector<uint8> src_tex_bytes;
      if (!cfile_stream::read_file_into_array(pSrc_filename, src_tex_bytes))
      {
         console::error("Failed loading source file: %s", pSrc_filename);
         return cCSFailed;
      }

      if (!src_tex_bytes.size())
      {
         console::warning("Source file is empty: %s", pSrc_filename);
         return cCSSkipped;
      }

      uint32 compressed_size = 0;
      if (m_params.has_key("lzmastats"))
      {
         lzma_codec lossless_codec;
         vector<uint8> cmp_tex_bytes;
         if (lossless_codec.pack(src_tex_bytes.get_ptr(), src_tex_bytes.size(), cmp_tex_bytes))
         {
            compressed_size = cmp_tex_bytes.size();
         }
      }
      console::info("Source texture dimensions: %ux%u, Levels: %u, Faces: %u, Format: %s\nPacked Format: %u, Apparent Type: %s, Flipped: %u, Can Unflip Without Unpacking: %u",
         src_tex.get_width(),
         src_tex.get_height(),
         src_tex.get_num_levels(),
         src_tex.get_num_faces(),
         pixel_format_helpers::get_pixel_format_string(src_tex.get_format()),
         src_tex.is_packed(), get_texture_type_desc(src_tex.determine_texture_type()),
         src_tex.is_flipped(), src_tex.can_unflip_without_unpacking());

      console::info("Total pixels: %u, Source file size: " CRNLIB_UINT64_FORMAT_SPECIFIER ", Source file bits/pixel: %1.3f",
         total_in_pixels, input_file_size, (input_file_size * 8.0f) / total_in_pixels);
      if (compressed_size)
      {
         console::info("LZMA compressed file size: %u bytes, %1.3f bits/pixel",
            compressed_size, compressed_size * 8.0f / total_in_pixels);
      }

      double entropy = math::compute_entropy(src_tex_bytes.get_ptr(), src_tex_bytes.size());
      console::info("Source file entropy: %3.6f bits per byte", entropy / src_tex_bytes.size());

      if (src_file_format == texture_file_types::cFormatCRN)
      {
         crnd::crn_texture_info tex_info;
         tex_info.m_struct_size = sizeof(crnd::crn_texture_info);
         crn_bool success = crnd::crnd_get_texture_info(src_tex_bytes.get_ptr(), src_tex_bytes.size(), &tex_info);
         if (!success)
            console::error("Failed retrieving CRN texture info!");
         else
         {
            console::info("CRN texture info:");

            console::info("Width: %u, Height: %u, Levels: %u, Faces: %u\nBytes per block: %u, User0: 0x%08X, User1: 0x%08X, CRN Format: %u",
               tex_info.m_width,
               tex_info.m_height,
               tex_info.m_levels,
               tex_info.m_faces,
               tex_info.m_bytes_per_block,
               tex_info.m_userdata0,
               tex_info.m_userdata1,
               tex_info.m_format);
         }
      }

      return cCSSucceeded;
   }

   void print_stats(texture_conversion::convert_stats &stats, bool force_image_stats = false)
   {
      dynamic_string csv_filename;
      const char *pCSVStatsFilename = m_params.get_value_as_string("csvfile", 0, csv_filename) ? csv_filename.get_ptr() : NULL;

      bool image_stats = force_image_stats || m_params.get_value_as_bool("imagestats") || m_params.get_value_as_bool("mipstats") || (pCSVStatsFilename != NULL);
      bool mip_stats = m_params.get_value_as_bool("mipstats");
      bool grayscale_sampling = m_params.get_value_as_bool("grayscalesampling");
      if (!stats.print(image_stats, mip_stats, grayscale_sampling, pCSVStatsFilename))
      {
         console::warning("Unable to compute/display full output file statistics.");
      }
   }

   convert_status compare_file(uint32 file_index, uint32 num_files, const char* pSrc_filename, const char* pDst_filename, texture_file_types::format out_file_type)
   {
      if (num_files > 1)
         console::message("[%u/%u] Comparing source texture \"%s\" to output texture \"%s\"", file_index + 1, num_files, pSrc_filename, pDst_filename);
      else
         console::message("Comparing source texture \"%s\" to output texture \"%s\"", pSrc_filename, pDst_filename);

      texture_file_types::format src_file_format = texture_file_types::determine_file_format(pSrc_filename);
      if (src_file_format == texture_file_types::cFormatInvalid)
      {
         console::error("Unrecognized file type: %s", pSrc_filename);
         return cCSFailed;
      }

      mipmapped_texture src_tex;

      if (!src_tex.read_from_file(pSrc_filename, src_file_format))
      {
         if (src_tex.get_last_error().is_empty())
            console::error("Failed reading source file: \"%s\"", pSrc_filename);
         else
            console::error("%s", src_tex.get_last_error().get_ptr());

         return cCSFailed;
      }

      texture_conversion::convert_stats stats;
      if (!stats.init(pSrc_filename, pDst_filename, src_tex, out_file_type, m_params.has_key("lzmastats")))
         return cCSFailed;

      print_stats(stats, true);

      return cCSSucceeded;
   }

   convert_status convert_file(uint32 file_index, uint32 num_files, const char* pSrc_filename, const char* pDst_filename, texture_file_types::format out_file_type)
   {
      timer tim;

      if (num_files > 1)
         console::message("[%u/%u] Reading source texture: \"%s\"", file_index + 1, num_files, pSrc_filename);
      else
         console::message("Reading source texture: \"%s\"", pSrc_filename);

      texture_file_types::format src_file_format = texture_file_types::determine_file_format(pSrc_filename);
      if (src_file_format == texture_file_types::cFormatInvalid)
      {
         console::error("Unrecognized file type: %s", pSrc_filename);
         return cCSFailed;
      }

      mipmapped_texture src_tex;
      tim.start();
      if (!src_tex.read_from_file(pSrc_filename, src_file_format))
      {
         if (src_tex.get_last_error().is_empty())
            console::error("Failed reading source file: \"%s\"", pSrc_filename);
         else
            console::error("%s", src_tex.get_last_error().get_ptr());

         return cCSFailed;
      }
      double total_time = tim.get_elapsed_secs();
      console::info("Texture successfully loaded in %3.3fs", total_time);

      if (m_params.get_value_as_bool("converttoluma"))
         src_tex.convert(image_utils::cConversion_Y_To_RGB);
      if (m_params.get_value_as_bool("setalphatoluma"))
         src_tex.convert(image_utils::cConversion_Y_To_A);

      texture_conversion::convert_params params;

      params.m_texture_type = src_tex.determine_texture_type();
      params.m_pInput_texture = &src_tex;
      params.m_dst_filename = pDst_filename;
      params.m_dst_file_type = out_file_type;
      params.m_lzma_stats = m_params.has_key("lzmastats");
      params.m_write_mipmaps_to_multiple_files = m_params.has_key("split");
      params.m_always_use_source_pixel_format = m_params.has_key("usesourceformat");
      params.m_y_flip = m_params.has_key("yflip");
      params.m_unflip = m_params.has_key("unflip");

      if ((!m_params.get_value_as_bool("noprogress")) && (!m_params.get_value_as_bool("quiet")))
         params.m_pProgress_func = progress_callback_func;

      if (m_params.get_value_as_bool("debug"))
      {
         params.m_debugging = true;
         params.m_comp_params.set_flag(cCRNCompFlagDebugging, true);
      }

      if (m_params.get_value_as_bool("paramdebug"))
         params.m_param_debugging = true;

      if (m_params.get_value_as_bool("quick"))
         params.m_quick = true;

      params.m_no_stats = m_params.get_value_as_bool("nostats");

      params.m_dst_format = PIXEL_FMT_INVALID;

      for (uint32 i = 0; i < pixel_format_helpers::get_num_formats(); i++)
      {
         pixel_format trial_fmt = pixel_format_helpers::get_pixel_format_by_index(i);
         if (m_params.has_key(pixel_format_helpers::get_pixel_format_string(trial_fmt)))
         {
            params.m_dst_format = trial_fmt;
            break;
         }
      }

      if (texture_file_types::supports_mipmaps(src_file_format))
      {
         params.m_mipmap_params.m_mode = cCRNMipModeUseSourceMips;
      }

      if (!parse_mipmap_params(params.m_mipmap_params))
         return cCSBadParam;

      if (!parse_comp_params(params.m_dst_file_type, params.m_comp_params))
         return cCSBadParam;

      if (!parse_scale_params(params.m_mipmap_params))
         return cCSBadParam;

      print_texture_info("Source texture", params, src_tex);

      if (params.m_texture_type == cTextureTypeNormalMap)
      {
         params.m_comp_params.set_flag(cCRNCompFlagPerceptual, false);
      }

      texture_conversion::convert_stats stats;

      tim.start();
      bool status = texture_conversion::process(params, stats);
      total_time = tim.get_elapsed_secs();

      if (!status)
      {
         if (params.m_error_message.is_empty())
            console::error("Failed writing output file: \"%s\"", pDst_filename);
         else
            console::error(params.m_error_message.get_ptr());
         return cCSFailed;
      }

      console::info("Texture successfully processed in %3.3fs", total_time);

      if (!m_params.get_value_as_bool("nostats"))
         print_stats(stats);

      return cCSSucceeded;
   }
};

//-----------------------------------------------------------------------------------------------------------------------

static bool check_for_option(int argc, char *argv[], const char *pOption)
{
   for (int i = 1; i < argc; i++)
   {
      if ((argv[i][0] == '/') || (argv[i][0] == '-'))
      {
         if (crn_stricmp(&argv[i][1], pOption) == 0)
            return true;
      }
   }
   return false;
}

//-----------------------------------------------------------------------------------------------------------------------

static void print_title()
{
   console::printf("crunch: Advanced DXTn Texture Compressor - http://code.google.com/p/crunch");
   console::printf("Copyright (c) 2010-2012 Rich Geldreich and Tenacious Software LLC");
   console::printf("crnlib version v%u.%02u %s Built %s, %s", CRNLIB_VERSION / 100U, CRNLIB_VERSION % 100U, crnlib_is_x64() ? "x64" : "x86", __DATE__, __TIME__);
   console::printf("");
}

//-----------------------------------------------------------------------------------------------------------------------

static int main_internal(int argc, char *argv[])
{
   argc;
   argv;

   colorized_console::init();

   if (check_for_option(argc, argv, "quiet"))
      console::disable_output();

   print_title();

   dynamic_string cmd_line;
   get_command_line_as_single_string(cmd_line, argc, argv);

   bool status = false;
   if (check_for_option(argc, argv, "corpus_gen"))
   {
      corpus_gen generator;
      status = generator.generate(cmd_line.get_ptr());
   }
   else if (check_for_option(argc, argv, "corpus_test"))
   {
      corpus_tester tester;
      status = tester.test(cmd_line.get_ptr());
   }
   else
   {
      crunch converter;
      status = converter.convert(cmd_line.get_ptr());
   }

   colorized_console::deinit();

   crnlib_print_mem_stats();

   return status ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void pause_and_wait(void)
{
   console::enable_output();

   console::message("\nPress a key to continue.");

   for ( ; ; )
   {
      if (crn_getch() != -1)
         break;
   }
}

//-----------------------------------------------------------------------------------------------------------------------

int main(int argc, char *argv[])
{
   int status = EXIT_FAILURE;

   if (crnlib_is_debugger_present())
   {
      status = main_internal(argc, argv);
   }
   else
   {
#ifdef _MSC_VER
      __try
      {
         status = main_internal(argc, argv);
      }
      __except(EXCEPTION_EXECUTE_HANDLER)
      {
         console::error("Uncached exception! crunch command line tool failed!");
      }
#else
      status = main_internal(argc, argv);
#endif
   }

   console::printf("\nExit status: %i", status);

   if (check_for_option(argc, argv, "pause"))
   {
      if ((status == EXIT_FAILURE) || (console::get_num_messages(cErrorConsoleMessage)))
         pause_and_wait();
   }

   return status;
}

