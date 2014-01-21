// File: crn_command_line_params.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_command_line_params.h"
#include "crn_console.h"
#include "crn_cfile_stream.h"

#ifdef WIN32
   #define CRNLIB_CMD_LINE_ALLOW_SLASH_PARAMS 1
#endif

#if CRNLIB_USE_WIN32_API
#include "crn_winhdr.h"
#endif
namespace crnlib
{
   void get_command_line_as_single_string(dynamic_string& cmd_line, int argc, char *argv[])
   {
      argc, argv;
#if CRNLIB_USE_WIN32_API
      cmd_line.set(GetCommandLineA());
#else
      cmd_line.clear();
      for (int i = 0; i < argc; i++)
      {
         dynamic_string tmp(argv[i]);
         if ((tmp.front() != '"') && (tmp.front() != '-') && (tmp.front() != '@'))
            tmp = "\"" + tmp + "\"";
         if (cmd_line.get_len())
            cmd_line += " ";
         cmd_line += tmp;
      }
#endif
   }

   command_line_params::command_line_params()
   {
   }

   void command_line_params::clear()
   {
      m_params.clear();

      m_param_map.clear();
   }

   bool command_line_params::split_params(const char* p, dynamic_string_array& params)
   {
      bool within_param = false;
      bool within_quote = false;

      uint ofs = 0;
      dynamic_string str;

      while (p[ofs])
      {
         const char c = p[ofs];

         if (within_param)
         {
            if (within_quote)
            {
               if (c == '"')
                  within_quote = false;

               str.append_char(c);
            }
            else if ((c == ' ') || (c == '\t'))
            {
               if (!str.is_empty())
               {
                  params.push_back(str);
                  str.clear();
               }
               within_param = false;
            }
            else
            {
               if (c == '"')
                  within_quote = true;

               str.append_char(c);
            }
         }
         else if ((c != ' ') && (c != '\t'))
         {
            within_param = true;

            if (c == '"')
               within_quote = true;

            str.append_char(c);
         }

         ofs++;
      }

      if (within_quote)
      {
         console::error("Unmatched quote in command line \"%s\"", p);
         return false;
      }

      if (!str.is_empty())
         params.push_back(str);

      return true;
   }

   bool command_line_params::load_string_file(const char* pFilename, dynamic_string_array& strings)
   {
      cfile_stream in_stream;
      if (!in_stream.open(pFilename, cDataStreamReadable | cDataStreamSeekable))
      {
         console::error("Unable to open file \"%s\" for reading!", pFilename);
         return false;
      }

      dynamic_string ansi_str;

      for ( ; ; )
      {
         if (!in_stream.read_line(ansi_str))
            break;

         ansi_str.trim();
         if (ansi_str.is_empty())
            continue;

         strings.push_back(dynamic_string(ansi_str.get_ptr()));
      }

      return true;
   }

