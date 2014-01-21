// File: crn_symbol_codec.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_prefix_coding.h"

namespace crnlib
{
   class symbol_codec;
   class adaptive_arith_data_model;

   const uint cSymbolCodecArithMinLen = 0x01000000U;
   const uint cSymbolCodecArithMaxLen = 0xFFFFFFFFU;

   const uint cSymbolCodecArithProbBits = 11;
   const uint cSymbolCodecArithProbScale = 1 << cSymbolCodecArithProbBits;
   const uint cSymbolCodecArithProbMoveBits = 5;

   const uint cSymbolCodecArithProbMulBits    = 8;
   const uint cSymbolCodecArithProbMulScale   = 1 << cSymbolCodecArithProbMulBits;

   class symbol_histogram
   {
   public:
      inline symbol_histogram(uint size = 0) : m_hist(size) { }

      inline void clear() { m_hist.clear(); }

      inline uint size() const { return static_cast<uint>(m_hist.size()); }

      inline void inc_freq(uint x, uint amount = 1)
      {
         uint h = m_hist[x];
         CRNLIB_ASSERT( amount <= (0xFFFFFFFF - h) );
         m_hist[x] = h + amount;
      }

      inline void set_all(uint val) { for (uint i = 0; i < m_hist.size(); i++) m_hist[i] = val; }

      inline void resize(uint new_size) { m_hist.resize(new_size); }

      inline const uint* get_ptr() const { return m_hist.empty() ? NULL : &m_hist.front(); }

      double calc_entropy() const;

      uint operator[] (uint i) const   { return m_hist[i]; }
      uint& operator[] (uint i)        { return m_hist[i]; }

      uint64 get_total() const;

   private:
      crnlib::vector<uint> m_hist;
   };

   class adaptive_huffman_data_model
   {
   public:
      adaptive_huffman_data_model(bool encoding = true, uint total_syms = 0);
      adaptive_huffman_data_model(const adaptive_huffman_data_model& other);
      ~adaptive_huffman_data_model();

      adaptive_huffman_data_model& operator= (const adaptive_huffman_data_model& rhs);

      void clear();

      void init(bool encoding, uint total_syms);
      void reset();

      void rescale();

      uint get_total_syms() const { return m_total_syms; }
      uint get_cost(uint sym) const { return m_code_sizes[sym]; }

   public:
      uint                             m_total_syms;

      uint                             m_update_cycle;
      uint                             m_symbols_until_update;

      uint                             m_total_count;

      crnlib::vector<uint16>           m_sym_freq;

      crnlib::vector<uint16>           m_codes;
      crnlib::vector<uint8>            m_code_sizes;

      prefix_coding::decoder_tables*   m_pDecode_tables;

      uint8                            m_decoder_table_bits;
      bool                             m_encoding;

      void update();

      friend class symbol_codec;
   };

   class static_huffman_data_model
   {
   public:
      static_huffman_data_model();
      static_huffman_data_model(const static_huffman_data_model& other);
      ~static_huffman_data_model();

      static_huffman_data_model& operator= (const static_huffman_data_model& rhs);

      void clear();

      bool init(bool encoding, uint total_syms, const uint16* pSym_freq, uint code_size_limit);
      bool init(bool encoding, uint total_syms, const uint* pSym_freq, uint code_size_limit);
      bool init(bool encoding, uint total_syms, const uint8* pCode_sizes, uint code_size_limit);
      bool init(bool encoding, const symbol_histogram& hist, uint code_size_limit);

      uint get_total_syms() const { return m_total_syms; }
      uint get_cost(uint sym) const { return m_code_sizes[sym]; }

      const uint8* get_code_sizes() const { return m_code_sizes.empty() ? NULL : &m_code_sizes[0]; }

   private:
      uint                             m_total_syms;

      crnlib::vector<uint16>            m_codes;
      crnlib::vector<uint8>             m_code_sizes;

      prefix_coding::decoder_tables*   m_pDecode_tables;

      bool                             m_encoding;

      bool prepare_decoder_tables();
      uint compute_decoder_table_bits() const;

