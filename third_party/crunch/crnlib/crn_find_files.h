// File: crn_win32_find_files.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   class find_files
   {
   public:
      struct file_desc
      {
         inline file_desc() : m_is_dir(false) { }

         dynamic_string  m_fullname;
         dynamic_string  m_base;
         dynamic_string  m_rel;
         dynamic_string  m_name;
         bool            m_is_dir;

         inline bool operator== (const file_desc& other) const { return m_fullname == other.m_fullname; }
         inline bool operator< (const file_desc& other) const { return m_fullname < other.m_fullname; }

         inline operator size_t() const { return static_cast<size_t>(m_fullname); }
      };

      typedef crnlib::vector<file_desc> file_desc_vec;

      inline find_files()
      {
         m_last_error = 0; // S_OK;
      }

      enum flags
      {
         cFlagRecursive = 1,
         cFlagAllowDirs = 2,
         cFlagAllowFiles = 4,
         cFlagAllowHidden = 8
      };

      bool find(const char* pBasepath, const char* pFilespec, uint flags = cFlagAllowFiles);

      bool find(const char* pSpec, uint flags = cFlagAllowFiles);

      // An HRESULT under Win32. FIXME: Abstract this better?
      inline int64 get_last_error() const { return m_last_error; }

      const file_desc_vec& get_files() const { return m_files; }

   private:
      file_desc_vec m_files;

      // A HRESULT under Win32
      int64 m_last_error;

      bool find_internal(const char* pBasepath, const char* pRelpath, const char* pFilespec, uint flags, int level);

   }; // class find_files

} // namespace crnlib
