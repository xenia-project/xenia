// File: crn_file_utils.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_file_utils.h"
#include "crn_strutils.h"

#if CRNLIB_USE_WIN32_API
#include "crn_winhdr.h"
#endif

#ifdef WIN32
#include <direct.h>
#endif

#ifdef __GNUC__
#include <sys/stat.h>
#include <sys/stat.h>
#include <libgen.h>
#endif

namespace crnlib
{
#if CRNLIB_USE_WIN32_API
   bool file_utils::is_read_only(const char* pFilename)
   {
      uint32 dst_file_attribs = GetFileAttributesA(pFilename);
      if (dst_file_attribs == INVALID_FILE_ATTRIBUTES)
         return false;
      if (dst_file_attribs & FILE_ATTRIBUTE_READONLY)
         return true;
      return false;
   }

   bool file_utils::disable_read_only(const char* pFilename)
   {
      uint32 dst_file_attribs = GetFileAttributesA(pFilename);
      if (dst_file_attribs == INVALID_FILE_ATTRIBUTES)
         return false;
      if (dst_file_attribs & FILE_ATTRIBUTE_READONLY)
      {
         dst_file_attribs &= ~FILE_ATTRIBUTE_READONLY;
         if (SetFileAttributesA(pFilename, dst_file_attribs))
            return true;
      }
      return false;
   }

   bool file_utils::is_older_than(const char* pSrcFilename, const char* pDstFilename)
   {
      WIN32_FILE_ATTRIBUTE_DATA src_file_attribs;
      const BOOL src_file_exists = GetFileAttributesExA(pSrcFilename, GetFileExInfoStandard, &src_file_attribs);

      WIN32_FILE_ATTRIBUTE_DATA dst_file_attribs;
      const BOOL dest_file_exists = GetFileAttributesExA(pDstFilename, GetFileExInfoStandard, &dst_file_attribs);

      if ((dest_file_exists) && (src_file_exists))
      {
         LONG timeComp = CompareFileTime(&src_file_attribs.ftLastWriteTime, &dst_file_attribs.ftLastWriteTime);
         if (timeComp < 0)
            return true;
      }
      return false;
   }

   bool file_utils::does_file_exist(const char* pFilename)
   {
      const DWORD fullAttributes = GetFileAttributesA(pFilename);

      if (fullAttributes == INVALID_FILE_ATTRIBUTES)
         return false;

      if (fullAttributes & FILE_ATTRIBUTE_DIRECTORY)
         return false;

      return true;
   }

   bool file_utils::does_dir_exist(const char* pDir)
   {
      //-- Get the file attributes.
      DWORD fullAttributes = GetFileAttributesA(pDir);

      if (fullAttributes == INVALID_FILE_ATTRIBUTES)
         return false;

      if (fullAttributes & FILE_ATTRIBUTE_DIRECTORY)
         return true;

      return false;
   }

   bool file_utils::get_file_size(const char* pFilename, uint64& file_size)
   {
      file_size = 0;

      WIN32_FILE_ATTRIBUTE_DATA attr;

      if (0 == GetFileAttributesExA(pFilename, GetFileExInfoStandard, &attr))
         return false;

      if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
         return false;

      file_size = static_cast<uint64>(attr.nFileSizeLow) | (static_cast<uint64>(attr.nFileSizeHigh) << 32U);

      return true;
   }
#elif defined( __GNUC__ )
   bool file_utils::is_read_only(const char* pFilename)
   {
      pFilename;
      // TODO
      return false;
   }

   bool file_utils::disable_read_only(const char* pFilename)
   {
      pFilename;
      // TODO
      return false;
   }

   bool file_utils::is_older_than(const char *pSrcFilename, const char* pDstFilename)
   {
      pSrcFilename, pDstFilename;
      // TODO
      return false;
   }

   bool file_utils::does_file_exist(const char* pFilename)
   {
      struct stat stat_buf;
      int result = stat(pFilename, &stat_buf);
      if (result)
         return false;
      if (S_ISREG(stat_buf.st_mode))
         return true;
      return false;
   }

   bool file_utils::does_dir_exist(const char* pDir)
   {
      struct stat stat_buf;
      int result = stat(pDir, &stat_buf);
      if (result)
         return false;
      if (S_ISDIR(stat_buf.st_mode) || S_ISLNK(stat_buf.st_mode))
         return true;
      return false;
   }

   bool file_utils::get_file_size(const char* pFilename, uint64& file_size)
   {
      file_size = 0;
      struct stat stat_buf;
      int result = stat(pFilename, &stat_buf);
      if (result)
         return false;
      if (!S_ISREG(stat_buf.st_mode))
         return false;
      file_size = stat_buf.st_size;
      return true;
   }
#else
   bool file_utils::is_read_only(const char* pFilename)
   {
      return false;
   }

   bool file_utils::disable_read_only(const char* pFilename)
   {
      pFilename;
      // TODO
      return false;
   }

   bool file_utils::is_older_than(const char *pSrcFilename, const char* pDstFilename)
   {
      return false;
   }

