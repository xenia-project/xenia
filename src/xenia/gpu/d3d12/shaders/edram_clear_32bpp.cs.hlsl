#define XE_EDRAM_WRITE_ONLY
#include "edram_load_store.hlsli"

[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 guest samples (all the pixels of it, no matter whether the
  // resolution scale is 1x or 2x).
  uint4 clear_rect;
  clear_rect.xz = xe_edram_clear_rect & 0xFFFFu;
  clear_rect.yw = xe_edram_clear_rect >> 16u;
  uint2 sample_index = xe_thread_id.xy;
  sample_index.x *= 2u;
  bool4 x_in_rect =
      sample_index.x >= clear_rect.x && sample_index.x < clear_rect.z;
  [branch] if (sample_index.y < clear_rect.y ||
               sample_index.y >= clear_rect.w || !any(x_in_rect)) {
    return;
  }
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 2u;
  uint edram_offset = XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index);
  [branch] if (XeEDRAMScaleLog2() != 0u) {
    [branch] if (x_in_rect.x) {
      xe_edram_load_store_dest.Store4(edram_offset,
                                      xe_edram_clear_color32.xxxx);
    }
    [branch] if (x_in_rect.y) {
      xe_edram_load_store_dest.Store4(edram_offset + 16u,
                                      xe_edram_clear_color32.xxxx);
    }
  } else {
    [branch] if (x_in_rect.x) {
      xe_edram_load_store_dest.Store(edram_offset, xe_edram_clear_color32);
    }
    [branch] if (x_in_rect.y) {
      xe_edram_load_store_dest.Store(edram_offset + 4u, xe_edram_clear_color32);
    }
  }
}
