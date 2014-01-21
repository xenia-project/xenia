// File: crn_value.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_strutils.h"
#include "crn_dynamic_string.h"
#include "crn_vec.h"

namespace crnlib
{
   enum value_data_type
   {
      cDTInvalid,
      cDTString,
      cDTBool,
      cDTInt,
      cDTUInt,
      cDTFloat,
      cDTVec3F,
      cDTVec3I,

      cDTTotal
   };

   extern const char* gValueDataTypeStrings[cDTTotal + 1];

   class value
   {
   public:
      value() :
         m_type(cDTInvalid)
      {
      }

      value(const char* pStr) :
         m_pStr(crnlib_new<dynamic_string>(pStr)), m_type(cDTString)
      {
      }

       value(const dynamic_string& str) :
         m_pStr(crnlib_new<dynamic_string>(str)), m_type(cDTString)
      {
      }

      explicit value(bool v) :
         m_bool(v), m_type(cDTBool)
      {
      }

      value(int v) :
         m_int(v), m_type(cDTInt)
      {
      }

      value(uint v) :
         m_uint(v), m_type(cDTUInt)
      {
      }

      value(float v) :
         m_float(v), m_type(cDTFloat)
      {
      }

      value(const vec3F& v) :
         m_pVec3F(crnlib_new<vec3F>(v)), m_type(cDTVec3F)
      {
      }

      value(const vec3I& v) :
         m_pVec3I(crnlib_new<vec3I>(v)), m_type(cDTVec3I)
      {
      }

      ~value()
      {
         switch (m_type)
         {
            case cDTString:
               crnlib_delete(m_pStr);
               break;
            case cDTVec3F:
               crnlib_delete(m_pVec3F);
               break;
            case cDTVec3I:
               crnlib_delete(m_pVec3I);
               break;
            default:
               break;
         }
      }

      value(const value& other) :
         m_type(cDTInvalid)
      {
         *this = other;
      }

      value& operator= (const value& other)
      {
         if (this == &other)
            return *this;

         change_type(other.m_type);

         switch (other.m_type)
         {
            case cDTString:   m_pStr->set(*other.m_pStr); break;
            case cDTBool:     m_bool = other.m_bool; break;
            case cDTInt:      m_int = other.m_int; break;
            case cDTUInt:     m_uint = other.m_uint; break;
            case cDTFloat:    m_float = other.m_float; break;
            case cDTVec3F:    m_pVec3F->set(*other.m_pVec3F); break;
            case cDTVec3I:    m_pVec3I->set(*other.m_pVec3I); break;
            default: break;
         }
         return *this;
      }

      inline value_data_type get_data_type() const { return m_type; }

      void clear()
      {
         clear_dynamic();

         m_type = cDTInvalid;
      }

      void set_string(const char* pStr)
      {
         set_str(pStr);
      }

      void set_int(int v)
      {
         clear_dynamic();
         m_type = cDTInt;
         m_int = v;
      }

      void set_uint(uint v)
      {
         clear_dynamic();
         m_type = cDTUInt;
         m_uint = v;
      }

      void set_bool(bool v)
      {
         clear_dynamic();
         m_type = cDTBool;
         m_bool = v;
      }

      void set_float(float v)
      {
         clear_dynamic();
         m_type = cDTFloat;
         m_float = v;
      }

      void set_vec(const vec3F& v)
      {
         change_type(cDTVec3F);
         m_pVec3F->set(v);
      }

      void set_vec(const vec3I& v)
      {
         change_type(cDTVec3I);
         m_pVec3I->set(v);
      }