   bool file_utils::does_file_exist(const char* pFilename)
   {
      FILE* pFile;
      crn_fopen(&pFile, pFilename, "rb");
      if (!pFile)
         return false;
      fclose(pFile);
      return true;
   }

   bool file_utils::does_dir_exist(const char* pDir)
   {
      return false;
   }

   bool file_utils::get_file_size(const char* pFilename, uint64& file_size)
   {
      FILE* pFile;
      crn_fopen(&pFile, pFilename, "rb");
      if (!pFile)
         return false;
      crn_fseek(pFile, 0, SEEK_END);
      file_size = crn_ftell(pFile);
      fclose(pFile);
      return true;
   }
#endif

   bool file_utils::get_file_size(const char* pFilename, uint32& file_size)
   {
      uint64 file_size64;
      if (!get_file_size(pFilename, file_size64))
      {
         file_size = 0;
         return false;
      }

      if (file_size64 > cUINT32_MAX)
         file_size64 = cUINT32_MAX;

      file_size = static_cast<uint32>(file_size64);
      return true;
   }

   bool file_utils::is_path_separator(char c)
   {
#ifdef WIN32
      return (c == '/') || (c == '\\');
#else
      return (c == '/');
#endif
   }

   bool file_utils::is_path_or_drive_separator(char c)
   {
#ifdef WIN32
      return (c == '/') || (c == '\\') || (c == ':');
#else
      return (c == '/');
#endif
   }

   bool file_utils::is_drive_separator(char c)
   {
#ifdef WIN32
      return (c == ':');
#else
      c;
      return false;
#endif
   }

   bool file_utils::split_path(const char* p, dynamic_string* pDrive, dynamic_string* pDir, dynamic_string* pFilename, dynamic_string* pExt)
   {
      CRNLIB_ASSERT(p);

#ifdef WIN32
      char drive_buf[_MAX_DRIVE];
      char dir_buf[_MAX_DIR];
      char fname_buf[_MAX_FNAME];
      char ext_buf[_MAX_EXT];

#ifdef _MSC_VER
      // Compiling with MSVC
      errno_t error = _splitpath_s(p,
         pDrive      ? drive_buf : NULL, pDrive    ? _MAX_DRIVE  : 0,
         pDir        ? dir_buf   : NULL, pDir      ? _MAX_DIR    : 0,
         pFilename   ? fname_buf : NULL, pFilename ? _MAX_FNAME  : 0,
         pExt        ? ext_buf   : NULL, pExt      ? _MAX_EXT    : 0);
      if (error != 0)
         return false;
#else
      // Compiling with MinGW
      _splitpath(p,
         pDrive      ? drive_buf : NULL,
         pDir        ? dir_buf   : NULL,
         pFilename   ? fname_buf : NULL,
         pExt        ? ext_buf   : NULL);
#endif

      if (pDrive)    *pDrive = drive_buf;
      if (pDir)      *pDir = dir_buf;
      if (pFilename) *pFilename = fname_buf;
      if (pExt)      *pExt = ext_buf;
#else
      char dirtmp[1024];
      char nametmp[1024];
      strcpy_safe(dirtmp, sizeof(dirtmp), p);
      strcpy_safe(nametmp, sizeof(nametmp), p);

      if (pDrive) pDrive->clear();

      const char *pDirName = dirname(dirtmp);
      if (!pDirName)
         return false;

      if (pDir)
      {
         pDir->set(pDirName);
         if ((!pDir->is_empty()) && (pDir->back() != '/'))
            pDir->append_char('/');
      }

      const char *pBaseName = basename(nametmp);
      if (!pBaseName)
         return false;

      if (pFilename)
      {
         pFilename->set(pBaseName);
         remove_extension(*pFilename);
      }

      if (pExt)
      {
         pExt->set(pBaseName);
         get_extension(*pExt);
         *pExt = "." + *pExt;
      }
#endif // #ifdef WIN32

      return true;
   }

   bool file_utils::split_path(const char* p, dynamic_string& path, dynamic_string& filename)
   {
      dynamic_string temp_drive, temp_path, temp_ext;
      if (!split_path(p, &temp_drive, &temp_path, &filename, &temp_ext))
         return false;

      filename += temp_ext;

      combine_path(path, temp_drive.get_ptr(), temp_path.get_ptr());
      return true;
   }

   bool file_utils::get_pathname(const char* p, dynamic_string& path)
   {
      dynamic_string temp_drive, temp_path;
      if (!split_path(p, &temp_drive, &temp_path, NULL, NULL))
         return false;

      combine_path(path, temp_drive.get_ptr(), temp_path.get_ptr());
      return true;
   }

   bool file_utils::get_filename(const char* p, dynamic_string& filename)
   {
      dynamic_string temp_ext;
      if (!split_path(p, NULL, NULL, &filename, &temp_ext))
         return false;

      filename += temp_ext;
      return true;
   }

