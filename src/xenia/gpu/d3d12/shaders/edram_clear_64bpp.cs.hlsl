#define XE_EDRAM_WRITE_ONLY
#include "edram_load_store.hlsli"

// Load4/Store4 aren't needed here, but 80x16 threads is over the limit.
[numthreads(40, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
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
  uint edram_offset = XeEDRAMOffset64bpp(xe_group_id.xy, tile_sample_index);
  [branch] if (XeEDRAMScaleLog2() != 0u) {
    uint3 edram_offsets = uint3(16u, 32u, 48u) + edram_offset;
    [branch] if (x_in_rect.x) {
      xe_edram_load_store_dest.Store4(edram_offset,
                                      xe_edram_clear_color64.xyxy);
      xe_edram_load_store_dest.Store4(edram_offsets.x,
                                      xe_edram_clear_color64.xyxy);
    }
    [branch] if (x_in_rect.y) {
      xe_edram_load_store_dest.Store4(edram_offsets.y,
                                      xe_edram_clear_color64.xyxy);
      xe_edram_load_store_dest.Store4(edram_offsets.z,
                                      xe_edram_clear_color64.xyxy);
    }
  } else {
    [branch] if (x_in_rect.x) {
      xe_edram_load_store_dest.Store2(edram_offset, xe_edram_clear_color64);
    }
    [branch] if (x_in_rect.y) {
      xe_edram_load_store_dest.Store2(edram_offset + 8u,
                                      xe_edram_clear_color64);
    }
  }
}