      bool parse(const char* p)
      {
         if ((!p) || (!p[0]))
         {
            clear();
            return false;
         }

         if (_stricmp(p, "false") == 0)
         {
            set_bool(false);
            return true;
         }
         else if (_stricmp(p, "true") == 0)
         {
            set_bool(true);
            return true;
         }

         if (p[0] == '\"')
         {
            dynamic_string str;
            str = p + 1;
            if (!str.is_empty())
            {
               if (str[str.get_len() - 1] == '\"')
               {
                  str.left(str.get_len() - 1);
                  set_str(str);

                  return true;
               }
            }
         }

         if (strchr(p, ',') != NULL)
         {
            float fx = 0, fy = 0, fz = 0;
#ifdef _MSC_VER
            if (sscanf_s(p, "%f,%f,%f", &fx, &fy, &fz) == 3)
#else
            if (sscanf(p, "%f,%f,%f", &fx, &fy, &fz) == 3)
#endif
            {
               bool as_float = true;
               int ix = 0, iy = 0, iz = 0;
#ifdef _MSC_VER
               if (sscanf_s(p, "%i,%i,%i", &ix, &iy, &iz) == 3)
#else
               if (sscanf(p, "%i,%i,%i", &ix, &iy, &iz) == 3)
#endif
               {
                  if ((ix == fx) && (iy == fy) && (iz == fz))
                     as_float = false;
               }

               if (as_float)
                  set_vec(vec3F(fx, fy, fz));
               else
                  set_vec(vec3I(ix, iy, iz));

               return true;
            }
         }

         const char* q = p;
         bool success = string_to_uint(q, m_uint);
         if ((success) && (*q == 0))
         {
            set_uint(m_uint);
            return true;
         }

         q = p;
         success = string_to_int(q, m_int);
         if ((success) && (*q == 0))
         {
            set_int(m_int);
            return true;
         }

         q = p;
         success = string_to_float(q, m_float);
         if ((success) && (*q == 0))
         {
            set_float(m_float);
            return true;
         }

         set_string(p);

         return true;
      }

      dynamic_string& get_as_string(dynamic_string& dst) const
      {
         switch (m_type)
         {
            case cDTInvalid:  dst.clear(); break;
            case cDTString:   dst = *m_pStr; break;
            case cDTBool:     dst = m_bool ? "TRUE" : "FALSE"; break;
            case cDTInt:      dst.format("%i", m_int); break;
            case cDTUInt:     dst.format("%u", m_uint); break;
            case cDTFloat:    dst.format("%f", m_float); break;
            case cDTVec3F:    dst.format("%f,%f,%f", (*m_pVec3F)[0], (*m_pVec3F)[1], (*m_pVec3F)[2]); break;
            case cDTVec3I:    dst.format("%i,%i,%i", (*m_pVec3I)[0], (*m_pVec3I)[1], (*m_pVec3I)[2]); break;
            default: break;
         }

         return dst;
      }

