// File: crn_symbol_codec.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_symbol_codec.h"
#include "crn_huffman_codes.h"

namespace crnlib
{
   static float gProbCost[cSymbolCodecArithProbScale];

   //const uint cArithProbMulLenSigBits  = 8;
   //const uint cArithProbMulLenSigScale  = 1 << cArithProbMulLenSigBits;

   class arith_prob_cost_initializer
   {
   public:
      arith_prob_cost_initializer()
      {
         const float cInvLn2 = 1.0f / 0.69314718f;

         for (uint i = 0; i < cSymbolCodecArithProbScale; i++)
            gProbCost[i] = -logf(i * (1.0f / cSymbolCodecArithProbScale)) * cInvLn2;
      }
   };

   static arith_prob_cost_initializer g_prob_cost_initializer;

   double symbol_histogram::calc_entropy() const
   {
      double total = 0.0f;
      for (uint i = 0; i < m_hist.size(); i++)
         total += m_hist[i];
      if (total == 0.0f)
         return 0.0f;

      double entropy = 0.0f;
      double neg_inv_log2 = -1.0f / log(2.0f);
      double inv_total = 1.0f / total;
      for (uint i = 0; i < m_hist.size(); i++)
      {
         if (m_hist[i])
         {
            double bits = log(m_hist[i] * inv_total) * neg_inv_log2;
            entropy += bits * m_hist[i];
         }
      }

      return entropy;
   }

   uint64 symbol_histogram::get_total() const
   {
      uint64 total = 0;
      for (uint i = 0; i < m_hist.size(); i++)
         total += m_hist[i];
      return total;
   }

   adaptive_huffman_data_model::adaptive_huffman_data_model(bool encoding, uint total_syms) :
      m_total_syms(0),
      m_update_cycle(0),
      m_symbols_until_update(0),
      m_total_count(0),
      m_pDecode_tables(NULL),
      m_decoder_table_bits(0),
      m_encoding(encoding)
   {
      if (total_syms)
         init(encoding, total_syms);
   }

   adaptive_huffman_data_model::adaptive_huffman_data_model(const adaptive_huffman_data_model& other) :
      m_total_syms(0),
      m_update_cycle(0),
      m_symbols_until_update(0),
      m_total_count(0),
      m_pDecode_tables(NULL),
      m_decoder_table_bits(0),
      m_encoding(false)
   {
      *this = other;
   }

   adaptive_huffman_data_model::~adaptive_huffman_data_model()
   {
      if (m_pDecode_tables)
         crnlib_delete(m_pDecode_tables);
   }

   adaptive_huffman_data_model& adaptive_huffman_data_model::operator= (const adaptive_huffman_data_model& rhs)
   {
      if (this == &rhs)
         return *this;

      m_total_syms = rhs.m_total_syms;

      m_update_cycle = rhs.m_update_cycle;
      m_symbols_until_update = rhs.m_symbols_until_update;

      m_total_count = rhs.m_total_count;

      m_sym_freq = rhs.m_sym_freq;

      m_codes = rhs.m_codes;
      m_code_sizes = rhs.m_code_sizes;

      if (rhs.m_pDecode_tables)
      {
         if (m_pDecode_tables)
            *m_pDecode_tables = *rhs.m_pDecode_tables;
         else
            m_pDecode_tables = crnlib_new<prefix_coding::decoder_tables>(*rhs.m_pDecode_tables);
      }
      else
      {
         crnlib_delete(m_pDecode_tables);
         m_pDecode_tables = NULL;
      }

      m_decoder_table_bits = rhs.m_decoder_table_bits;
      m_encoding = rhs.m_encoding;

      return *this;
   }

   void adaptive_huffman_data_model::clear()
   {
      m_sym_freq.clear();
      m_codes.clear();
      m_code_sizes.clear();

      m_total_syms = 0;
      m_update_cycle = 0;
      m_symbols_until_update = 0;
      m_decoder_table_bits = 0;
      m_total_count = 0;

      if (m_pDecode_tables)
      {
         crnlib_delete(m_pDecode_tables);
         m_pDecode_tables = NULL;
      }
   }

   void adaptive_huffman_data_model::init(bool encoding, uint total_syms)
   {
      clear();

      m_encoding = encoding;

      m_sym_freq.resize(total_syms);
      m_code_sizes.resize(total_syms);

      m_total_syms = total_syms;

      if (m_total_syms <= 16)
         m_decoder_table_bits = 0;
      else
         m_decoder_table_bits = static_cast<uint8>(math::minimum(1 + math::ceil_log2i(m_total_syms), prefix_coding::cMaxTableBits));

      if (m_encoding)
         m_codes.resize(total_syms);
      else
         m_pDecode_tables = crnlib_new<prefix_coding::decoder_tables>();

      reset();
   }

   void adaptive_huffman_data_model::reset()
   {
      if (!m_total_syms)
         return;

      for (uint i = 0; i < m_total_syms; i++)
         m_sym_freq[i] = 1;

      m_total_count = 0;
      m_update_cycle = m_total_syms;

      update();

      m_symbols_until_update = m_update_cycle = 8;//(m_total_syms + 6) >> 1;
   }

   void adaptive_huffman_data_model::rescale()
   {
      uint total_freq = 0;

      for (uint i = 0; i < m_total_syms; i++)
      {
         uint freq = (m_sym_freq[i] + 1) >> 1;
         total_freq += freq;
         m_sym_freq[i] = static_cast<uint16>(freq);
      }

      m_total_count = total_freq;
   }

   void adaptive_huffman_data_model::update()
   {
      m_total_count += m_update_cycle;

      if (m_total_count >= 32768)
         rescale();

      void* pTables = create_generate_huffman_codes_tables();

      uint max_code_size, total_freq;
      bool status = generate_huffman_codes(pTables, m_total_syms, &m_sym_freq[0], &m_code_sizes[0], max_code_size, total_freq);
      CRNLIB_ASSERT(status);
      CRNLIB_ASSERT(total_freq == m_total_count);

      if (max_code_size > prefix_coding::cMaxExpectedCodeSize)
         prefix_coding::limit_max_code_size(m_total_syms, &m_code_sizes[0], prefix_coding::cMaxExpectedCodeSize);

      free_generate_huffman_codes_tables(pTables);

      if (m_encoding)
         status = prefix_coding::generate_codes(m_total_syms, &m_code_sizes[0], &m_codes[0]);
      else
         status = prefix_coding::generate_decoder_tables(m_total_syms, &m_code_sizes[0], m_pDecode_tables, m_decoder_table_bits);

      CRNLIB_ASSERT(status);
      status;

      m_update_cycle = (5 * m_update_cycle) >> 2;
      uint max_cycle = (m_total_syms + 6) << 3; // this was << 2 - which is ~12% slower but compresses around .5% better

      if (m_update_cycle > max_cycle)
         m_update_cycle = max_cycle;

      m_symbols_until_update = m_update_cycle;
   }

   static_huffman_data_model::static_huffman_data_model() :
      m_total_syms(0),
      m_pDecode_tables(NULL),
      m_encoding(false)
   {
   }

