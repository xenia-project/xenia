// File: crn_huffman_codes.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   const uint cHuffmanMaxSupportedSyms = 8192;

   void* create_generate_huffman_codes_tables();
   void free_generate_huffman_codes_tables(void* p);
   
   bool generate_huffman_codes(void* pContext, uint num_syms, const uint16* pFreq, uint8* pCodesizes, uint& max_code_size, uint& total_freq_ret);

} // namespace crnlib
