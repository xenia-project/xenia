// File: crn_file_utils.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   struct file_utils
   {
      // Returns true if pSrcFilename is older than pDstFilename
      static bool is_read_only(const char* pFilename);
      static bool disable_read_only(const char* pFilename);
      static bool is_older_than(const char *pSrcFilename, const char* pDstFilename);
      static bool does_file_exist(const char* pFilename);
      static bool does_dir_exist(const char* pDir);
      static bool get_file_size(const char* pFilename, uint64& file_size);
      static bool get_file_size(const char* pFilename, uint32& file_size);

      static bool is_path_separator(char c);
      static bool is_path_or_drive_separator(char c);
      static bool is_drive_separator(char c);

      static bool split_path(const char* p, dynamic_string* pDrive, dynamic_string* pDir, dynamic_string* pFilename, dynamic_string* pExt);
      static bool split_path(const char* p, dynamic_string& path, dynamic_string& filename);

      static bool get_pathname(const char* p, dynamic_string& path);
      static bool get_filename(const char* p, dynamic_string& filename);

      static void combine_path(dynamic_string& dst, const char* pA, const char* pB);
      static void combine_path(dynamic_string& dst, const char* pA, const char* pB, const char* pC);

      static bool full_path(dynamic_string& path);
      static bool get_extension(dynamic_string& filename);
      static bool remove_extension(dynamic_string& filename);
      static bool create_path(const dynamic_string& path);
      static void trim_trailing_seperator(dynamic_string& path);

      static int wildcmp(const char* pWild, const char* pString);

      static bool write_buf_to_file(const char* pPath, const void* pData, size_t data_size);

   }; // struct file_utils

} // namespace crnlib