   static_huffman_data_model::static_huffman_data_model(const static_huffman_data_model& other) :
      m_total_syms(0),
      m_pDecode_tables(NULL),
      m_encoding(false)
   {
      *this = other;
   }

   static_huffman_data_model::~static_huffman_data_model()
   {
      if (m_pDecode_tables)
         crnlib_delete(m_pDecode_tables);
   }

   static_huffman_data_model& static_huffman_data_model::operator=(const static_huffman_data_model& rhs)
   {
      if (this == &rhs)
         return *this;

      m_total_syms = rhs.m_total_syms;
      m_codes = rhs.m_codes;
      m_code_sizes = rhs.m_code_sizes;

      if (rhs.m_pDecode_tables)
      {
         if (m_pDecode_tables)
            *m_pDecode_tables = *rhs.m_pDecode_tables;
         else
            m_pDecode_tables = crnlib_new<prefix_coding::decoder_tables>(*rhs.m_pDecode_tables);
      }
      else
      {
         crnlib_delete(m_pDecode_tables);
         m_pDecode_tables = NULL;
      }

      m_encoding = rhs.m_encoding;

      return *this;
   }

   void static_huffman_data_model::clear()
   {
      m_total_syms = 0;
      m_codes.clear();
      m_code_sizes.clear();
      if (m_pDecode_tables)
      {
         crnlib_delete(m_pDecode_tables);
         m_pDecode_tables = NULL;
      }
      m_encoding = false;
   }

   bool static_huffman_data_model::init(bool encoding, uint total_syms, const uint16* pSym_freq, uint code_size_limit)
   {
      CRNLIB_ASSERT((total_syms >= 1) && (total_syms <= prefix_coding::cMaxSupportedSyms) && (code_size_limit >= 1));

      m_encoding = encoding;

      m_total_syms = total_syms;

      code_size_limit = math::minimum(code_size_limit, prefix_coding::cMaxExpectedCodeSize);

      m_code_sizes.resize(total_syms);

      void* pTables = create_generate_huffman_codes_tables();

      uint max_code_size = 0, total_freq;
      bool status = generate_huffman_codes(pTables, m_total_syms, pSym_freq, &m_code_sizes[0], max_code_size, total_freq);

      free_generate_huffman_codes_tables(pTables);

      if (!status)
         return false;

      if (max_code_size > code_size_limit)
      {
         if (!prefix_coding::limit_max_code_size(m_total_syms, &m_code_sizes[0], code_size_limit))
            return false;
      }

      if (m_encoding)
      {
         m_codes.resize(total_syms);

         if (m_pDecode_tables)
         {
            crnlib_delete(m_pDecode_tables);
            m_pDecode_tables = NULL;
         }

         if (!prefix_coding::generate_codes(m_total_syms, &m_code_sizes[0], &m_codes[0]))
            return false;
      }
      else
      {
         m_codes.clear();

         if (!m_pDecode_tables)
            m_pDecode_tables = crnlib_new<prefix_coding::decoder_tables>();

         if (!prefix_coding::generate_decoder_tables(m_total_syms, &m_code_sizes[0], m_pDecode_tables, compute_decoder_table_bits()))
            return false;
      }

      return true;
   }

   bool static_huffman_data_model::init(bool encoding, uint total_syms, const uint* pSym_freq, uint code_size_limit)
   {
      CRNLIB_ASSERT((total_syms >= 1) && (total_syms <= prefix_coding::cMaxSupportedSyms) && (code_size_limit >= 1));

      crnlib::vector<uint16> sym_freq16(total_syms);

      uint max_freq = 0;
      for (uint i = 0; i < total_syms; i++)
         max_freq = math::maximum(max_freq, pSym_freq[i]);

      if (!max_freq)
         return false;

      if (max_freq <= cUINT16_MAX)
      {
         for (uint i = 0; i < total_syms; i++)
            sym_freq16[i] = static_cast<uint16>(pSym_freq[i]);
      }
      else
      {
         for (uint i = 0; i < total_syms; i++)
         {
            uint f = pSym_freq[i];
            if (!f)
               continue;

            uint64 fl = f;

            fl = ((fl << 16) - fl) + (max_freq >> 1);
            fl /= max_freq;
            if (fl < 1)
               fl = 1;

            CRNLIB_ASSERT(fl <= cUINT16_MAX);

            sym_freq16[i] = static_cast<uint16>(fl);
         }
      }

      return init(encoding, total_syms, &sym_freq16[0], code_size_limit);
   }

   bool static_huffman_data_model::init(bool encoding, uint total_syms, const uint8* pCode_sizes, uint code_size_limit)
   {
      CRNLIB_ASSERT((total_syms >= 1) && (total_syms <= prefix_coding::cMaxSupportedSyms) && (code_size_limit >= 1));

      m_encoding = encoding;

      code_size_limit = math::minimum(code_size_limit, prefix_coding::cMaxExpectedCodeSize);

      m_code_sizes.resize(total_syms);

      uint min_code_size = UINT_MAX;
      uint max_code_size = 0;

      for (uint i = 0; i < total_syms; i++)
      {
         uint s = pCode_sizes[i];
         m_code_sizes[i] = static_cast<uint8>(s);
         min_code_size = math::minimum(min_code_size, s);
         max_code_size = math::maximum(max_code_size, s);
      }

      if ((max_code_size < 1) || (max_code_size > 32) || (min_code_size > code_size_limit))
         return false;

      if (max_code_size > code_size_limit)
      {
         if (!prefix_coding::limit_max_code_size(m_total_syms, &m_code_sizes[0], code_size_limit))
            return false;
      }

      if (m_encoding)
      {
         m_codes.resize(total_syms);

         if (m_pDecode_tables)
         {
            crnlib_delete(m_pDecode_tables);
            m_pDecode_tables = NULL;
         }

         if (!prefix_coding::generate_codes(m_total_syms, &m_code_sizes[0], &m_codes[0]))
            return false;
      }
      else
      {
         m_codes.clear();

         if (!m_pDecode_tables)
            m_pDecode_tables = crnlib_new<prefix_coding::decoder_tables>();

         if (!prefix_coding::generate_decoder_tables(m_total_syms, &m_code_sizes[0], m_pDecode_tables, compute_decoder_table_bits()))
            return false;
      }

      return true;
   }

   bool static_huffman_data_model::init(bool encoding, const symbol_histogram& hist, uint code_size_limit)
   {
      return init(encoding, hist.size(), hist.get_ptr(), code_size_limit);
   }

   bool static_huffman_data_model::prepare_decoder_tables()
   {
      uint total_syms = m_code_sizes.size();

      CRNLIB_ASSERT((total_syms >= 1) && (total_syms <= prefix_coding::cMaxSupportedSyms));

      m_encoding = false;

      m_total_syms = total_syms;

      m_codes.clear();

      if (!m_pDecode_tables)
         m_pDecode_tables = crnlib_new<prefix_coding::decoder_tables>();

      return prefix_coding::generate_decoder_tables(m_total_syms, &m_code_sizes[0], m_pDecode_tables, compute_decoder_table_bits());
   }

