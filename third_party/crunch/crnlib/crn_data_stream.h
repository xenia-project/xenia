// File: crn_data_stream.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   enum data_stream_attribs
   {
      cDataStreamReadable = 1,
      cDataStreamWritable = 2,
      cDataStreamSeekable = 4
   };

   const int64 DATA_STREAM_SIZE_UNKNOWN = cINT64_MAX;
   const int64 DATA_STREAM_SIZE_INFINITE = cUINT64_MAX;

   class data_stream
   {
      data_stream(const data_stream&);
      data_stream& operator= (const data_stream&);

   public:
      data_stream();
      data_stream(const char* pName, uint attribs);

      virtual ~data_stream() { }

      virtual data_stream *get_parent() { return NULL; }

      virtual bool close() { m_opened = false; m_error = false; m_got_cr = false; return true; }

      typedef uint16 attribs_t;
      inline attribs_t get_attribs() const { return m_attribs; }

      inline bool is_opened() const { return m_opened; }

      inline bool is_readable() const { return utils::is_bit_set(m_attribs, cDataStreamReadable); }
      inline bool is_writable() const { return utils::is_bit_set(m_attribs, cDataStreamWritable); }
      inline bool is_seekable() const { return utils::is_bit_set(m_attribs, cDataStreamSeekable); }

      inline bool get_error() const { return m_error; }

      inline const dynamic_string& get_name() const { return m_name; }
      inline void set_name(const char* pName) { m_name.set(pName); }

      virtual uint read(void* pBuf, uint len) = 0;
      virtual uint64 skip(uint64 len);

      virtual uint write(const void* pBuf, uint len) = 0;
      virtual bool flush() = 0;

      virtual bool is_size_known() const { return true; }

      // Returns DATA_STREAM_SIZE_UNKNOWN if size hasn't been determined yet, or DATA_STREAM_SIZE_INFINITE for infinite streams.
      virtual uint64 get_size() = 0;
      virtual uint64 get_remaining() = 0;

      virtual uint64 get_ofs() = 0;
      virtual bool seek(int64 ofs, bool relative) = 0;

      virtual const void* get_ptr() const { return NULL; }

      inline int read_byte() { uint8 c; if (read(&c, 1) != 1) return -1; return c; }
      inline bool write_byte(uint8 c) { return write(&c, 1) == 1; }

      bool read_line(dynamic_string& str);
      bool printf(const char* p, ...);
      bool write_line(const dynamic_string& str);
      bool write_bom() { uint16 bom = 0xFEFF; return write(&bom, sizeof(bom)) == sizeof(bom); }

      bool read_array(vector<uint8>& buf);
      bool write_array(const vector<uint8>& buf);

   protected:
      dynamic_string m_name;

      attribs_t      m_attribs;
      bool           m_opened : 1;
      bool           m_error : 1;
      bool           m_got_cr : 1;

      inline void set_error() { m_error = true; }
      inline void clear_error() { m_error = false; }

      inline void post_seek() { m_got_cr = false; }
   };

} // namespace crnlib