   void file_utils::combine_path(dynamic_string& dst, const char* pA, const char* pB)
   {
      dynamic_string temp(pA);
      if ((!temp.is_empty()) && (!is_path_separator(pB[0])))
      {
         char c = temp[temp.get_len() - 1];
         if (!is_path_separator(c))
            temp.append_char(CRNLIB_PATH_SEPERATOR_CHAR);
      }
      temp += pB;
      dst.swap(temp);
   }

   void file_utils::combine_path(dynamic_string& dst, const char* pA, const char* pB, const char* pC)
   {
      combine_path(dst, pA, pB);
      combine_path(dst, dst.get_ptr(), pC);
   }

   bool file_utils::full_path(dynamic_string& path)
   {
#ifdef WIN32
      char buf[1024];
      char* p = _fullpath(buf, path.get_ptr(), sizeof(buf));
      if (!p)
         return false;
#else
      char buf[PATH_MAX];
      char* p;
      dynamic_string pn, fn;
      split_path(path.get_ptr(), pn, fn);
      if ((fn == ".") || (fn == ".."))
      {
         p = realpath(path.get_ptr(), buf);
         if (!p)
            return false;
         path.set(buf);
      }
      else
      {
         if (pn.is_empty())
            pn = "./";
         p = realpath(pn.get_ptr(), buf);
         if (!p)
            return false;
         combine_path(path, buf, fn.get_ptr());
      }
#endif

      return true;
   }

   bool file_utils::get_extension(dynamic_string& filename)
   {
      int sep = -1;
#ifdef WIN32
      sep = filename.find_right('\\');
#endif
      if (sep < 0)
         sep = filename.find_right('/');

      int dot = filename.find_right('.');
      if (dot < sep)
      {
         filename.clear();
         return false;
      }

      filename.right(dot + 1);

      return true;
   }

   bool file_utils::remove_extension(dynamic_string& filename)
   {
      int sep = -1;
#ifdef WIN32
      sep = filename.find_right('\\');
#endif
      if (sep < 0)
         sep = filename.find_right('/');

      int dot = filename.find_right('.');
      if (dot < sep)
         return false;

      filename.left(dot);

      return true;
   }

   bool file_utils::create_path(const dynamic_string& fullpath)
   {
      bool got_unc = false; got_unc;
      dynamic_string cur_path;

      const int l = fullpath.get_len();

      int n = 0;
      while (n < l)
      {
         const char c = fullpath.get_ptr()[n];

         const bool sep = is_path_separator(c);
         const bool back_sep = is_path_separator(cur_path.back());
         const bool is_last_char = (n == (l - 1));

         if ( ((sep) && (!back_sep)) || (is_last_char) )
         {
            if ((is_last_char) && (!sep))
               cur_path.append_char(c);

            bool valid = !cur_path.is_empty();

#ifdef WIN32
            // reject obvious stuff (drives, beginning of UNC paths):
            // c:\b\cool
            // \\machine\blah
            // \cool\blah
            if ((cur_path.get_len() == 2) && (cur_path[1] == ':'))
               valid = false;
            else if ((cur_path.get_len() >= 2) && (cur_path[0] == '\\') && (cur_path[1] == '\\'))
            {
               if (!got_unc)
                  valid = false;
               got_unc = true;
            }
            else if (cur_path == "\\")
               valid = false;
#endif
            if (cur_path == "/")
               valid = false;

            if ((valid) && (cur_path.get_len()))
            {
#ifdef WIN32
               _mkdir(cur_path.get_ptr());
#else
               mkdir(cur_path.get_ptr(), S_IRWXU | S_IRWXG | S_IRWXO );
#endif
            }
         }

         cur_path.append_char(c);

         n++;
      }

      return true;
   }

   void file_utils::trim_trailing_seperator(dynamic_string& path)
   {
      if ((path.get_len()) && (is_path_separator(path.back())))
         path.truncate(path.get_len() - 1);
   }

   // See http://www.codeproject.com/KB/string/wildcmp.aspx
   int file_utils::wildcmp(const char* pWild, const char* pString)
   {
      const char* cp = NULL, *mp = NULL;

      while ((*pString) && (*pWild != '*'))
      {
         if ((*pWild != *pString) && (*pWild != '?'))
            return 0;
         pWild++;
         pString++;
      }

      // Either *pString=='\0' or *pWild='*' here.

      while (*pString)
      {
         if (*pWild == '*')
         {
            if (!*++pWild)
               return 1;
            mp = pWild;
            cp = pString+1;
         }
         else if ((*pWild == *pString) || (*pWild == '?'))
         {
            pWild++;
            pString++;
         }
         else
         {
            pWild = mp;
            pString = cp++;
         }
      }

      while (*pWild == '*')
         pWild++;

      return !*pWild;
   }

   bool file_utils::write_buf_to_file(const char* pPath, const void* pData, size_t data_size)
   {
      FILE *pFile = NULL;

#ifdef _MSC_VER
      // Compiling with MSVC
      if (fopen_s(&pFile, pPath, "wb"))
         return false;
#else
      pFile = fopen(pPath, "wb");
#endif
      if (!pFile)
         return false;

      bool success = fwrite(pData, 1, data_size, pFile) == data_size;

      fclose(pFile);

      return success;
   }

} // namespace crnlib