   uint static_huffman_data_model::compute_decoder_table_bits() const
   {
      uint decoder_table_bits = 0;
      if (m_total_syms > 16)
         decoder_table_bits = static_cast<uint8>(math::minimum(1 + math::ceil_log2i(m_total_syms), prefix_coding::cMaxTableBits));
      return decoder_table_bits;
   }

   adaptive_bit_model::adaptive_bit_model()
   {
      clear();
   }

   adaptive_bit_model::adaptive_bit_model(float prob0)
   {
      set_probability_0(prob0);
   }

   adaptive_bit_model::adaptive_bit_model(const adaptive_bit_model& other) :
      m_bit_0_prob(other.m_bit_0_prob)
   {
   }

   adaptive_bit_model& adaptive_bit_model::operator= (const adaptive_bit_model& rhs)
   {
      m_bit_0_prob = rhs.m_bit_0_prob;
      return *this;
   }

   void adaptive_bit_model::clear()
   {
      m_bit_0_prob  = 1U << (cSymbolCodecArithProbBits - 1);
   }

   void adaptive_bit_model::set_probability_0(float prob0)
   {
      m_bit_0_prob = static_cast<uint16>(math::clamp<uint>((uint)(prob0 * cSymbolCodecArithProbScale), 1, cSymbolCodecArithProbScale - 1));
   }

   float adaptive_bit_model::get_cost(uint bit) const
   {
      return gProbCost[bit ? (cSymbolCodecArithProbScale - m_bit_0_prob) : m_bit_0_prob];
   }

   void adaptive_bit_model::update(uint bit)
   {
      if (!bit)
         m_bit_0_prob += ((cSymbolCodecArithProbScale - m_bit_0_prob) >> cSymbolCodecArithProbMoveBits);
      else
         m_bit_0_prob -= (m_bit_0_prob >> cSymbolCodecArithProbMoveBits);
      CRNLIB_ASSERT(m_bit_0_prob >= 1);
      CRNLIB_ASSERT(m_bit_0_prob < cSymbolCodecArithProbScale);
   }

   adaptive_arith_data_model::adaptive_arith_data_model(bool encoding, uint total_syms)
   {
      init(encoding, total_syms);
   }

   adaptive_arith_data_model::adaptive_arith_data_model(const adaptive_arith_data_model& other)
   {
      m_total_syms = other.m_total_syms;
      m_probs = other.m_probs;
   }

   adaptive_arith_data_model::~adaptive_arith_data_model()
   {
   }

   adaptive_arith_data_model& adaptive_arith_data_model::operator= (const adaptive_arith_data_model& rhs)
   {
      m_total_syms = rhs.m_total_syms;
      m_probs = rhs.m_probs;
      return *this;
   }

   void adaptive_arith_data_model::clear()
   {
      m_total_syms = 0;
      m_probs.clear();
   }

   void adaptive_arith_data_model::init(bool encoding, uint total_syms)
   {
      encoding;
      if (!total_syms)
      {
         clear();
         return;
      }

      if ((total_syms < 2) || (!math::is_power_of_2(total_syms)))
         total_syms = math::next_pow2(total_syms);

      m_total_syms = total_syms;

      m_probs.resize(m_total_syms);
   }

   void adaptive_arith_data_model::reset()
   {
      for (uint i = 0; i < m_probs.size(); i++)
         m_probs[i].clear();
   }

   float adaptive_arith_data_model::get_cost(uint sym) const
   {
      uint node = 1;

      uint bitmask = m_total_syms;

      float cost = 0.0f;
      do
      {
         bitmask >>= 1;

         uint bit = (sym & bitmask) ? 1 : 0;
         cost += m_probs[node].get_cost(bit);
         node = (node << 1) + bit;

      } while (bitmask > 1);

      return cost;
   }

   symbol_codec::symbol_codec()
   {
      clear();
   }

   void symbol_codec::clear()
   {
      m_pDecode_buf = NULL;
      m_pDecode_buf_next = NULL;
      m_pDecode_buf_end = NULL;
      m_decode_buf_size = 0;

      m_bit_buf = 0;
      m_bit_count = 0;
      m_total_model_updates = 0;
      m_mode = cNull;
      m_simulate_encoding = false;
      m_total_bits_written = 0;

      m_arith_base = 0;
      m_arith_value = 0;
      m_arith_length = 0;
      m_arith_total_bits = 0;

      m_output_buf.clear();
      m_arith_output_buf.clear();
      m_output_syms.clear();
   }

   void symbol_codec::start_encoding(uint expected_file_size)
   {
      m_mode = cEncoding;

      m_total_model_updates = 0;
      m_total_bits_written = 0;

      put_bits_init(expected_file_size);

      m_output_syms.resize(0);

      arith_start_encoding();
   }

   // Code length encoding symbols:
   // 0-16 - actual code lengths
   const uint cMaxCodelengthCodes      = 21;

   const uint cSmallZeroRunCode        = 17;
   const uint cLargeZeroRunCode        = 18;
   const uint cSmallRepeatCode         = 19;
   const uint cLargeRepeatCode         = 20;

   const uint cMinSmallZeroRunSize     = 3;
   const uint cMaxSmallZeroRunSize     = 10;
   const uint cMinLargeZeroRunSize     = 11;
   const uint cMaxLargeZeroRunSize     = 138;

   const uint cSmallMinNonZeroRunSize  = 3;
   const uint cSmallMaxNonZeroRunSize  = 6;
   const uint cLargeMinNonZeroRunSize  = 7;
   const uint cLargeMaxNonZeroRunSize  = 70;

   const uint cSmallZeroRunExtraBits   = 3;
   const uint cLargeZeroRunExtraBits   = 7;
   const uint cSmallNonZeroRunExtraBits = 2;
   const uint cLargeNonZeroRunExtraBits = 6;

   static const uint8 g_most_probable_codelength_codes[] =
   {
      cSmallZeroRunCode, cLargeZeroRunCode,
      cSmallRepeatCode,  cLargeRepeatCode,

      0, 8,
      7, 9,
      6, 10,
      5, 11,
      4, 12,
      3, 13,
      2, 14,
      1, 15,
      16
   };
   const uint cNumMostProbableCodelengthCodes = sizeof(g_most_probable_codelength_codes) / sizeof(g_most_probable_codelength_codes[0]);

   static inline void end_zero_run(uint& size, crnlib::vector<uint16>& codes)
   {
      if (!size)
         return;

      if (size < cMinSmallZeroRunSize)
      {
         while (size--)
            codes.push_back(0);
      }
      else if (size <= cMaxSmallZeroRunSize)
         codes.push_back( static_cast<uint16>(cSmallZeroRunCode | ((size - cMinSmallZeroRunSize) << 8)) );
      else
      {
         CRNLIB_ASSERT((size >= cMinLargeZeroRunSize) && (size <= cMaxLargeZeroRunSize));
         codes.push_back( static_cast<uint16>(cLargeZeroRunCode | ((size - cMinLargeZeroRunSize) << 8)) );
      }

      size = 0;
   }