   bool command_line_params::parse(const dynamic_string_array& params, uint n, const param_desc* pParam_desc)
   {
      CRNLIB_ASSERT(n && pParam_desc);

      m_params = params;

      uint arg_index = 0;
      while (arg_index < params.size())
      {
         const uint cur_arg_index = arg_index;
         const dynamic_string& src_param = params[arg_index++];

         if (src_param.is_empty())
            continue;
#if CRNLIB_CMD_LINE_ALLOW_SLASH_PARAMS
         if ((src_param[0] == '/') || (src_param[0] == '-'))
#else
         if (src_param[0] == '-')
#endif
         {
            if (src_param.get_len() < 2)
            {
               console::error("Invalid command line parameter: \"%s\"", src_param.get_ptr());
               return false;
            }

            dynamic_string key_str(src_param);

            key_str.right(1);

            int modifier = 0;
            char c = key_str[key_str.get_len() - 1];
            if (c == '+')
               modifier = 1;
            else if (c == '-')
               modifier = -1;

            if (modifier)
               key_str.left(key_str.get_len() - 1);

            uint param_index;
            for (param_index = 0; param_index < n; param_index++)
               if (key_str == pParam_desc[param_index].m_pName)
                  break;

            if (param_index == n)
            {
               console::error("Unrecognized command line parameter: \"%s\"", src_param.get_ptr());
               return false;
            }

            const param_desc& desc = pParam_desc[param_index];

            const uint cMaxValues = 16;
            dynamic_string val_str[cMaxValues];
            uint num_val_strs = 0;
            if (desc.m_num_values)
            {
               CRNLIB_ASSERT(desc.m_num_values <= cMaxValues);

               if ((arg_index + desc.m_num_values) > params.size())
               {
                  console::error("Expected %u value(s) after command line parameter: \"%s\"", desc.m_num_values, src_param.get_ptr());
                  return false;
               }

               for (uint v = 0; v < desc.m_num_values; v++)
                  val_str[num_val_strs++] = params[arg_index++];
            }

            dynamic_string_array strings;

            if ((desc.m_support_listing_file) && (val_str[0].get_len() >= 2) && (val_str[0][0] == '@'))
            {
               dynamic_string filename(val_str[0]);
               filename.right(1);
               filename.unquote();

               if (!load_string_file(filename.get_ptr(), strings))
               {
                  console::error("Failed loading listing file \"%s\"!", filename.get_ptr());
                  return false;
               }
            }
            else
            {
               for (uint v = 0; v < num_val_strs; v++)
               {
                  val_str[v].unquote();
                  strings.push_back(val_str[v]);
               }
            }

            param_value pv;
            pv.m_values.swap(strings);
            pv.m_index = cur_arg_index;
            pv.m_modifier = (int8)modifier;
            m_param_map.insert(std::make_pair(key_str, pv));
         }
         else
         {
            param_value pv;
            pv.m_values.push_back(src_param);
            pv.m_values.back().unquote();
            pv.m_index = cur_arg_index;
            m_param_map.insert(std::make_pair(g_empty_dynamic_string, pv));
         }
      }

      return true;
   }

   bool command_line_params::parse(const char* pCmd_line, uint n, const param_desc* pParam_desc, bool skip_first_param)
   {
      CRNLIB_ASSERT(n && pParam_desc);

      dynamic_string_array p;
      if (!split_params(pCmd_line, p))
         return 0;

      if (p.empty())
         return 0;

      if (skip_first_param)
         p.erase(0U);

      return parse(p, n, pParam_desc);
   }

   bool command_line_params::is_param(uint index) const
   {
      CRNLIB_ASSERT(index < m_params.size());
      if (index >= m_params.size())
         return false;

      const dynamic_string& w = m_params[index];
      if (w.is_empty())
         return false;

#if CRNLIB_CMD_LINE_ALLOW_SLASH_PARAMS
      return (w.get_len() >= 2) && ((w[0] == '-') || (w[0] == '/'));
#else
      return (w.get_len() >= 2) && (w[0] == '-');
#endif
   }

   uint command_line_params::find(uint num_keys, const char** ppKeys, crnlib::vector<param_map_const_iterator>* pIterators, crnlib::vector<uint>* pUnmatched_indices) const
   {
      CRNLIB_ASSERT(ppKeys);

      if (pUnmatched_indices)
      {
         pUnmatched_indices->resize(m_params.size());
         for (uint i = 0; i < m_params.size(); i++)
            (*pUnmatched_indices)[i] = i;
      }

      uint n = 0;
      for (uint i = 0; i < num_keys; i++)
      {
         const char* pKey = ppKeys[i];

         param_map_const_iterator begin, end;
         find(pKey, begin, end);

         while (begin != end)
         {
            if (pIterators)
               pIterators->push_back(begin);

            if (pUnmatched_indices)
            {
               int k = pUnmatched_indices->find(begin->second.m_index);
               if (k >= 0)
                  pUnmatched_indices->erase_unordered(k);
            }

            n++;
            begin++;
         }
      }

      return n;
   }

