// File: crn_dxt_hc_common.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   struct chunk_tile_desc
   {
      // These values are in pixels, and always a multiple of cBlockPixelWidth/cBlockPixelHeight.
      uint m_x_ofs;
      uint m_y_ofs;
      uint m_width;
      uint m_height;
      uint m_layout_index;
   };

   struct chunk_encoding_desc
   {
      uint m_num_tiles;
      chunk_tile_desc m_tiles[4];
   };

   const uint cChunkPixelWidth = 8;
   const uint cChunkPixelHeight = 8;
   const uint cChunkBlockWidth = 2;
   const uint cChunkBlockHeight = 2;

   const uint cChunkMaxTiles = 4;

   const uint cBlockPixelWidthShift = 2;
   const uint cBlockPixelHeightShift = 2;

   const uint cBlockPixelWidth = 4;
   const uint cBlockPixelHeight = 4;

   const uint cNumChunkEncodings = 8;
   extern chunk_encoding_desc g_chunk_encodings[cNumChunkEncodings];

   const uint cNumChunkTileLayouts = 9;
   const uint cFirst4x4ChunkTileLayout = 5;
   extern chunk_tile_desc g_chunk_tile_layouts[cNumChunkTileLayouts];
 
} // namespace crnlib