   static inline void end_nonzero_run(uint& size, uint len, crnlib::vector<uint16>& codes)
   {
      if (!size)
         return;

      if (size < cSmallMinNonZeroRunSize)
      {
         while (size--)
            codes.push_back(static_cast<uint16>(len));
      }
      else if (size <= cSmallMaxNonZeroRunSize)
      {
         codes.push_back(static_cast<uint16>(cSmallRepeatCode | ((size - cSmallMinNonZeroRunSize) << 8)));
      }
      else
      {
         CRNLIB_ASSERT((size >= cLargeMinNonZeroRunSize) && (size <= cLargeMaxNonZeroRunSize));
         codes.push_back(static_cast<uint16>(cLargeRepeatCode | ((size - cLargeMinNonZeroRunSize) << 8)));
      }

      size = 0;
   }

   uint symbol_codec::encode_transmit_static_huffman_data_model(static_huffman_data_model& model, bool simulate = false, static_huffman_data_model* pDeltaModel )
   {
      CRNLIB_ASSERT(m_mode == cEncoding);

      uint total_used_syms = 0;
      for (uint i = model.m_total_syms; i > 0; i--)
      {
         if (model.m_code_sizes[i - 1])
         {
            total_used_syms = i;
            break;
         }
      }

      if (!total_used_syms)
      {
         if (!simulate)
         {
            encode_bits(0, math::total_bits(prefix_coding::cMaxSupportedSyms));
         }

         return math::total_bits(prefix_coding::cMaxSupportedSyms);
      }

      crnlib::vector<uint16> codes;
      codes.reserve(model.m_total_syms);

      uint prev_len = UINT_MAX;
      uint cur_zero_run_size = 0;
      uint cur_nonzero_run_size = 0;

      const uint8* pCodesizes = &model.m_code_sizes[0];

      crnlib::vector<uint8> delta_code_sizes;
      if ((pDeltaModel) && (pDeltaModel->get_total_syms()))
      {
         if (pDeltaModel->m_code_sizes.size() < total_used_syms)
            return 0;

         delta_code_sizes.resize(total_used_syms);
         for (uint i = 0; i < total_used_syms; i++)
         {
            int delta = (int)model.m_code_sizes[i] - (int)pDeltaModel->m_code_sizes[i];
            if (delta < 0)
               delta += 17;
            delta_code_sizes[i] = static_cast<uint8>(delta);
         }

         pCodesizes = delta_code_sizes.get_ptr();
      }

      for (uint i = 0; i <= total_used_syms; i++)
      {
         const uint len = (i < total_used_syms) ? *pCodesizes++ : 0xFF;
         CRNLIB_ASSERT((len == 0xFF) || (len <= prefix_coding::cMaxExpectedCodeSize));

         if (!len)
         {
            end_nonzero_run(cur_nonzero_run_size, prev_len, codes);

            if (++cur_zero_run_size == cMaxLargeZeroRunSize)
               end_zero_run(cur_zero_run_size, codes);
         }
         else
         {
            end_zero_run(cur_zero_run_size, codes);

            if (len != prev_len)
            {
               end_nonzero_run(cur_nonzero_run_size, prev_len, codes);

               if (len != 0xFF)
                  codes.push_back(static_cast<uint16>(len));
            }
            else if (++cur_nonzero_run_size == cLargeMaxNonZeroRunSize)
               end_nonzero_run(cur_nonzero_run_size, prev_len, codes);
         }

         prev_len = len;
      }

      uint16 hist[cMaxCodelengthCodes];
      utils::zero_object(hist);

      for (uint i = 0; i < codes.size(); i++)
      {
         uint code = codes[i] & 0xFF;
         CRNLIB_ASSERT(code < cMaxCodelengthCodes);
         hist[code] = static_cast<uint16>(hist[code] + 1);
      }

      static_huffman_data_model dm;
      if (!dm.init(true, cMaxCodelengthCodes, hist, 7))
         return 0;

      uint num_codelength_codes_to_send;
      for (num_codelength_codes_to_send = cNumMostProbableCodelengthCodes; num_codelength_codes_to_send > 0; num_codelength_codes_to_send--)
         if (dm.get_cost(g_most_probable_codelength_codes[num_codelength_codes_to_send - 1]))
            break;

      uint total_bits = math::total_bits(prefix_coding::cMaxSupportedSyms);
      total_bits += 5;
      total_bits += 3 * num_codelength_codes_to_send;

      if (!simulate)
      {
         encode_bits(total_used_syms, math::total_bits(prefix_coding::cMaxSupportedSyms));

         encode_bits(num_codelength_codes_to_send, 5);
         for (uint i = 0; i < num_codelength_codes_to_send; i++)
            encode_bits(dm.get_cost(g_most_probable_codelength_codes[i]), 3);
      }

      for (uint i = 0; i < codes.size(); i++)
      {
         uint code = codes[i];
         uint extra = code >> 8;
         code &= 0xFF;

         uint extra_bits = 0;
         if (code == cSmallZeroRunCode)
            extra_bits = cSmallZeroRunExtraBits;
         else if (code == cLargeZeroRunCode)
            extra_bits = cLargeZeroRunExtraBits;
         else if (code == cSmallRepeatCode)
            extra_bits = cSmallNonZeroRunExtraBits;
         else if (code == cLargeRepeatCode)
            extra_bits = cLargeNonZeroRunExtraBits;

         total_bits += dm.get_cost(code);

         if (!simulate)
            encode(code, dm);

         if (extra_bits)
         {
            if (!simulate)
               encode_bits(extra, extra_bits);

            total_bits += extra_bits;
         }
      }

      return total_bits;
   }

   void symbol_codec::encode_bits(uint bits, uint num_bits)
   {
      CRNLIB_ASSERT(m_mode == cEncoding);

      if (!num_bits)
         return;

      CRNLIB_ASSERT((num_bits == 32) || (bits <= ((1U << num_bits) - 1)));

      if (num_bits > 16)
      {
         record_put_bits(bits >> 16, num_bits - 16);
         record_put_bits(bits & 0xFFFF, 16);
      }
      else
         record_put_bits(bits, num_bits);
   }

   void symbol_codec::encode_align_to_byte()
   {
      CRNLIB_ASSERT(m_mode == cEncoding);

      if (!m_simulate_encoding)
      {
         output_symbol sym;
         sym.m_bits = 0;
         sym.m_num_bits = output_symbol::cAlignToByteSym;
         sym.m_arith_prob0 = 0;
         m_output_syms.push_back(sym);
      }
      else
      {
         // We really don't know how many we're going to write, so just be conservative.
         m_total_bits_written += 7;
      }
   }

   void symbol_codec::encode(uint sym, adaptive_huffman_data_model& model)
   {
      CRNLIB_ASSERT(m_mode == cEncoding);
      CRNLIB_ASSERT(model.m_encoding);

      record_put_bits(model.m_codes[sym], model.m_code_sizes[sym]);

      uint freq = model.m_sym_freq[sym];
      freq++;
      model.m_sym_freq[sym] = static_cast<uint16>(freq);

      if (freq == cUINT16_MAX)
         model.rescale();

      if (--model.m_symbols_until_update == 0)
      {
         m_total_model_updates++;
         model.update();
      }
   }