   void command_line_params::find(const char* pKey, param_map_const_iterator& begin, param_map_const_iterator& end) const
   {
      dynamic_string key(pKey);
      begin = m_param_map.lower_bound(key);
      end = m_param_map.upper_bound(key);
   }

   uint command_line_params::get_count(const char* pKey) const
   {
      param_map_const_iterator begin, end;
      find(pKey, begin, end);

      uint n = 0;

      while (begin != end)
      {
         n++;
         begin++;
      }

      return n;
   }

   command_line_params::param_map_const_iterator command_line_params::get_param(const char* pKey, uint index) const
   {
      param_map_const_iterator begin, end;
      find(pKey, begin, end);

      if (begin == end)
         return m_param_map.end();

      uint n = 0;

      while ((begin != end) && (n != index))
      {
         n++;
         begin++;
      }

      if (begin == end)
         return m_param_map.end();

      return begin;
   }

   bool command_line_params::has_value(const char* pKey, uint index) const
   {
      return get_num_values(pKey, index) != 0;
   }

   uint command_line_params::get_num_values(const char* pKey, uint index) const
   {
      param_map_const_iterator it = get_param(pKey, index);

      if (it == end())
         return 0;

      return it->second.m_values.size();
   }

   bool command_line_params::get_value_as_bool(const char* pKey, uint index, bool def) const
   {
      param_map_const_iterator it = get_param(pKey, index);
      if (it == end())
         return def;

      if (it->second.m_modifier)
         return it->second.m_modifier > 0;
      else
         return true;
   }

   int command_line_params::get_value_as_int(const char* pKey, uint index, int def, int l, int h, uint value_index) const
   {
      param_map_const_iterator it = get_param(pKey, index);
      if ((it == end()) || (value_index >= it->second.m_values.size()))
         return def;

      int val;
      const char* p = it->second.m_values[value_index].get_ptr();
      if (!string_to_int(p, val))
      {
         crnlib::console::warning("Invalid value specified for parameter \"%s\", using default value of %i", pKey, def);
         return def;
      }

      if (val < l)
      {
         crnlib::console::warning("Value %i for parameter \"%s\" is out of range, clamping to %i", val, pKey, l);
         val = l;
      }
      else if (val > h)
      {
         crnlib::console::warning("Value %i for parameter \"%s\" is out of range, clamping to %i", val, pKey, h);
         val = h;
      }

      return val;
   }

   float command_line_params::get_value_as_float(const char* pKey, uint index, float def, float l, float h, uint value_index) const
   {
      param_map_const_iterator it = get_param(pKey, index);
      if ((it == end()) || (value_index >= it->second.m_values.size()))
         return def;

      float val;
      const char* p = it->second.m_values[value_index].get_ptr();
      if (!string_to_float(p, val))
      {
         crnlib::console::warning("Invalid value specified for float parameter \"%s\", using default value of %f", pKey, def);
         return def;
      }

      if (val < l)
      {
         crnlib::console::warning("Value %f for parameter \"%s\" is out of range, clamping to %f", val, pKey, l);
         val = l;
      }
      else if (val > h)
      {
         crnlib::console::warning("Value %f for parameter \"%s\" is out of range, clamping to %f", val, pKey, h);
         val = h;
      }

      return val;
   }

   bool command_line_params::get_value_as_string(const char* pKey, uint index, dynamic_string& value, uint value_index) const
   {
      param_map_const_iterator it = get_param(pKey, index);
      if ((it == end()) || (value_index >= it->second.m_values.size()))
      {
         value.empty();
         return false;
      }

      value = it->second.m_values[value_index];
      return true;
   }

   const dynamic_string& command_line_params::get_value_as_string_or_empty(const char* pKey, uint index, uint value_index) const
   {
      param_map_const_iterator it = get_param(pKey, index);
      if ((it == end()) || (value_index >= it->second.m_values.size()))
      return g_empty_dynamic_string;

      return it->second.m_values[value_index];
   }

} // namespace crnlib

