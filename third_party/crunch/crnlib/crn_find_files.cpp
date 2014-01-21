// File: crn_win32_find_files.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_find_files.h"
#include "crn_file_utils.h"
#include "crn_strutils.h"

#ifdef CRNLIB_USE_WIN32_API
#include "crn_winhdr.h"

#elif defined(__GNUC__)
#include <fnmatch.h>
#include <dirent.h>
#endif

namespace crnlib
{
#ifdef CRNLIB_USE_WIN32_API
   bool find_files::find(const char* pBasepath, const char* pFilespec, uint flags)
   {
      m_last_error = S_OK;
      m_files.resize(0);

      return find_internal(pBasepath, "", pFilespec, flags, 0);
   }

   bool find_files::find(const char* pSpec, uint flags)
   {
      dynamic_string find_name(pSpec);

      if (!file_utils::full_path(find_name))
         return false;

      dynamic_string find_pathname, find_filename;
      if (!file_utils::split_path(find_name.get_ptr(), find_pathname, find_filename))
         return false;

      return find(find_pathname.get_ptr(), find_filename.get_ptr(), flags);
   }

   bool find_files::find_internal(const char* pBasepath, const char* pRelpath, const char* pFilespec, uint flags, int level)
   {
      WIN32_FIND_DATAA find_data;

      dynamic_string filename;

      dynamic_string_array child_paths;
      if (flags & cFlagRecursive)
      {
         if (strlen(pRelpath))
            file_utils::combine_path(filename, pBasepath, pRelpath, "*");
         else
            file_utils::combine_path(filename, pBasepath, "*");

         HANDLE handle = FindFirstFileA(filename.get_ptr(), &find_data);
         if (handle == INVALID_HANDLE_VALUE)
         {
            HRESULT hres = GetLastError();
            if ((level == 0) && (hres != NO_ERROR) && (hres != ERROR_FILE_NOT_FOUND))
            {
               m_last_error = hres;
               return false;
            }
         }
         else
         {
            do
            {
               const bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

               bool skip = !is_dir;
               if (is_dir)
                  skip = (strcmp(find_data.cFileName, ".") == 0) || (strcmp(find_data.cFileName, "..") == 0);

               if (find_data.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY))
                  skip = true;

               if (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
               {
                  if ((flags & cFlagAllowHidden) == 0)
                     skip = true;
               }

               if (!skip)
               {
                  dynamic_string child_path(find_data.cFileName);
                  if ((!child_path.count_char('?')) && (!child_path.count_char('*')))
                     child_paths.push_back(child_path);
               }

            } while (FindNextFileA(handle, &find_data) != 0);

            HRESULT hres = GetLastError();

            FindClose(handle);
            handle = INVALID_HANDLE_VALUE;

            if (hres != ERROR_NO_MORE_FILES)
            {
               m_last_error = hres;
               return false;
            }
         }
      }

      if (strlen(pRelpath))
         file_utils::combine_path(filename, pBasepath, pRelpath, pFilespec);
      else
         file_utils::combine_path(filename, pBasepath, pFilespec);

      HANDLE handle = FindFirstFileA(filename.get_ptr(), &find_data);
      if (handle == INVALID_HANDLE_VALUE)
      {
         HRESULT hres = GetLastError();
         if ((level == 0) && (hres != NO_ERROR) && (hres != ERROR_FILE_NOT_FOUND))
         {
            m_last_error = hres;
            return false;
         }
      }
      else
      {
         do
         {
            const bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            bool skip = false;
            if (is_dir)
               skip = (strcmp(find_data.cFileName, ".") == 0) || (strcmp(find_data.cFileName, "..") == 0);

            if (find_data.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY))
               skip = true;

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            {
               if ((flags & cFlagAllowHidden) == 0)
                  skip = true;
            }

            if (!skip)
            {
               if (((is_dir) && (flags & cFlagAllowDirs)) || ((!is_dir) && (flags & cFlagAllowFiles)))
               {
                  m_files.resize(m_files.size() + 1);
                  file_desc& file = m_files.back();
                  file.m_is_dir = is_dir;
                  file.m_base = pBasepath;
                  file.m_name = find_data.cFileName;
                  file.m_rel = pRelpath;
                  if (strlen(pRelpath))
                     file_utils::combine_path(file.m_fullname, pBasepath, pRelpath, find_data.cFileName);
                  else
                     file_utils::combine_path(file.m_fullname, pBasepath, find_data.cFileName);
               }
            }

         } while (FindNextFileA(handle, &find_data) != 0);

         HRESULT hres = GetLastError();

         FindClose(handle);

         if (hres != ERROR_NO_MORE_FILES)
         {
            m_last_error = hres;
            return false;
         }
      }