      friend class symbol_codec;
   };

   class adaptive_bit_model
   {
   public:
      adaptive_bit_model();
      adaptive_bit_model(float prob0);
      adaptive_bit_model(const adaptive_bit_model& other);

      adaptive_bit_model& operator= (const adaptive_bit_model& rhs);

      void clear();
      void set_probability_0(float prob0);
      void update(uint bit);

      float get_cost(uint bit) const;

   public:
      uint16 m_bit_0_prob;

      friend class symbol_codec;
      friend class adaptive_arith_data_model;
   };

   class adaptive_arith_data_model
   {
   public:
      adaptive_arith_data_model(bool encoding = true, uint total_syms = 0);
      adaptive_arith_data_model(const adaptive_arith_data_model& other);
      ~adaptive_arith_data_model();

      adaptive_arith_data_model& operator= (const adaptive_arith_data_model& rhs);

      void clear();

      void init(bool encoding, uint total_syms);
      void reset();

      uint get_total_syms() const { return m_total_syms; }
      float get_cost(uint sym) const;

   private:
      uint m_total_syms;
      typedef crnlib::vector<adaptive_bit_model> adaptive_bit_model_vector;
      adaptive_bit_model_vector m_probs;

      friend class symbol_codec;
   };

#if (defined(_XBOX) || defined(_WIN64))
   #define CRNLIB_SYMBOL_CODEC_USE_64_BIT_BUFFER 1
#else
   #define CRNLIB_SYMBOL_CODEC_USE_64_BIT_BUFFER 0
#endif

   class symbol_codec
   {
   public:
      symbol_codec();

      void clear();

      // Encoding
      void start_encoding(uint expected_file_size);
      uint encode_transmit_static_huffman_data_model(static_huffman_data_model& model, bool simulate, static_huffman_data_model* pDelta_model = NULL );
      void encode_bits(uint bits, uint num_bits);
      void encode_align_to_byte();
      void encode(uint sym, adaptive_huffman_data_model& model);
      void encode(uint sym, static_huffman_data_model& model);
      void encode_truncated_binary(uint v, uint n);
      static uint encode_truncated_binary_cost(uint v, uint n);
      void encode_golomb(uint v, uint m);
      void encode_rice(uint v, uint m);
      static uint encode_rice_get_cost(uint v, uint m);
      void encode(uint bit, adaptive_bit_model& model, bool update_model = true);
      void encode(uint sym, adaptive_arith_data_model& model);

      inline void encode_enable_simulation(bool enabled) { m_simulate_encoding = enabled; }
      inline bool encode_get_simulation() { return m_simulate_encoding; }
      inline uint encode_get_total_bits_written() const { return m_total_bits_written; }

      void stop_encoding(bool support_arith);

      const crnlib::vector<uint8>& get_encoding_buf() const  { return m_output_buf; }
            crnlib::vector<uint8>& get_encoding_buf()        { return m_output_buf; }

      // Decoding

      typedef void (*need_bytes_func_ptr)(size_t num_bytes_consumed, void *pPrivate_data, const uint8* &pBuf, size_t &buf_size, bool &eof_flag);

      bool start_decoding(const uint8* pBuf, size_t buf_size, bool eof_flag = true, need_bytes_func_ptr pNeed_bytes_func = NULL, void *pPrivate_data = NULL);
      void decode_set_input_buffer(const uint8* pBuf, size_t buf_size, const uint8* pBuf_next, bool eof_flag = true);
      inline uint64 decode_get_bytes_consumed() const { return m_pDecode_buf_next - m_pDecode_buf; }
      inline uint64 decode_get_bits_remaining() const { return ((m_pDecode_buf_end - m_pDecode_buf_next) << 3) + m_bit_count; }
      void start_arith_decoding();
      bool decode_receive_static_huffman_data_model(static_huffman_data_model& model, static_huffman_data_model* pDeltaModel);
      uint decode_bits(uint num_bits);
      uint decode_peek_bits(uint num_bits);
      void decode_remove_bits(uint num_bits);
      void decode_align_to_byte();
      int decode_remove_byte_from_bit_buf();
      uint decode(adaptive_huffman_data_model& model);
      uint decode(static_huffman_data_model& model);
      uint decode_truncated_binary(uint n);
      uint decode_golomb(uint m);
      uint decode_rice(uint m);
      uint decode(adaptive_bit_model& model, bool update_model = true);
      uint decode(adaptive_arith_data_model& model);
      uint64 stop_decoding();

