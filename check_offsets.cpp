#include <cstddef>
#include <cstdio>
#include <cstdint>

struct SystemConstants {
    // ... previous members up to vertex_index_max
    float flags;
    float tessellation_factor_range[2];
    uint32_t line_loop_closing_index;
    
    uint32_t vertex_index_endian;
    uint32_t vertex_index_offset;
    uint32_t vertex_index_min_max[2];
    
    float user_clip_planes[6][4];
    
    float ndc_scale[3];
    float point_vertex_diameter_min;
    
    float ndc_offset[3];
    float point_vertex_diameter_max;
};

int main() {
    printf("Assuming SystemConstants starts at offset 0:\n");
    printf("user_clip_planes offset: %zu bytes\n", offsetof(SystemConstants, user_clip_planes));
    printf("ndc_scale offset: %zu bytes\n", offsetof(SystemConstants, ndc_scale));
    printf("ndc_offset offset: %zu bytes\n", offsetof(SystemConstants, ndc_offset));
    
    printf("\nFloats from start:\n");
    printf("ndc_scale at float index: %zu\n", offsetof(SystemConstants, ndc_scale) / 4);
    printf("ndc_offset at float index: %zu\n", offsetof(SystemConstants, ndc_offset) / 4);
    
    return 0;
}
