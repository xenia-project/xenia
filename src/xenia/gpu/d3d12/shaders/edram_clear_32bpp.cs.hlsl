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
  [branch] if (any(sample_index < clear_rect.xy) ||
               any(sample_index >= clear_rect.zw)) {
    return;
  }
  uint2 tile_sample_index = xe_group_thread_id.xy;
  tile_sample_index.x *= 2u;
  uint edram_offset = XeEDRAMOffset32bpp(xe_group_id.xy, tile_sample_index);
  xe_edram_load_store_dest.Store(edram_offset, xe_edram_clear_color32);
  if (sample_index.x + 1u < clear_rect.z) {
    xe_edram_load_store_dest.Store(edram_offset + 4u, xe_edram_clear_color32);
  }
}