      uint get_total_model_updates() const { return m_total_model_updates; }

   public:
      const uint8*            m_pDecode_buf;
      const uint8*            m_pDecode_buf_next;
      const uint8*            m_pDecode_buf_end;
      size_t                  m_decode_buf_size;
      bool                    m_decode_buf_eof;

      need_bytes_func_ptr     m_pDecode_need_bytes_func;
      void*                   m_pDecode_private_data;

#if CRNLIB_SYMBOL_CODEC_USE_64_BIT_BUFFER
      typedef uint64 bit_buf_t;
      enum { cBitBufSize = 64 };
#else
      typedef uint32 bit_buf_t;
      enum { cBitBufSize = 32 };
#endif

      bit_buf_t               m_bit_buf;
      int                     m_bit_count;

      uint                    m_total_model_updates;

      crnlib::vector<uint8>    m_output_buf;
      crnlib::vector<uint8>    m_arith_output_buf;

      struct output_symbol
      {
         uint m_bits;

         enum { cArithSym = -1, cAlignToByteSym = -2 };
         int16 m_num_bits;

         uint16 m_arith_prob0;
      };
      crnlib::vector<output_symbol> m_output_syms;

      uint                    m_total_bits_written;
      bool                    m_simulate_encoding;

      uint                    m_arith_base;
      uint                    m_arith_value;
      uint                    m_arith_length;
      uint                    m_arith_total_bits;

      bool                    m_support_arith;

      void put_bits_init(uint expected_size);
      void record_put_bits(uint bits, uint num_bits);

      void arith_propagate_carry();
      void arith_renorm_enc_interval();
      void arith_start_encoding();
      void arith_stop_encoding();

      void put_bits(uint bits, uint num_bits);
      void put_bits_align_to_byte();
      void flush_bits();
      void assemble_output_buf(bool support_arith);

      void get_bits_init();
      uint get_bits(uint num_bits);
      void remove_bits(uint num_bits);

      void decode_need_bytes();

      enum
      {
         cNull,
         cEncoding,
         cDecoding
      } m_mode;
   };

#define CRNLIB_SYMBOL_CODEC_USE_MACROS 1

#ifdef _XBOX
   #define CRNLIB_READ_BIG_ENDIAN_UINT32(p) *reinterpret_cast<const uint32*>(p)
#elif defined(_MSC_VER)
   #define CRNLIB_READ_BIG_ENDIAN_UINT32(p) _byteswap_ulong(*reinterpret_cast<const uint32*>(p))
#else
   #define CRNLIB_READ_BIG_ENDIAN_UINT32(p) utils::swap32(*reinterpret_cast<const uint32*>(p))
#endif

#if CRNLIB_SYMBOL_CODEC_USE_MACROS
   #define CRNLIB_SYMBOL_CODEC_DECODE_DECLARE(codec) \
      uint arith_value; \
      uint arith_length; \
      symbol_codec::bit_buf_t bit_buf; \
      int bit_count; \
      const uint8* pDecode_buf_next;

   #define CRNLIB_SYMBOL_CODEC_DECODE_BEGIN(codec) \
      arith_value = codec.m_arith_value; \
      arith_length = codec.m_arith_length; \
      bit_buf = codec.m_bit_buf; \
      bit_count = codec.m_bit_count; \
      pDecode_buf_next = codec.m_pDecode_buf_next;

   #define CRNLIB_SYMBOL_CODEC_DECODE_END(codec) \
      codec.m_arith_value = arith_value; \
      codec.m_arith_length = arith_length; \
      codec.m_bit_buf = bit_buf; \
      codec.m_bit_count = bit_count; \
      codec.m_pDecode_buf_next = pDecode_buf_next;