   void symbol_codec::encode(uint sym, static_huffman_data_model& model)
   {
      CRNLIB_ASSERT(m_mode == cEncoding);
      CRNLIB_ASSERT(model.m_encoding);

      CRNLIB_ASSERT(model.m_code_sizes[sym]);

      record_put_bits(model.m_codes[sym], model.m_code_sizes[sym]);
   }

   void symbol_codec::encode_truncated_binary(uint v, uint n)
   {
      CRNLIB_ASSERT((n >= 2) && (v < n));

      uint k = math::floor_log2i(n);
      uint u = (1 << (k + 1)) - n;

      if (v < u)
         encode_bits(v, k);
      else
         encode_bits(v + u, k + 1);
   }

   uint symbol_codec::encode_truncated_binary_cost(uint v, uint n)
   {
      CRNLIB_ASSERT((n >= 2) && (v < n));

      uint k = math::floor_log2i(n);
      uint u = (1 << (k + 1)) - n;

      if (v < u)
         return k;
      else
         return k + 1;
   }

   void symbol_codec::encode_golomb(uint v, uint m)
   {
      CRNLIB_ASSERT(m > 0);

      uint q = v / m;
      uint r = v % m;

      while (q > 16)
      {
         encode_bits(0xFFFF, 16);
         q -= 16;
      }

      if (q)
         encode_bits( (1 << q) - 1, q);

      encode_bits(0, 1);

      encode_truncated_binary(r, m);
   }

   void symbol_codec::encode_rice(uint v, uint m)
   {
      CRNLIB_ASSERT(m > 0);

      uint q = v >> m;
      uint r = v & ((1 << m) - 1);

      while (q > 16)
      {
         encode_bits(0xFFFF, 16);
         q -= 16;
      }

      if (q)
         encode_bits( (1 << q) - 1, q);

      encode_bits(0, 1);

      encode_bits(r, m);
   }

   uint symbol_codec::encode_rice_get_cost(uint v, uint m)
   {
      CRNLIB_ASSERT(m > 0);

      uint q = v >> m;
      //uint r = v & ((1 << m) - 1);

      return q + 1 + m;
   }

   void symbol_codec::arith_propagate_carry()
   {
      int index = m_arith_output_buf.size() - 1;
      while (index >= 0)
      {
         uint c = m_arith_output_buf[index];

         if (c == 0xFF)
            m_arith_output_buf[index] = 0;
         else
         {
            m_arith_output_buf[index]++;
            break;
         }

         index--;
      }
   }

   void symbol_codec::arith_renorm_enc_interval()
   {
      do
      {
         m_arith_output_buf.push_back( (m_arith_base >> 24) & 0xFF );
         m_total_bits_written += 8;

         m_arith_base <<= 8;
      } while ((m_arith_length <<= 8) < cSymbolCodecArithMinLen);
   }

   void symbol_codec::arith_start_encoding()
   {
      m_arith_output_buf.resize(0);

      m_arith_base = 0;
      m_arith_value = 0;
      m_arith_length = cSymbolCodecArithMaxLen;
      m_arith_total_bits = 0;
   }

   void symbol_codec::encode(uint bit, adaptive_bit_model& model, bool update_model)
   {
      CRNLIB_ASSERT(m_mode == cEncoding);

      m_arith_total_bits++;

      if (!m_simulate_encoding)
      {
         output_symbol sym;
         sym.m_bits = bit;
         sym.m_num_bits = -1;
         sym.m_arith_prob0 = model.m_bit_0_prob;
         m_output_syms.push_back(sym);
      }

      //uint x = gArithProbMulTab[model.m_bit_0_prob >> (cSymbolCodecArithProbBits - cSymbolCodecArithProbMulBits)][m_arith_length >> (32 - cSymbolCodecArithProbMulLenSigBits)] << 16;
      uint x = model.m_bit_0_prob * (m_arith_length >> cSymbolCodecArithProbBits);

      if (!bit)
      {
         if (update_model)
            model.m_bit_0_prob += ((cSymbolCodecArithProbScale - model.m_bit_0_prob) >> cSymbolCodecArithProbMoveBits);

         m_arith_length = x;
      }
      else
      {
         if (update_model)
            model.m_bit_0_prob -= (model.m_bit_0_prob >> cSymbolCodecArithProbMoveBits);

         uint orig_base = m_arith_base;
         m_arith_base   += x;
         m_arith_length -= x;
         if (orig_base > m_arith_base)
            arith_propagate_carry();
      }

      if (m_arith_length < cSymbolCodecArithMinLen)
         arith_renorm_enc_interval();
   }

   void symbol_codec::encode(uint sym, adaptive_arith_data_model& model)
   {
      uint node = 1;

      uint bitmask = model.m_total_syms;

      do
      {
         bitmask >>= 1;

         uint bit = (sym & bitmask) ? 1 : 0;
         encode(bit, model.m_probs[node]);
         node = (node << 1) + bit;

      } while (bitmask > 1);
   }

   void symbol_codec::arith_stop_encoding()
   {
      if (!m_arith_total_bits)
         return;

      uint orig_base = m_arith_base;

      if (m_arith_length > 2 * cSymbolCodecArithMinLen)
      {
         m_arith_base  += cSymbolCodecArithMinLen;
         m_arith_length = (cSymbolCodecArithMinLen >> 1);
      }
      else
      {
         m_arith_base  += (cSymbolCodecArithMinLen >> 1);
         m_arith_length = (cSymbolCodecArithMinLen >> 9);
      }

      if (orig_base > m_arith_base)
         arith_propagate_carry();

      arith_renorm_enc_interval();

      while (m_arith_output_buf.size() < 4)
      {
         m_arith_output_buf.push_back(0);
         m_total_bits_written += 8;
      }
   }

   void symbol_codec::stop_encoding(bool support_arith)
   {
      CRNLIB_ASSERT(m_mode == cEncoding);

      arith_stop_encoding();

      if (!m_simulate_encoding)
         assemble_output_buf(support_arith);

      m_mode = cNull;
   }

   void symbol_codec::record_put_bits(uint bits, uint num_bits)
   {
      CRNLIB_ASSERT(m_mode == cEncoding);

      CRNLIB_ASSERT(num_bits <= 25);
      CRNLIB_ASSERT(m_bit_count >= 25);

      if (!num_bits)
         return;

      m_total_bits_written += num_bits;

      if (!m_simulate_encoding)
      {
         output_symbol sym;
         sym.m_bits = bits;
         sym.m_num_bits = (uint16)num_bits;
         sym.m_arith_prob0 = 0;
         m_output_syms.push_back(sym);
      }
   }

   void symbol_codec::put_bits_init(uint expected_size)
   {
      m_bit_buf = 0;
      m_bit_count = cBitBufSize;

      m_output_buf.resize(0);
      m_output_buf.reserve(expected_size);
   }

