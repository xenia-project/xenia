// File: crn_dxt_hc_common.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_dxt_hc_common.h"

namespace crnlib
{
   chunk_encoding_desc g_chunk_encodings[cNumChunkEncodings] =
   {
      { 1, { { 0, 0, 8, 8, 0 } } },

      { 2, { { 0, 0, 8, 4, 1 }, { 0, 4, 8, 4, 2 } } },
      { 2, { { 0, 0, 4, 8, 3 }, { 4, 0, 4, 8, 4 } } },

      { 3, { { 0, 0, 8, 4, 1 }, { 0, 4, 4, 4, 7 }, { 4, 4, 4, 4, 8 } } },
      { 3, { { 0, 4, 8, 4, 2 }, { 0, 0, 4, 4, 5 }, { 4, 0, 4, 4, 6 } } },

      { 3, { { 0, 0, 4, 8, 3 }, { 4, 0, 4, 4, 6 }, { 4, 4, 4, 4, 8 } } },
      { 3, { { 4, 0, 4, 8, 4 }, { 0, 0, 4, 4, 5 }, { 0, 4, 4, 4, 7 } } },

      { 4, { { 0, 0, 4, 4, 5 }, { 4, 0, 4, 4, 6 }, { 0, 4, 4, 4, 7 }, { 4, 4, 4, 4, 8 } } }
   };

   chunk_tile_desc g_chunk_tile_layouts[cNumChunkTileLayouts] = 
   {
      // 2x2
      { 0, 0, 8, 8, 0 },

      // 2x1
      { 0, 0, 8, 4, 1 }, 
      { 0, 4, 8, 4, 2 },

      // 1x2
      { 0, 0, 4, 8, 3 },
      { 4, 0, 4, 8, 4 },

      // 1x1
      { 0, 0, 4, 4, 5 }, 
      { 4, 0, 4, 4, 6 },
      { 0, 4, 4, 4, 7 }, 
      { 4, 4, 4, 4, 8 } 
   };
   
} // namespace crnlib