   #define CRNLIB_SYMBOL_CODEC_DECODE_GET_BITS(codec, result, num_bits) \
   { \
      while (bit_count < (int)(num_bits)) \
      { \
         uint c = 0; \
         if (pDecode_buf_next == codec.m_pDecode_buf_end) \
         { \
            CRNLIB_SYMBOL_CODEC_DECODE_END(codec) \
            codec.decode_need_bytes(); \
            CRNLIB_SYMBOL_CODEC_DECODE_BEGIN(codec) \
            if (pDecode_buf_next < codec.m_pDecode_buf_end) c = *pDecode_buf_next++; \
         } \
         else \
            c = *pDecode_buf_next++; \
         bit_count += 8; \
         bit_buf |= (static_cast<symbol_codec::bit_buf_t>(c) << (symbol_codec::cBitBufSize - bit_count)); \
      } \
      result = num_bits ? static_cast<uint>(bit_buf >> (symbol_codec::cBitBufSize - (num_bits))) : 0; \
      bit_buf <<= (num_bits); \
      bit_count -= (num_bits); \
   }

   #define CRNLIB_SYMBOL_CODEC_DECODE_ARITH_BIT(codec, result, model) \
   { \
      if (arith_length < cSymbolCodecArithMinLen) \
      { \
         uint c; \
         CRNLIB_SYMBOL_CODEC_DECODE_GET_BITS(codec, c, 8); \
         arith_value = (arith_value << 8) | c; \
         arith_length <<= 8; \
      } \
      uint x = model.m_bit_0_prob * (arith_length >> cSymbolCodecArithProbBits); \
      result = (arith_value >= x); \
      if (!result) \
      { \
         model.m_bit_0_prob += ((cSymbolCodecArithProbScale - model.m_bit_0_prob) >> cSymbolCodecArithProbMoveBits); \
         arith_length = x; \
      } \
      else \
      { \
         model.m_bit_0_prob -= (model.m_bit_0_prob >> cSymbolCodecArithProbMoveBits); \
         arith_value  -= x; \
         arith_length -= x; \
      } \
   }

#if CRNLIB_SYMBOL_CODEC_USE_64_BIT_BUFFER
   #define CRNLIB_SYMBOL_CODEC_DECODE_ADAPTIVE_HUFFMAN(codec, result, model) \
   { \
      const prefix_coding::decoder_tables* pTables = model.m_pDecode_tables; \
      if (bit_count < 24) \
      { \
         uint c = 0; \
         pDecode_buf_next += sizeof(uint32); \
         if (pDecode_buf_next >= codec.m_pDecode_buf_end) \
         { \
            pDecode_buf_next -= sizeof(uint32); \
            while (bit_count < 24) \
            { \
               CRNLIB_SYMBOL_CODEC_DECODE_END(codec) \
               codec.decode_need_bytes(); \
               CRNLIB_SYMBOL_CODEC_DECODE_BEGIN(codec) \
               if (pDecode_buf_next < codec.m_pDecode_buf_end) c = *pDecode_buf_next++; \
               bit_count += 8; \
               bit_buf |= (static_cast<symbol_codec::bit_buf_t>(c) << (symbol_codec::cBitBufSize - bit_count)); \
            } \
         } \
         else \
         { \
            c = CRNLIB_READ_BIG_ENDIAN_UINT32(pDecode_buf_next - sizeof(uint32)); \
            bit_count += 32; \
            bit_buf |= (static_cast<symbol_codec::bit_buf_t>(c) << (symbol_codec::cBitBufSize - bit_count)); \
         } \
      } \
      uint k = static_cast<uint>((bit_buf >> (symbol_codec::cBitBufSize - 16)) + 1); \
      uint len; \
      if (k <= pTables->m_table_max_code) \
      { \
         uint32 t = pTables->m_lookup[bit_buf >> (symbol_codec::cBitBufSize - pTables->m_table_bits)]; \
         result = t & UINT16_MAX; \
         len = t >> 16; \
      } \
      else \
      { \
         len = pTables->m_decode_start_code_size; \
         for ( ; ; ) \
         { \
            if (k <= pTables->m_max_codes[len - 1]) \
               break; \
            len++; \
         } \
         int val_ptr = pTables->m_val_ptrs[len - 1] + static_cast<int>(bit_buf >> (symbol_codec::cBitBufSize - len)); \
         if (((uint)val_ptr >= model.m_total_syms)) val_ptr = 0; \
         result = pTables->m_sorted_symbol_order[val_ptr]; \
      }  \
      bit_buf <<= len; \
      bit_count -= len; \
      uint freq = model.m_sym_freq[result]; \
      freq++; \
      model.m_sym_freq[result] = static_cast<uint16>(freq); \
      if (freq == UINT16_MAX) model.rescale(); \
      if (--model.m_symbols_until_update == 0) \
      { \
         model.update(); \
      } \
   }
#else
   #define CRNLIB_SYMBOL_CODEC_DECODE_ADAPTIVE_HUFFMAN(codec, result, model) \
   { \
      const prefix_coding::decoder_tables* pTables = model.m_pDecode_tables; \
      while (bit_count < (symbol_codec::cBitBufSize - 8)) \
      { \
         uint c = 0; \
         if (pDecode_buf_next == codec.m_pDecode_buf_end) \
         { \
            CRNLIB_SYMBOL_CODEC_DECODE_END(codec) \
            codec.decode_need_bytes(); \
            CRNLIB_SYMBOL_CODEC_DECODE_BEGIN(codec) \
            if (pDecode_buf_next < codec.m_pDecode_buf_end) c = *pDecode_buf_next++; \
         } \
         else \
            c = *pDecode_buf_next++; \
         bit_count += 8; \
         bit_buf |= (static_cast<symbol_codec::bit_buf_t>(c) << (symbol_codec::cBitBufSize - bit_count)); \
      } \
      uint k = static_cast<uint>((bit_buf >> (symbol_codec::cBitBufSize - 16)) + 1); \
      uint len; \
      if (k <= pTables->m_table_max_code) \
      { \
         uint32 t = pTables->m_lookup[bit_buf >> (symbol_codec::cBitBufSize - pTables->m_table_bits)]; \
         result = t & UINT16_MAX; \
         len = t >> 16; \
      } \
      else \
      { \
         len = pTables->m_decode_start_code_size; \
         for ( ; ; ) \
         { \
            if (k <= pTables->m_max_codes[len - 1]) \
               break; \
            len++; \
         } \
         int val_ptr = pTables->m_val_ptrs[len - 1] + static_cast<int>(bit_buf >> (symbol_codec::cBitBufSize - len)); \
         if (((uint)val_ptr >= model.m_total_syms)) val_ptr = 0; \
         result = pTables->m_sorted_symbol_order[val_ptr]; \
      }  \
      bit_buf <<= len; \
      bit_count -= len; \
      uint freq = model.m_sym_freq[result]; \
      freq++; \
      model.m_sym_freq[result] = static_cast<uint16>(freq); \
      if (freq == UINT16_MAX) model.rescale(); \
      if (--model.m_symbols_until_update == 0) \
      { \
         model.update(); \
      } \
   }
#endif

#else
   #define CRNLIB_SYMBOL_CODEC_DECODE_DECLARE(codec)
   #define CRNLIB_SYMBOL_CODEC_DECODE_BEGIN(codec)
   #define CRNLIB_SYMBOL_CODEC_DECODE_END(codec)

   #define CRNLIB_SYMBOL_CODEC_DECODE_GET_BITS(codec, result, num_bits) result = codec.decode_bits(num_bits);
   #define CRNLIB_SYMBOL_CODEC_DECODE_ARITH_BIT(codec, result, model) result = codec.decode(model);
   #define CRNLIB_SYMBOL_CODEC_DECODE_ADAPTIVE_HUFFMAN(codec, result, model) result = codec.decode(model);
#endif

} // namespace crnlib