   void symbol_codec::put_bits(uint bits, uint num_bits)
   {
      CRNLIB_ASSERT(num_bits <= 25);
      CRNLIB_ASSERT(m_bit_count >= 25);

      if (!num_bits)
         return;

      m_bit_count -= num_bits;
      m_bit_buf |= (static_cast<bit_buf_t>(bits) << m_bit_count);

      m_total_bits_written += num_bits;

      while (m_bit_count <= (cBitBufSize - 8))
      {
         m_output_buf.push_back(static_cast<uint8>(m_bit_buf >> (cBitBufSize - 8)));

         m_bit_buf <<= 8;
         m_bit_count += 8;
      }
   }

   void symbol_codec::put_bits_align_to_byte()
   {
      uint num_bits_in = cBitBufSize - m_bit_count;
      if (num_bits_in & 7)
      {
         put_bits(0, 8 - (num_bits_in & 7));
      }
   }

   void symbol_codec::flush_bits()
   {
      //put_bits(15, 4); // for table look-ahead
      //put_bits(3, 3); // for table look-ahead

      put_bits(0, 7); // to ensure the last bits are flushed
   }

   void symbol_codec::assemble_output_buf(bool support_arith)
   {
      m_total_bits_written = 0;

      uint arith_buf_ofs = 0;

      if (support_arith)
      {
         if (m_arith_output_buf.size())
         {
            put_bits(1, 1);

            m_arith_length = cSymbolCodecArithMaxLen;
            m_arith_value = 0;
            for (uint i = 0; i < 4; i++)
            {
               const uint c = m_arith_output_buf[arith_buf_ofs++];
               m_arith_value = (m_arith_value << 8) | c;
               put_bits(c, 8);
            }
         }
         else
         {
            put_bits(0, 1);
         }
      }

      for (uint sym_index = 0; sym_index < m_output_syms.size(); sym_index++)
      {
         const output_symbol& sym = m_output_syms[sym_index];

         if (sym.m_num_bits == output_symbol::cAlignToByteSym)
         {
            put_bits_align_to_byte();
         }
         else if (sym.m_num_bits == output_symbol::cArithSym)
         {
            if (m_arith_length < cSymbolCodecArithMinLen)
            {
               do
               {
                  const uint c = (arith_buf_ofs < m_arith_output_buf.size()) ? m_arith_output_buf[arith_buf_ofs++] : 0;
                  put_bits(c, 8);
                  m_arith_value = (m_arith_value << 8) | c;
               } while ((m_arith_length <<= 8) < cSymbolCodecArithMinLen);
            }

            //uint x = gArithProbMulTab[sym.m_arith_prob0 >> (cSymbolCodecArithProbBits - cSymbolCodecArithProbMulBits)][m_arith_length >> (32 - cSymbolCodecArithProbMulLenSigBits)] << 16;
            uint x = sym.m_arith_prob0 * (m_arith_length >> cSymbolCodecArithProbBits);
            uint bit = (m_arith_value >= x);

            if (bit == 0)
            {
               m_arith_length = x;
            }
            else
            {
               m_arith_value  -= x;
               m_arith_length -= x;
            }

            CRNLIB_VERIFY(bit == sym.m_bits);
         }
         else
         {
            put_bits(sym.m_bits, sym.m_num_bits);
         }
      }

      flush_bits();
   }

   //------------------------------------------------------------------------------------------------------------------
   // Decoding
   //------------------------------------------------------------------------------------------------------------------

   bool symbol_codec::start_decoding(const uint8* pBuf, size_t buf_size, bool eof_flag, need_bytes_func_ptr pNeed_bytes_func, void *pPrivate_data)
   {
      if (!buf_size)
         return false;

      m_total_model_updates = 0;

      m_pDecode_buf = pBuf;
      m_pDecode_buf_next = pBuf;
      m_decode_buf_size = buf_size;
      m_pDecode_buf_end = pBuf + buf_size;

      m_pDecode_need_bytes_func = pNeed_bytes_func;
      m_pDecode_private_data = pPrivate_data;
      m_decode_buf_eof = eof_flag;
      if (!pNeed_bytes_func)
      {
         m_decode_buf_eof = true;
      }

      m_mode = cDecoding;

      get_bits_init();

      return true;
   }

   uint symbol_codec::decode_bits(uint num_bits)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      if (!num_bits)
         return 0;