      for (uint i = 0; i < child_paths.size(); i++)
      {
         dynamic_string child_path;
         if (strlen(pRelpath))
            file_utils::combine_path(child_path, pRelpath, child_paths[i].get_ptr());
         else
            child_path = child_paths[i];

         if (!find_internal(pBasepath, child_path.get_ptr(), pFilespec, flags, level + 1))
            return false;
      }

      return true;
   }
#elif defined(__GNUC__)
   bool find_files::find(const char* pBasepath, const char* pFilespec, uint flags)
   {
      m_files.resize(0);
      return find_internal(pBasepath, "", pFilespec, flags, 0);
   }

   bool find_files::find(const char* pSpec, uint flags)
   {
      dynamic_string find_name(pSpec);

      if (!file_utils::full_path(find_name))
         return false;

      dynamic_string find_pathname, find_filename;
      if (!file_utils::split_path(find_name.get_ptr(), find_pathname, find_filename))
         return false;

      return find(find_pathname.get_ptr(), find_filename.get_ptr(), flags);
   }

   bool find_files::find_internal(const char* pBasepath, const char* pRelpath, const char* pFilespec, uint flags, int level)
   {
      dynamic_string pathname;
      if (strlen(pRelpath))
         file_utils::combine_path(pathname, pBasepath, pRelpath);
      else
         pathname = pBasepath;

      if (!pathname.is_empty())
      {
         char c = pathname.back();
         if (c != '/')
            pathname += "/";
      }

      DIR *dp = opendir(pathname.get_ptr());

      if (!dp)
         return level ? true : false;

      dynamic_string_array paths;

      for ( ; ; )
      {
         struct dirent *ep = readdir(dp);
         if (!ep)
            break;
         if ((strcmp(ep->d_name, ".") == 0) || (strcmp(ep->d_name, "..") == 0))
            continue;

         const bool is_directory = (ep->d_type & DT_DIR) != 0;
         const bool is_file =  (ep->d_type & DT_REG) != 0;

         dynamic_string filename(ep->d_name);

         if (is_directory)
         {
            if (flags & cFlagRecursive)
            {
               paths.push_back(filename);
            }
         }

         if (((is_file) && (flags & cFlagAllowFiles)) || ((is_directory) && (flags & cFlagAllowDirs)))
         {
            if (0 == fnmatch(pFilespec, filename.get_ptr(), 0))
            {
               m_files.resize(m_files.size() + 1);
               file_desc& file = m_files.back();
               file.m_is_dir = is_directory;
               file.m_base = pBasepath;
               file.m_rel = pRelpath;
               file.m_name = filename;
               file.m_fullname = pathname + filename;
            }
         }
      }

      closedir(dp);
      dp = NULL;

      if (flags & cFlagRecursive)
      {
         for (uint i = 0; i < paths.size(); i++)
         {
            dynamic_string childpath;
            if (strlen(pRelpath))
               file_utils::combine_path(childpath, pRelpath, paths[i].get_ptr());
            else
               childpath = paths[i];

            if (!find_internal(pBasepath, childpath.get_ptr(), pFilespec, flags, level + 1))
               return false;
         }
      }

      return true;
   }
#else
   #error Unimplemented
#endif

} // namespace crnlib