      bool get_as_int(int& val, uint component = 0) const
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               val = 0;
               return false;
            }
            case cDTString:
            {
               const char* p = m_pStr->get_ptr();
               return string_to_int(p, val);
            }
            case cDTBool:  val = m_bool; break;
            case cDTInt:   val = m_int; break;
            case cDTUInt:
            {
               if (m_uint > INT_MAX)
               {
                  val = 0;
                  return false;
               }
               val = m_uint;
               break;
            }
            case cDTFloat:
            {
               if ((m_float < INT_MIN) || (m_float > INT_MAX))
               {
                  val = 0;
                  return false;
               }
               val = (int)m_float;
               break;
            }
            case cDTVec3F:
            {
               if (component > 2)
               {
                  val = 0;
                  return false;
               }
               if (((*m_pVec3F)[component] < INT_MIN) || ((*m_pVec3F)[component] > INT_MAX))
               {
                  val = 0;
                  return false;
               }
               val = (int)(*m_pVec3F)[component];
               break;
            }
            case cDTVec3I:
            {
               if (component > 2)
               {
                  val = 0;
                  return false;
               }
               val = (int)(*m_pVec3I)[component];
               break;
            }
            default: break;
         }
         return true;
      }

      bool get_as_uint(uint& val, uint component = 0) const
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               val = 0;
               return false;
            }
            case cDTString:
            {
               const char* p = m_pStr->get_ptr();
               return string_to_uint(p, val);
            }
            case cDTBool:
            {
               val = m_bool;
               break;
            }
            case cDTInt:
            {
               if (m_int < 0)
               {
                  val = 0;
                  return false;
               }
               val = (uint)m_int;
               break;
            }
            case cDTUInt:
            {
               val = m_uint;
               break;
            }
            case cDTFloat:
            {
               if ((m_float < 0) || (m_float > UINT_MAX))
               {
                  val = 0;
                  return false;
               }
               val = (uint)m_float;
               break;
            }
            case cDTVec3F:
            {
               if (component > 2)
               {
                  val = 0;
                  return false;
               }
               if (((*m_pVec3F)[component] < 0) || ((*m_pVec3F)[component] > UINT_MAX))
               {
                  val = 0;
                  return false;
               }
               val = (uint)(*m_pVec3F)[component];
               break;
            }
            case cDTVec3I:
            {
               if (component > 2)
               {
                  val = 0;
                  return false;
               }
               if ((*m_pVec3I)[component] < 0)
               {
                  val = 0;
                  return false;
               }
               val = (uint)(*m_pVec3I)[component];
               break;
            }
            default: break;
         }
         return true;
      }

      bool get_as_bool(bool& val, uint component = 0) const
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               val = false;
               return false;
            }
            case cDTString:
            {
               const char* p = m_pStr->get_ptr();
               return string_to_bool(p, val);
            }
            case cDTBool:
            {
               val = m_bool;
               break;
            }
            case cDTInt:
            {
               val = (m_int != 0);
               break;
            }
            case cDTUInt:
            {
               val = (m_uint != 0);
               break;
            }
            case cDTFloat:
            {
               val = (m_float != 0);
               break;
            }
            case cDTVec3F:
            {
               if (component > 2)
               {
                  val = false;
                  return false;
               }
               val = ((*m_pVec3F)[component] != 0);
               break;
            }
            case cDTVec3I:
            {
               if (component > 2)
               {
                  val = false;
                  return false;
               }
               val = ((*m_pVec3I)[component] != 0);
               break;
            }
            default: break;
         }
         return true;
      }

      bool get_as_float(float& val, uint component = 0) const
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               val = 0;
               return false;
            }
            case cDTString:
            {
               const char* p = m_pStr->get_ptr();
               return string_to_float(p, val);
            }
            case cDTBool:
            {
               val = m_bool;
               break;
            }
            case cDTInt:
            {
               val = (float)m_int;
               break;
            }
            case cDTUInt:
            {
               val = (float)m_uint;
               break;
            }
            case cDTFloat:
            {
               val = m_float;
               break;
            }
            case cDTVec3F:
            {
               if (component > 2)
               {
                  val = 0;
                  return false;
               }
               val = (*m_pVec3F)[component];
               break;
            }
            case cDTVec3I:
            {
               if (component > 2)
               {
                  val = 0;
                  return false;
               }
               val = (float)(*m_pVec3I)[component];
               break;
            }
            default: break;
         }
         return true;
      }

      bool get_as_vec(vec3F& val) const
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               val.clear();
               return false;
            }
            case cDTString:
            {
               const char* p = m_pStr->get_ptr();
               float x = 0, y = 0, z = 0;
#ifdef _MSC_VER
               if (sscanf_s(p, "%f,%f,%f", &x, &y, &z) == 3)
#else
               if (sscanf(p, "%f,%f,%f", &x, &y, &z) == 3)
#endif
               {
                  val.set(x, y, z);
                  return true;
               }
               else
               {
                  val.clear();
                  return false;
               }
            }
            case cDTBool:
            {
               val.set(m_bool);
               break;
            }
            case cDTInt:
            {
               val.set(static_cast<float>(m_int));
               break;
            }
            case cDTUInt:
            {
               val.set(static_cast<float>(m_uint));
               break;
            }
            case cDTFloat:
            {
               val.set(m_float);
               break;
            }
            case cDTVec3F:
            {
               val = *m_pVec3F;
               break;
            }
            case cDTVec3I:
            {
               val.set((float)(*m_pVec3I)[0], (float)(*m_pVec3I)[1], (float)(*m_pVec3I)[2]);
               break;
            }
            default: break;
         }
         return true;
      }

      bool get_as_vec(vec3I& val) const
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               val.clear();
               return false;
            }
            case cDTString:
            {
               const char* p = m_pStr->get_ptr();
               float x = 0, y = 0, z = 0;
#ifdef _MSC_VER
               if (sscanf_s(p, "%f,%f,%f", &x, &y, &z) == 3)
#else
               if (sscanf(p, "%f,%f,%f", &x, &y, &z) == 3)
#endif
               {
                  if ((x < INT_MIN) || (x > INT_MAX) || (y < INT_MIN) || (y > INT_MAX) || (z < INT_MIN) || (z > INT_MAX))
                  {
                     val.clear();
                     return false;
                  }
                  val.set((int)x, (int)y, (int)z);
                  return true;
               }
               else
               {
                  val.clear();
                  return false;
               }

               break;
            }
            case cDTBool:
            {
               val.set(m_bool);
               break;
            }
            case cDTInt:
            {
               val.set(m_int);
               break;
            }
            case cDTUInt:
            {
               val.set(m_uint);
               break;
            }
            case cDTFloat:
            {
               val.set((int)m_float);
               break;
            }
            case cDTVec3F:
            {
               val.set((int)(*m_pVec3F)[0], (int)(*m_pVec3F)[1], (int)(*m_pVec3F)[2]);
               break;
            }
            case cDTVec3I:
            {
               val = *m_pVec3I;
               break;
            }
            default: break;
         }
         return true;
      }

      bool set_zero()
      {
         switch (m_type)
         {
            case cDTInvalid:
            {
               return false;
            }
            case cDTString:
            {
               m_pStr->empty();
               break;
            }
            case cDTBool:
            {
               m_bool = false;
               break;
            }
            case cDTInt:
            {
               m_int = 0;
               break;
            }
            case cDTUInt:
            {
               m_uint = 0;
               break;
            }
            case cDTFloat:
            {
               m_float = 0;
               break;
            }
            case cDTVec3F:
            {
               m_pVec3F->clear();
               break;
            }
            case cDTVec3I:
            {
               m_pVec3I->clear();
               break;
            }
            default: break;
         }
         return true;
      }

      bool is_vector() const
      {
         switch (m_type)
         {
            case cDTVec3F:
            case cDTVec3I:
               return true;
            default: break;
         }
         return false;
      }

      uint get_num_components() const
      {
         switch (m_type)
         {
            case cDTVec3F:
            case cDTVec3I:
               return 3;
            default: break;
         }
         return 1;
      }

      bool is_numeric() const
      {
         switch (m_type)
         {
            case cDTInt:
            case cDTUInt:
            case cDTFloat:
            case cDTVec3F:
            case cDTVec3I:
               return true;
            default: break;
         }
         return false;
      }

      bool is_float() const
      {
         switch (m_type)
         {
            case cDTFloat:
            case cDTVec3F:
               return true;
            default: break;
         }
         return false;
      }

      bool is_integer() const
      {
         switch (m_type)
         {
            case cDTInt:
            case cDTUInt:
            case cDTVec3I:
               return true;
            default: break;
         }
         return false;
      }

      bool is_signed() const
      {
         switch (m_type)
         {
            case cDTInt:
            case cDTFloat:
            case cDTVec3F:
            case cDTVec3I:
               return true;
            default: break;
         }
         return false;
      }

      bool is_string() const
      {
         return m_type == cDTString;
      }

      int serialize(void* pBuf, uint buf_size, bool little_endian) const
      {
         uint buf_left = buf_size;

         uint8 t = (uint8)m_type;
         if (!utils::write_obj(t, pBuf, buf_left, little_endian)) return -1;

         switch (m_type)
         {
            case cDTString:
            {
               int bytes_written = m_pStr->serialize(pBuf, buf_left, little_endian);
               if (bytes_written < 0) return -1;

               pBuf = static_cast<uint8*>(pBuf) + bytes_written;
               buf_left -= bytes_written;

               break;
            }
            case cDTBool:
            {
               if (!utils::write_obj(m_bool, pBuf, buf_left, little_endian)) return -1;
               break;
            }
            case cDTInt:
            case cDTUInt:
            case cDTFloat:
            {
               if (!utils::write_obj(m_float, pBuf, buf_left, little_endian)) return -1;
               break;
            }
            case cDTVec3F:
            {
               for (uint i = 0; i < 3; i++)
                  if (!utils::write_obj((*m_pVec3F)[i], pBuf, buf_left, little_endian)) return -1;
               break;
            }
            case cDTVec3I:
            {
               for (uint i = 0; i < 3; i++)
                  if (!utils::write_obj((*m_pVec3I)[i], pBuf, buf_left, little_endian)) return -1;
               break;
            }
            default: break;
         }

         return buf_size - buf_left;
      }

      int deserialize(const void* pBuf, uint buf_size, bool little_endian)
      {
         uint buf_left = buf_size;

         uint8 t;
         if (!utils::read_obj(t, pBuf, buf_left, little_endian)) return -1;

         if (t >= cDTTotal)
            return -1;

         m_type = static_cast<value_data_type>(t);

         switch (m_type)
         {
            case cDTString:
            {
               change_type(cDTString);

               int bytes_read = m_pStr->deserialize(pBuf, buf_left, little_endian);
               if (bytes_read < 0) return -1;

               pBuf = static_cast<const uint8*>(pBuf) + bytes_read;
               buf_left -= bytes_read;

               break;
            }
            case cDTBool:
            {
               if (!utils::read_obj(m_bool, pBuf, buf_left, little_endian)) return -1;
               break;
            }
            case cDTInt:
            case cDTUInt:
            case cDTFloat:
            {
               if (!utils::read_obj(m_float, pBuf, buf_left, little_endian)) return -1;
               break;
            }
            case cDTVec3F:
            {
               change_type(cDTVec3F);

               for (uint i = 0; i < 3; i++)
                  if (!utils::read_obj((*m_pVec3F)[i], pBuf, buf_left, little_endian)) return -1;
               break;
            }
            case cDTVec3I:
            {
               change_type(cDTVec3I);

               for (uint i = 0; i < 3; i++)
                  if (!utils::read_obj((*m_pVec3I)[i], pBuf, buf_left, little_endian)) return -1;
               break;
            }
            default: break;
         }

         return buf_size - buf_left;
      }

      void swap(value& other)
      {
         for (uint i = 0; i < cUnionSize; i++)
            std::swap(m_union[i], other.m_union[i]);

         std::swap(m_type, other.m_type);
      }

   private:
      void clear_dynamic()
      {
         if (m_type == cDTVec3F)
         {
            crnlib_delete(m_pVec3F);
            m_pVec3F = NULL;

            m_type = cDTInvalid;
         }
         else if (m_type == cDTVec3I)
         {
            crnlib_delete(m_pVec3I);
            m_pVec3I = NULL;

            m_type = cDTInvalid;
         }
         else if (m_type == cDTString)
         {
            crnlib_delete(m_pStr);
            m_pStr = NULL;

            m_type = cDTInvalid;
         }
      }

      void change_type(value_data_type type)
      {
         if (type != m_type)
         {
            clear_dynamic();

            m_type = type;

            switch (m_type)
            {
               case cDTString:
                  m_pStr = crnlib_new<dynamic_string>();
                  break;
               case cDTVec3F:
                  m_pVec3F = crnlib_new<vec3F>();
                  break;
               case cDTVec3I:
                  m_pVec3I = crnlib_new<vec3I>();
                  break;
               default: break;
            }
         }
      }

      void set_str(const dynamic_string& s)
      {
         if (m_type == cDTString)
            m_pStr->set(s);
         else
         {
            clear_dynamic();

            m_type = cDTString;
            m_pStr = crnlib_new<dynamic_string>(s);
         }
      }

      void set_str(const char* p)
      {
         if (m_type == cDTString)
            m_pStr->set(p);
         else
         {
            clear_dynamic();

            m_type = cDTString;
            m_pStr = crnlib_new<dynamic_string>(p);
         }
      }

      enum { cUnionSize = 1 };

      union
      {
         bool              m_bool;
         int               m_int;
         uint              m_uint;
         float             m_float;

         vec3F*            m_pVec3F;
         vec3I*            m_pVec3I;
         dynamic_string*   m_pStr;

         uint              m_union[cUnionSize];
      };

      value_data_type    m_type;
   };

} // namespace crnlib