      if (num_bits > 16)
      {
         uint a = get_bits(num_bits - 16);
         uint b = get_bits(16);

         return (a << 16) | b;
      }
      else
         return get_bits(num_bits);
   }

   void symbol_codec::decode_remove_bits(uint num_bits)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      while (num_bits > 16)
      {
         remove_bits(16);
         num_bits -= 16;
      }

      remove_bits(num_bits);
   }

   uint symbol_codec::decode_peek_bits(uint num_bits)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);
      CRNLIB_ASSERT(num_bits <= 25);

      if (!num_bits)
         return 0;

      while (m_bit_count < (int)num_bits)
      {
         uint c = 0;
         if (m_pDecode_buf_next == m_pDecode_buf_end)
         {
            if (!m_decode_buf_eof)
            {
               m_pDecode_need_bytes_func(m_pDecode_buf_next - m_pDecode_buf, m_pDecode_private_data, m_pDecode_buf, m_decode_buf_size, m_decode_buf_eof);
               m_pDecode_buf_end = m_pDecode_buf + m_decode_buf_size;
               m_pDecode_buf_next = m_pDecode_buf;
               if (m_pDecode_buf_next < m_pDecode_buf_end) c = *m_pDecode_buf_next++;
            }
         }
         else
            c = *m_pDecode_buf_next++;

         m_bit_count += 8;
         CRNLIB_ASSERT(m_bit_count <= cBitBufSize);

         m_bit_buf |= (static_cast<bit_buf_t>(c) << (cBitBufSize - m_bit_count));
      }

      return static_cast<uint>(m_bit_buf >> (cBitBufSize - num_bits));
   }

   uint symbol_codec::decode(adaptive_huffman_data_model& model)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);
      CRNLIB_ASSERT(!model.m_encoding);

      const prefix_coding::decoder_tables* pTables = model.m_pDecode_tables;

      while (m_bit_count < (cBitBufSize - 8))
      {
         uint c = 0;
         if (m_pDecode_buf_next == m_pDecode_buf_end)
         {
            if (!m_decode_buf_eof)
            {
               m_pDecode_need_bytes_func(m_pDecode_buf_next - m_pDecode_buf, m_pDecode_private_data, m_pDecode_buf, m_decode_buf_size, m_decode_buf_eof);
               m_pDecode_buf_end = m_pDecode_buf + m_decode_buf_size;
               m_pDecode_buf_next = m_pDecode_buf;
               if (m_pDecode_buf_next < m_pDecode_buf_end) c = *m_pDecode_buf_next++;
            }
         }
         else
            c = *m_pDecode_buf_next++;

         m_bit_count += 8;
         m_bit_buf |= (static_cast<bit_buf_t>(c) << (cBitBufSize - m_bit_count));
      }

      uint k = static_cast<uint>((m_bit_buf >> (cBitBufSize - 16)) + 1);
      uint sym, len;

      if (k <= pTables->m_table_max_code)
      {
         uint32 t = pTables->m_lookup[m_bit_buf >> (cBitBufSize - pTables->m_table_bits)];

         CRNLIB_ASSERT(t != cUINT32_MAX);
         sym = t & cUINT16_MAX;
         len = t >> 16;

         CRNLIB_ASSERT(model.m_code_sizes[sym] == len);
      }
      else
      {
         len = pTables->m_decode_start_code_size;

         for ( ; ; )
         {
            if (k <= pTables->m_max_codes[len - 1])
               break;
            len++;
         }

         int val_ptr = pTables->m_val_ptrs[len - 1] + static_cast<int>((m_bit_buf >> (cBitBufSize - len)));

         if (((uint)val_ptr >= model.m_total_syms))
         {
            // corrupted stream, or a bug
            CRNLIB_ASSERT(0);
            return 0;
         }

         sym = pTables->m_sorted_symbol_order[val_ptr];
      }

      m_bit_buf <<= len;
      m_bit_count -= len;

      uint freq = model.m_sym_freq[sym];
      freq++;
      model.m_sym_freq[sym] = static_cast<uint16>(freq);

      if (freq == cUINT16_MAX)
         model.rescale();

      if (--model.m_symbols_until_update == 0)
      {
         m_total_model_updates++;
         model.update();
      }

      return sym;
   }

   void symbol_codec::decode_set_input_buffer(const uint8* pBuf, size_t buf_size, const uint8* pBuf_next, bool eof_flag)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      m_pDecode_buf = pBuf;
      m_pDecode_buf_next = pBuf_next;
      m_decode_buf_size = buf_size;
      m_pDecode_buf_end = pBuf + buf_size;

      if (!m_pDecode_need_bytes_func)
         m_decode_buf_eof = true;
      else
         m_decode_buf_eof = eof_flag;
   }

   bool symbol_codec::decode_receive_static_huffman_data_model(static_huffman_data_model& model, static_huffman_data_model* pDeltaModel)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      const uint total_used_syms = decode_bits(math::total_bits(prefix_coding::cMaxSupportedSyms));
      if (!total_used_syms)
      {
         model.clear();
         return true;
      }

      model.m_code_sizes.resize(total_used_syms);
      memset(&model.m_code_sizes[0], 0, sizeof(model.m_code_sizes[0]) * total_used_syms);

      const uint num_codelength_codes_to_send = decode_bits(5);
      if ((num_codelength_codes_to_send < 1) || (num_codelength_codes_to_send > cMaxCodelengthCodes))
         return false;

      static_huffman_data_model dm;
      dm.m_code_sizes.resize(cMaxCodelengthCodes);

      for (uint i = 0; i < num_codelength_codes_to_send; i++)
         dm.m_code_sizes[g_most_probable_codelength_codes[i]] = static_cast<uint8>(decode_bits(3));

      if (!dm.prepare_decoder_tables())
         return false;

      uint ofs = 0;
      while (ofs < total_used_syms)
      {
         const uint num_remaining = total_used_syms - ofs;

         uint code = decode(dm);
         if (code <= 16)
            model.m_code_sizes[ofs++] = static_cast<uint8>(code);
         else if (code == cSmallZeroRunCode)
         {
            uint len = decode_bits(cSmallZeroRunExtraBits) + cMinSmallZeroRunSize;
            if (len > num_remaining)
               return false;
            ofs += len;
         }
         else if (code == cLargeZeroRunCode)
         {
            uint len = decode_bits(cLargeZeroRunExtraBits) + cMinLargeZeroRunSize;
            if (len > num_remaining)
               return false;
            ofs += len;
         }
         else if ((code == cSmallRepeatCode) || (code == cLargeRepeatCode))
         {
            uint len;
            if (code == cSmallRepeatCode)
               len = decode_bits(cSmallNonZeroRunExtraBits) + cSmallMinNonZeroRunSize;
            else
               len = decode_bits(cLargeNonZeroRunExtraBits) + cLargeMinNonZeroRunSize;

            if ((!ofs) || (len > num_remaining))
               return false;
            const uint prev = model.m_code_sizes[ofs - 1];
            if (!prev)
               return false;
            const uint end = ofs + len;
            while (ofs < end)
               model.m_code_sizes[ofs++] = static_cast<uint8>(prev);
         }
         else
         {
            CRNLIB_ASSERT(0);
            return false;
         }
      }

      if (ofs != total_used_syms)
         return false;

      if ((pDeltaModel) && (pDeltaModel->get_total_syms()))
      {
         uint n = math::minimum(pDeltaModel->m_code_sizes.size(), total_used_syms);
         for (uint i = 0; i < n; i++)
         {
            int codesize = model.m_code_sizes[i] + pDeltaModel->m_code_sizes[i];
            if (codesize > 16)
               codesize -= 17;
            model.m_code_sizes[i] = static_cast<uint8>(codesize);
         }
      }

      return model.prepare_decoder_tables();
   }

   uint symbol_codec::decode(static_huffman_data_model& model)
   {
      CRNLIB_ASSERT(m_mode == cDecoding);
      CRNLIB_ASSERT(!model.m_encoding);

      const prefix_coding::decoder_tables* pTables = model.m_pDecode_tables;

      while (m_bit_count < (cBitBufSize - 8))
      {
         uint c = 0;
         if (m_pDecode_buf_next == m_pDecode_buf_end)
         {
            if (!m_decode_buf_eof)
            {
               m_pDecode_need_bytes_func(m_pDecode_buf_next - m_pDecode_buf, m_pDecode_private_data, m_pDecode_buf, m_decode_buf_size, m_decode_buf_eof);
               m_pDecode_buf_end = m_pDecode_buf + m_decode_buf_size;
               m_pDecode_buf_next = m_pDecode_buf;
               if (m_pDecode_buf_next < m_pDecode_buf_end) c = *m_pDecode_buf_next++;
            }
         }
         else
            c = *m_pDecode_buf_next++;

         m_bit_count += 8;
         m_bit_buf |= (static_cast<bit_buf_t>(c) << (cBitBufSize - m_bit_count));
      }

      uint k = static_cast<uint>((m_bit_buf >> (cBitBufSize - 16)) + 1);
      uint sym, len;

      if (k <= pTables->m_table_max_code)
      {
         uint32 t = pTables->m_lookup[m_bit_buf >> (cBitBufSize - pTables->m_table_bits)];

         CRNLIB_ASSERT(t != cUINT32_MAX);
         sym = t & cUINT16_MAX;
         len = t >> 16;

         CRNLIB_ASSERT(model.m_code_sizes[sym] == len);
      }
      else
      {
         len = pTables->m_decode_start_code_size;

         for ( ; ; )
         {
            if (k <= pTables->m_max_codes[len - 1])
               break;
            len++;
         }

         int val_ptr = pTables->m_val_ptrs[len - 1] + static_cast<int>((m_bit_buf >> (cBitBufSize - len)));

         if (((uint)val_ptr >= model.m_total_syms))
         {
            // corrupted stream, or a bug
            CRNLIB_ASSERT(0);
            return 0;
         }

         sym = pTables->m_sorted_symbol_order[val_ptr];
      }

      m_bit_buf <<= len;
      m_bit_count -= len;

      return sym;
   }

   uint symbol_codec::decode_truncated_binary(uint n)
   {
      CRNLIB_ASSERT(n >= 2);

      uint k = math::floor_log2i(n);
      uint u = (1 << (k + 1)) - n;

      uint i = decode_bits(k);

      if (i >= u)
         i = ((i << 1) | decode_bits(1)) - u;

      return i;
   }

   uint symbol_codec::decode_golomb(uint m)
   {
      CRNLIB_ASSERT(m > 1);

      uint q = 0;

      for ( ; ; )
      {
         uint k = decode_peek_bits(16);

         uint l = utils::count_leading_zeros16((~k) & 0xFFFF);
         q += l;
         if (l < 16)
            break;
      }

      decode_remove_bits(q + 1);

      uint r = decode_truncated_binary(m);

      return (q * m) + r;
   }

   uint symbol_codec::decode_rice(uint m)
   {
      CRNLIB_ASSERT(m > 0);

      uint q = 0;

      for ( ; ; )
      {
         uint k = decode_peek_bits(16);

         uint l = utils::count_leading_zeros16((~k) & 0xFFFF);

         q += l;

         decode_remove_bits(l);

         if (l < 16)
            break;
      }

      decode_remove_bits(1);

      uint r = decode_bits(m);

      return (q << m) + r;
   }

   uint64 symbol_codec::stop_decoding()
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      uint64 n = m_pDecode_buf_next - m_pDecode_buf;

      m_mode = cNull;

      return n;
   }

   void symbol_codec::get_bits_init()
   {
      m_bit_buf = 0;
      m_bit_count = 0;
   }

   uint symbol_codec::get_bits(uint num_bits)
   {
      CRNLIB_ASSERT(num_bits <= 25);

      if (!num_bits)
         return 0;

      while (m_bit_count < (int)num_bits)
      {
         uint c = 0;
         if (m_pDecode_buf_next == m_pDecode_buf_end)
         {
            if (!m_decode_buf_eof)
            {
               m_pDecode_need_bytes_func(m_pDecode_buf_next - m_pDecode_buf, m_pDecode_private_data, m_pDecode_buf, m_decode_buf_size, m_decode_buf_eof);
               m_pDecode_buf_end = m_pDecode_buf + m_decode_buf_size;
               m_pDecode_buf_next = m_pDecode_buf;
               if (m_pDecode_buf_next < m_pDecode_buf_end) c = *m_pDecode_buf_next++;
            }
         }
         else
            c = *m_pDecode_buf_next++;

         m_bit_count += 8;
         CRNLIB_ASSERT(m_bit_count <= cBitBufSize);

         m_bit_buf |= (static_cast<bit_buf_t>(c) << (cBitBufSize - m_bit_count));
      }

      uint result = static_cast<uint>(m_bit_buf >> (cBitBufSize - num_bits));

      m_bit_buf <<= num_bits;
      m_bit_count -= num_bits;

      return result;
   }

   void symbol_codec::remove_bits(uint num_bits)
   {
      CRNLIB_ASSERT(num_bits <= 25);

      if (!num_bits)
         return;

      while (m_bit_count < (int)num_bits)
      {
         uint c = 0;
         if (m_pDecode_buf_next == m_pDecode_buf_end)
         {
            if (!m_decode_buf_eof)
            {
               m_pDecode_need_bytes_func(m_pDecode_buf_next - m_pDecode_buf, m_pDecode_private_data, m_pDecode_buf, m_decode_buf_size, m_decode_buf_eof);
               m_pDecode_buf_end = m_pDecode_buf + m_decode_buf_size;
               m_pDecode_buf_next = m_pDecode_buf;
               if (m_pDecode_buf_next < m_pDecode_buf_end) c = *m_pDecode_buf_next++;
            }
         }
         else
            c = *m_pDecode_buf_next++;

         m_bit_count += 8;
         CRNLIB_ASSERT(m_bit_count <= cBitBufSize);

         m_bit_buf |= (static_cast<bit_buf_t>(c) << (cBitBufSize - m_bit_count));
      }

      m_bit_buf <<= num_bits;
      m_bit_count -= num_bits;
   }

   void symbol_codec::decode_align_to_byte()
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      if (m_bit_count & 7)
      {
         remove_bits(m_bit_count & 7);
      }
   }

   int symbol_codec::decode_remove_byte_from_bit_buf()
   {
      if (m_bit_count < 8)
         return -1;
      int result = static_cast<int>(m_bit_buf >> (cBitBufSize - 8));
      m_bit_buf <<= 8;
      m_bit_count -= 8;
      return result;
   }

   uint symbol_codec::decode(adaptive_bit_model& model, bool update_model)
   {
      if (m_arith_length < cSymbolCodecArithMinLen)
      {
         uint c = get_bits(8);
         m_arith_value = (m_arith_value << 8) | c;

         m_arith_length <<= 8;
         CRNLIB_ASSERT(m_arith_length >= cSymbolCodecArithMinLen);
      }

      CRNLIB_ASSERT(m_arith_length >= cSymbolCodecArithMinLen);

      //uint x = gArithProbMulTab[model.m_bit_0_prob >> (cSymbolCodecArithProbBits - cSymbolCodecArithProbMulBits)][m_arith_length >> (32 - cSymbolCodecArithProbMulLenSigBits)] << 16;
      uint x = model.m_bit_0_prob * (m_arith_length >> cSymbolCodecArithProbBits);
      uint bit = (m_arith_value >= x);

      if (!bit)
      {
         if (update_model)
            model.m_bit_0_prob += ((cSymbolCodecArithProbScale - model.m_bit_0_prob) >> cSymbolCodecArithProbMoveBits);

         m_arith_length = x;
      }
      else
      {
         if (update_model)
            model.m_bit_0_prob -= (model.m_bit_0_prob >> cSymbolCodecArithProbMoveBits);

         m_arith_value  -= x;
         m_arith_length -= x;
      }

      return bit;
   }

   uint symbol_codec::decode(adaptive_arith_data_model& model)
   {
      uint node = 1;

      do
      {
         uint bit = decode(model.m_probs[node]);

         node = (node << 1) + bit;

      } while (node < model.m_total_syms);

      return node - model.m_total_syms;
   }

   void symbol_codec::start_arith_decoding()
   {
      CRNLIB_ASSERT(m_mode == cDecoding);

      m_arith_length = cSymbolCodecArithMaxLen;
      m_arith_value = 0;

      if (get_bits(1))
      {
         m_arith_value = (get_bits(8) << 24);
         m_arith_value |= (get_bits(8) << 16);
         m_arith_value |= (get_bits(8) << 8);
         m_arith_value |= get_bits(8);
      }
   }

   void symbol_codec::decode_need_bytes()
   {
      if (!m_decode_buf_eof)
      {
         m_pDecode_need_bytes_func(m_pDecode_buf_next - m_pDecode_buf, m_pDecode_private_data, m_pDecode_buf, m_decode_buf_size, m_decode_buf_eof);
         m_pDecode_buf_end = m_pDecode_buf + m_decode_buf_size;
         m_pDecode_buf_next = m_pDecode_buf;
      }
   }

} // namespace crnlib
